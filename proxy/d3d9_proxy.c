// d3d9_proxy: the d3d9.dll next to CoD2SP_s.exe. The game and its renderer (gfx_d3d_x86_s.dll)
// import Direct3DCreate9 from "d3d9.dll", so this file is loaded instead of the system one.
//
// The headset images are D3D11 textures (OpenXR has no D3D9 binding) shared with the game's
// device, which only a D3D9Ex device can open. So Direct3DCreate9 hands the game an IDirect3D9Ex
// and CreateDevice makes a D3D9Ex device. D3D9Ex has no managed pool: managed resources are made
// in the default pool, textures as dynamic so the game can still lock them (D3D9Ex devices are
// never lost, so nothing has to be restored after a Reset).
//
// Hooks are vtable patches on the system's objects (one device per process).
#define _CRT_SECURE_NO_WARNINGS
#include "cod2_vr.h"
#include "controls.h"
#include <stdio.h>

typedef IDirect3D9 *(WINAPI *PFN_Direct3DCreate9)(UINT);
typedef HRESULT (WINAPI *PFN_Direct3DCreate9Ex)(UINT, IDirect3D9Ex **);

static HMODULE               g_sys;
static PFN_Direct3DCreate9   real_Create9;
static PFN_Direct3DCreate9Ex real_Create9Ex;
static FARPROC               real_perf[7];
static const char           *perfNames[7] = { "D3DPERF_BeginEvent", "D3DPERF_EndEvent", "D3DPERF_SetMarker",
                                              "D3DPERF_SetRegion", "D3DPERF_QueryRepeatFrame",
                                              "D3DPERF_SetOptions", "D3DPERF_GetStatus" };

IDirect3DDevice9Ex *g_dev;
IDirect3D9Ex       *g_d3d;
UINT                g_adapter;
int                 g_useEx = 1;     // [Proxy] D3D9Ex in cod2_vr.ini (0 = plain passthrough, no headset)

static void Patch(void **slot, void *hook, void **orig)
{
    DWORD old;
    if (*slot == hook) return;
    if (orig && !*orig) *orig = *slot;
    VirtualProtect(slot, sizeof(void *), PAGE_EXECUTE_READWRITE, &old);
    *slot = hook;
    VirtualProtect(slot, sizeof(void *), old, &old);
}

// ---------------------------------------------------------------------------
// Device: managed pool -> default pool
// ---------------------------------------------------------------------------
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateTexture)(IDirect3DDevice9 *, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9 **, HANDLE *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateVolumeTexture)(IDirect3DDevice9 *, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9 **, HANDLE *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateCubeTexture)(IDirect3DDevice9 *, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture9 **, HANDLE *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateVertexBuffer)(IDirect3DDevice9 *, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9 **, HANDLE *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateIndexBuffer)(IDirect3DDevice9 *, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9 **, HANDLE *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Reset)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_DevPresent)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_ScPresent)(IDirect3DSwapChain9 *, const RECT *, const RECT *, HWND, const RGNDATA *, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateDevice)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetRenderState)(IDirect3DDevice9 *, D3DRENDERSTATETYPE, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetViewport)(IDirect3DDevice9 *, const D3DVIEWPORT9 *);

static PFN_CreateTexture       o_CreateTexture;
static PFN_CreateVolumeTexture o_CreateVolumeTexture;
static PFN_CreateCubeTexture   o_CreateCubeTexture;
static PFN_CreateVertexBuffer  o_CreateVertexBuffer;
static PFN_CreateIndexBuffer   o_CreateIndexBuffer;
static PFN_Reset               o_Reset;
static PFN_DevPresent          o_DevPresent;
static PFN_ScPresent           o_ScPresent;
static PFN_CreateDevice        o_CreateDevice;
static PFN_SetRenderState      o_SetRenderState;
static PFN_SetViewport         o_SetViewport;
static int                     g_convFails;

static DWORD TexUsage(DWORD usage) { return (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) ? usage : usage | D3DUSAGE_DYNAMIC; }

static void ConvFailed(const char *what, HRESULT hr, UINT w, UINT h, DWORD usage, D3DFORMAT fmt)
{
    if (g_convFails++ < 20) Log("%s %ux%u usage %08x format %d in the default pool failed (%08x)", what, w, h, usage, fmt, hr);
}

