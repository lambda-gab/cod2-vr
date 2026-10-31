// controls: the controllers in the game and in its menus.
//  * Buttons run game commands through Cbuf_AddText ("+cmd" is held while pressed, "a|b" runs a
//    on press and b on release, "key:NAME" sends a key to the game window).
//  * The left stick walks (added to the usercmd in CL_FinishMove's place), the right one turns
//    (snap or smooth, into cl.viewangles) and changes stance (up / down).
//  * In menus (cls.keyCatchers & UI) the right hand points at the menu screen: the UI cursor is
//    moved there (CL_MouseEvent with the difference), the trigger clicks, B goes back, A confirms.
#define _CRT_SECURE_NO_WARNINGS
#include "cod2_vr.h"
#include "controls.h"
#include "cod2_addresses.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define MEM(type, addr) (*(type *)(addr))
#define DEG(x)  ((x) * (180.0f / 3.14159265f))

static struct {
    int   rightHanded;   // the pointer and the trigger that fires are the right hand's
    float snapTurn;      // degrees per flick, 0 = smooth
    float turnSpeed;     // smooth turn, degrees per second
    float deadzone;
    int   moveStick;     // 0 = the left stick moves, 1 = the right one, -1 = the off hand's (by RightHanded)
    char  bind[BT_COUNT][64];
    char  hold[BT_COUNT][64];   // <button>Hold: held this long (HOLD_MS) runs this instead, the bind runs on a short press
    int   twoHanded;     // 1 = the off hand's grip in front of the gun hand holds the gun with both hands
    float twoHandReach;  // metres between the hands within which that grip takes the gun
    float twoHandGripHeight;   // metres the off hand holds below the barrel line
    float twoHandStab;   // held with both hands: aim steadiness, 0 .. 1
    float backHeight, hipHeight, lowerBackHeight, chestNear, waistReach, chestWidth;   // body zones, metres from the eyes ([Holsters])
    int   zoneAct[7];    // what a grip does at each body zone (ZONE_* - 1): right / left shoulder, lower back, chest, waist left / right / front (ZA_*)
    float hapHolsters;   // vibration on reaching a body zone and taking from it, 0 .. 1
    int   objGesture;    // 1 = the back of the off hand turned to the eyes shows the objectives
    float aimSmoothing;  // seconds: the one-handed gun direction filter, 0 = off
    int   autoAds;       // 1 = aim down the sights (+speed) while the gun is at the eye
    int   holsterHum;    // 1 = two taps on reaching a body zone (at most once a second), a double click on taking; 0 = one tick
    char  shortcut[4][4][64];   // [A, B, X, Y held][A, B, X, Y pressed]: a wildcard's shortcut ([Buttons] ShortcutYA ...)
} ccfg = { 1, 45.0f, 150.0f, 0.15f, -1 };

enum { ZA_NONE, ZA_PRIMARY, ZA_PRIMARYB, ZA_GRENADE, ZA_SMOKE, ZA_SWITCH, ZA_BINOC };
static const char *const g_zoneActNames[] = { "none", "primary", "primaryb", "grenade", "smokegrenade", "switch", "binocular" };
static int ZoneAct(const char *v, int def)
{
    int i;
    for (i = 0; i < (int)(sizeof(g_zoneActNames) / sizeof(g_zoneActNames[0])); i++) if (!_stricmp(v, g_zoneActNames[i])) return i;
    return def;
}

#define HOLD_MS 500

static const struct { const char *key, *def, *holdDef; } g_buttons[BT_COUNT] = {
    { "PrimaryTrigger", "+attack", "" },
    { "PrimaryGrip", "+activate", "" }, // use / pick up (as the CoD 1 mod)
    { "SecondaryTrigger", "", "" },     // aiming down the sights is the flat game's; the free hand's trigger uses things
    { "SecondaryGrip", "", "" },        // off hand: holds the gun with both hands (TwoHanded); melee is a swing (SwingMelee)
    { "A", "+gostand", "" },
    { "B", "+reload", "" },
    { "X", "+frag", "" },
    { "Y", "weapnext", "" },
    { "LeftStickClick", "vr:recenter", "+scores" },   // press: recenter, hold: objectives
    { "RightStickClick", "toggleprone", "" },
    { "Menu", "key:ESCAPE", "" },
    { "TurnStickUp", "raisestance", "" },
    { "TurnStickDown", "lowerstance", "" },
    { "TurnStickLeft", "vr:turnleft", "" },
    { "TurnStickRight", "vr:turnright", "" },
};

static vr_input_t g_in;
static int    g_btnDown[BT_COUNT];
static DWORD  g_btnAt[BT_COUNT];     // when a button with a hold action went down
static int    g_btnHolding[BT_COUNT];   // ... and its hold action is running
static int    g_menuBtn[8];          // menu: previous state of trigger, A, B, menu, up, down
static int    g_snapReady = 1, g_dirReady[4] = { 1, 1, 1, 1 };
static float  g_moveF, g_moveR;      // -1..1, this frame's stick
static float  g_turnRate;           // smooth turning this frame, degrees per second
static int    g_stanceChanged;
static int    g_inMenu;
static int    g_useEnt, g_useTrigWas;   // the free hand's trigger is using this entity (+activate held); its last state
static int    g_gripUseEnt, g_gripUseHand = -1;   // a grip is using this entity (as the CoD 1 mod's armed mode); which hand
static HWND   g_wnd;

void Controls_LoadConfig(int (*getInt)(const char *, const char *, int), float (*getFloat)(const char *, const char *, float),
                         void (*getStr)(const char *, const char *, const char *, char *, int))
{
    int i;
    char key[64];
    ccfg.rightHanded = getInt("Controllers", "RightHanded", ccfg.rightHanded);
    ccfg.snapTurn = getFloat("Controllers", "SnapTurn", ccfg.snapTurn);
    ccfg.turnSpeed = getFloat("Controllers", "SmoothTurnSpeed", ccfg.turnSpeed);
    ccfg.deadzone = getFloat("Controllers", "Deadzone", ccfg.deadzone);
    ccfg.moveStick = getInt("Controllers", "MoveStick", ccfg.moveStick);
    ccfg.twoHanded = getInt("Controllers", "TwoHanded", 1);
    ccfg.twoHandReach = getFloat("Controllers", "TwoHandReach", 0.7f);
    ccfg.twoHandGripHeight = getFloat("Controllers", "TwoHandGripHeight", 0.04f);
    ccfg.twoHandStab = getFloat("Controllers", "TwoHandStabilization", 0.75f);
    if (ccfg.twoHandStab < 0) ccfg.twoHandStab = 0;
    if (ccfg.twoHandStab > 1) ccfg.twoHandStab = 1;
    ccfg.backHeight = getFloat("Holsters", "BackHeight", -0.30f);
    ccfg.hipHeight = getFloat("Holsters", "HipHeight", -0.75f);
    ccfg.lowerBackHeight = getFloat("Holsters", "LowerBackHeight", -0.70f);
    ccfg.chestNear = getFloat("Holsters", "ChestNear", -0.12f);
    ccfg.waistReach = getFloat("Holsters", "WaistReach", 0.32f);
    ccfg.chestWidth = getFloat("Holsters", "ChestWidth", 0.30f);
    {
        char v[32];
        getStr("Holsters", "RightShoulder", "primary", v, sizeof(v)); ccfg.zoneAct[0] = ZoneAct(v, ZA_PRIMARY);
        getStr("Holsters", "LeftShoulder", "primaryb", v, sizeof(v)); ccfg.zoneAct[1] = ZoneAct(v, ZA_PRIMARYB);
        getStr("Holsters", "LowerBack", "grenade", v, sizeof(v));     ccfg.zoneAct[2] = ZoneAct(v, ZA_GRENADE);
        getStr("Holsters", "Chest", "binocular", v, sizeof(v));       ccfg.zoneAct[3] = ZoneAct(v, ZA_BINOC);
        getStr("Holsters", "WaistLeft", "smokegrenade", v, sizeof(v)); ccfg.zoneAct[4] = ZoneAct(v, ZA_SMOKE);
        getStr("Holsters", "WaistRight", "none", v, sizeof(v));       ccfg.zoneAct[5] = ZoneAct(v, ZA_NONE);
        getStr("Holsters", "WaistFront", "none", v, sizeof(v));       ccfg.zoneAct[6] = ZoneAct(v, ZA_NONE);
    }
    ccfg.hapHolsters = getFloat("Controllers", "HapticsHolsters", getFloat("Controllers", "Haptics", 0.6f));
    ccfg.objGesture = getInt("HUD", "ObjectivesGesture", 1);
    ccfg.aimSmoothing = getFloat("Controllers", "AimSmoothing", 0.0f);
    if (ccfg.aimSmoothing < 0) ccfg.aimSmoothing = 0;
    if (ccfg.aimSmoothing > 0.5f) ccfg.aimSmoothing = 0.5f;
    ccfg.autoAds = getInt("Controllers", "AutoADS", 0);
    ccfg.holsterHum = getInt("Controllers", "HolsterHum", 1);
    for (i = 0; i < BT_COUNT; i++) {
        getStr("Buttons", g_buttons[i].key, g_buttons[i].def, ccfg.bind[i], sizeof(ccfg.bind[i]));
        _snprintf(key, sizeof(key), "%sHold", g_buttons[i].key);
        getStr("Buttons", key, g_buttons[i].holdDef, ccfg.hold[i], sizeof(ccfg.hold[i]));
    }
    {
        // Shortcut<held><pressed>; the default (as the CoD 1 mod): the weapon switch on Y + A right-handed, B + X left-handed
        static const char names[] = "ABXY";
        int m, f;
        for (m = 0; m < 4; m++)
            for (f = 0; f < 4; f++) {
                int def = ccfg.rightHanded ? m == 3 && f == 0 : m == 1 && f == 2;
                ccfg.shortcut[m][f][0] = 0;
                if (m == f) continue;
                _snprintf(key, sizeof(key), "Shortcut%c%c", names[m], names[f]);
                getStr("Buttons", key, def ? "vr:switchweapon" : "", ccfg.shortcut[m][f], sizeof(ccfg.shortcut[m][f]));
            }
    }
}

