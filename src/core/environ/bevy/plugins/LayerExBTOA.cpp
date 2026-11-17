// Adapted from krkrsdl3 plugins/LayerExBTOA.cpp; see LICENSE-THIRD-PARTY.md.
#define NCB_MODULE_NAME TJS_W("layerExBTOA.dll")
#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "BevyRender.h"
#include <algorithm>

namespace {
tTJSNI_BaseLayer *LayerObject(iTJSDispatch2 *object) {
    tTJSNI_BaseLayer *layer = nullptr;
    if (!object || TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
            tTJSNC_Layer::ClassID, reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer->GetMainImage())
        TVPThrowExceptionMessage(TJS_W("Layer image is required"));
    return layer;
}

void Dispatch(tTJSNI_BaseLayer *layer, iTVPTexture2D *target, TVPBevyRenderCommand &command,
              bool update = true) {
    command.operation = 4;
    command.target = TVPBevyTextureID(target);
    if (krkr_render_dispatch(&command))
        TVPThrowExceptionMessage(TJS_W("LayerExBTOA GPU operation failed"));
    TVPBevyInvalidateTexture(target);
    layer->SetImageModified(true);
    if (update) layer->Update();
}

TVPBevyRenderCommand Command(tTJSNI_BaseLayer *layer, int mode) {
    TVPBevyRenderCommand command{};
    command.method = "LayerExBTOA";
    command.rect[2] = layer->GetMainImage()->GetWidth();
    command.rect[3] = layer->GetMainImage()->GetHeight();
    command.parameters[4] = mode;
    return command;
}

tjs_error TJS_INTF_METHOD fillAlpha(tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *object) {
    auto layer = LayerObject(object);
    layer->IndependMainImage();
    auto texture = layer->GetMainImage()->GetTexture();
    auto command = Command(layer, 0);
    command.sources[1] = TVPBevyTextureID(texture);
    command.source_rects[1][2] = command.rect[2];
    command.source_rects[1][3] = command.rect[3];
    command.parameters[0] = 255;
    command.method = "FillMask";
    Dispatch(layer, texture, command);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD copyRightBlueToLeftAlpha(tTJSVariant *, tjs_int, tTJSVariant **,
                                                   iTJSDispatch2 *object) {
    auto layer = LayerObject(object);
    layer->IndependMainImage();
    auto texture = layer->GetMainImage()->GetTexture();
    auto command = Command(layer, 0);
    command.rect[2] /= 2;
    if (!command.rect[2]) return TJS_S_OK;
    command.sources[0] = TVPBevyTextureID(texture);
    command.parameters[5] = command.rect[2];
    Dispatch(layer, texture, command);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD copyBottomBlueToTopAlpha(tTJSVariant *, tjs_int, tTJSVariant **,
                                                   iTJSDispatch2 *object) {
    auto layer = LayerObject(object);
    layer->IndependMainImage();
    auto texture = layer->GetMainImage()->GetTexture();
    auto command = Command(layer, 1);
    command.rect[3] /= 2;
    if (!command.rect[3]) return TJS_S_OK;
    command.sources[0] = TVPBevyTextureID(texture);
    command.parameters[6] = command.rect[3];
    Dispatch(layer, texture, command);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD copyAlphaToProvince(tTJSVariant *, tjs_int count, tTJSVariant **params,
                                              iTJSDispatch2 *object) {
    auto layer = LayerObject(object);
    if (!layer->GetProvinceImage()) layer->SetProvincePixel(0, 0, 0);
    layer->IndependProvinceImage();
    auto command = Command(layer, 2);
    command.sources[0] = TVPBevyTextureID(layer->GetMainImage()->GetTexture());
    command.parameters[5] = count > 0 && params[0]->Type() != tvtVoid ? params[0]->AsInteger() : -1;
    Dispatch(layer, layer->GetProvinceImage()->GetTexture(), command, false);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD clipAlphaRect(tTJSVariant *, tjs_int count, tTJSVariant **params,
                                        iTJSDispatch2 *object) {
    if (count < 7) return TJS_E_BADPARAMCOUNT;
    int dx = params[0]->AsInteger(), dy = params[1]->AsInteger();
    int sx = params[3]->AsInteger(), sy = params[4]->AsInteger();
    int width = params[5]->AsInteger(), height = params[6]->AsInteger();
    if (width <= 0 || height <= 0) return TJS_E_INVALIDPARAM;
    auto layer = LayerObject(object);
    auto source = LayerObject(params[2]->AsObjectNoAddRef());
    layer->IndependMainImage();
    auto texture = layer->GetMainImage()->GetTexture();
    auto sourceTexture = source->GetMainImage()->GetTexture();
    auto command = Command(layer, 3);
    command.sources[0] = TVPBevyTextureID(sourceTexture);
    command.parameters[5] = sx - dx;
    command.parameters[6] = sy - dy;
    command.parameters[7] = -1;
    if (count >= 8 && params[7]->Type() != tvtVoid) {
        const auto clear = params[7]->AsInteger();
        if (clear >= 0 && clear < 256) command.parameters[7] = clear;
    }
    const int left = std::max({dx, dx - sx, 0});
    const int top = std::max({dy, dy - sy, 0});
    const int right = std::min({dx + width, dx - sx + int(sourceTexture->GetWidth()), command.rect[2]});
    const int bottom = std::min({dy + height, dy - sy + int(sourceTexture->GetHeight()), command.rect[3]});
    if ((right <= left || bottom <= top) && command.parameters[7] < 0) return TJS_S_OK;
    command.parameters[8] = left; command.parameters[9] = top;
    command.parameters[10] = right; command.parameters[11] = bottom;
    Dispatch(layer, texture, command);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD fillByProvince(tTJSVariant *, tjs_int count, tTJSVariant **params,
                                         iTJSDispatch2 *object) {
    if (count < 2) return TJS_E_BADPARAMCOUNT;
    auto layer = LayerObject(object);
    auto province = layer->GetProvinceImage();
    if (!province) TVPThrowExceptionMessage(TJS_W("Layer has no province image"));
    layer->IndependMainImage();
    auto command = Command(layer, 4);
    command.sources[0] = TVPBevyTextureID(province->GetTexture());
    command.parameters[5] = static_cast<unsigned char>(params[0]->AsInteger());
    const auto color = static_cast<tjs_uint32>(params[1]->AsInteger());
    command.parameters[12] = float((color >> 16) & 255) / 255;
    command.parameters[13] = float((color >> 8) & 255) / 255;
    command.parameters[14] = float(color & 255) / 255;
    command.parameters[15] = float(color >> 24) / 255;
    Dispatch(layer, layer->GetMainImage()->GetTexture(), command);
    return TJS_S_OK;
}

}

NCB_ATTACH_FUNCTION(copyRightBlueToLeftAlpha, Layer, copyRightBlueToLeftAlpha);
NCB_ATTACH_FUNCTION(copyBottomBlueToTopAlpha, Layer, copyBottomBlueToTopAlpha);
NCB_ATTACH_FUNCTION(fillAlpha, Layer, fillAlpha);
NCB_ATTACH_FUNCTION(copyAlphaToProvince, Layer, copyAlphaToProvince);
NCB_ATTACH_FUNCTION(clipAlphaRect, Layer, clipAlphaRect);
NCB_ATTACH_FUNCTION(fillByProvince, Layer, fillByProvince);
