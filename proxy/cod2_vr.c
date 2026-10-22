// cod2_vr: Call of Duty 2 single player (CoD2SP_s.exe 1.3) in an OpenXR headset.
//
// How it works (addresses in cod2_addresses.h, findings in ../docs/CoD2_VR_RE.md):
//  * d3d9_proxy.c makes the game's device a D3D9Ex device, so it can open the D3D11 textures the
//    OpenXR backend (vr_xr.c) shares with it: one per eye and one for the HUD.
//  * re.RenderScene in the client's renderer table (0x617ab0) is replaced. The world view the
//    cgame renders (CG_DrawActive) is rendered twice, once from each tracked eye, with each
//    eye's asymmetric frustum (the call to R_SetupProjection in R_SetupViewParms is redirected).
//  * The renderer queues commands for its backend. Before each eye (and before the HUD) a
//    marker command is queued: a viewport command (R_AddCmdSetViewport) with impossible values.
//    The backend's viewport handler is wrapped; on a marker it points the frame buffer render
//    target (dx.renderTargets[0]) at that eye's texture, or at the HUD texture, and rebinds it.
//  * The HUD and menus (everything 2D) land in the HUD texture, shown as a quad layer.
//  * At Present the frame is submitted to the headset (left eye mirrored on the monitor) and the
//    next headset frame is started; its head pose drives cl.viewangles (aim where you look).
//  * Without a headset the two eyes are shown side by side on the monitor (stereo test).
#define _CRT_SECURE_NO_WARNINGS
#include "cod2_vr.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <intrin.h>

#include "cod2_addresses.h"
#include "controls.h"

#define DEG(x)  ((x) * (180.0f / 3.14159265f))
#define RAD(x)  ((x) * (3.14159265f / 180.0f))
#define PITCH 0
#define YAW   1
#define MEM(type, addr) (*(type *)(addr))

char g_dir[MAX_PATH];
extern int g_useEx;

// ---- configuration (cod2_vr.ini next to the exe, then %APPDATA%\CoD2-VR\settings.ini) ----
static struct {
    int   enabled;
    int   vr;            // use a headset when there is one
    int   sbs;           // no headset: both eyes side by side on the monitor
    float separation;    // side by side: game units between the eyes
    float worldScale;    // game units per metre (1 unit = 1 inch)
    int   positional;    // head position moves the camera
    int   headAim;       // head yaw/pitch drive cl.viewangles
    float renderScale;   // eye resolution, times the headset's recommended size
    float cullMargin;    // degrees added on each side for what the game draws
    vr_hud_t hud;        // quad placement
    int   mirror;        // show the left eye in the game window
    int   traceCmds;     // diagnostics: log the backend commands of this many world frames
    int   menuFollowsHead; // 1 = menus follow the head, 0 = fixed in the room where they opened
    int   fakeHmd;       // diagnostics: the headset path with made-up eyes, shown side by side
    int   fakeW, fakeH;  // ... their resolution
    int   fakeCursor[2]; // ... where the menu cursor is put (640x480)
} cfg = { 1, 1, 1, 2.5f, 39.37f, 1, 1, 1.0f, 10.0f, { 0, 1, 1.4f, -0.05f, 1.0f, 1.8f }, 1, 0, 0, 0, 1440, 1584, { 395, 300 } };

static FILE *g_log;
static char  g_userIni[MAX_PATH];
static int   g_hooked;          // the game and renderer are the known versions and hooked
static DWORD g_gfx;             // gfx_d3d_x86_s.dll base
#define GFX(a) (g_gfx + ((DWORD)(a) - GFX_BASE))

