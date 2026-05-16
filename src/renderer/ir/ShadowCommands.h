/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file ShadowCommands.h
 *     Intermediate-representation command types for the shadow renderer.
 * @par Purpose:
 *     Shadow rendering is currently embedded inside GLWorldViewRenderer.
 *     Extracting it to a separate command buffer is the first step toward
 *     an IShadowRenderer interface that backends can implement independently.
 *
 *     Shadow caster geometry is already captured in IRWorldShadowCmd
 *     (WorldCommands.h).  This header holds commands for future shadow-map
 *     and shadow-volume passes that are not yet implemented.
 */
/******************************************************************************/
#pragma once

#include <cstdint>
#include "renderer/ir/IRCommandBuffer.h"
#include "bflib_render.h"  // PolyPoint

/******************************************************************************/

/** Shadow caster geometry for a single creature shadow quad.
 *  Mirrors GLWorldViewRenderer::ShadowCmd for IR compatibility. */
struct IRShadowCasterCmd
{
    struct PolyPoint verts[4]    = {};  /**< Screen-px coords + 16.16 UV. */
    unsigned short   anim_sprite = 0;
    short            angle       = 0;
    unsigned char    current_frame = 0;
    int              tex_w       = 0;
    int              tex_h       = 0;
    float            darkness    = 1.0f;  /**< Source alpha for multiply blend. */
    float            ndc_z       = 0.0f;  /**< NDC depth for depth testing. */
};

/** Shadow receiver hint — marks a surface that should receive shadow.
 *  Reserved for future shadow-map pass; currently unused. */
struct IRShadowReceiverCmd
{
    uint32_t surface_id = 0;  /**< Opaque surface identifier. */
};

/** Request a shadow map update for the current frame.
 *  Reserved for future GPU shadow map; backends without shadow maps ignore it. */
struct IRShadowMapUpdateCmd
{
    float light_x = 0.0f;
    float light_y = 0.0f;
    float light_z = 0.0f;
};

/******************************************************************************/

/** Combined per-frame shadow command buffers. */
struct ShadowCommandBuffers
{
    IRCommandBuffer<IRShadowCasterCmd>   casters;
    IRCommandBuffer<IRShadowReceiverCmd> receivers;
    IRCommandBuffer<IRShadowMapUpdateCmd> map_updates;

    void Reset()
    {
        casters.Reset();
        receivers.Reset();
        map_updates.Reset();
    }

    void Reserve(size_t casters_n)
    {
        casters.Reserve(casters_n);
        receivers.Reserve(casters_n);
        map_updates.Reserve(1);
    }
};

/******************************************************************************/
