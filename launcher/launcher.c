// Call of Duty 2 VR launcher: starts CoD2SP_s.exe and lets the player change the common settings.
//
// It lives in the game folder. The player's choices go to %APPDATA%\CoD2-VR\settings.ini, which
// the mod reads on top of cod2_vr.ini (the game folder is usually under Program Files, where
// writing needs administrator rights). Graphics settings of the game itself are passed on the
// command line when it is started (+set ...), so they hold whatever the game saved before.
//
// Plain Win32 + GDI, drawn by hand: a banner, a bar of buttons and pages of options.
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <olectl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "launcher_res.h"
#include "../third_party/miniz/miniz.h"
#define STB_DXT_IMPLEMENTATION
#include "../third_party/stb/stb_dxt.h"

#define VERSION_TEXT L"v1.0.1"
#define URL_GITHUB   "https://github.com/lambda-gab/cod2-vr"
#define URL_DISCORD  "https://discord.gg/SRsK2x5Tn"
#define URL_RELEASES "https://github.com/lambda-gab/cod2-vr/releases/latest"

// ---------------------------------------------------------------------------
// Layout, in pixels at 96 dpi (scaled by P())
// ---------------------------------------------------------------------------
#define BASE_W   1000
#define BASE_H   650
#define BANNER_H 364
#define BAR_Y    570

static float g_scale = 1.0f;
#define P(v) ((int)((v) * g_scale + 0.5f))

#define COL_GOLD      RGB(214, 170, 62)
#define COL_GOLD_DARK RGB(150, 112, 30)
#define COL_STEEL     RGB(170, 205, 220)
#define COL_TEXT      RGB(225, 225, 225)
#define COL_DIM       RGB(140, 140, 140)
#define COL_BAR       RGB(14, 14, 14)

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
// One option: choices are "key=value|key=value" lines written to [section] of settings.ini.
// The pages have the Call of Duty 1 VR mod's options, in its order; one the Call of Duty 2 mod does not have yet is
// shown greyed out (soon: SOON_LATER "Coming soon", SOON_NEVER "Not in Call of Duty 2") and cannot be changed.
enum { SOON_NO, SOON_LATER, SOON_NEVER };
typedef struct {
    const wchar_t *label, *hint;
    const char *section;
    int count;
    const wchar_t *names[10];
    const char *values[10];
    int def;
    int cur;      // -1 = what is set matches no choice ("Custom")
    int showRes;  // RenderScale choices: show the eye resolution each one gives
    int reticle;  // crosshair styles: a small picture of each one beside its name
    const wchar_t *const *choiceHints;   // a hint for each choice (instead of hint), or NULL
    int soon;     // SOON_*: not in this mod yet
    int swatch;   // a colour: shown as a swatch with its hex value, the click opens the colour picker (PickCrosshairColor)
} option_t;

static const wchar_t *SoonText(int soon) { return soon == SOON_NEVER ? L"Not in Call of Duty 2" : L"Coming soon"; }

static const wchar_t *const g_hudHints[] = {
    L"Everything on screen, as in the original game.",
    L"Nothing on screen. Hold the Objectives button to see health, ammo, compass and objectives.",
    L"Only the dialogue subtitles. Hold the Objectives button to see health, ammo and compass.",
    L"Only the compass. Hold the Objectives button to see health, ammo and subtitles.",
    L"Only health and ammo. Hold the Objectives button to see the compass, stance and subtitles.",
};

// VR page, two columns: comfort and movement on the left (VR_COL2 first on the right)
static option_t g_vrOpts[] = {
    { L"Movement", L"Continuous: walk with the move stick. Blink (beta): push the stick forward to aim an arc with that stick's hand (your gun hand stays free), let go to teleport (the screen blinks). Shift (beta): the same, gliding there.",
      "Controllers", 3, { L"Blink (beta)", L"Shift (beta)", L"Continuous" }, { "MoveMode=0", "MoveMode=1", "MoveMode=2" }, 2 },
    { L"Comfort vignette", L"Narrows the view while you move with the stick or turn smoothly. Helps if you feel sick.",
      "Comfort", 4, { L"Off", L"Low", L"Medium", L"High" },
      { "Vignette=0", "Vignette=1|VignetteStrength=0.35", "Vignette=1|VignetteStrength=0.6", "Vignette=1|VignetteStrength=0.9" }, 2 },
    { L"Vehicle vignette", L"Narrows the view while a vehicle carries you (truck, car, plane) and turns or speeds up.",
      "Comfort", 4, { L"Off", L"Low", L"Medium", L"High" },
      { "VehicleVignette=0", "VehicleVignette=0.35", "VehicleVignette=0.6", "VehicleVignette=0.9" }, 2, 0, 0, 0, NULL, SOON_LATER },
    { L"Vehicle turns", L"How fast the view follows a vehicle round a bend (truck, car). Smoother turns are gentler if bends make you dizzy.",
      "Comfort", 3, { L"Original", L"Smooth", L"Smoother" },
      { "VehicleTurnSmooth=0.15", "VehicleTurnSmooth=0.5", "VehicleTurnSmooth=1" }, 1, 0, 0, 0, NULL, SOON_LATER },
    { L"Camera shake", L"How much the view shakes in earthquakes and explosions (bombing runs, shelling). Less is gentler in VR.",
      "Comfort", 4, { L"Original", L"Reduced", L"Low", L"Off" },
      { "ShakeKeep=1", "ShakeKeep=0.4", "ShakeKeep=0.2", "ShakeKeep=0" }, 1 },
    { L"Turning", L"Right stick: turn in steps (gentler) or smoothly.",
      "Controllers", 4, { L"Snap 30\x00b0", L"Snap 45\x00b0", L"Snap 90\x00b0", L"Smooth" },
      { "SnapTurn=30", "SnapTurn=45", "SnapTurn=90", "SnapTurn=0" }, 1 },
    { L"Smooth turn speed", L"Only used with smooth turning.",
      "Controllers", 3, { L"Slow", L"Normal", L"Fast" },
      { "SmoothTurnSpeed=90", "SmoothTurnSpeed=150", "SmoothTurnSpeed=220" }, 1 },
    { L"Roomscale", L"Walk and lean for real: the soldier follows your body. (Off by default in Call of Duty 2 VR until it is tested.)",
      "VR", 2, { L"On", L"Off" }, { "RoomScale=1", "RoomScale=0" }, 1 },
    { L"Physical crouch", L"Crouching for real crouches the soldier. Recenter (left stick click) standing up straight.",
      "Controllers", 2, { L"On", L"Off" }, { "PhysicalCrouch=1", "PhysicalCrouch=0" }, 0 },
    { L"Crouch depth", L"How low your head goes to crouch the soldier.",
      "Controllers", 3, { L"Shallow 25 cm", L"Normal 32 cm", L"Deep 45 cm" },
      { "CrouchDepth=0.25", "CrouchDepth=0.32", "CrouchDepth=0.45" }, 1 },
    { L"Menus", L"Menus and loading screens: fixed in front of you, or following your head.",
      "HUD", 2, { L"Fixed", L"Follow head" }, { "MenuFollowsHead=0", "MenuFollowsHead=1" }, 0 },
    { L"HUD", L"What the HUD shows while you play.", "HUD", 5, { L"Full", L"Immersive (none)", L"Subtitles only", L"Compass only", L"Health and ammo" },
      { "HudMode=0", "HudMode=1", "HudMode=2", "HudMode=3", "HudMode=4" }, 0, 0, 0, 0, g_hudHints },
    { L"Subtitle size", L"How big the subtitles are. Small is a little bigger than the game's text; Extra Large is for reading from far away.",
      "HUD", 4, { L"Small", L"Medium", L"Large", L"Extra Large" },
      { "SubtitleSize=1", "SubtitleSize=1.5", "SubtitleSize=2.2", "SubtitleSize=3" }, 1 },
    // right column (the Call of Duty 1 VR launcher's order)
    // not a setting of cod2_vr.ini: the "no arms" pack in the game folder (HandsPack functions); the click is handled by Click
    { L"Player hands", L"(Beta) Shown: the soldier's arms follow your controllers from your shoulders. With some weapons the arms can look distorted.\n\nHidden: only the gun is in your hand. (Windows may ask for administrator rights.)",
      "Launcher", 2, { L"Shown (beta)", L"Hidden" }, { "", "" }, 1 },
    { L"Weapon handling", L"Always in hand: the weapon stays in your dominant hand (never dropped) and a grip on your body (either hand) switches weapons. Free hands: only the gun is in your hand, taken from and put back on your body, dropped when you let go.",
      "Controllers", 2, { L"Always in hand", L"Free hands" }, { "AlwaysArmed=1", "AlwaysArmed=0" }, 0, 0, 0, 0, NULL, SOON_LATER },
    { L"Crosshair", L"A crosshair where the gun is aimed, for playing without aiming down the sights.",
      "Controllers", 6, { L"None", L"Cross", L"Dot", L"Circle", L"Circle + dot", L"Cross + dot" },
      { "Crosshair=0", "Crosshair=1", "Crosshair=2", "Crosshair=3", "Crosshair=4", "Crosshair=5" }, 0, 0, 0, 1 },
    { L"Crosshair color", L"Click to pick the crosshair's colour from the colour box: drag in the square and the bar, or type the numbers (it only shows when a crosshair style above is chosen).",
      "Controllers", 8, { L"White", L"Red", L"Green", L"Yellow", L"Cyan", L"Blue", L"Orange", L"Magenta" },
      { "CrosshairColor=white", "CrosshairColor=red", "CrosshairColor=green", "CrosshairColor=yellow", "CrosshairColor=cyan",
        "CrosshairColor=blue", "CrosshairColor=orange", "CrosshairColor=magenta" }, 0, 0, 0, 0, NULL, 0, 1 },
    { L"Crosshair opacity", L"How solid the crosshair is: lower is gentler on the eyes, higher is easier to see.",
      "Controllers", 4, { L"30%", L"50%", L"80%", L"100%" },
      { "CrosshairOpacity=0.3", "CrosshairOpacity=0.5", "CrosshairOpacity=0.8", "CrosshairOpacity=1" }, 2 },
    { L"Grenade line", L"A line that shows where a grenade will fly and land.",
      "Grenade", 3, { L"Off", L"While aiming", L"Always" }, { "Trajectory=0", "Trajectory=1", "Trajectory=2" }, 1 },
    { L"Scope", L"Original: the game's own scope (the view zooms in with a round picture when you aim down the sights). Realistic (beta): the zoomed picture shows inside the rifle's own scope and you look through it by bringing the gun to your eye.",
      "Scope", 2, { L"Original", L"Realistic (beta)" }, { "Lens=0", "Lens=1" }, 0, 0, 0, 0, NULL, SOON_LATER },
    { L"Swing melee", L"Hit someone by swinging the gun at them, any part of it. Hard needs a faster swing.",
      "Controllers", 4, { L"Off", L"Light", L"Normal", L"Hard" },
      { "SwingMelee=0", "SwingMelee=1|MeleeSpeed=1.6", "SwingMelee=1|MeleeSpeed=2.2", "SwingMelee=1|MeleeSpeed=3" }, 2 },
    { L"Drop on release", L"Letting go of the grip away from its holster drops the weapon on the floor. Call of Duty 2 VR keeps the gun in your hand.",
      "Holsters", 2, { L"On", L"Off" }, { "DropOnRelease=1", "DropOnRelease=0" }, 0, 0, 0, 0, NULL, SOON_NEVER },
    { L"Two-hand steadiness", L"With both hands on the gun, hand tremor is damped; deliberate swings still follow quickly.",
      "Controllers", 4, { L"Off", L"Low", L"Normal", L"High" },
      { "TwoHandStabilization=0", "TwoHandStabilization=0.4", "TwoHandStabilization=0.75", "TwoHandStabilization=1" }, 2 },
    { L"Two-hand recoil", L"How much the gun kicks while you hold it with both hands.",
      "Controllers", 3, { L"Full", L"Reduced", L"Minimal" },
      { "TwoHandRecoilReduction=0", "TwoHandRecoilReduction=0.5", "TwoHandRecoilReduction=0.8" }, 2 },
    { L"Objectives on hand", L"Turn the back of your other hand to your eyes, like looking at a watch, to see the objectives on it.",
      "HUD", 2, { L"On", L"Off" }, { "ObjectivesGesture=1", "ObjectivesGesture=0" }, 0 },
    { L"Vibration: shots", L"Controller vibration when you fire.",
      "Controllers", 4, { L"Off", L"Light", L"Normal", L"Strong" },
      { "HapticsShots=0", "HapticsShots=0.35", "HapticsShots=0.6", "HapticsShots=1" }, 2 },
    { L"Vibration: hits", L"Both controllers shake when you are hit, harder for more damage.",
      "Controllers", 4, { L"Off", L"Light", L"Normal", L"Strong" },
      { "HapticsDamage=0", "HapticsDamage=0.35", "HapticsDamage=0.6", "HapticsDamage=1" }, 2 },
    { L"Vibration: holsters", L"Reaching a shoulder or the lower back, taking a weapon or a grenade there.",
      "Controllers", 4, { L"Off", L"Light", L"Normal", L"Strong" },
      { "HapticsHolsters=0", "HapticsHolsters=0.35", "HapticsHolsters=0.6", "HapticsHolsters=1" }, 2 },
};
#define VR_COL2  13
#define VR_HANDS 13   // the Player hands row (the first of the right column)

static option_t g_gfxOpts[] = {
    { L"Headset resolution", L"Sharpness in the headset, per eye. Lower it if the game stutters; above Ultra needs a strong GPU.",
      "VR", 9, { L"Potato (50%)", L"Low (70%)", L"Medium (85%)", L"Native (100%)", L"High (120%)", L"Ultra (140%)",
                 L"Eagle Eye (160%)", L"Sniper Vision (180%)", L"GPU Furnace (200%)" },
      { "RenderScale=0.5", "RenderScale=0.7", "RenderScale=0.85", "RenderScale=1", "RenderScale=1.2", "RenderScale=1.4",
        "RenderScale=1.6", "RenderScale=1.8", "RenderScale=2" }, 3, 0, 1 },
    { L"Antialiasing", L"Smooths jagged edges and the shimmer of fences and wires. 4x suits most PCs; 8x is heavier. (Off by default in Call of Duty 2 VR until it is tested.)",
      "VR", 4, { L"Off", L"2x MSAA", L"4x MSAA", L"8x MSAA" }, { "MSAA=1", "MSAA=2", "MSAA=4", "MSAA=8" }, 0 },
    { L"Texture filtering", L"Keeps floors and walls sharp when seen at an angle. Almost free on any recent GPU.",
      "Launcher", 4, { L"Off", L"4x anisotropic", L"8x anisotropic", L"16x anisotropic" },
      { "Anisotropy=1", "Anisotropy=4", "Anisotropy=8", "Anisotropy=16" }, 3 },
    { L"Texture quality", L"Detail of the game's textures. Maximum suits any recent PC.",
      "Launcher", 3, { L"Maximum", L"High", L"Medium" }, { "Picmip=0", "Picmip=1", "Picmip=2" }, 0 },
    // not a setting of cod2_vr.ini: the font pack in the game folder (FontPack functions); the click is handled by Click
    { L"Text font", L"Modern: the game's text in a cleaner font (Segoe UI, made on this PC) that is easier to read in the headset. Windows may ask for administrator rights.",
      "Launcher", 2, { L"Original", L"Modern" }, { "", "" }, 0 },
    { L"Brightness", L"Makes the game brighter in the headset (applied when the game starts).",
      "Launcher", 4, { L"Normal", L"Brighter", L"Bright", L"Brightest" },
      { "Gamma=1", "Gamma=1.2", "Gamma=1.4", "Gamma=1.6" }, 0 },
    // post-processing, in its own section from here (GFX_POST_FIRST)
    { L"Ambient occlusion", L"Soft contact shadows where surfaces meet: corners, under objects, around crates.",
      "PostFX", 4, { L"Off", L"Low", L"Medium", L"High" },
      { "AmbientOcclusion=0", "AmbientOcclusion=0.4", "AmbientOcclusion=0.65", "AmbientOcclusion=0.9" }, 0, 0, 0, 0, NULL, SOON_LATER },
    { L"Sharpening", L"Gives the old textures their definition back, without halos.",
      "PostFX", 4, { L"Off", L"Low", L"Medium", L"High" }, { "Sharpen=0", "Sharpen=0.3", "Sharpen=0.6", "Sharpen=1" }, 0 },
    { L"Color style", L"Original: the game's own colours. Cinema: muted and warm, like a war film.",
      "PostFX", 4, { L"Original", L"Natural", L"Cinema", L"Vivid" },
      { "Color=original", "Color=natural", "Color=cinema", "Color=vivid" }, 0 },
    { L"Bloom", L"A soft glow around bright light: fire, explosions, the sky.",
      "PostFX", 3, { L"Off", L"Low", L"Medium" }, { "Bloom=0", "Bloom=0.15", "Bloom=0.3" }, 0 },
};
#define GFX_POST_FIRST 6
#define GFX_FONT       4   // the Text font row

#define NUM_VR  (int)(sizeof(g_vrOpts) / sizeof(g_vrOpts[0]))
#define NUM_GFX (int)(sizeof(g_gfxOpts) / sizeof(g_gfxOpts[0]))

