/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RenderThreadManager.h
 *     Owns the render-thread lifecycle: spawn, per-frame signal/wait, stop.
 * @par Purpose:
 *     Encapsulates the mutex/condition-variable handshake that drives the
 *     dedicated render thread.  Any backend that wants a render thread
 *     (OpenGL, GLES2, Vulkan, …) can use this class instead of duplicating
 *     the synchronisation boilerplate.
 *
 * @par Usage:
 *     Typical per-backend setup in EndFrame() / Init():
 *     @code
 *       m_thread_mgr.Start(
 *           [this]{ platform_gl_acquire_context(); TracyGpuInit(); },
 *           [this]{ EndFrame_GL(); },
 *           [this]{ platform_gl_release_context(); }
 *       );
 *     @endcode
 *
 *     Per-frame protocol (called from the game thread):
 *     @code
 *       m_thread_mgr.WaitForCompletion();   // block until previous frame done
 *       ... snapshot command buffers ...
 *       m_thread_mgr.Signal();              // wake render thread for this frame
 *     @endcode
 *
 * @par Thread safety:
 *     All public methods are called from the game thread only, except that
 *     init_fn / work_fn / cleanup_fn execute on the render thread.
 */
/******************************************************************************/
#pragma once

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

/** True on the dedicated render thread; false on all other threads.
 *  Set once at thread startup in ThreadProc() — read-only thereafter.
 *  Use to guard state mutations that must not cross the game/render thread boundary. */
extern thread_local bool g_on_render_thread;

/******************************************************************************/

class RenderThreadManager
{
public:
    using Fn = std::function<void()>;

    RenderThreadManager() = default;

    /** Calls Stop() if the thread is still active. */
    ~RenderThreadManager();

    RenderThreadManager(const RenderThreadManager&)            = delete;
    RenderThreadManager& operator=(const RenderThreadManager&) = delete;

    /** Spawn the render thread and block until init_fn() completes.
     *
     *  @param init_fn     Called once at thread startup before the work loop.
     *                     Use to acquire the GL context, init Tracy, etc.
     *                     Start() blocks until it returns.
     *  @param work_fn     Called on the render thread each time Signal() is
     *                     issued.  Corresponds to EndFrame_GL().
     *  @param cleanup_fn  Called once on the render thread after the quit
     *                     signal is received, before the thread exits.
     *                     Use to release the GL context.
     *
     *  No-op if IsActive() is already true.
     */
    void Start(Fn init_fn, Fn work_fn, Fn cleanup_fn);

    /** Block the game thread until the render thread finishes work_fn().
     *  Pass-through on the very first call (m_work_done starts true so no
     *  previous frame needs to be awaited). */
    void WaitForCompletion();

    /** Signal the render thread to execute work_fn() for this frame.
     *  Clears the done flag and sets the ready flag atomically under the mutex,
     *  then notifies the render thread. */
    void Signal();

    /** Stop the render thread gracefully.
     *  Signals quit, joins the thread (cleanup_fn runs on the render thread
     *  before it exits), and resets internal state to allow a future Start(). */
    void Stop();

    bool IsActive() const { return m_active; }

private:
    void ThreadProc(Fn init_fn, Fn work_fn, Fn cleanup_fn);

    std::thread             m_thread;
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    bool m_work_ready  = false;
    bool m_work_done   = true;   // starts true: first WaitForCompletion() returns immediately
    bool m_quit        = false;
    bool m_initialized = false;
    bool m_active      = false;
};

/******************************************************************************/
