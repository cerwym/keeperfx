/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file UICommands.h
 *     Intermediate-representation command types for the UI renderer.
 * @par Purpose:
 *     Captures all UI element submissions made by the game thread (panel
 *     sprites, solid boxes, slab selectors, minimap, FBO quads) so the render
 *     thread can execute them without touching game globals.
 *
 *     Layer ordering is encoded in the IRUILayer enum; within each layer,
 *     commands are drawn in submission order (stable sort).
 */
/******************************************************************************/
#pragma once

#include <cstdint>
#include <cstddef>
#include <utility>
#include "renderer/SpriteHandle.h"
#include "renderer/GpuTypes.h"
#include "renderer/ir/IRCommandBuffer.h"

// Forward declarations to avoid pulling in platform headers.
struct TbSprite;

/******************************************************************************/

/** UI render layer (draw order: lower values first). */
enum class IRUILayer : uint8_t
{
    Back       = 0,  /**< Sidebar background panels, solid fill boxes.    */
    Front      = 1,  /**< Panel sprites, scaled sprites, sprite overlays. */
    WorldDepth = 2,  /**< Creature status, gold text — depth-tested in game viewport. */
    Overlay    = 3,  /**< Tooltip boxes, top-layer sprite overlays.        */
    Cursor     = 4,  /**< Mouse cursor and keeper hand sprites.            */
};

/******************************************************************************/
// Solid / filled shapes
/******************************************************************************/

/** Filled solid-colour rectangle. */
struct IRUISolidBoxCmd
{
    IRUILayer layer      = IRUILayer::Back;
    int32_t   x          = 0;
    int32_t   y          = 0;
    int32_t   w          = 0;
    int32_t   h          = 0;
    uint8_t   colour_idx = 0;   /**< Palette index. */
    float     alpha      = 1.0f; /**< 1.0 = opaque. */
    float     ndc_z      = 0.5f; /**< NDC depth for WorldDepth layer; ignored for other layers. */
    uint32_t  seq        = 0;   /**< Global submission order across all IR command types. */
};

/** Slab background tile fill. */
struct IRUISlabBackgroundCmd
{
    IRUILayer layer = IRUILayer::Back;
    int32_t   x     = 0;
    int32_t   y     = 0;
    int32_t   w     = 0;
    int32_t   h     = 0;
    uint32_t  seq   = 0; /**< Global submission order across all IR command types. */
};

/******************************************************************************/
// Sprite submission
/******************************************************************************/

/** Sprite display flags. */
static constexpr uint32_t kIRSpriteFlipHoriz = (1u << 0);
static constexpr uint32_t kIRSpriteScaled    = (1u << 1);

/** Draw a panel sprite at screen position. */
struct IRUISpriteCmd
{
    IRUILayer    layer         = IRUILayer::Front;
    int32_t      x             = 0;
    int32_t      y             = 0;
    int32_t      w             = 0;   /**< 0 = use units_per_px size. */
    int32_t      h             = 0;
    int32_t      units_per_px  = 16;  /**< 16 = 100% scale. */
    SpriteHandle sprite        = kInvalidSpriteHandle;
    uint32_t     flags         = 0;   /**< kIRSpriteFlipHoriz | kIRSpriteScaled */
    float        alpha         = 1.0f; /**< 1.0 = opaque; 0.333 = ghost transparent. */
    float        ndc_z         = 0.5f; /**< NDC depth for WorldDepth layer; ignored for other layers. */
    uint32_t     seq           = 0;   /**< Global submission order across all IR command types. */
};

/** Draw a palette-remap sprite (player colour recolour). */
struct IRUISpriteRemapCmd
{
    IRUILayer    layer        = IRUILayer::Front;
    int32_t      x            = 0;
    int32_t      y            = 0;
    int32_t      units_per_px = 16;
    SpriteHandle sprite       = kInvalidSpriteHandle;
    int32_t      remap_row    = 0;   /**< Row into fade_tables[]. */
    float        alpha        = 1.0f;
    float        ndc_z        = 0.5f; /**< NDC depth for WorldDepth layer; ignored for other layers. */
    uint32_t     seq          = 0;   /**< Global submission order across all IR command types. */
};

/** Draw a single-colour tinted sprite. */
struct IRUISpriteColoredCmd
{
    IRUILayer    layer        = IRUILayer::Front;
    int32_t      x            = 0;
    int32_t      y            = 0;
    int32_t      units_per_px = 16;
    SpriteHandle sprite       = kInvalidSpriteHandle;
    uint8_t      colour_idx   = 0;   /**< Palette index for flat output. */
    float        alpha        = 1.0f;
    float        ndc_z        = 0.5f; /**< NDC depth for WorldDepth layer; ignored for other layers. */
    uint32_t     seq          = 0;   /**< Global submission order across all IR command types. */
};

/******************************************************************************/
// Special / composite
/******************************************************************************/

/** Slab selector highlight overlay. */
struct IRUISlabSelectorCmd
{
    IRUILayer layer     = IRUILayer::Front;
    int32_t   x1        = 0;
    int32_t   y1        = 0;
    int32_t   x2        = 0;
    int32_t   y2        = 0;
    uint8_t   colour    = 0;
    float     z_depth   = 0.0f;
    /** Pre-computed rendering params captured at game-thread submit time. */
    float     line_length = 0.0f; /**< Pixel length of the segment.          */
    float     thickness   = 1.0f; /**< Line thickness in screen pixels.      */
    float     band_width  = 1.0f; /**< Width of each colour band (pixels).   */
    float     step        = 1.0f; /**< Colour-index advance per pixel.       */
    uint32_t  game_turn   = 0;    /**< Animated phase (gameturn & mask).     */
};

