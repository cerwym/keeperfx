/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLTextRenderer.cpp
 *     OpenGL hardware-accelerated text renderer.
 */
/******************************************************************************/
#include "pre_inc.h" 
#include "renderer/opengl/GLTextRenderer.h"

#ifdef RENDERER_OPENGL_ENABLED

#include "renderer/opengl/GLFontAtlas.h"
#include "renderer/opengl/GLDbcFontAtlas.h"
#include "renderer/opengl/GLShaders.h"
#include "bflib_sprfnt.h"
#include "bflib_video.h"
#include "frontend.h"       // frontend_font[], winfont, font_sprites, frontstory_font
#include "front_credits.h"  // frontstory_font

#include <glad/glad.h>
#include <cstring>
#include <climits>
#include "kfx/profiling/KfxProfiling.h"
#include "post_inc.h"

/******************************************************************************/

GLTextRenderer::GLTextRenderer()
    : m_active_atlas(nullptr)
    , m_active_dbc_atlas(nullptr)
    , m_current_dbc_colour0(0)
    , m_shader_program(0)
    , m_vao(0)
    , m_vbo(0)
    , m_palette_tex(0)
    , m_screen_width(0)
    , m_screen_height(0)
    , m_loc_viewport(-1)
    , m_loc_font_atlas(-1)
    , m_loc_palette(-1)
    , m_loc_text_color(-1)
    , m_font(nullptr)
    , m_justify_window{}
    , m_clip_window{}
    , m_dbc_font(nullptr)
    , m_dbc_colour0(0)
    , m_dbc_colour1(0)
    , m_dbc_enabled(false)
    , m_batch_scissor_x(0)
    , m_batch_scissor_y(0)
    , m_batch_scissor_w(0)
    , m_batch_scissor_h(0)
    , m_batch_scissor_enabled(false)
{
}

GLTextRenderer::~GLTextRenderer()
{
    Shutdown();
}

