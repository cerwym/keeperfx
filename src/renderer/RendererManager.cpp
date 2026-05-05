/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererManager.cpp
 *     Renderer backend registration and lifecycle management.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "renderer/SpriteHandle.h"
#include "renderer/ICursorLayer.h"
#include "renderer/backends/SWCursorLayer.h"

#include "renderer/RendererSoftware.h"
#include "renderer/backends/SoftwareWorldViewRenderer.h"
#include "renderer/backends/SoftwareMapFadePass.h"
#include "renderer/backends/SoftwareTextRenderer.h"
#include "renderer/backends/SoftwareUIRenderer.h"
#ifdef RENDERER_OPENGL_ENABLED
#  include "renderer/RendererOpenGL.h"
#  include "renderer/opengl/GLTileAtlas.h"
#  include "renderer/opengl/GLSpriteAtlas.h"
#  include "renderer/opengl/GLWorldViewRenderer.h"
#  include "renderer/opengl/GLMapFadePass.h"
#  include "renderer/opengl/GLTextRenderer.h"
#  include "renderer/opengl/GLUIRenderer.h"
#  include "renderer/opengl/GLCursorLayer.h"
#endif
#ifdef PLATFORM_VITA
#  include "renderer/RendererVita.h"
#endif
#ifdef PLATFORM_3DS
#  include "renderer/Renderer3DS.h"
#endif

#include "bflib_basics.h"
#include "bflib_datetm.h"
#include "globals.h"
#include "bflib_video.h"
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
extern "C" { struct TbSpriteSheet; extern struct TbSpriteSheet *button_sprites; extern struct TbSpriteSheet *custom_sprites; extern struct TbSpriteSheet *pointer_sprites; extern struct TbSpriteSheet *map_flag; }
#include "renderer/RenderPass_C.h"
#include <unordered_map>
#include "post_inc.h"

/******************************************************************************/

static IRenderer*           s_activeRenderer      = nullptr;
static RendererType         s_activeType          = RENDERER_INVALID;
static IWorldViewRenderer*  s_worldViewRenderer   = nullptr;
static IMapFadePass*        s_mapFadePass         = nullptr;
static ITextRenderer*       s_textRenderer        = nullptr;
static IUIRenderer*         s_uiRenderer          = nullptr;
static SoftwareUIRenderer*  s_softwareUIRenderer  = nullptr;  // non-null only in software mode
static ICursorLayer*        s_cursorLayer         = nullptr;
#ifdef RENDERER_OPENGL_ENABLED
static GLSpriteAtlas*       s_spriteAtlas         = nullptr;  // non-null only in GL mode
#endif
// Software-mode sprite handle registry (GL mode uses s_spriteAtlas->GetHandle instead)
static std::unordered_map<const TbSprite*, SpriteHandle> s_sprite_to_handle;
static uint32_t             s_software_next_handle = 0;

/******************************************************************************/

void RendererNotifyTexturesReloaded()
{
#ifdef RENDERER_OPENGL_ENABLED
    if (s_activeType == RENDERER_OPENGL)
    {
        RendererOpenGL* ogl = dynamic_cast<RendererOpenGL*>(s_activeRenderer);
        if (ogl)
            ogl->InvalidateTileAtlas();
    }
#endif
}

void RendererNotifySpritesReloaded()
{
#ifdef RENDERER_OPENGL_ENABLED
    SYNCLOG("RendererNotifySpritesReloaded: s_spriteAtlas=%p gui_panel_sprites=%p button_sprites=%p",
            (void*)s_spriteAtlas, (void*)gui_panel_sprites, (void*)button_sprites);
    if (s_spriteAtlas) {
        s_spriteAtlas->Free();
        if (s_spriteAtlas->Init()) {
            if (gui_panel_sprites) {
                int32_t n = num_sprites(gui_panel_sprites);
                s_spriteAtlas->AddSheet(gui_panel_sprites);
                SYNCLOG("RendererNotifySpritesReloaded: added gui_panel_sprites (%d sprites)", n);
            } else {
                SYNCLOG("RendererNotifySpritesReloaded: gui_panel_sprites is NULL - no panel sprites added!");
            }
            if (button_sprites) {
                int32_t n = num_sprites(button_sprites);
                s_spriteAtlas->AddSheet(button_sprites);
                SYNCLOG("RendererNotifySpritesReloaded: added button_sprites (%d sprites)", n);
            } else {
                SYNCLOG("RendererNotifySpritesReloaded: button_sprites is NULL");
            }
            if (custom_sprites && num_sprites(custom_sprites) > 0) {
                int32_t n = num_sprites(custom_sprites);
                s_spriteAtlas->AddSheet(custom_sprites);
                SYNCLOG("RendererNotifySpritesReloaded: added custom_sprites (%d sprites)", n);
            }
        } else {
            ERRORLOG("RendererNotifySpritesReloaded: GLSpriteAtlas::Init() FAILED");
        }
    } else {
        SYNCLOG("RendererNotifySpritesReloaded: s_spriteAtlas is NULL (GL not active or not yet initialised)");
    }
    // Upload the slab background tile now that the palette is ready.
    UIRenderer_SetSlabTexture();
#endif
}

