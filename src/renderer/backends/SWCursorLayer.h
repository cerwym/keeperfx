/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SWCursorLayer.h
 *     Software (CPU) implementation of ICursorLayer.
 */
/******************************************************************************/
#pragma once

#include "renderer/ICursorLayer.h"

struct TbSprite;

class SWCursorLayer : public ICursorLayer {
public:
    SWCursorLayer() = default;
    virtual ~SWCursorLayer() = default;

    virtual void SubmitPointerSprite(const TbSprite* spr,
                                     int32_t x, int32_t y,
                                     int units_per_px) override;

    virtual void SubmitKeeperHandSprite(short x, short y,
                                        unsigned short kspr_base,
                                        short angle,
                                        unsigned char sprgroup,
                                        int32_t scale,
                                        TbDrawFlagsMask draw_flags) override;

    virtual void Draw(const TbGraphicsWindow& target) override;
    virtual void Clear() override;
    virtual void SetCursorWriteBuffers(struct UICommandBuffers* cmds) override { m_write_cmds = cmds; }
    virtual const char* GetName() const override { return "SW_CURSOR"; }

private:
    const TbSprite* m_pointer_spr = nullptr;
    int32_t         m_pointer_x   = 0;
    int32_t         m_pointer_y   = 0;
    // When set, keeper-hand sprites are recorded into the UI command buffer
    // (seq-ordered, replayed after the world) instead of rasterising immediately.
    struct UICommandBuffers* m_write_cmds = nullptr;
};
