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
#include "renderer/RendererManager.h"
#include "renderer/RendererSettings.h"
#include "bflib_sprfnt.h"
#include "bflib_video.h"
#include "frontend.h"       // frontend_font[], winfont, font_sprites, frontstory_font
#include "front_credits.h"  // frontstory_font

#include <glad/glad.h>
#include <cstring>
#include <climits>
#include "kfx/profiling/KfxProfiling.h"
#include "renderer/VecMath.h"
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
    , m_batch_scissor_x(0)
    , m_batch_scissor_y(0)
    , m_batch_scissor_w(0)
    , m_batch_scissor_h(0)
    , m_batch_scissor_enabled(false)
    , m_font(nullptr)
    , m_justify_window{}
    , m_clip_window{}
    , m_dbc_font(nullptr)
    , m_dbc_colour0(0)
    , m_dbc_colour1(0)
    , m_dbc_enabled(false)
{
}

GLTextRenderer::~GLTextRenderer()
{
    Shutdown();
}

bool GLTextRenderer::Init()
{
    // Atlases are created lazily in Draw() per unique font pointer.

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

    // Palette texture is injected via SetPaletteTexture() after Init().
    // Shader compilation and uniform setup is deferred to CompileShaders(),
    // called by the bootstrapper in RendererManager::RendererInit().

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
    // m_palette_tex is NOT owned — shared from RendererOpenGL; do not delete.
    m_palette_tex = 0;
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

    // Cache uniform locations and set sampler bindings once.
    glUseProgram(m_shader_program);
    m_loc_viewport   = glGetUniformLocation(m_shader_program, "u_viewport");
    m_loc_font_atlas = glGetUniformLocation(m_shader_program, "u_font_atlas");
    m_loc_palette    = glGetUniformLocation(m_shader_program, "u_palette");
    m_loc_text_color = glGetUniformLocation(m_shader_program, "u_text_color");

    SYNCLOG("GLTextRenderer: Uniform locations - viewport=%d atlas=%d palette=%d color=%d",
            m_loc_viewport, m_loc_font_atlas, m_loc_palette, m_loc_text_color);

    if (m_loc_font_atlas >= 0)
        glUniform1i(m_loc_font_atlas, 0);  // GL_TEXTURE0
    if (m_loc_palette >= 0)
        glUniform1i(m_loc_palette, 1);     // GL_TEXTURE1
    glUseProgram(0);
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
            dbc_idx = (m_screen_width < 512) ? 0 : 1;
        } else {
            dbc_idx = (m_screen_width < 512) ? 0 : 1;
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
    if (x0 > m_screen_width)  x0 = m_screen_width;
    if (x1 > m_screen_width)  x1 = m_screen_width;
    if (y0 > m_screen_height) y0 = m_screen_height;
    if (y1 > m_screen_height) y1 = m_screen_height;
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

    // IR path: append to the write-side command buffer instead of the
    // internal DeferredDraw vector.  The render thread will reconstruct
    // DeferredDraw entries in ExecuteTextFromIR() and call Draw().
    if (m_text_write_cmds)
    {
        IRTextDrawCmd cmd;
        cmd.pos_x        = posx;
        cmd.pos_y        = posy;
        cmd.units_per_px = units_per_px;
        cmd.absolute     = 0;
        cmd.draw_colour  = lbDisplay.DrawColour;
        cmd.draw_flags   = lbDisplay.DrawFlags;
        cmd.justify_x    = m_justify_window.x;
        cmd.justify_y    = m_justify_window.y;
        cmd.justify_w    = m_justify_window.width;
        cmd.clip_x       = m_clip_window.x;
        cmd.clip_y       = m_clip_window.y;
        cmd.clip_w       = m_clip_window.width;
        cmd.clip_h       = m_clip_window.height;
        cmd.font         = m_font;
        cmd.dbc_font     = m_dbc_font;
        cmd.dbc_enabled  = (uint8_t)m_dbc_enabled;
        cmd.font_generation = RendererGetTextFontGeneration();
        cmd.dbc_colour0  = m_dbc_colour0;
        cmd.dbc_colour1  = m_dbc_colour1;
        cmd.SetText(text);
        m_text_write_cmds->draws.Append(cmd);
        return true;
    }

    // Write window closed: this call is on the game thread during a blocking
    // loop. Push to the mutex-guarded fallback queue, NOT m_pending — the
    // render thread owns m_pending and may be flushing it right now.
    {
        std::lock_guard<std::mutex> guard(m_fallback_mutex);
        m_pending_fallback.push_back({ posx, posy, units_per_px,
                              m_justify_window.x, m_justify_window.y, m_justify_window.width,
                              m_clip_window.x, m_clip_window.y, m_clip_window.width, m_clip_window.height,
                              lbDisplay.DrawColour, lbDisplay.DrawFlags,
                              text,
                              m_font,
                              m_dbc_font,
                              RendererGetTextFontGeneration(),
                              m_dbc_colour0, m_dbc_colour1, m_dbc_enabled });
    }
    return true;
}

