// OpenXR backend for cod2_vr: XR_KHR_D3D11_enable on a D3D11 device of its own (OpenXR has no
// D3D9 binding). The game renders each eye and the HUD into D3D11 textures created as shared
// resources and opened by the game's D3D9Ex device; at the end of the frame they are copied
// into the swapchain images and submitted.
// The loader (openxr_loader.dll, Win32 build) is loaded at runtime from the game folder, so a
// missing loader only disables VR.
#define _CRT_SECURE_NO_WARNINGS
#include "cod2_vr.h"
#include "overlay.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <dxgi.h>

static PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr;
#define XR_FUNCS(X) \
    X(xrDestroyInstance) X(xrGetSystem) X(xrGetInstanceProperties) X(xrGetD3D11GraphicsRequirementsKHR) \
    X(xrCreateSession) X(xrDestroySession) X(xrBeginSession) X(xrEndSession) X(xrPollEvent) \
    X(xrCreateReferenceSpace) X(xrDestroySpace) X(xrEnumerateViewConfigurationViews) \
    X(xrEnumerateSwapchainFormats) X(xrCreateSwapchain) X(xrDestroySwapchain) X(xrEnumerateSwapchainImages) \
    X(xrAcquireSwapchainImage) X(xrWaitSwapchainImage) X(xrReleaseSwapchainImage) \
    X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) X(xrLocateViews) X(xrLocateSpace) X(xrResultToString) \
    X(xrStringToPath) X(xrCreateActionSet) X(xrCreateAction) X(xrSuggestInteractionProfileBindings) \
    X(xrAttachSessionActionSets) X(xrCreateActionSpace) X(xrSyncActions) X(xrGetActionStateFloat) \
    X(xrGetActionStateBoolean) X(xrGetActionStateVector2f) X(xrApplyHapticFeedback)
#define X(name) static PFN_##name name;
XR_FUNCS(X)
#undef X
static PFN_xrCreateInstance xrCreateInstance_;

#define MAX_IMAGES 8
#define SC_EYE0 0
#define SC_EYE1 1
#define SC_HUD  2
#define SC_OVL0 3   // lines over everything, per eye
#define SC_OVL1 4
#define SC_COUNT 5

static struct {
    int        failed;
    DWORD      retryAt;
    int        warnedNoHmd;
    XrInstance instance;
    XrSystemId system;
    XrSession  session;
    XrSpace    local, view;
    XrSessionState state;
    int        running;
    XrSwapchain swapchain[SC_COUNT];
    int        scW[SC_COUNT], scH[SC_COUNT];
    uint32_t   imageCount[SC_COUNT];
    XrSwapchainImageD3D11KHR images[SC_COUNT][MAX_IMAGES];
    int        renderW, renderH;
    int        hudW, hudH;
    DXGI_FORMAT format;          // of every swapchain
    ID3D11Device        *dev;
    ID3D11DeviceContext *ctx;
    ID3D11Texture2D     *shared[SC_COUNT];   // what the game renders into
    HANDLE               shareHandle[SC_COUNT];
    // current frame
    int        frameOpen;
    XrFrameState frameState;
    XrView     views[2];
    int        viewsValid;
    // controllers
    XrActionSet actionSet;
    XrAction   aPose, aTrigger, aGrip, aStick, aStickClick, aA, aB, aX, aY, aMenu, aHaptic;
    XrPath     handPath[2];
    XrSpace    handSpace[2];
} xr;

static float xr_renderScale = 1.0f;
static int   xr_recW, xr_recH, xr_maxW, xr_maxH;

static const char *ResultStr(XrResult r)
{
    static char buf[XR_MAX_RESULT_STRING_SIZE];
    if (xrResultToString && xr.instance && xrResultToString(xr.instance, r, buf) == XR_SUCCESS) return buf;
    sprintf(buf, "XrResult %d", r);
    return buf;
}

#define CHECK(call) do { XrResult r_ = (call); if (XR_FAILED(r_)) { Log("OpenXR: %s failed: %s", #call, ResultStr(r_)); return 0; } } while (0)

static int LoadLoader(void)
{
    char path[MAX_PATH];
    HMODULE dll;
    _snprintf(path, sizeof(path), "%s\\openxr_loader.dll", g_dir);
    dll = LoadLibraryA(path);
    if (!dll) { Log("OpenXR: %s not found (error %lu)", path, GetLastError()); return 0; }
    xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)GetProcAddress(dll, "xrGetInstanceProcAddr");
    if (!xrGetInstanceProcAddr) return 0;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&xrCreateInstance_);
    return xrCreateInstance_ != NULL;
}

static int CreateInstance(void)
{
    const char *ext[] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME };
    XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
    XrInstanceProperties ip = { XR_TYPE_INSTANCE_PROPERTIES };

    strcpy(ci.applicationInfo.applicationName, "Call of Duty 2 VR");
    strcpy(ci.applicationInfo.engineName, "cod2_vr");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = ext;
    CHECK(xrCreateInstance_(&ci, &xr.instance));

