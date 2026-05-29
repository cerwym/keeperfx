/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKUIRenderer.h
 *     Vulkan implementation of IUIRenderer.
 * @par Purpose:
 *     Mirrors GLUIRenderer feature-for-feature using VkCommandBuffer recording
 *     instead of immediate-mode GL calls.  Vertex data is written into a
 *     per-frame CPU→GPU vertex buffer (VMA CPU_TO_GPU, persistently mapped);
 *     texture uploads use VKStagingRing.
 *
 * @par Call sequence (per frame):
 *   1. SetCommandBuffer(cmd, frame_index)  — set active VkCommandBuffer
 *   2. FlushPendingInit(cmd, staging)      — upload dirty atlas pixels if needed
 *   3. PopulateFromIR(cmds, fs)            — translate IR → m_rt_quads[]/m_rt_lines[]
 *   4. DrawBack()                          — flush layer 0 (back panels, slab bg)
 *   5. [world pass]
 *   6. DrawFrontBase()                     — flush FBO quads + layer 1 + minimap
 *   7. [optional intermediate passes]
 *   8. DrawFrontOverlay()                  — flush layer 2 (world-depth) + layer 3 (top)
 */
/******************************************************************************/
#pragma once
#ifndef VKUIRENDERER_H
#define VKUIRENDERER_H

#ifdef RENDERER_VULKAN_ENABLED
#ifdef RENDERER_VK_SHADERC_AVAILABLE

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <vector>
#include <atomic>
#include <cstdint>

#include "renderer/IUIRenderer.h"
#include "renderer/UIBatch.h"
#include "renderer/UIVertex.h"
#include "renderer/ir/UICommands.h"
#include "renderer/FrameState.h"
#include "renderer/vulkan/VKStagingRing.h"
#include "renderer/vulkan/VKPipelineCache.h"
#include "renderer/vulkan/VKDescriptorLayout.h"
#include "renderer/vulkan/VKSpriteAtlas.h"
#include "renderer/vulkan/VKFontAtlas.h"
#include "renderer/opengl/GLSpriteAtlas.h"  // SpriteUV

/******************************************************************************/

/** C++ mirror of the unified Vulkan push-constant block (all shaders).
 *  Must match VK_PC_BLOCK in VKShaders.h exactly (std430 layout). */
struct VKPushConstants {
    float screen_w, screen_h;    // offset   0 — render target size
    float z_ndc;                 // offset   8
    float alpha;                 // offset  12
    float clip_rect[4];          // offset  16 — (x0, y0, x1, y1)
    float clip_radius;           // offset  32
    float clip_screen_h;         // offset  36
    float remap_row;             // offset  40
    float tint_factor;           // offset  44
    float center_map[2];         // offset  48
    float screen_center[2];      // offset  56
    float zoom_scale;            // offset  64
    float _pad0;                 // offset  68 — padding to align inv_map_size to 8 bytes
    float inv_map_size[2];       // offset  72
    float map_step;              // offset  80
    float fullbright;            // offset  84
    float ambient;               // offset  88
    float shade_scale;           // offset  92
    float shade_gamma;           // offset  96
    int   lighting_mode;         // offset 100
    int   darkness_mode;         // offset 104
    int   tile_filter;           // offset 108
    float missing_tile;          // offset 112
    float time;                  // offset 116
    float fog_speed;             // offset 120
    float fog_density;           // offset 124
    // (128 bytes total — exactly the Vulkan minimum push-constant size)
};
static_assert(sizeof(VKPushConstants) == 128, "VKPushConstants must be exactly 128 bytes");

/******************************************************************************/

/** Maximum UI vertices per frame window (back + front + world-depth + overlay). */
static constexpr size_t kVKUIMaxVertices = 1024 * 64;

/******************************************************************************/

class VKUIRenderer : public IUIRenderer
{
public:
    VKUIRenderer();
    ~VKUIRenderer() override;