void Log(const char *fmt, ...)
{
    va_list ap;
    SYSTEMTIME t;
    if (!g_log) {
        char path[MAX_PATH];
        _snprintf(path, sizeof(path), "%s\\cod2_vr.log", g_dir);
        g_log = fopen(path, "w");
        if (!g_log) return;
    }
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static int CfgInt(const char *sec, const char *key, int def, const char *ini)
{
    return GetPrivateProfileIntA(sec, key, def, ini);
}

static float IniFloat(const char *sec, const char *key, float def, const char *ini)
{
    char buf[64];
    GetPrivateProfileStringA(sec, key, "", buf, sizeof(buf), ini);
    return buf[0] ? (float)atof(buf) : def;
}

// Every setting: cod2_vr.ini, then the launcher's player settings (which win).
static int GetInt(const char *sec, const char *key, int def)
{
    char ini[MAX_PATH];
    int v;
    _snprintf(ini, sizeof(ini), "%s\\cod2_vr.ini", g_dir);
    v = CfgInt(sec, key, def, ini);
    return g_userIni[0] ? CfgInt(sec, key, v, g_userIni) : v;
}

static float GetFloat(const char *sec, const char *key, float def)
{
    char ini[MAX_PATH];
    float v;
    _snprintf(ini, sizeof(ini), "%s\\cod2_vr.ini", g_dir);
    v = IniFloat(sec, key, def, ini);
    return g_userIni[0] ? IniFloat(sec, key, v, g_userIni) : v;
}

static void GetStr(const char *sec, const char *key, const char *def, char *out, int size)
{
    char ini[MAX_PATH], tmp[256];
    _snprintf(ini, sizeof(ini), "%s\\cod2_vr.ini", g_dir);
    GetPrivateProfileStringA(sec, key, def, tmp, sizeof(tmp), ini);
    if (g_userIni[0]) GetPrivateProfileStringA(sec, key, tmp, out, size, g_userIni);
    else { strncpy(out, tmp, size - 1); out[size - 1] = 0; }
}

static void LoadConfig(void)
{
    char appdata[MAX_PATH];
    if (GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata)))
        _snprintf(g_userIni, sizeof(g_userIni), "%s\\CoD2-VR\\settings.ini", appdata);
    cfg.enabled     = GetInt("General", "Enabled", cfg.enabled);
    g_useEx         = GetInt("Proxy", "D3D9Ex", 1);
    cfg.vr          = GetInt("VR", "Enabled", cfg.vr);
    cfg.sbs         = GetInt("VR", "SideBySideWithoutHeadset", cfg.sbs);
    cfg.separation  = GetFloat("VR", "Separation", cfg.separation);
    cfg.worldScale  = GetFloat("VR", "WorldScale", cfg.worldScale);
    cfg.positional  = GetInt("VR", "PositionalTracking", cfg.positional);
    cfg.headAim     = GetInt("VR", "HeadAim", cfg.headAim);
    cfg.renderScale = GetFloat("VR", "RenderScale", cfg.renderScale);
    cfg.cullMargin  = GetFloat("VR", "CullMargin", cfg.cullMargin);
    cfg.mirror      = GetInt("VR", "Mirror", cfg.mirror);
    cfg.hud.headLocked = GetInt("HUD", "HeadLocked", 1);
    cfg.hud.distance   = GetFloat("HUD", "Distance", cfg.hud.distance);
    cfg.hud.offsetY    = GetFloat("HUD", "OffsetY", cfg.hud.offsetY);
    cfg.hud.width      = GetFloat("HUD", "Width", cfg.hud.width);
    cfg.hud.menuWidth  = GetFloat("HUD", "MenuWidth", cfg.hud.menuWidth);
    cfg.traceCmds   = GetInt("Debug", "TraceCommands", 0);
    cfg.fakeHmd     = GetInt("Debug", "FakeHeadset", 0);
    cfg.fakeW       = GetInt("Debug", "FakeEyeWidth", cfg.fakeW);
    cfg.fakeH       = GetInt("Debug", "FakeEyeHeight", cfg.fakeH);
    cfg.fakeCursor[0] = GetInt("Debug", "FakeCursorX", cfg.fakeCursor[0]);
    cfg.fakeCursor[1] = GetInt("Debug", "FakeCursorY", cfg.fakeCursor[1]);
    cfg.menuFollowsHead = GetInt("HUD", "MenuFollowsHead", 0);
    Controls_LoadConfig(GetInt, GetFloat, GetStr);
    if (!cfg.enabled) g_useEx = 0;
    XR_SetRenderScale(cfg.renderScale);
    Log("config: enabled %d, D3D9Ex %d, VR %d, side by side %d, world scale %.2f, render scale %.2f",
        cfg.enabled, g_useEx, cfg.vr, cfg.sbs, cfg.worldScale, cfg.renderScale);
}

static DWORD PeTimestamp(DWORD base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    return ((IMAGE_NT_HEADERS *)(base + dos->e_lfanew))->FileHeader.TimeDateStamp;
}

void VR_ProcessAttach(HMODULE self)
{
    char *p;
    (void)self;
    GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    if ((p = strrchr(g_dir, '\\')) != NULL) *p = 0;
    LoadConfig();
    if (PeTimestamp((DWORD)GetModuleHandleA(NULL)) != COD2SP_TIMESTAMP) {
        Log("not CoD2SP_s.exe 1.3 (timestamp %08x): passthrough only", PeTimestamp((DWORD)GetModuleHandleA(NULL)));
        cfg.enabled = 0;
        g_useEx = 0;
    }
}

// ---------------------------------------------------------------------------
// Math (Quake 3 conventions: axis = forward, left, up; pitch > 0 looks down)
// ---------------------------------------------------------------------------
static float AngleNormalize180(float a)
{
    a = fmodf(a + 180.0f, 360.0f);
    if (a < 0) a += 360.0f;
    return a - 180.0f;
}

static float YawOf(const float fwd[3])   { return DEG(atan2f(fwd[1], fwd[0])); }
static float PitchOf(const float fwd[3])
{
    float z = fwd[2] < -1 ? -1 : fwd[2] > 1 ? 1 : fwd[2];
    return -DEG(asinf(z));
}

static void RotateZ(const float v[3], float yawDeg, float out[3])
{
    float s = sinf(RAD(yawDeg)), c = cosf(RAD(yawDeg));
    out[0] = v[0] * c - v[1] * s;
    out[1] = v[0] * s + v[1] * c;
    out[2] = v[2];
}

// ---------------------------------------------------------------------------
// Frame state
// ---------------------------------------------------------------------------
enum { MODE_OFF, MODE_SBS, MODE_VR };
enum { TGT_WINDOW, TGT_EYE0, TGT_EYE1, TGT_HUD, MARK_WORLD_BEGIN = 5, MARK_WORLD_END };   // backend targets / markers
#define MARKER_MAGIC 0x56523200

static int        g_mode;               // this frame (decided at BeginFrame)
static int        g_vrFrame;            // XR_BeginFrame succeeded for the frame being built
static vr_pose_t  g_eyes[2], g_head;
static int        g_worldThisFrame;     // the world view was rendered (VR: eyes, else menu)
static int        g_projEye = -1;       // R_SetupProjection: put this eye's frustum in
static float      g_headYawPrev, g_headYawCmd;
static int        g_headYawInit;
static float      g_clPitchCmd, g_pitchOffset;
static int        g_pitchOffsetInit, g_pitchSampled;
static DWORD      g_nextXrTry;

// D3D9 side of the targets
static IDirect3DTexture9 *g_eyeTex[2], *g_hudTex;
static IDirect3DSurface9 *g_eyeSurf[2], *g_eyeDepth[2], *g_hudSurf;
static int        g_eyeW, g_eyeH, g_hudW, g_hudH;   // texture sizes
static int        g_targetsVR;          // 1 = the eye textures are the headset's shared ones, 2 = fake headset
static IDirect3DQuery9 *g_flushQuery;
static int        g_winW, g_winH;       // back buffer

// backend state (runs inside the renderer's command execution)
typedef struct { void *image; IDirect3DSurface9 *color, *depth; int width, height; } gfx_rt_t;
static int        g_beTarget = TGT_WINDOW;
static gfx_rt_t   g_rtSaved;            // the frame buffer target as the game made it

