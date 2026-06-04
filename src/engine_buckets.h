/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_buckets.h
 *     Bucket-list type definitions for the 3D render pipeline.
 * @par Purpose:
 *     Exposes the QKinds enum, BasicQ base struct, and all BucketKind*
 *     sub-structs so that both the C rasteriser (engine_render.c) and the
 *     C++ GPU renderer (GLWorldViewRenderer.cpp, VitaGPUBackend.cpp) can
 *     operate on the same bucket types without duplication.
 * @par Comment:
 *     This header must remain valid as both C (engine_render.c) and C++.
 *     Keep all declarations C89-compatible (no // comments inside structs,
 *     no designated initialisers, etc.).
 */
/******************************************************************************/
#ifndef DK_ENGINE_BUCKETS_H
#define DK_ENGINE_BUCKETS_H

#include "bflib_render_types.h"  /* PolyPoint */
#include "engine_render.h"       /* BUCKETS_COUNT, XYZ */

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/

typedef unsigned char QKind;

enum QKinds {
    QK_PolygonStandard    = 0,
    QK_PolygonSimple      = 1,
    QK_PolyMode0          = 2,
    QK_PolyMode4          = 3,
    QK_TrigMode2          = 4,
    QK_PolyMode5          = 5,
    QK_TrigMode3          = 6,
    QK_TrigMode6          = 7,
    QK_RotableSprite      = 8,
    QK_PolygonNearFP      = 9,
    QK_BasicPolygon       = 10,
    QK_JontySprite        = 11,
    QK_CreatureShadow     = 12,
    QK_SlabSelector       = 13,
    QK_CreatureStatus     = 14,
    QK_TextureQuad        = 15,
    QK_FloatingGoldText   = 16,
    QK_RoomFlagBottomPole = 17,
    QK_JontyISOSprite     = 18,
    QK_RoomFlagStatusBox  = 19,
    QK_ListEnd            = 20,
};

struct BasicQ {
    struct BasicQ *next;
    QKind kind;
};

/* ------------------------------------------------------------------
 * Textured triangle types — vertices are PolyPoint (fixed-point 16:16)
 * ------------------------------------------------------------------ */
struct BucketKindPolygonStandard {
    struct BasicQ b;
    unsigned short block;
    struct PolyPoint vertex_first;
    struct PolyPoint vertex_second;
    struct PolyPoint vertex_third;
    long camera_z_first;   // camera-space Z for perspective-correct GPU interpolation
    long camera_z_second;
    long camera_z_third;
};

struct BucketKindPolygonSimple {
    struct BasicQ b;
    unsigned short block;
    struct PolyPoint vertex_first;
    struct PolyPoint vertex_second;
    struct PolyPoint vertex_third;
};

struct BucketKindPolygonNearFP {
    struct BasicQ b;
    unsigned char subtype;
    unsigned short block;
    struct PolyPoint vertex_first;
    struct PolyPoint vertex_second;
    struct PolyPoint vertex_third;
    struct XYZ coordinate_first;
    struct XYZ coordinate_second;
    struct XYZ coordinate_third;
};

struct BucketKindBasicUnk10 {
    struct BasicQ b;
    unsigned char color_value;
    struct PolyPoint vertex_first;
    struct PolyPoint vertex_second;
    struct PolyPoint vertex_third;
};

/* ------------------------------------------------------------------
 * Compact triangle types — vertices are unsigned short x/y + unsigned char UV
 * ------------------------------------------------------------------ */
struct BucketKindPolyMode0 {
    struct BasicQ b;
    unsigned char colour;
    unsigned short vertex_first_x;
    unsigned short vertex_first_y;
    unsigned short vertex_second_x;
    unsigned short vertex_second_y;
    unsigned short vertex_third_x;
    unsigned short vertex_third_y;
};

struct BucketKindPolyMode4 {
    struct BasicQ b;
    unsigned char colour;
    unsigned short vertex_first_x;
    unsigned short vertex_first_y;
    unsigned short vertex_second_x;
    unsigned short vertex_second_y;
    unsigned short vertex_third_x;
    unsigned short vertex_third_y;
    unsigned char texture_vertex_first;
    unsigned char texture_vertex_second;
    unsigned char texture_vertex_third;
};

