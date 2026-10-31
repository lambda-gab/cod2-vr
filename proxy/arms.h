// The soldier's arms following the controllers, on the view weapon's skeleton (arms.c).
#pragma once
#include "cod2_vr.h"

typedef struct {
    float eye[3];           // the player's eyes (world)
    float headFwd[3];       // where the head looks (world)
    float gunAxis[3][3];    // the gun controller's axes (world: forward, left, up)
    float offPos[3];        // the other controller (world)
    float offAxis[3][3];
    int   offValid;         // the other controller is tracked
    int   twoHanded;        // it holds the handguard (its hand stays as the animation has it)
    int   rightHanded;
    float worldScale;       // game units per metre
    float shoulderDown, shoulderSide, shoulderBack;   // cm from the eyes ([Hands])
    float armLength;        // the player's arm, metres, shoulder to wrist
    float offHand[3];       // cm: the wrist bone from the off controller (forward, left, up)
    float dt;
} arms_in_t;

// dobj: the view weapon's; org / ax: its entity's final place (the model space's frame in the world)
void Arms_Pose(BYTE *dobj, const float org[3], const float ax[3][3], const arms_in_t *in);
