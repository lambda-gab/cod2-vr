// overlay: lines drawn over everything in the headset (the menu pointer now; the grenade
// trajectory and the crosshair later), in an extra projection layer of its own.
//
// D3D11, one swapchain per eye. The geometry is tiny, so it is projected on the CPU with each
// eye's pose and fov: the shaders only pass clip-space positions and colours through. They are
// compiled at run time with d3dcompiler_47.dll (part of Windows 10 and 11).
#define _CRT_SECURE_NO_WARNINGS
#include "overlay.h"
#include <d3dcompiler.h>
#include <math.h>
#include <string.h>

typedef HRESULT (WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR,
                                         UINT, UINT, ID3DBlob **, ID3DBlob **);

static const char g_hlsl[] =
    "struct V { float4 pos : POSITION; float4 col : COLOR; };\n"
    "struct P { float4 pos : SV_POSITION; float4 col : COLOR; };\n"
    "P vs(V v) { P p; p.pos = v.pos; p.col = v.col; return p; }\n"
    "float4 ps(P p) : SV_TARGET { return p.col; }\n";

typedef struct { float pos[4], col[4]; } vtx_t;
#define MAX_VERTS 1536

static struct {
    int ready, failed;
    ID3D11Device        *dev;
    ID3D11DeviceContext *ctx;
    ID3D11VertexShader  *vs;
    ID3D11PixelShader   *ps;
    ID3D11InputLayout   *layout;
    ID3D11Buffer        *vb;
    ID3D11BlendState    *blend;
    ID3D11RasterizerState *raster;
    ID3D11DepthStencilState *depth;
} ov;

// what to draw this frame: segments (tracking space, XR axes, metres)
static struct { float a[3], b[3], width, col[4]; } g_seg[64];
static int   g_nseg;
static struct { float p[3], size, col[4]; } g_dot[8];
static int   g_ndot;

void Overlay_Clear(void) { g_nseg = g_ndot = 0; }
int  Overlay_Empty(void) { return g_nseg == 0 && g_ndot == 0; }

void Overlay_Line(const float a[3], const float b[3], float width, const float rgba[4])
{
    if (g_nseg >= 64) return;
    memcpy(g_seg[g_nseg].a, a, 12);
    memcpy(g_seg[g_nseg].b, b, 12);
    g_seg[g_nseg].width = width;
    memcpy(g_seg[g_nseg].col, rgba, 16);
    g_nseg++;
}

void Overlay_Dot(const float p[3], float size, const float rgba[4])
{
    if (g_ndot >= 8) return;
    memcpy(g_dot[g_ndot].p, p, 12);
    g_dot[g_ndot].size = size;
    memcpy(g_dot[g_ndot].col, rgba, 16);
    g_ndot++;
}

int Overlay_Init(ID3D11Device *dev, ID3D11DeviceContext *ctx)
{
    HMODULE comp;
    PFN_D3DCompile compile;
    ID3DBlob *vsb = NULL, *psb = NULL, *err = NULL;
    D3D11_INPUT_ELEMENT_DESC el[2] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    D3D11_BUFFER_DESC bd = { 0 };
    D3D11_BLEND_DESC bl = { 0 };
    D3D11_RASTERIZER_DESC rs = { 0 };
    D3D11_DEPTH_STENCIL_DESC ds = { 0 };

    if (ov.ready) return 1;
    if (ov.failed) return 0;
    ov.failed = 1;
    comp = LoadLibraryA("d3dcompiler_47.dll");
    compile = comp ? (PFN_D3DCompile)GetProcAddress(comp, "D3DCompile") : NULL;
    if (!compile) { Log("overlay: d3dcompiler_47.dll not found, no pointer line"); return 0; }
    if (FAILED(compile(g_hlsl, sizeof(g_hlsl) - 1, "overlay", NULL, NULL, "vs", "vs_4_0", 0, 0, &vsb, &err)) ||
        FAILED(compile(g_hlsl, sizeof(g_hlsl) - 1, "overlay", NULL, NULL, "ps", "ps_4_0", 0, 0, &psb, &err))) {
        Log("overlay: shader compile failed: %s", err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        return 0;
    }
    ov.dev = dev;
    ov.ctx = ctx;
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), NULL, &ov.vs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb), ID3D10Blob_GetBufferSize(psb), NULL, &ov.ps);
    ID3D11Device_CreateInputLayout(dev, el, 2, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), &ov.layout);
    ID3D10Blob_Release(vsb);
    ID3D10Blob_Release(psb);
    bd.ByteWidth = sizeof(vtx_t) * MAX_VERTS;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Device_CreateBuffer(dev, &bd, NULL, &ov.vb);
    bl.RenderTarget[0].BlendEnable = TRUE;   // premultiplied
    bl.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11Device_CreateBlendState(dev, &bl, &ov.blend);
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    rs.DepthClipEnable = FALSE;
    ID3D11Device_CreateRasterizerState(dev, &rs, &ov.raster);
    ds.DepthEnable = FALSE;
    ID3D11Device_CreateDepthStencilState(dev, &ds, &ov.depth);
    if (!ov.vs || !ov.ps || !ov.layout || !ov.vb || !ov.blend || !ov.raster || !ov.depth) { Log("overlay: D3D11 objects failed"); return 0; }
    ov.ready = 1;
    ov.failed = 0;
    return 1;
}

