// cod2_vr: Call of Duty 2 single player (CoD2SP_s.exe 1.3) in an OpenXR headset.
//
// How it works (addresses in cod2_addresses.h, findings in ../docs/CoD2_VR_RE.md):
//  * d3d9_proxy.c makes the game's device a D3D9Ex device, so it can open the D3D11 textures the
//    OpenXR backend (vr_xr.c) shares with it: one per eye and one for the HUD.
//  * re.RenderScene in the client's renderer table (0x617ab0) is replaced. The world view the
//    cgame renders (CG_DrawActive) is rendered twice, once from each tracked eye, with each
//    eye's asymmetric frustum (the call to R_SetupProjection in R_SetupViewParms is redirected).
//  * The renderer queues commands for its backend. Before each eye (and before the HUD) a
//    marker command is queued: a viewport command (R_AddCmdSetViewport) with impossible values.
//    The backend's viewport handler is wrapped; on a marker it points the frame buffer render
//    target (dx.renderTargets[0]) at that eye's texture, or at the HUD texture, and rebinds it.
//  * The HUD and menus (everything 2D) land in the HUD texture, shown as a quad layer.
//  * At Present the frame is submitted to the headset (left eye mirrored on the monitor) and the
//    next headset frame is started; its head pose drives cl.viewangles (aim where you look).
//  * Without a headset the two eyes are shown side by side on the monitor (stereo test).
#define _CRT_SECURE_NO_WARNINGS
#include "cod2_vr.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <intrin.h>

#include "cod2_addresses.h"
#include "controls.h"
#include "arms.h"

#define DEG(x)  ((x) * (180.0f / 3.14159265f))
#define RAD(x)  ((x) * (3.14159265f / 180.0f))
#define PITCH 0
#define YAW   1
#define MEM(type, addr) (*(type *)(addr))

char g_dir[MAX_PATH];
extern int g_useEx;

// ---- configuration (cod2_vr.ini next to the exe, then %APPDATA%\CoD2-VR\settings.ini) ----
static struct {
    int   enabled;
    int   vr;            // use a headset when there is one
    int   sbs;           // no headset: both eyes side by side on the monitor
    float separation;    // side by side: game units between the eyes
    float worldScale;    // game units per metre (1 unit = 1 inch)
    int   positional;    // head position moves the camera
    int   headAim;       // head yaw/pitch drive cl.viewangles
    float renderScale;   // eye resolution, times the headset's recommended size
    float cullMargin;    // degrees added on each side for what the game draws
    vr_hud_t hud;        // quad placement
    int   mirror;        // show the left eye in the game window
    int   traceCmds;     // diagnostics: log the backend commands of this many world frames
    int   skipIntro;     // 1 = straight to the menu (no legal screen, no logo videos)
    int   menuFollowsHead; // 1 = menus follow the head, 0 = fixed in the room where they opened
    int   fakeHmd;       // diagnostics: the headset path with made-up eyes, shown side by side
    int   fakeW, fakeH;  // ... their resolution
    int   fakeCursor[2]; // ... where the menu cursor is put (640x480)
    int   fakeHand;      // ... and a still gun hand held out at the right of the chest (the gun is placed there)
    float heightOffset;  // metres added to the head height
    int   weaponInHand;  // 1 = the gun hand aims and holds the view weapon (head aim without controllers)
    float converge;      // metres along the barrel where the crosshair is drawn (the shots follow the barrel's line)
    int   fireFromGun;   // [Controllers] FireFromGun: 1 = the player's shots leave the muzzle along the barrel (VR)
    float muzzleDist;    // [Controllers] MuzzleDistance: metres from the controller to the muzzle along the barrel
    float sightReach;    // [Controllers] SightReach: an eye this near the barrel's line (metres) aims through the sights
    float wofs[3];       // the spot of the flat game's gun (units from the eye: forward, right, up) that goes into the hand
    float wPitch, wYaw;  // gun angle relative to the controller, degrees (barrel up, to the left)
    int   physCrouch;    // 1 = crouching for real crouches the soldier
    float crouchDepth;   // metres the head goes below its standing height for that
    int   vignette;      // 1 = comfort vignette while moving with the stick or turning smoothly
    float vigStrength;   // how far it closes, 0 .. 1
    float vigSpeed;      // game units per second where it starts closing (fully at twice)
    float vigTurn;       // smooth turning, degrees per second where it starts closing
    float hapShots;      // controller vibration on the player's shots, 0 = off .. 1
    int   crosshair;     // where the gun is aimed: 0 none, 1 cross, 2 dot, 3 circle, 4 circle + dot, 5 cross + dot
    float crossColor[3]; // its colour ([Controllers] CrosshairColor: a name or RRGGBB, as the CoD 1 mod's)
    float crossAlpha;    // its opacity 0.1 .. 1 (CrosshairOpacity)
    float crossSize;     // ... its size, degrees
    float objWatchWidth; // the objectives on the back of the off hand: width in metres
    float objBackground; // ... the opacity of the dark panel behind them
    int   grenadeHold;   // 1 = grenades are held while their button is (the pin pulled) and thrown on release
    int   hipAccuracy;   // 1 = shots without aiming down the sights are as accurate as aimed ones
    float meleeSpeed;    // swing melee: m/s a part of the gun must move into someone, 0 = off
    int   meleeDamage;   // ... the damage of such a blow
    float wofsBase[3];   // WeaponOffset* as set; wofs is this or the weapon's own line of [WeaponOffsets]
    int   instantAds;    // 1 = scoped weapons go to their zoom and back at once
    int   drawTime;      // msec at most to put a weapon away and raise the next one, 0 = the game's own
    float twoHandKick;   // share of the recoil taken away while the gun is held with both hands, 0 .. 1
    float objMargin;     // the objectives on the hand: room between the panel's edge and them, share of the screen
    float hapDamage;     // controller vibration on being hit, 0 = off .. 1
    int   hudMode;       // [HUD] HudMode: what of the game's HUD stays (HUD_*), all of it while the objectives are held
    float subtitleSize;  // [HUD] SubtitleSize: the subtitles' size, 1 = a little bigger than the game's
    int   moveMode;      // [Controllers] MoveMode: 0 Blink, 1 Shift (both beta), 2 Continuous (the stick walks)
    int   roomScale;     // [VR] RoomScale: the soldier follows the real head
    int   mirrorLeft;    // [Controllers] MirrorLeftHand: a gun in the left hand is drawn mirrored
    int   armBones;      // [Hands] ArmBones: the soldier's arms follow the controllers (when they are shown)
    float shoulder[3];   // [Hands] ShoulderDown / ShoulderSide / ShoulderBack, cm from the eyes
    float armLength;     // [Hands] ArmLength: the player's arm in metres, 0 = from the eye height
    float offHand[3];    // [Hands] OffHandX / Y / Z: cm from the off controller to its wrist bone (forward, left, up)
    int   msaa;          // [VR] MSAA: samples per pixel of the eyes, 1 = off
    int   skipInvertMenu; // [Comfort] SkipInvertMenu: 1 = the training's "invert the mouse?" question is answered no in VR
    int   fxAlways;      // [VR] FxAlways: 1 = fx_cull 0 in VR (effects drawn wherever the gun points)
    int   weaponCamera;  // [Comfort] WeaponCamera: 1 = the weapon's animations move the camera (the flat game's), 0 = not in VR
    int   keepLook;      // [Comfort] KeepLook: a scripted scene taking the view does not turn the picture away
    float shakeKeep;     // [Comfort] ShakeKeep: share of the camera shake (earthquakes, explosions) kept, 1 = all
    int   headCollision; // [VR] HeadCollision: the camera stops a little before walls the head leans or walks into
    int   grenadeArc;    // [Grenade] Trajectory: 0 off, 1 / 2 the line while a grenade is in the hand
    float gravity;       // ... the game's gravity, units per second squared
    float grenadeOpacity; // ... the line's opacity
} cfg = { 1, 1, 1, 2.5f, 39.37f, 1, 1, 1.0f, 10.0f, { 0, 1, 1.4f, -0.05f, 1.0f, 1.8f }, 1, 0, 1, 0, 0, 1440, 1584, { 395, 300 } };

static FILE *g_log;
static char  g_userIni[MAX_PATH];
static int   g_testFire;        // [Debug] TestFire: FakeHeadset taps +attack every 3 s (checks the shot hook)
static int   g_probeEyes;       // [Debug] ProbeEyes: FakeHeadset logs the left eye's pixels at the corners and the centre
static int   g_hooked;          // the game and renderer are the known versions and hooked
static DWORD g_gfx;             // gfx_d3d_x86_s.dll base
#define GFX(a) (g_gfx + ((DWORD)(a) - GFX_BASE))

void Log(const char *fmt, ...)
{
    va_list ap;
    SYSTEMTIME t;
    if (!g_log) {
        char path[MAX_PATH];
        _snprintf(path, sizeof(path), "%s\\cod2_vr.log", g_dir);
        g_log = fopen(path, "w");
        if (!g_log) return;
    }
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
// (not GetPrivateProfileInt: it turns negative values into 0)
static int CfgInt(const char *sec, const char *key, int def, const char *ini)
{
    char buf[32];
    GetPrivateProfileStringA(sec, key, "", buf, sizeof(buf), ini);
    return buf[0] ? atoi(buf) : def;
}

static float IniFloat(const char *sec, const char *key, float def, const char *ini)
{
    char buf[64];
    GetPrivateProfileStringA(sec, key, "", buf, sizeof(buf), ini);
    return buf[0] ? (float)atof(buf) : def;
}

// Every setting: cod2_vr.ini, then the launcher's player settings (which win).
static int GetInt(const char *sec, const char *key, int def)
{
    char ini[MAX_PATH];
    int v;
    _snprintf(ini, sizeof(ini), "%s\\cod2_vr.ini", g_dir);
    v = CfgInt(sec, key, def, ini);
    return g_userIni[0] ? CfgInt(sec, key, v, g_userIni) : v;
}

static float GetFloat(const char *sec, const char *key, float def)
{
    char ini[MAX_PATH];
    float v;
    _snprintf(ini, sizeof(ini), "%s\\cod2_vr.ini", g_dir);
    v = IniFloat(sec, key, def, ini);
    return g_userIni[0] ? IniFloat(sec, key, v, g_userIni) : v;
}

static void GetStr(const char *sec, const char *key, const char *def, char *out, int size)
{
    char ini[MAX_PATH], tmp[256];
    _snprintf(ini, sizeof(ini), "%s\\cod2_vr.ini", g_dir);
    GetPrivateProfileStringA(sec, key, def, tmp, sizeof(tmp), ini);
    if (g_userIni[0]) GetPrivateProfileStringA(sec, key, tmp, out, size, g_userIni);
    else { strncpy(out, tmp, size - 1); out[size - 1] = 0; }
}

// Where the gun sits in the hand: read at start and again every second while playing, so it can
// be tuned with the game running (edit settings.ini or cod2_vr.ini and save).
#define MOVE_CONTINUOUS_DEFAULT 2
static char g_wofsName[64];   // the weapon whose offset is in cfg.wofs ("" = none yet)
static int  g_wofsIndex = -1; // ... its index

static void ApplyWeaponOffset(void);

static void ReloadWeaponTuning(void)
{
    cfg.wofsBase[0] = GetFloat("Controllers", "WeaponOffsetForward", 10.0f);
    cfg.wofsBase[1] = GetFloat("Controllers", "WeaponOffsetRight", 5.0f);
    cfg.wofsBase[2] = GetFloat("Controllers", "WeaponOffsetUp", -7.0f);
    cfg.wPitch  = GetFloat("Controllers", "WeaponPitch", 0.0f);
    cfg.wYaw    = GetFloat("Controllers", "WeaponYaw", 0.0f);
    ApplyWeaponOffset();
}

// Per weapon (as the CoD 1 mod's [WeaponOffsets]): "<weapon name> = forward right up" in cod2_vr.ini or
// settings.ini replaces WeaponOffset* for that weapon (the name is the weapon file's, as the log's "weapon
// offset for" line shows it). Read when the weapon in the hand changes and every second with the rest.
static void ApplyWeaponOffset(void)
{
    char v[96], ini[MAX_PATH];
    float a, b, c;
    int own = 0;
    memcpy(cfg.wofs, cfg.wofsBase, sizeof(cfg.wofs));
    if (g_wofsName[0]) {
        _snprintf(ini, sizeof(ini), "%s\\cod2_vr.ini", g_dir);
        GetPrivateProfileStringA("WeaponOffsets", g_wofsName, "", v, sizeof(v), ini);
        if (g_userIni[0]) GetPrivateProfileStringA("WeaponOffsets", g_wofsName, v, v, sizeof(v), g_userIni);
        if (v[0] && sscanf(v, "%f %f %f", &a, &b, &c) == 3) { cfg.wofs[0] = a; cfg.wofs[1] = b; cfg.wofs[2] = c; own = 1; }
    }
    {
        static char logged[64];
        static int loggedOwn = -1;
        if (strcmp(logged, g_wofsName) || own != loggedOwn) {
            strcpy(logged, g_wofsName);
            loggedOwn = own;
            if (g_wofsName[0])
                Log("weapon offset for %s: %.1f %.1f %.1f%s", g_wofsName, cfg.wofs[0], cfg.wofs[1], cfg.wofs[2], own ? "" : " (the general one)");
        }
    }
}

// The weapon the view model shows (BG_GetViewmodelWeaponIndex): the binoculars while they are up, the
// offhand item while it is out, else the weapon in use.
static int ViewWeaponIndex(void)
{
    const BYTE *ps = (const BYTE *)ADDR_cg_ps;
    int flags = MEM(int, ps + PS_OFS_pmFlags), i, n;
    if (flags & 0x20000) {
        n = MEM(int, ADDR_bg_numWeapons);
        for (i = 1; i <= n && i < 256; i++) {
            const BYTE *d = MEM(const BYTE *, ADDR_bg_weaponDefs + i * 4);
            if (d && MEM(int, d + WEAPDEF_OFS_weaponType) == 3) return i;
        }
        return 0;
    }
    return MEM(int, ps + ((flags & 0x10) ? PS_OFS_offhandWeapon : PS_OFS_weapon));
}

static void UpdateWeaponOffset(void)
{
    int w = 0;
    const char *name = NULL;
    __try {
        w = ViewWeaponIndex();
        if (w > 0 && w <= MEM(int, ADDR_bg_numWeapons)) {
            const BYTE *d = MEM(const BYTE *, ADDR_bg_weaponDefs + w * 4);
            name = d ? MEM(const char *, d) : NULL;
            if (name && (!name[0] || strlen(name) >= sizeof(g_wofsName))) name = NULL;
        } else w = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        w = 0;
        name = NULL;
    }
    if (w == g_wofsIndex && (name ? !strcmp(name, g_wofsName) : !g_wofsName[0])) return;
    g_wofsIndex = w;
    if (name) strcpy(g_wofsName, name); else g_wofsName[0] = 0;
    ApplyWeaponOffset();
}

static void LoadConfig(void)
{
    char appdata[MAX_PATH];
    if (GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata)))
        _snprintf(g_userIni, sizeof(g_userIni), "%s\\CoD2-VR\\settings.ini", appdata);
    cfg.enabled     = GetInt("General", "Enabled", cfg.enabled);
    g_useEx         = GetInt("Proxy", "D3D9Ex", 1);
    cfg.vr          = GetInt("VR", "Enabled", cfg.vr);
    cfg.sbs         = GetInt("VR", "SideBySideWithoutHeadset", cfg.sbs);
    cfg.separation  = GetFloat("VR", "Separation", cfg.separation);
    cfg.worldScale  = GetFloat("VR", "WorldScale", cfg.worldScale);
    cfg.positional  = GetInt("VR", "PositionalTracking", cfg.positional);
    cfg.headAim     = GetInt("VR", "HeadAim", cfg.headAim);
    cfg.renderScale = GetFloat("VR", "RenderScale", cfg.renderScale);
    cfg.cullMargin  = GetFloat("VR", "CullMargin", cfg.cullMargin);
    cfg.mirror      = GetInt("VR", "Mirror", cfg.mirror);
    cfg.hud.headLocked = GetInt("HUD", "HeadLocked", 1);
    cfg.hud.distance   = GetFloat("HUD", "Distance", cfg.hud.distance);
    cfg.hud.offsetY    = GetFloat("HUD", "OffsetY", cfg.hud.offsetY);
    cfg.hud.width      = GetFloat("HUD", "Width", cfg.hud.width);
    cfg.hud.menuWidth  = GetFloat("HUD", "MenuWidth", cfg.hud.menuWidth);
    cfg.traceCmds   = GetInt("Debug", "TraceCommands", 0);
    cfg.fakeHmd     = GetInt("Debug", "FakeHeadset", 0);
    cfg.fakeW       = GetInt("Debug", "FakeEyeWidth", cfg.fakeW);
    cfg.fakeH       = GetInt("Debug", "FakeEyeHeight", cfg.fakeH);
    cfg.fakeCursor[0] = GetInt("Debug", "FakeCursorX", cfg.fakeCursor[0]);
    cfg.fakeCursor[1] = GetInt("Debug", "FakeCursorY", cfg.fakeCursor[1]);
    cfg.menuFollowsHead = GetInt("HUD", "MenuFollowsHead", 0);
    cfg.skipIntro = GetInt("General", "SkipIntro", 1);
    cfg.fakeHand     = GetInt("Debug", "FakeHand", 0);
    g_testFire       = GetInt("Debug", "TestFire", 0);
    g_probeEyes      = GetInt("Debug", "ProbeEyes", 0);
    cfg.heightOffset = GetFloat("VR", "HeightOffset", 0.0f);
    cfg.weaponInHand = GetInt("Controllers", "WeaponInHand", 1);
    cfg.converge     = GetFloat("Controllers", "ConvergeDistance", 15.0f);
    cfg.fireFromGun  = GetInt("Controllers", "FireFromGun", 1);
    cfg.muzzleDist   = GetFloat("Controllers", "MuzzleDistance", 0.45f);
    cfg.sightReach   = GetFloat("Controllers", "SightReach", 0.15f);
    cfg.physCrouch   = GetInt("Controllers", "PhysicalCrouch", 1);
    cfg.crouchDepth  = GetFloat("Controllers", "CrouchDepth", 0.32f);
    cfg.vignette     = GetInt("Comfort", "Vignette", 1);
    cfg.vigStrength  = GetFloat("Comfort", "VignetteStrength", 0.6f);
    cfg.vigSpeed     = GetFloat("Comfort", "VignetteSpeed", 40.0f);
    cfg.vigTurn      = GetFloat("Comfort", "VignetteTurn", 45.0f);
    cfg.hapShots     = GetFloat("Controllers", "HapticsShots", GetFloat("Controllers", "Haptics", 0.6f));
    cfg.crosshair    = GetInt("Controllers", "Crosshair", 0);
    {
        static const struct { const char *name; float rgb[3]; } cols[] = {
            { "white", { 1.0f, 1.0f, 1.0f } }, { "red", { 1.0f, 0.15f, 0.12f } }, { "green", { 0.2f, 1.0f, 0.25f } },
            { "yellow", { 1.0f, 0.9f, 0.1f } }, { "cyan", { 0.1f, 0.95f, 1.0f } }, { "blue", { 0.25f, 0.45f, 1.0f } },
            { "orange", { 1.0f, 0.55f, 0.1f } }, { "magenta", { 1.0f, 0.2f, 0.9f } },
        };
        char name[24];
        int k, found = 0;
        GetStr("Controllers", "CrosshairColor", "white", name, sizeof(name));
        if (strlen(name) == 6 && strspn(name, "0123456789abcdefABCDEF") == 6) {   // RRGGBB, from the launcher's colour picker
            unsigned int rgb = (unsigned int)strtoul(name, NULL, 16);
            cfg.crossColor[0] = ((rgb >> 16) & 255) / 255.0f;
            cfg.crossColor[1] = ((rgb >> 8) & 255) / 255.0f;
            cfg.crossColor[2] = (rgb & 255) / 255.0f;
            found = 1;
        }
        for (k = 0; !found && k < (int)(sizeof(cols) / sizeof(cols[0])); k++)
            if (!_stricmp(name, cols[k].name)) { memcpy(cfg.crossColor, cols[k].rgb, sizeof(cfg.crossColor)); found = 1; }
        if (!found) cfg.crossColor[0] = cfg.crossColor[1] = cfg.crossColor[2] = 1.0f;
        cfg.crossAlpha = GetFloat("Controllers", "CrosshairOpacity", 0.8f);
        if (cfg.crossAlpha < 0.1f) cfg.crossAlpha = 0.1f;
        if (cfg.crossAlpha > 1.0f) cfg.crossAlpha = 1.0f;
    }
    cfg.objWatchWidth = GetFloat("HUD", "ObjectivesWatchWidth", 0.34f);
    cfg.objBackground = GetFloat("HUD", "ObjectivesBackground", 0.88f);
    cfg.grenadeHold  = GetInt("Holsters", "GrenadeHold", 1);
    cfg.hipAccuracy  = GetInt("Controllers", "HipFireAccuracy", 1);
    cfg.meleeSpeed   = GetInt("Controllers", "SwingMelee", 1) ? GetFloat("Controllers", "MeleeSpeed", 2.2f) : 0.0f;
    cfg.meleeDamage  = GetInt("Controllers", "MeleeDamage", 150);
    cfg.instantAds   = GetInt("Controllers", "InstantAds", 1);
    cfg.drawTime     = GetInt("Holsters", "DrawTime", 50);
    if (cfg.drawTime < 0) cfg.drawTime = 0;
    cfg.twoHandKick  = GetFloat("Controllers", "TwoHandRecoilReduction", 0.8f);
    if (cfg.twoHandKick < 0) cfg.twoHandKick = 0;
    if (cfg.twoHandKick > 1) cfg.twoHandKick = 1;
    cfg.hapDamage    = GetFloat("Controllers", "HapticsDamage", GetFloat("Controllers", "Haptics", 0.6f));
    cfg.headCollision = GetInt("VR", "HeadCollision", 1);
    cfg.hudMode      = GetInt("HUD", "HudMode", 0);
    cfg.subtitleSize = GetFloat("HUD", "SubtitleSize", 1.5f);
    if (cfg.subtitleSize < 0.5f) cfg.subtitleSize = 0.5f;
    if (cfg.subtitleSize > 4.0f) cfg.subtitleSize = 4.0f;
    cfg.moveMode     = GetInt("Controllers", "MoveMode", MOVE_CONTINUOUS_DEFAULT);
    if (cfg.moveMode < 0 || cfg.moveMode > 2) cfg.moveMode = MOVE_CONTINUOUS_DEFAULT;
    cfg.roomScale    = GetInt("VR", "RoomScale", 0);
    cfg.mirrorLeft   = GetInt("Controllers", "MirrorLeftHand", 1);
    cfg.armBones     = GetInt("Hands", "ArmBones", 1);
    cfg.shoulder[0]  = GetFloat("Hands", "ShoulderDown", 20.0f);
    cfg.shoulder[1]  = GetFloat("Hands", "ShoulderSide", 18.0f);
    cfg.shoulder[2]  = GetFloat("Hands", "ShoulderBack", 5.0f);
    cfg.armLength    = GetFloat("Hands", "ArmLength", 0.0f);
    cfg.offHand[0]   = GetFloat("Hands", "OffHandX", -10.0f);
    cfg.offHand[1]   = GetFloat("Hands", "OffHandY", 0.0f);
    cfg.offHand[2]   = GetFloat("Hands", "OffHandZ", -2.0f);
    cfg.msaa         = GetInt("VR", "MSAA", 1);
    if (cfg.msaa != 2 && cfg.msaa != 4 && cfg.msaa != 8) cfg.msaa = 1;
    cfg.keepLook     = GetInt("Comfort", "KeepLook", 1);
    cfg.weaponCamera = GetInt("Comfort", "WeaponCamera", 0);
    cfg.skipInvertMenu = GetInt("Comfort", "SkipInvertMenu", 1);
    cfg.fxAlways     = GetInt("VR", "FxAlways", 1);
    {
        // [PostFX] (as the CoD 1 mod's): sharpening, bloom, a colour style (contrast, saturation, tint)
        static const struct { const char *name; float contrast, sat, tint[3]; } styles[] = {
            { "original", 1.00f, 1.00f, { 1.00f, 1.00f, 1.00f } },
            { "natural",  1.05f, 1.08f, { 1.00f, 1.00f, 1.00f } },
            { "cinema",   1.10f, 0.82f, { 1.04f, 1.00f, 0.93f } },   // a muted, warm war film
            { "vivid",    1.08f, 1.25f, { 1.00f, 1.00f, 1.00f } },
        };
        vr_postfx_t fx;
        char style[32];
        int k;
        fx.sharpen = GetFloat("PostFX", "Sharpen", 0.0f);
        fx.bloom = GetFloat("PostFX", "Bloom", 0.0f);
        fx.bloomThreshold = GetFloat("PostFX", "BloomThreshold", 0.7f);
        if (fx.sharpen < 0) fx.sharpen = 0;
        if (fx.sharpen > 1) fx.sharpen = 1;
        if (fx.bloom < 0) fx.bloom = 0;
        if (fx.bloomThreshold > 0.95f) fx.bloomThreshold = 0.95f;
        fx.contrast = fx.saturation = 1;
        fx.tint[0] = fx.tint[1] = fx.tint[2] = 1;
        GetStr("PostFX", "Color", "original", style, sizeof(style));
        for (k = 0; k < (int)(sizeof(styles) / sizeof(styles[0])); k++)
            if (!_stricmp(style, styles[k].name)) {
                fx.contrast = styles[k].contrast;
                fx.saturation = styles[k].sat;
                memcpy(fx.tint, styles[k].tint, sizeof(fx.tint));
            }
        XR_SetPostFx(&fx);
        if (fx.sharpen > 0 || fx.bloom > 0 || _stricmp(style, "original"))
            Log("postfx: sharpen %.2f, bloom %.2f (threshold %.2f), colour %s", fx.sharpen, fx.bloom, fx.bloomThreshold, style);
    }
    cfg.shakeKeep    = GetFloat("Comfort", "ShakeKeep", 0.4f);
    if (cfg.shakeKeep < 0) cfg.shakeKeep = 0;
    if (cfg.shakeKeep > 1) cfg.shakeKeep = 1;
    cfg.grenadeArc   = GetInt("Grenade", "Trajectory", 1);
    cfg.gravity      = GetFloat("Grenade", "Gravity", 800.0f);
    if (cfg.gravity < 1.0f) cfg.gravity = 800.0f;
    cfg.grenadeOpacity = GetFloat("Grenade", "Opacity", 0.6f);
    if (cfg.grenadeOpacity < 0.05f) cfg.grenadeOpacity = 0.05f;
    if (cfg.grenadeOpacity > 1.0f) cfg.grenadeOpacity = 1.0f;
    cfg.objMargin    = GetFloat("HUD", "ObjectivesMargin", 0.02f);
    if (cfg.objMargin < 0) cfg.objMargin = 0;
    if (cfg.objMargin > 0.2f) cfg.objMargin = 0.2f;
    cfg.crossSize    = GetFloat("Controllers", "CrosshairSize", 1.5f);
    if (cfg.crossSize < 0.2f) cfg.crossSize = 0.2f;
    if (cfg.crossSize > 10.0f) cfg.crossSize = 10.0f;
    if (cfg.converge < 1.0f) cfg.converge = 1.0f;
    if (cfg.crouchDepth < 0.12f) cfg.crouchDepth = 0.12f;
    if (cfg.vigStrength < 0) cfg.vigStrength = 0;
    if (cfg.vigStrength > 1) cfg.vigStrength = 1;
    if (cfg.vigSpeed < 1.0f) cfg.vigSpeed = 1.0f;
    if (cfg.vigTurn < 1.0f) cfg.vigTurn = 1.0f;
    ReloadWeaponTuning();
    Controls_LoadConfig(GetInt, GetFloat, GetStr);
    if (!cfg.enabled) g_useEx = 0;
    XR_SetRenderScale(cfg.renderScale);
    Log("config: enabled %d, D3D9Ex %d, VR %d, side by side %d, world scale %.2f, render scale %.2f",
        cfg.enabled, g_useEx, cfg.vr, cfg.sbs, cfg.worldScale, cfg.renderScale);
}

