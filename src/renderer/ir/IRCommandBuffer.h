/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IRCommandBuffer.h
 *     Append-only per-frame command buffer for the intermediate representation.
 * @par Purpose:
 *     Each logical renderer (world, UI, text, shadow, debug) writes commands
 *     into its own IRCommandBuffer during the game-thread frame update.
 *     At EndFrame / RenderGraph::Flip() the buffers are swapped atomically
 *     and the render thread reads the read-side copy.
 *
 *     Design properties:
 *       - Single writer (game thread), single reader (render thread).
 *       - Append-only during a frame; Reset() at BeginFrame keeps capacity.
 *       - No allocations after the first warm-up frame (pre-Reserve at init).
 *       - Trivially copyable commands only; no heap allocations inside T.
 */
/******************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/******************************************************************************/

template<typename T>
class IRCommandBuffer
{
public:
    IRCommandBuffer() = default;

    /** Pre-allocate capacity for @p n commands (call once at startup). */
    void Reserve(size_t n) { m_cmds.reserve(n); }

    /** Append a command; returns a reference to the stored copy. */
    T& Append(const T& cmd)
    {
        m_cmds.push_back(cmd);
        return m_cmds.back();
    }

    /** Append a default-constructed command; caller fills fields. */
    T& AppendEmpty()
    {
        m_cmds.emplace_back();
        return m_cmds.back();
    }

    /** Reset without freeing backing memory (preserves capacity). */
    void Reset() { m_cmds.clear(); }

    /** Swap contents with another buffer (O(1), used by FlipBuffers). */
    void Swap(IRCommandBuffer<T>& other) { m_cmds.swap(other.m_cmds); }

    /* Read-side iteration (render thread). */
    const T*  Data()  const { return m_cmds.data(); }
    size_t    Size()  const { return m_cmds.size(); }
    bool      Empty() const { return m_cmds.empty(); }

    typename std::vector<T>::const_iterator begin() const { return m_cmds.begin(); }
    typename std::vector<T>::const_iterator end()   const { return m_cmds.end(); }

private:
    std::vector<T> m_cmds;
};

/******************************************************************************/
