#ifndef RENDER_PASS_H
#define RENDER_PASS_H

#include "bflib_basics.h"
#include "bflib_sprite.h"

class IBackend;

/**
 * RenderPassSystem
 * 
 * Central abstraction for sprite rendering across all platforms.
 * Absorbs the SpriteBatchInterface, providing a unified submission API.
 * Routes all sprite calls to the active backend (GPU or Software).
 * 
 * Singleton pattern - call GetInstance() to access.
 */
class RenderPassSystem {
public:
    static RenderPassSystem& GetInstance();
    
    // ========== Sprite Submission ==========
    // Submits a sprite to the active backend for rendering
    TbResult SubmitSprite(long x, long y, const struct TbSprite* spr, unsigned int draw_flags);
    
    // Submits a sprite with a single color tint
    TbResult SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr, 
                                   unsigned char colour, unsigned int draw_flags);
    
    // Submits a sprite with color remapping
    TbResult SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                              const unsigned char* colortable, unsigned int draw_flags);
    
    // ========== UI Convenience ==========
    // Convenience wrapper for UI sprites (may apply additional flags)
    TbResult SubmitUISprite(long x, long y, const struct TbSprite* spr, unsigned int draw_flags);
    
    // ========== Frame Lifecycle ==========
    // Called at the start of each frame before any sprite submissions
    void BeginFrame();
    
    // Called at the end of each frame after all sprite submissions
    void EndFrame();
    
    // ========== Sheet Lifecycle ==========
    // Notifies backend when a sprite sheet is loaded (GPU may upload to VRAM)
    void OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet);
    
    // Notifies backend when a sprite sheet is freed (GPU may free VRAM)
    void OnSpriteSheetFreed(const struct TbSpriteSheet* sheet);
    
    // ========== Palette Management ==========
    // Notifies backend when the game palette changes
    void OnPaletteSet(const unsigned char* lbPalette);
    
    // ========== Backend Selection & Lifecycle ==========
    enum BackendType { 
        BACKEND_AUTO,           // Auto-select based on platform
        BACKEND_GPU_VITA,       // Vita GPU (VitaGL + GXM)
        BACKEND_SOFTWARE        // CPU immediate rendering
    };
    
    // Initializes the render system with the specified backend
    bool Initialize(BackendType backend = BACKEND_AUTO);
    
    // Shuts down the active backend
    void Shutdown();
    
    // Returns the name of the active backend ("GPU_VITA", "SOFTWARE", etc.)
    const char* GetBackendName() const;
    
private:
    RenderPassSystem();
    ~RenderPassSystem();
    
    RenderPassSystem(const RenderPassSystem&) = delete;
    RenderPassSystem& operator=(const RenderPassSystem&) = delete;
    
    IBackend* m_backend;
    static RenderPassSystem* s_instance;
};

#endif // RENDER_PASS_H
