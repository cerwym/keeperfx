/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file UIBatch.h
 *     Backend-agnostic UIQuad and UILine types shared between GL and VK
 *     UI renderers.
 * @par Purpose:
 *     Extracted from GLUIRenderer.h so VKUIRenderer can use the same
 *     intermediate quad representation without pulling in GL headers.
 */
/******************************************************************************/
#pragma once

#include <cstdint>

/******************************************************************************/

/**
 * One quad in the UI batch pipeline.
 * Created by ExecuteUIFromIR() / PopulateFromIR() on the render thread;
 * consumed by the flush pass (FlushQuads_RT / VKUIRenderer::FlushLayer).
 */
struct UIQuad {
    float x0, y0;          ///< Top-left corner in screen pixels.
    float x1, y1;          ///< Bottom-right corner in screen pixels.
    float u0, v0, u1, v1;  ///< Texture coordinates.
    float r, g, b, a;      ///< Colour / tint.
    float z;               ///< NDC depth [-1, 1].
    float mode;            ///< Pass selector (0=sprite, 1=font, 3=solid, 10=slab, 20=colored, 30=remap).
    uint32_t texture_id;   ///< Sprite-sheet texture ID (0 = primary atlas).
    int remap_row;         ///< Fade-table row for mode 30; -1 = unused.
};

/**
 * One line segment in the UI batch pipeline (slab-selector strips).
 * Expanded to a thick rectangle (2 triangles) by the flush pass.
 */
struct UILine {
    float x1, y1;          ///< Start endpoint in screen pixels.
    float x2, y2;          ///< End endpoint in screen pixels.
    float r, g, b, a;      ///< Line colour.
    float z;               ///< NDC depth.
    float thickness;       ///< Line thickness in screen pixels.
};

/******************************************************************************/
