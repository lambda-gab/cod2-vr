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
#define ADDR_intro_legal           0x00575ce4   // "unskippablecinematic legal\n", queued by Com_Init (0x42e7c7)
#define ADDR_intro_check           0x0042e7e0   // jne over the logo videos when com_introPlayed (75 67)
#define ADDR_cl_viewangles         0x00621584   // float[3]: pitch, yaw, roll (CL_AdjustAngles 0x409400, CL_MouseMove 0x409720)
#define ADDR_cvar_sensitivity      0x00617c64   // cvar_t*, value at +8
#define ADDR_Cbuf_AddText          0x0041afc0   // text in EAX (as the snd_restart call at 0x42753e)
#define ADDR_Sys_QueEvent          0x004500d0   // time in EAX (0 = now), then type, value, value2, ptrLength, ptr (cdecl);
                                                // string "Sys_QueEvent: overflow" 0x57a158; keys: type 1, key number, down
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
// The view weapon. CG_AddViewWeapon (0x4b6d80, the only reader of cg_gun_x/y/z besides their
// registration: cvar_t* at 0xf569d4 / 0xf48b58 / 0xf56994) builds an entity on its stack -
// axis[3][3] at +0x14 (forward, left, up), origin at +0x3c - from the view, cg_gun_* and the
// weapon's own offsets, then calls 0x4b69b0(ps, 0xf78ea8, ?) with the entity in ESI. That
// function reads +0x14..+0x44 and hands them to the renderer (re+0x4c / re+0x50).
#define ADDR_CG_AddViewWeapon      0x004b6d80
#define ADDR_CG_AddPlayerWeapon    0x004b69b0
#define ADDR_CG_AddPlayerWeapon_call 0x004b6ecc   // the call in CG_AddViewWeapon (E8)
#define VIEWENT_axis               0x14
#define VIEWENT_origin             0x3c
// CG_FireWeapon(cent, ?, kind): its error string "CG_FireWeapon: ent->weapon > BG_GetNumWeapons()" is at 0x580181
// (referenced as 0x580180 at 0x4b782d); called by CG_EntityEvent for EV_FIRE_WEAPON* (0x4bb00e ... 0x4bb0d9). It
// compares cent+0xa8 (the entity number) with cg.snap (0xf5bd28) + 0xb0 (the player's client number) to treat the
// player's own shot. Prologue 83 ec 10 53 55 (sub esp, 10h; push ebx; push ebp): 5 bytes to relocate for a detour.
#define ADDR_CG_FireWeapon         0x004b7800
#define ADDR_cg_snap               0x00f5bd28   // snapshot_t *
#define ADDR_cg_ps                 0x00f708e8   // cg.predictedPlayerState: CG_FireWeapon loads it for the player's own
                                                // shot (0x4b7878), CG_AddViewWeapon gets it as its first argument
#define PS_OFS_stats               0x10c        // int stats[]: MSG_ReadDeltaPlayerstate (0x43d810) writes "PS_STATS" there.
                                                // NOT health at [0]: 0 275 0 0 at full health on demolition (2026-10-29)
