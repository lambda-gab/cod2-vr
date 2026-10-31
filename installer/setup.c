// CoD2 VR setup (as the Call of Duty 1 VR mod's v1.8.0 setup: one window, drawn by hand): installs the mod into a
// Call of Duty 2 folder, and updates or uninstalls it.
//
// The mod files (d3d9.dll, openxr_loader.dll, cod2_vr.ini), the launcher and supporters.ini are embedded as resources.
// No game file is shipped: the "no arms" pack is made by the launcher ("Call of Duty 2 VR.exe" /handspack hide) from the
// installed game's own iw_*.iwd, the same way as tools\build_iwd.py, and the supporters' panel by /exportpanel.
#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "setup_res.h"

#define COD2SP_TIMESTAMP  0x442ca3f7              // CoD2SP_s.exe 1.3
#define GAME_EXE          L"CoD2SP_s.exe"
#define BACKUP_SUFFIX     L".cod2_vr_backup"
#define HANDS_PACK        L"main\\zz_cod2vr.iwd"
#define FONT_PACK         L"main\\zz_cod2vr_font.iwd"   // made by the launcher (GRAPHICS > Text font: Modern)
#define LAUNCHER_EXE      L"Call of Duty 2 VR.exe"
#define OLD_LAUNCHER_EXE  L"CoD2-VR-Launcher.exe"   // its name before v1.0: removed by an update
#define SHORTCUT_NAME     L"Call of Duty 2 VR.lnk"

static const wchar_t *TITLE = L"Call of Duty 2 VR";
static int pt;   // Portuguese UI

#define T(en, ptbr) (pt ? (ptbr) : (en))

