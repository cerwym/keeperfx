/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererSoftware.cpp
 *     Software passthrough renderer backend implementation.
 * @par Purpose:
 *     Wraps the original SDL2-based rendering path so it satisfies the
 *     IRenderer interface. Behaviour is byte-for-byte identical to the
 *     pre-abstraction code path.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/RendererSoftware.h"

#include "renderer/backends/SoftwareWorldViewRenderer.h"
#include "renderer/backends/SoftwareMapFadePass.h"
#include "renderer/backends/SoftwareTextRenderer.h"
#include "renderer/backends/SoftwareUIRenderer.h"

#include "bflib_video.h"
#include "bflib_render.h"
#include "bflib_sprite.h"    // TbSpriteSheet, get_sprite
#include "bflib_vidraw.h"    // LbSpriteDrawResized
#include "engine_render.h"   // draw_texture
#include "vidmode.h"         // pixmap ghost tables
#include "renderer/RendererManager.h"
#include "platform/PlatformManager.h"
#include "platform.h"        // platform_get_sdl_window
#include "engine_redraw.h"
#include "lens_api.h"
#include "player_data.h"

#include <SDL3/SDL.h>
#include <cstring>
#include <cstdlib>

extern "C" void draw_texture(int32_t texture_x, int32_t texture_y, int32_t texture_width, int32_t texture_height,
                             int32_t texture_block_index, int32_t flags, int32_t fade_level);

#include "post_inc.h"

/******************************************************************************/

static SDL_Surface* s_screenSurface = nullptr;
static SDL_Surface* s_scaleSurface  = nullptr;

/******************************************************************************/
/* Draw surface — owned by the software renderer                              */
/******************************************************************************/

extern "C" SDL_Surface * lbDrawSurface = nullptr;

extern "C" TbResult LbScreenCreateDrawSurface(int w, int h, int bpp)
{
    SDL_PixelFormat fmt = (bpp == 8) ? SDL_PIXELFORMAT_INDEX8 : SDL_PIXELFORMAT_RGBX8888;
    lbDrawSurface = SDL_CreateSurface(w, h, fmt);
    if (!lbDrawSurface) {
        ERRORLOG("Failed to create draw surface (%dx%dx%d): %s", w, h, bpp, SDL_GetError());
        return Lb_FAIL;
    }
    // SDL3: an INDEX8 surface is created WITHOUT a palette (SDL2 attached a
    // default one).  Without a palette, LbPaletteSet() can't populate colours
    // and the EndFrame format-convert blit fails ("src does not have a palette
    // set") — a black screen.  Create and attach one now.
    if (fmt == SDL_PIXELFORMAT_INDEX8 && !SDL_CreateSurfacePalette(lbDrawSurface)) {
        ERRORLOG("Failed to create draw-surface palette: %s", SDL_GetError());
    }
    return Lb_SUCCESS;
}

extern "C" void LbScreenFreeDrawSurface(void)
{
    extern volatile TbBool lbHasSecondSurface;
    if (lbHasSecondSurface && lbDrawSurface) {
        SDL_DestroySurface(lbDrawSurface);
    }
    lbDrawSurface = nullptr;
}

void RendererSoftware::NotifyFmvPalette(const uint8_t* bgra_1024)
{
    if (!lbDrawSurface) return;
    SDL_Palette* pal = SDL_GetSurfacePalette(lbDrawSurface);
    if (!pal) return;
    SDL_Color colors[256];
    for (int i = 0; i < 256; ++i) {
        colors[i].b = bgra_1024[i * 4 + 0];
        colors[i].g = bgra_1024[i * 4 + 1];
        colors[i].r = bgra_1024[i * 4 + 2];
        colors[i].a = 255;
    }
    SDL_SetPaletteColors(pal, colors, 0, 256);
}