TbBool GLTextRenderer::DrawTextAt(int32_t screen_x, int32_t screen_y, int32_t units_per_px, const char* text)
{
    return DrawTextResized(screen_x, screen_y, units_per_px, text);
}

void GLTextRenderer::SetTextCommandBuffers(TextCommandBuffers* cmds)
{
    m_text_write_cmds = cmds;
}

void GLTextRenderer::ExecuteTextFromIR(const TextCommandBuffers& cmds)
{
    // Translate IRTextDrawCmd entries into DeferredDraw entries.
    // Draw() is always called regardless — it flushes both IR-translated items
    // and any fallback content that arrived via m_pending before the write
    // window opened.  The old early-return (Size()==0 → return) was removed
    // because an empty IR buffer does not mean m_pending is empty.
    if (cmds.draws.Size() > 0)
    {
        const uint32_t current_gen = RendererGetTextFontGeneration();
        m_pending.reserve(m_pending.size() + cmds.draws.Size());
        for (const IRTextDrawCmd& c : cmds.draws)
        {
            // Drop stale commands that captured non-owning font pointers before
            // a sprite/font reload or renderer teardown transition.
            if (c.font_generation != current_gen)
                continue;

            m_pending.push_back({
                c.pos_x, c.pos_y, c.units_per_px,
                c.justify_x, c.justify_y, c.justify_w,
                c.clip_x, c.clip_y, c.clip_w, c.clip_h,
                c.draw_colour, c.draw_flags,
                std::string(c.text),
                reinterpret_cast<const struct TbSpriteSheet*>(c.font),
                reinterpret_cast<const struct AsianFont*>(c.dbc_font),
                c.font_generation,
                c.dbc_colour0, c.dbc_colour1,
                (TbBool)c.dbc_enabled
            });
        }
    }
    // Splice in any game-thread fallback draws accumulated since the last frame
    // (loading screens, fades). Brief lock: move the queue out, then release.
    {
        std::lock_guard<std::mutex> guard(m_fallback_mutex);
        if (!m_pending_fallback.empty())
        {
            m_pending.insert(m_pending.end(),
                             std::make_move_iterator(m_pending_fallback.begin()),
                             std::make_move_iterator(m_pending_fallback.end()));
            m_pending_fallback.clear();
        }
    }
    Draw();
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

void GLTextRenderer::Draw()
{
    KFX_ZONE("TextRenderer::Draw");
    KFX_GPU_ZONE("TextPass");
    KFX_GL_SCOPE(text_grp, "TextPass");
    if (m_pending.empty())
        return;

    if (m_screen_width <= 0 || m_screen_height <= 0)
        return;  // Screen size not yet set — skip rendering this frame.

    // Set up GL state once for all draws this frame
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_shader_program);
    glBindVertexArray(m_vao);

    if (m_loc_viewport >= 0)
        glUniform2f(m_loc_viewport, (float)m_screen_width, (float)m_screen_height);
    // Per-draw alpha is applied via ApplyTextColorUniform() below; reset to opaque for now.
    if (m_loc_text_color >= 0)
        glUniform4f(m_loc_text_color, 1.0f, 1.0f, 1.0f, 1.0f);

    // Bind the shared palette texture (already uploaded by RendererOpenGL::EndFrame)
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_palette_tex);

    // CRITICAL: Reset active atlas tracker because other renderers (minimap, sprites)
    // bind their textures to GL_TEXTURE0 between our Draw() calls, corrupting state.
    // We must rebind the font atlas texture for the first draw of each frame.
    m_active_atlas = nullptr;
    m_active_dbc_atlas = nullptr;
    // Reset batch accumulation state
    m_vertex_batch.clear();
    m_batch_scissor_enabled = false;
    m_batch_scissor_x = m_batch_scissor_y = m_batch_scissor_w = m_batch_scissor_h = 0;

    // Save globals that FlushSegment control codes will overwrite
    unsigned char               saved_colour     = m_text_draw_colour;
    unsigned short              saved_draw_flags = m_text_draw_flags;

    // CRITICAL FIX: Sort pending draws by font pointer to minimize atlas rebinding.
    // Main menu uses 3 different fonts (36BC0DC8, 36BC0E18, 36BC1368) and without
    // sorting, the code thrashes between atlases hundreds of times per frame.
    // Group DBC draws together so the DBC atlas is bound once per font.
    // stable_sort, not sort: draws that share a font compare equal, and an
    // unstable sort may reorder them differently every frame. Overlapping
    // draws (shadow/face passes, highlight overdraw) then swap paint order
    // frame-to-frame, which shows up as text flicker on the frontend menus.
    std::stable_sort(m_pending.begin(), m_pending.end(),
              [](const DeferredDraw& a, const DeferredDraw& b) {
                  if (a.dbc_enabled != b.dbc_enabled)
                      return (int)a.dbc_enabled < (int)b.dbc_enabled;
                  if (a.dbc_enabled)
                      return a.dbc_font < b.dbc_font;
                  return a.font < b.font;
              });

    const uint32_t current_gen = RendererGetTextFontGeneration();
    for (const DeferredDraw& d : m_pending)
    {
        if (d.font_generation != current_gen) continue;
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
                            ERRORLOG("GLTextRenderer::Draw: failed to rebuild DBC atlas for font %p", d.dbc_font);
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
                        ERRORLOG("GLTextRenderer::Draw: failed to init DBC atlas for font %p", d.dbc_font);
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
                            ERRORLOG("GLTextRenderer::Draw: failed to rebuild atlas for font %p", d.font);
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
                        ERRORLOG("GLTextRenderer::Draw: failed to init atlas for font %p", d.font);
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

        // Set per-draw draw state so FlushSegment reads/writes
        // m_text_draw_flags/colour consistently for control codes.
        m_text_draw_flags  = d.draw_flags;
        m_text_draw_colour = d.draw_colour;
        ApplyTextColorUniform();

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

    // Restore draw state overwritten during layout
    m_text_draw_colour = saved_colour;
    m_text_draw_flags  = saved_draw_flags;

    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);
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
    // Line height in scaled pixels — used for underline geometry.
    const float line_h = (float)LineHeight() * scale_factor;

    for (const char* c = sbuf; c < ebuf && vertex_count <= 594; ++c)
    {
        unsigned char ch  = (unsigned char)*c;
        unsigned long chr = ch;

        // Non-breaking space (UTF-8: 0xC2 0xA0) — advance like a normal space
        if (ch == 0xC2 && (c + 1) < ebuf && (unsigned char)c[1] == 0xA0)
        {
            float prev_x = current_x;
            current_x += space_len;
            if (m_text_draw_flags & Lb_TEXT_UNDERLINE)
                AppendUnderlineRects(prev_x, current_x, screen_y, line_h);
            ++c;
            continue;
        }

        if (ch == ' ')
        {
            float prev_x = current_x;
            current_x += space_len;
            if (m_text_draw_flags & Lb_TEXT_UNDERLINE)
                AppendUnderlineRects(prev_x, current_x, screen_y, line_h);
            continue;
        }
        if (ch == '\t')
        {
            float prev_x = current_x;
            current_x += space_len * (float)LbTextGetSpacesPerTab();
            if (m_text_draw_flags & Lb_TEXT_UNDERLINE)
                AppendUnderlineRects(prev_x, current_x, screen_y, line_h);
            continue;
        }

        // Control codes 1–14: update m_text_draw_flags/colour (mirrors put_down_simpletext_sprites)
        if (ch < 15)
        {
            switch (ch)
            {
                case 1:  m_text_draw_flags ^= Lb_SPRITE_TRANSPAR4;   ApplyTextColorUniform(); break;
                case 2:  m_text_draw_flags ^= Lb_SPRITE_TRANSPAR8;   ApplyTextColorUniform(); break;
                case 3:  m_text_draw_flags ^= Lb_SPRITE_OUTLINE;     break;
                case 4:  m_text_draw_flags ^= Lb_SPRITE_FLIP_HORIZ;  break;
                case 5:  m_text_draw_flags ^= Lb_SPRITE_FLIP_VERTIC; break;
                case 11: m_text_draw_flags ^= Lb_TEXT_UNDERLINE;     break;
                case 12: m_text_draw_flags ^= Lb_TEXT_ONE_COLOR;     break;
                case 14:
                    ++c;
                    if (c < ebuf) m_text_draw_colour = (unsigned char)*c;
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
            forced_idx = (m_text_draw_flags & Lb_TEXT_ONE_COLOR)
                       ? (float)m_text_draw_colour : (float)m_current_dbc_colour0;
        }
        else
        {
            forced_idx = (m_text_draw_flags & Lb_TEXT_ONE_COLOR)
                       ? (float)m_text_draw_colour : -1.0f;
        }
                         
        int glyph_width = GenerateCharQuad(chr, current_x, screen_y, scale_factor,
                                           forced_idx, &vertices[vertex_count]);
        if (glyph_width > 0)
        {
            vertex_count += 6;
            float prev_x = current_x;
            current_x += (float)glyph_width * scale_factor;
            if (m_text_draw_flags & Lb_TEXT_UNDERLINE)
                AppendUnderlineRects(prev_x, current_x, screen_y, line_h);
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

void GLTextRenderer::AppendUnderlineRect(float x0, float x1, float y0, float y1, float palette_idx)
{
    // UV.x = -1 triggers the solid-colour sentinel in the fragment shader.
    const float su = -1.0f, sv = 0.0f;
    float nx0, ny0, nx1, ny1;
    ScreenToNDC(x0, y0, &nx0, &ny0);
    ScreenToNDC(x1, y1, &nx1, &ny1);
    TextVertex verts[6] = {
        { nx0, ny0, su, sv, palette_idx },  // TL
        { nx1, ny0, su, sv, palette_idx },  // TR
        { nx1, ny1, su, sv, palette_idx },  // BR
        { nx0, ny0, su, sv, palette_idx },  // TL
        { nx1, ny1, su, sv, palette_idx },  // BR
        { nx0, ny1, su, sv, palette_idx },  // BL
    };
    m_vertex_batch.insert(m_vertex_batch.end(), verts, verts + 6);
}

void GLTextRenderer::AppendUnderlineRects(float x0, float x1, float screen_y, float line_h)
{
    // Mirror LbDrawCharUnderline() (bflib_sprfnt.c:106).
    // Lines are drawn bottom-up: shadow first (if active), then main.
    // Double underline when line_h > DOUBLE_UNDERLINE_BOUND (16).
    const float DUB = 16.0f;
    float h = line_h;

    if ((lbDisplay.DrawFlags & Lb_TEXT_UNDERLNSHADOW) != 0)
    {
        float sx = (line_h > 2.0f * DUB) ? 2.0f : 1.0f;
        float pi  = (float)(unsigned char)lbDisplayEx.ShadowColour;
        AppendUnderlineRect(x0 + sx, x1 + sx, screen_y + h, screen_y + h + 1.0f, pi);
        h -= 1.0f;
        if (line_h > DUB) {
            AppendUnderlineRect(x0 + sx, x1 + sx, screen_y + h, screen_y + h + 1.0f, pi);
            h -= 1.0f;
        }
    }
    float pi = (float)(unsigned char)lbDisplay.DrawColour;
    AppendUnderlineRect(x0, x1, screen_y + h, screen_y + h + 1.0f, pi);
    h -= 1.0f;
    if (line_h > DUB) {
        AppendUnderlineRect(x0, x1, screen_y + h, screen_y + h + 1.0f, pi);
    }
}

void GLTextRenderer::ScreenToNDC(float screen_x, float screen_y, float* ndc_x, float* ndc_y) const
{
    Vec2f ndc = ::ScreenToNDC(screen_x, screen_y, (float)m_screen_width, (float)m_screen_height);
    *ndc_x = ndc.x;
    *ndc_y = ndc.y;
}

void GLTextRenderer::ApplyTextColorUniform()
{
    if (m_loc_text_color < 0) return;
    // Flush any pending vertices accumulated with the previous alpha before updating
    // the uniform — GL uniforms are program-global, so pending batch vertices would
    // otherwise be drawn with the new alpha when they are eventually flushed.
    FlushBatch();
    float alpha = 1.0f;
    // Priority matches software renderer (bflib_vidraw.c): TRANSPAR4 is checked first.
    if (m_text_draw_flags & Lb_SPRITE_TRANSPAR4)
        alpha = g_renderer_settings.transpar4_alpha;
    else if (m_text_draw_flags & Lb_SPRITE_TRANSPAR8)
        alpha = g_renderer_settings.transpar8_alpha;
    glUniform4f(m_loc_text_color, 1.0f, 1.0f, 1.0f, alpha);
}

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
