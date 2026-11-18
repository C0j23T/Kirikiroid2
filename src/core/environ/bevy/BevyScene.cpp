#include "BevyPlatform.h"
#include "BevyRender.h"
#include "BevyVideo.h"
#include "BitmapIntf.h"
#include "BitmapLayerTreeOwner.h"
#include "EventIntf.h"
#include "LayerIntf.h"
#include "LayerManager.h"
#include "tjsCommHead.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_set>

extern void *TVPBevyFrontWindow();

struct TVPBevyScene
{
    struct LayerImage
    {
        std::unique_ptr<tTVPBaseTexture> Bitmap;
        tTVPRect Source;
    };

    std::unique_ptr<tTVPBaseTexture> Target;
    void *Screen = nullptr;
    tTJSNI_BaseLayer *Layer = nullptr;
    int Width = 0;
    int Height = 0;
    std::unordered_set<tTJSNI_BaseLayer *> Painting;

    static void CopyRegion(tTVPBaseTexture &target, const LayerImage &source,
                           const tTVPRect &rect)
    {
        TVPBevyRenderCommand command{};
        command.operation = 7;
        command.target =
            TVPBevyTextureID(target.GetTextureForRender(true, nullptr));
        command.sources[0] = TVPBevyTextureID(source.Bitmap->GetTexture());
        command.rect[0] = rect.left;
        command.rect[1] = rect.top;
        command.rect[2] = rect.right;
        command.rect[3] = rect.bottom;
        command.source_rects[0][0] = source.Source.left;
        command.source_rects[0][1] = source.Source.top;
        command.source_rects[0][2] = source.Source.right;
        command.source_rects[0][3] = source.Source.bottom;
        if (krkr_render_dispatch(&command))
            throw std::runtime_error("Bevy scene texture copy failed");
        TVPBevyInvalidateTexture(target.GetTexture());
    }

    static void Composite(tTVPBaseTexture &target, const LayerImage &source, int x,
                          int y, int type, int opacity)
    {
        const int left = std::max(0, x);
        const int top = std::max(0, y);
        const int right = static_cast<int>(std::min<int64_t>(
            target.GetWidth(), int64_t(x) + source.Source.get_width()));
        const int bottom = static_cast<int>(std::min<int64_t>(
            target.GetHeight(), int64_t(y) + source.Source.get_height()));
        if (left >= right || top >= bottom)
            return;
        TVPBevyRenderCommand command{};
        command.operation = 5;
        command.target =
            TVPBevyTextureID(target.GetTextureForRender(true, nullptr));
        command.sources[0] = TVPBevyTextureID(source.Bitmap->GetTexture());
        command.rect[0] = left;
        command.rect[1] = top;
        command.rect[2] = right;
        command.rect[3] = bottom;
        command.source_rects[0][0] = int64_t(left) - x + source.Source.left;
        command.source_rects[0][1] = int64_t(top) - y + source.Source.top;
        command.source_rects[0][2] = int64_t(right) - x + source.Source.left;
        command.source_rects[0][3] = int64_t(bottom) - y + source.Source.top;
        command.parameters[0] = opacity;
        command.parameters[1] = type;
        if (krkr_render_dispatch(&command))
            throw std::runtime_error("Bevy layer composition failed");
    }

    static void DrawMainImage(tTVPBaseTexture &target, tTJSNI_BaseLayer &layer,
                              const tTVPRect &dirty, bool full_main_image)
    {
        if (!full_main_image)
            target.Fill(tTVPRect(0, 0, target.GetWidth(), target.GetHeight()),
                        layer.DisplayType == ltOpaque
                            ? layer.NeutralColor | 0xff000000
                            : 0);
        if (!layer.MainImage || layer.DisplayType == ltBinder)
            return;
        const int left = std::max(dirty.left, layer.ImageLeft);
        const int top = std::max(dirty.top, layer.ImageTop);
        const int right = static_cast<int>(std::min<int64_t>(
            dirty.right, int64_t(layer.ImageLeft) + layer.MainImage->GetWidth()));
        const int bottom = static_cast<int>(std::min<int64_t>(
            dirty.bottom, int64_t(layer.ImageTop) + layer.MainImage->GetHeight()));
        if (left < right && top < bottom)
            target.CopyRect(left - dirty.left, top - dirty.top, layer.MainImage,
                            tTVPRect(left - layer.ImageLeft,
                                     top - layer.ImageTop,
                                     right - layer.ImageLeft,
                                     bottom - layer.ImageTop));
    }

    static std::vector<tTJSNI_BaseLayer *> Children(tTJSNI_BaseLayer &layer)
    {
        std::vector<tTJSNI_BaseLayer *> children;
        for (unsigned i = 0; i < layer.GetCount(); ++i)
            children.push_back(layer.GetChildren(i));
        std::stable_sort(children.begin(), children.end(), [](auto a, auto b)
                         { return a->GetOrderIndex() < b->GetOrderIndex(); });
        return children;
    }