int Controls_DominantHand(void) { return ccfg.rightHanded ? 1 : 0; }

// ---------------------------------------------------------------------------
// Both hands on the gun (as in the CoD 1 mod): while the off hand's grip holds the handguard,
// the barrel points from the gun hand to the off hand; an adaptive filter damps the hands'
// tremor and still follows a deliberate swing.
// ---------------------------------------------------------------------------
static vr_pose_t g_gun;             // the gun's pose this frame (Controls_GunHand)
static int    g_gunValid, g_twoHand, g_offGripWas;
static float  g_fwdSmooth[3];
static int    g_smoothInit;
static int    g_aimInit;            // the one-handed AimSmoothing filter has a direction (restarted after both hands)

static float Norm(float v[3])
{
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-6f) { v[0] /= len; v[1] /= len; v[2] /= len; }
    return len;
}

static void Cross3(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}

// Would the off hand at o take the gun held at d by the handguard: in front of the gun hand,
// within TwoHandReach and close to the barrel's line (20 cm).
static int OffHandOnGun(const vr_pose_t *d, const vr_pose_t *o)
{
    float f[3], dist, dot;
    int i;
    for (i = 0; i < 3; i++) f[i] = o->pos[i] + d->axis[2][i] * ccfg.twoHandGripHeight - d->pos[i];
    dist = Norm(f);
    dot = f[0] * d->axis[0][0] + f[1] * d->axis[0][1] + f[2] * d->axis[0][2];
    if (dist > ccfg.twoHandReach || dot < 0.3f) return 0;
    return dist * sqrtf(1.0f - (dot > 1.0f ? 1.0f : dot * dot)) <= 0.2f;
}

static void UpdateGunPose(int dom, int off, float dt)
{
    const vr_pose_t *d = &g_in.hand[dom], *o = &g_in.hand[off];
    int gripDown = g_in.grip[off] > (g_twoHand ? 0.35f : 0.65f), i;
    float f[3], l[3], up[3];
    g_gunValid = g_in.handValid[dom];
    if (!g_gunValid) { g_twoHand = 0; g_offGripWas = gripDown; g_aimInit = 0; return; }
    g_gun = *d;
    // taken at the press only: a grip pressed away from the gun is the off hand's own button (melee)
    if (!ccfg.twoHanded || !g_in.handValid[off] || !gripDown) g_twoHand = 0;
    else if (!g_offGripWas && !g_twoHand && OffHandOnGun(d, o)) {
        g_twoHand = 1;
        g_smoothInit = 0;
        XR_Haptic(off, 0.4f, 30);
    }
    if (g_twoHand) g_aimInit = 0;
    g_offGripWas = gripDown;
    if (!g_twoHand) {
        // one hand: AimSmoothing, a plain filter of the barrel's direction and its up (steadier aim, a little lag)
        static float fwd[3], upS[3];
        float a;
        if (ccfg.aimSmoothing <= 0 || dt <= 0) { g_aimInit = 0; return; }
        if (!g_aimInit) { memcpy(fwd, d->axis[0], sizeof(fwd)); memcpy(upS, d->axis[2], sizeof(upS)); g_aimInit = 1; }
        a = 1.0f - expf(-dt / ccfg.aimSmoothing);
        for (i = 0; i < 3; i++) { fwd[i] += (d->axis[0][i] - fwd[i]) * a; upS[i] += (d->axis[2][i] - upS[i]) * a; }
        memcpy(f, fwd, sizeof(f));
        if (Norm(f) < 1e-3f) return;
        Cross3(upS, f, l);
        if (Norm(l) < 1e-3f) return;
        Cross3(f, l, up);
        for (i = 0; i < 3; i++) { g_gun.axis[0][i] = f[i]; g_gun.axis[1][i] = l[i]; g_gun.axis[2][i] = up[i]; }
        return;
    }
    for (i = 0; i < 3; i++) f[i] = o->pos[i] + d->axis[2][i] * ccfg.twoHandGripHeight - d->pos[i];
    if (Norm(f) < 0.05f) return;
    if (ccfg.twoHandStab > 0) {
        // small motions (tremor) damped a lot; the further the gun turns from where it is steadied,
        // the quicker it follows
        float ang = 0, k, tau, a;
        if (g_smoothInit) {
            float c = f[0] * g_fwdSmooth[0] + f[1] * g_fwdSmooth[1] + f[2] * g_fwdSmooth[2];
            ang = c >= 1.0f ? 0.0f : DEG(acosf(c < -1.0f ? -1.0f : c));
        } else {
            memcpy(g_fwdSmooth, f, sizeof(f));
            g_smoothInit = 1;
        }
        k = ang / 6.0f;
        if (k > 1) k = 1;
        tau = ccfg.twoHandStab * 0.18f * (1.0f - k) + 0.01f * k;
        a = dt > 0 ? 1.0f - expf(-dt / tau) : 0.0f;
        for (i = 0; i < 3; i++) g_fwdSmooth[i] += (f[i] - g_fwdSmooth[i]) * a;
        memcpy(f, g_fwdSmooth, sizeof(f));
        Norm(f);
    }
    // the gun hand's roll is kept: up = its up made perpendicular to the barrel
    Cross3(d->axis[2], f, l);           // left = up x forward
    if (Norm(l) < 1e-3f) return;
    Cross3(f, l, up);                   // up = forward x left
    for (i = 0; i < 3; i++) { g_gun.axis[0][i] = f[i]; g_gun.axis[1][i] = l[i]; g_gun.axis[2][i] = up[i]; }
}

int Controls_GunHand(vr_pose_t *out)
{
    if (!g_in.active || !g_gunValid) return 0;
    *out = g_gun;
    return 1;
}

