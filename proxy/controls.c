// controls: the controllers in the game and in its menus.
//  * Buttons run game commands through Cbuf_AddText ("+cmd" is held while pressed, "a|b" runs a
//    on press and b on release, "key:NAME" sends a key to the game window).
//  * The left stick walks (added to the usercmd in CL_FinishMove's place), the right one turns
//    (snap or smooth, into cl.viewangles) and changes stance (up / down).
//  * In menus (cls.keyCatchers & UI) the right hand points at the menu screen: the UI cursor is
//    moved there (CL_MouseEvent with the difference), the trigger clicks, B goes back, A confirms.
#define _CRT_SECURE_NO_WARNINGS
#include "cod2_vr.h"
#include "controls.h"
#include "cod2_addresses.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define MEM(type, addr) (*(type *)(addr))
#define DEG(x)  ((x) * (180.0f / 3.14159265f))

static struct {
    int   rightHanded;   // the pointer and the trigger that fires are the right hand's
    float snapTurn;      // degrees per flick, 0 = smooth
    float turnSpeed;     // smooth turn, degrees per second
    float deadzone;
    char  bind[BT_COUNT][64];
} ccfg = { 1, 45.0f, 150.0f, 0.15f };

static const struct { const char *key, *def; } g_buttons[BT_COUNT] = {
    { "PrimaryTrigger", "+attack" },
    { "PrimaryGrip", "+activate" },
    { "SecondaryTrigger", "toggleads|leaveads" },
    { "SecondaryGrip", "+melee" },
    { "A", "+gostand" },
    { "B", "+reload" },
    { "X", "+frag" },
    { "Y", "weapnext" },
    { "LeftStickClick", "+scores" },
    { "RightStickClick", "toggleprone" },
    { "Menu", "key:ESCAPE" },
    { "TurnStickUp", "raisestance" },
    { "TurnStickDown", "lowerstance" },
};

static vr_input_t g_in;
static int    g_btnDown[BT_COUNT];
static int    g_menuBtn[8];          // menu: previous state of trigger, A, B, menu, up, down
static int    g_snapReady = 1, g_stanceReady = 1;
static float  g_moveF, g_moveR;      // -1..1, this frame's stick
static int    g_inMenu;
static HWND   g_wnd;

void Controls_LoadConfig(int (*getInt)(const char *, const char *, int), float (*getFloat)(const char *, const char *, float),
                         void (*getStr)(const char *, const char *, const char *, char *, int))
{
    int i;
    ccfg.rightHanded = getInt("Controllers", "RightHanded", ccfg.rightHanded);
    ccfg.snapTurn = getFloat("Controllers", "SnapTurn", ccfg.snapTurn);
    ccfg.turnSpeed = getFloat("Controllers", "SmoothTurnSpeed", ccfg.turnSpeed);
    ccfg.deadzone = getFloat("Controllers", "Deadzone", ccfg.deadzone);
    for (i = 0; i < BT_COUNT; i++) getStr("Buttons", g_buttons[i].key, g_buttons[i].def, ccfg.bind[i], sizeof(ccfg.bind[i]));
}

void Controls_SetWindow(HWND w) { g_wnd = w; }

// Cbuf_AddText takes the text in EAX.
static void Cbuf(const char *text)
{
    DWORD fn = ADDR_Cbuf_AddText;
    __asm {
        mov eax, text
        call fn
    }
}

static const struct { const char *name; int vk; } g_keys[] = {
    { "ESCAPE", VK_ESCAPE }, { "ENTER", VK_RETURN }, { "TAB", VK_TAB }, { "SPACE", VK_SPACE },
    { "UPARROW", VK_UP }, { "DOWNARROW", VK_DOWN }, { "LEFTARROW", VK_LEFT }, { "RIGHTARROW", VK_RIGHT },
    { "BACKSPACE", VK_BACK },
};

// The game reads the keyboard from its window's messages (scan code in lParam).
static void SendKey(int vk, int down)
{
    UINT sc = MapVirtualKeyA(vk, 0);
    LPARAM lp = 1 | (sc << 16) | (down ? 0 : (3u << 30));
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT) lp |= 1 << 24;   // extended keys
    if (g_wnd) PostMessageA(g_wnd, down ? WM_KEYDOWN : WM_KEYUP, vk, lp);
}

static void SendClick(int down)
{
    if (g_wnd) PostMessageA(g_wnd, down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? MK_LBUTTON : 0, 0);
}

