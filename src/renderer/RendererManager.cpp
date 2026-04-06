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
#  include "renderer/opengl/GLTextRenderer.h"
#  include "renderer/opengl/GLUIRenderer.h"
#endif
#ifdef PLATFORM_VITA
#  include "renderer/RendererVita.h"
#endif
#ifdef PLATFORM_3DS
#  include "renderer/Renderer3DS.h"
#endif

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"      // LbSpriteDrawResized
#include "bflib_sprite.h"      // TbSprite
#include "engine_render.h"     // thing_being_displayed
#include "gui_draw.h"          // get_panel_sprite
#include "config_spritecolors.h" // get_player_colored_icon_idx
#include "custom_sprites.h"    // get_button_sprite_for_player
#include "player_data.h"       // my_player_number
// Forward declaration to avoid pulling in frontend.h (conflicts with C++ stdlib)
extern "C" { struct TbSpriteSheet; extern struct TbSpriteSheet *button_sprites; extern struct TbSpriteSheet *custom_sprites; }
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
                long n = num_sprites(gui_panel_sprites);
                s_spriteAtlas->AddSheet(gui_panel_sprites);
                SYNCLOG("RendererNotifySpritesReloaded: added gui_panel_sprites (%ld sprites)", n);
            } else {
                SYNCLOG("RendererNotifySpritesReloaded: gui_panel_sprites is NULL - no panel sprites added!");
            }
            if (button_sprites) {
                long n = num_sprites(button_sprites);
                s_spriteAtlas->AddSheet(button_sprites);
                SYNCLOG("RendererNotifySpritesReloaded: added button_sprites (%ld sprites)", n);
            } else {
                SYNCLOG("RendererNotifySpritesReloaded: button_sprites is NULL");
            }
            if (custom_sprites && num_sprites(custom_sprites) > 0) {
                long n = num_sprites(custom_sprites);
                s_spriteAtlas->AddSheet(custom_sprites);
                SYNCLOG("RendererNotifySpritesReloaded: added custom_sprites (%ld sprites)", n);
            }
        } else {
            ERRORLOG("RendererNotifySpritesReloaded: GLSpriteAtlas::Init() FAILED");
        }
    } else {
        SYNCLOG("RendererNotifySpritesReloaded: s_spriteAtlas is NULL (GL not active or not yet initialised)");
    }
#endif
}

/** Append any newly-built custom_sprites into the live atlas.
 *  Call this after every init_custom_sprites() so per-level icons are available. */
void RendererNotifyCustomSpritesReloaded()
{
#ifdef RENDERER_OPENGL_ENABLED
    if (s_spriteAtlas && custom_sprites && num_sprites(custom_sprites) > 0) {
        long before = (long)s_spriteAtlas->GetRegisteredCount();
        s_spriteAtlas->AddSheet(custom_sprites);
        long after  = (long)s_spriteAtlas->GetRegisteredCount();
        SYNCLOG("RendererNotifyCustomSpritesReloaded: custom_sprites=%p added %ld new sprites (total %ld)",
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

/** Register all valid sprites in a sheet into the software-mode handle table.
 *  Calls SoftwareUIRenderer::RegisterSpriteHandle so the renderer can reverse-
 *  resolve handles back to TbSprite* for CPU blitting. */
static void register_sheet_software(const struct TbSpriteSheet* sheet)
{
    if (!sheet || !s_softwareUIRenderer) return;
    long n = num_sprites(sheet);
    for (long i = 0; i < n; ++i) {
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
    (void)type; // reserved for future GPU dispatch
    return new SoftwareMapFadePass();
}

/** Allocates the appropriate ITextRenderer for the given renderer type. */
static ITextRenderer* create_text_renderer(RendererType type)
{
    // GLTextRenderer is currently disabled — always use software fallback.
    (void)type;
    return new SoftwareTextRenderer();
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
            glui->SetPaletteTexture(ogl->GetPaletteTex(), GL_TEXTURE_1D);
            glui->SetScreenDimensions(lbDisplay.PhysicalScreenWidth, lbDisplay.PhysicalScreenHeight);
            glui->SetWorldViewRenderer(dynamic_cast<GLWorldViewRenderer*>(s_worldViewRenderer));
            // Populate sprite atlas with currently-loaded panel sprite sheets.
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

/******************************************************************************/
/* C-callable world-view renderer wrappers */
/******************************************************************************/

void WorldViewRenderer_BeginWorldPass(unsigned char* framebuf, int pitch, int w, int h,
                                      int vp_x, int vp_y)
{
    if (s_worldViewRenderer)
        s_worldViewRenderer->BeginWorldPass(framebuf, pitch, w, h, vp_x, vp_y);
}

void WorldViewRenderer_FlushIsometricView(void)
{
    if (s_worldViewRenderer)
        s_worldViewRenderer->FlushIsometricView();
}

void WorldViewRenderer_FlushFrontView(struct Camera* cam)
{
    if (s_worldViewRenderer)
        s_worldViewRenderer->FlushFrontView(cam);
}

int WorldViewRenderer_SubmitKeeperSprite(long dst_x, long dst_y, long dst_w, long dst_h,
                                         const unsigned char* data, int src_w, int src_h,
                                         unsigned int draw_flags, const unsigned char* remap)
{
    if (s_worldViewRenderer)
        return s_worldViewRenderer->SubmitKeeperSprite(dst_x, dst_y, dst_w, dst_h,
                                                       data, src_w, src_h, draw_flags, remap);
    return 0;
}

/******************************************************************************/
/* C-callable map fade pass wrappers */
/******************************************************************************/

long MapFadePass_StepFadeIn(long step)
{
    if (s_mapFadePass)
        return s_mapFadePass->StepFadeIn(step);
    return step; // no-op: don't advance if not initialised
}

long MapFadePass_StepFadeOut(long step)
{
    if (s_mapFadePass)
        return s_mapFadePass->StepFadeOut(step);
    return step;
}

/******************************************************************************/
/* C-callable text renderer wrapper */
/******************************************************************************/

TbBool TextRenderer_DrawTextResized(int posx, int posy, int units_per_px, const char* text)
{
    if (s_textRenderer)
        return s_textRenderer->DrawTextResized(posx, posy, units_per_px, text);
    return false;
}

void TextRenderer_Flush(void)
{
    if (s_textRenderer)
        s_textRenderer->Flush();
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
    return copy_raw8_image_buffer(
        lbDisplay.WScreen,
        LbGraphicsScreenWidth(), LbGraphicsScreenHeight(),
        dst_width, dst_height, dst_x, dst_y,
        src_buf, src_width, src_height);
}

/******************************************************************************/
/* C-callable UI renderer wrappers                                            */
/******************************************************************************/

void UIRenderer_SubmitSlabSelector(int x1, int y1, int x2, int y2, unsigned char color, float z_depth)
{
    if (s_uiRenderer)
        s_uiRenderer->SubmitSlabSelector(x1, y1, x2, y2, color, z_depth);
}

void UIRenderer_SubmitKeeperSprite(short x, short y, unsigned short kspr_base,
                                   short kspr_angle, unsigned char sprgroup, long scale)
{
    if (s_uiRenderer)
        s_uiRenderer->SubmitKeeperSprite(x, y, kspr_base, kspr_angle, sprgroup, scale);
}

void UIRenderer_SubmitPanelSprite(long x, long y, int units_per_px, long spridx)
{
    if (!s_uiRenderer) return;
    const struct TbSprite* spr = get_panel_sprite(get_player_colored_icon_idx(spridx, my_player_number));
    SpriteHandle h = resolve_sprite_handle(spr);
    s_uiRenderer->SubmitPanelSprite(x, y, units_per_px, h);
}

void UIRenderer_SubmitPanelSpriteRaw(long x, long y, int units_per_px, const struct TbSprite* spr)
{
    if (!s_uiRenderer) return;
    SpriteHandle h = resolve_sprite_handle(spr);
    s_uiRenderer->SubmitPanelSprite(x, y, units_per_px, h);
}

void UIRenderer_SubmitPanelSpriteCentered(long x, long y, int units_per_px, long spridx)
{
    if (!s_uiRenderer) return;
    const struct TbSprite* spr = get_panel_sprite(get_player_colored_icon_idx(spridx, my_player_number));
    if (!spr) return;
    long ox = ((long)spr->SWidth  * units_per_px + 8) / 16 / 2;
    long oy = ((long)spr->SHeight * units_per_px + 8) / 16 / 2;
    SpriteHandle h = resolve_sprite_handle(spr);
    s_uiRenderer->SubmitPanelSprite(x - ox, y - oy, units_per_px, h);
}

void UIRenderer_SubmitButtonSprite(long x, long y, int units_per_px, short spridx)
{
    if (!s_uiRenderer) return;
    const struct TbSprite* spr = get_button_sprite_for_player(spridx, my_player_number);
    SpriteHandle h = resolve_sprite_handle(spr);
    s_uiRenderer->SubmitPanelSprite(x, y, units_per_px, h);
}

void UIRenderer_SubmitButtonSpriteFlipped(long x, long y, int units_per_px, short spridx)
{
    if (!s_uiRenderer) return;
    const struct TbSprite* spr = get_button_sprite_for_player(spridx, my_player_number);
    SpriteHandle h = resolve_sprite_handle(spr);
    s_uiRenderer->SubmitPanelSprite(x, y, units_per_px, h, true);
}

void UIRenderer_SubmitScaledSprite(long x, long y, long w, long h, const struct TbSprite *spr)
{
    if (s_uiRenderer) {
        SpriteHandle hspr = resolve_sprite_handle(spr);
        s_uiRenderer->SubmitScaledSprite(x, y, w, h, hspr);
    }
}

void UIRenderer_SubmitSolidBox(long x, long y, long w, long h, unsigned char color_idx)
{
    if (s_uiRenderer)
        s_uiRenderer->SubmitSolidBox(x, y, w, h, color_idx);
}

unsigned char* UIRenderer_AcquireMinimapBuffer(int size)
{
    if (s_uiRenderer)
        return s_uiRenderer->AcquireMinimapBuffer(size);
    return nullptr;
}

void UIRenderer_SubmitMinimap(int screen_x, int screen_y, int size)
{
    if (s_uiRenderer)
        s_uiRenderer->SubmitMinimap(screen_x, screen_y, size);
}

void UIRenderer_SubmitTiledSprite(long x, long y, int units_per_px, const struct TiledSprite* bigspr)
{
    if (!s_uiRenderer || !bigspr) return;
    long cur_y = y;
    for (int sy = 0; sy < bigspr->y_num; sy++)
    {
        long cur_x = x;
        long delta_y = 0;
        unsigned short spr_idx = bigspr->spr_idx[sy][0];
        for (int sx = 0; sx < bigspr->x_num; sx++)
        {
            const struct TbSprite* spr = get_panel_sprite(spr_idx);
            if (!spr) { spr_idx++; continue; }
            long delta_x = (long)spr->SWidth * units_per_px / 16;
            delta_y      = (long)spr->SHeight * units_per_px / 16;
            if (spr_idx)
            {
                SpriteHandle h = resolve_sprite_handle(spr);
                s_uiRenderer->SubmitScaledSprite(cur_x, cur_y, delta_x, delta_y, h);
            }
            spr_idx++;
            cur_x += delta_x;
        }
        cur_y += delta_y;
    }
}

void UIRenderer_SetLayer(int layer)
{
    if (s_uiRenderer)
        s_uiRenderer->SetLayer(layer);
}

void UIRenderer_FlushBack(void)
{
    if (s_uiRenderer)
        s_uiRenderer->FlushBack();
}

void UIRenderer_FlushFront(void)
{
    if (s_uiRenderer)
        s_uiRenderer->FlushFront();
}

void UIRenderer_Flush(void)
{
    if (s_uiRenderer)
        s_uiRenderer->Flush();
}

void UIRenderer_Clear(void)
{
    if (s_uiRenderer)
        s_uiRenderer->Clear();
}

TbBool UIRenderer_IsGpuActive(void)
{
    return (s_uiRenderer && s_uiRenderer->IsGpuAccelerated()) ? 1 : 0;
}
