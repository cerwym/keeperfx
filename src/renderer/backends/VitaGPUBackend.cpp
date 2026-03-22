#include "VitaGPUBackend.h"
#include "bflib_basics.h"

// Include VitaSpriteLayer implementation for delete in destructor
#ifdef PLATFORM_VITA
    #include "renderer/vita/VitaSpriteLayer.h"
#else
    // On non-Vita, forward declare (won't be instantiated anyway)
    class VitaSpriteLayer;
#endif

VitaGPUBackend::VitaGPUBackend()
    : m_sprite_layer(nullptr), m_initialized(false)
{
#ifdef PLATFORM_VITA
    m_sprite_layer = new VitaSpriteLayer();
    SYNCLOG("VitaGPUBackend: VitaSpriteLayer allocated");
#else
    ERRORLOG("VitaGPUBackend: Not supported on non-Vita platform");
#endif
}

VitaGPUBackend::~VitaGPUBackend()
{
#ifdef PLATFORM_VITA
    if (m_sprite_layer) {
        delete m_sprite_layer;
        m_sprite_layer = nullptr;
        SYNCLOG("VitaGPUBackend: VitaSpriteLayer destroyed");
    }
#else
    m_sprite_layer = nullptr;
#endif
}

bool VitaGPUBackend::Initialize()
{
#ifdef PLATFORM_VITA
    if (!m_sprite_layer) {
        ERRORLOG("VitaGPUBackend::Initialize: No sprite layer allocated");
        return false;
    }
    
    if (!m_sprite_layer->Init()) {
        ERRORLOG("VitaGPUBackend::Initialize: VitaSpriteLayer::Init() failed");
        return false;
    }
    
    m_initialized = true;
    SYNCLOG("VitaGPUBackend: Initialization complete");
    return true;
#else
    ERRORLOG("VitaGPUBackend::Initialize: Not supported on non-Vita platform");
    return false;
#endif
}

TbResult VitaGPUBackend::SubmitSprite(long x, long y, const struct TbSprite* spr,
                                      unsigned int draw_flags)
{
    if (!spr) {
        return Lb_FAIL;
    }
    
#ifdef PLATFORM_VITA
    if (!m_sprite_layer || !m_initialized) {
        ERRORLOG("VitaGPUBackend: SubmitSprite called without initialized sprite layer");
        return Lb_FAIL;
    }
    
    // Delegate to the GPU sprite layer batch queue
    return m_sprite_layer->SubmitSprite(x, y, spr, draw_flags);
#else
    return Lb_FAIL;
#endif
}

TbResult VitaGPUBackend::SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                               unsigned char colour, unsigned int draw_flags)
{
    if (!spr) {
        return Lb_FAIL;
    }
    
#ifdef PLATFORM_VITA
    if (!m_sprite_layer || !m_initialized) {
        ERRORLOG("VitaGPUBackend: SubmitSpriteOneColour called without initialized sprite layer");
        return Lb_FAIL;
    }
    
    // Delegate to the GPU sprite layer batch queue
    return m_sprite_layer->SubmitSpriteOneColour(x, y, spr, colour, draw_flags);
#else
    return Lb_FAIL;
#endif
}

TbResult VitaGPUBackend::SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                                           const unsigned char* colortable, unsigned int draw_flags)
{
    if (!spr || !colortable) {
        return Lb_FAIL;
    }
    
    // Note: VitaSpriteLayer doesn't directly support color remapping.
    // Remap sprites are typically high-frequency (player colors, creature effects).
    // They could be handled by:
    //   1. Pre-rendered with remap in software before frame
    //   2. Uploaded to a separate remap texture atlas  
    //   3. Left to software rendering (Phase 3 integration)
    // For now, return success but log that this is stubbed.
    SYNCDBG(19, "VitaGPUBackend::SubmitSpriteRemap: Not implemented, stub returns OK");
    return Lb_OK;
}

void VitaGPUBackend::BeginFrame()
{
#ifdef PLATFORM_VITA
    if (m_sprite_layer && m_initialized) {
        m_sprite_layer->BeginFrame();
    }
#endif
}

void VitaGPUBackend::EndFrame()
{
#ifdef PLATFORM_VITA
    if (m_sprite_layer && m_initialized) {
        // Flush all accumulated sprite quads to GPU
        m_sprite_layer->Flush();
    }
#endif
}

void VitaGPUBackend::OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet)
{
    if (!sheet) {
        return;
    }
    
#ifdef PLATFORM_VITA
    if (m_sprite_layer && m_initialized) {
        // Register sheet with the GPU texture atlas
        // This uploads the sprite data to VRAM and creates texture coordinates
        m_sprite_layer->GetAtlas().AddSheet(sheet);
        SYNCDBG(19, "VitaGPUBackend: Sheet loaded into GPU atlas");
    }
#endif
}

void VitaGPUBackend::OnSpriteSheetFreed(const struct TbSpriteSheet* sheet)
{
    if (!sheet) {
        return;
    }
    
#ifdef PLATFORM_VITA
    if (m_sprite_layer && m_initialized) {
        // Unregister sheet from the GPU texture atlas
        // This frees the VRAM and marks texture pages as available for reuse
        m_sprite_layer->GetAtlas().RemoveSheet(sheet);
        SYNCDBG(19, "VitaGPUBackend: Sheet removed from GPU atlas");
    }
#endif
}

void VitaGPUBackend::OnPaletteSet(const unsigned char* lbPalette)
{
    if (!lbPalette) {
        return;
    }
    
#ifdef PLATFORM_VITA
    if (m_sprite_layer && m_initialized) {
        // Update the 256×1 RGBA palette lookup texture
        // This is a small (~1 KB) upload that happens on every palette change
        m_sprite_layer->UpdatePaletteTexture(lbPalette);
    }
#endif
}
