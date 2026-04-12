/******************************************************************************/
/** @file KfxProfilingC.h
 *   Tracy instrumentation helpers — C-compatible (.c files).
 *
 *   Use KFX_C_ZONE_BEGIN / KFX_C_ZONE_END around function bodies.
 *   TracyCZoneN declares a local variable named by ctx, so ctx must be unique
 *   within its scope.  No-ops compile to nothing when TRACY_ENABLE is off.
 */
/******************************************************************************/
#ifndef KFX_PROFILING_C_H
#define KFX_PROFILING_C_H

#ifdef TRACY_ENABLE
#  include <tracy/TracyC.h>
#  define KFX_C_ZONE_BEGIN(ctx, name)  TracyCZoneN(ctx, name, 1)
#  define KFX_C_ZONE_END(ctx)          TracyCZoneEnd(ctx)
#  define KFX_C_PLOT(name, val)        TracyCPlot(name, (double)(val))
#else
#  define KFX_C_ZONE_BEGIN(ctx, name)  /* nothing */
#  define KFX_C_ZONE_END(ctx)          ((void)0)
#  define KFX_C_PLOT(name, val)        ((void)0)
#endif

#endif /* KFX_PROFILING_C_H */
