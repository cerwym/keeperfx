/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file FlatPolyVertex.h
 *     Shared flat-colour polygon vertex type for world rendering.
 */
/******************************************************************************/
#pragma once

/** A single vertex for the flat-colour polygon (QK_PolyMode0 / BasicPolygon) pass.
 *  X/Y/Z are NDC floats; R/G/B are linear colour [0..1]. */
struct FlatPolyVertex
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
};

/******************************************************************************/