bool GLTextRenderer::Init()
{
    // Atlases are created lazily in Flush() per unique font pointer.

    // Compile shaders
    if (!CompileShaders())
    {
        ERRORLOG("GLTextRenderer: failed to compile shaders");
        Shutdown();
        return false;
    }

    // Create vertex array and buffer for text quads
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Allocate dynamic buffer for text vertices
    glBufferData(GL_ARRAY_BUFFER, 32768 * sizeof(TextVertex), nullptr, GL_DYNAMIC_DRAW);

    // Vertex attributes: position (vec2) + UV (vec2) + forced_palette_idx (float)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // Create palette texture
    glGenTextures(1, &m_palette_tex);
    glBindTexture(GL_TEXTURE_2D, m_palette_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Get uniform locations
    glUseProgram(m_shader_program);
    m_loc_viewport = glGetUniformLocation(m_shader_program, "u_viewport");
    m_loc_font_atlas = glGetUniformLocation(m_shader_program, "u_font_atlas");
    m_loc_palette = glGetUniformLocation(m_shader_program, "u_palette");
    m_loc_text_color = glGetUniformLocation(m_shader_program, "u_text_color");

    SYNCLOG("GLTextRenderer: Uniform locations - viewport=%d atlas=%d palette=%d color=%d", 
            m_loc_viewport, m_loc_font_atlas, m_loc_palette, m_loc_text_color);

    // Only set sampler uniforms if locations are valid
    if (m_loc_font_atlas >= 0)
        glUniform1i(m_loc_font_atlas, 0);  // GL_TEXTURE0
    if (m_loc_palette >= 0)
        glUniform1i(m_loc_palette, 1);     // GL_TEXTURE1
    glUseProgram(0);

    m_vertex_batch.reserve(32768);
    SYNCLOG("GLTextRenderer: initialized");
    return true;
}

void GLTextRenderer::Shutdown()
{
    for (auto& kv : m_atlas_cache)
        delete kv.second;
    m_atlas_cache.clear();
    m_active_atlas = nullptr;

    for (auto& kv : m_dbc_atlas_cache)
        delete kv.second;
    m_dbc_atlas_cache.clear();
    m_active_dbc_atlas = nullptr;

    if (m_shader_program) { glDeleteProgram(m_shader_program); m_shader_program = 0; }
    if (m_vao)            { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo)            { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_palette_tex)    { glDeleteTextures(1, &m_palette_tex); m_palette_tex = 0; }
}

bool GLTextRenderer::CompileShaders()
{
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &TEXT_VERTEX_SHADER, nullptr);
    glCompileShader(vert);
    
    GLint vert_ok = 0;
    glGetShaderiv(vert, GL_COMPILE_STATUS, &vert_ok);
    if (!vert_ok)
    {
        char log[512];
        glGetShaderInfoLog(vert, sizeof(log), nullptr, log);
        ERRORLOG("GLTextRenderer: vertex shader compile error: %s", log);
        glDeleteShader(vert);
        return false;
    }

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &TEXT_FRAGMENT_SHADER, nullptr);
    glCompileShader(frag);
    
    GLint frag_ok = 0;
    glGetShaderiv(frag, GL_COMPILE_STATUS, &frag_ok);
    if (!frag_ok)
    {
        char log[512];
        glGetShaderInfoLog(frag, sizeof(log), nullptr, log);
        ERRORLOG("GLTextRenderer: fragment shader compile error: %s", log);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    m_shader_program = glCreateProgram();
    glAttachShader(m_shader_program, vert);
    glAttachShader(m_shader_program, frag);
    glLinkProgram(m_shader_program);
    
    GLint linked = 0;
    glGetProgramiv(m_shader_program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[512];
        glGetProgramInfoLog(m_shader_program, sizeof(log), nullptr, log);
        ERRORLOG("GLTextRenderer: shader link error: %s", log);
        glDeleteShader(vert);
        glDeleteShader(frag);
        glDeleteProgram(m_shader_program);
        m_shader_program = 0;
        return false;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return true;
}

void GLTextRenderer::SetFont(const struct TbSpriteSheet* font)
{
    m_font = font;

    if (dbc_initialized)
    {
        m_dbc_colour0 = LbTextGetFontFaceColor(font);
        m_dbc_colour1 = LbTextGetFontBackColor(font);

        // Resolve DBC font index from the Western font identity
        int dbc_idx;
        if (font == frontend_font[0]) {
            dbc_idx = 2;
        } else if (font == frontend_font[1] || font == frontend_font[2] ||
                   font == frontend_font[3] || font == winfont ||
                   font == font_sprites || font == frontstory_font) {
            dbc_idx = (lbDisplay.PhysicalScreenWidth < 512) ? 0 : 1;
        } else {
            dbc_idx = (lbDisplay.PhysicalScreenWidth < 512) ? 0 : 1;
        }

        const int32_t fonts_count = dbc_fonts_count();
        struct AsianFont* dbcfonts = dbc_fonts_list();
        if ((dbc_idx >= 0) && (dbc_idx < fonts_count) && (dbcfonts != nullptr))
        {
            m_dbc_font    = &dbcfonts[dbc_idx];
            m_dbc_enabled = true;
        }
        else
        {
            m_dbc_font    = nullptr;
            m_dbc_enabled = false;
        }

        // Keep globals in sync during transition
        active_dbcfont = const_cast<struct AsianFont*>(m_dbc_font);
    }
    else
    {
        m_dbc_font    = nullptr;
        m_dbc_enabled = false;
    }
}

void GLTextRenderer::SetWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
    m_justify_window = { x, y, w, 0 };
    SetClipWindow(x, y, w, h);
}

void GLTextRenderer::SetJustifyWindow(int32_t x, int32_t y, int32_t w)
{
    m_justify_window.x = x;
    m_justify_window.y = y;
    m_justify_window.width = w;
}

void GLTextRenderer::SetClipWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t x0 = x, y0 = y;
    int32_t x1 = x + w, y1 = y + h;
    if (x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int32_t t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 < 0) x1 = 0;
    if (y0 < 0) y0 = 0;
    if (y1 < 0) y1 = 0;
    if (x0 > lbDisplay.GraphicsScreenWidth)  x0 = lbDisplay.GraphicsScreenWidth;
    if (x1 > lbDisplay.GraphicsScreenWidth)  x1 = lbDisplay.GraphicsScreenWidth;
    if (y0 > lbDisplay.GraphicsScreenHeight) y0 = lbDisplay.GraphicsScreenHeight;
    if (y1 > lbDisplay.GraphicsScreenHeight) y1 = lbDisplay.GraphicsScreenHeight;
    m_clip_window = { x0, y0, x1 - x0, y1 - y0 };
}

void GLTextRenderer::GetJustifyWindow(int32_t* x, int32_t* y, int32_t* w) const
{
    if (x) *x = m_justify_window.x;
    if (y) *y = m_justify_window.y;
    if (w) *w = m_justify_window.width;
}

void GLTextRenderer::GetClipWindow(int32_t* x, int32_t* y, int32_t* w, int32_t* h) const
{
    if (x) *x = m_clip_window.x;
    if (y) *y = m_clip_window.y;
    if (w) *w = m_clip_window.width;
    if (h) *h = m_clip_window.height;
}

void GLTextRenderer::SetScreenSize(int width, int height)
{
    m_screen_width = width;
    m_screen_height = height;
}

TbBool GLTextRenderer::DrawTextResized(int32_t posx, int32_t posy, int32_t units_per_px, const char* text)
{
    if (!text)
        return false;

    m_pending.push_back({ posx, posy, units_per_px,
                          m_justify_window.x, m_justify_window.y, m_justify_window.width,
                          m_clip_window.x, m_clip_window.y, m_clip_window.width, m_clip_window.height,
                          lbDisplay.DrawColour, lbDisplay.DrawFlags,
                          text,
                          m_font,
                          m_dbc_font, m_dbc_colour0, m_dbc_colour1, m_dbc_enabled });
    return true;
}

TbBool GLTextRenderer::DrawTextAt(int32_t screen_x, int32_t screen_y, int32_t units_per_px, const char* text)
{
    // Phase 1 stub: queue identically to DrawTextResized.
    // Phase 4+ will implement single-line direct draw.
    return DrawTextResized(screen_x, screen_y, units_per_px, text);
}

int32_t GLTextRenderer::LineHeight()
{
    return LbSprFontCharHeight(m_font, ' ');
}

int32_t GLTextRenderer::CharWidth(uint32_t chr)
{
    return LbSprFontCharWidth(m_font, static_cast<unsigned char>(chr));
}

int32_t GLTextRenderer::CharWidthScaled(uint32_t chr, int32_t units_per_px)
{
    return LbSprFontCharWidth(m_font, static_cast<unsigned char>(chr)) * units_per_px / 16;
}

int32_t GLTextRenderer::StringWidth(const char* text)
{
    return LbTextStringPartWidth(text, INT_MAX);
}

int32_t GLTextRenderer::StringWidthScaled(const char* text, int32_t units_per_px)
{
    return StringWidth(text) * units_per_px / 16;
}

int32_t GLTextRenderer::WordWidth(const char* str)
{
    return LbSprFontWordWidth(m_font, str);
}

int32_t GLTextRenderer::WordWidthScaled(const char* str, int32_t units_per_px)
{
    return LbSprFontWordWidth(m_font, str) * units_per_px / 16;
}

int32_t GLTextRenderer::TextHeight(const char* text)
{
    return LineHeight();
}