static HRESULT STDMETHODCALLTYPE hk_CreateTexture(IDirect3DDevice9 *d, UINT w, UINT h, UINT levels, DWORD usage,
                                                  D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9 **out, HANDLE *share)
{
    HRESULT hr;
    if (pool != D3DPOOL_MANAGED) return o_CreateTexture(d, w, h, levels, usage, fmt, pool, out, share);
    hr = o_CreateTexture(d, w, h, levels, TexUsage(usage), fmt, D3DPOOL_DEFAULT, out, share);
    if (FAILED(hr)) ConvFailed("texture", hr, w, h, usage, fmt);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hk_CreateVolumeTexture(IDirect3DDevice9 *d, UINT w, UINT h, UINT depth, UINT levels, DWORD usage,
                                                        D3DFORMAT fmt, D3DPOOL pool, IDirect3DVolumeTexture9 **out, HANDLE *share)
{
    HRESULT hr;
    if (pool != D3DPOOL_MANAGED) return o_CreateVolumeTexture(d, w, h, depth, levels, usage, fmt, pool, out, share);
    hr = o_CreateVolumeTexture(d, w, h, depth, levels, TexUsage(usage), fmt, D3DPOOL_DEFAULT, out, share);
    if (FAILED(hr)) ConvFailed("volume texture", hr, w, h, usage, fmt);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hk_CreateCubeTexture(IDirect3DDevice9 *d, UINT size, UINT levels, DWORD usage,
                                                      D3DFORMAT fmt, D3DPOOL pool, IDirect3DCubeTexture9 **out, HANDLE *share)
{
    HRESULT hr;
    if (pool != D3DPOOL_MANAGED) return o_CreateCubeTexture(d, size, levels, usage, fmt, pool, out, share);
    hr = o_CreateCubeTexture(d, size, levels, TexUsage(usage), fmt, D3DPOOL_DEFAULT, out, share);
    if (FAILED(hr)) ConvFailed("cube texture", hr, size, size, usage, fmt);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hk_CreateVertexBuffer(IDirect3DDevice9 *d, UINT len, DWORD usage, DWORD fvf, D3DPOOL pool,
                                                       IDirect3DVertexBuffer9 **out, HANDLE *share)
{
    HRESULT hr;
    if (pool != D3DPOOL_MANAGED) return o_CreateVertexBuffer(d, len, usage, fvf, pool, out, share);
    hr = o_CreateVertexBuffer(d, len, usage, fvf, D3DPOOL_DEFAULT, out, share);
    if (FAILED(hr)) ConvFailed("vertex buffer", hr, len, 1, usage, 0);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hk_CreateIndexBuffer(IDirect3DDevice9 *d, UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool,
                                                      IDirect3DIndexBuffer9 **out, HANDLE *share)
{
    HRESULT hr;
    if (pool != D3DPOOL_MANAGED) return o_CreateIndexBuffer(d, len, usage, fmt, pool, out, share);
    hr = o_CreateIndexBuffer(d, len, usage, fmt, D3DPOOL_DEFAULT, out, share);
    if (FAILED(hr)) ConvFailed("index buffer", hr, len, 1, usage, fmt);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hk_Reset(IDirect3DDevice9 *d, D3DPRESENT_PARAMETERS *pp)
{
    HRESULT hr = o_Reset(d, pp);
    Log("Reset %ux%u windowed %d: %08x", pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed, hr);
    if (SUCCEEDED(hr)) VR_DeviceReset(pp);
    return hr;
}

// The whole present runs on the mod's big stack (VR_RunOnBigStack): the mod's frame work, and the game's present with the
// driver and whatever else hooks it (the Steam overlay, recording and FPS overlays). The game's threads have 64 KB of stack.
static struct { IDirect3DDevice9 *d; IDirect3DSwapChain9 *sc; const RECT *s, *dst; HWND w; const RGNDATA *r; DWORD f; HRESULT hr; } g_pr;

static void DevPresentWork(void)
{
    VR_BeforePresent();
    g_pr.hr = o_DevPresent(g_pr.d, g_pr.s, g_pr.dst, g_pr.w, g_pr.r);
    VR_AfterPresent();
}

static void ScPresentWork(void)
{
    VR_BeforePresent();
    g_pr.hr = o_ScPresent(g_pr.sc, g_pr.s, g_pr.dst, g_pr.w, g_pr.r, g_pr.f);
    VR_AfterPresent();
}

static HRESULT STDMETHODCALLTYPE hk_DevPresent(IDirect3DDevice9 *d, const RECT *s, const RECT *dst, HWND w, const RGNDATA *r)
{
    g_pr.d = d; g_pr.s = s; g_pr.dst = dst; g_pr.w = w; g_pr.r = r;
    VR_RunOnBigStack(DevPresentWork);
    return g_pr.hr;
}

static HRESULT STDMETHODCALLTYPE hk_ScPresent(IDirect3DSwapChain9 *sc, const RECT *s, const RECT *dst, HWND w, const RGNDATA *r, DWORD f)
{
    g_pr.sc = sc; g_pr.s = s; g_pr.dst = dst; g_pr.w = w; g_pr.r = r; g_pr.f = f;
    VR_RunOnBigStack(ScPresentWork);
    return g_pr.hr;
}

// The gun drawn mirrored (a left-handed player's: Proxy_SetMirrorGun). A mirrored model's triangles wind the other way, so
// while the backend draws the view weapon its cull mode is flipped. The view weapon is what the renderer draws with the depth
// range 0 .. 0.2 (its RB_SetDepthRange 0x1003ec50, for entities with render flag 0x08 - the first-person weapon - sets the
// viewport's MaxZ to 0.2 and calls SetViewport), so the viewport tells when that pass starts and ends.
static int   g_mirrorGun, g_gunPass;
static DWORD g_cullAsked = D3DCULL_CCW;   // what the renderer last set
static IDirect3DDevice9 *g_cullDev;

static DWORD FlipCull(DWORD c) { return c == D3DCULL_CW ? D3DCULL_CCW : c == D3DCULL_CCW ? D3DCULL_CW : c; }

static HRESULT STDMETHODCALLTYPE hk_SetRenderState(IDirect3DDevice9 *d, D3DRENDERSTATETYPE s, DWORD v)
{
    if (s == D3DRS_CULLMODE) {
        g_cullAsked = v;
        g_cullDev = d;
        if (g_mirrorGun && g_gunPass) v = FlipCull(v);
    }
    return o_SetRenderState(d, s, v);
}

static HRESULT STDMETHODCALLTYPE hk_SetViewport(IDirect3DDevice9 *d, const D3DVIEWPORT9 *vp)
{
    int gun = vp && vp->MinZ == 0.0f && vp->MaxZ > 0.15f && vp->MaxZ < 0.25f;
    HRESULT hr = o_SetViewport(d, vp);
    g_cullDev = d;
    if (gun != g_gunPass) {
        g_gunPass = gun;
        if (g_mirrorGun) o_SetRenderState(d, D3DRS_CULLMODE, gun ? FlipCull(g_cullAsked) : g_cullAsked);   // the pass's cull, at once
    }
    return hr;
}

void Proxy_SetMirrorGun(int on)
{
    if (on == g_mirrorGun) return;
    g_mirrorGun = on;
    if (g_gunPass && g_cullDev) o_SetRenderState(g_cullDev, D3DRS_CULLMODE, on ? FlipCull(g_cullAsked) : g_cullAsked);
}

void Proxy_HookSwapChain(IDirect3DSwapChain9 *sc)
{
    Patch((void **)&sc->lpVtbl->Present, (void *)hk_ScPresent, (void **)&o_ScPresent);
}

static void HookDevice(IDirect3DDevice9Ex *dev)
{
    IDirect3DDevice9ExVtbl *v = dev->lpVtbl;
    IDirect3DSwapChain9 *sc = NULL;
    Patch((void **)&v->CreateTexture, (void *)hk_CreateTexture, (void **)&o_CreateTexture);
    Patch((void **)&v->CreateVolumeTexture, (void *)hk_CreateVolumeTexture, (void **)&o_CreateVolumeTexture);
    Patch((void **)&v->CreateCubeTexture, (void *)hk_CreateCubeTexture, (void **)&o_CreateCubeTexture);
    Patch((void **)&v->CreateVertexBuffer, (void *)hk_CreateVertexBuffer, (void **)&o_CreateVertexBuffer);
    Patch((void **)&v->CreateIndexBuffer, (void *)hk_CreateIndexBuffer, (void **)&o_CreateIndexBuffer);
    Patch((void **)&v->Reset, (void *)hk_Reset, (void **)&o_Reset);
    Patch((void **)&v->Present, (void *)hk_DevPresent, (void **)&o_DevPresent);
    Patch((void **)&v->SetRenderState, (void *)hk_SetRenderState, (void **)&o_SetRenderState);
    Patch((void **)&v->SetViewport, (void *)hk_SetViewport, (void **)&o_SetViewport);
    // RB_SwapBuffers presents through the swap chain (GetSwapChain(0) -> Present)
    if (SUCCEEDED(IDirect3DDevice9Ex_GetSwapChain(dev, 0, &sc)) && sc) {
        Proxy_HookSwapChain(sc);
        IDirect3DSwapChain9_Release(sc);
    }
}

static HRESULT STDMETHODCALLTYPE hk_CreateDevice(IDirect3D9 *self, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                                 D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
    IDirect3DDevice9Ex *dev = NULL;
    D3DDISPLAYMODEEX mode = { sizeof(mode) };
    HRESULT hr;

    Log("CreateDevice adapter %u %ux%u windowed %d format %d flags %08x", adapter, pp->BackBufferWidth,
        pp->BackBufferHeight, pp->Windowed, pp->BackBufferFormat, flags);
    if (!pp->Windowed) {
        mode.Width = pp->BackBufferWidth;
        mode.Height = pp->BackBufferHeight;
        mode.RefreshRate = pp->FullScreen_RefreshRateInHz;
        mode.Format = pp->BackBufferFormat;
        mode.ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
    }
    hr = IDirect3D9Ex_CreateDeviceEx((IDirect3D9Ex *)self, adapter, type, focus, flags, pp, pp->Windowed ? NULL : &mode, &dev);
    Log("CreateDeviceEx: %08x", hr);
    if (FAILED(hr)) return hr;
    g_dev = dev;
    g_adapter = adapter;
    Controls_SetWindow(pp->hDeviceWindow ? pp->hDeviceWindow : focus);
    HookDevice(dev);
    *out = (IDirect3DDevice9 *)dev;
    VR_DeviceCreated(dev, pp);
    return hr;
}

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------
static int LoadSystemD3D9(void)
{
    char path[MAX_PATH];
    int i;
    if (g_sys) return 1;
    GetSystemDirectoryA(path, MAX_PATH);   // SysWOW64 for this 32-bit process
    strcat(path, "\\d3d9.dll");
    g_sys = LoadLibraryA(path);
    if (!g_sys) return 0;
    real_Create9 = (PFN_Direct3DCreate9)GetProcAddress(g_sys, "Direct3DCreate9");
    real_Create9Ex = (PFN_Direct3DCreate9Ex)GetProcAddress(g_sys, "Direct3DCreate9Ex");
    for (i = 0; i < 7; i++) real_perf[i] = GetProcAddress(g_sys, perfNames[i]);
    return real_Create9 != NULL;
}

IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk)
{
    IDirect3D9Ex *ex = NULL;
    if (!LoadSystemD3D9()) return NULL;
    if (g_useEx && real_Create9Ex && SUCCEEDED(real_Create9Ex(sdk, &ex)) && ex) {
        Log("Direct3DCreate9(%u): D3D9Ex", sdk);
        g_d3d = ex;
        Patch((void **)&ex->lpVtbl->CreateDevice, (void *)hk_CreateDevice, (void **)&o_CreateDevice);
        return (IDirect3D9 *)ex;
    }
    Log("Direct3DCreate9(%u): plain D3D9 (no headset possible)", sdk);
    return real_Create9(sdk);
}

HRESULT WINAPI Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex **out)
{
    if (!LoadSystemD3D9() || !real_Create9Ex) return D3DERR_NOTAVAILABLE;
    return real_Create9Ex(sdk, out);
}

int  WINAPI D3DPERF_BeginEvent(D3DCOLOR c, LPCWSTR n) { return real_perf[0] ? ((int (WINAPI *)(D3DCOLOR, LPCWSTR))real_perf[0])(c, n) : 0; }
int  WINAPI D3DPERF_EndEvent(void) { return real_perf[1] ? ((int (WINAPI *)(void))real_perf[1])() : 0; }
void WINAPI D3DPERF_SetMarker(D3DCOLOR c, LPCWSTR n) { if (real_perf[2]) ((void (WINAPI *)(D3DCOLOR, LPCWSTR))real_perf[2])(c, n); }
void WINAPI D3DPERF_SetRegion(D3DCOLOR c, LPCWSTR n) { if (real_perf[3]) ((void (WINAPI *)(D3DCOLOR, LPCWSTR))real_perf[3])(c, n); }
BOOL WINAPI D3DPERF_QueryRepeatFrame(void) { return real_perf[4] ? ((BOOL (WINAPI *)(void))real_perf[4])() : FALSE; }
void WINAPI D3DPERF_SetOptions(DWORD o) { if (real_perf[5]) ((void (WINAPI *)(DWORD))real_perf[5])(o); }
DWORD WINAPI D3DPERF_GetStatus(void) { return real_perf[6] ? ((DWORD (WINAPI *)(void))real_perf[6])() : 0; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        VR_ProcessAttach(inst);
    }
    return TRUE;
}