bool RendererSoftware::Init()
{
    // Ensure window has no SDL_WINDOW_OPENGL flag — SDL_GetWindowSurface is incompatible with it.
    if (!PlatformManager_RecreateWindowForSoftwareRenderer()) {
        ERRORLOG("RendererSoftware::Init: failed to recreate window for software rendering");
        return false;
    }

    SDL_Window* win = static_cast<SDL_Window*>(platform_get_sdl_window());
    s_screenSurface = SDL_GetWindowSurface(win);
    if (!s_screenSurface) {
        ERRORLOG("RendererSoftware::Init: SDL_GetWindowSurface failed: %s", SDL_GetError());
        return false;
    }

    // Create an intermediate surface when draw BPP differs from window BPP.
    {
        const SDL_PixelFormatDetails* draw_fmt   = SDL_GetPixelFormatDetails(lbDrawSurface->format);
        const SDL_PixelFormatDetails* screen_fmt = SDL_GetPixelFormatDetails(s_screenSurface->format);
        if (draw_fmt && screen_fmt && draw_fmt->bits_per_pixel != screen_fmt->bits_per_pixel) {
        s_scaleSurface = SDL_CreateSurface(lbDrawSurface->w, lbDrawSurface->h, s_screenSurface->format);
        if (!s_scaleSurface) {
            WARNLOG("RendererSoftware::Init: can't create scale surface: %s — direct blit will be attempted", SDL_GetError());
        }
        }
    }


    // Initialize sub-renderers
    m_worldViewRenderer = new SoftwareWorldViewRenderer();
    m_mapFadePass = new SoftwareMapFadePass();
    m_textRenderer = new SoftwareTextRenderer();
    m_uiRenderer = new SoftwareUIRenderer();

    return true;
}

void RendererSoftware::Shutdown()
{
    free(m_world_raster);
    m_world_raster = nullptr;
    m_world_raster_size = 0;
    m_world_raster_valid = false;

    if (s_scaleSurface) {
        SDL_DestroySurface(s_scaleSurface);
        s_scaleSurface = nullptr;
    }
    s_screenSurface = nullptr; // owned by SDL window, do not free

    // Clean up sub-renderers
    delete m_worldViewRenderer;
    m_worldViewRenderer = nullptr;
    delete m_mapFadePass;
    m_mapFadePass = nullptr;
    delete m_textRenderer;
    m_textRenderer = nullptr;
    delete m_uiRenderer;
    m_uiRenderer = nullptr;
}

bool RendererSoftware::BeginFrame()
{
    m_screenW = RendererPhysicalWidth();
    m_screenH = RendererPhysicalHeight();

    // RendererBeginFrame() is not guarded and runs twice per present (once in
    // RendererLockScreen, again in RendererPresentFrame).  Open the IR graph
    // only once per frame — a second BeginFrame() would wipe the UI already
    // appended this frame.  EndFrame() clears m_frame_open.
    if (m_frame_open)
        return true;
    m_frame_open = true;

    CursorLayer_Clear();

    // Open the UI+text IR write window for the software executor.  UI sprite and
    // text submissions during this frame append to the graph (sharing one seq
    // counter) instead of drawing immediately; they are replayed at EndFrame.
    m_render_graph.BeginFrame();
    m_frame_seq = 0;
    m_render_graph.GetUIBuffers().shared_seq   = &m_frame_seq;
    m_render_graph.GetTextBuffers().shared_seq = &m_frame_seq;
    // Open the write window on the MANAGER's sub-renderers — the ones the game
    // actually submits to via RendererGetUIRenderer()/RendererGetTextRenderer()
    // and on which the sprite sheets are registered.  (RendererSoftware's own
    // m_uiRenderer/m_textRenderer are a separate, unused pair.)
    if (IUIRenderer* ui = RendererGetUIRenderer())
        ui->SetUICommandBuffers(&m_render_graph.GetUIBuffers());
    if (ITextRenderer* text = RendererGetTextRenderer())
        text->SetTextCommandBuffers(&m_render_graph.GetTextBuffers());
    return true;
}

