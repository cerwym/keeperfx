/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file OpenGLSpriteBackend.h
 *     IBackend implementation for desktop OpenGL sprite rendering.
 * @par Purpose:
 *     Queues TbSprite submissions as SpriteQuads and flushes them to the
 *     screen via a palette-indexed GLSL shader at EndFrame time.
 *     Sprites are packed into a GLSpriteAtlas (4096×2048 R8) on sheet load.
 *
 *     SubmitSpriteRemap falls back to the CPU blitter (written into the
 *     RendererOpenGL staging buffer that is blitted in the prior pass).
 */
/******************************************************************************/
#ifndef OPENGL_SPRITE_BACKEND_H
#define OPENGL_SPRITE_BACKEND_H

#ifdef RENDERER_OPENGL_ENABLED

#include "renderer/backends/IBackend.h"
#include "renderer/opengl/GLSpriteAtlas.h"
#include <glad/glad.h>
#include <vector>
#include <cstdint>

/** One GPU vertex for a sprite quad corner. */
struct GLSpriteVertex {
    float x, y;       // screen position        (attrib 0, offset  0)
    float u, v;       // atlas UV               (attrib 1, offset  8)
    float r, g, b, a; // RGBA tint              (attrib 2, offset 16)
    float mode;       // 0=palette, 1=one-color (attrib 3, offset 32)
    float z;          // NDC depth, [-1,+1]     (attrib 4, offset 36)
};

/** Intermediate per-submission data expanded to 6 vertices in flush(). */
struct SpriteQuad {
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
    float r, g, b, a;
    float mode;
    float z;  // NDC depth assigned from current bucket
};

class OpenGLSpriteBackend : public IBackend {
public:
    OpenGLSpriteBackend()  = default;
    ~OpenGLSpriteBackend() override;

    OpenGLSpriteBackend(const OpenGLSpriteBackend&)            = delete;
    OpenGLSpriteBackend& operator=(const OpenGLSpriteBackend&) = delete;

    /** Must be called once after construction to set up GL resources. */
    bool Initialize();

    // ── IBackend ────────────────────────────────────────────────────────────
    TbResult SubmitSprite(long x, long y, const struct TbSprite* spr,
                          unsigned int draw_flags) override;

    TbResult SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                   unsigned char colour,
                                   unsigned int draw_flags) override;

    TbResult SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                               const unsigned char* colortable,
                               unsigned int draw_flags) override;

    void BeginFrame()                      override;
    void EndFrame()                        override;
    void FlushNow()                        override;
    void SetScreenSize(int w, int h)       override;

    void OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet) override;
    void OnSpriteSheetFreed(const struct TbSpriteSheet* sheet)  override;
    void OnPaletteSet(const unsigned char* lbPalette)           override;

    const char* GetName() const override { return "OPENGL_SPRITE"; }

    /**
     * Set the NDC depth assigned to all sprites submitted until the next call.
     * Called by GLWorldViewRenderer::GPUFlushNow before each bucket's sprite draw.
     * @param z_ndc  NDC z in [-1, +1]; -1 = near clip, +1 = far clip.
     */
    static void SetCurrentBucketZ(float z_ndc);

private:
    static constexpr int k_max_quads = 8192;

    void flush();
    void upload_palette();

    GLSpriteAtlas           m_atlas;
    GLuint                  m_shader     = 0;
    GLuint                  m_vao        = 0;
    GLuint                  m_vbo        = 0;
    GLuint                  m_texPalette = 0;
    GLint                   m_loc_inv_screen = -1;
    int                     m_override_w     = 0;  // 0 = use MyScreenWidth
    int                     m_override_h     = 0;  // 0 = use MyScreenHeight

    std::vector<SpriteQuad> m_quads;
    uint8_t                 m_local_palette[768] = {};
    bool                    m_palette_dirty      = true;
};

#endif // RENDERER_OPENGL_ENABLED
#endif // OPENGL_SPRITE_BACKEND_H
