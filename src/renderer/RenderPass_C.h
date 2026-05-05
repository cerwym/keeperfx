#pragma once

#include "bflib_basics.h"
#include "bflib_sprite.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * C wrapper functions for RenderPassSystem
 * These provide a C-compatible interface to the C++ RenderPassSystem singleton
 */

/**
 * 1 when a sprite backend is active; 0 otherwise.
 * Read directly by bflib_vidraw.c to decide whether to intercept LbSpriteDraw.
 */
extern int g_render_pass_active;

/**
 * Initialize the render system with the specified backend.
 * @param backend_type 0=AUTO, 1=GPU_VITA, 2=SOFTWARE
 * @return TbBool: TRUE if successful, FALSE otherwise
 */
TbBool RenderPass_Initialize(int backend_type);

/**
 * Shutdown the render system
 */
void RenderPass_Shutdown(void);

/**
 * Get the backend name
 * @return Human-readable backend name ("GPU_VITA", "SOFTWARE", etc.)
 */
const char* RenderPass_GetBackendName(void);

/**
 * Submit a sprite for rendering
 */
TbResult RenderPass_SubmitSprite(long x, long y, const struct TbSprite* spr, unsigned int draw_flags);

/**
 * Submit a sprite with single-color tint
 */
TbResult RenderPass_SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr, 
                                          unsigned char colour, unsigned int draw_flags);

/**
 * Submit a sprite with color remapping
 */
TbResult RenderPass_SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                                      const unsigned char* colortable, unsigned int draw_flags);

/**
 * Begin frame (call before sprite submissions)
 */
void RenderPass_BeginFrame(void);

/**
 * End frame (call after all sprite submissions)
 */
void RenderPass_EndFrame(void);

/**
 * Immediately draw any queued sprite draws to the GPU framebuffer.
 * Call after gpu_flush() and a sprite draw function to composite sprites
 * at the correct depth during the bucket walk.
 */
void RenderPass_DrawNow(void);

/**
 * Notify backend when sprite sheet is loaded
 */
void RenderPass_OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet);

/**
 * Notify backend when sprite sheet is freed
 */
void RenderPass_OnSpriteSheetFreed(const struct TbSpriteSheet* sheet);

/**
 * Notify backend when palette changes
 */
void RenderPass_OnPaletteSet(const unsigned char* palette);

/**
 * Temporarily suppress the GPU sprite submission path.
 * While suspended, LbSpriteDrawScaled and friends rasterise to WScreen
 * instead of submitting through the render pass backend.
 * Must be paired with RenderPass_Resume().
 */
void RenderPass_Suspend(void);

/**
 * Re-enable the GPU sprite submission path after a RenderPass_Suspend().
 */
void RenderPass_Resume(void);

#ifdef __cplusplus
}
#endif