/** Append pointer_sprites into the live atlas after load_pointer_file(). */
void RendererNotifyPointerSpritesLoaded()
{
#ifdef RENDERER_OPENGL_ENABLED
    if (s_spriteAtlas && pointer_sprites && num_sprites(pointer_sprites) > 0) {
        int32_t before = (int32_t)s_spriteAtlas->GetRegisteredCount();
        s_spriteAtlas->AddSheet(pointer_sprites);
        int32_t after  = (int32_t)s_spriteAtlas->GetRegisteredCount();
        SYNCLOG("RendererNotifyPointerSpritesLoaded: pointer_sprites=%p added %d new sprites (total %d)",
                (void*)pointer_sprites, after - before, after);
    }
#endif
}

/** Append frontend_sprite into the live atlas after frontend_load_data(). */
void RendererNotifyFrontendSpritesLoaded()
{
#ifdef RENDERER_OPENGL_ENABLED
    if (s_spriteAtlas && frontend_sprite && num_sprites(frontend_sprite) > 0) {
        int32_t before = (int32_t)s_spriteAtlas->GetRegisteredCount();
        s_spriteAtlas->AddSheet(frontend_sprite);
        int32_t after  = (int32_t)s_spriteAtlas->GetRegisteredCount();
        SYNCLOG("RendererNotifyFrontendSpritesLoaded: frontend_sprite=%p added %d new sprites (total %d)",
                (void*)frontend_sprite, after - before, after);
    }
#endif
}

static void register_sheet_software(const struct TbSpriteSheet* sheet); // fwd

/** Append map_flag into the live atlas after load_spritesheet() in front_landview.c.
 *  Handles both GL (GLSpriteAtlas) and software (IUIRenderer handle table) modes. */
void RendererNotifyLandviewFlagLoaded()
{
#ifdef RENDERER_OPENGL_ENABLED
    if (s_spriteAtlas && map_flag && num_sprites(map_flag) > 0) {
        int32_t before = (int32_t)s_spriteAtlas->GetRegisteredCount();
        s_spriteAtlas->AddSheet(map_flag);
        int32_t after  = (int32_t)s_spriteAtlas->GetRegisteredCount();
        SYNCLOG("RendererNotifyLandviewFlagLoaded: map_flag=%p added %d new sprites (total %d)",
                (void*)map_flag, after - before, after);
    }
#endif
    // SW mode: register into the IUIRenderer handle table so SubmitPanelSprite
    // falls back to LbSpriteDrawResized correctly.
    register_sheet_software(map_flag);
}

void RendererNotifyGameTablesReady()
{
#ifdef RENDERER_OPENGL_ENABLED
    if (s_activeType == RENDERER_OPENGL)
    {
        RendererOpenGL* ogl = dynamic_cast<RendererOpenGL*>(s_activeRenderer);
        if (ogl)
        {
            ogl->init_fade_table_texture();
            GLUIRenderer* glui = dynamic_cast<GLUIRenderer*>(RendererGetUIRenderer());
            if (glui)
            {
                glui->SetFadeTexture(ogl->GetFadeTex());
                glui->SetPaletteSource(lbPalette);  // palette is fully loaded by the time game tables are ready
            }
            GLWorldViewRenderer* glwr = dynamic_cast<GLWorldViewRenderer*>(s_worldViewRenderer);
            if (glwr)
            {
                glwr->SetFadeTexture(ogl->GetFadeTex());
                glwr->SetPaletteSource(lbPalette);  // palette is fully loaded by the time game tables are ready
            }
        }
    }
#endif
}

/** Explicitly composite m_stagingBuf (lbDisplay.WScreen) on the GL frame. */
TbBool RendererSubmitStagingOverlay()
{
    IRenderer* rend = RendererGetActive();
    if (!rend) return false;
    return rend->SubmitStagingOverlay() ? true : false;
}

