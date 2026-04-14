/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererOpenGL.cpp
 *     OpenGL renderer backend.
 * @par Purpose:
 *     Presents the software-rendered 8-bit paletted framebuffer via OpenGL
 *     3.3 Core.  Uses a two-texture approach: an 8-bit index texture and a
 *     256-entry RGBA palette texture.  The fragment shader does the palette
 *     lookup so the upload is a single byte per pixel.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererOpenGL.h"
#include "renderer/RendererManager.h"
#include "renderer/opengl/GLTileAtlas.h"
#include "renderer/opengl/GLSpriteAtlas.h"
#include "renderer/opengl/GLWorldViewRenderer.h"
#include "renderer/opengl/GLShaderLoader.h"
#include "renderer/util/RenderDocAPI.h"
#include "kfx/profiling/KfxProfiling.h"

#include "bflib_video.h"    // lbDisplay, lbPaletteColors, MyScreenWidth/Height
#include "bflib_render.h"   // render_fade_tables
#include "platform.h"       // platform_create_gl_context / swap / destroy
#include "renderer/RenderPass_C.h"
#include "engine_textures.h" // update_animating_texture_maps()

#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <cstring>
#include "post_inc.h"

extern "C" { extern float g_palette_possession_tint; }

/******************************************************************************/
// Fullscreen quad: two triangles covering NDC [-1,1]
static const float k_quadVerts[] = {
    // pos (xy)   uv
    -1.f, -1.f,   0.f, 1.f,
     1.f, -1.f,   1.f, 1.f,
     1.f,  1.f,   1.f, 0.f,

    -1.f, -1.f,   0.f, 1.f,
     1.f,  1.f,   1.f, 0.f,
    -1.f,  1.f,   0.f, 0.f,
};

/******************************************************************************/

