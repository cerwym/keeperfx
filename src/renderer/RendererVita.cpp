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
#include "globals.h"
#include "renderer/IPostProcessPass.h"
#include "renderer/ILensRenderer.h"
#include "renderer/ir/PostProcessCommands.h"
#include "renderer/vita/VitaMistPass.h"
#include "renderer/vita/VitaDisplacePass.h"
#include "renderer/vita/VitaFlyeyePass.h"
#include "renderer/vita/VitaOverlayPass.h"
#include "renderer/vita/VitaLensRenderer.h"
#include "kfx/lense/LensManager.h"
#include "renderer/backends/SoftwareWorldViewRenderer.h"
#include "renderer/backends/SoftwareMapFadePass.h"
#include "renderer/backends/SoftwareTextRenderer.h"
#include "renderer/backends/SoftwareUIRenderer.h"
#include "renderer/backends/SWCursorLayer.h"

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

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        // Own the software sub-renderers (CPU staging path for UI/text/world/fade).
        m_worldViewRenderer = new SoftwareWorldViewRenderer();
        m_mapFadePass       = new SoftwareMapFadePass();
        m_lensRenderer      = new VitaLensRenderer();
        m_textRenderer      = new SoftwareTextRenderer();
        m_uiRenderer        = new SoftwareUIRenderer();
        m_cursorLayer       = new SWCursorLayer();

        if (!m_lensRenderer->Init(k_gameW, k_gameH)) {
            Shutdown();
            return false;
        }

        m_initialized = true;
        SYNCLOG("RendererVita: vitaGL palette shader initialised (%dx%d -> 960x544)", k_gameW, k_gameH);
        return true;
    }
}

void RendererVita::Shutdown()
{
    if (!m_initialized) return;

    m_blit.Free();
    if (m_index_tex)   { glDeleteTextures(1, &m_index_tex);   m_index_tex   = 0; }
    if (m_palette_tex) { glDeleteTextures(1, &m_palette_tex); m_palette_tex = 0; }

    delete m_worldViewRenderer; m_worldViewRenderer = nullptr;
    delete m_mapFadePass;       m_mapFadePass       = nullptr;
    delete m_lensRenderer;      m_lensRenderer      = nullptr;
    delete m_textRenderer;      m_textRenderer      = nullptr;
    delete m_uiRenderer;        m_uiRenderer        = nullptr;
    delete m_cursorLayer;       m_cursorLayer       = nullptr;

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

        IRLensCmd lens_cmd;
        {
            LensManager* lm = LensManager::GetInstance();
            if (lm && lm->IsReady()) {
                lens_cmd = lm->CollectGPULensCmd();
            }
        }

        const bool lens_scene = m_lensRenderer && m_lensRenderer->BeginSceneCapture(lens_cmd);

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

        if (lens_scene) {
            // Ping-pong the GPU passes over the decoded scene and
            // blit the final result to the screen. Owned by the lens sub-renderer.
            m_lensRenderer->ResolveAndApply(lens_cmd);
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
