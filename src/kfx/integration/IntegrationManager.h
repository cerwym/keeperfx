/******************************************************************************/
/** @file IntegrationManager.h
 *     Platform integration manager for KeeperFX.
 * @par Purpose:
 *     Owns the lifecycle and command dispatch for all external platform
 *     backends (GOG Galaxy, Steam, future Epic/Xbox). Provides a unified
 *     thread-safe command queue with per-backend worker threads for SDKs
 *     that require thread affinity.
 * @author   Peter Lockett & KeeperFX Team
 * @date     25 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef INTEGRATION_MANAGER_H
#define INTEGRATION_MANAGER_H

#include "../achievement/achievement_api.h"

#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <string>
#include <functional>
#include <atomic>

/******************************************************************************/
/// Command types that can be enqueued for backend processing
enum class IntegrationCmdType {
    Unlock,
    Clear,
    SetProgress,
    Shutdown,
};

/// A single command destined for one or more backends
struct IntegrationCommand {
    IntegrationCmdType type;
    char id[ACHIEVEMENT_ID_LEN];
    float value;  ///< For SetProgress
    int retries;  ///< Number of retry attempts remaining
};

/******************************************************************************/
/// Wraps one AchievementBackend with its own optional worker thread and queue.
struct BackendWorker {
    struct AchievementBackend* backend;

    // Per-backend queue (used when backend has its own worker thread)
    std::deque<IntegrationCommand> queue;
    std::mutex mtx;
    std::condition_variable cv;
    std::jthread thread;
    std::atomic<bool> initialized{false};

    explicit BackendWorker(struct AchievementBackend* b) : backend(b) {}
    BackendWorker(BackendWorker&&) = default;
    BackendWorker& operator=(BackendWorker&&) = default;
};

/******************************************************************************/
/// Singleton manager for all platform integrations.
/// Thread-safe: enqueue methods are called from the game thread,
/// processing happens on per-backend worker threads.
class IntegrationManager {
public:
    static IntegrationManager& instance();

    /// Register a backend. Call before init(). Thread-safe.
    void register_backend(struct AchievementBackend* backend);

    /// Start all registered backends. Backends with needs_worker_thread get
    /// a dedicated jthread; others are initialized on the calling thread.
    void init();

    /// Stop all backends and join worker threads.
    void shutdown();

    /// Enqueue commands for all active backends (fire-and-forget from game thread).
    void enqueue_unlock(const char* achievement_id);
    void enqueue_clear(const char* achievement_id);
    void enqueue_set_progress(const char* achievement_id, float progress);

    /// Pump non-threaded backends (call from game loop).
    void sync();

    /// Returns true if at least one backend is active.
    bool has_active_backends() const;

private:
    IntegrationManager() = default;
    ~IntegrationManager() = default;
    IntegrationManager(const IntegrationManager&) = delete;
    IntegrationManager& operator=(const IntegrationManager&) = delete;

    /// Worker thread entry point for backends that need thread affinity
    static void worker_loop(std::stop_token stop, BackendWorker* bw);

    /// Dispatch a single command to a backend. Returns true on success.
    static bool dispatch(struct AchievementBackend* backend, const IntegrationCommand& cmd);

    std::vector<std::unique_ptr<BackendWorker>> backends_;
    bool running_ = false;
    mutable std::mutex reg_mtx_; ///< Protects backends_ during registration
};

/******************************************************************************/
/// C-callable bridge (for use from achievement_api.c and main.cpp)
#ifdef __cplusplus
extern "C" {
#endif

void integration_manager_register_backend(struct AchievementBackend* backend);
void integration_manager_init(void);
void integration_manager_shutdown(void);
void integration_manager_unlock(const char* achievement_id);
void integration_manager_clear(const char* achievement_id);
void integration_manager_set_progress(const char* achievement_id, float progress);
void integration_manager_sync(void);

#ifdef __cplusplus
}
#endif

#endif // INTEGRATION_MANAGER_H
