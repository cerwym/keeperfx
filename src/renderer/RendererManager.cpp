/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererManager.cpp
 *     Renderer backend registration and lifecycle management.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "kfx/assets/SpriteSheetManager.h"
#include "kfx/assets/FontManager.h"
#include "renderer/SpriteHandle.h"
#include "renderer/ICursorLayer.h"
#include "renderer/ir/WorldCommands.h"
#include "renderer/backends/SWCursorLayer.h"

#include "renderer/RendererSoftware.h"
#include "renderer/backends/SoftwareWorldViewRenderer.h"
#include "renderer/backends/SoftwareMapFadePass.h"
#include "renderer/ILensRenderer.h"
#include "renderer/backends/SoftwareTextRenderer.h"
#include "renderer/backends/SoftwareUIRenderer.h"
#ifdef RENDERER_OPENGL_ENABLED
#  include "renderer/RendererOpenGL.h"
#  include "renderer/opengl/GLSpriteAtlas.h"
#endif
#ifdef PLATFORM_VITA
#  include "renderer/RendererVita.h"
#endif

#include "bflib_basics.h"
#include "bflib_datetm.h"
#include "globals.h"

#include "bflib_vidraw.h"      // LbSpriteDrawResized
#include "bflib_mouse.h"       // LbMouseOnBeginSwap / LbMouseOnEndSwap
#include "bflib_sprite.h"      // TbSprite
#include "engine_render.h"     // thing_being_displayed
#include "platform/PlatformManager.h" // PlatformManager_FrameTick
#include "platform/kfx_breadcrumb.h"  // KFX_BREADCRUMB
#include "gui_draw.h"          // get_panel_sprite
#include "config_spritecolors.h" // get_player_colored_icon_idx
#include "custom_sprites.h"    // get_button_sprite_for_player
#include "sprites.h"           // GBS_fontchars_number_dig0
#include "player_data.h"       // my_player_number
// Forward declaration to avoid pulling in frontend.h (conflicts with C++ stdlib)
extern "C" {
    struct TbSpriteSheet;
    extern struct TbSpriteSheet *button_sprites;
    extern struct TbSpriteSheet *custom_sprites;
    extern struct TbSpriteSheet *pointer_sprites;
    extern struct TbSpriteSheet *map_flag;
    // font globals from frontend.h
    extern struct TbSpriteSheet *winfont;
    extern struct TbSpriteSheet *font_sprites;
    extern struct TbSpriteSheet *frontend_font[4]; // FRONTEND_FONTS_COUNT
    // from front_credits.h
    extern struct TbSpriteSheet *frontstory_font;
    // from front_landview.c
    extern struct TbSpriteSheet *map_font;
    extern struct TbSpriteSheet *map_hand;
    // from front_torture_data.cpp / front_torture.c
    extern struct TbSpriteSheet *fronttor_sprites;
    extern struct DoorDesc doors[9]; // TORTURE_DOORS_COUNT
}
#include "thing_creature.h"    // swipe_sprites
#include "front_torture.h"     // fronttorture_sprites, doors[]
#include <unordered_map>
#include <vector>
#include "post_inc.h"

/******************************************************************************/

static IRenderer*           s_activeRenderer      = nullptr;
static RendererType         s_activeType          = RENDERER_INVALID;

// Renderer-private screen dimensions.
static TbScreenCoord        s_physicalScreenWidth  = 0;
static TbScreenCoord        s_physicalScreenHeight = 0;
static TbScreenCoord        s_graphicsScreenWidth  = 0;
static TbScreenCoord        s_graphicsScreenHeight = 0;
static unsigned char*       s_wscreen              = NULL;
static unsigned char*       s_graphicsWindowPtr    = NULL;

// Renderer-private graphics/clip window rect.
static long                 s_graphicsWindowX      = 0;
static long                 s_graphicsWindowY      = 0;
static long                 s_graphicsWindowWidth  = 0;
static long                 s_graphicsWindowHeight = 0;

// Frame-lifecycle state (replaces the old per-draw-bracket screen lock).
static bool                 s_frame_open           = false; // BeginFrame ran, EndFrame not yet
static bool                 s_fb_locked            = false; // CPU framebuffer published this frame (software)

/******************************************************************************/

void RendererNotifyTexturesReloaded()
{
    if (s_activeRenderer)
        s_activeRenderer->NotifyTexturesReloaded();
}

void RendererFlushRenderWork()
{
    if (s_activeRenderer)
        s_activeRenderer->FlushRenderWork();
}

void RendererNotifySpritesReloaded()
{
    // Full GUI data reload — mark for init_gui() after next level load, bump
    // font generation so queued text commands from the old font are dropped,
    // and re-latch the slab-texture pointer.
    // Atlas rebuild is scheduled automatically by SpriteSheetManager::Load/Free
    // at the individual callsites before this function is reached.
    SpriteSheetManager::Get().MarkGUIDirty();
    FontManager::Get().BumpGeneration();
#ifdef RENDERER_OPENGL_ENABLED
    SYNCLOG("RendererNotifySpritesReloaded: gui dirty set, font gen=%u",
            FontManager::Get().GetGeneration());
    UIRenderer_SetSlabTexture();
#endif
}

