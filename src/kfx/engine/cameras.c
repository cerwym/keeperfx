/******************************************************************************/
// KeeperFX — opaque per-player camera store.
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "kfx/engine/cameras.h"
#include "kfx/save/save_fields.h"
#include "player_data.h"         /* PLAYERS_COUNT */
#include "game_saves.h"          /* struct FileChunkHeader, SGC_* ids        */
#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "post_inc.h"

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Internal storage — opaque, never exposed through the header.               */
/******************************************************************************/

typedef struct {
    struct Camera slots[CamIV_EndList];
    int           active_idx;
    TbBool        initialised;
} PlayerCameraSet;

static PlayerCameraSet s_cameras[PLAYERS_COUNT]; /* zero-init at program start */

/******************************************************************************/
/* Accessors                                                                   */
/******************************************************************************/

struct Camera* camera_get_slot(int plyr_idx, int cam_idx)
{
    return &s_cameras[plyr_idx].slots[cam_idx];
}

struct Camera* camera_get_active(int plyr_idx)
{
    return &s_cameras[plyr_idx].slots[s_cameras[plyr_idx].active_idx];
}

int camera_get_active_idx(int plyr_idx)
{
    return s_cameras[plyr_idx].active_idx;
}

void camera_set_active(int plyr_idx, int cam_idx)
{
    s_cameras[plyr_idx].active_idx = cam_idx;
}

TbBool camera_is_active(int plyr_idx)
{
    return s_cameras[plyr_idx].initialised;
}

void camera_init_player(int plyr_idx)
{
    /* Zero all slots so no stale data leaks; init is then done by
     * init_player_cameras() in engine_camera.c which calls us via
     * camera_get_slot().  We just mark the set as initialised. */
    memset(&s_cameras[plyr_idx], 0, sizeof(PlayerCameraSet));
    s_cameras[plyr_idx].active_idx = CamIV_Isometric;
    s_cameras[plyr_idx].initialised = true;
}

/******************************************************************************/
/* Save                                                                        */
/******************************************************************************/

TbBool camera_write_chunk(TbFileHandle fh)
{
    SaveFieldBuf buf;
    if (!sfbuf_init(&buf, 4096))
        return false;

    for (int pi = 0; pi < PLAYERS_COUNT; pi++)
    {
        SFBUF_WRITE_U8(&buf, CAMF_PLAYER_BEGIN, pi);
        SFBUF_WRITE_U8(&buf, CAMF_ACTIVE_IDX,   s_cameras[pi].active_idx);

        for (int ci = 0; ci < CamIV_EndList; ci++)
        {
            const struct Camera *c = &s_cameras[pi].slots[ci];
            SFBUF_WRITE_U8( &buf, CAMF_SLOT_BEGIN,  ci);
            SFBUF_WRITE_S32(&buf, CAMF_MAPPOS_X,    c->mappos.x.val);
            SFBUF_WRITE_S32(&buf, CAMF_MAPPOS_Y,    c->mappos.y.val);
            SFBUF_WRITE_S32(&buf, CAMF_MAPPOS_Z,    c->mappos.z.val);
            SFBUF_WRITE_U8( &buf, CAMF_VIEW_MODE,   c->view_mode);
            SFBUF_WRITE_S32(&buf, CAMF_ROT_X,       c->rotation_angle_x);
            SFBUF_WRITE_S32(&buf, CAMF_ROT_Y,       c->rotation_angle_y);
            SFBUF_WRITE_S32(&buf, CAMF_ROT_Z,       c->rotation_angle_z);
            SFBUF_WRITE_S32(&buf, CAMF_HORIZ_FOV,   c->horizontal_fov);
            SFBUF_WRITE_S32(&buf, CAMF_ZOOM,        c->zoom);
            SFBUF_WRITE_S32(&buf, CAMF_INERTIA_ROT, c->inertia_rotation);
            SFBUF_WRITE_S32(&buf, CAMF_INERTIA_X,   c->inertia_x);
            SFBUF_WRITE_S32(&buf, CAMF_INERTIA_Y,   c->inertia_y);
            sfbuf_write_field(&buf, CAMF_SLOT_END, NULL, 0);
        }

        sfbuf_write_field(&buf, CAMF_PLAYER_END, NULL, 0);
    }

    if (buf.error)
    {
        sfbuf_free(&buf);
        return false;
    }

    struct FileChunkHeader hdr;
    hdr.id  = SGC_CameraState;
    hdr.ver = 0;
    hdr.len = buf.size;

    TbBool ok = (LbFileWrite(fh, &hdr, sizeof(hdr)) == sizeof(hdr));
    ok = ok && sfbuf_flush_to_file(&buf, fh);

    sfbuf_free(&buf);
    return ok;
}