static DWORD PeTimestamp(DWORD base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    return ((IMAGE_NT_HEADERS *)(base + dos->e_lfanew))->FileHeader.TimeDateStamp;
}

// Com_Init queues "unskippablecinematic legal" and, unless com_introPlayed is set, the logo
// videos. The first is blanked out (same length) and the check of the second always passes.
static void SkipIntro(void)
{
    static const char legal[] = "unskippablecinematic legal\n";
    char *str = (char *)ADDR_intro_legal;
    BYTE *jne = (BYTE *)ADDR_intro_check;
    DWORD old;
    if (memcmp(str, legal, sizeof(legal)) || jne[0] != 0x75 || jne[1] != 0x67) {
        Log("intro: unexpected bytes, not skipped");
        return;
    }
    VirtualProtect(str, sizeof(legal), PAGE_EXECUTE_READWRITE, &old);
    memset(str, ' ', sizeof(legal) - 2);
    VirtualProtect(str, sizeof(legal), old, &old);
    VirtualProtect(jne, 1, PAGE_EXECUTE_READWRITE, &old);
    jne[0] = 0xEB;
    VirtualProtect(jne, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), jne, 2);
    Log("intro skipped");
}

void VR_ProcessAttach(HMODULE self)
{
    char *p;
    (void)self;
    GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    if ((p = strrchr(g_dir, '\\')) != NULL) *p = 0;
    LoadConfig();
    if (PeTimestamp((DWORD)GetModuleHandleA(NULL)) != COD2SP_TIMESTAMP) {
        Log("not CoD2SP_s.exe 1.3 (timestamp %08x): passthrough only", PeTimestamp((DWORD)GetModuleHandleA(NULL)));
        cfg.enabled = 0;
        g_useEx = 0;
        return;
    }
    if (cfg.enabled && cfg.skipIntro) SkipIntro();
}

// ---------------------------------------------------------------------------
// Math (Quake 3 conventions: axis = forward, left, up; pitch > 0 looks down)
// ---------------------------------------------------------------------------
static float AngleNormalize180(float a)
{
    a = fmodf(a + 180.0f, 360.0f);
    if (a < 0) a += 360.0f;
    return a - 180.0f;
}

static float YawOf(const float fwd[3])   { return DEG(atan2f(fwd[1], fwd[0])); }
static float PitchOf(const float fwd[3])
{
    float z = fwd[2] < -1 ? -1 : fwd[2] > 1 ? 1 : fwd[2];
    return -DEG(asinf(z));
}

static void RotateZ(const float v[3], float yawDeg, float out[3])
{
    float s = sinf(RAD(yawDeg)), c = cosf(RAD(yawDeg));
    out[0] = v[0] * c - v[1] * s;
    out[1] = v[0] * s + v[1] * c;
    out[2] = v[2];
}

static float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void  Cross(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}

// ---------------------------------------------------------------------------
// Frame state
// ---------------------------------------------------------------------------
enum { MODE_OFF, MODE_SBS, MODE_VR };
// TGT_HUDK: the HUD bound as it is (not cleared); TGT_FULLK: the full-view texture, cleared at its first use in a frame
enum { TGT_WINDOW, TGT_EYE0, TGT_EYE1, TGT_HUD, MARK_WORLD_BEGIN = 5, MARK_WORLD_END, TGT_HUDK = 9, TGT_FULLK };   // backend targets / markers
#define MARKER_MAGIC 0x56523200

static int        g_mode;               // this frame (decided at BeginFrame)
static int        g_vrFrame;            // XR_BeginFrame succeeded for the frame being built
static int        g_xrIdle;             // the game is loading: the backend's thread gives the headset its frames (XR_KeepAlive)
static vr_pose_t  g_eyes[2], g_head;
static int        g_worldThisFrame;     // the world view was rendered (VR: eyes, else menu)
static int        g_projEye = -1;       // R_SetupProjection: put this eye's frustum in
static float      g_aimYawPrev, g_aimYawCmd;   // tracking-space yaw of the aim put into cl.viewangles (now / at the usercmd)
static int        g_aimYawInit, g_aimHand;     // ... g_aimHand: it comes from the gun hand, not the head
static float      g_headYawNow, g_moveYaw;     // head yaw; head minus aim at the usercmd (the stick walks where you look)
static float      g_ref[3];             // tracking point that is the soldier's eye: x, y and the standing height (recenter)
static float      g_lastViewOrg[3];     // the game's view origin of the last world frame
static float      g_lastViewFwd[3];     // ... and its forward axis
static int        g_lastWorld;          // the last finished frame had the world (not a menu or a loading screen)
static int        g_fullPainted;        // a full-screen picture went to the full-view texture this frame (hk_StretchPic)
static int        g_deathFrame;         // the death screen is open: none of its 2D is shown, the world stays in view
static int        g_frameNo;            // counts BeginFrames (what is worked out once per frame)
static float      g_shakeOfs[3];        // world units added to the game's view origin this frame: its shake taken out (ShakeFilter)
static float      g_crouchCorr;         // metres given back while the game crouches because the player did
static int        g_crouchOwned;        // ... it did (UpdatePhysicalCrouch)
static void SetCrouchCorrection(const float *vieworg);
static float      g_clPitchCmd, g_pitchOffset;
static int        g_pitchOffsetInit, g_pitchSampled;
static DWORD      g_nextXrTry;

// D3D9 side of the targets
static IDirect3DTexture9 *g_eyeTex[2], *g_hudTex, *g_fullTex;
static IDirect3DSurface9 *g_eyeSurf[2], *g_eyeDepth[2], *g_hudSurf, *g_fullSurf;
static IDirect3DSurface9 *g_hudCopy;   // the HUD image while the objectives are inset (InsetHud)
static IDirect3DSurface9 *g_eyeMS[2], *g_eyeMSDepth[2];   // [VR] MSAA: each eye drawn multisampled, resolved into g_eyeSurf
static int        g_eyeW, g_eyeH, g_hudW, g_hudH;   // texture sizes
static int        g_targetsVR;          // 1 = the eye textures are the headset's shared ones, 2 = fake headset
static IDirect3DQuery9 *g_flushQuery;
static int        g_winW, g_winH;       // back buffer

// backend state (runs inside the renderer's command execution)
typedef struct { void *image; IDirect3DSurface9 *color, *depth; int width, height; } gfx_rt_t;
static int        g_beTarget = TGT_WINDOW;
static int        g_beFullFilled;       // the full-view texture was cleared this frame
static gfx_rt_t   g_rtSaved;            // the frame buffer target as the game made it

typedef void (__cdecl *PFN_Void)(void);
typedef void (__cdecl *PFN_RenderScene)(cod2_refdef_t *fd);
typedef void (__cdecl *PFN_CmdHandler)(BYTE **ctx);
typedef void (__cdecl *PFN_AddCmdViewport)(int x, int y, int w, int h);
static PFN_Void        orig_BeginFrame, orig_EndFrame;
static PFN_RenderScene orig_RenderScene;
static PFN_CmdHandler  orig_CmdViewport;
static DWORD           p_SetupProjection, p_RB_SetRenderTarget;
static PFN_AddCmdViewport p_AddCmdViewport;

// ---------------------------------------------------------------------------
// D3D9 resources
// ---------------------------------------------------------------------------
static void ReleaseTargets(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (g_eyeSurf[i]) IDirect3DSurface9_Release(g_eyeSurf[i]);
        if (g_eyeTex[i]) IDirect3DTexture9_Release(g_eyeTex[i]);
        if (g_eyeDepth[i]) IDirect3DSurface9_Release(g_eyeDepth[i]);
        g_eyeSurf[i] = NULL; g_eyeTex[i] = NULL; g_eyeDepth[i] = NULL;
    }
    if (g_hudSurf) IDirect3DSurface9_Release(g_hudSurf);
    if (g_hudTex) IDirect3DTexture9_Release(g_hudTex);
    if (g_fullSurf) IDirect3DSurface9_Release(g_fullSurf);
    if (g_fullTex) IDirect3DTexture9_Release(g_fullTex);
    if (g_hudCopy) IDirect3DSurface9_Release(g_hudCopy);
    for (i = 0; i < 2; i++) {
        if (g_eyeMS[i]) IDirect3DSurface9_Release(g_eyeMS[i]);
        if (g_eyeMSDepth[i]) IDirect3DSurface9_Release(g_eyeMSDepth[i]);
        g_eyeMS[i] = g_eyeMSDepth[i] = NULL;
    }
    g_hudSurf = NULL; g_hudTex = NULL; g_fullSurf = NULL; g_fullTex = NULL; g_hudCopy = NULL;
    g_eyeW = g_eyeH = g_hudW = g_hudH = 0;
    g_targetsVR = 0;
}

static int OpenTexture(int w, int h, HANDLE share, IDirect3DTexture9 **tex, IDirect3DSurface9 **surf)
{
    HANDLE hnd = share;
    HRESULT hr = IDirect3DDevice9Ex_CreateTexture(g_dev, w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                                  tex, share ? &hnd : NULL);
    if (FAILED(hr)) { Log("render target %dx%d (%s) failed: %08x", w, h, share ? "shared" : "local", hr); return 0; }
    IDirect3DTexture9_GetSurfaceLevel(*tex, 0, surf);
    return 1;
}

// VR: the headset's shared textures. Side by side: local ones of the window's size.
static int CreateTargets(int vr)
{
    int i, w, h, hw, hh;
    ReleaseTargets();
    if (vr == 1) {
        if (!XR_EyeSize(&w, &h) || !XR_HudSize(&hw, &hh)) return 0;
    } else if (vr == 2) {
        w = cfg.fakeW; h = cfg.fakeH; hw = g_winW; hh = g_winH;
    } else {
        w = g_winW; h = g_winH; hw = g_winW; hh = g_winH;
    }
    for (i = 0; i < 2; i++) {
        if (!OpenTexture(w, h, vr == 1 ? XR_EyeShareHandle(i) : NULL, &g_eyeTex[i], &g_eyeSurf[i])) goto fail;
        if (FAILED(IDirect3DDevice9Ex_CreateDepthStencilSurface(g_dev, w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE,
                                                                 &g_eyeDepth[i], NULL))) { Log("eye depth %dx%d failed", w, h); goto fail; }
    }
    if (vr == 1 && cfg.msaa > 1) {
        // [VR] MSAA (as the CoD 1 mod's): the eyes are drawn into multisampled surfaces and resolved at the end of each
        // eye's pass (hk_CmdViewport). Not supported or not made: no MSAA, nothing else changes
        D3DMULTISAMPLE_TYPE type = (D3DMULTISAMPLE_TYPE)cfg.msaa;
        DWORD q1 = 0, q2 = 0;
        int ok = SUCCEEDED(IDirect3D9Ex_CheckDeviceMultiSampleType(g_d3d, g_adapter, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, FALSE, type, &q1)) &&
                 SUCCEEDED(IDirect3D9Ex_CheckDeviceMultiSampleType(g_d3d, g_adapter, D3DDEVTYPE_HAL, D3DFMT_D24S8, FALSE, type, &q2));
        for (i = 0; i < 2 && ok; i++)
            ok = SUCCEEDED(IDirect3DDevice9Ex_CreateRenderTarget(g_dev, w, h, D3DFMT_A8R8G8B8, type, 0, FALSE, &g_eyeMS[i], NULL)) &&
                 SUCCEEDED(IDirect3DDevice9Ex_CreateDepthStencilSurface(g_dev, w, h, D3DFMT_D24S8, type, 0, TRUE, &g_eyeMSDepth[i], NULL));
        if (!ok) {
            for (i = 0; i < 2; i++) {
                if (g_eyeMS[i]) IDirect3DSurface9_Release(g_eyeMS[i]);
                if (g_eyeMSDepth[i]) IDirect3DSurface9_Release(g_eyeMSDepth[i]);
                g_eyeMS[i] = g_eyeMSDepth[i] = NULL;
            }
            Log("MSAA %dx: not available, the eyes are not multisampled", cfg.msaa);
        } else Log("MSAA %dx on the eyes", cfg.msaa);
    }
    if (vr && !OpenTexture(hw, hh, vr == 1 ? XR_HudShareHandle() : NULL, &g_hudTex, &g_hudSurf)) goto fail;
    // optional: without it full-screen pictures stay on the HUD screen
    if (vr && !OpenTexture(hw, hh, vr == 1 ? XR_FullShareHandle() : NULL, &g_fullTex, &g_fullSurf)) { g_fullTex = NULL; g_fullSurf = NULL; }
    g_eyeW = w; g_eyeH = h; g_hudW = hw; g_hudH = hh;
    g_targetsVR = vr;
    Log("targets: eyes %dx%d, HUD %dx%d (%s)", w, h, hw, hh, vr == 1 ? "headset" : vr == 2 ? "fake headset" : "side by side");
    return 1;
fail:
    ReleaseTargets();
    return 0;
}

static void FlushGPU(void)
{
    int spins = 0;
    if (!g_flushQuery && FAILED(IDirect3DDevice9Ex_CreateQuery(g_dev, D3DQUERYTYPE_EVENT, &g_flushQuery))) return;
    IDirect3DQuery9_Issue(g_flushQuery, D3DISSUE_END);
    while (IDirect3DQuery9_GetData(g_flushQuery, NULL, 0, D3DGETDATA_FLUSH) == S_FALSE && spins++ < 200000) YieldProcessor();
}

// ---------------------------------------------------------------------------
// Backend: switching the frame buffer render target
// ---------------------------------------------------------------------------
static void RB_SetRenderTarget0(void)
{
    DWORD fn = p_RB_SetRenderTarget;
    __asm {
        xor eax, eax
        call fn
    }
}

static void BE_Bind(IDirect3DSurface9 *color, IDirect3DSurface9 *depth, int w, int h)
{
    gfx_rt_t *rt = (gfx_rt_t *)GFX(GFX_dx_renderTargets);
    rt->color = color;
    rt->depth = depth;
    rt->width = w;
    rt->height = h;
    MEM(int, GFX(GFX_rb_currentTarget)) = 14;   // none: the next bind really binds
    RB_SetRenderTarget0();
}

static void BE_SetTarget(int t)
{
    gfx_rt_t *rt = (gfx_rt_t *)GFX(GFX_dx_renderTargets);
    if (g_beTarget == TGT_WINDOW) g_rtSaved = *rt;
    switch (t) {
    case TGT_EYE0:
    case TGT_EYE1: {
        int e = t - TGT_EYE0;
        if (!g_eyeSurf[e]) return;
        if (g_eyeMS[e]) BE_Bind(g_eyeMS[e], g_eyeMSDepth[e], g_eyeW, g_eyeH);
        else BE_Bind(g_eyeSurf[e], g_eyeDepth[e], g_eyeW, g_eyeH);
        IDirect3DDevice9Ex_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xff000000, 1.0f, 0);
        break;
    }
    case TGT_FULLK:
        if (!g_fullSurf) return;
        BE_Bind(g_fullSurf, g_rtSaved.depth, g_hudW, g_hudH);
        if (!g_beFullFilled)
            IDirect3DDevice9Ex_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0x00000000, 1.0f, 0);
        g_beFullFilled = 1;
        break;
    case TGT_HUDK:
        if (!g_hudSurf) return;
        BE_Bind(g_hudSurf, g_rtSaved.depth, g_hudW, g_hudH);
        t = TGT_HUD;
        break;
    case TGT_HUD:
        if (!g_hudSurf) return;
        BE_Bind(g_hudSurf, g_rtSaved.depth, g_hudW, g_hudH);
        IDirect3DDevice9Ex_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0x00000000, 1.0f, 0);
        break;
    default:
        if (g_beTarget != TGT_WINDOW) BE_Bind(g_rtSaved.color, g_rtSaved.depth, g_rtSaved.width, g_rtSaved.height);
        break;
    }
    g_beTarget = t;
}

// The world is rendered once by the frontend and twice by the backend. The world view's
// commands lie between the WORLD_BEGIN and WORLD_END markers (they include the shadow maps,
// floatz and post effects of that frame). At WORLD_BEGIN the view is set up for the left eye
// and the left eye's texture is bound; at the first WORLD_END the view is set up for the right
// eye and execution jumps back to just after WORLD_BEGIN; at the second one the 2D follows.
static BYTE         *g_vp;              // the world view's viewParms (frontend data, 0x14c bytes)
static cod2_refdef_t g_eyeRef[2];       // what each eye's view is made from
static int           g_stereoReady;     // this frame's world view has eyes to render
static int           g_bePass;          // 0, 1 (left eye), 2 (right eye)
static BYTE         *g_replayStart;

static void SetupViewParms(BYTE *vp, cod2_refdef_t *fd)
{
    DWORD fn = GFX(GFX_R_SetupViewParms);
    __asm {
        push ebx
        mov ebx, vp
        push fd
        call fn
        add esp, 4
        pop ebx
    }
}

// Rebuilds the world view (camera, matrices, viewport) for one eye, as R_RenderScene does.
static void SetEyeView(int eye)
{
    float *globals = (float *)GFX(GFX_r_viewOrgFwd);
    g_projEye = g_mode == MODE_VR ? eye : -1;
    SetupViewParms(g_vp, &g_eyeRef[eye]);
    g_projEye = -1;
    memcpy(globals, g_eyeRef[eye].vieworg, 3 * sizeof(float));
    memcpy(globals + 3, g_eyeRef[eye].viewaxis[0], 3 * sizeof(float));
}

static void SideBySideComposite(void)
{
    BE_Bind(g_rtSaved.color, g_rtSaved.depth, g_rtSaved.width, g_rtSaved.height);
    if (g_eyeSurf[0] && g_eyeSurf[1]) {
        RECT l = { 0, 0, g_rtSaved.width / 2, g_rtSaved.height }, r = { g_rtSaved.width / 2, 0, g_rtSaved.width, g_rtSaved.height };
        IDirect3DDevice9Ex_StretchRect(g_dev, g_eyeSurf[0], NULL, g_rtSaved.color, &l, D3DTEXF_LINEAR);
        IDirect3DDevice9Ex_StretchRect(g_dev, g_eyeSurf[1], NULL, g_rtSaved.color, &r, D3DTEXF_LINEAR);
    }
    g_beTarget = TGT_WINDOW;
}

