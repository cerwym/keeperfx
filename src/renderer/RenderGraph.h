/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RenderGraph.h
 *     Unified render graph: owns all per-renderer IR command buffers,
 *     performs the double-buffer flip, and dispatches to backend executors.
 * @par Purpose:
 *     The game thread writes IR commands into the write-side buffers via the
 *     Get*Buffer() accessors during its frame update.  At present time
 *     (RendererPresentFrame → EndFrame), RenderGraph::Flip() atomically swaps
 *     all write-side buffers with the read-side copies and captures a
 *     FrameState snapshot.  The render thread then calls Execute() to consume
 *     the read-side snapshot without touching any game-thread state.
 *
 * @par Threading contract:
 *     - All Get*Buffer() / Append() calls: GAME THREAD only, before Flip().
 *     - Flip(): GAME THREAD, exactly once per frame (inside EndFrame).
 *     - Execute() / Get*BufferRT(): RENDER THREAD only, after Flip() signal.
 *     - No locks are needed here; the caller guarantees the phase ordering via
 *       the existing m_rt_mutex / m_rt_cv frame sync in RendererOpenGL.
 *
 * @par Graceful degradation:
 *     Execute() accepts a BackendCapabilities reference.  If a backend does not
 *     support a particular buffer (e.g. no GPU debug overlay), that dispatcher
 *     is simply skipped.  No per-backend identity checks are needed.
 */
/******************************************************************************/
#pragma once

#include <optional>
#include "renderer/FrameState.h"
#include "renderer/BackendCapabilities.h"
#include "renderer/IShadowRenderer.h"
#include "renderer/ir/WorldCommands.h"
#include "renderer/ir/UICommands.h"
#include "renderer/ir/TextCommands.h"
#include "renderer/ir/ShadowCommands.h"
#include "renderer/ir/PostProcessCommands.h"
#include "renderer/ir/ImagePresentCommands.h"

/******************************************************************************/

class IFrameGraphExecutor;

/******************************************************************************/

/**
 * Owns all per-renderer double-buffered IR command buffers.
 *
 * One instance lives as a member of RendererOpenGL (or any future
 * multi-threaded backend).  Software / single-threaded backends may also use
 * it — Flip() is a safe no-op when there is only one thread.
 */
class RenderGraph
{
public:
    RenderGraph();
    ~RenderGraph() = default;

    // =========================================================================
    // Capacity pre-allocation (call once after renderer init)

    /** Reserve backing memory for expected peak commands per frame. */
    void Reserve(size_t world_tiles, size_t world_sprites, size_t world_shadows,
                 size_t ui_cmds,     size_t text_cmds,
                 size_t shadow_cmds);

    // =========================================================================
    // Game-thread write accessors

    WorldCommandBuffers&       GetWorldBuffers()       { return m_write.world;        }
    UICommandBuffers&          GetUIBuffers()          { return m_write.ui;           }
    TextCommandBuffers&        GetTextBuffers()        { return m_write.text;         }
    ShadowCommandBuffers&      GetShadowBuffers()      { return m_write.shadow;       }
    PostProcessCommandBuffers& GetPostProcessBuffers() { return m_write.post_process; }
    ImagePresentBuffers&       GetImagePresentBuffers(){ return m_write.image_present; }

    /** Read-only access to the write-side UI buffer (game thread, before Flip).
     *  Used by EndFrame() to detect empty frames and skip Flip(). */
    const UICommandBuffers& GetWriteUIBuffers() const { return m_write.ui; }

    // =========================================================================
    // Frame lifecycle (game thread)

    /**
     * Reset all write-side buffers for a new frame.
     * Call at BeginFrame() before any Submit* calls.
     */
    void BeginFrame();

    /**
     * Atomically swap write↔read buffers and capture a FrameState snapshot.
     *
     * After this call the game thread may start writing into the (now-cleared)
     * write side for frame N+1 while the render thread processes the read side
     * for frame N.
     *
     * @param fs  Game-thread globals to snapshot (palette, tints, screen dims,
     *            lens mode).  Caller captures these immediately before calling
     *            Flip().
     */
    void Flip(const FrameState& fs);

    /**
     * Update only the read-side FrameState without swapping IR buffers.
     *
     * Used during palette-fade loops (RendererIsFadeCachePreserved() == true)
     * where the IR content from the previous real frame must be preserved so
     * the render thread can re-draw the same UI geometry with an updated
     * palette.  Flip() is skipped; only m_read_fs is updated.
     *
     * @param fs  Updated FrameState (palette, tints) captured this fade step.
     */
    void UpdateFrameState(const FrameState& fs);

    // =========================================================================
    // Render-thread read accessors (valid after Flip(), before next Flip())

    const WorldCommandBuffers&       GetWorldBuffersRT()       const { return m_read.world;        }
    const UICommandBuffers&          GetUIBuffersRT()          const { return m_read.ui;           }
    const TextCommandBuffers&        GetTextBuffersRT()        const { return m_read.text;         }
    const ShadowCommandBuffers&      GetShadowBuffersRT()      const { return m_read.shadow;       }
    const PostProcessCommandBuffers& GetPostProcessBuffersRT() const { return m_read.post_process; }
    const ImagePresentBuffers&       GetImagePresentBuffersRT()const { return m_read.image_present; }
    const FrameState&                GetFrameStateRT()         const { return m_read_fs;           }

    // =========================================================================
    // Render-thread execution

    /**
     * Drive one frame's ordered execution through a backend realizer.
     *
     */
    void Execute(IFrameGraphExecutor& exec);

private:
    // -------------------------------------------------------------------------
    // All command buffers for one frame side.

    struct FrameBuffers
    {
        WorldCommandBuffers       world;
        UICommandBuffers          ui;
        TextCommandBuffers        text;
        ShadowCommandBuffers      shadow;
        PostProcessCommandBuffers post_process;
        ImagePresentBuffers       image_present;

        void Reset();
        void Reserve(size_t world_tiles, size_t world_sprites, size_t world_shadows,
                     size_t ui_cmds,     size_t text_cmds,
                     size_t shadow_cmds);
        void Swap(FrameBuffers& other);
    };

    FrameBuffers m_write;     /**< Written by game thread.  */
    FrameBuffers m_read;      /**< Read by render thread.   */
    FrameState   m_read_fs;   /**< Snapshot captured at Flip(). */
};

/******************************************************************************/
