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
#include "renderer/opengl/GLShaders.h"
#include "bflib_sprfnt.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "globals.h"

#include <glad/glad.h>
#include <cstring>
#include "renderer/opengl/KfxProfiling.h"
#include "post_inc.h"

/******************************************************************************/

GLTextRenderer::GLTextRenderer()
    : m_active_atlas(nullptr)
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

    // Allocate dynamic buffer for text vertices (reserve space for ~100 characters)
    glBufferData(GL_ARRAY_BUFFER, 600 * sizeof(TextVertex), nullptr, GL_DYNAMIC_DRAW);

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

    SYNCLOG("GLTextRenderer: initialized");
    return true;
}

void GLTextRenderer::Shutdown()
{
    for (auto& kv : m_atlas_cache)
        delete kv.second;
    m_atlas_cache.clear();
    m_active_atlas = nullptr;

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
    // Phase 1: delegate to the global-based path until Phase 4 owns state internally.
    LbTextSetFont(font);
}

void GLTextRenderer::SetWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
    LbTextSetWindow(x, y, w, h);
}

void GLTextRenderer::SetJustifyWindow(int32_t x, int32_t y, int32_t w)
{
    LbTextSetJustifyWindow(x, y, w);
}

void GLTextRenderer::SetClipWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
    LbTextSetClipWindow(x, y, w, h);
}

void GLTextRenderer::GetJustifyWindow(int32_t* x, int32_t* y, int32_t* w) const
{
    LbTextGetJustifyWindow(reinterpret_cast<int*>(x),
                           reinterpret_cast<int*>(y),
                           reinterpret_cast<int*>(w));
}

void GLTextRenderer::GetClipWindow(int32_t* x, int32_t* y, int32_t* w, int32_t* h) const
{
    LbTextGetClipWindow(reinterpret_cast<int*>(x),
                        reinterpret_cast<int*>(y),
                        reinterpret_cast<int*>(w),
                        reinterpret_cast<int*>(h));
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

    // Capture all text-window and display state at call time.
    // Actual GL draws happen in Flush(), called by RendererOpenGL::EndFrame()
    // after the staging-buffer blit so text composites correctly over WScreen content.
    int wnd_x = 0, wnd_y = 0, wnd_width = 0;
    LbTextGetJustifyWindow(&wnd_x, &wnd_y, &wnd_width);
    int clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;
    LbTextGetClipWindow(&clip_x, &clip_y, &clip_w, &clip_h);
    m_pending.push_back({ posx, posy, units_per_px,
                          wnd_x, wnd_y, wnd_width,
                          clip_x, clip_y, clip_w, clip_h,
                          lbDisplay.DrawColour, lbDisplay.DrawFlags,
                          text, lbFontPtr });
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
    return LbTextLineHeight();
}

int32_t GLTextRenderer::CharWidth(uint32_t chr)
{
    return LbTextCharWidth(static_cast<long>(chr));
}

int32_t GLTextRenderer::CharWidthScaled(uint32_t chr, int32_t units_per_px)
{
    return LbTextCharWidthM(static_cast<long>(chr), units_per_px);
}

int32_t GLTextRenderer::StringWidth(const char* text)
{
    return LbTextStringWidth(text);
}

int32_t GLTextRenderer::StringWidthScaled(const char* text, int32_t units_per_px)
{
    return LbTextStringWidthM(text, units_per_px);
}

int32_t GLTextRenderer::WordWidth(const char* str)
{
    return LbTextWordWidth(str);
}

int32_t GLTextRenderer::WordWidthScaled(const char* str, int32_t units_per_px)
{
    return LbTextWordWidthM(str, units_per_px);
}

int32_t GLTextRenderer::TextHeight(const char* text)
{
    return LbTextHeight(text);
}

int32_t GLTextRenderer::StringHeight(int32_t units_per_px, const char* text)
{
    return static_cast<int32_t>(text_string_height(units_per_px, text));
}

