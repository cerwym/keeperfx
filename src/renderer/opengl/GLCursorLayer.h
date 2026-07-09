/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLCursorLayer.h
 *     OpenGL implementation of ICursorLayer.
 */
/******************************************************************************/
#pragma once

#ifdef RENDERER_OPENGL_ENABLED

#include "renderer/ICursorLayer.h"
#include "renderer/ir/UICommands.h"
#include "renderer/SpriteHandle.h"
#include <vector>

struct TbSprite;
class GLWorldViewRenderer;
class GLSpriteAtlas;
class GLUIRenderer;
typedef unsigned int GLuint;

class GLCursorLayer : public ICursorLayer {
public:
    GLCursorLayer() = default;
    virtual ~GLCursorLayer() = default;

    /** Set the world-view renderer used to drive keeper-sprite GL state.
     *  Must be called before the first Draw().  Not owned. */
    void SetWorldViewRenderer(GLWorldViewRenderer* wvr) { m_wvr = wvr; }

    /** Set the sprite atlas used to resolve pointer sprites to UV coordinates.
     *  Must be called before the first Draw().  Not owned. */
    void SetSpriteAtlas(GLSpriteAtlas* atlas) { m_atlas = atlas; }

    /** Set the GLUIRenderer used to draw atlas quads for pointer sprites.
     *  Must be called before the first Draw().  Not owned. */
    void SetGLUIRenderer(GLUIRenderer* glui) { m_glui = glui; }

    // ICursorLayer interface
    virtual void SubmitPointerSprite(const TbSprite* spr,
                                     int32_t x, int32_t y,
                                     int units_per_px) override;

    virtual void SubmitKeeperHandSprite(short x, short y,
                                        unsigned short kspr_base,
                                        short angle,
                                        unsigned char sprgroup,
                                        int32_t scale,
                                        TbDrawFlagsMask draw_flags) override;

    /** No-op: cursor data now lives in UICommandBuffers and is flushed via
     *  ExecuteCursorFromIR().  Draw() is retained for the ICursorLayer default
     *  implementation only; it does nothing in GL mode. */
    virtual void Draw() override {}
    virtual void Clear() override {}
    virtual const char* GetName() const override { return "GL_CURSOR"; }

    /** No-op: UICommandBuffers::flip (via RenderGraph::Flip) handles double-
     *  buffering for cursor IR data. */
    void FlipBuffers() override {}

    /** Open / close the IR write window.
     *  @p cmds != nullptr: Submit* calls emit into the write-side UICommandBuffers.
     *  @p cmds == nullptr: submit calls become no-ops (window closed). */
    void SetCursorWriteBuffers(UICommandBuffers* cmds) override { m_write_cmds = cmds; }

    /** Execute cursor draws from the render-thread read-side UICommandBuffers.
     *  Reads cmds.cursor_pointers (atlas quad path) and cmds.cursor_hands
     *  (keeper-sprite path).  Called from EndFrame_GL() after all other passes. */
    void ExecuteCursorFromIR(const UICommandBuffers& cmds) override;

private:
    GLWorldViewRenderer* m_wvr        = nullptr;
    GLSpriteAtlas*       m_atlas      = nullptr;
    GLUIRenderer*        m_glui       = nullptr;
    UICommandBuffers*    m_write_cmds = nullptr;  /**< Game-thread write target; null when closed. */
};

#endif // RENDERER_OPENGL_ENABLED