// MODS: things the original game does not have
static const wchar_t *const g_difficultyHints[] = {
    L"The game's own difficulty (chosen when you start a new game).",
    L"Real life: one shot, one kill. For you and for them.",
    L"Perma death: if you die, the run is over. The checkpoints are erased and you start a new game (your own saves go to a backup folder).",
    L"Real life with perma death: one shot kills, and dying ends the run.",
    L"God mode: you never die.",
};
static option_t g_modOpts[] = {
    { L"Custom difficulty", L"A difficulty the original game does not have.", "Mods", 5, { L"Normal", L"Real life", L"Perma death", L"Real life + perma death", L"God mode" },
      { "Difficulty=0", "Difficulty=1", "Difficulty=2", "Difficulty=3", "Difficulty=4" }, 0, 0, 0, 0, g_difficultyHints, SOON_LATER },
    { L"Blood", L"Off: no blood. On: the game's own blood. Gore: much more blood, spraying and falling from every hit.",
      "Mods", 3, { L"Off", L"On", L"Gore" }, { "Gore=0", "Gore=1", "Gore=2" }, 1, 0, 0, 0, NULL, SOON_LATER },
    { L"Infinite ammo", L"Full ammo: the reserve stays full (you still reload), or the clip too (no reloading).",
      "Mods", 3, { L"Off", L"Full reserve", L"No reloading" }, { "InfiniteAmmo=0", "InfiniteAmmo=1", "InfiniteAmmo=2" }, 0, 0, 0, 0, NULL, SOON_LATER },
};
#define NUM_MOD (int)(sizeof(g_modOpts) / sizeof(g_modOpts[0]))
#define MOD_DIFFICULTY 0

static char g_dir[MAX_PATH], g_gameIni[MAX_PATH], g_userIni[MAX_PATH];
static int  g_haveGame, g_haveMod;

// The value of a key as the mod will see it: settings.ini, else cod2_vr.ini, else "".
static void ReadSetting(const char *sec, const char *key, char *out, int size)
{
    char v[128];
    GetPrivateProfileStringA(sec, key, "", v, sizeof(v), g_gameIni);
    GetPrivateProfileStringA(sec, key, v, out, size, g_userIni);
}

static int SameValue(const char *a, const char *b)
{
    char *ea, *eb;
    double x = strtod(a, &ea), y = strtod(b, &eb);
    if (*a && *b && !*ea && !*eb) return x - y < 1e-4 && y - x < 1e-4;
    return !_stricmp(a, b);
}

// Does every key=value of a choice hold?
static int ChoiceMatches(const char *sec, const char *pairs, int isDefault)
{
    char buf[256], *tok, *ctx = NULL;
    strncpy(buf, pairs, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    for (tok = strtok_s(buf, "|", &ctx); tok; tok = strtok_s(NULL, "|", &ctx)) {
        char *eq = strchr(tok, '='), cur[128];
        if (!eq) continue;
        *eq = 0;
        ReadSetting(sec, tok, cur, sizeof(cur));
        if (!cur[0]) { if (!isDefault) return 0; continue; }   // unset: the default choice holds
        if (!SameValue(cur, eq + 1)) return 0;
    }
    return 1;
}

static void LoadOption(option_t *o)
{
    int i;
    o->cur = -1;
    for (i = 0; i < o->count; i++)
        if (ChoiceMatches(o->section, o->values[i], i == o->def)) { o->cur = i; break; }
}

static void SaveOption(option_t *o)
{
    char buf[256], *tok, *ctx = NULL, dir[MAX_PATH], *slash;
    if (o->cur < 0) return;
    strcpy(dir, g_userIni);
    slash = strrchr(dir, '\\');
    if (slash) { *slash = 0; CreateDirectoryA(dir, NULL); }
    strncpy(buf, o->values[o->cur], sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    for (tok = strtok_s(buf, "|", &ctx); tok; tok = strtok_s(NULL, "|", &ctx)) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        WritePrivateProfileStringA(o->section, tok, eq + 1, g_userIni);
    }
}

// CONTROLS (as the Call of Duty 1 VR mod's): what each body zone holds and what each controller button does, as
// drop-down lists. The mod reads [Holsters] (zone = what the grip does there) and [Buttons] (button = game command,
// "" = nothing) of settings.ini; one never changed keeps its default, the one the mod uses when the key is missing.
typedef struct { const wchar_t *name; const char *val; } choice_t;

static const choice_t g_holsterActs[] = {
    { L"Nothing", "none" },
    { L"Weapon 1", "primary" },
    { L"Weapon 2", "primaryb" },
    { L"Switch Weapon 1 & 2", "switch" },
    { L"Grenade", "grenade" },
    { L"Smoke grenade", "smokegrenade" },
    { L"Binoculars", "binocular" },
};
#define NUM_HOLSTER_ACTS (int)(sizeof(g_holsterActs) / sizeof(g_holsterActs[0]))

static const choice_t g_actions[] = {
    { L"Nothing", "" },
    { L"Fire", "+attack" },
    { L"Jump", "+gostand" },
    { L"Reload", "+reload" },
    { L"Melee", "+melee" },
    { L"Use / pick up", "+activate" },
    { L"Objectives", "+scores" },
    { L"Aim down sights", "+speed" },
    { L"Crouch (hold)", "+movedown" },
    { L"Crouch (toggle)", "togglecrouch" },
    { L"Go prone", "toggleprone" },
    { L"Lean left", "+leanleft" },
    { L"Lean right", "+leanright" },
    { L"Next weapon", "weapnext" },
    { L"Previous weapon", "weapprev" },
    { L"Raise stance", "raisestance" },
    { L"Lower stance", "lowerstance" },
    { L"Grenade", "+frag" },
    { L"Smoke grenade", "+smoke" },
    { L"Binoculars", "+binoculars" },
    { L"Recenter", "vr:recenter" },
    { L"Switch weapon 1 & 2", "vr:switchweapon" },     // without reaching for the shoulders
    { L"Comfort vignette (next)", "vr:vignette" },      // Off, Low, Medium, High, Off...
    { L"Menu", "key:ESCAPE" },          // the game's menu, for controllers without a menu button
    { L"Turn left", "vr:turnleft" },    // only for the free stick (the lists of the buttons stop before these)
    { L"Turn right", "vr:turnright" },
};
#define NUM_STK_ACTS (int)(sizeof(g_actions) / sizeof(g_actions[0]))   // the free stick's directions
#define NUM_BTN_ACTS (NUM_STK_ACTS - 2)                                // a button's

static const choice_t g_handActs[] = {
    { L"Right", "1" },
    { L"Left", "0" },
};

// The stick that moves you; the other one is free: each of its four directions gets an action.
static const choice_t g_stickActs[] = {
    { L"Left stick", "0" },
    { L"Right stick", "1" },
};

#define NUM_HOLSTERS 7
#define CTL_HAND     NUM_HOLSTERS       // the dominant hand
#define CTL_BUTTONS  (CTL_HAND + 1)     // then A, B, X, Y and the stick clicks (the page shows the two of the dominant hand)
#define NUM_BUTTONS  6
#define NUM_SHORTCUTS 12 // then the shortcuts: X + A, X + B, X + Y, Y + A, Y + B, Y + X (right-handed), A + X, A + Y, A + B, B + X, B + Y, B + A (left-handed)
#define CTL_SHORTCUT (CTL_BUTTONS + NUM_BUTTONS)
#define CTL_MOVE     (CTL_SHORTCUT + NUM_SHORTCUTS)   // then the move stick and the free stick's up, down, left, right
// the body zones are the Call of Duty 1 VR mod's; the waist's left holds the smoke grenades by default (Call of Duty 2 has them)
static struct { const wchar_t *label; const char *section, *key, *def; const choice_t *ch; int n; int cur; int soon; } g_ctl[] = {
    { L"Left shoulder", "Holsters", "LeftShoulder", "primaryb", g_holsterActs, NUM_HOLSTER_ACTS, 0 },
    { L"Right shoulder", "Holsters", "RightShoulder", "primary", g_holsterActs, NUM_HOLSTER_ACTS, 0 },
    { L"Chest", "Holsters", "Chest", "binocular", g_holsterActs, NUM_HOLSTER_ACTS, 0 },
    { L"Waist, right", "Holsters", "WaistRight", "none", g_holsterActs, NUM_HOLSTER_ACTS, 0 },
    { L"Waist, left", "Holsters", "WaistLeft", "smokegrenade", g_holsterActs, NUM_HOLSTER_ACTS, 0 },
    { L"Waist, front", "Holsters", "WaistFront", "none", g_holsterActs, NUM_HOLSTER_ACTS, 0 },
    { L"Waist, back", "Holsters", "LowerBack", "grenade", g_holsterActs, NUM_HOLSTER_ACTS, 0 },
    { L"Dominant hand", "Controllers", "RightHanded", "1", g_handActs, 2, 0 },
    { L"A", "Buttons", "A", "+gostand", g_actions, NUM_BTN_ACTS, 0 },
    { L"B", "Buttons", "B", "+reload", g_actions, NUM_BTN_ACTS, 0 },
    { L"X", "Buttons", "X", "+frag", g_actions, NUM_BTN_ACTS, 0 },
    { L"Y", "Buttons", "Y", "weapnext", g_actions, NUM_BTN_ACTS, 0 },
    { L"Left stick click", "Buttons", "LeftStickClick", "vr:recenter", g_actions, NUM_BTN_ACTS, 0 },
    { L"Right stick click", "Buttons", "RightStickClick", "toggleprone", g_actions, NUM_BTN_ACTS, 0 },
    { L"X + A", "Buttons", "ShortcutXA", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"X + B", "Buttons", "ShortcutXB", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"X + Y", "Buttons", "ShortcutXY", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"Y + A", "Buttons", "ShortcutYA", "vr:switchweapon", g_actions, NUM_BTN_ACTS, 0 },
    { L"Y + B", "Buttons", "ShortcutYB", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"Y + X", "Buttons", "ShortcutYX", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"A + X", "Buttons", "ShortcutAX", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"A + Y", "Buttons", "ShortcutAY", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"A + B", "Buttons", "ShortcutAB", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"B + X", "Buttons", "ShortcutBX", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"B + Y", "Buttons", "ShortcutBY", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"B + A", "Buttons", "ShortcutBA", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"Move with", "Controllers", "MoveStick", "0", g_stickActs, 2, 0 },
    { L"Right stick up", "Buttons", "TurnStickUp", "raisestance", g_actions, NUM_STK_ACTS, 0 },      // the labels follow the move stick (DrawControls)
    { L"Right stick down", "Buttons", "TurnStickDown", "lowerstance", g_actions, NUM_STK_ACTS, 0 },
    { L"Right stick left", "Buttons", "TurnStickLeft", "vr:turnleft", g_actions, NUM_STK_ACTS, 0 },
    { L"Right stick right", "Buttons", "TurnStickRight", "vr:turnright", g_actions, NUM_STK_ACTS, 0 },
};
#define NUM_CTL (int)(sizeof(g_ctl) / sizeof(g_ctl[0]))
#define CTL_ARG 100   // hit argument of the first drop-down (the option pages use small ones)

// The defaults follow the dominant hand: the table above holds the right-handed ones; with the left hand chosen the weapons
// are on the opposite shoulders, the left stick turns, the face buttons and stick clicks swap controllers. The profile
// called Default means these, whichever hand is chosen.
static int g_leftHand;   // the dominant hand of the profile in use: 1 = left

static const char *CtlDefFor(int i, int left)
{
    static const char *const mirrored[][2] = {
        { "LeftShoulder", "primary" }, { "RightShoulder", "primaryb" },
        { "MoveStick", "1" }, { "A", "+frag" }, { "B", "weapnext" }, { "X", "+gostand" }, { "Y", "+reload" },
        { "LeftStickClick", "toggleprone" }, { "RightStickClick", "vr:recenter" },
        { "ShortcutYA", "" }, { "ShortcutBX", "vr:switchweapon" },
    };
    int k;
    if (left)
        for (k = 0; k < (int)(sizeof(mirrored) / sizeof(mirrored[0])); k++)
            if (!strcmp(mirrored[k][0], g_ctl[i].key)) return mirrored[k][1];
    return g_ctl[i].def;
}
static const char *CtlDef(int i) { return CtlDefFor(i, g_leftHand); }

static void LoadControls(void)
{
    int i, a;
    {
        char hand[8];
        GetPrivateProfileStringA("Controllers", "RightHanded", "1", hand, sizeof(hand), g_gameIni);
        GetPrivateProfileStringA("Controllers", "RightHanded", hand, hand, sizeof(hand), g_userIni);
        g_leftHand = !strcmp(hand, "0");
    }
    for (i = 0; i < NUM_CTL; i++) {
        char val[64], v[64];
        GetPrivateProfileStringA(g_ctl[i].section, g_ctl[i].key, CtlDef(i), v, sizeof(v), g_gameIni);
        GetPrivateProfileStringA(g_ctl[i].section, g_ctl[i].key, v, val, sizeof(val), g_userIni);
        // MoveStick -1 (cod2_vr.ini's default) is the stick of the hand without the gun: the hand's default here
        if (i == CTL_MOVE && !strcmp(val, "-1")) strcpy(val, CtlDef(i));
        g_ctl[i].cur = -1;   // something typed into the ini that is not in the list: Custom
        for (a = 0; a < g_ctl[i].n; a++)
            if (!_stricmp(val, g_ctl[i].ch[a].val)) { g_ctl[i].cur = a; break; }
    }
}

static void EnsureUserDir(void)
{
    char dir[MAX_PATH], *slash;
    strcpy(dir, g_userIni);
    slash = strrchr(dir, 0x5c);
    if (slash) { *slash = 0; CreateDirectoryA(dir, NULL); }
}

// Control profiles. The mod reads the active choices from [Holsters], [Buttons] and [Controllers]; each profile
// keeps its own copy in a [Profile.<name>] section of the same file (the launcher's settings.ini:
// the game folder is not writable without administrator rights). The Default profile is the first one.
#define MAX_PROFILES 32
static char g_profNames[MAX_PROFILES][32] = { "Default" };
static int  g_numProfiles = 1, g_profile;   // g_profile 0 = Default

static void ProfileSection(int i, char *out, int size)
{
    _snprintf(out, size, "Profile.%s", g_profNames[i]);
    out[size - 1] = 0;
}

static void LoadProfileList(void)
{
    char all[4096] = "", cur[64], *q;
    int i;
    g_numProfiles = 1;
    GetPrivateProfileSectionNamesA(all, sizeof(all), g_userIni);
    for (q = all; *q && g_numProfiles < MAX_PROFILES; q += strlen(q) + 1)
        if (!_strnicmp(q, "Profile.", 8) && q[8] && strlen(q + 8) < sizeof(g_profNames[0]) && _stricmp(q + 8, "Default"))
            strcpy(g_profNames[g_numProfiles++], q + 8);   // (Default is the first one, kept in [Profile.Default] like the others)
    GetPrivateProfileStringA("Launcher", "Profile", "Default", cur, sizeof(cur), g_userIni);
    g_profile = 0;
    for (i = 1; i < g_numProfiles; i++) if (!_stricmp(cur, g_profNames[i])) g_profile = i;
}

static void SaveControl(int i)
{
    if (g_ctl[i].cur < 0 || g_ctl[i].soon) return;
    EnsureUserDir();
    WritePrivateProfileStringA(g_ctl[i].section, g_ctl[i].key, g_ctl[i].ch[g_ctl[i].cur].val, g_userIni);
    {
        char sec[64];
        ProfileSection(g_profile, sec, sizeof(sec));
        WritePrivateProfileStringA(sec, g_ctl[i].key, g_ctl[i].ch[g_ctl[i].cur].val, g_userIni);
    }
}

// The active profile's choices become the ones the mod reads. A profile that never held the dominant hand (the
// Default one, the first time) starts with the one in use, so a hand chosen before the profiles is kept.
static void ApplyProfile(void)
{
    char sec[64], val[64];
    int i;
    EnsureUserDir();
    ProfileSection(g_profile, sec, sizeof(sec));
    GetPrivateProfileStringA(sec, "RightHanded", "", val, sizeof(val), g_userIni);
    if (!val[0]) {
        char hand[8];
        GetPrivateProfileStringA("Controllers", "RightHanded", "1", hand, sizeof(hand), g_gameIni);
        GetPrivateProfileStringA("Controllers", "RightHanded", hand, val, sizeof(val), g_userIni);
        WritePrivateProfileStringA(sec, "RightHanded", strcmp(val, "0") ? "1" : "0", g_userIni);
    }
    g_leftHand = !strcmp(val, "0");
    for (i = 0; i < NUM_CTL; i++) {
        if (g_ctl[i].soon) continue;
        GetPrivateProfileStringA(sec, g_ctl[i].key, CtlDef(i), val, sizeof(val), g_userIni);   // what was never changed is the default (of the hand)
        WritePrivateProfileStringA(g_ctl[i].section, g_ctl[i].key, val, g_userIni);
    }
    WritePrivateProfileStringA("Launcher", "Profile", g_profNames[g_profile], g_userIni);
    LoadControls();
}

// Does choice a of control i hold the default?
static int IsDefault(int i, int a)
{
    return a >= 0 && !_stricmp(g_ctl[i].ch[a].val, CtlDef(i));
}

// ---------------------------------------------------------------------------
// Player hands (as the Call of Duty 1 VR launcher's). The game's first-person arms (xmodel/viewmodel_hands*) are drawn
// with the gun. The pack main\zz_cod2vr.iwd gives each of those models an invisible material, so only the gun shows
// (Hidden); without it the arms are drawn (Shown). It is made here from the player's own game files, the same as
// tools\build_iwd.py: the arms' xmodels with every material name replaced by mtl_vr_invisible, that material (a copy of
// the game's phong_alphatest material hill400_treeline01 with its colour map changed) and a 4x4 transparent DXT1 image.
// Writing into the game folder usually needs administrator rights: the launcher then runs itself again elevated
// (/handspack hide|show).
// ---------------------------------------------------------------------------
#define HANDS_PACK_FILE "\\main\\zz_cod2vr.iwd"
#define INVISIBLE_MTL   "mtl_vr_invisible"
#define INVISIBLE_IMG   "vr_invisible"

typedef struct { char name[128]; void *data; size_t size; } pak_entry_t;
static pak_entry_t g_packFiles[64];
static int g_numPackFiles;

static int CmpNames(const void *a, const void *b) { return _stricmp((const char *)a, (const char *)b); }

static void KeepPackFile(const char *name, void *data, size_t size)
{
    int i;
    for (i = 0; i < g_numPackFiles; i++)
        if (!_stricmp(g_packFiles[i].name, name)) { free(g_packFiles[i].data); break; }   // a later pak wins
    if (i == g_numPackFiles) {
        if (g_numPackFiles == 64) { free(data); return; }
        g_numPackFiles++;
    }
    strncpy(g_packFiles[i].name, name, sizeof(g_packFiles[i].name) - 1);
    g_packFiles[i].name[sizeof(g_packFiles[i].name) - 1] = 0;
    g_packFiles[i].data = data;
    g_packFiles[i].size = size;
}

// xmodel: ... ff ff ff ff 00 00 00 00, u16 material count, NUL-terminated names, rest (kept).
static void *InvisibleXModel(const BYTE *d, size_t n, size_t *outSize)
{
    static const BYTE marker[8] = { 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0 };
    size_t at = (size_t)-1, i, pos, k, len = strlen(INVISIBLE_MTL) + 1;
    unsigned count;
    BYTE *out, *w;
    for (i = 0; i + 8 <= n; i++) if (!memcmp(d + i, marker, 8)) { at = i; break; }
    if (at == (size_t)-1 || at + 10 > n) return NULL;
    pos = at + 8;
    count = d[pos] | (d[pos + 1] << 8);
    k = pos + 2;
    for (i = 0; i < count; i++) {
        while (k < n && d[k]) k++;
        if (k >= n) return NULL;
        k++;
    }
    *outSize = pos + 2 + count * len + (n - k);
    out = w = malloc(*outSize);
    if (!out) return NULL;
    memcpy(w, d, pos + 2);
    w += pos + 2;
    for (i = 0; i < count; i++) { memcpy(w, INVISIBLE_MTL, len); w += len; }
    memcpy(w, d + k, n - k);
    return out;
}

static void *RenameMaterial(const BYTE *t, size_t n, const char *name, const char *image, size_t *outSize);

static int HandsPackExists(void)
{
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "%s" HANDS_PACK_FILE, g_dir);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

// Makes the pack from the game's own iw_*.iwd (the same as tools\build_iwd.py). 1 when it is there.
static int BuildHandsPack(void)
{
    char pattern[MAX_PATH], (*paks)[MAX_PATH] = NULL, out[MAX_PATH], tmp[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int count = 0, cap = 0, i, ok, models = 0, tmpl = -1;
    mz_zip_archive zw;
    // IWi v5, DXT1, no mipmaps, 4x4: colour0 = colour1 = 0 (3-colour mode), every index 3 = transparent
    static const BYTE iwi[36] = { 'I', 'W', 'i', 5, 0x0b, 0x03, 4, 0, 4, 0, 1, 0, 36, 0, 0, 0, 36, 0, 0, 0, 36, 0, 0, 0, 36, 0, 0, 0,
                                  0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff };

    _snprintf(pattern, sizeof(pattern), "%s\\main\\iw_*.iwd", g_dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        void *p;
        if (count == cap) {
            cap = cap ? cap * 2 : 32;
            p = realloc(paks, cap * sizeof(*paks));
            if (!p) { free(paks); FindClose(h); return 0; }
            paks = p;
        }
        _snprintf(paks[count], MAX_PATH, "%s", fd.cFileName);
        count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (count) qsort(paks, count, sizeof(*paks), CmpNames);   // the engine's order: later packs override

    g_numPackFiles = 0;
    for (i = 0; i < count; i++) {
        mz_zip_archive z;
        mz_uint e;
        memset(&z, 0, sizeof(z));
        _snprintf(path, sizeof(path), "%s\\main\\%s", g_dir, paks[i]);
        if (!mz_zip_reader_init_file(&z, path, 0)) continue;
        for (e = 0; e < mz_zip_reader_get_num_files(&z); e++) {
            char name[256];
            mz_zip_reader_get_filename(&z, e, name, sizeof(name));
            if (!_strnicmp(name, "xmodel/viewmodel_hands", 22) || !_stricmp(name, "materials/hill400_treeline01")) {
                size_t size;
                void *data = mz_zip_reader_extract_to_heap(&z, e, &size, 0);
                if (data) KeepPackFile(name, data, size);
            }
        }
        mz_zip_reader_end(&z);
    }
    free(paks);
    for (i = 0; i < g_numPackFiles; i++) if (!_stricmp(g_packFiles[i].name, "materials/hill400_treeline01")) tmpl = i;
    {
        // the template must be the expected kind of material: its technique name is among its strings
        int found = 0;
        if (tmpl >= 0 && g_numPackFiles >= 2) {
            size_t k, L = strlen("phong_alphatest");
            const BYTE *d = g_packFiles[tmpl].data;
            for (k = 0; k + L <= g_packFiles[tmpl].size; k++) if (!memcmp(d + k, "phong_alphatest", L)) { found = 1; break; }
        }
        if (!found) {
            for (i = 0; i < g_numPackFiles; i++) free(g_packFiles[i].data);
            g_numPackFiles = 0;
            return 0;
        }
    }

    _snprintf(out, sizeof(out), "%s" HANDS_PACK_FILE, g_dir);
    _snprintf(tmp, sizeof(tmp), "%s.tmp", out);
    memset(&zw, 0, sizeof(zw));
    if (!mz_zip_writer_init_file(&zw, tmp, 0)) {
        for (i = 0; i < g_numPackFiles; i++) free(g_packFiles[i].data);
        return 0;
    }
    {
        size_t size;
        void *m = RenameMaterial(g_packFiles[tmpl].data, g_packFiles[tmpl].size, INVISIBLE_MTL, INVISIBLE_IMG, &size);
        ok = m && mz_zip_writer_add_mem(&zw, "materials/" INVISIBLE_MTL, m, size, MZ_DEFAULT_COMPRESSION);
        free(m);
    }
    ok = ok && mz_zip_writer_add_mem(&zw, "images/" INVISIBLE_IMG ".iwi", iwi, sizeof(iwi), MZ_DEFAULT_COMPRESSION);
    for (i = 0; i < g_numPackFiles; i++) {
        size_t size;
        void *data = ok && i != tmpl ? InvisibleXModel(g_packFiles[i].data, g_packFiles[i].size, &size) : NULL;
        if (data) {
            ok = mz_zip_writer_add_mem(&zw, g_packFiles[i].name, data, size, MZ_DEFAULT_COMPRESSION);
            free(data);
            models++;
        }
        free(g_packFiles[i].data);
    }
    g_numPackFiles = 0;
    ok = ok && mz_zip_writer_finalize_archive(&zw);
    mz_zip_writer_end(&zw);
    if (!ok || !models) { DeleteFileA(tmp); return 0; }
    return MoveFileExA(tmp, out, MOVEFILE_REPLACE_EXISTING) != 0;
}

static int RemoveHandsPack(void)
{
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "%s" HANDS_PACK_FILE, g_dir);
    return DeleteFileA(path) != 0 || !HandsPackExists();
}

// Runs the launcher again as administrator with these arguments (/handspack, /fontpack); 1 when it worked (exit code 0).
static int RunElevated(HWND wnd, const char *args)
{
    char exe[MAX_PATH];
    SHELLEXECUTEINFOA sei;
    DWORD code = 1;
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = wnd;
    sei.lpVerb = "runas";
    sei.lpFile = exe;
    sei.lpParameters = args;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExA(&sei) || !sei.hProcess) return 0;
    WaitForSingleObject(sei.hProcess, 120000);
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code == 0;
}

// ---------------------------------------------------------------------------
// Text font (as the Call of Duty 1 VR mod's font pack): the game's text fonts drawn again from Segoe UI Semibold, which
// comes with Windows, on this PC, cleaner to read in the headset. main\zz_cod2vr_font.iwd holds the game's five text fonts
// (fonts/smallFont, normalFont, bigFont, extraBigFont, boldFont: a header { name offset, pixel height, glyph count,
// material offset }, then 24-byte glyphs { u16 letter, s8 x0, s8 y0 (the top, negative from the baseline), u8 advance, u8
// width, u8 height, pad, float s0 t0 s1 t1 }) with their letters on a new picture of their own (images/vr_modernfont.iwi:
// IWi 5, DXT5, 512 x 1024 with every mip, as the game's gamefonts; material fonts/vr_modernfont, a copy of
// fonts/gamefonts). The glyphs that are not letters (1 .. 31, 127 .. 159: icons) are copied from the game's own picture.
// The console font keeps the game's. Removing the pack gives the original text back.
// ---------------------------------------------------------------------------
#define FONT_PACK_FILE "\\main\\zz_cod2vr_font.iwd"
#define FONT_MTL       "fonts/vr_modernfont"
#define FONT_IMG       "vr_modernfont"
#define ATLAS_W 512
#define ATLAS_H 1024

static int FontPackExists(void)
{
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "%s" FONT_PACK_FILE, g_dir);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static int RemoveFontPack(void)
{
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "%s" FONT_PACK_FILE, g_dir);
    return DeleteFileA(path) != 0 || !FontPackExists();
}