int32_t GLTextRenderer::StringHeight(int32_t units_per_px, const char* text)
{
    if (!m_font || !text)
        return 0;

    int32_t nlines = 0;
    int32_t lnwidth_clip = m_justify_window.x - m_clip_window.x;
    int32_t lnwidth = lnwidth_clip;

    for (const char* pchr = text; *pchr != '\0'; pchr++)
    {
        int32_t chr = (unsigned char)(*pchr);
        if (is_wide_charcode(chr))
        {
            pchr++;
            if (*pchr == '\0') break;
            chr = (chr << 8) + (unsigned char)*pchr;
        }

        if (chr > 32)
        {
            int32_t w = CharWidthScaled(chr, units_per_px);
            if (lnwidth + w - lnwidth_clip > m_justify_window.width)
            {
                lnwidth = lnwidth_clip + w;
                nlines++;
            }
            else
            {
                lnwidth += w;
            }
        }
        else if (chr == ' ')
        {
            if (lnwidth > 0)
            {
                int32_t w = CharWidth(' ') * units_per_px / 16;
                if (lnwidth + w + WordWidth(pchr + 1) * units_per_px / 16 - lnwidth_clip > m_justify_window.width)
                {
                    lnwidth = lnwidth_clip;
                    nlines++;
                }
                else
                {
                    lnwidth += w;
                }
            }
        }
        else
        {
            switch (chr)
            {
            case '\r':
                lnwidth = lnwidth_clip;
                nlines++;
                if (pchr[1] == '\n') pchr++;
                break;
            case '\n':
                lnwidth = lnwidth_clip;
                nlines++;
                break;
            case '\t':
            {
                int32_t w = CharWidth(' ') * units_per_px / 16;
                lnwidth += LbTextGetSpacesPerTab() * w;
                if (lnwidth + WordWidth(pchr + 1) * units_per_px / 16 - lnwidth_clip > m_justify_window.width)
                {
                    lnwidth = lnwidth_clip;
                    nlines++;
                }
                break;
            }
            case 14:
                pchr++;
                break;
            }
        }
    }
    nlines++;
    return nlines * (LineHeight() * units_per_px / 16);
}

void GLTextRenderer::gl_draw_segment(const char* sbuf, const char* ebuf,
                                      int32_t x, int32_t y, int32_t space_len,
                                      int32_t units_per_px, void* userdata)
{
    GLTextRenderer* self = static_cast<GLTextRenderer*>(userdata);
    float scale = (float)units_per_px / 16.0f;
    self->FlushSegment(sbuf, ebuf,
                       (float)(self->m_clip_window.x + x),
                       (float)(self->m_clip_window.y + y),
                       (float)space_len, scale);
}

