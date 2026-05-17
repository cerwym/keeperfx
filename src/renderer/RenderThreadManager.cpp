/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RenderThreadManager.cpp
 *     Render-thread lifecycle implementation.
 * @see RenderThreadManager.h
 */
/******************************************************************************/
#include "renderer/RenderThreadManager.h"

/******************************************************************************/

RenderThreadManager::~RenderThreadManager()
{
    if (m_active)
        Stop();
}

void RenderThreadManager::Start(Fn init_fn, Fn work_fn, Fn cleanup_fn)
{
    if (m_active)
        return;

    m_active       = true;
    m_initialized  = false;
    m_quit         = false;
    m_work_ready   = false;
    m_work_done    = true;

    m_thread = std::thread(
        &RenderThreadManager::ThreadProc, this,
        std::move(init_fn), std::move(work_fn), std::move(cleanup_fn));

    // Block the game thread until init_fn() has completed on the render thread
    // (GL context acquired, Tracy initialised, etc.).
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]{ return m_initialized; });
}

void RenderThreadManager::WaitForCompletion()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]{ return m_work_done; });
}

void RenderThreadManager::Signal()
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_work_done  = false;
        m_work_ready = true;
    }
    m_cv.notify_one();
}

void RenderThreadManager::Stop()
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_quit = true;
    }
    m_cv.notify_one();
    m_thread.join();
    m_active = false;
}

void RenderThreadManager::ThreadProc(Fn init_fn, Fn work_fn, Fn cleanup_fn)
{
    init_fn();

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_initialized = true;
    }
    m_cv.notify_one();

    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]{ return m_work_ready || m_quit; });
            if (m_quit) break;
            m_work_ready = false;
        }

        work_fn();

        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_work_done = true;
        }
        m_cv.notify_one();
    }

    cleanup_fn();
}

/******************************************************************************/
