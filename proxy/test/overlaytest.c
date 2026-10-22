// overlaytest: draws the menu pointer (overlay.c) for one made-up eye into a texture and saves it
// as overlaytest.bmp. Build: build_overlaytest.bat
#define _CRT_SECURE_NO_WARNINGS
#include "../overlay.h"
#include <stdio.h>
#include <math.h>

char g_dir[MAX_PATH] = ".";
void Log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

int main(void)
{
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    ID3D11Texture2D *rt = NULL, *staging = NULL;
    D3D11_TEXTURE2D_DESC d = { 0 };
    D3D11_MAPPED_SUBRESOURCE m;
    XrPosef pose = { { 0, 0, 0, 1 }, { 0, 1.6f, 0 } };   // at 1.6 m, looking down -z
    XrFovf fov = { -0.8f, 0.7f, 0.75f, -0.85f };
    // a beam from the right hand (below, to the right) to a point 1.5 m ahead, with a spot
    float hand[3] = { 0.2f, 1.2f, -0.3f }, hit[3] = { -0.1f, 1.6f, -1.5f };
    float beam[4] = { 0.18f, 0.81f, 0.9f, 0.9f }, spot[4] = { 1, 1, 1, 1 };
    const int W = 512, H = 512;
    int x, y, lit = 0;
    FILE *f;

    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx))) return 1;
    if (!Overlay_Init(dev, ctx)) return 2;
    d.Width = W; d.Height = H; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;   // as the headset's swapchains
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Device_CreateTexture2D(dev, &d, NULL, &rt);
    d.BindFlags = 0; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Device_CreateTexture2D(dev, &d, NULL, &staging);

    Overlay_Clear();
    Overlay_Line(hand, hit, 0.004f, beam);
    Overlay_Dot(hit, 0.012f, spot);
    Overlay_Draw(rt, d.Format, W, H, &pose, &fov);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)rt);
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m))) return 3;
    f = fopen("overlaytest.bmp", "wb");
    {
        BITMAPFILEHEADER fh = { 0x4d42, sizeof(fh) + sizeof(BITMAPINFOHEADER) + W * H * 4, 0, 0, sizeof(fh) + sizeof(BITMAPINFOHEADER) };
        BITMAPINFOHEADER ih = { sizeof(ih), W, -H, 1, 32, BI_RGB };
        fwrite(&fh, sizeof(fh), 1, f);
        fwrite(&ih, sizeof(ih), 1, f);
    }
    for (y = 0; y < H; y++) {
        const BYTE *row = (const BYTE *)m.pData + y * m.RowPitch;
        for (x = 0; x < W; x++) if (row[x * 4 + 3]) lit++;
        fwrite(row, 4, W, f);
    }
    fclose(f);
    printf("pixels drawn: %d\n", lit);
    return lit ? 0 : 4;
}
