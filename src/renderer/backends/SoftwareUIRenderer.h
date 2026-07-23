/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareUIRenderer.h
 *     Software implementation of IUIRenderer.
 * @par Design:
 *     Most CPU draw logic lives in IUIRenderer (the base).  This class supplies
 *     a distinct GetName(), plus the renderer-owned pixel-handoff surfaces that
 *     the GPU path already implements, so game code never needs a framebuffer
 *     pointer of its own:
 *
 *       AcquireMinimapBuffer() / SubmitMinimap()
 *
 *     Before this existed the base returned nullptr, game code wrote minimap
 *     pixels straight into the framebuffer, and SubmitMinimap() had to read
 *     them back out again to re-blit them in submission order.  Owning the
 *     buffer here removes that round trip and the engine-side branch with it.
 */
/******************************************************************************/
#pragma once

#include "renderer/IUIRenderer.h"

#include <vector>

/** Software UI renderer — inherits the CPU draw logic from IUIRenderer. */
class SoftwareUIRenderer final : public IUIRenderer {
public:
    const char* GetName() const override { return "SOFTWARE_UI"; }

    /** Return a renderer-owned size*size palette-index buffer, zero-filled.
     *  The caller fills it and then calls SubmitMinimap(). */
    uint8_t* AcquireMinimapBuffer(int size) override;

    /** Hand the filled buffer to the frame's IR so the replay blits it in
     *  submission order (masked to the minimap circle by MapShape*). */
    void SubmitMinimap(int screen_x, int screen_y, int size) override;

    /** Tile the GUI slab texture across the given rect.  Drawn immediately —
     *  it is a background, so it belongs under the deferred UI that replays on
     *  top of it in EndFrame.  (Folding this into the IR proper is Stage 5
     *  work, alongside the world compositing.) */
    bool SubmitSlabBackground(int x, int y, int w, int h) override;

private:
    std::vector<uint8_t> m_minimap_buf;
    int                  m_minimap_size = 0;
};
