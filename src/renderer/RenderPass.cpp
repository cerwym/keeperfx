#include "RenderPass.h"
// Included on every platform so `delete m_backend` sees a complete type; the
// class itself compiles to stubs off-Vita.
#include "backends/VitaGPUBackend.h"
#include "RenderPassProfiler.h"
#include "bflib_basics.h"
#include <cstdio>

// Static instance
RenderPassSystem* RenderPassSystem::s_instance = nullptr;

// C-visible flag read by bflib_vidraw.c — 1 when a backend is active, 0 otherwise
extern "C" {
    int g_render_pass_active = 0;
}

RenderPassSystem::RenderPassSystem()
    : m_backend(nullptr)
{
}

RenderPassSystem::~RenderPassSystem()
{
    Shutdown();
}

RenderPassSystem& RenderPassSystem::GetInstance()
{
    if (!s_instance) {
        s_instance = new RenderPassSystem();
    }
    return *s_instance;
}

bool RenderPassSystem::Initialize(BackendType backend)
{
    // Shutdown any existing backend
    if (m_backend) {
        delete m_backend;
        m_backend = nullptr;
    }
    
    // Select backend.  The GPU sprite path exists only on Vita; every other
    // platform submits sprites through the UIRenderer IR path instead.
    switch (backend) {
        case BACKEND_GPU_VITA:
        case BACKEND_AUTO:
#if defined(__VITA__)
            m_backend = new VitaGPUBackend();
            break;
#else
            ERRORLOG("RenderPassSystem: GPU sprite backend is Vita-only");
            return false;
#endif

        case BACKEND_SOFTWARE:
            ERRORLOG("RenderPassSystem: no software sprite backend; sprites go through the UIRenderer IR path");
            return false;

        default:
            ERRORLOG("RenderPassSystem: Unknown backend type: %d", backend);
            return false;
    }
    
    if (!m_backend) {
        ERRORLOG("RenderPassSystem: Failed to allocate backend");
        return false;
    }
    
    // For GPU_VITA backend, call Initialize() to set up GPU resources
#if defined(__VITA__)
    if (backend == BACKEND_GPU_VITA || (backend == BACKEND_AUTO)) {
        if (m_backend && !m_backend->Initialize()) {
            ERRORLOG("RenderPassSystem: GPU backend initialization failed");
            delete m_backend;
            m_backend = nullptr;
            return false;
        }
    }
#endif
    
    g_render_pass_active = 1;
    return true;
}

void RenderPassSystem::Shutdown()
{
    g_render_pass_active = 0;
    if (m_backend) {
        delete m_backend;
        m_backend = nullptr;
    }
}

const char* RenderPassSystem::GetBackendName() const
{
    if (m_backend) {
        return m_backend->GetName();
    }
    return "UNINITIALIZED";
}

TbResult RenderPassSystem::SubmitSprite(long x, long y, const struct TbSprite* spr, 
                                        unsigned int draw_flags)
{
    if (!m_backend || !spr) {
        return Lb_FAIL;
    }
    
    RenderPassProfiler::GetInstance().RecordSubmission();
    return m_backend->SubmitSprite(x, y, spr, draw_flags);
}

TbResult RenderPassSystem::SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                                 unsigned char colour, unsigned int draw_flags)
{
    if (!m_backend || !spr) {
        return Lb_FAIL;
    }
    
    RenderPassProfiler::GetInstance().RecordSubmission();
    return m_backend->SubmitSpriteOneColour(x, y, spr, colour, draw_flags);
}

TbResult RenderPassSystem::SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                                             const unsigned char* colortable, unsigned int draw_flags)
{
    if (!m_backend || !spr || !colortable) {
        return Lb_FAIL;
    }
    
    RenderPassProfiler::GetInstance().RecordSubmission();
    return m_backend->SubmitSpriteRemap(x, y, spr, colortable, draw_flags);
}

TbResult RenderPassSystem::SubmitUISprite(long x, long y, const struct TbSprite* spr, 
                                          unsigned int draw_flags)
{
    // UI sprites use the standard submit (may apply additional flags in future)
    return SubmitSprite(x, y, spr, draw_flags);
}

