/*
 * @file stack_monitor.c
 * @brief Stack depth monitoring implementation for ARM Vita.
 */

#include "stack_monitor.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
    #include <direct.h>
#endif

#ifdef PLATFORM_VITA
    #include <psp2/kernel/processmgr.h>
    #include <psp2/io/fcntl.h>
    #include <psp2/io/stat.h>
    extern int sceUserMainThreadStackSize; /* Defined in PlatformVita.cpp: 4 MB */
#endif

/* ===================================================================
 * Internal State
 * =================================================================== */

static struct {
    int initialized;
    size_t total_stack_size;      /* Total 4 MB on Vita */
    uintptr_t stack_init_sp;      /* SP at init time */
    size_t high_water_mark;       /* Max depth observed */
    size_t sample_count;          /* Number of samples taken */
    FILE *log_file;               /* profiler.log file handle */
} g_stack_monitor = {0, 0, 0, 0, 0, NULL};


/* ===================================================================
 * ARM/Vita Stack Pointer Intrinsics
 * =================================================================== */

/**
 * Get current stack pointer.
 * ARM ABI: SP register in r13.
 * On Vita, stack grows downward (toward lower addresses).
 */
static inline uintptr_t GetStackPointer(void)
{
    uintptr_t sp;
    #if defined(__GNUC__) && (defined(__arm__) || defined(__aarch64__))
        asm volatile("mov %0, sp" : "=r"(sp));
    #else
        /* Fallback: assume SP-like behavior for non-GCC */
        sp = (uintptr_t)&sp; /* Address of local var as rough estimate */
    #endif
    return sp;
}

/**
 * Ensure directory exists (create if needed).
 * On Vita, creates with sceIoMkdir; on other platforms uses mkdir.
 */
static void _ensure_directory(const char *path)
{
#ifdef PLATFORM_VITA
    /* Vita: use sceIoMkdir (from psp2/io/stat.h) */
    sceIoMkdir(path, 0777);  /* Fails gracefully if exists; error code ignored */
#elif defined(_WIN32)
    _mkdir(path);            /* Windows: _mkdir has a single-argument signature */
#else
    mkdir(path, 0755);       /* Unix: mkdir with permission; error ignored */
#endif
}


/* ===================================================================
 * API Implementation
 * =================================================================== */

void StackMonitor_Init(void)
{
    if (g_stack_monitor.initialized)
        return; /* Idempotent */

    uintptr_t sp_now = GetStackPointer();
    
    #ifdef PLATFORM_VITA
        g_stack_monitor.total_stack_size = sceUserMainThreadStackSize; /* 4 MB */
    #else
        g_stack_monitor.total_stack_size = 8 * 1024 * 1024; /* 8 MB estimate for other platforms */
    #endif

    g_stack_monitor.stack_init_sp = sp_now;
    g_stack_monitor.high_water_mark = 0;
    g_stack_monitor.sample_count = 0;
    g_stack_monitor.initialized = 1;

    /* Ensure directory exists before opening log file */
    _ensure_directory("ux0:data");
    _ensure_directory("ux0:data/keeperfx");

    /* Open profiler.log for writing profiler measurements */
    g_stack_monitor.log_file = fopen("ux0:data/keeperfx/profiler.log", "w");
    if (g_stack_monitor.log_file) {
        fprintf(g_stack_monitor.log_file, "[StackMonitor] Initialized: total=%lu bytes (%.2f MB), init_SP=0x%lx\n",
                (unsigned long)g_stack_monitor.total_stack_size,
                g_stack_monitor.total_stack_size / (1024.0f * 1024.0f),
                (unsigned long)sp_now);
        fflush(g_stack_monitor.log_file);
    }
}

void StackMonitor_Shutdown(void)
{
    if (!g_stack_monitor.initialized)
        return;

    StackMonitor_LogStatistics();
    
    if (g_stack_monitor.log_file) {
        fclose(g_stack_monitor.log_file);
        g_stack_monitor.log_file = NULL;
    }
    
    g_stack_monitor.initialized = 0;
}

