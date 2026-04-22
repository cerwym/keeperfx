/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLUIRenderer.h
 *     OpenGL hardware-accelerated UI element renderer.
 * @par Purpose:
 *     Renders status sprites, floating text, slab selectors, and room flags
 *     using GPU-accelerated batched quads with proper z-ordering.
 *     Eliminates CPU staging buffer conflicts that cause UI flickering.
 */
/******************************************************************************/
#ifndef GLUI_RENDERER_H
#define GLUI_RENDERER_H

#include "renderer/IUIRenderer.h"
#include <vector>
#include <cstdint>

#ifdef RENDERER_OPENGL_ENABLED

#include <glad/glad.h>

class GLSpriteAtlas;
class GLFontAtlas;

/******************************************************************************/

/** GPU vertex for UI element quad corners. */
struct GLUIVertex {
    float x, y;       // Screen position (pixels)
    float u, v;       // Texture UV coordinates
    float r, g, b, a; // RGBA color/tint
    float z;          // NDC depth for z-ordering
    float mode;       // Render mode: 0=sprite, 1=text, 2=line, 3=solid_color
};

/** Batched UI element data before vertex expansion. */
struct UIQuad {
    float x0, y0, x1, y1;  // Screen rectangle
    float u0, v0, u1, v1;  // Texture coordinates  
    float r, g, b, a;      // Color/tint
    float z;               // Z-depth
    float mode;            // Render mode
    uint32_t texture_id;   // Sprite sheet texture ID
    uint8_t layer;         // 0=back (before staging blit), 1=front (after staging blit)
};

/** Line segment for slab selectors. */
struct UILine {
    float x1, y1, x2, y2;  // Line endpoints
    float r, g, b, a;      // Line color
    float z;               // Z-depth
    float thickness;       // Line thickness
    uint8_t layer;         // 0=back, 1=front
};

/** Batched UI element with a player-colour remap applied via the fade table. */
struct UIRemapQuad {
    float x0, y0, x1, y1;  // Screen rectangle
    float u0, v0, u1, v1;  // Atlas UV coordinates
    float r, g, b, a;      // Color tint
    float z;               // Z-depth
    uint8_t layer;         // 0=back, 1=front
    int   remap_row;       // Row in fade table (0–255)
};

/** Picture-in-picture FBO composite quad.  Uses the FBO colour texture directly
 *  (no palette lookup).  Rendered before layer-1 atlas sprites so the zoom-box
 *  frame corners appear on top of the isometric PiP content. */
struct FBOQuad {
    float x0, y0, x1, y1;   // Screen rectangle (pixels)
    GLuint tex_id;           // RGBA8 FBO colour texture
};

/**
 * OpenGL implementation of IUIRenderer.
 * Batches UI elements and renders with GPU shaders to eliminate flickering.
 */
class GLUIRenderer : public IUIRenderer {
public:
    GLUIRenderer();
    virtual ~GLUIRenderer();

