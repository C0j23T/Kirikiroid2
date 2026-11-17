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
    std::unique_ptr<tTVPBaseTexture> Target;
    void *Screen = nullptr;
    tTJSNI_BaseLayer *Layer = nullptr;
    int Width = 0;
    int Height = 0;
    std::unordered_set<tTJSNI_BaseLayer *> Painting;

    static void CopyRegion(tTVPBaseTexture &target, tTVPBaseTexture &source,
                           const tTVPRect &rect)
    {
        TVPBevyRenderCommand command{};
        command.operation = 7;
        command.target =
            TVPBevyTextureID(target.GetTextureForRender(true, nullptr));
        command.sources[0] = TVPBevyTextureID(source.GetTexture());
        command.rect[0] = rect.left;
        command.rect[1] = rect.top;
        command.rect[2] = rect.right;
        command.rect[3] = rect.bottom;
        command.source_rects[0][0] = rect.left;
        command.source_rects[0][1] = rect.top;
        command.source_rects[0][2] = rect.right;
        command.source_rects[0][3] = rect.bottom;
        if (krkr_render_dispatch(&command))
            throw std::runtime_error("Bevy scene texture copy failed");
        TVPBevyInvalidateTexture(target.GetTexture());
    }

    static void Composite(tTVPBaseTexture &target, tTVPBaseTexture &source, int x,
                          int y, int type, int opacity, const tTVPRect &dirty)
    {
        const int left = std::max({dirty.left, 0, x});
        const int top = std::max({dirty.top, 0, y});
        const int right = static_cast<int>(std::min<int64_t>(
            std::min<int64_t>(dirty.right, target.GetWidth()),
            int64_t(x) + source.GetWidth()));
        const int bottom = static_cast<int>(std::min<int64_t>(
            std::min<int64_t>(dirty.bottom, target.GetHeight()),
            int64_t(y) + source.GetHeight()));
        if (left >= right || top >= bottom)
            return;
        TVPBevyRenderCommand command{};
        command.operation = 5;
        command.target =
            TVPBevyTextureID(target.GetTextureForRender(true, nullptr));
        command.sources[0] = TVPBevyTextureID(source.GetTexture());
        command.rect[0] = left;
        command.rect[1] = top;
        command.rect[2] = right;
        command.rect[3] = bottom;
        command.source_rects[0][0] = int64_t(left) - x;
        command.source_rects[0][1] = int64_t(top) - y;
        command.source_rects[0][2] = int64_t(right) - x;
        command.source_rects[0][3] = int64_t(bottom) - y;
        command.parameters[0] = opacity;
        command.parameters[1] = type;
        if (krkr_render_dispatch(&command))
            throw std::runtime_error("Bevy layer composition failed");
    }

    static void DrawMainImage(tTVPBaseTexture &target, tTJSNI_BaseLayer &layer,
                              const tTVPRect &dirty, bool full_main_image)
    {
        if (!full_main_image)
            target.Fill(dirty, layer.DisplayType == ltOpaque
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
            target.CopyRect(left, top, layer.MainImage,
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
        if (layer.CallOnPaint)
        {
            layer.CallOnPaint = false;
            static ttstr event(TJS_W("onPaint"));
            TVPPostEvent(layer.Owner, layer.Owner, event, 0, TVP_EPT_IMMEDIATE, 0,
                         nullptr);
        }
        for (auto child : Children(layer))
            Prepare(*child);
    }

    static bool HasActiveTransition(tTJSNI_BaseLayer &layer)
    {
        if (layer.InTransition && layer.DivisibleTransHandler)
            return true;
        for (auto child : Children(layer))
            if (HasActiveTransition(*child))
                return true;
        return false;
    }

    void DrawChildren(tTVPBaseTexture &target, tTJSNI_BaseLayer &parent,
                      const tTVPRect &dirty, const tTVPRect *primary_dirty,
                      int64_t primary_x, int64_t primary_y)
    {
        for (auto child : Children(parent))
        {
            if (!child->IsSeen() || !child->GetWidth() || !child->GetHeight())
                continue;
            const int64_t child_x = primary_x + child->GetLeft();
            const int64_t child_y = primary_y + child->GetTop();
            if (primary_dirty &&
                (child_x >= primary_dirty->right ||
                 child_y >= primary_dirty->bottom ||
                 child_x + child->GetWidth() <= primary_dirty->left ||
                 child_y + child->GetHeight() <= primary_dirty->top))
                continue;
            auto image = Build(*child, true, true, primary_dirty, child_x,
                               child_y);
            Composite(target, *image, child->GetLeft(), child->GetTop(),
                      child->DisplayType, child->Opacity, dirty);
        }
    }

    std::unique_ptr<tTVPBaseTexture>
    Build(tTJSNI_BaseLayer &layer, bool transition = true, bool children = true,
          const tTVPRect *primary_dirty = nullptr, int64_t primary_x = 0,
          int64_t primary_y = 0)
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
            primary_dirty = nullptr;
        tTVPRect dirty(0, 0, layer.GetWidth(), layer.GetHeight());
        if (primary_dirty)
        {
            const auto local = [](int edge, int64_t origin, int extent)
            {
                return static_cast<int>(std::max<int64_t>(
                    0, std::min<int64_t>(extent, int64_t(edge) - origin)));
            };
            dirty.left = local(primary_dirty->left, primary_x, layer.GetWidth());
            dirty.top = local(primary_dirty->top, primary_y, layer.GetHeight());
            dirty.right = local(primary_dirty->right, primary_x, layer.GetWidth());
            dirty.bottom = local(primary_dirty->bottom, primary_y, layer.GetHeight());
        }
        const bool fullMainImage = layer.MainImage &&
                                   layer.DisplayType != ltBinder &&
                                   layer.MainImage->Is32BPP() &&
                                   layer.ImageLeft == 0 && layer.ImageTop == 0 &&
                                   layer.MainImage->GetWidth() == layer.GetWidth() &&
                                   layer.MainImage->GetHeight() == layer.GetHeight();
        if (!active && children && layer.GetCount() == 0 && fullMainImage)
            return std::make_unique<tTVPBaseTexture>(*layer.MainImage);
        auto result =
            std::make_unique<tTVPBaseTexture>(layer.GetWidth(), layer.GetHeight());
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
            DrawChildren(*result, layer, dirty, primary_dirty, primary_x,
                         primary_y);
        if (active)
        {
            if (!layer.TransSrc)
                throw std::runtime_error("Transition has no source layer");
            auto source = Build(*layer.TransSrc, false, layer.TransWithChildren);
            auto output = std::make_unique<tTVPBaseTexture>(layer.GetWidth(),
                                                            layer.GetHeight());
            tTVPScanLineProviderForBaseBitmap first(result.get()),
                second(source.get()), destination(output.get());
            tTVPDivisibleData data{};
            data.Width = layer.GetWidth();
            data.Height = layer.GetHeight();
            data.Src1 = &first;
            data.Src2 = &second;
            data.Dest = &destination;
            if (TJS_FAILED(layer.DivisibleTransHandler->Process(&data)))
                throw std::runtime_error("Bevy transition failed");
            if (data.Dest == &second)
                result = std::move(source);
            else if (data.Dest == &destination)
                result = std::move(output);
            if (children && !layer.TransWithChildren)
                DrawChildren(*result, layer, dirty, primary_dirty, primary_x,
                             primary_y);
            if (layer.DivisibleTransHandler->EndProcess() != TJS_S_TRUE)
                layer.StopTransitionByHandler();
        }
        return result;
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
    CacheBitmap->Assign(*result);
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

    const auto scene_screen = screen ? screen : TVPBevyFrontWindow();
    const int layer_width = layer->GetWidth();
    const int layer_height = layer->GetHeight();
    auto *manager = layer->GetManager();
    const bool key_changed = Scene.Screen != scene_screen || Scene.Layer != layer ||
                             Scene.Width != layer_width || Scene.Height != layer_height;
    TVPBevyScene::Prepare(*layer);
    auto *update_region = manager ? &manager->GetUpdateRegionForCompletion() : nullptr;
    const bool layer_dirty = update_region && update_region->GetCount();
    const bool scene_dirty = SceneDirty.exchange(false);
    const bool transition_active = TVPBevyScene::HasActiveTransition(*layer);
    const bool target_changed = !Scene.Target || key_changed ||
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
            TVPBevyScene::CopyRegion(*Scene.Target, *result, dirty_rect);
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
    return TVPBevyCompositeVideo(
        TVPBevyFrontWindow(),
        TVPBevyTextureID(Scene.Target->GetTextureForRender(true, nullptr)));
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
