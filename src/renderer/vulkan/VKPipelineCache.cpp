/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKPipelineCache.cpp
 *     VkPipeline factory and cache — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"

#ifdef RENDERER_VULKAN_ENABLED
#ifdef RENDERER_VK_SHADERC_AVAILABLE

#include "renderer/vulkan/VKPipelineCache.h"
#include "renderer/vulkan/VKShaders.h"
#include "renderer/UIVertex.h"
#include "renderer/WorldVertex.h"
#include "globals.h"

#include "post_inc.h"

/******************************************************************************/

// Vertex binding / attribute descriptions for each vertex type.
// Type 0 = UI (GLUIVertex: pos2, uv2, color4, z1, mode1 = 10 floats = 40 bytes)
// Type 1 = World (WorldVertex: 10 floats = 40 bytes)
// Type 2 = Sprite (pos2, uv2 = 16 bytes)
// Type 3 = FlatPoly (pos3, color3 = 24 bytes)
// Type 4 = Fullscreen (pos2, uv2 = 16 bytes, same as Sprite)

static void FillUIVertexInput(VkPipelineVertexInputStateCreateInfo& vi,
                               VkVertexInputBindingDescription& binding,
                               std::vector<VkVertexInputAttributeDescription>& attrs)
{
    binding.binding   = 0;
    binding.stride    = sizeof(UIVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    attrs.resize(5);
    // a_pos   (location 0): vec2  — UIVertex::x at byte 0
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT;      attrs[0].offset = 0u;
    // a_uv    (location 1): vec2  — UIVertex::u at byte 8
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32_SFLOAT;      attrs[1].offset = 8u;
    // a_color (location 2): vec4  — UIVertex::r at byte 16
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[2].offset = 16u;
    // a_z     (location 3): float — UIVertex::z at byte 32
    attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32_SFLOAT;          attrs[3].offset = 32u;
    // a_mode  (location 4): float — UIVertex::mode at byte 36
    attrs[4].location = 4; attrs[4].binding = 0; attrs[4].format = VK_FORMAT_R32_SFLOAT;          attrs[4].offset = 36u;

    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions    = attrs.data();
}

static void FillWorldVertexInput(VkPipelineVertexInputStateCreateInfo& vi,
                                  VkVertexInputBindingDescription& binding,
                                  std::vector<VkVertexInputAttributeDescription>& attrs)
{
    binding.binding   = 0;
    binding.stride    = sizeof(WorldVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    attrs.resize(7);
    // WorldVertex: x(0) y(4) z(8) u(12) v(16) shade(20) stl_x(24) stl_y(28) camera_z(32) atlas_layer(36) wx(40) wy(44) wz(48)
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;   attrs[0].offset = 0u;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32_SFLOAT;      attrs[1].offset = 12u;
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32_SFLOAT;          attrs[2].offset = 20u;
    attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32G32_SFLOAT;      attrs[3].offset = 24u;
    attrs[4].location = 4; attrs[4].binding = 0; attrs[4].format = VK_FORMAT_R32_SFLOAT;          attrs[4].offset = 32u;
    attrs[5].location = 5; attrs[5].binding = 0; attrs[5].format = VK_FORMAT_R32_SFLOAT;          attrs[5].offset = 36u;
    attrs[6].location = 6; attrs[6].binding = 0; attrs[6].format = VK_FORMAT_R32G32B32_SFLOAT;   attrs[6].offset = 40u;

    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions    = attrs.data();
}

// Sprite vertex: float pos[2], float uv[2]  (16 bytes)
struct SpriteVertex2D { float x, y, u, v; };

static void FillSpriteVertexInput(VkPipelineVertexInputStateCreateInfo& vi,
                                   VkVertexInputBindingDescription& binding,
                                   std::vector<VkVertexInputAttributeDescription>& attrs)
{
    binding.binding   = 0;
    binding.stride    = sizeof(SpriteVertex2D);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    attrs.resize(2);
    // SpriteVertex2D: x(0) y(4) u(8) v(12)
    attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0u};
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, 8u};

    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions    = attrs.data();
}