int Controls_TwoHanded(void) { return g_in.active && g_gunValid && g_twoHand; }

float Controls_MoveAmount(void) { float m = sqrtf(g_moveF * g_moveF + g_moveR * g_moveR); return m > 1 ? 1 : m; }
float Controls_TurnRate(void) { return g_turnRate; }
int   Controls_StanceChanged(void) { int c = g_stanceChanged; g_stanceChanged = 0; return c; }

// Which stick moves you: MoveStick, or the off hand's.
static int MoveHand(void)
{
    if (ccfg.moveStick == 0 || ccfg.moveStick == 1) return ccfg.moveStick;
    return ccfg.rightHanded ? 0 : 1;
}

// +1 = the action turns right, -1 = left, 0 = an ordinary action
static int TurnOf(const char *cmd)
{
    if (!_stricmp(cmd, "vr:turnright")) return 1;
    if (!_stricmp(cmd, "vr:turnleft")) return -1;
    return 0;
}

void Controls_SetWindow(HWND w) { g_wnd = w; }

// Cbuf_AddText takes the text in EAX.
static void Cbuf(const char *text)
{
    DWORD fn = ADDR_Cbuf_AddText;
    __asm {
        mov eax, text
        call fn
    }
}

// key: the game's key number (Quake 3 numbering: the ASCII ones are the character), 0 = by window message only
static const struct { const char *name; int vk, key; } g_keys[] = {
    { "ESCAPE", VK_ESCAPE, 27 }, { "ENTER", VK_RETURN, 13 }, { "TAB", VK_TAB, 9 }, { "SPACE", VK_SPACE, 32 },
    { "UPARROW", VK_UP, 0 }, { "DOWNARROW", VK_DOWN, 0 }, { "LEFTARROW", VK_LEFT, 0 }, { "RIGHTARROW", VK_RIGHT, 0 },
    { "BACKSPACE", VK_BACK, 127 },
};

// Sys_QueEvent(time in EAX, type, value, value2, ptrLength, ptr): a key event straight into the
// engine's queue (type 1, as the window procedure queues them: 0x45217e), like the CoD 1 mod's
// QueueEvent. Window messages only arrive when the game's window takes the keyboard, which it
// does not while the player is in the headset (the Menu button did not open the menu).
static void QueueKey(int key, int down)
{
    DWORD fn = ADDR_Sys_QueEvent;
    __asm {
        push 0
        push 0
        push down
        push key
        push 1
        xor eax, eax
        call fn
        add esp, 20
    }
}

// The game reads the keyboard from its window's messages (scan code in lParam).
static void SendKey(int vk, int down)
{
    UINT sc = MapVirtualKeyA(vk, 0);
    LPARAM lp = 1 | (sc << 16) | (down ? 0 : (3u << 30));
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT) lp |= 1 << 24;   // extended keys
    if (g_wnd) PostMessageA(g_wnd, down ? WM_KEYDOWN : WM_KEYUP, vk, lp);
}

static void SendClick(int down)
{
    if (g_wnd) PostMessageA(g_wnd, down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? MK_LBUTTON : 0, 0);
}

static void MenuButtonStates(int now[6]);
static void MoveCursorTo(int x, int y);

void Controls_Command(const char *text) { Cbuf(text); }

// A loading screen or a video (no world, no menu; the controllers are read by the keep-alive thread then): the game goes
// on at a click (ClickToContinue posts WM_LBUTTONDOWN itself), Enter for a video that takes a key. The press may still
// be held as the level comes up: it acts there only once released, as leaving a menu.
void Controls_SkipCinematic(void)
{
    __try {
        ((void (__cdecl *)(void))ADDR_SCR_StopCinematic)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("cinematic: fault while stopping it");
    }
}

void Controls_ScreenPress(void)
{
    if (MEM(int, ADDR_cls_keyCatchers) & KEYCATCH_UI) {
        // a menu (pregame.menu's full-screen button, "click to start the mission"): the cursor moved to the middle first,
        // so the button under it has the focus a click needs
        MoveCursorTo(321, 241);
        MoveCursorTo(320, 240);
    }
    SendClick(1); SendClick(0);
    QueueKey(13, 1); QueueKey(13, 0);
    g_inMenu = 1;
    MenuButtonStates(g_menuBtn);   // a trigger still held is not a second click
}

static int g_scoresHeld;   // +scores is held (a button or the watch gesture): the objectives are up

static void RunCommand(const char *bind, int down)
{
    char cmd[80];
    const char *bar;
    if (!bind[0]) return;
    if (!_stricmp(bind, "+scores")) g_scoresHeld = down;
    if (!_strnicmp(bind, "vr:", 3)) {   // the mod's own commands; turning is done by the stick code
        if (down && !TurnOf(bind)) VR_Command(bind + 3);
        return;
    }
    if (down && (strstr(bind, "stance") || strstr(bind, "prone") || strstr(bind, "crouch"))) g_stanceChanged = 1;
    if (!_strnicmp(bind, "key:", 4)) {
        int i;
        for (i = 0; i < (int)(sizeof(g_keys) / sizeof(g_keys[0])); i++)
            if (!_stricmp(bind + 4, g_keys[i].name)) {
                if (g_keys[i].key) QueueKey(g_keys[i].key, down);
                else SendKey(g_keys[i].vk, down);
            }
        return;
    }
    if ((bar = strchr(bind, '|')) != NULL) {          // "on press|on release"
        if (down) _snprintf(cmd, sizeof(cmd), "%.*s\n", (int)(bar - bind), bind);
        else _snprintf(cmd, sizeof(cmd), "%s\n", bar + 1);
    } else if (bind[0] == '+') {
        _snprintf(cmd, sizeof(cmd), "%c%s\n", down ? '+' : '-', bind + 1);
    } else if (down) {
        _snprintf(cmd, sizeof(cmd), "%s\n", bind);
    } else return;
    cmd[sizeof(cmd) - 1] = 0;
    if (cmd[0] && cmd[0] != '\n') Cbuf(cmd);
}

static void RunBind(int b, int down) { RunCommand(ccfg.bind[b], down); }

// A button with a hold action: a short press runs its bind (pressed and released at once), held
// HOLD_MS it runs the hold action until released (UpdateHolds).
static void SetButton(int b, int down)
{
    if (g_btnDown[b] == down) return;
    g_btnDown[b] = down;
    if (!ccfg.hold[b][0]) { RunBind(b, down); return; }
    if (down) {
        g_btnAt[b] = GetTickCount();
        g_btnHolding[b] = 0;
    } else if (g_btnHolding[b]) {
        RunCommand(ccfg.hold[b], 0);
        g_btnHolding[b] = 0;
    } else {
        RunBind(b, 1);
        RunBind(b, 0);
    }
}

static void UpdateHolds(void)
{
    int b;
    for (b = 0; b < BT_COUNT; b++)
        if (ccfg.hold[b][0] && g_btnDown[b] && !g_btnHolding[b] && GetTickCount() - g_btnAt[b] >= HOLD_MS) {
            g_btnHolding[b] = 1;
            RunCommand(ccfg.hold[b], 1);
        }
}

static int Pressed(float v, int b) { return g_btnDown[b] ? v > 0.35f : v > 0.65f; }

// Face buttons (as the CoD 1 mod's UpdateFaceButtons). The two buttons of the hand that does not hold
// the gun (X and Y right-handed, A and B left-handed) are wildcards when a shortcut is set for them:
// tapped alone, a wildcard runs its own bind; held, the next face button pressed runs that pair's
// shortcut (Shortcut<held><pressed>) instead of its own bind. A wildcard whose bind is a "+" command
// held alone WILD_HOLD_MS holds that command (so +frag can still be held). Every other face button
// acts at once, as before.
enum { FACE_A, FACE_B, FACE_X, FACE_Y };
static const int g_faceBt[4] = { BT_A, BT_B, BT_X, BT_Y };
static int   g_faceDown[4];      // the buttons as read
static int   g_faceShortcut[4];  // 1 + the wildcard whose shortcut a pressed button runs (0 = none)
static int   g_shortcutHeld = -1, g_shortcutUsed, g_wildHold;
static DWORD g_wildAt;
#define WILD_HOLD_MS 200

