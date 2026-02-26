/******************************************************************************/
/** @file IntegrationManager.cpp
 *     Platform integration manager implementation.
 * @par Purpose:
 *     Thread-safe command queue and per-backend worker thread management.
 * @author   Peter Lockett & KeeperFX Team
 * @date     25 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "../../pre_inc.h"
#include "IntegrationManager.h"
#include "GogBackend.h"
#include "SteamBackend.h"
#include "../../bflib_basics.h"
#include "../../post_inc.h"

#include <cstring>

/******************************************************************************/
IntegrationManager& IntegrationManager::instance()
{
    static IntegrationManager inst;
    return inst;
}

void IntegrationManager::register_backend(struct AchievementBackend* backend)
{
    if (!backend)
        return;
    std::lock_guard lock(reg_mtx_);
    backends_.push_back(std::make_unique<BackendWorker>(backend));
    SYNCLOG("IntegrationManager: registered backend '%s' (worker_thread=%d)",
            backend->name, (int)backend->needs_worker_thread);
}

void IntegrationManager::init()
{
    // Discover and register all available platform backends
    steam_backend_register();
    gog_backend_register();

    std::lock_guard lock(reg_mtx_);
    if (running_)
        return;

    SYNCLOG("IntegrationManager: initializing %u backends", (unsigned)backends_.size());

    for (auto& bw : backends_)
    {
        if (bw->backend->needs_worker_thread)
        {
            // Spawn a dedicated worker thread — init happens on the worker
            bw->thread = std::jthread([bw_ptr = bw.get()](std::stop_token st) {
                worker_loop(st, bw_ptr);
            });
            SYNCLOG("IntegrationManager: spawned worker thread for '%s'", bw->backend->name);
        }
        else
        {
            // Init on current thread
            if (bw->backend->init)
            {
                if (bw->backend->init())
                {
                    bw->initialized.store(true);
                    SYNCLOG("IntegrationManager: initialized '%s' on main thread", bw->backend->name);
                }
                else
                {
                    WARNLOG("IntegrationManager: failed to initialize '%s'", bw->backend->name);
                }
            }
        }
    }

    running_ = true;
}

void IntegrationManager::shutdown()
{
    std::lock_guard lock(reg_mtx_);
    if (!running_)
        return;

    SYNCLOG("IntegrationManager: shutting down");

    for (auto& bw : backends_)
    {
        if (bw->backend->needs_worker_thread && bw->thread.joinable())
        {
            // Signal the worker to stop and wake it up
            {
                std::lock_guard q_lock(bw->mtx);
                IntegrationCommand cmd{};
                cmd.type = IntegrationCmdType::Shutdown;
                bw->queue.push_back(cmd);
            }
            bw->cv.notify_one();
            bw->thread.request_stop();
            bw->thread.join();
        }
        else if (bw->initialized.load())
        {
            // Shutdown on main thread
            if (bw->backend->shutdown)
                bw->backend->shutdown();
        }
        bw->initialized.store(false);
    }

    backends_.clear();
    running_ = false;
}

void IntegrationManager::enqueue_unlock(const char* achievement_id)
{
    IntegrationCommand cmd{};
    cmd.type = IntegrationCmdType::Unlock;
    cmd.retries = 10;
    std::strncpy(cmd.id, achievement_id, sizeof(cmd.id) - 1);

    std::lock_guard lock(reg_mtx_);
    for (auto& bw : backends_)
    {
        if (bw->backend->needs_worker_thread)
        {
            std::lock_guard q_lock(bw->mtx);
            bw->queue.push_back(cmd);
            bw->cv.notify_one();
        }
        else if (bw->initialized.load() && bw->backend->unlock)
        {
            bw->backend->unlock(achievement_id);
        }
    }
}

void IntegrationManager::enqueue_clear(const char* achievement_id)
{
    IntegrationCommand cmd{};
    cmd.type = IntegrationCmdType::Clear;
    cmd.retries = 10;
    std::strncpy(cmd.id, achievement_id, sizeof(cmd.id) - 1);

    std::lock_guard lock(reg_mtx_);
    for (auto& bw : backends_)
    {
        if (bw->backend->needs_worker_thread)
        {
            std::lock_guard q_lock(bw->mtx);
            bw->queue.push_back(cmd);
            bw->cv.notify_one();
        }
        else if (bw->initialized.load() && bw->backend->clear)
        {
            bw->backend->clear(achievement_id);
        }
    }
}