struct FlatPolyVertex3D { float x, y, z, r, g, b; };

static void FillFlatPolyVertexInput(VkPipelineVertexInputStateCreateInfo& vi,
                                     VkVertexInputBindingDescription& binding,
                                     std::vector<VkVertexInputAttributeDescription>& attrs)
{
    binding.binding   = 0;
    binding.stride    = sizeof(FlatPolyVertex3D);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    attrs.resize(2);
    // FlatPolyVertex3D: x(0) y(4) z(8) r(12) g(16) b(20)
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0u};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12u};

    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions    = attrs.data();
}

/******************************************************************************/

bool VKPipelineCache::CreateShaderModule(shaderc_compiler_t compiler,
                                          const char* src,
                                          shaderc_shader_kind stage,
                                          const char* name,
                                          VkShaderModule& out_module)
{
    std::vector<uint32_t> spv = VKShaders_Compile(compiler, src, stage, name);
    if (spv.empty())
        return false;

    VkShaderModuleCreateInfo ci = {};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spv.size() * sizeof(uint32_t);
    ci.pCode    = spv.data();

    if (vkCreateShaderModule(m_device, &ci, nullptr, &out_module) != VK_SUCCESS)
    {
        ERRORLOG("VKPipelineCache: failed to create shader module '%s'", name);
        return false;
    }
    return true;
}

/******************************************************************************/

bool VKPipelineCache::CreatePipeline(VKPassType type,
                                      VkShaderModule vert, VkShaderModule frag,
                                      VkBool32 depth_test, VkBool32 depth_write,
                                      VkCompareOp depth_op,
                                      VkBlendFactor src_color, VkBlendFactor dst_color,
                                      VkBlendFactor src_alpha, VkBlendFactor dst_alpha,
                                      VkBlendOp blend_op_color, VkBlendOp blend_op_alpha,
                                      bool blend_enable,
                                      int vertex_type)
{
    // Shader stages
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName  = "main";

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vi = {};
    VkVertexInputBindingDescription binding = {};
    std::vector<VkVertexInputAttributeDescription> attrs;

    switch (vertex_type)
    {
        case 0: FillUIVertexInput(vi, binding, attrs);       break;
        case 1: FillWorldVertexInput(vi, binding, attrs);    break;
        case 2: FillSpriteVertexInput(vi, binding, attrs);   break;
        case 3: FillFlatPolyVertexInput(vi, binding, attrs); break;
        case 4: FillSpriteVertexInput(vi, binding, attrs);   break;  // fullscreen same as sprite
        default:
            vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            break;
    }

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Dynamic viewport + scissor
    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rast = {};
    rast.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode    = VK_CULL_MODE_NONE;
    rast.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rast.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable       = depth_test;
    ds.depthWriteEnable      = depth_write;
    ds.depthCompareOp        = depth_op;

    VkPipelineColorBlendAttachmentState blend_att = {};
    blend_att.blendEnable         = blend_enable ? VK_TRUE : VK_FALSE;
    blend_att.srcColorBlendFactor = src_color;
    blend_att.dstColorBlendFactor = dst_color;
    blend_att.colorBlendOp        = blend_op_color;
    blend_att.srcAlphaBlendFactor = src_alpha;
    blend_att.dstAlphaBlendFactor = dst_alpha;
    blend_att.alphaBlendOp        = blend_op_alpha;
    blend_att.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                  | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend = {};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_att;

    static const VkDynamicState dyn_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dyn_states;

    VkGraphicsPipelineCreateInfo pci = {};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = 2;
    pci.pStages             = stages;
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &viewport_state;
    pci.pRasterizationState = &rast;
    pci.pMultisampleState   = &ms;
    pci.pDepthStencilState  = &ds;
    pci.pColorBlendState    = &blend;
    pci.pDynamicState       = &dyn;
    pci.layout              = m_layout;
    pci.renderPass          = m_render_pass;
    pci.subpass             = 0;

    int idx = static_cast<int>(type);
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipelines[idx]) != VK_SUCCESS)
    {
        ERRORLOG("VKPipelineCache: failed to create pipeline %d", idx);
        return false;
    }
    return true;
}