void RenderPassSystem::BeginFrame()
{
    RenderPassProfiler::GetInstance().BeginFrame();
    if (!m_backend) {
        return;
    }
    m_backend->BeginFrame();
}

void RenderPassSystem::EndFrame()
{
    if (!m_backend) {
        return;
    }
    m_backend->EndFrame();
    RenderPassProfiler::GetInstance().EndFrame();
}

void RenderPassSystem::DrawNow()
{
    static int flush_debug_count = 0;
    if (flush_debug_count < 5) {
        SYNCLOG("RenderPassSystem::DrawNow: backend=%s active=%d", 
                m_backend ? m_backend->GetName() : "nullptr", g_render_pass_active);
        flush_debug_count++;
    }

    if (!m_backend) {
        return;
    }
    m_backend->DrawNow();
}

void RenderPassSystem::SetScreenSize(int w, int h)
{
    if (!m_backend) {
        return;
    }
    m_backend->SetScreenSize(w, h);
}

void RenderPassSystem::OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet)
{
    if (!m_backend || !sheet) {
        return;
    }
    
    m_backend->OnSpriteSheetLoaded(sheet);
}

void RenderPassSystem::OnSpriteSheetFreed(const struct TbSpriteSheet* sheet)
{
    if (!m_backend || !sheet) {
        return;
    }
    
    m_backend->OnSpriteSheetFreed(sheet);
}

void RenderPassSystem::OnPaletteSet(const unsigned char* lbPalette)
{
    if (!m_backend || !lbPalette) {
        return;
    }
    
    m_backend->OnPaletteSet(lbPalette);
}

// ============================================================================
// C Wrapper Functions (for C files to call)
// ============================================================================

extern "C" {

TbBool RenderPass_Initialize(int backend_type)
{
    RenderPassSystem::BackendType bt;
    switch (backend_type) {
        case 0: bt = RenderPassSystem::BACKEND_AUTO; break;
        case 1: bt = RenderPassSystem::BACKEND_GPU_VITA; break;
        case 2: bt = RenderPassSystem::BACKEND_SOFTWARE; break;
        default:
            return 0; // FALSE
    }
    
    return RenderPassSystem::GetInstance().Initialize(bt) ? 1 : 0; // TRUE or FALSE
}

void RenderPass_Shutdown(void)
{
    RenderPassSystem::GetInstance().Shutdown();
}

const char* RenderPass_GetBackendName(void)
{
    return RenderPassSystem::GetInstance().GetBackendName();
}

TbResult RenderPass_SubmitSprite(long x, long y, const struct TbSprite* spr, unsigned int draw_flags)
{
    return RenderPassSystem::GetInstance().SubmitSprite(x, y, spr, draw_flags);
}

TbResult RenderPass_SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                          unsigned char colour, unsigned int draw_flags)
{
    return RenderPassSystem::GetInstance().SubmitSpriteOneColour(x, y, spr, colour, draw_flags);
}

TbResult RenderPass_SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                                      const unsigned char* colortable, unsigned int draw_flags)
{
    return RenderPassSystem::GetInstance().SubmitSpriteRemap(x, y, spr, colortable, draw_flags);
}

void RenderPass_BeginFrame(void)
{
    RenderPassSystem::GetInstance().BeginFrame();
}

void RenderPass_EndFrame(void)
{
    RenderPassSystem::GetInstance().EndFrame();
}

void RenderPass_DrawNow(void)
{
    RenderPassSystem::GetInstance().DrawNow();
}

void RenderPass_OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet)
{
    RenderPassSystem::GetInstance().OnSpriteSheetLoaded(sheet);
}

void RenderPass_OnSpriteSheetFreed(const struct TbSpriteSheet* sheet)
{
    RenderPassSystem::GetInstance().OnSpriteSheetFreed(sheet);
}

void RenderPass_OnPaletteSet(const unsigned char* palette)
{
    RenderPassSystem::GetInstance().OnPaletteSet(palette);
}

void RenderPass_Suspend(void)
{
    g_render_pass_active = 0;
}

void RenderPass_Resume(void)
{
    // Only re-enable if the system was initialised (has a backend).
    if (RenderPassSystem::GetInstance().IsInitialised())
        g_render_pass_active = 1;
}

} // extern "C"
