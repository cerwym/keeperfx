/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererThread.h
 *     Debug-mode thread-ownership assertions for the renderer.
 * @par Purpose:
 *     Every renderer field must be annotated with one of:
 *       // GT:   game thread only — written and read on the game thread
 *       // RT:   render thread only — written and read on the render thread
 *       // GT→RT: transferred atomically at Flip() / FlipBuffers()
 *
 *     In DEBUG builds, use ASSERT_GAME_THREAD() / ASSERT_RENDER_THREAD() at
 *     the top of every method that has a clear ownership contract.  The macros
 *     are no-ops in Release, so there is zero runtime cost in shipped builds.
 *
 *     Register both thread IDs at renderer initialisation:
 *       RendererThread_RegisterGameThread()   — call from main thread Init()
 *       RendererThread_RegisterRenderThread() — call from render thread startup
 */
/******************************************************************************/
#pragma once

#include <thread>

/******************************************************************************/

/** Register the calling thread as the game (main) thread. */
void RendererThread_RegisterGameThread();

/** Register the calling thread as the render thread. */
void RendererThread_RegisterRenderThread();

/** Returns true if the calling thread is the registered game thread. */
bool RendererThread_IsGameThread();

/** Returns true if the calling thread is the registered render thread. */
bool RendererThread_IsRenderThread();

/******************************************************************************/

#if DEBUG
#  include <cassert>
#  define ASSERT_GAME_THREAD()   do { assert(RendererThread_IsGameThread());   } while (0)
#  define ASSERT_RENDER_THREAD() do { assert(RendererThread_IsRenderThread()); } while (0)
#else
#  define ASSERT_GAME_THREAD()   do {} while (0)
#  define ASSERT_RENDER_THREAD() do {} while (0)
#endif

/******************************************************************************/
