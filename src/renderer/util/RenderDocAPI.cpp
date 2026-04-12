/******************************************************************************/
/** @file RenderDocAPI.cpp
 *   Runtime detection and wrapper for the RenderDoc in-application API.
 *
 *   On Windows: queries "renderdoc.dll" via GetModuleHandleA at runtime.
 *   On other platforms: all calls compile to no-ops (inline stubs in .h).
 *
 *   renderdoc_app.h is the official single-header API from:
 *   https://github.com/baldurk/renderdoc (MIT License, © Baldur Karlsson)
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/util/RenderDocAPI.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "renderer/util/renderdoc_app.h"  // vendored from RenderDoc repo
#include "bflib_basics.h"                   // SYNCLOG / WARNLOG

#include "post_inc.h"

// ─────────────────────────────────────────────────────────────────────────────

static RENDERDOC_API_1_6_0* s_rdoc = nullptr;

void RenderDocAPI::Init()
{
    HMODULE mod = GetModuleHandleA("renderdoc.dll");
    if (!mod)
        return;  // not running under RenderDoc — all calls will be no-ops

    pRENDERDOC_GetAPI get_api =
        (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
    if (!get_api)
    {
        LbWarnLog("%s: found renderdoc.dll but RENDERDOC_GetAPI not exported\n", __func__);
        return;
    }

    int ok = get_api(eRENDERDOC_API_Version_1_6_0, (void**)&s_rdoc);
    if (ok != 1 || !s_rdoc)
    {
        LbWarnLog("%s: RENDERDOC_GetAPI returned %d\n", __func__, ok);
        s_rdoc = nullptr;
        return;
    }

    // Disable the default RenderDoc overlay (FPS counter) — it overlaps the game HUD.
    s_rdoc->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);

    // Save captures alongside the executable by default.
    s_rdoc->SetCaptureFilePathTemplate("keeperfx_capture");

    LbSyncLog("%s: API v1.6.0 obtained - programmatic capture enabled\n", __func__);
}

bool RenderDocAPI::IsActive()
{
    return s_rdoc != nullptr;
}

void RenderDocAPI::TriggerCapture()
{
    if (s_rdoc) s_rdoc->TriggerCapture();
}

void RenderDocAPI::StartFrameCapture()
{
    if (s_rdoc) s_rdoc->StartFrameCapture(nullptr, nullptr);
}

void RenderDocAPI::EndFrameCapture()
{
    if (s_rdoc) s_rdoc->EndFrameCapture(nullptr, nullptr);
}

void RenderDocAPI::SetCapturePathTemplate(const char* path_template)
{
    if (s_rdoc && path_template) s_rdoc->SetCaptureFilePathTemplate(path_template);
}

void RenderDocAPI::SetCaptureTitle(const char* title)
{
    if (s_rdoc && title)
        s_rdoc->SetCaptureTitle(title);
}

#else // !_WIN32 — non-Windows stubs (already inlined as no-ops in the header)

#include "post_inc.h"

// The non-Windows implementations are empty inline functions defined in
// RenderDocAPI.h (guarded by !_WIN32), so no definitions needed here.

#endif // _WIN32