    VKUIRenderer(const VKUIRenderer&)            = delete;
    VKUIRenderer& operator=(const VKUIRenderer&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /** Initialise GPU resources.
     *  @param device       Logical device.
     *  @param allocator    VMA allocator.
     *  @param pipelines    Shared pipeline cache (caller owns, must outlive this).
     *  @param descriptors  Per-frame descriptor pool manager (caller owns).
     *  @param set_layout   VkDescriptorSetLayout used by the pipeline cache.
     *  @return true on success. */
    bool Init(VkDevice device, VmaAllocator allocator,
              VKPipelineCache* pipelines,
              VKDescriptorLayout* descriptors,
              VkDescriptorSetLayout set_layout);

    void Shutdown();

    // ── Per-frame setup (must be called before any Draw*) ────────────────────

    /** Set the command buffer to record into and the current frame index.
     *  Called once per frame by RendererVulkan::EndFrame_VK() on the render thread. */
    void SetCommandBuffer(VkCommandBuffer cmd, int frame_index);

    /** Upload dirty atlas pixels to GPU.  Must be called before PopulateFromIR(). */
    void FlushPendingInit(VkCommandBuffer cmd, VKStagingRing& staging);

    // ── IUIRenderer overrides ─────────────────────────────────────────────────

    void SetPaletteSource(const uint8_t* palette) override { m_palette_data = palette; }
    void SetScreenSize(int w, int h) override { m_screen_w = w; m_screen_h = h; }
    void SetGameViewport(int x, int y, int w, int h) override;
    void SetLayer(int layer)      override { m_current_layer = layer; }
    void SetWorldDepth(float z)   override { m_world_z = z; m_world_depth_active = true; }
    void ClearWorldDepth()        override { m_world_depth_active = false; }
    void SetTopOverlay()          override { m_top_overlay_active = true;  }
    void ClearTopOverlay()        override { m_top_overlay_active = false; }

    void PopulateFromIR(const UICommandBuffers& cmds, const FrameState& fs) override;
    void DrawBack()          override;
    void DrawFront()         override;
    void DrawFrontBase()     override;
    void DrawFrontOverlay()  override;
    void DrawWorldSprites()  override;
    void FlipBuffers()       override;
    void Clear()             override;

    void SetUICommandBuffers(UICommandBuffers* cmds) override { m_write_cmds = cmds; }

    void FlushPendingInit() override;  // no-op here; use FlushPendingInit(cmd, staging)

    // Sprite and atlas setters
    void SetSpriteAtlas(VKSpriteAtlas* atlas) { m_sprite_atlas = atlas; }
    void SetFontAtlas(VKFontAtlas* atlas)     { m_font_atlas   = atlas; }

    /** Set the palette texture (256×1 R8 or RGBA8). */
    void SetPaletteTexture(VkImageView view, VkSampler sampler)
        { m_palette_view = view; m_palette_sampler = sampler; }

    /** Set the fade/remap texture (256×256 R8). */
    void SetFadeTexture(GpuTextureHandle /*unused*/) override {}
    void SetFadeTextureVK(VkImageView view, VkSampler sampler)
        { m_fade_view = view; m_fade_sampler = sampler; }

    /** Set the slab tile texture. */
    void UpdateSlabTexture(const uint8_t* data, int dim) override;

    uint8_t* AcquireMinimapBuffer(int size) override;
    void SubmitMinimap(int screen_x, int screen_y, int size) override;

    // Submit calls — all go to m_write_cmds (IR path) or m_quads[] (legacy)
    void SubmitPanelSprite(int32_t x, int32_t y, int units_per_px,
                           SpriteHandle spr, bool flip_horiz = false) override;
    void SubmitPanelSpriteRemap(int32_t x, int32_t y, int units_per_px,
                                SpriteHandle spr, int remap_row) override;
    void SubmitPanelSpriteColored(int32_t x, int32_t y, int units_per_px,
                                  SpriteHandle spr, uint8_t color_idx) override;
    void SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h,
                            SpriteHandle spr) override;
    void SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h,
                        uint8_t color_idx) override;
    void SubmitSolidBoxAlpha(int32_t x, int32_t y, int32_t w, int32_t h,
                             uint8_t color_idx, float alpha) override;
    bool SubmitSlabBackground(int x, int y, int w, int h) override;
    void SubmitSlabSelector(int x1, int y1, int x2, int y2,
                            unsigned char color, float z_depth) override;
    void SubmitFBOQuad(int x, int y, int w, int h,
                       GpuTextureHandle tex_id, float clip_radius = -1.0f) override;
    void BeginPiPSprites() override;
    void DrawPiPSprites(int pip_w, int pip_h) override;

    const char* GetName() const override { return "VULKAN_UI"; }