static int IsWildcard(int f)
{
    int k;
    if (ccfg.rightHanded ? f != FACE_X && f != FACE_Y : f != FACE_A && f != FACE_B) return 0;
    for (k = 0; k < 4; k++) if (ccfg.shortcut[f][k][0]) return 1;
    return 0;
}

static void UpdateFaceButtons(const int down[4])
{
    static const char names[] = "ABXY";
    int f;
    for (f = 0; f < 4; f++) {
        if (down[f] == g_faceDown[f]) continue;
        g_faceDown[f] = down[f];
        if (down[f]) {
            if (g_shortcutHeld >= 0 && f != g_shortcutHeld) {
                const char *cmd = ccfg.shortcut[g_shortcutHeld][f];
                if (g_wildHold) { SetButton(g_faceBt[g_shortcutHeld], 0); g_wildHold = 0; }   // it was a hold: the shortcut takes over
                g_faceShortcut[f] = 1 + g_shortcutHeld;
                g_shortcutUsed = 1;
                Log("shortcut %c + %c: %s", names[g_shortcutHeld], names[f], cmd[0] ? cmd : "(nothing)");
                RunCommand(cmd, 1);
            } else if (IsWildcard(f)) {
                g_shortcutHeld = f;
                g_shortcutUsed = 0;
                g_wildHold = 0;
                g_wildAt = GetTickCount();
            } else {
                SetButton(g_faceBt[f], 1);
            }
        } else if (g_faceShortcut[f]) {
            RunCommand(ccfg.shortcut[g_faceShortcut[f] - 1][f], 0);
            g_faceShortcut[f] = 0;
        } else if (f == g_shortcutHeld) {
            if (g_wildHold) { SetButton(g_faceBt[f], 0); g_wildHold = 0; }   // a hold: let go with the button
            else if (!g_shortcutUsed) { SetButton(g_faceBt[f], 1); SetButton(g_faceBt[f], 0); }   // a tap: its own bind
            g_shortcutHeld = -1;
        } else {
            SetButton(g_faceBt[f], 0);
        }
    }
    if (g_shortcutHeld >= 0 && !g_shortcutUsed && !g_wildHold && ccfg.bind[g_faceBt[g_shortcutHeld]][0] == '+' &&
        g_faceDown[g_shortcutHeld] && GetTickCount() - g_wildAt >= WILD_HOLD_MS) {
        g_wildHold = 1;
        SetButton(g_faceBt[g_shortcutHeld], 1);
    }
}

// Auto ADS (as the CoD 1 mod's UpdateAutoAds, off by default): the gun raised to the eye and lined up
// with the view holds +speed (the game's aim down the sights while held), so the game uses its aimed
// accuracy and zoom. Hysteresis against flicker.
static int g_autoAdsOn;

static void UpdateAutoAds(const vr_pose_t *head)
{
    float d[3], dist, c;
    int i, on = 0;
    if (ccfg.autoAds && g_gunValid && head) {
        for (i = 0; i < 3; i++) d[i] = g_gun.pos[i] - head->pos[i];
        dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        c = g_gun.axis[0][0] * head->axis[0][0] + g_gun.axis[0][1] * head->axis[0][1] + g_gun.axis[0][2] * head->axis[0][2];
        on = g_autoAdsOn ? dist < 0.40f && c > 0.94f : dist < 0.32f && c > 0.97f;   // ~20 / ~14 degrees
    }
    if (on == g_autoAdsOn) return;
    g_autoAdsOn = on;
    Cbuf(on ? "+speed\n" : "-speed\n");
}

// Everything let go (menu opened, controllers lost): a short press waiting for its release is
// dropped, not run.
static void ReleaseAll(void)
{
    static const int none[4] = { 0, 0, 0, 0 };
    int b;
    if (g_shortcutHeld >= 0) g_shortcutUsed = 1;   // a wildcard held as everything lets go is no tap
    UpdateFaceButtons(none);                       // a held shortcut lets go too
    if (g_autoAdsOn) { g_autoAdsOn = 0; Cbuf("-speed\n"); }
    if (g_useEnt) { g_useEnt = 0; Cbuf("-activate\n"); }
    if (g_gripUseEnt) { g_gripUseEnt = 0; g_gripUseHand = -1; Cbuf("-activate\n"); }
    for (b = 0; b < BT_COUNT; b++) {
        if (ccfg.hold[b][0] && g_btnDown[b] && !g_btnHolding[b]) { g_btnDown[b] = 0; continue; }
        SetButton(b, 0);
    }
    g_moveF = g_moveR = 0;
    g_turnRate = 0;
}

static float Dead(float v) { return fabsf(v) < ccfg.deadzone ? 0 : (v - (v > 0 ? ccfg.deadzone : -ccfg.deadzone)) / (1 - ccfg.deadzone); }

