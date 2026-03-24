/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererOpenGL.h
 *     OpenGL framebuffer blit renderer backend + world shader resources.
 */
/******************************************************************************/
#ifndef RENDERER_OPENGL_H
#define RENDERER_OPENGL_H

#include "IRenderer.h"

class GLTileAtlas;
class GLWorldViewRenderer;

/******************************************************************************/

/**
 * OpenGL renderer backend.
 *
 * The CPU-rendered 8-bit paletted framebuffer is blitted to screen via a
 * fullscreen palette-decode shader (index texture → RGBA via 1D palette).
 *
 * Also owns the shared GPU resources (fade table texture, tile atlas,
 * palette texture) injected into GLWorldViewRenderer by RendererManager.
 */
class RendererOpenGL : public IRenderer {
public:
    RendererOpenGL();
    ~RendererOpenGL() override;

    bool     Init() override;
    void     Shutdown() override;
    bool     BeginFrame() override;
    void     EndFrame() override;
    uint8_t* LockFramebuffer(int* out_pitch) override;
    void     UnlockFramebuffer() override;
    const char* GetName() const override;
    bool     SupportsRuntimeSwitch() const override;

    // Accessors — used by RendererManager to inject into GLWorldViewRenderer
    GLTileAtlas*  GetTileAtlas()  const { return m_tile_atlas; }
    unsigned int  GetFadeTex()    const { return m_texFade; }
    unsigned int  GetPaletteTex() const { return m_texPalette; }

    // Wired by RendererManager after GLWorldViewRenderer is created so that
    // EndFrame() can flush GPU geometry before the CPU blit overlay.
    void SetWorldRenderer(GLWorldViewRenderer* wr) { m_world_renderer = wr; }

private:
    bool compile_shaders();
    void upload_palette_texture();
    bool init_fade_table_texture();
    bool init_tile_atlas();

    // Staging buffer (CPU-side, 8-bit paletted, width * height bytes)
    uint8_t* m_stagingBuf  = nullptr;
    int      m_stagingW    = 0;
    int      m_stagingH    = 0;

    // GL objects — fullscreen palette-blit quad
    unsigned int m_vao          = 0;
    unsigned int m_vbo          = 0;
    unsigned int m_shader       = 0;
    unsigned int m_texIndex     = 0; // R8: 8-bit index texture (staging upload)
    unsigned int m_texPalette   = 0; // RGBA8 256-entry 1D palette

    // Shared GPU resources (owned here, injected into world renderer)
    unsigned int m_texFade      = 0; // R8 256×256: render_fade_tables lighting LUT
    GLTileAtlas* m_tile_atlas   = nullptr;

    // Not owned — set by RendererManager after world renderer creation
    GLWorldViewRenderer* m_world_renderer = nullptr;
};

/******************************************************************************/
#endif // RENDERER_OPENGL_H
