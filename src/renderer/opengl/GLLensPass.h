/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLLensPass.h
 *     OpenGL implementations of IPostProcessPass for lens effects.
 *     Each class wraps a fragment shader that operates on the decoded RGBA
 *     scene texture and writes to an FBO (ping-pong chain).
 */
/******************************************************************************/
#pragma once

#include "renderer/IPostProcessPass.h"
#include "renderer/opengl/IGLShaderCompilable.h"

/******************************************************************************/

/** Base helper for all GL lens passes — owns a shader program and a
 *  fullscreen quad VAO that are shared across Apply() calls. */
class GLLensPassBase : public IPostProcessPass, public IGLShaderCompilable {
public:
    virtual ~GLLensPassBase();
    void Free() override;

protected:
    // Compile the pass shader from the given fragment GLSL source string.
    // Vertex shader is always PALETTE_BLIT_VERTEX_SHADER (pos+uv fullscreen quad).
    // Sets m_prog and creates the shared fullscreen-quad VAO.
    bool CompilePass(const char* frag_src);

    // Compile+link a standalone program (vs = PALETTE_BLIT_VERTEX_SHADER, fs =
    // frag_src) and bind its u_texture sampler to unit 0. Returns the program name
    // (0 on failure). Does NOT touch m_prog/m_vao — used for auxiliary programs
    // (e.g. a pass with two shader variants sharing one VAO).
    unsigned int CompileProgram(const char* frag_src);

    // Lazily compile shaders on first use.  GL resource creation must run on
    // the thread that owns the GL context (the render thread); effect Setup()
    // runs on the game thread, so Apply() calls this at its top rather than
    // compiling eagerly in Init().  Retries are suppressed after one failure.
    bool EnsureCompiled();

    // Bind the fullscreen quad, set the source texture on unit 0, bind the
    // destination FBO, draw, and unbind.  Derived classes should set their
    // own uniforms between calling BindPass() and DrawPass().
    void BindPass(unsigned int src_tex, unsigned int dst_fbo, int w, int h);
    // As BindPass() but activates an arbitrary program (for passes with more than
    // one shader variant sharing the base VAO).
    void BindPassProgram(unsigned int prog, unsigned int src_tex, unsigned int dst_fbo, int w, int h);
    void DrawPass();

    unsigned int m_prog = 0;
    unsigned int m_vao  = 0;
    unsigned int m_vbo  = 0;
    bool         m_compile_attempted = false;
};

/******************************************************************************/

/** Geometric remap pass — shared by the Displacement and Flyeye effects. Both
 *  supply an exact per-output-pixel source lookup table (RG16: src_x, src_y),
 *  which this pass uploads to a GL_RG16UI texture and samples the scene through.
 *  The upload is deferred to Apply() (render thread) and gated by remap_version
 *  so an unchanged table is uploaded once, not every frame. */
class GLRemapPass : public GLLensPassBase {
public:
    ~GLRemapPass() override;
    bool Init() override;
    bool CompileShaders() override;
    void Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h) override;
    void Free() override;
    const char* RendererName() const override { return "GLRemapPass"; }

    // Stage the remap table for a render-thread upload on the next Apply(). The
    // table bytes are owned by the IR (re-pointed into params.remap_data by the
    // renderer); we only stash the pointer + dims + version here.
    void Configure(const LensGPUPassParams& params) override {
        if (params.remap_data != nullptr &&
            params.remap_version != m_uploaded_version &&
            params.remap_w > 0 && params.remap_h > 0)
        {
            m_pending_remap   = params.remap_data;
            m_pending_w       = params.remap_w;
            m_pending_h       = params.remap_h;
            m_pending_version = params.remap_version;
            m_remap_dirty     = true;
        }
    }

    bool UploadRemap(const unsigned char* rg16, int w, int h);

private:
    int          m_loc_remap_dim = -1;
    unsigned int m_remap_tex     = 0;
    int          m_remap_w       = 0;
    int          m_remap_h       = 0;

    // Remap table pending a render-thread upload (see Configure()).
    const unsigned char* m_pending_remap = nullptr;
    int          m_pending_w        = 0;
    int          m_pending_h        = 0;
    uint32_t     m_pending_version  = 0;
    uint32_t     m_uploaded_version = 0;
    bool         m_remap_dirty      = false;
};

/******************************************************************************/