TbBool RendererSubmitTransparentBlit(const unsigned char* buf, int w, int h)
{
    IRenderer* rend = RendererGetActive();
    if (!rend) return false;
    return rend->SubmitTransparentBlit(buf, w, h) ? true : false;
}

void RendererDrawSwipeOverlay(struct TbSpriteSheet* sprites, int frame,
                              int draw_lr, int engine_window_x)
{
    IRenderer* rend = RendererGetActive();
    if (!rend) return;
    rend->DrawSwipeOverlay(sprites, frame, draw_lr != 0, engine_window_x);
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
#ifdef RENDERER_OPENGL_ENABLED
    RendererOpenGL* rend = dynamic_cast<RendererOpenGL*>(RendererGetActive());
    if (rend)
        rend->SubmitZoomBoxTiles((const uint16_t*)tile_block_ids, tiles_x, tiles_y,
                                  dst_x, dst_y, tile_w, tile_h);
#endif
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

void RendererSchedulePiPRender(struct Camera* cam, int x, int y, int w, int h)
{
#ifdef RENDERER_OPENGL_ENABLED
    RendererOpenGL* rend = dynamic_cast<RendererOpenGL*>(RendererGetActive());
    if (rend)
        rend->SubmitPiPRender(cam, x, y, w, h);
#else
    (void)cam; (void)x; (void)y; (void)w; (void)h;
#endif
}

/** Append any newly-built custom_sprites into the live atlas.
 *  Call this after every init_custom_sprites() so per-level icons are available. */
void RendererNotifyCustomSpritesReloaded()
{
#ifdef RENDERER_OPENGL_ENABLED
    if (s_spriteAtlas && custom_sprites && num_sprites(custom_sprites) > 0) {
        int32_t before = (int32_t)s_spriteAtlas->GetRegisteredCount();
        s_spriteAtlas->AddSheet(custom_sprites);
        int32_t after  = (int32_t)s_spriteAtlas->GetRegisteredCount();
        SYNCLOG("RendererNotifyCustomSpritesReloaded: custom_sprites=%p added %d new sprites (total %d)",
                (void*)custom_sprites, after - before, after);
    }
#endif
}

/******************************************************************************/

/** Resolve a TbSprite pointer to its registered SpriteHandle.
 *  In GL mode: queries the sprite atlas.  In software mode: queries the
 *  RendererManager-owned handle table populated by register_sheet_software(). */
static SpriteHandle resolve_sprite_handle(const TbSprite* spr)
{
    if (!spr) return kInvalidSpriteHandle;
#ifdef RENDERER_OPENGL_ENABLED
    if (s_spriteAtlas) {
        SpriteHandle h = s_spriteAtlas->GetHandle(spr);
        if (h == kInvalidSpriteHandle) {
            static int s_miss_count = 0;
            if (s_miss_count < 5) {
                SYNCLOG("resolve_sprite_handle: spr %p not in atlas (miss #%d, atlas size=%u)",
                        (void*)spr, ++s_miss_count,
                        (unsigned)s_spriteAtlas->GetRegisteredCount());
            }
        }
        return h;
    }
#endif
    auto it = s_sprite_to_handle.find(spr);
    return (it != s_sprite_to_handle.end()) ? it->second : kInvalidSpriteHandle;
}

SpriteHandle RendererResolveSprite(const TbSprite* spr)
{
    return resolve_sprite_handle(spr);
}

/** Register all valid sprites in a sheet into the software-mode handle table.
 *  Calls SoftwareUIRenderer::RegisterSpriteHandle so the renderer can reverse-
 *  resolve handles back to TbSprite* for CPU blitting. */
static void register_sheet_software(const struct TbSpriteSheet* sheet)
{
    if (!sheet || !s_softwareUIRenderer) return;
    int32_t n = num_sprites(sheet);
    for (int32_t i = 0; i < n; ++i) {
        const struct TbSprite* spr = get_sprite(sheet, i);
        if (!spr || !spr->Data || spr->SWidth == 0 || spr->SHeight == 0) continue;
        if (s_sprite_to_handle.count(spr)) continue;
        SpriteHandle h = s_software_next_handle++;
        s_sprite_to_handle[spr] = h;
        s_softwareUIRenderer->RegisterSpriteHandle(h, spr);
    }
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

#ifdef PLATFORM_3DS
        case RENDERER_3DS:
            return new Renderer3DS();
#endif

        default:
            return nullptr;
    }
}

/** Allocates the appropriate IWorldViewRenderer for the given renderer type.
 *  OpenGL uses GLWorldViewRenderer (GPU geometry); all others use software. */
