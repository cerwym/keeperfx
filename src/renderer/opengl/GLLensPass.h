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
    bool CompilePass(const char* frag_src);

    // Lazily compile shaders on first use.  GL resource creation must run on
    // the thread that owns the GL context (the render thread); effect Setup()
    // runs on the game thread, so Apply() calls this at its top rather than
    // compiling eagerly in Init().  Retries are suppressed after one failure.
    bool EnsureCompiled();

    // Bind the fullscreen quad, set the source texture on unit 0, bind the
    // destination FBO, draw, and unbind.  Derived classes should set their
    // own uniforms between calling BindPass() and DrawPass().
    void BindPass(unsigned int src_tex, unsigned int dst_fbo, int w, int h);
    void DrawPass();

    unsigned int m_prog = 0;
    unsigned int m_vao  = 0;
    unsigned int m_vbo  = 0;
    bool         m_compile_attempted = false;
};

/******************************************************************************/

class GLDisplacementPass : public GLLensPassBase {
public:
    bool Init() override;
    bool CompileShaders() override;
    void Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h) override;
    const char* RendererName() const override { return "GLDisplacementPass"; }

    void Configure(const LensGPUPassParams& params) override {
        SetMagnitude(params.displace_magnitude / 1000.0f);
        SetPeriod(params.displace_period);
    }

    void SetMagnitude(float mag) { m_magnitude = mag; }
    void SetPeriod(float per)    { m_period = per; }

private:
    int   m_loc_time      = -1;
    int   m_loc_magnitude = -1;
    int   m_loc_period    = -1;
    float m_magnitude     = 0.005f;
    float m_period        = 20.0f;
    float m_time          = 0.0f;
};

/******************************************************************************/

class GLMistPass : public GLLensPassBase {
public:
    bool Init() override;
    bool CompileShaders() override;
    void Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h) override;
    const char* RendererName() const override { return "GLMistPass"; }

    void Configure(const LensGPUPassParams& params) override {
        SetColor(params.mist_color[0], params.mist_color[1], params.mist_color[2], params.mist_color[3]);
    }

    void SetColor(float r, float g, float b, float density) {
        m_color[0] = r; m_color[1] = g; m_color[2] = b; m_color[3] = density;
    }

private:
    int   m_loc_time       = -1;
    int   m_loc_mist_color = -1;
    float m_color[4]       = {0.5f, 0.5f, 0.5f, 0.3f};
    float m_time           = 0.0f;
};

/******************************************************************************/

class GLFlyeyePass : public GLLensPassBase {
public:
    bool Init() override;
    bool CompileShaders() override;
    void Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h) override;
    const char* RendererName() const override { return "GLFlyeyePass"; }

    void Configure(const LensGPUPassParams& params) override {
        SetHexSize(params.flyeye_hex_size);
    }

    void SetHexSize(float size) { m_hex_size = size; }

private:
    int   m_loc_hex_size   = -1;
    int   m_loc_resolution = -1;
    float m_hex_size       = 0.03f;
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