// Command 13 (viewport): markers; real viewports in window pixels are scaled to the eye texture
// while one is bound.
static void __cdecl hk_CmdViewport(BYTE **ctx)
{
    BYTE *cmd = *ctx;
    int *v = (int *)(cmd + 4);
    if ((v[0] & 0xffffff00) == MARKER_MAGIC) {
        int m = v[0] & 0xff;
        *ctx = cmd + *(WORD *)(cmd + 2);
        // what the backend has batched goes to the target bound until now (as the game's own
        // viewport and render target commands do): a full-screen picture lands in the eye it was meant for
        if (MEM(int, GFX(GFX_tess_pendingA)) || MEM(int, GFX(GFX_tess_pendingB))) ((PFN_Void)GFX(GFX_RB_EndTessSurface))();
        if (m == MARK_WORLD_BEGIN) {
            if (!g_stereoReady || !g_eyeSurf[0]) return;
            g_bePass = 1;
            g_replayStart = *ctx;
            BE_SetTarget(TGT_EYE0);
            SetEyeView(0);
        } else if (m == MARK_WORLD_END) {
            if (g_bePass == 1) {
                if (g_eyeMS[0]) IDirect3DDevice9Ex_StretchRect(g_dev, g_eyeMS[0], NULL, g_eyeSurf[0], NULL, D3DTEXF_NONE);   // MSAA: resolved
                g_bePass = 2;
                BE_SetTarget(TGT_EYE1);
                SetEyeView(1);
                ctx[0] = g_replayStart;      // again, from the top level
                ((int *)ctx)[1] = 0;
            } else if (g_bePass == 2) {
                if (g_eyeMS[1]) IDirect3DDevice9Ex_StretchRect(g_dev, g_eyeMS[1], NULL, g_eyeSurf[1], NULL, D3DTEXF_NONE);
                g_bePass = 0;
                g_stereoReady = 0;
                if (g_mode == MODE_VR) BE_SetTarget(TGT_HUD);
                else SideBySideComposite();
            }
        } else {
            BE_SetTarget(m);
        }
        return;
    }
    if ((g_beTarget == TGT_EYE0 || g_beTarget == TGT_EYE1) && g_rtSaved.width > 0 && g_rtSaved.height > 0 &&
        (g_eyeW != g_rtSaved.width || g_eyeH != g_rtSaved.height)) {
        BYTE tmp[0x14];
        BYTE *tp = tmp;
        int *t = (int *)(tmp + 4);
        memcpy(tmp, cmd, sizeof(tmp));
        t[0] = v[0] * g_eyeW / g_rtSaved.width;
        t[1] = v[1] * g_eyeH / g_rtSaved.height;
        t[2] = v[2] * g_eyeW / g_rtSaved.width;
        t[3] = v[3] * g_eyeH / g_rtSaved.height;
        orig_CmdViewport(&tp);
        *ctx = cmd + *(WORD *)(cmd + 2);
        return;
    }
    orig_CmdViewport(ctx);
}

// Diagnostics: every command handler wrapped, logging what the backend executes.
static PFN_CmdHandler g_origCmd[GFX_CMD_COUNT];
static int            g_tracing;   // frames left to log (counted at EndFrame)

static void TraceCmd(int op, BYTE *cmd)
{
    int *v = (int *)(cmd + 4);
    if (op >= 1 && op <= 3) Log("  cmd %2d @%p -> %08x", op, cmd, v[0]);
    else if (op == GFX_CMD_SETVIEWPORT) Log("  cmd 13 @%p viewport %08x %d %d %d", cmd, v[0], v[1], v[2], v[3]);
    else if (op == 14) Log("  cmd 14 @%p render target %d", cmd, v[0]);
    else Log("  cmd %2d @%p size %d", op, cmd, *(WORD *)(cmd + 2));
}

#define TRACE_THUNK(n) static void __cdecl TraceCmd##n(BYTE **ctx) { if (g_tracing) TraceCmd(n, *ctx);     if (n == GFX_CMD_SETVIEWPORT) hk_CmdViewport(ctx); else g_origCmd[n](ctx); }
TRACE_THUNK(1) TRACE_THUNK(2) TRACE_THUNK(3) TRACE_THUNK(4) TRACE_THUNK(5) TRACE_THUNK(6) TRACE_THUNK(7)
TRACE_THUNK(8) TRACE_THUNK(9) TRACE_THUNK(10) TRACE_THUNK(11) TRACE_THUNK(12) TRACE_THUNK(13) TRACE_THUNK(14)
TRACE_THUNK(15) TRACE_THUNK(16) TRACE_THUNK(17) TRACE_THUNK(18) TRACE_THUNK(19) TRACE_THUNK(20) TRACE_THUNK(21)
TRACE_THUNK(22) TRACE_THUNK(23) TRACE_THUNK(24) TRACE_THUNK(25) TRACE_THUNK(26) TRACE_THUNK(27) TRACE_THUNK(28)
TRACE_THUNK(29) TRACE_THUNK(30) TRACE_THUNK(31) TRACE_THUNK(32) TRACE_THUNK(33)
static PFN_CmdHandler g_traceThunks[GFX_CMD_COUNT] = { NULL, TraceCmd1, TraceCmd2, TraceCmd3, TraceCmd4, TraceCmd5,
    TraceCmd6, TraceCmd7, TraceCmd8, TraceCmd9, TraceCmd10, TraceCmd11, TraceCmd12, TraceCmd13, TraceCmd14, TraceCmd15,
    TraceCmd16, TraceCmd17, TraceCmd18, TraceCmd19, TraceCmd20, TraceCmd21, TraceCmd22, TraceCmd23, TraceCmd24,
    TraceCmd25, TraceCmd26, TraceCmd27, TraceCmd28, TraceCmd29, TraceCmd30, TraceCmd31, TraceCmd32, TraceCmd33 };