/** Notify that map_flag was loaded.  GL: atlas rebuild is scheduled by the
 *  SpriteSheetMgr_Load() at the callsite (RegisterSpriteSheet is a no-op).
 *  Software: register handle table entries so SubmitPanelSprite falls back to
 *  LbSpriteDrawResized correctly. */
void RendererNotifyLandviewFlagLoaded()
{
    if (RendererGetUIRenderer())
        RendererGetUIRenderer()->RegisterSpriteSheet(map_flag);
}

void RendererNotifyGameTablesReady()
{
    if (s_activeRenderer)
        s_activeRenderer->NotifyGameTablesReady();
}

TbBool RendererSubmitTransparentBlit(const unsigned char* buf, int w, int h)
{
    IRenderer* rend = RendererGetActive();
    if (!rend) return false;
    return rend->SubmitTransparentBlit(buf, w, h) ? true : false;
}

TbBool RendererDrawLandviewFrame(const struct TbHugeSprite* spr, long sp_len,
                                 int xshift, int yshift, int units_per_px)
{
    IRenderer* rend = RendererGetActive();
    if (!rend) return false;
    return rend->DrawLandviewFrame(spr, sp_len, xshift, yshift, units_per_px) ? true : false;
}

void RendererDrawSwipeOverlay(struct TbSpriteSheet* sprites, int frame,
                              int draw_lr, int engine_window_x)
{
    IRenderer* rend = RendererGetActive();
    if (!rend) return;
    rend->DrawSwipeOverlay(sprites, frame, draw_lr != 0, engine_window_x);
}

void RendererBeginLensCapture(void)
{
    ILensRenderer* lens = RendererGetLensRenderer();
    if (lens) lens->BeginWorldCapture();
}

void RendererEndLensCapture(void)
{
    // If the lens armed a capture this frame, flag the world pass the engine just
    // recorded as a lens capture so the world resolver routes it through the lens
    // buffer.  GL applies its own lens in EndFrame_GL (IsWorldCaptureActive false).
    ILensRenderer* lens = RendererGetLensRenderer();
    if (lens && lens->IsWorldCaptureActive())
        if (IWorldViewRenderer* w = RendererGetWorldViewRenderer())
            w->MarkDeferredWorldAsLensCapture();
}
 
TbBool RendererSubmitOverheadMap(const unsigned char* tile_colors, int tiles_x, int tiles_y,
                                  int dst_x, int dst_y, int dst_w, int dst_h)
{
    IRenderer* rend = RendererGetActive();
    if (!rend) return false;
    return rend->SubmitOverheadMap(tile_colors, tiles_x, tiles_y,
                                   dst_x, dst_y, dst_w, dst_h) ? true : false;
}

void RendererSubmitZoomBoxTiles(const unsigned short* tile_block_ids, int tiles_x, int tiles_y,
                                int dst_x, int dst_y, int tile_w, int tile_h)
{
    IRenderer* rend = RendererGetActive();
    if (rend)
        rend->SubmitZoomBoxTiles((const uint16_t*)tile_block_ids, tiles_x, tiles_y,
                                  dst_x, dst_y, tile_w, tile_h);
}

/******************************************************************************/
/* Zoom-box render mode                                                       */
/******************************************************************************/


ZoomBoxMode RendererGetZoomBoxMode(void)
{
    return static_cast<ZoomBoxMode>(g_renderer_settings.zoom_box_mode);
}

void RendererSetZoomBoxMode(ZoomBoxMode mode)
{
    g_renderer_settings.zoom_box_mode = mode;
}

static float s_zoom_box_clip_radius = -1.0f;

void RendererSetZoomBoxClipRadius(float radius)
{
    s_zoom_box_clip_radius = radius;
}

float RendererGetZoomBoxClipRadius(void)
{
    return s_zoom_box_clip_radius;
}

// Screen-space rect of the active zoom box for the current frame.
// Set by ZoomBoxView::draw() (via RendererSetZoomBoxScreenRect) before terrain
// and things are submitted so draw_overhead_* can skip markers inside the box.
static struct { int x0, y0, x1, y1; int active; } s_zoom_box_screen_rect = {0,0,0,0,0};

void RendererSetZoomBoxScreenRect(int x0, int y0, int x1, int y1)
{
    s_zoom_box_screen_rect.x0 = x0;
    s_zoom_box_screen_rect.y0 = y0;
    s_zoom_box_screen_rect.x1 = x1;
    s_zoom_box_screen_rect.y1 = y1;
    s_zoom_box_screen_rect.active = 1;
}

void RendererClearZoomBoxScreenRect(void)
{
    s_zoom_box_screen_rect.active = 0;
}

int RendererPointInZoomBoxScreenRect(int x, int y)
{
    if (!s_zoom_box_screen_rect.active) return 0;
    return (x >= s_zoom_box_screen_rect.x0 && x < s_zoom_box_screen_rect.x1 &&
            y >= s_zoom_box_screen_rect.y0 && y < s_zoom_box_screen_rect.y1);
}

