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
void VR_OnCreateCmd(void);       // controls.c: a usercmd is being made (CL_FinishMove)

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
} vr_hud_t;

int    XR_Init(LUID adapterLuid, int hudW, int hudH);   // instance, D3D11 device, session, swapchains
void   XR_SetRenderScale(float s);
int    XR_EyeSize(int *w, int *h);
void   XR_RecommendedSize(int *w, int *h, int *maxW, int *maxH);
void   XR_Shutdown(void);
int    XR_Ready(void);
int    XR_TryAgain(void);           // 1 = no headset yet, worth retrying XR_Init later
HANDLE XR_EyeShareHandle(int eye);   // shared B8G8R8A8 textures the game renders into (D3D9Ex opens them)
HANDLE XR_HudShareHandle(void);
int    XR_HudSize(int *w, int *h);
int    XR_ResizeHud(int w, int h);    // window size changed: new HUD texture (new share handle)
int    XR_BeginFrame(vr_pose_t eyes[2], vr_pose_t *head);   // 1 = this frame goes to the headset
void   XR_EndFrame(const vr_hud_t *hud);   // copies the shared textures to the swapchains and submits
int    XR_FrameOpen(void);

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
