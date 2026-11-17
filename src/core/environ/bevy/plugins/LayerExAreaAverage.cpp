// Adapted from krkrsdl3 plugins/LayerExAreaAverage.cpp; see LICENSE-THIRD-PARTY.md.
#define NCB_MODULE_NAME TJS_W("layerExAreaAverage.dll")
#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "BevyRender.h"

namespace {
using FixDot = tjs_int;
constexpr int DotBase = 12;
constexpr FixDot RealToFix(tjs_real value) { return (FixDot)(value * (1 << DotBase)); }

struct AreaAverage {
    static tjs_error TJS_INTF_METHOD stretchCopyAA(tTJSVariant *, tjs_int count, tTJSVariant **params,
                                                   iTJSDispatch2 *object) {
        if (count < 9) return TJS_E_BADPARAMCOUNT;
        tTJSNI_BaseLayer *dstLayer = nullptr;
        if (!object || TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                tTJSNC_Layer::ClassID, reinterpret_cast<iTJSNativeInstance **>(&dstLayer))) ||
            !dstLayer->GetMainImage())
            TVPThrowExceptionMessage(TJS_W("Layer image is required"));
        iTJSDispatch2 *srcObject = params[4]->AsObjectNoAddRef();
        tTJSNI_BaseLayer *srcLayer = nullptr;
        if (!srcObject || TJS_FAILED(srcObject->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                tTJSNC_Layer::ClassID, reinterpret_cast<iTJSNativeInstance **>(&srcLayer))) ||
            !srcLayer->GetMainImage())
            TVPThrowExceptionMessage(TJS_W("Source layer image is required"));

        tjs_int dLeft = (tjs_int)params[0]->AsInteger(), dTop = (tjs_int)params[1]->AsInteger();
        tjs_int dWidth = (tjs_int)params[2]->AsInteger(), dHeight = (tjs_int)params[3]->AsInteger();
        tjs_int sLeft = (tjs_int)params[5]->AsInteger(), sTop = (tjs_int)params[6]->AsInteger();
        tjs_int sWidth = (tjs_int)params[7]->AsInteger(), sHeight = (tjs_int)params[8]->AsInteger();
        if (dWidth <= 0 || dHeight <= 0 || sWidth <= 0 || sHeight <= 0) return TJS_E_INVALIDPARAM;
        if (dWidth > sWidth || dHeight > sHeight)
            TVPThrowExceptionMessage(TJS_W("stretchCopyAA cannot enlarge"));

        auto dstImage = dstLayer->GetMainImage();
        auto srcImage = srcLayer->GetMainImage();
        tjs_int dImageWidth = (tjs_int)dstImage->GetWidth(), dImageHeight = (tjs_int)dstImage->GetHeight();
        tjs_int sImageWidth = (tjs_int)srcImage->GetWidth(), sImageHeight = (tjs_int)srcImage->GetHeight();
        if (dLeft < 0 || dTop < 0 || sLeft < 0 || sTop < 0 || dLeft >= dImageWidth ||
            dTop >= dImageHeight || sLeft >= sImageWidth || sTop >= sImageHeight)
            return TJS_S_OK;

        if (dLeft + dWidth > dImageWidth) {
            tjs_int width = dImageWidth - dLeft;
            sWidth = (tjs_int)((tjs_real)sWidth * ((tjs_real)width / (tjs_real)dWidth));
            dWidth = width;
        }
        if (sLeft + sWidth > sImageWidth) {
            tjs_int width = sImageWidth - sLeft;
            dWidth = (tjs_int)((tjs_real)dWidth * ((tjs_real)width / (tjs_real)sWidth));
            sWidth = width;
        }
        if (dTop + dHeight > dImageHeight) {
            tjs_int height = dImageHeight - dTop;
            sHeight = (tjs_int)((tjs_real)sHeight * ((tjs_real)height / (tjs_real)dHeight));
            dHeight = height;
        }
        if (sTop + sHeight > sImageHeight) {
            tjs_int height = sImageHeight - sTop;
            dHeight = (tjs_int)((tjs_real)dHeight * ((tjs_real)height / (tjs_real)sHeight));
            sHeight = height;
        }
        if (dWidth <= 0 || dHeight <= 0 || sWidth <= 0 || sHeight <= 0) return TJS_S_OK;

        dstLayer->IndependMainImage();
        auto texture = dstLayer->GetMainImage()->GetTexture();
        TVPBevyRenderCommand command{};
        command.operation = 4;
        command.method = "LayerExAreaAverage";
        command.target = TVPBevyTextureID(texture);
        command.sources[0] = TVPBevyTextureID(srcLayer->GetMainImage()->GetTexture());
        command.rect[0] = dLeft; command.rect[1] = dTop;
        command.rect[2] = dLeft + dWidth; command.rect[3] = dTop + dHeight;
        command.source_rects[0][0] = sLeft; command.source_rects[0][1] = sTop;
        command.source_rects[0][2] = sLeft + sWidth;
        command.source_rects[0][3] = sTop + sHeight;
        command.parameters[4] = RealToFix((tjs_real)sWidth / dWidth);
        command.parameters[5] = RealToFix((tjs_real)sHeight / dHeight);
        if (krkr_render_dispatch(&command))
            TVPThrowExceptionMessage(TJS_W("Area average GPU operation failed"));
        TVPBevyInvalidateTexture(texture);
        dstLayer->SetImageModified(true);
        dstLayer->Update(tTVPRect(dLeft + dstLayer->GetImageLeft(), dTop + dstLayer->GetImageTop(),
                                  dLeft + dWidth + dstLayer->GetImageLeft(),
                                  dTop + dHeight + dstLayer->GetImageTop()));
        return TJS_S_OK;
    }
};
}

NCB_ATTACH_CLASS(AreaAverage, Layer) {
    RawCallback("stretchCopyAA", &AreaAverage::stretchCopyAA, 0);
}