static unsigned int compile_shader(GLenum type, const char* src)
{
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        ERRORLOG("RendererOpenGL shader compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/******************************************************************************/

RendererOpenGL::RendererOpenGL() = default;

RendererOpenGL::~RendererOpenGL()
{
    Shutdown();
}

bool RendererOpenGL::Init()
{
    // Create GL context (SDL2-based on desktop; see platform_gl_sdl2.cpp)
    if (!platform_create_gl_context(lbWindow))
    {
        ERRORLOG("RendererOpenGL::Init: failed to create GL context: %s", SDL_GetError());
        return false;
    }

    // Load GL function pointers via glad
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        ERRORLOG("RendererOpenGL::Init: glad failed to load GL function pointers");
        platform_destroy_gl_context();
        return false;
    }

    // Detect RenderDoc (must be before any GL object creation).
    RenderDocAPI::Init();

    // Initialise Tracy GPU profiling context (requires GL function pointers).
    KFX_GPU_CTX_CREATE();

    if (!compile_shaders())
    {
        platform_destroy_gl_context();
        return false;
    }

    // ── Fullscreen palette-blit quad ─────────────────────────────────────────
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    KFX_GL_LABEL(GL_VERTEX_ARRAY, m_vao, "Blit/QuadVAO");
    KFX_GL_LABEL(GL_BUFFER, m_vbo, "Blit/QuadVBO");
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(k_quadVerts), k_quadVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // 8-bit index texture (sized to screen, filled each frame)
    m_stagingW = MyScreenWidth;
    m_stagingH = MyScreenHeight;
    m_stagingBuf = new uint8_t[m_stagingW * m_stagingH]();

    glGenTextures(1, &m_texIndex);
    KFX_GL_LABEL(GL_TEXTURE, m_texIndex, "Blit/StagingIndexTex");
    glBindTexture(GL_TEXTURE_2D, m_texIndex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_stagingW, m_stagingH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

    // Palette texture (256 RGBA entries)
    glGenTextures(1, &m_texPalette);
    KFX_GL_LABEL(GL_TEXTURE, m_texPalette, "Blit/PaletteTex");
    glBindTexture(GL_TEXTURE_1D, m_texPalette);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    upload_palette_texture();

    // Bind sampler uniforms
    glUseProgram(m_shader);
    glUniform1i(glGetUniformLocation(m_shader, "u_index"),   0);
    glUniform1i(glGetUniformLocation(m_shader, "u_palette"), 1);
    m_uTintFactor = glGetUniformLocation(m_shader, "u_tint_factor");

    // ── World-geometry GPU resources ─────────────────────────────────────────
    if (!init_fade_table_texture())
    {
        WARNLOG("RendererOpenGL: fade table texture init failed — world GPU renderer disabled");
        // Non-fatal: framebuffer blit still works
    }

    if (!init_tile_atlas())
    {
        WARNLOG("RendererOpenGL: tile atlas init failed — world GPU renderer disabled");
    }

    // ── Raw-image GPU blit (frontend background images) ────────────────────────
    // Compile shader: reuse palette_blit_vert.glsl + rawimage_blit_frag.glsl.
    // Fatal if shader compilation fails — no CPU fallback is permitted in GL mode.
    {
        std::string rv_src = get_embedded_shader_source("palette_blit_vert.glsl");
        std::string rf_src = get_embedded_shader_source("rawimage_blit_frag.glsl");
        if (rv_src.empty() || rf_src.empty())
        {
            ERRORLOG("RendererOpenGL::Init: rawimage blit shader source missing");
            platform_destroy_gl_context();
            return false;
        }
        unsigned int rv = compile_shader(GL_VERTEX_SHADER,   rv_src.c_str());
        unsigned int rf = compile_shader(GL_FRAGMENT_SHADER, rf_src.c_str());
        if (!rv || !rf)
        {
            if (rv) glDeleteShader(rv);
            if (rf) glDeleteShader(rf);
            ERRORLOG("RendererOpenGL::Init: rawimage blit shader compile failed");
            platform_destroy_gl_context();
            return false;
        }
        m_rawblit_shader = glCreateProgram();
        glAttachShader(m_rawblit_shader, rv);
        glAttachShader(m_rawblit_shader, rf);
        glLinkProgram(m_rawblit_shader);
        glDeleteShader(rv);
        glDeleteShader(rf);
        int ok = 0;
        glGetProgramiv(m_rawblit_shader, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[512];
            glGetProgramInfoLog(m_rawblit_shader, sizeof(log), nullptr, log);
            ERRORLOG("RendererOpenGL::Init: rawimage blit shader link error: %s", log);
            glDeleteProgram(m_rawblit_shader);
            m_rawblit_shader = 0;
            platform_destroy_gl_context();
            return false;
        }
        glUseProgram(m_rawblit_shader);
        glUniform1i(glGetUniformLocation(m_rawblit_shader, "u_index"),   0);
        glUniform1i(glGetUniformLocation(m_rawblit_shader, "u_palette"), 1);
        KFX_GL_LABEL(GL_PROGRAM, m_rawblit_shader, "RawBlit/Program");
    }

    // Raw-blit quad VAO/VBO — positions/UVs updated per blit in EndFrame().
    glGenVertexArrays(1, &m_rawblit_vao);
    glGenBuffers(1, &m_rawblit_vbo);
    KFX_GL_LABEL(GL_VERTEX_ARRAY, m_rawblit_vao, "RawBlit/QuadVAO");
    KFX_GL_LABEL(GL_BUFFER, m_rawblit_vbo, "RawBlit/QuadVBO");
    glBindVertexArray(m_rawblit_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_rawblit_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Raw-blit index texture — uploads source image data per blit.
    glGenTextures(1, &m_rawblit_tex);
    KFX_GL_LABEL(GL_TEXTURE, m_rawblit_tex, "RawBlit/IndexTex");
    glBindTexture(GL_TEXTURE_2D, m_rawblit_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Sprite atlas for UI panel sprites (gui_panel_sprites, button_sprites).
    // Sheets are added in create_ui_renderer() after this returns.
    m_sprite_atlas = new GLSpriteAtlas();
    if (!m_sprite_atlas->Init())
    {
        WARNLOG("RendererOpenGL: UI sprite atlas init failed — panel sprites will use staging buffer");
        delete m_sprite_atlas;
        m_sprite_atlas = nullptr;
    }
    else
    {
        SYNCLOG("RendererOpenGL: UI sprite atlas initialized successfully");
    }

    return true;
}

void RendererOpenGL::Shutdown()
{
    delete m_tile_atlas;
    m_tile_atlas = nullptr;

    delete m_sprite_atlas;
    m_sprite_atlas = nullptr;

    delete[] m_stagingBuf;
    m_stagingBuf = nullptr;

    if (m_vao)     { glDeleteVertexArrays(1, &m_vao);  m_vao = 0; }
    if (m_vbo)     { glDeleteBuffers(1, &m_vbo);        m_vbo = 0; }
    if (m_shader)  { glDeleteProgram(m_shader);          m_shader = 0; }
    if (m_tintProg){ glDeleteProgram(m_tintProg);        m_tintProg = 0; }
    if (m_texIndex)   { glDeleteTextures(1, &m_texIndex);   m_texIndex = 0; }
    if (m_texPalette) { glDeleteTextures(1, &m_texPalette); m_texPalette = 0; }
    if (m_texFade)    { glDeleteTextures(1, &m_texFade);    m_texFade = 0; }
    if (m_rawblit_shader) { glDeleteProgram(m_rawblit_shader);            m_rawblit_shader = 0; }
    if (m_rawblit_vao)    { glDeleteVertexArrays(1, &m_rawblit_vao);      m_rawblit_vao = 0; }
    if (m_rawblit_vbo)    { glDeleteBuffers(1, &m_rawblit_vbo);           m_rawblit_vbo = 0; }
    if (m_rawblit_tex)    { glDeleteTextures(1, &m_rawblit_tex);          m_rawblit_tex = 0; }

    platform_destroy_gl_context();
}

bool RendererOpenGL::BeginFrame()
{
    // Idempotent: multiple LbScreenLock calls per frame must not clear the UI queue again.
    // The staging buffer has the same guard via m_staging_cleared / LockFramebuffer().
    if (m_frame_begun) return true;
    m_frame_begun = true;

    // Lazy-retry resources that depend on game data loaded after Init().
    if (!m_texFade && render_fade_tables)
        init_fade_table_texture();
    if (m_tile_atlas && !m_tile_atlas->IsInitialized())
        init_tile_atlas();

    // Re-upload the animated tile atlas rows only when the game-logic tick has
    // actually advanced the animation (update_animating_texture_maps() called from
    // main.cpp swaps block_ptrs pointers, changing the sentinel value).
    // This avoids expensive palette→RGBA8 decodes + GPU uploads at the render
    // frame rate (60 fps); uploads now happen at the game-tick rate only.
    if (m_tile_atlas && m_tile_atlas->IsInitialized())
    {
        const uint8_t* anim_sentinel = block_ptrs[TEXTURE_BLOCKS_STAT_COUNT_A];
        if (anim_sentinel != m_last_anim_sentinel)
        {
            m_tile_atlas->UpdateAnimatedTiles();
            m_last_anim_sentinel = anim_sentinel;
        }
    }

    // Tag the next RenderDoc capture with a monotonic frame number so captures
    // are easy to compare (RenderDoc shows this in the capture list).
    if (RenderDocAPI::IsActive())
    {
        static int s_frame = 0;
        static char s_title[64];
        snprintf(s_title, sizeof(s_title), "KFX Frame %d", s_frame++);
        RenderDocAPI::SetCaptureTitle(s_title);
    }

    RenderPass_BeginFrame();
    UIRenderer_Clear();
    return true;
}

/** ============================================================
 *  Frame composition pipeline (bottom-to-top draw order)
 *  ============================================================
 *
 *  Step 1 — glClear()
 *       Wipes the colour + depth buffer to the GL clear colour (black).
 *       Everything below composites on top of this.
 *
 *  Step 2 — GPUFlushNow()                      [GLWorldViewRenderer]
 *       Replays the bucket-walk command list:
 *         CMD_TILES    — indexed-colour tile meshes (world geometry)
 *         CMD_SHADOWS  — per-creature floor-shadow quads
 *         CMD_SPRITES  — depth-sorted 3D billboard sprites (creatures, objects)
 *         CMD_WORLDTEXT — floating 3D text (gold numbers above piles)
 *       Hardware depth test keeps geometry correctly occluded.
 *       Shade is per-vertex Gouraud from the DK fade table (mode 0) or
 *       per-fragment lightmap (mode 1, Phase 3+).
 *
 *  Step 3 — UIRenderer_FlushBack()               [GLUIRenderer, layer=0]
 *       Flushes UIQuads tagged layer=0 (back):
 *         mode 10 — tiled slab-background quads (sidebar panel fill)
 *         mode  3 — solid-colour quads (progress bar trough fills)
 *         mode  0 — palette-indexed atlas sprites (back-layer panel sprites)
 *       Must land BEFORE the staging blit (Step 4) so that CPU-drawn text
 *       from the staging buffer composites on top of panel backgrounds.
 *       (GPU-active path skips Step 4, but the ordering still matters for
 *       slab backgrounds vs. front-layer sprites.)
 *
 *  Step 4 — CPU staging blit                     [skipped when GPU active]
 *       Uploads m_stagingBuf (raw 8-bit palette indices, 1 byte per pixel)
 *       to m_texIndex (GL_R8 GL_TEXTURE_2D).  Index 0 = transparent (alpha 0).
 *       Draws a fullscreen quad through the palette-lookup shader.
 *       Non-zero pixels from CPU-drawn UI (minimap digits, text, gold totals)
 *       composite over Step 3.  Skipped entirely when GPU world-view is active
 *       because the staging buffer is all-zeros (nothing writes to it).
 *
 *  Step 5 — TextRenderer_Flush()                 [GLTextRenderer, non-overlay]
 *       Renders all non-overlay deferred text BEFORE sprite layers so the
 *       tooltip box (layer 3) can composite on top of event messages etc.
 *
 *  Step 6 — UIRenderer_FlushFront()              [GLUIRenderer, layers 1→2→3]
 *       Layer 1: panel/button atlas sprites, escape menu, battler icons, gems.
 *       Layer 2: world-depth-tested sprites (creature status flowers, payday digits).
 *       Layer 3: top-overlay sprites (tooltip box, slab selector) — rendered last.
 *
 *  Step 7 — TextRenderer_FlushOverlay()          [GLTextRenderer, overlay]
 *       Renders overlay-tagged text (tooltip text) AFTER layer-3 sprites so it
 *       is readable over the tooltip box background.
 *
 *  Step 8 — UIRenderer_FlushHandSprites()        [GLUIRenderer, hand/cursor]
 *       Cursor drawn last so it is always on top of all text and sprites.
 *
 *  Step 9 — platform_swap_gl_buffers()
 *       Flips the back buffer to the display.
 *
 *  RenderDoc tip: each Step above corresponds to one or more draw calls.
 *  The GPUFlushNow tile meshes are one draw per tile variation (all-opaque);
 *  shadows are one draw call each; sprites are batched by bucket depth.
 *  UI quads are one draw call per render-mode pass per layer.
 *  ============================================================ */
void RendererOpenGL::EndFrame()
{
    // Upload palette unconditionally — it may have changed this frame via LbPaletteSet.
    // Palette switches happen rarely (level load, possession), so the overhead of a
    // 1 KB CPU expand + glTexSubImage1D is negligible compared to other frame work.
    upload_palette_texture();

    // Restore depth mask before clearing — GPUFlushNow() ends with
    // glDepthMask(GL_FALSE) to protect against accidental depth writes during
    // the overlay blit, but glClear(GL_DEPTH_BUFFER_BIT) respects the mask.
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Cache the world-active state BEFORE GPUFlushNow clears the per-frame flag.
    // IsGpuAccelerated() returns true only when BeginWorldPass was called this frame;
    // GPUFlushNow resets the flag at the end of its execution so we must read it first.
    const TbBool world_gpu_active = WorldViewRenderer_IsGpuActive();

    // Flush GPU world geometry + depth-correct sprites.
    // Runs BEFORE the staging buffer upload so both layers composite correctly.
    if (m_world_renderer)
        m_world_renderer->GPUFlushNow();

    // Flush layer-0 (back) GPU UI elements — sidebar background panels.
    UIRenderer_FlushBack();

    // Raw-image GPU blit — frontend background images (legal, loading, menu bg,
    // map bg, torture, etc.).  Queued by BlitRaw8GPU() during the frame; drawn
    // here as an opaque quad so that the staging-blit overlay (which composites
    // any CPU-drawn menu sprites above index 0) lands on top.
    if (m_rawblit_pending)
    {
        const RawBlitCmd& cmd = m_rawblit_cmd;

        // Ensure full-screen viewport and no depth interaction.
        // UIRenderer_FlushBack() returns early (no quads) on pure-frontend frames
        // without disabling depth test, so we must guard here explicitly.
        glViewport(0, 0, m_stagingW, m_stagingH);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        // Upload source image to the raw-blit index texture.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_rawblit_tex);
        if (cmd.src_w != m_rawblit_tex_w || cmd.src_h != m_rawblit_tex_h)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, cmd.src_w, cmd.src_h,
                         0, GL_RED, GL_UNSIGNED_BYTE, cmd.src_buf);
            m_rawblit_tex_w = cmd.src_w;
            m_rawblit_tex_h = cmd.src_h;
        }
        else
        {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cmd.src_w, cmd.src_h,
                            GL_RED, GL_UNSIGNED_BYTE, cmd.src_buf);
        }

        // Palette texture already on unit 1 from upload_palette_texture() above.
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_1D, m_texPalette);

        // Build a rect quad in NDC covering [dst_x..dst_x+dst_w] x [dst_y..dst_y+dst_h].
        // Screen-space: y increases downward; NDC: y increases upward.
        const float sw = (float)m_stagingW;
        const float sh = (float)m_stagingH;
        const float x0 = (float)cmd.dst_x             / sw * 2.0f - 1.0f;
        const float x1 = (float)(cmd.dst_x + cmd.dst_w) / sw * 2.0f - 1.0f;
        const float y0 = 1.0f - (float)cmd.dst_y             / sh * 2.0f;  // top NDC
        const float y1 = 1.0f - (float)(cmd.dst_y + cmd.dst_h) / sh * 2.0f; // bottom NDC

        // Two triangles; UV (0,0) = top-left, (1,1) = bottom-right.
        const float verts[6][4] = {
            { x0, y1,  0.f, 1.f },  // bottom-left
            { x1, y1,  1.f, 1.f },  // bottom-right
            { x1, y0,  1.f, 0.f },  // top-right
            { x0, y1,  0.f, 1.f },  // bottom-left
            { x1, y0,  1.f, 0.f },  // top-right
            { x0, y0,  0.f, 0.f },  // top-left
        };
        glBindVertexArray(m_rawblit_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_rawblit_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        glDisable(GL_BLEND);
        glUseProgram(m_rawblit_shader);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        // Restore depth mask and reset active texture unit to 0 so the
        // subsequent staging blit and other passes start from a clean state.
        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);

        m_rawblit_pending = false;
    }

    // When the GPU world-view renderer is active every drawing path (status panel,
    // GUI, text, sprites, shadows) is routed through GPU shaders / UIRenderer /
    // GLTextRenderer.  The staging buffer is all-zeros, so uploading + blitting it
    // is pure overhead (~1 MB upload + one draw call doing nothing).  Skip it.
    // Use the cached value from before GPUFlushNow so main-menu frames (where
    // BeginWorldPass was never called and the flag is false) always run the blit.
    if (!world_gpu_active)
    {
        // Full-screen viewport + no depth interaction for this 2D overlay pass.
        glViewport(0, 0, m_stagingW, m_stagingH);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        // Upload CPU framebuffer to index texture.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texIndex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_stagingW, m_stagingH, GL_RED, GL_UNSIGNED_BYTE, m_stagingBuf);

        // CPU framebuffer blit — palette index 0 is transparent so the GPU
        // back-layer sidebar panels show through, while non-zero CPU pixels
        // composite on top.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texIndex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_1D, m_texPalette);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(m_shader);
        glUniform1f(m_uTintFactor, g_palette_possession_tint);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
    }

    // Map-fade GPU compose pass — active during PVM_ParchFadeIn / ParchFadeOut.
    // GLMapFadePass::StepFadeIn/Out() records the current step without writing
    // to WScreen; this hook renders the native-resolution wipe quad on top of
    // the (empty) staging blit.  No-op for SoftwareMapFadePass.
    {
        IMapFadePass* mfp = RendererGetMapFadePass();
        if (mfp && mfp->HasGPUComposePass())
            mfp->RenderGPUComposePass();
    }

    // Flush layer-1 (front) GPU UI elements — escape menu, minimap, slab
    // selectors, power-hand — on top of everything else.
    UIRenderer_FlushFront();

    // Text on top of all sprites (sidebar labels, event messages, tooltips).
    TextRenderer_Flush();

    // Cursor drawn last — always on top of everything.
    UIRenderer_FlushHandSprites();

    // Screen-tint overlay — composites over all rendered layers (tiles, sprites,
    // UI, text). Driven by g_screen_tint set from palette-effect callbacks:
    // possession/pain (red), dungeon-heart death flash (white), zoom-to-heart.
    // No-op when alpha == 0 or no tint shader compiled.
    if (g_screen_tint[3] > 0.0f && m_tintProg)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(m_tintProg);
        glUniform4fv(m_uTintColor, 1, g_screen_tint);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
    }

    RenderPass_EndFrame();
    platform_swap_gl_buffers(lbWindow);

    // Collect pending GPU timer query results for Tracy GPU zones.
    KFX_GPU_COLLECT();
    // Mark the end of this rendered frame in Tracy's timeline.
    KFX_FRAMEMARK();

    m_staging_cleared = false; // next frame's first LockFramebuffer will clear the staging buffer
    m_frame_begun     = false; // allow BeginFrame to run fully on the next frame
}

