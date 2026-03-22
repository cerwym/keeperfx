/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaSpriteLayer.h
 *     GPU UI sprite batch for the Vita renderer (Tier 1).
 * @par Purpose:
 *     Accumulates screen-space sprite draw commands during a frame and flushes
 *     them as a single alpha-blended GPU draw pass after the 3D scene and
 *     post-process chain are complete.
 *
 *     Call order each frame:
 *       BeginFrame()  — resets the command list
 *       SubmitSprite() / SubmitSpriteOneColour()  — accumulate quads
 *       Flush()       — issues GPU draw on FBO 0 (screen)
 *
 *     Coordinates are supplied in virtual game space (640×480).  The layer
 *     transforms them to Vita native (960×544) internally.
 */
/******************************************************************************/
#ifndef VITA_SPRITE_LAYER_H
#define VITA_SPRITE_LAYER_H

#ifdef PLATFORM_VITA

#include <vitaGL.h>
#include <stdint.h>

#include "bflib_basics.h"   // TbResult
#include "bflib_sprite.h"   // TbSprite, TbSpriteSheet
#include "renderer/vita/UISpriteAtlas.h"

/** Maximum sprite quads accumulated per frame. */
static const int k_sprite_queue_max = 2048;

/******************************************************************************/

/** Draw mode flags forwarded from SpriteBatchInterface. */
enum SpriteMode : uint8_t {
    SPRMODE_NORMAL       = 0,  /**< Normal (atlas colour + alpha). */
    SPRMODE_ONE_COLOUR   = 1,  /**< Single fill colour, alpha mask from atlas. */
};

/** One sprite quad in the frame's draw list. */
struct SpriteQuad {
    /* Screen-space position in Vita native pixels (960×544). */
    float sc_x, sc_y, sc_w, sc_h;

    /* UV rect on the atlas page. */
    float u0, v0, u1, v1;

    /* Which atlas GL texture page to bind. */
    uint8_t atlas_page;

    /* Draw mode. */
    SpriteMode mode;

    /* Fill colour (palette index, for SPRMODE_ONE_COLOUR).
     * Expanded to RGBA8 at flush time using the current lbPalette. */
    uint8_t colour_idx;

    /* Blend flags (SB_FLAG_TRANSPAR4 / SB_FLAG_TRANSPAR8). */
    uint32_t draw_flags;
};

/******************************************************************************/

class VitaSpriteLayer {
public:
    VitaSpriteLayer()  = default;
    ~VitaSpriteLayer() { Free(); }

    VitaSpriteLayer(const VitaSpriteLayer&)            = delete;
    VitaSpriteLayer& operator=(const VitaSpriteLayer&) = delete;

    /** Compile shaders, create VBO.  Called from RendererVita::Init(). */
    bool Init();

    /** Release all GL resources. */
    void Free();

    bool IsInitialized() const { return m_initialized; }

    // -------------------------------------------------------------------------
    // Per-frame interface (called from the SpriteBatchInterface shims)

    /** Reset draw list for the new frame. */
    void BeginFrame();

    /** Upload the current 6-bit lbPalette as a 256×1 RGBA8 texture.
     *  Called once from on_palette_set — ~1 KB upload, no atlas rebuild. */
    void UpdatePaletteTexture(const unsigned char* lbPalette);

    /** Accumulate a normal sprite quad.
     *  @param x,y        Virtual game coordinates (640×480 space).
     *  @param spr        Sprite to draw.
     *  @param draw_flags SB_FLAG_* blend/flip bits. */
    TbResult SubmitSprite(long x, long y,
                          const struct TbSprite* spr,
                          unsigned int draw_flags);

    /** Accumulate a one-colour sprite quad. */
    TbResult SubmitSpriteOneColour(long x, long y,
                                   const struct TbSprite* spr,
                                   unsigned char colour,
                                   unsigned int draw_flags);

    /** Issue GPU draw for all accumulated quads, then clear the list.
     *  Renders onto the currently bound framebuffer (caller sets FBO 0). */
    void Flush();

    // -------------------------------------------------------------------------
    // Atlas access (forwarded from SpriteBatchInterface shims)
    UISpriteAtlas& GetAtlas() { return m_atlas; }

private:
    bool m_initialized = false;

    /* Vita native display dimensions. */
    static const int k_screenW = 960;
    static const int k_screenH = 544;
    /* Virtual game dimensions. */
    static const int k_gameW   = 640;
    static const int k_gameH   = 480;

    UISpriteAtlas m_atlas;

    /* Accumulated draw list. */
    SpriteQuad m_queue[k_sprite_queue_max];
    int        m_queue_count = 0;

    /* GLSL programs. */
    GLuint m_prog_normal     = 0;   /**< atlas colour + alpha. */
    GLuint m_prog_one_colour = 0;   /**< solid colour + alpha mask. */

    /* Uniform locations for m_prog_normal. */
    GLint  m_loc_atlas_n      = -1;
    GLint  m_loc_palette_n    = -1;
    GLint  m_loc_alpha_n      = -1;

    /* Uniform locations for m_prog_one_colour. */
    GLint  m_loc_atlas_oc     = -1;
    GLint  m_loc_palette_oc   = -1;
    GLint  m_loc_alpha_oc     = -1;
    GLint  m_loc_colour_oc    = -1;

    /* 256×1 RGBA palette lookup texture — updated cheaply on each palette set. */
    GLuint m_palette_tex = 0;

    /* VBO for interleaved quad vertex data: (pos.xy, uv.xy) per vertex. */
    GLuint m_vbo = 0;

    /* --- Internal helpers --- */
    bool BuildShaders();

    /** Push one quad to m_queue after computing screen-space pos from @p x,y
     *  and looking up the atlas UV.  Returns Lb_SUCCESS or Lb_FAIL. */
    TbResult PushQuad(long x, long y, const struct TbSprite* spr,
                      const AtlasEntry& uv,
                      SpriteMode mode, uint8_t colour_idx,
                      unsigned int draw_flags);

    /** Convert a palette index to R,G,B using lbPalette (6-bit → float). */
    static void PaletteToRGBf(uint8_t idx, float* r, float* g, float* b);
};

#endif // PLATFORM_VITA
#endif // VITA_SPRITE_LAYER_H