static IWorldViewRenderer* create_world_view_renderer(RendererType type)
{
#ifdef RENDERER_OPENGL_ENABLED
    if (type == RENDERER_OPENGL)
    {
        // Inject shared GPU resources from the already-initialised RendererOpenGL
        RendererOpenGL* ogl = dynamic_cast<RendererOpenGL*>(s_activeRenderer);
        if (ogl)
        {
            auto* glwr = new GLWorldViewRenderer(
                ogl->GetTileAtlas(),
                (GLuint)ogl->GetFadeTex(),
                (GLuint)ogl->GetPaletteTex());
            glwr->SetPaletteSource(lbPalette);  // address is always valid; contents may be zeroed until LbPaletteSet
            ogl->SetWorldRenderer(glwr);
            return glwr;
        }
        WARNLOG("RendererManager: GLWorldViewRenderer requested but no RendererOpenGL active");
    }
#endif
    (void)type;
    return new SoftwareWorldViewRenderer();
}

/** Allocates the appropriate IMapFadePass for the given renderer type. */
static IMapFadePass* create_map_fade_pass(RendererType type)
{
#ifdef RENDERER_OPENGL_ENABLED
    if (type == RENDERER_OPENGL)
    {
        auto* glmf = new GLMapFadePass();
        glmf->SetScreenSize(lbDisplay.PhysicalScreenWidth, lbDisplay.PhysicalScreenHeight);
        return glmf;
    }
#endif
    (void)type;
    return new SoftwareMapFadePass();
}

/** Allocates the appropriate ITextRenderer for the given renderer type. */
static ITextRenderer* create_text_renderer(RendererType type)
{
#ifdef RENDERER_OPENGL_ENABLED
    if (type == RENDERER_OPENGL)
    {
        RendererOpenGL* ogl = dynamic_cast<RendererOpenGL*>(s_activeRenderer);
        if (ogl)
        {
            auto* glt = new GLTextRenderer();
            if (glt->Init())
            {
                glt->SetPaletteTexture(ogl->GetPaletteTex());
                glt->SetScreenSize(lbDisplay.PhysicalScreenWidth, lbDisplay.PhysicalScreenHeight);
                return glt;
            }
            WARNLOG("GLTextRenderer::Init() failed, falling back to software");
            delete glt;
        }
    }
#endif
    (void)type;
    return new SoftwareTextRenderer();
}

/** Allocates the appropriate ICursorLayer for the given renderer type. */
static ICursorLayer* create_cursor_layer(RendererType type)
{
#ifdef RENDERER_OPENGL_ENABLED
    if (type == RENDERER_OPENGL)
    {
        RendererOpenGL* ogl = dynamic_cast<RendererOpenGL*>(s_activeRenderer);
        if (ogl)
        {
            auto* glcur = new GLCursorLayer();
            glcur->SetWorldViewRenderer(dynamic_cast<GLWorldViewRenderer*>(s_worldViewRenderer));
            glcur->SetSpriteAtlas(s_spriteAtlas);
            glcur->SetGLUIRenderer(dynamic_cast<GLUIRenderer*>(s_uiRenderer));
            return glcur;
        }
    }
#endif
    (void)type;
    return new SWCursorLayer();
}

/** Allocates the appropriate IUIRenderer for the given renderer type.
 *  OpenGL uses GLUIRenderer (GPU-accelerated UI elements); all others use software no-ops. */
static IUIRenderer* create_ui_renderer(RendererType type)
{
#ifdef RENDERER_OPENGL_ENABLED
    if (type == RENDERER_OPENGL)
    {
        RendererOpenGL* ogl = dynamic_cast<RendererOpenGL*>(s_activeRenderer);
        if (ogl)
        {
            auto* glui = new GLUIRenderer();
            if (!glui->Init())
            {
                WARNLOG("GLUIRenderer::Init() failed, falling back to software");
                delete glui;
                return new SoftwareUIRenderer();
            }
            glui->SetSpriteAtlas(ogl->GetSpriteAtlas());
            glui->SetFontAtlas(ogl->GetFontAtlas());
            glui->SetPaletteTexture(ogl->GetPaletteTex(), GL_TEXTURE_2D);
            glui->SetPaletteSource(lbPalette);  // address is always valid; contents may be zeroed until LbPaletteSet
            // Fade texture set later by RendererNotifyGameTablesReady() after
            // render_fade_tables is loaded in setup_stuff().
            glui->SetScreenDimensions(lbDisplay.PhysicalScreenWidth, lbDisplay.PhysicalScreenHeight);
            s_spriteAtlas = ogl->GetSpriteAtlas();
            if (s_spriteAtlas) {
                if (gui_panel_sprites) s_spriteAtlas->AddSheet(gui_panel_sprites);
                if (button_sprites)    s_spriteAtlas->AddSheet(button_sprites);
            }
            s_softwareUIRenderer = nullptr;
            return glui;
        }
        WARNLOG("RendererManager: GLUIRenderer requested but no RendererOpenGL active");
    }
#endif
    (void)type;
    auto* swui = new SoftwareUIRenderer();
    s_spriteAtlas = nullptr;
    s_softwareUIRenderer = swui;
    register_sheet_software(gui_panel_sprites);
    register_sheet_software(button_sprites);
    return swui;
}