void RendererSoftware::EndFrame()
{
    // Software IR executor: close the write window, flip, and replay the
    // deferred UI + text (merged by shared seq) into lbDrawSurface — on top of
    // everything drawn immediately this frame — before the cursor + blit.
    // Reset the open-guard first so it clears even on the early-return blit
    // error paths below.
    m_frame_open = false;
    IUIRenderer*   ui   = RendererGetUIRenderer();
    ITextRenderer* text = RendererGetTextRenderer();
    if (ui)   ui->SetUICommandBuffers(nullptr);
    if (text) text->SetTextCommandBuffers(nullptr);
    FrameState fs = {};
    m_render_graph.Flip(fs);

    // The deferred UI+text replay and the cursor draw into the WScreen buffer,
    // but RendererUnlockScreen() nulled it before present.  The draw-surface
    // pixels are always valid (no real SDL lock needed), so point WScreen back
    // at them with a full-screen graphics window for the replay, then clear it.
    if (lbDrawSurface)
    {
        RendererSetWScreen(static_cast<TbPixel*>(lbDrawSurface->pixels));
        RendererSetScreenDimensions(lbDrawSurface->pitch, lbDrawSurface->h);
        RendererSetViewport(0, 0, lbDrawSurface->w, lbDrawSurface->h);

        // World-raster cache: make transparent UI compositing idempotent.
        // On frames that drew world content (LockScreen was called), snapshot
        // lbDrawSurface into m_world_raster.  On present-only frames, restore
        // the clean world snapshot so overlays don't accumulate.
        const size_t surface_bytes = (size_t)lbDrawSurface->pitch * (size_t)lbDrawSurface->h;
        if (RendererConsumeWorldDrawn())
        {
            if (m_world_raster_size != surface_bytes)
            {
                free(m_world_raster);
                m_world_raster = static_cast<uint8_t*>(malloc(surface_bytes));
                m_world_raster_size = m_world_raster ? surface_bytes : 0;
            }
            if (m_world_raster)
            {
                memcpy(m_world_raster, lbDrawSurface->pixels, surface_bytes);
                m_world_raster_valid = true;
            }
        }
        else if (m_world_raster_valid)
        {
            memcpy(lbDrawSurface->pixels, m_world_raster, surface_bytes);
        }

        if (ui)
            ui->ReplayMergedFromIR(m_render_graph.GetUIBuffersRT(),
                                   m_render_graph.GetTextBuffersRT(),
                                   text);
        CursorLayer_Draw();

        RendererSetWScreen(NULL);
    }

    SDL_Window* win = static_cast<SDL_Window*>(platform_get_sdl_window());
    // Refresh the window surface pointer each frame (guards against resize/alt-tab).
    s_screenSurface = SDL_GetWindowSurface(win);
    if (!s_screenSurface) {
        ERRORLOG("RendererSoftware::EndFrame: SDL_GetWindowSurface returned NULL: %s", SDL_GetError());
        return;
    }
    SDL_Rect dst = { 0, 0, s_screenSurface->w, s_screenSurface->h };

    if (s_scaleSurface != nullptr) {
        // Two-step: convert format then scale.
        if (!SDL_BlitSurface(lbDrawSurface, NULL, s_scaleSurface, NULL)) {
            ERRORLOG("RendererSoftware::EndFrame: format-convert blit failed: %s", SDL_GetError());
            return;
        }
        if (!SDL_BlitSurfaceScaled(s_scaleSurface, NULL, s_screenSurface, &dst, SDL_SCALEMODE_NEAREST)) {
            ERRORLOG("RendererSoftware::EndFrame: scale blit failed: %s", SDL_GetError());
            return;
        }
    } else if (lbDrawSurface->w != s_screenSurface->w || lbDrawSurface->h != s_screenSurface->h) {
        if (!SDL_BlitSurfaceScaled(lbDrawSurface, NULL, s_screenSurface, &dst, SDL_SCALEMODE_NEAREST)) {
            ERRORLOG("RendererSoftware::EndFrame: scaled blit failed: %s", SDL_GetError());
            return;
        }
    } else {
        if (!SDL_BlitSurface(lbDrawSurface, NULL, s_screenSurface, NULL)) {
            ERRORLOG("RendererSoftware::EndFrame: blit failed: %s", SDL_GetError());
            return;
        }
    }

    if (!SDL_UpdateWindowSurface(win)) {
        ERRORDBG(11, "RendererSoftware::EndFrame: flip failed: %s", SDL_GetError());
    }
}

void RendererSoftware::ClearScreen(uint8_t colour_index)
{
    if (lbDrawSurface)
        SDL_FillSurfaceRect(lbDrawSurface, NULL, colour_index);
}

uint8_t* RendererSoftware::LockFramebuffer(int* out_pitch)
{
    if (!lbDrawSurface)
        return nullptr;
    if (!SDL_LockSurface(lbDrawSurface))
        return nullptr;
    if (out_pitch)
        *out_pitch = lbDrawSurface->pitch;
    return static_cast<uint8_t*>(lbDrawSurface->pixels);
}

