/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererVita.h
 *     PlayStation Vita renderer backend declaration.
 * @par Purpose:
 *     IRenderer for the PlayStation Vita — vitaGL GPU palette shader path.
 *
 *     8-bit indexed framebuffer → GL_LUMINANCE texture → Cg palette
 *     shader → 960×544 screen.  Zero CPU per-pixel cost.
 */
/******************************************************************************/
#pragma once

#ifdef PLATFORM_VITA

#include "IRenderer.h"

#include <vitaGL.h>
#include "renderer/vita/VitaBlitShader.h"
#include "renderer/vita/VitaLensRenderer.h"

/**
 * Vita renderer backend — vitaGL GPU palette shader path.
 */
class RendererVita : public IRenderer {
public:
    RendererVita();
    ~RendererVita() override;

    bool Init() override;
    void Shutdown() override;

    bool BeginFrame() override;
    void EndFrame() override;

    uint8_t* LockFramebuffer(int* out_pitch) override;
    void UnlockFramebuffer() override;

    const char* GetName() const override { return "Vita (vitaGL palette shader)"; }

    BackendCapabilities GetCapabilities() const override {
        BackendCapabilities c = {};
        c.hasGPURenderPath        = 1;
        c.wantsFullscreenViewport = 1;
        return c;
    }

    // Vita uses the software sub-renderers (the GPU path handles the framebuffer
    // blit + passes; UI/text/world/fade go through the CPU staging path).  Owned
    // here, created in Init(), destroyed in Shutdown().
    IWorldViewRenderer* GetWorldViewRenderer() override { return m_worldViewRenderer; }
    IMapFadePass*        GetMapFadePass()        override { return m_mapFadePass; }
    ILensRenderer*       GetLensRenderer()       override { return m_lensRenderer; }
    ITextRenderer*       GetTextRenderer()       override { return m_textRenderer; }
    IUIRenderer*         GetUIRenderer()         override { return m_uiRenderer; }
    ICursorLayer*        GetCursorLayer()        override { return m_cursorLayer; }

private:
    static const int k_gameW = 640;
    static const int k_gameH = 480;

    bool m_initialized = false;

    IWorldViewRenderer* m_worldViewRenderer = nullptr;
    IMapFadePass*       m_mapFadePass       = nullptr;
    VitaLensRenderer*   m_lensRenderer      = nullptr;
    ITextRenderer*      m_textRenderer      = nullptr;
    IUIRenderer*        m_uiRenderer        = nullptr;
    ICursorLayer*       m_cursorLayer       = nullptr;

    GLuint m_index_tex   = 0;   /**< 640×480 GL_LUMINANCE: 8-bit palette indices */
    GLuint m_palette_tex = 0;   /**< 256×1  GL_RGBA:       expanded palette colours */

    VitaBlitShader m_blit;      /**< fullscreen palette-decode blit shader */

};

#endif // PLATFORM_VITA