// ---------------------------------------------------------------------------
// Projection on the CPU
// ---------------------------------------------------------------------------
typedef struct {
    float pos[3], right[3], up[3], back[3];   // eye pose: position and axes (XR: x right, y up, z back)
    float l, r, u, d;                         // fov tangents
} eye_t;

static void QuatAxes(const XrQuaternionf *q, float right[3], float up[3], float back[3])
{
    float x = q->x, y = q->y, z = q->z, w = q->w;
    right[0] = 1 - 2 * (y * y + z * z); right[1] = 2 * (x * y + w * z);     right[2] = 2 * (x * z - w * y);
    up[0] = 2 * (x * y - w * z);        up[1] = 1 - 2 * (x * x + z * z);    up[2] = 2 * (y * z + w * x);
    back[0] = 2 * (x * z + w * y);      back[1] = 2 * (y * z - w * x);      back[2] = 1 - 2 * (x * x + y * y);
}

static float Dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// eye space (x right, y up, f forward > 0)
static void ToEye(const eye_t *e, const float p[3], float o[3])
{
    float v[3] = { p[0] - e->pos[0], p[1] - e->pos[1], p[2] - e->pos[2] };
    o[0] = Dot3(v, e->right);
    o[1] = Dot3(v, e->up);
    o[2] = -Dot3(v, e->back);
}

static int Clip(const eye_t *e, const float v[3], vtx_t *out, const float col[4])
{
    float w = v[2];
    if (w < 0.02f) return 0;
    out->pos[0] = (2 * v[0] / w - (e->r + e->l)) / (e->r - e->l) * w;
    out->pos[1] = (2 * v[1] / w - (e->u + e->d)) / (e->u - e->d) * w;
    out->pos[2] = 0.5f * w;
    out->pos[3] = w;
    memcpy(out->col, col, 16);
    return 1;
}

// a quad from 4 eye-space corners (a0 a1 b1 b0), as two triangles
static int Quad(const eye_t *e, const float c[4][3], const float col[4], vtx_t *o)
{
    vtx_t v[4];
    int k;
    for (k = 0; k < 4; k++) if (!Clip(e, c[k], &v[k], col)) return 0;
    o[0] = v[0]; o[1] = v[1]; o[2] = v[2];
    o[3] = v[0]; o[4] = v[2]; o[5] = v[3];
    return 6;
}

