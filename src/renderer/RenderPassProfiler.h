/**
 * @file RenderPassProfiler.h
 * @brief Performance profiling framework for RenderPassSystem
 *
 * Tracks submission overhead, backend dispatch time, and batch statistics.
 * Can be compiled in/out with RENDERPASS_PROFILING define.
 */

#ifndef RENDER_PASS_PROFILER_H
#define RENDER_PASS_PROFILER_H

#include "bflib_basics.h"

/**
 * RenderPassProfiler
 *
 * Optional profiling layer to measure:
 * - Submission overhead per sprite
 * - Backend dispatch time
 * - Batch fill rates (GPU backend)
 * - Frame submission statistics
 *
 * Define RENDERPASS_PROFILING=1 to enable (default off for release builds)
 */
class RenderPassProfiler {
public:
    struct FrameStats {
        unsigned int sprites_submitted;      // Total sprites submitted this frame
        unsigned int cpu_sprites;            // Sprites routed to CPU backend
        unsigned int gpu_sprites;            // Sprites routed to GPU backend
        unsigned int palette_updates;        // Palette texture updates
        unsigned int sheet_updates;          // Sheet VRAM uploads/frees
        unsigned long submission_us;         // Total submission time (microseconds)
        unsigned long dispatch_us;           // Backend dispatch time
        unsigned long frame_us;              // Entire frame time (BeginFrame → EndFrame)
    };

    static RenderPassProfiler& GetInstance();
    
    // Enable/disable profiling at runtime
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled; }
    
    // Call at frame start
    void BeginFrame();
    
    // Track individual submissions
    void RecordSubmission(unsigned int sprites = 1);
    void RecordSubmissionTime(unsigned long us);
    void RecordDispatchTime(unsigned long us);
    
    // Call at frame end
    void EndFrame();
    
    // Get per-frame statistics
    const FrameStats& GetLastFrameStats() const { return m_last_frame_stats; }
    
    // Get rolling average over last N frames
    const FrameStats& GetAverageStats(int frame_window = 60) const;
    
    // Reset accumulators
    void Reset();
    
    // Print human-readable statistics to log
    void PrintStats() const;
    
private:
    RenderPassProfiler();
    ~RenderPassProfiler();
    
    RenderPassProfiler(const RenderPassProfiler&) = delete;
    RenderPassProfiler& operator=(const RenderPassProfiler&) = delete;
    
    bool m_enabled;
    FrameStats m_current_frame;
    FrameStats m_last_frame_stats;
    FrameStats m_average_stats;
    
    static RenderPassProfiler* s_instance;
};

// Helper: RAII timer for measuring code blocks
class RenderPassTimer {
public:
    RenderPassTimer(const char* name);
    ~RenderPassTimer();
    
private:
    const char* m_name;
    unsigned long m_start_us;
};

// Profiling macros (compile-time disabled unless RENDERPASS_PROFILING=1)
#ifdef RENDERPASS_PROFILING
  #define RENDERPASS_PROFILE_ENABLED 1
  #define RENDERPASS_TIMER(name) RenderPassTimer __timer(name)
  #define RENDERPASS_RECORD_SUBMISSION() RenderPassProfiler::GetInstance().RecordSubmission()
#else
  #define RENDERPASS_PROFILE_ENABLED 0
  #define RENDERPASS_TIMER(name)
  #define RENDERPASS_RECORD_SUBMISSION()
#endif

#endif // RENDER_PASS_PROFILER_H
