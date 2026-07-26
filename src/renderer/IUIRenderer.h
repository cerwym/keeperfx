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
#include "renderer/DrawState.h"
#include "bflib_basics.h"
#include <unordered_map>
#include <cstdint>

struct TbSprite;

// Forward-declare IR types so PopulateFromIR can appear on this interface
// without pulling UICommands.h / FrameState.h into every consumer.
struct UICommandBuffers;
struct TextCommandBuffers;
struct FrameState;
class  ITextRenderer;

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

    /** Resolve a raw TbSprite* to its SpriteHandle — the single entry point the
     *  RendererManager C bridge uses before crossing into the handle-based API.
     *
     *  Base (CPU) impl: lazily assigns and registers a handle for any not-yet-seen
     *  sprite, so every valid sprite resolves.  UI submission is single-threaded,
     *  so mutating the tables here is safe.
     */
    virtual SpriteHandle ResolveSprite(const struct TbSprite* spr);

    /** Eagerly register every packable sprite in a sheet.
     *  Base (CPU) impl assigns handles now so later ResolveSprite() calls hit
     *  without a first-use miss */
    virtual void RegisterSpriteSheet(const struct TbSpriteSheet* sheet);

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
     * CPU default: LbSpriteDrawResized using the supplied Bullfrog draw flags.
     * @param flip_horiz  Mirror the sprite horizontally.
     * @param draw_flags  Bullfrog draw flags captured at submission time.
     */
    virtual void SubmitPanelSprite(int32_t x, int32_t y, int units_per_px,
                                   SpriteHandle spr, bool flip_horiz,
                                   KfxDrawState state);

    /**
     * Submit a panel/button sprite with palette remap (player colour tinting).
     * CPU default: LbSpriteDrawResizedRemap using pixmap.fade_tables[remap_row]
     * and the supplied Bullfrog draw flags.
     * GPU backends override with a remap shader that samples the fade-table texture.
     * @param remap_row   Row index into pixmap.fade_tables (e.g. 12, 22, 44).
     * @param draw_flags  Bullfrog draw flags captured at submission time.
     */
    virtual void SubmitPanelSpriteRemap(int32_t x, int32_t y, int units_per_px,
                                        SpriteHandle spr, int remap_row,
                                        KfxDrawState state);

    /**
     * Submit a panel/button sprite drawn entirely in a single flat colour (sprite used as a mask).
     * CPU default: LbSpriteDrawResizedOneColour using the supplied Bullfrog draw flags.
     * GPU: uses the atlas R8 index as a discard mask; outputs color_idx as a flat colour.
     * @param color_idx   DK palette index for the flat output colour.
     * @param draw_flags  Bullfrog draw flags captured at submission time.
     */
    virtual void SubmitPanelSpriteColored(int32_t x, int32_t y, int units_per_px,
                                          SpriteHandle spr, uint8_t color_idx,
                                          KfxDrawState state);

    /**
     * Submit a sprite with explicit pixel dimensions.
     * CPU default: LbSpriteDrawScaled using the supplied Bullfrog draw flags.
     * @param draw_flags  Bullfrog draw flags captured at submission time.
     */
    virtual void SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h,
                                    SpriteHandle spr, KfxDrawState state);
 
    /**
     * Submit a solid-color rectangle.
     * CPU default: direct LbDrawBox software rasterisation.
     * @param state  Per-call draw descriptor (only Lb_SPRITE_OUTLINE is consulted here).
     */
    virtual void SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx, KfxDrawState state);
 
    /**
     * Submit a solid-color rectangle with explicit alpha (e.g. 0.5 for TRANSPAR4 darkening).
     * CPU default: direct GlassMap box blend.
     */
    virtual void SubmitSolidBoxAlpha(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx, float alpha);
 
    /** Submit a raw TbSprite draw with explicit Bullfrog draw flags.
     *  CPU default: original LbSpriteDraw path. */
    virtual TbResult SubmitRawSprite(long x, long y, const struct TbSprite* spr,
                                    KfxDrawState state);
 
    /** Submit a one-colour raw sprite draw with explicit Bullfrog draw flags.
     *  CPU default: original LbSpriteDrawOneColour path. */
    virtual TbResult SubmitRawSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                             unsigned char colour, KfxDrawState state);
 
    /** Submit a remapped raw sprite draw with explicit Bullfrog draw flags.
     *  CPU default: original LbSpriteDrawScaledRemap path at native sprite size. */
    virtual TbResult SubmitRawSpriteRemap(long x, long y, const struct TbSprite* spr,
                                         const unsigned char* cmap, KfxDrawState state);

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
     * CPU default: returns nullptr (caller writes directly to the CPU staging buffer).
     * GPU backends return a palette-index buffer; caller fills it then calls SubmitMinimap().
     */
    virtual uint8_t* AcquireMinimapBuffer(int size);

    /**
     * Finalise the minimap for this frame.
     * CPU default: no-op (pixels were written directly to the CPU staging buffer).
     */
    virtual void SubmitMinimap(int screen_x, int screen_y, int size);

    // -------------------------------------------------------------------------
    // Layer / draw control (GPU-only concept; CPU default = no-op or passthrough)
    // -------------------------------------------------------------------------

    /** Begin a world-overlay batch that depth-tests against world geometry:
     *  subsequent submissions are tagged with ndc_z (in [-1,1]) and rendered
     *  with GL depth-test ON, so they are occluded by world geometry drawn
     *  earlier this frame (e.g. creature status — occlusion by walls is
     *  intentional).  CPU default: no-op. */
    virtual void SetWorldOverlay(float /*ndc_z*/) { }

    /** End a world-overlay (depth-tested) batch. */
    virtual void ClearWorldOverlay() { }

    /** Begin a world-overlay batch that does NOT depth-test: subsequent
     *  submissions are tagged with ndc_z but rendered with depth-test OFF, so
     *  they stay visible above world geometry regardless of nearby walls or
     *  placement-preview overlays (e.g. room flags, floating gold/damage
     *  text).  Still scissor-clipped to the game viewport like SetWorldOverlay().
     *  CPU default: no-op. */
    virtual void SetWorldOverlayFlat(float /*ndc_z*/) { }

    /** End a world-overlay-flat (non-depth-tested) batch. */
    virtual void ClearWorldOverlayFlat() { }

    /** Set the game viewport rect (screen pixels) for scissor-clipping
     *  WorldOverlay/WorldOverlayFlat sprites so they don't bleed into the
     *  overhead map or zoom box.  CPU default: no-op. */
    virtual void SetGameViewport(int /*x*/, int /*y*/, int /*w*/, int /*h*/) { }

    /** Begin a top-overlay batch: subsequent submissions are drawn dead-last in the
     *  frame, on top of all other UI and world-depth elements (depth test OFF).
     *  Use for cursor-driven affordances (slab selector) that must never be
     *  obscured by room flags, status flowers, or any other world element.  CPU default: no-op. */
    virtual void SetTopOverlay() { }

    /** End the top-overlay batch. */
    virtual void ClearTopOverlay() { }

    /** Begin drawing zoom-box thing sprites.
     *  GPU: enters top-overlay layer so sprites render over the zoom-box tiles.
     *  SW: pushes a clipped drawing viewport so sprites are confined to the box. */
    virtual void BeginZoomBoxOverlay(int x, int y, int w, int h);

    /** End zoom-box thing sprites drawing scope. */
    virtual void EndZoomBoxOverlay(int x, int y, int w, int h);

    /** Initialise NumBackColours/MapBackColours for minimap rendering.
     *  SW: samples WScreen pixels; GPU: sets a single zeroed background entry. */
    virtual void SetupMinimapBackground(int diaglen, int panel_x, int panel_y);

    /** Return true and set *idx to the 'opaque black' palette index the GPU minimap
     *  shader uses (to avoid transparent-sentinel 0). SW returns false. */
    virtual bool GetMinimapOpaqueBlackIndex(uint8_t* idx) const;
 
    /** Draw the composed in-game UI (sidebar background + buttons + minimap,
     *  compass, dialogs, power-hand, messages, pause menu — see
     *  src/kfx/ui/GameUI.cpp).  Drawn after both world-overlay layers and
     *  opaque where it draws, so nothing world-positioned needs to scissor
     *  itself away from it.  Must be followed by DrawFrontOverlay().
     *  CPU default: calls Draw(). */
    virtual void DrawGameUI() { Draw(); }

    /** Legacy combined "draw everything" entry point.  GPU backends call
     *  DrawWorldSpriteLayerRT()/DrawWorldOverlayFlatLayerRT()/DrawGameUI()/
     *  DrawFrontOverlay() separately in the live frame path so intermediate
     *  passes (overhead map, zoom box, PiP) can be composited between them.
     *  CPU default: calls DrawGameUI(). */
    virtual void DrawFront() { DrawGameUI(); }

    /** Draw top-overlay elements (tooltip, zoom-box corner frames).
     *  Must be called after DrawGameUI().  GPU default: no-op. */
    virtual void DrawFrontOverlay() {}

    /** Draw WorldOverlay sprites (depth-tested, RT copy) between the world
     *  geometry pass and GameUI.  Creature status icons etc. must appear in
     *  front of world geometry but occluded by walls.  Must be called before
     *  DrawGameUI().  GPU default: no-op. */
    virtual void DrawWorldSpriteLayerRT() {}

    /** Draw WorldOverlayFlat sprites (NOT depth-tested, RT copy) between the
     *  world geometry pass and GameUI.  Room flags, floating gold/damage text
     *  etc. must stay visible above world geometry regardless of nearby walls.
     *  Must be called before DrawGameUI().  GPU default: no-op. */
    virtual void DrawWorldOverlayFlatLayerRT() {}

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
     *  Base impl stores the pointer in m_ui_write_cmds and sets ir_active, so
     *  the CPU Submit* paths append instead of drawing (software IR executor).
     *  GPU backends override to store in their own way. */
    virtual void SetUICommandBuffers(UICommandBuffers* cmds);

    /** Replay a UI + text command stream, merged by their shared submission
     *  `seq`, through the immediate CPU draw path — the software IR executor.
     *  Called by RendererSoftware::EndFrame after RenderGraph::Flip.  Walks both
     *  read-side buffers in seq order; UI commands are drawn by re-invoking this
     *  renderer's immediate Submit* bodies, text commands via
     *  ITextRenderer::ReplayTextCommand.  No-op on GPU backends (they use their
     *  own PopulateFromIR/Draw). */
    virtual void ReplayMergedFromIR(const UICommandBuffers& ui,
                                    const TextCommandBuffers& text,
                                    ITextRenderer* text_renderer);

    // ── IR (Intermediate Representation) dispatch ──────────────────────────────

    /** Populate the render-thread quad/line buffers from the read-side IR command
     *  buffer captured by RenderGraph::Flip().
     *
     *  Called by RenderGraph::Execute() on the render thread, before the
     *  world-overlay/GameUI draws.
     *  GPU backends override this to translate IR commands into their internal
     *  geometry queues (e.g. m_rt_quads[]).  The base no-op is correct for the
     *  software renderer — it never uses IR.
     *
     *  @param cmds  Read-side UICommandBuffers (const — render thread never writes).
     *  @param fs    FrameState snapshot for palette lookup. */
    virtual void PopulateFromIR(const UICommandBuffers& /*cmds*/, const FrameState& /*fs*/) {}

    /** Flush any WorldOverlay sprites submitted via SetWorldOverlay() since
     *  the last call.
     *  GPU: flushes the WorldOverlay quad queue to the GPU and clears it.
     *  CPU default: no-op. */
    virtual void DrawWorldSprites() {}

