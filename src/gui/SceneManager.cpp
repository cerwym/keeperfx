/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file SceneManager.cpp
 *     SceneManager implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "gui/SceneManager.h"
#include "player_data.h"        // get_my_player()
#include "bflib_basics.h"       // ERRORLOG

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

SceneManager& SceneManager::get()
{
    static SceneManager s_instance;
    return s_instance;
}

// ---------------------------------------------------------------------------
// Stack operations
// ---------------------------------------------------------------------------

void SceneManager::push(std::unique_ptr<IScene> scene)
{
    if (!m_stack.empty())
        m_stack.back()->onPause();

    scene->onPush();
    m_stack.push_back(std::move(scene));
}

void SceneManager::pop()
{
    if (m_stack.empty())
    {
        ERRORLOG("SceneManager::pop() called on empty stack");
        return;
    }

    m_stack.back()->onPop();
    m_stack.pop_back();

    if (!m_stack.empty())
        m_stack.back()->onResume();
}

void SceneManager::replace(std::unique_ptr<IScene> scene)
{
    if (!m_stack.empty())
    {
        m_stack.back()->onPop();
        m_stack.pop_back();
    }

    scene->onPush();
    m_stack.push_back(std::move(scene));
}

void SceneManager::resetTo(std::unique_ptr<IScene> scene)
{
    // Pop all scenes without triggering onResume on intermediate ones.
    while (!m_stack.empty())
    {
        m_stack.back()->onPop();
        m_stack.pop_back();
    }

    scene->onPush();
    m_stack.push_back(std::move(scene));
}

bool SceneManager::empty() const
{
    return m_stack.empty();
}

int SceneManager::depth() const
{
    return (int)m_stack.size();
}

// ---------------------------------------------------------------------------
// Per-frame
// ---------------------------------------------------------------------------

void SceneManager::update(float global_dt)
{
    // Mirror legacy player view state into ClientViewState once per frame.
    // This is the ONLY place in the new layer that calls get_my_player().
    m_view.syncFromLegacy(get_my_player());

    // Tick all scenes bottom-to-top, each respecting its own Hz cap.
    for (auto& scene : m_stack)
    {
        const float hz = scene->m_tick_hz;
        if (hz <= 0.f)
        {
            // Uncapped — tick every frame with global dt.
            scene->tick(global_dt, m_view);
        }
        else
        {
            scene->m_tick_accum += global_dt;
            const float interval = 1.f / hz;
            if (scene->m_tick_accum >= interval)
            {
                scene->tick(interval, m_view);
                // Consume one interval; retain remainder to avoid drift.
                scene->m_tick_accum -= interval;
                // Guard against spiral-of-death if we fall very far behind.
                if (scene->m_tick_accum > interval * 4.f)
                    scene->m_tick_accum = 0.f;
            }
        }
    }
}

void SceneManager::draw()
{
    if (m_stack.empty())
        return;

    const DrawContext ctx = DrawContext::capture();

    // Draw bottom-to-top so higher scenes composite over lower ones.
    for (auto& scene : m_stack)
        scene->draw(ctx, m_view);
}

bool SceneManager::handleInput(const InputEvent& ev)
{
    if (m_stack.empty())
        return false;

    // Only the top-of-stack scene receives input.
    return m_stack.back()->handleInput(ev, m_view);
}