/******************************************************************************/

bool VKPipelineCache::Init(VkDevice device, VkRenderPass render_pass,
                            shaderc_compiler_t compiler)
{
    m_device      = device;
    m_render_pass = render_pass;

    // Push constant range: 128 bytes, all stages
    VkPushConstantRange pc_range = {};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset     = 0;
    pc_range.size       = 128;

    // Descriptor set layout: up to 4 combined image samplers (binding 0..3)
    VkDescriptorSetLayoutBinding bindings[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        bindings[i].binding         = (uint32_t)i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo dsl_ci = {};
    dsl_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl_ci.bindingCount = 4;
    dsl_ci.pBindings    = bindings;
    if (vkCreateDescriptorSetLayout(device, &dsl_ci, nullptr, &m_desc_layout) != VK_SUCCESS)
    {
        ERRORLOG("VKPipelineCache: failed to create descriptor set layout");
        return false;
    }

    VkPipelineLayoutCreateInfo layout_ci = {};
    layout_ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.setLayoutCount         = 1;
    layout_ci.pSetLayouts            = &m_desc_layout;
    layout_ci.pushConstantRangeCount = 1;
    layout_ci.pPushConstantRanges    = &pc_range;
    if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &m_layout) != VK_SUCCESS)
    {
        ERRORLOG("VKPipelineCache: failed to create pipeline layout");
        return false;
    }

    // Premultiplied-alpha blend factors (standard for UI sprites)
    const VkBlendFactor kSrcRGBA = VK_BLEND_FACTOR_ONE;
    const VkBlendFactor kDstRGBA = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    const VkBlendFactor kSrcA    = VK_BLEND_FACTOR_ONE;
    const VkBlendFactor kDstA    = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    const VkBlendOp     kAddBlend = VK_BLEND_OP_ADD;

    // Additive blend factors (glow pass)
    const VkBlendFactor kAddSrc = VK_BLEND_FACTOR_ONE;
    const VkBlendFactor kAddDst = VK_BLEND_FACTOR_ONE;

    // Multiply-blend factors (shadow: dst * (1-src_alpha))
    const VkBlendFactor kMulSrc      = VK_BLEND_FACTOR_ZERO;
    const VkBlendFactor kMulDstColor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    // Pre-declare all shader modules (MSVC requires declarations before any goto that skips them)
    VkShaderModule ui_vert          = VK_NULL_HANDLE;
    VkShaderModule blit_vert        = VK_NULL_HANDLE;
    VkShaderModule kspr_vert        = VK_NULL_HANDLE;
    VkShaderModule shadow_vert      = VK_NULL_HANDLE;
    VkShaderModule world_vert       = VK_NULL_HANDLE;
    VkShaderModule flatpoly_vert    = VK_NULL_HANDLE;
    VkShaderModule mapfade_vert     = VK_NULL_HANDLE;
    VkShaderModule screentint_vert  = VK_NULL_HANDLE;

    VkShaderModule ui_sprite_frag    = VK_NULL_HANDLE;
    VkShaderModule ui_solid_frag     = VK_NULL_HANDLE;
    VkShaderModule ui_remap_frag     = VK_NULL_HANDLE;
    VkShaderModule ui_font_frag      = VK_NULL_HANDLE;
    VkShaderModule ui_colored_frag   = VK_NULL_HANDLE;
    VkShaderModule ui_fbo_frag       = VK_NULL_HANDLE;
    VkShaderModule blit_frag         = VK_NULL_HANDLE;
    VkShaderModule rawblit_frag      = VK_NULL_HANDLE;
    VkShaderModule world_frag        = VK_NULL_HANDLE;
    VkShaderModule kspr_frag         = VK_NULL_HANDLE;
    VkShaderModule kspr_array_frag   = VK_NULL_HANDLE;
    VkShaderModule kspr_glow_frag    = VK_NULL_HANDLE;
    VkShaderModule kspr_outline_frag = VK_NULL_HANDLE;
    VkShaderModule shadow_frag       = VK_NULL_HANDLE;
    VkShaderModule flatpoly_frag     = VK_NULL_HANDLE;
    VkShaderModule passthrough_frag  = VK_NULL_HANDLE;
    VkShaderModule lens_displace_frag= VK_NULL_HANDLE;
    VkShaderModule lens_mist_frag    = VK_NULL_HANDLE;
    VkShaderModule lens_flyeye_frag  = VK_NULL_HANDLE;
    VkShaderModule lens_overlay_frag = VK_NULL_HANDLE;
    VkShaderModule mapfade_frag      = VK_NULL_HANDLE;
    VkShaderModule screentint_frag   = VK_NULL_HANDLE;

