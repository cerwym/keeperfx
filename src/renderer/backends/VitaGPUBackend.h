#ifndef VITA_GPU_BACKEND_H
#define VITA_GPU_BACKEND_H

#include "IBackend.h"

/**
 * VitaGPUBackend
 * 
 * GPU-accelerated sprite rendering backend for Vita (VitaGL + GXM).
 * Wraps the existing VitaSpriteLayer GPU sprite batch system.
 * 
 * Batches sprite submissions and performs GPU rendering in EndFrame().
 * Manages VRAM for sprite sheets and palette lookups.
 */
class VitaGPUBackend : public IBackend {
public:
    VitaGPUBackend();
    virtual ~VitaGPUBackend();
    
    // Initialize the sprite layer (called from RenderPassSystem::Initialize)
    bool Initialize();
    
    // Queue a standard sprite for batch submission
    virtual TbResult SubmitSprite(long x, long y, const struct TbSprite* spr,
                                 unsigned int draw_flags) override;
    
    // Queue a sprite with single-color tint
    virtual TbResult SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                          unsigned char colour, unsigned int draw_flags) override;
    
    // Queue a sprite with color remapping (not directly supported by GPU layer, returns silent success)
    virtual TbResult SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                                      const unsigned char* colortable, unsigned int draw_flags) override;
    
    // Reset sprite queue for new frame
    virtual void BeginFrame() override;
    
    // Flush accumulated sprites to GPU
    virtual void EndFrame() override;
    
    // Register sprite sheet with GPU VRAM texture atlas
    virtual void OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet) override;
    
    // Unregister sprite sheet from GPU VRAM
    virtual void OnSpriteSheetFreed(const struct TbSpriteSheet* sheet) override;
    
    // Update GPU palette lookup texture
    virtual void OnPaletteSet(const unsigned char* lbPalette) override;
    
    virtual const char* GetName() const override { return "GPU_VITA"; }
    
    // Accessor for RendererVita integration (for sheet manual uploggling if needed)
    class VitaSpriteLayer* GetSpriteLayer() const { return m_sprite_layer; }

private:
    class VitaSpriteLayer* m_sprite_layer;
    bool m_initialized;
};

#endif // VITA_GPU_BACKEND_H
