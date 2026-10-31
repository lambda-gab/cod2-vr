// Image effects on the eye images (postfx.c): sharpening, bloom, a colour style.
#pragma once
#include "cod2_vr.h"
#include <d3d11.h>

int  PostFx_Init(ID3D11Device *dev, ID3D11DeviceContext *ctx);
int  PostFx_Active(const vr_postfx_t *p);
// src: the eye's shared texture (gamma-encoded B8G8R8A8), dst: the eye's swapchain image of dstFormat, w x h
int  PostFx_Apply(ID3D11Texture2D *src, ID3D11Texture2D *dst, DXGI_FORMAT dstFormat, int w, int h, const vr_postfx_t *p);
