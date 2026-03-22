/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaSpriteLayer.cpp
 *     GPU UI sprite batch for the Vita renderer (Tier 1) — implementation.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/vita/VitaSpriteLayer.h"

#ifdef PLATFORM_VITA

#include <string.h>
#include "globals.h"
#include "bflib_basics.h"
#include "bflib_video.h"    // lbPalette, lbDisplay
#include "renderer/vita/VitaPassCommon.h"
#include "sprite_batch.h"   // SB_FLAG_* constants
#include "post_inc.h"

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

// Vertex shader: transforms from 960×544 screen pixels to NDC.
// aPos: screen-space x,y in pixels
// aUV:  atlas UV coordinates
static const char* k_sprite_vert_glsl =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying   vec2 vUV;\n"
    "uniform   vec2 uInvScreen;\n"   // (1/960, 1/544)
    "void main() {\n"
    "    vec2 ndc = aPos * uInvScreen * 2.0 - 1.0;\n"
    "    ndc.y = -ndc.y;\n"          // flip Y: screen top → NDC +1
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "    vUV = aUV;\n"
    "}\n";

// Fragment shader — normal: look up RGBA from palette texture, apply blend alpha.
// Atlas is GL_LUMINANCE_ALPHA: .r = palette index (0..1), .a = opacity (0 or 1).
static const char* k_sprite_frag_normal_glsl =
    "precision mediump float;\n"
    "varying vec2      vUV;\n"
    "uniform sampler2D uAtlas;\n"
    "uniform sampler2D uPalette;\n"
    "uniform float     uAlpha;\n"
    "void main() {\n"
    "    vec2  ia = texture2D(uAtlas, vUV).ra;\n"  // ia.x=index, ia.y=alpha
    "    if (ia.y < 0.01) discard;\n"
    "    vec3 rgb = texture2D(uPalette, vec2(ia.x, 0.5)).rgb;\n"
    "    gl_FragColor = vec4(rgb, ia.y * uAlpha);\n"
    "}\n";

// Fragment shader — one colour: alpha mask from atlas, solid fill colour.
static const char* k_sprite_frag_one_colour_glsl =
    "precision mediump float;\n"
    "varying vec2      vUV;\n"
    "uniform sampler2D uAtlas;\n"
    "uniform float     uAlpha;\n"
    "uniform vec3      uColour;\n"
    "void main() {\n"
    "    float a = texture2D(uAtlas, vUV).a;\n"
    "    if (a < 0.01) discard;\n"
    "    gl_FragColor = vec4(uColour, a * uAlpha);\n"
    "}\n";

