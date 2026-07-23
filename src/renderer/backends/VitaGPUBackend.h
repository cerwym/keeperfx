#pragma once

#include "bflib_basics.h"
#include "bflib_sprite.h"

/**
 * VitaGPUBackend
 *
 * GPU-accelerated sprite rendering backend for Vita (VitaGL + GXM).
 * Wraps the existing VitaSpriteLayer GPU sprite batch system.
 *
 * Batches sprite submissions and performs GPU rendering in EndFrame().
 * Manages VRAM for sprite sheets and palette lookups.
 *
 * This is the only GPU sprite backend: every other platform submits sprites
 * through the UIRenderer IR path, so there is no backend interface to derive
 * from — RenderPassSystem owns this type directly.
 */
class VitaGPUBackend {
public:
    VitaGPUBackend();
    ~VitaGPUBackend();

    // Initialize the sprite layer (called from RenderPassSystem::Initialize)
    bool Initialize();

    // Queue a standard sprite for batch submission
    TbResult SubmitSprite(long x, long y, const struct TbSprite* spr,
                          unsigned int draw_flags);

    // Queue a sprite with single-color tint
    TbResult SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                   unsigned char colour, unsigned int draw_flags);

    // Queue a sprite with color remapping (not directly supported by GPU layer, returns silent success)
    TbResult SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                               const unsigned char* colortable, unsigned int draw_flags);

    // Reset sprite queue for new frame
    void BeginFrame();

    // Flush accumulated sprites to GPU
    void EndFrame();

    // Sprites are dispatched in EndFrame(); there is nothing to flush on demand.
    void DrawNow() {}

    // Vita draws sprites in full-screen space; no viewport override needed.
    void SetScreenSize(int /*w*/, int /*h*/) {}

    // Register sprite sheet with GPU VRAM texture atlas
    void OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet);

    // Unregister sprite sheet from GPU VRAM
    void OnSpriteSheetFreed(const struct TbSpriteSheet* sheet);

    // Update GPU palette lookup texture
    void OnPaletteSet(const unsigned char* lbPalette);

    const char* GetName() const { return "GPU_VITA"; }

    // Accessor for RendererVita integration (for sheet manual uploggling if needed)
    class VitaSpriteLayer* GetSpriteLayer() const { return m_sprite_layer; }

private:
    class VitaSpriteLayer* m_sprite_layer;
    bool m_initialized;
};
