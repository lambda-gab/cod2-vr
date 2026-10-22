// Lines over everything in the headset (overlay.c), drawn into an extra projection layer.
#pragma once
#include "cod2_vr.h"
#include <d3d11.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

int  Overlay_Init(ID3D11Device *dev, ID3D11DeviceContext *ctx);
void Overlay_Clear(void);
int  Overlay_Empty(void);
// tracking space in OpenXR axes, metres; colours premultiplied by alpha
void Overlay_Line(const float a[3], const float b[3], float width, const float rgba[4]);
void Overlay_Dot(const float p[3], float size, const float rgba[4]);
void Overlay_Draw(ID3D11Texture2D *target, DXGI_FORMAT format, int w, int h, const XrPosef *pose, const XrFovf *fov);