#define COMPILE_VERT(handle, src, name) \
    if (!CreateShaderModule(compiler, src, shaderc_glsl_vertex_shader, name, handle)) \
        goto cleanup;

#define COMPILE_FRAG(handle, src, name) \
    if (!CreateShaderModule(compiler, src, shaderc_glsl_fragment_shader, name, handle)) \
        goto cleanup;

    COMPILE_VERT(ui_vert,          VK_UI_VERT,          "ui.vert")
    COMPILE_VERT(blit_vert,        VK_BLIT_VERT,        "blit.vert")
    COMPILE_VERT(kspr_vert,        VK_KSPR_VERT,        "kspr.vert")
    COMPILE_VERT(shadow_vert,      VK_SHADOW_VERT,      "shadow.vert")
    COMPILE_VERT(world_vert,       VK_WORLD_VERT,       "world.vert")
    COMPILE_VERT(flatpoly_vert,    VK_FLATPOLY_VERT,    "flatpoly.vert")
    COMPILE_VERT(mapfade_vert,     VK_MAP_FADE_VERT,    "mapfade.vert")
    COMPILE_VERT(screentint_vert,  VK_SCREEN_TINT_VERT, "screentint.vert")

    COMPILE_FRAG(ui_sprite_frag,         VK_UI_SPRITE_FRAG,      "ui_sprite.frag")
    COMPILE_FRAG(ui_solid_frag,          VK_UI_SOLID_FRAG,       "ui_solid.frag")
    COMPILE_FRAG(ui_remap_frag,          VK_UI_REMAP_FRAG,       "ui_remap.frag")
    COMPILE_FRAG(ui_font_frag,           VK_UI_FONT_FRAG,        "ui_font.frag")
    COMPILE_FRAG(ui_colored_frag,        VK_UI_COLORED_FRAG,     "ui_colored.frag")
    COMPILE_FRAG(ui_fbo_frag,            VK_UI_FBO_FRAG,         "ui_fbo.frag")
    COMPILE_FRAG(blit_frag,              VK_BLIT_FRAG,           "blit.frag")
    COMPILE_FRAG(rawblit_frag,           VK_RAWBLIT_FRAG,        "rawblit.frag")
    COMPILE_FRAG(world_frag,             VK_WORLD_FRAG,          "world.frag")
    COMPILE_FRAG(kspr_frag,              VK_KSPR_FRAG,           "kspr.frag")
    COMPILE_FRAG(kspr_array_frag,        VK_KSPR_ARRAY_FRAG,     "kspr_array.frag")
    COMPILE_FRAG(kspr_glow_frag,         VK_KSPR_GLOW_FRAG,      "kspr_glow.frag")
    COMPILE_FRAG(kspr_outline_frag,      VK_KSPR_OUTLINE_FRAG,   "kspr_outline.frag")
    COMPILE_FRAG(shadow_frag,            VK_SHADOW_FRAG,         "shadow.frag")
    COMPILE_FRAG(flatpoly_frag,          VK_FLATPOLY_FRAG,       "flatpoly.frag")
    COMPILE_FRAG(passthrough_frag,       VK_PASSTHROUGH_FRAG,    "passthrough.frag")
    COMPILE_FRAG(lens_displace_frag,     VK_LENS_DISPLACE_FRAG,  "lens_displace.frag")
    COMPILE_FRAG(lens_mist_frag,         VK_LENS_MIST_FRAG,      "lens_mist.frag")
    COMPILE_FRAG(lens_flyeye_frag,       VK_LENS_FLYEYE_FRAG,    "lens_flyeye.frag")
    COMPILE_FRAG(lens_overlay_frag,      VK_LENS_OVERLAY_FRAG,   "lens_overlay.frag")
    COMPILE_FRAG(mapfade_frag,           VK_MAP_FADE_FRAG,       "mapfade.frag")
    COMPILE_FRAG(screentint_frag,        VK_SCREEN_TINT_FRAG,    "screentint.frag")

