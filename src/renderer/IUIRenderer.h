/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IUIRenderer.h
 *     Base UI renderer — CPU implementation is the default; GPU backends override.
 * @par Design:
 *     IUIRenderer is a concrete base class whose default method bodies perform
 *     immediate CPU staging-buffer blitting (the original software path).
 *     GPU backends (e.g. GLUIRenderer) override only the methods they accelerate.
 *
 *     This means adding a new submission method requires writing the CPU path
 *     exactly once here, then overriding in the GPU backend.  No other file
 *     ever holds a CPU fallback for UI rendering.
 *
 *     SoftwareUIRenderer is a trivial subclass that adds nothing; it exists only
 *     so RendererSoftware can instantiate a named type.
 */
/******************************************************************************/
#ifndef IUI_RENDERER_H
#define IUI_RENDERER_H

#include "renderer/SpriteHandle.h"
#include <unordered_map>
#include <cstdint>

struct TbSprite;

#ifdef __cplusplus

/******************************************************************************/

/**
 * Types of UI elements that can be rendered.
 * Corresponds to the QK_* bucket kinds in engine_render.c
 */
enum UIElementType {
    UI_ELEMENT_STATUS_SPRITE,      // QK_CreatureStatus - creature status flowers
    UI_ELEMENT_FLOATING_TEXT,      // QK_FloatingGoldText - floating gold numbers  
    UI_ELEMENT_SLAB_SELECTOR,      // QK_SlabSelector - selection outline boxes
    UI_ELEMENT_ROOM_FLAG_POLE,     // QK_RoomFlagBottomPole - room status pole
    UI_ELEMENT_ROOM_FLAG_TOP,      // QK_RoomFlagStatusBox - room status display
};

/**
 * Base UI renderer.
 *
 * Default implementations delegate to the existing CPU staging-buffer paths
 * (LbSpriteDrawResized, LbDrawBox, process_keeper_sprite, etc.).  GPU subclasses
 * override whichever methods they can accelerate and leave the rest untouched —
 * the base provides a transparent, correct fallback automatically.
 */
class IUIRenderer {
public:
    virtual ~IUIRenderer() = default;

    // -------------------------------------------------------------------------
    // Sprite handle registry
    // Every sprite handle submitted via SubmitPanelSprite / SubmitScaledSprite
    // must be registered here so the CPU default implementations can resolve it
    // to a TbSprite*.  GPU backends also receive the registration call so they
    // can upload the sprite to VRAM.
    // -------------------------------------------------------------------------

    /** Register a sprite handle → TbSprite* mapping.
     *  Called from RendererManager whenever sprite sheets are loaded. */
    void RegisterSpriteHandle(SpriteHandle h, const struct TbSprite* spr);

    // -------------------------------------------------------------------------
    // Submission API  (default = CPU; GPU backends override selectively)
    // -------------------------------------------------------------------------

    /**
     * Submit slab selector outline for rendering.
     * CPU default: no-op (software renderer draws selectors through its own path).
     */
    virtual void SubmitSlabSelector(int x1, int y1, int x2, int y2,
                                    unsigned char color, float z_depth);

    /**
     * Submit a keeper-hand/cursor sprite for rendering.
     * CPU default: calls process_keeper_sprite() immediately.
     * GPU backends defer until after glClear().
     */
    virtual void SubmitKeeperSprite(short x, short y, unsigned short kspr_base,
                                    short angle, unsigned char sprgroup, long scale);

    /**
     * Submit a panel/button sprite.
     * CPU default: LbSpriteDrawResized (with optional horizontal flip).
     * @param flip_horiz  Mirror the sprite horizontally.
     */
    virtual void SubmitPanelSprite(int32_t x, int32_t y, int units_per_px,
                                   SpriteHandle spr, bool flip_horiz = false);

