// Minimal native "open file" dialog helper for the debug tools.
//
// Isolated in its own translation unit so the platform headers (windows.h and
// its macro soup) never leak into the ImGui panel sources. On platforms without
// a native picker the function simply returns false and callers fall back to the
// manual path text field.
#pragma once

#include <cstddef>

namespace kfx {

// Opens a modal "open file" dialog.
//   title       - dialog caption (may be nullptr).
//   initialDir  - folder to start in (may be nullptr / empty).
//   filter      - human|pattern pairs separated by '|', e.g.
//                 "FXSPR sprites|*.fxspr|All files|*.*". May be nullptr.
//   outPath     - receives the chosen absolute path (NUL-terminated).
//   outSize     - size of outPath in bytes.
// Returns true if the user picked a file, false on cancel / unsupported.
bool OpenFileDialog(const char* title, const char* initialDir, const char* filter,
                    char* outPath, std::size_t outSize);

} // namespace kfx
