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
#include "bflib_sprfnt.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "globals.h"

#include <glad/glad.h>
#include <cstring>
#include "post_inc.h"

/******************************************************************************/

GLTextRenderer::GLTextRenderer()
    : m_current_font(nullptr)
    , m_font_atlas(nullptr)
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
    // Create font atlas
    m_font_atlas = new GLFontAtlas();
    if (!m_font_atlas)
    {
        ERRORLOG("GLTextRenderer: failed to create font atlas");
        return false;
    }

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
    if (m_font_atlas)
    {
        delete m_font_atlas;
        m_font_atlas = nullptr;
    }

    if (m_shader_program) { glDeleteProgram(m_shader_program); m_shader_program = 0; }
    if (m_vao)            { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo)            { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_palette_tex)    { glDeleteTextures(1, &m_palette_tex); m_palette_tex = 0; }
}

bool GLTextRenderer::CompileShaders()
{
    static const char* vert_src =
        "#version 330 core\n"
        "layout(location = 0) in vec2 a_pos;\n"
        "layout(location = 1) in vec2 a_uv;\n"
        "layout(location = 2) in float a_forced_idx;\n"
        "uniform vec2 u_viewport;\n"
        "uniform vec4 u_text_color;\n"
        "out vec2 v_uv;\n"
        "out float v_forced_idx;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "    v_uv = a_uv;\n"
        "    v_forced_idx = a_forced_idx;\n"
        "}\n";

    static const char* frag_src =
        "#version 330 core\n"
        "in vec2 v_uv;\n"
        "in float v_forced_idx;\n"
        "uniform sampler2D u_font_atlas;\n"
        "uniform sampler2D u_palette;\n"
        "uniform vec4 u_text_color;\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    vec4 atlas_sample = texture(u_font_atlas, v_uv);\n"
        "    float alpha = atlas_sample.a;\n"
        "    if (alpha < 0.1) discard;\n"
        "    float palette_u;\n"
        "    if (v_forced_idx >= 0.0) {\n"
        "        palette_u = (v_forced_idx + 0.5) / 256.0;\n"
        "    } else {\n"
        "        float raw_idx = atlas_sample.r * 255.0;\n"
        "        palette_u = (raw_idx + 0.5) / 256.0;\n"
        "    }\n"
        "    vec3 palette_color = texture(u_palette, vec2(palette_u, 0.5)).rgb;\n"
        "    vec3 final_color = palette_color * u_text_color.rgb;\n"
        "    FragColor = vec4(final_color, alpha * u_text_color.a);\n"
        "}\n";

    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vert_src, nullptr);
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
    glShaderSource(frag, 1, &frag_src, nullptr);
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

bool GLTextRenderer::SetFont(const struct TbSpriteSheet* font_sheet)
{
    if (!m_font_atlas)
        return false;

    // Shutdown current atlas and initialize with new font
    m_font_atlas->Shutdown();
    return m_font_atlas->Init(font_sheet);
}

void GLTextRenderer::SetScreenSize(int width, int height)
{
    m_screen_width = width;
    m_screen_height = height;
}

TbBool GLTextRenderer::DrawTextResized(int posx, int posy, int units_per_px, const char* text)
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
    if (m_pending.empty() || !m_font_atlas)
    {
        static int s_diag = 0;
        if ((s_diag++ % 300) == 0 && !m_pending.empty())
            SYNCLOG("GLTextRenderer::Flush: %d pending draws but no font_atlas!", (int)m_pending.size());
        m_pending.clear();
        return;
    }

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

    // Save globals that the layout engine and FlushSegment will overwrite
    const struct TbSpriteSheet* saved_font      = lbFontPtr;
    unsigned char               saved_colour     = lbDisplay.DrawColour;
    unsigned short              saved_draw_flags = lbDisplay.DrawFlags;

    for (const DeferredDraw& d : m_pending)
    {
        if (!d.font) continue;

        // Switch font atlas when the font changes between queued draws.
        // IMPORTANT: set active unit to GL_TEXTURE0 before calling Init() so that
        // PackAndUploadAtlas's glBindTexture / final glBindTexture(0) ops do NOT
        // disturb the palette binding on GL_TEXTURE1.
        if (d.font != m_current_font)
        {
            glActiveTexture(GL_TEXTURE0);
            m_font_atlas->Shutdown();
            if (!m_font_atlas->Init(d.font))
            {
                ERRORLOG("GLTextRenderer::Flush: failed to init atlas for font");
                continue;
            }
            m_current_font = d.font;
        }

        if (!m_font_atlas->IsInitialized()) continue;

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_font_atlas->GetTextureID());

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
    if (!sbuf || sbuf >= ebuf || !m_font_atlas || !m_font_atlas->IsInitialized())
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
    const FontGlyph* glyph = m_font_atlas->GetGlyph(chr);
    if (!glyph)
    {
        WARNLOG("GLTextRenderer: No glyph for character %lu", chr);
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
    // Convert game palette to RGB format for GPU
    unsigned char rgb_palette[256 * 3];
    for (int i = 0; i < 256; ++i)
    {
        // lbPalette is 6-bit per channel, convert to 8-bit
        rgb_palette[i*3 + 0] = (unsigned char)((int)lbPalette[i*3 + 0] << 2);
        rgb_palette[i*3 + 1] = (unsigned char)((int)lbPalette[i*3 + 1] << 2);  
        rgb_palette[i*3 + 2] = (unsigned char)((int)lbPalette[i*3 + 2] << 2);
    }

    glBindTexture(GL_TEXTURE_2D, m_palette_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb_palette);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED