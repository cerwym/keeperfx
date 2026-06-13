#pragma once

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
    // Submit a standard sprite for rendering.
    // CPU default: LbSpriteDraw with DrawFlags save/restore.
    virtual TbResult SubmitSprite(long x, long y, const struct TbSprite* spr,
                                 unsigned int draw_flags);
    
    // Submit a sprite with single-color tint (used for creature tinting, UI highlights).
    // CPU default: LbSpriteDrawOneColour.
    virtual TbResult SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                          unsigned char colour, unsigned int draw_flags);
    
    // Submit a sprite with color remapping (used for paletted sprites, player colors).
    // CPU default: LbSpriteDrawScaledRemap.
    // GPU backends that cannot perform remap delegate explicitly: IBackend::SubmitSpriteRemap(...).
    virtual TbResult SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                                      const unsigned char* colortable, unsigned int draw_flags);
    
    // ========== Frame Lifecycle ==========
    // Called at the start of each frame (may initialize batch, clear state, etc.).
    // CPU default: no-op.
    virtual void BeginFrame();

    // Called at the end of each frame (GPU backends submit batches, software no-op).
    // CPU default: no-op.
    virtual void EndFrame();

    // Immediately draw any queued sprite draws to the GPU.
    // Used by GLWorldViewRenderer to interleave sprites at the correct bucket depth.
    // Default is a no-op for backends that render immediately (Software, Vita).
    virtual void DrawNow() {}

    // Override the screen dimensions used for NDC conversion during the next draw.
    // Pass (0, 0) to restore the default (RendererGetScreenWidth() / RendererGetScreenHeight()).
    // Used by GLWorldViewRenderer when sprites are rendered inside the game viewport
    // rather than in full-screen space.
    virtual void SetScreenSize(int /*w*/, int /*h*/) {}
    
    // ========== Sprite Sheet Lifecycle ==========
    // Notifies backend when a sheet is loaded (GPU may upload to VRAM).
    // CPU default: no-op.
    virtual void OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet);
    
    // Notifies backend when a sheet is freed (GPU may free VRAM).
    // CPU default: no-op.
    virtual void OnSpriteSheetFreed(const struct TbSpriteSheet* sheet);
    
    // ========== Palette Management ==========
    // Notifies backend when the game palette changes.
    // GPU backends may upload new palette to VRAM. CPU default: no-op.
    virtual void OnPaletteSet(const unsigned char* lbPalette);
    
    // ========== Backend Info ==========
    // Returns a human-readable name for this backend (e.g., "GPU_VITA", "SOFTWARE").
    // CPU default: returns "CPU".
    virtual const char* GetName() const;
};
