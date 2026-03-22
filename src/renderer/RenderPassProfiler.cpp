/**
 * @file RenderPassProfiler.cpp
 * @brief Performance profiling implementation
 *
 * Stub implementation ready for optimization measurements.
 * Fully functional but default-disabled to avoid runtime overhead.
 */

#include "RenderPassProfiler.h"
#include "bflib_basics.h"
#include "../globals.h"

RenderPassProfiler* RenderPassProfiler::s_instance = nullptr;

RenderPassProfiler& RenderPassProfiler::GetInstance()
{
    if (!s_instance) {
        s_instance = new RenderPassProfiler();
    }
    return *s_instance;
}

RenderPassProfiler::RenderPassProfiler()
    : m_enabled(false)
{
    Reset();
}

RenderPassProfiler::~RenderPassProfiler()
{
}

void RenderPassProfiler::SetEnabled(bool enabled)
{
    m_enabled = enabled;
}

void RenderPassProfiler::BeginFrame()
{
    if (!m_enabled) return;
    
    m_current_frame.sprites_submitted = 0;
    m_current_frame.cpu_sprites = 0;
    m_current_frame.gpu_sprites = 0;
    m_current_frame.palette_updates = 0;
    m_current_frame.sheet_updates = 0;
    m_current_frame.submission_us = 0;
    m_current_frame.dispatch_us = 0;
    m_current_frame.frame_us = 0;
}

void RenderPassProfiler::RecordSubmission(unsigned int sprites)
{
    if (!m_enabled) return;
    m_current_frame.sprites_submitted += sprites;
}

void RenderPassProfiler::RecordSubmissionTime(unsigned long us)
{
    if (!m_enabled) return;
    m_current_frame.submission_us += us;
}

void RenderPassProfiler::RecordDispatchTime(unsigned long us)
{
    if (!m_enabled) return;
    m_current_frame.dispatch_us += us;
}

void RenderPassProfiler::EndFrame()
{
    if (!m_enabled) return;
    
    m_last_frame_stats = m_current_frame;
}

const RenderPassProfiler::FrameStats& RenderPassProfiler::GetAverageStats(int frame_window) const
{
    // Phase 4 TODO: Implement rolling average over frame_window frames
    return m_average_stats;
}

void RenderPassProfiler::Reset()
{
    m_current_frame = {};
    m_last_frame_stats = {};
    m_average_stats = {};
}

void RenderPassProfiler::PrintStats() const
{
    if (!m_enabled) return;
    
    SYNCLOG("=== RenderPassSystem Frame Statistics ===");
    SYNCLOG("Sprites submitted: %u (CPU: %u, GPU: %u)",
            m_last_frame_stats.sprites_submitted,
            m_last_frame_stats.cpu_sprites,
            m_last_frame_stats.gpu_sprites);
    SYNCLOG("Submission overhead: %lu us, Dispatch: %lu us, Total frame: %lu us",
            m_last_frame_stats.submission_us,
            m_last_frame_stats.dispatch_us,
            m_last_frame_stats.frame_us);
    SYNCLOG("Sheet updates: %u, Palette updates: %u",
            m_last_frame_stats.sheet_updates,
            m_last_frame_stats.palette_updates);
}

// ============================================================================
// RenderPassTimer Implementation
// ============================================================================

RenderPassTimer::RenderPassTimer(const char* name)
    : m_name(name), m_start_us(0)
{
    // Phase 4 TODO: Capture start time using platform timer (e.g., sceKernelGetProcessTimeFrequencyMhz)
    // For now, placeholder only
}

RenderPassTimer::~RenderPassTimer()
{
    // Phase 4 TODO: Calculate elapsed time, record to profiler
    // RenderPassProfiler& prof = RenderPassProfiler::GetInstance();
    // unsigned long elapsed_us = get_elapsed_us(m_start_us);
    // prof.RecordSubmissionTime(elapsed_us);
    // SYNCDBG(20, "RenderPass [%s]: %lu us", m_name, elapsed_us);
}