void RendererSchedulePiPRender(struct Camera* cam, int x, int y, int w, int h)
{
    IRenderer* rend = RendererGetActive();
    if (rend)
        rend->SubmitPiPRender(cam, x, y, w, h);
}

/******************************************************************************/

// Sprite -> handle resolution lives on the active UI renderer: the software
// (base) renderer lazily mints and registers handles; the GL renderer looks them
// up in its atlas.  The manager just forwards to whichever is active.
SpriteHandle RendererResolveSprite(const TbSprite* spr)
{
    return RendererGetUIRenderer() ? RendererGetUIRenderer()->ResolveSprite(spr) : kInvalidSpriteHandle;
}

/** Allocates a new backend instance for the requested type.
 *  Returns nullptr if the type is unknown or not compiled in. */
static IRenderer* create_renderer(RendererType type)
{
    switch (type)
    {
        case RENDERER_SOFTWARE:
            return new RendererSoftware();

#ifdef RENDERER_OPENGL_ENABLED
        case RENDERER_OPENGL:
            return new RendererOpenGL();
#endif

#ifdef PLATFORM_VITA
        case RENDERER_VITA:
            return new RendererVita();
#endif

        default:
            return nullptr;
    }
}

/** Resolve RENDERER_AUTO to a concrete type.
 *  Prefers OpenGL when available, falls back to software. */
static RendererType resolve_auto()
{
#ifdef PLATFORM_VITA
    return RENDERER_VITA;
#elif defined(RENDERER_OPENGL_ENABLED)
    return RENDERER_OPENGL;
#else
    return RENDERER_SOFTWARE;
#endif
}

/******************************************************************************/

int RendererInit(RendererType type)
{
    // Initialise settings to defaults on first call.
    // keeperfx.cfg parsing may override startup-only knobs (e.g. palette_mode)
    // before this function is reached; only reset if this is the first init.
    static int s_settings_initialised = 0;
    if (!s_settings_initialised) {
        RendererSettings_Reset();
        s_settings_initialised = 1;
    }

    // Register all managed sprite/font slots (idempotent — safe on reinit).
    {
        auto& ssmgr = SpriteSheetManager::Get();
        ssmgr.Register(&gui_panel_sprites, "gui_panel_sprites");
        ssmgr.Register(&button_sprites,    "button_sprites");
        ssmgr.Register(&custom_sprites,    "custom_sprites");
        ssmgr.Register(&pointer_sprites,   "pointer_sprites");
        ssmgr.Register(&frontend_sprite,   "frontend_sprite");
        ssmgr.Register(&map_flag,          "map_flag");
        ssmgr.Register(&map_hand,          "map_hand");
        ssmgr.Register(&swipe_sprites,     "swipe_sprites");
        ssmgr.Register(&fronttor_sprites,  "fronttor_sprites");
        for (int i = 0; i < TORTURE_DOORS_COUNT; ++i) {
            static char door_names[TORTURE_DOORS_COUNT][16]; // stable storage for name strings
            if (!door_names[i][0]) snprintf(door_names[i], sizeof(door_names[i]), "door[%d]", i);
            ssmgr.Register(&doors[i].sprites, door_names[i]);
        }
    }
    {
        auto& fmgr = FontManager::Get();
        fmgr.Register(&winfont,           "winfont");
        fmgr.Register(&font_sprites,      "font_sprites");
        fmgr.Register(&map_font,          "map_font");
        fmgr.Register(&frontend_font[0],  "frontend_font[0]");
        fmgr.Register(&frontend_font[1],  "frontend_font[1]");
        fmgr.Register(&frontend_font[2],  "frontend_font[2]");
        fmgr.Register(&frontend_font[3],  "frontend_font[3]");
        fmgr.Register(&frontstory_font,   "frontstory_font");
    }

    if (type == RENDERER_AUTO)
        type = resolve_auto();

    IRenderer* backend = create_renderer(type);
    if (!backend)
    {
        ERRORLOG("RendererInit: unknown or unsupported renderer type %d", (int)type);
        return false;
    }

    if (!backend->Init())
    {
        ERRORLOG("RendererInit: backend '%s' failed to initialise", backend->GetName());
        delete backend;
        return false;
    }

    s_activeRenderer = backend;
    s_activeType     = type;
    SYNCLOG("Renderer initialised: %s", backend->GetName());

    // The backend created and owns its sub-renderers inside Init().  Register the
    // GUI sprite sheets on the active UI renderer and schedule the atlas rebuild.
    // On the GL backend RegisterSpriteSheet() is a no-op (the atlas Rebuild path
    // owns registration); ScheduleRebuild() is a no-op flag on software.
    if (IUIRenderer* ui = RendererGetUIRenderer())
    {
        ui->RegisterSpriteSheet(gui_panel_sprites);
        ui->RegisterSpriteSheet(button_sprites);
        SYNCLOG("UIRenderer initialised: %s", ui->GetName());
    }
    SpriteSheetManager::Get().ScheduleRebuild();


    return true;
}

