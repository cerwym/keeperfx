/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IUIRenderer.h
 *     Abstract interface for GPU-accelerated UI element rendering.
 * @par Design:
 *     Game code in engine_render.c dispatches UI elements (status flowers,
 *     floating text, room flags, slab selectors) through this interface.
 *     
 *     The software backend falls back to CPU blitting into staging buffer.
 *     GPU backends batch UI elements into vertex buffers and render via shaders.
 */
/******************************************************************************/
#ifndef IUI_RENDERER_H
#define IUI_RENDERER_H

#include "renderer/SpriteHandle.h"

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
 * Abstract UI renderer interface.
 * Allows GPU backends to render UI elements with proper batching and z-ordering.
 */
class IUIRenderer {
public:
    virtual ~IUIRenderer() = default;

    /**
     * Submit slab selector outline for rendering.
     * @param x1, y1 Start coordinates of line
     * @param x2, y2 End coordinates of line
     * @param color Line color
     * @param z_depth Z-depth for proper sorting
     */
    virtual void SubmitSlabSelector(int x1, int y1, int x2, int y2, unsigned char color, float z_depth) = 0;

    /**
     * Submit a keeper-hand/cursor sprite for rendering.
     * Hardware backends defer this to Flush() so it executes after frame setup (glClear).
     * The software backend executes immediately via process_keeper_sprite().
     */
    virtual void SubmitKeeperSprite(short x, short y, unsigned short kspr_base,
                                    short angle, unsigned char sprgroup, long scale) = 0;

    /**
     * Submit a single panel/button sprite for GPU rendering.
     * The sprite pointer must already be resolved (player coloring applied).
     * Hardware backends defer until Flush(); software renders immediately.
     * @param flip_horiz  When true, render the sprite mirrored horizontally.
     */
    virtual void SubmitPanelSprite(long x, long y, int units_per_px,
                                   SpriteHandle spr, bool flip_horiz = false) = 0;

    /**
     * Submit a sprite with explicit pixel dimensions for GPU rendering.
     * Used internally by game-logic hooks (draw_status_sprites, draw_engine_number, etc.)
     * to redirect LbSpriteDrawScaled calls through the GPU batch when active.
     * @param x, y  Top-left screen position in pixels
     * @param w, h  Destination pixel dimensions
     * @param spr   Sprite pointer (must be in the sprite atlas)
     */
    virtual void SubmitScaledSprite(long x, long y, long w, long h,
                                    SpriteHandle spr) = 0;

    /**
     * Submit a solid-color rectangle for GPU rendering.
     * Used internally by game-logic hooks to redirect LbDrawBox calls through
     * the GPU batch when the UI renderer is active.
     * @param x, y        Top-left screen position
     * @param w, h        Rectangle pixel dimensions
     * @param color_idx   DK palette index (0-255); converted to float RGB via lbPalette
     */
    virtual void SubmitSolidBox(long x, long y, long w, long h, uint8_t color_idx) = 0;

    /**
     * Return a renderer-owned scratch buffer for the caller to draw minimap pixels into.
     * In GPU backends: returns a zeroed size×size palette-index buffer (stride = size).
     *   The caller writes palette indices; index 0 = transparent.
     *   Call SubmitMinimap() once drawing is complete to upload the result.
     * In software backends: returns NULL — the caller should write directly to
     *   lbDisplay.WScreen at the minimap's screen position instead.
     * @param size  Width and height of the square buffer (MapDiagonalLength)
     */
    virtual uint8_t* AcquireMinimapBuffer(int size) = 0;

    /**
     * Finalise the minimap for this frame.
     * In GPU backends: uploads the buffer filled since AcquireMinimapBuffer() and
     *   queues a palette-lookup quad at (screen_x, screen_y).
     * In software backends: no-op (pixels were already written to WScreen).
     * @param screen_x  Left edge of the minimap on screen (pixels)
     * @param screen_y  Top edge of the minimap on screen (pixels)
     * @param size      Width and height (must match the AcquireMinimapBuffer call)
     */
    virtual void SubmitMinimap(int screen_x, int screen_y, int size) = 0;

    /**
     * Set the render layer for subsequent submissions.
     * @param layer  0 = back (drawn before the CPU staging blit, for GPU UI that must
     *               appear under CPU-drawn text/numbers);
     *               1 = front (drawn after the staging blit, for GPU UI on top of everything).
     * Default-layer on frame start is 1 (front).  Reset to 1 after drawing layer-0 elements.
     */
    virtual void SetLayer(int /*layer*/) { }

    /**
     * Flush only layer-0 (back) GPU UI elements.
     * Must be called before the CPU staging-buffer blit so that sidebar background
     * panels land beneath the palette-lookup staging quad.
     * The null/software renderer provides a default no-op.
     */
    virtual void FlushBack() { }

    /**
     * Flush all layer-1 (front) GPU UI elements, the minimap, and slab selectors.
     * Must be called after the CPU staging-buffer blit so that menus, power-hand,
     * and other overlay sprites appear on top of CPU-drawn content.
     * The null/software renderer default forwards to Flush().
     */
    virtual void FlushFront() { Flush(); }

    /**
     * Flush all queued UI elements to the GPU.
     * Called at the end of each frame after world rendering.
     */
    virtual void Flush() = 0;

    /**
     * Clear all queued UI elements without rendering.
     * Used when switching render modes or on errors.
     */
    virtual void Clear() = 0;

    virtual const char* GetName() const = 0;

    /** Returns true when this renderer submits GPU quads (i.e. LbSpriteDrawResized
     *  calls that have been replaced with UIRenderer_Submit* are genuinely skipped).
     *  Software backends return false; GPU-accelerated backends return true. */
    virtual bool IsGpuAccelerated() const { return false; }
};

/******************************************************************************/

#endif // __cplusplus
#endif // IUI_RENDERER_H