class GLMistPass : public GLLensPassBase {
public:
    ~GLMistPass() override;
    bool Init() override;
    bool CompileShaders() override;
    void Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h) override;
    void Free() override;
    const char* RendererName() const override { return "GLMistPass"; }

    // Stage the 256x256 mist amplitude texture for a render-thread upload and
    // capture the current animation offsets + fog colour. The offsets are advanced
    // on the game thread (frame-rate independent) and delivered here every frame;
    // the renderer only hands over mist_data on the frame the texture actually
    // changed (like the remap path), so an unchanged texture is uploaded once.
    void Configure(const LensGPUPassParams& params) override {
        SetColor(params.mist_color[0], params.mist_color[1], params.mist_color[2], params.mist_color[3]);
        m_pos_x = params.mist_pos_x;
        m_pos_y = params.mist_pos_y;
        m_sec_x = params.mist_sec_x;
        m_sec_y = params.mist_sec_y;
        m_mist_lightness = params.mist_lightness;
        m_truecolor      = params.mist_truecolor;
        if (params.mist_data != nullptr)
        {
            m_pending_mist = params.mist_data;
            m_mist_dirty   = true;
        }
    }

    void SetColor(float r, float g, float b, float density) {
        m_color[0] = r; m_color[1] = g; m_color[2] = b; m_color[3] = density;
    }

    // Provide the shared GL textures the accurate (paletted) mist path needs:
    // the 256x1 RGBA palette, the 256x256 R8 fade table, and the 64^3 R8 reverse
    // RGB->index LUT. Owned by GLLensRenderer / RendererOpenGL; set every frame
    // before Apply(). Zero handles force the truecolor path (safe fallback).
    void SetAccurateResources(unsigned int palette_tex, unsigned int fade_tex, unsigned int rgb2idx_tex) {
        m_tex_palette = palette_tex;
        m_tex_fade    = fade_tex;
        m_tex_rgb2idx = rgb2idx_tex;
    }

    bool UploadMist(const unsigned char* r8_256x256);

private:
    // Whether the accurate paletted path can run this frame.
    bool AccurateReady() const {
        return !m_truecolor && m_prog_accurate != 0 &&
               m_tex_palette != 0 && m_tex_fade != 0 && m_tex_rgb2idx != 0;
    }

    int   m_loc_pos        = -1;
    int   m_loc_sec        = -1;
    int   m_loc_mist_color = -1;
    float m_color[4]       = {0.5f, 0.5f, 0.5f, 0.3f};

    // Accurate (paletted) shader variant + its uniform locations.
    unsigned int m_prog_accurate     = 0;
    int   m_loc_acc_pos       = -1;
    int   m_loc_acc_sec       = -1;
    int   m_loc_acc_lightness = -1;
    int   m_mist_lightness    = 0;
    bool  m_truecolor         = false;
    // Shared textures for the accurate path (not owned).
    unsigned int m_tex_palette = 0;
    unsigned int m_tex_fade    = 0;
    unsigned int m_tex_rgb2idx = 0;

    unsigned int m_mist_tex = 0;

    // Current animation offsets, supplied each frame by Configure() (game-thread
    // owned phase). The pass no longer self-accumulates, so drift speed follows
    // game.delta_time rather than the render frame rate.
    float m_pos_x = 0.0f,  m_pos_y = 0.0f;
    float m_sec_x = 50.0f, m_sec_y = 128.0f;

    // Mist texture pending a render-thread upload (see Configure()).
    const unsigned char* m_pending_mist = nullptr;
    bool  m_mist_dirty  = false;
    bool  m_mist_staged = false; // texture uploaded once per pass lifetime
};

/******************************************************************************/

class GLOverlayPass : public GLLensPassBase {
public:
    bool Init() override;
    bool CompileShaders() override;
    void Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h) override;
    void Free() override;
    const char* RendererName() const override { return "GLOverlayPass"; }

    // Stores the overlay source for a render-thread upload on the next Apply();
    // the actual GL upload must not run here (Setup() is on the game thread).
    void Configure(const LensGPUPassParams& params) override {
        m_pending_overlay = params.overlay_data;
        m_pending_w       = params.overlay_w;
        m_pending_h       = params.overlay_h;
        m_overlay_dirty   = (params.overlay_data != nullptr);
        SetOverlayAlpha(params.overlay_alpha);
    }

    void SetOverlayAlpha(float a) { m_alpha = a; }
    bool UploadOverlay(const unsigned char* rgba, int w, int h);

private:
    int          m_loc_overlay_alpha = -1;
    unsigned int m_overlay_tex       = 0;
    unsigned int m_null_overlay_tex  = 0; // 1×1 transparent fallback for when no overlay is loaded
    int          m_overlay_w         = 0;
    int          m_overlay_h         = 0;
    float        m_alpha             = 1.0f;
    // Overlay source pending a render-thread upload (see Configure()).
    const unsigned char* m_pending_overlay = nullptr;
    int          m_pending_w         = 0;
    int          m_pending_h         = 0;
    bool         m_overlay_dirty     = false;
};

/******************************************************************************/