typedef void (__cdecl *PFN_Void)(void);
typedef void (__cdecl *PFN_RenderScene)(cod2_refdef_t *fd);
typedef void (__cdecl *PFN_CmdHandler)(BYTE **ctx);
typedef void (__cdecl *PFN_AddCmdViewport)(int x, int y, int w, int h);
static PFN_Void        orig_BeginFrame, orig_EndFrame;
static PFN_RenderScene orig_RenderScene;
static PFN_CmdHandler  orig_CmdViewport;
static DWORD           p_SetupProjection, p_RB_SetRenderTarget;
static PFN_AddCmdViewport p_AddCmdViewport;

// ---------------------------------------------------------------------------
// D3D9 resources
// ---------------------------------------------------------------------------
static void ReleaseTargets(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (g_eyeSurf[i]) IDirect3DSurface9_Release(g_eyeSurf[i]);
        if (g_eyeTex[i]) IDirect3DTexture9_Release(g_eyeTex[i]);
        if (g_eyeDepth[i]) IDirect3DSurface9_Release(g_eyeDepth[i]);
        g_eyeSurf[i] = NULL; g_eyeTex[i] = NULL; g_eyeDepth[i] = NULL;
    }
    if (g_hudSurf) IDirect3DSurface9_Release(g_hudSurf);
    if (g_hudTex) IDirect3DTexture9_Release(g_hudTex);
    g_hudSurf = NULL; g_hudTex = NULL;
    g_eyeW = g_eyeH = g_hudW = g_hudH = 0;
    g_targetsVR = 0;
}

static int OpenTexture(int w, int h, HANDLE share, IDirect3DTexture9 **tex, IDirect3DSurface9 **surf)
{
    HANDLE hnd = share;
    HRESULT hr = IDirect3DDevice9Ex_CreateTexture(g_dev, w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                                  tex, share ? &hnd : NULL);
    if (FAILED(hr)) { Log("render target %dx%d (%s) failed: %08x", w, h, share ? "shared" : "local", hr); return 0; }
    IDirect3DTexture9_GetSurfaceLevel(*tex, 0, surf);
    return 1;
}

// VR: the headset's shared textures. Side by side: local ones of the window's size.
static int CreateTargets(int vr)
{
    int i, w, h, hw, hh;
    ReleaseTargets();
    if (vr == 1) {
        if (!XR_EyeSize(&w, &h) || !XR_HudSize(&hw, &hh)) return 0;
    } else if (vr == 2) {
        w = cfg.fakeW; h = cfg.fakeH; hw = g_winW; hh = g_winH;
    } else {
        w = g_winW; h = g_winH; hw = g_winW; hh = g_winH;
    }
    for (i = 0; i < 2; i++) {
        if (!OpenTexture(w, h, vr == 1 ? XR_EyeShareHandle(i) : NULL, &g_eyeTex[i], &g_eyeSurf[i])) goto fail;
        if (FAILED(IDirect3DDevice9Ex_CreateDepthStencilSurface(g_dev, w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE,
                                                                 &g_eyeDepth[i], NULL))) { Log("eye depth %dx%d failed", w, h); goto fail; }
    }
    if (vr && !OpenTexture(hw, hh, vr == 1 ? XR_HudShareHandle() : NULL, &g_hudTex, &g_hudSurf)) goto fail;
    g_eyeW = w; g_eyeH = h; g_hudW = hw; g_hudH = hh;
    g_targetsVR = vr;
    Log("targets: eyes %dx%d, HUD %dx%d (%s)", w, h, hw, hh, vr == 1 ? "headset" : vr == 2 ? "fake headset" : "side by side");
    return 1;
fail:
    ReleaseTargets();
    return 0;
}

static void FlushGPU(void)
{
    int spins = 0;
    if (!g_flushQuery && FAILED(IDirect3DDevice9Ex_CreateQuery(g_dev, D3DQUERYTYPE_EVENT, &g_flushQuery))) return;
    IDirect3DQuery9_Issue(g_flushQuery, D3DISSUE_END);
    while (IDirect3DQuery9_GetData(g_flushQuery, NULL, 0, D3DGETDATA_FLUSH) == S_FALSE && spins++ < 200000) YieldProcessor();
}

// ---------------------------------------------------------------------------
// Backend: switching the frame buffer render target
// ---------------------------------------------------------------------------
static void RB_SetRenderTarget0(void)
{
    DWORD fn = p_RB_SetRenderTarget;
    __asm {
        xor eax, eax
        call fn
    }
}

static void BE_Bind(IDirect3DSurface9 *color, IDirect3DSurface9 *depth, int w, int h)
{
    gfx_rt_t *rt = (gfx_rt_t *)GFX(GFX_dx_renderTargets);
    rt->color = color;
    rt->depth = depth;
    rt->width = w;
    rt->height = h;
    MEM(int, GFX(GFX_rb_currentTarget)) = 14;   // none: the next bind really binds
    RB_SetRenderTarget0();
}

static void BE_SetTarget(int t)
{
    gfx_rt_t *rt = (gfx_rt_t *)GFX(GFX_dx_renderTargets);
    if (g_beTarget == TGT_WINDOW) g_rtSaved = *rt;
    switch (t) {
    case TGT_EYE0:
    case TGT_EYE1: {
        int e = t - TGT_EYE0;
        if (!g_eyeSurf[e]) return;
        BE_Bind(g_eyeSurf[e], g_eyeDepth[e], g_eyeW, g_eyeH);
        IDirect3DDevice9Ex_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xff000000, 1.0f, 0);
        break;
    }
    case TGT_HUD:
        if (!g_hudSurf) return;
        BE_Bind(g_hudSurf, g_rtSaved.depth, g_hudW, g_hudH);
        IDirect3DDevice9Ex_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0x00000000, 1.0f, 0);
        break;
    default:
        if (g_beTarget != TGT_WINDOW) BE_Bind(g_rtSaved.color, g_rtSaved.depth, g_rtSaved.width, g_rtSaved.height);
        break;
    }
    g_beTarget = t;
}

