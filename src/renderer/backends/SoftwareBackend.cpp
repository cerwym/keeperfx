#include "SoftwareBackend.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"

SoftwareBackend::SoftwareBackend()
{
}

SoftwareBackend::~SoftwareBackend()
{
}

TbResult SoftwareBackend::SubmitSprite(long x, long y, const struct TbSprite* spr,
                                       unsigned int draw_flags)
{
    if (!spr) {
        return Lb_FAIL;
    }
    
    // Phase 2B: Route to CPU software blitter
    // Save the original draw flags and clear the GPU batch flag to prevent recursion
    unsigned int saved_flags = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags & ~Lb_SPRITE_GPU_BATCH_UI;
    
    // Call the CPU rendering function directly
    // Use the original unscaled LbSpriteDraw for standard sprite submission
    TbResult ret = LbSpriteDraw(x, y, spr);
    
    // Restore original flags
    lbDisplay.DrawFlags = saved_flags;
    return ret;
}

TbResult SoftwareBackend::SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                                unsigned char colour, unsigned int draw_flags)
{
    if (!spr) {
        return Lb_FAIL;
    }
    
    // Phase 2B: Route to CPU software blitter with color tint
    unsigned int saved_flags = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags & ~Lb_SPRITE_GPU_BATCH_UI;
    
    TbResult ret = LbSpriteDrawOneColour(x, y, spr, colour);
    
    lbDisplay.DrawFlags = saved_flags;
    return ret;
}

TbResult SoftwareBackend::SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                                            const unsigned char* colortable, unsigned int draw_flags)
{
    if (!spr || !colortable) {
        return Lb_FAIL;
    }
    
    // Phase 2B: Route to CPU software blitter with color remapping
    unsigned int saved_flags = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags & ~Lb_SPRITE_GPU_BATCH_UI;
    
    // LbSpriteDrawScaledRemap can be called with 1:1 scaling (16 units = 1 pixel)
    TbResult ret = LbSpriteDrawScaledRemap(x, y, spr, spr->SWidth, spr->SHeight, colortable);
    
    lbDisplay.DrawFlags = saved_flags;
    return ret;
}

void SoftwareBackend::BeginFrame()
{
    // Software backend does immediate CPU rendering - no frame setup needed
}

void SoftwareBackend::EndFrame()
{
    // Software backend does immediate CPU rendering - no flush needed
}

void SoftwareBackend::OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet)
{
    // Software backend uses main RAM sheets - no GPU VRAM management needed
}

void SoftwareBackend::OnSpriteSheetFreed(const struct TbSpriteSheet* sheet)
{
    // Software backend uses main RAM sheets - no GPU VRAM management needed
}

void SoftwareBackend::OnPaletteSet(const unsigned char* palette)
{
    // Software backend uses main RAM palette - no GPU texture upload needed
}
