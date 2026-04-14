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
class GLWorldViewRenderer;

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

/** Deferred keeper-hand sprite, captured at SubmitKeeperSprite() time. */
struct PendingHandSprite {
    short x, y;
    unsigned short kspr_base;
    short angle;
    unsigned char sprgroup;
    long scale;
    unsigned int draw_flags;
    unsigned char draw_alpha;  // EngineSpriteDrawUsingAlpha at submit time
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
    virtual void SubmitKeeperSprite(short x, short y, unsigned short kspr_base,
                                    short angle, unsigned char sprgroup, long scale) override;
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
    virtual void SetLayer(int layer) override;
    virtual void SetWorldDepth(float ndc_z) override;
    virtual void ClearWorldDepth() override;
    virtual void SetTopOverlay() override;
    virtual void ClearTopOverlay() override;
    virtual void FlushHandSprites() override;
    virtual void FlushBack() override;
    virtual void FlushFront() override;
    virtual void Flush() override;
    virtual void Clear() override;
    virtual const char* GetName() const override { return "OPENGL_UI"; }
    virtual bool IsGpuAccelerated() const override { return true; }

    /** Initialize OpenGL resources.
     *  @return true if successful */
    bool Init();

    /** Clean up GPU resources. */
    void Shutdown();

    /** Set world-view renderer for hand sprite rendering during Flush().
     *  Must be set before the first Flush() call in OpenGL mode. */
    void SetWorldViewRenderer(GLWorldViewRenderer* wvr);

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

    // Per-program u_screen_size uniform locations.
    GLint  m_loc_screen_sprite;
    GLint  m_loc_screen_sprite_colored;
    GLint  m_loc_screen_font;
    GLint  m_loc_screen_solid;
    GLint  m_loc_screen_remap;
    GLint  m_loc_remap_row;

    GLuint m_vao;
    GLuint m_vbo;
    GLuint m_uniform_mvp;
    GLuint m_uniform_texture;

    // Rendering data
    std::vector<UIQuad> m_ui_quads;
    std::vector<UILine> m_ui_lines;
    std::vector<GLUIVertex> m_vertices;
    
    // Screen properties
    int m_screen_width;
    int m_screen_height;
    
    // Resources
    GLSpriteAtlas* m_sprite_atlas;
    GLFontAtlas* m_font_atlas;
    GLuint m_palette_texture;
    GLenum m_palette_texture_target;   // GL_TEXTURE_1D or GL_TEXTURE_2D — set by SetPaletteTexture()
    GLuint m_fade_texture;             // R8 256×256 remap LUT — set by SetFadeTexture(), not owned

    // Slab background tile texture (64×64 R8, GL_REPEAT) — uploaded via UpdateSlabTexture()
    GLuint m_slab_texture = 0;
    int    m_slab_dim     = 0;

    // World-view renderer reference for hand sprite Flush()
    GLWorldViewRenderer* m_world_view_renderer;

    // Deferred keeper-hand sprites (flushed after frame setup in Flush())
    std::vector<PendingHandSprite> m_pending_hand_sprites;

    // Batched player-colour remap quads (flushed alongside front-layer quads)
    std::vector<UIRemapQuad> m_remap_quads;

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