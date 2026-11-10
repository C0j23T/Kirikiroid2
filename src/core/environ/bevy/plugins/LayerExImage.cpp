// Algorithms follow wamsoft/layerExImage (CxImage); see LICENSE-THIRD-PARTY.md.
#define NCB_MODULE_NAME TJS_W("layerExImage.dll")
#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "BevyRender.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

struct BevyLayerExImage {
    template<int Operation, int Count>
    static tjs_error TJS_INTF_METHOD apply(tTJSVariant *, tjs_int count,
                                          tTJSVariant **parameters, iTJSDispatch2 *object) {
        if (count < Count) return TJS_E_BADPARAMCOUNT;
        tTJSNI_BaseLayer *layer = nullptr;
        if (!object || TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                tTJSNC_Layer::ClassID, reinterpret_cast<iTJSNativeInstance **>(&layer))))
            return TJS_E_NATIVECLASSCRASH;
        if (!layer->GetMainImage()) TVPThrowExceptionMessage(TJS_W("Layer has no image"));
        float values[3]{};
        for (int i = 0; i < Count; ++i) values[i] = parameters[i]->AsReal();
        if (Operation == 5 && !std::isfinite(values[0]))
            TVPThrowExceptionMessage(TJS_W("Invalid gaussianBlur radius"));
        layer->IndependMainImage();
        auto texture = layer->GetMainImage()->GetTexture();
        auto rect = layer->GetClip();
        rect.left = std::max(rect.left, 0); rect.top = std::max(rect.top, 0);
        rect.right = std::min(rect.right, (int)texture->GetWidth());
        rect.bottom = std::min(rect.bottom, (int)texture->GetHeight());
        if (rect.is_empty()) return TJS_S_OK;
        static const char *methods[] = {"LayerExLight", "LayerExColorize", "LayerExModulate",
                                       "LayerExNoise", "LayerExWhiteNoise", "LayerExGaussian"};
        TVPBevyRenderCommand command{};
        command.operation = 4;
        command.target = TVPBevyTextureID(texture);
        command.sources[1] = command.target;
        command.rect[0] = rect.left; command.rect[1] = rect.top;
        command.rect[2] = rect.right; command.rect[3] = rect.bottom;
        for (int i = 0; i < 4; ++i) command.source_rects[1][i] = command.rect[i];
        command.method = methods[Operation];
        std::copy(values, values + 3, command.parameters + 4);
        if (Operation == 1) {
            double blend = std::clamp(parameters[2]->AsReal(), 0.0, 1.0);
            command.parameters[6] = static_cast<int>(256 * blend);
            command.parameters[7] = blend > 0.999;
        } else if (Operation == 3 || Operation == 4) {
            command.parameters[7] = std::rand() & 0xffffff;
        }
        if (krkr_render_dispatch(&command)) TVPThrowExceptionMessage(TJS_W("LayerExImage GPU operation failed"));
        TVPBevyInvalidateTexture(texture);
        rect.add_offsets(layer->GetImageLeft(), layer->GetImageTop());
        layer->SetImageModified(true);
        layer->Update(rect);
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(BevyLayerExImage, Layer) {
    RawCallback("light", &BevyLayerExImage::apply<0, 2>, 0);
    RawCallback("colorize", &BevyLayerExImage::apply<1, 3>, 0);
    RawCallback("modulate", &BevyLayerExImage::apply<2, 3>, 0);
    RawCallback("noise", &BevyLayerExImage::apply<3, 1>, 0);
    RawCallback("generateWhiteNoise", &BevyLayerExImage::apply<4, 0>, 0);
    RawCallback("gaussianBlur", &BevyLayerExImage::apply<5, 1>, 0);
}