    // IUIRenderer interface
    virtual void SubmitSlabSelector(int x1, int y1, int x2, int y2, unsigned char color, float z_depth) override;
    virtual void SubmitPanelSprite(int32_t x, int32_t y, int units_per_px,
                                   SpriteHandle spr, bool flip_horiz = false) override;
    virtual void SubmitPanelSpriteRemap(int32_t x, int32_t y, int units_per_px,
                                       SpriteHandle spr, int remap_row) override;
    virtual void SubmitPanelSpriteColored(int32_t x, int32_t y, int units_per_px,
                                          SpriteHandle spr, uint8_t color_idx) override;
    virtual void SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h,
                                    SpriteHandle spr) override;
    virtual void SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx) override;
    virtual void SubmitSolidBoxAlpha(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx, float alpha) override;
    virtual void UpdateSlabTexture(const uint8_t* data, int dim) override;
    virtual bool SubmitSlabBackground(int x, int y, int w, int h) override;
    virtual uint8_t* AcquireMinimapBuffer(int size) override;
    virtual void SubmitMinimap(int screen_x, int screen_y, int size) override;

    /** Submit an RGBA8 FBO colour texture as a picture-in-picture quad.
     *  Rendered during FlushFront() before atlas-sprite layer-1 quads so that
     *  decorative frame sprites appear on top of the isometric content.  Not
     *  part of IUIRenderer — called directly from RendererOpenGL::EndFrame(). */
    void SubmitFBOQuad(int x, int y, int w, int h, GLuint tex_id);
    virtual void SetLayer(int layer) override;
    virtual void SetWorldDepth(float ndc_z) override;
    virtual void ClearWorldDepth() override;
    virtual void SetTopOverlay() override;
    virtual void ClearTopOverlay() override;
    virtual void FlushBack() override;
    virtual void FlushFront() override;
    virtual void Flush() override;
    virtual void Clear() override;
    virtual const char* GetName() const override { return "OPENGL_UI"; }
    virtual bool IsGpuAccelerated() const override { return true; }

    /** Flush any atlas-quad sprites submitted since the last FlushFront().
     *  Called by GLCursorLayer::Flush() to render the OS pointer sprite
     *  as the final draw before the buffer swap. */
    void FlushCursorSprites();

    /** Record the current queue sizes as the PiP "start of pass" snapshot.
     *  Call this immediately before the PiP draw_view() call.  Any quads
     *  submitted after this point (up until FlushPiPSprites()) are treated
     *  as PiP-sourced and will be routed into the FBO rather than the main
     *  frame. */
    void BeginPiPSprites();

    /** Flush all UIRenderer quads queued since BeginPiPSprites() into the
     *  currently-bound PiP FBO (call while the FBO is still bound), then
     *  remove them from the queue so FlushFront() never sees them at
     *  full-screen coordinates.
     *  - Layer-2 quads (creature status/gold text): rendered with GL_LEQUAL
     *    depth test so they occlude correctly behind walls.
     *  - Layer-1 quads (room flags, slab selector): rendered without depth
     *    test so they always appear on top inside the zoom box.
     *  Uses pip_w x pip_h for NDC conversion. */
    void FlushPiPSprites(int pip_w, int pip_h);

    /** Initialize OpenGL resources.
     *  @return true if successful */
    bool Init();

    /** Clean up GPU resources. */
    void Shutdown();

    /** Set screen dimensions for coordinate conversion.
     *  @param width Screen width in pixels
     *  @param height Screen height in pixels */
    void SetScreenDimensions(int width, int height);

    /** Set sprite atlas for UI element textures.
     *  @param atlas Sprite atlas containing UI textures
     *  @return true if successful */
    bool SetSpriteAtlas(GLSpriteAtlas* atlas);

    /** Set font atlas for text rendering.
     *  @param atlas Font atlas for floating text
     *  @return true if successful */
    bool SetFontAtlas(GLFontAtlas* atlas);

    /** Set palette texture for color lookups.
     *  @param palette_texture_id OpenGL texture ID for 256-color palette
     *  @return true if successful */
    bool SetPaletteTexture(GLuint palette_texture_id, GLenum target = GL_TEXTURE_2D);

    /** Set the fade-table texture for player-colour remap draws.
     *  @param tex GL_TEXTURE_2D, R8, 256×256 — owned by RendererOpenGL */
    void SetFadeTexture(GLuint tex);

