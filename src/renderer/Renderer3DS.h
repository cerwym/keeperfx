/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file Renderer3DS.h
 *     Nintendo 3DS renderer backend declaration.
 * @par Purpose:
 *     C++ IRenderer implementation using citro3d (PICA200 hardware).
 *     Only compiled when PLATFORM_3DS is defined.
 */
/******************************************************************************/
#pragma once

#ifdef PLATFORM_3DS

#include "IRenderer.h"

/**
 * 3DS renderer backend.
 *
 * Uses citro3d for hardware-accelerated rendering on the PICA200 GPU.
 * The 8-bit paletted framebuffer is converted to RGBA8 and uploaded as
 * a citro3d texture each frame, then rendered as a fullscreen quad on
 * the top screen (400x240).
 *
 * Note: Only the top screen is used for gameplay. The bottom screen
 * (320x240) can be used for a HUD or map at a later stage.
 */
class Renderer3DS : public IRenderer {
public:
    Renderer3DS();
    ~Renderer3DS() override;

    bool Init() override;
    void Shutdown() override;

    bool BeginFrame() override;
    void EndFrame() override;

    uint8_t* LockFramebuffer(int* out_pitch) override;
    void UnlockFramebuffer() override;

    const char* GetName() const override { return "3DS (citro3d)"; }

    BackendCapabilities GetCapabilities() const override {
        BackendCapabilities c = {};
        // 3DS is a software-palette path (CPU indexed → RGBA expand each frame).
        c.supportsMovieCapture = 1;
        return c;
    }

private:
    bool m_initialized = false;
    uint8_t* m_framebuffer = nullptr;   /**< CPU-side staging buffer (8-bit indexed) */
    uint8_t* m_rgbaBuffer  = nullptr;   /**< RGBA8 expanded buffer for C3D texture */
    int m_width  = 400;
    int m_height = 240;

    void ExpandPalette();   /**< Convert 8-bit indexed to RGBA8 using lbPalette */
};

#endif // PLATFORM_3DS