// The world is rendered once by the frontend and twice by the backend. The world view's
// commands lie between the WORLD_BEGIN and WORLD_END markers (they include the shadow maps,
// floatz and post effects of that frame). At WORLD_BEGIN the view is set up for the left eye
// and the left eye's texture is bound; at the first WORLD_END the view is set up for the right
// eye and execution jumps back to just after WORLD_BEGIN; at the second one the 2D follows.
static BYTE         *g_vp;              // the world view's viewParms (frontend data, 0x14c bytes)
static cod2_refdef_t g_eyeRef[2];       // what each eye's view is made from
static int           g_stereoReady;     // this frame's world view has eyes to render
static int           g_bePass;          // 0, 1 (left eye), 2 (right eye)
static BYTE         *g_replayStart;

static void SetupViewParms(BYTE *vp, cod2_refdef_t *fd)
{
    DWORD fn = GFX(GFX_R_SetupViewParms);
    __asm {
        push ebx
        mov ebx, vp
        push fd
        call fn
        add esp, 4
        pop ebx
    }
}

// Rebuilds the world view (camera, matrices, viewport) for one eye, as R_RenderScene does.
static void SetEyeView(int eye)
{
    float *globals = (float *)GFX(GFX_r_viewOrgFwd);
    g_projEye = g_mode == MODE_VR ? eye : -1;
    SetupViewParms(g_vp, &g_eyeRef[eye]);
    g_projEye = -1;
    memcpy(globals, g_eyeRef[eye].vieworg, 3 * sizeof(float));
    memcpy(globals + 3, g_eyeRef[eye].viewaxis[0], 3 * sizeof(float));
}

static void SideBySideComposite(void)
{
    BE_Bind(g_rtSaved.color, g_rtSaved.depth, g_rtSaved.width, g_rtSaved.height);
    if (g_eyeSurf[0] && g_eyeSurf[1]) {
        RECT l = { 0, 0, g_rtSaved.width / 2, g_rtSaved.height }, r = { g_rtSaved.width / 2, 0, g_rtSaved.width, g_rtSaved.height };
        IDirect3DDevice9Ex_StretchRect(g_dev, g_eyeSurf[0], NULL, g_rtSaved.color, &l, D3DTEXF_LINEAR);
        IDirect3DDevice9Ex_StretchRect(g_dev, g_eyeSurf[1], NULL, g_rtSaved.color, &r, D3DTEXF_LINEAR);
    }
    g_beTarget = TGT_WINDOW;
}

// Command 13 (viewport): markers; real viewports in window pixels are scaled to the eye texture
// while one is bound.
static void __cdecl hk_CmdViewport(BYTE **ctx)
{
    BYTE *cmd = *ctx;
    int *v = (int *)(cmd + 4);
    if ((v[0] & 0xffffff00) == MARKER_MAGIC) {
        int m = v[0] & 0xff;
        *ctx = cmd + *(WORD *)(cmd + 2);
        if (m == MARK_WORLD_BEGIN) {
            if (!g_stereoReady || !g_eyeSurf[0]) return;
            g_bePass = 1;
            g_replayStart = *ctx;
            BE_SetTarget(TGT_EYE0);
            SetEyeView(0);
        } else if (m == MARK_WORLD_END) {
            if (g_bePass == 1) {
                g_bePass = 2;
                BE_SetTarget(TGT_EYE1);
                SetEyeView(1);
                ctx[0] = g_replayStart;      // again, from the top level
                ((int *)ctx)[1] = 0;
            } else if (g_bePass == 2) {
                g_bePass = 0;
                g_stereoReady = 0;
                if (g_mode == MODE_VR) BE_SetTarget(TGT_HUD);
                else SideBySideComposite();
            }
        } else {
            BE_SetTarget(m);
        }
        return;
    }
    if ((g_beTarget == TGT_EYE0 || g_beTarget == TGT_EYE1) && g_rtSaved.width > 0 && g_rtSaved.height > 0 &&
        (g_eyeW != g_rtSaved.width || g_eyeH != g_rtSaved.height)) {
        BYTE tmp[0x14];
        BYTE *tp = tmp;
        int *t = (int *)(tmp + 4);
        memcpy(tmp, cmd, sizeof(tmp));
        t[0] = v[0] * g_eyeW / g_rtSaved.width;
        t[1] = v[1] * g_eyeH / g_rtSaved.height;
        t[2] = v[2] * g_eyeW / g_rtSaved.width;
        t[3] = v[3] * g_eyeH / g_rtSaved.height;
        orig_CmdViewport(&tp);
        *ctx = cmd + *(WORD *)(cmd + 2);
        return;
    }
    orig_CmdViewport(ctx);
}

// Diagnostics: every command handler wrapped, logging what the backend executes.
static PFN_CmdHandler g_origCmd[GFX_CMD_COUNT];
static int            g_tracing;   // frames left to log (counted at EndFrame)

static void TraceCmd(int op, BYTE *cmd)
{
    int *v = (int *)(cmd + 4);
    if (op >= 1 && op <= 3) Log("  cmd %2d @%p -> %08x", op, cmd, v[0]);
    else if (op == GFX_CMD_SETVIEWPORT) Log("  cmd 13 @%p viewport %08x %d %d %d", cmd, v[0], v[1], v[2], v[3]);
    else if (op == 14) Log("  cmd 14 @%p render target %d", cmd, v[0]);
    else Log("  cmd %2d @%p size %d", op, cmd, *(WORD *)(cmd + 2));
}

