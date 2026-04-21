/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file DrawFlagsGuard.h
 *     RAII guard for lbDisplay.DrawFlags.
 *
 *     Saves DrawFlags on construction, restores on destruction.  Use as a
 *     stack variable around any block that needs to set/clear transparency or
 *     outline flags without leaking state to the caller.
 *
 *     @code
 *     {
 *         DrawFlagsGuard guard(Lb_SPRITE_TRANSPAR4);
 *         // ...draw calls that need TRANSPAR4...
 *     } // flags restored here
 *     @endcode
 *
 *     Non-copyable, non-movable — intentionally scoped to a block.
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus

#include "bflib_video.h"    // lbDisplay

class DrawFlagsGuard
{
    unsigned long m_saved;
public:
    /// Save current flags and set @p new_flags as the new value.
    explicit DrawFlagsGuard(unsigned long new_flags)
        : m_saved(lbDisplay.DrawFlags)
    {
        lbDisplay.DrawFlags = new_flags;
    }

    /// Restore the flags saved at construction time.
    ~DrawFlagsGuard()
    {
        lbDisplay.DrawFlags = m_saved;
    }

    // Non-copyable, non-movable.
    DrawFlagsGuard(const DrawFlagsGuard&)            = delete;
    DrawFlagsGuard& operator=(const DrawFlagsGuard&) = delete;
    DrawFlagsGuard(DrawFlagsGuard&&)                 = delete;
    DrawFlagsGuard& operator=(DrawFlagsGuard&&)      = delete;
};

#endif // __cplusplus
