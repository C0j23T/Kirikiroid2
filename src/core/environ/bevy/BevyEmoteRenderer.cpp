#include "BevyEmoteRenderer.h"
#include "BevyRender.h"
#include "BevyMesh.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "emoteplayer/emotegeometry.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace krkrsdl3
{
namespace
{
iTVPTexture2D* texture(void* handle) { return static_cast<iTVPTexture2D*>(handle); }
class Renderer final : public iTVPRenderBackend
{
    iTVPTexture2D* target = nullptr;
    iTVPTexture2D* mask = nullptr;
    int blend = 0;
    float color[4] = {};

public:
    void* CreateTexture(int width, int height) override
    {
        if (width <= 0 || height <= 0) throw std::invalid_argument("Invalid Emote texture size");
        return TVPGetRenderManager()->CreateTexture2D(nullptr, 0, width, height, TVPTextureFormat::RGBA);
    }
    void* CreateTarget(int width, int height) override { return CreateTexture(width, height); }
    void DestroyTexture(void* handle) override
    {
        auto image = texture(handle);
        if (target == image) target = nullptr;
        if (mask == image) mask = nullptr;
        if (image) image->Release();
    }
    void DestroyTarget(void* handle) override { DestroyTexture(handle); }
    void SetTarget(void* handle) override { target = texture(handle); mask = nullptr; }
    void ClearTarget(bool clearColor) override
    {
        if (!clearColor || !target) return;
        auto manager = TVPGetRenderManager();
        auto method = manager->GetRenderMethod("FillARGB");
        method->SetParameterColor4B(method->EnumParameterID("color"), 0);
        manager->OperateRect(method, target, target,
                             tTVPRect(0, 0, target->GetWidth(), target->GetHeight()), {});
        TVPBevyInvalidateTexture(target);
    }
    uint8_t* LockTarget(void* handle, int& pitch) override
    {
        auto image = texture(handle);
        if (!image) return nullptr;
        pitch = image->GetPitch();
        return const_cast<uint8_t*>(static_cast<const uint8_t*>(image->GetScanLineForRead(0)));
    }
    void UnlockTarget(void*) override {}
    void* GetTargetTexture(void* handle) override { return handle; }
    void UpdateTexture(void* handle, const uint8_t* pixels,
                       int width, int height, int pitch) override
    {
        auto image = texture(handle);
        if (!image) throw std::invalid_argument("Invalid Emote texture");
        image->Update(pixels, TVPTextureFormat::RGBA, pitch, tTVPRect(0, 0, width, height));
    }
    void UpdateTargetTexture(void* handle, const uint8_t* pixels,
                             int width, int height, int pitch) override
    { UpdateTexture(handle, pixels, width, height, pitch); }
    void SetMask(void* handle) override { mask = texture(handle); }
    void SetBlendMode(int mode, const float* uniformColor) override
    {
        blend = mode;
        std::fill_n(color, 4, 0);
        if (uniformColor) std::copy_n(uniformColor, 4, color);
    }
    void DrawSurface(const emoteplayer::emoteRender* chain, uint32_t count,
                     uint32_t divisionX, uint32_t divisionY, void* source,
                     float opacity, const float* tint) override
    {
        if (!target || !source || !chain || !count) return;
        std::vector<TVPBevyMeshSurface> surfaces(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& input = chain[i];
            auto& output = surfaces[i];
            std::copy_n(input.controlPts, 32, output.control_points);
            for (uint32_t variant = 0; variant < 32; ++variant)
            {
                const auto model = input.attachMat * emoteplayer::buildMotionLinear(input, variant << 4);
                std::memcpy(output.models[variant], &model[0][0], sizeof(output.models[variant]));
            }
            const auto placement = input.attachMat * emoteplayer::math::vec4(input.currCoordx, input.currCoordy, 0, 1);
            output.placement[0] = placement.x; output.placement[1] = placement.y;
            output.placement[2] = placement.z; output.placement[3] = 0;
            output.geometry[0] = input.originX; output.geometry[1] = input.originY;
            output.geometry[2] = input.width; output.geometry[3] = input.height;
            output.offset[0] = input.currOx; output.offset[1] = input.currOy;
            output.kind = input.type; output.inherit_mask = input.currInheritMask;
        }
        TVPBevyMeshCommand command{};
        command.target = TVPBevyTextureID(target);
        command.source = TVPBevyTextureID(texture(source));
        command.mask = mask ? TVPBevyTextureID(mask) : 0;
        command.surfaces = surfaces.data(); command.surface_count = count;
        command.division_x = divisionX; command.division_y = divisionY;
        command.blend = blend; command.opacity = opacity;
        std::fill_n(command.tint, 4, 1);
        if (tint) std::copy_n(tint, 4, command.tint);
        std::copy_n(color, 4, command.color);
        if (krkr_render_mesh(&command)) throw std::runtime_error("Bevy Emote mesh operation failed");
        TVPBevyInvalidateTexture(target);
    }
};
}
iTVPRenderBackend* TVPGetRenderBackend()
{
    static Renderer renderer;
    return &renderer;
}
}

void TVPBevyCopyEmoteTargetToLayer(void* source, tTJSNI_BaseLayer* layer)
{
    if (!source || !layer || !layer->GetMainImage()) return;
    layer->IndependMainImage();
    auto target = layer->GetMainImage()->GetTexture();
    auto input = static_cast<iTVPTexture2D*>(source);
    tTVPRect rect(0, 0, std::min(target->GetWidth(), input->GetWidth()),
                  std::min(target->GetHeight(), input->GetHeight()));
    tRenderTexRectArray::Element sources[] = {{input, rect}};
    auto manager = TVPGetRenderManager();
    manager->OperateRect(manager->GetRenderMethod("Copy"), target, target, rect, sources);
    TVPBevyInvalidateTexture(target);
    layer->SetImageModified(true);
    rect.add_offsets(layer->GetImageLeft(), layer->GetImageTop());
    layer->Update(rect);
}