void GLTextRenderer::Flush()
{
    KFX_ZONE("TextRenderer::Flush");
    KFX_GPU_ZONE("TextPass");
    KFX_GL_SCOPE(text_grp, "TextPass");
    if (m_pending.empty())
        return;

    { // Diagnostic
        static int s_diag_frame = 0;
        if ((s_diag_frame++ % 300) == 0)
            SYNCLOG("GLTextRenderer::Flush: %d pending text draws", (int)m_pending.size());
    }

    if (m_screen_width <= 0 || m_screen_height <= 0)
    {
        m_screen_width  = MyScreenWidth;
        m_screen_height = MyScreenHeight;
    }

    // Set up GL state once for all draws this frame
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_shader_program);
    glBindVertexArray(m_vao);

    if (m_loc_viewport >= 0)
        glUniform2f(m_loc_viewport, (float)m_screen_width, (float)m_screen_height);
    if (m_loc_text_color >= 0)
        glUniform4f(m_loc_text_color, 1.0f, 1.0f, 1.0f, 1.0f);

    UpdatePaletteTexture();
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_palette_tex);

    // CRITICAL: Reset active atlas tracker because other renderers (minimap, sprites)
    // bind their textures to GL_TEXTURE0 between our Flush() calls, corrupting state.
    // We must rebind the font atlas texture for the first draw of each frame.
    m_active_atlas = nullptr;
    m_active_dbc_atlas = nullptr;
    // Reset batch accumulation state
    m_vertex_batch.clear();
    m_batch_scissor_enabled = false;
    m_batch_scissor_x = m_batch_scissor_y = m_batch_scissor_w = m_batch_scissor_h = 0;

    // Save globals that FlushSegment control codes will overwrite
    unsigned char               saved_colour     = lbDisplay.DrawColour;
    unsigned short              saved_draw_flags = lbDisplay.DrawFlags;

    // CRITICAL FIX: Sort pending draws by font pointer to minimize atlas rebinding.
    // Main menu uses 3 different fonts (36BC0DC8, 36BC0E18, 36BC1368) and without
    // sorting, the code thrashes between atlases hundreds of times per frame.
    // Group DBC draws together so the DBC atlas is bound once per font.
    std::sort(m_pending.begin(), m_pending.end(),
              [](const DeferredDraw& a, const DeferredDraw& b) {
                  if (a.dbc_enabled != b.dbc_enabled)
                      return (int)a.dbc_enabled < (int)b.dbc_enabled;
                  if (a.dbc_enabled)
                      return a.dbc_font < b.dbc_font;
                  return a.font < b.font;
              });

    for (const DeferredDraw& d : m_pending)
    {
        if (!d.font) continue;

        if (d.dbc_enabled && d.dbc_font)
        {
            // ---- DBC path: use DBC atlas for all characters ----
            GLDbcFontAtlas* dbc_atlas = nullptr;
            {
                auto it = m_dbc_atlas_cache.find(d.dbc_font);
                if (it != m_dbc_atlas_cache.end())
                {
                    dbc_atlas = it->second;
                    if (dbc_atlas->NeedsRebuild(d.dbc_font))
                    {
                        SYNCLOG("GLTextRenderer: Rebuilding stale DBC atlas for font %p", d.dbc_font);
                        dbc_atlas->Shutdown();
                        glActiveTexture(GL_TEXTURE0);
                        if (!dbc_atlas->Init(d.dbc_font))
                        {
                            ERRORLOG("GLTextRenderer::Flush: failed to rebuild DBC atlas for font %p", d.dbc_font);
                            m_dbc_atlas_cache.erase(it);
                            delete dbc_atlas;
                            continue;
                        }
                        if (dbc_atlas == m_active_dbc_atlas)
                            m_active_dbc_atlas = nullptr;
                    }
                }
                else
                {
                    SYNCLOG("GLTextRenderer: Creating new DBC atlas for font %p (cache size: %d)",
                           d.dbc_font, (int)m_dbc_atlas_cache.size());
                    dbc_atlas = new GLDbcFontAtlas();
                    glActiveTexture(GL_TEXTURE0);
                    if (!dbc_atlas->Init(d.dbc_font))
                    {
                        ERRORLOG("GLTextRenderer::Flush: failed to init DBC atlas for font %p", d.dbc_font);
                        delete dbc_atlas;
                        continue;
                    }
                    m_dbc_atlas_cache[d.dbc_font] = dbc_atlas;
                }
            }

            if (!dbc_atlas->IsInitialized()) continue;

            if (dbc_atlas != m_active_dbc_atlas)
            {
                FlushBatch();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, dbc_atlas->GetTextureID());
                m_active_dbc_atlas = dbc_atlas;
                m_active_atlas = nullptr;  // DBC atlas is bound, not Western
            }

            m_current_dbc_colour0 = d.dbc_colour0;
        }
        else
        {
            // ---- Western path: use sprite-based atlas ----
            GLFontAtlas* atlas = nullptr;
            {
                auto it = m_atlas_cache.find(d.font);
                if (it != m_atlas_cache.end())
                {
                    atlas = it->second;
                    if (atlas->NeedsRebuild(d.font))
                    {
                        SYNCLOG("GLTextRenderer: Rebuilding stale atlas for font %p (sprite count changed)", d.font);
                        atlas->Shutdown();
                        glActiveTexture(GL_TEXTURE0);
                        if (!atlas->Init(d.font))
                        {
                            ERRORLOG("GLTextRenderer::Flush: failed to rebuild atlas for font %p", d.font);
                            m_atlas_cache.erase(it);
                            delete atlas;
                            continue;
                        }
                        if (atlas == m_active_atlas)
                            m_active_atlas = nullptr;
                    }
                }
                else
                {
                    SYNCLOG("GLTextRenderer: Creating new atlas for font %p (cache size: %d)",
                           d.font, (int)m_atlas_cache.size());
                    atlas = new GLFontAtlas();
                    glActiveTexture(GL_TEXTURE0);
                    if (!atlas->Init(d.font))
                    {
                        ERRORLOG("GLTextRenderer::Flush: failed to init atlas for font %p", d.font);
                        delete atlas;
                        continue;
                    }
                    m_atlas_cache[d.font] = atlas;
                }
            }

            if (!atlas->IsInitialized()) continue;

            if (atlas != m_active_atlas)
            {
                FlushBatch();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, atlas->GetTextureID());
                m_active_atlas = atlas;
                m_active_dbc_atlas = nullptr;  // Western atlas is bound, not DBC
            }
        }

        // Set the clip window on the renderer so gl_draw_segment can read it
        // for absolute screen coordinate conversion.
        m_clip_window = { d.clip_x, d.clip_y, d.clip_w, d.clip_h };

        // Restore font state so virtual CharWidthScaled/WordWidthScaled
        // calls from TextLayout read the correct font for this draw.
        m_font        = d.font;
        m_dbc_font    = d.dbc_font;
        m_dbc_enabled = d.dbc_enabled;

        // Expose draw state as globals so FlushSegment reads/writes
        // lbDisplay.DrawColour / DrawFlags consistently for control codes.
        lbDisplay.DrawColour = d.draw_colour;
        lbDisplay.DrawFlags  = d.draw_flags;

        // Update batch scissor — flush if it changed so pending vertices use the old rect
        {
            bool new_enabled = (d.clip_w > 0 && d.clip_h > 0);
            if (new_enabled != m_batch_scissor_enabled
                || (new_enabled && (d.clip_x != m_batch_scissor_x
                                 || d.clip_y != m_batch_scissor_y
                                 || d.clip_w != m_batch_scissor_w
                                 || d.clip_h != m_batch_scissor_h)))
            {
                FlushBatch();
                m_batch_scissor_enabled = new_enabled;
                m_batch_scissor_x = d.clip_x;
                m_batch_scissor_y = d.clip_y;
                m_batch_scissor_w = d.clip_w;
                m_batch_scissor_h = d.clip_h;
            }
        }

        // Build layout context from the deferred draw's captured state
        TextLayoutContext ctx{};
        ctx.font           = d.font;
        ctx.dbc_font       = d.dbc_font;
        ctx.dbc_enabled    = d.dbc_enabled;
        ctx.draw_flags     = d.draw_flags;
        ctx.justify_window = { d.wnd_x, d.wnd_y, d.wnd_width, 0 };
        ctx.clip_window    = { d.clip_x, d.clip_y, d.clip_w, d.clip_h };
        ctx.spaces_per_tab = LbTextGetSpacesPerTab();

        TextLayout(ctx, d.posx, d.posy, d.units_per_px, d.text.c_str(),
                   gl_draw_segment, this);
    }

    FlushBatch();
    glDisable(GL_SCISSOR_TEST);
    m_pending.clear();

    // Restore globals overwritten during layout
    lbDisplay.DrawColour = saved_colour;
    lbDisplay.DrawFlags  = saved_draw_flags;

    // Restore GL state
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
}

