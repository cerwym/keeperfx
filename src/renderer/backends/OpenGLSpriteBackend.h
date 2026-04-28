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
 *     SubmitSpriteRemap performs a GPU double-lookup (atlas index → remap row
 *     → RGBA).  Sprites not in the atlas fall back to the CPU blitter.
 */
/******************************************************************************/
#pragma once

#ifdef RENDERER_OPENGL_ENABLED

#include "renderer/backends/IBackend.h"
#include "renderer/opengl/GLSpriteAtlas.h"
#include <glad/glad.h>
#include <vector>
#include <unordered_map>
#include <cstdint>

/** One GPU vertex for a sprite quad corner. */
struct GLSpriteVertex {
    float x, y;       // screen position              (attrib 0, offset  0)
    float u, v;       // atlas UV                     (attrib 1, offset  8)
    float r, g, b, a; // RGBA tint                    (attrib 2, offset 16)
    float mode;       // 0=palette,1=one-color,2=remap (attrib 3, offset 32)
    float z;          // NDC depth, [-1,+1]            (attrib 4, offset 36)
    float pal_row;    // normalised V into u_remap     (attrib 5, offset 40)
};

/** Intermediate per-submission data expanded to 6 vertices in flush(). */
struct SpriteQuad {
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
    float r, g, b, a;
    float mode;
    float z;       // NDC depth assigned from current bucket
    float pal_row; // normalised V into u_remap (0 for non-remap modes)
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
    void DrawNow()                         override;
    void SetScreenSize(int w, int h)       override;

    void OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet) override;
    void OnSpriteSheetFreed(const struct TbSpriteSheet* sheet)  override;
    void OnPaletteSet(const unsigned char* lbPalette)           override;

    const char* GetName() const override { return "OPENGL_SPRITE"; }

    /**
     * Set the NDC depth assigned to all sprites submitted until the next call.
     * Called by GLWorldViewRenderer::GPURenderNow before each bucket's sprite draw.
     * @param z_ndc  NDC z in [-1, +1]; -1 = near clip, +1 = far clip.
     */
    static void SetCurrentBucketZ(float z_ndc);

private:
    static constexpr int k_max_quads          = 8192;
    static constexpr int k_initial_remap_rows = 64;

    void flush();
    void upload_palette();
    void upload_remap_row(int row, const unsigned char* colortable);
    void grow_remap_texture();

    GLSpriteAtlas           m_atlas;
    GLuint                  m_shader     = 0;
    GLuint                  m_vao        = 0;
    GLuint                  m_vbo        = 0;
    GLuint                  m_texPalette = 0;
    GLuint                  m_texRemap   = 0;  // 256 × m_remap_capacity RGBA8
    int                     m_remap_capacity  = k_initial_remap_rows;
    std::vector<uint8_t>    m_remap_row_data; // capacity * 256 * 4; row cache for mid-frame grows
    GLint                   m_loc_inv_screen = -1;
    int                     m_override_w     = 0;  // 0 = use MyScreenWidth
    int                     m_override_h     = 0;  // 0 = use MyScreenHeight

    std::vector<SpriteQuad>                       m_quads;
    std::unordered_map<const unsigned char*, int> m_remap_cache;  // ptr → row index
    int                                           m_remap_row_count = 0;
    uint8_t                 m_local_palette[768] = {};
    bool                    m_palette_dirty      = true;
};

#endif // RENDERER_OPENGL_ENABLED
