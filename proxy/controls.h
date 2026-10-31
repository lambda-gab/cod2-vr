// Controllers: game buttons, walking, turning and the menu pointer (controls.c).
#pragma once
#include "cod2_vr.h"

// BT_STICK*: the stick that does not move you, pushed up / down / left / right
enum { BT_PTRIGGER, BT_PGRIP, BT_STRIGGER, BT_SGRIP, BT_A, BT_B, BT_X, BT_Y, BT_LCLICK, BT_RCLICK,
       BT_MENU, BT_STICKUP, BT_STICKDOWN, BT_STICKLEFT, BT_STICKRIGHT, BT_COUNT };

void Controls_LoadConfig(int (*getInt)(const char *, const char *, int), float (*getFloat)(const char *, const char *, float),
                         void (*getStr)(const char *, const char *, const char *, char *, int));
void Controls_SetWindow(HWND w);
int  Controls_Install(void);   // stick movement into the usercmd
// Once per headset frame (main thread): reads the controllers and acts. menuScreen is where the
// menu screen is (tracking space, Quake axes), w x h metres.
void Controls_Frame(float dt, const vr_pose_t *menuScreen, float w, float h);
int  Controls_InMenu(void);
void Controls_TestCursor(int x, int y);   // diagnostics
int  Controls_DominantHand(void);          // 1 = right, 0 = left
int  Controls_GunHand(vr_pose_t *out);     // the gun's pose this frame (both hands: towards the off hand), 0 = not tracked
int  Controls_TwoHanded(void);             // the off hand holds the handguard
float Controls_MoveAmount(void);           // how far the move stick is pushed, 0 .. 1
float Controls_TurnRate(void);             // smooth turning this frame, degrees per second
void Controls_SkipCinematic(void);        // the flat game's skip of a movie (the right mouse button)
void Controls_ScreenPress(void);          // a press on a loading screen / video: a click and Enter (it goes on)
void Controls_Command(const char *text);   // a console command line ("lowerstance\n")
int  Controls_StanceChanged(void);         // a button changed the stance since the last call
void Controls_SetHead(const vr_pose_t *head);   // this frame's head, for the body zones and the watch gesture
// The objectives are up (+scores held): their screen on the back of the off hand. 0 = not up / hand busy.
int  Controls_ObjectivesPose(const vr_pose_t *head, vr_pose_t *out);
// A grenade was let go from the off hand in the last half second: the direction it was thrown (tracking space).
int  Controls_ThrowDir(float dir[3]);
// A grenade is in a hand (taken at the lower back, until its throw is done): that hand's pose.
int  Controls_GrenadePose(vr_pose_t *out);
// The free hand's trigger is using this entity (held at something usable): the aim goes to it. 0 = none.
int  Controls_UseTarget(void);
int  Controls_ObjectivesHeld(void);
int  Controls_GrenadeHand(void);       // the hand holding a grenade from the body (0 left, 1 right), -1 = none   // +scores is held (a button or the watch gesture)
// The move stick (x, y: -1 .. 1, y up = forward) and the hand it is on (0 left, 1 right); 0 = no controllers or a menu.
int  Controls_MoveStick(float *x, float *y, int *hand);
int  Controls_HandPose(int h, vr_pose_t *out);   // a hand's pose this frame (tracking space), 0 = not tracked
