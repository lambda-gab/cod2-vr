// arms: the soldier's arms follow the controllers (as the CoD 1 mod's bone-driven arms, HANDS_EXPERIMENT.md there), on
// the view weapon's own skeleton. Only seen when the arms are not hidden (no main\zz_cod2vr.iwd, launcher Player hands
// = Shown).
//
// CoD2's view weapon is a DObj (weaponInfo 0xf48d80 + weapon * 0x1b8, the DObj at +0): its models (+0x18 count, +0x1c
// the XModel pointers; the hands model first, the gun hung from tag_weapon after it) and its skeleton (+4) of
// DObjAnimMat { quat x y z w, trans xyz, transWeight = 2 / |q|^2 }, 0x20 bytes per bone from skeleton + 0x30, in the
// entity's own frame (model space). The cgame builds it each frame (0x4b6c80 -> DObjCalcSkel 0x472af0) before
// CG_AddViewWeapon places the entity; the renderer skins it after re.AddRefEntityToScene. In between, when the entity's
// place is known (FixViewWeapon), the arm bones are rewritten here.
//
// The hands models (viewmodel_hands_*, 69 bones each, the same rig): tag_torso -> j_shoulder_le / ri -> j_elbow ->
// j_wrist -> the fingers (j_index / mid / ring / pinky / thumb _0 .. _3) and j_flesh; the sleeves (sleave_extra_*,
// sleeve_extra_upper_le), j_sleaveaim and j_wristtwist hang from the elbow. The right arm holds the gun (also when the
// gun is mirrored into the left hand: then it is seen on the left).
//
// The geometry, as in the CoD 1 mod: the shoulders are the player's own (from the eyes, along a torso yaw that follows
// the head slowly); the arm's line is stretched by the model's arm length over the player's, so a straight real arm is
// straight in the game; two-bone IK with the elbow down and out; the upper arm and the forearm are rebuilt from the bend
// plane (no twist at the elbow); each bone group moves rigidly (the forearm with its sleeves, the hand with its fingers).
// The gun hand stays where the animation holds it on the gun: only its arm comes from the real shoulder. The other hand
// goes to its controller, turned as the animation holds it relative to the gun's controller (so a controller held
// parallel to the gun's gives the animation's grip), unless it holds the handguard (then the animation's hand stays).
#define _CRT_SECURE_NO_WARNINGS
#include "arms.h"
#include "cod2_addresses.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define MEM(type, addr) (*(type *)(addr))
#define MAX_BONES 128

typedef struct { float q[4], t[3], tw; } animMat_t;
typedef struct { float c[3][3]; } rot_t;   // c[j] = the rotation of axis j (column j)

enum { G_NONE, G_UPPER, G_FORE, G_HAND };
static struct {
    BYTE *dobj;
    void *models[4];
    int   count;                      // bones in the DObj
    signed char group[2][MAX_BONES];  // per side (0 = le, 1 = ri): the group of each bone
    int   shoulder[2], elbow[2], wrist[2];
    int   ok;
} g_rig;

static const char *BoneName(WORD id)
{
    BYTE *mt = MEM(BYTE *, ADDR_scrMemTreeBuf);
    return id && mt ? (const char *)(mt + id * 8 + 4) : NULL;
}

static int Side(const char *n)
{
    if (strstr(n, "_le")) return 0;
    if (strstr(n, "_ri")) return 1;
    return -1;
}