// A material: the template's header with its strings rebuilt (name, reference image, technique, texture names; the
// colour map's image is the given one). The hands pack's invisible material is made the same way.
static void *RenameMaterial(const BYTE *t, size_t n, const char *name, const char *image, size_t *outSize)
{
    DWORD count, techOff, tableOff, strStart, k;
    BYTE *out;
    size_t used;
#define U32(o) (*(const DWORD *)(t + (o)))
    if (n < 0x44) return NULL;
    count = U32(0x34); techOff = U32(0x38); tableOff = U32(0x3c); strStart = U32(0x40);
    if (strStart > n || tableOff + count * 12 > strStart || techOff >= n || count > 32) return NULL;
    out = malloc(strStart + 4096);
    if (!out) return NULL;
    memcpy(out, t, strStart);
    used = strStart;
#define ADD(s) (memcpy(out + used, (s), strlen(s) + 1), used += strlen(s) + 1, (DWORD)(used - strlen(s) - 1))
    *(DWORD *)(out + 0x38) = ADD((const char *)t + techOff);
    *(DWORD *)(out + 0x00) = ADD(name);
    *(DWORD *)(out + 0x04) = ADD(image);
    for (k = 0; k < count; k++) {
        DWORD e = tableOff + k * 12, sem = U32(e), img = U32(e + 8);
        const char *semName = (const char *)t + sem;
        if (sem >= n || img >= n || used + 512 > strStart + 4096) { free(out); return NULL; }
        *(DWORD *)(out + e) = ADD(semName);
        *(DWORD *)(out + e + 8) = !strcmp(semName, "colorMap") ? ADD(image) : ADD((const char *)t + img);
    }
#undef ADD
#undef U32
    *outSize = used;
    return out;
}

// One DXT5 block into 4x4 RGBA.
static void DecodeDxt5(const BYTE *b, BYTE out[64])
{
    unsigned a[8], c[4][3], k;
    unsigned c0 = b[8] | (b[9] << 8), c1 = b[10] | (b[11] << 8);
    unsigned long long ai = 0;
    a[0] = b[0]; a[1] = b[1];
    for (k = 0; k < 6; k++) ai |= (unsigned long long)b[2 + k] << (8 * k);
    if (a[0] > a[1]) for (k = 1; k < 7; k++) a[k + 1] = ((7 - k) * a[0] + k * a[1]) / 7;
    else { for (k = 1; k < 5; k++) a[k + 1] = ((5 - k) * a[0] + k * a[1]) / 5; a[6] = 0; a[7] = 255; }
    c[0][0] = (c0 >> 11) * 255 / 31; c[0][1] = ((c0 >> 5) & 63) * 255 / 63; c[0][2] = (c0 & 31) * 255 / 31;
    c[1][0] = (c1 >> 11) * 255 / 31; c[1][1] = ((c1 >> 5) & 63) * 255 / 63; c[1][2] = (c1 & 31) * 255 / 31;
    for (k = 0; k < 3; k++) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; }
    for (k = 0; k < 16; k++) {
        unsigned ci = (b[12 + k / 4] >> (2 * (k % 4))) & 3;
        out[k * 4 + 0] = (BYTE)c[ci][0]; out[k * 4 + 1] = (BYTE)c[ci][1]; out[k * 4 + 2] = (BYTE)c[ci][2];
        out[k * 4 + 3] = (BYTE)a[(ai >> (3 * k)) & 7];
    }
}

// The game's font picture (images/gamefonts.iwi, IWi 5, DXT5 512 x 1024): its largest mip as RGBA, or NULL.
static BYTE *DecodeGameAtlas(const BYTE *d, size_t n)
{
    DWORD start, end;
    BYTE *rgba;
    int bx, by, x, y;
    if (n < 28 || memcmp(d, "IWi", 3) || d[3] != 5 || d[4] != 0x0d) return NULL;
    if ((d[6] | (d[7] << 8)) != ATLAS_W || (d[8] | (d[9] << 8)) != ATLAS_H) return NULL;
    end = *(const DWORD *)(d + 12);
    start = *(const DWORD *)(d + 16);
    if (end > n || end - start != ATLAS_W * ATLAS_H) return NULL;   // DXT5: one byte a pixel
    rgba = malloc(ATLAS_W * ATLAS_H * 4);
    if (!rgba) return NULL;
    for (by = 0; by < ATLAS_H / 4; by++)
        for (bx = 0; bx < ATLAS_W / 4; bx++) {
            BYTE px[64];
            DecodeDxt5(d + start + (by * (ATLAS_W / 4) + bx) * 16, px);
            for (y = 0; y < 4; y++)
                for (x = 0; x < 4; x++) memcpy(rgba + (((by * 4 + y) * ATLAS_W) + bx * 4 + x) * 4, px + (y * 4 + x) * 4, 4);
        }
    return rgba;
}