// ---------------------------------------------------------------------------
// Helper: build one program from the shared sprite vertex shader + a frag.
// Binds aPos=0, aUV=1.
// ---------------------------------------------------------------------------
static GLuint build_sprite_program(const char* frag_glsl)
{
    GLuint vert = vita_compile_shader(GL_VERTEX_SHADER,   k_sprite_vert_glsl);
    GLuint frag = vita_compile_shader(GL_FRAGMENT_SHADER, frag_glsl);
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return 0;
    }
    GLuint prog = glCreateProgram();
    if (!prog) {
        glDeleteShader(vert);
        glDeleteShader(frag);
        return 0;
    }
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aUV");
    glLinkProgram(prog);
    glDeleteShader(vert);
    glDeleteShader(frag);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetProgramInfoLog(prog, sizeof(log) - 1, nullptr, log);
        ERRORLOG("[VitaSpriteLayer] link error: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// ---------------------------------------------------------------------------
bool VitaSpriteLayer::BuildShaders()
{
    m_prog_normal = build_sprite_program(k_sprite_frag_normal_glsl);
    if (!m_prog_normal) return false;
    glUseProgram(m_prog_normal);
    m_loc_atlas_n   = glGetUniformLocation(m_prog_normal, "uAtlas");
    m_loc_palette_n = glGetUniformLocation(m_prog_normal, "uPalette");
    m_loc_alpha_n   = glGetUniformLocation(m_prog_normal, "uAlpha");
    GLint loc_inv = glGetUniformLocation(m_prog_normal, "uInvScreen");
    glUniform1i(m_loc_atlas_n,   0);   // TEXUNIT0 = index atlas
    glUniform1i(m_loc_palette_n, 1);   // TEXUNIT1 = palette lookup
    glUniform2f(loc_inv, 1.0f / (float)k_screenW, 1.0f / (float)k_screenH);

    m_prog_one_colour = build_sprite_program(k_sprite_frag_one_colour_glsl);
    if (!m_prog_one_colour) return false;
    glUseProgram(m_prog_one_colour);
    m_loc_atlas_oc   = glGetUniformLocation(m_prog_one_colour, "uAtlas");
    m_loc_palette_oc = glGetUniformLocation(m_prog_one_colour, "uPalette");
    m_loc_alpha_oc   = glGetUniformLocation(m_prog_one_colour, "uAlpha");
    m_loc_colour_oc  = glGetUniformLocation(m_prog_one_colour, "uColour");
    GLint loc_inv2   = glGetUniformLocation(m_prog_one_colour, "uInvScreen");
    glUniform1i(m_loc_atlas_oc,   0);
    glUniform1i(m_loc_palette_oc, 1);
    glUniform2f(loc_inv2, 1.0f / (float)k_screenW, 1.0f / (float)k_screenH);

    glUseProgram(0);
    return true;
}

// ---------------------------------------------------------------------------
bool VitaSpriteLayer::Init()
{
    if (m_initialized) return true;

    if (!m_atlas.Init()) {
        ERRORLOG("VitaSpriteLayer: UISpriteAtlas init failed");
        return false;
    }

    if (!BuildShaders()) {
        ERRORLOG("VitaSpriteLayer: shader build failed");
        return false;
    }

    glGenBuffers(1, &m_vbo);
    if (!m_vbo) {
        ERRORLOG("VitaSpriteLayer: VBO alloc failed");
        return false;
    }

    // Create the 256×1 palette lookup texture (initially zeros; will be
    // populated on the first on_palette_set call).
    glGenTextures(1, &m_palette_tex);
    glBindTexture(GL_TEXTURE_2D, m_palette_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Validate palette dimensions (256×1 is a fixed constant)
    const int palette_w = 256, palette_h = 1;
    if (palette_w <= 0 || palette_w > 4096 || palette_h <= 0 || palette_h > 4096) {
        ERRORLOG("VitaSpriteLayer: invalid palette texture dimensions %dx%d", palette_w, palette_h);
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
        return false;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, palette_w, palette_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_queue_count = 0;
    m_initialized = true;
    return true;
}

// ---------------------------------------------------------------------------
void VitaSpriteLayer::Free()
{
    if (!m_initialized) return;
    m_atlas.Free();
    if (m_prog_normal)     { glDeleteProgram(m_prog_normal);     m_prog_normal     = 0; }
    if (m_prog_one_colour) { glDeleteProgram(m_prog_one_colour); m_prog_one_colour = 0; }
    if (m_vbo)             { glDeleteBuffers(1, &m_vbo);         m_vbo             = 0; }
    if (m_palette_tex)     { glDeleteTextures(1, &m_palette_tex); m_palette_tex    = 0; }
    m_queue_count = 0;
    m_initialized = false;
}

// ---------------------------------------------------------------------------
void VitaSpriteLayer::BeginFrame()
{
    m_queue_count = 0;
}

// ---------------------------------------------------------------------------
void VitaSpriteLayer::UpdatePaletteTexture(const unsigned char* lbPal)
{
    if (!m_palette_tex || !lbPal) return;

    // Expand 256 6-bit entries to RGBA8 (A=255 for all; transparency comes
    // from the atlas alpha channel, not the palette).
    static uint8_t rgba[256 * 4];
    for (int i = 0; i < 256; i++) {
        rgba[i * 4 + 0] = (uint8_t)((lbPal[i * 3 + 0] << 2) | (lbPal[i * 3 + 0] >> 4));
        rgba[i * 4 + 1] = (uint8_t)((lbPal[i * 3 + 1] << 2) | (lbPal[i * 3 + 1] >> 4));
        rgba[i * 4 + 2] = (uint8_t)((lbPal[i * 3 + 2] << 2) | (lbPal[i * 3 + 2] >> 4));
        rgba[i * 4 + 3] = 0xFF;
    }
    glBindTexture(GL_TEXTURE_2D, m_palette_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
void VitaSpriteLayer::PaletteToRGBf(uint8_t idx, float* r, float* g, float* b)
{
    // lbPalette is extern uint8_t[768], 6-bit entries
    extern uint8_t lbPalette[];
    *r = (float)((lbPalette[idx * 3 + 0] << 2) | (lbPalette[idx * 3 + 0] >> 4)) / 255.0f;
    *g = (float)((lbPalette[idx * 3 + 1] << 2) | (lbPalette[idx * 3 + 1] >> 4)) / 255.0f;
    *b = (float)((lbPalette[idx * 3 + 2] << 2) | (lbPalette[idx * 3 + 2] >> 4)) / 255.0f;
}

// ---------------------------------------------------------------------------
TbResult VitaSpriteLayer::PushQuad(long virt_x, long virt_y,
                                    const TbSprite* spr,
                                    const AtlasEntry& uv,
                                    SpriteMode mode, uint8_t colour_idx,
                                    unsigned int draw_flags)
{
    if (m_queue_count >= k_sprite_queue_max) {
        // Flush early to make room (non-fatal, maintains correct ordering).
        SYNCDBG(11, "VitaSpriteLayer: queue full — early flush");
        Flush();
    }

    // Convert virtual game coords to Vita native pixels.
    // Apply the current GraphicsWindow offset so windowed sub-draws are
    // correctly positioned (matches what LbSpriteDrawPrepare does for CPU).
    long abs_x = virt_x + lbDisplay.GraphicsWindowX;
    long abs_y = virt_y + lbDisplay.GraphicsWindowY;

    const float sx = (float)k_screenW / (float)k_gameW;
    const float sy = (float)k_screenH / (float)k_gameH;

    SpriteQuad& q = m_queue[m_queue_count++];
    q.sc_x = (float)abs_x * sx;
    q.sc_y = (float)abs_y * sy;
    q.sc_w = (float)spr->SWidth  * sx;
    q.sc_h = (float)spr->SHeight * sy;

    // Handle horizontal/vertical flip: swap the UV corners.
    q.u0 = (draw_flags & SB_FLAG_FLIP_HORIZ)  ? uv.u1 : uv.u0;
    q.u1 = (draw_flags & SB_FLAG_FLIP_HORIZ)  ? uv.u0 : uv.u1;
    q.v0 = (draw_flags & SB_FLAG_FLIP_VERTIC) ? uv.v1 : uv.v0;
    q.v1 = (draw_flags & SB_FLAG_FLIP_VERTIC) ? uv.v0 : uv.v1;

    q.atlas_page  = (uint8_t)uv.page;
    q.mode        = mode;
    q.colour_idx  = colour_idx;
    q.draw_flags  = draw_flags;

    return Lb_SUCCESS;
}

// ---------------------------------------------------------------------------
TbResult VitaSpriteLayer::SubmitSprite(long x, long y,
                                        const TbSprite* spr,
                                        unsigned int draw_flags)
{
    if (!m_initialized) return Lb_FAIL;
    AtlasEntry uv;
    if (!m_atlas.GetUV(spr, &uv)) return Lb_FAIL;
    return PushQuad(x, y, spr, uv, SPRMODE_NORMAL, 0, draw_flags);
}

// ---------------------------------------------------------------------------
TbResult VitaSpriteLayer::SubmitSpriteOneColour(long x, long y,
                                                 const TbSprite* spr,
                                                 unsigned char colour,
                                                 unsigned int draw_flags)
{
    if (!m_initialized) return Lb_FAIL;
    AtlasEntry uv;
    if (!m_atlas.GetUV(spr, &uv)) return Lb_FAIL;
    return PushQuad(x, y, spr, uv, SPRMODE_ONE_COLOUR, (uint8_t)colour, draw_flags);
}

// ---------------------------------------------------------------------------
void VitaSpriteLayer::Flush()
{
    if (!m_initialized || m_queue_count == 0) return;

    // Save GL state we'll modify.
    GLboolean blend_was_on = glIsEnabled(GL_BLEND);
    GLint     prev_prog    = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, k_screenW, k_screenH);

    // Interleaved vertex layout per vertex (for a quad = 4 vertices):
    //   float x, y  (screen pixels)
    //   float u, v  (atlas UV)
    // We'll stream the whole batch into the VBO at once.
    struct GpuVertex { float x, y, u, v; };
    // 4 vertices per quad (triangle strip), but we use two triangles (6 verts)
    // to keep indexing simple with glDrawArrays.
    static GpuVertex buf[k_sprite_queue_max * 6];

    int     total_verts  = 0;
    int     cur_page     = -1;
    int     seg_start    = 0;   // first vertex index of current atlas-page segment
    GLuint  cur_prog     = 0;
    float   cur_alpha    = -1.0f;  // sentinel: force first-quad uniform upload
    uint8_t cur_colour   = 0xFF;   // sentinel: force first one-colour upload

    // Flush a contiguous run of vertices [seg_start, seg_end) as one draw call.
    // We break a segment whenever page, program, alpha OR colour changes so
    // every draw call has consistent uniform values across all its quads.
    auto flush_segment = [&](int seg_end) {
        if (seg_end <= seg_start) return;
        int count = seg_end - seg_start;

        // Upload this segment's vertices to VBO.
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     count * sizeof(GpuVertex),
                     buf + seg_start,
                     GL_STREAM_DRAW);

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                              (void*)offsetof(GpuVertex, x));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                              (void*)offsetof(GpuVertex, u));

        // Bind the atlas page texture to TEXUNIT0, palette to TEXUNIT1.
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_palette_tex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_atlas.GetTexture(cur_page));

        glDrawArrays(GL_TRIANGLES, 0, count);
        seg_start = seg_end;
    };

    for (int qi = 0; qi < m_queue_count; qi++)
    {
        const SpriteQuad& q = m_queue[qi];

        // Determine blend alpha from draw_flags.
        float alpha = 1.0f;
        if (q.draw_flags & (SB_FLAG_TRANSPAR4 | SB_FLAG_TRANSPAR8))
            alpha = 0.5f;

        // Select the correct GPU program.
        GLuint want_prog = (q.mode == SPRMODE_ONE_COLOUR)
                           ? m_prog_one_colour : m_prog_normal;

        // Break the segment whenever page, program, blend alpha, or fill
        // colour changes — any of these require a new uniform upload.
        bool need_break = (q.atlas_page != cur_page)
                       || (want_prog    != cur_prog)
                       || (alpha        != cur_alpha)
                       || (q.mode == SPRMODE_ONE_COLOUR && q.colour_idx != cur_colour);

        if (need_break) {
            flush_segment(total_verts);
            cur_page   = q.atlas_page;
            cur_prog   = want_prog;
            cur_alpha  = alpha;
            cur_colour = q.colour_idx;
            glUseProgram(cur_prog);
            if (q.mode == SPRMODE_ONE_COLOUR) {
                float r, g, b;
                PaletteToRGBf(q.colour_idx, &r, &g, &b);
                glUniform1i(m_loc_atlas_oc,  0);
                glUniform1i(m_loc_palette_oc, 1);
                glUniform1f(m_loc_alpha_oc,  alpha);
                glUniform3f(m_loc_colour_oc, r, g, b);
            } else {
                glUniform1i(m_loc_atlas_n,   0);
                glUniform1i(m_loc_palette_n, 1);
                glUniform1f(m_loc_alpha_n,   alpha);
            }
        }

        // Emit two triangles (CCW) for this quad.
        // Screen coords: (x0,y0) = top-left, (x1,y1) = bottom-right.
        const float x0 = q.sc_x,         y0 = q.sc_y;
        const float x1 = q.sc_x + q.sc_w, y1 = q.sc_y + q.sc_h;

        GpuVertex* v = buf + total_verts;
        // Triangle 1: TL, TR, BL
        v[0] = { x0, y0, q.u0, q.v0 };
        v[1] = { x1, y0, q.u1, q.v0 };
        v[2] = { x0, y1, q.u0, q.v1 };
        // Triangle 2: TR, BR, BL
        v[3] = { x1, y0, q.u1, q.v0 };
        v[4] = { x1, y1, q.u1, q.v1 };
        v[5] = { x0, y1, q.u0, q.v1 };
        total_verts += 6;
    }

    // Flush the final segment.
    flush_segment(total_verts);

    // Restore state.
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glUseProgram(prev_prog);
    if (!blend_was_on) glDisable(GL_BLEND);

    m_queue_count = 0;
}

#endif // PLATFORM_VITA
