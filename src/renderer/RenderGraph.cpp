/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RenderGraph.cpp
 *     RenderGraph implementation — buffer lifecycle, Flip(), Execute().
 */
/******************************************************************************/
#include "renderer/RenderGraph.h"

#include "renderer/IWorldViewRenderer.h"
#include "renderer/IUIRenderer.h"
#include "renderer/ITextRenderer.h"

// IShadowRenderer and IDebugRenderer are forward-declared in RenderGraph.h.
// They don't have implementations yet; Execute() skips them if nullptr.

/******************************************************************************/
// RenderGraph::FrameBuffers
/******************************************************************************/

void RenderGraph::FrameBuffers::Reset()
{
    world.Reset();
    ui.Reset();
    text.Reset();
    shadow.Reset();
    debug.Reset();
}

void RenderGraph::FrameBuffers::Reserve(
    size_t world_tiles, size_t world_sprites, size_t world_shadows,
    size_t ui_cmds,     size_t text_cmds,
    size_t shadow_cmds, size_t debug_cmds)
{
    world.Reserve(world_tiles, world_sprites, world_shadows);
    ui.Reserve(ui_cmds);
    text.Reserve(text_cmds);
    shadow.Reserve(shadow_cmds);
    debug.Reserve(debug_cmds);
}

void RenderGraph::FrameBuffers::Swap(FrameBuffers& other)
{
    world.Swap(other.world);
    ui.Swap(other.ui);
    text.Swap(other.text);
    shadow.Swap(other.shadow);
    debug.Swap(other.debug);
}
/******************************************************************************/
// RenderGraph
/******************************************************************************/

RenderGraph::RenderGraph()
{
    // Zero-initialise the read-side FrameState so the render thread always
    // has a valid (if empty) snapshot even before the first Flip().
    m_read_fs = FrameState{};
}

void RenderGraph::Reserve(
    size_t world_tiles, size_t world_sprites, size_t world_shadows,
    size_t ui_cmds,     size_t text_cmds,
    size_t shadow_cmds, size_t debug_cmds)
{
    // Reserve both write and read sides so neither triggers a realloc
    // during a frame.
    m_write.Reserve(world_tiles, world_sprites, world_shadows,
                    ui_cmds, text_cmds, shadow_cmds, debug_cmds);
    m_read.Reserve(world_tiles,  world_sprites, world_shadows,
                   ui_cmds, text_cmds, shadow_cmds, debug_cmds);
}

void RenderGraph::BeginFrame()
{
    m_write.Reset();
}

void RenderGraph::Flip(const FrameState& fs)
{
    // Swap write ↔ read atomically (O(1) std::vector swap, no copies).
    m_write.Swap(m_read);

    // Capture FrameState snapshot from game-thread globals.
    m_read_fs = fs;

    // Reset the (now-old read-side, now-write-side) buffers so the game
    // thread can start writing frame N+1 immediately.
    m_write.Reset();
}

void RenderGraph::Execute(
    const BackendCapabilities& caps,
    IWorldViewRenderer*  world,
    IUIRenderer*         ui,
    ITextRenderer*       text,
    IShadowRenderer*     shadow,
    IDebugRenderer*      debug)
{
    // -------------------------------------------------------------------------
    // Layer 1: Shadow pass (before world so shadows composite underneath)
    // Shadow commands are currently embedded in the world geometry stream
    // (IRWorldShadowCmd in WorldCommandBuffers).  The dedicated IShadowRenderer
    // path is reserved for future shadow-map / shadow-volume implementations.
    // When IShadowRenderer exists and the backend supports it, dispatch here.
    (void)shadow;  // not yet implemented

    // -------------------------------------------------------------------------
    // Layer 2: World pass (geometry → shadows → sprites → world-UI)
    // GLWorldViewRenderer::ExecuteWorldFromIR() is called directly from
    // EndFrame_GL() (needed for lens-FBO wrapping); the (void) here documents
    // that the RenderGraph tracks the world IR but doesn't dispatch it centrally.
    (void)world;

    // -------------------------------------------------------------------------
    // Layer 3: UI pass (back → front → overlay → cursor)
    // GLUIRenderer::ExecuteUIFromIR() is called directly from EndFrame_GL()
    // after FlushPendingInit() so GPU resources (slab texture etc.) are ready.
    (void)ui;

    // -------------------------------------------------------------------------
    // Layer 4: Text pass
    // GLTextRenderer::ExecuteTextFromIR() is called directly from EndFrame_GL()
    // after layer-2/3 sprite draws complete.
    (void)text;

    // -------------------------------------------------------------------------
    // Layer 5: Debug pass (compiled away in release if KFX_DEBUG_RENDERER unset)
    if (debug && caps.supportsDebugOverlay)
    {
        // TODO: dispatch DebugCommandBuffers to IDebugRenderer when implemented.
        (void)debug;
    }

    // NOTE: This Execute() is intentionally minimal for the initial wiring.
    // Subsequent tasks will replace the (void) stubs with real dispatch as
    // each logical renderer is migrated to consume from its IR command buffer
    // rather than calling the backend API directly.
}

/******************************************************************************/