#define X(name) xrGetInstanceProcAddr(xr.instance, #name, (PFN_xrVoidFunction *)&name);
    XR_FUNCS(X)
#undef X
    if (xrGetInstanceProperties(xr.instance, &ip) == XR_SUCCESS)
        Log("OpenXR runtime: %s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
            XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));
    return 1;
}

static int GetSystem(void)
{
    XrSystemGetInfo si = { XR_TYPE_SYSTEM_GET_INFO };
    XrResult r;
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(xr.instance, &si, &xr.system);
    if (XR_SUCCEEDED(r)) return 1;
    if (!xr.warnedNoHmd) { Log("OpenXR: no headset yet (%s), retrying every 2 s", ResultStr(r)); xr.warnedNoHmd = 1; }
    xr.retryAt = GetTickCount() + 2000;
    return 0;
}

// The D3D11 device must be on the adapter the runtime asks for (the game's D3D9 device should be
// too, or the shared textures cannot be opened).
static int CreateD3D11(LUID want, LUID game)
{
    typedef HRESULT (WINAPI *PFN_CreateFactory1)(REFIID, void **);
    PFN_CreateFactory1 createFactory;
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL, *pick = NULL;
    D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    HMODULE dxgi = LoadLibraryA("dxgi.dll"), d3d11 = LoadLibraryA("d3d11.dll");
    PFN_D3D11_CREATE_DEVICE create;
    HRESULT hr;
    UINT i;

    if (!dxgi || !d3d11) { Log("OpenXR: dxgi/d3d11 not available"); return 0; }
    createFactory = (PFN_CreateFactory1)GetProcAddress(dxgi, "CreateDXGIFactory1");
    create = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(d3d11, "D3D11CreateDevice");
    if (!createFactory || !create || FAILED(createFactory(&IID_IDXGIFactory1, (void **)&factory))) return 0;
    for (i = 0; IDXGIFactory1_EnumAdapters1(factory, i, &adapter) == S_OK; i++) {
        DXGI_ADAPTER_DESC1 d;
        IDXGIAdapter1_GetDesc1(adapter, &d);
        if (d.AdapterLuid.LowPart == want.LowPart && d.AdapterLuid.HighPart == want.HighPart) {
            char name[128];
            WideCharToMultiByte(CP_ACP, 0, d.Description, -1, name, sizeof(name), NULL, NULL);
            Log("OpenXR: headset adapter %s", name);
            pick = adapter;
            break;
        }
        IDXGIAdapter1_Release(adapter);
    }
    IDXGIFactory1_Release(factory);
    if (want.LowPart != game.LowPart || want.HighPart != game.HighPart)
        Log("OpenXR: WARNING: the game runs on another adapter than the headset's; the images cannot be shared");
    hr = create((IDXGIAdapter *)pick, pick ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL, 0, fl, 3,
                D3D11_SDK_VERSION, &xr.dev, NULL, &xr.ctx);
    if (pick) IDXGIAdapter1_Release(pick);
    if (FAILED(hr)) { Log("OpenXR: D3D11CreateDevice failed %08x", hr); return 0; }
    return 1;
}

static int64_t PickFormat(void)
{
    int64_t formats[64];
    uint32_t n = 0, i;
    if (XR_FAILED(xrEnumerateSwapchainFormats(xr.session, 64, &n, formats))) return 0;
    // The game writes gamma-encoded colours: an sRGB swapchain shows them unchanged. The copy
    // from the shared B8G8R8A8 textures needs the same channel order.
    for (i = 0; i < n; i++) if (formats[i] == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) return formats[i];
    for (i = 0; i < n; i++) if (formats[i] == DXGI_FORMAT_B8G8R8A8_UNORM) return formats[i];
    Log("OpenXR: the runtime offers no B8G8R8A8 swapchain format (%u formats, first %lld)", n, n ? formats[0] : 0);
    return 0;
}

static int CreateSwapchain(int i, int64_t format, int w, int h)
{
    XrSwapchainCreateInfo sc = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    uint32_t k;
    sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    sc.format = format;
    sc.sampleCount = 1;
    sc.width = w;
    sc.height = h;
    sc.faceCount = 1;
    sc.arraySize = 1;
    sc.mipCount = 1;
    CHECK(xrCreateSwapchain(xr.session, &sc, &xr.swapchain[i]));
    xr.scW[i] = w;
    xr.scH[i] = h;
    for (k = 0; k < MAX_IMAGES; k++) xr.images[i][k].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
    CHECK(xrEnumerateSwapchainImages(xr.swapchain[i], MAX_IMAGES, &xr.imageCount[i], (XrSwapchainImageBaseHeader *)xr.images[i]));
    Log("OpenXR: %s swapchain %dx%d, %u images", i >= SC_OVL0 ? "overlay" : i == SC_HUD ? "HUD" : i ? "right eye" : "left eye", w, h, xr.imageCount[i]);
    return 1;
}

