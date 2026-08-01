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

    /** Record the slab-background tile in the UI command buffer so it replays in
     *  submission order with the rest of the UI, after the world (falls back to an
     *  immediate tile when there is no write window). */
    bool SubmitSlabBackground(int x, int y, int w, int h) override;

protected:
    /** Tile the GUI slab texture into the current WScreen — the realization used
     *  both by the immediate fallback and by the base merged replay. */
    void TileSlabBackground(int x, int y, int w, int h,
                            const TbGraphicsWindow& target) override;

private:
    std::vector<uint8_t> m_minimap_buf;
    int                  m_minimap_size = 0;
};