static void RunBind(int b, int down)
{
    char cmd[80];
    const char *bind = ccfg.bind[b], *bar;
    if (!bind[0]) return;
    if (!_strnicmp(bind, "key:", 4)) {
        int i;
        for (i = 0; i < (int)(sizeof(g_keys) / sizeof(g_keys[0])); i++)
            if (!_stricmp(bind + 4, g_keys[i].name)) SendKey(g_keys[i].vk, down);
        return;
    }
    if ((bar = strchr(bind, '|')) != NULL) {          // "on press|on release"
        if (down) _snprintf(cmd, sizeof(cmd), "%.*s\n", (int)(bar - bind), bind);
        else _snprintf(cmd, sizeof(cmd), "%s\n", bar + 1);
    } else if (bind[0] == '+') {
        _snprintf(cmd, sizeof(cmd), "%c%s\n", down ? '+' : '-', bind + 1);
    } else if (down) {
        _snprintf(cmd, sizeof(cmd), "%s\n", bind);
    } else return;
    cmd[sizeof(cmd) - 1] = 0;
    if (cmd[0] && cmd[0] != '\n') Cbuf(cmd);
}

static void SetButton(int b, int down)
{
    if (g_btnDown[b] == down) return;
    g_btnDown[b] = down;
    RunBind(b, down);
}

static int Pressed(float v, int b) { return g_btnDown[b] ? v > 0.35f : v > 0.65f; }

static void ReleaseAll(void)
{
    int b;
    for (b = 0; b < BT_COUNT; b++) SetButton(b, 0);
}

static float Dead(float v) { return fabsf(v) < ccfg.deadzone ? 0 : (v - (v > 0 ? ccfg.deadzone : -ccfg.deadzone)) / (1 - ccfg.deadzone); }

static void Cross(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
static float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Where the hand's ray meets the screen (pose: axis[0] away from the viewer, axis[1] left,
// axis[2] up; width x height metres): 0..1 across and down, 0 = misses.
static int PointAt(const vr_pose_t *hand, const vr_pose_t *scr, float w, float h, float *u, float *v)
{
    float d = Dot(hand->axis[0], scr->axis[0]), rel[3], t, hit[3];
    int i;
    if (d <= 0.01f) return 0;
    for (i = 0; i < 3; i++) rel[i] = scr->pos[i] - hand->pos[i];
    t = Dot(rel, scr->axis[0]) / d;
    if (t <= 0) return 0;
    for (i = 0; i < 3; i++) hit[i] = hand->pos[i] + hand->axis[0][i] * t - scr->pos[i];
    *u = 0.5f - Dot(hit, scr->axis[1]) / w;
    *v = 0.5f - Dot(hit, scr->axis[2]) / h;
    return *u > -0.1f && *u < 1.1f && *v > -0.1f && *v < 1.1f;
}

static void MoveCursorTo(int x, int y)
{
    DWORD fn = ADDR_CL_MouseEvent;
    int mx = x - MEM(int, ADDR_ui_cursorX), my = y - MEM(int, ADDR_ui_cursorY);
    if (!mx && !my) return;
    __asm {
        mov eax, mx
        mov edx, my
        call fn
    }
}

void Controls_TestCursor(int x, int y) { MoveCursorTo(x, y); }

static void MenuButton(int slot, int now, void (*fn)(int, int), int arg)
{
    if (now != g_menuBtn[slot]) { g_menuBtn[slot] = now; fn(arg, now); }
}
static void KeyFn(int vk, int down) { SendKey(vk, down); }
static void ClickFn(int unused, int down) { (void)unused; SendClick(down); }

static void MenuControls(const vr_pose_t *screen, float w, float h)
{
    int dom = ccfg.rightHanded ? 1 : 0;
    float u, v;
    if (g_in.handValid[dom] && PointAt(&g_in.hand[dom], screen, w, h, &u, &v)) {
        int x = (int)(u * 640.0f), y = (int)(v * 480.0f);
        MoveCursorTo(x < 0 ? 0 : x > 640 ? 640 : x, y < 0 ? 0 : y > 480 ? 480 : y);
    }
    MenuButton(0, g_in.trigger[dom] > 0.6f, ClickFn, 0);
    MenuButton(1, (g_in.buttons & VRB_A) != 0, KeyFn, VK_RETURN);
    MenuButton(2, (g_in.buttons & VRB_B) != 0, KeyFn, VK_ESCAPE);
    MenuButton(3, (g_in.buttons & VRB_MENU) != 0, KeyFn, VK_ESCAPE);
    MenuButton(4, g_in.stick[dom][1] > 0.7f, KeyFn, VK_UP);
    MenuButton(5, g_in.stick[dom][1] < -0.7f, KeyFn, VK_DOWN);
}

static int AnyMenuButton(void)
{
    int i;
    for (i = 0; i < 6; i++) if (g_menuBtn[i]) return 1;
    return 0;
}

void Controls_Frame(float dt, const vr_pose_t *menuScreen, float w, float h)
{
    int dom = ccfg.rightHanded ? 1 : 0, off = 1 - dom;
    float turn;
    float *va = (float *)ADDR_cl_viewangles;
    int menu = (MEM(int, ADDR_cls_keyCatchers) & KEYCATCH_UI) != 0;

    XR_GetInput(&g_in);
    g_moveF = g_moveR = 0;
    if (!g_in.active) { ReleaseAll(); return; }

    if (menu) {
        if (!g_inMenu) { ReleaseAll(); g_inMenu = 1; }
        MenuControls(menuScreen, w, h);
        return;
    }
    if (g_inMenu) {
        // leaving a menu: buttons still held from it act only once released
        if (AnyMenuButton() || g_in.trigger[0] > 0.3f || g_in.trigger[1] > 0.3f || g_in.buttons) { memset(g_menuBtn, 0, sizeof(g_menuBtn)); return; }
        g_inMenu = 0;
    }

    SetButton(BT_PTRIGGER, Pressed(g_in.trigger[dom], BT_PTRIGGER));
    SetButton(BT_PGRIP, Pressed(g_in.grip[dom], BT_PGRIP));
    SetButton(BT_STRIGGER, Pressed(g_in.trigger[off], BT_STRIGGER));
    SetButton(BT_SGRIP, Pressed(g_in.grip[off], BT_SGRIP));
    SetButton(BT_A, (g_in.buttons & VRB_A) != 0);
    SetButton(BT_B, (g_in.buttons & VRB_B) != 0);
    SetButton(BT_X, (g_in.buttons & VRB_X) != 0);
    SetButton(BT_Y, (g_in.buttons & VRB_Y) != 0);
    SetButton(BT_LCLICK, (g_in.buttons & VRB_LSTICK) != 0);
    SetButton(BT_RCLICK, (g_in.buttons & VRB_RSTICK) != 0);
    SetButton(BT_MENU, (g_in.buttons & VRB_MENU) != 0);

    // walking: the off hand's stick, relative to where the view (the head) points
    g_moveF = Dead(g_in.stick[off][1]);
    g_moveR = Dead(g_in.stick[off][0]);

    // turning and stance: the gun hand's stick
    turn = g_in.stick[dom][0];
    if (ccfg.snapTurn > 0) {
        if (g_snapReady && fabsf(turn) > 0.7f) { va[1] -= turn > 0 ? ccfg.snapTurn : -ccfg.snapTurn; g_snapReady = 0; }
        else if (fabsf(turn) < 0.3f) g_snapReady = 1;
    } else {
        va[1] -= Dead(turn) * ccfg.turnSpeed * dt;
    }
    if (g_stanceReady && fabsf(g_in.stick[dom][1]) > 0.75f && fabsf(g_in.stick[dom][1]) > fabsf(turn)) {
        RunBind(g_in.stick[dom][1] > 0 ? BT_STICKUP : BT_STICKDOWN, 1);
        g_stanceReady = 0;
    } else if (fabsf(g_in.stick[dom][1]) < 0.3f) g_stanceReady = 1;
}

int Controls_InMenu(void) { return g_inMenu; }

static signed char ClampMove(int v) { return (signed char)(v > 127 ? 127 : v < -127 ? -127 : v); }

// CL_FinishMove(EBX = usercmd): the stick is added to the keyboard's movement first.
static void __cdecl AddVrMove(BYTE *cmd)
{
    if (g_moveF == 0 && g_moveR == 0) return;
    cmd[USERCMD_forwardmove] = ClampMove((signed char)cmd[USERCMD_forwardmove] + (int)(g_moveF * 127.0f));
    cmd[USERCMD_rightmove] = ClampMove((signed char)cmd[USERCMD_rightmove] + (int)(g_moveR * 127.0f));
}

static __declspec(naked) void hk_FinishMove(void)
{
    __asm {
        pushad
        push ebx
        call AddVrMove
        add esp, 4
        popad
        mov eax, ADDR_CL_FinishMove
        jmp eax
    }
}

int Controls_Install(void)
{
    DWORD site = ADDR_CL_FinishMove_call, old;
    if (MEM(BYTE, site) != 0xE8 || site + 5 + MEM(DWORD, site + 1) != ADDR_CL_FinishMove) {
        if (site + 5 + MEM(DWORD, site + 1) == (DWORD)hk_FinishMove) return 1;
        Log("controls: CL_FinishMove call not found, no stick movement");
        return 0;
    }
    VirtualProtect((void *)site, 5, PAGE_EXECUTE_READWRITE, &old);
    MEM(DWORD, site + 1) = (DWORD)hk_FinishMove - (site + 5);
    VirtualProtect((void *)site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)site, 5);
    return 1;
}