void IntegrationManager::enqueue_set_progress(const char* achievement_id, float progress)
{
    IntegrationCommand cmd{};
    cmd.type = IntegrationCmdType::SetProgress;
    cmd.retries = 10;
    std::strncpy(cmd.id, achievement_id, sizeof(cmd.id) - 1);
    cmd.value = progress;

    std::lock_guard lock(reg_mtx_);
    for (auto& bw : backends_)
    {
        if (bw->backend->needs_worker_thread)
        {
            std::lock_guard q_lock(bw->mtx);
            bw->queue.push_back(cmd);
            bw->cv.notify_one();
        }
        else if (bw->initialized.load() && bw->backend->set_progress)
        {
            bw->backend->set_progress(achievement_id, progress);
        }
    }
}

void IntegrationManager::sync()
{
    std::lock_guard lock(reg_mtx_);
    for (auto& bw : backends_)
    {
        // Only pump non-threaded backends — threaded ones pump themselves
        if (!bw->backend->needs_worker_thread && bw->initialized.load() && bw->backend->sync)
        {
            bw->backend->sync();
        }
    }
}

bool IntegrationManager::has_active_backends() const
{
    std::lock_guard lock(reg_mtx_);
    for (auto& bw : backends_)
    {
        if (bw->initialized.load())
            return true;
    }
    return false;
}

/******************************************************************************/
/// Worker thread loop for backends that require thread affinity (e.g. GOG Galaxy).
/// Calls init on the worker thread, then processes commands until shutdown.
void IntegrationManager::worker_loop(std::stop_token stop, BackendWorker* bw)
{
    // Init on this thread (thread affinity for SDK calls)
    if (bw->backend->init && bw->backend->init())
    {
        bw->initialized.store(true);
        SYNCLOG("IntegrationManager: worker '%s' initialized", bw->backend->name);
    }
    else
    {
        WARNLOG("IntegrationManager: worker '%s' init failed", bw->backend->name);
        return;
    }

    while (!stop.stop_requested())
    {
        // Drain command queue
        std::deque<IntegrationCommand> batch;
        {
            std::unique_lock lock(bw->mtx);
            bw->cv.wait_for(lock, std::chrono::milliseconds(50),
                            [&] { return !bw->queue.empty() || stop.stop_requested(); });
            std::swap(batch, bw->queue);
        }

        for (auto& cmd : batch)
        {
            if (cmd.type == IntegrationCmdType::Shutdown)
                goto done;
            if (!dispatch(bw->backend, cmd) && cmd.retries > 0)
            {
                // Re-enqueue for retry on next pump cycle
                cmd.retries--;
                std::lock_guard q_lock(bw->mtx);
                bw->queue.push_back(cmd);
            }
        }

        // Pump SDK callbacks
        if (bw->backend->sync)
            bw->backend->sync();
    }

done:
    if (bw->backend->shutdown)
        bw->backend->shutdown();
    bw->initialized.store(false);
    SYNCLOG("IntegrationManager: worker '%s' shut down", bw->backend->name);
}

bool IntegrationManager::dispatch(struct AchievementBackend* backend, const IntegrationCommand& cmd)
{
    switch (cmd.type)
    {
    case IntegrationCmdType::Unlock:
        if (backend->unlock)
            return backend->unlock(cmd.id);
        break;
    case IntegrationCmdType::Clear:
        if (backend->clear)
            return backend->clear(cmd.id);
        break;
    case IntegrationCmdType::SetProgress:
        if (backend->set_progress)
            return backend->set_progress(cmd.id, cmd.value);
        break;
    default:
        break;
    }
    return true;
}

/******************************************************************************/
/// C-callable bridge functions

extern "C" void integration_manager_register_backend(struct AchievementBackend* backend)
{
    IntegrationManager::instance().register_backend(backend);
}

extern "C" void integration_manager_init(void)
{
    IntegrationManager::instance().init();
}

extern "C" void integration_manager_shutdown(void)
{
    IntegrationManager::instance().shutdown();
}

extern "C" void integration_manager_unlock(const char* achievement_id)
{
    IntegrationManager::instance().enqueue_unlock(achievement_id);
}

extern "C" void integration_manager_clear(const char* achievement_id)
{
    IntegrationManager::instance().enqueue_clear(achievement_id);
}

extern "C" void integration_manager_set_progress(const char* achievement_id, float progress)
{
    IntegrationManager::instance().enqueue_set_progress(achievement_id, progress);
}

extern "C" void integration_manager_sync(void)
{
    IntegrationManager::instance().sync();
}

/******************************************************************************/