    static void Prepare(tTJSNI_BaseLayer &layer)
    {
        if (!layer.PendingPaintCount)
            return;
        if (layer.GetCallOnPaint())
        {
            layer.SetCallOnPaint(false);
            static ttstr event(TJS_W("onPaint"));
            TVPPostEvent(layer.Owner, layer.Owner, event, 0, TVP_EPT_IMMEDIATE, 0,
                         nullptr);
        }
        if (!layer.PendingPaintCount)
            return;
        for (auto child : Children(layer))
            Prepare(*child);
    }

    static bool HasActiveTransition(tTJSNI_BaseLayer &layer)
    {
        if (!layer.DivisibleTransitionCount)
            return false;
        if (layer.InTransition && layer.DivisibleTransHandler)
            return true;
        for (unsigned i = 0; i < layer.GetCount(); ++i)
            if (HasActiveTransition(*layer.GetChildren(i)))
                return true;
        return false;
    }

    void DrawChildren(tTVPBaseTexture &target, tTJSNI_BaseLayer &parent,
                      const tTVPRect *dirty)
    {
        for (auto child : Children(parent))
        {
            if (!child->IsSeen() || !child->GetWidth() || !child->GetHeight())
                continue;
            int x = child->GetLeft();
            int y = child->GetTop();
            tTVPRect child_dirty;
            if (dirty)
            {
                if (!TVPIntersectRect(&child_dirty, *dirty, child->GetRect()))
                    continue;
                const int target_x = child_dirty.left - dirty->left;
                const int target_y = child_dirty.top - dirty->top;
                child_dirty = tTVPRect(child_dirty.left - x, child_dirty.top - y,
                                       child_dirty.right - x, child_dirty.bottom - y);
                x = target_x;
                y = target_y;
            }
            auto image = Build(*child, true, true, dirty ? &child_dirty : nullptr);
            Composite(target, image, x, y, child->DisplayType, child->Opacity);
        }
    }

    LayerImage
    Build(tTJSNI_BaseLayer &layer, bool transition = true, bool children = true,
          const tTVPRect *region = nullptr)
    {
        if (!Painting.insert(&layer).second)
            throw std::runtime_error("Cyclic layer transition");
        struct Scope
        {
            std::unordered_set<tTJSNI_BaseLayer *> &Set;
            tTJSNI_BaseLayer *Layer;
            ~Scope() { Set.erase(Layer); }
        } scope{Painting, &layer};
        bool active =
            transition && layer.InTransition && layer.DivisibleTransHandler;
        if (active)
            region = nullptr;
        const tTVPRect dirty = region ? *region
            : tTVPRect(0, 0, layer.GetWidth(), layer.GetHeight());
        const bool fullMainImage = layer.MainImage &&
                                   layer.DisplayType != ltBinder &&
                                   layer.MainImage->Is32BPP() &&
                                   layer.ImageLeft == 0 && layer.ImageTop == 0 &&
                                   layer.MainImage->GetWidth() == layer.GetWidth() &&
                                   layer.MainImage->GetHeight() == layer.GetHeight();
        if (!active && children && layer.GetCount() == 0 && fullMainImage)
            return {std::make_unique<tTVPBaseTexture>(*layer.MainImage), dirty};
        auto result =
            std::make_unique<tTVPBaseTexture>(dirty.get_width(), dirty.get_height());
        DrawMainImage(*result, layer, dirty, fullMainImage);

        if (active)
        {
            if (layer.TransSelfUpdate)
                layer.TransTick = layer.GetTransTick();
            if (layer.DivisibleTransHandler->StartProcess(layer.TransTick) !=
                TJS_S_TRUE)
            {
                layer.StopTransitionByHandler();
                active = false;
                DrawMainImage(*result, layer, dirty, fullMainImage);
            }
        }
        if (children && (!active || layer.TransWithChildren))
            DrawChildren(*result, layer, region);
        if (active)
        {
            if (!layer.TransSrc)
                throw std::runtime_error("Transition has no source layer");
            auto source = Build(*layer.TransSrc, false, layer.TransWithChildren);
            auto output = std::make_unique<tTVPBaseTexture>(layer.GetWidth(),
                                                            layer.GetHeight());
            tTVPScanLineProviderForBaseBitmap first(result.get()),
                second(source.Bitmap.get()), destination(output.get());
            tTVPDivisibleData data{};
            data.Width = layer.GetWidth();
            data.Height = layer.GetHeight();
            data.Src1 = &first;
            data.Src2 = &second;
            data.Dest = &destination;
            if (TJS_FAILED(layer.DivisibleTransHandler->Process(&data)))
                throw std::runtime_error("Bevy transition failed");
            if (data.Dest == &second)
                result = std::move(source.Bitmap);
            else if (data.Dest == &destination)
                result = std::move(output);
            if (children && !layer.TransWithChildren)
                DrawChildren(*result, layer, nullptr);
            if (layer.DivisibleTransHandler->EndProcess() != TJS_S_TRUE)
                layer.StopTransitionByHandler();
        }
        return {std::move(result),
                tTVPRect(0, 0, dirty.get_width(), dirty.get_height())};
    }
};