void GLTextRenderer::FlushBatch()
{
    if (m_vertex_batch.empty())
        return;

    if (m_batch_scissor_enabled)
    {
        glEnable(GL_SCISSOR_TEST);
        int gl_y = m_screen_height - (m_batch_scissor_y + m_batch_scissor_h);
        glScissor(m_batch_scissor_x, gl_y, m_batch_scissor_w, m_batch_scissor_h);
    }
    else
    {
        glDisable(GL_SCISSOR_TEST);
    }

    GLsizeiptr byte_size = (GLsizeiptr)(m_vertex_batch.size() * sizeof(TextVertex));
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, byte_size, nullptr, GL_DYNAMIC_DRAW);  // orphan old storage
    glBufferSubData(GL_ARRAY_BUFFER, 0, byte_size, m_vertex_batch.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_vertex_batch.size());
    m_vertex_batch.clear();
}

void GLTextRenderer::FlushSegment(const char* sbuf, const char* ebuf,
                                   float screen_x, float screen_y,
                                   float space_len, float scale_factor)
{
    if (!sbuf || sbuf >= ebuf || (!m_active_atlas && !m_active_dbc_atlas))
        return;

    // Stack buffer: 100 characters × 6 vertices each
    TextVertex vertices[600];
    int vertex_count = 0;

    float current_x = screen_x;

    for (const char* c = sbuf; c < ebuf && vertex_count <= 594; ++c)
    {
        unsigned char ch  = (unsigned char)*c;
        unsigned long chr = ch;

        // Non-breaking space (UTF-8: 0xC2 0xA0) — advance like a normal space
        if (ch == '\xc2' && (c + 1) < ebuf && (unsigned char)c[1] == '\xa0')
        {
            current_x += space_len;
            ++c;
            continue;
        }

        if (ch == ' ')  { current_x += space_len; continue; }
        if (ch == '\t') { current_x += space_len * (float)LbTextGetSpacesPerTab(); continue; }

        // Control codes 1–14: update lbDisplay globals (mirrors put_down_simpletext_sprites)
        if (ch < 15)
        {
            switch (ch)
            {
                case 1:  lbDisplay.DrawFlags ^= Lb_SPRITE_TRANSPAR4;   break;
                case 2:  lbDisplay.DrawFlags ^= Lb_SPRITE_TRANSPAR8;   break;
                case 3:  lbDisplay.DrawFlags ^= Lb_SPRITE_OUTLINE;     break;
                case 4:  lbDisplay.DrawFlags ^= Lb_SPRITE_FLIP_HORIZ;  break;
                case 5:  lbDisplay.DrawFlags ^= Lb_SPRITE_FLIP_VERTIC; break;
                case 11: lbDisplay.DrawFlags ^= Lb_TEXT_UNDERLINE;     break;
                case 12: lbDisplay.DrawFlags ^= Lb_TEXT_ONE_COLOR;     break;
                case 14:
                    ++c;
                    if (c < ebuf) lbDisplay.DrawColour = (unsigned char)*c;
                    break;
                default: break;
            }
            continue;
        }

        // Wide character (Asian fonts)
        if (is_wide_charcode(chr))
        {
            ++c;
            if (c >= ebuf) break;
            chr = (chr << 8) | (unsigned long)(unsigned char)*c;
        }

        // Normal character — emit a textured quad
        float forced_idx;
        if (m_active_dbc_atlas)
        {
            // DBC glyphs are monochrome masks — always force a palette index.
            forced_idx = (lbDisplay.DrawFlags & Lb_TEXT_ONE_COLOR)
                       ? (float)lbDisplay.DrawColour : (float)m_current_dbc_colour0;
        }
        else
        {
            forced_idx = (lbDisplay.DrawFlags & Lb_TEXT_ONE_COLOR)
                       ? (float)lbDisplay.DrawColour : -1.0f;
        }
                         
        int glyph_width = GenerateCharQuad(chr, current_x, screen_y, scale_factor,
                                           forced_idx, &vertices[vertex_count]);
        if (glyph_width > 0)
        {
            vertex_count += 6;
            current_x += (float)glyph_width * scale_factor;
        }
    }

    if (vertex_count > 0)
    {
        m_vertex_batch.insert(m_vertex_batch.end(), vertices, vertices + vertex_count);
    }
}