private:
    // GPU shader programs — one per rendering domain to isolate texture unit bindings.
    GLuint m_prog_sprite;          // palette-indexed atlas sprites (unit 0 = atlas R8, unit 1 = palette 1D)
    GLuint m_prog_sprite_colored;  // atlas-as-mask, flat vertex colour output (unit 0 = atlas R8)
    GLuint m_prog_font;     // glyph rendering (unit 0 = font atlas RGBA)
    GLuint m_prog_solid;    // solid-colour quads and lines (no textures)
    GLuint m_prog_remap;    // fade-table remap sprites (units 0 atlas, 1 palette, 2 fade)
    GLuint m_prog_fbo;      // FBO/PiP composite (unit 0: RGBA8 colour attachment)

    // Per-program u_screen_size uniform locations.
    GLint  m_loc_screen_sprite;
    GLint  m_loc_screen_sprite_colored;
    GLint  m_loc_screen_font;
    GLint  m_loc_screen_solid;
    GLint  m_loc_screen_remap;
    GLint  m_loc_remap_row;
    GLint  m_loc_screen_fbo;

    GLuint m_vao;
    GLuint m_vbo;
    GLuint m_uniform_mvp;
    GLuint m_uniform_texture;

    // Rendering data
    std::vector<UIQuad> m_ui_quads;
    std::vector<UILine> m_ui_lines;
    std::vector<FBOQuad> m_fbo_quads;   // PiP composite quads — drawn first in FlushFront
    std::vector<GLUIVertex> m_vertices;
    
    // Screen properties
    int m_screen_width;
    int m_screen_height;
    
    // Resources
    GLSpriteAtlas* m_sprite_atlas;
    GLFontAtlas* m_font_atlas;
    GLuint m_palette_texture;
    GLenum m_palette_texture_target;   // GL_TEXTURE_2D — set by SetPaletteTexture()
    GLuint m_fade_texture;             // R8 256×256 remap LUT — set by SetFadeTexture(), not owned

    // Slab background tile texture (64×64 R8, GL_REPEAT) — uploaded via UpdateSlabTexture()
    GLuint m_slab_texture = 0;
    int    m_slab_dim     = 0;

    // Batched player-colour remap quads (flushed alongside front-layer quads)
    std::vector<UIRemapQuad> m_remap_quads;

    // PiP sprite watermarks: queue sizes recorded at BeginPiPSprites() time.
    // Quads/lines at indices [0..watermark) were submitted before the PiP draw_view
    // (i.e. corner-frame sprites) and must survive into FlushFront() untouched.
    // Quads at [watermark..end) were submitted during draw_view(pip_cam) (NSP sprites,
    // room flags) and are rendered into the FBO by FlushPiPSprites(), then erased.
    int  m_pip_quad_watermark  = 0;
    int  m_pip_remap_watermark = 0;
    int  m_pip_line_watermark  = 0;
    bool m_pip_capture_active  = false;

    // Minimap: renderer-owned CPU scratch buffer + deferred GL texture upload
    uint8_t* m_minimap_cpu_buf;    // renderer-owned; returned by AcquireMinimapBuffer
    int      m_minimap_cpu_size;   // current allocation side length (0 = none)
    GLuint   m_minimap_texture;
    int      m_minimap_tex_size;   // currently allocated GL texture side (0 = none)
    int      m_minimap_x;
    int      m_minimap_y;
    int      m_minimap_size;
    bool     m_minimap_pending;
    
    // Current render layer: 0=back (before staging blit), 1=front (after staging blit),
    // 2=world-depth (after GPUFlushNow, depth test ON against tile depth buffer).
    // Set by SetLayer()/SetWorldDepth(); reset to 1 each Clear().
    int   m_current_layer       = 1;
    float m_world_z             = 0.0f;  // NDC z for active world-depth batch
    bool  m_world_depth_active  = false; // when true, SubmitQuad/SubmitLine use layer=2, z=m_world_z
    bool  m_top_overlay_active  = false; // when true, SubmitQuad/SubmitLine use layer=3 (drawn last, depth-test OFF)

    // Internal methods
    bool CreateShaders();
    void CreateVertexArrays();
    void FlushQuads(int layer);       // Render and remove only quads matching layer
    void FlushRemapQuads(int layer);  // Render and remove remap quads for this layer
    void FlushLines(int layer);       // Render and remove only lines matching layer
    void ExpandQuadToVertices(const UIQuad& quad);
    void ExpandLineToVertices(const UILine& line);
    void SubmitQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, 
                   float r, float g, float b, float a, float z, float mode, uint32_t texture_id = 0);
    void SubmitLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a, 
                   float z, float thickness = 2.0f);
};

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
#endif // GLUI_RENDERER_H