static void Marker(int target)
{
    p_AddCmdViewport(MARKER_MAGIC | target, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// Frontend: the world view from each eye
// ---------------------------------------------------------------------------
// The yaw of the soldier's body: the game's view yaw minus the aim the mod put into it.
static int InLevel(void);

// A scripted scene (as the CoD 1 mod's g_viewLocked / KeepLook): the game holds its view and does not follow the aim
// written into it. Then none of the aim is in the view and the head turns freely on top of it; with [Comfort] KeepLook
// the picture keeps the way the body faced when the scene took the view (g_sceneBase) and follows the scene's own turns
// from there, instead of jumping to where the game points.
static int   g_viewLocked, g_sceneHold;
static float g_sceneBase, g_sceneV0;

static float BodyYaw(const float viewFwd[3])
{
    if (g_sceneHold) return AngleNormalize180(g_sceneBase + YawOf(viewFwd) - g_sceneV0);
    if (g_viewLocked) return YawOf(viewFwd);
    return YawOf(viewFwd) - g_aimYawCmd;
}

// Once per world frame, from the game's view: does it follow the aim? Held for a good while (45 frames; drawing a weapon
// holds it briefly) = locked; turning with the aim again = free.
static void UpdateViewLock(const float viewFwd[3])
{
    static float lastView, lastAim, accView, accAim, prevBody;
    static int init, heldFrames;
    float v = YawOf(viewFwd);
    int hold;
    if (g_mode != MODE_VR || !InLevel()) { init = 0; g_viewLocked = g_sceneHold = 0; return; }
    if (init) {
        accView = accView * 0.92f + fabsf(AngleNormalize180(v - lastView));
        accAim = accAim * 0.92f + fabsf(AngleNormalize180(g_aimYawCmd - lastAim));
        heldFrames = (accAim > 4.0f && accView < 0.15f * accAim) ? heldFrames + 1 : 0;
        if (!g_viewLocked && heldFrames >= 45) { g_viewLocked = 1; Log("view held by the game: the head turns freely"); }
        else if (g_viewLocked && accAim > 1.0f && accView > 0.5f * accAim) { g_viewLocked = 0; Log("view follows the aim again"); }
    }
    lastView = v;
    lastAim = g_aimYawCmd;
    init = 1;
    hold = cfg.keepLook && g_viewLocked;
    if (hold && !g_sceneHold) {
        g_sceneBase = prevBody;
        g_sceneV0 = v;
        Log("scene: the view is kept where the body faced (%.1f; the game's view is %.1f now)", g_sceneBase, g_sceneV0);
    }
    g_sceneHold = hold;
    prevBody = BodyYaw(viewFwd);
}

// A tracked point relative to the soldier's eye, metres, tracking axes. The eye is the tracking
// origin (where the head was when the headset started) until a recenter moves it to the head;
// HeightOffset makes the player taller or shorter. Rotation only: relative to the head.
static void TrackingRel(const float p[3], float rel[3])
{
    int i;
    if (cfg.positional) {
        for (i = 0; i < 3; i++) rel[i] = p[i] - g_ref[i];
        rel[2] += cfg.heightOffset + g_crouchCorr;
    } else {
        for (i = 0; i < 3; i++) rel[i] = p[i] - g_head.pos[i];
    }
}

// ... and in the game's world, from the view origin and the body's yaw.
static void TrackingToWorld(const float p[3], const float org[3], float body, float out[3])
{
    float rel[3], w[3];
    int i;
    TrackingRel(p, rel);
    RotateZ(rel, body, w);
    for (i = 0; i < 3; i++) out[i] = org[i] + w[i] * cfg.worldScale;
}

static float g_headPush[3];   // world units added to both eyes this frame: the head kept out of walls (UpdateHeadPush)

static void ApplyEyePose(cod2_refdef_t *fd, const vr_pose_t *eye, const float org[3], const float axis[3][3])
{
    float body = BodyYaw(axis[0]);
    int i;
    for (i = 0; i < 3; i++) RotateZ(eye->axis[i], body, fd->viewaxis[i]);
    TrackingToWorld(eye->pos, org, body, fd->vieworg);
    for (i = 0; i < 3; i++) fd->vieworg[i] += g_headPush[i];
}

// Head collision (as the CoD 1 mod's UpdateHeadPush, [VR] HeadCollision): the real head can lean or walk
// past where the soldier stands, into a wall, a door frame or a corner. The way from the soldier's eye (the
// game's view origin, inside his box) to the head is traced against the map; when something is in the way
// the camera stops HEAD_WALL_GAP units before it.
#define HEAD_WALL_GAP 4.0f
static float WorldTrace(const float start[3], const float end[3], int passEnt, int mask, float normal[3], int *hit);

static void UpdateHeadPush(const float org[3], const float axis[3][3])
{
    float head[3], p[3], len, frac, keep;
    int i;
    g_headPush[0] = g_headPush[1] = g_headPush[2] = 0;
    if (!cfg.headCollision || !cfg.positional) return;
    TrackingToWorld(g_head.pos, org, BodyYaw(axis[0]), head);
    for (i = 0; i < 3; i++) p[i] = head[i] - org[i];
    len = sqrtf(Dot(p, p));
    if (len < 0.5f) return;
    frac = WorldTrace(org, head, 0, 1, NULL, NULL);
    if (frac >= 1.0f) return;
    keep = frac * len - HEAD_WALL_GAP;
    if (keep < 0) keep = 0;
    for (i = 0; i < 3; i++) g_headPush[i] = p[i] * (keep / len) - p[i];
    {
        static DWORD last;
        if (GetTickCount() - last > 5000) { last = GetTickCount(); Log("head collision: the head is %.0f units into a wall, kept out", len - keep); }
    }
}

// The gun hand with the gun's angle in it ([Controllers] WeaponPitch / WeaponYaw), 0 = no gun
// hand (WeaponInHand off, or the controller is not tracked). FakeHand: a still hand held out in
// front of the right side of the chest, for tests without a headset.
static int GunPose(vr_pose_t *out)
{
    vr_pose_t h;
    float cy = cosf(RAD(cfg.wYaw)), sy = sinf(RAD(cfg.wYaw)), cp = cosf(RAD(cfg.wPitch)), sp = sinf(RAD(cfg.wPitch)), f[3];
    int i;
    if (!cfg.weaponInHand) return 0;
    if (cfg.fakeHmd && cfg.fakeHand) {
        memset(&h, 0, sizeof(h));
        h.pos[0] = 0.35f; h.pos[1] = -0.20f; h.pos[2] = -0.25f;
        h.axis[0][0] = h.axis[1][1] = h.axis[2][2] = 1;
    } else if (!Controls_GunHand(&h)) {
        return 0;
    }
    *out = h;
    if (Controls_TwoHanded()) return 1;   // the barrel is the line between the hands: no angle added
    for (i = 0; i < 3; i++) {
        f[i] = cy * h.axis[0][i] + sy * h.axis[1][i];
        out->axis[1][i] = cy * h.axis[1][i] - sy * h.axis[0][i];
    }
    for (i = 0; i < 3; i++) out->axis[0][i] = cp * f[i] + sp * h.axis[2][i];
    Cross(out->axis[0], out->axis[1], out->axis[2]);   // up = forward x left
    return 1;
}

// ---------------------------------------------------------------------------
// Camera shake (as the CoD 1 mod's FilterShake, [Comfort] ShakeKeep): earthquakes and explosions shake the
// game's view origin, which in VR shakes the whole world around a head that holds still. The origin is followed
// by a smoothing filter that tracks its steady motion (walking, being carried); what it does around that, beyond
// a centimetre or so (a step's bob stays), is kept only in part (ShakeKeep). A jump of more than 24 units (a
// teleport, a load) is taken at once. Position only: the head turns the view. Worked out once per frame
// (g_shakeOfs) and used for the eyes and the gun in the hand alike.
// ---------------------------------------------------------------------------
static void ShakeFilter(const float raw[3])
{
    static float s[3], b[3];   // smoothed origin, and how fast it is moving
    static int init, frame = -1;
    static LARGE_INTEGER last;
    LARGE_INTEGER now, freq;
    float dt, a;
    int i;
    if (frame == g_frameNo) return;
    frame = g_frameNo;
    g_shakeOfs[0] = g_shakeOfs[1] = g_shakeOfs[2] = 0;
    if (cfg.shakeKeep >= 0.999f) { init = 0; return; }
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    dt = last.QuadPart ? (float)((double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart) : 0.0f;
    last = now;
    if (!init || dt <= 0 || dt > 0.25f) {
        for (i = 0; i < 3; i++) { s[i] = raw[i]; b[i] = 0; }
        init = 1;
        return;
    }
    a = 1.0f - expf(-dt / 0.12f);
    for (i = 0; i < 3; i++) {
        float pred = s[i] + b[i] * dt, d = raw[i] - pred, ns, out;
        if (fabsf(d) > 24.0f) { s[i] = raw[i]; b[i] = 0; continue; }
        ns = pred + a * d;
        b[i] += a * ((ns - s[i]) / dt - b[i]);
        s[i] = ns;
        d = raw[i] - ns;
        out = ns + (fabsf(d) > 1.0f ? (d > 0 ? 1.0f : -1.0f) * (1.0f + (fabsf(d) - 1.0f) * cfg.shakeKeep) : d);
        g_shakeOfs[i] = out - raw[i];
    }
}

// ---------------------------------------------------------------------------
// The view weapon in the gun hand
// ---------------------------------------------------------------------------
// CG_AddViewWeapon placed the gun from the game's view (cg.refdef: the aim, which is the gun
// hand's) with cg_gun_*, the weapon's own offsets, its sway and the kick of a shot. That placement
// is kept relative to one spot of the flat game's gun - WeaponOffset*, the grip as seen from the
// eye - and that spot goes into the hand: the gun's animations and recoil stay, the camera does
// not carry it.
// The arms are hidden (the launcher's Player hands = Hidden made main\zz_cod2vr.iwd): nothing to pose. Looked up every 5 s.
static int ArmsHidden(void)
{
    static DWORD next;
    static int hidden;
    if (GetTickCount() >= next) {
        char p[MAX_PATH];
        _snprintf(p, sizeof(p), "%s\\main\\zz_cod2vr.iwd", g_dir);
        hidden = GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
        next = GetTickCount() + 5000;
    }
    return hidden;
}

static void __cdecl FixViewWeapon(BYTE *ent, BYTE *ps)
{
    const cod2_refdef_t *rd = (const cod2_refdef_t *)ADDR_cg_refdef;
    float *org = (float *)(ent + VIEWENT_origin), (*ax)[3] = (float (*)[3])(ent + VIEWENT_axis);
    float lo[3], rk[3][3], d[3], hand[3], hax[3][3], body;
    vr_pose_t gun;
    int i, k;
    static DWORD nextDiag;
    static int diags;
    float base[3];
    int inLeft;
    if (g_mode != MODE_VR || !GunPose(&gun)) { Proxy_SetMirrorGun(0); return; }
    if (fabsf(Dot(rd->viewaxis[0], rd->viewaxis[0]) - 1.0f) > 0.05f) return;   // not a view (should not happen)
    inLeft = !Controls_DominantHand();
    {
        // a grenade in the hand that took it (the view model is then the grenade's): placed in that hand
        vr_pose_t gh;
        if (Controls_GrenadePose(&gh)) { gun = gh; inLeft = Controls_GrenadeHand() == 0; }
    }
    for (i = 0; i < 3; i++) d[i] = org[i] - rd->vieworg[i];
    for (i = 0; i < 3; i++) lo[i] = Dot(d, rd->viewaxis[i]);
    for (k = 0; k < 3; k++) for (i = 0; i < 3; i++) rk[k][i] = Dot(ax[k], rd->viewaxis[i]);
    if (diags < 5 && GetTickCount() >= nextDiag) {
        // what the game's own placement is, relative to its view (forward, left, up): small numbers
        // mean cg.refdef already holds this frame's view when the gun is added
        nextDiag = GetTickCount() + 2000;
        diags++;
        Log("view weapon: game places it %.1f %.1f %.1f from the view (forward, left, up), axis forward %.2f %.2f %.2f",
            lo[0], lo[1], lo[2], rk[0][0], rk[0][1], rk[0][2]);
        if (ps) Log("player: ps %p (%s), eye height %.1f, stats %d %d %d %d", ps, ps == (BYTE *)ADDR_cg_ps ? "cg.predictedPlayerState" : "another",
                    rd->vieworg[2] - MEM(float, ps + PS_OFS_origin + 8), MEM(int, ps + PS_OFS_stats), MEM(int, ps + PS_OFS_stats + 4),
                    MEM(int, ps + PS_OFS_stats + 8), MEM(int, ps + PS_OFS_stats + 12));
    }
    body = BodyYaw(rd->viewaxis[0]);
    // from the view without its shake, as the eyes are (the gun stays in the hand while the camera shakes)
    ShakeFilter(rd->vieworg);
    for (i = 0; i < 3; i++) base[i] = rd->vieworg[i] + g_shakeOfs[i];
    TrackingToWorld(gun.pos, base, body, hand);
    for (k = 0; k < 3; k++) RotateZ(gun.axis[k], body, hax[k]);
    lo[0] -= cfg.wofs[0];
    lo[1] += cfg.wofs[1];   // left = -right
    lo[2] -= cfg.wofs[2];
    for (i = 0; i < 3; i++) org[i] = hand[i] + lo[0] * hax[0][i] + lo[1] * hax[1][i] + lo[2] * hax[2][i];
    for (k = 0; k < 3; k++)
        for (i = 0; i < 3; i++) ax[k][i] = rk[k][0] * hax[0][i] + rk[k][1] * hax[1][i] + rk[k][2] * hax[2][i];
    if (cfg.mirrorLeft && inLeft) {
        // A gun in the left hand drawn mirrored (as the CoD 1 mod's MirrorLeftHand): the entity's left axis turned round, the
        // grip spot kept in the hand (its model point g: hand = org + g0 ax0 + g1 ax1 + g2 ax2 before, with -ax1 after). Its
        // triangles then wind the other way: the backend flips the cull mode while it draws the view weapon (d3d9_proxy.c).
        float g[3];
        for (i = 0; i < 3; i++) d[i] = hand[i] - org[i];
        for (k = 0; k < 3; k++) g[k] = Dot(d, ax[k]);
        for (i = 0; i < 3; i++) {
            ax[1][i] = -ax[1][i];
            org[i] = hand[i] - g[0] * ax[0][i] - g[1] * ax[1][i] - g[2] * ax[2][i];
        }
        Proxy_SetMirrorGun(1);
    } else Proxy_SetMirrorGun(0);
    if (cfg.armBones && !ArmsHidden()) {
        // the soldier's arms from the player's shoulders to the hands (arms.c)
        arms_in_t a;
        vr_pose_t off;
        static LARGE_INTEGER last;
        LARGE_INTEGER now, freq;
        int w = ViewWeaponIndex();
        memset(&a, 0, sizeof(a));
        TrackingToWorld(g_head.pos, base, body, a.eye);
        RotateZ(g_head.axis[0], body, a.headFwd);
        memcpy(a.gunAxis, hax, sizeof(a.gunAxis));
        if (Controls_HandPose(Controls_DominantHand() ? 0 : 1, &off)) {
            a.offValid = 1;
            TrackingToWorld(off.pos, base, body, a.offPos);
            for (k = 0; k < 3; k++) RotateZ(off.axis[k], body, a.offAxis[k]);
        }
        a.twoHanded = Controls_TwoHanded();
        a.rightHanded = Controls_DominantHand();
        a.worldScale = cfg.worldScale;
        a.shoulderDown = cfg.shoulder[0];
        a.shoulderSide = cfg.shoulder[1];
        a.shoulderBack = cfg.shoulder[2];
        a.armLength = cfg.armLength > 0 ? cfg.armLength : 0.36f * 60.0f / cfg.worldScale;   // the soldier's eye height (60 units)
        memcpy(a.offHand, cfg.offHand, sizeof(a.offHand));
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        a.dt = last.QuadPart ? (float)((double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart) : 0.0f;
        if (a.dt > 0.1f) a.dt = 0.1f;
        last = now;
        if (w > 0 && w < 256) Arms_Pose(MEM(BYTE *, ADDR_weaponInfo + w * WEAPONINFO_SIZE), org, (const float (*)[3])ax, &a);
    }
}

// The view weapon's animations (a reload above all) move the game's camera by their tag_camera bone (0x4af1f0). In the
// headset that moves the world around a head that holds still: in VR it is left out ([Comfort] WeaponCamera 0).
static void __cdecl hk_ViewModelCamera(void)
{
    if (g_mode == MODE_VR && !cfg.weaponCamera) return;
    ((void (__cdecl *)(void))ADDR_CG_ViewModelCamera)();
}

// The script's playerADS() (how far the player aims down the sights, 0 .. 1). In VR the gun is aimed with the hand, the
// sights at the eye: the scripts that wait for the flat game's aim (the training's second bottles, "aim and shoot")
// get 1 (fully aimed), whatever the game's own ADS.
static void __cdecl hk_PlayerADS(int entref)
{
    if (g_mode == MODE_VR) { ((void (__cdecl *)(float))ADDR_Scr_AddFloat)(1.0f); return; }
    ((void (__cdecl *)(int))ADDR_Scr_PlayerADS)(entref);
}

// The script's openMenu(). The training (moscow.gsc) asks after the teddy bears whether to invert the mouse's pitch
// (ui/scriptmenus/invert_axis1 / 2, and the "i" ones): no sense in VR. Those menus are not opened; the answer "no" goes
// to the script as the menu's own No button sends it (ScriptMenuAnswerFrame), a moment later, when the script waits.
static char  g_menuAnswerName[32];
static DWORD g_menuAnswerAt;

static void __cdecl hk_OpenMenu(int entref)
{
    if (g_mode == MODE_VR && cfg.skipInvertMenu) {
        const char *name = NULL;
        DWORD fn = ADDR_Scr_GetConstString;
        int id = 0;
        __asm {
            xor eax, eax
            call fn
            mov id, eax
        }
        if (id) name = (const char *)(MEM(DWORD, ADDR_scrMemTreeBuf) + id * 8 + 4);
        if (name && !_strnicmp(name, "invert_axis", 11)) {
            strncpy(g_menuAnswerName, name, sizeof(g_menuAnswerName) - 1);
            g_menuAnswerAt = GetTickCount() + 300;
            Log("menu: %s not opened in VR, answered no", name);
            ((void (__cdecl *)(int))ADDR_Scr_AddInt)(1);
            return;
        }
    }
    ((void (__cdecl *)(int))ADDR_Scr_OpenMenu)(entref);
}

static void ScriptMenuAnswerFrame(void)
{
    char line[96];
    int i;
    if (!g_menuAnswerName[0] || GetTickCount() < g_menuAnswerAt) return;
    __try {
        for (i = 0; i < 32; i++) {
            int id = MEM(WORD, ADDR_cl_scriptMenus + i * 2);
            if (id && !_stricmp((const char *)(MEM(DWORD, ADDR_scrMemTreeBuf) + id * 8 + 4), g_menuAnswerName)) break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        i = 32;
    }
    if (i < 32) {
        _snprintf(line, sizeof(line), "cmd mr %d accept_normal\n", i);
        Controls_Command(line);
    } else Log("menu: %s is not among the client's script menus, no answer sent", g_menuAnswerName);
    g_menuAnswerName[0] = 0;
}

// The impact marks (decals: explosions' scorch on the ground, bullet holes) and parts of the effects are tested against
// the game's own view (the aim) by FX_CullSphere: aiming at the floor, the scorch ahead vanished. In VR ([VR] FxAlways)
// the test says "visible" (the eyes' own culling still runs in the renderer).
static int g_noViewCull;

static __declspec(naked) void hk_FxCullSphere(void)
{
    __asm {
        cmp g_noViewCull, 0
        je orig
        xor al, al
        ret 4
    orig:
        mov eax, ADDR_FX_CullSphere
        jmp eax
    }
}

static __declspec(naked) void hk_FxCullSphere2(void)
{
    __asm {
        cmp g_noViewCull, 0
        je orig
        xor al, al
        ret 4
    orig:
        mov eax, ADDR_FX_CullSphere2
        jmp eax
    }
}

// The game's texts the mod changes in VR (the training's hints that ask for the keyboard's way of doing things): the
// reference (no '@') and the text; "" hides the hint.
static const char *__cdecl LocalizeOverride(const char *ref)
{
    static const struct { const char *ref, *text; } over[] = {
        { "MOSCOW_PLATFORM_TOGGLE_ADS", "" },        // "Press [..] to aim down the sight": the gun is aimed with the hand
        { "MOSCOW_PLATFORM_HOLD_TO_ADS", "" },
        { "MOSCOW_PLATFORM_PUSH_TO_MELEE_NORM", "Strike the mannequin with your weapon: hit it with the muzzle or the stock." },
        { "MOSCOW_PLATFORM_PUSH_TO_MELEE_BREATH", "Strike the mannequin with your weapon: hit it with the muzzle or the stock." },
        { "MOSCOW_PLATFORM_CLICK_TO_MELEE", "Strike the mannequin with your weapon: hit it with the muzzle or the stock." },
    };
    int i;
    if (g_mode != MODE_VR || !ref) return NULL;
    if (*ref == '@') ref++;
    for (i = 0; i < (int)(sizeof(over) / sizeof(over[0])); i++)
        if (!_stricmp(ref, over[i].ref)) return over[i].text;
    return NULL;
}

static __declspec(naked) void hk_StringEdGetString(void)
{
    __asm {
        pushad
        push eax
        call LocalizeOverride
        add esp, 4
        test eax, eax
        je orig
        mov [esp + 28], eax      // pushad's EAX: the text is returned
        popad
        ret
    orig:
        popad
        sub esp, 0x20            // the 5 bytes moved here
        push ebx
        push esi
        push ADDR_SEH_StringEd_GetString + 5
        ret
    }
}

// The player's shots leave the gun (as a real one): the game fires them from the soldier's eye along the view, and with
// the gun at the hip the line from the eye could go through a tree beside you that the barrel was clear of (the shot
// hit it half a metre off). In VR ([Controllers] FireFromGun 1) the bullet starts at the muzzle (MuzzleDistance metres
// along the controller's barrel line) and goes along the barrel, the spread around it; the muzzle is never past a wall
// (the line from the eye to it is traced first). The crosshair stays on that line (ConvergeDistance ahead).
// Where the shot's line starts (tracking space), along the barrel's direction. Aiming through the sights, your eye is on
// the sight line, a few centimetres above the bore (as with a real gun): the shots then follow the line through that eye
// (they hit what the sights cover, not a little below). The other times (from the hip) they follow the bore, from the
// muzzle. The eye counts as aiming when it is within SightReach of the barrel's line, ahead of the grip; the nearer eye
// (the one you aim with) is taken. The crosshair is drawn on the same line.
static float g_crossHitW[3];   // where the crosshair was drawn last, in the world (the shot log compares)
static int   g_crossHitOk;

static void ShotOrigin(const vr_pose_t *gun, float out[3])
{
    float m[3], best = 1e9f;
    int i, k, pick = -1;
    for (i = 0; i < 3; i++) m[i] = gun->pos[i] + gun->axis[0][i] * cfg.muzzleDist;
    for (k = 0; k < 2; k++) {
        float r[3], along, perp[3], d;
        for (i = 0; i < 3; i++) r[i] = g_eyes[k].pos[i] - gun->pos[i];
        along = Dot(r, gun->axis[0]);
        if (along > 0.05f) continue;   // the eye is behind the grip when aiming: not past it
        for (i = 0; i < 3; i++) perp[i] = r[i] - gun->axis[0][i] * along;
        d = sqrtf(Dot(perp, perp));
        if (d < best) { best = d; pick = k; }
    }
    if (pick < 0 || best > cfg.sightReach) { memcpy(out, m, 12); return; }
    {
        // on the sight line (through the eye, along the barrel), level with the muzzle
        float r[3], along;
        for (i = 0; i < 3; i++) r[i] = m[i] - g_eyes[pick].pos[i];
        along = Dot(r, gun->axis[0]);
        for (i = 0; i < 3; i++) out[i] = g_eyes[pick].pos[i] + gun->axis[0][i] * along;
    }
}

static void __cdecl VR_BulletFromGun(BYTE *wp, BYTE *attacker)
{
    vr_pose_t gun;
    float body, p[3], m[3], eye[3], fwd[3], left[3], up[3], f;
    int i;
    if (g_mode != MODE_VR || !cfg.fireFromGun || attacker != (BYTE *)ADDR_g_entities || !wp || !g_lastWorld || !GunPose(&gun)) return;
    body = BodyYaw(g_lastViewFwd);
    ShotOrigin(&gun, p);
    TrackingToWorld(p, g_lastViewOrg, body, m);
    memcpy(eye, wp + 0x24, 12);
    f = WorldTrace(eye, m, 0, MASK_SHOT, NULL, NULL);
    if (f < 1.0f)   // a wall between the eye and the muzzle: start a little short of it
        for (i = 0; i < 3; i++) m[i] = eye[i] + (m[i] - eye[i]) * (f > 0.05f ? f - 0.05f : 0.0f);
    RotateZ(gun.axis[0], body, fwd);
    RotateZ(gun.axis[1], body, left);
    RotateZ(gun.axis[2], body, up);
    for (i = 0; i < 3; i++) left[i] = -left[i];   // the game's right
    memcpy(wp, fwd, 12);
    memcpy(wp + 0x0c, left, 12);
    memcpy(wp + 0x18, up, 12);
    memcpy(wp + 0x24, m, 12);
    {
        // the first shots: where the line meets the world (no spread), against where the crosshair was drawn
        static int logged;
        if (logged < 12) {
            float end[3], hit[3], fr;
            logged++;
            for (i = 0; i < 3; i++) end[i] = m[i] + fwd[i] * 8192.0f;
            fr = WorldTrace(m, end, 0, MASK_SHOT, NULL, NULL);
            for (i = 0; i < 3; i++) hit[i] = m[i] + fwd[i] * 8192.0f * fr;
            Log("shot: from %.1f %.1f %.1f (the game's eye %.1f %.1f %.1f, the view %.1f %.1f %.1f) along %.3f %.3f %.3f: hits %.1f %.1f %.1f; "
                "crosshair %s %.1f %.1f %.1f", m[0], m[1], m[2], eye[0], eye[1], eye[2], g_lastViewOrg[0], g_lastViewOrg[1], g_lastViewOrg[2],
                fwd[0], fwd[1], fwd[2], hit[0], hit[1], hit[2], g_crossHitOk ? "at" : "(none)", g_crossHitW[0], g_crossHitW[1], g_crossHitW[2]);
        }
    }
}

static __declspec(naked) void hk_BulletFire(void)
{
    __asm {
        pushad
        push ebx
        push eax
        call VR_BulletFromGun
        add esp, 8
        popad
        mov ecx, ADDR_Bullet_Fire   // ecx: not an argument (Bullet_Fire sets it before use)
        jmp ecx
    }
}

// fx_cull: kept at 0 in VR ([VR] FxAlways), so smoke, fire and the rest do not vanish when the gun points away.
static void FxCullFrame(void)
{
    static DWORD next;
    DWORD cv;
    g_noViewCull = g_mode == MODE_VR && cfg.fxAlways;
    if (!cfg.fxAlways || GetTickCount() < next) return;
    next = GetTickCount() + 1000;
    cv = MEM(DWORD, ADDR_dvar_fx_cull);
    if (cv && MEM(BYTE, cv + 8)) { Controls_Command("set fx_cull 0\n"); Log("effects: fx_cull 0 (drawn wherever the gun points)"); }
}

// CG_FireWeapon (every shot event the cgame plays): the player's own shot buzzes the gun hand.
static void __cdecl OnFireWeapon(const BYTE *cent)
{
    const BYTE *snap = MEM(const BYTE *, ADDR_cg_snap);
    static int logged;
    if (g_mode != MODE_VR || !cent || !snap || cfg.hapShots <= 0) return;
    if (MEM(WORD, cent + CENT_OFS_number) != (WORD)MEM(int, snap + SNAP_OFS_clientNum)) return;
    XR_Haptic(Controls_DominantHand(), cfg.hapShots, 45);
    if (!logged) { logged = 1; Log("shot: the gun hand vibrates (HapticsShots %.2f)", cfg.hapShots); }
}

// Being hit (as the CoD 1 mod, [Controllers] HapticsDamage): both controllers shake, harder and longer for more
// damage, when the playerState's damageEvent changes (damageCount: how much); a long full one at death (the
// death screen opening: entity 0's health is no sign of it in CoD2).
static void UpdateDamageHaptics(void)
{
    static int lastEvent = -1, wasDead, logged;
    const BYTE *ps = (const BYTE *)ADDR_cg_ps;
    int ev, dmg, dead;
    if (!InLevel()) { lastEvent = -1; wasDead = 0; return; }
    dead = MEM(int, ADDR_cg_deadScreenTime) != 0;
    if (dead && !wasDead && cfg.hapDamage > 0) { XR_Haptic(0, cfg.hapDamage / 0.6f, 600); XR_Haptic(1, cfg.hapDamage / 0.6f, 600); }
    wasDead = dead;
    ev = MEM(int, ps + PS_OFS_damageEvent) & 0xff;
    dmg = MEM(int, ps + PS_OFS_damageCount) & 0x7f;
    if (lastEvent < 0 || !g_lastWorld) { lastEvent = ev; return; }   // a new level, a load: where the count stands now
    if (ev == lastEvent) return;
    lastEvent = ev;
    if (logged < 5) { logged++; Log("hit: damage %d (damageEvent %d)", dmg, ev); }
    if (cfg.hapDamage > 0 && !dead) {
        float amp = 0.45f + dmg / 60.0f;
        int ms = 90 + dmg * 3;
        if (amp > 1.0f) amp = 1.0f;
        if (ms > 300) ms = 300;
        amp *= cfg.hapDamage / 0.6f;
        XR_Haptic(0, amp, ms);
        XR_Haptic(1, amp, ms);
    }
}

// Detour at CG_FireWeapon's start: its first 5 bytes (sub esp, 10h; push ebx; push ebp) run here.
static __declspec(naked) void hk_FireWeapon(void)
{
    __asm {
        pushad
        mov eax, [esp + 36]
        push eax
        call OnFireWeapon
        add esp, 4
        popad
        sub esp, 0x10
        push ebx
        push ebp
        mov eax, ADDR_CG_FireWeapon + 5
        jmp eax
    }
}

// The call 0x4b6ecc of CG_AddViewWeapon: entity in ESI, playerState the first argument.
static __declspec(naked) void hk_AddPlayerWeapon(void)
{
    __asm {
        pushad
        mov eax, [esp + 36]
        push eax
        push esi
        call FixViewWeapon
        add esp, 8
        popad
        mov eax, ADDR_CG_AddPlayerWeapon
        jmp eax
    }
}

static void SamplePitchOffset(const cod2_refdef_t *fd)
{
    float raw = AngleNormalize180(PitchOf(fd->viewaxis[0]) - g_clPitchCmd);
    if (!g_pitchOffsetInit || fabsf(raw - g_pitchOffset) > 15.0f) { g_pitchOffset = raw; g_pitchOffsetInit = 1; }
    else g_pitchOffset += (raw - g_pitchOffset) * 0.05f;
}

// The world view: eye refdefs for the backend, and one frontend pass from between the eyes with
// a frustum that holds both (union of the eyes' fovs plus a margin), so what either eye sees is
// in the frame's draw lists.
static void RenderStereo(cod2_refdef_t *fd)
{
    cod2_refdef_t saved = *fd;
    BYTE *fe = (BYTE *)MEM(DWORD, GFX(GFX_frontEndData));
    int k, i;

    if (g_mode == MODE_VR) UpdateHeadPush(saved.vieworg, saved.viewaxis);
    for (k = 0; k < 2; k++) {
        cod2_refdef_t *r = &g_eyeRef[k];
        *r = saved;
        r->x = 0;
        r->y = 0;
        r->width = g_eyeW;
        r->height = g_eyeH;
        if (g_mode == MODE_VR) {
            ApplyEyePose(r, &g_eyes[k], saved.vieworg, saved.viewaxis);
        } else {
            float shift = (k == 0 ? 0.5f : -0.5f) * cfg.separation;   // viewaxis[1] points left
            for (i = 0; i < 3; i++) r->vieworg[i] += r->viewaxis[1][i] * shift;
        }
    }
    *fd = g_eyeRef[0];
    for (i = 0; i < 3; i++) fd->vieworg[i] = 0.5f * (g_eyeRef[0].vieworg[i] + g_eyeRef[1].vieworg[i]);
    if (g_mode == MODE_VR) {
        float tx = 0, ty = 0;
        for (k = 0; k < 2; k++) {
            tx = max(tx, max(fabsf(g_eyes[k].tanLeft), fabsf(g_eyes[k].tanRight)));
            ty = max(ty, max(fabsf(g_eyes[k].tanUp), fabsf(g_eyes[k].tanDown)));
        }
        fd->fov_x = g_eyeRef[0].fov_x = g_eyeRef[1].fov_x = min(170.0f, 2.0f * DEG(atanf(tx)) + 2.0f * cfg.cullMargin);
        fd->fov_y = g_eyeRef[0].fov_y = g_eyeRef[1].fov_y = min(170.0f, 2.0f * DEG(atanf(ty)) + 2.0f * cfg.cullMargin);
    }
    // R_RenderScene sets up the next viewParms of the frame
    g_vp = fe + GFX_FE_viewParms + MEM(int, fe + GFX_FE_viewParmsCount) * GFX_VIEWPARMS_SIZE;
    g_stereoReady = 1;
    Marker(MARK_WORLD_BEGIN);
    orig_RenderScene(fd);
    Marker(MARK_WORLD_END);
    *fd = saved;
}

static void UpdateRoomScale(const float org[3], const float fwd[3]);
static int g_ladderMove;   // (Movement, below)

static void __cdecl hk_RenderScene(cod2_refdef_t *fd)
{
    // only the cgame's world view; other scenes keep the game's camera and go where the 2D goes
    if (g_mode == MODE_OFF || (DWORD)_ReturnAddress() != ADDR_CG_RenderScene_ret || !g_eyeSurf[0] || g_worldThisFrame) {
        orig_RenderScene(fd);
        return;
    }
    g_worldThisFrame = 1;
    SetCrouchCorrection(fd->vieworg);   // the game's own eye height: from its view origin as it is
    {
        // the camera shake taken out (ShakeFilter), for the eyes and everything placed from the view this frame
        float raw[3];
        int i;
        memcpy(raw, fd->vieworg, sizeof(raw));
        if (g_mode == MODE_VR) {
            ShakeFilter(raw);
            for (i = 0; i < 3; i++) fd->vieworg[i] += g_shakeOfs[i];
        }
        memcpy(g_lastViewOrg, fd->vieworg, sizeof(g_lastViewOrg));
        memcpy(g_lastViewFwd, fd->viewaxis[0], sizeof(g_lastViewFwd));
        if (g_mode == MODE_VR && !g_pitchSampled) { SamplePitchOffset(fd); g_pitchSampled = 1; }
        UpdateViewLock(fd->viewaxis[0]);
        UpdateRoomScale(raw, fd->viewaxis[0]);
        RenderStereo(fd);
        memcpy(fd->vieworg, raw, sizeof(raw));
    }
}

// R_SetupProjection(fov_x, fov_y, zNear) builds an infinite left-handed D3D projection into ESI
// (view space x right, y up, z forward). The eyes' frustums are asymmetric: m[0], m[5], m[8], m[9].
static void __cdecl FixProjection(float *m)
{
    const vr_pose_t *e;
    if (g_projEye < 0) return;
    e = &g_eyes[g_projEye];
    m[0] = 2.0f / (e->tanRight - e->tanLeft);
    m[8] = -(e->tanRight + e->tanLeft) / (e->tanRight - e->tanLeft);
    m[5] = 2.0f / (e->tanUp - e->tanDown);
    m[9] = -(e->tanUp + e->tanDown) / (e->tanUp - e->tanDown);
}

static __declspec(naked) void hk_SetupProjection(void)
{
    __asm {
        mov eax, [esp + 12]
        push eax
        mov eax, [esp + 12]
        push eax
        mov eax, [esp + 12]
        push eax
        call dword ptr [p_SetupProjection]
        add esp, 12
        push esi
        call FixProjection
        add esp, 4
        ret
    }
}

static int EntityCenter(int ent, float c[3]);
static void WorldToTracking(const float w[3], float body, float out[3]);

// The centre of the entity the free hand uses, in tracking space (0 = it is gone or no longer usable).
static int UseTargetAt(int ent, float out[3])
{
    float c[3];
    __try {
        if (!EntityCenter(ent, c)) return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    WorldToTracking(c, BodyYaw(g_lastViewFwd), out);
    return 1;
}

// The aim -> cl.viewangles. With a tracked gun hand: along its barrel, seen from the soldier's eye
// (where the game fires from) so that the shots cross the barrel's line ConvergeDistance ahead;
// otherwise the head. Yaw as a delta (the stick and the mouse keep turning the body), pitch
// absolute, corrected by the measured offset between cl.viewangles and the view (delta_angles).
static void UpdateViewAngles(void)
{
    float *va = (float *)ADDR_cl_viewangles;
    float dir[3], len, yaw, pitch;
    vr_pose_t gun;
    int i, hand = GunPose(&gun), use = Controls_UseTarget();
    float throwDir[3], useAt[3];
    if (g_ladderMove) {
        // on a ladder: where the head looks, 50 degrees up (forward climbs, back goes down)
        float hy = RAD(YawOf(g_head.axis[0]));
        dir[0] = cosf(hy) * 0.6428f; dir[1] = sinf(hy) * 0.6428f; dir[2] = 0.766f;
        hand = g_aimHand;
    } else if (use && UseTargetAt(use, useAt)) {
        // the free hand's trigger is using something: the aim goes to its centre from the soldier's eye, so the
        // game uses it
        TrackingRel(useAt, dir);
        hand = g_aimHand;   // not a change of the aim's source
    } else if (Controls_ThrowDir(throwDir)) {
        // a grenade on its way out of the off hand: the game throws it along the view, so the view
        // follows the throwing hand's motion until it has left
        for (i = 0; i < 3; i++) dir[i] = throwDir[i];
        hand = g_aimHand;   // not a change of the aim's source
    } else if (hand) {
        float p[3];
        for (i = 0; i < 3; i++) p[i] = gun.pos[i] + gun.axis[0][i] * cfg.converge;
        TrackingRel(p, dir);
    } else {
        for (i = 0; i < 3; i++) dir[i] = g_head.axis[0][i];
    }
    len = sqrtf(Dot(dir, dir));
    if (len < 1e-4f) return;
    for (i = 0; i < 3; i++) dir[i] /= len;
    yaw = YawOf(dir);
    pitch = PitchOf(dir);
    g_headYawNow = YawOf(g_head.axis[0]);
    // first frame, or the aim changed hands (controller lost / found): start the delta from here
    if (!g_aimYawInit || hand != g_aimHand) {
        Log("aim: %s", hand ? "the gun hand" : "the head");
        // the game's crosshair is in the middle of the HUD screen, which follows the head: with the
        // gun aiming it would lie (you aim with the gun's own sights, or [Controllers] Crosshair)
        Controls_Command(hand ? "cg_drawCrosshair 0\n" : "cg_drawCrosshair 1\n");
        g_aimYawPrev = yaw;
        g_aimYawInit = 1;
        g_aimHand = hand;
    }
    if (!cfg.headAim) return;
    va[YAW] += AngleNormalize180(yaw - g_aimYawPrev);
    g_aimYawPrev = yaw;
    if (g_pitchOffsetInit) va[PITCH] = pitch - g_pitchOffset;
}

// CL_FinishMove: the usercmd of the frame about to be drawn is made from cl.viewangles as they
// are now. The view the cgame draws this frame comes from it, so what the aim put into it (yaw)
// and the pitch sent are taken here, not when they were written.
void VR_OnCreateCmd(void)
{
    g_aimYawCmd = g_aimYawPrev;
    g_moveYaw = AngleNormalize180(g_headYawNow - g_aimYawPrev);
    g_clPitchCmd = ((float *)ADDR_cl_viewangles)[PITCH];
}

float VR_MoveYawOffset(void) { return g_mode == MODE_VR ? g_moveYaw : 0.0f; }

static void FakeEyes(void)
{
    int k;
    memset(g_eyes, 0, sizeof(g_eyes));
    memset(&g_head, 0, sizeof(g_head));
    g_head.axis[0][0] = g_head.axis[1][1] = g_head.axis[2][2] = 1;
    for (k = 0; k < 2; k++) {
        vr_pose_t *e = &g_eyes[k];
        e->axis[0][0] = e->axis[1][1] = e->axis[2][2] = 1;
        e->pos[1] = k ? -0.032f : 0.032f;            // y points left
        e->tanLeft = k ? -0.90f : -1.10f;            // the outer side of each eye sees further
        e->tanRight = k ? 1.10f : 0.90f;
        e->tanUp = 0.88f;
        e->tanDown = -1.20f;
    }
}

// re.DrawStretchPic(x, y, w, h, s0, t0, s1, t1, color, material): command 15. A picture covering
// the whole screen during play - the black screen a mission starts with, the red flash of a hit,
// the blood at the edges at low health, fades - goes to a texture of its own, shown stuck to the
// head and larger than the view (XR_EndFrame), so it covers the whole view as it covers the
// monitor, instead of being a rectangle on the HUD screen. (Drawn straight into the eyes it covered
// only their top-left 1280 x 720: the backend maps the 2D's pixels by the size of the target bound,
// and the eyes are bigger than the window.) The HUD's text stays on the HUD screen.
typedef void (__cdecl *PFN_StretchPic)(float x, float y, float w, float h, float s0, float t0, float s1, float t1,
                                       const float *color, void *material);
static PFN_StretchPic orig_StretchPic;

static const char *MaterialName(void *m)
{
    const char *n = NULL;
    __try {
        n = m ? MEM(const char *, m) : NULL;
        if (n && !n[0]) n = NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        n = NULL;
    }
    return n;
}

static int InLevel(void)
{
    DWORD cv = MEM(DWORD, ADDR_cvar_sv_running);
    return cv && MEM(BYTE, cv + 8);
}

// Which full-screen pictures cover the whole view (the 2D of a frame is queued before that frame's
// world view, so "playing" is the last frame having the world: g_lastWorld):
//  * playing (no menu): all of them - hits, fades, the blood at low health;
//  * otherwise only an opaque black one: the black screen a mission starts with (no menu) and the
//    death screen with its quote (a menu): black all around, their text on the screen in front.
// A menu's other pictures (the soldier of the game's menu, its translucent veil) and the loading
// picture ($levelbriefing) stay on the menu's screen with its options.
// The death screen (the quote over the game's menu picture): open from the server's "opendeadscreen"
// until its "closedeadscreen" (cg.deadScreenTime). The player's health is no sign of it (it did not
// reach 0 at two deaths of the maintainer's), and its picture is the pause menu's too.
static int DeadScreen(void)
{
    return InLevel() && MEM(int, ADDR_cg_deadScreenTime) != 0;
}

static int OpaqueBlack(const char *name, const float *color)
{
    float a = color ? color[3] : 1.0f;
    if (a < 0.98f) return 0;
    if (name && !_stricmp(name, "black")) return 1;
    return color && color[0] < 0.05f && color[1] < 0.05f && color[2] < 0.05f;
}

static int FullViewPicture(int ui, const char *name, const float *color)
{
    if (!ui && g_lastWorld) return 1;
    return OpaqueBlack(name, color);
}

static void __cdecl hk_StretchPic(float x, float y, float w, float h, float s0, float t0, float s1, float t1,
                                  const float *color, void *material)
{
    int ui = (MEM(int, ADDR_cls_keyCatchers) & KEYCATCH_UI) != 0;
    if (g_deathFrame) { orig_StretchPic(x, y, w, h, s0, t0, s1, t1, color, material); return; }   // to the HUD, not shown
    if (g_mode == MODE_VR && g_fullSurf && x <= 1 && y <= 1 &&
        ((x + w >= g_winW - 1 && y + h >= g_winH - 1) || (x + w >= 639 && y + h >= 479 && x + w <= 641))) {
        const char *name = MaterialName(material);
        int full = FullViewPicture(ui, name, color);
        static char seen[48][64];
        static int nseen;
        char key[64];
        int k;
        // each material once per kind of screen and decision, taken or not, so a screen that goes wrong
        // shows up in the first log
        _snprintf(key, sizeof(key), "%.48s %d %d %d", name ? name : "?", ui, g_lastWorld, full);
        key[sizeof(key) - 1] = 0;
        for (k = 0; k < nseen && strcmp(seen[k], key); k++) ;
        if (k == nseen && nseen < 48) {
            strcpy(seen[nseen], key);
            nseen++;
            Log("full-screen picture %s: %s, %.0f x %.0f, colour %.2f %.2f %.2f alpha %.2f, playing %d, menu %d",
                full ? "to the full-view layer" : "left on its screen", name ? name : "?", w, h,
                color ? color[0] : 1.0f, color ? color[1] : 1.0f, color ? color[2] : 1.0f, color ? color[3] : 1.0f, g_lastWorld, ui);
        }
        if (!full) { orig_StretchPic(x, y, w, h, s0, t0, s1, t1, color, material); return; }
        Marker(TGT_FULLK);
        orig_StretchPic(x, y, w, h, s0, t0, s1, t1, color, material);
        Marker(TGT_HUDK);
        g_fullPainted = 1;
        return;
    }
    orig_StretchPic(x, y, w, h, s0, t0, s1, t1, color, material);
}

static void __cdecl hk_BeginFrame(void)
{
    orig_BeginFrame();
    g_fullPainted = 0;
    g_beFullFilled = 0;   // the backend of this frame runs in re.EndFrame, after this
    g_worldThisFrame = 0;
    g_pitchSampled = 0;
    g_frameNo++;
    if (!cfg.enabled) { g_mode = MODE_OFF; return; }
    if (g_targetsVR == 2) {
        FakeEyes();
        g_mode = MODE_VR;
        if (MEM(int, ADDR_cls_keyCatchers) & KEYCATCH_UI) Controls_TestCursor(cfg.fakeCursor[0], cfg.fakeCursor[1]);
        if (g_testFire) {
            static DWORD next;
            static int down;
            if (GetTickCount() >= next) {
                down = !down;
                Controls_Command(down ? "+attack\n" : "-attack\n");
                next = GetTickCount() + (down ? 150 : 3000);
            }
        }
    }
    else if ((g_vrFrame || g_xrIdle) && g_targetsVR) g_mode = MODE_VR;
    else if (cfg.sbs && g_eyeSurf[0] && !g_targetsVR) g_mode = MODE_SBS;
    else g_mode = MODE_OFF;
    // Everything 2D goes to the HUD. The death screen (its quote over the game's menu picture) is not
    // shown at all - the maintainer's choice: on a screen it was a rectangle, over the whole view its
    // quote was stretched and blurred - so the world stays in view until the game goes back to the
    // last checkpoint.
    if (g_mode == MODE_VR) {
        g_deathFrame = DeadScreen();
        {
            static int was;
            if (g_deathFrame != was) { was = g_deathFrame; Log("death screen: %s", g_deathFrame ? "on, not shown" : "off"); }
        }
        Marker(TGT_HUD);
    } else g_deathFrame = 0;
}

static void __cdecl hk_EndFrame(void)
{
    static int traced;
    if (g_mode != MODE_OFF) Marker(TGT_WINDOW);
    if (cfg.traceCmds && g_worldThisFrame && !g_tracing && traced < cfg.traceCmds) { g_tracing = 1; traced++; Log("---- commands of world frame %d", traced); }
    orig_EndFrame();
    g_tracing = 0;
}

// ---------------------------------------------------------------------------
// Present: submit, mirror, start the next headset frame
// ---------------------------------------------------------------------------
// Threads made in the game's process with the default stack size get the size in CoD2SP_s.exe's header: 64 KB. The
// SteamVR runtime makes its own threads that way and overflowed them as soon as the VR session started (exception
// c00000fd; Virtual Desktop's runtime fits). Every thread of this process made after VR starts gets at least 1 MB of
// stack: kernelbase's CreateRemoteThreadEx (which CreateThread, _beginthreadex and std::thread all end in) goes
// through hk_CreateRemoteThreadEx. Its first instruction (push imm32, 5 bytes) is moved to a trampoline.
typedef HANDLE (WINAPI *PFN_CreateRemoteThreadEx)(HANDLE, LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD,
                                                 LPPROC_THREAD_ATTRIBUTE_LIST, LPDWORD);
static PFN_CreateRemoteThreadEx o_CreateRemoteThreadEx;
#define MIN_THREAD_STACK (1 << 20)

static HANDLE WINAPI hk_CreateRemoteThreadEx(HANDLE proc, LPSECURITY_ATTRIBUTES sa, SIZE_T size, LPTHREAD_START_ROUTINE start, LPVOID arg,
                                             DWORD flags, LPPROC_THREAD_ATTRIBUTE_LIST attrs, LPDWORD id)
{
    if ((proc == GetCurrentProcess() || GetProcessId(proc) == GetCurrentProcessId()) && size < MIN_THREAD_STACK) {
        // 0, or a small commit or reserve: a 1 MB reservation (the commit stays the default, it grows as needed)
        size = MIN_THREAD_STACK;
        flags |= STACK_SIZE_PARAM_IS_A_RESERVATION;
    }
    return o_CreateRemoteThreadEx(proc, sa, size, start, arg, flags, attrs, id);
}

static void InstallThreadStackHook(void)
{
    static int done;
    HMODULE kb = GetModuleHandleA("kernelbase.dll");
    BYTE *p, *tramp;
    DWORD old;
    if (done) return;
    done = 1;
    p = kb ? (BYTE *)GetProcAddress(kb, "CreateRemoteThreadEx") : NULL;
    if (!p || p[0] != 0x68) { Log("threads: CreateRemoteThreadEx not as expected, thread stacks left as they are"); return; }
    tramp = (BYTE *)VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return;
    memcpy(tramp, p, 5);                                   // push imm32
    tramp[5] = 0xE9;                                       // jmp back after it
    MEM(DWORD, tramp + 6) = (DWORD)(p + 5) - ((DWORD)tramp + 10);
    o_CreateRemoteThreadEx = (PFN_CreateRemoteThreadEx)tramp;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    MEM(DWORD, p + 1) = (DWORD)hk_CreateRemoteThreadEx - ((DWORD)p + 5);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    Log("threads: new threads get at least 1 MB of stack (the game's default is 64 KB)");
}

static void StartVR(void)
{
    LUID luid = { 0 };
    if (cfg.fakeHmd) { if (g_targetsVR != 2) CreateTargets(2); return; }
    if (!cfg.vr || XR_Ready() || !g_d3d || (int)(GetTickCount() - g_nextXrTry) < 0) return;
    g_nextXrTry = GetTickCount() + 2000;
    InstallThreadStackHook();   // before the runtime is loaded: its threads need more than the game's 64 KB
    IDirect3D9Ex_GetAdapterLUID(g_d3d, g_adapter, &luid);
    if (!XR_Init(luid, g_winW, g_winH)) return;
    if (!CreateTargets(1)) { Log("VR: could not open the headset textures, side by side instead"); XR_Shutdown(); cfg.vr = 0; CreateTargets(0); return; }
    if (g_userIni[0]) {
        // for the launcher: the eye size of each resolution choice
        char dir[MAX_PATH], v[16], *slash;
        int w, h, mw, mh;
        XR_RecommendedSize(&w, &h, &mw, &mh);
        strcpy(dir, g_userIni);
        if ((slash = strrchr(dir, '\\')) != NULL) { *slash = 0; CreateDirectoryA(dir, NULL); }
        sprintf(v, "%d", w);  WritePrivateProfileStringA("Headset", "RecommendedWidth", v, g_userIni);
        sprintf(v, "%d", h);  WritePrivateProfileStringA("Headset", "RecommendedHeight", v, g_userIni);
        sprintf(v, "%d", mw); WritePrivateProfileStringA("Headset", "MaxWidth", v, g_userIni);
        sprintf(v, "%d", mh); WritePrivateProfileStringA("Headset", "MaxHeight", v, g_userIni);
    }
}

// The menu screen: placed level in front of the head when a menu opens, and left there (you can
// look around it and point at it). During play the HUD follows the head.
static vr_pose_t g_menuPose;
static vr_hud_t  g_lastHud;      // what the last frame showed (the backend shows it again while the game loads)
static int       g_lastHudValid;
static int       g_menuPlaced, g_menuShown;
static float     g_menuRefZ;     // the head's height when the menu screen was placed
static LARGE_INTEGER g_lastTick;

static int MenuActive(void)
{
    return !g_worldThisFrame || (MEM(int, ADDR_cls_keyCatchers) & KEYCATCH_UI) != 0;
}

static void PlaceMenu(void)
{
    float f[3] = { g_head.axis[0][0], g_head.axis[0][1], 0 }, len = sqrtf(f[0] * f[0] + f[1] * f[1]);
    int i;
    if (len < 0.01f) { f[0] = 1; f[1] = 0; len = 1; }
    f[0] /= len; f[1] /= len;
    memset(&g_menuPose, 0, sizeof(g_menuPose));
    for (i = 0; i < 3; i++) g_menuPose.pos[i] = g_head.pos[i] + f[i] * cfg.hud.distance;
    g_menuPose.pos[2] += cfg.hud.offsetY;
    g_menuPose.axis[0][0] = f[0]; g_menuPose.axis[0][1] = f[1];
    g_menuPose.axis[1][0] = -f[1]; g_menuPose.axis[1][1] = f[0];   // left = up x forward
    g_menuPose.axis[2][2] = 1;
    g_menuPlaced = 1;
    g_menuRefZ = g_head.pos[2];
    Log("menu screen placed in front of the head (head at %.2f %.2f %.2f)", g_head.pos[0], g_head.pos[1], g_head.pos[2]);
}

// The HUD screen as it is shown now (head-locked during play): tracking space, Quake axes.
static void CurrentScreen(vr_pose_t *p, float *w, float *h)
{
    int i;
    *w = MenuActive() ? cfg.hud.menuWidth : cfg.hud.width;
    *h = *w * (g_hudH > 0 ? (float)g_hudH / g_hudW : 0.5625f);
    if (MenuActive() && !cfg.menuFollowsHead) { *p = g_menuPose; return; }
    *p = g_head;
    for (i = 0; i < 3; i++) p->pos[i] = g_head.pos[i] + g_head.axis[0][i] * cfg.hud.distance + g_head.axis[2][i] * cfg.hud.offsetY;
}

// Stand up straight and press the recenter button: the head is now the soldier's eye (its height
// is the standing height for the physical crouch, and the camera is back in his head); a menu on
// screen comes back in front of you.
static void Recenter(void)
{
    int i;
    for (i = 0; i < 3; i++) g_ref[i] = g_head.pos[i];
    g_menuPlaced = 0;
    Log("recentered: head at %.2f %.2f, height %.2f", g_ref[0], g_ref[1], g_ref[2]);
    XR_Haptic(0, 0.5f, 60);
    XR_Haptic(1, 0.5f, 60);
}

// ---------------------------------------------------------------------------
// Comfort vignette: seeing motion the body does not feel (walking with the stick, turning
// smoothly) is what makes VR sickness; narrowing the view while it happens hides the motion at the
// edges, and it opens again when it stops. Walking for real and snap turns do not close it. The
// speed is the game's own (pushing against a wall moves nothing).
// ---------------------------------------------------------------------------
static float g_vigAmount;   // 0 = open .. 1 = as closed as VignetteStrength allows

static void UpdateVignette(float dt)
{
    static float prev[2], speedF;
    static int havePrev;
    float speed = 0, target = 0, tau, stick = Controls_MoveAmount(), turnRate = Controls_TurnRate();
    if (!g_lastWorld) havePrev = 0;   // menus and loading screens: no motion
    if (havePrev && dt > 0) {
        float dx = g_lastViewOrg[0] - prev[0], dy = g_lastViewOrg[1] - prev[1];
        speed = sqrtf(dx * dx + dy * dy) / dt;
        if (speed > 2000.0f) speed = 0;   // a jump in place: level start, load
    }
    prev[0] = g_lastViewOrg[0]; prev[1] = g_lastViewOrg[1];
    havePrev = g_lastWorld;
    if (stick < 0.15f) speed = 0;   // carried by something else, or walking for real
    // the game moves the player in steps, so the speed seen frame by frame jitters: smoothed (the
    // stick, which is steady, says how much it closes) or the edge would pulse
    if (dt > 0) speedF += (speed - speedF) * (1.0f - expf(-dt / 0.3f));
    if (cfg.vignette && g_lastWorld) {
        float moving = (speedF - cfg.vigSpeed) / cfg.vigSpeed;
        float push = (stick - 0.15f) / 0.6f;
        float byTurn = (turnRate - cfg.vigTurn) / (cfg.vigTurn * 3.0f), bySpeed;
        moving = moving < 0 ? 0 : moving > 1 ? 1 : moving;
        push = push < 0 ? 0 : push > 1 ? 1 : push;
        bySpeed = moving * push;
        target = bySpeed > byTurn ? bySpeed : byTurn;
        target = target < 0 ? 0 : target > 1 ? 1 : target;
    }
    // closes quickly (protection), opens slowly (no jarring release)
    tau = target > g_vigAmount ? 0.06f : 0.22f;
    if (dt > 0) g_vigAmount += (target - g_vigAmount) * (1.0f - expf(-dt / tau));
    if (target == 0 && g_vigAmount < 0.002f) g_vigAmount = 0;
    {
        static int logged;
        if (!logged && g_vigAmount > 0.5f) { logged = 1; Log("comfort vignette active (speed %.0f, turn %.0f)", speedF, turnRate); }
    }
}

// vr:vignette: Off, Low, Medium, High in turn, kept in settings.ini (the launcher's choices).
static void CycleVignette(void)
{
    static const float levels[4] = { 0.0f, 0.35f, 0.6f, 0.9f };
    static const char *const names[4] = { "Off", "Low", "Medium", "High" };
    char v[16];
    int i, cur = 0;
    if (cfg.vignette)
        for (i = 1; i < 4; i++)
            if (cur == 0 || fabsf(cfg.vigStrength - levels[i]) < fabsf(cfg.vigStrength - levels[cur])) cur = i;
    cur = (cur + 1) % 4;
    cfg.vignette = cur > 0;
    if (cur > 0) cfg.vigStrength = levels[cur];
    if (g_userIni[0]) {
        WritePrivateProfileStringA("Comfort", "Vignette", cur > 0 ? "1" : "0", g_userIni);
        _snprintf(v, sizeof(v), "%.2f", levels[cur]);
        if (cur > 0) WritePrivateProfileStringA("Comfort", "VignetteStrength", v, g_userIni);
    }
    // no text on screen yet: one buzz per step (Off = none)
    if (cur > 0) XR_Haptic(Controls_DominantHand(), 0.15f + 0.25f * cur, 40 + 40 * cur);
    Log("comfort vignette %s", names[cur]);
}

// ---------------------------------------------------------------------------
// Physical crouch: the head going CrouchDepth below its standing height (the recentered one)
// crouches the soldier, standing up stands him up again. Only stances the mod made are undone:
// a crouch or prone chosen with the stick stays.
// ---------------------------------------------------------------------------
static void UpdatePhysicalCrouch(void)
{
    static int crouched, owned;
    float drop = g_ref[2] - g_head.pos[2];
    if (!cfg.physCrouch || !cfg.positional || !g_lastWorld || MenuActive()) {
        crouched = 0;
        return;
    }
    if (!crouched && drop > cfg.crouchDepth) {
        crouched = 1;
        if (!owned) { Controls_Command("lowerstance\n"); owned = 1; Log("physical crouch: down (%.2f m)", drop); }
    } else if (crouched && drop < cfg.crouchDepth - 0.08f) {
        crouched = 0;
        if (owned) { Controls_Command("raisestance\n"); Log("physical crouch: up"); }
        owned = 0;
    }
    if (Controls_StanceChanged()) owned = 0;   // the stick changed the stance: it is the player's now
    g_crouchOwned = owned;
}

// While the game crouches the soldier because the player crouched, the camera keeps the real
// head's height: the game's own drop of the eye (60 units standing, 40 crouched) is given back.
// Its eye height is the view origin minus the playerState's origin (ps + 0x14, the Quake 3
// layout: 60.0 standing on demolition). Taken from the same frame's view.
static void SetCrouchCorrection(const float *vieworg)
{
    float c = 0;
    if (g_crouchOwned) {
        float vh = vieworg[2] - MEM(float, ADDR_cg_ps + PS_OFS_origin + 8);
        if (vh > 5.0f && vh < 75.0f) c = (60.0f - vh) / cfg.worldScale;
        if (c < 0) c = 0;
        if (c > 0.8f) c = 0.8f;
    }
    g_crouchCorr = c;
}

// The crosshair. With the shots leaving the muzzle along the barrel (FireFromGun) it sits where that line meets the first
// thing it would hit (traced as the shot is, MASK_SHOT, from the muzzle): a mark at a fixed distance on the barrel's line
// is seen from the eye, which is above that line, higher than a hit farther away (the shots looked low) and lower than a
// nearer one. Without FireFromGun: ConvergeDistance ahead, where the flat game's shots from the eye cross the barrel.
// Its size is kept as an angle (CrosshairSize degrees) whatever the distance.
static void UpdateCrosshair(void)
{
    float col[4];   // premultiplied: [Controllers] CrosshairColor, CrosshairOpacity
    vr_pose_t gun;
    float c[3], right[3], o[3], dist = cfg.converge, seen[3];
    int i;
    if (!cfg.crosshair || !g_lastWorld || MenuActive() || !GunPose(&gun)) return;
    for (i = 0; i < 3; i++) col[i] = cfg.crossColor[i] * cfg.crossAlpha;
    col[3] = cfg.crossAlpha;
    memcpy(o, gun.pos, 12);
    if (g_mode == MODE_VR && cfg.fireFromGun && InLevel()) {
        float body = BodyYaw(g_lastViewFwd), m[3], fwd[3], end[3], f;
        const float range = 150.0f;   // metres
        ShotOrigin(&gun, o);          // the shots' line (the sight line or the bore)
        TrackingToWorld(o, g_lastViewOrg, body, m);
        RotateZ(gun.axis[0], body, fwd);
        for (i = 0; i < 3; i++) end[i] = m[i] + fwd[i] * range * cfg.worldScale;
        f = WorldTrace(m, end, 0, MASK_SHOT, NULL, NULL);
        dist = (f > 0 && f <= 1 ? f : 1.0f) * range;
        for (i = 0; i < 3; i++) g_crossHitW[i] = m[i] + (end[i] - m[i]) * (f > 0 && f <= 1 ? f : 1.0f);
        g_crossHitOk = 1;
    }
    for (i = 0; i < 3; i++) {
        c[i] = o[i] + gun.axis[0][i] * dist;
        right[i] = -gun.axis[1][i];
        seen[i] = c[i] - g_head.pos[i];
    }
    // its size an angle as seen from the head
    XR_SetReticle(cfg.crosshair, c, right, gun.axis[2], sqrtf(Dot(seen, seen)) * tanf(RAD(cfg.crossSize * 0.5f)), col);
}

// ---------------------------------------------------------------------------
// Swing melee (as the CoD 1 mod): any part of the gun, stock to muzzle, moving fast into someone
// hits him. The damage is the game's own (G_Damage with MOD_MELEE, the player as the attacker: pain,
// death, blood), dealt at once where the gun hit - no melee animation of the flat game. Only real
// hand motion counts (tracking space: walking does not), and only the part of it going into the
// body, so resting the gun on someone does nothing.
// ---------------------------------------------------------------------------
typedef void (__cdecl *PFN_G_Damage)(BYTE *targ, BYTE *inflictor, BYTE *attacker, float *dir, float *point,
                                     int damage, int dflags, int mod, int hitLoc);
#define SWING_POINTS 6

// A world point in tracking space (the inverse of TrackingToWorld, with the last world frame's view).
static void WorldToTracking(const float w[3], float body, float out[3])
{
    float d[3], r[3];
    int i;
    for (i = 0; i < 3; i++) d[i] = (w[i] - g_lastViewOrg[i]) / cfg.worldScale;
    RotateZ(d, -body, r);
    if (cfg.positional) {
        for (i = 0; i < 3; i++) out[i] = r[i] + g_ref[i];
        out[2] -= cfg.heightOffset + g_crouchCorr;
    } else {
        for (i = 0; i < 3; i++) out[i] = r[i] + g_head.pos[i];
    }
}

// ---------------------------------------------------------------------------
// World trace (as the CoD 1 mod's WorldTrace, which used CM_BoxTrace): the game's own SV_Trace, a point from
// start to end (world units) against the map and the entities, passEnt left out (0 = the player, ENTITYNUM_NONE
// = none). Returns the fraction of the way that is clear (1 = nothing hit, or no level / a fault); normal and
// hit (the entity, ENTITYNUM_WORLD for the map) when asked for. For the grenade line, head collision, teleport
// and ladders.
// ---------------------------------------------------------------------------
// A box (mins / maxs around the point, NULL = a point); surf = the surface flags of what it hit.
static float TraceBox(const float start[3], const float end[3], const float mins[3], const float maxs[3], int passEnt, int mask,
                      float normal[3], int *hit, int *surf)
{
    static const float zero[3] = { 0, 0, 0 };
    BYTE tr[64];
    const float *mn = mins ? mins : zero, *mx = maxs ? maxs : zero;
    DWORD fn = ADDR_SV_Trace;
    if (!InLevel()) return 1.0f;
    memset(tr, 0, sizeof(tr));
    __try {
        __asm {
            push 0
            push 0
            push 0
            push mask
            push passEnt
            push mx             // maxs
            push mn             // mins
            lea esi, tr
            mov ebx, start
            mov edi, end
            call fn
            add esp, 28
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged;
        if (!logged) { logged = 1; Log("trace: fault in SV_Trace"); }
        return 1.0f;
    }
    if (normal) memcpy(normal, tr + 4, 12);
    if (hit) *hit = MEM(WORD, tr + 0x1c);
    if (surf) *surf = MEM(int, tr + 0x10);
    return MEM(float, tr);
}

static float WorldTrace(const float start[3], const float end[3], int passEnt, int mask, float normal[3], int *hit)
{
    return TraceBox(start, end, NULL, NULL, passEnt, mask, normal, hit, NULL);
}

// Diagnostics for the trace: a few times per level, how far the view's line is clear (look at a wall a known
// distance away and compare). Nothing in the game depends on it yet.
static void TraceDiag(void)
{
    static DWORD next;
    static int lines;
    float end[3], n[3], f;
    int i, hit = 0;
    if (lines >= 6 || !InLevel() || !g_lastWorld || MenuActive() || GetTickCount() < next) return;
    next = GetTickCount() + 5000;
    for (i = 0; i < 3; i++) end[i] = g_lastViewOrg[i] + g_lastViewFwd[i] * 4000.0f;
    f = WorldTrace(g_lastViewOrg, end, 0, 1, n, &hit);
    lines++;
    Log("trace: the view is clear for %.2f m (fraction %.3f), hit %s %d, normal %.2f %.2f %.2f", f * 4000.0f / cfg.worldScale, f,
        hit == ENTITYNUM_WORLD ? "the world" : hit == ENTITYNUM_NONE ? "nothing" : "entity", hit, n[0], n[1], n[2]);
}

// ---------------------------------------------------------------------------
// The free hand uses things (as the CoD 1 mod's UsableNearEx): the trigger_use whose box, grown by USE_REACH,
// holds the hand, the nearest by its centre. While its trigger is held the aim goes to that centre
// (UpdateViewAngles), so the game uses it whatever the gun points at.
// ---------------------------------------------------------------------------
#define USE_REACH 24.0f   // game units around the box

static int EntityCenter(int ent, float c[3])
{
    const BYTE *e = (const BYTE *)ADDR_g_entities + ent * GENTITY_SIZE;
    const float *o = (const float *)(e + GENT_OFS_currentOrigin), *mn = (const float *)(e + GENT_OFS_mins), *mx = (const float *)(e + GENT_OFS_maxs);
    int k;
    if (!(MEM(int, e + GENT_OFS_contents) & CONTENTS_USABLE)) return 0;   // used, or gone
    for (k = 0; k < 3; k++) c[k] = o[k] + (mn[k] + mx[k]) * 0.5f;
    return 1;
}

int VR_UsableNear(const float at[3])
{
    float hand[3], best = 1e9f;
    int i, found = 0;
    if (!InLevel() || !g_lastWorld) return 0;
    TrackingToWorld(at, g_lastViewOrg, BodyYaw(g_lastViewFwd), hand);
    __try {
        for (i = 1; i < 1022; i++) {
            const BYTE *e = (const BYTE *)ADDR_g_entities + i * GENTITY_SIZE;
            const float *o = (const float *)(e + GENT_OFS_currentOrigin), *mn = (const float *)(e + GENT_OFS_mins), *mx = (const float *)(e + GENT_OFS_maxs);
            float size = 0, d = 0;
            int k;
            if (!(MEM(int, e + GENT_OFS_contents) & CONTENTS_USABLE)) continue;
            for (k = 0; k < 3; k++) if (mx[k] - mn[k] > size) size = mx[k] - mn[k];
            if (size <= 0 || size > 5000.0f) continue;
            for (k = 0; k < 3; k++) {
                float lo = o[k] + mn[k] - USE_REACH, hi = o[k] + mx[k] + USE_REACH, c = o[k] + (mn[k] + mx[k]) * 0.5f;
                if (hand[k] < lo || hand[k] > hi) break;
                d += (hand[k] - c) * (hand[k] - c);
            }
            if (k == 3 && d < best) { best = d; found = i; }
        }
        if (found) {
            const BYTE *e = (const BYTE *)ADDR_g_entities + found * GENTITY_SIZE;
            const float *mn = (const float *)(e + GENT_OFS_mins), *mx = (const float *)(e + GENT_OFS_maxs);
            Log("use: entity %d within reach (contents %08x, size %.0f x %.0f x %.0f)", found, MEM(int, e + GENT_OFS_contents),
                mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged;
        if (!logged) { logged = 1; Log("use: fault reading the game's entities"); }
        return 0;
    }
    return found;
}

// Swing melee: the blow is the gun itself, any part of it (stock to muzzle) going into someone or something fast enough;
// not the flat game's melee key (+melee, its animation). The damage is dealt where the gun hit, by the game's G_Damage
// with MOD_MELEE and the player as the attacker (pain, death, blood; the training's mannequin waits for exactly that).
// Targets: an enemy or neutral AI (never one of the player's side: friendly fire ends the mission), or a scripted thing
// that takes damage, man-sized at most (the mannequin, a target); a model without a box (the mannequin) counts as an
// upright body from its origin. Only real hand motion counts (tracking space: walking does not), and only the part of it
// going into the body, so resting the gun on someone does nothing.
#define TEAM_ALLIES 2

static void SwingMelee(float dt)
{
    static float prev[SWING_POINTS][3];
    static int prevValid;
    static DWORD next, nextDiag;
    float pts[SWING_POINTS][3], body, fastest = 0, nearMiss = 1e9f;
    vr_pose_t gun;
    DWORD now = GetTickCount();
    int i, j, k, nearEnt = 0;
    if (cfg.meleeSpeed <= 0 || !g_lastWorld || MenuActive() || !InLevel() || dt <= 0 || !GunPose(&gun)) { prevValid = 0; return; }
    body = BodyYaw(g_lastViewFwd);
    for (k = 0; k < SWING_POINTS; k++) {
        float s = -0.3f + 0.8f * k / (SWING_POINTS - 1);   // metres along the gun from the grip
        for (i = 0; i < 3; i++) pts[k][i] = gun.pos[i] + gun.axis[0][i] * s;
    }
    if (!prevValid || now < next) { memcpy(prev, pts, sizeof(pts)); prevValid = 1; return; }
    for (k = 0; k < SWING_POINTS; k++) {
        float d[3], sp;
        for (i = 0; i < 3; i++) d[i] = (pts[k][i] - prev[k][i]) / dt;
        sp = sqrtf(Dot(d, d));
        if (sp > fastest) fastest = sp;
    }
    if (fastest < cfg.meleeSpeed) { memcpy(prev, pts, sizeof(pts)); return; }
    __try {
        for (i = 1; i < 1022; i++) {
            BYTE *e = (BYTE *)ADDR_g_entities + i * GENTITY_SIZE;
            const float *org = (const float *)(e + GENT_OFS_currentOrigin);
            const float *mn = (const float *)(e + GENT_OFS_mins), *mx = (const float *)(e + GENT_OFS_maxs);
            float feet[3], top[3], w[3], dx, dy, radius = 0.35f;
            int actor = MEM(DWORD, e + GENT_OFS_actor) != 0;
            if (!e[GENT_OFS_takedamage]) continue;
            if (actor) {
                BYTE *sent = (BYTE *)MEM(DWORD, e + GENT_OFS_sentient);
                if (MEM(int, e + GENT_OFS_health) <= 0) continue;
                if (sent && MEM(int, sent + SENTIENT_OFS_team) == TEAM_ALLIES) continue;   // a friend
                memcpy(w, org, 12);
                w[2] = org[2];
            } else {
                float half = (mx[0] - mn[0] > mx[1] - mn[1] ? mx[0] - mn[0] : mx[1] - mn[1]) * 0.5f;
                if (half > 48.0f || mx[2] - mn[2] > 128.0f) continue;   // not a building that takes damage
                if (half <= 0 || mx[2] - mn[2] <= 0) {
                    // a model without a box (the mannequin): an upright body from its origin
                    memcpy(w, org, 12);
                } else {
                    radius = half / cfg.worldScale + 0.1f;
                    w[0] = org[0] + (mn[0] + mx[0]) * 0.5f; w[1] = org[1] + (mn[1] + mx[1]) * 0.5f; w[2] = org[2] + mn[2];
                }
            }
            WorldToTracking(w, body, feet);
            dx = feet[0] - g_head.pos[0]; dy = feet[1] - g_head.pos[1];
            if (dx * dx + dy * dy > 3.0f * 3.0f) continue;
            w[2] = actor || mx[2] - mn[2] <= 0 ? w[2] + 70.0f : org[2] + mx[2];
            WorldToTracking(w, body, top);
            for (k = 0; k < SWING_POINTS; k++) {
                // inside the body (an upright capsule), moving into it fast enough
                float *p = pts[k], v[3], hz[2], d, into, speed;
                if (p[2] < feet[2] + 0.1f || p[2] > top[2] + 0.15f) continue;
                hz[0] = feet[0] + (top[0] - feet[0]) * 0.5f - p[0];
                hz[1] = feet[1] + (top[1] - feet[1]) * 0.5f - p[1];
                d = sqrtf(hz[0] * hz[0] + hz[1] * hz[1]);
                if (d - radius < nearMiss) { nearMiss = d - radius; nearEnt = i; }
                if (d > radius || d < 1e-3f) continue;
                for (j = 0; j < 3; j++) v[j] = (p[j] - prev[k][j]) / dt;
                into = (v[0] * hz[0] + v[1] * hz[1]) / d;
                speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                if (speed < cfg.meleeSpeed || into < cfg.meleeSpeed * 0.3f) continue;
                {
                    // where and which way it hit, in the world
                    float pw[3], vt[3], dir[3], len;
                    int head = actor && p[2] > feet[2] + (top[2] - feet[2]) * 0.82f;
                    TrackingToWorld(p, g_lastViewOrg, body, pw);
                    RotateZ(v, body, vt);
                    len = sqrtf(vt[0] * vt[0] + vt[1] * vt[1] + vt[2] * vt[2]);
                    for (j = 0; j < 3; j++) dir[j] = len > 0 ? vt[j] / len : 0;
                    Log("swing melee: %s %d hit (%s), %.1f m/s, %.1f into it, point %d, health %d", actor ? "AI" : "object", i,
                        head ? "head" : "body", speed, into, k, MEM(int, e + GENT_OFS_health));
                    ((PFN_G_Damage)ADDR_G_Damage)(e, (BYTE *)ADDR_g_entities, (BYTE *)ADDR_g_entities, dir, pw, cfg.meleeDamage,
                                                   0, MOD_MELEE, head ? HITLOC_HEAD : HITLOC_TORSO_UPPER);
                }
                XR_Haptic(Controls_DominantHand(), 1.0f, 70);
                next = now + 700;
                memcpy(prev, pts, sizeof(pts));
                return;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged;
        if (!logged) { logged = 1; Log("swing melee: fault reading the game's entities, off"); }
        cfg.meleeSpeed = 0;
    }
    if (now >= nextDiag) {
        nextDiag = now + 3000;
        if (nearEnt) Log("swing melee: a swing (%.1f m/s) missed entity %d by %.2f m", fastest, nearEnt, nearMiss);
    }
    memcpy(prev, pts, sizeof(pts));
}

// ---------------------------------------------------------------------------
// Grenade line (as the CoD 1 mod's UpdateGrenadeArc): while a grenade is in the hand (pin pulled), where it
// will fly when let go, computed as the game throws it: from the soldier's eye, at the grenade's
// projectileSpeed along the way the hand that holds it points (released with the hand still, the mod throws
// it there; a swing throws along the swing, which cannot be known before) - along the aim for a grenade from
// a button - plus projectileSpeedUp upwards, under gravity. Each 0.05 s step is traced against the map and
// what is solid (the physics mask, as the game's physicstrace); the line stops where it first hits and a ring
// lies on that spot. Bounces are not simulated. Trajectory 1 and 2 are the same here: CoD2 has a grenade in
// the hand only while it is held.
// ---------------------------------------------------------------------------
static void UpdateGrenadeArc(void)
{
    float pts[72][3], wv[3], p0[3], p1[3], n[3] = { 0, 0, 1 }, fuse, t, body, col[4], op;
    const BYTE *d = NULL;
    vr_pose_t gh;
    int k, np = 0, hit = 0, speed = 0, up = 0, fuseMs = 0, w;
    if (!cfg.grenadeArc || !g_lastWorld || MenuActive() || !InLevel()) return;
    {
        float dummy[3];
        if (Controls_ThrowDir(dummy)) return;   // let go: on its way
    }
    __try {
        w = ViewWeaponIndex();
        if (w > 0 && w <= MEM(int, ADDR_bg_numWeapons)) d = MEM(const BYTE *, ADDR_bg_weaponDefs + w * 4);
        if (!d || MEM(int, d + WEAPDEF_OFS_weaponType) != WEAPTYPE_GRENADE) return;
        speed = MEM(int, d + WEAPDEF_OFS_projectileSpeed);
        up = MEM(int, d + WEAPDEF_OFS_projectileSpeedUp);
        fuseMs = MEM(int, d + WEAPDEF_OFS_fuseTime);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (speed <= 0) return;
    fuse = fuseMs > 0 && fuseMs < 20000 ? fuseMs * 0.001f : 3.5f;
    body = BodyYaw(g_lastViewFwd);
    if (Controls_GrenadePose(&gh)) RotateZ(gh.axis[0], body, wv);   // the hand that holds it, in the world
    else memcpy(wv, g_lastViewFwd, sizeof(wv));
    for (k = 0; k < 3; k++) wv[k] *= speed;
    wv[2] += up;
    memcpy(p0, g_lastViewOrg, sizeof(p0));
    for (t = 0.05f; t <= fuse + 1e-4f && np < 72 && !hit; t += 0.05f) {
        float f;
        for (k = 0; k < 3; k++) p1[k] = g_lastViewOrg[k] + wv[k] * t;
        p1[2] -= 0.5f * cfg.gravity * t * t;
        f = WorldTrace(p0, p1, 0, MASK_PHYSICS, n, NULL);
        if (f < 1.0f) {
            if (f < 0) f = 0;
            for (k = 0; k < 3; k++) p1[k] = p0[k] + (p1[k] - p0[k]) * f;
            hit = 1;
        }
        if (t >= 0.1f || hit) WorldToTracking(p1, body, pts[np++]);   // from 0.1 s on: not inside the face
        memcpy(p0, p1, sizeof(p0));
    }
    op = cfg.grenadeOpacity;
    for (k = 0; k < 3; k++) col[k] = op;   // white, premultiplied
    col[3] = op;
    if (np >= 2) XR_SetPath((const float (*)[3])pts, np, 0.006f, col);
    if (hit && np >= 1) {
        // a ring flat on what it hits: its axes perpendicular to the surface's normal (in tracking space)
        float nt[3], a[3] = { 0, 0, 1 }, r[3], u[3], len;
        RotateZ(n, -body, nt);
        if (fabsf(nt[2]) > 0.9f) { a[0] = 1; a[2] = 0; }
        Cross(nt, a, r);
        len = sqrtf(Dot(r, r));
        if (len > 1e-3f) {
            for (k = 0; k < 3; k++) r[k] /= len;
            Cross(r, nt, u);
            XR_SetReticle(3, pts[np - 1], r, u, 0.25f, col);
        }
    }
    {
        static int logged;
        if (logged < 3) {
            logged++;
            Log("grenade line: %s, speed %d + %d up, fuse %.1f s, %d points, %s", MEM(const char *, d) ? MEM(const char *, d) : "?",
                speed, up, fuse, np, hit ? "lands on something" : "no hit before the fuse");
        }
    }
}

// ---------------------------------------------------------------------------
// Movement (as the CoD 1 mod): Blink / Shift (MoveMode), ladders, roomscale. The player is moved with the game's own
// SetClientOrigin (the "setorigin" script method's), on the server's playerState.
// ---------------------------------------------------------------------------
enum { MOVE_BLINK, MOVE_SHIFT, MOVE_CONTINUOUS };
enum { TP_IDLE, TP_FADE_OUT, TP_FADE_IN, TP_GLIDE };
#define TP_SPEED     640.0f   // launch speed of the arc (units per second): about 13 m at 45 degrees
#define TP_STEP      0.04f
#define TP_STEP_UP   18.0f
static int   g_tpState, g_tpAiming, g_tpAimOk, g_tpOwnsStick;
static float g_tpDest[3], g_tpFrom[3], g_tpT, g_tpFade;
static int   g_ladderMove;        // on a ladder, or pushing into one: the aim looks up so forward climbs
static int   g_roomOrgValid;      // roomscale: g_roomOrg is last frame's game view origin
static float g_roomOrg[3], g_roomF, g_roomR;
static int   g_roomSent;          // the last usercmd had a roomscale move in it

static BYTE *PlayerClient(void)
{
    BYTE *cl = NULL;
    if (!InLevel()) return NULL;
    __try {
        cl = MEM(BYTE *, ADDR_g_entities + GENT_OFS_client);
        if (cl) (void)MEM(volatile int, cl + PS_OFS_origin);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cl = NULL;
    }
    return cl;
}

// The game's player is put at o (feet), at rest.
static void SetPlayerOrigin(const float o[3])
{
    BYTE *cl = PlayerClient();
    DWORD fn = ADDR_SetClientOrigin;
    const BYTE *ent = (const BYTE *)ADDR_g_entities;
    if (!cl) return;
    __try {
        __asm {
            mov eax, ent
            mov edx, o
            call fn
        }
        memset(cl + PS_OFS_velocity, 0, 12);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged;
        if (!logged) { logged = 1; Log("move: fault in SetClientOrigin"); }
    }
    g_roomOrgValid = 0;   // not a roomscale step
}

// The soldier's box fits at the origin o (feet).
static int HullFits(const float o[3])
{
    static const float mins[3] = { -15, -15, 0 }, maxs[3] = { 15, 15, PLAYER_HEIGHT };
    float a[3] = { o[0], o[1], o[2] + 1.0f }, b[3] = { o[0], o[1], o[2] + 2.0f };
    return TraceBox(a, b, mins, maxs, 0, MASK_PLAYERSOLID, NULL, NULL, NULL) >= 1.0f;
}

// The arc from start along dir (world), as tracking-space points for the line; 1 when it ends on a floor where the
// soldier fits, with the origin to stand at in dest.
static int TeleportArc(const float start[3], const float dir[3], float body, float pts[64][3], int *count, float dest[3])
{
    float p0[3], p1[3], t, frac;
    int k, n = 0, hit = 0;
    memcpy(p0, start, sizeof(p0));
    memcpy(p1, start, sizeof(p1));
    for (t = TP_STEP; t < 2.5f && n < 63 && !hit; t += TP_STEP) {
        for (k = 0; k < 3; k++) p1[k] = start[k] + dir[k] * TP_SPEED * t;
        p1[2] -= 0.5f * cfg.gravity * t * t;
        frac = WorldTrace(p0, p1, 0, MASK_PLAYERSOLID, NULL, NULL);
        if (frac < 1.0f) {
            if (frac < 0) frac = 0;
            for (k = 0; k < 3; k++) p1[k] = p0[k] + (p1[k] - p0[k]) * frac;
            hit = 1;
        }
        WorldToTracking(p1, body, pts[n++]);
        memcpy(p0, p1, sizeof(p0));
    }
    *count = n;
    if (!hit) return 0;
    {
        // the floor within 48 units under the landing point (a wall hit a little above the floor counts)
        float top[3] = { p1[0], p1[1], p1[2] + 12.0f }, bot[3] = { p1[0], p1[1], p1[2] - 48.0f };
        frac = WorldTrace(top, bot, 0, MASK_PLAYERSOLID, NULL, NULL);
        if (frac >= 1.0f) return 0;
        dest[0] = top[0]; dest[1] = top[1]; dest[2] = top[2] + (bot[2] - top[2]) * frac + 0.25f;
    }
    return HullFits(dest);
}

// Blink (fade to black, move, fade in) and Shift (glide there in a quarter of a second along a clear line): pushing the
// move stick forward aims an arc with that stick's hand (the gun hand stays free), letting go moves the soldier where it
// lands if he fits there; pulling back cancels. Only on foot, playing, not on a ladder.
static void UpdateTeleport(float dt)
{
    float mx, my, *me;
    int hand, active;
    BYTE *cl = PlayerClient();
    active = cfg.moveMode != MOVE_CONTINUOUS && cl && g_lastWorld && !MenuActive() && !g_ladderMove && !g_deathFrame &&
             Controls_MoveStick(&mx, &my, &hand);
    g_tpOwnsStick = active || g_tpState != TP_IDLE;
    if (!active && g_tpState == TP_IDLE) { g_tpAiming = 0; g_tpFade = 0; return; }
    if (g_tpState == TP_FADE_OUT) {
        g_tpFade += dt / 0.12f;
        if (g_tpFade >= 1.0f) { g_tpFade = 1.0f; SetPlayerOrigin(g_tpDest); g_tpState = TP_FADE_IN; Log("move: blink to %.0f %.0f %.0f", g_tpDest[0], g_tpDest[1], g_tpDest[2]); }
        return;
    }
    if (g_tpState == TP_FADE_IN) {
        g_tpFade -= dt / 0.22f;
        if (g_tpFade <= 0) { g_tpFade = 0; g_tpState = TP_IDLE; }
        return;
    }
    if (g_tpState == TP_GLIDE) {
        float o[3];
        int k;
        g_tpT += dt / 0.25f;
        if (g_tpT >= 1.0f) { g_tpT = 1.0f; g_tpState = TP_IDLE; }
        for (k = 0; k < 3; k++) o[k] = g_tpFrom[k] + (g_tpDest[k] - g_tpFrom[k]) * g_tpT;
        SetPlayerOrigin(o);
        return;
    }
    if (!active) { g_tpAiming = 0; return; }
    me = (float *)(cl + PS_OFS_origin);
    if (my > 0.7f) {
        vr_pose_t h;
        float body = BodyYaw(g_lastViewFwd), s[3], d[3], pts[64][3], dest[3], col[4];
        int n = 0, ok;
        if (!Controls_HandPose(hand, &h) && !Controls_HandPose(1 - hand, &h)) h = g_head;
        TrackingToWorld(h.pos, g_lastViewOrg, body, s);
        RotateZ(h.axis[0], body, d);
        ok = TeleportArc(s, d, body, pts, &n, dest);
        col[0] = ok ? 0.2f * 0.85f : 1.0f * 0.85f; col[1] = ok ? 1.0f * 0.85f : 0.25f * 0.85f; col[2] = ok ? 0.4f * 0.85f : 0.2f * 0.85f; col[3] = 0.85f;
        if (n >= 2) XR_SetPath((const float (*)[3])pts, n, 0.008f, col);
        if (ok && n >= 1) {
            static const float r[3] = { 0, -1, 0 }, u[3] = { 1, 0, 0 };   // a ring lying on the floor
            float rr[3], uu[3];
            RotateZ(r, YawOf(g_head.axis[0]), rr);
            RotateZ(u, YawOf(g_head.axis[0]), uu);
            XR_SetReticle(3, pts[n - 1], rr, uu, 0.35f, col);
            memcpy(g_tpDest, dest, sizeof(g_tpDest));
        }
        g_tpAiming = 1;
        g_tpAimOk = ok;
    } else if (g_tpAiming && my < -0.6f) {
        g_tpAiming = 0;   // pulled back: no move
    } else if (g_tpAiming && my < 0.35f) {
        // let go: move if the last arc landed somewhere he fits
        g_tpAiming = 0;
        if (!g_tpAimOk) return;
        if (cfg.moveMode == MOVE_BLINK) {
            g_tpState = TP_FADE_OUT;
        } else {
            // Shift: the straight line must be clear (steps are walked over)
            static const float mins[3] = { -15, -15, 0 }, maxs[3] = { 15, 15, PLAYER_HEIGHT };
            float a[3] = { me[0], me[1], me[2] + TP_STEP_UP }, b[3] = { g_tpDest[0], g_tpDest[1], g_tpDest[2] + TP_STEP_UP };
            if (TraceBox(a, b, mins, maxs, 0, MASK_PLAYERSOLID, NULL, NULL, NULL) < 1.0f) { Log("move: shift blocked on the way"); return; }
            memcpy(g_tpFrom, me, sizeof(g_tpFrom));
            g_tpT = 0;
            g_tpState = TP_GLIDE;
            Log("move: shift to %.0f %.0f %.0f", g_tpDest[0], g_tpDest[1], g_tpDest[2]);
        }
        XR_Haptic(hand, 0.4f, 40);
    }
}

int VR_StickWalks(void) { return g_mode != MODE_VR || !g_tpOwnsStick; }

// A ladder right ahead (the game looks for one along its view, which in VR is the gun's aim): a short trace along where
// the head looks, a little low (the trace is from the eyes); a ladder is a surface with SURF_LADDER.
static int LadderAhead(float worldYaw)
{
    float end[3];
    int surf = 0;
    const float *start = g_lastViewOrg;
    end[0] = start[0] + cosf(RAD(worldYaw)) * 30.0f;
    end[1] = start[1] + sinf(RAD(worldYaw)) * 30.0f;
    end[2] = start[2] - 20.0f;
    return TraceBox(start, end, NULL, NULL, 0, MASK_PLAYERSOLID, NULL, NULL, &surf) < 1.0f && (surf & SURF_LADDER);
}

// Ladders (as the CoD 1 mod): the game climbs with "forward" while its view looks up, but in VR the view is the gun's.
// On a ladder (pm_flags PMF_LADDER), or pushing the move stick into one, the aim goes to where the head looks, 50
// degrees up: forward climbs, back goes down. Kept 0.8 s after the ladder lets go at the top, so the player steps off.
static void UpdateLadder(void)
{
    static int was;
    static DWORD until;
    float mx = 0, my = 0;
    int hand, on = 0, ahead = 0;
    BYTE *cl = PlayerClient();
    if (!cl || !g_lastWorld || MenuActive()) { g_ladderMove = 0; return; }
    __try {
        on = (MEM(int, cl + PS_OFS_pmFlags) & PMF_LADDER) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        on = 0;
    }
    Controls_MoveStick(&mx, &my, &hand);
    if (on) until = GetTickCount() + 800;
    if (!on && my > 0.3f) ahead = LadderAhead(BodyYaw(g_lastViewFwd) + YawOf(g_head.axis[0]));
    if (!on && my > 0.3f && GetTickCount() < until) ahead = 1;
    g_ladderMove = on || ahead;
    if (on != was) { was = on; Log("ladder: %s", on ? "on" : "off"); }
}

// Roomscale (as the CoD 1 mod's UpdateRoomScale, [VR] RoomScale): the soldier follows the real head. Once per world
// frame: what the game moved him because of the last roomscale command moves the tracking reference along (so the
// camera stays at the head), then the game is asked to walk him to the head again, closing the gap in about 0.2 s.
// Walls stop him as usual; the camera then stays where the head is. Not while the stick walks (the game moves him
// alone then) or Blink / Shift runs.
static void UpdateRoomScale(const float org[3], const float fwd[3])
{
    float body = BodyYaw(fwd), c[3], d[3], dl[3], len;
    int i;
    g_roomF = g_roomR = 0;
    if (!cfg.roomScale || !cfg.positional || g_mode != MODE_VR || !InLevel()) { g_roomOrgValid = 0; return; }
    if (Controls_MoveAmount() > 0.15f || g_tpState != TP_IDLE || g_ladderMove) { g_roomOrgValid = 0; return; }
    if (g_roomOrgValid) {
        for (i = 0; i < 2; i++) d[i] = org[i] - g_roomOrg[i];
        d[2] = 0;
        if (d[0] * d[0] + d[1] * d[1] > 60.0f * 60.0f) {
            // teleport, level change, cutscene: start over from the current head position
            g_ref[0] = g_head.pos[0]; g_ref[1] = g_head.pos[1];
        } else if (g_roomSent) {
            RotateZ(d, -body, dl);
            g_ref[0] += dl[0] / cfg.worldScale;
            g_ref[1] += dl[1] / cfg.worldScale;
        }
    }
    memcpy(g_roomOrg, org, sizeof(g_roomOrg));
    g_roomOrgValid = 1;
    c[0] = g_head.pos[0] - g_ref[0];
    c[1] = g_head.pos[1] - g_ref[1];
    c[2] = 0;
    len = sqrtf(c[0] * c[0] + c[1] * c[1]);
    if (len > 1.5f) { g_ref[0] = g_head.pos[0]; g_ref[1] = g_head.pos[1]; return; }   // far: catch up at once
    if (len < 0.03f) return;
    {
        // the usercmd moves relative to the game's view (the aim): rotate from tracking into it
        float v[3], m, speed = len * cfg.worldScale / 0.2f, amp;
        RotateZ(c, -g_aimYawCmd, v);
        v[0] /= len; v[1] /= len;
        m = fabsf(v[0]) > fabsf(v[1]) ? fabsf(v[0]) : fabsf(v[1]);
        amp = speed / 190.0f / m;   // pmove: speed = g_speed (190) * max(|f|,|r|) / 127
        if (amp > 1.0f / m) amp = 1.0f / m;
        g_roomF = v[0] * amp;
        g_roomR = -v[1] * amp;
    }
}

int VR_RoomMove(float *forward, float *right)
{
    g_roomSent = (g_roomF != 0 || g_roomR != 0) && g_mode == MODE_VR;
    if (!g_roomSent) return 0;
    *forward = g_roomF > 1 ? 1 : g_roomF < -1 ? -1 : g_roomF;
    *right = g_roomR > 1 ? 1 : g_roomR < -1 ? -1 : g_roomR;
    return 1;
}

// ---------------------------------------------------------------------------
// HUD modes and the subtitles' size (as the CoD 1 mod's UpdateHudMode, with CoD2's own dvars). CoD2's HUD fades each
// part out by itself a while after it last changed (hud_fade_*: ammo 3 s, health bar 2 s, compass 8 s, stance 1.7 s,
// offhand 3 s; flags 0x1001, registered at 0x4a726f..); a part a mode leaves out fades at once (0.01 s), so it only
// flickers when it changes. cg_hudCompassSize 0 takes the compass away, cg_subtitles the subtitles. Holding the
// objectives shows everything. The subtitles: cg_subtitleCharHeight (12 by default) times SubtitleSize x 1.25, the
// line (cg_subtitleWidthStandard / Widescreen, 80 characters) shorter by as much, so it still fits.
// ---------------------------------------------------------------------------
enum { HUD_FULL, HUD_NONE, HUD_SUBTITLES, HUD_COMPASS, HUD_STATUS };
#define SUBTITLE_BOOST 1.25f

static void UpdateHudMode(void)
{
    static int shown = -1;
    static float shownSize = -1;
    int mode = Controls_ObjectivesHeld() ? HUD_FULL : cfg.hudMode, h, w, y;
    float k = cfg.subtitleSize * SUBTITLE_BOOST;
    const char *ammo, *health, *compass, *stance, *offhand;
    char line[512];
    if (!InLevel()) { shown = -1; return; }   // set again in each level (the cgame registers its dvars per level)
    if (mode < HUD_FULL || mode > HUD_STATUS) mode = HUD_FULL;
    if (mode == shown && cfg.subtitleSize == shownSize) return;
    ammo = mode == HUD_FULL || mode == HUD_STATUS ? "3" : "0.01";
    health = mode == HUD_FULL || mode == HUD_STATUS ? "2" : "0.01";
    compass = mode == HUD_FULL || mode == HUD_COMPASS ? "8" : "0.01";
    stance = mode == HUD_FULL ? "1.7" : "0.01";
    offhand = mode == HUD_FULL || mode == HUD_STATUS ? "3" : "0.01";
    h = (int)(12.0f * k + 0.5f);
    w = (int)(80.0f / k + 0.5f);
    y = (int)(480.0f - 80.0f * k + 0.5f);   // the block grows upwards: as far from the bottom as it is tall
    if (h < 1) h = 1;
    if (w < 20) w = 20;
    if (w > 256) w = 256;
    if (y < 200) y = 200;
    _snprintf(line, sizeof(line), "set hud_fade_ammodisplay %s; set hud_fade_healthbar %s; set hud_fade_compass %s; set hud_fade_stance %s; "
              "set hud_fade_offhand %s; set cg_hudCompassSize %s; set cg_subtitles %d; set cg_subtitleCharHeight %d; "
              "set cg_subtitleWidthStandard %d; set cg_subtitleWidthWidescreen %d; set cg_subtitlePosY %d\n",
              ammo, health, compass, stance, offhand, mode == HUD_FULL || mode == HUD_COMPASS ? "1" : "0",
              mode == HUD_FULL || mode == HUD_SUBTITLES, h, w, w, y);
    line[sizeof(line) - 1] = 0;
    Controls_Command(line);
    if (mode != shown) Log("HUD mode %d%s", mode, mode != cfg.hudMode ? " (the objectives are held)" : "");
    if (cfg.subtitleSize != shownSize) Log("subtitles: %d high, %d characters a line, at %d", h, w, y);
    shown = mode;
    shownSize = cfg.subtitleSize;
}

void VR_Command(const char *cmd)
{
    if (!_stricmp(cmd, "recenter")) Recenter();
    else if (!_stricmp(cmd, "vignette")) CycleVignette();
    // weapon 1 <-> weapon 2 without reaching for the shoulders: CoD2 has two weapon slots, and weapnext
    // goes from one to the other
    else if (!_stricmp(cmd, "switchweapon")) Controls_Command("weapnext\n");
    else Log("unknown command vr:%s", cmd);
}

// Grenades held (as with "cookOffHold 1" in the weapon files, which CoD2 mods use for cooking): the
// pin is pulled when the grip takes one at the lower back and it stays in the hand until the grip lets
// go. The SP grenades come with 0 (thrown right after the pin pull, held or not). Set in memory for
// every grenade of the level (the definitions are loaded per level), checked every second.
// Hip fire accuracy (as the CoD 1 mod's HipFireAccuracy): in VR the gun is aimed with its own sights
// without the aim-down-sights button, and the flat game's hip spread (2 to 8 degrees on a rifle, 8
// more while moving, against 0.2 aimed) scattered the shots around the crosshair. Every bullet weapon's
// hip spread is set to its aimed spread, without the additions for firing, turning and moving.
// Instant ADS (as the CoD 1 mod's [Scope] InstantAds): a scoped weapon (it has an overlay picture) takes no
// time to go to its zoom and back. DrawTime (as the CoD 1 mod's): putting a weapon away and raising the next
// one take at most that long (the hand takes it from the shoulder; the game's own lowering and raising took
// most of a second).
static void WeaponTweaks(void)
{
    int i, n, k;
    if ((!cfg.grenadeHold && !cfg.hipAccuracy && !cfg.instantAds && cfg.drawTime <= 0) || !InLevel()) return;
    __try {
        n = MEM(int, ADDR_bg_numWeapons);
        if (n > 128) n = 128;
        for (i = 1; i <= n; i++) {
            BYTE *d = MEM(BYTE *, ADDR_bg_weaponDefs + i * 4);
            int type;
            if (!d) continue;
            type = MEM(int, d + WEAPDEF_OFS_weaponType);
            if (cfg.grenadeHold && type == WEAPTYPE_GRENADE && !MEM(int, d + WEAPDEF_OFS_cookOffHold)) {
                MEM(int, d + WEAPDEF_OFS_cookOffHold) = 1;
                Log("grenades: %s is held until released (cookOffHold)", MEM(const char *, d) ? MEM(const char *, d) : "?");
            }
            if (cfg.hipAccuracy && type == WEAPTYPE_BULLET) {
                float ads = MEM(float, d + WEAPDEF_OFS_adsSpread), *hip = (float *)(d + WEAPDEF_OFS_hipSpreadStandMin);
                if (hip[3] != ads || hip[9] != 0) {   // not done yet (Max, MoveAdd)
                    Log("hip fire: %s spread %.1f .. %.1f (+%.1f moving) -> %.2f", MEM(const char *, d) ? MEM(const char *, d) : "?",
                        hip[0], hip[3], hip[9], ads);
                    for (k = 0; k < 6; k++) hip[k] = ads;   // StandMin, DuckedMin, ProneMin, Max, DuckedMax, ProneMax
                    hip[7] = hip[8] = hip[9] = 0;            // FireAdd, TurnAdd, MoveAdd
                }
            }
            if (cfg.instantAds) {
                const char *ov = MEM(const char *, d + WEAPDEF_OFS_adsOverlayShader);
                int *t = (int *)(d + WEAPDEF_OFS_adsTransInTime);   // in, out (ms)
                if ((DWORD)ov > 0x10000 && ov[0] && (t[0] > 1 || t[1] > 1)) {
                    Log("instant ADS: %s zooms at once (was %d / %d ms, overlay %s)", MEM(const char *, d) ? MEM(const char *, d) : "?", t[0], t[1], ov);
                    t[0] = t[1] = 1;
                }
            }
            if (cfg.drawTime > 0 && type != WEAPTYPE_GRENADE) {
                int *t = (int *)(d + WEAPDEF_OFS_dropTime), changed = 0;   // drop, raise, alt drop, alt raise, quick drop, quick raise
                for (k = 0; k < 6; k++) if (t[k] > cfg.drawTime) { t[k] = cfg.drawTime; changed = 1; }
                if (changed) Log("draw time: %s raised and put away in %d ms", MEM(const char *, d) ? MEM(const char *, d) : "?", cfg.drawTime);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cfg.grenadeHold = cfg.hipAccuracy = cfg.instantAds = 0;
        cfg.drawTime = 0;
        Log("weapons: fault reading the weapon definitions, left as they are");
    }
}

// Recoil with both hands on the gun (as the CoD 1 mod's TwoHandRecoilReduction): every weapon's kick - of the
// view model (gun kick) and of the camera (view kick), aimed and from the hip - is scaled by 1 - the reduction
// while the gun is held with both hands, and put back when it is not. The values the game loaded are kept per
// weapon; a value that is not what was written (a new level loaded the definitions again) is taken as the new
// original.
#define KICK_VALUES 16
static struct { const BYTE *def; float orig[KICK_VALUES], set[KICK_VALUES]; } g_kick[129];

static void UpdateTwoHandKick(void)
{
    static const int groups[4] = { WEAPDEF_OFS_adsGunKick, WEAPDEF_OFS_adsViewKick, WEAPDEF_OFS_hipGunKick, WEAPDEF_OFS_hipViewKick };
    static float applied = 1.0f;
    static DWORD last;
    float factor = Controls_TwoHanded() ? 1.0f - cfg.twoHandKick : 1.0f;
    int i, n, g, k;
    if (cfg.twoHandKick <= 0 && applied == 1.0f) return;
    // on a change, and every second (weapons are loaded as the level uses them)
    if (factor == applied && GetTickCount() - last < 1000) return;
    if (!InLevel()) { applied = 1.0f; return; }
    last = GetTickCount();
    if (factor != applied) Log("recoil: %s", factor < 1.0f ? "reduced (both hands on the gun)" : "the game's own");
    applied = factor;
    __try {
        n = MEM(int, ADDR_bg_numWeapons);
        if (n > 128) n = 128;
        for (i = 1; i <= n; i++) {
            BYTE *d = MEM(BYTE *, ADDR_bg_weaponDefs + i * 4);
            if (!d) continue;
            for (g = 0; g < 4; g++)
                for (k = 0; k < 4; k++) {
                    float *v = (float *)(d + groups[g]) + k;
                    int j = g * 4 + k;
                    if (g_kick[i].def != d || *v != g_kick[i].set[j]) g_kick[i].orig[j] = *v;
                    *v = g_kick[i].set[j] = g_kick[i].orig[j] * factor;
                }
            g_kick[i].def = d;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cfg.twoHandKick = 0;
        applied = 1.0f;
        Log("recoil: fault reading the weapon definitions, left as they are");
    }
}

static void ControllersFrame(void)
{
    LARGE_INTEGER now, freq;
    vr_pose_t scr;
    float dt, w, h;
    static DWORD nextTuning;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    dt = g_lastTick.QuadPart ? (float)(now.QuadPart - g_lastTick.QuadPart) / freq.QuadPart : 0;
    if (dt > 0.1f) dt = 0.1f;
    g_lastTick = now;
    if (GetTickCount() >= nextTuning) { nextTuning = GetTickCount() + 1000; ReloadWeaponTuning(); WeaponTweaks(); }
    // placed when it comes up, and again when the head has moved well above or below it (the first
    // frames of the game, before the headset's tracking settles; sitting down or standing up): it does
    // not follow the head otherwise (following it made players dizzy in the CoD 1 mod)
    if (MenuActive() && (!g_menuShown || !g_menuPlaced || fabsf(g_head.pos[2] - g_menuRefZ) > 0.3f)) PlaceMenu();
    g_menuShown = MenuActive();
    CurrentScreen(&scr, &w, &h);
    Controls_SetHead(&g_head);
    Controls_Frame(dt, &scr, w, h);
    if (g_deathFrame) XR_ClearLines();   // the death screen is not shown: no menu beam either
    if (InLevel()) UpdateWeaponOffset();
    UpdateTwoHandKick();
    TraceDiag();
    UpdateLadder();
    UpdateTeleport(dt);
    UpdateHudMode();
    UpdateGrenadeArc();
    UpdateDamageHaptics();
    UpdateVignette(dt);
    UpdatePhysicalCrouch();
    UpdateCrosshair();
    SwingMelee(dt);
}

// Diagnostics: what the left eye holds (corners at 3 % in, and the centre), to check that a
// full-screen picture covers the whole eye.
static void ProbeEye(void)
{
    static IDirect3DSurface9 *sys;
    static DWORD next;
    static int lines;
    D3DLOCKED_RECT lr;
    static const float at[5][2] = { { 0.03f, 0.03f }, { 0.97f, 0.03f }, { 0.5f, 0.5f }, { 0.03f, 0.97f }, { 0.97f, 0.97f } };
    char line[256];
    int k, n = 0;
    // in the headset only while a full-screen picture is in the eyes, a few times (it stalls the GPU)
    if (lines >= (g_targetsVR == 2 ? 60 : 15) || GetTickCount() < next || !g_eyeSurf[0]) return;
    if (g_targetsVR == 1 && !g_fullPainted) return;
    next = GetTickCount() + (g_targetsVR == 2 ? 700 : 1000);
    if (!sys && FAILED(IDirect3DDevice9Ex_CreateOffscreenPlainSurface(g_dev, g_eyeW, g_eyeH, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL))) return;
    if (FAILED(IDirect3DDevice9Ex_GetRenderTargetData(g_dev, g_eyeSurf[0], sys))) return;
    if (FAILED(IDirect3DSurface9_LockRect(sys, &lr, NULL, D3DLOCK_READONLY))) return;
    n = _snprintf(line, sizeof(line), "eye probe: world %d, pictures %d, pixels", g_worldThisFrame, g_fullPainted);
    for (k = 0; k < 5; k++) {
        DWORD c = ((DWORD *)((BYTE *)lr.pBits + (int)(at[k][1] * (g_eyeH - 1)) * lr.Pitch))[(int)(at[k][0] * (g_eyeW - 1))];
        n += _snprintf(line + n, sizeof(line) - n, " %06x", c & 0xffffff);
    }
    IDirect3DSurface9_UnlockRect(sys);
    lines++;
    Log("%s (corners and centre)", line);
}

// The Patreon supporters beside the game's main menu (as the CoD 1 mod): the launcher draws the list (supporters.ini)
// into %APPDATA%\CoD2-VR\supporters_panel.bin ("SUPP", width, height, premultiplied BGRA, bottom row first) each time it
// starts; the mod loads it the first time the main menu is up. No file, no panel (tried again every 5 s).
static void LoadSupporterPanel(void)
{
    static DWORD tried;
    char path[MAX_PATH], *slash;
    HANDLE f;
    DWORD head[3], got = 0;
    BYTE *px;
    if (XR_PanelReady() || !g_userIni[0]) return;
    if (tried && GetTickCount() - tried < 5000) return;
    tried = GetTickCount() | 1;
    strncpy(path, g_userIni, MAX_PATH - 40);
    path[MAX_PATH - 40] = 0;
    slash = strrchr(path, '\\');
    if (!slash) return;
    strcpy(slash + 1, "supporters_panel.bin");
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        static int told;
        if (!told) { told = 1; Log("supporters: no panel file at %s (the launcher writes it when it starts)", path); }
        return;
    }
    if (!ReadFile(f, head, sizeof(head), &got, NULL) || got != sizeof(head) || head[0] != 0x50505553 ||
        head[1] < 16 || head[1] > 2048 || head[2] < 16 || head[2] > 4096) {
        CloseHandle(f);
        Log("supporters: %s is not a panel file", path);
        return;
    }
    px = (BYTE *)malloc((size_t)head[1] * head[2] * 4);
    if (!px || !ReadFile(f, px, head[1] * head[2] * 4, &got, NULL) || got != head[1] * head[2] * 4) {
        CloseHandle(f);
        free(px);
        Log("supporters: %s is cut short", path);
        return;
    }
    CloseHandle(f);
    if (XR_SetPanelImage(px, (int)head[1], (int)head[2])) Log("supporters: panel %ux%u loaded", head[1], head[2]);
    free(px);
}

// The game draws the objectives almost to the screen's edge: to leave ObjectivesMargin of the dark panel
// around them (as the CoD 1 mod's InsetHud), the HUD image is put back a little smaller, centred, before
// it goes to the headset (the panel is drawn under it there).
static void InsetHud(void)
{
    int mx = (int)(g_hudW * cfg.objMargin), my = (int)(g_hudH * cfg.objMargin);
    RECT r;
    if ((mx <= 0 && my <= 0) || !g_hudSurf) return;
    if (!g_hudCopy && FAILED(IDirect3DDevice9Ex_CreateRenderTarget(g_dev, g_hudW, g_hudH, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0,
                                                                   FALSE, &g_hudCopy, NULL))) {
        g_hudCopy = NULL;
        cfg.objMargin = 0;
        Log("objectives: no surface for the margin, none");
        return;
    }
    if (FAILED(IDirect3DDevice9Ex_StretchRect(g_dev, g_hudSurf, NULL, g_hudCopy, NULL, D3DTEXF_NONE))) return;
    IDirect3DDevice9Ex_ColorFill(g_dev, g_hudSurf, NULL, 0x00000000);
    r.left = mx; r.top = my; r.right = g_hudW - mx; r.bottom = g_hudH - my;
    IDirect3DDevice9Ex_StretchRect(g_dev, g_hudCopy, NULL, g_hudSurf, &r, D3DTEXF_LINEAR);
}

// The mod's frame work runs on a stack of its own. CoD2SP_s.exe asks Windows for 64 KB of stack per thread (its header),
// and the game is already deep in its own calls when it presents a frame: the SteamVR runtime (its xrWaitFrame, IPC) ran
// out of that stack and the game closed (exception c00000fd, stack overflow), where Virtual Desktop's runtime fitted.
// A fiber with 4 MB is switched to for the work and back (the same thread: D3D9, D3D11 and OpenXR see no difference).
static LPVOID g_gameFiber, g_workFiber;
static void (*g_fiberWork)(void);
static int g_fiberFailed;

static void CALLBACK WorkFiber(LPVOID unused)
{
    (void)unused;
    for (;;) {
        g_fiberWork();
        SwitchToFiber(g_gameFiber);
    }
}

static void RunOnBigStack(void (*work)(void))
{
    if (!g_workFiber && !g_fiberFailed) {
        g_gameFiber = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
        if (!g_gameFiber && GetLastError() == ERROR_ALREADY_FIBER) g_gameFiber = GetCurrentFiber();
        if (g_gameFiber) g_workFiber = CreateFiberEx(0, 4 << 20, FIBER_FLAG_FLOAT_SWITCH, WorkFiber, NULL);
        if (!g_workFiber) { g_fiberFailed = 1; Log("stack: no fiber for the frame work (%lu), on the game's stack", GetLastError()); }
        else Log("stack: the frame work runs on a 4 MB stack of its own (the game's threads have 64 KB)");
    }
    if (!g_workFiber || GetCurrentFiber() != g_gameFiber) { work(); return; }   // another thread, or already on it
    g_fiberWork = work;
    SwitchToFiber(g_workFiber);
}

static void BeforePresentWork(void)
{
    vr_hud_t hud;
    vr_pose_t objPose;
    int objPosed;
    if (!g_hooked) return;
    XR_FlushHaptics();
    g_lastWorld = g_worldThisFrame;
    if (g_beTarget != TGT_WINDOW) BE_SetTarget(TGT_WINDOW);
    if (g_mode == MODE_VR && g_probeEyes) ProbeEye();
    if (g_mode == MODE_VR && g_targetsVR == 2) {
        IDirect3DSurface9 *bb = NULL;
        if (SUCCEEDED(IDirect3DDevice9Ex_GetBackBuffer(g_dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
            RECT l = { 0, 0, g_winW / 2, g_winH }, r = { g_winW / 2, 0, g_winW, g_winH };
            RECT q = { g_winW * 3 / 4, g_winH * 3 / 4, g_winW, g_winH };
            if (g_worldThisFrame) {
                IDirect3DDevice9Ex_StretchRect(g_dev, g_eyeSurf[0], NULL, bb, &l, D3DTEXF_LINEAR);
                IDirect3DDevice9Ex_StretchRect(g_dev, g_eyeSurf[1], NULL, bb, &r, D3DTEXF_LINEAR);
                IDirect3DDevice9Ex_StretchRect(g_dev, g_hudSurf, NULL, bb, &q, D3DTEXF_LINEAR);
            } else {
                IDirect3DDevice9Ex_StretchRect(g_dev, g_hudSurf, NULL, bb, NULL, D3DTEXF_LINEAR);
            }
            IDirect3DSurface9_Release(bb);
        }
        return;
    }
    if (g_mode == MODE_VR && g_xrIdle) {
        // the loading screen moved on: the game's frame again (the backend's thread stops first)
        XR_KeepAlive(0, NULL);
        g_xrIdle = 0;
        if (!XR_BeginFrame(g_eyes, &g_head) && XR_FrameOpen()) XR_EndFrame(NULL);
        XR_PollScreenPress();   // a movie presents every frame: its presses are read here too
    }
    if (g_mode != MODE_VR || !XR_FrameOpen()) return;
    objPosed = !MenuActive() && Controls_ObjectivesPose(&g_head, &objPose);
    if (objPosed) InsetHud();
    FlushGPU();   // the shared textures are complete before D3D11 copies them
    if (cfg.mirror) {
        IDirect3DSurface9 *bb = NULL;
        if (SUCCEEDED(IDirect3DDevice9Ex_GetBackBuffer(g_dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
            // the middle of the left eye, at the window's aspect
            IDirect3DSurface9 *src = g_worldThisFrame ? g_eyeSurf[0] : g_hudSurf;
            int sw = g_worldThisFrame ? g_eyeW : g_hudW, sh = g_worldThisFrame ? g_eyeH : g_hudH, cw = sw, ch = sh;
            RECT r;
            if (g_winW * sh > g_winH * sw) ch = sw * g_winH / g_winW; else cw = sh * g_winW / g_winH;
            r.left = (sw - cw) / 2; r.top = (sh - ch) / 2; r.right = r.left + cw; r.bottom = r.top + ch;
            IDirect3DDevice9Ex_StretchRect(g_dev, src, &r, bb, NULL, D3DTEXF_LINEAR);
            IDirect3DSurface9_Release(bb);
        }
    }
    hud = cfg.hud;
    hud.world = g_worldThisFrame;
    hud.full = g_fullPainted;
    if (g_deathFrame) { hud.hideHud = 1; hud.full = 0; }   // the death screen: only the world
    hud.vignette = hud.world ? g_vigAmount * cfg.vigStrength : 0;
    hud.fade = hud.world ? g_tpFade : 0;   // Blink
    if (MenuActive()) {
        hud.width = cfg.hud.menuWidth;
        if (!cfg.menuFollowsHead && g_menuPlaced) { hud.posed = 1; hud.pose = g_menuPose; }
    } else if (objPosed) {
        hud.pose = objPose;
        hud.posed = 1;   // the objectives on the back of the off hand, like a watch
        hud.width = cfg.objWatchWidth;
        hud.panel = cfg.objBackground;   // on a dark rounded panel, so they read against the world
    }
    if (MenuActive() && !InLevel() && g_menuPlaced && !cfg.menuFollowsHead) {
        // the main menu: the supporters' panel outside the menu screen, on its left (the same height centre), so the
        // menus never cover it or get covered
        LoadSupporterPanel();
        if (XR_PanelReady()) {
            const float panelWidth = 0.365f, gap = 0.07f;
            float pos[3];
            int k;
            for (k = 0; k < 3; k++) pos[k] = g_menuPose.pos[k] + g_menuPose.axis[1][k] * (cfg.hud.menuWidth * 0.5f + gap + panelWidth * 0.5f);
            XR_ShowPanel(pos, (const float (*)[3])g_menuPose.axis, panelWidth);
        }
    }
    g_lastHud = hud;
    g_lastHud.world = 0;   // the eyes are not shown again: only the screens
    g_lastHudValid = 1;
    XR_EndFrame(&hud);
}

// A press on the loading screen (the briefing video plays while the level loads, and the game reads no input then) is
// kept until the level is loaded and the start screen is up (pregame.menu, "click to start the mission"), and starts the
// mission there: as on a PC, where a click while loading counts once it is done. Kept a minute at most.
static DWORD g_screenPressAt;

static void ScreenPressFrame(void)
{
    if (!g_screenPressAt) return;
    if (GetTickCount() - g_screenPressAt > 60000) { g_screenPressAt = 0; return; }
    if (!(MEM(int, ADDR_cls_keyCatchers) & KEYCATCH_UI) || g_worldThisFrame) return;
    Log("screen: the press on the loading screen goes on (click, Enter)");
    Controls_ScreenPress();
    g_screenPressAt = 0;
}

static void AfterPresentWork(void)
{
    static int logged;
    if (!g_hooked) return;
    StartVR();
    g_vrFrame = 0;
    // A loading screen (no world, no menu): the game draws a frame only when the bar moves, seconds
    // apart. The headset is given frames meanwhile by the backend's own thread, with the last screen
    // where it was (it jumped about at every step of the bar); the next frame of the game takes over.
    g_xrIdle = g_targetsVR == 1 && XR_Ready() && g_lastHudValid && !g_worldThisFrame &&
               !(MEM(int, ADDR_cls_keyCatchers) & KEYCATCH_UI);
    if (g_xrIdle) {
        // fixed in front like a menu (loading straight from play has not placed it yet)
        if (!g_menuShown || !g_menuPlaced) PlaceMenu();
        g_menuShown = 1;
        if (!cfg.menuFollowsHead) { g_lastHud.posed = 1; g_lastHud.pose = g_menuPose; g_lastHud.width = cfg.hud.menuWidth; }
        XR_KeepAlive(1, &g_lastHud);
        if (XR_KeepAlivePresses()) {
            int cin = MEM(int, ADDR_cin_handle);
            if (cin >= 0 && cin < 16 && MEM(BYTE, ADDR_cin_skippable)) {
                // a movie (the campaign's, before Moscow): stopped as the game stops a skippable one, which goes on to the level
                Log("cinematic %d: skipped (the game's own stop)", cin);
                Controls_SkipCinematic();
            } else {
                Log("screen: the press is kept for the start screen (cinematic %d, skippable %d)", cin, MEM(BYTE, ADDR_cin_skippable));
                g_screenPressAt = GetTickCount();
            }
        }
        return;
    }
    if (g_targetsVR && XR_Ready()) {
        g_vrFrame = XR_BeginFrame(g_eyes, &g_head);
        if (!g_vrFrame && XR_FrameOpen()) XR_EndFrame(NULL);   // not shown: close it at once
        // the controllers first: the gun hand of this frame is what the aim follows
        if (g_vrFrame) { XR_ScreenPressReset(); ScreenPressFrame(); ScriptMenuAnswerFrame(); if (InLevel()) FxCullFrame(); ControllersFrame(); UpdateViewAngles(); }
        if (g_vrFrame && !logged) { logged = 1; Log("VR: first headset frame"); }
    }
}

void VR_BeforePresent(void) { RunOnBigStack(BeforePresentWork); }
void VR_AfterPresent(void) { RunOnBigStack(AfterPresentWork); }

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
static void PatchPointer(DWORD addr, void *value)
{
    DWORD old;
    VirtualProtect((void *)addr, 4, PAGE_EXECUTE_READWRITE, &old);
    MEM(void *, addr) = value;
    VirtualProtect((void *)addr, 4, old, &old);
}

static int PatchCall(DWORD site, DWORD expect, void *target)
{
    DWORD old;
    if (MEM(BYTE, site) != 0xE8 || site + 5 + MEM(DWORD, site + 1) != expect) {
        Log("call at %08x does not go to %08x: not patched", site, expect);
        return 0;
    }
    VirtualProtect((void *)site, 5, PAGE_EXECUTE_READWRITE, &old);
    MEM(DWORD, site + 1) = (DWORD)target - (site + 5);
    VirtualProtect((void *)site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)site, 5);
    return 1;
}

static int InstallHooks(void)
{
    HMODULE gfx = GetModuleHandleA("gfx_d3d_x86_s.dll");
    DWORD *re = (DWORD *)ADDR_re, cvar;
    if (!gfx) { Log("gfx_d3d_x86_s.dll not loaded"); return 0; }
    g_gfx = (DWORD)gfx;
    if (PeTimestamp(g_gfx) != GFXD3D_TIMESTAMP) { Log("unknown gfx_d3d_x86_s.dll (timestamp %08x)", PeTimestamp(g_gfx)); return 0; }
    // the client's table must be the copy of this renderer's
    if (re[RE_RenderScene / 4] != GFX(GFX_R_RenderScene)) {
        Log("re.RenderScene is %08x, expected %08x: not hooking", re[RE_RenderScene / 4], GFX(GFX_R_RenderScene));
        return 0;
    }
    p_SetupProjection = GFX(GFX_R_SetupProjection);
    p_RB_SetRenderTarget = GFX(GFX_RB_SetRenderTarget);
    p_AddCmdViewport = (PFN_AddCmdViewport)GFX(GFX_R_AddCmdSetViewport);
    if (!PatchCall(GFX(GFX_R_SetupProjection_call), p_SetupProjection, hk_SetupProjection)) return 0;
    Controls_Install();
    // the view weapon into the gun hand (the exe is patched once; a new device after vid_restart finds it done)
    if (MEM(BYTE, ADDR_CG_AddPlayerWeapon_call) == 0xE8 &&
        ADDR_CG_AddPlayerWeapon_call + 5 + MEM(DWORD, ADDR_CG_AddPlayerWeapon_call + 1) != (DWORD)hk_AddPlayerWeapon)
        PatchCall(ADDR_CG_AddPlayerWeapon_call, ADDR_CG_AddPlayerWeapon, hk_AddPlayerWeapon);
    // the view weapon's camera animation (reloads): through hk_ViewModelCamera (once per process)
    if (MEM(BYTE, ADDR_CG_ViewModelCamera_call) == 0xE8 &&
        ADDR_CG_ViewModelCamera_call + 5 + MEM(DWORD, ADDR_CG_ViewModelCamera_call + 1) != (DWORD)hk_ViewModelCamera)
        PatchCall(ADDR_CG_ViewModelCamera_call, ADDR_CG_ViewModelCamera, hk_ViewModelCamera);
    // the player's shots from the gun (checked; once per process)
    if (MEM(BYTE, ADDR_Bullet_Fire_call) == 0xE8 && ADDR_Bullet_Fire_call + 5 + MEM(DWORD, ADDR_Bullet_Fire_call + 1) != (DWORD)hk_BulletFire)
        PatchCall(ADDR_Bullet_Fire_call, ADDR_Bullet_Fire, hk_BulletFire);
    // the effects camera's sphere tests (decals, effects): their calls (checked; once per process)
    {
        static const DWORD sites[5] = { 0x4802e0, 0x482610, 0x48283d, 0x4841a0, 0x4ad743 };
        int s;
        for (s = 0; s < 5; s++)
            if (MEM(BYTE, sites[s]) == 0xE8 && sites[s] + 5 + MEM(DWORD, sites[s] + 1) != (DWORD)hk_FxCullSphere)
                PatchCall(sites[s], ADDR_FX_CullSphere, hk_FxCullSphere);
        if (MEM(BYTE, 0x48550b) == 0xE8 && 0x48550b + 5 + MEM(DWORD, 0x48550b + 1) != (DWORD)hk_FxCullSphere2)
            PatchCall(0x48550b, ADDR_FX_CullSphere2, hk_FxCullSphere2);
    }
    // the localized texts (the training's keyboard hints): a jump over the lookup's first 5 bytes (checked)
    {
        static const BYTE pro[5] = { 0x83, 0xEC, 0x20, 0x53, 0x56 };
        BYTE *p = (BYTE *)ADDR_SEH_StringEd_GetString;
        DWORD old;
        if (!memcmp(p, pro, 5)) {
            VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
            p[0] = 0xE9;
            MEM(DWORD, p + 1) = (DWORD)hk_StringEdGetString - ((DWORD)p + 5);
            VirtualProtect(p, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
        } else if (p[0] != 0xE9) Log("texts: unexpected bytes at the string lookup, the game's texts kept");
    }
    // the script's openMenu(): its slot in the method table (checked)
    if (MEM(DWORD, ADDR_Scr_OpenMenu_slot) == ADDR_Scr_OpenMenu) PatchPointer(ADDR_Scr_OpenMenu_slot, hk_OpenMenu);
    else if (MEM(DWORD, ADDR_Scr_OpenMenu_slot) != (DWORD)hk_OpenMenu) Log("openMenu: unexpected method table, not hooked");
    // the script's playerADS(): its slot in the method table (checked)
    if (MEM(DWORD, ADDR_Scr_PlayerADS_slot) == ADDR_Scr_PlayerADS) PatchPointer(ADDR_Scr_PlayerADS_slot, hk_PlayerADS);
    else if (MEM(DWORD, ADDR_Scr_PlayerADS_slot) != (DWORD)hk_PlayerADS) Log("playerADS: unexpected method table, not hooked");
    // shots: a jump over CG_FireWeapon's first 5 bytes (checked; once per process)
    {
        static const BYTE pro[5] = { 0x83, 0xEC, 0x10, 0x53, 0x55 };
        BYTE *p = (BYTE *)ADDR_CG_FireWeapon;
        DWORD old;
        if (!memcmp(p, pro, 5)) {
            VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
            p[0] = 0xE9;
            MEM(DWORD, p + 1) = (DWORD)hk_FireWeapon - (ADDR_CG_FireWeapon + 5);
            VirtualProtect(p, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
        } else if (p[0] != 0xE9) {
            Log("CG_FireWeapon: unexpected bytes, no vibration on shots");
        }
    }

    orig_RenderScene = (PFN_RenderScene)re[RE_RenderScene / 4];
    orig_BeginFrame = (PFN_Void)re[RE_BeginFrame / 4];
    orig_EndFrame = (PFN_Void)re[RE_EndFrame / 4];
    PatchPointer((DWORD)&re[RE_RenderScene / 4], hk_RenderScene);
    PatchPointer((DWORD)&re[RE_BeginFrame / 4], hk_BeginFrame);
    PatchPointer((DWORD)&re[RE_EndFrame / 4], hk_EndFrame);
    if (re[RE_DrawStretchPic / 4] == GFX(GFX_R_DrawStretchPic)) {
        orig_StretchPic = (PFN_StretchPic)re[RE_DrawStretchPic / 4];
        PatchPointer((DWORD)&re[RE_DrawStretchPic / 4], hk_StretchPic);
    } else Log("re.DrawStretchPic is %08x, not hooked (no full-view intro screens)", re[RE_DrawStretchPic / 4]);
    {
        DWORD slot = GFX(GFX_cmdTable) + GFX_CMD_SETVIEWPORT * 4;
        int i;
        orig_CmdViewport = (PFN_CmdHandler)MEM(DWORD, slot);
        PatchPointer(slot, hk_CmdViewport);
        if (cfg.traceCmds)
            for (i = 1; i < GFX_CMD_COUNT; i++) {
                slot = GFX(GFX_cmdTable) + i * 4;
                g_origCmd[i] = i == GFX_CMD_SETVIEWPORT ? orig_CmdViewport : (PFN_CmdHandler)MEM(DWORD, slot);
                PatchPointer(slot, g_traceThunks[i]);
            }
    }
    // the backend must run on the main thread, inside re.EndFrame (frame pacing, poses)
    cvar = MEM(DWORD, GFX(GFX_cvar_r_smp_backend));
    if (cvar && MEM(BYTE, cvar + 8)) { Log("r_smp_backend was on: turned off"); MEM(BYTE, cvar + 8) = 0; }
    Log("hooks installed (gfx at %08x)", g_gfx);
    return 1;
}

void VR_DeviceCreated(IDirect3DDevice9Ex *dev, const D3DPRESENT_PARAMETERS *pp)
{
    (void)dev;
    if (!cfg.enabled) return;
    g_winW = pp->BackBufferWidth;
    g_winH = pp->BackBufferHeight;
    // a new device (vid_restart): the old one's resources are gone with it, nothing to release
    g_flushQuery = NULL;
    memset(g_eyeTex, 0, sizeof(g_eyeTex)); memset(g_eyeSurf, 0, sizeof(g_eyeSurf)); memset(g_eyeDepth, 0, sizeof(g_eyeDepth));
    memset(g_eyeMS, 0, sizeof(g_eyeMS)); memset(g_eyeMSDepth, 0, sizeof(g_eyeMSDepth));
    g_hudTex = NULL; g_hudSurf = NULL; g_fullTex = NULL; g_fullSurf = NULL; g_hudCopy = NULL;
    ReleaseTargets();
    if (XR_Ready()) RunOnBigStack(XR_Shutdown);
    g_beTarget = TGT_WINDOW;
    g_bePass = 0;
    g_hooked = InstallHooks();
    if (!g_hooked) return;
    StartVR();
    if (!g_targetsVR && cfg.sbs) CreateTargets(0);
}

void VR_DeviceReset(const D3DPRESENT_PARAMETERS *pp)
{
    if (!g_hooked) return;
    g_beTarget = TGT_WINDOW;
    if ((int)pp->BackBufferWidth == g_winW && (int)pp->BackBufferHeight == g_winH) return;
    g_winW = pp->BackBufferWidth;
    g_winH = pp->BackBufferHeight;
    if (g_targetsVR) {
        if (!XR_ResizeHud(g_winW, g_winH) || !CreateTargets(1)) Log("VR: HUD resize failed");
    } else if (cfg.sbs) CreateTargets(0);
}
