#ifndef KFX_BREADCRUMB_H
#define KFX_BREADCRUMB_H

/**
 * Lightweight crash breadcrumb trail for PS Vita builds.
 *
 * A fixed-size ring buffer records the last N function visits.
 * On crash, the on-device handler dumps this trail to crash.log,
 * giving "last functions called" context even without a full stack trace.
 *
 * Usage:
 *   KFX_BREADCRUMB("engine_update");
 *   KFX_BREADCRUMB("LbScreenSetup");
 *
 * Only active in debug/reldebug builds (NDEBUG not defined).
 */

#ifdef __cplusplus
extern "C" {
#endif

#define KFX_BREADCRUMB_SLOTS 64

typedef struct {
    const char* labels[KFX_BREADCRUMB_SLOTS];
    unsigned int head;  /* next write position (wraps modulo SLOTS) */
    unsigned int count; /* total breadcrumbs recorded (for underflow detection) */
} KfxBreadcrumbRing;

/* Global ring buffer — defined in PlatformVita.cpp */
#if defined(PLATFORM_VITA) && !defined(NDEBUG)

extern KfxBreadcrumbRing g_kfx_breadcrumbs;

static inline void kfx_breadcrumb_push(const char* label) {
    unsigned int idx = g_kfx_breadcrumbs.head % KFX_BREADCRUMB_SLOTS;
    g_kfx_breadcrumbs.labels[idx] = label;
    g_kfx_breadcrumbs.head = idx + 1;
    g_kfx_breadcrumbs.count++;
}

#define KFX_BREADCRUMB(label) kfx_breadcrumb_push(label)

#else
#define KFX_BREADCRUMB(label) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* KFX_BREADCRUMB_H */