// The new picture as IWi 5 (DXT5, every mip down to 1x1, the smallest first, as the game's).
static BYTE *EncodeAtlas(const BYTE *rgba, size_t *outSize)
{
    int levels = 0, w = ATLAS_W, h = ATLAS_H, L, total = 28;
    BYTE *mips[12], *out;
    int mw[12], mh[12], bytes[12];
    DWORD ends[12];
    while (1) {
        mw[levels] = w; mh[levels] = h;
        bytes[levels] = ((w + 3) / 4) * ((h + 3) / 4) * 16;
        levels++;
        if (w == 1 && h == 1) break;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    mips[0] = (BYTE *)rgba;
    for (L = 1; L < levels; L++) {   // box filter
        int x, y, k;
        mips[L] = malloc(mw[L] * mh[L] * 4);
        for (y = 0; y < mh[L]; y++)
            for (x = 0; x < mw[L]; x++)
                for (k = 0; k < 4; k++) {
                    int x0 = x * 2 < mw[L - 1] ? x * 2 : mw[L - 1] - 1, x1 = x0 + 1 < mw[L - 1] ? x0 + 1 : x0;
                    int y0 = y * 2 < mh[L - 1] ? y * 2 : mh[L - 1] - 1, y1 = y0 + 1 < mh[L - 1] ? y0 + 1 : y0;
                    const BYTE *p = mips[L - 1];
                    mips[L][(y * mw[L] + x) * 4 + k] = (BYTE)((p[(y0 * mw[L - 1] + x0) * 4 + k] + p[(y0 * mw[L - 1] + x1) * 4 + k] +
                                                               p[(y1 * mw[L - 1] + x0) * 4 + k] + p[(y1 * mw[L - 1] + x1) * 4 + k] + 2) / 4);
                }
    }
    for (L = 0; L < levels; L++) total += bytes[L];
    out = calloc(1, total);
    if (!out) return NULL;
    {
        int pos = 28;
        for (L = levels - 1; L >= 0; L--) {   // smallest first
            int bx, by, x, y;
            for (by = 0; by < (mh[L] + 3) / 4; by++)
                for (bx = 0; bx < (mw[L] + 3) / 4; bx++) {
                    BYTE block[64];
                    for (y = 0; y < 4; y++)
                        for (x = 0; x < 4; x++) {
                            int sx = bx * 4 + x < mw[L] ? bx * 4 + x : mw[L] - 1, sy = by * 4 + y < mh[L] ? by * 4 + y : mh[L] - 1;
                            memcpy(block + (y * 4 + x) * 4, mips[L] + (sy * mw[L] + sx) * 4, 4);
                        }
                    stb_compress_dxt_block(out + pos + (by * ((mw[L] + 3) / 4) + bx) * 16, block, 1, STB_DXT_HIGHQUAL);
                }
            pos += bytes[L];
            ends[L] = pos;
        }
    }
    memcpy(out, "IWi", 3);
    out[3] = 5; out[4] = 0x0d; out[5] = 0x01;
    out[6] = ATLAS_W & 0xff; out[7] = ATLAS_W >> 8; out[8] = ATLAS_H & 0xff; out[9] = ATLAS_H >> 8; out[10] = 1; out[11] = 0;
    for (L = 0; L < 4; L++) *(DWORD *)(out + 12 + L * 4) = ends[L];   // where mips 0 .. 3 end
    for (L = 1; L < levels; L++) free(mips[L]);
    *outSize = total;
    return out;
}

typedef struct { WORD letter; signed char x0, y0; BYTE dx, w, h, pad; float s0, t0, s1, t1; } fontGlyph_t;

// One font: its letters drawn with GDI into the picture at (*px, *py) (a shelf packer), the rest copied from the game's
// picture. Returns the new font file, or NULL when the picture is full.
static BYTE *BuildFont(const BYTE *src, size_t n, const BYTE *game, BYTE *atlas, int *px, int *py, int *rowH, size_t *outSize)
{
    DWORD nameOff, height, count, mtlOff, k;
    BYTE *out, *buf = malloc(256 * 256);
    fontGlyph_t *g;
    HDC dc;
    HFONT font;
    static const MAT2 ident = { { 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 } };
    size_t len;
    if (n < 16 || !buf) { free(buf); return NULL; }
    nameOff = *(const DWORD *)src; height = *(const DWORD *)(src + 4); count = *(const DWORD *)(src + 8); mtlOff = *(const DWORD *)(src + 12);
    if (16 + count * 24 > n || nameOff >= n || height < 4 || height > 64) { free(buf); return NULL; }
    len = 16 + count * 24 + strlen((const char *)src + nameOff) + 1 + strlen(FONT_MTL) + 1;
    out = calloc(1, len);
    if (!out) { free(buf); return NULL; }
    memcpy(out, src, 16 + count * 24);
    g = (fontGlyph_t *)(out + 16);
    dc = CreateCompatibleDC(NULL);
    font = CreateFontW(-(int)height, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    SelectObject(dc, font);
    for (k = 0; k < count; k++) {
        fontGlyph_t *q = &g[k];
        int c = q->letter, w, h, x, y, own = (c >= 32 && c < 127) || (c >= 160 && c < 256);
        GLYPHMETRICS gm;
        DWORD got = GDI_ERROR;
        int sx0 = 0, sy0 = 0;
        if (own) {
            WCHAR wc;
            char ch = (char)c;
            MultiByteToWideChar(1252, 0, &ch, 1, &wc, 1);   // the game's text is Windows-1252
            got = GetGlyphOutlineW(dc, wc, GGO_GRAY8_BITMAP, &gm, 256 * 256, buf, &ident);
            if (got == GDI_ERROR && GetGlyphOutlineW(dc, wc, GGO_METRICS, &gm, 0, NULL, &ident) == GDI_ERROR) own = 0;
        }
        if (own) {
            w = got != GDI_ERROR && got ? (int)gm.gmBlackBoxX : 0;
            h = got != GDI_ERROR && got ? (int)gm.gmBlackBoxY : 0;
        } else {   // the game's own glyph, its pixels from the game's picture
            sx0 = (int)(q->s0 * ATLAS_W + 0.5f); sy0 = (int)(q->t0 * ATLAS_H + 0.5f);
            w = (int)(q->s1 * ATLAS_W + 0.5f) - sx0; h = (int)(q->t1 * ATLAS_H + 0.5f) - sy0;
            if (w < 0 || h < 0 || w > 128 || h > 128) w = h = 0;
        }
        if (*px + w + 1 > ATLAS_W) { *px = 1; *py += *rowH + 1; *rowH = 0; }
        if (*py + h + 1 > ATLAS_H) { DeleteObject(font); DeleteDC(dc); free(buf); free(out); return NULL; }
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                BYTE *d = atlas + ((*py + y) * ATLAS_W + *px + x) * 4;
                if (own) {
                    int v = buf[y * ((w + 3) & ~3) + x] * 255 / 64;
                    d[0] = d[1] = d[2] = 255;
                    d[3] = (BYTE)(v > 255 ? 255 : v);
                } else memcpy(d, game + ((sy0 + y) * ATLAS_W + sx0 + x) * 4, 4);
            }
        if (own) {
            q->x0 = (signed char)gm.gmptGlyphOrigin.x;
            q->y0 = (signed char)(w ? -gm.gmptGlyphOrigin.y : -(int)height);
            q->dx = (BYTE)gm.gmCellIncX;
            q->w = (BYTE)w;
            q->h = (BYTE)h;
        }
        q->s0 = (float)*px / ATLAS_W; q->t0 = (float)*py / ATLAS_H;
        q->s1 = (float)(*px + w) / ATLAS_W; q->t1 = (float)(*py + h) / ATLAS_H;
        *px += w + 1;
        if (h > *rowH) *rowH = h;
    }
    DeleteObject(font);
    DeleteDC(dc);
    free(buf);
    // the strings: the font's own name, then the new material
    {
        size_t p = 16 + count * 24;
        *(DWORD *)out = (DWORD)p;
        strcpy((char *)out + p, (const char *)src + nameOff);
        p += strlen((const char *)src + nameOff) + 1;
        *(DWORD *)(out + 12) = (DWORD)p;
        strcpy((char *)out + p, FONT_MTL);
    }
    (void)mtlOff;
    *outSize = len;
    return out;
}

static int BuildFontPack(void)
{
    static const char *const fonts[] = { "fonts/smallFont", "fonts/normalFont", "fonts/bigFont", "fonts/extraBigFont", "fonts/boldFont" };
    char pattern[MAX_PATH], (*paks)[MAX_PATH] = NULL, out[MAX_PATH], tmp[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int count = 0, cap = 0, i, ok = 1, px = 1, py = 1, rowH = 0, f;
    BYTE *game = NULL, *atlas = NULL;
    mz_zip_archive zw;
    void *file[8] = { 0 };
    size_t size[8] = { 0 };
    // the files wanted from the game's packs (the last pack with each wins): the 5 fonts, gamefonts.iwi, its material
    static const char *const wanted[] = { "fonts/smallFont", "fonts/normalFont", "fonts/bigFont", "fonts/extraBigFont", "fonts/boldFont",
                                          "images/gamefonts.iwi", "materials/fonts/gamefonts" };
    _snprintf(pattern, sizeof(pattern), "%s\\main\\iw_*.iwd", g_dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        void *p;
        if (count == cap) {
            cap = cap ? cap * 2 : 32;
            p = realloc(paks, cap * sizeof(*paks));
            if (!p) { free(paks); FindClose(h); return 0; }
            paks = p;
        }
        _snprintf(paks[count], MAX_PATH, "%s", fd.cFileName);
        count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (count) qsort(paks, count, sizeof(*paks), CmpNames);
    for (i = 0; i < count; i++) {
        mz_zip_archive z;
        memset(&z, 0, sizeof(z));
        _snprintf(path, sizeof(path), "%s\\main\\%s", g_dir, paks[i]);
        if (!mz_zip_reader_init_file(&z, path, 0)) continue;
        for (f = 0; f < 7; f++) {
            int e = mz_zip_reader_locate_file(&z, wanted[f], NULL, 0);
            if (e >= 0) {
                size_t sz = 0;
                void *d = mz_zip_reader_extract_to_heap(&z, e, &sz, 0);
                if (d) { free(file[f]); file[f] = d; size[f] = sz; }
            }
        }
        mz_zip_reader_end(&z);
    }
    free(paks);
    for (f = 0; f < 7; f++) if (!file[f]) ok = 0;
    if (ok) game = DecodeGameAtlas(file[5], size[5]);
    if (!game) ok = 0;
    if (ok) {
        atlas = malloc(ATLAS_W * ATLAS_H * 4);
        if (!atlas) ok = 0;
        else for (i = 0; i < ATLAS_W * ATLAS_H; i++) { atlas[i * 4] = atlas[i * 4 + 1] = atlas[i * 4 + 2] = 255; atlas[i * 4 + 3] = 0; }
    }
    _snprintf(out, sizeof(out), "%s" FONT_PACK_FILE, g_dir);
    _snprintf(tmp, sizeof(tmp), "%s.tmp", out);
    memset(&zw, 0, sizeof(zw));
    if (ok && !mz_zip_writer_init_file(&zw, tmp, 0)) ok = 0;
    else if (ok) {
        for (f = 0; ok && f < 5; f++) {
            size_t len;
            BYTE *nf = BuildFont(file[f], size[f], game, atlas, &px, &py, &rowH, &len);
            ok = nf && mz_zip_writer_add_mem(&zw, fonts[f], nf, len, MZ_DEFAULT_COMPRESSION);
            free(nf);
        }
        if (ok) {
            size_t len;
            BYTE *iwi = EncodeAtlas(atlas, &len);
            ok = iwi && mz_zip_writer_add_mem(&zw, "images/" FONT_IMG ".iwi", iwi, len, MZ_DEFAULT_COMPRESSION);
            free(iwi);
        }
        if (ok) {
            size_t len;
            void *m = RenameMaterial(file[6], size[6], FONT_MTL, FONT_IMG, &len);
            ok = m && mz_zip_writer_add_mem(&zw, "materials/" FONT_MTL, m, len, MZ_DEFAULT_COMPRESSION);
            free(m);
        }
        ok = ok && mz_zip_writer_finalize_archive(&zw);
        mz_zip_writer_end(&zw);
    }
    for (f = 0; f < 7; f++) free(file[f]);
    free(game);
    free(atlas);
    if (!ok) { DeleteFileA(tmp); return 0; }
    return MoveFileExA(tmp, out, MOVEFILE_REPLACE_EXISTING) != 0;
}

static void LoadAll(void)
{
    int i;
    for (i = 0; i < NUM_VR; i++) LoadOption(&g_vrOpts[i]);
    g_vrOpts[VR_HANDS].cur = HandsPackExists();   // "Player hands": Hidden when the pack is there
    for (i = 0; i < NUM_GFX; i++) LoadOption(&g_gfxOpts[i]);
    g_gfxOpts[GFX_FONT].cur = FontPackExists();     // "Text font": Modern when the pack is there
    for (i = 0; i < NUM_MOD; i++) LoadOption(&g_modOpts[i]);
    LoadProfileList();
    ApplyProfile();
}

// ---------------------------------------------------------------------------
// Starting the game
// ---------------------------------------------------------------------------
// The game started from here, while it runs (the launcher stays open to try settings).
static HANDLE g_game;

// A CoD2SP_s.exe from this folder is running (also when Steam started it). Looked up at most once a second.
static int GameProcessRunning(void)
{
    static DWORD last;
    static int running;
    PROCESSENTRY32W pe;
    HANDLE snap;
    BOOL ok;
    if (last && GetTickCount() - last < 1000) return running;
    last = GetTickCount();
    running = 0;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    pe.dwSize = sizeof(pe);
    for (ok = Process32FirstW(snap, &pe); ok && !running; ok = Process32NextW(snap, &pe)) {
        HANDLE h;
        wchar_t path[MAX_PATH], dir[MAX_PATH];
        DWORD len = MAX_PATH;
        if (_wcsicmp(pe.szExeFile, L"CoD2SP_s.exe")) continue;
        h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) { running = 1; break; }   // cannot tell where it is from: take it as ours
        if (QueryFullProcessImageNameW(h, 0, path, &len)) {
            wchar_t *slash = wcsrchr(path, L'\\');
            if (slash) *slash = 0;
            MultiByteToWideChar(CP_ACP, 0, g_dir, -1, dir, MAX_PATH);
            running = !_wcsicmp(path, dir);
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    return running;
}

static int GameRunning(void)
{
    if (g_game && WaitForSingleObject(g_game, 0) == WAIT_TIMEOUT) return 1;
    if (g_game) { CloseHandle(g_game); g_game = NULL; }
    return GameProcessRunning();
}

// The Steam version's CoD2SP_s.exe only runs when Steam starts it (it reopens itself through Steam,
// or asks for Steam when it is not there): it is started with a steam:// link, which keeps the
// command line. Any other copy (CD, other stores) is started directly.
#define STEAM_APP_ID "2630"
static int SteamCopy(void)
{
    char p[MAX_PATH], lower[MAX_PATH];
    int i;
    for (i = 0; g_dir[i] && i < MAX_PATH - 1; i++) lower[i] = (char)tolower((unsigned char)g_dir[i]);
    lower[i] = 0;
    if (strstr(lower, "\\steamapps\\")) return 1;
    _snprintf(p, sizeof(p), "%s\\steam_api.dll", g_dir);
    if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) return 1;
    _snprintf(p, sizeof(p), "%s\\steam_appid.txt", g_dir);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

static int SteamInstalled(void)
{
    HKEY k;
    if (RegOpenKeyExA(HKEY_CLASSES_ROOT, "steam\\Shell\\Open\\Command", 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    RegCloseKey(k);
    return 1;
}

// steam://run/<app>//<arguments>/, spaces as %20
static int StartThroughSteam(HWND wnd, const char *args)
{
    char url[1024];
    int n = _snprintf(url, sizeof(url), "steam://run/" STEAM_APP_ID "//");
    for (; *args && n < (int)sizeof(url) - 5; args++) {
        if (*args == ' ') { memcpy(url + n, "%20", 3); n += 3; }
        else url[n++] = *args;
    }
    url[n++] = '/';
    url[n] = 0;
    return (INT_PTR)ShellExecuteA(wnd, "open", url, NULL, NULL, SW_SHOWNORMAL) > 32;
}

static int StartGame(HWND wnd)
{
    char exe[MAX_PATH], cmd[512], args[400], picmip[16], glow[16], aniso[16], gamma[16];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    if (GameRunning()) {
        MessageBoxW(wnd, L"The game is already running. Settings changed here apply the next time it starts.",
                    L"Call of Duty 2 VR", MB_ICONINFORMATION);
        return 0;
    }
    _snprintf(exe, sizeof(exe), "%s\\CoD2SP_s.exe", g_dir);
    GetPrivateProfileStringA("Launcher", "Picmip", "0", picmip, sizeof(picmip), g_userIni);
    GetPrivateProfileStringA("Launcher", "Glow", "0", glow, sizeof(glow), g_userIni);
    GetPrivateProfileStringA("Launcher", "Anisotropy", "16", aniso, sizeof(aniso), g_userIni);
    GetPrivateProfileStringA("Launcher", "Gamma", "1", gamma, sizeof(gamma), g_userIni);
    // r_smp_backend 0: the renderer's backend runs on the game's thread, where the mod paces the
    // headset frames. r_aasamples 1: the eye images are not multisampled. Windowed: the monitor
    // shows the left eye and the headset's frame rate is not tied to the monitor's.
    _snprintf(args, sizeof(args), "+set r_smp_backend 0 +set r_aasamples 1 +set r_fullscreen 0"
              " +set r_picmip %d +set r_glow %d +set r_anisotropy %d +set r_gamma %.2f", atoi(picmip), atoi(glow) ? 1 : 0,
              atoi(aniso) < 1 ? 1 : atoi(aniso) > 16 ? 16 : atoi(aniso), atof(gamma) < 0.5 || atof(gamma) > 3 ? 1.0 : atof(gamma));
    if (SteamCopy() && SteamInstalled()) {
        if (StartThroughSteam(wnd, args)) return 1;
        MessageBoxW(wnd, L"Could not start the game through Steam. Is Steam installed and logged in?",
                    L"Call of Duty 2 VR", MB_ICONERROR);
        return 0;
    }
    _snprintf(cmd, sizeof(cmd), "\"%s\" %s", exe, args);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, g_dir, &si, &pi)) {
        MessageBoxW(wnd, L"Could not start CoD2SP_s.exe.", L"Call of Duty 2 VR", MB_ICONERROR);
        return 0;
    }
    CloseHandle(pi.hThread);
    g_game = pi.hProcess;
    return 1;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
enum { PAGE_HOME, PAGE_CONTROLS, PAGE_GRAPHICS, PAGE_VR, PAGE_MODS, PAGE_ABOUT };
enum { HIT_NONE, HIT_PLAY, HIT_NAV, HIT_PREV, HIT_NEXT, HIT_ROW, HIT_LINK, HIT_RESET, HIT_PROFILE, HIT_NEWPROF, HIT_DELPROF };

typedef struct { RECT r; int type, arg; } hit_t;
static hit_t   g_hits[256];   // a page of options has up to 3 areas each (row, left arrow, right arrow)
static int     g_numHits, g_hover = -1, g_page = PAGE_HOME;
static HBITMAP g_banner;
static int     g_bannerW, g_bannerH;
static HFONT   g_fTitle, g_fNav, g_fPlay, g_fText, g_fSmall, g_fBold;

static void AddHit(int x0, int y0, int x1, int y1, int type, int arg)
{
    hit_t *h;
    if (g_numHits >= (int)(sizeof(g_hits) / sizeof(g_hits[0]))) return;
    h = &g_hits[g_numHits++];
    SetRect(&h->r, x0, y0, x1, y1);
    h->type = type;
    h->arg = arg;
}

static int Hovered(int type, int arg)
{
    return g_hover >= 0 && g_hover < g_numHits && g_hits[g_hover].type == type && g_hits[g_hover].arg == arg;
}

static void Fill(HDC dc, int x0, int y0, int x1, int y1, COLORREF c)
{
    RECT r;
    HBRUSH b = CreateSolidBrush(c);
    SetRect(&r, x0, y0, x1, y1);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void Text(HDC dc, HFONT f, COLORREF c, int x0, int y0, int x1, int y1, const wchar_t *s, UINT flags)
{
    RECT r;
    SetRect(&r, x0, y0, x1, y1);
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, flags | DT_NOPREFIX);
}

// A dark veil over the banner for the option pages.
static void VeilRGB(HDC dc, int x0, int y0, int x1, int y1, COLORREF color, BYTE alpha)
{
    HDC src = CreateCompatibleDC(dc);
    HBITMAP one = CreateCompatibleBitmap(dc, 1, 1);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
    HGDIOBJ old = SelectObject(src, one);
    SetPixel(src, 0, 0, color);
    AlphaBlend(dc, x0, y0, x1 - x0, y1 - y0, src, 0, 0, 1, 1, bf);
    SelectObject(src, old);
    DeleteObject(one);
    DeleteDC(src);
}

static void Veil(HDC dc, int x0, int y0, int x1, int y1, BYTE alpha) { VeilRGB(dc, x0, y0, x1, y1, RGB(6, 8, 10), alpha); }

static void DrawBanner(HDC dc)
{
    Fill(dc, 0, 0, P(BASE_W), P(BAR_Y), RGB(0, 0, 0));
    if (g_banner) {
        HDC src = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(src, g_banner);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        StretchBlt(dc, 0, 0, P(BASE_W), P(BANNER_H), src, 0, 0, g_bannerW, g_bannerH, SRCCOPY);
        SelectObject(src, old);
        DeleteDC(src);
    }
}

// ---------------------------------------------------------------------------
// Update check: once at start, in the background, the latest release on GitHub. A newer one
// is shown on the home page (a link to its download page); nothing is downloaded.
// ---------------------------------------------------------------------------
#define WM_UPDATE_FOUND (WM_APP + 1)
static wchar_t g_newVersion[32];

// "v1.10.2" -> 1 10 2; a > b
static int NewerVersion(const char *a, const wchar_t *b)
{
    int x[3] = { 0 }, y[3] = { 0 }, i;
    if (*a == 'v' || *a == 'V') a++;
    if (*b == L'v' || *b == L'V') b++;
    sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
    swscanf(b, L"%d.%d.%d", &y[0], &y[1], &y[2]);
    for (i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] > y[i];
    return 0;
}

static DWORD WINAPI UpdateCheckThread(LPVOID param)
{
    HWND wnd = (HWND)param;
    HINTERNET s = NULL, c = NULL, r = NULL;
    char body[65536], *tag;
    DWORD got, total = 0;
    s = WinHttpOpen(L"CoD2-VR-Launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) WinHttpSetTimeouts(s, 5000, 5000, 5000, 5000);
    if (s) c = WinHttpConnect(s, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (c) r = WinHttpOpenRequest(c, L"GET", L"/repos/lambda-gab/cod2-vr/releases/latest", NULL,
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        while (total < sizeof(body) - 1 && WinHttpReadData(r, body + total, sizeof(body) - 1 - total, &got) && got) total += got;
    }
    body[total] = 0;
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    if ((tag = strstr(body, "\"tag_name\"")) && (tag = strchr(tag + 10, '"'))) {
        char v[32];
        int i;
        tag++;
        for (i = 0; i < 31 && tag[i] && tag[i] != '"'; i++) v[i] = tag[i];
        v[i] = 0;
        if (v[0] && NewerVersion(v, VERSION_TEXT)) {
            MultiByteToWideChar(CP_UTF8, 0, v, -1, g_newVersion, 32);
            PostMessageW(wnd, WM_UPDATE_FOUND, 0, 0);
        }
    }
    return 0;
}

int WriteDiagnostics(const char *gameDir, const wchar_t *version, char *outPath, int outLen);

// ---------------------------------------------------------------------------
// Patreon supporters: a rounded, translucent black column over the left of the home banner. The list is
// supporters.ini in the game folder (UTF-8), shown in the file's own order:
//   [Panel]       Title=Patreon Supporters
//   [Supporters]  one per line, "Tier|Name" (a "1=" before the tier is ignored); Tier = Recruit, Veteran or Master
// Masters get the most: a gold plate, the largest badge, the name in gold and "[MASTER]" under it. Veterans have a
// light plate and a bold name, Recruits a plain line. More names than fit are shown a page at a time.
// ---------------------------------------------------------------------------
enum { SUP_RECRUIT, SUP_VETERAN, SUP_MASTER, SUP_TIERS };
#define MAX_SUP 64
#define SUP_ICON 96
typedef struct { int tier; wchar_t name[48]; } supporter_t;
static supporter_t g_sup[MAX_SUP];
static int         g_numSup;
static wchar_t     g_supTitle[40] = L"Patreon Supporters";
static HBITMAP     g_supBmp[SUP_TIERS];       // each badge at its size on screen (made on first use)
static int         g_supSize[SUP_TIERS];
static int         g_supPageCount = 1;

static void LoadSupporters(void)
{
    char path[MAX_PATH];
    HANDLE f;
    DWORD got = 0;
    static char buf[65536];
    static wchar_t wide[65536];
    wchar_t *p, *line;
    int n, section = 0;
    g_numSup = 0;
    _snprintf(path, sizeof(path), "%s\\supporters.ini", g_dir);
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    if (!ReadFile(f, buf, sizeof(buf) - 1, &got, NULL)) got = 0;
    CloseHandle(f);
    buf[got] = 0;
    {
        const char *src = buf;
        if (got >= 3 && (BYTE)buf[0] == 0xEF && (BYTE)buf[1] == 0xBB && (BYTE)buf[2] == 0xBF) src += 3;
        n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, wide, 65535);
        if (!n) n = MultiByteToWideChar(CP_ACP, 0, src, -1, wide, 65535);   // not UTF-8: the Windows code page
        if (!n) return;
    }
    for (p = wide; *p; ) {
        wchar_t *end, *bar;
        line = p;
        while (*p && *p != L'\n') p++;
        end = p;
        if (*p) p++;
        while (end > line && (end[-1] == L'\r' || end[-1] == L' ' || end[-1] == L'\t')) end--;
        *end = 0;
        while (*line == L' ' || *line == L'\t') line++;
        if (!*line || *line == L';' || *line == L'#') continue;
        if (*line == L'[') {
            section = !_wcsnicmp(line, L"[Panel]", 7) ? 1 : !_wcsnicmp(line, L"[Supporters]", 12) ? 2 : 0;
            continue;
        }
        if (section == 1) {
            if (!_wcsnicmp(line, L"Title=", 6) && line[6]) { wcsncpy(g_supTitle, line + 6, 39); g_supTitle[39] = 0; }
        } else if (section == 2 && g_numSup < MAX_SUP) {
            wchar_t *eq = wcschr(line, L'=');
            int tier;
            bar = wcschr(line, L'|');
            if (!bar || !bar[1]) continue;
            if (eq && eq < bar) line = eq + 1;      // "1=Master|Name"
            bar = wcschr(line, L'|');
            while (*line == L' ') line++;
            if (!_wcsnicmp(line, L"Master", 6)) tier = SUP_MASTER;
            else if (!_wcsnicmp(line, L"Veteran", 7)) tier = SUP_VETERAN;
            else if (!_wcsnicmp(line, L"Recruit", 7)) tier = SUP_RECRUIT;
            else continue;
            bar++;
            while (*bar == L' ') bar++;
            if (!*bar) continue;
            g_sup[g_numSup].tier = tier;
            wcsncpy(g_sup[g_numSup].name, bar, 47);
            g_sup[g_numSup].name[47] = 0;
            g_numSup++;
        }
    }
}

// A badge (96x96 premultiplied BGRA from the resource) shrunk to size x size with a box filter.
static HBITMAP MakeBadge(int tier, int size)
{
    static const int ids[SUP_TIERS] = { IDR_SUP_RECRUIT, IDR_SUP_VETERAN, IDR_SUP_MASTER };
    HMODULE inst = GetModuleHandleA(NULL);
    HRSRC res = FindResourceA(inst, MAKEINTRESOURCEA(ids[tier]), (LPCSTR)RT_RCDATA);
    const BYTE *src = res && SizeofResource(inst, res) >= SUP_ICON * SUP_ICON * 4 ? (const BYTE *)LockResource(LoadResource(inst, res)) : NULL;
    BITMAPINFO bi;
    BYTE *dst = NULL;
    HBITMAP bmp;
    int x, y;
    if (!src || size < 4) return NULL;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&dst, NULL, 0);
    if (!bmp || !dst) return NULL;
    for (y = 0; y < size; y++) {
        int sy0 = y * SUP_ICON / size, sy1 = (y + 1) * SUP_ICON / size;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (x = 0; x < size; x++) {
            int sx0 = x * SUP_ICON / size, sx1 = (x + 1) * SUP_ICON / size, sx, sy, k, cnt = 0, sum[4] = { 0, 0, 0, 0 };
            if (sx1 <= sx0) sx1 = sx0 + 1;
            for (sy = sy0; sy < sy1; sy++)
                for (sx = sx0; sx < sx1; sx++) {
                    const BYTE *s = src + (sy * SUP_ICON + sx) * 4;
                    for (k = 0; k < 4; k++) sum[k] += s[k];
                    cnt++;
                }
            for (k = 0; k < 4; k++) dst[(y * size + x) * 4 + k] = (BYTE)(sum[k] / cnt);
        }
    }
    return bmp;
}

static void DrawBadge(HDC dc, int tier, int x, int y, int size)
{
    HDC mem;
    HGDIOBJ old;
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (!g_supBmp[tier] || g_supSize[tier] != size) {
        if (g_supBmp[tier]) DeleteObject(g_supBmp[tier]);
        g_supBmp[tier] = MakeBadge(tier, size);
        g_supSize[tier] = size;
    }
    if (!g_supBmp[tier]) return;
    mem = CreateCompatibleDC(dc);
    old = SelectObject(mem, g_supBmp[tier]);
    AlphaBlend(dc, x, y, size, size, mem, 0, 0, size, size, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

// A translucent plate with rounded corners (its fill blended over what is behind, then a thin frame).
static void RoundPlate(HDC dc, int x0, int y0, int x1, int y1, int radius, COLORREF fill, BYTE alpha, COLORREF edge)
{
    POINT corner[2] = { { x0, y0 }, { x1 + 1, y1 + 1 } };
    HRGN rgn;
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldPen, oldBrush;
    LPtoDP(dc, corner, 2);   // clip regions are in device units (the export draws with the viewport moved)
    rgn = CreateRoundRectRgn(corner[0].x, corner[0].y, corner[1].x, corner[1].y, radius, radius);
    SelectClipRgn(dc, rgn);
    VeilRGB(dc, x0, y0, x1, y1, fill, alpha);
    SelectClipRgn(dc, NULL);
    DeleteObject(rgn);
    oldPen = SelectObject(dc, pen);
    oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, x0, y0, x1, y1, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static int SupRowHeight(int tier) { return tier == SUP_MASTER ? 36 : tier == SUP_VETERAN ? 26 : 22; }

// exportIt: every name on one tall panel (for the game's own menu, ExportSupporterPanel); *outH gets the panel's height
static void DrawSupporters(HDC dc, int exportIt, int *outH)
{
    const int x0 = 14, y0 = 14, x1 = 244, top = y0 + 34;
    int y1 = 354, bottom;
    int i, page = 0, pages = 1, y, used = 0, first = 0, last = 0;
    if (g_numSup <= 0) return;
    if (exportIt) {
        int total = 0;
        for (i = 0; i < g_numSup; i++) total += SupRowHeight(g_sup[i].tier) + 4;
        y1 = top + total + 8;
    }
    bottom = exportIt ? 100000 : y1 - 24;
    if (outH) *outH = y1 - y0;
    // the rows that fit on the page showing now (a new page every 6 s)
    for (i = 0, used = 0; i < g_numSup; i++) {
        int h = SupRowHeight(g_sup[i].tier) + 4;
        if (used + h > bottom - top && used > 0) { pages++; used = 0; }
        used += h;
    }
    g_supPageCount = pages;
    page = pages > 1 ? (int)((GetTickCount() / 6000) % pages) : 0;
    {
        int cur = 0;
        for (i = 0, used = 0; i < g_numSup; i++) {
            int h = SupRowHeight(g_sup[i].tier) + 4;
            if (used + h > bottom - top && used > 0) { cur++; used = 0; }
            if (cur == page) { if (used == 0 && i >= 0) first = i; last = i + 1; }
            used += h;
        }
    }
    RoundPlate(dc, P(x0), P(y0), P(x1), P(y1), P(16), RGB(0, 0, 0), 175, RGB(120, 98, 44));
    Text(dc, g_fBold, COL_GOLD, P(x0 + 12), P(y0 + 5), P(x1 - 8), P(y0 + 25), g_supTitle, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    Fill(dc, P(x0 + 12), P(y0 + 27), P(x1 - 12), P(y0 + 27) + 1, RGB(120, 98, 44));
    y = top;
    for (i = first; i < last; i++) {
        const supporter_t *s = &g_sup[i];
        int h = SupRowHeight(s->tier), rx0 = x0 + 8, rx1 = x1 - 8;
        if (s->tier == SUP_MASTER) {
            // the top rank: a gold plate, the big badge, the name in gold and its rank tag
            RoundPlate(dc, P(rx0), P(y), P(rx1), P(y + h), P(11), RGB(150, 110, 20), 85, RGB(214, 170, 62));
            DrawBadge(dc, SUP_MASTER, P(rx0 + 4), P(y + 4), P(28));
            Text(dc, g_fBold, RGB(255, 214, 110), P(rx0 + 38), P(y + 3), P(rx1 - 4), P(y + 20), s->name, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            Text(dc, g_fSmall, COL_GOLD, P(rx0 + 38), P(y + 19), P(rx1 - 4), P(y + 33), L"\x2605 [MASTER] \x2605", DT_SINGLELINE | DT_VCENTER);
        } else if (s->tier == SUP_VETERAN) {
            RoundPlate(dc, P(rx0), P(y), P(rx1), P(y + h), P(9), RGB(190, 190, 205), 28, RGB(110, 110, 120));
            DrawBadge(dc, SUP_VETERAN, P(rx0 + 4), P(y + 3), P(20));
            Text(dc, g_fBold, COL_TEXT, P(rx0 + 30), P(y), P(rx1 - 4), P(y + h), s->name, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        } else {
            DrawBadge(dc, SUP_RECRUIT, P(rx0 + 5), P(y + 3), P(16));
            Text(dc, g_fText, COL_DIM, P(rx0 + 30), P(y), P(rx1 - 4), P(y + h), s->name, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
        y += h + 4;
    }
    if (pages > 1) {
        // which page: one dot each
        int d, cx = (x0 + x1) / 2 - (pages * 12) / 2 + 6;
        HBRUSH on = CreateSolidBrush(COL_GOLD), offb = CreateSolidBrush(RGB(90, 84, 70));
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN)), oldBr = SelectObject(dc, offb);
        for (d = 0; d < pages; d++) {
            SelectObject(dc, d == page ? on : offb);
            Ellipse(dc, P(cx + d * 12 - 3), P(y1 - 17), P(cx + d * 12 + 4), P(y1 - 10));
        }
        SelectObject(dc, oldBr);
        SelectObject(dc, oldPen);
        DeleteObject(on);
        DeleteObject(offb);
    }
}

// The same panel as an image for the game's main menu (the mod shows it beside the menu in the headset): the supporters
// drawn at twice the size on black, then given an alpha (a rounded translucent plate, solid where something was drawn),
// premultiplied, bottom row first: %APPDATA%\CoD-VR\supporters_panel.bin = "SUPP", width, height, BGRA pixels.
// No supporters: the file is removed.
static HFONT MakeFont(int px, int weight, const wchar_t *face);
static void ExportSupporterPanel(void)
{
    char path[MAX_PATH], *slash;
    float saved = g_scale;
    HFONT sb = g_fBold, ss = g_fSmall, st = g_fText;
    HDC dc;
    HBITMAP bmp, old;
    BITMAPINFO bi;
    BYTE *px = NULL;
    int w, h, bh = 0, x, y, r;
    HANDLE f;
    DWORD wrote;
    strncpy(path, g_userIni, MAX_PATH - 40);
    path[MAX_PATH - 40] = 0;
    slash = strrchr(path, '\\');
    if (!slash) return;
    strcpy(slash + 1, "supporters_panel.bin");
    if (g_numSup <= 0) { DeleteFileA(path); return; }
    g_scale = 2.0f;
    g_fBold = MakeFont(14, FW_BOLD, L"Segoe UI");
    g_fText = MakeFont(13, FW_NORMAL, L"Segoe UI");
    g_fSmall = MakeFont(11, FW_NORMAL, L"Segoe UI");
    w = P(230);
    // the height is only known once the panel has said how tall it is: a first, measuring pass on a small bitmap
    dc = CreateCompatibleDC(NULL);
    {
        BITMAP bm;
        HBITMAP tiny = CreateCompatibleBitmap(GetDC(NULL), 8, 8);
        HGDIOBJ o = SelectObject(dc, tiny);
        (void)bm;
        DrawSupporters(dc, 1, &bh);
        SelectObject(dc, o);
        DeleteObject(tiny);
    }
    h = P(bh);
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;   // positive: the bottom row comes first in memory
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    if (bmp && px) {
        old = SelectObject(dc, bmp);
        memset(px, 0, (size_t)w * h * 4);
        SetBkMode(dc, TRANSPARENT);   // the text has no box of its own colour
        SetViewportOrgEx(dc, -P(14), -P(14), NULL);
        DrawSupporters(dc, 1, NULL);
        SetViewportOrgEx(dc, 0, 0, NULL);
        GdiFlush();
        r = P(16);
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                BYTE *p = px + ((size_t)y * w + x) * 4;
                int dx = x < r ? r - x : x > w - 1 - r ? x - (w - 1 - r) : 0;
                int dy = y < r ? r - y : y > h - 1 - r ? y - (h - 1 - r) : 0;
                int inside = dx * dx + dy * dy <= r * r;
                if (p[0] > 8 || p[1] > 8 || p[2] > 8) p[3] = 255;   // something drawn: solid
                else { p[0] = p[1] = p[2] = 0; p[3] = inside ? 225 : 0; }   // the plate: translucent black
            }
        }
        f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD head[3] = { 0x50505553, (DWORD)w, (DWORD)h };   // "SUPP"
            WriteFile(f, head, sizeof(head), &wrote, NULL);
            WriteFile(f, px, (DWORD)((size_t)w * h * 4), &wrote, NULL);
            CloseHandle(f);
        }
        SelectObject(dc, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(dc);
    DeleteObject(g_fBold); DeleteObject(g_fText); DeleteObject(g_fSmall);
    g_fBold = sb; g_fSmall = ss; g_fText = st;
    g_scale = saved;
}

static void DrawHome(HDC dc)
{
    Text(dc, g_fText, COL_TEXT, P(40), P(384), P(960), P(414),
         L"The Call of Duty 2 single player campaign, in virtual reality.", DT_CENTER | DT_SINGLELINE);
    if (!g_haveGame)
        Text(dc, g_fSmall, RGB(230, 110, 90), P(40), P(420), P(960), P(470),
             L"CoD2SP_s.exe was not found: put this launcher in the game folder (where CoD2SP_s.exe is).", DT_CENTER | DT_WORDBREAK);
    else if (!g_haveMod)
        Text(dc, g_fSmall, RGB(230, 110, 90), P(40), P(420), P(960), P(470),
             L"The VR mod is not installed in this folder: copy d3d9.dll, openxr_loader.dll and cod2_vr.ini here.", DT_CENTER | DT_WORDBREAK);
    else
        Text(dc, g_fSmall, COL_DIM, P(40), P(420), P(960), P(446),
             L"Connect your headset (Virtual Desktop, Air Link or SteamVR with a 32-bit OpenXR runtime), then press PLAY.",
             DT_CENTER | DT_WORDBREAK);
    if (g_newVersion[0]) {
        wchar_t line[128];
        int hot = Hovered(HIT_LINK, 2);
        _snwprintf(line, 128, L"A new version (%s) is available: click here to download it.", g_newVersion);
        line[127] = 0;
        Text(dc, g_fBold, hot ? RGB(255, 220, 120) : COL_GOLD, P(40), P(448), P(960), P(474), line, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        AddHit(P(250), P(448), P(750), P(474), HIT_LINK, 2);
    }
}

static void DrawPageTitle(HDC dc, const wchar_t *title, const wchar_t *sub)
{
    Text(dc, g_fTitle, COL_GOLD, P(50), P(22), P(950), P(62), title, DT_SINGLELINE);
    if (sub) Text(dc, g_fSmall, COL_DIM, P(50), P(62), P(950), P(84), sub, DT_SINGLELINE);
}

// Rows opts[first .. first + n - 1] from y = top; returns the one under the mouse, or -1.
static int DrawRows(HDC dc, option_t *opts, int first, int n, int rowH, int top)
{
    int j, hint = -1;
    for (j = 0; j < n; j++) {
        int i = first + j;
        option_t *o = &opts[i];
        int y0 = P(top + j * rowH), y1 = P(top + (j + 1) * rowH - 4);
        int vx0 = P(560), vx1 = P(950);
        int hot = Hovered(HIT_ROW, i) || Hovered(HIT_PREV, i) || Hovered(HIT_NEXT, i);
        const wchar_t *val = o->cur >= 0 ? o->names[o->cur] : L"Custom";
        wchar_t withRes[96];
        if (o->soon) {   // not in this mod yet: greyed out, no arrows (the row still shows its hint)
            if (hot) { Fill(dc, P(40), y0, P(960), y1, RGB(26, 26, 26)); hint = i; }
            Text(dc, g_fText, COL_DIM, P(60), y0, P(540), y1, o->label, DT_SINGLELINE | DT_VCENTER);
            Text(dc, g_fSmall, COL_DIM, vx0 + P(40), y0, vx1 - P(40), y1, SoonText(o->soon), DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            AddHit(P(40), y0, P(960), y1, HIT_ROW, i);
            continue;
        }
        if (o->showRes && o->cur >= 0) {
            // the eye size this gives, from what the headset asked for last time the game ran
            int rw = GetPrivateProfileIntA("Headset", "RecommendedWidth", 0, g_userIni);
            int rh = GetPrivateProfileIntA("Headset", "RecommendedHeight", 0, g_userIni);
            int mw = GetPrivateProfileIntA("Headset", "MaxWidth", 0, g_userIni);
            int mh = GetPrivateProfileIntA("Headset", "MaxHeight", 0, g_userIni);
            double sc = atof(strchr(o->values[o->cur], '=') + 1);
            if (rw > 0 && rh > 0) {
                int w = (int)(rw * sc + 0.5), h = (int)(rh * sc + 0.5);
                if (mw > 0 && w > mw) w = mw;
                if (mh > 0 && h > mh) h = mh;
                _snwprintf(withRes, 96, L"%s   %d\x00d7%d", val, w, h);
                withRes[95] = 0;
                val = withRes;
            }
        }
        if (hot) { Fill(dc, P(40), y0, P(960), y1, RGB(34, 30, 20)); hint = i; }
        Text(dc, g_fText, COL_TEXT, P(60), y0, P(540), y1, o->label, DT_SINGLELINE | DT_VCENTER);
        Text(dc, g_fBold, Hovered(HIT_PREV, i) ? COL_GOLD : COL_DIM, vx0, y0, vx0 + P(40), y1, L"\x25C0", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        Text(dc, g_fBold, COL_STEEL, vx0 + P(40), y0, vx1 - P(40), y1, val, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        Text(dc, g_fBold, Hovered(HIT_NEXT, i) ? COL_GOLD : COL_DIM, vx1 - P(40), y0, vx1, y1, L"\x25B6", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(vx0, y0, vx0 + P(40), y1, HIT_PREV, i);
        AddHit(vx1 - P(40), y0, vx1, y1, HIT_NEXT, i);
        AddHit(P(40), y0, P(960), y1, HIT_ROW, i);
    }
    return hint;
}

// A crosshair style (as the mod draws it: arms around a gap, a ring, a dot) in a dark square.
static void DrawReticle(HDC dc, int style, int x0, int y0, int sz)
{
    int cx = x0 + sz / 2, cy = y0 + sz / 2, r = sz * 7 / 16, k;
    int cross = style == 1 || style == 5, dot = style == 2 || style == 4 || style == 5, ring = style == 3 || style == 4;
    HPEN pen = CreatePen(PS_SOLID, P(1) > 1 ? P(1) : 1, RGB(235, 235, 235));
    HBRUSH white = CreateSolidBrush(RGB(235, 235, 235));
    HGDIOBJ oldPen, oldBrush;
    Fill(dc, x0, y0, x0 + sz, y0 + sz, RGB(52, 56, 50));
    oldPen = SelectObject(dc, pen);
    oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (style == 0) {   // none: a struck-through square
        MoveToEx(dc, x0 + sz / 4, y0 + sz * 3 / 4, NULL);
        LineTo(dc, x0 + sz * 3 / 4, y0 + sz / 4);
    }
    if (cross)
        for (k = 0; k < 4; k++) {
            int dx = k == 0 ? 1 : k == 1 ? -1 : 0, dy = k == 2 ? 1 : k == 3 ? -1 : 0;
            MoveToEx(dc, cx + dx * r * 45 / 100, cy + dy * r * 45 / 100, NULL);
            LineTo(dc, cx + dx * (r + 1), cy + dy * (r + 1));
        }
    if (ring) {
        int rr = r * 7 / 10;
        Ellipse(dc, cx - rr, cy - rr, cx + rr + 1, cy + rr + 1);
    }
    if (dot) {
        int d = r / 4 > 1 ? r / 4 : 1;
        SelectObject(dc, white);
        Ellipse(dc, cx - d, cy - d, cx + d + 1, cy + d + 1);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(white);
    DeleteObject(pen);
}

// Rows in a column from x0 to x1 (label, then the value with its arrows); returns the hovered one.
// The crosshair's colour: [Controllers] CrosshairColor is a name (the presets) or RRGGBB (the colour picker), as the
// Call of Duty 1 VR launcher's. The mod reads both.
static COLORREF g_custColors[16];
static int CrosshairRGB(COLORREF *out, wchar_t *label, int labelLen)
{
    static const struct { const char *name; BYTE r, g, b; } cols[] = {
        { "white", 255, 255, 255 }, { "red", 255, 38, 31 }, { "green", 51, 255, 64 }, { "yellow", 255, 230, 26 },
        { "cyan", 26, 242, 255 }, { "blue", 64, 115, 255 }, { "orange", 255, 140, 26 }, { "magenta", 255, 51, 230 },
    };
    char v[64];
    int i;
    ReadSetting("Controllers", "CrosshairColor", v, sizeof(v));
    if (!v[0]) strcpy(v, "white");
    if (strlen(v) == 6 && strspn(v, "0123456789abcdefABCDEF") == 6) {
        unsigned long rgb = strtoul(v, NULL, 16);
        *out = RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
        _snwprintf(label, labelLen, L"#%06lX", rgb & 0xFFFFFF);
        label[labelLen - 1] = 0;
        return 1;
    }
    for (i = 0; i < (int)(sizeof(cols) / sizeof(cols[0])); i++)
        if (!_stricmp(v, cols[i].name)) {
            *out = RGB(cols[i].r, cols[i].g, cols[i].b);
            _snwprintf(label, labelLen, L"#%02X%02X%02X", cols[i].r, cols[i].g, cols[i].b);
            label[labelLen - 1] = 0;
            return 1;
        }
    *out = RGB(255, 255, 255);
    _snwprintf(label, labelLen, L"#FFFFFF");
    return 0;
}

static void PickCrosshairColor(HWND wnd)
{
    CHOOSECOLORW cc;
    COLORREF cur;
    wchar_t label[16];
    char hex[16], dir[MAX_PATH], *slash;
    CrosshairRGB(&cur, label, 16);
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = wnd;
    cc.rgbResult = cur;
    cc.lpCustColors = g_custColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return;
    strcpy(dir, g_userIni);
    slash = strrchr(dir, '\\');
    if (slash) { *slash = 0; CreateDirectoryA(dir, NULL); }
    _snprintf(hex, sizeof(hex), "%02X%02X%02X", GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
    WritePrivateProfileStringA("Controllers", "CrosshairColor", hex, g_userIni);
}

static int DrawColumn(HDC dc, option_t *opts, int first, int n, int rowH, int top, int x0, int x1)
{
    int j, hint = -1, vx0 = P(x0 + 215), vx1 = P(x1);
    for (j = 0; j < n; j++) {
        int i = first + j;
        option_t *o = &opts[i];
        int y0 = P(top + j * rowH), y1 = P(top + (j + 1) * rowH - 4);
        int hot = Hovered(HIT_ROW, i) || Hovered(HIT_PREV, i) || Hovered(HIT_NEXT, i);
        const wchar_t *val = o->cur >= 0 ? o->names[o->cur] : L"Custom";
        if (o->soon) {   // not in this mod yet: greyed out, no arrows (the row still shows its hint)
            if (hot) { Fill(dc, P(x0), y0, P(x1), y1, RGB(26, 26, 26)); hint = i; }
            Text(dc, g_fText, COL_DIM, P(x0 + 12), y0, vx0, y1, o->label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            Text(dc, g_fSmall, COL_DIM, vx0 + P(26), y0, vx1 - P(26), y1, SoonText(o->soon), DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
            AddHit(P(x0), y0, P(x1), y1, HIT_ROW, i);
            continue;
        }
        if (hot) { Fill(dc, P(x0), y0, P(x1), y1, RGB(34, 30, 20)); hint = i; }
        Text(dc, g_fText, COL_TEXT, P(x0 + 12), y0, vx0, y1, o->label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        if (o->swatch) {   // a colour: the swatch and its hex value, no arrows (the click opens the picker)
            COLORREF c;
            wchar_t lab[16];
            int sz = y1 - y0 - P(6);
            CrosshairRGB(&c, lab, 16);
            Fill(dc, vx0 + P(34), y0 + P(3), vx0 + P(34) + sz, y0 + P(3) + sz, hot ? COL_GOLD : RGB(120, 120, 120));
            Fill(dc, vx0 + P(35), y0 + P(4), vx0 + P(34) + sz - P(1), y0 + P(3) + sz - P(1), c);
            Text(dc, g_fBold, COL_STEEL, vx0 + P(42) + sz, y0, vx1 - P(10), y1, lab, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            AddHit(P(x0), y0, P(x1), y1, HIT_ROW, i);
            continue;
        }
        Text(dc, g_fBold, Hovered(HIT_PREV, i) ? COL_GOLD : COL_DIM, vx0, y0, vx0 + P(26), y1, L"\x25C0", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        if (o->reticle && o->cur >= 0) {   // the picture, then the name
            int sz = y1 - y0 - P(6);
            DrawReticle(dc, o->cur, vx0 + P(34), y0 + P(3), sz);
            Text(dc, g_fBold, COL_STEEL, vx0 + P(42) + sz, y0, vx1 - P(26), y1, val, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
        } else
        Text(dc, g_fBold, COL_STEEL, vx0 + P(26), y0, vx1 - P(26), y1, val, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
        Text(dc, g_fBold, Hovered(HIT_NEXT, i) ? COL_GOLD : COL_DIM, vx1 - P(26), y0, vx1, y1, L"\x25B6", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(vx0, y0, vx0 + P(26), y1, HIT_PREV, i);
        AddHit(vx1 - P(26), y0, vx1, y1, HIT_NEXT, i);
        AddHit(P(x0), y0, P(x1), y1, HIT_ROW, i);
    }
    return hint;
}

static void DrawHint(HDC dc, option_t *opts, int hint)
{
    if (hint >= 0)
        Text(dc, g_fSmall, COL_DIM, P(50), P(444), P(950), P(476),
             opts[hint].choiceHints && opts[hint].cur >= 0 && !opts[hint].soon ? opts[hint].choiceHints[opts[hint].cur] : opts[hint].hint,
             DT_SINGLELINE | DT_END_ELLIPSIS);
    else
        Text(dc, g_fSmall, COL_DIM, P(50), P(444), P(950), P(476),
             L"Click a setting to change it. Changes are saved at once and used the next time the game starts.", DT_SINGLELINE);
}

// The description of the option under the mouse, in a rounded box at the bottom of the page (VR tab). Empty page: a line of
// advice.
static void DrawDescriptionBox(HDC dc, option_t *opts, int hint)
{
    int x0 = P(40), y0 = P(492), x1 = P(960), y1 = P(562);
    const wchar_t *s = hint >= 0 ? (opts[hint].choiceHints && opts[hint].cur >= 0 && !opts[hint].soon ? opts[hint].choiceHints[opts[hint].cur] : opts[hint].hint)
                                 : L"Move the mouse over an option to see what it does. Click it to change it: changes are saved at once and used the next time the game starts.";
    HPEN pen = CreatePen(PS_SOLID, P(1) > 1 ? P(1) : 1, hint >= 0 ? COL_GOLD_DARK : RGB(80, 80, 80));
    HBRUSH brush = CreateSolidBrush(RGB(18, 18, 18));
    HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, brush);
    RoundRect(dc, x0, y0, x1, y1, P(16), P(16));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    Text(dc, g_fSmall, hint >= 0 ? COL_TEXT : COL_DIM, x0 + P(14), y0 + P(7), x1 - P(14), y1 - P(6), s, DT_WORDBREAK);
}

static void DrawOptions(HDC dc, option_t *opts, int n, int rowH, int top)
{
    DrawHint(dc, opts, DrawRows(dc, opts, 0, n, rowH, top));
}

// GRAPHICS: the main settings, then the post-processing ones under their own title.
static void DrawGraphics(HDC dc)
{
    int h1, h2;
    DrawPageTitle(dc, L"GRAPHICS", L"The game's own options (Options in the game menu) still apply on top of these.");
    h1 = DrawRows(dc, g_gfxOpts, 0, GFX_POST_FIRST, 30, 94);
    Text(dc, g_fBold, COL_GOLD, P(50), P(278), P(950), P(300), L"POST-PROCESSING", DT_SINGLELINE | DT_VCENTER);
    Text(dc, g_fSmall, COL_DIM, P(50), P(300), P(950), P(318),
         L"Test options: if the image looks worse, leave them off.", DT_SINGLELINE | DT_VCENTER);
    h2 = DrawRows(dc, g_gfxOpts, GFX_POST_FIRST, NUM_GFX - GFX_POST_FIRST, 29, 322);
    DrawHint(dc, g_gfxOpts, h1 >= 0 ? h1 : h2);
}

// One drop-down box of the CONTROLS page: the label, the choice, an arrow; clicking opens the list.
static void DrawDropDown(HDC dc, int i, int y0, int y1, int lx0, int bx0, int rx0, int rx1)
{
    int arg = CTL_ARG + i, hot = Hovered(HIT_ROW, arg), cur = g_ctl[i].cur;
    int changed = cur < 0 || !IsDefault(i, cur);
    if (g_ctl[i].soon) {   // not in this mod yet: greyed out, no list
        Text(dc, g_fText, COL_DIM, lx0, y0, bx0, y1, g_ctl[i].label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        Fill(dc, bx0, y0, rx1, y1, RGB(50, 50, 50));
        Fill(dc, bx0 + P(1), y0 + P(1), rx1 - P(1), y1 - P(1), RGB(20, 20, 20));
        Text(dc, g_fSmall, COL_DIM, bx0 + P(8), y0, rx1 - P(8), y1, SoonText(g_ctl[i].soon), DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        return;
    }
    if (hot) Fill(dc, rx0, y0, rx1, y1, RGB(34, 30, 20));
    Text(dc, g_fText, COL_TEXT, lx0, y0, bx0, y1, g_ctl[i].label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    Fill(dc, bx0, y0, rx1, y1, hot ? COL_GOLD : RGB(80, 80, 80));
    Fill(dc, bx0 + P(1), y0 + P(1), rx1 - P(1), y1 - P(1), RGB(24, 24, 24));
    Text(dc, g_fBold, changed ? COL_GOLD : COL_STEEL, bx0 + P(8), y0, rx1 - P(26), y1,
         cur >= 0 ? g_ctl[i].ch[cur].name : L"Custom", DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    Text(dc, g_fSmall, hot ? COL_GOLD : COL_DIM, rx1 - P(26), y0, rx1, y1, L"\x25BC", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    AddHit(rx0, y0, rx1, y1, HIT_ROW, arg);
}

static void DrawControls(HDC dc)
{
    int i;
    DrawPageTitle(dc, L"CONTROLS", L"Touch controllers. Click a box to pick from a list. Changes are used the next time the game starts.");
    {
        // the profile in use: a drop-down, then New and Delete
        wchar_t pname[40];
        int hotP = Hovered(HIT_PROFILE, 0), hotN = Hovered(HIT_NEWPROF, 0), hotD = Hovered(HIT_DELPROF, 0), y0 = P(26), y1 = P(54);
        MultiByteToWideChar(CP_ACP, 0, g_profNames[g_profile], -1, pname, 40);
        Text(dc, g_fText, COL_TEXT, P(330), y0, P(410), y1, L"Profile", DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        Fill(dc, P(424), y0, P(690), y1, hotP ? COL_GOLD : RGB(80, 80, 80));
        Fill(dc, P(425), y0 + P(1), P(689), y1 - P(1), RGB(24, 24, 24));
        Text(dc, g_fBold, COL_STEEL, P(434), y0, P(662), y1, pname, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        Text(dc, g_fSmall, hotP ? COL_GOLD : COL_DIM, P(662), y0, P(690), y1, L"\x25BC", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(P(424), y0, P(690), y1, HIT_PROFILE, 0);
        Fill(dc, P(702), y0, P(810), y1, hotN ? COL_GOLD : RGB(80, 80, 80));
        Fill(dc, P(703), y0 + P(1), P(809), y1 - P(1), RGB(24, 24, 24));
        Text(dc, g_fBold, hotN ? COL_GOLD : COL_TEXT, P(702), y0, P(810), y1, L"New", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(P(702), y0, P(810), y1, HIT_NEWPROF, 0);
        Fill(dc, P(822), y0, P(930), y1, g_profile > 0 && hotD ? COL_GOLD : RGB(80, 80, 80));
        Fill(dc, P(823), y0 + P(1), P(929), y1 - P(1), RGB(24, 24, 24));
        Text(dc, g_fBold, g_profile == 0 ? COL_DIM : hotD ? COL_GOLD : COL_TEXT, P(822), y0, P(930), y1, L"Delete", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        if (g_profile > 0) AddHit(P(822), y0, P(930), y1, HIT_DELPROF, 0);
    }
    // the dominant hand is the most important choice: a row of its own across the page
    DrawDropDown(dc, CTL_HAND, P(92), P(118), P(60), P(230), P(40), P(470));
    Text(dc, g_fText, COL_DIM, P(520), P(92), P(960), P(118), L"Weapon in hand: the gun stays in your dominant hand",
         DT_SINGLELINE | DT_VCENTER);
    // left column: the weapons on the body, then the sticks; right column: the buttons, then their shortcuts
    Text(dc, g_fBold, COL_GOLD, P(50), P(130), P(480), P(154), L"Grip on your body (either hand): weapon to your hand", DT_SINGLELINE);
    for (i = 0; i < NUM_HOLSTERS; i++) {
        int y0 = P(158 + i * 30);
        DrawDropDown(dc, i, y0, y0 + P(26), P(60), P(230), P(40), P(480));
    }
    Text(dc, g_fBold, COL_GOLD, P(50), P(376), P(480), P(400), L"Sticks", DT_SINGLELINE);
    {
        // the stick that does not move you is free: its four directions take an action
        static const wchar_t *const left[4] = { L"Left stick up", L"Left stick down", L"Left stick left", L"Left stick right" };
        static const wchar_t *const right[4] = { L"Right stick up", L"Right stick down", L"Right stick left", L"Right stick right" };
        int moveRight = g_ctl[CTL_MOVE].cur == 1;
        for (i = CTL_MOVE; i < NUM_CTL; i++) {
            int y0 = P(404 + (i - CTL_MOVE) * 28);
            if (i > CTL_MOVE) g_ctl[i].label = moveRight ? left[i - CTL_MOVE - 1] : right[i - CTL_MOVE - 1];
            DrawDropDown(dc, i, y0, y0 + P(24), P(60), P(230), P(40), P(480));
        }
    }
    {
        // the dominant hand's two buttons (A and B, or X and Y), then the stick clicks
        int rh = g_ctl[CTL_HAND].cur != 1, first = CTL_BUTTONS, k;
        static const wchar_t *const plain[4] = { L"A", L"B", L"X", L"Y" };
        static const wchar_t *const tap[4] = { L"A (tap)", L"B (tap)", L"X (tap)", L"Y (tap)" };
        for (k = 0; k < 4; k++) g_ctl[first + k].label = plain[k];
        Text(dc, g_fBold, COL_GOLD, P(520), P(130), P(950), P(154), L"Buttons", DT_SINGLELINE);
        for (k = 0; k < 4; k++) {
            int idx = k < 2 ? first + (rh ? 0 : 2) + k : first + 4 + (k - 2);   // A B | X Y, then the two stick clicks
            int y0 = P(158 + k * 28);
            DrawDropDown(dc, idx, y0, y0 + P(24), P(540), P(710), P(520), P(960));
        }
        // the two buttons of the other hand are wildcards: tapped on their own they run their action (first two rows), held,
        // then another button, one shortcut each
        Text(dc, g_fBold, COL_GOLD, P(520), P(280), P(960), P(304), rh ? L"X and Y: tap, or hold then press" : L"A and B: tap, or hold then press", DT_SINGLELINE);
        for (k = 0; k < 2; k++) {
            int idx = first + (rh ? 2 : 0) + k, y0 = P(308 + k * 26);
            g_ctl[idx].label = tap[(rh ? 2 : 0) + k];
            DrawDropDown(dc, idx, y0, y0 + P(24), P(540), P(710), P(520), P(960));
        }
        for (k = 0; k < 6; k++) {
            int idx = CTL_SHORTCUT + (rh ? 0 : 6) + k, y0 = P(308 + (k + 2) * 26);
            DrawDropDown(dc, idx, y0, y0 + P(24), P(540), P(710), P(520), P(960));
        }
    }
    {
        int hot = Hovered(HIT_RESET, 0), x0 = P(760), y0 = P(524), x1 = P(960), y1 = P(550);
        Fill(dc, x0, y0, x1, y1, hot ? COL_GOLD : RGB(80, 80, 80));
        Fill(dc, x0 + P(1), y0 + P(1), x1 - P(1), y1 - P(1), RGB(24, 24, 24));
        Text(dc, g_fBold, hot ? COL_GOLD : COL_TEXT, x0, y0, x1, y1, L"Reset to defaults", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(x0, y0, x1, y1, HIT_RESET, 0);
    }
}

static void DrawAbout(HDC dc)
{
    DrawPageTitle(dc, L"ABOUT", L"Call of Duty 2 VR " VERSION_TEXT);
    Text(dc, g_fText, COL_TEXT, P(50), P(100), P(950), P(200),
         L"A free, fan-made mod that turns the Call of Duty 2 single player campaign into a PC VR game, "
         L"from the makers of the Call of Duty 1 VR mod.\n\n"
         L"The project is in early development and bugs can happen. Please report them on Discord.",
         DT_WORDBREAK);
    Text(dc, g_fBold, Hovered(HIT_LINK, 0) ? COL_GOLD : COL_STEEL, P(50), P(250), P(600), P(280),
         L"GitHub: download, updates and documentation", DT_SINGLELINE);
    AddHit(P(50), P(250), P(600), P(280), HIT_LINK, 0);
    Text(dc, g_fBold, Hovered(HIT_LINK, 1) ? COL_GOLD : COL_STEEL, P(50), P(290), P(600), P(320),
         L"Discord: bugs, suggestions and news", DT_SINGLELINE);
    AddHit(P(50), P(290), P(600), P(320), HIT_LINK, 1);
    Text(dc, g_fBold, Hovered(HIT_LINK, 3) ? COL_GOLD : COL_STEEL, P(50), P(330), P(900), P(360),
         L"Problem starting in VR? Save a diagnostic report to send on Discord", DT_SINGLELINE);
    AddHit(P(50), P(330), P(900), P(360), HIT_LINK, 3);
    Text(dc, g_fSmall, COL_DIM, P(50), P(380), P(950), P(470),
         L"Unofficial fan project, not affiliated with or endorsed by Activision. Call of Duty is a trademark of "
         L"Activision Publishing, Inc. A legitimate copy of the game is required; no game files are included.",
         DT_WORDBREAK);
}

static void DrawBar(HDC dc)
{
    // HOME: the way back from any option page
    static const wchar_t *names[] = { L"HOME", L"CONTROLS", L"GRAPHICS", L"VR", L"MODS", L"ABOUT" };
    static const int widths[] = { 80, 130, 130, 60, 90, 100 };
    int i, x = P(230);
    Fill(dc, 0, P(BAR_Y), P(BASE_W), P(BASE_H), COL_BAR);
    Fill(dc, 0, P(BAR_Y), P(BASE_W), P(BAR_Y) + P(2), COL_GOLD_DARK);
    // PLAY
    {
        int x0 = P(30), y0 = P(BAR_Y + 14), x1 = P(210), y1 = P(BASE_H - 14);
        int hot = Hovered(HIT_PLAY, 0), running = GameRunning();
        Fill(dc, x0, y0, x1, y1, running ? COL_GOLD_DARK : hot ? RGB(236, 192, 80) : COL_GOLD);
        Text(dc, running ? g_fNav : g_fPlay, RGB(20, 16, 6), x0, y0, x1, y1, running ? L"RUNNING" : L"PLAY",
             DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(x0, y0, x1, y1, HIT_PLAY, 0);
    }
    for (i = 0; i < 6; i++) {
        int page = PAGE_HOME + i, w = P(widths[i]);
        int on = g_page == page, hot = Hovered(HIT_NAV, page);
        int y0 = P(BAR_Y + 14), y1 = P(BASE_H - 14);
        Text(dc, g_fNav, on || hot ? COL_GOLD : COL_TEXT, x, y0, x + w, y1, names[i], DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        if (on) Fill(dc, x + P(16), y1 - P(4), x + w - P(16), y1 - P(1), COL_GOLD);
        AddHit(x, y0, x + w, y1, HIT_NAV, page);
        x += w + P(12);
    }
    Text(dc, g_fSmall, COL_DIM, P(900), P(BAR_Y + 14), P(980), P(BASE_H - 14), VERSION_TEXT, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
}

static void Paint(HWND wnd, HDC wdc)
{
    RECT rc;
    HDC dc;
    HBITMAP bmp;
    HGDIOBJ old;
    GetClientRect(wnd, &rc);
    dc = CreateCompatibleDC(wdc);
    bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
    old = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);
    g_numHits = 0;

    DrawBanner(dc);
    if (g_page == PAGE_HOME) {
        DrawHome(dc);
    } else {
        Veil(dc, 0, 0, P(BASE_W), P(BAR_Y), 225);
        if (g_page == PAGE_CONTROLS) DrawControls(dc);
        else if (g_page == PAGE_GRAPHICS) {
            DrawGraphics(dc);
        } else if (g_page == PAGE_VR) {
            int h1, h2;
            DrawPageTitle(dc, L"VR", NULL);
            Text(dc, g_fBold, COL_GOLD, P(40), P(66), P(480), P(90), L"COMFORT AND MOVEMENT", DT_SINGLELINE | DT_VCENTER);
            Text(dc, g_fBold, COL_GOLD, P(510), P(66), P(960), P(90), L"WEAPONS AND VIBRATION", DT_SINGLELINE | DT_VCENTER);
            h1 = DrawColumn(dc, g_vrOpts, 0, VR_COL2, 26, 94, 40, 480);
            h2 = DrawColumn(dc, g_vrOpts, VR_COL2, NUM_VR - VR_COL2, 26, 94, 510, 960);   // 15 rows: 26 px each, like the left column
            DrawDescriptionBox(dc, g_vrOpts, h1 >= 0 ? h1 : h2);
        } else if (g_page == PAGE_MODS) {
            DrawPageTitle(dc, L"MODS", L"Things the original game does not have. Used the next time the game starts.");
            DrawOptions(dc, g_modOpts, NUM_MOD, 40, 100);
            if (g_modOpts[MOD_DIFFICULTY].cur >= 0 && !g_modOpts[MOD_DIFFICULTY].soon)   // what the chosen difficulty does, always in view
                Text(dc, g_fSmall, COL_STEEL, P(60), P(100 + NUM_MOD * 40 + 12), P(950), P(100 + NUM_MOD * 40 + 60),
                     g_difficultyHints[g_modOpts[MOD_DIFFICULTY].cur], DT_WORDBREAK);
        } else if (g_page == PAGE_ABOUT) DrawAbout(dc);
    }
    DrawBar(dc);

    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static int HitAt(int x, int y)
{
    int i;
    POINT p = { x, y };
    for (i = 0; i < g_numHits; i++) if (PtInRect(&g_hits[i].r, p)) return i;
    return -1;
}

static option_t *PageOption(int i)
{
    if (g_page == PAGE_VR && i < NUM_VR) return &g_vrOpts[i];
    if (g_page == PAGE_GRAPHICS && i < NUM_GFX) return &g_gfxOpts[i];
    if (g_page == PAGE_MODS && i < NUM_MOD) return &g_modOpts[i];
    return NULL;
}

// ---- a small dialog asking for a profile's name (built in memory: no resource needed) ----
#define IDC_PROFNAME 100

static INT_PTR CALLBACK NameProc(HWND d, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_INITDIALOG) {
        SetWindowLongPtrW(d, DWLP_USER, l);
        SendDlgItemMessageW(d, IDC_PROFNAME, EM_LIMITTEXT, 30, 0);
        SetFocus(GetDlgItem(d, IDC_PROFNAME));
        return FALSE;
    }
    if (m == WM_COMMAND && LOWORD(w) == IDOK) {
        GetDlgItemTextW(d, IDC_PROFNAME, (wchar_t *)GetWindowLongPtrW(d, DWLP_USER), 32);
        EndDialog(d, 1);
        return TRUE;
    }
    if (m == WM_COMMAND && LOWORD(w) == IDCANCEL) {
        EndDialog(d, 0);
        return TRUE;
    }
    return FALSE;
}

static WORD *DlgAlign(WORD *p) { return (WORD *)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3); }

static WORD *DlgItem(WORD *p, DWORD style, int x, int y, int cx, int cy, WORD id, WORD cls, const wchar_t *text)
{
    DLGITEMTEMPLATE *it;
    p = DlgAlign(p);
    it = (DLGITEMTEMPLATE *)p;
    it->style = style | WS_CHILD | WS_VISIBLE;
    it->dwExtendedStyle = 0;
    it->x = (short)x; it->y = (short)y; it->cx = (short)cx; it->cy = (short)cy;
    it->id = id;
    p = (WORD *)(it + 1);
    *p++ = 0xFFFF;
    *p++ = cls;
    while ((*p++ = *text++) != 0) ;
    *p++ = 0;   // no creation data
    return p;
}

// Asks for a name; returns 1 with it in name (32 characters), 0 when cancelled.
static int AskName(HWND wnd, wchar_t *name)
{
    DWORD buf[256] = { 0 };
    WORD *p = (WORD *)buf;
    DLGTEMPLATE *t = (DLGTEMPLATE *)buf;
    const wchar_t *title = L"New profile", *font = L"Segoe UI";
    t->style = DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    t->cdit = 4;
    t->cx = 200; t->cy = 62;
    p = (WORD *)(t + 1);
    *p++ = 0;   // no menu
    *p++ = 0;   // default dialog class
    while ((*p++ = *title++) != 0) ;
    *p++ = 9;
    while ((*p++ = *font++) != 0) ;
    p = DlgItem(p, SS_LEFT, 10, 8, 180, 10, (WORD)-1, 0x0082, L"Name of the new profile:");
    p = DlgItem(p, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, 20, 180, 14, IDC_PROFNAME, 0x0081, L"");
    p = DlgItem(p, WS_TABSTOP | BS_DEFPUSHBUTTON, 90, 42, 48, 14, IDOK, 0x0080, L"OK");
    p = DlgItem(p, WS_TABSTOP | BS_PUSHBUTTON, 144, 42, 48, 14, IDCANCEL, 0x0080, L"Cancel");
    name[0] = 0;
    return DialogBoxIndirectParamW(GetModuleHandleW(NULL), (DLGTEMPLATE *)buf, wnd, NameProc, (LPARAM)name) == 1;
}

// New profile: asks for a name; it starts as a copy of the one in use and becomes the active one.
static void NewProfile(HWND wnd)
{
    wchar_t wname[32], back[32];
    char name[32], sec[64], val[64];
    int i, len;
    if (g_numProfiles >= MAX_PROFILES) {
        MessageBoxW(wnd, L"There are too many profiles: delete one first.", L"CoD2 VR", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!AskName(wnd, wname)) return;
    for (len = (int)wcslen(wname); len > 0 && wname[len - 1] == L' '; len--) wname[len - 1] = 0;
    for (i = 0; wname[i] == L' '; i++) ;
    if (i) memmove(wname, wname + i, (wcslen(wname + i) + 1) * sizeof(wchar_t));
    if (!wname[0]) return;
    WideCharToMultiByte(CP_ACP, 0, wname, -1, name, sizeof(name), NULL, NULL);
    name[sizeof(name) - 1] = 0;
    MultiByteToWideChar(CP_ACP, 0, name, -1, back, 32);
    if (wcscmp(wname, back) || strpbrk(name, "[]=")) {
        MessageBoxW(wnd, L"The name has characters that cannot be used.", L"CoD2 VR", MB_OK | MB_ICONWARNING);
        return;
    }
    for (i = 0; i < g_numProfiles; i++)
        if (!_stricmp(name, g_profNames[i])) {
            MessageBoxW(wnd, L"A profile with that name already exists.", L"CoD2 VR", MB_OK | MB_ICONWARNING);
            return;
        }
    strcpy(g_profNames[g_numProfiles], name);
    g_profile = g_numProfiles++;
    ProfileSection(g_profile, sec, sizeof(sec));
    for (i = 0; i < NUM_CTL; i++) {   // the copy of what is set now
        if (g_ctl[i].soon) continue;
        GetPrivateProfileStringA(g_ctl[i].section, g_ctl[i].key, CtlDef(i), val, sizeof(val), g_userIni);
        WritePrivateProfileStringA(sec, g_ctl[i].key, val, g_userIni);
    }
    ApplyProfile();
}

// The dominant hand changes: what the profile still holds at the OLD hand's default was never a choice, so it is dropped
// (it takes the new hand's default); what the player changed stays.
static void ChangeHand(int left)
{
    char sec[64], val[64];
    int i, old = g_leftHand;
    ProfileSection(g_profile, sec, sizeof(sec));
    for (i = 0; i < NUM_CTL; i++) {
        if (i == CTL_HAND || g_ctl[i].soon) continue;
        GetPrivateProfileStringA(sec, g_ctl[i].key, "\x01", val, sizeof(val), g_userIni);
        if (val[0] != 1 && !_stricmp(val, CtlDefFor(i, old))) WritePrivateProfileStringA(sec, g_ctl[i].key, NULL, g_userIni);
    }
    WritePrivateProfileStringA(sec, "RightHanded", left ? "0" : "1", g_userIni);
    ApplyProfile();
}

static void Click(HWND wnd, int idx)
{
    hit_t *h;
    option_t *o;
    if (idx < 0) return;
    h = &g_hits[idx];
    switch (h->type) {
    case HIT_PLAY:
        StartGame(wnd);
        break;
    case HIT_NAV:
        g_page = g_page == h->arg ? PAGE_HOME : h->arg;
        break;
    case HIT_PREV:
    case HIT_NEXT:
    case HIT_ROW:
        if (g_page == PAGE_CONTROLS && h->arg >= CTL_ARG && h->arg < CTL_ARG + NUM_CTL) {
            // the drop-down list, under the box; the current choice is ticked
            int ct = h->arg - CTL_ARG, a, pick;
            HMENU menu;
            POINT pt = { h->r.right - P(250), h->r.bottom };
            if (g_ctl[ct].soon) break;
            menu = CreatePopupMenu();
            for (a = 0; a < g_ctl[ct].n; a++) {
                wchar_t name[64];
                _snwprintf(name, 64, IsDefault(ct, a) ? L"%s   (default)" : L"%s", g_ctl[ct].ch[a].name);
                name[63] = 0;
                AppendMenuW(menu, MF_STRING | (a == g_ctl[ct].cur ? MF_CHECKED : 0), a + 1, name);
            }
            ClientToScreen(wnd, &pt);
            pick = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, wnd, NULL);
            DestroyMenu(menu);
            if (pick > 0 && ct == CTL_HAND && pick - 1 != g_ctl[ct].cur) ChangeHand(!strcmp(g_ctl[ct].ch[pick - 1].val, "0"));
            else if (pick > 0) { g_ctl[ct].cur = pick - 1; SaveControl(ct); }
            break;
        }
        o = PageOption(h->arg);
        if (!o || o->soon) break;   // a greyed-out row: not in this mod yet
        if (o->swatch) { PickCrosshairColor(wnd); break; }
        if (g_page == PAGE_GRAPHICS && o == &g_gfxOpts[GFX_FONT]) {
            // Text font: make or remove the font pack (elevated when the game folder is not writable)
            int modern = !FontPackExists();
            int done = modern ? BuildFontPack() : RemoveFontPack();
            if (!done) done = RunElevated(wnd, modern ? "/fontpack on" : "/fontpack off");
            if (!done)
                MessageBoxW(wnd, modern ? L"The font pack could not be made from your game files." : L"The font pack could not be removed. Delete main\\zz_cod2vr_font.iwd from the game folder.",
                            L"CoD2 VR", MB_OK | MB_ICONWARNING);
            o->cur = FontPackExists();
            break;
        }
        if (g_page == PAGE_VR && o == &g_vrOpts[VR_HANDS]) {
            // Player hands: make or remove the pack (elevated when the game folder is not writable)
            int hide = !HandsPackExists();
            int done = hide ? BuildHandsPack() : RemoveHandsPack();
            if (!done) done = RunElevated(wnd, hide ? "/handspack hide" : "/handspack show");
            if (!done)
                MessageBoxW(wnd, hide ? L"The arms could not be hidden: the pack could not be made from your game files. "
                                        L"The mod works without it."
                                      : L"The pack could not be removed. Delete main\\zz_cod2vr.iwd from the game folder.",
                            L"CoD2 VR", MB_OK | MB_ICONWARNING);
            o->cur = HandsPackExists();
            break;
        }
        if (o->cur < 0) o->cur = o->def;
        else if (h->type == HIT_PREV) o->cur = (o->cur + o->count - 1) % o->count;
        else o->cur = (o->cur + 1) % o->count;
        SaveOption(o);
        break;
    case HIT_RESET:
        if (MessageBoxW(wnd, L"Put every weapon zone and button of this profile back to its default?", L"CoD2 VR", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            int c, a;
            for (c = 0; c < NUM_CTL; c++) {
                if (c == CTL_HAND || g_ctl[c].soon) continue;   // the dominant hand stays: the defaults are its
                g_ctl[c].cur = -1;
                for (a = 0; a < g_ctl[c].n; a++)
                    if (IsDefault(c, a)) { g_ctl[c].cur = a; break; }
                SaveControl(c);   // written out: the defaults win over a cod2_vr.ini edit as well
            }
        }
        break;
    case HIT_PROFILE: {
        // the drop-down of profiles: the one in use is ticked
        int a, pick;
        HMENU menu = CreatePopupMenu();
        POINT pt = { h->r.left, h->r.bottom };
        for (a = 0; a < g_numProfiles; a++) {
            wchar_t name[40];
            MultiByteToWideChar(CP_ACP, 0, g_profNames[a], -1, name, 40);
            AppendMenuW(menu, MF_STRING | (a == g_profile ? MF_CHECKED : 0), a + 1, name);
        }
        ClientToScreen(wnd, &pt);
        pick = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, wnd, NULL);
        DestroyMenu(menu);
        if (pick > 0 && pick - 1 != g_profile) { g_profile = pick - 1; ApplyProfile(); }
        break;
    }
    case HIT_NEWPROF:
        NewProfile(wnd);
        break;
    case HIT_DELPROF:
        if (g_profile > 0) {
            wchar_t msg[96], name[40];
            MultiByteToWideChar(CP_ACP, 0, g_profNames[g_profile], -1, name, 40);
            _snwprintf(msg, 96, L"Delete the profile \"%s\"?", name);
            msg[95] = 0;
            if (MessageBoxW(wnd, msg, L"CoD2 VR", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                char sec[64];
                ProfileSection(g_profile, sec, sizeof(sec));
                WritePrivateProfileStringA(sec, NULL, NULL, g_userIni);   // the whole section
                LoadProfileList();
                g_profile = 0;
                ApplyProfile();
            }
        }
        break;
    case HIT_LINK:
        if (h->arg == 3) {
            char out[MAX_PATH], arg[MAX_PATH + 16];
            wchar_t msg[MAX_PATH + 200], wout[MAX_PATH];
            if (!WriteDiagnostics(g_dir, VERSION_TEXT, out, MAX_PATH)) {
                MessageBoxW(wnd, L"The report could not be written.", L"CoD2 VR", MB_OK | MB_ICONWARNING);
                break;
            }
            MultiByteToWideChar(CP_ACP, 0, out, -1, wout, MAX_PATH);
            _snwprintf(msg, MAX_PATH + 200, L"The report was saved here:\n\n%s\n\nSend this file on Discord.", wout);
            MessageBoxW(wnd, msg, L"CoD2 VR", MB_OK | MB_ICONINFORMATION);
            _snprintf(arg, sizeof(arg), "/select,\"%s\"", out);
            ShellExecuteA(wnd, "open", "explorer.exe", arg, NULL, SW_SHOWNORMAL);
            break;
        }
        ShellExecuteA(wnd, "open", h->arg == 2 ? URL_RELEASES : h->arg ? URL_DISCORD : URL_GITHUB, NULL, NULL, SW_SHOWNORMAL);
        break;
    }
    InvalidateRect(wnd, NULL, FALSE);
}

static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        Paint(wnd, dc);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER: {
        static int was;
        int now = GameRunning();
        if (now != was) { was = now; InvalidateRect(wnd, NULL, FALSE); }
        return 0;
    }
    case WM_MOUSEMOVE: {
        int h = HitAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
        TrackMouseEvent(&tme);
        if (h != g_hover) { g_hover = h; InvalidateRect(wnd, NULL, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hover != -1) { g_hover = -1; InvalidateRect(wnd, NULL, FALSE); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(NULL, g_hover >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        Click(wnd, HitAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)));
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            if (g_page != PAGE_HOME) { g_page = PAGE_HOME; InvalidateRect(wnd, NULL, FALSE); }
            else PostMessageW(wnd, WM_CLOSE, 0, 0);
        } else if (wp == VK_RETURN) {
            StartGame(wnd);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    case WM_UPDATE_FOUND:
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
// The banner is a JPEG resource, decoded by OLE (no image library needed).
static void LoadBanner(HINSTANCE inst)
{
    HRSRC res = FindResourceW(inst, MAKEINTRESOURCEW(IDR_BANNER), (LPCWSTR)RT_RCDATA);
    DWORD size = res ? SizeofResource(inst, res) : 0;
    const void *data = res ? LockResource(LoadResource(inst, res)) : NULL;
    HGLOBAL mem;
    IStream *stream = NULL;
    IPicture *pic = NULL;
    if (!data || !size) return;
    mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!mem) return;
    memcpy(GlobalLock(mem), data, size);
    GlobalUnlock(mem);
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream))) { GlobalFree(mem); return; }
    if (SUCCEEDED(OleLoadPicture(stream, (LONG)size, FALSE, &IID_IPicture, (void **)&pic)) && pic) {
        OLE_HANDLE h = 0;
        if (SUCCEEDED(pic->lpVtbl->get_Handle(pic, &h)) && h) {
            BITMAP bm;
            g_banner = (HBITMAP)CopyImage((HANDLE)(UINT_PTR)h, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            if (g_banner && GetObject(g_banner, sizeof(bm), &bm)) { g_bannerW = bm.bmWidth; g_bannerH = bm.bmHeight; }
        }
        pic->lpVtbl->Release(pic);
    }
    stream->lpVtbl->Release(stream);
}

static HFONT MakeFont(int px, int weight, const wchar_t *face)
{
    return CreateFontW(-P(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdLine, int show)
{
    WNDCLASSEXW wc;
    HWND wnd;
    MSG msg;
    RECT r;
    HDC screen;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    char path[MAX_PATH], *slash;
    const char *appdata = getenv("APPDATA");
    (void)prev;

    GetModuleFileNameA(NULL, path, sizeof(path));
    slash = strrchr(path, '\\');
    if (slash) *slash = 0;
    strcpy(g_dir, path);
    {
        // started through a link in the game folder: Windows reports the link's target as the
        // module, so look in the folder it was started from too
        char cwd[MAX_PATH];
        _snprintf(path, sizeof(path), "%s\\CoD2SP_s.exe", g_dir);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES && GetCurrentDirectoryA(sizeof(cwd), cwd)) {
            _snprintf(path, sizeof(path), "%s\\CoD2SP_s.exe", cwd);
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) strcpy(g_dir, cwd);
        }
    }
    if (cmdLine && wcsstr(cmdLine, L"/exportpanel")) {   // the installer: only the supporters' panel image, no window
        _snprintf(g_userIni, sizeof(g_userIni), "%s\\CoD2-VR\\settings.ini", appdata ? appdata : g_dir);
        LoadSupporters();
        ExportSupporterPanel();
        return 0;
    }
    if (cmdLine && wcsstr(cmdLine, L"/fontpack"))   // run again elevated by the Text font row: do it and quit
        return wcsstr(cmdLine, L"/fontpack on") ? !BuildFontPack() : !RemoveFontPack();
    if (cmdLine && wcsstr(cmdLine, L"/handspack"))   // run again elevated by the Player hands row: do it and quit
        return wcsstr(cmdLine, L"hide") ? !BuildHandsPack() : !RemoveHandsPack();
    _snprintf(g_gameIni, sizeof(g_gameIni), "%s\\cod2_vr.ini", g_dir);
    _snprintf(g_userIni, sizeof(g_userIni), "%s\\CoD2-VR\\settings.ini", appdata ? appdata : g_dir);
    _snprintf(path, sizeof(path), "%s\\CoD2SP_s.exe", g_dir);
    g_haveGame = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    _snprintf(path, sizeof(path), "%s\\d3d9.dll", g_dir);
    g_haveMod = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    LoadAll();
    LoadSupporters();

    screen = GetDC(NULL);
    g_scale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, screen);
    if (g_scale < 1.0f) g_scale = 1.0f;

    CoInitialize(NULL);
    LoadBanner(inst);
    g_fTitle = MakeFont(30, FW_BOLD, L"Segoe UI");
    g_fNav   = MakeFont(19, FW_BOLD, L"Segoe UI");
    g_fPlay  = MakeFont(24, FW_HEAVY, L"Segoe UI");
    g_fText  = MakeFont(17, FW_NORMAL, L"Segoe UI");
    g_fBold  = MakeFont(17, FW_BOLD, L"Segoe UI");
    g_fSmall = MakeFont(14, FW_NORMAL, L"Segoe UI");
    ExportSupporterPanel();   // the list for the game's main menu (the mod shows it beside the menu in the headset)

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_LAUNCHER));
    wc.lpszClassName = L"CoD2VRLauncher";
    RegisterClassExW(&wc);

    SetRect(&r, 0, 0, P(BASE_W), P(BASE_H));
    AdjustWindowRect(&r, style, FALSE);
    wnd = CreateWindowExW(0, wc.lpszClassName, L"Call of Duty 2 VR", style,
                          (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2,
                          (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2,
                          r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (!wnd) return 1;
    ShowWindow(wnd, show);
    UpdateWindow(wnd);
    SetTimer(wnd, 1, 1000, NULL);   // PLAY comes back when the game is closed
    CloseHandle(CreateThread(NULL, 0, UpdateCheckThread, wnd, 0, NULL));
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
