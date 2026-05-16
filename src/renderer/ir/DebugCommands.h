/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file DebugCommands.h
 *     Intermediate-representation command types for the debug renderer.
 * @par Purpose:
 *     Debug visualisations (collision boxes, LOS cones, hearing radii,
 *     navigation paths, bounding boxes) are submitted as typed commands
 *     during the game thread and drawn on the render thread after the main
 *     world pass but before the HUD.
 *
 *     Debug commands that require expensive per-entity iteration at submit
 *     time (nav mesh, LOS re-computation) are wrapped with KFX_DEBUG_RENDERER
 *     guards so they compile away completely in release builds.
 *
 *     Debug flags live in RendererSettings::debug_overlay_flags; each bit
 *     corresponds to a DBG_OVR_* constant (see DebugOverlayFlags.h when
 *     that header is added in a later phase).
 */
/******************************************************************************/
#pragma once

#include <cstdint>
#include <cstring>
#include "renderer/ir/IRCommandBuffer.h"

/******************************************************************************/

static constexpr size_t kIRDebugTextMaxLen = 128;

/** Wireframe axis-aligned bounding box in world space. */
struct IRDebugAABBCmd
{
    float    min_x         = 0.0f;
    float    min_y         = 0.0f;
    float    min_z         = 0.0f;
    float    max_x         = 0.0f;
    float    max_y         = 0.0f;
    float    max_z         = 0.0f;
    uint32_t color_rgba    = 0xFF0000FF;  /**< 0xRRGGBBAA, default = red. */
    float    line_thickness = 1.0f;
};

/** Line-of-sight cone from one entity (two rays from origin). */
struct IRDebugLOSConeCmd
{
    float    origin_x          = 0.0f;
    float    origin_y          = 0.0f;
    float    origin_z          = 0.0f;
    float    target_x          = 0.0f;
    float    target_y          = 0.0f;
    float    target_z          = 0.0f;
    float    half_angle_rad    = 0.785398f; /**< PI/4 = 45 degrees. */
    float    max_range         = 0.0f;      /**< 0 = unlimited. */
    uint32_t color_hit_rgba    = 0x00FF00FF;
    uint32_t color_blocked_rgba = 0xFF4400FF;
    int      los_result        = 1;         /**< 1=clear, 0=blocked. */
};

/** Hearing radius circle drawn on the dungeon floor. */
struct IRDebugHearRadiusCmd
{
    float    center_x      = 0.0f;
    float    center_y      = 0.0f;
    float    center_z      = 0.0f;
    float    radius        = 0.0f;
    uint32_t color_rgba    = 0x0088FFFF;
    int      segments      = 32;
    float    line_thickness = 1.0f;
};

/** Per-entity tight bounding box (narrower than a MapVolumeBox). */
struct IRDebugBoundingBoxCmd
{
    float    center_x      = 0.0f;
    float    center_y      = 0.0f;
    float    center_z      = 0.0f;
    float    half_x        = 0.0f;
    float    half_y        = 0.0f;
    float    half_z        = 0.0f;
    uint32_t color_rgba    = 0xFFFF00FF;
    float    line_thickness = 1.0f;
};

/** Creature navigation path / Ariadne waypoints.
 *  Waypoints are snapshotted at submission time (safe for render thread). */
struct IRDebugNavPathCmd
{
    static constexpr int kMaxWaypoints = 10;

    float    waypoint_x[kMaxWaypoints] = {};
    float    waypoint_y[kMaxWaypoints] = {};
    int      waypoint_count            = 0;
    float    dest_x                    = 0.0f;
    float    dest_y                    = 0.0f;
    uint32_t waypoint_color_rgba       = 0x00FF88FF;
    uint32_t path_color_rgba           = 0x44FF44FF;
    uint32_t dest_color_rgba           = 0xFF8800FF;
    float    line_thickness            = 1.5f;
};

/** Generic 3D line segment in world space. */
struct IRDebugLineCmd
{
    float    x0             = 0.0f;
    float    y0             = 0.0f;
    float    z0             = 0.0f;
    float    x1             = 0.0f;
    float    y1             = 0.0f;
    float    z1             = 0.0f;
    uint32_t color_rgba     = 0xFFFFFFFF;
    float    line_thickness = 1.0f;
    uint8_t  animated       = 0;   /**< 1 = stripey-line palette cycling. */
    int      stripey_idx    = 0;   /**< Index into colored_stripey_lines[]. */
};

/** Screen-space text label (HP bars, thing indices, frame counters). */
struct IRDebugTextCmd
{
    int      screen_x       = 0;
    int      screen_y       = 0;
    uint32_t color_rgba     = 0xFFFFFFFF;
    float    scale          = 1.0f;
    char     text[kIRDebugTextMaxLen] = {};

    void SetText(const char* src)
    {
        if (src)
        {
            std::strncpy(text, src, kIRDebugTextMaxLen - 1);
            text[kIRDebugTextMaxLen - 1] = '\0';
        }
        else
        {
            text[0] = '\0';
        }
    }
};

/******************************************************************************/

/** Combined per-frame debug command buffers.
 *  All buffers are valid in all build configs; caller guards submissions with
 *  RendererSettings::debug_overlay_flags checks so they remain empty in
 *  production when overlays are disabled. */
struct DebugCommandBuffers
{
    IRCommandBuffer<IRDebugAABBCmd>        aabbs;
    IRCommandBuffer<IRDebugLOSConeCmd>     los_cones;
    IRCommandBuffer<IRDebugHearRadiusCmd>  hear_radii;
    IRCommandBuffer<IRDebugBoundingBoxCmd> bounding_boxes;
    IRCommandBuffer<IRDebugNavPathCmd>     nav_paths;
    IRCommandBuffer<IRDebugLineCmd>        lines;
    IRCommandBuffer<IRDebugTextCmd>        texts;

    void Reset()
    {
        aabbs.Reset();
        los_cones.Reset();
        hear_radii.Reset();
        bounding_boxes.Reset();
        nav_paths.Reset();
        lines.Reset();
        texts.Reset();
    }

    void Reserve(size_t things_n)
    {
        aabbs.Reserve(things_n);
        los_cones.Reserve(things_n);
        hear_radii.Reserve(things_n);
        bounding_boxes.Reserve(things_n);
        nav_paths.Reserve(things_n / 4);
        lines.Reserve(things_n * 4);
        texts.Reserve(things_n);
    }

    void Swap(DebugCommandBuffers& other)
    {
        aabbs.Swap(other.aabbs);
        los_cones.Swap(other.los_cones);
        hear_radii.Swap(other.hear_radii);
        bounding_boxes.Swap(other.bounding_boxes);
        nav_paths.Swap(other.nav_paths);
        lines.Swap(other.lines);
        texts.Swap(other.texts);
    }
};

/******************************************************************************/