static int CreateShared(int i, int w, int h)
{
    D3D11_TEXTURE2D_DESC d = { 0 };
    IDXGIResource *res = NULL;
    if (xr.shared[i]) { ID3D11Texture2D_Release(xr.shared[i]); xr.shared[i] = NULL; xr.shareHandle[i] = NULL; }
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;   // D3DFMT_A8R8G8B8 on the D3D9 side
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    if (FAILED(ID3D11Device_CreateTexture2D(xr.dev, &d, NULL, &xr.shared[i]))) { Log("OpenXR: shared texture %dx%d failed", w, h); return 0; }
    if (SUCCEEDED(ID3D11Texture2D_QueryInterface(xr.shared[i], &IID_IDXGIResource, (void **)&res))) {
        IDXGIResource_GetSharedHandle(res, &xr.shareHandle[i]);
        IDXGIResource_Release(res);
    }
    return xr.shareHandle[i] != NULL;
}

static XrPath Path(const char *s)
{
    XrPath p = XR_NULL_PATH;
    xrStringToPath(xr.instance, s, &p);
    return p;
}

static int CreateAction(XrAction *out, const char *name, const char *localized, XrActionType type, int perHand)
{
    XrActionCreateInfo ai = { XR_TYPE_ACTION_CREATE_INFO };
    strcpy(ai.actionName, name);
    strcpy(ai.localizedActionName, localized);
    ai.actionType = type;
    if (perHand) { ai.countSubactionPaths = 2; ai.subactionPaths = xr.handPath; }
    CHECK(xrCreateAction(xr.actionSet, &ai, out));
    return 1;
}

typedef struct { XrAction *action; const char *path; } binding_t;

static void Suggest(const char *profile, const binding_t *b, int n)
{
    XrActionSuggestedBinding sb[32];
    XrInteractionProfileSuggestedBinding ip = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    XrResult r;
    int i;
    for (i = 0; i < n; i++) { sb[i].action = *b[i].action; sb[i].binding = Path(b[i].path); }
    ip.interactionProfile = Path(profile);
    ip.suggestedBindings = sb;
    ip.countSuggestedBindings = n;
    r = xrSuggestInteractionProfileBindings(xr.instance, &ip);
    if (XR_FAILED(r)) Log("OpenXR: bindings for %s rejected: %s", profile, ResultStr(r));
}