    /**
     * Submit a panel/button sprite with palette remap (player colour tinting).
     * CPU default: LbSpriteDrawResizedRemap using pixmap.fade_tables[remap_row].
     * GPU backends override with a remap shader that samples the fade-table texture.
     * @param remap_row  Row index into pixmap.fade_tables (e.g. 12, 22, 44).
     */
    virtual void SubmitPanelSpriteRemap(int32_t x, int32_t y, int units_per_px,
                                        SpriteHandle spr, int remap_row);

    /**
     * Submit a panel/button sprite drawn entirely in a single flat colour (sprite used as a mask).
     * CPU default: LbSpriteDrawResizedOneColour.
     * GPU: uses the atlas R8 index as a discard mask; outputs color_idx as a flat colour.
     * @param color_idx  DK palette index for the flat output colour.
     */
    virtual void SubmitPanelSpriteColored(int32_t x, int32_t y, int units_per_px,
                                          SpriteHandle spr, uint8_t color_idx);

    /**
     * Submit a sprite with explicit pixel dimensions.
     * CPU default: LbSpriteDrawScaled.
     */
    virtual void SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h,
                                    SpriteHandle spr);

    /**
     * Submit a solid-color rectangle.
     * CPU default: LbDrawBox.
     */
    virtual void SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx);

    /**
     * Submit a solid-color rectangle with explicit alpha (e.g. 0.5 for TRANSPAR4 darkening).
     * CPU default: LbDrawBox with Lb_SPRITE_TRANSPAR4 when alpha < 0.75.
     */
    virtual void SubmitSolidBoxAlpha(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx, float alpha);

    /**
     * Upload the 64×64 palette-indexed gui_slab tile to the GPU.
     * No-op in CPU mode. Call whenever gui_slab data changes.
     */
    virtual void UpdateSlabTexture(const uint8_t* data, int dim) {}

    /**
     * Submit a tiled slab-background quad for the given screen rect.
     * GPU: queued as a back-layer (layer 0) R8-tiled texture quad using GL_REPEAT UVs.
     * CPU: returns false; caller falls through to draw_slab64k_background WScreen writes.
     * @return true if GPU path handled it (caller must skip WScreen writes).
     */
    virtual bool SubmitSlabBackground(int x, int y, int w, int h) { return false; }

    /**
     * Return a scratch buffer for minimap pixels.
     * CPU default: returns nullptr (caller writes directly to lbDisplay.WScreen).
     * GPU backends return a palette-index buffer; caller fills it then calls SubmitMinimap().
     */
    virtual uint8_t* AcquireMinimapBuffer(int size);

    /**
     * Finalise the minimap for this frame.
     * CPU default: no-op (pixels were written directly to lbDisplay.WScreen).
     */
    virtual void SubmitMinimap(int screen_x, int screen_y, int size);

    // -------------------------------------------------------------------------
    // Layer / flush control (GPU-only concept; CPU default = no-op or passthrough)
    // -------------------------------------------------------------------------

    /** Set the active render layer (0=back, 1=front).  CPU default: no-op. */
    virtual void SetLayer(int /*layer*/) { }

    /** Flush layer-0 (back) elements before the CPU staging-buffer blit.
     *  CPU default: no-op. */
    virtual void FlushBack() { }

    /** Flush layer-1 (front) elements after the CPU staging-buffer blit.
     *  CPU default: calls Flush(). */
    virtual void FlushFront() { Flush(); }

    /** Flush all queued elements.  CPU default: no-op. */
    virtual void Flush();

    /** Discard all queued elements without rendering.  CPU default: no-op. */
    virtual void Clear();

    virtual const char* GetName() const;

    /** Returns true when this renderer submits GPU quads.
     *  CPU base returns false; GPU subclasses override to return true. */
    virtual bool IsGpuAccelerated() const { return false; }

protected:
    /** Sprite handle → raw TbSprite* map, used by CPU default implementations. */
    std::unordered_map<SpriteHandle, const struct TbSprite*> m_handle_to_sprite;
};

/******************************************************************************/

#endif // __cplusplus
#endif // IUI_RENDERER_H