void RendererShutdown()
{

    // The backend owns its sub-renderers and destroys them inside Shutdown()
    // (after joining its render thread and while its GL context is still current),
    // so tearing the backend down releases everything in the correct order.
    if (s_activeRenderer)
    {
        s_activeRenderer->Shutdown();
        FontManager::Get().BumpGeneration();
        delete s_activeRenderer;
        s_activeRenderer = nullptr;
    }
    s_activeType = RENDERER_INVALID;
}

IRenderer* RendererGetActive()
{
    return s_activeRenderer;
}

// Sub-renderers are owned by the active backend and vended through its
// IRenderer::GetXxx() virtuals; the manager just forwards to whichever backend
// is active.
IWorldViewRenderer* RendererGetWorldViewRenderer()
{
    return s_activeRenderer ? s_activeRenderer->GetWorldViewRenderer() : nullptr;
}

IMapFadePass* RendererGetMapFadePass()
{
    return s_activeRenderer ? s_activeRenderer->GetMapFadePass() : nullptr;
}

ILensRenderer* RendererGetLensRenderer()
{
    return s_activeRenderer ? s_activeRenderer->GetLensRenderer() : nullptr;
}

ITextRenderer* RendererGetTextRenderer()
{
    return s_activeRenderer ? s_activeRenderer->GetTextRenderer() : nullptr;
}

IUIRenderer* RendererGetUIRenderer()
{
    return s_activeRenderer ? s_activeRenderer->GetUIRenderer() : nullptr;
}

ICursorLayer* RendererGetCursorLayer()
{
    return s_activeRenderer ? s_activeRenderer->GetCursorLayer() : nullptr;
}

GLSpriteAtlas* RendererGetSpriteAtlas()
{
#ifdef RENDERER_OPENGL_ENABLED
    if (s_activeType == RENDERER_OPENGL)
        return static_cast<RendererOpenGL*>(s_activeRenderer)->GetSpriteAtlas();
#endif
    return nullptr;
}

extern "C" const unsigned char* LbPaletteGetReadonly(void);

const unsigned char* RendererGetActivePalette()
{
    // The currently-active 6-bit VGA palette (what indexed drawing samples). This
    // is NOT the fixed in-game engine_palette:
    return LbPaletteGetReadonly();
}

RendererType RendererGetActiveType()
{
    return s_activeType;
}

TbBool RendererWantsFullscreenViewport()
{
    return (s_activeRenderer && s_activeRenderer->GetCapabilities().wantsFullscreenViewport) ? 1 : 0;
}

struct BackendCapabilities RendererGetCapabilities()
{
    if (s_activeRenderer)
        return s_activeRenderer->GetCapabilities();
    struct BackendCapabilities empty = {};
    return empty;
}

TbBool RendererScheduleScreenshot(const char* path, int fmt)
{
    if (!s_activeRenderer)
        return 0;
    return s_activeRenderer->ScheduleScreenshot(path, fmt) ? 1 : 0;
}

/******************************************************************************/
/* C-callable wrappers */
/******************************************************************************/

unsigned char* RendererLockFramebuffer(int* out_pitch)
{
    if (!s_activeRenderer)
        return nullptr;
    return s_activeRenderer->LockFramebuffer(out_pitch);
}

void RendererUnlockFramebuffer(void)
{
    if (s_activeRenderer)
        s_activeRenderer->UnlockFramebuffer();
}

int RendererBeginFrame(void)
{
    if (!s_activeRenderer)
        return 0;
    if (!s_activeRenderer->BeginFrame())
        return 0;
    if (!s_fb_locked && !RendererGetCapabilities().hasGPURenderPath)
    {
        int pitch = 0;
        unsigned char* pixels = RendererLockFramebuffer(&pitch);
        if (!pixels)
            return 0; // CPU framebuffer unavailable — frame not drawable
        s_wscreen = pixels;
        s_graphicsScreenWidth = pitch;
        s_graphicsWindowPtr = &s_wscreen[s_graphicsWindowX +
            s_graphicsScreenWidth * s_graphicsWindowY];
        s_fb_locked = true;
    }
    s_frame_open = true;
    return 1;
}

void RendererEndFrame(void)
{
    // Release the whole-frame CPU framebuffer lock BEFORE the backend's EndFrame.
    if (s_fb_locked)
    {
        RendererUnlockFramebuffer();
        s_fb_locked = false;
    }
    if (s_activeRenderer)
        s_activeRenderer->EndFrame();
    s_wscreen = NULL;
    s_graphicsWindowPtr = NULL;
    s_frame_open = false;
}

void RendererClearScreen(unsigned char colour_index)
{
    if (s_activeRenderer)
        s_activeRenderer->ClearScreen(colour_index);
}

/******************************************************************************/
/* High-level screen lifecycle (replaces LbScreen* trampolines)               */
/******************************************************************************/