void RendererSoftware::UnlockFramebuffer()
{
    if (lbDrawSurface)
        SDL_UnlockSurface(lbDrawSurface);
}

bool RendererSoftware::PresentImage(const struct RendererPresentImageDesc* desc)
{
    // Only handle FMV frames with an embedded palette; other cases fall through
    // to the C bridge's existing software path (copy_raw8_image_buffer).
    if (!desc || !desc->src)
        return false;
    if (desc->format  != PRESENT_FORMAT_INDEXED8 ||
        desc->kind    != PRESENT_KIND_OPAQUE     ||
        desc->palette != PRESENT_PALETTE_EMBEDDED)
    {
        return false;
    }

    uint8_t* dst_buf = RendererGetWScreen();
    if (!dst_buf) return false;

    const int scanline = RendererScreenWidth();
    const int nlines   = RendererScreenHeight();
    const int src_pitch = desc->src_pitch ? desc->src_pitch : desc->src_w;

    // Clear letterbox bars (top + bottom)
    for (int y = 0; y < desc->dst_y; ++y)
        memset(&dst_buf[y * scanline], 0, scanline);
    for (int y = desc->dst_y + desc->dst_h; y < nlines; ++y)
        memset(&dst_buf[y * scanline], 0, scanline);

    // Nearest-neighbour blit from src to the destination rect in WScreen
    int dh_start = desc->dst_y;
    for (int sh = 0; sh < desc->src_h; ++sh)
    {
        const int dh_end = desc->dst_y + (desc->dst_h * (sh + 1) / desc->src_h);
        const uint8_t* src = &desc->src[sh * src_pitch];
        const int mh_min = (dh_start < 0) ? -dh_start : 0;
        const int mh_max = ((dh_end - dh_start) < (nlines - dh_start))
                         ? (dh_end - dh_start) : (nlines - dh_start);
        for (int k = mh_min; k < mh_max; ++k)
        {
            uint8_t* dst = &dst_buf[(dh_start + k) * scanline];
            // Clear left bar
            if (desc->dst_x > 0)
                memset(dst, 0, desc->dst_x);
            int dw_start = desc->dst_x;
            for (int sw = 0; sw < desc->src_w; ++sw)
            {
                const int dw_end = desc->dst_x + (desc->dst_w * (sw + 1) / desc->src_w);
                const int mw_min = (dw_start < 0) ? -dw_start : 0;
                const int mw_max = ((dw_end - dw_start) < (scanline - dw_start))
                                 ? (dw_end - dw_start) : (scanline - dw_start);
                for (int i = mw_min; i < mw_max; ++i)
                    dst[dw_start + i] = src[sw];
                dw_start = dw_end;
            }
            // Clear right bar
            if (dw_start < scanline)
                memset(dst + dw_start, 0, scanline - dw_start);
        }
        dh_start = dh_end;
    }
    return true;
}

bool RendererSoftware::SubmitTransparentBlit(const uint8_t* buf, int w, int h)
{
    if (!buf || !RendererGetWScreen()) return false;
    if (w != RendererScreenWidth() || h != RendererPhysicalHeight()) return false;

    uint8_t* dst = RendererGetWScreen();
    const size_t count = (size_t)w * (size_t)h;
    for (size_t i = 0; i < count; ++i)
    {
        if (buf[i] != 0)
            dst[i] = buf[i];
    }
    return true;
}