private:
    // ── Descriptor helper ─────────────────────────────────────────────────────
    /** bind_pass values correspond to VKUIPass in VKUIRenderer.cpp */
    VkDescriptorSet alloc_and_bind_descriptors(int pass, int remap_row);

    // ── Quad → vertex expansion (shared with GL path) ────────────────────────
    void expand_quad_to_vertices(const UIQuad& q, UIVertex* out) const;
    void expand_line_to_vertices(const UILine& l, UIVertex* out) const;

    // ── Flush helpers ─────────────────────────────────────────────────────────
    /** Upload and draw all quads + lines in the given layer. */
    void flush_layer(int layer, bool depth_test);

    /** Upload vertices via staging ring.
     *  Returns false if the ring is exhausted. */
    bool upload_vertices(const UIVertex* verts, size_t count, VkBuffer& out_buf, VkDeviceSize& out_off);

    /** Allocate a descriptor set for the given binding and record the draw. */
    void record_draw(VkPipeline pipeline,
                     VkDescriptorSet desc_set,
                     VkBuffer vtx_buf, VkDeviceSize vtx_off,
                     uint32_t vtx_count);

    void setup_push_constants_ui(float remap_row, float z_ndc,
                                 float clip_x0, float clip_y0,
                                 float clip_x1, float clip_y1,
                                 float clip_radius);

    // ── Slab texture upload ────────────────────────────────────────────────────
    void flush_slab_texture_upload(VkCommandBuffer cmd, VKStagingRing& staging);

    // ── Device handles ────────────────────────────────────────────────────────
    VkDevice      m_device    = VK_NULL_HANDLE;
    VmaAllocator  m_allocator = VK_NULL_HANDLE;

    // Shared infrastructure (not owned)
    VKPipelineCache*    m_pipelines    = nullptr;
    VKDescriptorLayout* m_descriptors  = nullptr;
    VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;

    // Per-frame state
    VkCommandBuffer m_cmd         = VK_NULL_HANDLE;
    int             m_frame_index = 0;

    // Per-frame vertex ring (CPU_TO_GPU, persistently mapped, VERTEX_BUFFER_BIT)
    VkBuffer        m_vtx_buf     = VK_NULL_HANDLE;
    VmaAllocation   m_vtx_alloc   = VK_NULL_HANDLE;
    UIVertex*       m_vtx_mapped  = nullptr;   // persistently mapped
    size_t          m_vtx_cursor  = 0;         // reset each frame

    // Textures (not owned)
    VKSpriteAtlas* m_sprite_atlas = nullptr;
    VKFontAtlas*   m_font_atlas   = nullptr;
    VkImageView    m_palette_view    = VK_NULL_HANDLE;
    VkSampler      m_palette_sampler = VK_NULL_HANDLE;
    VkImageView    m_fade_view       = VK_NULL_HANDLE;
    VkSampler      m_fade_sampler    = VK_NULL_HANDLE;

    // Slab tile texture (R8, GL_REPEAT equivalent)
    VkImage        m_slab_image    = VK_NULL_HANDLE;
    VmaAllocation  m_slab_alloc    = VK_NULL_HANDLE;
    VkImageView    m_slab_view     = VK_NULL_HANDLE;
    VkSampler      m_slab_sampler  = VK_NULL_HANDLE;  // REPEAT sampler
    int            m_slab_dim      = 0;
    std::atomic<const uint8_t*> m_slab_pending_data {nullptr};
    std::atomic<int>            m_slab_pending_dim  {0};

    // Minimap texture (R8, uploaded per frame)
    VkImage        m_minimap_image    = VK_NULL_HANDLE;
    VmaAllocation  m_minimap_alloc    = VK_NULL_HANDLE;
    VkImageView    m_minimap_view     = VK_NULL_HANDLE;
    VkSampler      m_minimap_sampler  = VK_NULL_HANDLE;
    int            m_minimap_tex_dim  = 0;
    uint8_t*       m_minimap_cpu_buf  = nullptr;
    int            m_minimap_cpu_size = 0;
    int            m_minimap_x = 0, m_minimap_y = 0, m_minimap_size = 0;
    bool           m_minimap_pending  = false;
    // Render-thread shadow copies (after FlipBuffers)
    uint8_t*       m_rt_minimap_cpu_buf  = nullptr;
    int            m_rt_minimap_cpu_size = 0;
    int            m_rt_minimap_x = 0, m_rt_minimap_y = 0, m_rt_minimap_size = 0;
    bool           m_rt_minimap_pending  = false;

    // Screen size
    int m_screen_w = 0, m_screen_h = 0;

    // Game viewport (scissor for WorldDepth layer)
    int  m_rt_game_vp_x = 0, m_rt_game_vp_y = 0;
    int  m_rt_game_vp_w = 0, m_rt_game_vp_h = 0;
    bool m_rt_game_vp_set = false;

    // Palette source pointer
    const uint8_t* m_palette_data = nullptr;

    // Layer / mode state (game thread)
    int   m_current_layer       = 1;
    float m_world_z             = 0.5f;
    bool  m_world_depth_active  = false;
    bool  m_top_overlay_active  = false;

    // Render-thread quad/line buffers (5 layers: Back, Front, WorldDepth, Overlay, Cursor)
    std::vector<UIQuad> m_rt_quads[5];
    std::vector<UILine> m_rt_lines[5];

    // FBO composite quads (render thread only — PiP)
    struct VKFBOQuad {
        float x0, y0, x1, y1;
        VkImageView  view;
        VkSampler    sampler;
        float clip_radius;
    };
    std::vector<VKFBOQuad> m_fbo_quads;

    // PiP capture watermarks
    int  m_pip_quad_wm[5]    = {};
    int  m_pip_line_wm[5]    = {};
    bool m_pip_active        = false;

    // IR write target
    UICommandBuffers* m_write_cmds = nullptr;
};

/******************************************************************************/

#endif // RENDERER_VK_SHADERC_AVAILABLE
#endif // RENDERER_VULKAN_ENABLED
#endif // VKUIRENDERER_H