/** Resolve RENDERER_AUTO to a concrete type.
 *  Prefers OpenGL when available, falls back to software. */
static RendererType resolve_auto()
{
#ifdef PLATFORM_VITA
    return RENDERER_VITA;
#elif defined(PLATFORM_3DS)
    return RENDERER_3DS;
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

    s_worldViewRenderer = create_world_view_renderer(type);
    SYNCLOG("WorldViewRenderer initialised: %s", s_worldViewRenderer->GetName());

    s_mapFadePass = create_map_fade_pass(type);
    SYNCLOG("MapFadePass initialised: %s", s_mapFadePass->GetName());

    s_textRenderer = create_text_renderer(type);
    SYNCLOG("TextRenderer initialised: %s", s_textRenderer->GetName());

    s_uiRenderer = create_ui_renderer(type);
    SYNCLOG("UIRenderer initialised: %s", s_uiRenderer->GetName());

    s_cursorLayer = create_cursor_layer(type);
    SYNCLOG("CursorLayer initialised: %s", s_cursorLayer->GetName());

    // Wire sprite intercept backend: Vita uses GPU batch; OpenGL uses software
    // (software backend exercises the full intercept path for testing on desktop).
#if defined(PLATFORM_VITA)
    if (type == RENDERER_VITA)
        RenderPass_Initialize(1); // BACKEND_GPU_VITA
#elif defined(RENDERER_OPENGL_ENABLED)
    if (type == RENDERER_OPENGL)
        RenderPass_Initialize(3); // BACKEND_OPENGL
#endif
    if (g_render_pass_active)
        SYNCLOG("SpriteBackend initialised: %s", RenderPass_GetBackendName());

    return true;
}

int RendererSwitch(RendererType type)
{
    if (type == RENDERER_AUTO)
        type = resolve_auto();

    if (type == s_activeType)
        return true; // already active

    IRenderer* next = create_renderer(type);
    if (!next)
    {
        ERRORLOG("RendererSwitch: unknown or unsupported renderer type %d", (int)type);
        return false;
    }

    if (!next->SupportsRuntimeSwitch())
    {
        ERRORLOG("RendererSwitch: backend '%s' does not support runtime switching", next->GetName());
        delete next;
        return false;
    }

    // Tear down current backend
    if (s_activeRenderer)
    {
        s_activeRenderer->Shutdown();
        delete s_activeRenderer;
        s_activeRenderer = nullptr;
    }

    // Bring up new backend
    if (!next->Init())
    {
        ERRORLOG("RendererSwitch: backend '%s' failed to initialise — falling back to software", next->GetName());
        delete next;
        // Fallback to software renderer
        next = new RendererSoftware();
        if (!next->Init())
        {
            ERRORLOG("RendererSwitch: software fallback also failed");
            delete next;
            return false;
        }
        type = RENDERER_SOFTWARE;
    }

    s_activeRenderer = next;
    s_activeType     = type;
    SYNCLOG("Renderer switched to: %s", next->GetName());
    return true;
}

void RendererShutdown()
{
    RenderPass_Shutdown();
    if (s_cursorLayer)
    {
        delete s_cursorLayer;
        s_cursorLayer = nullptr;
    }
    if (s_uiRenderer)
    {
        delete s_uiRenderer;
        s_uiRenderer = nullptr;
        s_softwareUIRenderer = nullptr;  // owned by s_uiRenderer above
    }
#ifdef RENDERER_OPENGL_ENABLED
    s_spriteAtlas = nullptr;  // owned by RendererOpenGL
#endif
    s_sprite_to_handle.clear();
    s_software_next_handle = 0;
    if (s_textRenderer)
    {
        delete s_textRenderer;
        s_textRenderer = nullptr;
    }
    if (s_mapFadePass)
    {
        delete s_mapFadePass;
        s_mapFadePass = nullptr;
    }
    if (s_worldViewRenderer)
    {
        delete s_worldViewRenderer;
        s_worldViewRenderer = nullptr;
    }
    if (s_activeRenderer)
    {
        s_activeRenderer->Shutdown();
        delete s_activeRenderer;
        s_activeRenderer = nullptr;
    }
    s_activeType = RENDERER_INVALID;
}

