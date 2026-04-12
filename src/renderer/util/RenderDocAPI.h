/******************************************************************************/
/** @file RenderDocAPI.h
 *   Minimal runtime wrapper around the RenderDoc in-application API.
 *
 *   RenderDoc injects itself into the process when launched as the capture
 *   host and exports RENDERDOC_GetAPI() via the DLL.  We detect this at
 *   runtime using GetModuleHandleA("renderdoc.dll") so no linking is required.
 *   All public functions silently no-op when RenderDoc is not present.
 */
/******************************************************************************/
#pragma once

#ifdef _WIN32

namespace RenderDocAPI {

/// Called once immediately after gladLoadGLLoader().  Detects if the process
/// is running under RenderDoc and caches the API function table.
void Init();

/// Returns true when a RenderDoc API pointer was successfully obtained.
bool IsActive();

/// Ask RenderDoc to capture the next complete frame.
void TriggerCapture();

/// Bracket-style manual capture (surround multi-frame work if needed).
void StartFrameCapture();
void EndFrameCapture();

/// Set the filesystem path template for saved .rdc files (no extension).
/// Called once at init; e.g. "keeperfx_capture" → keeperfx_capture_0001.rdc
void SetCapturePathTemplate(const char* path_template);

/// Set a human-readable title for the next triggered capture.
/// Useful for tagging captures by game turn: "Turn 42".
void SetCaptureTitle(const char* title);

} // namespace RenderDocAPI

#else // !_WIN32 — no-op inline stubs

namespace RenderDocAPI {
    inline void Init()                              {}
    inline bool IsActive()                          { return false; }
    inline void TriggerCapture()                    {}
    inline void StartFrameCapture()                 {}
    inline void EndFrameCapture()                   {}
    inline void SetCapturePathTemplate(const char*) {}
    inline void SetCaptureTitle(const char*)        {}
} // namespace RenderDocAPI

#endif // _WIN32
