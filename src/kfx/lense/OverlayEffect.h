/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file OverlayEffect.h
 *     Overlay compositing lens effect.
 * @par Purpose:
 *     Overlay graphics compositing effect implementation.
 * @author   Peter Lockett, KeeperFX Team
 * @date     09 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KFX_OVERLAYEFFECT_H
#define KFX_OVERLAYEFFECT_H

#include "LensEffect.h"

#ifdef PLATFORM_VITA
#include "renderer/vita/VitaOverlayPass.h"
#endif

/******************************************************************************/

class OverlayEffect : public LensEffect {
public:
    OverlayEffect();
    virtual ~OverlayEffect();
    
    virtual TbBool Setup(long lens_idx) override;
    virtual void Cleanup() override;
    virtual TbBool Draw(LensRenderContext* ctx) override;

#ifdef PLATFORM_VITA
    virtual IPostProcessPass* GetGPUPass() override {
        return m_gpu_pass.IsInitialized() ? &m_gpu_pass : nullptr;
    }
#endif

private:
    long m_current_lens;

#ifdef PLATFORM_VITA
    VitaOverlayPass m_gpu_pass;
#endif
};

/******************************************************************************/
#endif