bool RendererSoftware::SubmitOverheadMap(const uint8_t* tile_colors, int tiles_x, int tiles_y,
                                         int dst_x, int dst_y, int dst_w, int dst_h)
{
    if (!tile_colors || tiles_x <= 0 || tiles_y <= 0) return false;

    uint8_t* dst_screen = RendererGetWScreen();
    if (!dst_screen) return false;

    const int block_w = dst_w / tiles_x;
    const int block_h = dst_h / tiles_y;
    const int screen_w = RendererScreenWidth();

    for (int ty = 0; ty < tiles_y; ++ty)
    {
        for (int tx = 0; tx < tiles_x; ++tx)
        {
            const int idx = (ty * tiles_x + tx) * 2;
            const uint8_t r_val = tile_colors[idx + 0];
            const uint8_t op = tile_colors[idx + 1];
            if (op == 0x00)
                continue;

            for (int py = 0; py < block_h; ++py)
            {
                uint8_t* dst = &dst_screen[(dst_y + ty * block_h + py) * screen_w + dst_x + tx * block_w];
                for (int px = 0; px < block_w; ++px, ++dst)
                {
                    switch (op)
                    {
                    case 0xFF:
                        *dst = r_val;
                        break;
                    case 0x01:
                        *dst = pixmap.ghost[((unsigned int)r_val << 8) | *dst];
                        break;
                    case 0x02:
                        *dst = (uint8_t)(102 + (pixmap.ghost[(64u << 8) | *dst] >> 6));
                        break;
                    case 0x03:
                        *dst = (uint8_t)(pixmap.ghost[((unsigned int)r_val << 8) | *dst] + 2);
                        break;
                    default:
                        *dst = r_val;
                        break;
                    }
                }
            }
        }
    }
    return true;
}

void RendererSoftware::SubmitZoomBoxTiles(const uint16_t* tile_block_ids, int tiles_x, int tiles_y,
                                          int dst_x, int dst_y, int tile_w, int tile_h)
{
    if (!tile_block_ids || tiles_x <= 0 || tiles_y <= 0) return;

    lbDisplay.DrawFlags = 0;
    setup_vecs(RendererGetWScreen(), NULL, (unsigned int)RendererScreenWidth(),
               (unsigned int)RendererScreenWidth(), (unsigned int)RendererScreenHeight());

    int scr_y = dst_y;
    for (int ty = 0; ty < tiles_y; ++ty)
    {
        int scr_x = dst_x;
        for (int tx = 0; tx < tiles_x; ++tx)
        {
            const uint16_t block = tile_block_ids[ty * tiles_x + tx];
            if (block != 0xFFFF)
                draw_texture(scr_x, scr_y, tile_w, tile_h, block, 0, -1);
            else
                LbDrawBox(scr_x, scr_y, tile_w, tile_h, 1);
            scr_x += tile_w;
        }
        scr_y += tile_h;
    }

    lbDisplay.DrawFlags |= Lb_SPRITE_OUTLINE;
    LbDrawBox(dst_x, dst_y, tiles_x * tile_w, tiles_y * tile_h, 0);
    lbDisplay.DrawFlags &= ~Lb_SPRITE_OUTLINE;
}

void RendererSoftware::DrawSwipeOverlay(struct TbSpriteSheet* sprites, int frame,
                                        bool draw_lr, int engine_window_x)
{
    static const int SPRITES_X = 3;
    static const int SPRITES_Y = 2;

    const struct TbSprite* sprlist = get_sprite(sprites, SPRITES_X * SPRITES_Y * frame);
    if (!sprlist)
        return;

    const struct TbSprite* startspr = &sprlist[1];
    const struct TbSprite* endspr   = &sprlist[1];
    long allwidth = 0;
    for (int n = 0; n < SPRITES_X; n++)
    {
        allwidth += endspr->SWidth;
        endspr++;
    }
    int units_per_px = (RendererPhysicalWidth() * 59 / 64) * 16 / allwidth;
    int scrpos_y = (m_screenH * 16 / units_per_px - (startspr->SHeight + endspr->SHeight)) / 2;

    if (draw_lr)
    {
        lbDisplay.DrawFlags = Lb_SPRITE_TRANSPAR4;
        int delta_y = sprlist[1].SHeight;
        for (int i = 0; i < SPRITES_X * SPRITES_Y; i += SPRITES_X)
        {
            const struct TbSprite* spr = &startspr[i];
            int scrpos_x = ((m_screenW + (2 * engine_window_x)) * 16 / units_per_px - allwidth) / 2;
            for (int n = 0; n < SPRITES_X; n++)
            {
                LbSpriteDrawResized(scrpos_x * units_per_px / 16, scrpos_y * units_per_px / 16, units_per_px, spr);
                scrpos_x += spr->SWidth;
                spr++;
            }
            scrpos_y += delta_y;
        }
    }
    else
    {
        lbDisplay.DrawFlags = Lb_SPRITE_TRANSPAR4 | Lb_SPRITE_FLIP_HORIZ;
        for (int i = 0; i < SPRITES_X * SPRITES_Y; i += SPRITES_X)
        {
            const struct TbSprite* spr = &sprlist[SPRITES_X + i];
            int delta_y = spr->SHeight;
            int scrpos_x = (m_screenW * 16 / units_per_px - allwidth) / 2;
            for (int n = 0; n < SPRITES_X; n++)
            {
                LbSpriteDrawResized(scrpos_x * units_per_px / 16, scrpos_y * units_per_px / 16, units_per_px, spr);
                scrpos_x += spr->SWidth;
                spr--;
            }
            scrpos_y += delta_y;
        }
    }
    lbDisplay.DrawFlags = 0;
}

