/*
 * @file stack_monitor.h
 * @brief Runtime stack depth monitoring for Vita hardware acceleration branch.
 * 
 * Tracks stack usage to detect potential overflow conditions during
 * level loading and gameplay. Provides high-water mark statistics
 * for optimization validation.
 * 
 * ARM Vita: 4 MB main thread stack (sceUserMainThreadStackSize)
 * Safe headroom: 1.5 MB minimum recommended
 */

#ifndef STACK_MONITOR_H
#define STACK_MONITOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize stack monitoring. Call once at game startup.
 * Safe to call multiple times (idempotent).
 */
void StackMonitor_Init(void);

/**
 * Shutdown stack monitoring. Call at game exit.
 */
void StackMonitor_Shutdown(void);

/**
 * Sample current stack depth and update high-water mark.
 * Cheap to call frequently (O(1) comparison + update).
 * 
 * @return Current stack used (bytes from stack base toward SP)
 */
size_t StackMonitor_Sample(void);

/**
 * Get current high-water mark (maximum depth seen so far).
 * 
 * @return Max stack used observed (bytes)
 */
size_t StackMonitor_GetHighWaterMark(void);

/**
 * Get total stack size (4 MB on Vita).
 * 
 * @return Total stack size in bytes
 */
size_t StackMonitor_GetTotalSize(void);

/**
 * Get remaining free stack.
 * 
 * @return Free stack = TotalSize - HighWaterMark
 */
size_t StackMonitor_GetRemaining(void);

/**
 * Log current stack statistics to stderr.
 * Called automatically during level transitions.
 */
void StackMonitor_LogStatistics(void);

/**
 * Check if stack is critically low (< 500 KB remaining).
 * Can be used for runtime warnings in debug builds.
 * 
 * @return 1 if critical, 0 otherwise
 */
int StackMonitor_IsCritical(void);

/**
 * Reset statistics (useful for profiling specific code sections).
 */
void StackMonitor_ResetHighWaterMark(void);
/**
 * Log current stack depth with a label (for contextual profiling).
 * Outputs to stderr for immediate visibility during dev/debugging.
 * 
 * @param label Descriptive context string (e.g., "after_load_textures")
 */
void StackMonitor_SampleWithLabel(const char *label);
#ifdef __cplusplus
}
#endif

#endif // STACK_MONITOR_H
