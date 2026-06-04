/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKWorldViewRenderer.h
 *     Vulkan world-geometry renderer.
 * @par Purpose:
 *     Implements IWorldViewRenderer for the Vulkan backend.
 *     Mirrors GLWorldViewRenderer's double-buffer pattern and draw-command
 *     vocabulary exactly; replaces GL calls with Vulkan command recording.
 *
 * Thread model (same as GLWorldViewRenderer):
 *   GT (game thread):  BeginWorldPass, DrawIsometricView, DrawFrontView,
 *                      SubmitKeeperSprite, FlipBuffers
 *   RT (render thread): ExecuteFromIR, FlushPendingInit, SetCommandBuffer
 *   GT→RT (atomic):     m_slab/kspr/clut state — N/A for world renderer
 */
/******************************************************************************/
#pragma once
#ifndef VKWORLDVIEWRENDERER_H
#define VKWORLDVIEWRENDERER_H

#ifdef RENDERER_VULKAN_ENABLED
#ifdef RENDERER_VK_SHADERC_AVAILABLE

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "renderer/IWorldViewRenderer.h"
#include "renderer/WorldVertex.h"
#include "renderer/FlatPolyVertex.h"
#include "renderer/EngineVertex.h"
#include "renderer/ir/WorldCommands.h"
#include "renderer/vulkan/VKTileAtlas.h"
#include "renderer/vulkan/VKPipelineCache.h"
#include "renderer/vulkan/VKDescriptorLayout.h"
#include "renderer/vulkan/VKStagingRing.h"

/******************************************************************************/

struct PolyPoint;

class VKWorldViewRenderer : public IWorldViewRenderer
{
public:
    // ── Constants ────────────────────────────────────────────────────────────
    static constexpr int k_max_verts         = 65536;   // world geometry verts
    static constexpr int k_kspr_atlas_layers = 512;     // keeper sprite array layers
    static constexpr int k_clut_rows         = 128;     // CLUT table rows (row 0 = identity)
    static constexpr int k_kspr_decode_dim   = 256;     // stride for sprite decode scratch

    // ── Internal command types ───────────────────────────────────────────────

    /** Deferred draw command, recorded on game thread, executed on render thread. */
    struct DrawCmd
    {
        enum Type {
            CMD_TILES,              ///< Tile geometry batch
            CMD_IR_KEEPER_SPRITES,  ///< Keeper sprites (indices into m_rt_kspr_ir)
            CMD_SHADOWS,            ///< One shadow, index into m_rt_shadow_cmds
            CMD_FLAT_POLYS,         ///< Flat-colour polygon batch
        } type = CMD_TILES;
        int vert_start      = 0;
        int vert_count      = 0;
        int sprite_ir_start = 0;
        int sprite_ir_count = 0;
        int shadow_idx      = 0;
    };

    /** Per-shadow data resolved on the game thread. */
    struct ShadowCmd
    {
        EnginePolyVertex verts[4]   = {};
        unsigned short   anim_sprite  = 0;
        short            angle        = 0;
        unsigned char    current_frame = 0;
        int              tex_w        = 0;
        int              tex_h        = 0;
        float            darkness     = 1.0f;
        float            ndc_z        = 0.0f;
    };

    // ── Lifecycle ────────────────────────────────────────────────────────────

    VKWorldViewRenderer() = default;
    ~VKWorldViewRenderer() override { Shutdown(); }

    VKWorldViewRenderer(const VKWorldViewRenderer&)            = delete;
    VKWorldViewRenderer& operator=(const VKWorldViewRenderer&) = delete;

    /**
     * Initialise Vulkan resources.
     * @param device           Logical device.
     * @param allocator        VMA allocator.
     * @param pipelines        Shared pipeline cache.
     * @param desc_layout      Shared descriptor layout / pool manager.
     * @param staging          Per-frame staging ring (render-thread use only).
     * @param tile_atlas       Tile atlas (must be Init()'d before calling this).
     * @param palette_view     RGBA8 256×1 palette image view (owned externally).
     * @param palette_sampler  Palette sampler.
     * @param fade_view        R8 256×256 fade/remap LUT image view.
     * @param fade_sampler     Fade sampler.
     */
    bool Init(VkDevice           device,
              VmaAllocator       allocator,
              VKPipelineCache*   pipelines,
              VKDescriptorLayout* desc_layout,
              VKStagingRing*     staging,
              VKTileAtlas*       tile_atlas,
              VkImageView        palette_view,
              VkSampler          palette_sampler,
              VkImageView        fade_view,
              VkSampler          fade_sampler);

    void Shutdown();

    // ── Render-thread bookkeeping ─────────────────────────────────────────────

    /** Supply the active VkCommandBuffer and frame index before ExecuteFromIR(). */
    void SetCommandBuffer(VkCommandBuffer cmd, int frame_index);