size_t StackMonitor_Sample(void)
{
    if (!g_stack_monitor.initialized)
        StackMonitor_Init();

    uintptr_t sp_now = GetStackPointer();
    
    /* Stack grows downward; lower SP = more stack used.
     * Used = (init_SP - current_SP). */
    size_t used;
    if (sp_now < g_stack_monitor.stack_init_sp) {
        used = g_stack_monitor.stack_init_sp - sp_now;
    } else {
        /* SP increased (stack unwound) — shouldn't happen except at shallow depth */
        used = 0;
    }

    /* Cap at total (sanity check) */
    if (used > g_stack_monitor.total_stack_size)
        used = g_stack_monitor.total_stack_size;

    /* Update high-water mark */
    if (used > g_stack_monitor.high_water_mark)
        g_stack_monitor.high_water_mark = used;

    g_stack_monitor.sample_count++;
    return used;
}

size_t StackMonitor_GetHighWaterMark(void)
{
    if (!g_stack_monitor.initialized)
        return 0;
    return g_stack_monitor.high_water_mark;
}

size_t StackMonitor_GetTotalSize(void)
{
    if (!g_stack_monitor.initialized)
        return 0;
    return g_stack_monitor.total_stack_size;
}

size_t StackMonitor_GetRemaining(void)
{
    size_t total = StackMonitor_GetTotalSize();
    size_t hwm = StackMonitor_GetHighWaterMark();
    return total > hwm ? total - hwm : 0;
}

void StackMonitor_LogStatistics(void)
{
    if (!g_stack_monitor.initialized)
        return;

    size_t current = StackMonitor_Sample();
    size_t remaining = StackMonitor_GetRemaining();
    float used_pct = (g_stack_monitor.high_water_mark * 100.0f) / g_stack_monitor.total_stack_size;

    if (g_stack_monitor.log_file) {
        fprintf(g_stack_monitor.log_file, "\n");
        fprintf(g_stack_monitor.log_file, "========== STACK MONITOR STATISTICS ==========\n");
        fprintf(g_stack_monitor.log_file, "Total Stack:    %8lu bytes (%.2f MB)\n",
                (unsigned long)g_stack_monitor.total_stack_size,
                g_stack_monitor.total_stack_size / (1024.0f * 1024.0f));
        fprintf(g_stack_monitor.log_file, "Current Used:   %8lu bytes (%.2f MB)\n",
                (unsigned long)current,
                current / (1024.0f * 1024.0f));
        fprintf(g_stack_monitor.log_file, "HWM (peak):     %8lu bytes (%.2f MB, %.1f%%)\n",
                (unsigned long)g_stack_monitor.high_water_mark,
                g_stack_monitor.high_water_mark / (1024.0f * 1024.0f),
                used_pct);
        fprintf(g_stack_monitor.log_file, "Remaining:      %8lu bytes (%.2f MB)\n",
                (unsigned long)remaining,
                remaining / (1024.0f * 1024.0f));
        fprintf(g_stack_monitor.log_file, "Sample Count:   %8lu\n", (unsigned long)g_stack_monitor.sample_count);
        
        if (StackMonitor_IsCritical()) {
            fprintf(g_stack_monitor.log_file, "!!! WARNING: Stack is critically low (<512KB remaining) !!!\n");
        }
        
        fprintf(g_stack_monitor.log_file, "=============================================\n\n");
        fflush(g_stack_monitor.log_file);
    }
}

int StackMonitor_IsCritical(void)
{
    return StackMonitor_GetRemaining() < 512 * 1024; /* < 512 KB */
}

void StackMonitor_ResetHighWaterMark(void)
{
    if (!g_stack_monitor.initialized)
        return;
    g_stack_monitor.high_water_mark = 0;
    g_stack_monitor.sample_count = 0;
}

void StackMonitor_SampleWithLabel(const char *label)
{
    size_t current = StackMonitor_Sample();
    size_t hwm = StackMonitor_GetHighWaterMark();
    size_t remaining = StackMonitor_GetRemaining();
    
    if (g_stack_monitor.log_file) {
        fprintf(g_stack_monitor.log_file, "[STACK] %-40s | Current: %6lu KB (%5.2f MB) | HWM: %6lu KB (%5.2f MB) | Remaining: %6lu KB\n",
                label,
                (unsigned long)(current / 1024),
                current / (1024.0f * 1024.0f),
                (unsigned long)(hwm / 1024),
                hwm / (1024.0f * 1024.0f),
                (unsigned long)(remaining / 1024));
        fflush(g_stack_monitor.log_file);
    }
}
