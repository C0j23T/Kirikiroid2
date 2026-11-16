// Adapted from krkrsdl3 plugins/LayerExBTOA.cpp; see LICENSE-THIRD-PARTY.md.
#define NCB_MODULE_NAME TJS_W("layerExBTOA.dll")
#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "BevyRender.h"

namespace {
struct LayerPixels {
    tTJSNI_BaseLayer *layer;
    tTVPBaseTexture *image;
    int width, height, pitch;
    unsigned char *buffer;
};

tTJSNI_BaseLayer *LayerObject(iTJSDispatch2 *object) {
    tTJSNI_BaseLayer *layer = nullptr;
    if (!object || TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
            tTJSNC_Layer::ClassID, reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer->GetMainImage())
        TVPThrowExceptionMessage(TJS_W("Layer image is required"));
    return layer;
}

LayerPixels WritablePixels(iTJSDispatch2 *object) {
    auto layer = LayerObject(object);
    auto image = layer->GetMainImage();
    return {layer, image, static_cast<int>(image->GetWidth()), static_cast<int>(image->GetHeight()),
            image->GetPitchBytes(), static_cast<unsigned char *>(image->GetScanLineForWrite(0))};
}

void Commit(const LayerPixels &pixels) {
    pixels.image->Update(pixels.buffer, pixels.pitch, 0, 0, pixels.width, pixels.height);
    pixels.layer->SetImageModified(true);
    pixels.layer->Update();
}

void CopyChannel(unsigned char *dst, const unsigned char *src, int rows, int columns, int dstPitch,
                 int srcPitch) {
    for (int y = 0; y < rows; ++y, dst += dstPitch, src += srcPitch)
        for (int x = 0; x < columns; ++x, dst += 4, src += 4) *dst = *src;
}

tjs_error TJS_INTF_METHOD fillAlpha(tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *object) {
    auto layer = LayerObject(object);
    layer->IndependMainImage();
    auto image = layer->GetMainImage();
    auto texture = image->GetTexture();
    int width = static_cast<int>(image->GetWidth()), height = static_cast<int>(image->GetHeight());
    TVPBevyRenderCommand command{};
    command.operation = 4;
    command.target = TVPBevyTextureID(texture);
    command.sources[1] = command.target;
    command.rect[2] = width;
    command.rect[3] = height;
    command.source_rects[1][2] = width;
    command.source_rects[1][3] = height;
    command.parameters[0] = 255;
    command.method = "FillMask";
    if (krkr_render_dispatch(&command))
        TVPThrowExceptionMessage(TJS_W("LayerExBTOA GPU operation failed"));
    TVPBevyInvalidateTexture(texture);
    layer->SetImageModified(true);
    layer->Update(tTVPRect(layer->GetImageLeft(), layer->GetImageTop(),
                           layer->GetImageLeft() + width, layer->GetImageTop() + height));
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD copyRightBlueToLeftAlpha(tTJSVariant *, tjs_int, tTJSVariant **,
                                                   iTJSDispatch2 *object) {
    auto pixels = WritablePixels(object);
    int half = pixels.width / 2;
    CopyChannel(pixels.buffer + 3, pixels.buffer + half * 4 + 2, pixels.height, half, pixels.pitch,
                pixels.pitch);
    Commit(pixels);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD copyBottomBlueToTopAlpha(tTJSVariant *, tjs_int, tTJSVariant **,
                                                   iTJSDispatch2 *object) {
    auto pixels = WritablePixels(object);
    int half = pixels.height / 2;
    CopyChannel(pixels.buffer + 3, pixels.buffer + half * pixels.pitch + 2, half, pixels.width,
                pixels.pitch, pixels.pitch);
    Commit(pixels);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD copyAlphaToProvince(tTJSVariant *, tjs_int count, tTJSVariant **params,
                                              iTJSDispatch2 *object) {
    long threshold = count > 0 && params[0]->Type() != tvtVoid ? (long)params[0]->AsInteger() : -1;
    auto layer = LayerObject(object);
    auto image = layer->GetMainImage();
    int width = static_cast<int>(image->GetWidth()), height = static_cast<int>(image->GetHeight());
    int srcPitch = image->GetPitchBytes();
    auto src = static_cast<const unsigned char *>(image->GetScanLine(0));
    auto dst = static_cast<unsigned char *>(layer->GetProvinceImagePixelBufferForWrite());
    int dstPitch = layer->GetProvinceImagePixelBufferPitch();
    unsigned char low = static_cast<unsigned char>(threshold);
    int mode = threshold >= 0 && threshold < 256 ? 1 : threshold >= 256 ? 2 : 0;
    for (int y = 0; y < height; ++y, src += srcPitch, dst += dstPitch) {
        auto p = dst;
        auto q = src + 3;
        for (int x = 0; x < width; ++x, q += 4, ++p)
            *p = mode == 0 ? *q : mode == 1 ? static_cast<unsigned char>(*q >= low) : 0;
    }
    layer->SetImageModified(true);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD clipAlphaRect(tTJSVariant *, tjs_int count, tTJSVariant **params,
                                        iTJSDispatch2 *object) {
    if (count < 7) return TJS_E_BADPARAMCOUNT;
    long dx = (long)params[0]->AsInteger(), dy = (long)params[1]->AsInteger();
    iTJSDispatch2 *srcObject = params[2]->AsObjectNoAddRef();
    long sx = (long)params[3]->AsInteger(), sy = (long)params[4]->AsInteger();
    long w = (long)params[5]->AsInteger(), h = (long)params[6]->AsInteger();
    bool clear = false;
    unsigned char clearValue = 0;
    if (count >= 8 && params[7]->Type() != tvtVoid) {
        long value = (long)params[7]->AsInteger();
        clear = value >= 0 && value < 256;
        clearValue = static_cast<unsigned char>(value & 255);
    }
    if (w <= 0 || h <= 0) return TJS_E_INVALIDPARAM;

    auto dst = WritablePixels(object);
    auto srcLayer = srcObject == object ? dst.layer : LayerObject(srcObject);
    auto srcImage = srcLayer->GetMainImage();
    long diw = dst.width, dih = dst.height, dpitch = dst.pitch;
    long siw = srcImage->GetWidth(), sih = srcImage->GetHeight(), spitch = srcImage->GetPitchBytes();
    auto dbuf = dst.buffer;
    auto sbuf = static_cast<const unsigned char *>(srcImage->GetScanLine(0));
    bool clipped = false;

    if (sx + w <= 0 || sy + h <= 0 || sx >= siw || sy >= sih) goto none;
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    {
        long cut;
        if ((cut = sx + w - siw) > 0) w -= cut;
        if ((cut = sy + h - sih) > 0) h -= cut;
    }
    if (dx + w <= 0 || dy + h <= 0 || dx >= diw || dy >= dih) goto none;
    if (dx < 0) { w += dx; sx -= dx; dx = 0; }
    if (dy < 0) { h += dy; sy -= dy; dy = 0; }
    {
        long cut;
        if ((cut = dx + w - diw) > 0) w -= cut;
        if ((cut = dy + h - dih) > 0) h -= cut;
    }
    if (w <= 0 || h <= 0) goto none;

    clipped = true;
    for (long y = 0; y < dy; ++y)
        for (long x = 0, p = y * dpitch + 3; x < diw; ++x, p += 4)
            if (clear) dbuf[p] = clearValue;
    if (clear)
        for (long y = dy + h; y < dih; ++y)
            for (long x = 0, p = y * dpitch + 3; x < diw; ++x, p += 4) dbuf[p] = clearValue;

    for (long y = 0; y < h; ++y) {
        auto p = dbuf + (y + dy) * dpitch + 3;
        auto q = sbuf + (y + sy) * spitch + 3 + sx * 4;
        if (clear)
            for (long x = 0; x < dx; ++x, p += 4) *p = clearValue;
        p = dbuf + (y + dy) * dpitch + 3 + dx * 4;
        q = sbuf + (y + sy) * spitch + 3 + sx * 4;
        for (long x = 0; x < w; ++x, p += 4, q += 4) {
            unsigned long n = (unsigned long)*p * (unsigned long)*q;
            *p = static_cast<unsigned char>((n + (n >> 7)) >> 8);
        }
        if (clear)
            for (long x = dx + w; x < diw; ++x, p += 4) *p = clearValue;
    }

none:
    if (!clipped && clear) {
        for (long y = 0; y < dih; ++y) {
            auto p = dbuf + y * dpitch + 3;
            for (long x = 0; x < diw; ++x, p += 4) *p = clearValue;
        }
    }
    if (clipped || clear) Commit(dst);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD fillByProvince(tTJSVariant *, tjs_int count, tTJSVariant **params,
                                         iTJSDispatch2 *object) {
    if (count < 2) return TJS_E_BADPARAMCOUNT;
    unsigned char index = static_cast<unsigned char>(params[0]->AsInteger());
    unsigned long color = static_cast<unsigned long>(params[1]->AsInteger());
    unsigned char rgba[4] = {static_cast<unsigned char>((color >> 16) & 0xff),
                             static_cast<unsigned char>((color >> 8) & 0xff),
                             static_cast<unsigned char>(color & 0xff),
                             static_cast<unsigned char>((color >> 24) & 0xff)};
    auto pixels = WritablePixels(object);
    auto province = static_cast<const unsigned char *>(pixels.layer->GetProvinceImagePixelBuffer());
    int provincePitch = pixels.layer->GetProvinceImagePixelBufferPitch();
    if (!province) TVPThrowExceptionMessage(TJS_W("Layer has no province image"));
    for (int y = 0; y < pixels.height; ++y, province += provincePitch) {
        auto p = pixels.buffer + y * pixels.pitch;
        for (int x = 0; x < pixels.width; ++x, ++province, p += 4)
            if (*province == index) {
                p[0] = rgba[0];
                p[1] = rgba[1];
                p[2] = rgba[2];
                p[3] = rgba[3];
            }
    }
    Commit(pixels);
    return TJS_S_OK;
}
}

NCB_ATTACH_FUNCTION(copyRightBlueToLeftAlpha, Layer, copyRightBlueToLeftAlpha);
NCB_ATTACH_FUNCTION(copyBottomBlueToTopAlpha, Layer, copyBottomBlueToTopAlpha);
NCB_ATTACH_FUNCTION(fillAlpha, Layer, fillAlpha);
NCB_ATTACH_FUNCTION(copyAlphaToProvince, Layer, copyAlphaToProvince);
NCB_ATTACH_FUNCTION(clipAlphaRect, Layer, clipAlphaRect);
NCB_ATTACH_FUNCTION(fillByProvince, Layer, fillByProvince);