#define TRACE_THUNK(n) static void __cdecl TraceCmd##n(BYTE **ctx) { if (g_tracing) TraceCmd(n, *ctx);     if (n == GFX_CMD_SETVIEWPORT) hk_CmdViewport(ctx); else g_origCmd[n](ctx); }
TRACE_THUNK(1) TRACE_THUNK(2) TRACE_THUNK(3) TRACE_THUNK(4) TRACE_THUNK(5) TRACE_THUNK(6) TRACE_THUNK(7)
TRACE_THUNK(8) TRACE_THUNK(9) TRACE_THUNK(10) TRACE_THUNK(11) TRACE_THUNK(12) TRACE_THUNK(13) TRACE_THUNK(14)
TRACE_THUNK(15) TRACE_THUNK(16) TRACE_THUNK(17) TRACE_THUNK(18) TRACE_THUNK(19) TRACE_THUNK(20) TRACE_THUNK(21)
TRACE_THUNK(22) TRACE_THUNK(23) TRACE_THUNK(24) TRACE_THUNK(25) TRACE_THUNK(26) TRACE_THUNK(27) TRACE_THUNK(28)
TRACE_THUNK(29) TRACE_THUNK(30) TRACE_THUNK(31) TRACE_THUNK(32) TRACE_THUNK(33)
static PFN_CmdHandler g_traceThunks[GFX_CMD_COUNT] = { NULL, TraceCmd1, TraceCmd2, TraceCmd3, TraceCmd4, TraceCmd5,
    TraceCmd6, TraceCmd7, TraceCmd8, TraceCmd9, TraceCmd10, TraceCmd11, TraceCmd12, TraceCmd13, TraceCmd14, TraceCmd15,
    TraceCmd16, TraceCmd17, TraceCmd18, TraceCmd19, TraceCmd20, TraceCmd21, TraceCmd22, TraceCmd23, TraceCmd24,
    TraceCmd25, TraceCmd26, TraceCmd27, TraceCmd28, TraceCmd29, TraceCmd30, TraceCmd31, TraceCmd32, TraceCmd33 };

