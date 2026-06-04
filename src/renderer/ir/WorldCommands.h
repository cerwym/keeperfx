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
#include <utility>
#include "renderer/WorldVertex.h"
#include "renderer/FlatPolyVertex.h"
#include "renderer/EngineVertex.h"

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

/** Draw a single palette-indexed keeper sprite at world-screen coordinates.
 *
 *  Captured on the game thread during the bucket walk; the render thread
 *  decodes the RLE, does atlas/CLUT management, and issues the GL draw.
 *
 *  Both @p data and @p remap point into static game-level arrays that remain
 *  resident for the entire level lifetime, so raw pointers are safe across the
 *  Flip() frame boundary.
 */
struct IRWorldKeeperSpriteCmd
{
    WorldCmdLayer         layer         = WorldCmdLayer::Sprites;
    int32_t               dst_x         = 0;   /**< Screen destination left. */
    int32_t               dst_y         = 0;   /**< Screen destination top. */
    int32_t               dst_w         = 0;   /**< Destination width. */
    int32_t               dst_h         = 0;   /**< Destination height. */
    int32_t               src_w         = 0;   /**< Source sprite width. */
    int32_t               src_h         = 0;   /**< Source sprite height. */
    uint32_t              draw_flags    = 0;   /**< Sprite draw flags (Lb_SPRITE_*). */
    /** RLE-compressed pixel data (keepersprite_array — stable for level lifetime). */
    const unsigned char*  data          = nullptr;
    /** 256-byte remap table (static global palette table), or nullptr for identity. */
    const unsigned char*  remap         = nullptr;
    float                 z_ndc         = 0.0f;  /**< Pre-computed NDC depth (half-bucket bias). */
    int8_t                owner         = -1;    /**< Player owner index (-1 = none). */
    int8_t                wants_outline =  0;    /**< Non-zero if depth-fail outline is wanted. */
    uint32_t              sort_key      = 0;
};

/******************************************************************************/
// Shadows
/******************************************************************************/

/** Draw a single creature shadow quad. */
struct IRWorldShadowCmd
{
    WorldCmdLayer    layer         = WorldCmdLayer::Shadows;
    EnginePolyVertex verts[4]      = {};  /**< Screen-px coords + fixed-point UV. */
    unsigned short   anim_sprite   = 0;
    short            angle         = 0;
    unsigned char    current_frame = 0;
    int              tex_w         = 0;
    int              tex_h         = 0;
    float            darkness      = 1.0f;  /**< Alpha for multiply-blend. */
    float            ndc_z         = 0.0f;  /**< NDC depth for depth test. */
    int32_t          wx            = 0, wy = 0, wz = 0;  /**< World-space caster origin. */
    uint32_t         sort_key      = 0;
};

/******************************************************************************/

/** Combined per-frame world command buffers. */
#include "renderer/ir/IRCommandBuffer.h"

struct WorldCommandBuffers
{
    IRCommandBuffer<IRWorldTileBatchCmd>     tiles;
    IRCommandBuffer<IRWorldTexQuadCmd>       tex_quads;
    IRCommandBuffer<IRWorldFlatPolyBatchCmd> flat_polys;
    IRCommandBuffer<IRWorldKeeperSpriteCmd>  keeper_sprites;
    IRCommandBuffer<IRWorldShadowCmd>        shadows;

    // Vertex data backing vert_start/vert_count in tiles and flat_polys commands.
    // Owned here so any backend's ExecuteWorldFromIR can access them without
    // coupling to backend-private arrays.
    std::vector<WorldVertex>    tile_verts;
    std::vector<FlatPolyVertex> flat_poly_verts;

    void Reset()
    {
        tiles.Reset();
        tex_quads.Reset();
        flat_polys.Reset();
        keeper_sprites.Reset();
        shadows.Reset();
        tile_verts.clear();
        flat_poly_verts.clear();
    }

    void Reserve(size_t tiles_n, size_t sprites_n, size_t shadows_n)
    {
        tiles.Reserve(tiles_n);
        flat_polys.Reserve(tiles_n / 4);
        keeper_sprites.Reserve(sprites_n);
        shadows.Reserve(shadows_n);
        tile_verts.reserve(65536);
        flat_poly_verts.reserve(8192);
    }

    void Swap(WorldCommandBuffers& other)
    {
        tiles.Swap(other.tiles);
        tex_quads.Swap(other.tex_quads);
        flat_polys.Swap(other.flat_polys);
        keeper_sprites.Swap(other.keeper_sprites);
        shadows.Swap(other.shadows);
        std::swap(tile_verts, other.tile_verts);
        std::swap(flat_poly_verts, other.flat_poly_verts);
    }
};

/******************************************************************************/
