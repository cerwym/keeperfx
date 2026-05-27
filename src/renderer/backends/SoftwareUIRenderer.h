/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareUIRenderer.h
 *     Trivial subclass of IUIRenderer used by the software renderer path.
 * @par Design:
 *     All CPU fallback logic now lives in IUIRenderer (the base).  This class
 *     exists only to supply a distinct GetName() so log output is readable.
 */
/******************************************************************************/
#pragma once

#include "renderer/IUIRenderer.h"

/** Software UI renderer — inherits all CPU fallback logic from IUIRenderer. */
class SoftwareUIRenderer final : public IUIRenderer {
public:
    const char* GetName() const override { return "SOFTWARE_UI"; }
};