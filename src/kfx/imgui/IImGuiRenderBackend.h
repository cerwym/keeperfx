/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file IImGuiRenderBackend.h
 *     Abstract renderer-API binding for the ImGui debug overlay.
 *
 *     This is the entire surface a renderer backend implements to get ImGui
 *     support: construct one, hand it to kfx::DevTools::instance().initOverlay(),
 *     and the rest of the tooling (event queue, context lifecycle, the
 *     overlay's own ImGui widgets) never needs to know which graphics API is
 *     in use. GLImGuiBackend is the only implementation today.
 *
 *     Deliberately free of any ImGui or graphics-API include: this header is
 *     meant to be included from a renderer's own .cpp without dragging in
 *     ImGui itself.
 */
/******************************************************************************/
#ifndef KEEPERFX_KFX_IMGUI_IIMGUIRENDERBACKEND_H
#define KEEPERFX_KFX_IMGUI_IIMGUIRENDERBACKEND_H

namespace kfx {

class IImGuiRenderBackend {
public:
    virtual ~IImGuiRenderBackend() = default;

    /** Bind ImGui to this backend's windowing/graphics API. Called once, on
     *  the thread that owns the graphics context, after ImGui::CreateContext()
     *  already exists. Returns false on failure. */
    virtual bool Init() = 0;

    /** Unbind ImGui from this backend. Called once, on the same thread Init()
     *  ran on, before ImGui::DestroyContext(). */
    virtual void Shutdown() = 0;

    /** Start a new ImGui frame on this backend (platform + renderer side). */
    virtual void NewFrame() = 0;

    /** Render whatever ImGui::Render() produced (reads ImGui::GetDrawData()
     *  itself -- no ImGui type crosses this interface). */
    virtual void RenderDrawData() = 0;

    /** Forward one SDL event to this backend's platform binding. */
    virtual void ProcessEvent(const void* sdl_event) = 0;
};

} // namespace kfx

#endif /* KEEPERFX_KFX_IMGUI_IIMGUIRENDERBACKEND_H */
