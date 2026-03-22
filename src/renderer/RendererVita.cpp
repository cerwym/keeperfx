/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererVita.cpp
 *     PlayStation Vita renderer backend implementation.
 * @par Purpose:
 *     IRenderer for PlayStation Vita — vitaGL GPU palette shader path.
 *
 *     vita_vitagl_preinit() is called from LbScreenInitialize() BEFORE
 *     SDL_Init so vitaGL owns the GXM display context.  Init() then sets up
 *     the GL textures and the blit shader program.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/RendererVita.h"

#ifdef PLATFORM_VITA

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bflib_video.h"
#include "bflib_vidsurface.h"
#include "globals.h"
#include "renderer/IPostProcessPass.h"
#include "kfx/lense/LensManager.h"

#include <psp2/io/stat.h>    // sceIoMkdir
#include <psp2/kernel/sysmem.h>  // sceKernelAllocMemBlock probe
extern "C" {
#include "platform/vita_sce_diag.h"
}

#include "post_inc.h"

extern "C" bool vita_is_vitagl_ready(void);

static const float k_quad_pos[4][2] = {
    { -1.0f,  1.0f },
    {  1.0f,  1.0f },
    { -1.0f, -1.0f },
    {  1.0f, -1.0f },
};

/** Allocate a RGBA render target FBO with a backing texture.
 *  Returns false and logs an error if the FBO is incomplete. */
static bool create_rgba_fbo(int w, int h, GLuint& out_fbo, GLuint& out_tex)
{
    glGenTextures(1, &out_tex);
    glBindTexture(GL_TEXTURE_2D, out_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &out_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, out_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out_tex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        ERRORLOG("[vitaGL] FBO incomplete: 0x%x", status);
        return false;
    }
    return true;
}

/******************************************************************************/
// RendererVita
/*****************************************************************************/

RendererVita::RendererVita() = default;
RendererVita::~RendererVita() { Shutdown(); }

