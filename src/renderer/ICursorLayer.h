/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file ICursorLayer.h
 *     Cursor rendering abstraction — owns both the OS pointer sprite and the
 *     in-game keeper-hand sprite so they share a single flush point.
 *     This makes it easier to guarantee that both sprites are always drawn on top of everything else, without needing special-case handling in the main renderers or the text renderer
 * 
 *
 * @par Design:
 *     The OS pointer sprite (set via LbMouseChangeSprite) and the in-game
 *     power-hand sprite (submitted by draw_power_hand) are fundamentally
 *     different things — different sprite formats, different shader paths —
 *     but they share one requirement: both must be the absolute last elements
 *     drawn before platform_swap_gl_buffers() so they are never obscured by
 *     any other renderer layer (including the screen-tint overlay).
 *
 *     ICursorLayer owns that contract.  RendererManager creates the
 *     appropriate concrete implementation at RendererInit() time and calls
 *     CursorLayer_Draw() from EndFrame() after every other pass.
 *
 *     Software mode (SWCursorLayer):
 *       SubmitPointerSprite  — deferred; drawn to WScreen in Draw() immediately
 *                              before the SDL blit.  No backup/restore needed
 *                              since WScreen is rebuilt from scratch each frame.
 *       SubmitKeeperHandSprite — calls process_keeper_sprite() immediately
 *                                (same as IUIRenderer::SubmitKeeperSprite did).
 *       Draw()               — draws the pointer sprite to WScreen if one was
 *                              submitted this frame; no-ops for the hand.
 *
 *     OpenGL mode (GLCursorLayer):
 *       SubmitPointerSprite  — queued into m_pending_pointer (atlas quad path).
 *       SubmitKeeperHandSprite — queued into m_pending_keeper.
 *       Draw()               — drains m_pending_pointer via atlas sprite shader,
 *                              then drains m_pending_keeper via the keeper-sprite
 *                              shader (BeginHandSpriteRendering / process_keeper_sprite
 *                              loop / EndHandSpriteRendering).
 */
/******************************************************************************/
#pragma once

#include <cstdint>

struct TbSprite;
struct UICommandBuffers; // forward declaration — full type needed only by implementations

#ifdef __cplusplus

class ICursorLayer {
public:
    virtual ~ICursorLayer() = default;
    /** Submit the OS pointer sprite for deferred rendering at end-of-frame.
     *  Called from CursorLayer_SubmitPointerSprite() bridge. */
    virtual void SubmitPointerSprite(const TbSprite* spr,
                                     int32_t x, int32_t y,
                                     int units_per_px) = 0;

    /** Submit one keeper-hand sprite for this frame.
     *  Called from draw_power_hand() in place of UIRenderer_SubmitKeeperSprite.
     *  Parameters match process_keeper_sprite() exactly. */
    virtual void SubmitKeeperHandSprite(short x, short y,
                                        unsigned short kspr_base,
                                        short angle,
                                        unsigned char sprgroup,
                                        int32_t scale) = 0;

    /** Draw all queued cursor sprites to the framebuffer.
     *  Called from RendererOpenGL::EndFrame() and RendererSoftware::EndFrame()
     *  as the absolute last draw step before the buffer swap. */
    virtual void Draw() = 0;

    /** Clear queued state.  Called at the start of each frame (BeginFrame). */
    virtual void Clear() = 0;

    /** Flip game-thread command lists to render-thread copies.
     *  Called from EndFrame() before signalling the render thread.
     *  CPU default: no-op. */
    virtual void FlipBuffers() {}

    /** Open the IR write window for this frame (game thread).
     *  When @p cmds is non-null, Submit* calls emit into @p cmds->cursor_pointers
     *  and @p cmds->cursor_hands instead of internal queues.
     *  Call with nullptr to close the window before FlipBuffers(). */
    virtual void SetCursorWriteBuffers(UICommandBuffers* cmds) {}

    /** Execute cursor draws from the read-side IR buffers (render thread).
     *  Called from EndFrame_GL() after all other passes, replacing Draw().
     *  Default: forwards to Draw() for backward compatibility. */
    virtual void ExecuteCursorFromIR(const UICommandBuffers& cmds) { Draw(); }

    virtual const char* GetName() const = 0;
};

#endif // __cplusplus
