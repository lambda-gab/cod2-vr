// Call of Duty 2 single player 1.3 (CoD2SP_s.exe, PE timestamp 0x442ca3f7) and its renderer
// gfx_d3d_x86_s.dll (PE timestamp 0x4456a390). Found with REA + Ghidra and capstone; notes in
// ../docs/CoD2_VR_RE.md.
#pragma once

#define COD2SP_TIMESTAMP   0x442ca3f7u
#define GFXD3D_TIMESTAMP   0x4456a390u

// ---------------------------------------------------------------------------
// CoD2SP_s.exe (base 0x400000, no relocations: absolute addresses)
// ---------------------------------------------------------------------------
#define ADDR_re                    0x00617ab0   // refexport_t copy (0x6c pointers), filled by CL_InitRef 0x40da80
#define RE_SIZE                    0x1b0
#define ADDR_CL_InitRef_copy       0x0040db33   // rep movsd into ADDR_re
#define ADDR_SCR_UpdateScreen      0x0040fb00   // re.BeginFrame, SCR_DrawScreenField 0x40f9d0, re.EndFrame
#define ADDR_CG_DrawActive         0x004bfbf0   // fills cg.refdef and calls re.RenderScene
#define ADDR_CG_RenderScene_ret    0x004bfcd3   // return address of that call (the world view)
#define ADDR_cg_refdef             0x00f79138   // refdef_t of the world view
#define ADDR_cl_viewangles         0x00621584   // float[3]: pitch, yaw, roll (CL_AdjustAngles 0x409400, CL_MouseMove 0x409720)
#define ADDR_cvar_sensitivity      0x00617c64   // cvar_t*, value at +8
#define ADDR_Cbuf_AddText          0x0041afc0   // text in EAX (as the snd_restart call at 0x42753e)
#define ADDR_cls_keyCatchers       0x00617f04   // KEYCATCH_UI = 8 sends the mouse to the UI
#define KEYCATCH_UI                8
#define ADDR_CL_MouseEvent         0x004096d0   // dx in EAX, dy in EDX (IN_MouseMove 0x44ef70)
#define ADDR_ui_cursorX            0x010fec3c   // UI cursor, 0..640 (UI_MouseEvent 0x4c6470)
#define ADDR_ui_cursorY            0x010fec40   // 0..480
#define ADDR_CL_CreateCmd          0x00409da0   // AdjustAngles, key/mouse moves, FinishMove
#define ADDR_CL_FinishMove         0x00409d00   // usercmd in EBX
#define ADDR_CL_FinishMove_call    0x00409e27
#define USERCMD_forwardmove        0x18         // signed char (CL_KeyMove 0x409688)
#define USERCMD_rightmove          0x19

// refexport_t offsets (re + offset)
#define RE_RegisterModel    0x008
#define RE_RegisterShader   0x010   // (name, ...): 80 callers
#define RE_ClearScene       0x048   // called by SCR_UpdateScreen after BeginFrame
#define RE_RenderScene      0x094   // void (refdef_t *)
#define RE_DrawStretchPic   0x0ac   // 0x10021560: command 0xf
#define RE_BeginFrame       0x0c0   // void (void)
#define RE_EndFrame         0x0c4   // void (void)
#define RE_ClearScreen      0x0e0   // (flags, color, depth, stencil) -> command

// ---------------------------------------------------------------------------
// gfx_d3d_x86_s.dll: preferred base 0x10000000, has relocations; every address below is
// converted with the module's real base at run time (GFX(addr)).
// ---------------------------------------------------------------------------
#define GFX_BASE                   0x10000000u
#define GFX_refexport              0x10748e00   // the table GetRefAPI returns
#define GFX_R_RenderScene          0x10025bf0
#define GFX_R_SetupViewParms       0x100259c0   // viewParms from the refdef (EBX = viewParms)
#define GFX_R_SetupProjection      0x1005bfe0   // (fov_x, fov_y, zNear), ESI = 4x4 matrix
#define GFX_R_SetupProjection_call 0x10025aa8   // the call inside R_SetupViewParms (checked at run time)
#define GFX_R_AddCmdSetViewport    0x10022050   // cdecl (x, y, w, h): command 13
#define GFX_RB_SetRenderTarget     0x1003ee80   // id in EAX
#define GFX_cmdTable               0x10190518   // backend command handlers, cdecl handler(ctx*), ctx = { BYTE *cur, int depth, BYTE *stack[] }
#define GFX_CMD_COUNT              34
#define GFX_CMD_SETVIEWPORT        13           // { u16 id, u16 size, int x, y, w, h }
#define GFX_dx_device              0x101d1bf8   // IDirect3DDevice9 *
#define GFX_dx_renderTargets       0x101d4820   // { image*, colorSurface, depthStencilSurface, int width, height } x 13
#define GFX_RT_FRAMEBUFFER         0            // 1 resolved_post_sun, 2 resolved_scene, 3 dynamic_shadows, 6 floatz,
                                                // 7 savedscreen, 8 blurred_screen, 9-10 glow, 11-12 pingpong
#define GFX_rb_currentTarget       0x10744c98   // RB_SetRenderTarget's current id (0xe = none)
#define GFX_rb_currentColor        0x10744ca8   // ... the colour surface it last bound
#define GFX_rb_currentDepth        0x10744cac   // ... the depth surface
#define GFX_vidWidth               0x101d4ca8
#define GFX_vidHeight              0x101d4cac
#define GFX_cvar_r_smp_backend     0x101ce698   // cvar_t* (value byte at +8); r_skipBackEnd is 0x101ce758
#define GFX_frontEndData           0x101da9e8   // frame data pointer: viewParms array, command lists
#define GFX_FE_viewParmsCount      0x169f3c
#define GFX_FE_viewParms           0x169f40
#define GFX_VIEWPARMS_SIZE         0x14c        // org[3], axis[3][3], viewport x,y,w,h (+0x30), view matrix (+0x48),
                                                // projection (+0x88), viewProj (+0xc8)
#define GFX_r_viewOrgFwd           0x101d4d04   // R_RenderScene's copy of vieworg and viewaxis[0]
#define GFX_cmdList                0x10366a38   // pointer to the frame's command buffer (used bytes at +0xc000)

// refdef_t (same order as Quake 3, fov in degrees)
typedef struct {
    int   x, y, width, height;
    float fov_x, fov_y;
    float vieworg[3];
    float viewaxis[3][3];   // forward, left, up
    int   time;             // +0x48
    float zNear;            // +0x4c, 0 = r_znear
    int   unk50;
    int   unk54;
} cod2_refdef_t;
