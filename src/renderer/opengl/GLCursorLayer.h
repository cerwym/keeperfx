/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLCursorLayer.h
 *     OpenGL implementation of ICursorLayer.
 */
/******************************************************************************/
#ifndef GL_CURSOR_LAYER_H
#define GL_CURSOR_LAYER_H

#ifdef RENDERER_OPENGL_ENABLED

#include "renderer/ICursorLayer.h"
#include "renderer/SpriteHandle.h"
#include <vector>

struct TbSprite;
class GLWorldViewRenderer;
class GLSpriteAtlas;
class GLUIRenderer;
typedef unsigned int GLuint;

/** Deferred keeper-hand sprite, captured at SubmitKeeperHandSprite() time. */
struct PendingKeeperSprite {
    short          x, y;
    unsigned short kspr_base;
    short          angle;
    unsigned char  sprgroup;
    int32_t           scale;
    unsigned int   draw_flags;    // lbDisplay.DrawFlags at submit time
    unsigned char  draw_alpha;    // EngineSpriteDrawUsingAlpha at submit time
};

/** Deferred OS pointer sprite, captured at SubmitPointerSprite() time. */
struct PendingPointerSprite {
    const TbSprite* spr;
    int32_t            x, y;
    int             units_per_px;
};

class GLCursorLayer : public ICursorLayer {
public:
    GLCursorLayer() = default;
    virtual ~GLCursorLayer() = default;

    /** Set the world-view renderer used to drive keeper-sprite GL state.
     *  Must be called before the first Flush().  Not owned. */
    void SetWorldViewRenderer(GLWorldViewRenderer* wvr) { m_wvr = wvr; }

    /** Set the sprite atlas used to resolve pointer sprites to UV coordinates.
     *  Must be called before the first Flush().  Not owned. */
    void SetSpriteAtlas(GLSpriteAtlas* atlas) { m_atlas = atlas; }

    /** Set the GLUIRenderer used to flush atlas quads for pointer sprites.
     *  Must be called before the first Flush().  Not owned. */
    void SetGLUIRenderer(GLUIRenderer* glui) { m_glui = glui; }

    // ICursorLayer interface
    virtual void SubmitPointerSprite(const TbSprite* spr,
                                     int32_t x, int32_t y,
                                     int units_per_px) override;

    virtual void SubmitKeeperHandSprite(short x, short y,
                                        unsigned short kspr_base,
                                        short angle,
                                        unsigned char sprgroup,
                                        int32_t scale) override;

    virtual void Flush() override;
    virtual void Clear() override;
    virtual const char* GetName() const override { return "GL_CURSOR"; }

private:
    GLWorldViewRenderer*              m_wvr    = nullptr;
    GLSpriteAtlas*                    m_atlas  = nullptr;
    GLUIRenderer*                     m_glui   = nullptr;
    std::vector<PendingPointerSprite> m_pointers;
    std::vector<PendingKeeperSprite>  m_keepers;
};

#endif // RENDERER_OPENGL_ENABLED
#endif // GL_CURSOR_LAYER_H