#define SNAP_OFS_clientNum         0xb0
#define CENT_OFS_number            0xa8         // word
// The game module (in the exe). g_entities from the "dodamage" script method (0x4efaf0): entity n at
// 0x1b15f00 + n * 0x1fc; G_Damage's default attacker 0x1c13b08 is entity 1022 (ENTITYNUM_WORLD).
#define ADDR_g_entities            0x01b15f00
#define GENTITY_SIZE               0x1fc
#define GENT_OFS_currentOrigin     0xdc         // float[3] (dodamage takes the source vector from it)
// The entity's box: the "istouching" script method (0x4ef100, its table entry at 0x56f078) makes absmin = currentOrigin
// + mins (+0xa8) and absmax = currentOrigin + maxs (+0xb4). contents at +0xc0 (the Quake 3 order: mins, maxs, contents,
// absmin, absmax, currentOrigin): trigger_use and trigger_use_touch (spawns 0x52cfd0 / 0x52cfe0 -> 0x52cde0) set it to
// 0x200000 at 0x52ce2c, the same CONTENTS_USABLE as CoD 1's; other writers set 0x40000000 (triggers), 0x2080 ...
// SV_Trace 0x44bf00 (the "physicstrace" script function 0x4f2680 and "bullettrace" 0x4f2230 call it; their table entries
// 0x5a5f08 / 0x5a5ee4): ESI = trace_t *results, EBX = start, EDI = end, then cdecl on the stack mins, maxs,
// passEntityNum (0x7ff = none), contentmask, and three ints (physicstrace 0, 0, 0; bullettrace 1, 0, 1); the caller pops
// 28 bytes. It clears the trace (0x24 bytes), sets fraction 1, traces the world with CM_BoxTrace 0x417640, then the
// entities. trace_t: +0 fraction, +4 normal[3], +0x10 surfaceFlags (surface type = >> 20 & 0x1f), +0x1c hit entity
// (u16: 0x7ff nothing, 0x7fe the world). Masks the game uses: physicstrace 0x820011, bullettrace 0x280e831 / 0x802831.
#define ADDR_SV_Trace              0x0044bf00
#define TRACE_SIZE                 0x24
#define ENTITYNUM_NONE             0x7ff
#define ENTITYNUM_WORLD            0x7fe
#define MASK_PHYSICS               0x820011
// The player's box and mask: the "drop to the floor" traces at 0x4e8317 / 0x4e838a pass the player's entity, mins
// 0x5a5bdc (-15 -15 0), maxs 0x5a5be8 (15 15 70) and the mask 0x281c011.
#define MASK_PLAYERSOLID           0x281c011
#define PLAYER_HEIGHT              70.0f
// Ladders: the surface flag "ladder" = 0x8 (the surface name table, entry 0x5a06b4: { name, 0, surfaceFlags, contents });
// on one the pmove sets pm_flags 0x20 (tested at 0x50b359 next to the bg_ladder_yawcap cvar 0x1c7f034; ps+0x64 vLadderVec).
#define SURF_LADDER                0x8
#define PMF_LADDER                 0x20
// SetClientOrigin 0x4e8890 (EAX = gentity, EDX = origin), as the "setorigin" script method (0x538410) calls it: the
// client's ps.origin (+0x14) = origin (z + 1), the teleport bit of ps.eFlags (+0x7c ^= 2), the entity's currentOrigin.
// playerState velocity is at +0x20 (netfield table: origin[0] +0x14, velocity[0] +0x20).
#define ADDR_SetClientOrigin       0x004e8890
// The key binding text: 0x4cdbf0 (ECX = command, ESI = out[256]; returns how many keys run it, writing "K1 or K2",
// KEY_OR 0x5816d4; none -> the localized KEY_UNBOUND 0x57e1b4). Called by the cgame's text parser for "[{cmd}]"
// (0x4a3769: it looks for "[{" 0x57e1c4 and "}]" 0x57e1c0) and its hint ownerdraws (0x4a8696 .. 0x4aa009).
// Prologue 81 ec 00 01 00 00 (sub esp, 100h): 6 bytes for a detour.
#define ADDR_KeyBindingText        0x004cdbf0
// The view weapon's DObj and skeleton (third session). weaponInfo 0xf48d80 + weapon * 0x1b8, its DObj at +0 (0x4b6c80
// updates its animation and calls DObjCalcSkel 0x472af0 on it each frame; CG_AddPlayerWeapon 0x4b69b0 gives it to
// re.AddRefEntityToScene, 0x4b6b11). DObj: +4 the skeleton, +0x18 the model count (byte), +0x1c the XModel pointers
// (0x4725f0, the bone lookup: parts = *xmodel, bone count = short [parts], names = *(u16 **)[parts + 4], script string
// ids). Skeleton: from +0x30, 0x20 bytes a bone: DObjAnimMat { quat x y z w, trans[3], transWeight = 2 / |q|^2 }
// (DObjCalcSkel writes 2 / sum of squares at +0x1c, 0x472d32; ConvertQuatToMat 0x420270 gives the convention), in the
// entity's frame (0x423430: world = origin + x axis0 + y axis1 + z axis2). Script strings: [0x8aed00] + id * 8 + 4.
#define ADDR_weaponInfo            0x00f48d80
#define WEAPONINFO_SIZE            0x1b8
#define DOBJ_OFS_skel              0x04
#define DOBJ_OFS_numModels         0x18
#define DOBJ_OFS_models            0x1c
#define SKEL_OFS_mats              0x30
#define ADDR_scrMemTreeBuf         0x008aed00
// The view weapon's camera: 0x4af1f0 (no arguments) takes the view weapon's tag_camera bone (its script string id at
// 0x1c7f174, registered at 0x5071ff) and turns / moves cg.refdef's view by it (viewaxis 0xf7915c, vieworg 0xf79150): the
// reload and other animations move the camera. Called once per frame from the cgame's view code at 0x4af65b.
#define ADDR_CG_ViewModelCamera    0x004af1f0
// The script method table (name, function, developer; 12 bytes an entry): playerads at 0x570a04 (its function pointer at
// +4: 0x538a60, (scr_entref) cdecl, returns ps+0xb8 (the ADS fraction) through Scr_AddFloat 0x46ef10).
// Cinematics (the campaign's movie maps, movie_eastern ...: maps/_briefing.gsc plays cinematic("movie_*")): the handle of
// the one playing (-1 = none, 0 .. 15) and whether it may be skipped ("cinematic" sets 1 at 0x403010,
// "unskippablecinematic" 0 at 0x403020; the stop if skippable is 0x403070). The flat game skips it with the right mouse
// button (K_MOUSE2 = 201, K_MOUSE1 = 200 in the key name table at 0x59a0d8).
#define ADDR_cin_handle            0x005a4c1c
#define ADDR_cin_skippable         0x0059a0d0
// SCR_StopCinematic-like (no arguments): if skippable and a cinematic plays, CIN_StopCinematic 0x49dc40 (ecx = handle;
// its shutdown 0x49db20 puts back the client state and, in play, tells the server it finished: 0x4d9f00, so the movie
// map's changeLevel goes on, as at the movie's natural end), sounds stopped (0x444730), handle -1. The skip used here: a
// right mouse button sent as a key event (Sys_QueEvent) did not skip the movie.
#define ADDR_SCR_StopCinematic     0x00403070
#define K_MOUSE2                   201
#define ADDR_Scr_PlayerADS_slot    0x00570a08
#define ADDR_Scr_PlayerADS         0x00538a60
#define ADDR_Scr_AddFloat          0x0046ef10
#define ADDR_Scr_AddInt            0x0046eec0   // cdecl (int)
#define ADDR_Scr_GetConstString    0x0046e760   // eax = parameter index, returns its script string id (string at [0x8aed00] + id * 8 + 4)
// openmenu method: slot at 0x570a68 (name 0x570a64) -> 0x539000 (server command "popupopen %i"). A script menu's answer
// (the UI's scriptMenuResponse) is the client command "cmd mr <index> <response>" (0x4c8ea8), index = the menu's place in
// the client's 32 script menu names, u16 script string ids at 0x620e4a.
#define ADDR_Scr_OpenMenu_slot     0x00570a68
#define ADDR_Scr_OpenMenu          0x00539000
#define ADDR_cl_scriptMenus        0x00620e4a
// fx_cull (dvar pointer at 0x6ac7b0, default 1): the effects' draw loops (0x48bf76, 0x48c116) test each effect against the
// game's view (the aim) only while it is set; the CoD 1 mod's FxAlways sets it to 0 (the same dvar, the same cause).
#define ADDR_dvar_fx_cull          0x006ac7b0
// A sphere against the effects camera's frustum (edi = centre, [esp+4] = radius, esi = plane count; ret 4; 1 = outside):
// the planes are the game's view (the aim). Called for the impact marks / decals (CG_AddMarks 0x4ad710, at 0x4ad743) and
// in the effects code (0x4802e0, 0x482610, 0x48283d, 0x4841a0); 0x486e90 is the same against a second plane set (its
// count at camera + 0xf0), called at 0x48550b.
#define ADDR_FX_CullSphere         0x00486e30
#define ADDR_FX_CullSphere2        0x00486e90
// SEH_StringEd_GetString: eax = a localized string's reference ("MOSCOW_PLATFORM_TOGGLE_ADS"), returns its text or NULL.
// Prologue 83 EC 20 53 56 (sub esp, 20h; push ebx; push esi). 10 callers (the HUD's hint texts among them).
#define ADDR_SEH_StringEd_GetString 0x00498a20
// Bullet_Fire (eax = weaponParms: forward +0, right +0x0c, up +0x18, muzzleTrace +0x24 (the start: the player's eye);
// ebx = the attacker's gentity; cdecl, 2 arguments): the end is muzzleTrace + forward * 8192 with the spread along right /
// up (0x531210), traced by Bullet_Fire_Extended 0x531340 (mask 0x280e831). Called for the player's weapons at 0x531cf7
// (also 0x51af9f, AI, and 0x550c08, turrets).
#define ADDR_Bullet_Fire           0x005312b0
#define ADDR_Bullet_Fire_call      0x00531cf7
#define MASK_SHOT                  0x0280e831
#define ADDR_CG_ViewModelCamera_call 0x004af65b
#define PS_OFS_velocity            0x20
#define GENT_OFS_mins              0xa8
#define GENT_OFS_maxs              0xb4
#define GENT_OFS_contents          0xc0
#define CONTENTS_USABLE            0x200000
#define GENT_OFS_client            0xfc         // gclient_t * (its playerState origin at +0x14)
#define GENT_OFS_actor             0x100        // non-zero for AI (G_Damage reads hit location multipliers from it)
#define GENT_OFS_sentient          0x104        // sentient_t * (AI and the player; G_Damage counts axis kills from it)
#define SENTIENT_OFS_team          0x04         // int: 1 axis, 2 allies, 3 neutral (the script field "team", table at 0x570f48)
#define GENT_OFS_takedamage        0x111        // byte
#define GENT_OFS_health            0x13c        // int (G_Damage sets 0 / -999 on death)
// G_Damage(targ, inflictor, attacker, dir *, point *, damage, dflags, mod, hitLoc), cdecl, as dodamage calls it
// (0x4efc9a). MOD_MELEE = 7 (table at 0x5a95ac: MOD_UNKNOWN ...), hit locations: 2 head, 4 torso_upper (table of
// "none", "helmet", "head", "neck", "torso_upper", ...).
#define ADDR_G_Damage              0x00528050
#define MOD_MELEE                  7
#define HITLOC_HEAD                2
#define HITLOC_TORSO_UPPER         4
// Weapon definitions (bg_weaponDefs, shared by the game and the cgame: 317 uses as [n*4 + 0x1c64548]); their count is
// what CG_FireWeapon checks the weapon index against. Field offsets from the weapon file field table (0x5a7e..0x5a87:
// { name, offset, type } entries): weaponType +0x78 (0 bullet, 1 grenade, 2 projectile, 3 binoculars, names at 0x5a7e0c),
// cookOffHold +0x33c (qboolean). The SP grenades have cookOffHold 0 (weapons/sp/fraggrenade in iw_13.iwd): the throw
// follows the pin pull (holdFireTime 0.4) whether the button is still held or not; 1 holds the grenade until released.
#define ADDR_bg_weaponDefs         0x01c64548
#define ADDR_bg_numWeapons         0x01cbad9c
#define WEAPDEF_OFS_weaponType     0x78
#define WEAPDEF_OFS_cookOffHold    0x33c
#define WEAPTYPE_GRENADE           1
#define WEAPTYPE_BULLET            0
// spread, degrees (floats, same table): hipSpreadStandMin +0x28c, DuckedMin +0x290, ProneMin +0x294, Max +0x298,
// DuckedMax +0x29c, ProneMax +0x2a0, DecayRate +0x2a4, FireAdd +0x2a8, TurnAdd +0x2ac, MoveAdd +0x2b0, adsSpread +0x4bc.
// The M1 Garand (weapons/sp/m1garand): hip 2 .. 8 (+8 moving), ads 0.2.
#define WEAPDEF_OFS_hipSpreadStandMin 0x28c
#define WEAPDEF_OFS_hipSpreadMoveAdd  0x2b0
#define WEAPDEF_OFS_adsSpread      0x4bc
// Times (field type 7: seconds in the file, int milliseconds in memory - holdFireTime is "0.4" in the files), same table:
// dropTime +0x230, raiseTime +0x234, altDropTime +0x238, altRaiseTime +0x23c, quickDropTime +0x240, quickRaiseTime +0x244;
// adsTransInTime +0x2c0, adsTransOutTime +0x2c4. adsOverlayShader +0x274 (string: a scope's picture, empty on iron sights).
#define WEAPDEF_OFS_dropTime       0x230
// Grenades (same table): fuseTime +0x248 (ms), projectileSpeed +0x384 and projectileSpeedUp +0x388 (int, units per second;
// weapons/sp/fraggrenade: 940, 120, fuse 3.5 s).
#define WEAPDEF_OFS_fuseTime       0x248
#define WEAPDEF_OFS_projectileSpeed 0x384
#define WEAPDEF_OFS_projectileSpeedUp 0x388
#define WEAPDEF_OFS_adsTransInTime 0x2c0
#define WEAPDEF_OFS_adsOverlayShader 0x274
// Recoil (floats, pitch min, pitch max, yaw min, yaw max each): adsGunKickPitchMin +0x480, adsViewKickPitchMin +0x4a0,
// hipGunKickPitchMin +0x4c8, hipViewKickPitchMin +0x4e8 (gun kick = the view model, view kick = the camera).
#define WEAPDEF_OFS_adsGunKick     0x480
#define WEAPDEF_OFS_adsViewKick    0x4a0
#define WEAPDEF_OFS_hipGunKick     0x4c8
#define WEAPDEF_OFS_hipViewKick    0x4e8
// The weapon the view model shows: BG_GetViewmodelWeaponIndex 0x4dc6a0 (EAX = ps), called by CG_AddViewWeapon (0x4b6dc1):
// ps+0xc & 0x20000 (binoculars up) -> the binoculars' index; ps+0xc & 0x10 -> ps+0xac; else ps+0xb0 (ps->weapon).
// The player's damage: the playerState netfield table (entries {name, offset, bits}, "damageEvent" at 0x56d210, its
// neighbours pm_flags +0xc and weapon +0xb0 match what is known) gives damageEvent +0xf8 (8 bits: changes at every
// hit), damageYaw +0xfc, damagePitch +0x100 (8 bits each: where it came from), damageCount +0x104 (7 bits: how much).
#define PS_OFS_damageEvent         0xf8
#define PS_OFS_damageCount         0x104
#define PS_OFS_pmFlags             0x0c
#define PS_OFS_offhandWeapon       0xac
#define PS_OFS_weapon              0xb0
#define ADDR_BG_GetViewmodelWeaponIndex 0x004dc6a0
// The death screen: at the player's death the server sends "opendeadscreen <ui_deadquote>" (0x4e8f19, after the
// death's "time_slow"); the cgame's server command handler (0x4a6ad0) sets cg.deadScreenTime 0xf7966c = cg.time
// (0xf708dc) for it (0x4a6d3c) and 0 for "closedeadscreen" (0x4a6d5d); the quote is drawn by the cgame from that time
// (reader 0x4aa2a1, then ui_deadquote at 0x4aa2ee). The level's cgame starts with it 0.
#define ADDR_cg_deadScreenTime     0x00f7966c
#define ADDR_cvar_sv_running       0x00747f04   // cvar_t * (bool at +8): 1 while a level runs (registered at 0x42e0f6)
#define PS_OFS_origin              0x14        // playerState_t origin[3] (Quake 3 layout). Confirmed 2026-10-29 on demolition:
                                                // view z - origin z = 60.0 standing; the ps was 0x00f708e8 (cg.predictedPlayerState?)