#undef COMPILE_VERT
#undef COMPILE_FRAG

    // Create pipelines — vertex_type: 0=UI, 1=World, 2=Sprite, 3=FlatPoly, 4=Fullscreen

    // UI: premult-alpha blend, no depth
    if (!CreatePipeline(VKPassType::UI_Sprite,  ui_vert, ui_sprite_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            kSrcRGBA, kDstRGBA, kSrcA, kDstA, kAddBlend, kAddBlend, true, 0)) goto cleanup;
    if (!CreatePipeline(VKPassType::UI_Solid,   ui_vert, ui_solid_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            kSrcRGBA, kDstRGBA, kSrcA, kDstA, kAddBlend, kAddBlend, true, 0)) goto cleanup;
    if (!CreatePipeline(VKPassType::UI_Remap,   ui_vert, ui_remap_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            kSrcRGBA, kDstRGBA, kSrcA, kDstA, kAddBlend, kAddBlend, true, 0)) goto cleanup;
    if (!CreatePipeline(VKPassType::UI_Font,    ui_vert, ui_font_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            kSrcRGBA, kDstRGBA, kSrcA, kDstA, kAddBlend, kAddBlend, true, 0)) goto cleanup;
    if (!CreatePipeline(VKPassType::UI_Colored, ui_vert, ui_colored_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            kSrcRGBA, kDstRGBA, kSrcA, kDstA, kAddBlend, kAddBlend, true, 0)) goto cleanup;
    if (!CreatePipeline(VKPassType::UI_FBO,     ui_vert, ui_fbo_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            kSrcRGBA, kDstRGBA, kSrcA, kDstA, kAddBlend, kAddBlend, true, 0)) goto cleanup;

    // Blit: opaque, no depth, no blend
    if (!CreatePipeline(VKPassType::Blit,    blit_vert, blit_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
            kAddBlend, kAddBlend, false, 4)) goto cleanup;
    if (!CreatePipeline(VKPassType::BlitRaw, blit_vert, rawblit_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
            kAddBlend, kAddBlend, false, 4)) goto cleanup;

    // World tile: depth write + test, opaque
    if (!CreatePipeline(VKPassType::WorldTile, world_vert, world_frag,
            VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
            kAddBlend, kAddBlend, false, 1)) goto cleanup;

    // World sprite: depth test (LEQUAL), no depth write, premult-alpha blend
    if (!CreatePipeline(VKPassType::WorldSprite, kspr_vert, kspr_frag,
            VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL,
            kSrcRGBA, kDstRGBA, kSrcA, kDstA, kAddBlend, kAddBlend, true, 2)) goto cleanup;
    if (!CreatePipeline(VKPassType::WorldSpriteArray, kspr_vert, kspr_array_frag,
            VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL,
            kSrcRGBA, kDstRGBA, kSrcA, kDstA, kAddBlend, kAddBlend, true, 2)) goto cleanup;

    // Glow: additive blend, depth test, no write
    if (!CreatePipeline(VKPassType::WorldGlow, kspr_vert, kspr_glow_frag,
            VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL,
            kAddSrc, kAddDst, kAddSrc, kAddDst, kAddBlend, kAddBlend, true, 2)) goto cleanup;

    // Outline: depth GREATER (depth-fail), premult-alpha blend, no write
    if (!CreatePipeline(VKPassType::WorldSpriteOutline, kspr_vert, kspr_outline_frag,
            VK_TRUE, VK_FALSE, VK_COMPARE_OP_GREATER,
            kSrcRGBA, kDstRGBA, kSrcA, kDstA, kAddBlend, kAddBlend, true, 2)) goto cleanup;

    // Shadow: multiply blend (GL_ZERO, GL_ONE_MINUS_SRC_ALPHA), depth test, no write
    if (!CreatePipeline(VKPassType::WorldShadow, shadow_vert, shadow_frag,
            VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL,
            kMulSrc, kMulDstColor, kMulSrc, kMulDstColor, kAddBlend, kAddBlend, true, 2)) goto cleanup;

    // Flat-colour polygons: depth test + write, no blend
    if (!CreatePipeline(VKPassType::WorldFlatPoly, flatpoly_vert, flatpoly_frag,
            VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
            kAddBlend, kAddBlend, false, 3)) goto cleanup;

    // Post-process: no depth, no blend
    if (!CreatePipeline(VKPassType::Passthrough, blit_vert, passthrough_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
            kAddBlend, kAddBlend, false, 4)) goto cleanup;
    if (!CreatePipeline(VKPassType::LensDisplace, blit_vert, lens_displace_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
            kAddBlend, kAddBlend, false, 4)) goto cleanup;
    if (!CreatePipeline(VKPassType::LensMist, blit_vert, lens_mist_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            kSrcRGBA, kDstRGBA, kSrcA, kDstA, kAddBlend, kAddBlend, true, 4)) goto cleanup;
    if (!CreatePipeline(VKPassType::LensFlyeye, blit_vert, lens_flyeye_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
            kAddBlend, kAddBlend, false, 4)) goto cleanup;
    if (!CreatePipeline(VKPassType::LensOverlay, blit_vert, lens_overlay_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            kSrcRGBA, kDstRGBA, kSrcA, kDstA, kAddBlend, kAddBlend, true, 4)) goto cleanup;
    if (!CreatePipeline(VKPassType::MapFade, mapfade_vert, mapfade_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
            kAddBlend, kAddBlend, false, 4)) goto cleanup;
    if (!CreatePipeline(VKPassType::ScreenTint, screentint_vert, screentint_frag,
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS,
            kSrcRGBA, kDstRGBA, kSrcA, kDstA, kAddBlend, kAddBlend, true, 4)) goto cleanup;

    m_ready = true;
    SYNCLOG("VKPipelineCache: all %d pipelines created", (int)VKPassType::Count);