uint8_t* RendererOpenGL::LockFramebuffer(int* out_pitch)
{
    // Clear once per frame on the first lock so old pixel data doesn't
    // accumulate.  Subsequent locks within the same frame are additive.
    if (!m_staging_cleared && m_stagingBuf)
    {
        memset(m_stagingBuf, 0, m_stagingW * m_stagingH);
        m_staging_cleared = true;
    }
    if (out_pitch)
        *out_pitch = m_stagingW;
    return m_stagingBuf;
}

void RendererOpenGL::UnlockFramebuffer()
{
    // Nothing to do — CPU writes go directly into m_stagingBuf.
}

bool RendererOpenGL::BlitRaw8GPU(int dst_width, int dst_height, int dst_x, int dst_y,
                                  const unsigned char* src_buf, int src_width, int src_height)
{
    if (!m_rawblit_shader)
    {
        // Shader failed to compile during Init().  This is a fatal misconfiguration
        // in GL mode — no CPU fallback is permitted.
        ERRORLOG("RendererOpenGL::BlitRaw8GPU: shader not compiled; GPU blit dropped (src %dx%d)",
                 src_width, src_height);
        return false;
    }
    if (!src_buf || src_width <= 0 || src_height <= 0)
    {
        ERRORLOG("RendererOpenGL::BlitRaw8GPU: invalid source (buf=%p w=%d h=%d)",
                 (const void*)src_buf, src_width, src_height);
        return false;
    }
    // Queue — executed in EndFrame() after UIRenderer_FlushBack().
    m_rawblit_cmd     = { src_buf, src_width, src_height,
                          dst_x, dst_y, dst_width, dst_height };
    m_rawblit_pending = true;
    return true;
}

