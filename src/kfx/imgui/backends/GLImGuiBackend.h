/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file GLImGuiBackend.h
 *     OpenGL implementation of kfx::IImGuiRenderBackend.
 *
 *     The only file in src/kfx/imgui/ that includes imgui_impl_opengl3.h and
 *     binds to a real GL/SDL native handle. A future non-GL renderer backend
 *     writes its own analogous ..ImGuiBackend implementing the same
 *     interface, without touching anything else under src/kfx/imgui/.
 */
/******************************************************************************/
#ifndef KEEPERFX_KFX_IMGUI_BACKENDS_GLIMGUIBACKEND_H
#define KEEPERFX_KFX_IMGUI_BACKENDS_GLIMGUIBACKEND_H

#include "kfx/imgui/IImGuiRenderBackend.h"

namespace kfx {

class GLImGuiBackend : public IImGuiRenderBackend {
public:
    /** sdl_window: the real SDL_Window*, cast to void* (see
     *  IWindowSystem::GetNativeWindowHandle()). sdl_gl_context: the real
     *  SDL_GLContext, cast to void* (see IGLContext::GetNativeContextHandle()). */
    GLImGuiBackend(void* sdl_window, void* sdl_gl_context);

    bool Init() override;
    void Shutdown() override;
    void NewFrame() override;
    void RenderDrawData() override;
    void ProcessEvent(const void* sdl_event) override;

private:
    void* m_window;
    void* m_gl_context;
};

} // namespace kfx

#endif /* KEEPERFX_KFX_IMGUI_BACKENDS_GLIMGUIBACKEND_H */