/******************************************************************************/
/* Read — TLV callback state machine                                           */
/******************************************************************************/

typedef struct {
    int cur_player; /* player index being populated (-1 = none)   */
    int cur_slot;   /* camera slot being populated (-1 = none)     */
} CameraReadState;

static void camera_field_cb(unsigned short tag, const void *data,
                             unsigned short len, void *userdata)
{
    CameraReadState *st = (CameraReadState *)userdata;

    switch ((enum CameraFieldTag)tag)
    {
    case CAMF_PLAYER_BEGIN:
        st->cur_player = sfield_u8(data, len);
        st->cur_slot   = -1;
        if (st->cur_player < 0 || st->cur_player >= PLAYERS_COUNT)
            st->cur_player = -1;
        break;

    case CAMF_PLAYER_END:
        st->cur_player = -1;
        st->cur_slot   = -1;
        break;

    case CAMF_ACTIVE_IDX:
        if (st->cur_player >= 0)
            s_cameras[st->cur_player].active_idx = sfield_u8(data, len);
        break;

    case CAMF_SLOT_BEGIN:
        st->cur_slot = sfield_u8(data, len);
        if (st->cur_player < 0 || st->cur_slot < 0 || st->cur_slot >= CamIV_EndList)
            st->cur_slot = -1;
        break;

    case CAMF_SLOT_END:
        st->cur_slot = -1;
        break;

    /* Camera slot fields — only written when both player and slot are valid. */
#define NEED_SLOT  if (st->cur_player < 0 || st->cur_slot < 0) break; \
                   struct Camera *c = &s_cameras[st->cur_player].slots[st->cur_slot];

    case CAMF_MAPPOS_X:    { NEED_SLOT c->mappos.x.val       = sfield_s32(data, len); break; }
    case CAMF_MAPPOS_Y:    { NEED_SLOT c->mappos.y.val       = sfield_s32(data, len); break; }
    case CAMF_MAPPOS_Z:    { NEED_SLOT c->mappos.z.val       = sfield_s32(data, len); break; }
    case CAMF_VIEW_MODE:   { NEED_SLOT c->view_mode          = sfield_u8(data, len);  break; }
    case CAMF_ROT_X:       { NEED_SLOT c->rotation_angle_x   = sfield_s32(data, len); break; }
    case CAMF_ROT_Y:       { NEED_SLOT c->rotation_angle_y   = sfield_s32(data, len); break; }
    case CAMF_ROT_Z:       { NEED_SLOT c->rotation_angle_z   = sfield_s32(data, len); break; }
    case CAMF_HORIZ_FOV:   { NEED_SLOT c->horizontal_fov     = sfield_s32(data, len); break; }
    case CAMF_ZOOM:        { NEED_SLOT c->zoom               = sfield_s32(data, len); break; }
    case CAMF_INERTIA_ROT: { NEED_SLOT c->inertia_rotation   = sfield_s32(data, len); break; }
    case CAMF_INERTIA_X:   { NEED_SLOT c->inertia_x         = sfield_s32(data, len); break; }
    case CAMF_INERTIA_Y:   { NEED_SLOT c->inertia_y         = sfield_s32(data, len); break; }

#undef NEED_SLOT

    default:
        /* Unknown tag — silently skipped for forward compatibility. */
        break;
    }
}

TbBool camera_read_chunk(TbFileHandle fh, long chunk_len)
{
    CameraReadState st;
    st.cur_player = -1;
    st.cur_slot   = -1;

    /* Mark all players as initialised from save data. */
    for (int i = 0; i < PLAYERS_COUNT; i++)
        s_cameras[i].initialised = true;

    return sfield_read_fields(fh, chunk_len, camera_field_cb, &st);
}

#ifdef __cplusplus
}
#endif