void RendererPresentFrame(void)
{
    PlatformManager_FrameTick();
    // Ensure BeginFrame() has run — many call sites (fade loops, screen-mode
    // transitions, draw_clear_screen) do ClearScreen+PresentFrame without a
    // preceding BeginFrame().  BeginFrame() is idempotent, so this is a no-op on
    // the normal path where the frame was already opened.
    RendererBeginFrame();
    TbResult ret = LbMouseOnBeginSwap();
    if (ret != Lb_SUCCESS) {
        // Mouse swap failed — log once so it's detectable, but do not skip rendering.
        // In the GL renderer the cursor is drawn by GLCursorLayer (hardware sprites)
        // and does not depend on the software pointer-swap protocol; dropping the
        // entire frame here would cause a visible stutter.
        static int s_warn = 0;
        if (s_warn++ < 5)
            WARNLOG("RendererPresentFrame: LbMouseOnBeginSwap failed (ret=%d), rendering continued", (int)ret);
    }
    RendererEndFrame();
    LbMouseOnEndSwap();
}

int RendererIsFrameOpen(void)
{
    return s_frame_open ? 1 : 0;
}

TbBool RendererReadFramePixels(RendererFramePixelsFn fn, void* user)
{
    if (!fn || !s_wscreen)
        return false;
    return fn(s_wscreen, (int)RendererScreenWidth(), (int)RendererScreenHeight(),
              (int)s_graphicsScreenWidth, user);
}


/******************************************************************************/
/* Screen setup / teardown (replaces LbScreenSetup / LbScreenReset)           */
/******************************************************************************/

// Internal bflib_video.c implementations — kept there because they own SDL statics.
extern "C" TbResult LbScreenSetup(TbScreenMode mode, TbScreenCoord width, TbScreenCoord height,
    unsigned char *palette, short buffers_count, TbBool wscreen_vid);
extern "C" TbResult LbScreenReset(TbBool exiting_application);

TbResult RendererSetupScreen(TbScreenMode mode, TbScreenCoord width, TbScreenCoord height,
    unsigned char *palette, short buffers_count, TbBool wscreen_vid)
{
    return LbScreenSetup(mode, width, height, palette, buffers_count, wscreen_vid);
}

TbResult RendererResetScreen(TbBool exiting_application)
{
    return LbScreenReset(exiting_application);
}

/******************************************************************************/
/* Graphics viewport (replaces LbScreenSetGraphicsWindow / Store / Load)      */
/******************************************************************************/

void RendererSetViewport(int32_t x, int32_t y, int32_t width, int32_t height)
{
    int32_t right_edge = x + width;
    int32_t bottom_edge = y + height;
    // Swap if inverted
    if (right_edge < x) { int32_t t = x; x = right_edge; right_edge = t; }
    if (bottom_edge < y) { int32_t t = y; y = bottom_edge; bottom_edge = t; }
    // Clamp to screen
    if (x < 0) x = 0;
    if (right_edge < 0) right_edge = 0;
    if (y < 0) y = 0;
    if (bottom_edge < 0) bottom_edge = 0;
    if (x > s_graphicsScreenWidth) x = s_graphicsScreenWidth;
    if (right_edge > s_graphicsScreenWidth) right_edge = s_graphicsScreenWidth;
    if (y > RendererScreenHeight()) y = RendererScreenHeight();
    if (bottom_edge > RendererScreenHeight()) bottom_edge = RendererScreenHeight();
    s_graphicsWindowX = x;
    s_graphicsWindowY = y;
    s_graphicsWindowWidth = right_edge - x;
    s_graphicsWindowHeight = bottom_edge - y;
    if (s_wscreen != NULL)
        s_graphicsWindowPtr = s_wscreen + s_graphicsScreenWidth * y + x;
    else
        s_graphicsWindowPtr = NULL;
}

void RendererStoreViewport(TbGraphicsWindow *grwnd)
{
    grwnd->x = s_graphicsWindowX;
    grwnd->y = s_graphicsWindowY;
    grwnd->width = s_graphicsWindowWidth;
    grwnd->height = s_graphicsWindowHeight;
    grwnd->ptr = NULL;
}

void RendererLoadViewport(TbGraphicsWindow *grwnd)
{
    s_graphicsWindowX = grwnd->x;
    s_graphicsWindowY = grwnd->y;
    s_graphicsWindowWidth = grwnd->width;
    s_graphicsWindowHeight = grwnd->height;
    if (s_wscreen != NULL)
        s_graphicsWindowPtr = s_wscreen
            + s_graphicsScreenWidth * s_graphicsWindowY + s_graphicsWindowX;
    else
        s_graphicsWindowPtr = NULL;
}

/******************************************************************************/
/* Display property accessors                                                 */
/******************************************************************************/

