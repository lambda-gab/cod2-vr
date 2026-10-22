// Controllers: game buttons, walking, turning and the menu pointer (controls.c).
#pragma once
#include "cod2_vr.h"

enum { BT_PTRIGGER, BT_PGRIP, BT_STRIGGER, BT_SGRIP, BT_A, BT_B, BT_X, BT_Y, BT_LCLICK, BT_RCLICK,
       BT_MENU, BT_STICKUP, BT_STICKDOWN, BT_COUNT };

void Controls_LoadConfig(int (*getInt)(const char *, const char *, int), float (*getFloat)(const char *, const char *, float),
                         void (*getStr)(const char *, const char *, const char *, char *, int));
void Controls_SetWindow(HWND w);
int  Controls_Install(void);   // stick movement into the usercmd
// Once per headset frame (main thread): reads the controllers and acts. menuScreen is where the
// menu screen is (tracking space, Quake axes), w x h metres.
void Controls_Frame(float dt, const vr_pose_t *menuScreen, float w, float h);
int  Controls_InMenu(void);
void Controls_TestCursor(int x, int y);   // diagnostics
