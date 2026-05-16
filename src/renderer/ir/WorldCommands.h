/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file WorldCommands.h
 *     Intermediate-representation command types for the world renderer.
 * @par Purpose:
 *     These structs are written by the game thread (bucket walk in
 *     DrawIsometricView / DrawFrontView) and read by the render thread during
 *     RenderGraph::Execute().  They capture all data needed to render world
 *     geometry and sprites without accessing game-engine globals on the render
 *     thread.
 *
 *     Geometry is already converted to NDC float by the time it enters the IR;
 *     the CPU rasterizer uses the same structs as a compact record of what to
 *     draw, projecting back to screen coordinates via FrameState.
 */
/******************************************************************************/
#pragma once

#include <cstdint>
#include <cstddef>
#include "renderer/WorldVertex.h"
#include "bflib_render.h"  // PolyPoint (for shadow geometry)

/******************************************************************************/

/** Render layer / priority within the world pass.
 *  Higher values are drawn on top (painters order for world geometry). */
enum class WorldCmdLayer : uint8_t
{
    Geometry   = 0,  /**< Standard tiles, front-view tiles, flat polys. */
    Shadows    = 1,  /**< Creature shadow quads (rendered before sprites). */
    Sprites    = 2,  /**< Keeper sprites, jonty sprites, rotable sprites. */
    WorldUI    = 3,  /**< Depth-tested UI: status icons, floating text, flags. */
};

/******************************************************************************/
// Tile / polygon geometry
/******************************************************************************/

/** Draw a batch of tile triangles already packed into the world vertex buffer.
 *  CMD_TILES in the existing GLWorldViewRenderer::DrawCmd vocabulary. */
struct IRWorldTileBatchCmd
{
    WorldCmdLayer layer      = WorldCmdLayer::Geometry;
    int           vert_start = 0;   /**< First index in the renderer's vertex buffer. */
    int           vert_count = 0;   /**< Number of vertices (must be multiple of 3). */
    uint32_t      sort_key   = 0;   /**< Bucket index (large = far, small = near). */
};

/** Draw front-view axis-aligned tile quad. */
struct IRWorldTexQuadCmd
{
    WorldCmdLayer layer        = WorldCmdLayer::Geometry;
    uint8_t       orient       = 0;
    int32_t       texture_idx  = 0;
    int32_t       texture_x    = 0;
    int32_t       texture_y    = 0;
    int32_t       zoom_x       = 0;
    int32_t       zoom_y       = 0;
    int32_t       shade[4]     = {};  /**< Per-corner shade_intensity. */
    int32_t       marked_mode  = 0;
    uint32_t      sort_key     = 0;
};

/** Draw a batch of flat-colour triangles (QK_PolyMode0 / BasicPolygon).
 *  Vertices are screen-pixel coords; the backend converts to NDC. */
struct IRWorldFlatPolyBatchCmd
{
    WorldCmdLayer layer      = WorldCmdLayer::Geometry;
    int           vert_start = 0;   /**< First index in the flat-poly vertex buffer. */
    int           vert_count = 0;
    uint32_t      sort_key   = 0;
};

/******************************************************************************/
// Sprites
/******************************************************************************/

/** Draw the world sprites in a single engine bucket.
 *  The actual sprite list is still walked via the bucket list on the render
 *  thread (pending full IR migration); this cmd captures the bucket number. */
struct IRWorldSpriteBucketCmd
{
    WorldCmdLayer layer      = WorldCmdLayer::Sprites;
    int           bucket_num = 0;   /**< Engine bucket index to render. */
    uint32_t      sort_key   = 0;
};

/** Draw a single palette-indexed keeper sprite at world-screen coordinates. */
struct IRWorldKeeperSpriteCmd
{
    WorldCmdLayer layer         = WorldCmdLayer::Sprites;
    int32_t       dst_x         = 0;   /**< Screen destination left. */
    int32_t       dst_y         = 0;   /**< Screen destination top. */
    int32_t       dst_w         = 0;   /**< Destination width. */
    int32_t       dst_h         = 0;   /**< Destination height. */
    int32_t       src_w         = 0;   /**< Source sprite width. */
    int32_t       src_h         = 0;   /**< Source sprite height. */
    uint32_t      draw_flags    = 0;   /**< Sprite draw flags. */
    /* Pixel data is stored separately in the renderer's owned buffer;
     * this index references that buffer rather than a raw pointer. */
    uint32_t      pixel_buf_idx = 0;
    uint32_t      remap_buf_idx = 0;   /**< 0 = no remap table. */
    uint32_t      sort_key      = 0;
};

/******************************************************************************/
// Shadows
/******************************************************************************/

/** Draw a single creature shadow quad. */
struct IRWorldShadowCmd
{
    WorldCmdLayer    layer         = WorldCmdLayer::Shadows;
    struct PolyPoint verts[4]      = {};  /**< Screen-px coords + 16.16 UV. */
    unsigned short   anim_sprite   = 0;
    short            angle         = 0;
    unsigned char    current_frame = 0;
    int              tex_w         = 0;
    int              tex_h         = 0;
    float            darkness      = 1.0f;  /**< Alpha for multiply-blend. */
    float            ndc_z         = 0.0f;  /**< NDC depth for depth test. */
    uint32_t         sort_key      = 0;
};

/******************************************************************************/
// World-depth UI (depth-tested, inside game viewport)
/******************************************************************************/

/** Record a world-depth UI element by bucket (pending full IR migration). */
struct IRWorldUIBucketCmd
{
    WorldCmdLayer layer      = WorldCmdLayer::WorldUI;
    int           bucket_num = 0;
    uint32_t      sort_key   = 0;
};

/******************************************************************************/

/** Combined per-frame world command buffers. */
#include "renderer/ir/IRCommandBuffer.h"

struct WorldCommandBuffers
{
    IRCommandBuffer<IRWorldTileBatchCmd>    tiles;
    IRCommandBuffer<IRWorldTexQuadCmd>      tex_quads;
    IRCommandBuffer<IRWorldFlatPolyBatchCmd> flat_polys;
    IRCommandBuffer<IRWorldSpriteBucketCmd> sprite_buckets;
    IRCommandBuffer<IRWorldKeeperSpriteCmd> keeper_sprites;
    IRCommandBuffer<IRWorldShadowCmd>       shadows;
    IRCommandBuffer<IRWorldUIBucketCmd>     world_ui;

    void Reset()
    {
        tiles.Reset();
        tex_quads.Reset();
        flat_polys.Reset();
        sprite_buckets.Reset();
        keeper_sprites.Reset();
        shadows.Reset();
        world_ui.Reset();
    }

    void Reserve(size_t tiles_n, size_t sprites_n, size_t shadows_n)
    {
        tiles.Reserve(tiles_n);
        flat_polys.Reserve(tiles_n / 4);
        sprite_buckets.Reserve(sprites_n);
        keeper_sprites.Reserve(sprites_n);
        shadows.Reserve(shadows_n);
        world_ui.Reserve(sprites_n / 2);
    }
};

/******************************************************************************/
