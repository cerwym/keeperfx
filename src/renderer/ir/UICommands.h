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
#include <vector>
#include "renderer/SpriteHandle.h"
#include "renderer/GpuTypes.h"
#include "renderer/ir/IRCommandBuffer.h"

// Forward declarations to avoid pulling in platform headers.
struct TbSprite;

/******************************************************************************/

/** UI render layer (draw order: lower values first).
 *
 *  WorldOverlay/WorldOverlayFlat are world-positioned content clipped to the
 *  game viewport; the only difference is whether they depth-test against
 *  world geometry. GameUI is the single composed pass for all in-game 2D
 *  chrome (sidebar, compass, dialogs, power-hand, messages, pause menu —
 *  owned by GameUI, src/kfx/ui/GameUI.cpp) — it draws AFTER both world-overlay
 *  layers and is opaque where it draws, so it simply paints over whatever's
 *  underneath. Nothing world-positioned needs to scissor itself away from
 *  GameUI's footprint; GameUI just wins by drawing last. */
enum class IRUILayer : uint8_t
{
    WorldOverlay     = 0,  /**< World-space sprites that SHOULD depth-test (creature status — occlusion by walls is intentional). */
    WorldOverlayFlat = 1,  /**< World-space sprites that should NOT depth-test (room flags, floating gold/damage text). */
    GameUI           = 2,  /**< All in-game 2D chrome, composed as one unit, drawn last over world content. */
    Overlay          = 3,  /**< Tooltip boxes, top-layer sprite overlays — drawn after GameUI.               */
    Cursor           = 4,  /**< Mouse cursor and keeper hand sprites.                                        */
};

/******************************************************************************/
// Solid / filled shapes
/******************************************************************************/

/** Filled solid-colour rectangle. */
struct IRUISolidBoxCmd
{
    IRUILayer layer      = IRUILayer::GameUI;
    int32_t   x          = 0;
    int32_t   y          = 0;
    int32_t   w          = 0;
    int32_t   h          = 0;
    uint8_t   colour_idx = 0;   /**< Palette index. */
    float     alpha      = 1.0f; /**< 1.0 = opaque. */
    unsigned int draw_flags = 0; /**< Bullfrog draw flags at submit (software replay honours Lb_SPRITE_OUTLINE); GL ignores. */
    float     ndc_z      = 0.5f; /**< NDC depth for WorldOverlay/WorldOverlayFlat layers; ignored for other layers. */
    uint32_t  seq        = 0;   /**< Global submission order across all IR command types. */
};

/** Slab background tile fill. */
struct IRUISlabBackgroundCmd
{
    IRUILayer layer = IRUILayer::GameUI;
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
    IRUILayer    layer         = IRUILayer::GameUI;
    int32_t      x             = 0;
    int32_t      y             = 0;
    int32_t      w             = 0;   /**< 0 = use units_per_px size. */
    int32_t      h             = 0;
    int32_t      units_per_px  = 16;  /**< 16 = 100% scale. */
    SpriteHandle sprite        = kInvalidSpriteHandle;
    uint32_t     flags         = 0;   /**< kIRSpriteFlipHoriz | kIRSpriteScaled */
    unsigned int draw_flags    = 0;   /**< Authoritative Bullfrog draw flags (Lb_SPRITE_TRANSPAR*, etc.); backends derive alpha/transparency from this. */
    float        ndc_z         = 0.5f; /**< NDC depth for WorldOverlay/WorldOverlayFlat layers; ignored for other layers. */
    uint32_t     seq           = 0;   /**< Global submission order across all IR command types. */
};

/** Draw a palette-remap sprite (player colour recolour). */
struct IRUISpriteRemapCmd
{
    IRUILayer    layer        = IRUILayer::GameUI;
    int32_t      x            = 0;
    int32_t      y            = 0;
    int32_t      units_per_px = 16;
    SpriteHandle sprite       = kInvalidSpriteHandle;
    int32_t      remap_row    = 0;   /**< Row into fade_tables[]. */
    unsigned int draw_flags   = 0;   /**< Authoritative Bullfrog draw flags; backends derive alpha/transparency from this. */
    float        ndc_z        = 0.5f; /**< NDC depth for WorldOverlay/WorldOverlayFlat layers; ignored for other layers. */
    uint32_t     seq          = 0;   /**< Global submission order across all IR command types. */
};

/** Draw a single-colour tinted sprite. */
struct IRUISpriteColoredCmd
{
    IRUILayer    layer        = IRUILayer::GameUI;
    int32_t      x            = 0;
    int32_t      y            = 0;
    int32_t      units_per_px = 16;
    SpriteHandle sprite       = kInvalidSpriteHandle;
    uint8_t      colour_idx   = 0;   /**< Palette index for flat output. */
    unsigned int draw_flags   = 0;   /**< Authoritative Bullfrog draw flags; backends derive alpha/transparency from this. */
    float        ndc_z        = 0.5f; /**< NDC depth for WorldOverlay/WorldOverlayFlat layers; ignored for other layers. */
    uint32_t     seq          = 0;   /**< Global submission order across all IR command types. */
};

/******************************************************************************/
// Clip state
/******************************************************************************/

/** Set the software clip window at replay time. Ordered against sprites/text
 *  via the shared seq so deferred draws between a Begin/End pair replay under
 *  the same clip they were submitted under. w<=0 restores the full screen. */
struct IRUIViewportCmd
{
    int32_t  x   = 0;
    int32_t  y   = 0;
    int32_t  w   = 0;
    int32_t  h   = 0;
    uint32_t seq = 0; /**< Global submission order across all IR command types. */
};