int GLTextRenderer::GenerateCharQuad(unsigned long chr, float x, float y, float scale_factor,
                                      float forced_palette_idx, TextVertex* verts)
{
    const FontGlyph* glyph = nullptr;
    if (m_active_dbc_atlas)
        glyph = m_active_dbc_atlas->GetGlyph(chr);
    else if (m_active_atlas)
        glyph = m_active_atlas->GetGlyph(chr);

    if (!glyph)
    {
        // Log the first few missing glyphs for diagnosis
        static int missing_glyph_count = 0;
        if (missing_glyph_count < 10) {
            WARNLOG("GLTextRenderer: No glyph for character %lu (0x%02lX), atlas=%p dbc_atlas=%p", 
                    chr, chr, (void*)m_active_atlas, (void*)m_active_dbc_atlas);
            missing_glyph_count++;
        }
        return 0;
    }

    float char_width  = glyph->width  * scale_factor;
    float char_height = glyph->height * scale_factor;

    float ndc_x0, ndc_y0, ndc_x1, ndc_y1;
    ScreenToNDC(x,              y,               &ndc_x0, &ndc_y0);
    ScreenToNDC(x + char_width, y + char_height, &ndc_x1, &ndc_y1);

    // Triangle 1: TL, TR, BR
    verts[0] = { ndc_x0, ndc_y0, glyph->u0, glyph->v0, forced_palette_idx };  // TL
    verts[1] = { ndc_x1, ndc_y0, glyph->u1, glyph->v0, forced_palette_idx };  // TR
    verts[2] = { ndc_x1, ndc_y1, glyph->u1, glyph->v1, forced_palette_idx };  // BR

    // Triangle 2: TL, BR, BL
    verts[3] = { ndc_x0, ndc_y0, glyph->u0, glyph->v0, forced_palette_idx };  // TL
    verts[4] = { ndc_x1, ndc_y1, glyph->u1, glyph->v1, forced_palette_idx };  // BR
    verts[5] = { ndc_x0, ndc_y1, glyph->u0, glyph->v1, forced_palette_idx };  // BL

    return glyph->width;
}

