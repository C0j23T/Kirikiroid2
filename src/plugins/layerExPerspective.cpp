#include <cmath>
#include <algorithm>

#define NCB_MODULE_NAME TJS_W("perspective.dll")

static const char *copyright = 
"----- AntiGrainGeometry Copyright START -----\n"
"Anti-Grain Geometry - Version 2.4\n"
"Copyright (C) 2002-2005 Maxim Shemanarev (McSeem)\n"
"\n"
"Permission to copy, use, modify, sell and distribute this software\n"
"is granted provided this copyright notice appears in all copies. \n"
"This software is provided \"as is\" without express or implied\n"
"warranty, and with no claim as to its suitability for any purpose.\n"
"----- AntiGrainGeometry Copyright END -----\n";

#include "ncbind/ncbind.hpp"

#include "LayerImpl.h"
#include "tjsNative.h"
#include "RenderManager.h"
static tjs_error PerspectiveCopy_GL( tTJSVariant *result, 
    tjs_int numparams, tTJSVariant **param,	iTJSDispatch2 *objthis)
{
    if(!objthis) return TJS_E_NATIVECLASSCRASH;
    tTJSNI_Layer *_this;
    { 
        tjs_error hr; 
        hr = objthis->NativeInstanceSupport(TJS_NIS_GETINSTANCE, 
            tTJSNC_Layer::ClassID, (iTJSNativeInstance**)&_this); 
        if(TJS_FAILED(hr)) return TJS_E_NATIVECLASSCRASH; 
    }

    if (numparams < 13) return TJS_E_BADPARAMCOUNT;

    tTJSNI_BaseLayer * src = NULL;
    tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
    if(!clo.Object) TVPThrowExceptionMessage(TVPSpecifyLayer);
    if(clo.Object)
    {
        if(TJS_FAILED(clo.Object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
            tTJSNC_Layer::ClassID, (iTJSNativeInstance**)&src)))
            TVPThrowExceptionMessage(TVPSpecifyLayer);
    }

    if (!src->GetMainImage() || !_this->GetMainImage()) TVPThrowExceptionMessage(TJS_W("Layer has no image"));
    _this->IndependMainImage();

    double g_x1 = param[1]->AsReal();
    double g_y1 = param[2]->AsReal();
    double g_x2 = g_x1 + param[3]->AsReal();
    double g_y2 = g_y1 + param[4]->AsReal();

    tTVPPointD srcpt[4] = {
        { g_x1, g_y1 },
        { g_x2, g_y1 },
		{ g_x1, g_y2 },
        { g_x2, g_y2 },
    };
    tTVPPointD dstpt[4] = {
        { param[5 ]->AsReal(), param[6 ]->AsReal() }, // 左上
        { param[7 ]->AsReal(), param[8 ]->AsReal() }, // 右上
		{ param[9]->AsReal(), param[10]->AsReal() }, // 左下
        { param[11]->AsReal(), param[12]->AsReal() }, // 右下
    };
	static iTVPRenderMethod *method = TVPGetRenderManager()->GetRenderMethod("PerspectiveAlphaBlend_a");
	static int id_opa = method->EnumParameterID("opacity");
	method->SetParameterOpa(id_opa, 255);
	iTVPTexture2D *tex = src->GetMainImage()->GetTexture();
	tRenderTexQuadArray::Element src_tex[] = {
		tRenderTexQuadArray::Element(tex, srcpt)
	};
    auto clip = _this->GetClip();
    clip.clip(tTVPRect(0, 0, _this->GetMainImage()->GetWidth(), _this->GetMainImage()->GetHeight()));
    double left = dstpt[0].x, top = dstpt[0].y, right = left, bottom = top;
    for (auto &p : srcpt) if (!std::isfinite(p.x) || !std::isfinite(p.y))
        TVPThrowExceptionMessage(TJS_W("Invalid perspective source coordinate"));
    for (auto &p : dstpt) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            TVPThrowExceptionMessage(TJS_W("Invalid perspective coordinate"));
        left = std::min(left, p.x); top = std::min(top, p.y);
        right = std::max(right, p.x); bottom = std::max(bottom, p.y);
    }
    if (clip.is_empty() || left >= clip.right || top >= clip.bottom || right <= clip.left || bottom <= clip.top)
        return TJS_S_OK;
    tTVPRect rctar(std::max(left, double(clip.left)), std::max(top, double(clip.top)),
                  std::min(std::ceil(right), double(clip.right)), std::min(std::ceil(bottom), double(clip.bottom)));
    TVPGetRenderManager()->OperatePerspective(method, 1,
		_this->GetMainImage()->GetTextureForRender(method->IsBlendTarget(), &rctar), nullptr, rctar, dstpt,
		tRenderTexQuadArray(src_tex));
	_this->SetImageModified(true);
    _this->Update();
    return TJS_S_OK;
}

struct Perspective {};
NCB_ATTACH_CLASS(Perspective, Layer) {
    RawCallback("perspectiveCopy", PerspectiveCopy_GL, 0);
}