static int CreateActions(void)
{
    XrActionSetCreateInfo si = { XR_TYPE_ACTION_SET_CREATE_INFO };
    static const binding_t touch[] = {
        { &xr.aPose, "/user/hand/left/input/aim/pose" },          { &xr.aPose, "/user/hand/right/input/aim/pose" },
        { &xr.aTrigger, "/user/hand/left/input/trigger/value" },  { &xr.aTrigger, "/user/hand/right/input/trigger/value" },
        { &xr.aGrip, "/user/hand/left/input/squeeze/value" },     { &xr.aGrip, "/user/hand/right/input/squeeze/value" },
        { &xr.aStick, "/user/hand/left/input/thumbstick" },       { &xr.aStick, "/user/hand/right/input/thumbstick" },
        { &xr.aStickClick, "/user/hand/left/input/thumbstick/click" }, { &xr.aStickClick, "/user/hand/right/input/thumbstick/click" },
        { &xr.aA, "/user/hand/right/input/a/click" },             { &xr.aB, "/user/hand/right/input/b/click" },
        { &xr.aX, "/user/hand/left/input/x/click" },              { &xr.aY, "/user/hand/left/input/y/click" },
        { &xr.aMenu, "/user/hand/left/input/menu/click" },
        { &xr.aHaptic, "/user/hand/left/output/haptic" },         { &xr.aHaptic, "/user/hand/right/output/haptic" },
    };
    static const binding_t simple[] = {
        { &xr.aPose, "/user/hand/left/input/aim/pose" },          { &xr.aPose, "/user/hand/right/input/aim/pose" },
        { &xr.aTrigger, "/user/hand/left/input/select/click" },   { &xr.aTrigger, "/user/hand/right/input/select/click" },
        { &xr.aMenu, "/user/hand/left/input/menu/click" },
        { &xr.aHaptic, "/user/hand/left/output/haptic" },         { &xr.aHaptic, "/user/hand/right/output/haptic" },
    };

    if (xr.actionSet) return 1;
    xr.handPath[0] = Path("/user/hand/left");
    xr.handPath[1] = Path("/user/hand/right");
    strcpy(si.actionSetName, "gameplay");
    strcpy(si.localizedActionSetName, "Gameplay");
    CHECK(xrCreateActionSet(xr.instance, &si, &xr.actionSet));
    if (!CreateAction(&xr.aPose, "aim", "Aim", XR_ACTION_TYPE_POSE_INPUT, 1) ||
        !CreateAction(&xr.aTrigger, "trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, 1) ||
        !CreateAction(&xr.aGrip, "grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, 1) ||
        !CreateAction(&xr.aStick, "thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, 1) ||
        !CreateAction(&xr.aStickClick, "thumbstick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT, 1) ||
        !CreateAction(&xr.aA, "button_a", "A", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
        !CreateAction(&xr.aB, "button_b", "B", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
        !CreateAction(&xr.aX, "button_x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
        !CreateAction(&xr.aY, "button_y", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
        !CreateAction(&xr.aMenu, "menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
        !CreateAction(&xr.aHaptic, "haptic", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, 1))
        return 0;
    Suggest("/interaction_profiles/oculus/touch_controller", touch, sizeof(touch) / sizeof(touch[0]));
    Suggest("/interaction_profiles/khr/simple_controller", simple, sizeof(simple) / sizeof(simple[0]));
    return 1;
}

static void AttachActions(void)
{
    XrSessionActionSetsAttachInfo ai = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    XrResult r;
    int i;
    if (!xr.actionSet) return;
    ai.countActionSets = 1;
    ai.actionSets = &xr.actionSet;
    r = xrAttachSessionActionSets(xr.session, &ai);
    if (XR_FAILED(r)) { Log("OpenXR: controllers unavailable: %s", ResultStr(r)); return; }
    for (i = 0; i < 2; i++) {
        XrActionSpaceCreateInfo sp = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        sp.action = xr.aPose;
        sp.subactionPath = xr.handPath[i];
        sp.poseInActionSpace.orientation.w = 1;
        xrCreateActionSpace(xr.session, &sp, &xr.handSpace[i]);
    }
    Log("OpenXR: controller actions attached");
}

static int CreateSession(LUID gameLuid)
{
    XrGraphicsRequirementsD3D11KHR req = { XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
    XrGraphicsBindingD3D11KHR binding = { XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    XrReferenceSpaceCreateInfo rs = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    XrViewConfigurationView vcv[2] = { { XR_TYPE_VIEW_CONFIGURATION_VIEW }, { XR_TYPE_VIEW_CONFIGURATION_VIEW } };
    uint32_t n = 0;
    int64_t format;
    int i;

    CHECK(xrGetD3D11GraphicsRequirementsKHR(xr.instance, xr.system, &req));
    if (!xr.dev && !CreateD3D11(req.adapterLuid, gameLuid)) return 0;
    binding.device = xr.dev;
    sci.next = &binding;
    sci.systemId = xr.system;
    CHECK(xrCreateSession(xr.instance, &sci, &xr.session));

    rs.poseInReferenceSpace.orientation.w = 1;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    CHECK(xrCreateReferenceSpace(xr.session, &rs, &xr.local));
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    CHECK(xrCreateReferenceSpace(xr.session, &rs, &xr.view));
    AttachActions();

    CHECK(xrEnumerateViewConfigurationViews(xr.instance, xr.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &n, vcv));
    format = PickFormat();
    if (!format) return 0;
    Log("OpenXR: swapchain format %lld", format);
    xr.format = (DXGI_FORMAT)format;

    xr_recW = vcv[0].recommendedImageRectWidth; xr_recH = vcv[0].recommendedImageRectHeight;
    xr_maxW = vcv[0].maxImageRectWidth; xr_maxH = vcv[0].maxImageRectHeight;
    // Eyes rendered (and sent) at the recommended size times RenderScale, within the maximum.
    xr.renderW = (int)(xr_recW * xr_renderScale + 0.5f);
    xr.renderH = (int)(xr_recH * xr_renderScale + 0.5f);
    if (xr.renderW > xr_maxW) xr.renderW = xr_maxW;
    if (xr.renderH > xr_maxH) xr.renderH = xr_maxH;
    for (i = 0; i < 2; i++) {
        if (!CreateSwapchain(i, format, xr.renderW, xr.renderH)) return 0;
        if (!CreateShared(i, xr.renderW, xr.renderH)) return 0;
    }
    Log("OpenXR: eyes %dx%d (recommended %dx%d, RenderScale %.2f)", xr.renderW, xr.renderH, xr_recW, xr_recH, xr_renderScale);
    if (!CreateSwapchain(SC_HUD, format, xr.hudW, xr.hudH) || !CreateShared(SC_HUD, xr.hudW, xr.hudH)) return 0;
    // the overlay is optional: without it there is no pointer line, nothing else changes
    if (Overlay_Init(xr.dev, xr.ctx) &&
        (!CreateSwapchain(SC_OVL0, format, xr.renderW, xr.renderH) || !CreateSwapchain(SC_OVL1, format, xr.renderW, xr.renderH)))
        Log("OpenXR: no overlay swapchains");
    return 1;
}

void XR_SetRenderScale(float s) { xr_renderScale = s < 0.5f ? 0.5f : s > 2.0f ? 2.0f : s; }

void XR_RecommendedSize(int *w, int *h, int *maxW, int *maxH)
{
    *w = xr_recW; *h = xr_recH; *maxW = xr_maxW; *maxH = xr_maxH;
}

int XR_EyeSize(int *w, int *h)
{
    if (!xr.session || !xr.renderW) return 0;
    *w = xr.renderW;
    *h = xr.renderH;
    return 1;
}

int XR_HudSize(int *w, int *h)
{
    if (!xr.session) return 0;
    *w = xr.hudW;
    *h = xr.hudH;
    return 1;
}

HANDLE XR_EyeShareHandle(int eye) { return xr.shareHandle[eye ? SC_EYE1 : SC_EYE0]; }
HANDLE XR_HudShareHandle(void) { return xr.shareHandle[SC_HUD]; }

int XR_ResizeHud(int w, int h)
{
    int64_t format;
    if (!xr.session) return 0;
    if (w == xr.hudW && h == xr.hudH) return 1;
    if (xr.frameOpen) XR_EndFrame(NULL);
    if (xr.swapchain[SC_HUD]) { xrDestroySwapchain(xr.swapchain[SC_HUD]); xr.swapchain[SC_HUD] = XR_NULL_HANDLE; }
    xr.hudW = w;
    xr.hudH = h;
    format = PickFormat();
    return format && CreateSwapchain(SC_HUD, format, w, h) && CreateShared(SC_HUD, w, h);
}

int XR_TryAgain(void) { return xr.retryAt != 0; }

int XR_Init(LUID adapterLuid, int hudW, int hudH)
{
    if (xr.session) return 1;
    xr.hudW = hudW;
    xr.hudH = hudH;
    if (xr.retryAt && (int)(GetTickCount() - xr.retryAt) < 0) return 0;
    if (xr.failed && !xr.retryAt) return 0;
    xr.failed = 1;
    xr.retryAt = 0;
    if (!xrGetInstanceProcAddr && !LoadLoader()) return 0;
    if (!xr.instance && !CreateInstance()) return 0;
    if (!xr.system && !GetSystem()) return 0;
    if (!CreateActions()) Log("OpenXR: controller actions could not be created");
    if (!CreateSession(adapterLuid)) { XR_Shutdown(); xr.failed = 1; return 0; }
    xr.failed = 0;
    Log("OpenXR: session created, waiting for the headset");
    return 1;
}

int XR_Ready(void) { return xr.session != XR_NULL_HANDLE; }
int XR_FrameOpen(void) { return xr.frameOpen; }

void XR_Shutdown(void)
{
    int i;
    if (xr.session) {
        if (xr.running) xrEndSession(xr.session);
        for (i = 0; i < SC_COUNT; i++) if (xr.swapchain[i]) xrDestroySwapchain(xr.swapchain[i]);
        for (i = 0; i < 2; i++) if (xr.handSpace[i]) xrDestroySpace(xr.handSpace[i]);
        if (xr.local) xrDestroySpace(xr.local);
        if (xr.view) xrDestroySpace(xr.view);
        xrDestroySession(xr.session);
        Log("OpenXR: session destroyed");
    }
    for (i = 0; i < SC_COUNT; i++) if (xr.shared[i]) ID3D11Texture2D_Release(xr.shared[i]);
    {
        XrInstance keep = xr.instance;
        XrSystemId sys = xr.system;
        XrActionSet set = xr.actionSet;
        XrAction actions[11];
        XrPath hands[2];
        ID3D11Device *dev = xr.dev;
        ID3D11DeviceContext *ctx = xr.ctx;
        DWORD retry = xr.retryAt;
        memcpy(actions, &xr.aPose, sizeof(actions));
        memcpy(hands, xr.handPath, sizeof(hands));
        memset(&xr, 0, sizeof(xr));
        xr.instance = keep;
        xr.system = sys;
        xr.actionSet = set;
        xr.dev = dev;
        xr.ctx = ctx;
        xr.retryAt = retry;
        memcpy(&xr.aPose, actions, sizeof(actions));
        memcpy(xr.handPath, hands, sizeof(hands));
    }
}

static void PollEvents(void)
{
    XrEventDataBuffer ev;
    for (;;) {
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        ev.next = NULL;
        if (xrPollEvent(xr.instance, &ev) != XR_SUCCESS) break;
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            XrEventDataSessionStateChanged *sc = (XrEventDataSessionStateChanged *)&ev;
            xr.state = sc->state;
            Log("OpenXR: session state %d", sc->state);
            if (sc->state == XR_SESSION_STATE_READY && !xr.running) {
                XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (XR_SUCCEEDED(xrBeginSession(xr.session, &bi))) { xr.running = 1; Log("OpenXR: session running"); }
            } else if (sc->state == XR_SESSION_STATE_STOPPING && xr.running) {
                xrEndSession(xr.session);
                xr.running = 0;
            } else if (sc->state == XR_SESSION_STATE_EXITING || sc->state == XR_SESSION_STATE_LOSS_PENDING) {
                xr.running = 0;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            Log("OpenXR: instance loss pending");
            xr.running = 0;
        }
    }
}

// XR (x right, y up, -z forward) -> Quake 3 (x forward, y left, z up)
static void ToQ3(const float v[3], float out[3]) { out[0] = -v[2]; out[1] = -v[0]; out[2] = v[1]; }
static void FromQ3(const float v[3], float out[3]) { out[0] = -v[1]; out[1] = v[2]; out[2] = -v[0]; }

static void Rotate(const XrQuaternionf *q, const float v[3], float out[3])
{
    float t[3], c[3];
    t[0] = q->y * v[2] - q->z * v[1] + q->w * v[0];
    t[1] = q->z * v[0] - q->x * v[2] + q->w * v[1];
    t[2] = q->x * v[1] - q->y * v[0] + q->w * v[2];
    c[0] = q->y * t[2] - q->z * t[1];
    c[1] = q->z * t[0] - q->x * t[2];
    c[2] = q->x * t[1] - q->y * t[0];
    out[0] = v[0] + 2 * c[0]; out[1] = v[1] + 2 * c[1]; out[2] = v[2] + 2 * c[2];
}

static void PoseToQ3(const XrPosef *p, vr_pose_t *out)
{
    static const float fwd[3] = { 0, 0, -1 }, left[3] = { -1, 0, 0 }, up[3] = { 0, 1, 0 };
    float pos[3] = { p->position.x, p->position.y, p->position.z }, t[3];
    ToQ3(pos, out->pos);
    Rotate(&p->orientation, fwd, t);  ToQ3(t, out->axis[0]);
    Rotate(&p->orientation, left, t); ToQ3(t, out->axis[1]);
    Rotate(&p->orientation, up, t);   ToQ3(t, out->axis[2]);
}

int XR_BeginFrame(vr_pose_t eyes[2], vr_pose_t *head)
{
    XrFrameWaitInfo wi = { XR_TYPE_FRAME_WAIT_INFO };
    XrFrameBeginInfo bi = { XR_TYPE_FRAME_BEGIN_INFO };
    XrViewLocateInfo li = { XR_TYPE_VIEW_LOCATE_INFO };
    XrViewState vs = { XR_TYPE_VIEW_STATE };
    XrSpaceLocation hl = { XR_TYPE_SPACE_LOCATION };
    uint32_t n = 0;
    int i;

    if (!xr.session) return 0;
    if (xr.frameOpen) XR_EndFrame(NULL);
    Overlay_Clear();
    PollEvents();
    if (!xr.running) return 0;

    xr.frameState.type = XR_TYPE_FRAME_STATE;
    xr.frameState.next = NULL;
    if (XR_FAILED(xrWaitFrame(xr.session, &wi, &xr.frameState))) return 0;
    if (XR_FAILED(xrBeginFrame(xr.session, &bi))) return 0;
    xr.frameOpen = 1;

    xr.views[0].type = xr.views[1].type = XR_TYPE_VIEW;
    xr.views[0].next = xr.views[1].next = NULL;
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = xr.frameState.predictedDisplayTime;
    li.space = xr.local;
    if (XR_FAILED(xrLocateViews(xr.session, &li, &vs, 2, &n, xr.views)) ||
        !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        xr.viewsValid = 0;
        return 0;
    }
    xr.viewsValid = 1;
    {
        static int logged;
        if (!logged) {
            logged = 1;
            for (i = 0; i < 2; i++)
                Log("OpenXR: %s eye fov left %.1f right %.1f up %.1f down %.1f deg", i ? "right" : "left",
                    xr.views[i].fov.angleLeft * 57.29578f, xr.views[i].fov.angleRight * 57.29578f,
                    xr.views[i].fov.angleUp * 57.29578f, xr.views[i].fov.angleDown * 57.29578f);
        }
    }
    for (i = 0; i < 2; i++) {
        PoseToQ3(&xr.views[i].pose, &eyes[i]);
        eyes[i].tanLeft  = tanf(xr.views[i].fov.angleLeft);
        eyes[i].tanRight = tanf(xr.views[i].fov.angleRight);
        eyes[i].tanUp    = tanf(xr.views[i].fov.angleUp);
        eyes[i].tanDown  = tanf(xr.views[i].fov.angleDown);
    }
    if (XR_SUCCEEDED(xrLocateSpace(xr.view, xr.local, xr.frameState.predictedDisplayTime, &hl)) &&
        (hl.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        PoseToQ3(&hl.pose, head);
    } else {
        *head = eyes[0];
    }
    return xr.frameState.shouldRender ? 1 : 0;
}

static int CopyToSwapchain(int i)
{
    XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    uint32_t index;
    if (!xr.swapchain[i] || !xr.shared[i]) return 0;
    if (XR_FAILED(xrAcquireSwapchainImage(xr.swapchain[i], &ai, &index))) return 0;
    wi.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(xr.swapchain[i], &wi))) return 0;
    ID3D11DeviceContext_CopySubresourceRegion(xr.ctx, (ID3D11Resource *)xr.images[i][index].texture, 0, 0, 0, 0,
                                              (ID3D11Resource *)xr.shared[i], 0, NULL);
    xrReleaseSwapchainImage(xr.swapchain[i], &ri);
    return 1;
}

// The overlay layer: lines drawn into each eye's overlay swapchain image.
static int DrawOverlay(int eye)
{
    XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    int sc = SC_OVL0 + eye;
    uint32_t index;
    if (!xr.swapchain[sc]) return 0;
    if (XR_FAILED(xrAcquireSwapchainImage(xr.swapchain[sc], &ai, &index))) return 0;
    wi.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(xr.swapchain[sc], &wi))) return 0;
    Overlay_Draw(xr.images[sc][index].texture, xr.format, xr.scW[sc], xr.scH[sc], &xr.views[eye].pose, &xr.views[eye].fov);
    xrReleaseSwapchainImage(xr.swapchain[sc], &ri);
    return 1;
}

// Menu pointer: a beam from the hand to where it points (tracking space, Quake axes, metres).
void XR_SetLaser(const float from[3], const float to[3], int hit)
{
    static const float beam[4] = { 0.2f * 0.9f, 0.9f * 0.9f, 1.0f * 0.9f, 0.9f };
    static const float spot[4] = { 1, 1, 1, 1 };
    float a[3], b[3];
    FromQ3(from, a);
    FromQ3(to, b);
    Overlay_Line(a, b, 0.004f, beam);
    if (hit) Overlay_Dot(b, 0.02f, spot);
}

static XrQuaternionf MatrixToQuat(const float c0[3], const float c1[3], const float c2[3])
{
    XrQuaternionf q;
    float tr = c0[0] + c1[1] + c2[2], s;
    if (tr > 0) {
        s = sqrtf(tr + 1.0f) * 2;
        q.w = 0.25f * s; q.x = (c1[2] - c2[1]) / s; q.y = (c2[0] - c0[2]) / s; q.z = (c0[1] - c1[0]) / s;
    } else if (c0[0] > c1[1] && c0[0] > c2[2]) {
        s = sqrtf(1.0f + c0[0] - c1[1] - c2[2]) * 2;
        q.w = (c1[2] - c2[1]) / s; q.x = 0.25f * s; q.y = (c1[0] + c0[1]) / s; q.z = (c2[0] + c0[2]) / s;
    } else if (c1[1] > c2[2]) {
        s = sqrtf(1.0f + c1[1] - c0[0] - c2[2]) * 2;
        q.w = (c2[0] - c0[2]) / s; q.x = (c1[0] + c0[1]) / s; q.y = 0.25f * s; q.z = (c2[1] + c1[2]) / s;
    } else {
        s = sqrtf(1.0f + c2[2] - c0[0] - c1[1]) * 2;
        q.w = (c0[1] - c1[0]) / s; q.x = (c2[0] + c0[2]) / s; q.y = (c2[1] + c1[2]) / s; q.z = 0.25f * s;
    }
    return q;
}

void XR_EndFrame(const vr_hud_t *hud)
{
    XrFrameEndInfo ei = { XR_TYPE_FRAME_END_INFO };
    XrCompositionLayerProjection proj = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    XrCompositionLayerProjectionView pv[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
    XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    const XrCompositionLayerBaseHeader *layers[3];
    XrCompositionLayerProjection ovl = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    XrCompositionLayerProjectionView ov[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
    int i, n = 0, render;

    if (!xr.frameOpen) return;
    xr.frameOpen = 0;
    render = hud && xr.viewsValid && xr.frameState.shouldRender;

    if (render && hud->world && CopyToSwapchain(SC_EYE0) && CopyToSwapchain(SC_EYE1)) {
        for (i = 0; i < 2; i++) {
            pv[i].pose = xr.views[i].pose;
            pv[i].fov = xr.views[i].fov;
            pv[i].subImage.swapchain = xr.swapchain[i];
            pv[i].subImage.imageRect.extent.width = xr.scW[i];
            pv[i].subImage.imageRect.extent.height = xr.scH[i];
        }
        proj.space = xr.local;
        proj.viewCount = 2;
        proj.views = pv;
        layers[n++] = (const XrCompositionLayerBaseHeader *)&proj;
    }

    // HUD / menus on a flat screen. The game blends its 2D into a transparent texture, so the
    // colours are already multiplied by alpha.
    if (render && CopyToSwapchain(SC_HUD)) {
        float width = hud->width;
        quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        quad.space = hud->headLocked ? xr.view : xr.local;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = xr.swapchain[SC_HUD];
        quad.subImage.imageRect.extent.width = xr.scW[SC_HUD];
        quad.subImage.imageRect.extent.height = xr.scH[SC_HUD];
        if (hud->posed) {
            float right[3], back[3], c0[3], c1[3], c2[3];
            for (i = 0; i < 3; i++) { right[i] = -hud->pose.axis[1][i]; back[i] = -hud->pose.axis[0][i]; }
            FromQ3(right, c0);
            FromQ3(hud->pose.axis[2], c1);
            FromQ3(back, c2);
            FromQ3(hud->pose.pos, &quad.pose.position.x);
            quad.pose.orientation = MatrixToQuat(c0, c1, c2);
            quad.space = xr.local;
        } else {
            quad.pose.orientation.w = 1;
            quad.pose.position.y = hud->offsetY;
            quad.pose.position.z = -hud->distance;
        }
        quad.size.width = width;
        quad.size.height = width * xr.scH[SC_HUD] / xr.scW[SC_HUD];
        layers[n++] = (const XrCompositionLayerBaseHeader *)&quad;
    }
    // lines over everything (menu pointer)
    if (render && !Overlay_Empty() && DrawOverlay(0) && DrawOverlay(1)) {
        for (i = 0; i < 2; i++) {
            ov[i].pose = xr.views[i].pose;
            ov[i].fov = xr.views[i].fov;
            ov[i].subImage.swapchain = xr.swapchain[SC_OVL0 + i];
            ov[i].subImage.imageRect.extent.width = xr.scW[SC_OVL0 + i];
            ov[i].subImage.imageRect.extent.height = xr.scH[SC_OVL0 + i];
        }
        ovl.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        ovl.space = xr.local;
        ovl.viewCount = 2;
        ovl.views = ov;
        layers[n++] = (const XrCompositionLayerBaseHeader *)&ovl;
    }
    if (n) ID3D11DeviceContext_Flush(xr.ctx);

    ei.displayTime = xr.frameState.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = n;
    ei.layers = layers;
    {
        XrResult r = xrEndFrame(xr.session, &ei);
        if (XR_FAILED(r)) Log("OpenXR: xrEndFrame failed: %s", ResultStr(r));
    }
}

void XR_Haptic(int hand, float amplitude, int ms)
{
    XrHapticVibration vib = { XR_TYPE_HAPTIC_VIBRATION };
    XrHapticActionInfo hi = { XR_TYPE_HAPTIC_ACTION_INFO };
    if (!xr.session || !xr.aHaptic || !xrApplyHapticFeedback || hand < 0 || hand > 1 || amplitude <= 0) return;
    vib.duration = (XrDuration)ms * 1000000;
    vib.frequency = XR_FREQUENCY_UNSPECIFIED;
    vib.amplitude = amplitude > 1 ? 1 : amplitude;
    hi.action = xr.aHaptic;
    hi.subactionPath = xr.handPath[hand];
    xrApplyHapticFeedback(xr.session, &hi, (const XrHapticBaseHeader *)&vib);
}

static float GetFloat(XrAction a, int hand)
{
    XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
    XrActionStateFloat st = { XR_TYPE_ACTION_STATE_FLOAT };
    gi.action = a;
    gi.subactionPath = hand >= 0 ? xr.handPath[hand] : XR_NULL_PATH;
    return XR_SUCCEEDED(xrGetActionStateFloat(xr.session, &gi, &st)) && st.isActive ? st.currentState : 0.0f;
}

static int GetBool(XrAction a, int hand)
{
    XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
    XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
    gi.action = a;
    gi.subactionPath = hand >= 0 ? xr.handPath[hand] : XR_NULL_PATH;
    return XR_SUCCEEDED(xrGetActionStateBoolean(xr.session, &gi, &st)) && st.isActive && st.currentState;
}

void XR_GetInput(vr_input_t *in)
{
    XrActiveActionSet active = { 0 };
    XrActionsSyncInfo si = { XR_TYPE_ACTIONS_SYNC_INFO };
    int i;

    memset(in, 0, sizeof(*in));
    if (!xr.session || !xr.running || !xr.actionSet || !xr.handSpace[1] || xr.state != XR_SESSION_STATE_FOCUSED) return;
    active.actionSet = xr.actionSet;
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;
    if (xrSyncActions(xr.session, &si) != XR_SUCCESS) return;
    in->active = 1;

    for (i = 0; i < 2; i++) {
        XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
        XrActionStateVector2f st = { XR_TYPE_ACTION_STATE_VECTOR2F };
        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        const XrSpaceLocationFlags need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;

        if (XR_SUCCEEDED(xrLocateSpace(xr.handSpace[i], xr.local, xr.frameState.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & need) == need) {
            PoseToQ3(&loc.pose, &in->hand[i]);
            in->handValid[i] = 1;
        }
        in->trigger[i] = GetFloat(xr.aTrigger, i);
        in->grip[i] = GetFloat(xr.aGrip, i);
        gi.action = xr.aStick;
        gi.subactionPath = xr.handPath[i];
        if (XR_SUCCEEDED(xrGetActionStateVector2f(xr.session, &gi, &st)) && st.isActive) {
            in->stick[i][0] = st.currentState.x;
            in->stick[i][1] = st.currentState.y;
        }
        if (GetBool(xr.aStickClick, i)) in->buttons |= i ? VRB_RSTICK : VRB_LSTICK;
    }
    if (GetBool(xr.aA, -1)) in->buttons |= VRB_A;
    if (GetBool(xr.aB, -1)) in->buttons |= VRB_B;
    if (GetBool(xr.aX, -1)) in->buttons |= VRB_X;
    if (GetBool(xr.aY, -1)) in->buttons |= VRB_Y;
    if (GetBool(xr.aMenu, -1)) in->buttons |= VRB_MENU;
}