protected:
    /** Immediate WScreen sampling body of SetupMinimapBackground; also invoked
     *  by the software IR executor when the setup was deferred (the sampled
     *  pixels only exist after the panel sprites have replayed). */
    void SampleMinimapBackground(int diaglen, int panel_x, int panel_y);

    /** Tile the slab texture into the current WScreen at (x,y,w,h).  Owned by the
     *  CPU backend (SoftwareUIRenderer); the base merged replay calls it via
     *  virtual dispatch for a deferred IRUISlabBackgroundCmd.  No-op by default. */
    virtual void TileSlabBackground(int /*x*/, int /*y*/, int /*w*/, int /*h*/) {}

    /** Sprite handle → raw TbSprite* map, used by CPU default implementations. */
    std::unordered_map<SpriteHandle, const struct TbSprite*> m_handle_to_sprite;

    /** Reverse index (raw TbSprite* → handle) backing the base ResolveSprite().
     *  Populated by ResolveSprite()/RegisterSpriteHandle(); unused by GPU backends,
     *  which resolve through their atlas instead. */
    std::unordered_map<const struct TbSprite*, SpriteHandle> m_sprite_to_handle;

    /** Next handle to mint in the base (CPU) ResolveSprite(). */
    uint32_t m_next_handle = 0;

    /** IR write target for this frame.  When non-null (software IR executor),
     *  the CPU Submit* paths append a command instead of drawing immediately;
     *  null (default) means draw immediately (legacy path).  Set via
     *  SetUICommandBuffers(); GPU backends manage their own equivalent. */
    UICommandBuffers* m_ui_write_cmds = nullptr;
};

/******************************************************************************/

#endif // __cplusplus