static int Build(const eye_t *e, vtx_t *o)
{
    int n = 0, i, k;
    for (i = 0; i < g_nseg && n + 6 <= MAX_VERTS; i++) {
        float a[3], b[3], d[3], s[3], len, c[4][3];
        ToEye(e, g_seg[i].a, a);
        ToEye(e, g_seg[i].b, b);
        if (a[2] < 0.02f && b[2] < 0.02f) continue;
        if (a[2] < 0.02f || b[2] < 0.02f) {   // cut at the near plane
            float t = (0.02f - a[2]) / (b[2] - a[2]);
            float *behind = a[2] < 0.02f ? a : b;
            for (k = 0; k < 3; k++) behind[k] = a[k] + (b[k] - a[k]) * t;
            behind[2] = 0.02f;
        }
        for (k = 0; k < 3; k++) d[k] = b[k] - a[k];
        // across the line and across the view: d x (midpoint)
        s[0] = d[1] * (a[2] + b[2]) - d[2] * (a[1] + b[1]);
        s[1] = d[2] * (a[0] + b[0]) - d[0] * (a[2] + b[2]);
        s[2] = d[0] * (a[1] + b[1]) - d[1] * (a[0] + b[0]);
        len = sqrtf(Dot3(s, s));
        if (len < 1e-6f) continue;
        for (k = 0; k < 3; k++) s[k] *= 0.5f * g_seg[i].width / len;
        for (k = 0; k < 3; k++) {
            c[0][k] = a[k] - s[k]; c[1][k] = a[k] + s[k];
            c[2][k] = b[k] + s[k]; c[3][k] = b[k] - s[k];
        }
        n += Quad(e, c, g_seg[i].col, o + n);
    }
    for (i = 0; i < g_ndot && n + 6 <= MAX_VERTS; i++) {   // a square facing the eye
        float p[3], h = g_dot[i].size * 0.5f, c[4][3];
        ToEye(e, g_dot[i].p, p);
        c[0][0] = p[0] - h; c[0][1] = p[1] - h; c[0][2] = p[2];
        c[1][0] = p[0] + h; c[1][1] = p[1] - h; c[1][2] = p[2];
        c[2][0] = p[0] + h; c[2][1] = p[1] + h; c[2][2] = p[2];
        c[3][0] = p[0] - h; c[3][1] = p[1] + h; c[3][2] = p[2];
        n += Quad(e, c, g_dot[i].col, o + n);
    }
    return n;
}

void Overlay_Draw(ID3D11Texture2D *target, DXGI_FORMAT format, int w, int h, const XrPosef *pose, const XrFovf *fov)
{
    D3D11_RENDER_TARGET_VIEW_DESC rd = { 0 };
    ID3D11RenderTargetView *rtv = NULL;
    D3D11_MAPPED_SUBRESOURCE map;
    D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
    static const float clear[4] = { 0, 0, 0, 0 };
    UINT stride = sizeof(vtx_t), offset = 0;
    eye_t e;
    int n = 0;

    if (!ov.ready) return;
    rd.Format = format;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(ID3D11Device_CreateRenderTargetView(ov.dev, (ID3D11Resource *)target, &rd, &rtv))) return;
    ID3D11DeviceContext_ClearRenderTargetView(ov.ctx, rtv, clear);

    e.pos[0] = pose->position.x; e.pos[1] = pose->position.y; e.pos[2] = pose->position.z;
    QuatAxes(&pose->orientation, e.right, e.up, e.back);
    e.l = tanf(fov->angleLeft); e.r = tanf(fov->angleRight);
    e.u = tanf(fov->angleUp);   e.d = tanf(fov->angleDown);
    if (SUCCEEDED(ID3D11DeviceContext_Map(ov.ctx, (ID3D11Resource *)ov.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        n = Build(&e, (vtx_t *)map.pData);
        ID3D11DeviceContext_Unmap(ov.ctx, (ID3D11Resource *)ov.vb, 0);
    }
    if (n > 0) {
        ID3D11DeviceContext_OMSetRenderTargets(ov.ctx, 1, &rtv, NULL);
        ID3D11DeviceContext_OMSetBlendState(ov.ctx, ov.blend, NULL, 0xffffffff);
        ID3D11DeviceContext_OMSetDepthStencilState(ov.ctx, ov.depth, 0);
        ID3D11DeviceContext_RSSetState(ov.ctx, ov.raster);
        ID3D11DeviceContext_RSSetViewports(ov.ctx, 1, &vp);
        ID3D11DeviceContext_IASetInputLayout(ov.ctx, ov.layout);
        ID3D11DeviceContext_IASetPrimitiveTopology(ov.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_IASetVertexBuffers(ov.ctx, 0, 1, &ov.vb, &stride, &offset);
        ID3D11DeviceContext_VSSetShader(ov.ctx, ov.vs, NULL, 0);
        ID3D11DeviceContext_PSSetShader(ov.ctx, ov.ps, NULL, 0);
        ID3D11DeviceContext_Draw(ov.ctx, n, 0);
        ID3D11DeviceContext_OMSetRenderTargets(ov.ctx, 0, NULL, NULL);
    }
    ID3D11RenderTargetView_Release(rtv);
}