/** Software minimap background sampling, run at replay time: the sampled
 *  pixels (panel sprites under the minimap circle) are produced by the IR
 *  replay itself, so sampling at submit time would read a stale frame. */
struct IRUIMinimapBgSetupCmd
{
    int32_t  diaglen = 0;
    int32_t  panel_x = 0;
    int32_t  panel_y = 0;
    uint32_t seq     = 0; /**< Global submission order across all IR command types. */
};

/** Software minimap replay blit. The minimap is rasterised into WScreen
 *  mid-frame, before the deferred panel background replays over it; this
 *  circle-span pixel snapshot re-applies it at its submission seq. */
struct UIMinimapBlit
{
    int32_t  x    = 0;
    int32_t  y    = 0;
    int32_t  size = 0;
    uint32_t seq  = 0;
    bool     set  = false;
};

/******************************************************************************/
// Special / composite
/******************************************************************************/

/** Slab selector highlight overlay. */
struct IRUISlabSelectorCmd
{
    IRUILayer layer     = IRUILayer::GameUI;
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
    unsigned int      draw_flags    = 0;        /**< Draw flags captured at submit time. */
};

/** Draw the in-game keeper-hand / picked-up-thing sprite (software path).
 *  The GPU path draws the hand as part of the world overlay, so it does not
 *  need a separate IR command.  The hand sprite is drawn at the mouse cursor
 */
struct IRUICursorKeeperHandCmd
{
    int32_t        x          = 0;
    int32_t        y          = 0;
    unsigned short kspr_base  = 0;
    short          angle      = 0;
    unsigned char  sprgroup   = 0;
    long           scale      = 0;
    unsigned int   draw_flags = 0;
    unsigned char  alpha      = 0;
    uint32_t       seq        = 0; /**< Global submission order across all IR command types. */
};

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
    IRCommandBuffer<IRUICursorPointerCmd>    cursor_pointers;
    IRCommandBuffer<IRUICursorKeeperHandCmd> cursor_hands;
    IRCommandBuffer<IRUIViewportCmd>         viewports;
    IRCommandBuffer<IRUIMinimapBgSetupCmd>   minimap_bg_setups;

    /** At most one minimap blit per frame (see UIMinimapBlit). */
    UIMinimapBlit        minimap_blit;
    /** size*size square snapshot backing minimap_blit; only the circle spans
     *  (MapShapeStart/End) hold valid pixels. */
    std::vector<uint8_t> minimap_pixels;

    /** Game viewport captured at SetGameViewport() — restored by ExecuteUIFromIR(). */
    UIGameViewport game_vp;
    /** True when this buffer was populated via the IR path (not stale-replay). */
    bool           ir_active = false;
    /** Monotonically increasing counter assigned to each command Append() call.
     *  Stored in each command's seq field so ExecuteUIFromIR() can restore the
     *  original game-thread submission order after processing commands by type. */
    uint32_t       next_seq  = 0;
    /** When set (software), points at the shared UI+text frame counter so a
     *  software executor replays UI and text commands in true submission order.
     *  Null on GL (which orders text as always-on-top, not by cross-seq). Not
     *  reset/swapped — owned by the backend and re-pointed each frame. */
    uint32_t*      shared_seq = nullptr;

    /** Next submission sequence number: the shared counter if wired, else per-buffer. */
    uint32_t NextSeq() { return shared_seq ? (*shared_seq)++ : next_seq++; }

    void Reset()
    {
        solid_boxes.Reset();
        slab_backgrounds.Reset();
        sprites.Reset();
        sprites_remap.Reset();
        sprites_colored.Reset();
        slab_selectors.Reset();
        cursor_pointers.Reset();
        cursor_hands.Reset();
        viewports.Reset();
        minimap_bg_setups.Reset();
        minimap_blit = {};
        minimap_pixels.clear();
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
        cursor_pointers.Reserve(4);
        cursor_hands.Reserve(16);
        viewports.Reserve(8);
    }

    /** Returns true if any drawable commands were submitted this frame.
     *  Used by EndFrame() to decide whether to Flip() (real frame) or
     *  UpdateFrameState() (empty frame that should preserve the previous UI). */
    bool HasAnyCommands() const
    {
        return !solid_boxes.Empty()     || !slab_backgrounds.Empty() ||
               !sprites.Empty()         || !sprites_remap.Empty()    ||
               !sprites_colored.Empty() || !slab_selectors.Empty()   ||
               !cursor_pointers.Empty() || !cursor_hands.Empty();
    }

    void Swap(UICommandBuffers& other)
    {
        solid_boxes.Swap(other.solid_boxes);
        slab_backgrounds.Swap(other.slab_backgrounds);
        sprites.Swap(other.sprites);
        sprites_remap.Swap(other.sprites_remap);
        sprites_colored.Swap(other.sprites_colored);
        slab_selectors.Swap(other.slab_selectors);
        cursor_pointers.Swap(other.cursor_pointers);
        cursor_hands.Swap(other.cursor_hands);
        viewports.Swap(other.viewports);
        minimap_bg_setups.Swap(other.minimap_bg_setups);
        std::swap(minimap_blit,   other.minimap_blit);
        std::swap(minimap_pixels, other.minimap_pixels);
        std::swap(game_vp,   other.game_vp);
        std::swap(ir_active, other.ir_active);
        std::swap(next_seq,  other.next_seq);
    }
};

/******************************************************************************/
