// postfx: image effects on the eye images in the headset (as the CoD 1 mod's postfx.c, there in GLSL):
//   * bloom: the bright parts, at a quarter of the size, blurred and added back softly;
//   * sharpening: contrast-adaptive (the idea of AMD's CAS), which gives the old textures their definition back
//     without halos;
//   * a colour style: saturation, contrast and a tint.
// D3D11, from the eye's shared texture (what the game rendered, gamma-encoded B8G8R8A8) into the eye's swapchain image,
// instead of the plain copy. The shaders are compiled at run time with d3dcompiler_47.dll, like the overlay's; if they
// cannot be built, the plain copy is used. Ambient occlusion needs the depth, which stays on the D3D9 side: not here.
#define _CRT_SECURE_NO_WARNINGS
#include "postfx.h"
#include <d3dcompiler.h>
#include <string.h>

typedef HRESULT (WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR,
                                         UINT, UINT, ID3DBlob **, ID3DBlob **);

// a triangle over the whole target from the vertex id (no vertex buffer)
static const char g_hlsl[] =
    "Texture2D img : register(t0); Texture2D bloomTex : register(t1); SamplerState smp : register(s0);\n"
    "cbuffer P : register(b0) { float4 a; float4 b; float4 tint; float4 dir; };\n"
    // a: texel.xy, sharp, bloomAmt; b: contrast, sat, threshold, srgbOut
    "struct V { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) { V o; float2 t = float2((id << 1) & 2, id & 2); o.uv = t; o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
    "float3 bright(float2 p) { float3 c = img.Sample(smp, p).rgb; float l = max(c.r, max(c.g, c.b));\n"
    "  return c * (max(l - b.z, 0.0) / max(1.0 - b.z, 0.001)) / max(l, 0.001); }\n"
    "float4 psBright(V v) : SV_TARGET { float2 t = a.xy;\n"
    "  float3 c = bright(v.uv + t * float2(-1, -1)) + bright(v.uv + t * float2(1, -1)) + bright(v.uv + t * float2(-1, 1)) + bright(v.uv + t * float2(1, 1));\n"
    "  return float4(c * 0.25, 1); }\n"
    "float4 psBlur(V v) : SV_TARGET { float3 c = img.Sample(smp, v.uv).rgb * 0.2270;\n"
    "  c += (img.Sample(smp, v.uv + dir.xy * 1.3846).rgb + img.Sample(smp, v.uv - dir.xy * 1.3846).rgb) * 0.3162;\n"
    "  c += (img.Sample(smp, v.uv + dir.xy * 3.2308).rgb + img.Sample(smp, v.uv - dir.xy * 3.2308).rgb) * 0.0703;\n"
    "  return float4(c, 1); }\n"
    "float4 psFinal(V v) : SV_TARGET { float3 c = img.Sample(smp, v.uv).rgb;\n"
    "  if (a.z > 0) {\n"
    "    float3 n = img.Sample(smp, v.uv + float2(0, a.y)).rgb, s = img.Sample(smp, v.uv - float2(0, a.y)).rgb;\n"
    "    float3 e = img.Sample(smp, v.uv + float2(a.x, 0)).rgb, w = img.Sample(smp, v.uv - float2(a.x, 0)).rgb;\n"
    "    float3 mn = min(c, min(min(n, s), min(e, w))), mx = max(c, max(max(n, s), max(e, w)));\n"
    "    float3 amp = sqrt(saturate(min(mn, 2.0 - mx) / max(mx, 0.0001)));\n"
    "    float3 wt = amp * (-1.0 / lerp(8.0, 5.0, a.z));\n"
    "    c = saturate((c + (n + s + e + w) * wt) / (1.0 + 4.0 * wt)); }\n"
    "  if (a.w > 0) c += bloomTex.Sample(smp, v.uv).rgb * a.w;\n"
    "  float l = dot(c, float3(0.2126, 0.7152, 0.0722));\n"
    "  c = lerp(float3(l, l, l), c, b.y);\n"
    "  c = (c - 0.5) * b.x + 0.5;\n"
    "  c = saturate(c * tint.rgb);\n"
    // an sRGB target encodes what is written: the game's gamma-encoded colours are decoded first, so they arrive unchanged
    "  if (b.w > 0) c = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);\n"
    "  return float4(c, 1); }\n";

typedef struct { float a[4], b[4], tint[4], dir[4]; } consts_t;

static struct {
    int ready, failed;
    ID3D11Device *dev;
    ID3D11DeviceContext *ctx;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *bright, *blur, *final;
    ID3D11Buffer *cb;
    ID3D11SamplerState *smp;
    ID3D11RasterizerState *raster;
    ID3D11DepthStencilState *depth;
    ID3D11BlendState *noBlend;
    ID3D11Texture2D *q[2];             // the quarter-size bloom images
    ID3D11ShaderResourceView *qSrv[2];
    ID3D11RenderTargetView *qRtv[2];
    int qW, qH;
} fx;