TbScreenCoord RendererPhysicalWidth(void)  { return s_physicalScreenWidth;  }
TbScreenCoord RendererPhysicalHeight(void) { return s_physicalScreenHeight; }
TbScreenCoord RendererScreenWidth(void)    { return s_graphicsScreenWidth;  }
TbScreenCoord RendererScreenHeight(void)   { return s_graphicsScreenHeight; }
long RendererGraphicsWindowX(void)      { return s_graphicsWindowX;      }
long RendererGraphicsWindowY(void)      { return s_graphicsWindowY;      }
long RendererGraphicsWindowWidth(void)  { return s_graphicsWindowWidth;  }
long RendererGraphicsWindowHeight(void) { return s_graphicsWindowHeight; }
unsigned short RendererGetScreenWidth(void)  { return MyScreenWidth; }
unsigned short RendererGetScreenHeight(void) { return MyScreenHeight; }
unsigned char* RendererGetWScreen(void)    { return s_wscreen; }
unsigned char* RendererGetGraphicsWindowPtr(void) { return s_graphicsWindowPtr; }

void RendererSetWScreen(unsigned char* buf)
{
    s_wscreen = buf;
    if (!buf)
        s_graphicsWindowPtr = NULL;
}

void RendererSetScreenDimensions(int width, int height)
{
    s_graphicsScreenWidth  = width;
    s_graphicsScreenHeight         = height;
}

/** Set the physical (video-mode) resolution. */
void RendererSetPhysicalDimensions(int width, int height)
{
    s_physicalScreenWidth  = width;
    s_physicalScreenHeight = height;
    s_wscreen = NULL;
    s_graphicsWindowPtr = NULL;
}

/******************************************************************************/
/* Palette management (replaces LbPalette* functions)                         */
/******************************************************************************/

extern "C" TbResult LbPaletteSet(unsigned char *palette);
extern "C" TbResult LbPaletteGet(unsigned char *palette);
extern "C" long LbPaletteFade(unsigned char *pal, long fade_steps, enum TbPaletteFadeFlag flg);
extern "C" TbResult LbPaletteStopOpenFade(void);

TbResult RendererPaletteSet(unsigned char *palette) { return LbPaletteSet(palette); }
TbResult RendererPaletteGet(unsigned char *palette) { return LbPaletteGet(palette); }
int32_t RendererPaletteFade(unsigned char *pal, int32_t fade_steps, enum TbPaletteFadeFlag flg) { return LbPaletteFade(pal, fade_steps, flg); }
TbResult RendererPaletteStopFade(void) { return LbPaletteStopOpenFade(); }

void RendererApplyPossessionPalette(long step, const unsigned char *main_palette)
{
    // GPU renderers use the screen tint overlay for possession/pain effects;
    // the software path must modify the INDEX8 surface palette directly.
    if (RendererGetCapabilities().hasGPURenderPath)
        return;
    unsigned char palette[PALETTE_SIZE];
    for (int i = 0; i < PALETTE_COLORS; i++)
    {
        const unsigned char *src = &main_palette[3 * i];
        unsigned char       *dst = &palette[3 * i];
        unsigned long pix = ((step * (((long)src[0]) - 63)) / 120) + 63;
        if (pix > 63) pix = 63;
        dst[0] = (unsigned char)pix;
        pix = (step * ((long)src[1])) / 120;
        if (pix > 63) pix = 63;
        dst[1] = (unsigned char)pix;
        pix = (step * ((long)src[2])) / 120;
        if (pix > 63) pix = 63;
        dst[2] = (unsigned char)pix;
    }

    RendererPaletteSet(palette);
}

/******************************************************************************/
/* Screen lifecycle helpers                                                   */
/******************************************************************************/

extern "C" TbResult LbScreenInitialize(void);
extern "C" TbResult LbScreenSetDoubleBuffering(TbBool state);
extern "C" TbResult LbSetTitle(const char *title);
extern "C" TbResult LbSetIcon(unsigned short nicon);
extern "C" TbScreenMode LbScreenActiveMode(void);

TbResult RendererScreenInitialize(void)         { return LbScreenInitialize(); }
TbResult RendererSetDoubleBuffering(TbBool state){ return LbScreenSetDoubleBuffering(state); }
TbResult RendererSetTitle(const char *title)     { return LbSetTitle(title); }
TbResult RendererSetIcon(unsigned short nicon)   { return LbSetIcon(nicon); }
TbScreenMode RendererActiveMode(void)            { return LbScreenActiveMode(); }

/******************************************************************************/
/* C-callable world-view renderer wrappers */
/******************************************************************************/

void WorldViewRenderer_BeginWorldPass(int w, int h, int vp_x, int vp_y)
{
    if (RendererGetWorldViewRenderer())
        RendererGetWorldViewRenderer()->BeginWorldPass(w, h, vp_x, vp_y);
}

void WorldViewRenderer_DrawIsometricView(void)
{
    if (RendererGetWorldViewRenderer())
        RendererGetWorldViewRenderer()->DrawIsometricView();
}

void WorldViewRenderer_DrawFrontView(struct Camera* cam)
{
    if (RendererGetWorldViewRenderer())
        RendererGetWorldViewRenderer()->DrawFrontView(cam);
}

int RendererExecutePendingWorld(void)
{
    IWorldViewRenderer* w = RendererGetWorldViewRenderer();
    return w ? w->ResolveDeferredWorld() : 0;
}

