/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareWorldViewRenderer.h
 *     CPU software implementation of IWorldViewRenderer.
 */
/******************************************************************************/
#ifndef SOFTWARE_WORLD_VIEW_RENDERER_H
#define SOFTWARE_WORLD_VIEW_RENDERER_H

#include "renderer/IWorldViewRenderer.h"

/******************************************************************************/

/**
 * CPU software implementation of IWorldViewRenderer.
 *
 * Calls the C rasterizer functions directly:
 *   BeginWorldPass  → setup_vecs(framebuf, NULL, pitch, w, h)
 *   FlushIsometric  → display_drawlist()
 *   FlushFrontView  → display_fast_drawlist(cam)
 */
class SoftwareWorldViewRenderer : public IWorldViewRenderer {
public:
    SoftwareWorldViewRenderer()  = default;
    ~SoftwareWorldViewRenderer() = default;

    void BeginWorldPass(uint8_t* framebuf, int pitch, int w, int h,
                        int vp_x, int vp_y) override;
    void DrawIsometricView() override;
    void DrawFrontView(struct Camera* cam) override;

    const char* GetName() const override { return "SOFTWARE"; }
};

/******************************************************************************/
#endif // SOFTWARE_WORLD_VIEW_RENDERER_H