const char* RendererOpenGL::GetName() const
{
    return "OpenGL";
}

bool RendererOpenGL::SupportsRuntimeSwitch() const
{
    return true;
}

IWorldViewRenderer* RendererOpenGL::GetWorldViewRenderer()
{
    return RendererGetWorldViewRenderer();
}

IMapFadePass* RendererOpenGL::GetMapFadePass()
{
    return RendererGetMapFadePass();
}

ITextRenderer* RendererOpenGL::GetTextRenderer()
{
    return RendererGetTextRenderer();
}

IUIRenderer* RendererOpenGL::GetUIRenderer()
{
    return RendererGetUIRenderer();
}

/******************************************************************************/


bool RendererOpenGL::compile_shaders()
{
    std::string vert_src = get_embedded_shader_source("palette_blit_vert.glsl");
    std::string frag_src = get_embedded_shader_source("palette_blit_frag.glsl");
    if (vert_src.empty() || frag_src.empty())
        return false;

    unsigned int vert = compile_shader(GL_VERTEX_SHADER,   vert_src.c_str());
    unsigned int frag = compile_shader(GL_FRAGMENT_SHADER, frag_src.c_str());
    if (!vert || !frag)
    {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return false;
    }

    m_shader = glCreateProgram();
    glAttachShader(m_shader, vert);
    glAttachShader(m_shader, frag);
    glLinkProgram(m_shader);
    glDeleteShader(vert);
    glDeleteShader(frag);

    int ok = 0;
    glGetProgramiv(m_shader, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(m_shader, sizeof(log), nullptr, log);
        ERRORLOG("RendererOpenGL shader link error: %s", log);
        glDeleteProgram(m_shader);
        m_shader = 0;
        return false;
    }

    // Compile the screen-tint overlay program (flat-colour fullscreen quad).
    // Non-fatal: if this fails, palette effects (possession, white flash, etc.)
    // won't show on the 3D world pass but gameplay is unaffected.
    {
        std::string tv_src = get_embedded_shader_source("screen_tint_vert.glsl");
        std::string tf_src = get_embedded_shader_source("screen_tint_frag.glsl");
        if (!tv_src.empty() && !tf_src.empty())
        {
            unsigned int tv = compile_shader(GL_VERTEX_SHADER,   tv_src.c_str());
            unsigned int tf = compile_shader(GL_FRAGMENT_SHADER, tf_src.c_str());
            if (tv && tf)
            {
                m_tintProg = glCreateProgram();
                glAttachShader(m_tintProg, tv);
                glAttachShader(m_tintProg, tf);
                glLinkProgram(m_tintProg);
                glDeleteShader(tv);
                glDeleteShader(tf);
                m_uTintColor = glGetUniformLocation(m_tintProg, "u_tint_color");
            }
            else
            {
                if (tv) glDeleteShader(tv);
                if (tf) glDeleteShader(tf);
                WARNLOG("RendererOpenGL: screen-tint shader compile failed");
            }
        }
    }

    return true;
}