IRenderer* RendererGetActive()
{
    return s_activeRenderer;
}

IWorldViewRenderer* RendererGetWorldViewRenderer()
{
    return s_worldViewRenderer;
}

IMapFadePass* RendererGetMapFadePass()
{
    return s_mapFadePass;
}

ITextRenderer* RendererGetTextRenderer()
{
    return s_textRenderer;
}

IUIRenderer* RendererGetUIRenderer()
{
    return s_uiRenderer;
}

RendererType RendererGetActiveType()
{
    return s_activeType;
}

TbBool RendererWantsFullscreenViewport()
{
    return (s_activeType == RENDERER_OPENGL || s_activeType == RENDERER_VITA) ? 1 : 0;
}

TbBool RendererHasGPURenderPath()
{
    return (s_activeType == RENDERER_OPENGL || s_activeType == RENDERER_VITA) ? 1 : 0;
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
    return s_activeRenderer->BeginFrame() ? 1 : 0;
}

void RendererEndFrame(void)
{
    if (s_activeRenderer)
        s_activeRenderer->EndFrame();
}

void RendererClearScreen(unsigned char colour_index)
{
    if (s_activeRenderer)
        s_activeRenderer->ClearScreen(colour_index);
}

/******************************************************************************/
/* High-level screen lifecycle (replaces LbScreen* trampolines)               */
/******************************************************************************/

int RendererLockScreen(void)
{
    if (!RendererBeginFrame())
        return 0;
    int pitch = 0;
    unsigned char *pixels = RendererLockFramebuffer(&pitch);
    if (!pixels) {
        lbDisplay.GraphicsWindowPtr = NULL;
        lbDisplay.WScreen = NULL;
        return 0;
    }
    lbDisplay.WScreen = pixels;
    lbDisplay.GraphicsScreenWidth = pitch;
    lbDisplay.GraphicsWindowPtr = &lbDisplay.WScreen[lbDisplay.GraphicsWindowX +
        lbDisplay.GraphicsScreenWidth * lbDisplay.GraphicsWindowY];
    return 1;
}

void RendererUnlockScreen(void)
{
    lbDisplay.WScreen = NULL;
    lbDisplay.GraphicsWindowPtr = NULL;
    RendererUnlockFramebuffer();
}

void RendererPresentFrame(void)
{
    PlatformManager_FrameTick();
    // Ensure BeginFrame() has run — many call sites (fade loops, screen-mode
    // transitions, draw_clear_screen) do ClearScreen+PresentFrame without a
    // preceding LockScreen/BeginFrame.  BeginFrame() is idempotent, so this
    // is a no-op on the normal path where LockScreen was already called.
    RendererBeginFrame();
    TbResult ret = LbMouseOnBeginSwap();
    if (ret == Lb_SUCCESS) {
#if defined(VITA_PERF_LOG)
        {
            static TbClockMSec _ef_accum = 0;
            static int    _ef_cnt   = 0;
            TbClockMSec _ef_t0 = LbTimerClock();
            RendererEndFrame();
            _ef_accum += LbTimerClock() - _ef_t0;
            if (++_ef_cnt >= 60) {
                JUSTLOG("[perf] EndFrame   avg %d ms/frame (60-frame window)", (int)(_ef_accum / 60));
                _ef_accum = 0; _ef_cnt = 0;
            }
        }
#else
        RendererEndFrame();
#endif
    }
    LbMouseOnEndSwap();
}

int RendererIsScreenLocked(void)
{
    return (lbDisplay.WScreen != NULL) ? 1 : 0;
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
    if (x > lbDisplay.GraphicsScreenWidth) x = lbDisplay.GraphicsScreenWidth;
    if (right_edge > lbDisplay.GraphicsScreenWidth) right_edge = lbDisplay.GraphicsScreenWidth;
    if (y > lbDisplay.GraphicsScreenHeight) y = lbDisplay.GraphicsScreenHeight;
    if (bottom_edge > lbDisplay.GraphicsScreenHeight) bottom_edge = lbDisplay.GraphicsScreenHeight;
    lbDisplay.GraphicsWindowX = x;
    lbDisplay.GraphicsWindowY = y;
    lbDisplay.GraphicsWindowWidth = right_edge - x;
    lbDisplay.GraphicsWindowHeight = bottom_edge - y;
    if (lbDisplay.WScreen != NULL)
        lbDisplay.GraphicsWindowPtr = lbDisplay.WScreen + lbDisplay.GraphicsScreenWidth * y + x;
    else
        lbDisplay.GraphicsWindowPtr = NULL;
}

