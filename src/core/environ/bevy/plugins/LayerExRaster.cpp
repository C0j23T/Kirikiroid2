// Raster algorithm: wamsoft/layerExRaster; see LICENSE-THIRD-PARTY.md.
#define NCB_MODULE_NAME TJS_W("layerExRaster.dll")
#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "BevyRender.h"
#include <algorithm>
#include <cmath>
#include <vector>

struct BevyLayerExRaster {
    static tjs_error TJS_INTF_METHOD copyRaster(tTJSVariant *, tjs_int count,
            tTJSVariant **p, iTJSDispatch2 *object) {
        if (count < 5) return TJS_E_BADPARAMCOUNT;
        auto layer = [](iTJSDispatch2 *obj) {
            tTJSNI_BaseLayer *out = nullptr;
            if (!obj || TJS_FAILED(obj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                    tTJSNC_Layer::ClassID, reinterpret_cast<iTJSNativeInstance **>(&out))))
                TVPThrowExceptionMessage(TJS_W("Layer instance is required"));
            if (!out->GetMainImage()) TVPThrowExceptionMessage(TJS_W("Layer has no image"));
            return out;
        };
        auto dest = layer(object), src = layer(p[0]->AsObjectNoAddRef());
        auto image = src->GetMainImage();
        int w = image->GetWidth(), h = image->GetHeight();
        if (w != dest->GetMainImage()->GetWidth() || h != dest->GetMainImage()->GetHeight()) return TJS_S_OK;
        int amplitude = p[1]->AsInteger(), lines = p[2]->AsInteger(), cycle = p[3]->AsInteger();
        if (!lines || !cycle) TVPThrowExceptionMessage(TJS_W("Raster lines and cycle must be nonzero"));
        double omega = 2 * 3.14159265358979323846 / lines;
        auto rect = dest->GetClip();
        rect.left = std::max(rect.left, 0); rect.top = std::max(rect.top, 0);
        rect.right = std::min(rect.right, w); rect.bottom = std::min(rect.bottom, h);
        if (rect.is_empty()) return TJS_S_OK;
        std::vector<float> shifts(h);
        double rad = -omega * p[4]->AsInteger() / cycle * (h/2) + omega * rect.top;
        for (int y = rect.top; y < rect.bottom; ++y, rad += omega)
            shifts[y] = std::clamp(std::trunc(std::sin(rad) * amplitude), -double(w), double(w));
        dest->IndependMainImage();
        auto texture = dest->GetMainImage()->GetTexture();
        TVPBevyRenderCommand c{};
        c.operation = 4; c.target = TVPBevyTextureID(texture);
        c.sources[0] = TVPBevyTextureID(src->GetMainImage()->GetTexture());
        c.method = "LayerExRaster";
        c.rect[0] = rect.left; c.rect[1] = rect.top; c.rect[2] = rect.right; c.rect[3] = rect.bottom;
        c.data = shifts.data(); c.pitch = h;
        if (krkr_render_dispatch(&c)) TVPThrowExceptionMessage(TJS_W("Raster GPU copy failed"));
        TVPBevyInvalidateTexture(texture);
        dest->SetImageModified(true);
        rect.add_offsets(dest->GetImageLeft(), dest->GetImageTop()); dest->Update(rect);
        return TJS_S_OK;
    }
};
NCB_ATTACH_CLASS(BevyLayerExRaster, Layer) {
    RawCallback("copyRaster", BevyLayerExRaster::copyRaster, 0);
}
