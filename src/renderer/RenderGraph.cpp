/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RenderGraph.cpp
 *     RenderGraph implementation — buffer lifecycle, Flip(), Execute().
 */
/******************************************************************************/
#include "renderer/RenderGraph.h"

#include "renderer/IFrameGraphExecutor.h"

/******************************************************************************/
// RenderGraph::FrameBuffers
/******************************************************************************/

void RenderGraph::FrameBuffers::Reset()
{
    world.Reset();
    ui.Reset();
    text.Reset();
    post_process.Reset();
    image_present.Reset();
}

void RenderGraph::FrameBuffers::Reserve(
    size_t world_tiles, size_t world_sprites, size_t world_shadows,
    size_t ui_cmds,     size_t text_cmds)
{
    world.Reserve(world_tiles, world_sprites, world_shadows);
    ui.Reserve(ui_cmds);
    text.Reserve(text_cmds);
    // Full-screen presents are few per frame (usually 1); a small fixed reserve
    // avoids the first-frame realloc without over-allocating owned pixel buffers.
    image_present.Reserve(8);
}

void RenderGraph::FrameBuffers::Swap(FrameBuffers& other)
{
    world.Swap(other.world);
    ui.Swap(other.ui);
    text.Swap(other.text);
    post_process.Swap(other.post_process);
    image_present.Swap(other.image_present);
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
    size_t ui_cmds,     size_t text_cmds)
{
    // Reserve both write and read sides so neither triggers a realloc
    // during a frame.
    m_write.Reserve(world_tiles, world_sprites, world_shadows,
                    ui_cmds, text_cmds);
    m_read.Reserve(world_tiles,  world_sprites, world_shadows,
                   ui_cmds, text_cmds);
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

void RenderGraph::UpdateFrameState(const FrameState& fs)
{
    // Update only the FrameState on the read side.  IR buffers are unchanged.
    // Used during palette-fade loops where the same UI geometry must be
    // re-rendered each fade step with an updated palette.
    m_read_fs = fs;
}

void RenderGraph::Execute(IFrameGraphExecutor& exec)
{
    // This function OWNS the per-frame execution order.  Each call
    // is one ordered phase realised by the backend.
    // Ordering constraints:
    //   • World must run INSIDE the lens FBO bracket
    //     (FGBeginWorldCapture … FGResolveWorldCapture) so post-process passes
    //     operate on the captured scene before it reaches the screen.
    //   • Map-fade must run AFTER world but BEFORE image-presents / overhead so
    //     those queues are still available for the parchment FBO capture.
    //   • The deferred world-view capture runs AFTER GameUI so the sidebar is
    //     part of the crossfaded snapshot.
    //   • Screenshot / scRGB-lift run last, after all draws, before the swap
    //     (which stays in the backend's EndFrame).

    // Frame setup.
    exec.FGClearFrame();
    exec.FGPopulateUI();

    // World inside the lens scene-capture bracket.
    exec.FGBeginWorldCapture();
    exec.FGExecuteWorld();
    exec.FGFlushSwipeOverlay();
    exec.FGResolveWorldCapture();
    exec.FGApplyLensPaletteUIExclusion();

    // Map-fade compose (after world, before presents / overhead).
    exec.FGExecuteMapFade();

    // World-space sprite / flat overlay layers.
    exec.FGDrawWorldSpriteLayer();
    exec.FGDrawWorldOverlayFlatLayer();

    // Full-screen image presents (backgrounds / parchment / FMV).
    exec.FGExecuteImagePresents();

    // Overhead map / PiP captures.
    exec.FGDrawOverheadMap();
    exec.FGExecutePiPCaptures();

    // Game UI, then the deferred world-view capture.
    exec.FGDrawGameUI();
    exec.FGCaptureWorldFrameIfPending();

    // Zoom-box tiles (on top of GameUI), front overlay, text.
    exec.FGDrawZoomBoxes();
    exec.FGDrawFrontOverlay();
    exec.FGExecuteText();

    // Full-screen tint, cursor, dev overlay.
    exec.FGDrawScreenTint();
    exec.FGExecuteCursor();
    exec.FGDrawDevToolsOverlay();

    // Present-time captures (before the backend's buffer swap).
    exec.FGCaptureScreenshot();
    exec.FGApplyScrgbLift();
}

/******************************************************************************/
