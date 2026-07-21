/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaLensRenderer.cpp
 *     Vita implementation of ILensRenderer. See header for the design.
 */
/******************************************************************************/
#include "pre_inc.h"

#ifdef PLATFORM_VITA

#include "renderer/vita/VitaLensRenderer.h"
#include "renderer/vita/VitaMistPass.h"
#include "renderer/vita/VitaDisplacePass.h"
#include "renderer/vita/VitaFlyeyePass.h"
#include "renderer/vita/VitaOverlayPass.h"
#include "renderer/ir/PostProcessCommands.h"    // IRLensCmd
#include "globals.h"                            // ERRORLOG

#include <cstring>
#include "post_inc.h"

/******************************************************************************/
// File-local FBO helper (mirrors the one previously in RendererVita.cpp).
/******************************************************************************/

static bool vita_lens_create_rgba_fbo(int w, int h, GLuint& out_fbo, GLuint& out_tex)
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
        ERRORLOG("[vitaGL] Lens FBO incomplete: 0x%x", status);
        return false;
    }
    return true;
}

/******************************************************************************/

VitaLensRenderer::~VitaLensRenderer()
{
    Shutdown();
}

bool VitaLensRenderer::Init(int w, int h)
{
    if (!m_passthrough.Init()) {
        Shutdown();
        return false;
    }

    if (!vita_lens_create_rgba_fbo(w, h, m_scene_fbo, m_scene_tex) ||
        !vita_lens_create_rgba_fbo(w, h, m_pass_fbo_a, m_pass_tex_a) ||
        !vita_lens_create_rgba_fbo(w, h, m_pass_fbo_b, m_pass_tex_b)) {
        Shutdown();
        return false;
    }

    m_fbo_w = w;
    m_fbo_h = h;
    return true;
}

void VitaLensRenderer::Shutdown()
{
    ReleaseAll();

    m_passthrough.Free();

    auto del_fbo = [](GLuint& fbo, GLuint& tex) {
        if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (tex) { glDeleteTextures(1, &tex);      tex = 0; }
    };
    del_fbo(m_scene_fbo,  m_scene_tex);
    del_fbo(m_pass_fbo_a, m_pass_tex_a);
    del_fbo(m_pass_fbo_b, m_pass_tex_b);

    m_fbo_w = 0;
    m_fbo_h = 0;
}

IPostProcessPass* VitaLensRenderer::CreatePass(LensEffectType type)
{
    switch (type)
    {
        case LensEffectType::Mist:         return new VitaMistPass();
        case LensEffectType::Displacement: return new VitaDisplacePass();
        case LensEffectType::Flyeye:       return new VitaFlyeyePass();
        case LensEffectType::Overlay:      return new VitaOverlayPass();
        default:                           return nullptr;
    }
}

IPostProcessPass* VitaLensRenderer::AcquireConfiguredPass(const IRLensEffect& effect)
{
    const int idx = static_cast<int>(effect.type);
    if (idx < 0 || idx >= kSlotCount)
        return nullptr;

    Slot& s = m_slots[idx];
    if (s.pass == nullptr)
    {
        s.pass = CreatePass(effect.type);
        if (s.pass == nullptr)
            return nullptr;
        s.pass->Init();
    }

    // Resolve the effect's owned pixel payloads into a local params copy. The IR
    // never carries a live pointer (detached into the owned vectors on the game
    // thread), so nothing here dereferences memory the game thread may free on
    // depossess.
    LensGPUPassParams params = effect.params;
    if (!effect.mist_pixels.empty())    params.mist_data    = effect.mist_pixels.data();
    if (!effect.overlay_pixels.empty()) params.overlay_data = effect.overlay_pixels.data();

    // Re-configure only when the parameters or the owned payload change. The
    // pointer-free effect.params is the compare key; payload changes are caught by
    // the vector comparisons (so a stable mist/overlay is not re-uploaded).
    const bool changed =
        !s.configured
        || std::memcmp(&s.last_params, &effect.params, sizeof(effect.params)) != 0
        || s.last_mist    != effect.mist_pixels
        || s.last_overlay != effect.overlay_pixels;
    if (changed)
    {
        s.pass->Configure(params);
        s.last_params  = effect.params;   // pointer-free snapshot for next-frame compare
        s.last_mist    = effect.mist_pixels;
        s.last_overlay = effect.overlay_pixels;
        s.configured   = true;
    }

    return s.pass;
}

bool VitaLensRenderer::BeginSceneCapture(const IRLensCmd& cmd)
{
    // Stage 1 target selection — palette decode goes either straight to the
    // screen (no GPU passes) or into the scene FBO (GPU passes active).
    if (cmd.count == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, 960, 544);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fbo);
    glViewport(0, 0, m_fbo_w, m_fbo_h);
    return true;
}

void VitaLensRenderer::ResolveAndApply(const IRLensCmd& cmd)
{
    if (cmd.count <= 0)
        return;

    // Stage 2 — ping-pong each GPU pass over the decoded scene. The backend-owned
    // lens renderer resolves each pure-data effect entry into a configured pass.
    GLuint src_tex = m_scene_tex;
    bool   flip    = false;
    for (int i = 0; i < cmd.count; ++i) {
        IPostProcessPass* pass = AcquireConfiguredPass(cmd.effects[i]);
        if (!pass) continue;
        GLuint dst_fbo = flip ? m_pass_fbo_b : m_pass_fbo_a;
        GLuint dst_tex = flip ? m_pass_tex_b : m_pass_tex_a;
        pass->Apply(src_tex, dst_fbo, m_fbo_w, m_fbo_h);
        src_tex = dst_tex;
        flip    = !flip;
    }

    // Stage 3 — final blit to screen (960×544 upscale/stretch).
    m_passthrough.Apply(src_tex, 0, m_fbo_w, m_fbo_h);
}

void VitaLensRenderer::ReleaseAll()
{
    for (Slot& s : m_slots)
    {
        if (s.pass)
        {
            s.pass->Free();
            delete s.pass;
            s.pass = nullptr;
        }
        s.configured = false;
    }
}

/******************************************************************************/
#endif // PLATFORM_VITA