static void Marker(int target)
{
    p_AddCmdViewport(MARKER_MAGIC | target, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// Frontend: the world view from each eye
// ---------------------------------------------------------------------------
static float BodyYaw(const float viewFwd[3]) { return YawOf(viewFwd) - g_headYawCmd; }

static void ApplyEyePose(cod2_refdef_t *fd, const vr_pose_t *eye, const float org[3], const float axis[3][3])
{
    float body = BodyYaw(axis[0]), rel[3], p[3];
    int i;
    for (i = 0; i < 3; i++) RotateZ(eye->axis[i], body, fd->viewaxis[i]);
    if (cfg.positional) { rel[0] = eye->pos[0]; rel[1] = eye->pos[1]; rel[2] = eye->pos[2]; }
    else { rel[0] = eye->pos[0] - g_head.pos[0]; rel[1] = eye->pos[1] - g_head.pos[1]; rel[2] = eye->pos[2] - g_head.pos[2]; }
    RotateZ(rel, body, p);
    for (i = 0; i < 3; i++) fd->vieworg[i] = org[i] + p[i] * cfg.worldScale;
}

static void SamplePitchOffset(const cod2_refdef_t *fd)
{
    float raw = AngleNormalize180(PitchOf(fd->viewaxis[0]) - g_clPitchCmd);
    if (!g_pitchOffsetInit || fabsf(raw - g_pitchOffset) > 15.0f) { g_pitchOffset = raw; g_pitchOffsetInit = 1; }
    else g_pitchOffset += (raw - g_pitchOffset) * 0.05f;
}

// The world view: eye refdefs for the backend, and one frontend pass from between the eyes with
// a frustum that holds both (union of the eyes' fovs plus a margin), so what either eye sees is
// in the frame's draw lists.
static void RenderStereo(cod2_refdef_t *fd)
{
    cod2_refdef_t saved = *fd;
    BYTE *fe = (BYTE *)MEM(DWORD, GFX(GFX_frontEndData));
    int k, i;

    for (k = 0; k < 2; k++) {
        cod2_refdef_t *r = &g_eyeRef[k];
        *r = saved;
        r->x = 0;
        r->y = 0;
        r->width = g_eyeW;
        r->height = g_eyeH;
        if (g_mode == MODE_VR) {
            ApplyEyePose(r, &g_eyes[k], saved.vieworg, saved.viewaxis);
        } else {
            float shift = (k == 0 ? 0.5f : -0.5f) * cfg.separation;   // viewaxis[1] points left
            for (i = 0; i < 3; i++) r->vieworg[i] += r->viewaxis[1][i] * shift;
        }
    }
    *fd = g_eyeRef[0];
    for (i = 0; i < 3; i++) fd->vieworg[i] = 0.5f * (g_eyeRef[0].vieworg[i] + g_eyeRef[1].vieworg[i]);
    if (g_mode == MODE_VR) {
        float tx = 0, ty = 0;
        for (k = 0; k < 2; k++) {
            tx = max(tx, max(fabsf(g_eyes[k].tanLeft), fabsf(g_eyes[k].tanRight)));
            ty = max(ty, max(fabsf(g_eyes[k].tanUp), fabsf(g_eyes[k].tanDown)));
        }
        fd->fov_x = g_eyeRef[0].fov_x = g_eyeRef[1].fov_x = min(170.0f, 2.0f * DEG(atanf(tx)) + 2.0f * cfg.cullMargin);
        fd->fov_y = g_eyeRef[0].fov_y = g_eyeRef[1].fov_y = min(170.0f, 2.0f * DEG(atanf(ty)) + 2.0f * cfg.cullMargin);
    }
    // R_RenderScene sets up the next viewParms of the frame
    g_vp = fe + GFX_FE_viewParms + MEM(int, fe + GFX_FE_viewParmsCount) * GFX_VIEWPARMS_SIZE;
    g_stereoReady = 1;
    Marker(MARK_WORLD_BEGIN);
    orig_RenderScene(fd);
    Marker(MARK_WORLD_END);
    *fd = saved;
}

static void __cdecl hk_RenderScene(cod2_refdef_t *fd)
{
    // only the cgame's world view; other scenes keep the game's camera and go where the 2D goes
    if (g_mode == MODE_OFF || (DWORD)_ReturnAddress() != ADDR_CG_RenderScene_ret || !g_eyeSurf[0] || g_worldThisFrame) {
        orig_RenderScene(fd);
        return;
    }
    g_worldThisFrame = 1;
    if (g_mode == MODE_VR && !g_pitchSampled) { SamplePitchOffset(fd); g_pitchSampled = 1; }
    RenderStereo(fd);
}

// R_SetupProjection(fov_x, fov_y, zNear) builds an infinite left-handed D3D projection into ESI
// (view space x right, y up, z forward). The eyes' frustums are asymmetric: m[0], m[5], m[8], m[9].
static void __cdecl FixProjection(float *m)
{
    const vr_pose_t *e;
    if (g_projEye < 0) return;
    e = &g_eyes[g_projEye];
    m[0] = 2.0f / (e->tanRight - e->tanLeft);
    m[8] = -(e->tanRight + e->tanLeft) / (e->tanRight - e->tanLeft);
    m[5] = 2.0f / (e->tanUp - e->tanDown);
    m[9] = -(e->tanUp + e->tanDown) / (e->tanUp - e->tanDown);
}

static __declspec(naked) void hk_SetupProjection(void)
{
    __asm {
        mov eax, [esp + 12]
        push eax
        mov eax, [esp + 12]
        push eax
        mov eax, [esp + 12]
        push eax
        call dword ptr [p_SetupProjection]
        add esp, 12
        push esi
        call FixProjection
        add esp, 4
        ret
    }
}

// Head orientation -> cl.viewangles: yaw as a delta (the mouse keeps turning the body), pitch
// absolute, corrected by the measured offset between cl.viewangles and the view (delta_angles).
static void UpdateViewAngles(void)
{
    float *va = (float *)ADDR_cl_viewangles;
    float yaw = YawOf(g_head.axis[0]), pitch = PitchOf(g_head.axis[0]);
    if (!g_headYawInit) { g_headYawPrev = yaw; g_headYawInit = 1; }
    g_headYawCmd = g_headYawPrev;
    g_clPitchCmd = va[PITCH];
    if (!cfg.headAim) return;
    va[YAW] += AngleNormalize180(yaw - g_headYawPrev);
    g_headYawPrev = yaw;
    if (g_pitchOffsetInit) va[PITCH] = pitch - g_pitchOffset;
}

static void FakeEyes(void)
{
    int k;
    memset(g_eyes, 0, sizeof(g_eyes));
    memset(&g_head, 0, sizeof(g_head));
    g_head.axis[0][0] = g_head.axis[1][1] = g_head.axis[2][2] = 1;
    for (k = 0; k < 2; k++) {
        vr_pose_t *e = &g_eyes[k];
        e->axis[0][0] = e->axis[1][1] = e->axis[2][2] = 1;
        e->pos[1] = k ? -0.032f : 0.032f;            // y points left
        e->tanLeft = k ? -0.90f : -1.10f;            // the outer side of each eye sees further
        e->tanRight = k ? 1.10f : 0.90f;
        e->tanUp = 0.88f;
        e->tanDown = -1.20f;
    }
}

static void __cdecl hk_BeginFrame(void)
{
    orig_BeginFrame();
    g_worldThisFrame = 0;
    g_pitchSampled = 0;
    if (!cfg.enabled) { g_mode = MODE_OFF; return; }
    if (g_targetsVR == 2) {
        FakeEyes();
        g_mode = MODE_VR;
        if (MEM(int, ADDR_cls_keyCatchers) & KEYCATCH_UI) Controls_TestCursor(cfg.fakeCursor[0], cfg.fakeCursor[1]);
    }
    else if (g_vrFrame && g_targetsVR) g_mode = MODE_VR;
    else if (cfg.sbs && g_eyeSurf[0] && !g_targetsVR) g_mode = MODE_SBS;
    else g_mode = MODE_OFF;
    if (g_mode == MODE_VR) Marker(TGT_HUD);   // everything 2D goes to the HUD
}

static void __cdecl hk_EndFrame(void)
{
    static int traced;
    if (g_mode != MODE_OFF) Marker(TGT_WINDOW);
    if (cfg.traceCmds && g_worldThisFrame && !g_tracing && traced < cfg.traceCmds) { g_tracing = 1; traced++; Log("---- commands of world frame %d", traced); }
    orig_EndFrame();
    g_tracing = 0;
}

// ---------------------------------------------------------------------------
// Present: submit, mirror, start the next headset frame
// ---------------------------------------------------------------------------
static void StartVR(void)
{
    LUID luid = { 0 };
    if (cfg.fakeHmd) { if (g_targetsVR != 2) CreateTargets(2); return; }
    if (!cfg.vr || XR_Ready() || !g_d3d || (int)(GetTickCount() - g_nextXrTry) < 0) return;
    g_nextXrTry = GetTickCount() + 2000;
    IDirect3D9Ex_GetAdapterLUID(g_d3d, g_adapter, &luid);
    if (!XR_Init(luid, g_winW, g_winH)) return;
    if (!CreateTargets(1)) { Log("VR: could not open the headset textures, side by side instead"); XR_Shutdown(); cfg.vr = 0; CreateTargets(0); return; }
    if (g_userIni[0]) {
        // for the launcher: the eye size of each resolution choice
        char dir[MAX_PATH], v[16], *slash;
        int w, h, mw, mh;
        XR_RecommendedSize(&w, &h, &mw, &mh);
        strcpy(dir, g_userIni);
        if ((slash = strrchr(dir, '\\')) != NULL) { *slash = 0; CreateDirectoryA(dir, NULL); }
        sprintf(v, "%d", w);  WritePrivateProfileStringA("Headset", "RecommendedWidth", v, g_userIni);
        sprintf(v, "%d", h);  WritePrivateProfileStringA("Headset", "RecommendedHeight", v, g_userIni);
        sprintf(v, "%d", mw); WritePrivateProfileStringA("Headset", "MaxWidth", v, g_userIni);
        sprintf(v, "%d", mh); WritePrivateProfileStringA("Headset", "MaxHeight", v, g_userIni);
    }
}

// The menu screen: placed level in front of the head when a menu opens, and left there (you can
// look around it and point at it). During play the HUD follows the head.
static vr_pose_t g_menuPose;
static int       g_menuPlaced, g_menuShown;
static LARGE_INTEGER g_lastTick;

static int MenuActive(void)
{
    return !g_worldThisFrame || (MEM(int, ADDR_cls_keyCatchers) & KEYCATCH_UI) != 0;
}

static void PlaceMenu(void)
{
    float f[3] = { g_head.axis[0][0], g_head.axis[0][1], 0 }, len = sqrtf(f[0] * f[0] + f[1] * f[1]);
    int i;
    if (len < 0.01f) { f[0] = 1; f[1] = 0; len = 1; }
    f[0] /= len; f[1] /= len;
    memset(&g_menuPose, 0, sizeof(g_menuPose));
    for (i = 0; i < 3; i++) g_menuPose.pos[i] = g_head.pos[i] + f[i] * cfg.hud.distance;
    g_menuPose.pos[2] += cfg.hud.offsetY;
    g_menuPose.axis[0][0] = f[0]; g_menuPose.axis[0][1] = f[1];
    g_menuPose.axis[1][0] = -f[1]; g_menuPose.axis[1][1] = f[0];   // left = up x forward
    g_menuPose.axis[2][2] = 1;
    g_menuPlaced = 1;
}

// The HUD screen as it is shown now (head-locked during play): tracking space, Quake axes.
static void CurrentScreen(vr_pose_t *p, float *w, float *h)
{
    int i;
    *w = MenuActive() ? cfg.hud.menuWidth : cfg.hud.width;
    *h = *w * (g_hudH > 0 ? (float)g_hudH / g_hudW : 0.5625f);
    if (MenuActive() && !cfg.menuFollowsHead) { *p = g_menuPose; return; }
    *p = g_head;
    for (i = 0; i < 3; i++) p->pos[i] = g_head.pos[i] + g_head.axis[0][i] * cfg.hud.distance + g_head.axis[2][i] * cfg.hud.offsetY;
}

static void ControllersFrame(void)
{
    LARGE_INTEGER now, freq;
    vr_pose_t scr;
    float dt, w, h;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    dt = g_lastTick.QuadPart ? (float)(now.QuadPart - g_lastTick.QuadPart) / freq.QuadPart : 0;
    if (dt > 0.1f) dt = 0.1f;
    g_lastTick = now;
    if (MenuActive() && (!g_menuShown || !g_menuPlaced)) PlaceMenu();
    g_menuShown = MenuActive();
    CurrentScreen(&scr, &w, &h);
    Controls_Frame(dt, &scr, w, h);
}

void VR_BeforePresent(void)
{
    vr_hud_t hud;
    if (!g_hooked) return;
    if (g_beTarget != TGT_WINDOW) BE_SetTarget(TGT_WINDOW);
    if (g_mode == MODE_VR && g_targetsVR == 2) {
        IDirect3DSurface9 *bb = NULL;
        if (SUCCEEDED(IDirect3DDevice9Ex_GetBackBuffer(g_dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
            RECT l = { 0, 0, g_winW / 2, g_winH }, r = { g_winW / 2, 0, g_winW, g_winH };
            RECT q = { g_winW * 3 / 4, g_winH * 3 / 4, g_winW, g_winH };
            if (g_worldThisFrame) {
                IDirect3DDevice9Ex_StretchRect(g_dev, g_eyeSurf[0], NULL, bb, &l, D3DTEXF_LINEAR);
                IDirect3DDevice9Ex_StretchRect(g_dev, g_eyeSurf[1], NULL, bb, &r, D3DTEXF_LINEAR);
                IDirect3DDevice9Ex_StretchRect(g_dev, g_hudSurf, NULL, bb, &q, D3DTEXF_LINEAR);
            } else {
                IDirect3DDevice9Ex_StretchRect(g_dev, g_hudSurf, NULL, bb, NULL, D3DTEXF_LINEAR);
            }
            IDirect3DSurface9_Release(bb);
        }
        return;
    }
    if (g_mode != MODE_VR || !XR_FrameOpen()) return;
    FlushGPU();   // the shared textures are complete before D3D11 copies them
    if (cfg.mirror) {
        IDirect3DSurface9 *bb = NULL;
        if (SUCCEEDED(IDirect3DDevice9Ex_GetBackBuffer(g_dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
            // the middle of the left eye, at the window's aspect
            IDirect3DSurface9 *src = g_worldThisFrame ? g_eyeSurf[0] : g_hudSurf;
            int sw = g_worldThisFrame ? g_eyeW : g_hudW, sh = g_worldThisFrame ? g_eyeH : g_hudH, cw = sw, ch = sh;
            RECT r;
            if (g_winW * sh > g_winH * sw) ch = sw * g_winH / g_winW; else cw = sh * g_winW / g_winH;
            r.left = (sw - cw) / 2; r.top = (sh - ch) / 2; r.right = r.left + cw; r.bottom = r.top + ch;
            IDirect3DDevice9Ex_StretchRect(g_dev, src, &r, bb, NULL, D3DTEXF_LINEAR);
            IDirect3DSurface9_Release(bb);
        }
    }
    hud = cfg.hud;
    hud.world = g_worldThisFrame;
    if (MenuActive()) {
        hud.width = cfg.hud.menuWidth;
        if (!cfg.menuFollowsHead && g_menuPlaced) { hud.posed = 1; hud.pose = g_menuPose; }
    }
    XR_EndFrame(&hud);
}

void VR_AfterPresent(void)
{
    static int logged;
    if (!g_hooked) return;
    StartVR();
    g_vrFrame = 0;
    if (g_targetsVR && XR_Ready()) {
        g_vrFrame = XR_BeginFrame(g_eyes, &g_head);
        if (!g_vrFrame && XR_FrameOpen()) XR_EndFrame(NULL);   // not shown: close it at once
        if (g_vrFrame) { UpdateViewAngles(); ControllersFrame(); }
        if (g_vrFrame && !logged) { logged = 1; Log("VR: first headset frame"); }
    }
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
static void PatchPointer(DWORD addr, void *value)
{
    DWORD old;
    VirtualProtect((void *)addr, 4, PAGE_EXECUTE_READWRITE, &old);
    MEM(void *, addr) = value;
    VirtualProtect((void *)addr, 4, old, &old);
}

static int PatchCall(DWORD site, DWORD expect, void *target)
{
    DWORD old;
    if (MEM(BYTE, site) != 0xE8 || site + 5 + MEM(DWORD, site + 1) != expect) {
        Log("call at %08x does not go to %08x: not patched", site, expect);
        return 0;
    }
    VirtualProtect((void *)site, 5, PAGE_EXECUTE_READWRITE, &old);
    MEM(DWORD, site + 1) = (DWORD)target - (site + 5);
    VirtualProtect((void *)site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)site, 5);
    return 1;
}

static int InstallHooks(void)
{
    HMODULE gfx = GetModuleHandleA("gfx_d3d_x86_s.dll");
    DWORD *re = (DWORD *)ADDR_re, cvar;
    if (!gfx) { Log("gfx_d3d_x86_s.dll not loaded"); return 0; }
    g_gfx = (DWORD)gfx;
    if (PeTimestamp(g_gfx) != GFXD3D_TIMESTAMP) { Log("unknown gfx_d3d_x86_s.dll (timestamp %08x)", PeTimestamp(g_gfx)); return 0; }
    // the client's table must be the copy of this renderer's
    if (re[RE_RenderScene / 4] != GFX(GFX_R_RenderScene)) {
        Log("re.RenderScene is %08x, expected %08x: not hooking", re[RE_RenderScene / 4], GFX(GFX_R_RenderScene));
        return 0;
    }
    p_SetupProjection = GFX(GFX_R_SetupProjection);
    p_RB_SetRenderTarget = GFX(GFX_RB_SetRenderTarget);
    p_AddCmdViewport = (PFN_AddCmdViewport)GFX(GFX_R_AddCmdSetViewport);
    if (!PatchCall(GFX(GFX_R_SetupProjection_call), p_SetupProjection, hk_SetupProjection)) return 0;
    Controls_Install();

    orig_RenderScene = (PFN_RenderScene)re[RE_RenderScene / 4];
    orig_BeginFrame = (PFN_Void)re[RE_BeginFrame / 4];
    orig_EndFrame = (PFN_Void)re[RE_EndFrame / 4];
    PatchPointer((DWORD)&re[RE_RenderScene / 4], hk_RenderScene);
    PatchPointer((DWORD)&re[RE_BeginFrame / 4], hk_BeginFrame);
    PatchPointer((DWORD)&re[RE_EndFrame / 4], hk_EndFrame);
    {
        DWORD slot = GFX(GFX_cmdTable) + GFX_CMD_SETVIEWPORT * 4;
        int i;
        orig_CmdViewport = (PFN_CmdHandler)MEM(DWORD, slot);
        PatchPointer(slot, hk_CmdViewport);
        if (cfg.traceCmds)
            for (i = 1; i < GFX_CMD_COUNT; i++) {
                slot = GFX(GFX_cmdTable) + i * 4;
                g_origCmd[i] = i == GFX_CMD_SETVIEWPORT ? orig_CmdViewport : (PFN_CmdHandler)MEM(DWORD, slot);
                PatchPointer(slot, g_traceThunks[i]);
            }
    }
    // the backend must run on the main thread, inside re.EndFrame (frame pacing, poses)
    cvar = MEM(DWORD, GFX(GFX_cvar_r_smp_backend));
    if (cvar && MEM(BYTE, cvar + 8)) { Log("r_smp_backend was on: turned off"); MEM(BYTE, cvar + 8) = 0; }
    Log("hooks installed (gfx at %08x)", g_gfx);
    return 1;
}

void VR_DeviceCreated(IDirect3DDevice9Ex *dev, const D3DPRESENT_PARAMETERS *pp)
{
    (void)dev;
    if (!cfg.enabled) return;
    g_winW = pp->BackBufferWidth;
    g_winH = pp->BackBufferHeight;
    // a new device (vid_restart): the old one's resources are gone with it, nothing to release
    g_flushQuery = NULL;
    memset(g_eyeTex, 0, sizeof(g_eyeTex)); memset(g_eyeSurf, 0, sizeof(g_eyeSurf)); memset(g_eyeDepth, 0, sizeof(g_eyeDepth));
    g_hudTex = NULL; g_hudSurf = NULL;
    ReleaseTargets();
    if (XR_Ready()) XR_Shutdown();
    g_beTarget = TGT_WINDOW;
    g_bePass = 0;
    g_hooked = InstallHooks();
    if (!g_hooked) return;
    StartVR();
    if (!g_targetsVR && cfg.sbs) CreateTargets(0);
}

void VR_DeviceReset(const D3DPRESENT_PARAMETERS *pp)
{
    if (!g_hooked) return;
    g_beTarget = TGT_WINDOW;
    if ((int)pp->BackBufferWidth == g_winW && (int)pp->BackBufferHeight == g_winH) return;
    g_winW = pp->BackBufferWidth;
    g_winH = pp->BackBufferHeight;
    if (g_targetsVR) {
        if (!XR_ResizeHud(g_winW, g_winH) || !CreateTargets(1)) Log("VR: HUD resize failed");
    } else if (cfg.sbs) CreateTargets(0);
}