static void Cross(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
static float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Where the hand's ray meets the screen (pose: axis[0] away from the viewer, axis[1] left,
// axis[2] up; width x height metres): 0..1 across and down and the point, 0 = misses.
static int PointAt(const vr_pose_t *hand, const vr_pose_t *scr, float w, float h, float *u, float *v, float at[3])
{
    float d = Dot(hand->axis[0], scr->axis[0]), rel[3], t, hit[3];
    int i;
    if (d <= 0.01f) return 0;
    for (i = 0; i < 3; i++) rel[i] = scr->pos[i] - hand->pos[i];
    t = Dot(rel, scr->axis[0]) / d;
    if (t <= 0) return 0;
    for (i = 0; i < 3; i++) {
        at[i] = hand->pos[i] + hand->axis[0][i] * t;
        hit[i] = at[i] - scr->pos[i];
    }
    *u = 0.5f - Dot(hit, scr->axis[1]) / w;
    *v = 0.5f - Dot(hit, scr->axis[2]) / h;
    return *u > -0.1f && *u < 1.1f && *v > -0.1f && *v < 1.1f;
}

static void MoveCursorTo(int x, int y)
{
    DWORD fn = ADDR_CL_MouseEvent;
    int mx = x - MEM(int, ADDR_ui_cursorX), my = y - MEM(int, ADDR_ui_cursorY);
    if (!mx && !my) return;
    __asm {
        mov eax, mx
        mov edx, my
        call fn
    }
}

void Controls_TestCursor(int x, int y) { MoveCursorTo(x, y); }

static void MenuButton(int slot, int now, void (*fn)(int, int), int arg)
{
    if (now != g_menuBtn[slot]) { g_menuBtn[slot] = now; fn(arg, now); }
}
static void KeyFn(int vk, int down)
{
    if (vk == VK_ESCAPE) QueueKey(27, down);        // as the Menu button: no window focus needed
    else if (vk == VK_RETURN) QueueKey(13, down);
    else SendKey(vk, down);
}
static void ClickFn(int unused, int down) { (void)unused; SendClick(down); }

// The menu buttons' states now (trigger, A, B, Menu, stick up, stick down), as MenuControls reads them.
static void MenuButtonStates(int now[6])
{
    int dom = ccfg.rightHanded ? 1 : 0;
    now[0] = g_in.trigger[dom] > 0.6f;
    now[1] = (g_in.buttons & VRB_A) != 0;
    now[2] = (g_in.buttons & VRB_B) != 0;
    now[3] = (g_in.buttons & VRB_MENU) != 0;
    now[4] = g_in.stick[dom][1] > 0.7f;
    now[5] = g_in.stick[dom][1] < -0.7f;
}

static void MenuControls(const vr_pose_t *screen, float w, float h)
{
    int dom = ccfg.rightHanded ? 1 : 0, i;
    float u, v, at[3];
    if (g_in.handValid[dom]) {
        const vr_pose_t *hand = &g_in.hand[dom];
        if (PointAt(hand, screen, w, h, &u, &v, at)) {
            int x = (int)(u * 640.0f), y = (int)(v * 480.0f);
            MoveCursorTo(x < 0 ? 0 : x > 640 ? 640 : x, y < 0 ? 0 : y > 480 ? 480 : y);
            XR_SetLaser(hand->pos, at, 1);
        } else {
            for (i = 0; i < 3; i++) at[i] = hand->pos[i] + hand->axis[0][i] * 3.0f;   // pointing away: 3 m of beam
            XR_SetLaser(hand->pos, at, 0);
        }
    }
    MenuButton(0, g_in.trigger[dom] > 0.6f, ClickFn, 0);
    MenuButton(1, (g_in.buttons & VRB_A) != 0, KeyFn, VK_RETURN);
    MenuButton(2, (g_in.buttons & VRB_B) != 0, KeyFn, VK_ESCAPE);
    MenuButton(3, (g_in.buttons & VRB_MENU) != 0, KeyFn, VK_ESCAPE);
    MenuButton(4, g_in.stick[dom][1] > 0.7f, KeyFn, VK_UP);
    MenuButton(5, g_in.stick[dom][1] < -0.7f, KeyFn, VK_DOWN);
}

static int AnyMenuButton(void)
{
    int i;
    for (i = 0; i < 6; i++) if (g_menuBtn[i]) return 1;
    return 0;
}

// The stick that does not move you: each direction runs its action (TurnStickUp/Down/Left/Right);
// vr:turnleft / vr:turnright turn the body (snap or smooth, SnapTurn), the others act once per push.
static void FreeStick(int ts, float dt)
{
    static const int dirBt[4] = { BT_STICKUP, BT_STICKDOWN, BT_STICKLEFT, BT_STICKRIGHT };
    float *va = (float *)ADDR_cl_viewangles;
    float sx = g_in.stick[ts][0], sy = g_in.stick[ts][1], push[4], tin = 0;
    int d, best = -1;
    push[0] = sy > 0 ? sy : 0; push[1] = sy < 0 ? -sy : 0;
    push[2] = sx < 0 ? -sx : 0; push[3] = sx > 0 ? sx : 0;
    for (d = 0; d < 4; d++) {
        tin += TurnOf(ccfg.bind[dirBt[d]]) * push[d];
        if (best < 0 || push[d] > push[best]) best = d;
    }
    g_turnRate = 0;
    if (ccfg.snapTurn > 0) {
        if (g_snapReady && fabsf(tin) > 0.7f) { va[1] -= tin > 0 ? ccfg.snapTurn : -ccfg.snapTurn; g_snapReady = 0; }
        else if (fabsf(tin) < 0.3f) g_snapReady = 1;
    } else {
        float t = Dead(tin > 1 ? 1 : tin < -1 ? -1 : tin);
        va[1] -= t * ccfg.turnSpeed * dt;
        g_turnRate = fabsf(t) * ccfg.turnSpeed;
    }
    // the other directions: once per push, when that direction is clearly the one pushed
    for (d = 0; d < 4; d++) {
        if (TurnOf(ccfg.bind[dirBt[d]])) continue;
        if (g_dirReady[d] && d == best && push[d] > 0.75f) {
            SetButton(dirBt[d], 1);
            SetButton(dirBt[d], 0);
            g_dirReady[d] = 0;
        } else if (push[d] < 0.3f) {
            g_dirReady[d] = 1;
        }
    }
}

// ---------------------------------------------------------------------------
// The body (as the CoD 1 mod's always-armed mode: the gun stays in the dominant hand). Either hand's
// grip at the right shoulder takes weapon 1, at the left shoulder weapon 2 ("weaponslot primary /
// primaryb"); at the lower back it holds a grenade (+frag) that is thrown when the grip lets go.
// Reaching a zone that does something gives a light vibration.
// ---------------------------------------------------------------------------
static const vr_pose_t *g_head;   // this frame's head (Controls_SetHead)
static float g_throwDir[3];       // where the hand threw it (its motion, or where it pointed)
static int   g_grenadeHand = -1;  // the hand holding a grenade (until its throw is done)
static DWORD g_throwUntil;
static float g_handPrev[2][3], g_handVel[2][3];   // the hands' motion, m/s, smoothed
static int g_zoneIn[2], g_zoneGrip[2], g_gripWas[2];
static DWORD g_zoneBuzzAt[2], g_zoneTapTwo[2], g_zoneClickTwo[2];   // HolsterHum: last cue, its second tap, a take's second click

void Controls_SetHead(const vr_pose_t *head) { g_head = head; }

enum { ZONE_NONE, ZONE_SHOULDER_R, ZONE_SHOULDER_L, ZONE_BACK, ZONE_CHEST, ZONE_WAIST_L, ZONE_WAIST_R, ZONE_WAIST_F };

static int HandZone(int h)
{
    const float *p = g_in.hand[h].pos;
    float yaw, c, s, dx, dy, up, fwd, left, reach, chestTop;
    if (!g_head || !g_in.handValid[h]) return ZONE_NONE;
    yaw = atan2f(g_head->axis[0][1], g_head->axis[0][0]); c = cosf(yaw); s = sinf(yaw);
    dx = p[0] - g_head->pos[0]; dy = p[1] - g_head->pos[1]; up = p[2] - g_head->pos[2];
    fwd = dx * c + dy * s; left = -dx * s + dy * c;
    reach = sqrtf(fwd * fwd + left * left);   // reaching out for something on the floor is not a holster
    // the same zones as the CoD 1 mod's HandZone: shoulders, chest, lower back, the waist's left, right and front
    chestTop = ccfg.backHeight + 0.15f;
    if (up > ccfg.backHeight && fwd < 0.05f && fabsf(left) < 0.5f && fabsf(left) > 0.12f && (up > chestTop || fwd < ccfg.chestNear))
        return left > 0 ? ZONE_SHOULDER_L : ZONE_SHOULDER_R;
    // the chest: in front of the body between the shoulders and the waist (not arms held out, fwd < 0.28)
    if (up < chestTop && up > ccfg.hipHeight + 0.2f && fwd > ccfg.chestNear && fwd < 0.28f && fabsf(left) < ccfg.chestWidth)
        return ZONE_CHEST;
    // the hip's sides before the lower back (the two overlapped: a hand at the left hip, a little behind the body's
    // line, was the lower back and took the frag grenade instead of the smoke)
    if (fabsf(up - ccfg.hipHeight) < 0.2f && fwd > -0.10f && fwd < 0.2f && reach < ccfg.waistReach && fabsf(left) > 0.12f && fabsf(left) < 0.5f)
        return left > 0 ? ZONE_WAIST_L : ZONE_WAIST_R;
    if (fabsf(up - ccfg.lowerBackHeight) < 0.32f && fabsf(left) < 0.4f && fwd < -0.03f && fwd > -0.7f && reach < ccfg.waistReach + 0.1f)
        return ZONE_BACK;
    if (fabsf(up - ccfg.hipHeight) < 0.2f && fwd > 0.1f && fwd < 0.5f && reach < ccfg.waistReach && fabsf(left) < 0.12f)
        return ZONE_WAIST_F;
    return ZONE_NONE;
}

static int ZoneAction(int zone) { return zone == ZONE_NONE ? ZA_NONE : ccfg.zoneAct[zone - 1]; }

// Returns 1 when this hand's grip belongs to a zone this frame (its ordinary use is skipped).
static int UpdateZone(int h)
{
    int down = g_in.grip[h] > (g_gripWas[h] ? 0.35f : 0.65f), zone = HandZone(h), act;
    int press = down && !g_gripWas[h];
    DWORD now = GetTickCount();
    g_gripWas[h] = down;
    if (zone != g_zoneIn[h]) {
        g_zoneIn[h] = zone;
        // reaching a zone that does something: HolsterHum = two taps of 45 ms, not again within a second (a
        // hand shivering on the zone's edge does not keep buzzing); otherwise one light tick
        if (ZoneAction(zone) != ZA_NONE && ccfg.hapHolsters > 0) {
            if (!ccfg.holsterHum) XR_Haptic(h, 0.25f * ccfg.hapHolsters, 25);
            else if (now - g_zoneBuzzAt[h] > 1000) {
                XR_Haptic(h, 0.35f * ccfg.hapHolsters, 45);
                g_zoneBuzzAt[h] = now;
                g_zoneTapTwo[h] = now + 100;
            }
        }
    }
    if (g_zoneTapTwo[h] && now >= g_zoneTapTwo[h]) { XR_Haptic(h, 0.35f * ccfg.hapHolsters, 45); g_zoneTapTwo[h] = 0; }
    if (g_zoneClickTwo[h] && now >= g_zoneClickTwo[h]) { XR_Haptic(h, 0.85f * ccfg.hapHolsters, 30); g_zoneClickTwo[h] = 0; }
    if (g_zoneGrip[h]) {
        if (!down) {
            // let go: the grenade taken from the back is thrown now (the game throws at the press of
            // +frag, so it is pressed only here; it flies where the gun aims)
            if (g_zoneGrip[h] == ZA_GRENADE || g_zoneGrip[h] == ZA_SMOKE) {
                float sp = sqrtf(g_handVel[h][0] * g_handVel[h][0] + g_handVel[h][1] * g_handVel[h][1] + g_handVel[h][2] * g_handVel[h][2]);
                int i;
                // thrown with a swing: along it; let go with the hand still: where it points
                for (i = 0; i < 3; i++) g_throwDir[i] = sp > 1.0f ? g_handVel[h][i] / sp : g_in.hand[h].axis[0][i];
                g_throwUntil = GetTickCount() + 500;
            }
            // thrown now; still in that hand while the throw is played (g_throwUntil)
            if (g_zoneGrip[h] == ZA_GRENADE) { Cbuf("-frag\n"); g_grenadeHand = h; }
            if (g_zoneGrip[h] == ZA_SMOKE) { Cbuf("-smoke\n"); g_grenadeHand = h; }
            if (g_zoneGrip[h] == ZA_BINOC) Cbuf("-binoculars\n");   // put away as the grip lets go
            g_zoneGrip[h] = 0;
        }
        return 1;
    }
    act = ZoneAction(zone);
    if (!press || act == ZA_NONE) return 0;
    // something made to be used at the hand (a weapon on a table) comes first, as the CoD 1 mod's armed mode: GripUse
    if (g_in.handValid[h] && VR_UsableNear(g_in.hand[h].pos)) return 0;
    g_zoneGrip[h] = act;
    if (act == ZA_PRIMARY) Cbuf("weaponslot primary\n");
    else if (act == ZA_PRIMARYB) Cbuf("weaponslot primaryb\n");
    else if (act == ZA_SWITCH) Cbuf("weapnext\n");   // the other of the two weapons (as the CoD 1 mod's "switch")
    // the pin is pulled now and the grenade stays in the hand until the grip lets go (the mod makes
    // the game's grenades held: GrenadeHold in cod2_vr.c)
    else if (act == ZA_GRENADE) { Cbuf("+frag\n"); g_grenadeHand = h; }
    else if (act == ZA_SMOKE) { Cbuf("+smoke\n"); g_grenadeHand = h; }
    // the binoculars (as the CoD 1 mod's chest holster): up while the grip holds them
    else if (act == ZA_BINOC) Cbuf("+binoculars\n");
    if (ccfg.hapHolsters > 0) {
        XR_Haptic(h, 0.85f * ccfg.hapHolsters, ccfg.holsterHum ? 30 : 40);
        if (ccfg.holsterHum) g_zoneClickTwo[h] = GetTickCount() + 90;   // taking: a double click
    }
    return 1;
}

// Objectives by gesture: the back of the off hand turned to the eyes (within 55 cm, in front of the
// face, for a quarter of a second) holds +scores; turning it away lets go. The back of the hand is
// the controller's left on the left hand and its right on the right hand.
static int g_gestureOn;

static void HandBack(int h, float out[3])
{
    int i;
    for (i = 0; i < 3; i++) out[i] = h == 0 ? g_in.hand[h].axis[1][i] : -g_in.hand[h].axis[1][i];
}

static void UpdateWatchGesture(int off)
{
    static DWORD since;
    float d[3], back[3], len, facing, ahead;
    int i, want = 0;
    if (ccfg.objGesture && g_head && g_in.handValid[off] && !g_twoHand && !g_zoneGrip[off]) {
        for (i = 0; i < 3; i++) d[i] = g_head->pos[i] - g_in.hand[off].pos[i];
        len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 0.05f && len < 0.55f) {
            for (i = 0; i < 3; i++) d[i] /= len;
            HandBack(off, back);
            facing = back[0] * d[0] + back[1] * d[1] + back[2] * d[2];
            ahead = -(g_head->axis[0][0] * d[0] + g_head->axis[0][1] * d[1] + g_head->axis[0][2] * d[2]);
            want = g_gestureOn ? facing > 0.55f && ahead > 0.2f : facing > 0.8f && ahead > 0.4f;
        }
    }
    if (!want) {
        since = 0;
        if (g_gestureOn) { g_gestureOn = 0; RunCommand("+scores", 0); }
        return;
    }
    if (!g_gestureOn) {
        if (!since) since = GetTickCount();
        else if (GetTickCount() - since > 250) { g_gestureOn = 1; RunCommand("+scores", 1); }
    }
}

