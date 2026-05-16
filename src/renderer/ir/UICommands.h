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
#include "renderer/SpriteHandle.h"
#include "renderer/ir/IRCommandBuffer.h"

// Forward declarations to avoid pulling in platform headers.
struct TbSprite;

/******************************************************************************/

/** UI render layer (draw order: lower values first). */
enum class IRUILayer : uint8_t
{
    Back       = 0,  /**< Sidebar background panels, solid fill boxes.    */
    Front      = 1,  /**< Panel sprites, scaled sprites, sprite overlays. */
    Overlay    = 2,  /**< Tooltip boxes, top-layer sprite overlays.        */
    Cursor     = 3,  /**< Mouse cursor and keeper hand sprites.            */
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
};

/** Slab background tile fill. */
struct IRUISlabBackgroundCmd
{
    IRUILayer layer = IRUILayer::Back;
    int32_t   x     = 0;
    int32_t   y     = 0;
    int32_t   w     = 0;
    int32_t   h     = 0;
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
};

/** FBO quad composite (Picture-in-Picture zoom box). */
struct IRUIFBOQuadCmd
{
    IRUILayer layer          = IRUILayer::Front;
    int32_t   x              = 0;
    int32_t   y              = 0;
    int32_t   w              = 0;
    int32_t   h              = 0;
    uint32_t  fbo_texture_id = 0;   /**< Opaque backend GL texture handle. */
    float     clip_radius    = -1.0f; /**< < 0 = no rounded clip. */
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

/** Draw the keeper hand sprite at the cursor position. */
struct IRUICursorKeeperHandCmd
{
    IRUILayer  layer        = IRUILayer::Cursor;
    int16_t    x            = 0;
    int16_t    y            = 0;
    uint16_t   kspr_base    = 0;
    int16_t    angle        = 0;
    uint8_t    sprgroup     = 0;
    int32_t    scale        = 0;
    uint32_t   draw_flags   = 0;
    uint8_t    draw_alpha   = 255;
};

/******************************************************************************/

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
    IRCommandBuffer<IRUICursorKeeperHandCmd> cursor_hands;

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
        cursor_hands.Reset();
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
        cursor_hands.Reserve(4);
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
        cursor_hands.Swap(other.cursor_hands);
    }
};

/******************************************************************************/