void GLTextRenderer::ScreenToNDC(float screen_x, float screen_y, float* ndc_x, float* ndc_y) const
{
    *ndc_x = (screen_x / (float)m_screen_width) * 2.0f - 1.0f;
    *ndc_y = 1.0f - (screen_y / (float)m_screen_height) * 2.0f;
}

void GLTextRenderer::UpdatePaletteTexture()
{
    // CRITICAL: Check if the game palette has been initialized.
    // lbPalette is loaded during level/menu initialization. If all entries are zero,
    // the palette isn't ready yet and we'd render invisible black text.
    bool palette_valid = false;
    for (int i = 0; i < 256 * 3; ++i)
    {
        if (lbPalette[i] != 0)
        {
            palette_valid = true;
            break;
        }
    }

    if (!palette_valid)
    {
        static bool warned = false;
        if (!warned)
        {
            WARNLOG("GLTextRenderer::UpdatePaletteTexture: lbPalette is all zeros - palette not loaded yet");
            warned = true;
        }
        // Don't upload an all-black palette - keep whatever was there before or skip rendering
        return;
    }

    // Convert game palette to RGB format for GPU
    unsigned char rgb_palette[256 * 3];
    for (int i = 0; i < 256; ++i)
    {
        // lbPalette is 6-bit per channel, convert to 8-bit
        rgb_palette[i*3 + 0] = (unsigned char)((int)lbPalette[i*3 + 0] << 2);
        rgb_palette[i*3 + 1] = (unsigned char)((int)lbPalette[i*3 + 1] << 2);  
        rgb_palette[i*3 + 2] = (unsigned char)((int)lbPalette[i*3 + 2] << 2);
    }

    // Log palette entries every frame to track corruption
    static int palette_frame_count = 0;
    palette_frame_count++;
    if ((palette_frame_count % 500) == 1) {  // Log every 500 frames
        SYNCLOG("GLTextRenderer: Palette entries at frame %d:", palette_frame_count);
        for (int i = 0; i < 10; ++i) {
            SYNCLOG("  [%d] = RGB(%d, %d, %d)", i,
                   rgb_palette[i*3 + 0], rgb_palette[i*3 + 1], rgb_palette[i*3 + 2]);
        }
    }

    glBindTexture(GL_TEXTURE_2D, m_palette_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb_palette);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED