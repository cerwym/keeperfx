/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererManager.cpp
 *     Renderer backend registration and lifecycle management.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"

#include "renderer/RendererSoftware.h"
#include "renderer/backends/SoftwareWorldViewRenderer.h"
#include "renderer/backends/SoftwareMapFadePass.h"
#include "renderer/backends/SoftwareTextRenderer.h"
#ifdef RENDERER_OPENGL_ENABLED
#  include "renderer/RendererOpenGL.h"
#  include "renderer/opengl/GLTileAtlas.h"
#  include "renderer/opengl/GLWorldViewRenderer.h"
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
#include "post_inc.h"

/******************************************************************************/

static IRenderer*           s_activeRenderer      = nullptr;
static RendererType         s_activeType          = RENDERER_INVALID;
static IWorldViewRenderer*  s_worldViewRenderer   = nullptr;
static IMapFadePass*        s_mapFadePass         = nullptr;
static ITextRenderer*       s_textRenderer        = nullptr;

/******************************************************************************/

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
        if (ogl && ogl->GetTileAtlas())
        {
            auto* glwr = new GLWorldViewRenderer(
                ogl->GetTileAtlas(),
                (GLuint)ogl->GetFadeTex(),
                (GLuint)ogl->GetPaletteTex());
            ogl->SetWorldRenderer(glwr);
            return glwr;
        }
        WARNLOG("RendererManager: GLWorldViewRenderer requested but resources not ready — using software fallback");
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
    (void)type; // reserved for future GPU dispatch
    return new SoftwareTextRenderer();
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

void WorldViewRenderer_BeginWorldPass(unsigned char* framebuf, int pitch, int w, int h)
{
    if (s_worldViewRenderer)
        s_worldViewRenderer->BeginWorldPass(framebuf, pitch, w, h);
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