bool RendererVita::Init()
{
    if (m_initialized) return true;

    if (!vita_is_vitagl_ready()) {
        ERRORLOG("RendererVita: vitaGL preinit failed");
        return false;
    }

    {
        SYNCLOG("Init: RendererVita: vitaGL ready — GPU palette path active");
        // vitaGL context is up — set up GL resources.
        glGenTextures(1, &m_index_tex);
        glBindTexture(GL_TEXTURE_2D, m_index_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, k_gameW, k_gameH, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
        { GLenum _e = glGetError(); if (_e != GL_NO_ERROR) ERRORLOG("[vitaGL] GL error 0x%x after index glTexImage2D", _e); }

        glGenTextures(1, &m_palette_tex);
        glBindTexture(GL_TEXTURE_2D, m_palette_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        { GLenum _e = glGetError(); if (_e != GL_NO_ERROR) ERRORLOG("[vitaGL] GL error 0x%x after palette glTexImage2D", _e); }

        if (!m_blit.Init()) {
            Shutdown();
            return false;
        }

        if (!m_passthrough.Init()) {
            Shutdown();
            return false;
        }

        if (!create_rgba_fbo(k_gameW, k_gameH, m_scene_fbo, m_scene_tex) ||
            !create_rgba_fbo(k_gameW, k_gameH, m_pass_fbo_a, m_pass_tex_a) ||
            !create_rgba_fbo(k_gameW, k_gameH, m_pass_fbo_b, m_pass_tex_b)) {
            Shutdown();
            return false;
        }
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        m_initialized = true;
        SYNCLOG("RendererVita: vitaGL palette shader initialised (%dx%d -> 960x544)", k_gameW, k_gameH);
        return true;
    }
}

void RendererVita::Shutdown()
{
    if (!m_initialized) return;

    m_blit.Free();
    m_passthrough.Free();
    if (m_index_tex)   { glDeleteTextures(1, &m_index_tex);   m_index_tex   = 0; }
    if (m_palette_tex) { glDeleteTextures(1, &m_palette_tex); m_palette_tex = 0; }

    auto del_fbo = [](GLuint& fbo, GLuint& tex) {
        if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (tex) { glDeleteTextures(1, &tex);      tex = 0; }
    };
    del_fbo(m_scene_fbo,  m_scene_tex);
    del_fbo(m_pass_fbo_a, m_pass_tex_a);
    del_fbo(m_pass_fbo_b, m_pass_tex_b);

    m_initialized = false;
}

bool RendererVita::BeginFrame()
{
    return m_initialized;
}

void RendererVita::EndFrame()
{
    if (!m_initialized) return;

    {
        const int w = lbDrawSurface->w;
        const int h = lbDrawSurface->h;

        SDL_LockSurface(lbDrawSurface);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_index_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                        GL_LUMINANCE, GL_UNSIGNED_BYTE, lbDrawSurface->pixels);
        SDL_UnlockSurface(lbDrawSurface);

        uint8_t rgba[256 * 4];
        for (int i = 0; i < 256; i++) {
            rgba[i*4+0] = (uint8_t)(lbPalette[i*3+0] << 2);
            rgba[i*4+1] = (uint8_t)(lbPalette[i*3+1] << 2);
            rgba[i*4+2] = (uint8_t)(lbPalette[i*3+2] << 2);
            rgba[i*4+3] = 0xFF;
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_palette_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba);

        const float u1 = (float)w / (float)k_gameW;
        const float v1 = (float)h / (float)k_gameH;
        const float dyn_uv[4][2] = {
            { 0.0f, 0.0f }, { u1, 0.0f }, { 0.0f, v1 }, { u1, v1 },
        };

        // Collect GPU-side post-process passes from active lens effects.
        // These run on the GPU instead of the CPU lens path in LensManager.
        std::vector<IPostProcessPass*> gpu_passes;
        {
            LensManager* lm = LensManager::GetInstance();
            if (lm && lm->IsReady()) {
                for (LensEffect* e : lm->GetEffects()) {
                    if (e->IsEnabled()) {
                        IPostProcessPass* p = e->GetGPUPass();
                        if (p) gpu_passes.push_back(p);
                    }
                }
            }
        }

        // Stage 1 — palette decode: index_tex + palette_tex → (scene FBO when GPU
        //   passes are active, directly to screen when running CPU-only effects).
        if (gpu_passes.empty()) {
            // Direct-to-screen path: ensure we're rendering to the default
            // framebuffer at the full display size.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, 960, 544);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fbo);
            glViewport(0, 0, k_gameW, k_gameH);
        }

        // Explicitly (re-)bind both textures.  vitaGL's glUseProgram marks
        // uniforms dirty but the sampler bindings come from glUniform1i which
        // stores the texture-unit index, not the texture handle.  The actual
        // texture objects must still be current on those units at draw time.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_index_tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_palette_tex);

        m_blit.Bind();
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, k_quad_pos);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, dyn_uv);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        if (!gpu_passes.empty()) {
            // Stage 2 — ping-pong each GPU pass over the decoded scene.
            GLuint src_tex = m_scene_tex;
            bool   flip    = false;
            for (IPostProcessPass* pass : gpu_passes) {
                GLuint dst_fbo = flip ? m_pass_fbo_b : m_pass_fbo_a;
                GLuint dst_tex = flip ? m_pass_tex_b : m_pass_tex_a;
                pass->Apply(src_tex, dst_fbo, k_gameW, k_gameH);
                src_tex = dst_tex;
                flip    = !flip;
            }

            // Stage 3 — final blit to screen (960×544 upscale/stretch).
            m_passthrough.Apply(src_tex, 0, k_gameW, k_gameH);
        }
        vglSwapBuffers(GL_FALSE);
    }
}

uint8_t* RendererVita::LockFramebuffer(int* out_pitch)
{
    if (SDL_LockSurface(lbDrawSurface) < 0) return nullptr;
    if (out_pitch) *out_pitch = lbDrawSurface->pitch;
    return static_cast<uint8_t*>(lbDrawSurface->pixels);
}

void RendererVita::UnlockFramebuffer()
{
    SDL_UnlockSurface(lbDrawSurface);
}

#endif // PLATFORM_VITA