// Seen in the same run: the game's view weapon origin is the view origin itself (0, 0, 0 from the view at rest):
// CoD2's view models are made with their origin at the eye.

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
#define GFX_R_DrawStretchPic       0x10021560   // cdecl (x, y, w, h, s0, t0, s1, t1, const float *color, Material *): command 15
#define GFX_R_SetupViewParms       0x100259c0   // viewParms from the refdef (EBX = viewParms)
#define GFX_R_SetupProjection      0x1005bfe0   // (fov_x, fov_y, zNear), ESI = 4x4 matrix
#define GFX_R_SetupProjection_call 0x10025aa8   // the call inside R_SetupViewParms (checked at run time)
#define GFX_R_AddCmdSetViewport    0x10022050   // cdecl (x, y, w, h): command 13
#define GFX_RB_SetRenderTarget     0x1003ee80   // id in EAX
// The backend batches 2D quads; the viewport and render target handlers (commands 13 0x10032750, 14 0x10032530) first
// draw what is batched: if (tessA 0x1070bfd0 || tessB 0x1070bfe0) RB_EndTessSurface 0x1003ad10 (void, no register input).
#define GFX_RB_EndTessSurface      0x1003ad10
#define GFX_tess_pendingA          0x1070bfd0
#define GFX_tess_pendingB          0x1070bfe0
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
