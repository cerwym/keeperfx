#ifndef SOFTWARE_BACKEND_H
#define SOFTWARE_BACKEND_H

#include "IBackend.h"

/**
 * SoftwareBackend
 * 
 * CPU-based sprite rendering backend.
 * Routes all sprite submissions directly to existing CPU rendering functions
 * (LbSpriteDrawScaled, LbSpriteDrawOneColour, LbSpriteDrawRemap).
 * 
 * No batching, no GPU VRAM management.
 * Frame lifecycle methods are no-ops (immediate rendering).
 */
class SoftwareBackend : public IBackend {
public:
    SoftwareBackend();
    virtual ~SoftwareBackend();
    
    // Submit a standard sprite (routes to LbSpriteDrawScaled)
    virtual TbResult SubmitSprite(long x, long y, const struct TbSprite* spr,
                                 unsigned int draw_flags) override;
    
    // Submit a sprite with single-color tint (routes to LbSpriteDrawOneColour)
    virtual TbResult SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                          unsigned char colour, unsigned int draw_flags) override;
    
    // Submit a sprite with color remapping (routes to LbSpriteDrawRemap)
    virtual TbResult SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                                      const unsigned char* colortable, unsigned int draw_flags) override;
    
    // Frame lifecycle (no-ops, rendering is immediate)
    virtual void BeginFrame() override;
    virtual void EndFrame() override;
    
    // Sheet lifecycle (no-ops, no VRAM management)
    virtual void OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet) override;
    virtual void OnSpriteSheetFreed(const struct TbSpriteSheet* sheet) override;
    
    // Palette management (no-op, palettes are in main RAM)
    virtual void OnPaletteSet(const unsigned char* lbPalette) override;
    
    virtual const char* GetName() const override { return "SOFTWARE"; }
};

#endif // SOFTWARE_BACKEND_H
