// Adapted from krkrsdl3 plugins/LayerExAreaAverage.cpp; see LICENSE-THIRD-PARTY.md.
#define NCB_MODULE_NAME TJS_W("layerExAreaAverage.dll")
#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"

namespace {
using FixDot = tjs_int;
constexpr int DotBase = 12;
constexpr FixDot IntToFix(tjs_int value) { return value << DotBase; }
constexpr FixDot RealToFix(tjs_real value) { return (FixDot)(value * (1 << DotBase)); }
constexpr tjs_int FixToInt(FixDot value) { return value >> DotBase; }

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

        FixDot sl = IntToFix(sLeft), st = IntToFix(sTop);
        FixDot rw = RealToFix((tjs_real)sWidth / dWidth);
        FixDot rh = RealToFix((tjs_real)sHeight / dHeight);
        tjs_int dPitch = dstImage->GetPitchBytes(), sPitch = srcImage->GetPitchBytes();
        auto dBuffer = static_cast<unsigned char *>(dstImage->GetScanLineForWrite(0));
        auto sBuffer = static_cast<const unsigned char *>(srcImage->GetScanLine(0));

        for (tjs_int y = 0; y < dHeight; ++y) {
            auto output = reinterpret_cast<tjs_uint32 *>(dBuffer + (y + dTop) * dPitch) + dLeft;
            for (tjs_int x = 0; x < dWidth; ++x) {
                FixDot x1 = sl + x * rw, y1 = st + y * rh;
                FixDot x2 = x1 + rw, y2 = y1 + rh;
                tjs_int sx = FixToInt(x1), sy = FixToInt(y1);
                tjs_int ex = FixToInt(x2 + IntToFix(1) - 1);
                tjs_int ey = FixToInt(y2 + IntToFix(1) - 1);
                if (ex >= sImageWidth) ex = sImageWidth - 1;
                if (ey >= sImageHeight) ey = sImageHeight - 1;

                FixDot totalAreaAlpha = 0, alpha = 0;
                FixDot totalAreaColor = 0, red = 0, green = 0, blue = 0;
                FixDot totalAreaTransparent = 0, tr = 0, tg = 0, tb = 0;
                for (tjs_int ay = sy; ay < ey; ++ay) {
                    auto input = reinterpret_cast<const tjs_uint32 *>(sBuffer + ay * sPitch) + sx;
                    FixDot e1 = IntToFix(ay), e2 = IntToFix(ay + 1);
                    if (e1 < y1) e1 = y1;
                    if (e2 > y2) e2 = y2;
                    FixDot ah = e2 - e1;
                    for (tjs_int ax = sx; ax < ex; ++ax, ++input) {
                        e1 = IntToFix(ax);
                        e2 = IntToFix(ax + 1);
                        if (e1 < x1) e1 = x1;
                        if (e2 > x2) e2 = x2;
                        FixDot area = ((e2 - e1) * ah) >> DotBase;
                        totalAreaAlpha += area;
                        tjs_uint32 pixel = *input;
                        tjs_uint32 a = (pixel >> 24) & 0xff;
                        alpha += a * area;
                        if (a > 0) {
                            area = (area * a) >> 8;
                            red += (pixel & 0xff) * area;
                            green += ((pixel >> 8) & 0xff) * area;
                            blue += ((pixel >> 16) & 0xff) * area;
                            totalAreaColor += area;
                        } else {
                            tr += (pixel & 0xff) * area;
                            tg += ((pixel >> 8) & 0xff) * area;
                            tb += ((pixel >> 16) & 0xff) * area;
                            totalAreaTransparent += area;
                        }
                    }
                }
                if (totalAreaAlpha == 0) continue;
                alpha /= totalAreaAlpha;
                if (totalAreaColor == 0) {
                    totalAreaColor = totalAreaTransparent;
                    red = tr;
                    green = tg;
                    blue = tb;
                }
                if (totalAreaColor == 0) {
                    red = green = blue = 0;
                } else {
                    red /= totalAreaColor;
                    green /= totalAreaColor;
                    blue /= totalAreaColor;
                }
                *output = (((tjs_int)alpha & 0xff) << 24) | (((tjs_int)blue & 0xff) << 16) |
                          (((tjs_int)green & 0xff) << 8) | ((tjs_int)red & 0xff);
                ++output;
            }
        }

        dstImage->Update(dBuffer, dPitch, dLeft, dTop, dWidth, dHeight);
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
