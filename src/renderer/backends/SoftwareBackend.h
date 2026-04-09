/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareBackend.h
 *     Trivial subclass of IBackend used by the software renderer path.
 * @par Design:
 *     All CPU fallback logic now lives in IBackend (the base).  This class
 *     exists only to supply a distinct GetName() so log output is readable.
 */
/******************************************************************************/
#ifndef SOFTWARE_BACKEND_H
#define SOFTWARE_BACKEND_H

#include "IBackend.h"

/** Software sprite backend — inherits all CPU fallback logic from IBackend. */
class SoftwareBackend final : public IBackend {
public:
    const char* GetName() const override { return "SOFTWARE"; }
};

#endif // SOFTWARE_BACKEND_H
