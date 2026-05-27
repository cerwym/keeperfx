/******************************************************************************/
// KeeperFX — opaque per-player camera store.
/******************************************************************************/
/** @file kfx/engine/cameras.h
 *     Manages all player camera slots (isometric, first-person, parchment,
 *     front-view) outside of PlayerInfo.
 *
 *     Internal storage is opaque — callers see only struct Camera* pointers
 *     returned by the accessor functions.  The raw PlayerCameraSet layout
 *     is never exposed through this header.
 *
 *     Save / load uses a versioned TLV chunk (SGC_CameraState) via the
 *     save_fields machinery.  Field tags are local to this chunk:
 *     adding a tag here never touches any other module.
 */
/******************************************************************************/
#ifndef KFX_ENGINE_CAMERAS_H
#define KFX_ENGINE_CAMERAS_H

#include "engine_camera.h"   /* struct Camera, CamIV_* enum */
#include "bflib_basics.h"
#include "bflib_fileio.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Per-chunk TLV tag enum — local to this module.                              */
/******************************************************************************/
enum CameraFieldTag {
    CAMF_PLAYER_BEGIN = 0x01,  /* u8:  player index                          */
    CAMF_PLAYER_END   = 0x02,  /* —    no payload (sentinel)                 */
    CAMF_ACTIVE_IDX   = 0x03,  /* u8:  active slot index (CamIV_*)           */
    CAMF_SLOT_BEGIN   = 0x04,  /* u8:  slot index (CamIV_*)                  */
    CAMF_SLOT_END     = 0x05,  /* —    no payload (sentinel)                 */
    CAMF_MAPPOS_X     = 0x06,  /* s32: mappos.x.val                          */
    CAMF_MAPPOS_Y     = 0x07,  /* s32: mappos.y.val                          */
    CAMF_MAPPOS_Z     = 0x08,  /* s32: mappos.z.val                          */
    CAMF_VIEW_MODE    = 0x09,  /* u8:  view_mode                             */
    CAMF_ROT_X        = 0x0A,  /* s32: rotation_angle_x                      */
    CAMF_ROT_Y        = 0x0B,  /* s32: rotation_angle_y                      */
    CAMF_ROT_Z        = 0x0C,  /* s32: rotation_angle_z                      */
    CAMF_HORIZ_FOV    = 0x0D,  /* s32: horizontal_fov                        */
    CAMF_ZOOM         = 0x0E,  /* s32: zoom                                  */
    CAMF_INERTIA_ROT  = 0x0F,  /* s32: inertia_rotation                      */
    CAMF_INERTIA_X    = 0x10,  /* s32: inertia_x                             */
    CAMF_INERTIA_Y    = 0x11,  /* s32: inertia_y                             */
};

/******************************************************************************/
/* Accessors                                                                   */
/******************************************************************************/

/** Return a pointer to the requested camera slot for a player.
 *  cam_idx is a CamIV_* value.  Never returns NULL. */
struct Camera* camera_get_slot(int plyr_idx, int cam_idx);

/** Return a pointer to the currently active camera for a player.
 *  Never returns NULL. */
struct Camera* camera_get_active(int plyr_idx);

/** Return the CamIV_* index of the currently active camera slot. */
int camera_get_active_idx(int plyr_idx);

/** Set the active camera slot.  cam_idx must be a CamIV_* value. */
void camera_set_active(int plyr_idx, int cam_idx);

/** Initialise all camera slots for a player to sensible defaults.
 *  Must be called before rendering or packet processing for that player. */
void camera_init_player(int plyr_idx);

/** Returns true after camera_init_player() has been called for plyr_idx. */
TbBool camera_is_active(int plyr_idx);

/******************************************************************************/
/* Save / load                                                                 */
/******************************************************************************/

/** Write a complete SGC_CameraState chunk (header + TLV fields) to fh.
 *  Returns false on I/O error. */
TbBool camera_write_chunk(TbFileHandle fh);

/** Read a SGC_CameraState chunk body from fh (chunk_len bytes of TLV data).
 *  Deserialises into internal storage.
 *  Unknown tags are skipped; missing fields keep their current values.
 *  Returns false on I/O error. */
TbBool camera_read_chunk(TbFileHandle fh, long chunk_len);

#ifdef __cplusplus
}
#endif
#endif /* KFX_ENGINE_CAMERAS_H */
