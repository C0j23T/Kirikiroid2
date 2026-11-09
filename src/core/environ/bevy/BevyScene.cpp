#include "BevyPlatform.h"
#include "BevyRender.h"
#include "BitmapIntf.h"
#include "BitmapLayerTreeOwner.h"
#include "EventIntf.h"
#include "LayerIntf.h"
#include "tjsCommHead.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <unordered_set>

struct TVPBevyScene {
  std::unique_ptr<tTVPBaseTexture> Target;
  std::unordered_set<tTJSNI_BaseLayer *> Painting;

  static void Composite(tTVPBaseTexture &target, tTVPBaseTexture &source, int x,
                        int y, int type, int opacity) {
    TVPBevyRenderCommand command{};
    command.operation = 5;
    command.target =
        TVPBevyTextureID(target.GetTextureForRender(true, nullptr));
    command.sources[0] = TVPBevyTextureID(source.GetTexture());
    command.rect[0] = x;
    command.rect[1] = y;
    command.rect[2] = x + source.GetWidth();
    command.rect[3] = y + source.GetHeight();
    command.parameters[0] = opacity;
    command.parameters[1] = type;
    if (krkr_render_dispatch(&command))
      throw std::runtime_error("Bevy layer composition failed");
  }

  static std::vector<tTJSNI_BaseLayer *> Children(tTJSNI_BaseLayer &layer) {
    std::vector<tTJSNI_BaseLayer *> children;
    for (unsigned i = 0; i < layer.GetCount(); ++i)
      children.push_back(layer.GetChildren(i));
    std::stable_sort(children.begin(), children.end(), [](auto a, auto b) {
      return a->GetOrderIndex() < b->GetOrderIndex();
    });
    return children;
  }

  static void Prepare(tTJSNI_BaseLayer &layer) {
    if (layer.CallOnPaint) {
      layer.CallOnPaint = false;
      static ttstr event(TJS_W("onPaint"));
      TVPPostEvent(layer.Owner, layer.Owner, event, 0, TVP_EPT_IMMEDIATE, 0,
                   nullptr);
    }
    for (auto child : Children(layer))
      Prepare(*child);
  }

  void DrawChildren(tTVPBaseTexture &target, tTJSNI_BaseLayer &parent) {
    for (auto child : Children(parent)) {
      if (!child->IsSeen() || !child->GetWidth() || !child->GetHeight())
        continue;
      auto image = Build(*child);
      Composite(target, *image, child->GetLeft(), child->GetTop(),
                child->DisplayType, child->Opacity);
    }
  }

  std::unique_ptr<tTVPBaseTexture>
  Build(tTJSNI_BaseLayer &layer, bool transition = true, bool children = true) {
    if (!Painting.insert(&layer).second)
      throw std::runtime_error("Cyclic layer transition");
    struct Scope {
      std::unordered_set<tTJSNI_BaseLayer *> &Set;
      tTJSNI_BaseLayer *Layer;
      ~Scope() { Set.erase(Layer); }
    } scope{Painting, &layer};
    auto result =
        std::make_unique<tTVPBaseTexture>(layer.GetWidth(), layer.GetHeight());
    tTVPRect rect(0, 0, layer.GetWidth(), layer.GetHeight());
    result->Fill(rect, layer.DisplayType == ltOpaque
                           ? layer.NeutralColor | 0xff000000
                           : 0);
    if (layer.MainImage && layer.DisplayType != ltBinder)
      result->CopyRect(layer.ImageLeft, layer.ImageTop, layer.MainImage,
                       tTVPRect(0, 0, layer.MainImage->GetWidth(),
                                layer.MainImage->GetHeight()));

    bool active =
        transition && layer.InTransition && layer.DivisibleTransHandler;
    if (active) {
      if (layer.TransSelfUpdate)
        layer.TransTick = layer.GetTransTick();
      if (layer.DivisibleTransHandler->StartProcess(layer.TransTick) !=
          TJS_S_TRUE) {
        layer.StopTransitionByHandler();
        active = false;
        result->Fill(rect, layer.DisplayType == ltOpaque
                               ? layer.NeutralColor | 0xff000000
                               : 0);
        if (layer.MainImage)
          result->CopyRect(layer.ImageLeft, layer.ImageTop, layer.MainImage,
                           tTVPRect(0, 0, layer.MainImage->GetWidth(),
                                    layer.MainImage->GetHeight()));
      }
    }
    if (children && (!active || layer.TransWithChildren))
      DrawChildren(*result, layer);
    if (active) {
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
        DrawChildren(*result, layer);
      if (layer.DivisibleTransHandler->EndProcess() != TJS_S_TRUE)
        layer.StopTransitionByHandler();
    }
    return result;
  }
};

static TVPBevyScene Scene;

tTVPBaseTexture *tTJSNI_BaseLayer::Complete(const tTVPRect &) {
  return Complete();
}
tTVPBaseTexture *tTJSNI_BaseLayer::Complete() {
  TVPBevyScene::Prepare(*this);
  auto result = Scene.Build(*this);
  if (!CacheBitmap)
    CacheBitmap = new tTVPBaseTexture(GetWidth(), GetHeight());
  CacheBitmap->Assign(*result);
  return CacheBitmap;
}

uint64_t TVPBevyBuildScene(void *screen, int *width, int *height) {
  if (width)
    *width = 0;
  if (height)
    *height = 0;
  auto layer = TVPBevyFrontWindowLayer();
  if (!layer && screen) {
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
  if (!layer && screen) {
    tTJSNI_Bitmap *bitmap = nullptr;
    auto object = static_cast<iTJSDispatch2 *>(screen);
    if (TJS_SUCCEEDED(object->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
            (iTJSNativeInstance **)&bitmap))) {
      if (width)
        *width = bitmap->GetWidth();
      if (height)
        *height = bitmap->GetHeight();
      return TVPBevyTextureID(bitmap->GetBitmap()->GetTexture());
    }
  }
  if (!layer || !layer->GetWidth() || !layer->GetHeight())
    return 0;
  TVPBevyScene::Prepare(*layer);
  Scene.Target = Scene.Build(*layer);
  if (width)
    *width = layer->GetWidth();
  if (height)
    *height = layer->GetHeight();
  return TVPBevyTextureID(Scene.Target->GetTexture());
}
void TVPBevyResetScene() {
  Scene.Target.reset();
  Scene.Painting.clear();
  TVPBevyRenderCommand command{};
  command.operation = 6;
  krkr_render_dispatch(&command);
}
