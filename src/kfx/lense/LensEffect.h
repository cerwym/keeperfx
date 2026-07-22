/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file LensEffect.h
 *     Base class for lens effects.
 * @par Purpose:
 *     Defines the interface and common functionality for all lens effects.
 * @par Comment:
 *     Uses inheritance to provide consistent effect lifecycle and asset loading.
 * @author   Peter Lockett, KeeperFX Team
 * @date     09 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KFX_LENSEFFECT_H
#define KFX_LENSEFFECT_H

#include "../../bflib_basics.h"

#include <cstdint>
#include <vector>

/**
 * Effect rendering context - passed to all effect draw methods.
 */
struct LensRenderContext {
    unsigned char *dstbuf;       // Destination buffer (viewport)
    unsigned char *srcbuf;       // Source buffer (full screen width, unclipped)
    long dstpitch;               // Destination pitch
    long srcpitch;               // Source pitch (full screen width)
    long width;                  // Viewport width
    long height;                 // Viewport height
    long viewport_x;             // X offset of viewport in source buffer
    TbBool buffer_copied;        // Whether srcbuf has been copied to dstbuf yet
};

/**
 * Effect type enumeration.
 */
enum class LensEffectType {
    Mist,           // Fog/haze effect
    Displacement,   // Image warping/distortion
    Palette,        // Color palette changes
    Overlay,        // Overlay graphics compositing
    Flyeye,         // Fisheye/compound eye effect
    Custom,         // LUA-defined custom effect
};

/**
 * Base class for all lens effects.
 * 
 * Provides common functionality:
 * - Asset loading with JSON → mods → data fallback
 * - Enable/disable state management
 * - Virtual interface for effect lifecycle
 */
class LensEffect {
public:
    LensEffect(LensEffectType type, const char* name);
    virtual ~LensEffect();
    
    // Effect lifecycle (override in derived classes)
    virtual TbBool Setup(long lens_idx) = 0;
    virtual void Cleanup() = 0;
    virtual TbBool Draw(LensRenderContext* ctx) = 0;
    
    // Configuration
    void SetEnabled(TbBool enabled) { m_enabled = enabled; }
    TbBool IsEnabled() const { return m_enabled; }

    /**
     * Fill `out` with this effect's GPU pass parameters (pure data) and return
     * true if the effect has a GPU realization; return false for CPU-only
     * effects (the default). The backend's ILensRenderer maps this effect's
     * type + params to a concrete pass — the game side owns no renderer object.
     */
    virtual bool BuildGPUParams(struct LensGPUPassParams& /*out*/) const { return false; }

    /**
     * Geometric effects (Displacement / Flyeye) pre-compute an exact per-output-
     * pixel source lookup table at the given render resolution and hand it to the
     * backend as a tightly-packed RG16 image (two uint16 per pixel: src_x, src_y,
     * row-major, top row first) so GL/Vita resample the scene identically to the
     * software path.
     *
     * Returns true for geometric effects. `out_w`/`out_h` and `io_version` are
     * always set to the current table dimensions/version. `out_pixels` is filled
     * ONLY when the table changed since the last call (version bump on lens or
     * resolution change); on unchanged frames it is left empty and the backend
     * reuses the texture it already uploaded for that version. Non-geometric
     * effects return false (the default).
     */
    virtual bool BuildRemap(int /*render_w*/, int /*render_h*/,
                            std::vector<unsigned char>& /*out_pixels*/,
                            int& /*out_w*/, int& /*out_h*/,
                            uint32_t& /*io_version*/) { return false; }

    /**
     * Advance any time-based animation this effect owns by `delta` game-turns
     * worth of time (i.e. game.delta_time: 1.0 == one turn at the target frame
     * rate). Called once per rendered frame on the game thread. Keeping the
     * animation phase here — rather than self-accumulating inside a GPU pass —
     * makes the drift speed frame-rate independent and identical across the
     * software and GPU backends. Default: no-op (static effects).
     */
    virtual void AdvanceAnimation(float /*delta*/) {}
    
    // Identification
    LensEffectType GetType() const { return m_type; }
    const char* GetName() const { return m_name; }
    
    // Common asset loading helper (public for use by effect renderers)
    // Searches: 1) Mod data directories
    //          2) Base game /data directory
    TbBool LoadAssetWithFallback(const char* filename, unsigned char* buffer, 
                                  size_t buffer_size, const char** loaded_from);
    
protected:
    LensEffectType m_type;
    const char* m_name;
    TbBool m_enabled;
    void* m_user_data;  // For derived classes to store internal state (renderers, etc.)
};

#endif