void GLTextRenderer::gl_draw_segment(const char* sbuf, const char* ebuf,
                                      long x, long y, long space_len,
                                      int units_per_px, void* userdata)
{
    GLTextRenderer* self = static_cast<GLTextRenderer*>(userdata);
    int clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;
    LbTextGetClipWindow(&clip_x, &clip_y, &clip_w, &clip_h);
    float scale = (float)units_per_px / 16.0f;
    self->FlushSegment(sbuf, ebuf,
                       (float)(clip_x + x), (float)(clip_y + y),
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

    // Save globals that the layout engine and FlushSegment will overwrite
    const struct TbSpriteSheet* saved_font      = lbFontPtr;
    unsigned char               saved_colour     = lbDisplay.DrawColour;
    unsigned short              saved_draw_flags = lbDisplay.DrawFlags;

    // CRITICAL FIX: Sort pending draws by font pointer to minimize atlas rebinding.
    // Main menu uses 3 different fonts (36BC0DC8, 36BC0E18, 36BC1368) and without
    // sorting, the code thrashes between atlases hundreds of times per frame.
    std::sort(m_pending.begin(), m_pending.end(),
              [](const DeferredDraw& a, const DeferredDraw& b) {
                  return a.font < b.font;
              });

    for (const DeferredDraw& d : m_pending)
    {
        if (!d.font) continue;

        // Look up the cached atlas for this font; build it on first use.
        // IMPORTANT: activate GL_TEXTURE0 before Init() so PackAndUploadAtlas's
        // glBindTexture calls don't disturb the palette binding on GL_TEXTURE1.
        GLFontAtlas* atlas = nullptr;
        {
            auto it = m_atlas_cache.find(d.font);
            if (it != m_atlas_cache.end())
            {
                atlas = it->second;
                // Rebuild if the sprite sheet changed since the atlas was built
                // (font loaded/reloaded after the atlas was first created from empty data).
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
                        m_active_atlas = nullptr; // force rebind since texture was recreated
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

        // Only rebind the texture when the atlas actually changes.
        if (atlas != m_active_atlas)
        {
            SYNCLOG("GLTextRenderer: Rebinding atlas from %p to %p for font %p", 
                   m_active_atlas, atlas, d.font);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, atlas->GetTextureID());
            m_active_atlas = atlas;
        }

        // Expose draw state as globals so the layout engine and FlushSegment
        // read/write them consistently (same as the software path).
        lbFontPtr            = const_cast<struct TbSpriteSheet*>(d.font);
        lbDisplay.DrawColour = d.draw_colour;
        lbDisplay.DrawFlags  = d.draw_flags;
        LbTextSetJustifyWindow(d.wnd_x, d.wnd_y, d.wnd_width);
        LbTextSetClipWindow(d.clip_x, d.clip_y, d.clip_w, d.clip_h);

        // Scissor to the captured clip window
        if (d.clip_w > 0 && d.clip_h > 0)
        {
            glEnable(GL_SCISSOR_TEST);
            int gl_y = m_screen_height - (d.clip_y + d.clip_h);
            glScissor(d.clip_x, gl_y, d.clip_w, d.clip_h);
        }

        // Shared paragraph layout engine — calls gl_draw_segment once per
        // justified line segment, which calls FlushSegment to emit quads.
        LbTextLayout(d.posx, d.posy, d.units_per_px, d.text.c_str(),
                     gl_draw_segment, this);

        glDisable(GL_SCISSOR_TEST);
    }

    m_pending.clear();

    // Restore globals overwritten during layout
    lbFontPtr            = saved_font;
    lbDisplay.DrawColour = saved_colour;
    lbDisplay.DrawFlags  = saved_draw_flags;

    // Restore GL state
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
}

void GLTextRenderer::FlushSegment(const char* sbuf, const char* ebuf,
                                   float screen_x, float screen_y,
                                   float space_len, float scale_factor)
{
    KFX_ZONE("TextRenderer::FlushSegment");
    if (!sbuf || sbuf >= ebuf || !m_active_atlas || !m_active_atlas->IsInitialized())
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
        float forced_idx = (lbDisplay.DrawFlags & Lb_TEXT_ONE_COLOR)
                         ? (float)lbDisplay.DrawColour : -1.0f;

        // Log ALL characters being rendered to identify the source of black squares
        static int debug_char_count = 0;
        if (debug_char_count < 50) {
            SYNCLOG("GLTextRenderer: Rendering character %lu (0x%02lX) '%c' forced_idx=%.1f", 
                   chr, chr, (chr >= 32 && chr <= 126) ? (char)chr : '?', forced_idx);
            debug_char_count++;
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
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count * sizeof(TextVertex), vertices);
        glDrawArrays(GL_TRIANGLES, 0, vertex_count);
    }
}

int GLTextRenderer::GenerateCharQuad(unsigned long chr, float x, float y, float scale_factor,
                                      float forced_palette_idx, TextVertex* verts)
{
    const FontGlyph* glyph = m_active_atlas->GetGlyph(chr);
    if (!glyph)
    {
        // Log the first few missing glyphs for diagnosis
        static int missing_glyph_count = 0;
        if (missing_glyph_count < 10) {
            WARNLOG("GLTextRenderer: No glyph for character %lu (0x%02lX) in font %p", 
                    chr, chr, lbFontPtr);
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