void RendererStoreViewport(TbGraphicsWindow *grwnd)
{
    grwnd->x = lbDisplay.GraphicsWindowX;
    grwnd->y = lbDisplay.GraphicsWindowY;
    grwnd->width = lbDisplay.GraphicsWindowWidth;
    grwnd->height = lbDisplay.GraphicsWindowHeight;
    grwnd->ptr = NULL;
}

void RendererLoadViewport(TbGraphicsWindow *grwnd)
{
    lbDisplay.GraphicsWindowX = grwnd->x;
    lbDisplay.GraphicsWindowY = grwnd->y;
    lbDisplay.GraphicsWindowWidth = grwnd->width;
    lbDisplay.GraphicsWindowHeight = grwnd->height;
    if (lbDisplay.WScreen != NULL)
        lbDisplay.GraphicsWindowPtr = lbDisplay.WScreen
            + lbDisplay.GraphicsScreenWidth * lbDisplay.GraphicsWindowY + lbDisplay.GraphicsWindowX;
    else
        lbDisplay.GraphicsWindowPtr = NULL;
}

/******************************************************************************/
/* Display property accessors                                                 */
/******************************************************************************/

TbScreenCoord RendererPhysicalWidth(void)  { return lbDisplay.PhysicalScreenWidth;  }
TbScreenCoord RendererPhysicalHeight(void) { return lbDisplay.PhysicalScreenHeight; }
TbScreenCoord RendererScreenWidth(void)    { return lbDisplay.GraphicsScreenWidth;  }
TbScreenCoord RendererScreenHeight(void)   { return lbDisplay.GraphicsScreenHeight; }
unsigned char* RendererGetWScreen(void)    { return lbDisplay.WScreen; }

void RendererSetWScreen(unsigned char* buf)
{
    lbDisplay.WScreen = buf;
}

void RendererSetScreenDimensions(int width, int height)
{
    lbDisplay.GraphicsScreenWidth  = width;
    lbDisplay.GraphicsScreenHeight = height;
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

/******************************************************************************/
/* Screen lifecycle helpers                                                   */
/******************************************************************************/

extern "C" TbResult LbScreenInitialize(void);
extern "C" TbResult LbScreenSetDoubleBuffering(TbBool state);
extern "C" TbResult LbSetTitle(const char *title);
extern "C" TbResult LbSetIcon(unsigned short nicon);
extern "C" TbScreenMode LbScreenActiveMode(void);
extern "C" TbResult LbScreenWaitVbi(void);

TbResult RendererScreenInitialize(void)         { return LbScreenInitialize(); }
TbResult RendererSetDoubleBuffering(TbBool state){ return LbScreenSetDoubleBuffering(state); }
TbResult RendererSetTitle(const char *title)     { return LbSetTitle(title); }
TbResult RendererSetIcon(unsigned short nicon)   { return LbSetIcon(nicon); }
TbScreenMode RendererActiveMode(void)            { return LbScreenActiveMode(); }
TbResult RendererWaitVbi(void)                   { return LbScreenWaitVbi(); }

/******************************************************************************/
/* C-callable world-view renderer wrappers */
/******************************************************************************/

void WorldViewRenderer_BeginWorldPass(unsigned char* framebuf, int pitch, int w, int h,
                                      int vp_x, int vp_y)
{
    if (s_worldViewRenderer)
        s_worldViewRenderer->BeginWorldPass(framebuf, pitch, w, h, vp_x, vp_y);
}

void WorldViewRenderer_DrawIsometricView(void)
{
    if (s_worldViewRenderer)
        s_worldViewRenderer->DrawIsometricView();
}

void WorldViewRenderer_DrawFrontView(struct Camera* cam)
{
    if (s_worldViewRenderer)
        s_worldViewRenderer->DrawFrontView(cam);
}

int WorldViewRenderer_SubmitKeeperSprite(int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
                                         const unsigned char* data, int src_w, int src_h,
                                         unsigned int draw_flags, const unsigned char* remap)
{
    if (s_worldViewRenderer)
        return s_worldViewRenderer->SubmitKeeperSprite(dst_x, dst_y, dst_w, dst_h,
                                                       data, src_w, src_h, draw_flags, remap);
    return 0;
}

void WorldViewRenderer_ClearKeeperSpriteAtlas(void)
{
#ifdef RENDERER_OPENGL_ENABLED
    if (s_worldViewRenderer) {
        auto* gl = dynamic_cast<GLWorldViewRenderer*>(s_worldViewRenderer);
        if (gl) gl->ClearKeeperSpriteAtlas();
    }
#endif
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
    if (s_cursorLayer)
        s_cursorLayer->Draw();
}

void CursorLayer_Clear(void)
{
    if (s_cursorLayer)
        s_cursorLayer->Clear();
}

void CursorLayer_SubmitPointerSprite(const struct TbSprite* spr, int32_t x, int32_t y, int units_per_px)
{
    if (s_cursorLayer)
        s_cursorLayer->SubmitPointerSprite(spr, x, y, units_per_px);
}

void CursorLayer_SubmitKeeperHandSprite(short x, short y, unsigned short kspr_base,
                                        short kspr_angle, unsigned char sprgroup, int32_t scale)
{
    if (s_cursorLayer)
        s_cursorLayer->SubmitKeeperHandSprite(x, y, kspr_base, kspr_angle, sprgroup, scale);
}
/******************************************************************************/

int32_t MapFadePass_StepFadeIn(int32_t step)
{
    if (s_mapFadePass)
        return s_mapFadePass->StepFadeIn(step);
    return step; // no-op: don't advance if not initialised
}

int32_t MapFadePass_StepFadeOut(int32_t step)
{
    if (s_mapFadePass)
        return s_mapFadePass->StepFadeOut(step);
    return step;
}

TbBool MapFadePass_SupportsNativeResolution(void)
{
    if (s_mapFadePass)
        return s_mapFadePass->SupportsNativeResolution() ? 1 : 0;
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

TbBool RendererBlitRaw8(int dst_width, int dst_height, int dst_x, int dst_y,
                        const unsigned char* src_buf, int src_width, int src_height)
{
    // Prefer the GPU path: in OpenGL mode BlitRaw8GPU queues an opaque palette-decoded
    // quad so the source image bypasses the staging buffer entirely.
    // No CPU fallback is permitted when the GPU path is active — returning false
    // from BlitRaw8GPU in GL mode is a fatal misconfiguration (ERRORLOG inside).
    IRenderer* rend = RendererGetActive();
    if (rend && rend->BlitRaw8GPU(dst_width, dst_height, dst_x, dst_y,
                                   src_buf, src_width, src_height))
        return true;

    // Software renderer (or GPU path unavailable): write to the CPU framebuffer.
    return copy_raw8_image_buffer(
        lbDisplay.WScreen,
        RendererScreenWidth(), RendererScreenHeight(),
        dst_width, dst_height, dst_x, dst_y,
        src_buf, src_width, src_height);
}

TbBool RendererSubmitVideoFrame(const unsigned char* px, int src_w, int src_h, int src_pitch,
                                const unsigned char* bgra_pal,
                                int dst_x, int dst_y, int dst_w, int dst_h)
{
    IRenderer* rend = RendererGetActive();
    if (!rend) return false;
    return rend->SubmitVideoFrame(px, src_w, src_h, src_pitch, bgra_pal,
                                  dst_x, dst_y, dst_w, dst_h) ? true : false;
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

/******************************************************************************/
/* C-callable UI renderer wrappers (stubs only — canonical wrappers live in   */
/* RendererBridge_UI.cpp)                                                     */
/******************************************************************************/

void UIRenderer_DrawHandSprites(void)
{
    // Legacy stub — kept so existing callers compile until fully purged.
    // Real draw now happens via CursorLayer_Draw().
}

void UIRenderer_SubmitKeeperSprite(short x, short y, unsigned short kspr_base,
                                   short kspr_angle, unsigned char sprgroup, int32_t scale)
{
    // Legacy stub — calls route to CursorLayer_SubmitKeeperHandSprite now.
    CursorLayer_SubmitKeeperHandSprite(x, y, kspr_base, kspr_angle, sprgroup, scale);
}

void RendererApplySettings(const RendererSettings* s)
{
    if (!s) return;

    int prev_palette_mode = g_renderer_settings.palette_mode;
    g_renderer_settings = *s;

    // If the atlas colour format changed, rebuild the sprite atlas immediately
    // so the new format takes effect without a restart.
    if (g_renderer_settings.palette_mode != prev_palette_mode) {
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

void RendererPreserveFadeCache(int active)
{
    g_fade_cache_preserve = active;
}

int RendererIsFadeCachePreserved(void)
{
    return g_fade_cache_preserve;
}