static int Ask(UINT type, const wchar_t *fmt, ...)
{
    wchar_t msg[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(msg, 2047, fmt, ap);
    va_end(ap);
    msg[2047] = 0;
    return MessageBoxW(NULL, msg, TITLE, type | MB_SETFOREGROUND);
}

static int FileExists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void Join(wchar_t *out, const wchar_t *dir, const wchar_t *name)
{
    _snwprintf(out, MAX_PATH, L"%s\\%s", dir, name);
    out[MAX_PATH - 1] = 0;
}

static int HasGame(const wchar_t *dir)
{
    wchar_t p[MAX_PATH];
    Join(p, dir, GAME_EXE);
    return FileExists(p);
}

// PE header TimeDateStamp of the game exe, 0 if unreadable
static DWORD ExeTimestamp(const wchar_t *dir)
{
    wchar_t p[MAX_PATH];
    BYTE buf[1024];
    DWORD n = 0, pe;
    HANDLE f;
    Join(p, dir, GAME_EXE);
    f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    ReadFile(f, buf, sizeof(buf), &n, NULL);
    CloseHandle(f);
    if (n < 0x40) return 0;
    pe = *(DWORD *)(buf + 0x3c);
    if (pe + 12 > n || memcmp(buf + pe, "PE\0\0", 4)) return 0;
    return *(DWORD *)(buf + pe + 8);
}

static int RegPath(HKEY root, const wchar_t *key, const wchar_t *value, wchar_t *out)
{
    DWORD size = MAX_PATH * sizeof(wchar_t), type;
    HKEY k;
    int ok = 0;
    if (RegOpenKeyExW(root, key, 0, KEY_READ | KEY_WOW64_32KEY, &k) != ERROR_SUCCESS) return 0;
    if (RegQueryValueExW(k, value, NULL, &type, (BYTE *)out, &size) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        size_t len = wcslen(out);
        while (len && (out[len - 1] == L'\\' || out[len - 1] == L'/')) out[--len] = 0;
        ok = len > 0;
    }
    RegCloseKey(k);
    return ok;
}

// Candidates: the folder this setup runs from, the Activision install key, Steam (app 2630).
static int FindGame(wchar_t *out)
{
    wchar_t p[MAX_PATH], *slash;
    GetModuleFileNameW(NULL, p, MAX_PATH);
    if ((slash = wcsrchr(p, L'\\')) != NULL) *slash = 0;
    if (HasGame(p)) { wcscpy(out, p); return 1; }
    if (RegPath(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Activision\\Call of Duty 2", L"InstallPath", p) && HasGame(p)) { wcscpy(out, p); return 1; }
    if (RegPath(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 2630", L"InstallLocation", p) && HasGame(p)) {
        wcscpy(out, p);
        return 1;
    }
    if (RegPath(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", p)) {
        wcscat(p, L"\\steamapps\\common\\Call of Duty 2");
        if (HasGame(p)) { wcscpy(out, p); return 1; }
    }
    return 0;
}

static int PickFolder(wchar_t *out)
{
    BROWSEINFOW bi = { 0 };
    PIDLIST_ABSOLUTE id;
    bi.lpszTitle = T(L"Select the Call of Duty 2 folder (the one with CoD2SP_s.exe)",
                     L"Selecione a pasta do Call of Duty 2 (a que tem o CoD2SP_s.exe)");
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    id = SHBrowseForFolderW(&bi);
    if (!id) return 0;
    SHGetPathFromIDListW(id, out);
    CoTaskMemFree(id);
    return 1;
}

// ---------------------------------------------------------------------------
// Embedded files
// ---------------------------------------------------------------------------
static const void *Resource(int id, DWORD *size)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    HGLOBAL g;
    if (!r) return NULL;
    g = LoadResource(NULL, r);
    *size = SizeofResource(NULL, r);
    return g ? LockResource(g) : NULL;
}

static int WriteResource(int id, const wchar_t *path)
{
    DWORD size, written = 0;
    const void *data = Resource(id, &size);
    HANDLE f;
    if (!data) return 0;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    WriteFile(f, data, size, &written, NULL);
    CloseHandle(f);
    return written == size;
}

// Our d3d9.dll names its log file cod2_vr.log.
static int IsOurDll(const wchar_t *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, n = 0, i;
    char *buf;
    int found = 0;
    if (f == INVALID_HANDLE_VALUE) return 0;
    size = GetFileSize(f, NULL);
    if (size > 0 && size < 16 * 1024 * 1024 && (buf = malloc(size)) != NULL) {
        ReadFile(f, buf, size, &n, NULL);
        for (i = 0; i + 11 <= n && !found; i++) found = !memcmp(buf + i, "cod2_vr.log", 11);
        free(buf);
    }
    CloseHandle(f);
    return found;
}

// Runs the installed launcher with these arguments (no window) and waits; 1 when it exited with 0.
static int RunLauncher(const wchar_t *game, const wchar_t *args)
{
    wchar_t exe[MAX_PATH], cmdline[MAX_PATH + 64];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    Join(exe, game, LAUNCHER_EXE);
    _snwprintf(cmdline, MAX_PATH + 64, L"\"%s\" %s", exe, args);
    cmdline[MAX_PATH + 63] = 0;
    if (!CreateProcessW(exe, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, game, &si, &pi)) return 0;
    WaitForSingleObject(pi.hProcess, 60000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

// ---------------------------------------------------------------------------
// Install / uninstall
// ---------------------------------------------------------------------------
static int ShortcutPath(wchar_t *out)
{
    wchar_t desk[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desk))) return 0;
    Join(out, desk, SHORTCUT_NAME);
    return 1;
}

static int CreateShortcut(const wchar_t *game)
{
    wchar_t lnk[MAX_PATH], exe[MAX_PATH];
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    int ok = 0;
    if (!ShortcutPath(lnk)) return 0;
    Join(exe, game, LAUNCHER_EXE);
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) return 0;
    sl->lpVtbl->SetPath(sl, exe);
    sl->lpVtbl->SetWorkingDirectory(sl, game);
    sl->lpVtbl->SetIconLocation(sl, exe, 0);
    sl->lpVtbl->SetDescription(sl, L"Call of Duty 2 VR");
    if (SUCCEEDED(sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) {
        ok = SUCCEEDED(pf->lpVtbl->Save(pf, lnk, TRUE));
        pf->lpVtbl->Release(pf);
    }
    sl->lpVtbl->Release(sl);
    return ok;
}

static int Install(const wchar_t *game, int *keptIni)
{
    wchar_t dll[MAX_PATH], bak[MAX_PATH], p[MAX_PATH];
    Join(dll, game, L"d3d9.dll");
    if (FileExists(dll) && !IsOurDll(dll)) {
        // another Direct3D 9 wrapper is installed: keep it as a backup
        _snwprintf(bak, MAX_PATH, L"%s%s", dll, BACKUP_SUFFIX);
        MoveFileExW(dll, bak, MOVEFILE_REPLACE_EXISTING);
    }
    if (!WriteResource(IDR_D3D9, dll)) return 0;
    Join(p, game, L"openxr_loader.dll");
    if (!WriteResource(IDR_OPENXR_LOADER, p)) return 0;
    Join(p, game, LAUNCHER_EXE);
    if (!WriteResource(IDR_LAUNCHER, p)) return 0;
    Join(p, game, OLD_LAUNCHER_EXE);   // the launcher under its old name
    DeleteFileW(p);
    Join(p, game, L"supporters.ini");   // the Patreon supporters shown beside the main menu: always the release's list
    WriteResource(IDR_SUPPORTERS, p);
    Join(p, game, L"cod2_vr.ini");
    *keptIni = FileExists(p);
    if (*keptIni) Join(p, game, L"cod2_vr.ini.default");   // keep the player's settings; the new defaults go next to it
    return WriteResource(IDR_COD2_VR_INI, p);
}

static void Uninstall(const wchar_t *game)
{
    wchar_t p[MAX_PATH], bak[MAX_PATH];
    Join(p, game, L"d3d9.dll");
    if (IsOurDll(p)) DeleteFileW(p);
    _snwprintf(bak, MAX_PATH, L"%s%s", p, BACKUP_SUFFIX);
    if (FileExists(bak) && !FileExists(p)) MoveFileW(bak, p);
    Join(p, game, L"openxr_loader.dll");   DeleteFileW(p);
    Join(p, game, L"cod2_vr.ini");         DeleteFileW(p);
    Join(p, game, L"cod2_vr.ini.default"); DeleteFileW(p);
    Join(p, game, L"supporters.ini");      DeleteFileW(p);
    Join(p, game, HANDS_PACK);             DeleteFileW(p);
    Join(p, game, FONT_PACK);              DeleteFileW(p);
    Join(p, game, LAUNCHER_EXE);           DeleteFileW(p);
    Join(p, game, OLD_LAUNCHER_EXE);       DeleteFileW(p);
    if (ShortcutPath(p)) DeleteFileW(p);
}

// Command line (no dialogs): /silent /dir="<game folder>" [/hands] [/shortcut] [/uninstall]
// Exit code 0 = done, 1 = failed, 2 = no game in that folder.
static int Silent(const wchar_t *cmd)
{
    wchar_t game[MAX_PATH] = L"", pack[MAX_PATH];
    const wchar_t *d = wcsstr(cmd, L"/dir=");
    int keptIni;
    if (d) {
        const wchar_t *s = d + 5, *e;
        if (*s == L'"') { s++; e = wcschr(s, L'"'); } else e = wcschr(s, L' ');
        if (!e) e = s + wcslen(s);
        if (e - s >= MAX_PATH) return 1;
        wcsncpy(game, s, e - s);
        game[e - s] = 0;
    } else if (!FindGame(game)) {
        return 2;
    }
    if (!HasGame(game)) return 2;
    if (wcsstr(cmd, L"/uninstall")) { Uninstall(game); return 0; }
    if (!Install(game, &keptIni)) return 1;
    RunLauncher(game, L"/exportpanel");
    if (wcsstr(cmd, L"/shortcut")) CreateShortcut(game);
    if (wcsstr(cmd, L"/hands")) return RunLauncher(game, L"/handspack hide") ? 0 : 1;
    Join(pack, game, HANDS_PACK);
    DeleteFileW(pack);
    return 0;
}

// ---------------------------------------------------------------------------
// The setup window (as the Call of Duty 1 VR mod's v1.8.0 setup): one dark window in the launcher's colours (a welcome
// page, the progress, the end), drawn by hand.
// ---------------------------------------------------------------------------
#define SETUP_VERSION L"v1.0.3"   // the launcher's VERSION_TEXT
#define BASE_W 640
#define BASE_H 420

#define C_BG    RGB(14, 14, 14)
#define C_PANEL RGB(28, 28, 28)
#define C_GOLD  RGB(214, 170, 62)
#define C_GOLDD RGB(150, 112, 30)
#define C_TEXT  RGB(225, 225, 225)
#define C_DIM   RGB(140, 140, 140)
#define C_STEEL RGB(170, 205, 220)
#define C_OK    RGB(120, 200, 120)
#define C_WARN  RGB(232, 150, 64)
#define C_BAD   RGB(235, 90, 80)

enum { PG_WELCOME, PG_WORKING, PG_DONE, PG_REMOVED, PG_FAILED };
enum { B_PRIMARY = 1, B_SECOND, B_CHANGE, B_SHORTCUT, B_CLOSE };

typedef struct { RECT r; int id; } button_t;

static HWND    g_wnd;
static float   g_sc = 1.0f;
static HFONT   g_fTitle, g_fHead, g_fText, g_fSmall, g_fBold, g_fBtn;
static HICON   g_icon;
static button_t g_btn[8];
static int     g_nBtn, g_page, g_shortcut = 1, g_installed, g_unsupported, g_haveGame, g_keptIni;
static wchar_t g_game[MAX_PATH], g_status[200], g_error[300];
static int     g_step, g_steps = 3;
static POINT   g_mouse = { -1, -1 };

#define S(v) ((int)((v) * g_sc + 0.5f))

static void Refresh(void)
{
    wchar_t dll[MAX_PATH];
    g_haveGame = g_installed = g_unsupported = 0;
    if (!g_game[0]) return;
    g_haveGame = HasGame(g_game);
    Join(dll, g_game, L"d3d9.dll");
    g_installed = g_haveGame && FileExists(dll) && IsOurDll(dll);
    g_unsupported = g_haveGame && ExeTimestamp(g_game) != COD2SP_TIMESTAMP;   // the version the mod knows: CoD2SP_s.exe 1.3
}

static void FillR(HDC dc, int x0, int y0, int x1, int y1, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    RECT r = { x0, y0, x1, y1 };
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void Txt(HDC dc, HFONT f, COLORREF c, int x0, int y0, int x1, int y1, const wchar_t *s, UINT flags)
{
    RECT r = { x0, y0, x1, y1 };
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, flags | DT_NOPREFIX);
}

static void RoundBox(HDC dc, int x0, int y0, int x1, int y1, int rad, COLORREF fill, COLORREF edge)
{
    HPEN pen = CreatePen(PS_SOLID, S(1) > 1 ? S(1) : 1, edge);
    HBRUSH br = CreateSolidBrush(fill);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, br);
    RoundRect(dc, x0, y0, x1, y1, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static int Hot(const RECT *r) { return PtInRect(r, g_mouse); }

// primary: gold, filled; otherwise an outline. disabled buttons are drawn dim and get no area.
static void Button(HDC dc, int id, int x0, int y0, int x1, int y1, const wchar_t *label, int primary, int enabled)
{
    RECT r = { x0, y0, x1, y1 };
    int hot = enabled && Hot(&r);
    if (primary) {
        COLORREF fill = !enabled ? RGB(60, 52, 30) : hot ? RGB(236, 192, 80) : C_GOLD;
        RoundBox(dc, x0, y0, x1, y1, S(12), fill, fill);
        Txt(dc, g_fBtn, enabled ? RGB(20, 16, 6) : C_DIM, x0, y0, x1, y1, label, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        RoundBox(dc, x0, y0, x1, y1, S(12), hot ? RGB(40, 34, 20) : C_PANEL, hot ? C_GOLD : RGB(80, 80, 80));
        Txt(dc, g_fBtn, !enabled ? C_DIM : hot ? C_GOLD : C_TEXT, x0, y0, x1, y1, label, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }
    if (enabled && g_nBtn < 8) { g_btn[g_nBtn].r = r; g_btn[g_nBtn].id = id; g_nBtn++; }
}

static void Header(HDC dc)
{
    wchar_t sub[160];
    if (g_icon) DrawIconEx(dc, S(32), S(20), g_icon, S(72), S(72), 0, NULL, DI_NORMAL);
    Txt(dc, g_fTitle, C_GOLD, S(122), S(22), S(620), S(64), L"CALL OF DUTY 2 VR", DT_SINGLELINE | DT_VCENTER);
    _snwprintf(sub, 160, T(L"Setup  %s  \x00b7  Call of Duty 2 in virtual reality", L"Instalador  %s  \x00b7  Call of Duty 2 em realidade virtual"), SETUP_VERSION);
    sub[159] = 0;
    Txt(dc, g_fSmall, C_DIM, S(122), S(66), S(620), S(90), sub, DT_SINGLELINE | DT_VCENTER);
    FillR(dc, 0, S(110), S(BASE_W), S(112), C_GOLDD);
}

static void Footer(HDC dc) { FillR(dc, 0, S(340), S(BASE_W), S(342), RGB(40, 40, 40)); }

static void DrawWelcome(HDC dc)
{
    int ok = g_game[0] && g_haveGame;
    wchar_t line[300];
    Txt(dc, g_fBold, C_GOLD, S(32), S(126), S(400), S(148), T(L"GAME FOLDER", L"PASTA DO JOGO"), DT_SINGLELINE | DT_VCENTER);
    RoundBox(dc, S(32), S(152), S(470), S(190), S(10), C_PANEL, RGB(70, 70, 70));
    Txt(dc, g_fText, ok ? C_TEXT : C_WARN, S(46), S(152), S(458), S(190),
        g_game[0] ? g_game : T(L"Not found: click Change and pick the folder with CoD2SP_s.exe",
                               L"Não encontrada: clique em Alterar e escolha a pasta com o CoD2SP_s.exe"),
        DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS);
    Button(dc, B_CHANGE, S(482), S(152), S(608), S(190), T(L"Change...", L"Alterar..."), 0, 1);

    Txt(dc, g_fText, g_haveGame ? C_OK : C_DIM, S(32), S(202), S(608), S(224),
        g_haveGame ? L"\x2714  Call of Duty 2 (CoD2SP_s.exe)" : T(L"\x2013  Call of Duty 2 not here", L"\x2013  Call of Duty 2 não está aqui"),
        DT_SINGLELINE | DT_VCENTER);

    if (g_error[0]) {
        Txt(dc, g_fText, C_BAD, S(32), S(238), S(608), S(290), g_error, DT_WORDBREAK);
    } else if (ok && g_unsupported) {
        Txt(dc, g_fText, C_WARN, S(32), S(238), S(608), S(290),
            T(L"The game here is not the version the mod supports (CoD2SP_s.exe 1.3). The mod stays inactive in other versions.",
              L"O jogo desta pasta não é a versão que o mod suporta (CoD2SP_s.exe 1.3). Nas outras versões o mod fica inativo."),
            DT_WORDBREAK);
    } else if (ok && g_installed) {
        Txt(dc, g_fText, C_STEEL, S(32), S(238), S(608), S(290),
            T(L"The VR mod is already installed in this folder. Installing again updates it; your settings are kept.",
              L"O mod de VR já está instalado nesta pasta. Instalar de novo atualiza; suas configurações são mantidas."),
            DT_WORDBREAK);
    } else if (ok) {
        Txt(dc, g_fText, C_DIM, S(32), S(238), S(608), S(290),
            T(L"Installs the VR mod and the launcher. The optional packs (hidden arms, the modern font) are made later by the launcher from your own game files. No game file is changed.",
              L"Instala o mod de VR e o launcher. Os pacotes opcionais (braços escondidos, a fonte moderna) são gerados depois pelo launcher a partir dos seus arquivos do jogo. Nenhum arquivo do jogo é alterado."),
            DT_WORDBREAK);
    }

    {   // the shortcut checkbox
        RECT cb = { S(32), S(298), S(420), S(324) };
        int hot = Hot(&cb);
        RoundBox(dc, S(32), S(301), S(52), S(321), S(6), g_shortcut ? C_GOLD : C_PANEL, hot ? C_GOLD : RGB(110, 110, 110));
        if (g_shortcut) Txt(dc, g_fBold, RGB(20, 16, 6), S(32), S(301), S(52), S(321), L"\x2714", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        Txt(dc, g_fText, hot ? C_GOLD : C_TEXT, S(64), S(298), S(420), S(324), T(L"Create a desktop shortcut to the launcher", L"Criar um atalho do launcher na área de trabalho"),
            DT_SINGLELINE | DT_VCENTER);
        if (g_nBtn < 8) { g_btn[g_nBtn].r = cb; g_btn[g_nBtn].id = B_SHORTCUT; g_nBtn++; }
    }

    Footer(dc);
    Button(dc, B_CLOSE, S(32), S(356), S(150), S(398), T(L"Cancel", L"Cancelar"), 0, 1);
    if (g_installed && ok) Button(dc, B_SECOND, S(290), S(356), S(430), S(398), T(L"Uninstall", L"Desinstalar"), 0, 1);
    _snwprintf(line, 300, L"%s", g_installed ? T(L"UPDATE", L"ATUALIZAR") : g_unsupported ? T(L"INSTALL ANYWAY", L"INSTALAR MESMO ASSIM") : T(L"INSTALL", L"INSTALAR"));
    line[299] = 0;
    Button(dc, B_PRIMARY, S(442), S(356), S(608), S(398), line, 1, ok);
}

static void DrawWorking(HDC dc)
{
    int w = S(608) - S(32), done = (int)((double)w * g_step / g_steps);
    Txt(dc, g_fHead, C_GOLD, S(32), S(140), S(608), S(176), T(L"Installing...", L"Instalando..."), DT_SINGLELINE | DT_VCENTER);
    Txt(dc, g_fText, C_TEXT, S(32), S(190), S(608), S(220), g_status, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    RoundBox(dc, S(32), S(236), S(608), S(254), S(10), C_PANEL, RGB(70, 70, 70));
    if (done > S(10)) RoundBox(dc, S(32), S(236), S(32) + done, S(254), S(10), C_GOLD, C_GOLD);
    Txt(dc, g_fSmall, C_DIM, S(32), S(270), S(608), S(296), T(L"Please wait: this takes a few seconds.", L"Aguarde: isto leva alguns segundos."),
        DT_SINGLELINE | DT_VCENTER);
}

static void DrawDone(HDC dc)
{
    wchar_t msg[900];
    Txt(dc, g_fHead, C_GOLD, S(32), S(130), S(608), S(170), T(L"ALL SET!", L"TUDO PRONTO!"), DT_SINGLELINE | DT_VCENTER);
    _snwprintf(msg, 900,
               T(L"Connect your headset (Virtual Desktop or Quest Link: the game needs a 32-bit OpenXR runtime) and start the game with the Call of Duty 2 VR launcher, "
                 L"which also has the settings.%s\n\nRun this setup again to update or uninstall.",
                 L"Conecte o headset (Virtual Desktop ou Quest Link: o jogo precisa de um runtime OpenXR de 32 bits) e abra o jogo pelo launcher do Call of Duty 2 VR, "
                 L"que também tem as configurações.%s\n\nExecute este instalador de novo para atualizar ou desinstalar."),
               g_keptIni ? T(L"\n\nYour existing cod2_vr.ini was kept (the new defaults are in cod2_vr.ini.default).",
                             L"\n\nSeu cod2_vr.ini foi mantido (os novos padrões estão em cod2_vr.ini.default).") : L"");
    msg[899] = 0;
    Txt(dc, g_fText, C_TEXT, S(32), S(184), S(608), S(330), msg, DT_WORDBREAK);
    Footer(dc);
    Button(dc, B_CLOSE, S(32), S(356), S(150), S(398), T(L"Close", L"Fechar"), 0, 1);
    Button(dc, B_PRIMARY, S(390), S(356), S(608), S(398), T(L"OPEN THE LAUNCHER", L"ABRIR O LAUNCHER"), 1, 1);
}

static void DrawEnd(HDC dc, const wchar_t *title, const wchar_t *text, COLORREF col, const wchar_t *primary)
{
    Txt(dc, g_fHead, col, S(32), S(130), S(608), S(170), title, DT_SINGLELINE | DT_VCENTER);
    Txt(dc, g_fText, C_TEXT, S(32), S(184), S(608), S(330), text, DT_WORDBREAK);
    Footer(dc);
    Button(dc, B_CLOSE, S(32), S(356), S(150), S(398), T(L"Close", L"Fechar"), 0, 1);
    if (primary) Button(dc, B_PRIMARY, S(442), S(356), S(608), S(398), primary, 1, 1);
}

static void Paint(HWND wnd)
{
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(wnd, &ps), dc;
    RECT rc;
    HBITMAP bmp;
    HGDIOBJ old;
    GetClientRect(wnd, &rc);
    dc = CreateCompatibleDC(wdc);
    bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
    old = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);
    FillR(dc, 0, 0, rc.right, rc.bottom, C_BG);
    g_nBtn = 0;
    Header(dc);
    switch (g_page) {
    case PG_WELCOME: DrawWelcome(dc); break;
    case PG_WORKING: DrawWorking(dc); break;
    case PG_DONE:    DrawDone(dc); break;
    case PG_REMOVED:
        DrawEnd(dc, T(L"REMOVED", L"REMOVIDO"), T(L"The VR mod was removed from the game folder. Your game files were not touched.",
                                                  L"O mod de VR foi removido da pasta do jogo. Os arquivos do jogo não foram alterados."), C_GOLD, NULL);
        break;
    case PG_FAILED:
        DrawEnd(dc, T(L"COULD NOT INSTALL", L"NÃO FOI POSSÍVEL INSTALAR"), g_error, C_BAD, T(L"BACK", L"VOLTAR"));
        break;
    }
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(wnd, &ps);
}

static void Pump(void)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
}

// one line of progress; the window is drawn at once (the work is done on this thread)
static void Step(const wchar_t *text)
{
    wcsncpy(g_status, text, 199);
    g_status[199] = 0;
    InvalidateRect(g_wnd, NULL, FALSE);
    UpdateWindow(g_wnd);
    Pump();
    g_step++;
}

static void DoInstall(void)
{
    g_error[0] = 0;
    g_step = 0;
    g_page = PG_WORKING;
    Step(T(L"Copying the mod and the launcher...", L"Copiando o mod e o launcher..."));
    if (!Install(g_game, &g_keptIni)) {
        _snwprintf(g_error, 300, T(L"Could not write the files to:\n\n%s\n\nIs the game running?", L"Não foi possível gravar os arquivos em:\n\n%s\n\nO jogo está aberto?"), g_game);
        g_error[299] = 0;
        g_page = PG_FAILED;
        InvalidateRect(g_wnd, NULL, FALSE);
        return;
    }
    Step(T(L"Preparing the supporters' panel...", L"Preparando o painel de apoiadores..."));
    RunLauncher(g_game, L"/exportpanel");   // there even if the game is started without the launcher
    if (g_shortcut) {
        Step(T(L"Creating the desktop shortcut...", L"Criando o atalho..."));
        CreateShortcut(g_game);
    } else {
        g_step++;
    }
    g_step = g_steps;
    g_page = PG_DONE;
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void DoUninstall(void)
{
    if (MessageBoxW(g_wnd, T(L"Remove the VR mod from this game folder?", L"Remover o mod de VR desta pasta do jogo?"), TITLE,
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    Uninstall(g_game);
    g_page = PG_REMOVED;
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void Click(int id)
{
    wchar_t exe[MAX_PATH], picked[MAX_PATH];
    switch (id) {
    case B_CLOSE:
        DestroyWindow(g_wnd);
        break;
    case B_CHANGE:
        picked[0] = 0;
        if (PickFolder(picked)) {
            if (HasGame(picked)) {
                wcscpy(g_game, picked);
                g_error[0] = 0;
                Refresh();
            } else {
                _snwprintf(g_error, 300, T(L"CoD2SP_s.exe was not found in:\n%s", L"O CoD2SP_s.exe não foi encontrado em:\n%s"), picked);
                g_error[299] = 0;
            }
        }
        break;
    case B_SHORTCUT:
        g_shortcut = !g_shortcut;
        break;
    case B_SECOND:
        if (g_page == PG_WELCOME && g_installed) DoUninstall();
        break;
    case B_PRIMARY:
        if (g_page == PG_WELCOME) {
            if (g_game[0] && g_haveGame) DoInstall();
        } else if (g_page == PG_FAILED) {
            g_page = PG_WELCOME;
            g_error[0] = 0;
        } else if (g_page == PG_DONE) {
            Join(exe, g_game, LAUNCHER_EXE);
            ShellExecuteW(NULL, L"open", exe, NULL, g_game, SW_SHOWNORMAL);
            DestroyWindow(g_wnd);
        }
        break;
    }
    InvalidateRect(g_wnd, NULL, FALSE);
}

static LRESULT CALLBACK SetupProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;
    switch (msg) {
    case WM_PAINT:
        Paint(wnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
        TrackMouseEvent(&tme);
        g_mouse.x = (short)LOWORD(lp);
        g_mouse.y = (short)HIWORD(lp);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_mouse.x = g_mouse.y = -1;
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            int hand = 0;
            for (i = 0; i < g_nBtn; i++) if (PtInRect(&g_btn[i].r, g_mouse)) hand = 1;
            SetCursor(LoadCursor(NULL, hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        if (g_page == PG_WORKING) return 0;
        for (i = 0; i < g_nBtn; i++)
            if (PtInRect(&g_btn[i].r, p)) { Click(g_btn[i].id); break; }
        return 0;
    }
    case WM_KEYDOWN:
        if (g_page == PG_WORKING) return 0;
        if (wp == VK_ESCAPE) DestroyWindow(wnd);
        else if (wp == VK_RETURN) Click(B_PRIMARY);
        return 0;
    case WM_CLOSE:
        if (g_page == PG_WORKING) return 0;
        DestroyWindow(wnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static HFONT MakeFont(int px, int weight)
{
    return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    WNDCLASSW wc = { 0 };
    RECT rc;
    MSG m;
    HDC screen;
    int w, h, style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    (void)prev;

    pt = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE;
    CoInitialize(NULL);
    if (cmd && wcsstr(cmd, L"/silent")) return Silent(cmd);

    screen = GetDC(NULL);
    g_sc = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, screen);
    g_fTitle = MakeFont(30, FW_BOLD);
    g_fHead = MakeFont(26, FW_BOLD);
    g_fText = MakeFont(15, FW_NORMAL);
    g_fSmall = MakeFont(13, FW_NORMAL);
    g_fBold = MakeFont(13, FW_BOLD);
    g_fBtn = MakeFont(15, FW_SEMIBOLD);
    g_icon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_SETUP), IMAGE_ICON, S(72), S(72), LR_DEFAULTCOLOR);

    FindGame(g_game);   // the folder this runs from, the Activision key, Steam; empty when none
    Refresh();

    wc.lpfnWndProc = SetupProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_SETUP), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.lpszClassName = L"CoD2VRSetup";
    RegisterClassW(&wc);
    SetRect(&rc, 0, 0, S(BASE_W), S(BASE_H));
    AdjustWindowRect(&rc, style, FALSE);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    g_wnd = CreateWindowExW(0, wc.lpszClassName, TITLE, style, (GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h,
                            NULL, NULL, inst, NULL);
    if (!g_wnd) return 1;
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);
    while (GetMessageW(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