// The objectives while they are up: on the back of the off hand (4 cm off it, 6 cm towards the
// wrist), flat on it, upright for the eyes. 0 = not up, or that hand is busy / not tracked.
int Controls_ObjectivesPose(const vr_pose_t *head, vr_pose_t *p)
{
    int off = ccfg.rightHanded ? 0 : 1, i;
    const vr_pose_t *h = &g_in.hand[off];
    float back[3], fw[3], up[3], l[3], d;
    if (!g_scoresHeld || !g_in.active || !g_in.handValid[off] || g_twoHand) return 0;
    HandBack(off, back);
    for (i = 0; i < 3; i++) {
        p->pos[i] = h->pos[i] + back[i] * 0.04f - h->axis[0][i] * 0.06f;
        fw[i] = -back[i];   // away from the viewer: into the hand
    }
    d = head->axis[2][0] * fw[0] + head->axis[2][1] * fw[1] + head->axis[2][2] * fw[2];
    for (i = 0; i < 3; i++) up[i] = head->axis[2][i] - fw[i] * d;   // the eyes' up, laid onto the hand
    if (Norm(up) < 1e-3f) memcpy(up, h->axis[0], sizeof(up));
    Cross3(up, fw, l);
    Norm(l);
    Cross3(fw, l, up);
    memcpy(p->axis[0], fw, sizeof(fw));
    memcpy(p->axis[1], l, sizeof(l));
    memcpy(p->axis[2], up, sizeof(up));
    return 1;
}

int Controls_GrenadePose(vr_pose_t *out)
{
    int h = g_grenadeHand;
    if (h < 0) return 0;
    if (!g_zoneGrip[h] && (!g_throwUntil || GetTickCount() >= g_throwUntil)) { g_grenadeHand = -1; return 0; }
    if (!g_in.active || !g_in.handValid[h]) return 0;
    *out = g_in.hand[h];
    return 1;
}

int Controls_ThrowDir(float dir[3])
{
    if (!g_throwUntil || GetTickCount() >= g_throwUntil) return 0;
    memcpy(dir, g_throwDir, sizeof(g_throwDir));
    return 1;
}

static void UpdateHandMotion(float dt)
{
    int h, i;
    for (h = 0; h < 2; h++) {
        if (!g_in.handValid[h] || dt <= 0) { memset(g_handVel[h], 0, sizeof(g_handVel[h])); continue; }
        for (i = 0; i < 3; i++) {
            float v = (g_in.hand[h].pos[i] - g_handPrev[h][i]) / dt;
            g_handVel[h][i] += (v - g_handVel[h][i]) * 0.5f;
            g_handPrev[h][i] = g_in.hand[h].pos[i];
        }
    }
}