cleanup:
    // Shader modules are no longer needed after pipeline creation
    auto destroy = [&](VkShaderModule& mod) {
        if (mod != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, mod, nullptr);
            mod = VK_NULL_HANDLE;
        }
    };
    destroy(ui_vert);         destroy(blit_vert);      destroy(kspr_vert);
    destroy(shadow_vert);     destroy(world_vert);      destroy(flatpoly_vert);
    destroy(mapfade_vert);    destroy(screentint_vert);
    destroy(ui_sprite_frag);  destroy(ui_solid_frag);   destroy(ui_remap_frag);
    destroy(ui_font_frag);    destroy(ui_colored_frag); destroy(ui_fbo_frag);
    destroy(blit_frag);       destroy(rawblit_frag);
    destroy(world_frag);      destroy(kspr_frag);        destroy(kspr_array_frag);
    destroy(kspr_glow_frag);  destroy(kspr_outline_frag);
    destroy(shadow_frag);     destroy(flatpoly_frag);
    destroy(passthrough_frag); destroy(lens_displace_frag); destroy(lens_mist_frag);
    destroy(lens_flyeye_frag); destroy(lens_overlay_frag);
    destroy(mapfade_frag);    destroy(screentint_frag);

    return m_ready;
}

void VKPipelineCache::Shutdown()
{
    if (m_device == VK_NULL_HANDLE)
        return;

    for (auto& pipeline : m_pipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    if (m_layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }

    if (m_desc_layout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(m_device, m_desc_layout, nullptr);
        m_desc_layout = VK_NULL_HANDLE;
    }

    m_ready = false;
}

VkPipeline VKPipelineCache::GetPipeline(VKPassType type) const
{
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(VKPassType::Count))
        return VK_NULL_HANDLE;
    return m_pipelines[idx];
}

/******************************************************************************/

#endif // RENDERER_VK_SHADERC_AVAILABLE
#endif // RENDERER_VULKAN_ENABLED
