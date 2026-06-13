/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKPipelineCache.h
 *     VkPipeline factory and cache.
 *
 * One VkPipeline per logical pass type.  Pipelines are created at Init() and
 * cached until Shutdown().  VKUIRenderer and VKWorldViewRenderer hold a pointer
 * to the shared cache and call GetPipeline(VKPassType) each draw call.
 */
/******************************************************************************/
#pragma once
#ifndef VKPIPELINECACHE_H
#define VKPIPELINECACHE_H

#ifdef RENDERER_VULKAN_ENABLED
#ifdef RENDERER_VK_SHADERC_AVAILABLE

#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>
#include <cstdint>
#include <vector>

/******************************************************************************/

/** Logical pass types — one pipeline per entry. */
enum class VKPassType : int
{
    // UI passes
    UI_Sprite  = 0,  // palette-indexed atlas (R8 + palette 1D)
    UI_Solid,        // vertex colour only
    UI_Remap,        // atlas + palette + 256×256 fade table
    UI_Font,         // RGBA8 font atlas
    UI_Colored,      // atlas as discard mask + flat vertex colour
    UI_FBO,          // RGBA8 FBO composite (PiP)

    // Blit passes
    Blit,            // fullscreen palette decode (staging→swapchain)
    BlitRaw,         // fullscreen raw-image decode (no index-0 transparency)

    // World passes
    WorldTile,       // tile geometry with per-vertex shade
    WorldSprite,     // keeper sprite (palette-indexed, single atlas page)
    WorldSpriteArray,// keeper sprite (atlas array + CLUT)
    WorldGlow,       // keeper sprite additive glow
    WorldSpriteOutline, // depth-fail creature outline
    WorldShadow,     // multiply-blend shadow
    WorldFlatPoly,   // flat-colour polygons

    // Post-process
    Passthrough,     // passthrough blit (lens→swapchain)
    LensDisplace,    // displacement lens
    LensMist,        // mist lens
    LensFlyeye,      // compound-eye lens
    LensOverlay,     // alpha-composite overlay
    MapFade,         // parchment↔world elastic wipe
    ScreenTint,      // fullscreen flat-colour tint

    Count
};

/******************************************************************************/

/**
 * Owns one VkPipeline per VKPassType.
 * Caller provides the VkRenderPass and screen size at Init(); pipelines are
 * recreated on RecreateForSwapchain().
 */
class VKPipelineCache
{
public:
    VKPipelineCache() = default;
    VKPipelineCache(const VKPipelineCache&) = delete;
    VKPipelineCache& operator=(const VKPipelineCache&) = delete;

    /** Create all pipelines.
     *  @param device       Logical device.
     *  @param render_pass  Render pass the pipelines will be used with.
     *  @param compiler     shaderc compiler (caller owns).
     *  @return true if all pipelines compiled successfully. */
    bool Init(VkDevice device, VkRenderPass render_pass, shaderc_compiler_t compiler);

    /** Destroy all pipelines and shader modules. */
    void Shutdown();

    /** Return the VkPipeline for @p type (VK_NULL_HANDLE if not ready). */
    VkPipeline GetPipeline(VKPassType type) const;

    /** Return the shared pipeline layout (all passes use the same push-constant layout). */
    VkPipelineLayout GetLayout() const { return m_layout; }

    /** Return the shared descriptor set layout (one combined-image-sampler per set). */
    VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_desc_layout; }

    bool IsReady() const { return m_ready; }

private:
    bool CreateShaderModule(shaderc_compiler_t compiler,
                            const char* src,
                            shaderc_shader_kind stage,
                            const char* name,
                            VkShaderModule& out_module);

    bool CreatePipeline(VKPassType type,
                        VkShaderModule vert, VkShaderModule frag,
                        VkBool32 depth_test, VkBool32 depth_write,
                        VkCompareOp depth_op,
                        VkBlendFactor src_color, VkBlendFactor dst_color,
                        VkBlendFactor src_alpha, VkBlendFactor dst_alpha,
                        VkBlendOp blend_op_color, VkBlendOp blend_op_alpha,
                        bool blend_enable,
                        int vertex_type  // 0=UI, 1=World, 2=Sprite, 3=FlatPoly, 4=Fullscreen
                        );

    VkDevice          m_device      = VK_NULL_HANDLE;
    VkRenderPass      m_render_pass = VK_NULL_HANDLE;
    VkPipelineLayout  m_layout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_desc_layout = VK_NULL_HANDLE;

    VkPipeline m_pipelines[static_cast<int>(VKPassType::Count)] = {};
    bool       m_ready = false;
};

/******************************************************************************/

#endif // RENDERER_VK_SHADERC_AVAILABLE
#endif // RENDERER_VULKAN_ENABLED
#endif // VKPIPELINECACHE_H
