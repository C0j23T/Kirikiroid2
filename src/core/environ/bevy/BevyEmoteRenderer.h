#pragma once

#include <cstdint>

namespace emoteplayer { struct emoteRender; }
namespace krkrsdl3
{
class iTVPRenderBackend
{
public:
    virtual ~iTVPRenderBackend() = default;
    virtual void* CreateTarget(int width, int height) = 0;
    virtual void DestroyTarget(void* target) = 0;
    virtual void SetTarget(void* target) = 0;
    virtual void ClearTarget(bool color) = 0;
    virtual uint8_t* LockTarget(void* target, int& pitch) = 0;
    virtual void UnlockTarget(void* target) = 0;
    virtual void* GetTargetTexture(void* target) = 0;
    virtual void UpdateTargetTexture(void* target, const uint8_t* pixels,
                                     int width, int height, int pitch) = 0;
    virtual void* CreateTexture(int width, int height) = 0;
    virtual void UpdateTexture(void* texture, const uint8_t* pixels,
                              int width, int height, int pitch) = 0;
    virtual void DestroyTexture(void* texture) = 0;
    virtual void SetMask(void* target) = 0;
    virtual void SetBlendMode(int mode, const float* color = nullptr) = 0;
    virtual void DrawSurface(const emoteplayer::emoteRender* chain, uint32_t count,
                             uint32_t divisionX, uint32_t divisionY, void* texture,
                             float opacity, const float* tint = nullptr) = 0;

};
iTVPRenderBackend* TVPGetRenderBackend();
}

class tTJSNI_BaseLayer;
void TVPBevyCopyEmoteTargetToLayer(void* source, tTJSNI_BaseLayer* layer);