struct BucketKindTrigMode2 {
    struct BasicQ b;
    unsigned short vertex_first_x;
    unsigned short vertex_first_y;
    unsigned short vertex_second_x;
    unsigned short vertex_second_y;
    unsigned short vertex_third_x;
    unsigned short vertex_third_y;
    unsigned char texture_u_first;
    unsigned char texture_v_first;
    unsigned char texture_u_second;
    unsigned char texture_v_second;
    unsigned char texture_u_third;
    unsigned char texture_v_third;
};

struct BucketKindPolyMode5 {
    struct BasicQ b;
    unsigned short vertex_first_x;
    unsigned short vertex_first_y;
    unsigned short vertex_second_x;
    unsigned short vertex_second_y;
    unsigned short vertex_third_x;
    unsigned short vertex_third_y;
    unsigned char texture_u_first;
    unsigned char texture_v_first;
    unsigned char texture_u_second;
    unsigned char texture_v_second;
    unsigned char texture_u_third;
    unsigned char texture_v_third;
    unsigned char texture_w_first;
    unsigned char texture_w_second;
    unsigned char texture_w_third;
};

struct BucketKindTrigMode3 {
    struct BasicQ b;
    unsigned short vertex_first_x;
    unsigned short vertex_first_y;
    unsigned short vertex_second_x;
    unsigned short vertex_second_y;
    unsigned short vertex_third_x;
    unsigned short vertex_third_y;
    unsigned char texture_u_first;
    unsigned char texture_v_first;
    unsigned char texture_u_second;
    unsigned char texture_v_second;
    unsigned char texture_u_third;
    unsigned char texture_v_third;
};

struct BucketKindTrigMode6 {
    struct BasicQ b;
    unsigned short vertex_first_x;
    unsigned short vertex_first_y;
    unsigned short vertex_second_x;
    unsigned short vertex_second_y;
    unsigned short vertex_third_x;
    unsigned short vertex_third_y;
    unsigned char texture_u_first;
    unsigned char texture_v_first;
    unsigned char texture_u_second;
    unsigned char texture_v_second;
    unsigned char texture_u_third;
    unsigned char texture_v_third;
    unsigned char texture_w_first;
    unsigned char texture_w_second;
    unsigned char texture_w_third;
};

/* ------------------------------------------------------------------
 * Sprite / UI bucket types
 * ------------------------------------------------------------------ */
struct BucketKindRotableSprite {
    struct BasicQ b;
    long clip_flags;
    long depth_fade;
};

struct Thing; /* forward-declare; full type in thing.h */

struct BucketKindJontySprite {
    struct BasicQ b;
    struct Thing *thing;
    long scr_x;
    long scr_y;
    long depth_fade;
};

struct BucketKindCreatureShadow {
    struct BasicQ b;
    unsigned short color_value;
    struct PolyPoint vertex_first;
    struct PolyPoint vertex_second;
    struct PolyPoint vertex_third;
    struct PolyPoint vertex_fourth;
    long angle;
    unsigned short anim_sprite;
    unsigned char current_frame;
};

struct BucketKindSlabSelector {
    struct BasicQ b;
    unsigned short color_value;
    struct PolyPoint p;
};

struct BucketKindCreatureStatus {
    struct BasicQ b;
    unsigned char padding[3];
    struct Thing *thing;
    long x;
    long y;
    long z;
};

#define SHADOW_SOURCES_MAX_COUNT 4
struct NearestLights {
    struct Coord3d coord[SHADOW_SOURCES_MAX_COUNT];
};

struct BucketKindTexturedQuad {
    struct BasicQ b;
    unsigned char orient;
    long texture_idx;
    long texture_x;
    long texture_y;
    long zoom_x;
    long zoom_y;
    long shade_intensity0;
    long shade_intensity1;
    long shade_intensity2;
    long shade_intensity3;
    long marked_mode;
};

struct BucketKindFloatingGoldText {
    struct BasicQ b;
    long x;
    long y;
    long lvl;
};

struct BucketKindRoomFlag {
    struct BasicQ b;
    unsigned short lvl;
    long x;
    long y;
};

/******************************************************************************/
/* Bucket list — indexed by Z depth, one linked list per bucket */
extern struct BasicQ *buckets[BUCKETS_COUNT];

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif /* DK_ENGINE_BUCKETS_H */