    /** Upload any pending atlas/CLUT data that arrived since last call.
     *  Must be called before draw commands are recorded. */
    void FlushPendingInit(VkCommandBuffer cmd);

    // ── Game-thread ↔ render-thread swap ─────────────────────────────────────

    /** Swap game-thread buffers into render-thread read copies.
     *  Called from RendererVulkan::EndFrame() while the render thread is idle. */
    void FlipBuffers();

    // ── IWorldViewRenderer ───────────────────────────────────────────────────

    void BeginWorldPass(uint8_t* framebuf, int pitch, int w, int h,
                        int vp_x, int vp_y) override;
    void DrawIsometricView() override;
    void DrawFrontView(struct Camera* cam) override;
    void ExecuteFromIR(const WorldCommandBuffers& cmds) override;

    int  SubmitKeeperSprite(int32_t dst_x, int32_t dst_y,
                            int32_t dst_w, int32_t dst_h,
                            const unsigned char* data,
                            int src_w, int src_h,
                            unsigned int draw_flags,
                            const unsigned char* remap) override;
    void ClearKeeperSpriteAtlas() override;
    void PreloadKeeperSpriteAtlas() override;

    void SetScreenSize(int w, int h) override
    {
        m_full_screen_w = w;
        m_full_screen_h = h;
    }
    void SetPaletteSource(const uint8_t* palette) override { m_palette_data = palette; }

    const char* GetName()       const override { return "VKWorldViewRenderer"; }

    bool HasPendingCommands() const
    {
        return !m_draw_cmds.empty() || !m_shadow_cmds.empty()
            || !m_flatpoly_verts.empty() || (m_vert_count > m_cmd_vert_start);
    }

private:
    // ── Geometry helpers (game thread) ───────────────────────────────────────

    bool append_triangle(int tile_id,
                         const struct PolyPoint* p0,
                         const struct PolyPoint* p1,
                         const struct PolyPoint* p2,
                         int32_t cam_z0 = 0, int32_t cam_z1 = 0, int32_t cam_z2 = 0);

    bool append_triangle_compact(int sx0, int sy0, int u0, int v0, int shade0,
                                 int sx1, int sy1, int u1, int v1, int shade1,
                                 int sx2, int sy2, int u2, int v2, int shade2);

    bool append_frontview_quad(const struct BucketKindTexturedQuad* txquad);
    void gpu_flush();
    void setup_world_sprite_processing(int32_t bucket_num);

    // ── Render-thread execution ───────────────────────────────────────────────

    void execute_passes(VkCommandBuffer cmd,
                        int vp_x, int vp_y, int screen_w, int screen_h,
                        const std::vector<IRWorldKeeperSpriteCmd>& kspr_ir);

    void DrawKeeperSpriteVK(VkCommandBuffer cmd, const IRWorldKeeperSpriteCmd& kspr);

    // ── CLUT / palette helpers (render thread) ────────────────────────────────

    void ensure_clut_valid(VkCommandBuffer cmd);
    int  alloc_clut_row(const uint8_t* remap);

    // ── Keeper sprite atlas helpers ───────────────────────────────────────────

    bool init_kspr_atlas();
    void free_kspr_atlas();

    // ── VK resources ─────────────────────────────────────────────────────────

    VkDevice           m_device     = VK_NULL_HANDLE;
    VmaAllocator       m_allocator  = VK_NULL_HANDLE;
    VKPipelineCache*   m_pipelines  = nullptr;
    VKDescriptorLayout* m_desc_layout= nullptr;
    VKStagingRing*     m_staging    = nullptr;
    VKTileAtlas*       m_tile_atlas = nullptr;

    // External texture views (not owned)
    VkImageView m_palette_view    = VK_NULL_HANDLE;
    VkSampler   m_palette_sampler = VK_NULL_HANDLE;
    VkImageView m_fade_view       = VK_NULL_HANDLE;
    VkSampler   m_fade_sampler    = VK_NULL_HANDLE;

    // World vertex VMA buffer: k_max_verts × kVKFramesInFlight × sizeof(WorldVertex)
    // CPU_TO_GPU, persistently mapped; double-windowed for game/render thread isolation.
    VkBuffer      m_vtx_buf        = VK_NULL_HANDLE;
    VmaAllocation m_vtx_alloc      = VK_NULL_HANDLE;
    WorldVertex*  m_vtx_mapped     = nullptr;          // RT: write window for this frame
    int           m_rt_frame_index = 0;                // RT: current frame window index

    // Flat-poly vertex VMA buffer (similar layout, FlatPolyVertex)
    static constexpr int k_max_flatpoly_verts = 8192;
    VkBuffer      m_fp_buf         = VK_NULL_HANDLE;
    VmaAllocation m_fp_alloc       = VK_NULL_HANDLE;
    FlatPolyVertex* m_fp_mapped    = nullptr;

