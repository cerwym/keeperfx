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
    m_write_map_fade = std::nullopt;
}

void RenderGraph::Flip(const FrameState& fs)
{
    // Swap write ↔ read atomically (O(1) std::vector swap, no copies).
    m_write.Swap(m_read);

    // Capture FrameState snapshot from game-thread globals.
    m_read_fs = fs;

    // Transfer map-fade command to render-side.
    m_read_map_fade  = m_write_map_fade;
    m_write_map_fade = std::nullopt;

    // Reset the (now-old read-side, now-write-side) buffers so the game
    // thread can start writing frame N+1 immediately.
    m_write.Reset();
}

void RenderGraph::UpdateFrameState(const FrameState& fs)
{
    // Update only the FrameState on the read side.  IR buffers are unchanged.
    // Used during palette-fade loops where the same UI geometry must be
    // re-rendered each fade step with an updated palette.
    m_read_fs = fs;

    // Transfer the map-fade command even during preserve frames — the step
    // value changes each game tick and the render thread needs the latest value.
    m_read_map_fade = m_write_map_fade;
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
    (void)shadow;  // not yet implemented

    // -------------------------------------------------------------------------
    // Layer 2: World pass (geometry → shadows → sprites → world-UI)
    // GLWorldViewRenderer::ExecuteWorldFromIR() is called directly from
    // EndFrame_GL() inside the lens-FBO bracket so that the lens post-process
    // pass can operate on the world output.  Execute() documents the world IR
    // is owned by the graph but does not dispatch it here to preserve the
    // required FBO ordering.  IWorldViewRenderer::ExecuteFromIR() exists on
    // the interface for future backends that don't use a lens FBO.
    (void)world;

    // -------------------------------------------------------------------------
    // Layer 3: UI pass — populate render-thread quad/line buffers from IR.
    // PopulateFromIR() only fills the GPU geometry queues (m_rt_quads[] etc.);
    // it issues no GL draw calls.  DrawBack() / DrawFront() in EndFrame_GL()
    // then issue the actual draws in their required order relative to the
    // rawblit, FMV, PiP, and zoom-box passes.
    if (ui)
        ui->PopulateFromIR(m_read.ui, m_read_fs);

    // -------------------------------------------------------------------------
    // Layer 4: Text pass
    // GLTextRenderer::ExecuteTextFromIR() translates IR commands to deferred
    // draws AND flushes them in one call.  It must run after DrawFrontOverlay()
    // so text sits above all sprite layers.  EndFrame_GL() calls it directly.
    (void)text;

    // -------------------------------------------------------------------------
    // Layer 5: Debug pass (compiled away in release if KFX_DEBUG_RENDERER unset)
    if (debug && caps.supportsDebugOverlay)
    {
        // TODO: dispatch DebugCommandBuffers to IDebugRenderer when implemented.
        (void)debug;
    }
}

/******************************************************************************/
