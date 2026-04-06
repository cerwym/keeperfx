/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareUIRenderer.h
 *     Software fallback UI renderer - delegates to staging buffer blitting.
 * @par Purpose:
 *     Provides IUIRenderer interface for software renderer compatibility.
 *     All UI operations fall back to existing CPU staging buffer blitting.
 */
/******************************************************************************/
#ifndef SOFTWARE_UI_RENDERER_H
#define SOFTWARE_UI_RENDERER_H

#include "renderer/IUIRenderer.h"
#include <unordered_map>

struct TbSprite;

/**
 * Software implementation of IUIRenderer.
 * Falls back to existing CPU staging buffer rendering for compatibility.
 */
class SoftwareUIRenderer : public IUIRenderer {
public:
    SoftwareUIRenderer() = default;
    virtual ~SoftwareUIRenderer() = default;

    // IUIRenderer interface - all delegate to existing software rendering
    virtual void SubmitSlabSelector(int x1, int y1, int x2, int y2, unsigned char color, float z_depth) override;
    virtual void SubmitKeeperSprite(short x, short y, unsigned short kspr_base,
                                    short angle, unsigned char sprgroup, long scale) override;
    virtual void SubmitPanelSprite(long x, long y, int units_per_px, SpriteHandle spr, bool flip_horiz = false) override;
    virtual void SubmitScaledSprite(long x, long y, long w, long h, SpriteHandle spr) override;
    virtual void SubmitSolidBox(long x, long y, long w, long h, uint8_t color_idx) override;
    virtual uint8_t* AcquireMinimapBuffer(int size) override;
    virtual void SubmitMinimap(int screen_x, int screen_y, int size) override;
    virtual void Flush() override;
    virtual void Clear() override;
    virtual const char* GetName() const override { return "SOFTWARE_UI"; }

    /** Register a sprite handle → TbSprite* mapping for software-mode blitting.
     *  Called from RendererManager when sprite sheets are loaded. */
    void RegisterSpriteHandle(SpriteHandle h, const struct TbSprite* spr);

private:
    std::unordered_map<SpriteHandle, const struct TbSprite*> m_handle_to_sprite;
};

#endif // SOFTWARE_UI_RENDERER_H