static ID3DBlob *CompileBlob(PFN_D3DCompile compile, const char *entry, const char *target)
{
    ID3DBlob *code = NULL, *err = NULL;
    if (FAILED(compile(g_hlsl, sizeof(g_hlsl) - 1, "postfx", NULL, NULL, entry, target, 0, 0, &code, &err))) {
        Log("postfx: shader %s did not compile: %s", entry, err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        if (err) ID3D10Blob_Release(err);
        return NULL;
    }
    return code;
}

static ID3D11PixelShader *CompilePS(PFN_D3DCompile compile, const char *entry)
{
    ID3DBlob *code = CompileBlob(compile, entry, "ps_4_0");
    ID3D11PixelShader *ps = NULL;
    if (!code) return NULL;
    ID3D11Device_CreatePixelShader(fx.dev, ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code), NULL, &ps);
    ID3D10Blob_Release(code);
    return ps;
}

int PostFx_Init(ID3D11Device *dev, ID3D11DeviceContext *ctx)
{
    HMODULE comp;
    PFN_D3DCompile compile;
    ID3DBlob *vsb = NULL;
    D3D11_BUFFER_DESC bd = { 0 };
    D3D11_SAMPLER_DESC sd = { 0 };
    D3D11_RASTERIZER_DESC rs = { 0 };
    D3D11_DEPTH_STENCIL_DESC ds = { 0 };
    D3D11_BLEND_DESC bl = { 0 };
    if (fx.ready) return 1;
    if (fx.failed) return 0;
    fx.failed = 1;
    fx.dev = dev;
    fx.ctx = ctx;
    comp = LoadLibraryA("d3dcompiler_47.dll");
    compile = comp ? (PFN_D3DCompile)GetProcAddress(comp, "D3DCompile") : NULL;
    if (!compile) { Log("postfx: d3dcompiler_47.dll not found, no image effects"); return 0; }
    if (!(vsb = CompileBlob(compile, "vs", "vs_4_0"))) return 0;
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), NULL, &fx.vs);
    ID3D10Blob_Release(vsb);
    fx.bright = CompilePS(compile, "psBright");
    fx.blur = CompilePS(compile, "psBlur");
    fx.final = CompilePS(compile, "psFinal");
    bd.ByteWidth = sizeof(consts_t);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Device_CreateBuffer(dev, &bd, NULL, &fx.cb);
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11Device_CreateSamplerState(dev, &sd, &fx.smp);
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    ID3D11Device_CreateRasterizerState(dev, &rs, &fx.raster);
    ds.DepthEnable = FALSE;
    ID3D11Device_CreateDepthStencilState(dev, &ds, &fx.depth);
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11Device_CreateBlendState(dev, &bl, &fx.noBlend);
    if (!fx.vs || !fx.bright || !fx.blur || !fx.final || !fx.cb || !fx.smp || !fx.raster || !fx.depth || !fx.noBlend) {
        Log("postfx: D3D11 objects failed, no image effects");
        return 0;
    }
    fx.ready = 1;
    fx.failed = 0;
    Log("postfx: ready");
    return 1;
}

int PostFx_Active(const vr_postfx_t *p)
{
    return p && (p->sharpen > 0.001f || p->bloom > 0.001f || p->contrast != 1.0f || p->saturation != 1.0f ||
                 p->tint[0] != 1.0f || p->tint[1] != 1.0f || p->tint[2] != 1.0f);
}

static int EnsureQuarter(int w, int h)
{
    int i;
    w = w / 4 > 1 ? w / 4 : 1;
    h = h / 4 > 1 ? h / 4 : 1;
    if (fx.q[0] && fx.qW == w && fx.qH == h) return 1;
    for (i = 0; i < 2; i++) {
        D3D11_TEXTURE2D_DESC d = { 0 };
        if (fx.qSrv[i]) ID3D11ShaderResourceView_Release(fx.qSrv[i]);
        if (fx.qRtv[i]) ID3D11RenderTargetView_Release(fx.qRtv[i]);
        if (fx.q[i]) ID3D11Texture2D_Release(fx.q[i]);
        fx.q[i] = NULL; fx.qSrv[i] = NULL; fx.qRtv[i] = NULL;
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(ID3D11Device_CreateTexture2D(fx.dev, &d, NULL, &fx.q[i]))) return 0;
        ID3D11Device_CreateShaderResourceView(fx.dev, (ID3D11Resource *)fx.q[i], NULL, &fx.qSrv[i]);
        ID3D11Device_CreateRenderTargetView(fx.dev, (ID3D11Resource *)fx.q[i], NULL, &fx.qRtv[i]);
        if (!fx.qSrv[i] || !fx.qRtv[i]) return 0;
    }
    fx.qW = w;
    fx.qH = h;
    return 1;
}