static TVPBevyScene Scene;
static std::atomic<bool> SceneDirty{true};
static std::atomic<bool> SceneBuilding{false};

tTVPBaseTexture *tTJSNI_BaseLayer::Complete(const tTVPRect &)
{
    return Complete();
}
tTVPBaseTexture *tTJSNI_BaseLayer::Complete()
{
    TVPBevyScene::Prepare(*this);
    auto result = Scene.Build(*this);
    if (!CacheBitmap)
        CacheBitmap = new tTVPBaseTexture(GetWidth(), GetHeight());
    CacheBitmap->Assign(*result.Bitmap);
    return CacheBitmap;
}

uint64_t TVPBevyBuildScene(void *screen, int *width, int *height)
{
    if (width)
        *width = 0;
    if (height)
        *height = 0;
    auto layer = TVPBevyFrontWindowLayer();
    if (!layer && screen)
    {
        auto object = static_cast<iTJSDispatch2 *>(screen);
        tTJSNI_BitmapLayerTreeOwner *owner = nullptr;
        if (TJS_SUCCEEDED(object->NativeInstanceSupport(
                TJS_NIS_GETINSTANCE, tTJSNC_BitmapLayerTreeOwner::ClassID,
                (iTJSNativeInstance **)&owner)))
            layer = owner->GetPrimaryLayer();
        else
            object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                          (iTJSNativeInstance **)&layer);
    }
    if (!layer && screen)
    {
        tTJSNI_Bitmap *bitmap = nullptr;
        auto object = static_cast<iTJSDispatch2 *>(screen);
        if (TJS_SUCCEEDED(object->NativeInstanceSupport(
                TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
                (iTJSNativeInstance **)&bitmap)))
        {
            if (width)
                *width = bitmap->GetWidth();
            if (height)
                *height = bitmap->GetHeight();
            return TVPBevyTextureID(bitmap->GetBitmap()->GetTexture());
        }
    }
    if (!layer || !layer->GetWidth() || !layer->GetHeight())
        return 0;

    const auto window = TVPBevyFrontWindow();
    const auto scene_screen = window ? window : screen;
    const int layer_width = layer->GetWidth();
    const int layer_height = layer->GetHeight();
    auto *manager = layer->GetManager();
    TVPBevyScene::Prepare(*layer);
    auto *update_region = manager ? &manager->GetUpdateRegionForCompletion() : nullptr;
    const bool layer_dirty = update_region && update_region->GetCount();
    const bool scene_dirty = SceneDirty.exchange(false);
    const bool transition_active = TVPBevyScene::HasActiveTransition(*layer);
    const bool target_changed = !Scene.Target || Scene.Screen != scene_screen ||
                                Scene.Layer != layer ||
                                Scene.Width != layer_width ||
                                Scene.Height != layer_height;
    if (target_changed)
    {
        Scene.Target =
            std::make_unique<tTVPBaseTexture>(layer_width, layer_height);
        Scene.Screen = scene_screen;
        Scene.Layer = layer;
        Scene.Width = layer_width;
        Scene.Height = layer_height;
    }

    bool full_dirty = target_changed || !manager || scene_dirty || transition_active;
    tTVPRect dirty_rect(0, 0, layer_width, layer_height);
    if (layer_dirty)
    {
        const auto &bound = update_region->GetBound();
        dirty_rect.left = std::max(0, bound.left);
        dirty_rect.top = std::max(0, bound.top);
        dirty_rect.right = std::min(layer_width, bound.right);
        dirty_rect.bottom = std::min(layer_height, bound.bottom);
        if (dirty_rect.left >= dirty_rect.right ||
            dirty_rect.top >= dirty_rect.bottom)
            full_dirty = true;
    }

    if (full_dirty || layer_dirty)
    {
        if (update_region)
            update_region->Clear();
        SceneBuilding.store(true);
        try
        {
            auto result = Scene.Build(*layer, true, true,
                                      full_dirty ? nullptr : &dirty_rect);
            if (full_dirty)
                dirty_rect = tTVPRect(0, 0, layer_width, layer_height);
            TVPBevyScene::CopyRegion(*Scene.Target, result, dirty_rect);
            TVPBevyCompositeVideo(window, TVPBevyTextureID(Scene.Target->GetTexture()));
            SceneBuilding.store(false);
        }
        catch (...)
        {
            SceneBuilding.store(false);
            SceneDirty.store(true);
            throw;
        }
    }
    if (width)
        *width = layer_width;
    if (height)
        *height = layer_height;
    return TVPBevyTextureID(Scene.Target->GetTexture());
}
void TVPBevyMarkSceneDirty()
{
    if (!SceneBuilding.load())
        SceneDirty.store(true);
}
void TVPBevyResetScene()
{
    Scene.Target.reset();
    Scene.Screen = nullptr;
    Scene.Layer = nullptr;
    Scene.Width = 0;
    Scene.Height = 0;
    Scene.Painting.clear();
    SceneDirty.store(true);
    TVPBevyRenderCommand command{};
    command.operation = 6;
    krkr_render_dispatch(&command);
}