// Either hand's grip at something made to be used (a trigger_use: a weapon on the training's table, a switch, explosives
// to plant), as the CoD 1 mod's armed mode: +activate is held with the aim on it (UpdateViewAngles) until that grip lets
// go. Not a grip taken at a body zone, nor the off hand on the handguard. Anywhere else the gun hand's grip is its own
// button ([Buttons] PrimaryGrip, +activate: the game's use, where the gun points).
static void GripUse(int dom, int zd, int off, int zo)
{
    static int was[2];
    int hands[2], k;
    hands[0] = dom; hands[1] = off;
    if (g_gripUseEnt && g_in.grip[g_gripUseHand] < 0.35f) {
        g_gripUseEnt = 0;
        g_gripUseHand = -1;
        Cbuf("-activate\n");
    }
    for (k = 0; k < 2; k++) {
        int hh = hands[k], down = g_in.grip[hh] > (was[hh] ? 0.35f : 0.65f), press = down && !was[hh], ent;
        was[hh] = down;
        if (!press || g_gripUseEnt || g_useEnt || !g_in.handValid[hh]) continue;
        if (k == 0 ? zd : (zo || g_twoHand)) continue;
        ent = VR_UsableNear(g_in.hand[hh].pos);
        if (!ent) continue;
        g_gripUseEnt = ent;
        g_gripUseHand = hh;
        Cbuf("+activate\n");
        XR_Haptic(hh, 0.4f, 30);
        Log("use: the %s hand's grip uses entity %d", hh == dom ? "gun" : "free", ent);
    }
}

void Controls_Frame(float dt, const vr_pose_t *menuScreen, float w, float h)
{
    int dom = ccfg.rightHanded ? 1 : 0, off = 1 - dom, ms, zd, zo;
    int menu = (MEM(int, ADDR_cls_keyCatchers) & KEYCATCH_UI) != 0;

    XR_GetInput(&g_in);
    g_moveF = g_moveR = 0;
    g_turnRate = 0;
    if (!g_in.active) { ReleaseAll(); return; }
    UpdateGunPose(dom, off, dt);

    if (menu) {
        if (!g_inMenu) {
            // what is held as the menu opens (the Menu button that opened it) acts only after a release:
            // seen as a new press, the Menu button closed the menu it had just opened
            ReleaseAll();
            MenuButtonStates(g_menuBtn);
            g_inMenu = 1;
        }
        MenuControls(menuScreen, w, h);
        return;
    }
    if (g_inMenu) {
        // leaving a menu: buttons still held from it act only once released
        if (AnyMenuButton() || g_in.trigger[0] > 0.3f || g_in.trigger[1] > 0.3f || g_in.buttons) { memset(g_menuBtn, 0, sizeof(g_menuBtn)); return; }
        g_inMenu = 0;
    }

    {
        // a skippable cinematic (the campaign's movies): the trigger or A stops it
        static int cinBtn;
        int h = MEM(int, ADDR_cin_handle), now = g_in.trigger[dom] > 0.6f || (g_in.buttons & VRB_A);
        if (h >= 0 && h < 16 && MEM(BYTE, ADDR_cin_skippable)) {
            if (now && !cinBtn) { Log("cinematic: skipped (the game's own stop)"); Controls_SkipCinematic(); }
            cinBtn = now;
        } else cinBtn = 0;
    }
    SetButton(BT_PTRIGGER, Pressed(g_in.trigger[dom], BT_PTRIGGER));
    // the body's zones first: a grip taken there is not "use", melee or the handguard
    UpdateHandMotion(dt);
    zd = UpdateZone(dom);
    zo = g_twoHand ? 0 : UpdateZone(off);
    if (g_twoHand) g_gripWas[off] = 1;   // the handguard's grip: not a press at a zone when it lets go
    GripUse(dom, zd, off, zo);
    SetButton(BT_PGRIP, !zd && g_gripUseHand != dom && Pressed(g_in.grip[dom], BT_PGRIP));
    UpdateWatchGesture(off);

    {
        // The free hand's trigger is "use" (as the CoD 1 mod's) when that HAND is at something made to be used (a
        // trigger_use: switches, explosives to plant): +activate is held with the aim on it (UpdateViewAngles) and it
        // does nothing else; anywhere else it is its own button (aim down the sights). Taken at the press only.
        int down = g_useEnt ? g_in.trigger[off] > 0.35f : g_in.trigger[off] > 0.65f;
        if (down && !g_useEnt && !g_useTrigWas && !g_btnDown[BT_STRIGGER] && !g_twoHand && g_in.handValid[off]) {
            int ent = VR_UsableNear(g_in.hand[off].pos);
            if (ent) {
                g_useEnt = ent;
                Cbuf("+activate\n");
                XR_Haptic(off, 0.4f, 30);
                Log("use: the free hand's trigger uses entity %d", ent);
            }
        } else if (!down && g_useEnt) {
            g_useEnt = 0;
            Cbuf("-activate\n");
        }
        g_useTrigWas = down;
        SetButton(BT_STRIGGER, !g_useEnt && Pressed(g_in.trigger[off], BT_STRIGGER));
    }
    SetButton(BT_SGRIP, !g_twoHand && !zo && g_gripUseHand != off && Pressed(g_in.grip[off], BT_SGRIP));   // on the handguard it holds the gun
    {
        int face[4];
        face[FACE_A] = (g_in.buttons & VRB_A) != 0;
        face[FACE_B] = (g_in.buttons & VRB_B) != 0;
        face[FACE_X] = (g_in.buttons & VRB_X) != 0;
        face[FACE_Y] = (g_in.buttons & VRB_Y) != 0;
        UpdateFaceButtons(face);
    }
    UpdateAutoAds(g_head);
    SetButton(BT_LCLICK, (g_in.buttons & VRB_LSTICK) != 0);
    SetButton(BT_RCLICK, (g_in.buttons & VRB_RSTICK) != 0);
    SetButton(BT_MENU, (g_in.buttons & VRB_MENU) != 0);
    UpdateHolds();
    (void)off;

    // walking: the move stick, relative to where the head points (AddVrMove)
    ms = MoveHand();
    g_moveF = Dead(g_in.stick[ms][1]);
    g_moveR = Dead(g_in.stick[ms][0]);
    // turning, stance and the rest: the other stick
    FreeStick(1 - ms, dt);
}

int Controls_InMenu(void) { return g_inMenu; }

// ---------------------------------------------------------------------------
// Key hints (as the CoD 1 mod's HookKeyBindingText): "Press [{+activate}] to use" names the controller button that
// runs the command instead of the keyboard key. The game's binding text function (0x4cdbf0: command in ECX, out[256] in
// ESI, returns how many keys are bound; the cgame's text parser and its hints call it) is detoured; a command no
// controller button runs keeps the game's text.
// ---------------------------------------------------------------------------
static const char *ButtonName(int b)
{
    int right = MoveHand() == 0;   // the stick that does not move you is the right one
    switch (b) {
    case BT_PTRIGGER:  return ccfg.rightHanded ? "RIGHT TRIGGER" : "LEFT TRIGGER";
    case BT_PGRIP:     return ccfg.rightHanded ? "RIGHT GRIP" : "LEFT GRIP";
    case BT_STRIGGER:  return ccfg.rightHanded ? "LEFT TRIGGER" : "RIGHT TRIGGER";
    case BT_SGRIP:     return ccfg.rightHanded ? "LEFT GRIP" : "RIGHT GRIP";
    case BT_A:         return "A";
    case BT_B:         return "B";
    case BT_X:         return "X";
    case BT_Y:         return "Y";
    case BT_LCLICK:    return "LEFT STICK CLICK";
    case BT_RCLICK:    return "RIGHT STICK CLICK";
    case BT_MENU:      return "MENU";
    case BT_STICKUP:   return right ? "RIGHT STICK UP" : "LEFT STICK UP";
    case BT_STICKDOWN: return right ? "RIGHT STICK DOWN" : "LEFT STICK DOWN";
    case BT_STICKLEFT: return right ? "RIGHT STICK LEFT" : "LEFT STICK LEFT";
    case BT_STICKRIGHT: return right ? "RIGHT STICK RIGHT" : "LEFT STICK RIGHT";
    }
    return "?";
}