static void Pass(ID3D11RenderTargetView *rtv, int w, int h, ID3D11PixelShader *ps, ID3D11ShaderResourceView *t0,
                 ID3D11ShaderResourceView *t1, const consts_t *c)
{
    D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11ShaderResourceView *srv[2], *none[2] = { NULL, NULL };
    srv[0] = t0;
    srv[1] = t1;
    if (SUCCEEDED(ID3D11DeviceContext_Map(fx.ctx, (ID3D11Resource *)fx.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, c, sizeof(*c));
        ID3D11DeviceContext_Unmap(fx.ctx, (ID3D11Resource *)fx.cb, 0);
    }
    ID3D11DeviceContext_OMSetRenderTargets(fx.ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_OMSetBlendState(fx.ctx, fx.noBlend, NULL, 0xffffffff);
    ID3D11DeviceContext_OMSetDepthStencilState(fx.ctx, fx.depth, 0);
    ID3D11DeviceContext_RSSetState(fx.ctx, fx.raster);
    ID3D11DeviceContext_RSSetViewports(fx.ctx, 1, &vp);
    ID3D11DeviceContext_IASetInputLayout(fx.ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(fx.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(fx.ctx, fx.vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(fx.ctx, ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(fx.ctx, 0, 1, &fx.cb);
    ID3D11DeviceContext_PSSetSamplers(fx.ctx, 0, 1, &fx.smp);
    ID3D11DeviceContext_PSSetShaderResources(fx.ctx, 0, 2, srv);
    ID3D11DeviceContext_Draw(fx.ctx, 3, 0);
    ID3D11DeviceContext_PSSetShaderResources(fx.ctx, 0, 2, none);
    ID3D11DeviceContext_OMSetRenderTargets(fx.ctx, 0, NULL, NULL);
}

static int IsSrgb(DXGI_FORMAT f)
{
    return f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
}

int PostFx_Apply(ID3D11Texture2D *src, ID3D11Texture2D *dst, DXGI_FORMAT dstFormat, int w, int h, const vr_postfx_t *p)
{
    D3D11_RENDER_TARGET_VIEW_DESC rd = { 0 };
    ID3D11ShaderResourceView *srcSrv = NULL;
    ID3D11RenderTargetView *dstRtv = NULL;
    consts_t c;
    int bloom = p->bloom > 0.001f && EnsureQuarter(w, h);
    if (!fx.ready || w <= 0 || h <= 0) return 0;
    if (FAILED(ID3D11Device_CreateShaderResourceView(fx.dev, (ID3D11Resource *)src, NULL, &srcSrv))) return 0;
    rd.Format = dstFormat;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(ID3D11Device_CreateRenderTargetView(fx.dev, (ID3D11Resource *)dst, &rd, &dstRtv))) {
        ID3D11ShaderResourceView_Release(srcSrv);
        return 0;
    }
    memset(&c, 0, sizeof(c));
    c.b[2] = p->bloomThreshold;
    if (bloom) {
        // bright parts into the quarter image, then blurred across and down (into the other one and back)
        c.a[0] = 1.0f / w; c.a[1] = 1.0f / h;
        Pass(fx.qRtv[0], fx.qW, fx.qH, fx.bright, srcSrv, NULL, &c);
        c.dir[0] = 1.0f / fx.qW; c.dir[1] = 0;
        Pass(fx.qRtv[1], fx.qW, fx.qH, fx.blur, fx.qSrv[0], NULL, &c);
        c.dir[0] = 0; c.dir[1] = 1.0f / fx.qH;
        Pass(fx.qRtv[0], fx.qW, fx.qH, fx.blur, fx.qSrv[1], NULL, &c);
    }
    c.a[0] = 1.0f / w; c.a[1] = 1.0f / h;
    c.a[2] = p->sharpen;
    c.a[3] = bloom ? p->bloom : 0.0f;
    c.b[0] = p->contrast;
    c.b[1] = p->saturation;
    c.b[3] = IsSrgb(dstFormat) ? 1.0f : 0.0f;
    c.tint[0] = p->tint[0]; c.tint[1] = p->tint[1]; c.tint[2] = p->tint[2]; c.tint[3] = 1;
    Pass(dstRtv, w, h, fx.final, srcSrv, bloom ? fx.qSrv[0] : NULL, &c);
    ID3D11RenderTargetView_Release(dstRtv);
    ID3D11ShaderResourceView_Release(srcSrv);
    return 1;
}