void RendererReexecuteWorld(void)
{
    if (IWorldViewRenderer* w = RendererGetWorldViewRenderer())
        w->ReexecuteDeferredWorld();
}

int WorldViewRenderer_SubmitKeeperSprite(int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
                                         const unsigned char* data, int src_w, int src_h,
                                         unsigned int draw_flags, const unsigned char* remap,
                                         int32_t sprite_id)
{
    if (RendererGetWorldViewRenderer())
        return RendererGetWorldViewRenderer()->SubmitKeeperSprite(dst_x, dst_y, dst_w, dst_h,
                                                       data, src_w, src_h, draw_flags, remap, sprite_id);
    return 0;
}

static int SubmitWorldShadowCmd(const IRWorldShadowCmd& cmd)
{
    if (RendererGetWorldViewRenderer())
        return RendererGetWorldViewRenderer()->SubmitWorldShadowCmd(cmd);
    return 0;
}

int WorldViewRenderer_SubmitWorldShadow(const struct WorldShadowSubmitCmd* cmd)
{
    if (!cmd || !RendererGetWorldViewRenderer())
        return 0;

    IRWorldShadowCmd ir_cmd;
    for (int i = 0; i < 4; ++i)
    {
        ir_cmd.verts[i].x = cmd->verts[i].x;
        ir_cmd.verts[i].y = cmd->verts[i].y;
        ir_cmd.verts[i].u = cmd->verts[i].u;
        ir_cmd.verts[i].v = cmd->verts[i].v;
    }
    ir_cmd.anim_sprite   = cmd->anim_sprite;
    ir_cmd.angle         = cmd->angle;
    ir_cmd.current_frame = cmd->current_frame;
    ir_cmd.tex_w         = cmd->tex_w;
    ir_cmd.tex_h         = cmd->tex_h;
    ir_cmd.darkness      = cmd->darkness;
    ir_cmd.is_circle     = cmd->is_circle;
    ir_cmd.ndc_z         = cmd->ndc_z;
    ir_cmd.wx            = cmd->wx;
    ir_cmd.wy            = cmd->wy;
    ir_cmd.wz            = cmd->wz;
    ir_cmd.sort_key      = cmd->sort_key;
    return SubmitWorldShadowCmd(ir_cmd);
}

int WorldViewRenderer_BeginWorldSpriteCapture(int bucket_idx)
{
    if (RendererGetWorldViewRenderer())
        return RendererGetWorldViewRenderer()->BeginWorldSpriteCapture(bucket_idx);
    return 0;
}

int WorldViewRenderer_UsesFillTimeWorldSubmit(void)
{
    if (RendererGetWorldViewRenderer())
        return RendererGetWorldViewRenderer()->UsesFillTimeWorldSubmit();
    return 0;
}

void WorldViewRenderer_ClearKeeperSpriteAtlas(void)
{
    if (RendererGetWorldViewRenderer())
        RendererGetWorldViewRenderer()->ClearKeeperSpriteAtlas();
}

void WorldViewRenderer_PreloadKeeperSpriteAtlas(void)
{
    if (RendererGetWorldViewRenderer())
        RendererGetWorldViewRenderer()->PreloadKeeperSpriteAtlas();
}

uint32_t RendererGetTextFontGeneration(void)
{
    return FontManager::Get().GetGeneration();
}

/******************************************************************************/
/* Sprite-owner tracking for the depth-fail creature outline                  */
/******************************************************************************/

static int s_current_sprite_owner        = -1;
static int s_current_sprite_wants_outline =  0;

void WorldViewRenderer_SetCurrentSpriteContext(int player_idx, int wants_outline)
{
    s_current_sprite_owner        = player_idx;
    s_current_sprite_wants_outline = wants_outline;
}

int WorldViewRenderer_GetCurrentSpriteOwner(void)
{
    return s_current_sprite_owner;
}

int WorldViewRenderer_GetCurrentSpriteWantsOutline(void)
{
    return s_current_sprite_wants_outline;
}

/******************************************************************************/
/* C-callable cursor layer wrappers                                           */
/******************************************************************************/

void CursorLayer_Draw(void)
{
    if (RendererGetCursorLayer())
        RendererGetCursorLayer()->Draw();
}

void CursorLayer_Clear(void)
{
    if (RendererGetCursorLayer())
        RendererGetCursorLayer()->Clear();
}

void CursorLayer_FlipBuffers(void)
{
    if (RendererGetCursorLayer())
        RendererGetCursorLayer()->FlipBuffers();
}

void CursorLayer_SubmitPointerSprite(const struct TbSprite* spr, int32_t x, int32_t y, int units_per_px)
{
    if (RendererGetCursorLayer())
        RendererGetCursorLayer()->SubmitPointerSprite(spr, x, y, units_per_px);
}

void CursorLayer_SubmitKeeperHandSprite(short x, short y, unsigned short kspr_base,
                                        short kspr_angle, unsigned char sprgroup, int32_t scale,
                                        TbDrawFlagsMask draw_flags)
{
    if (RendererGetCursorLayer())
        RendererGetCursorLayer()->SubmitKeeperHandSprite(x, y, kspr_base, kspr_angle, sprgroup, scale, draw_flags);
}
/******************************************************************************/