void RendererSoftware::BeginLensCapture()
{
    m_lens_capture_active = false;
    if (!lens_is_ready())
        return;

    m_lens_buffer = lens_get_render_target();
    m_lens_buffer_w = lens_get_render_target_width();
    m_lens_buffer_h = lens_get_render_target_height();
    if (!m_lens_buffer || m_lens_buffer_w == 0 || m_lens_buffer_h == 0)
        return;

    m_saved_wscreen = RendererGetWScreen();
    m_saved_graphics_w = RendererScreenWidth();
    m_saved_graphics_h = RendererScreenHeight();
    RendererStoreViewport(&m_saved_viewport);

    memset(m_lens_buffer, 0, (size_t)m_lens_buffer_w * (size_t)m_lens_buffer_h * sizeof(TbPixel));
    RendererSetWScreen(m_lens_buffer);
    RendererSetScreenDimensions((int)m_lens_buffer_w, (int)m_lens_buffer_h);
    RendererSetViewport(0, 0, RendererScreenWidth(), RendererScreenHeight());
    setup_engine_window(0, 0, RendererGetScreenWidth(), RendererGetScreenHeight());
    m_lens_capture_active = true;
}

void RendererSoftware::EndLensCapture()
{
    if (!m_lens_capture_active)
        return;

    struct PlayerInfo* player = get_my_player();
    const long view_width = player->engine_window_width / pixel_size;
    const long view_height = player->engine_window_height / pixel_size;
    const long view_x = player->engine_window_x / pixel_size;
    const long view_y = player->engine_window_y / pixel_size;

    RendererSetWScreen(m_saved_wscreen);
    RendererSetScreenDimensions(m_saved_graphics_w, m_saved_graphics_h);
    RendererLoadViewport(&m_saved_viewport);
    setup_engine_window(0, 0, RendererGetScreenWidth(), RendererGetScreenHeight());

    const long dst_offset = view_y * RendererScreenWidth() + view_x;
    draw_lens_effect(RendererGetWScreen() + dst_offset, RendererScreenWidth(),
        m_lens_buffer, m_lens_buffer_w, view_width, view_height, view_x, game.applied_lens_type);

    m_lens_capture_active = false;
    m_lens_buffer = nullptr;
    m_lens_buffer_w = 0;
    m_lens_buffer_h = 0;
}
 
const char* RendererSoftware::GetName() const
{
    return "Software";
}

bool RendererSoftware::SupportsRuntimeSwitch() const
{
    return true;
}

IWorldViewRenderer* RendererSoftware::GetWorldViewRenderer()
{
    return m_worldViewRenderer;
}

IMapFadePass* RendererSoftware::GetMapFadePass()
{
    return m_mapFadePass;
}

ITextRenderer* RendererSoftware::GetTextRenderer()
{
    return m_textRenderer;
}

IUIRenderer* RendererSoftware::GetUIRenderer()
{
    return m_uiRenderer;
}

bool RendererSoftware::ScheduleScreenshot(const char* path, int fmt)
{
    if (!lbDrawSurface)
    {
        ERRORLOG("Screenshot: lbDrawSurface is null");
        return false;
    }
    bool ok;
    switch (fmt)
    {
        case 1:  ok = SDL_SavePNG(lbDrawSurface, path); break;
        case 2:  ok = SDL_SaveBMP(lbDrawSurface, path); break;
        default: ok = false; break;
    }
    if (!ok)
        ERRORLOG("Screenshot failed (%s): %s", path, SDL_GetError());
    return ok;
}