    // Transient vertex buffer for per-sprite and per-shadow quads.
    // Sprites/shadows have a different vertex format to WorldVertex so they
    // need a separate binding.  One window per frame (k_max_transient_verts × 2).
    static constexpr int k_max_transient_verts = 8192; // 6 verts × ~1300 sprites/shadows
    VkBuffer      m_transient_buf     = VK_NULL_HANDLE;
    VmaAllocation m_transient_alloc   = VK_NULL_HANDLE;
    uint8_t*      m_transient_mapped  = nullptr;
    int           m_transient_cursor  = 0; // bytes written so far into current RT window

    // Shadow silhouette temporary image (256×256, R8_UNORM)
    VkImage       m_shadow_img     = VK_NULL_HANDLE;
    VmaAllocation m_shadow_alloc   = VK_NULL_HANDLE;
    VkImageView   m_shadow_view    = VK_NULL_HANDLE;
    VkSampler     m_shadow_sampler = VK_NULL_HANDLE;

    // Active command buffer for the current render frame (RT)
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;

    // ── Keeper sprite atlas ───────────────────────────────────────────────────

    VkImage       m_kspr_image      = VK_NULL_HANDLE;
    VmaAllocation m_kspr_alloc      = VK_NULL_HANDLE;
    VkImageView   m_kspr_view       = VK_NULL_HANDLE;
    VkSampler     m_kspr_sampler    = VK_NULL_HANDLE;
    int           m_kspr_atlas_used = 0;  // next free layer
    int           m_kspr_atlas_hits = 0;  // cache hit counter (diagnostic)
    struct AtlasEntry { int layer; int src_w; };
    std::unordered_map<const uint8_t*, AtlasEntry> m_kspr_atlas_map;

    // CLUT image: 256 × k_clut_rows RGBA8_UNORM
    VkImage       m_clut_image      = VK_NULL_HANDLE;
    VmaAllocation m_clut_alloc      = VK_NULL_HANDLE;
    VkImageView   m_clut_view       = VK_NULL_HANDLE;
    VkSampler     m_clut_sampler    = VK_NULL_HANDLE;
    int           m_kspr_clut_used  = 1;  // row 0 = identity, always allocated
    std::unordered_map<const uint8_t*, int> m_kspr_clut_map;
    uint8_t       m_clut_palette_snap[768] = {};
    bool          m_clut_dirty             = false;

    // ── Game-thread state ─────────────────────────────────────────────────────

    int            m_full_screen_w = 0;  // GT:
    int            m_full_screen_h = 0;  // GT:
    const uint8_t* m_palette_data  = nullptr; // GT:

    int            m_screen_w        = 0;   // GT:
    int            m_screen_h        = 0;   // GT:
    int            m_vp_x            = 0;   // GT:
    int            m_vp_y            = 0;   // GT:
    uint8_t*       m_framebuf        = nullptr; // GT:
    int            m_pitch           = 0;       // GT:
    int            m_current_bucket  = 0;        // GT:
    float          m_current_sprite_z = 0.0f;   // GT:

    WorldVertex* m_verts          = nullptr; // GT: game-thread write buffer
    int          m_vert_count     = 0;       // GT:
    int          m_cmd_vert_start = 0;       // GT:

    std::vector<DrawCmd>              m_draw_cmds;
    std::vector<ShadowCmd>            m_shadow_cmds;
    std::vector<FlatPolyVertex>       m_flatpoly_verts;
    std::vector<IRWorldKeeperSpriteCmd> m_kspr_ir;

    // ── Render-thread copies ──────────────────────────────────────────────────

    WorldVertex* m_rt_verts          = nullptr; // RT:
    int          m_rt_vert_count     = 0;       // RT:
    std::vector<DrawCmd>              m_rt_draw_cmds;
    std::vector<ShadowCmd>            m_rt_shadow_cmds;
    std::vector<FlatPolyVertex>       m_rt_flatpoly_verts;
    std::vector<IRWorldKeeperSpriteCmd> m_rt_kspr_ir;
    int          m_rt_screen_w = 0;
    int          m_rt_screen_h = 0;
    int          m_rt_vp_x     = 0;
    int          m_rt_vp_y     = 0;
    uint8_t      m_rt_palette[768] = {};

    bool         m_initialized          = false;
    bool         m_shadow_img_ready     = false; // RT: shadow image transitioned to SHADER_READ
    bool         m_clut_img_ready       = false; // RT: CLUT image transitioned to SHADER_READ
    bool         m_world_pass_active    = false; // GT:
};

/******************************************************************************/

#endif // RENDERER_VK_SHADERC_AVAILABLE
#endif // RENDERER_VULKAN_ENABLED
#endif // VKWORLDVIEWRENDERER_H
