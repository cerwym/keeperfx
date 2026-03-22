#ifndef IBACKEND_H
#define IBACKEND_H

#include "bflib_basics.h"
#include "bflib_sprite.h"

/**
 * IBackend
 * 
 * Abstract interface for sprite rendering backends.
 * Implementations can be GPU-accelerated (Vita) or CPU-based (Software).
 * 
 * All backends receive the same API calls and must behave equivalently
 * (though performance characteristics may differ).
 */
class IBackend {
public:
    virtual ~IBackend() = default;
    
    // ========== Sprite Submission ==========
    // Submit a standard sprite for rendering
    virtual TbResult SubmitSprite(long x, long y, const struct TbSprite* spr,
                                 unsigned int draw_flags) = 0;
    
    // Submit a sprite with single-color tint (used for creature tinting, UI highlights)
    virtual TbResult SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                          unsigned char colour, unsigned int draw_flags) = 0;
    
    // Submit a sprite with color remapping (used for paletted sprites, player colors)
    virtual TbResult SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                                      const unsigned char* colortable, unsigned int draw_flags) = 0;
    
    // ========== Frame Lifecycle ==========
    // Called at the start of each frame (may initialize batch, clear state, etc.)
    virtual void BeginFrame() = 0;
    
    // Called at the end of each frame (GPU backends flush batches, software no-op)
    virtual void EndFrame() = 0;
    
    // ========== Sprite Sheet Lifecycle ==========
    // Notifies backend when a sheet is loaded (GPU may upload to VRAM, software no-op)
    virtual void OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet) = 0;
    
    // Notifies backend when a sheet is freed (GPU may free VRAM, software no-op)
    virtual void OnSpriteSheetFreed(const struct TbSpriteSheet* sheet) = 0;
    
    // ========== Palette Management ==========
    // Notifies backend when the game palette changes
    // GPU backends may upload new palette to VRAM
    virtual void OnPaletteSet(const unsigned char* lbPalette) = 0;
    
    // ========== Backend Info ==========
    // Returns a human-readable name for this backend (e.g., "GPU_VITA", "SOFTWARE")
    virtual const char* GetName() const = 0;
};

#endif // IBACKEND_H