// Does a button's command line run cmd? ("toggleads|leaveads" runs toggleads; the game's hints ask for +speed, the
// hold-to-aim command, which the mod's aim-down-sights button stands for)
static int BindRuns(const char *bind, const char *cmd)
{
    size_t n = strcspn(bind, "|");
    if (!bind[0]) return 0;
    if (strlen(cmd) == n && !_strnicmp(bind, cmd, n)) return 1;
    return !_stricmp(cmd, "+speed") && !_strnicmp(bind, "toggleads", 9);
}

static int __cdecl KeyBindingText(const char *cmd, char *out)
{
    char text[256] = "";
    int b, n = 0, f, m;
    static const char faces[] = "ABXY";
    if (!g_in.active || !cmd || !cmd[0]) return -1;
    if (!_stricmp(cmd, "+forward") || !_stricmp(cmd, "+back") || !_stricmp(cmd, "+moveleft") || !_stricmp(cmd, "+moveright")) {
        _snprintf(text, sizeof(text), "%s STICK", MoveHand() ? "RIGHT" : "LEFT");
        n = 1;
    }
    for (b = 0; b < BT_COUNT && !n; b++) {
        const char *name = ButtonName(b);
        if (BindRuns(ccfg.bind[b], cmd) || BindRuns(ccfg.hold[b], cmd)) {
            size_t len = strlen(text);
            _snprintf(text + len, sizeof(text) - len, "%s%s%s", n ? " or " : "", BindRuns(ccfg.bind[b], cmd) ? "" : "HOLD ", name);
            n++;
        }
    }
    for (m = 0; m < 4 && n < 3; m++)   // a shortcut: the wildcard held, then the other button
        for (f = 0; f < 4; f++)
            if (IsWildcard(m) && BindRuns(ccfg.shortcut[m][f], cmd)) {
                size_t len = strlen(text);
                _snprintf(text + len, sizeof(text) - len, "%s%c + %c", n ? " or " : "", faces[m], faces[f]);
                n++;
            }
    if (!_stricmp(cmd, "+frag") || !_stricmp(cmd, "+smoke")) {
        // the body zone that holds that grenade
        static const char *const where[7] = { "THE RIGHT SHOULDER", "THE LEFT SHOULDER", "THE LOWER BACK", "THE CHEST",
                                              "THE LEFT HIP", "THE RIGHT HIP", "THE FRONT OF THE WAIST" };
        int z, want = !_stricmp(cmd, "+frag") ? ZA_GRENADE : ZA_SMOKE;
        for (z = 0; z < 7 && n < 3; z++)
            if (ccfg.zoneAct[z] == want) {
                size_t len = strlen(text);
                _snprintf(text + len, sizeof(text) - len, "%sGRIP AT %s", n ? " or " : "", where[z]);
                n++;
            }
    }
    if (!n) return -1;
    text[sizeof(text) - 1] = 0;
    strncpy(out, text, 255);
    out[255] = 0;
    {
        static int logged;
        if (logged < 8) { logged++; Log("key hint: [%s] -> %s", cmd, text); }
    }
    return n;
}

static __declspec(naked) void hk_KeyBinding(void)
{
    __asm {
        pushad
        push esi
        push ecx
        call KeyBindingText
        add esp, 8
        cmp eax, -1
        je fallback
        mov [esp + 28], eax      // pushad's EAX: the count is returned
        popad
        ret
    fallback:
        popad
        sub esp, 0x100           // the 6 bytes moved here
        mov eax, ADDR_KeyBindingText + 6
        jmp eax
    }
}

static void InstallKeyHints(void)
{
    static const BYTE pro[6] = { 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00 };   // sub esp, 100h
    BYTE *p = (BYTE *)ADDR_KeyBindingText;
    DWORD old;
    if (p[0] == 0xE9) return;   // done (a new device after vid_restart)
    if (memcmp(p, pro, 6)) { Log("key hints: unexpected bytes at the binding text function, keyboard names kept"); return; }
    VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    MEM(DWORD, p + 1) = (DWORD)hk_KeyBinding - ((DWORD)p + 5);
    p[5] = 0x90;
    VirtualProtect(p, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 6);
}

// The move stick (-1 .. 1, up = forward) and the hand it is on; 0 = no controllers.
int Controls_MoveStick(float *x, float *y, int *hand)
{
    int ms = MoveHand();
    if (!g_in.active || g_inMenu) return 0;
    *x = g_in.stick[ms][0];
    *y = g_in.stick[ms][1];
    *hand = ms;
    return 1;
}

int Controls_HandPose(int h, vr_pose_t *out)
{
    if (!g_in.active || h < 0 || h > 1 || !g_in.handValid[h]) return 0;
    *out = g_in.hand[h];
    return 1;
}
int Controls_UseTarget(void) { return g_in.active ? (g_useEnt ? g_useEnt : g_gripUseEnt) : 0; }
int Controls_ObjectivesHeld(void) { return g_scoresHeld; }
int Controls_GrenadeHand(void) { return g_grenadeHand; }

static signed char ClampMove(int v) { return (signed char)(v > 127 ? 127 : v < -127 ? -127 : v); }

// CL_FinishMove(EBX = usercmd): the stick is added to the keyboard's movement first. The game
// moves relative to the aim (the gun hand); the stick is meant relative to the head, so it is
// turned by the yaw between them.
static void __cdecl AddVrMove(BYTE *cmd)
{
    float d, c, s, f, r;
    VR_OnCreateCmd();
    if (!VR_StickWalks()) return;   // Blink / Shift: the stick aims a jump, it does not walk
    if (g_moveF == 0 && g_moveR == 0) {
        // roomscale: the soldier walks to where the real head went (VR_RoomMove, already relative to the game's view)
        if (VR_RoomMove(&f, &r)) {
            cmd[USERCMD_forwardmove] = ClampMove((signed char)cmd[USERCMD_forwardmove] + (int)(f * 127.0f));
            cmd[USERCMD_rightmove] = ClampMove((signed char)cmd[USERCMD_rightmove] + (int)(r * 127.0f));
        }
        return;
    }
    d = VR_MoveYawOffset() * (3.14159265f / 180.0f);
    c = cosf(d); s = sinf(d);
    f = g_moveF * c + g_moveR * s;
    r = g_moveR * c - g_moveF * s;
    cmd[USERCMD_forwardmove] = ClampMove((signed char)cmd[USERCMD_forwardmove] + (int)(f * 127.0f));
    cmd[USERCMD_rightmove] = ClampMove((signed char)cmd[USERCMD_rightmove] + (int)(r * 127.0f));
}

static __declspec(naked) void hk_FinishMove(void)
{
    __asm {
        pushad
        push ebx
        call AddVrMove
        add esp, 4
        popad
        mov eax, ADDR_CL_FinishMove
        jmp eax
    }
}

int Controls_Install(void)
{
    DWORD site = ADDR_CL_FinishMove_call, old;
    InstallKeyHints();
    if (MEM(BYTE, site) != 0xE8 || site + 5 + MEM(DWORD, site + 1) != ADDR_CL_FinishMove) {
        if (site + 5 + MEM(DWORD, site + 1) == (DWORD)hk_FinishMove) return 1;
        Log("controls: CL_FinishMove call not found, no stick movement");
        return 0;
    }
    VirtualProtect((void *)site, 5, PAGE_EXECUTE_READWRITE, &old);
    MEM(DWORD, site + 1) = (DWORD)hk_FinishMove - (site + 5);
    VirtualProtect((void *)site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)site, 5);
    return 1;
}