// The rig's bones, by name (once per DObj and its models).
static int FindRig(BYTE *dobj)
{
    int nModels = MEM(BYTE, dobj + DOBJ_OFS_numModels), m, base = 0, s, b;
    void **models = (void **)(dobj + DOBJ_OFS_models);
    if (nModels < 1 || nModels > 4) return 0;
    if (g_rig.dobj == dobj && !memcmp(g_rig.models, models, nModels * sizeof(void *))) return g_rig.ok;
    memset(&g_rig, 0, sizeof(g_rig));
    g_rig.dobj = dobj;
    memcpy(g_rig.models, models, nModels * sizeof(void *));
    for (s = 0; s < 2; s++) g_rig.shoulder[s] = g_rig.elbow[s] = g_rig.wrist[s] = -1;
    for (m = 0; m < nModels; m++) {
        BYTE *parts = models[m] ? MEM(BYTE *, models[m]) : NULL;
        WORD *names;
        int n;
        if (!parts) return 0;
        n = MEM(short, parts);
        names = MEM(WORD *, MEM(BYTE *, parts + 4));
        if (n <= 0 || base + n > MAX_BONES || !names) return 0;
        for (b = 0; b < n; b++) {
            const char *nm = BoneName(names[b]);
            int i = base + b, side, grp = G_NONE;
            if (!nm) continue;
            side = Side(nm);
            if (side < 0) continue;
            if (!strncmp(nm, "j_shoulder", 10)) { grp = G_UPPER; g_rig.shoulder[side] = i; }
            else if (!strncmp(nm, "j_elbow", 7)) { grp = G_FORE; g_rig.elbow[side] = i; }
            else if (!strncmp(nm, "j_sleaveaim", 11) || !strncmp(nm, "j_wristtwist", 12) || !strncmp(nm, "sleave_extra", 12) ||
                     !strncmp(nm, "sleeve_extra", 12)) grp = G_FORE;
            else if (!strncmp(nm, "j_wrist", 7)) { grp = G_HAND; g_rig.wrist[side] = i; }
            else if (!strncmp(nm, "j_flesh", 7) || !strncmp(nm, "j_index", 7) || !strncmp(nm, "j_mid", 5) || !strncmp(nm, "j_ring", 6) ||
                     !strncmp(nm, "j_pinky", 7) || !strncmp(nm, "j_thumb", 7)) grp = G_HAND;
            g_rig.group[side][i] = (signed char)grp;
        }
        base += n;
    }
    g_rig.count = base;
    g_rig.ok = g_rig.shoulder[0] >= 0 && g_rig.elbow[0] >= 0 && g_rig.wrist[0] >= 0 &&
               g_rig.shoulder[1] >= 0 && g_rig.elbow[1] >= 0 && g_rig.wrist[1] >= 0;
    Log("arms: %s (%d bones in %d models)", g_rig.ok ? "the view weapon has both arms" : "no j_shoulder / j_elbow / j_wrist _le and _ri in the view weapon",
        base, nModels);
    return g_rig.ok;
}

