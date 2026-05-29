/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file UIVertex.h
 *     Shared GPU vertex format for UI rendering (used by both GL and Vulkan).
 * @par Purpose:
 *     Defines UIVertex (formerly GLUIVertex) in a backend-independent header
 *     so VKPipelineCache and other non-GL code can reference the layout.
 */
/******************************************************************************/
#pragma once
#ifndef UIVERTEX_H
#define UIVERTEX_H

#include <stddef.h>  /* offsetof */

/** GPU vertex for UI element quad corners (all backends). */
struct UIVertex {
    float x, y;       // Screen position (pixels)
    float u, v;       // Texture UV coordinates
    float r, g, b, a; // RGBA color/tint
    float z;          // NDC depth for z-ordering
    float mode;       // Render mode: 0=sprite, 1=text, 2=line, 3=solid_color
};

#endif // UIVERTEX_H