/** FBO quad composite (Picture-in-Picture zoom box). */
struct IRUIFBOQuadCmd
{
    IRUILayer        layer          = IRUILayer::Front;
    int32_t          x              = 0;
    int32_t          y              = 0;
    int32_t          w              = 0;
    int32_t          h              = 0;
    GpuTextureHandle fbo_texture_id = kInvalidGpuTexture; /**< Opaque backend texture handle. */
    float            clip_radius    = -1.0f; /**< < 0 = no rounded clip. */
};

/** Minimap pixel buffer submission. */
struct IRUIMinimapCmd
{
    IRUILayer layer          = IRUILayer::Front;
    int32_t   screen_x       = 0;
    int32_t   screen_y       = 0;
    int32_t   size           = 0;    /**< NxN pixel square. */
    /* Pointer to the palette-indexed pixel buffer valid for this frame.
     * The render thread must not access this after the next BeginFrame(). */
    const uint8_t* pixels    = nullptr;
};

/** Fullscreen map-fade transition wipe composite. */
struct IRMapFadeCmd
{
    float step            = 0.f;   ///< Interpolated wipe step [0.0..32.0].
    bool  capture_pending = false; ///< Render thread must re-capture both views before compositing.
};

/******************************************************************************/
// Cursor layer
/******************************************************************************/

/** Draw the mouse cursor sprite. */
struct IRUICursorPointerCmd
{
    IRUILayer         layer         = IRUILayer::Cursor;
    const struct TbSprite* sprite   = nullptr;  /**< Non-owning; frame-valid. */
    int32_t           x             = 0;
    int32_t           y             = 0;
    int32_t           units_per_px  = 16;
};

/* IRUICursorKeeperHandCmd removed: keeper-hand sprites are now pre-computed on
 * the game thread via GLWorldViewRenderer::BeginCursorCapture() /
 * EndCursorCapture() and stored as IRWorldKeeperSpriteCmd in the WVR's own
 * shadow buffer.  No game-side globals are touched from the render thread. */

/******************************************************************************/

/** Game viewport rect — snapshot captured at SetGameViewport() call time. */
struct UIGameViewport
{
    int32_t x   = 0;
    int32_t y   = 0;
    int32_t w   = 0;
    int32_t h   = 0;
    bool    set = false;
};

/** Combined per-frame UI command buffers. */
struct UICommandBuffers
{
    IRCommandBuffer<IRUISolidBoxCmd>         solid_boxes;
    IRCommandBuffer<IRUISlabBackgroundCmd>   slab_backgrounds;
    IRCommandBuffer<IRUISpriteCmd>           sprites;
    IRCommandBuffer<IRUISpriteRemapCmd>      sprites_remap;
    IRCommandBuffer<IRUISpriteColoredCmd>    sprites_colored;
    IRCommandBuffer<IRUISlabSelectorCmd>     slab_selectors;
    IRCommandBuffer<IRUIFBOQuadCmd>          fbo_quads;
    IRCommandBuffer<IRUIMinimapCmd>          minimaps;
    IRCommandBuffer<IRUICursorPointerCmd>    cursor_pointers;

    /** Game viewport captured at SetGameViewport() — restored by ExecuteUIFromIR(). */
    UIGameViewport game_vp;
    /** True when this buffer was populated via the IR path (not stale-replay). */
    bool           ir_active = false;
    /** Monotonically increasing counter assigned to each command Append() call.
     *  Stored in each command's seq field so ExecuteUIFromIR() can restore the
     *  original game-thread submission order after processing commands by type. */
    uint32_t       next_seq  = 0;

    void Reset()
    {
        solid_boxes.Reset();
        slab_backgrounds.Reset();
        sprites.Reset();
        sprites_remap.Reset();
        sprites_colored.Reset();
        slab_selectors.Reset();
        fbo_quads.Reset();
        minimaps.Reset();
        cursor_pointers.Reset();
        game_vp   = {};
        ir_active = false;
        next_seq  = 0;
    }

    void Reserve(size_t sprites_n)
    {
        solid_boxes.Reserve(64);
        slab_backgrounds.Reserve(16);
        sprites.Reserve(sprites_n);
        sprites_remap.Reserve(sprites_n / 4);
        sprites_colored.Reserve(sprites_n / 4);
        slab_selectors.Reserve(8);
        fbo_quads.Reserve(8);
        minimaps.Reserve(2);
        cursor_pointers.Reserve(4);
    }

    /** Returns true if any drawable commands were submitted this frame.
     *  Used by EndFrame() to decide whether to Flip() (real frame) or
     *  UpdateFrameState() (empty frame that should preserve the previous UI). */
    bool HasAnyCommands() const
    {
        return !solid_boxes.Empty()     || !slab_backgrounds.Empty() ||
               !sprites.Empty()         || !sprites_remap.Empty()    ||
               !sprites_colored.Empty() || !slab_selectors.Empty()   ||
               !fbo_quads.Empty()       || !minimaps.Empty()         ||
               !cursor_pointers.Empty();
    }

    void Swap(UICommandBuffers& other)
    {
        solid_boxes.Swap(other.solid_boxes);
        slab_backgrounds.Swap(other.slab_backgrounds);
        sprites.Swap(other.sprites);
        sprites_remap.Swap(other.sprites_remap);
        sprites_colored.Swap(other.sprites_colored);
        slab_selectors.Swap(other.slab_selectors);
        fbo_quads.Swap(other.fbo_quads);
        minimaps.Swap(other.minimaps);
        cursor_pointers.Swap(other.cursor_pointers);
        std::swap(game_vp,   other.game_vp);
        std::swap(ir_active, other.ir_active);
        std::swap(next_seq,  other.next_seq);
    }
};

/******************************************************************************/
