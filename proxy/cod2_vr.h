// Shared declarations between the D3D9 proxy (d3d9_proxy.c), the game hooks (cod2_vr.c) and the
// OpenXR backend (vr_xr.c).
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define COBJMACROS
#include <d3d9.h>

extern char g_dir[MAX_PATH];
void Log(const char *fmt, ...);

// ---- d3d9_proxy.c ----
extern IDirect3DDevice9Ex *g_dev;      // the game's device (created as a D3D9Ex device)
extern IDirect3D9Ex       *g_d3d;
extern UINT                g_adapter;  // adapter the game's device runs on
void Proxy_HookSwapChain(IDirect3DSwapChain9 *sc);   // Present of the game's swap chain -> VR_Present

// ---- cod2_vr.c (called by the proxy) ----
void VR_ProcessAttach(HMODULE self);
void VR_DeviceCreated(IDirect3DDevice9Ex *dev, const D3DPRESENT_PARAMETERS *pp);
void VR_DeviceReset(const D3DPRESENT_PARAMETERS *pp);   // after a successful Reset
void VR_BeforePresent(void);    // the frame is complete in the back buffer's place
void VR_AfterPresent(void);
void VR_RunOnBigStack(void (*work)(void));   // work on the mod's 4 MB stack (the game's threads have 64 KB)
void VR_OnCreateCmd(void);       // controls.c: a usercmd is being made (CL_FinishMove)
void VR_Command(const char *cmd);   // controls.c: a "vr:" button command was pressed (recenter, vignette)
void Proxy_SetMirrorGun(int on);   // d3d9_proxy.c: the view weapon is drawn mirrored this frame (its cull mode flipped)
float VR_MoveYawOffset(void);    // degrees from the aim's yaw to the head's: the stick walks where you look
int VR_UsableNear(const float at[3]);
int VR_StickWalks(void);           // controls.c: the move stick walks (not while it aims a Blink / Shift)
int VR_RoomMove(float *forward, float *right);   // controls.c: roomscale's usercmd move this frame (-1 .. 1), 0 = none   // controls.c: the usable entity (a trigger_use) at a tracked point, 0 = none

// ---- OpenXR backend (vr_xr.c) ----
// One eye (or the head) in tracking space, converted to Quake 3 conventions:
// x forward, y left, z up, metres. fov tangents follow OpenGL eye space (left/down negative).
typedef struct {
    float pos[3];
    float axis[3][3];   // forward, left, up
    float tanLeft, tanRight, tanUp, tanDown;
} vr_pose_t;

typedef struct {
    int    world;        // the 3D world was drawn this frame (0 = menu or loading screen)
    int    headLocked;   // 1 = screen follows the head, 0 = fixed in the room
    float  distance;     // metres in front of the eyes
    float  offsetY;      // metres up (negative = down)
    float  width;        // metres, while playing
    float  menuWidth;    // metres, in menus (no world drawn)
    int    posed;        // 1 = placed at pose (tracking space) instead of headLocked / distance / offsetY
    vr_pose_t pose;      // axis[0] points away from the viewer, axis[2] is the screen's up
    float  vignette;     // comfort vignette drawn over the eyes, 0 = open .. 1 = closed to its smallest
    float  fade;         // the eyes darkened by this much (0 .. 1)
    int    full;         // the full-view layer has the game's full-screen pictures this frame
    int    fullAspect;   // ... menu pictures: kept at their shape (covering the view, the sides cut), not stretched
    float  panel;        // a dark rounded panel under the HUD screen's content (the objectives on the hand), opacity
    int    backdrop;     // opaque black behind the screens (no world: the death screen), instead of the empty space
    int    hideHud;      // no HUD screen this frame (all its 2D went to the full-view layer: the death screen)
} vr_hud_t;

// image effects on the eye images ([PostFX]; postfx.c)
typedef struct { float sharpen, bloom, bloomThreshold, contrast, saturation, tint[3]; } vr_postfx_t;
void   XR_SetPostFx(const vr_postfx_t *p);

int    XR_Init(LUID adapterLuid, int hudW, int hudH);   // instance, D3D11 device, session, swapchains
void   XR_SetRenderScale(float s);
int    XR_EyeSize(int *w, int *h);
void   XR_RecommendedSize(int *w, int *h, int *maxW, int *maxH);
void   XR_Shutdown(void);
int    XR_Ready(void);
int    XR_TryAgain(void);           // 1 = no headset yet, worth retrying XR_Init later
HANDLE XR_EyeShareHandle(int eye);   // shared B8G8R8A8 textures the game renders into (D3D9Ex opens them)
HANDLE XR_HudShareHandle(void);
HANDLE XR_FullShareHandle(void);   // full-screen pictures, the HUD's size, shown over the whole view
int    XR_HudSize(int *w, int *h);
int    XR_ResizeHud(int w, int h);    // window size changed: new HUD texture (new share handle)
int    XR_BeginFrame(vr_pose_t eyes[2], vr_pose_t *head);   // 1 = this frame goes to the headset
void   XR_EndFrame(const vr_hud_t *hud);   // copies the shared textures to the swapchains and submits
int    XR_FrameOpen(void);
// While the game loads (a frame every few seconds): on = a thread of the backend submits frames with 'hud'
// (its screens only, as they were) until called with off, which returns once that thread has stopped.
void   XR_KeepAlive(int on, const vr_hud_t *hud);
void   XR_FlushHaptics(void);      // the vibrations asked for since the last call go to the runtime
int    XR_KeepAlivePresses(void);   // trigger / A presses on a screen without world or menu since the last call
void   XR_PollScreenPress(void);    // ... read on the game's thread (its frame open, the keep-alive thread stopped)
void   XR_ScreenPressReset(void);   // the game's own controls run again: forget the state

// ---- controllers ----
#define VRB_A       0x01
#define VRB_B       0x02
#define VRB_X       0x04
#define VRB_Y       0x08
#define VRB_LSTICK  0x10
#define VRB_RSTICK  0x20
#define VRB_MENU    0x40

typedef struct {
    int       active;
    int       handValid[2];  // 0 = left, 1 = right
    vr_pose_t hand[2];       // aim pose in tracking space, Quake 3 conventions
    float     trigger[2];
    float     grip[2];
    float     stick[2][2];   // [hand][x right, y forward]
    unsigned  buttons;       // VRB_*
} vr_input_t;

void XR_GetInput(vr_input_t *in);
void XR_Haptic(int hand, float amplitude, int ms);
// Menu pointer this frame: a beam from 'from' to 'to' (tracking space, Quake axes, metres), with a
// spot at the end when it hits the screen. Cleared by XR_BeginFrame.
void XR_SetLaser(const float from[3], const float to[3], int hit);
void XR_SetPath(const float (*pts)[3], int n, float width, const float rgba[4]);   // a line through n points (tracking space, Quake axes)
void XR_ClearLines(void);
// a fixed image panel in the room: the image once (premultiplied BGRA, bottom row first), then shown one frame at a time
// (pos / axis in tracking space, Quake axes: axis[0] away from the viewer, axis[1] left, axis[2] up; width in metres)
int  XR_SetPanelImage(const unsigned char *bgra, int w, int h);
void XR_ShowPanel(const float pos[3], const float axis[3][3], float width);
int  XR_PanelReady(void);   // an image was given (and the session that holds it is still there)   // nothing drawn over everything this frame (the beam, the crosshair)
// Crosshair (style 1..5 as [Controllers] Crosshair) at c, r metres in size, in the plane right x up. Cleared likewise.
void XR_SetReticle(int style, const float c[3], const float right[3], const float up[3], float r, const float rgba[4]);