// ---- vector and rotation helpers ----
static float Dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void  Cross3(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
static float Norm3(float v[3])
{
    float l = sqrtf(Dot3(v, v));
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
    return l;
}
static void Apply(const rot_t *r, const float v[3], float o[3])
{
    int i;
    for (i = 0; i < 3; i++) o[i] = r->c[0][i] * v[0] + r->c[1][i] * v[1] + r->c[2][i] * v[2];
}
// a * b
static void Mul(const rot_t *a, const rot_t *b, rot_t *o)
{
    int j;
    for (j = 0; j < 3; j++) Apply(a, b->c[j], o->c[j]);
}
// a * transpose(b)
static void MulT(const rot_t *a, const rot_t *b, rot_t *o)
{
    int j, i;
    for (j = 0; j < 3; j++)
        for (i = 0; i < 3; i++) o->c[j][i] = a->c[0][i] * b->c[0][j] + a->c[1][i] * b->c[1][j] + a->c[2][i] * b->c[2][j];
}
// the game's ConvertQuatToMat (0x420270): axis 0 = (1 - (yy + zz), xy + zw, xz - yw) ..., the products times transWeight
static void QuatToRot(const animMat_t *m, rot_t *r)
{
    float tw = m->tw, x = m->q[0], y = m->q[1], z = m->q[2], w = m->q[3];
    float xx = x * x * tw, yy = y * y * tw, zz = z * z * tw, xy = x * y * tw, xz = x * z * tw, yz = y * z * tw;
    float xw = x * w * tw, yw = y * w * tw, zw = z * w * tw;
    r->c[0][0] = 1 - (yy + zz); r->c[0][1] = xy + zw; r->c[0][2] = xz - yw;
    r->c[1][0] = xy - zw; r->c[1][1] = 1 - (xx + zz); r->c[1][2] = yz + xw;
    r->c[2][0] = xz + yw; r->c[2][1] = yz - xw; r->c[2][2] = 1 - (xx + yy);
}
static void RotToQuat(const rot_t *r, float q[4])
{
    // R[i][j] = c[j][i]
    float t = r->c[0][0] + r->c[1][1] + r->c[2][2], s;
    if (t > 0) {
        s = 0.5f / sqrtf(t + 1.0f);
        q[3] = 0.25f / s;
        q[0] = (r->c[1][2] - r->c[2][1]) * s;
        q[1] = (r->c[2][0] - r->c[0][2]) * s;
        q[2] = (r->c[0][1] - r->c[1][0]) * s;
    } else if (r->c[0][0] > r->c[1][1] && r->c[0][0] > r->c[2][2]) {
        s = 2.0f * sqrtf(1.0f + r->c[0][0] - r->c[1][1] - r->c[2][2]);
        q[3] = (r->c[1][2] - r->c[2][1]) / s;
        q[0] = 0.25f * s;
        q[1] = (r->c[1][0] + r->c[0][1]) / s;
        q[2] = (r->c[2][0] + r->c[0][2]) / s;
    } else if (r->c[1][1] > r->c[2][2]) {
        s = 2.0f * sqrtf(1.0f + r->c[1][1] - r->c[0][0] - r->c[2][2]);
        q[3] = (r->c[2][0] - r->c[0][2]) / s;
        q[0] = (r->c[1][0] + r->c[0][1]) / s;
        q[1] = 0.25f * s;
        q[2] = (r->c[2][1] + r->c[1][2]) / s;
    } else {
        s = 2.0f * sqrtf(1.0f + r->c[2][2] - r->c[0][0] - r->c[1][1]);
        q[3] = (r->c[0][1] - r->c[1][0]) / s;
        q[0] = (r->c[2][0] + r->c[0][2]) / s;
        q[1] = (r->c[2][1] + r->c[1][2]) / s;
        q[2] = 0.25f * s;
    }
}

// A frame from the bend plane of a chain a -> b -> c: x along a -> b, z the plane's normal (pole: when straight).
static void BendFrame(const float a[3], const float b[3], const float c[3], const float pole[3], rot_t *f)
{
    float x[3], d2[3], n[3], y[3];
    int i;
    for (i = 0; i < 3; i++) { x[i] = b[i] - a[i]; d2[i] = c[i] - b[i]; }
    Norm3(x);
    Cross3(x, d2, n);
    if (Norm3(n) < 1e-4f) { Cross3(x, pole, n); if (Norm3(n) < 1e-4f) { n[0] = 0; n[1] = 0; n[2] = 1; } }
    Cross3(n, x, y);
    for (i = 0; i < 3; i++) { f->c[0][i] = x[i]; f->c[1][i] = y[i]; f->c[2][i] = n[i]; }
}

// A frame with x along x, z the normal n made perpendicular to it.
static void FrameXN(const float xIn[3], const float nIn[3], rot_t *f)
{
    float x[3], n[3], y[3];
    int i;
    memcpy(x, xIn, sizeof(x));
    Norm3(x);
    for (i = 0; i < 3; i++) n[i] = nIn[i] - x[i] * Dot3(nIn, x);
    if (Norm3(n) < 1e-4f) { n[0] = 0; n[1] = 0; n[2] = 1; }
    Cross3(n, x, y);
    for (i = 0; i < 3; i++) { f->c[0][i] = x[i]; f->c[1][i] = y[i]; f->c[2][i] = n[i]; }
}

// A rotation g about pivot pOld moved to pNew, applied to every bone of a group (one side).
static void MoveGroup(animMat_t *mats, int side, int grp, const rot_t *g, const float pOld[3], const float pNew[3])
{
    float gq[4], d[3], o[3], len;
    int i, k;
    RotToQuat(g, gq);
    for (i = 0; i < g_rig.count; i++) {
        animMat_t *m = &mats[i];
        float q[4];
        if (g_rig.group[side][i] != grp) continue;
        for (k = 0; k < 3; k++) d[k] = m->t[k] - pOld[k];
        Apply(g, d, o);
        for (k = 0; k < 3; k++) m->t[k] = o[k] + pNew[k];
        len = sqrtf(m->q[0] * m->q[0] + m->q[1] * m->q[1] + m->q[2] * m->q[2] + m->q[3] * m->q[3]);
        if (len < 1e-6f) continue;
        for (k = 0; k < 4; k++) q[k] = m->q[k] / len;
        // gq * q (Hamilton, x y z w)
        m->q[0] = gq[3] * q[0] + gq[0] * q[3] + gq[1] * q[2] - gq[2] * q[1];
        m->q[1] = gq[3] * q[1] - gq[0] * q[2] + gq[1] * q[3] + gq[2] * q[0];
        m->q[2] = gq[3] * q[2] + gq[0] * q[1] - gq[1] * q[0] + gq[2] * q[3];
        m->q[3] = gq[3] * q[3] - gq[0] * q[0] - gq[1] * q[1] - gq[2] * q[2];
        len = m->q[0] * m->q[0] + m->q[1] * m->q[1] + m->q[2] * m->q[2] + m->q[3] * m->q[3];
        m->tw = len > 1e-6f ? 2.0f / len : 2.0f;
    }
}

static void ToModel(const float w[3], const float org[3], const float ax[3][3], float m[3])
{
    float d[3];
    int i;
    for (i = 0; i < 3; i++) d[i] = w[i] - org[i];
    for (i = 0; i < 3; i++) m[i] = Dot3(d, ax[i]);
}
static void DirToModel(const float w[3], const float ax[3][3], float m[3])
{
    int i;
    for (i = 0; i < 3; i++) m[i] = Dot3(w, ax[i]);
}

// One arm: shoulder S (model space, the player's), hand target H and its rotation (NULL = the animation's), pole: where
// the elbow points.
static void PoseArm(animMat_t *mats, int side, const float S[3], const float H[3], const rot_t *handRot, const float pole[3],
                    float realArm)
{
    animMat_t *ms = &mats[g_rig.shoulder[side]], *me = &mats[g_rig.elbow[side]], *mw = &mats[g_rig.wrist[side]];
    float As[3], Ae[3], Aw[3], L1, L2, d[3], dist, k, Se[3], E[3], p[3], a, h, t[3];
    rot_t fa, fn, g1, g2, f2a, f2n, wristRot, g3;
    int i;
    memcpy(As, ms->t, 12); memcpy(Ae, me->t, 12); memcpy(Aw, mw->t, 12);
    for (i = 0; i < 3; i++) t[i] = Ae[i] - As[i];
    L1 = sqrtf(Dot3(t, t));
    for (i = 0; i < 3; i++) t[i] = Aw[i] - Ae[i];
    L2 = sqrtf(Dot3(t, t));
    if (L1 < 1.0f || L2 < 1.0f) return;
    // the line from the hand to the player's shoulder, stretched by the model's arm over the player's
    k = realArm > 1.0f ? (L1 + L2) / realArm : 1.0f;
    if (k < 0.6f) k = 0.6f;
    if (k > 1.6f) k = 1.6f;
    for (i = 0; i < 3; i++) Se[i] = H[i] - (H[i] - S[i]) * k;
    for (i = 0; i < 3; i++) d[i] = H[i] - Se[i];
    dist = Norm3(d);
    if (dist > (L1 + L2) * 0.999f) dist = (L1 + L2) * 0.999f;
    if (dist < fabsf(L1 - L2) + 0.1f) dist = fabsf(L1 - L2) + 0.1f;
    // the elbow: in the plane of the arm's line and the pole
    a = (L1 * L1 - L2 * L2 + dist * dist) / (2.0f * dist);
    h = L1 * L1 - a * a;
    h = h > 0 ? sqrtf(h) : 0;
    for (i = 0; i < 3; i++) p[i] = pole[i] - d[i] * Dot3(pole, d);
    if (Norm3(p) < 1e-4f) { p[0] = 0; p[1] = 0; p[2] = -1; }
    for (i = 0; i < 3; i++) E[i] = Se[i] + d[i] * a + p[i] * h;
    {
        // the hand where the arm ends (the IK may have shortened the line)
        float Hn[3];
        for (i = 0; i < 3; i++) Hn[i] = Se[i] + d[i] * dist;
        // upper arm and forearm from the bend plane
        BendFrame(As, Ae, Aw, pole, &fa);
        BendFrame(Se, E, Hn, p, &fn);
        MulT(&fn, &fa, &g1);
        {
            // the forearm: x along it, the same bend plane's normal
            float xa[3], xn[3];
            for (i = 0; i < 3; i++) { xa[i] = Aw[i] - Ae[i]; xn[i] = Hn[i] - E[i]; }
            FrameXN(xa, fa.c[2], &f2a);
            FrameXN(xn, fn.c[2], &f2n);
        }
        MulT(&f2n, &f2a, &g2);
        // the hand: its own rotation, or carried by the forearm (the gun hand: it stays where the animation has it)
        QuatToRot(mw, &wristRot);
        if (handRot) MulT(handRot, &wristRot, &g3);
        else {
            memset(&g3, 0, sizeof(g3));
            g3.c[0][0] = g3.c[1][1] = g3.c[2][2] = 1;
        }
        MoveGroup(mats, side, G_HAND, &g3, Aw, Hn);
        MoveGroup(mats, side, G_FORE, &g2, Ae, E);
        MoveGroup(mats, side, G_UPPER, &g1, As, Se);
    }
}

static float g_torsoYaw;   // world, degrees: follows the head slowly
static int   g_torsoInit;
static float g_lastPole[2][3];

void Arms_Pose(BYTE *dobj, const float org[3], const float ax[3][3], const arms_in_t *in)
{
    animMat_t *mats;
    BYTE *skel;
    float tf[3], tl[3], up[3] = { 0, 0, 1 }, sh[2][3], cm = in->worldScale / 100.0f, realArm;
    int s, i;
    if (!dobj) return;
    __try {
        if (!FindRig(dobj)) return;
        skel = MEM(BYTE *, dobj + DOBJ_OFS_skel);
        if (!skel) return;
        mats = (animMat_t *)(skel + SKEL_OFS_mats);
        // the torso: the head's yaw, followed slowly (1.5 s) or at once past 35 degrees, so looking around does not move the arms
        {
            float hy = atan2f(in->headFwd[1], in->headFwd[0]) * 57.29578f, dy;
            if (!g_torsoInit) { g_torsoYaw = hy; g_torsoInit = 1; }
            dy = fmodf(hy - g_torsoYaw + 540.0f, 360.0f) - 180.0f;
            if (fabsf(dy) > 35.0f) g_torsoYaw += dy - (dy > 0 ? 35.0f : -35.0f);
            else g_torsoYaw += dy * (in->dt > 0 ? 1.0f - expf(-in->dt / 1.5f) : 0.0f);
        }
        tf[0] = cosf(g_torsoYaw / 57.29578f); tf[1] = sinf(g_torsoYaw / 57.29578f); tf[2] = 0;
        tl[0] = -tf[1]; tl[1] = tf[0]; tl[2] = 0;
        // the player's shoulders (world), from the eyes: down, to the side, back
        for (s = 0; s < 2; s++) {
            float side = s == 0 ? 1.0f : -1.0f;   // 0 = left, 1 = right (of the player)
            for (i = 0; i < 3; i++)
                sh[s][i] = in->eye[i] - up[i] * in->shoulderDown * cm + tl[i] * side * in->shoulderSide * cm - tf[i] * in->shoulderBack * cm;
        }
        realArm = in->armLength * in->worldScale;
        for (s = 0; s < 2; s++) {
            // model side s: 1 (ri) holds the gun, 0 (le) is the other hand. Which of the player's shoulders: the gun hand's side
            int gunArm = s == 1, playerLeft = gunArm ? !in->rightHanded : in->rightHanded;
            float S[3], H[3], pole[3], poleW[3], outW[3];
            rot_t hr, *hrp = NULL;
            ToModel(sh[playerLeft ? 0 : 1], org, ax, S);
            // the elbow: down and out, a little of last frame's side so it does not flip
            for (i = 0; i < 3; i++) outW[i] = tl[i] * (playerLeft ? 1.0f : -1.0f);
            for (i = 0; i < 3; i++) poleW[i] = -up[i] + outW[i] * 0.8f;
            DirToModel(poleW, ax, pole);
            for (i = 0; i < 3; i++) pole[i] += g_lastPole[s][i] * 0.6f;
            Norm3(pole);
            memcpy(g_lastPole[s], pole, sizeof(pole));
            if (gunArm || in->twoHanded || !in->offValid) {
                memcpy(H, mats[g_rig.wrist[s]].t, 12);   // the animation's hand (on the gun)
            } else {
                // the off hand at its controller: the wrist bone OffHand* behind the grip, turned as the animation holds it
                // relative to the gun's controller
                float w[3], fwd[3], lft[3], upc[3];
                rot_t ctrlOff, ctrlGun, animHand;
                for (i = 0; i < 3; i++)
                    w[i] = in->offPos[i] + (in->offAxis[0][i] * in->offHand[0] + in->offAxis[1][i] * in->offHand[1] + in->offAxis[2][i] * in->offHand[2]) * cm;
                ToModel(w, org, ax, H);
                DirToModel(in->offAxis[0], ax, fwd); DirToModel(in->offAxis[1], ax, lft); DirToModel(in->offAxis[2], ax, upc);
                for (i = 0; i < 3; i++) { ctrlOff.c[0][i] = fwd[i]; ctrlOff.c[1][i] = lft[i]; ctrlOff.c[2][i] = upc[i]; }
                DirToModel(in->gunAxis[0], ax, fwd); DirToModel(in->gunAxis[1], ax, lft); DirToModel(in->gunAxis[2], ax, upc);
                for (i = 0; i < 3; i++) { ctrlGun.c[0][i] = fwd[i]; ctrlGun.c[1][i] = lft[i]; ctrlGun.c[2][i] = upc[i]; }
                QuatToRot(&mats[g_rig.wrist[s]], &animHand);
                {
                    // the hand turned with the off controller as the animation holds it relative to the gun's:
                    // (ctrlOff * ctrlGun^T) * animHand
                    rot_t turn;
                    MulT(&ctrlOff, &ctrlGun, &turn);
                    Mul(&turn, &animHand, &hr);
                }
                hrp = &hr;
            }
            PoseArm(mats, s, S, H, hrp, pole, realArm);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged;
        if (!logged) { logged = 1; Log("arms: fault on the view weapon's skeleton, arms left as the animation has them"); }
        g_rig.ok = 0;
    }
}