void RendererOpenGL::upload_palette_texture()
{
    // lbPalette is unsigned char[768] (R, G, B per entry, 6-bit values)
    uint8_t rgba[256 * 4];
    for (int i = 0; i < 256; ++i)
    {
        rgba[i * 4 + 0] = (uint8_t)(lbPalette[i * 3 + 0] << 2);
        rgba[i * 4 + 1] = (uint8_t)(lbPalette[i * 3 + 1] << 2);
        rgba[i * 4 + 2] = (uint8_t)(lbPalette[i * 3 + 2] << 2);
        rgba[i * 4 + 3] = 255;
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_1D, m_texPalette);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 256, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

bool RendererOpenGL::init_fade_table_texture()
{
    if (!render_fade_tables)
    {
        WARNLOG("RendererOpenGL::init_fade_table_texture — render_fade_tables not ready");
        return false;
    }

    glGenTextures(1, &m_texFade);
    glBindTexture(GL_TEXTURE_2D, m_texFade);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // 256 palette indices × 256 shade levels = 65536 bytes
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 256, 0,
                 GL_RED, GL_UNSIGNED_BYTE, render_fade_tables);
    return true;
}

bool RendererOpenGL::init_tile_atlas()
{
    if (!m_tile_atlas)
        m_tile_atlas = new GLTileAtlas();
    return m_tile_atlas->Init();
}

void RendererOpenGL::InvalidateTileAtlas()
{
    if (m_tile_atlas)
        m_tile_atlas->Free();
    // Also reset the anim sentinel so the rebuilt atlas is immediately populated
    // with the animated tile strip on the next BeginFrame().
    m_last_anim_sentinel = nullptr;
}