void MapFadePass_PrepareBuffers(unsigned char* fade_src, unsigned char* fade_dest, int scanline, int height)
{
    if (RendererGetMapFadePass())
        RendererGetMapFadePass()->PrepareBuffers(fade_src, fade_dest, scanline, height);
}

int32_t MapFadePass_StepFadeIn(int32_t step)
{
    if (RendererGetMapFadePass())
        return RendererGetMapFadePass()->StepFadeIn(step);
    return step; // no-op: don't advance if not initialised
}

int32_t MapFadePass_StepFadeOut(int32_t step)
{
    if (RendererGetMapFadePass())
        return RendererGetMapFadePass()->StepFadeOut(step);
    return step;
}

TbBool MapFadePass_SupportsNativeResolution(void)
{
    if (RendererGetMapFadePass())
        return RendererGetMapFadePass()->SupportsNativeResolution() ? 1 : 0;
    return 0;
}

/******************************************************************************/
/* C-callable raw framebuffer blit                                            */
/******************************************************************************/

// Forward-declared: implemented in front_simple.c — used by the software blit path.
extern "C" TbBool copy_raw8_image_buffer(
    unsigned char *dst_buf, const int scanline, const int nlines,
    const int dst_width, const int dst_height, const int spw, const int sph,
    const unsigned char *src_buf, const int src_width, const int src_height);

TbBool RendererPresentImage(const struct RendererPresentImageDesc* desc)
{
    if (!desc || !desc->src) return false;

    // Backend virtual — GPU queues an IR present, software handles FMV
    // embedded-palette blits via its PresentImage override.
    IRenderer* rend = RendererGetActive();
    if (rend && rend->PresentImage(desc))
        return true;

    // Fallback for the classic opaque game-palette blit (menu backgrounds,
    // loading screens). Transparent overlays, landview zoom, and RGBA8 are
    // not supported here; their callers use backend-specific paths.
    if (desc->format  != PRESENT_FORMAT_INDEXED8 ||
        desc->kind    != PRESENT_KIND_OPAQUE     ||
        desc->palette != PRESENT_PALETTE_GAME)
    {
        return false;
    }
    return copy_raw8_image_buffer(
        s_wscreen,
        RendererScreenWidth(), RendererScreenHeight(),
        desc->dst_w, desc->dst_h, desc->dst_x, desc->dst_y,
        desc->src, desc->src_w, desc->src_h);
}

void RendererNotifyFmvPalette(const unsigned char* bgra_1024)
{
    IRenderer* rend = RendererGetActive();
    if (rend) rend->NotifyFmvPalette(bgra_1024);
}

TbBool RendererSubmitLandviewZoom(const unsigned char* src_buf, int src_w, int src_h,
                                  float center_map_x, float center_map_y,
                                  float screen_cx,    float screen_cy,
                                  float scale)
{
    IRenderer* rend = RendererGetActive();
    if (!rend) return false;
    return rend->SubmitLandviewZoom(src_buf, src_w, src_h,
                                    center_map_x, center_map_y,
                                    screen_cx, screen_cy, scale) ? true : false;
}

void RendererApplySettings(const RendererSettings* s)
{
    if (!s) return;

    int prev_palette_mode = g_renderer_settings.palette_mode;
    g_renderer_settings = *s;

    // If the atlas colour format changed, rebuild the sprite atlas immediately
    // so the new format takes effect without a restart.
    if (g_renderer_settings.palette_mode != prev_palette_mode) {
        // Palette format changed — rebuild the atlas under the new format, and
        // invalidate font caches / GUI state as if sprites were reloaded.
        SpriteSheetManager::Get().ScheduleRebuild();
        RendererNotifySpritesReloaded();
    }

    // Propagate zoom-box mode to the runtime selector.
    RendererSetZoomBoxMode((ZoomBoxMode)g_renderer_settings.zoom_box_mode);

    // TODO: push shade/filter uniforms to the active world-view renderer
}

const RendererSettings* RendererGetSettings(void)
{
    return &g_renderer_settings;
}

/******************************************************************************/
/* Screen-tint overlay                                                        */
/******************************************************************************/

float g_screen_tint[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

void RendererSetScreenTint(float r, float g, float b, float a)
{
    g_screen_tint[0] = r;
    g_screen_tint[1] = g;
    g_screen_tint[2] = b;
    g_screen_tint[3] = a;
}

static int g_fade_cache_preserve = 0;
static int g_force_ui_flip = 0;

void RendererPreserveFadeCache(int active)
{
    g_fade_cache_preserve = active;
}

int RendererIsFadeCachePreserved(void)
{
    return g_fade_cache_preserve;
}

void RendererForceUIFlipNextFrame(void)
{
    g_force_ui_flip = 1;
}

int RendererConsumeForceUIFlip(void)
{
    int v = g_force_ui_flip;
    g_force_ui_flip = 0;
    return v;
}
