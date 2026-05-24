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
#pragma once

#include "renderer/SpriteHandle.h"
#include "renderer/GpuTypes.h"
#include <unordered_map>
#include <cstdint>

struct TbSprite;

// Forward-declare IR types so PopulateFromIR can appear on this interface
// without pulling UICommands.h / FrameState.h into every consumer.
struct UICommandBuffers;
struct FrameState;

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
     * In GPU mode the data is latched here and the actual GL upload is deferred to
     * FlushPendingInit(), which must be called from the render-thread context.
     * No-op in CPU mode.
     */
    virtual void UpdateSlabTexture(const uint8_t* data, int dim) {}

    /**
     * Perform any pending GPU-side initialisations (texture creates / uploads) that
     * were deferred because they must run on the thread that owns the GL context.
     * Called from RendererOpenGL::EndFrame_GL() before the first draw call.
     * No-op in CPU mode.
     */
    virtual void FlushPendingInit() {}

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
    // Layer / draw control (GPU-only concept; CPU default = no-op or passthrough)
    // -------------------------------------------------------------------------

    /** Set the active render layer (0=back, 1=front).  CPU default: no-op. */
    virtual void SetLayer(int /*layer*/) { }

    /** Begin a world-depth-tested batch: subsequent submissions are tagged with
     *  ndc_z (in [-1,1]) and rendered with GL depth-test ON so they are occluded
     *  by world geometry drawn earlier this frame.  CPU default: no-op. */
    virtual void SetWorldDepth(float /*ndc_z*/) { }

    /** End a world-depth-tested batch, returning to normal (depth-test OFF) rendering. */
    virtual void ClearWorldDepth() { }

    /** Set the game viewport rect (screen pixels) for scissor-clipping layer-2
     *  sprites.  Prevents world-depth sprites from bleeding onto the sidebar,
     *  overhead map, or zoom box.  CPU default: no-op. */
    virtual void SetGameViewport(int /*x*/, int /*y*/, int /*w*/, int /*h*/) { }

    /** Begin a top-overlay batch: subsequent submissions are drawn dead-last in the
     *  frame, on top of all other UI and world-depth elements (depth test OFF).
     *  Use for cursor-driven affordances (slab selector) that must never be
     *  obscured by room flags, status flowers, or any other world element.  CPU default: no-op. */
    virtual void SetTopOverlay() { }

    /** End the top-overlay batch. */
    virtual void ClearTopOverlay() { }

    /** Draw layer-0 (back) elements before the CPU staging-buffer blit.
     *  CPU default: no-op. */
    virtual void DrawBack() { }

    /** Draw layer-1 (front) elements after the CPU staging-buffer blit.
     *  CPU default: calls Draw(). */
    virtual void DrawFront() { Draw(); }

    /** Draw layer-1 front elements excluding the top-overlay (layer-2/3).
     *  GPU backends split DrawFront() into two calls so that zoom-box tiles
     *  and other intermediate passes can be composited between them.
     *  EndFrame_GL() calls DrawFrontBase() first, then DrawFrontOverlay().
     *  CPU default: calls DrawFront() (combines both phases). */
    virtual void DrawFrontBase() { DrawFront(); }

    /** Draw layer-2/3 top-overlay elements (tooltip, zoom-box corner frames).
     *  Must be called after DrawFrontBase().  GPU default: no-op. */
    virtual void DrawFrontOverlay() {}

    /** Flip game-thread command lists to render-thread read copies.
     *  Called from EndFrame() on the game thread before signalling the
     *  render thread.  CPU default: no-op. */
    virtual void FlipBuffers() {}

    /** Picture-in-Picture support: mark the start of PiP sprite capture.
     *  All UI quads submitted between BeginPiPSprites() and DrawPiPSprites()
     *  are routed into the PiP FBO rather than the main frame.
     *  CPU default: no-op (PiP is GPU-only). */
    virtual void BeginPiPSprites() {}

    /** Flush PiP sprites into the currently-bound PiP FBO.
     *  @param pip_w  PiP framebuffer width in pixels.
     *  @param pip_h  PiP framebuffer height in pixels.
     *  CPU default: no-op. */
    virtual void DrawPiPSprites(int /*pip_w*/, int /*pip_h*/) {}

    /** Submit an FBO colour-attachment texture as a picture-in-picture quad.
     *  @param x            Screen X (top-left corner, pixels).
     *  @param y            Screen Y (top-left corner, pixels).
     *  @param w            Quad width in pixels.
     *  @param h            Quad height in pixels.
     *  @param tex_id       GPU texture handle (uint32_t == GLuint on all platforms).
     *  @param clip_radius  Clip-circle radius in NDC space; < 0 = no clip.
     *  CPU default: no-op. */
    virtual void SubmitFBOQuad(int /*x*/, int /*y*/, int /*w*/, int /*h*/,
                               GpuTextureHandle /*tex_id*/, float /*clip_radius*/ = -1.0f) {}

    /** Draw all queued elements.  CPU default: no-op. */
    virtual void Draw();

    /** Discard all queued elements without rendering.  CPU default: no-op. */
    virtual void Clear();

    virtual const char* GetName() const;

    // -------------------------------------------------------------------------
    // Hit-mask query (GPU only; software renderer returns nullptr)
    // -------------------------------------------------------------------------

    /** Return a packed 1-bit-per-pixel transparency mask for the sprite at handle h,
     *  at the sprite's native resolution.  Bit=1 means opaque; bit=0 means transparent.
     *  Pixel order: left-to-right, top-to-bottom; bit 0 of byte 0 is the top-left pixel.
     *  The returned buffer is heap-allocated — caller must free() it.
     *  Returns nullptr on the CPU/software renderer (no atlas to read from),
     *  or if the handle is invalid.
     *
     *  @param out_w      Receives the sprite's native pixel width.
     *  @param out_h      Receives the sprite's native pixel height.
     *  @param out_stride Receives the row stride in bytes ((w+7)/8). */
    virtual uint8_t* QuerySpriteMask(SpriteHandle /*h*/, int* /*out_w*/, int* /*out_h*/, int* /*out_stride*/) { return nullptr; }

    /** Notify the renderer of the current OS-window dimensions.
     *  Called from RendererOpenGL::BeginFrame_GL() on each frame.
     *  GPU backends update projection matrices and viewport state here.
     *  Default: no-op. */
    virtual void SetScreenSize(int /*w*/, int /*h*/) {}

    /** Supply the active 256-colour VGA palette (768 bytes: R,G,B × 256).
     *  Called by RendererSetPaletteForRenderers() on the game thread.
     *  Only a pointer copy — safe to call without holding the GL context.
     *  Default: no-op. */
    virtual void SetPaletteSource(const uint8_t* /*palette*/) {}

    /** Supply the fade-table GPU texture (256×256 R8 remap LUT).
     *  Called once on the render thread after the fade texture is created.
     *  GPU backends bind this as a lookup table for the fade/silhouette pass.
     *  Using uint32_t instead of GLuint to avoid pulling GL headers into this
     *  backend-agnostic interface (GLuint == uint32_t on all supported platforms).
     *  Default: no-op. */
    virtual void SetFadeTexture(GpuTextureHandle /*tex*/) {}

    /** Open the IR write window for this frame.
     *  Called from RendererOpenGL::BeginFrame_GL() once per non-fade-cache frame.
     *  GPU backends store the pointer and append IR commands to it during Submit*().
     *  Pass nullptr to close the window (e.g. in stale-replay / PiP frames).
     *  Default: no-op (software renderer never uses IR). */
    virtual void SetUICommandBuffers(UICommandBuffers* /*cmds*/) {}

    // ── IR (Intermediate Representation) dispatch ──────────────────────────────

    /** Populate the render-thread quad/line buffers from the read-side IR command
     *  buffer captured by RenderGraph::Flip().
     *
     *  Called by RenderGraph::Execute() on the render thread, before DrawBack().
     *  GPU backends override this to translate IR commands into their internal
     *  geometry queues (e.g. m_rt_quads[]).  The base no-op is correct for the
     *  software renderer — it never uses IR.
     *
     *  @param cmds  Read-side UICommandBuffers (const — render thread never writes).
     *  @param fs    FrameState snapshot for palette lookup. */
    virtual void PopulateFromIR(const UICommandBuffers& /*cmds*/, const FrameState& /*fs*/) {}

    /** Flush any world-depth sprites (layer 2) submitted via SetWorldDepth() since
     *  the last call.  Called from the render thread during the world bucket walk,
     *  once per bucket after draw_3d_sprites_for_bucket().
     *  GPU: flushes m_quads[2] to the GPU and clears it.  CPU default: no-op. */
    virtual void DrawWorldSprites() {}

protected:
    /** Sprite handle → raw TbSprite* map, used by CPU default implementations. */
    std::unordered_map<SpriteHandle, const struct TbSprite*> m_handle_to_sprite;
};

/******************************************************************************/

#endif // __cplusplus