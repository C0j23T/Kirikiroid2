//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Base Layer Bitmap implementation
//---------------------------------------------------------------------------
#include <vector>

#include "tjsCommHead.h"

#include "DebugIntf.h"
#include "LayerBitmapIntf.h"
#include "MsgIntf.h"
#include "DebugIntf.h"
#include "tvpgl.h"
#include "argb.h"
#include "tjsUtils.h"
#include "ThreadIntf.h"
#include "RenderManager.h"
#include <assert.h>

//#define TVP_FORCE_BILINEAR


//---------------------------------------------------------------------------
// To forcing bilinear interpolation, define TVP_FORCE_BILINEAR.

#ifdef TVP_FORCE_BILINEAR
	#define TVP_BILINEAR_FORCE_COND true
#else
	#define TVP_BILINEAR_FORCE_COND false
#endif
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// intact ( does not affect ) gamma adjustment data
tTVPGLGammaAdjustData TVPIntactGammaAdjustData =
{ 1.0, 0, 255, 1.0, 0, 255, 1.0, 0, 255 };
//---------------------------------------------------------------------------
#define RET_VOID
#define BOUND_CHECK(x) \
{ \
	tjs_int i; \
	if(rect.left < 0) rect.left = 0; \
	if(rect.top < 0) rect.top = 0; \
	if(rect.right > (i=GetWidth())) rect.right = i; \
	if(rect.bottom > (i=GetHeight())) rect.bottom = i; \
	if(rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0) \
		return x; \
}


//---------------------------------------------------------------------------
// tTVPBaseBitmap
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void iTVPBaseBitmap::SetSizeWithFill(tjs_uint w, tjs_uint h, tjs_uint32 fillvalue)
{
	// resize, and fill the expanded region with specified value.

	tjs_uint orgw = GetWidth();
	tjs_uint orgh = GetHeight();

	SetSize(w, h);

	if(w > orgw && h > orgh)
	{
		// both width and height were expanded
		tTVPRect rect;
		rect.left = orgw;
		rect.top = 0;
		rect.right = w;
		rect.bottom = h;
		Fill(rect, fillvalue);

		rect.left = 0;
		rect.top = orgh;
		rect.right = orgw;
		rect.bottom = h;
		Fill(rect, fillvalue);
	}
	else if(w > orgw)
	{
		// width was expanded
		tTVPRect rect;
		rect.left = orgw;
		rect.top = 0;
		rect.right = w;
		rect.bottom = h;
		Fill(rect, fillvalue);
	}
	else if(h > orgh)
	{
		// height was expanded
		tTVPRect rect;
		rect.left = 0;
		rect.top = orgh;
		rect.right = w;
		rect.bottom = h;
		Fill(rect, fillvalue);
	}
}
//---------------------------------------------------------------------------
tjs_uint32 iTVPBaseBitmap::GetPoint(tjs_int x, tjs_int y) const
{
	// get specified point's color or color index
	if(x < 0 || y < 0 || x >= (tjs_int)GetWidth() || y >= (tjs_int)GetHeight())
		TVPThrowExceptionMessage(TVPOutOfRectangle);

	return Bitmap->GetPoint(x, y);
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::SetPoint(tjs_int x, tjs_int y, tjs_uint32 value)
{
    return Fill(tTVPRect(x,y,x+1,y+1),value);
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::SetPointMain(tjs_int x, tjs_int y, tjs_uint32 color)
{
    return FillColor(tTVPRect(x,y,x+1,y+1),color,255);
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::SetPointMask(tjs_int x, tjs_int y, tjs_int mask)
{
    return FillMask(tTVPRect(x,y,x+1,y+1),mask);
}
//---------------------------------------------------------------------------
bool tTVPBaseBitmap::Fill(tTVPRect rect, tjs_uint32 value)
{
	// fill target rectangle represented as "rect", with color ( and opacity )
	// passed by "value".
	// value must be : 0xAARRGGBB (for 32bpp) or 0xII ( for 8bpp )
	BOUND_CHECK(false);

	if (Is32BPP()) value = TVP_REVRGB(value);
	static iTVPRenderMethod* method = GetRenderManager()->GetRenderMethod("FillARGB");
	static int paramid = method->EnumParameterID("color");
	method->SetParameterColor4B(paramid, value);
	iTVPTexture2D *reftex = GetTexture();
	GetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
		reftex, rect, tRenderTexRectArray());

	return true;
}

bool iTVPBaseBitmap::Fill(tTVPRect rect, tjs_uint32 value)
{
	// fill target rectangle represented as "rect", with color ( and opacity )
	// passed by "value".
	// value must be : 0xAARRGGBB (for 32bpp) or 0xII ( for 8bpp )
	BOUND_CHECK(false);
	if (Is32BPP()) value = TVP_REVRGB(value);

	static iTVPRenderMethod* method = TVPGetRenderManager()->GetRenderMethod("FillARGB");
	static int paramid = method->EnumParameterID("color");
	method->SetParameterColor4B(paramid, value);
	iTVPTexture2D *reftex = GetTexture();
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
		reftex, rect, tRenderTexRectArray());
        return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::FillColor(tTVPRect rect, tjs_uint32 color, tjs_int opa)
{
	// fill rectangle with specified color.
	// this ignores destination alpha (destination alpha will not change)
	// opa is fill opacity if opa is positive value.
	// negative value of opa is not allowed.
	BOUND_CHECK(false);

	if(!Is32BPP()) TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

	if(opa == 0) return false; // no action

	if(opa < 0) opa = 0;
	if(opa > 255) opa = 255;

	color = TVP_REVRGB(color);
	iTVPRenderMethod *method;
	if (opa == 255) {
		static iTVPRenderMethod *_method = TVPGetRenderManager()->GetRenderMethod("FillColor");
		static int opa_id = _method->EnumParameterID("opacity"), color_id = _method->EnumParameterID("color");
		method = _method;
		method->SetParameterOpa(opa_id, opa);
		method->SetParameterColor4B(color_id, color);
	} else {
		static iTVPRenderMethod *_method = TVPGetRenderManager()->GetRenderMethod("ConstColorAlphaBlend");
		static int color_id = _method->EnumParameterID("color");
		method = _method;
		method->SetParameterColor4B(color_id, color | 0xFF000000);
        }
	iTVPTexture2D *reftex = GetTexture();
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
		reftex, rect, tRenderTexRectArray());
        return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::BlendColor(tTVPRect rect, tjs_uint32 color, tjs_int opa,
	bool additive)
{
	// fill rectangle with specified color.
	// this considers destination alpha (additive or simple)

	BOUND_CHECK(false);

	if(!Is32BPP()) TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

	if(opa == 0) return false; // no action
	if(opa < 0) opa = 0;
	if(opa > 255) opa = 255;

        if(opa == 255 && !IsIndependent())
          {
            if(rect.left == 0 && rect.top == 0 &&
               rect.right == (tjs_int)GetWidth() && rect.bottom == (tjs_int)GetHeight())
              {
                // cover overall
                IndependNoCopy(); // indepent with no-copy
              }
          }
	color = TVP_REVRGB(color);
	iTVPRenderMethod *method;
	if (opa == 255) {
		static iTVPRenderMethod *_method = TVPGetRenderManager()->GetRenderMethod("FillARGB");
		static int color_id = _method->EnumParameterID("color");
		method = _method;
		method->SetParameterColor4B(color_id, color | 0xFF000000);
	} else if (!additive) {
		static iTVPRenderMethod *_method = TVPGetRenderManager()->GetRenderMethod("ConstColorAlphaBlend_d");
		static int opa_id = _method->EnumParameterID("opacity"), color_id = _method->EnumParameterID("color");
		method = _method;
		method->SetParameterOpa(opa_id, opa);
		method->SetParameterColor4B(color_id, color);
	} else {
		static iTVPRenderMethod *_method = TVPGetRenderManager()->GetRenderMethod("ConstColorAlphaBlend_a");
		static int opa_id = _method->EnumParameterID("opacity"), color_id = _method->EnumParameterID("color");
		method = _method;
		method->SetParameterOpa(opa_id, opa);
		method->SetParameterColor4B(color_id, color);
        }
	iTVPTexture2D *reftex = GetTexture();
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
		reftex, rect, tRenderTexRectArray());
        return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::RemoveConstOpacity(tTVPRect rect, tjs_int level)
{
	// remove constant opacity from bitmap. ( similar to PhotoShop's eraser tool )
	// level is a strength of removing ( 0 thru 255 )
	// this cannot work with additive alpha mode.

	BOUND_CHECK(false);

	if(!Is32BPP()) TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

	if(level == 0) return false; // no action
	if(level < 0) level = 0;
	if(level > 255) level = 255;

	iTVPRenderMethod *method;
	if (level == 255) {
		static iTVPRenderMethod *_method = TVPGetRenderManager()->GetRenderMethod("FillMask");
		static int opa_id = _method->EnumParameterID("opacity");
		method = _method;
		method->SetParameterOpa(opa_id, 0);
	} else {
		static iTVPRenderMethod *_method = TVPGetRenderManager()->GetRenderMethod("RemoveConstOpacity");
		static int opa_id = _method->EnumParameterID("opacity");
		method = _method;
		method->SetParameterOpa(opa_id, level);
        }
	iTVPTexture2D *reftex = GetTexture();
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
		reftex, rect, tRenderTexRectArray());
        return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::FillMask(tTVPRect rect, tjs_int value)
{
	// fill mask with specified value.
	BOUND_CHECK(false);

	if(!Is32BPP()) TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

	static iTVPRenderMethod *method = TVPGetRenderManager()->GetRenderMethod("FillMask");
	static int opa_id = method->EnumParameterID("opacity");
	method->SetParameterOpa(opa_id, value);
	iTVPTexture2D *reftex = GetTexture();
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect), reftex,
		rect, tRenderTexRectArray());
        return true;
}
//---------------------------------------------------------------------------

bool tTVPBaseBitmap::CopyRect(tjs_int x, tjs_int y, const iTVPBaseBitmap *ref,
	tTVPRect refrect, tjs_int plane)
{
	// copy bitmap rectangle.
	// TVP_BB_COPY_MAIN in "plane" : main image is copied
	// TVP_BB_COPY_MASK in "plane" : mask image is copied
	// "plane" is ignored if the bitmap is 8bpp
	// the source rectangle is ( "refrect" ) and the destination upper-left corner
	// is (x, y).
	if (!Is32BPP()) plane = (TVP_BB_COPY_MASK | TVP_BB_COPY_MAIN);
	if (x == 0 && y == 0 && refrect.left == 0 && refrect.top == 0 &&
		refrect.right == (tjs_int)ref->GetWidth() &&
		refrect.bottom == (tjs_int)ref->GetHeight() &&
		(tjs_int)GetWidth() == refrect.right &&
		(tjs_int)GetHeight() == refrect.bottom &&
		plane == (TVP_BB_COPY_MASK | TVP_BB_COPY_MAIN) &&
		(bool)!Is32BPP() == (bool)!ref->Is32BPP())
	{
		// entire area of both bitmaps
		AssignTexture(ref->GetTexture());
		return true;
	}

	// bound check
	tjs_int bmpw, bmph;

	bmpw = ref->GetWidth();
	bmph = ref->GetHeight();

	if (refrect.left < 0)
		x -= refrect.left, refrect.left = 0;
	if (refrect.right > bmpw)
		refrect.right = bmpw;

	if (refrect.left >= refrect.right) return false;

	if (refrect.top < 0)
		y -= refrect.top, refrect.top = 0;
	if (refrect.bottom > bmph)
		refrect.bottom = bmph;

	if (refrect.top >= refrect.bottom) return false;

	bmpw = GetWidth();
	bmph = GetHeight();

	tTVPRect rect;
	rect.left = x;
	rect.top = y;
	rect.right = rect.left + refrect.get_width();
	rect.bottom = rect.top + refrect.get_height();

	if (rect.left < 0)
	{
		refrect.left += -rect.left;
		rect.left = 0;
	}

	if (rect.right > bmpw)
	{
		refrect.right -= (rect.right - bmpw);
		rect.right = bmpw;
	}

	if (refrect.left >= refrect.right) return false; // not drawable

	if (rect.top < 0)
	{
		refrect.top += -rect.top;
		rect.top = 0;
	}

	if (rect.bottom > bmph)
	{
		refrect.bottom -= (rect.bottom - bmph);
		rect.bottom = bmph;
	}

	if (refrect.top >= refrect.bottom) return false; // not drawable

	iTVPRenderMethod *method;
	switch (plane)
	{
	case TVP_BB_COPY_MAIN:
	{
		static iTVPRenderMethod *_method = GetRenderManager()->GetRenderMethod("CopyColor");
		method = _method;
	}
		break;
	case TVP_BB_COPY_MASK:
	{
		static iTVPRenderMethod *_method = GetRenderManager()->GetRenderMethod("CopyMask");
		method = _method;
	}
		break;
	case TVP_BB_COPY_MAIN | TVP_BB_COPY_MASK:
	{
		static iTVPRenderMethod *_method = GetRenderManager()->GetRenderMethod("Copy");
		method = _method;
	}
		break;
	}
	tRenderTexRectArray::Element src_tex[] = {
		tRenderTexRectArray::Element(ref->GetTexture(), refrect)
	};
	iTVPTexture2D *reftex = GetTexture();
	GetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect), reftex,
		rect, tRenderTexRectArray(src_tex));

	return true;
}

bool iTVPBaseBitmap::CopyRect(tjs_int x, tjs_int y, const iTVPBaseBitmap *ref,
		tTVPRect refrect, tjs_int plane)
{
	// copy bitmap rectangle.
	// TVP_BB_COPY_MAIN in "plane" : main image is copied
	// TVP_BB_COPY_MASK in "plane" : mask image is copied
	// "plane" is ignored if the bitmap is 8bpp
	// the source rectangle is ( "refrect" ) and the destination upper-left corner
	// is (x, y).
	if(!Is32BPP()) plane = (TVP_BB_COPY_MASK|TVP_BB_COPY_MAIN);
	if(x == 0 && y == 0 && refrect.left == 0 && refrect.top == 0 &&
		refrect.right == (tjs_int)ref->GetWidth() &&
		refrect.bottom == (tjs_int)ref->GetHeight() &&
		(tjs_int)GetWidth() == refrect.right &&
		(tjs_int)GetHeight() == refrect.bottom &&
		plane == (TVP_BB_COPY_MASK|TVP_BB_COPY_MAIN) &&
		(bool)!Is32BPP() == (bool)!ref->Is32BPP())
	{
		// entire area of both bitmaps
		AssignTexture(ref->GetTexture());
		return true;
	}

	// bound check
	tjs_int bmpw, bmph;

	bmpw = ref->GetWidth();
	bmph = ref->GetHeight();

	if(refrect.left < 0)
		x -= refrect.left, refrect.left = 0;
	if(refrect.right > bmpw)
		refrect.right = bmpw;

	if(refrect.left >= refrect.right) return false;

	if(refrect.top < 0)
		y -= refrect.top, refrect.top = 0;
	if(refrect.bottom > bmph)
		refrect.bottom = bmph;

	if(refrect.top >= refrect.bottom) return false;

	bmpw = GetWidth();
	bmph = GetHeight();

	tTVPRect rect;
	rect.left = x;
	rect.top = y;
	rect.right = rect.left + refrect.get_width();
	rect.bottom = rect.top + refrect.get_height();

	if(rect.left < 0)
	{
		refrect.left += -rect.left;
		rect.left = 0;
	}

	if(rect.right > bmpw)
	{
		refrect.right -= (rect.right - bmpw);
		rect.right = bmpw;
	}

	if(refrect.left >= refrect.right) return false; // not drawable

	if(rect.top < 0)
	{
		refrect.top += -rect.top;
		rect.top = 0;
	}

	if(rect.bottom > bmph)
	{
		refrect.bottom -= (rect.bottom - bmph);
		rect.bottom = bmph;
	}

	if(refrect.top >= refrect.bottom) return false; // not drawable

	iTVPRenderMethod *method;
		switch(plane)
		{
		case TVP_BB_COPY_MAIN:
			{
			static iTVPRenderMethod *_method = TVPGetRenderManager()->GetRenderMethod("CopyColor");
			method = _method;
			}
			break;
		case TVP_BB_COPY_MASK:
			{
			static iTVPRenderMethod *_method = TVPGetRenderManager()->GetRenderMethod("CopyMask");
			method = _method;
			}
			break;
		case TVP_BB_COPY_MAIN|TVP_BB_COPY_MASK:
			{
			static iTVPRenderMethod *_method = TVPGetRenderManager()->GetRenderMethod("Copy");
			method = _method;
			}
			break;
		}
	tRenderTexRectArray::Element src_tex[] = {
		tRenderTexRectArray::Element(ref->GetTexture(), refrect)
	};
	iTVPTexture2D *reftex = GetTexture();
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect), reftex,
		rect, tRenderTexRectArray(src_tex));
	return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::Copy9Patch(const iTVPBaseBitmap *source, tTVPRect &margin)
{
    const int width = source->GetWidth(), height = source->GetHeight();
    const int target_width = GetWidth(), target_height = GetHeight();
    if (width < 3 || height < 3 || target_width < width-2 || target_height < height-2) return false;
    auto span = [&](bool horizontal, bool content) {
        const int length = horizontal ? width : height;
        int first = -1, last = -1;
        for (int i = 1; i < length-1; ++i) {
            const int x = horizontal ? i : (content ? width-1 : 0);
            const int y = horizontal ? (content ? height-1 : 0) : i;
            if ((source->GetPoint(x,y) >> 24) == 255) {
                if (first < 0) first = i;
                last = i+1;
            }
        }
        return std::make_pair(first,last);
    };
    const auto horizontal = span(true,false), vertical = span(false,false);
    if (horizontal.first < 0 || vertical.first < 0) return false;
    const auto content_x = span(true,true), content_y = span(false,true);
    margin = tTVPRect(content_x.first < 0 ? -1 : content_x.first-1,
                     content_y.first < 0 ? -1 : content_y.first-1,
                     content_x.second < 0 ? -1 : width-1-content_x.second,
                     content_y.second < 0 ? -1 : height-1-content_y.second);
    const int sx[] = {1,horizontal.first,horizontal.second,width-1};
    const int sy[] = {1,vertical.first,vertical.second,height-1};
    const int dx[] = {0,sx[1]-1,target_width-(width-1-sx[2]),target_width};
    const int dy[] = {0,sy[1]-1,target_height-(height-1-sy[2]),target_height};
    const tTVPRect clip(0,0,target_width,target_height);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            StretchBlt(clip,tTVPRect(dx[x],dy[y],dx[x+1],dy[y+1]),source,
                       tTVPRect(sx[x],sy[y],sx[x+1],sy[y+1]),bmCopy,255,false,stLinear);
    return true;
}

bool iTVPBaseBitmap::Blt(tjs_int x, tjs_int y, const iTVPBaseBitmap *ref,
	tTVPRect refrect, tTVPBBBltMethod method, tjs_int opa, bool hda)
{
	// blt src bitmap with various methods.

	// hda option ( hold destination alpha ) holds distination alpha,
	// but will select more complex function ( and takes more time ) for it ( if
	// can do )

	// this function does not matter whether source and destination bitmap is
	// overlapped.

	if(opa == 255 && method == bmCopy && !hda)
	{
		return CopyRect(x, y, ref, refrect);
	}

	if(!Is32BPP()) TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

	if(opa == 0) return false; // opacity==0 has no action

	// bound check
	tjs_int bmpw, bmph;

	bmpw = ref->GetWidth();
	bmph = ref->GetHeight();

	if(refrect.left < 0)
		x -= refrect.left, refrect.left = 0;
	if(refrect.right > bmpw)
		refrect.right = bmpw;

	if(refrect.left >= refrect.right) return false;

	if(refrect.top < 0)
		y -= refrect.top, refrect.top = 0;
	if(refrect.bottom > bmph)
		refrect.bottom = bmph;

	if(refrect.top >= refrect.bottom) return false;

	bmpw = GetWidth();
	bmph = GetHeight();


	tTVPRect rect;
	rect.left = x;
	rect.top = y;
	rect.right = rect.left + refrect.get_width();
	rect.bottom = rect.top + refrect.get_height();

	if(rect.left < 0)
	{
		refrect.left += -rect.left;
		rect.left = 0;
	}

	if(rect.right > bmpw)
	{
		refrect.right -= (rect.right - bmpw);
		rect.right = bmpw;
	}

	if(refrect.left >= refrect.right) return false; // not drawable

	if(rect.top < 0)
	{
		refrect.top += -rect.top;
		rect.top = 0;
	}

	if(rect.bottom > bmph)
	{
		refrect.bottom -= (rect.bottom - bmph);
		rect.bottom = bmph;
	}

	if(refrect.top >= refrect.bottom) return false; // not drawable

	tRenderTexRectArray::Element src_tex[] = {
		tRenderTexRectArray::Element(ref->GetTexture(), refrect)
	};
	iTVPRenderManager *mgr = GetRenderManager();
	iTVPRenderMethod *rmethod = mgr->GetRenderMethod(opa, hda, method);
	if (!rmethod) return false;
	iTVPTexture2D *reftex = GetTexture();
	mgr->OperateRect(rmethod, GetTextureForRender(rmethod->IsBlendTarget(), &rect), reftex,
		rect, tRenderTexRectArray(src_tex));
        return true;
}

//---------------------------------------------------------------------------
bool iTVPBaseBitmap::Blt(tjs_int x, tjs_int y, const iTVPBaseBitmap *ref,
	const tTVPRect &refrect, tTVPLayerType type, tjs_int opa, bool hda) {

	tTVPBBBltMethod met;
	switch (type) {
	case ltBinder:
		// no action
		return false;

	case ltOpaque: // formerly ltCoverRect
				   // copy
		met = opa == 255 ? bmCopy : bmCopyOnAlpha;
		break;

	case ltAlpha: // formerly ltTransparent
				  // alpha blend
		met = hda ? bmAlpha : bmAlphaOnAlpha;
		break;

	case ltAdditive:
		// additive blend
		// hda = true if destination has alpha
		// ( preserving mask )
		met = bmAdd;
		break;

	case ltSubtractive:
		// subtractive blend
		met = bmSub;
		break;

	case ltMultiplicative:
		// multiplicative blend
		met = bmMul;
		break;

	case ltDodge:
		// color dodge ( "Ooi yaki" in Japanese )
		met = bmDodge;
		break;

	case ltDarken:
		// darken blend (select lower luminosity)
		met = bmDarken;
		break;

	case ltLighten:
		// lighten blend (select higher luminosity)
		met = bmLighten;
		break;

	case ltScreen:
		// screen multiplicative blend
		met = bmScreen;
		break;

	case ltAddAlpha:
		// alpha blend
		met = bmAddAlpha;
		break;

	case ltPsNormal:
		// Photoshop compatible normal blend
		met = bmPsNormal;
		break;

	case ltPsAdditive:
		// Photoshop compatible additive blend
		met = bmPsAdditive;
		break;

	case ltPsSubtractive:
		// Photoshop compatible subtractive blend
		met = bmPsSubtractive;
		break;

	case ltPsMultiplicative:
		// Photoshop compatible multiplicative blend
		met = bmPsMultiplicative;
		break;

	case ltPsScreen:
		// Photoshop compatible screen blend
		met = bmPsScreen;
		break;

	case ltPsOverlay:
		// Photoshop compatible overlay blend
		met = bmPsOverlay;
		break;

	case ltPsHardLight:
		// Photoshop compatible hard light blend
		met = bmPsHardLight;
		break;

	case ltPsSoftLight:
		// Photoshop compatible soft light blend
		met = bmPsSoftLight;
		break;

	case ltPsColorDodge:
		// Photoshop compatible color dodge blend
		met = bmPsColorDodge;
		break;

	case ltPsColorDodge5:
		// Photoshop 5.x compatible color dodge blend
		met = bmPsColorDodge5;
		break;

	case ltPsColorBurn:
		// Photoshop compatible color burn blend
		met = bmPsColorBurn;
		break;

	case ltPsLighten:
		// Photoshop compatible compare (lighten) blend
		met = bmPsLighten;
		break;

	case ltPsDarken:
		// Photoshop compatible compare (darken) blend
		met = bmPsDarken;
		break;

	case ltPsDifference:
		// Photoshop compatible difference blend
		met = bmPsDifference;
		break;

	case ltPsDifference5:
		// Photoshop 5.x compatible difference blend
		met = bmPsDifference5;
		break;

	case ltPsExclusion:
		// Photoshop compatible exclusion blend
		met = bmPsExclusion;
		break;

	default:
		return false;
	}

	return Blt(x, y, ref, refrect, met, opa, hda);
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::StretchBlt(tTVPRect cliprect,
		tTVPRect destrect, const iTVPBaseBitmap *ref,
		tTVPRect refrect, tTVPBBBltMethod method, tjs_int opa,
			bool hda, tTVPBBStretchType mode, tjs_real typeopt )
{
	// do stretch blt
	// stFastLinear is enabled only in following condition:
	// -------TODO: write corresponding condition--------

	// stLinear and stCubic mode are enabled only in following condition:
	// any magnification, opa:255, method:bmCopy, hda:false
	// no reverse, destination rectangle is within the image.

	// source and destination check
	tjs_int dw = destrect.get_width(), dh = destrect.get_height();
	tjs_int rw = refrect.get_width(), rh = refrect.get_height();

	if(!dw || !rw || !dh || !rh) return false; // nothing to do

	// quick check for simple blt
	if(dw == rw && dh == rh && destrect.included_in(cliprect))
	{
		return Blt(destrect.left, destrect.top, ref, refrect, method, opa, hda);
			// no stretch; do normal blt
	}

	if(!Is32BPP()) TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

	// check for special case noticed above

	//--- extract stretch type
	tTVPBBStretchType type = (tTVPBBStretchType)(mode & stTypeMask);

	//--- pull the dimension
	tjs_int w = GetWidth();
	tjs_int h = GetHeight();
	tjs_int refw = ref->GetWidth();
	tjs_int refh = ref->GetHeight();

	//--- clop clipping rectangle with the image
	tTVPRect cr = cliprect;
	if(cr.left < 0) cr.left = 0;
	if(cr.top < 0) cr.top = 0;
	if(cr.right > w) cr.right = w;
	if(cr.bottom > h) cr.bottom = h;

	if (cr.left > destrect.left) {
		refrect.left += (float)rw / dw * (cr.left - destrect.left);
		destrect.left = cr.left;
	}
	if (cr.right < destrect.right) {
		refrect.right -= (float)refrect.get_width() / destrect.get_width() * (destrect.right - cr.right);
		destrect.right = cr.right;
	}
	if (cr.top > destrect.top) {
		refrect.top += (float)rh / dh * (cr.top - destrect.top);
		destrect.top = cr.top;
	}
	if (cr.bottom < destrect.bottom) {
		refrect.bottom -= (float)refrect.get_height() / destrect.get_height() * (destrect.bottom - cr.bottom);
		destrect.bottom = cr.bottom;
	}

	static int StretchTypeId = TVPGetRenderManager()->EnumParameterID("StretchType");
	TVPGetRenderManager()->SetParameterInt(StretchTypeId, (int)type);

	tRenderTexRectArray::Element src_tex[] = {
		tRenderTexRectArray::Element(ref->GetTexture(), refrect)
	};
	iTVPRenderManager *mgr = GetRenderManager();
	iTVPRenderMethod *rmethod = mgr->GetRenderMethod(opa, hda, method);
	if (!rmethod) return false;
	iTVPTexture2D *reftex = GetTexture();
	mgr->OperateRect(rmethod, GetTextureForRender(rmethod->IsBlendTarget(), &destrect), reftex,
		destrect, tRenderTexRectArray(src_tex));
	return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::AffineBlt(tTVPRect destrect, const iTVPBaseBitmap *ref,
		tTVPRect refrect, const tTVPPointD * points_in,
			tTVPBBBltMethod method, tjs_int opa,
			tTVPRect * updaterect,
			bool hda, tTVPBBStretchType mode, bool clear, tjs_uint32 clearcolor)
{
	// check source rectangle
	if (refrect.left >= refrect.right ||
		refrect.top >= refrect.bottom) return false;
	if (refrect.left < 0 || refrect.top < 0 ||
		refrect.right >(tjs_int)ref->GetWidth() ||
		refrect.bottom >(tjs_int)ref->GetHeight())
		TVPThrowExceptionMessage(TVPOutOfRectangle);

	// create point list in fixed point real format

	if (destrect.left < 0) destrect.left = 0;
	if (destrect.top < 0) destrect.top = 0;
	if (destrect.right >(tjs_int)GetWidth()) destrect.right = GetWidth();
	if (destrect.bottom >(tjs_int)GetHeight()) destrect.bottom = GetHeight();

	if (destrect.left >= destrect.right ||
		destrect.top >= destrect.bottom) return false; // not drawable

	if (clear)
	{
		if (hda)
			FillColor(destrect, clearcolor, 255);
		else
			Fill(destrect, clearcolor);
	}

	tTVPPointD dstpt[6] = {
		{ points_in[0].x, points_in[0].y }, // left-top
		{ points_in[1].x, points_in[1].y }, // right-top
		{ points_in[2].x, points_in[2].y }, // left-bottom

		{ points_in[1].x, points_in[1].y }, // right-top
		{ points_in[2].x, points_in[2].y }, // left-bottom
		{ points_in[1].x - points_in[0].x + points_in[2].x, points_in[1].y - points_in[0].y + points_in[2].y }, // right-bottom
	};
	tTVPPointD refpt[6] = {
		{ (double)refrect.left, (double)refrect.top },
		{ (double)refrect.right, (double)refrect.top },
		{ (double)refrect.left, (double)refrect.bottom },

		{ (double)refrect.right, (double)refrect.top },
		{ (double)refrect.left, (double)refrect.bottom },
		{ (double)refrect.right, (double)refrect.bottom },
	};

	tTVPBBStretchType type = (tTVPBBStretchType)(mode & stTypeMask);
	static int StretchTypeId = TVPGetRenderManager()->EnumParameterID("StretchType");
	TVPGetRenderManager()->SetParameterInt(StretchTypeId, (int)type);
	iTVPRenderManager *mgr = GetRenderManager();
	iTVPRenderMethod *_method = mgr->GetRenderMethod(opa, hda, method);
	if (!_method) return false;
	tRenderTexQuadArray::Element src_tex[] = { tRenderTexQuadArray::Element(ref->GetTexture(), refpt) };
	iTVPTexture2D *reftex = GetTexture();
	mgr->OperateTriangles(_method, 2, GetTextureForRender(_method->IsBlendTarget(), &destrect),
		reftex, destrect, dstpt, tRenderTexQuadArray(src_tex));
	if (updaterect) *updaterect = destrect;
	return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::AffineBlt(tTVPRect destrect, const iTVPBaseBitmap *ref,
		tTVPRect refrect, const t2DAffineMatrix & matrix,
			tTVPBBBltMethod method, tjs_int opa,
			tTVPRect * updaterect,
			bool hda, tTVPBBStretchType type, bool clear, tjs_uint32 clearcolor)
{
	// do transformation using 2D affine matrix.
	//  x' =  ax + cy + tx
	//  y' =  bx + dy + ty
	tTVPPointD points[3];
	int rp = refrect.get_width();
	int bp = refrect.get_height();

	// note that a pixel has actually 1 x 1 size, so
	// a pixel at (0,0) covers (-0.5, -0.5) - (0.5, 0.5).

#define CALC_X(x, y) (matrix.a * (x) + matrix.c * (y) + matrix.tx)
#define CALC_Y(x, y) (matrix.b * (x) + matrix.d * (y) + matrix.ty)

	// - upper-left
	points[0].x = CALC_X(-0.5, -0.5);
	points[0].y = CALC_Y(-0.5, -0.5);

	// - upper-right
	points[1].x = CALC_X(rp - 0.5, -0.5);
	points[1].y = CALC_Y(rp - 0.5, -0.5);

/*	// - bottom-right
	points[2].x = CALC_X(rp - 0.5, bp - 0.5);
	points[2].y = CALC_Y(rp - 0.5, bp - 0.5);
*/

	// - bottom-left
	points[2].x = CALC_X(-0.5, bp - 0.5);
	points[2].y = CALC_Y(-0.5, bp - 0.5);

	return AffineBlt(destrect, ref, refrect, points, method, opa, updaterect, hda, type, clear, clearcolor);
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::InternalDoBoxBlur(tTVPRect rect, tTVPRect area, bool hasalpha)
{
	BOUND_CHECK(false);

	if(area.right < area.left)
		std::swap(area.right, area.left);
	if(area.bottom < area.top)
		std::swap(area.bottom, area.top);

	if(area.left == 0 && area.right == 0 &&
		area.top == 0 && area.bottom == 0) return false; // no conversion occurs

	if(area.left > 0 || area.right < 0 || area.top > 0 || area.bottom < 0)
		TVPThrowExceptionMessage(TVPBoxBlurAreaMustContainCenterPixel);

	iTVPRenderMethod *method; int idL, idT, idR, idB;
	if (hasalpha) {
		static iTVPRenderMethod *_method = TVPGetRenderManager()->GetRenderMethod("BoxBlurAlpha");
		static int
			_idL = _method->EnumParameterID("area_left"),
			_idT = _method->EnumParameterID("area_top"),
			_idR = _method->EnumParameterID("area_right"),
			_idB = _method->EnumParameterID("area_bottom");
		idL = _idL, idT = _idT, idR = _idR, idB = _idB;
		method = _method;
	} else {
		static iTVPRenderMethod *_method = TVPGetRenderManager()->GetRenderMethod("BoxBlur");
		static int
			_idL = _method->EnumParameterID("area_left"),
			_idT = _method->EnumParameterID("area_top"),
			_idR = _method->EnumParameterID("area_right"),
			_idB = _method->EnumParameterID("area_bottom");
		idL = _idL, idT = _idT, idR = _idR, idB = _idB;
		method = _method;
	}
	method->SetParameterInt(idL, area.left);
	method->SetParameterInt(idT, area.top);
	method->SetParameterInt(idR, area.right);
	method->SetParameterInt(idB, area.bottom);

	iTVPTexture2D *reftex = GetTexture();
	tRenderTexRectArray::Element src_tex[] = {
		tRenderTexRectArray::Element(reftex, rect)
	};
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect), reftex,
		rect, tRenderTexRectArray(src_tex));
	return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::DoBoxBlur(const tTVPRect & rect, const tTVPRect & area)
{
	// Blur the bitmap with box-blur algorithm.
	// 'rect' is a rectangle to blur.
	// 'area' is an area which destination pixel refers.
	// right and bottom of 'area' *does contain* pixels in the boundary.
	// eg. area:(-1,-1,1,1)  : Blur is to be performed using average of 3x3
	//                          pixels around the destination pixel.
	//     area:(-10,0,10,0) : Blur is to be performed using average of 21x1
	//                          pixels around the destination pixel. This results
	//                          horizontal blur.

	return InternalDoBoxBlur(rect, area, false);
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::DoBoxBlurForAlpha(const tTVPRect & rect, const tTVPRect &area)
{
	return InternalDoBoxBlur(rect, area, true);
}
//---------------------------------------------------------------------------
void tTVPBaseBitmap::UDFlip(const tTVPRect &rect)
{
    iTVPBaseBitmap::UDFlip(rect);
}

void iTVPBaseBitmap::UDFlip(const tTVPRect &rect)
{
	// up-down flip for given rectangle

	if(rect.left < 0 || rect.top < 0 || rect.right > (tjs_int)GetWidth() ||
		rect.bottom > (tjs_int)GetHeight())
				TVPThrowExceptionMessage(TVPSrcRectOutOfBitmap);

	iTVPTexture2D * reftex = GetTexture();
	tRenderTexRectArray::Element src_tex[] = {
		tRenderTexRectArray::Element(reftex, tTVPRect(rect.left, rect.bottom, rect.right, rect.top))
	};
	static iTVPRenderMethod* method = GetRenderManager()->GetRenderMethod("Copy");
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
		reftex, rect, tRenderTexRectArray(src_tex));
}
//---------------------------------------------------------------------------
void tTVPBaseBitmap::LRFlip(const tTVPRect &rect)
{
    iTVPBaseBitmap::LRFlip(rect);
}

void iTVPBaseBitmap::LRFlip(const tTVPRect &rect)
{
	// left-right flip
	if(rect.left < 0 || rect.top < 0 || rect.right > (tjs_int)GetWidth() ||
		rect.bottom > (tjs_int)GetHeight())
				TVPThrowExceptionMessage(TVPSrcRectOutOfBitmap);

	iTVPTexture2D * reftex = GetTexture();
	tRenderTexRectArray::Element src_tex[] = {
		tRenderTexRectArray::Element(reftex, tTVPRect(rect.right, rect.top, rect.left, rect.bottom))
	};
	static iTVPRenderMethod* method = GetRenderManager()->GetRenderMethod("Copy");
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
		reftex, rect, tRenderTexRectArray(src_tex));
}
//---------------------------------------------------------------------------
void iTVPBaseBitmap::DoGrayScale(tTVPRect rect)
{
	if(!Is32BPP()) return;  // 8bpp is always grayscaled bitmap

	BOUND_CHECK(RET_VOID);
	static iTVPRenderMethod* method = TVPGetRenderManager()->GetRenderMethod("DoGrayScale");
	iTVPTexture2D *reftex = GetTexture();
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect), reftex,
		rect, tRenderTexRectArray());
}
//---------------------------------------------------------------------------
void iTVPBaseBitmap::AdjustGamma(tTVPRect rect, const tTVPGLGammaAdjustData & data)
{
	if(!Is32BPP()) TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

	BOUND_CHECK(RET_VOID);

	if(!memcmp(&data, &TVPIntactGammaAdjustData, sizeof(tTVPGLGammaAdjustData)))
		return;
	static iTVPRenderMethod *method = TVPGetRenderManager()->GetRenderMethod("AdjustGamma");
	static int data_id = method->EnumParameterID("gammaAdjustData");
	method->SetParameterPtr(data_id, &data);
	iTVPTexture2D *reftex = GetTexture();
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect), reftex,
		rect, tRenderTexRectArray());
}
//---------------------------------------------------------------------------
void iTVPBaseBitmap::AdjustGammaForAdditiveAlpha(tTVPRect rect, const tTVPGLGammaAdjustData & data)
{
	if(!Is32BPP()) TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

	BOUND_CHECK(RET_VOID);

	if(!memcmp(&data, &TVPIntactGammaAdjustData, sizeof(tTVPGLGammaAdjustData)))
		return;
	static iTVPRenderMethod *method = TVPGetRenderManager()->GetRenderMethod("AdjustGamma_a");
	static int data_id = method->EnumParameterID("gammaAdjustData");
	method->SetParameterPtr(data_id, &data);
	iTVPTexture2D *reftex = GetTexture();
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect), reftex,
		rect, tRenderTexRectArray());
}
//---------------------------------------------------------------------------
void iTVPBaseBitmap::ConvertAddAlphaToAlpha()
{
	// convert additive alpha representation to alpha representation

	if(!Is32BPP()) TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

	tjs_int w = GetWidth();
	tjs_int h = GetHeight();
	static iTVPRenderMethod* method = TVPGetRenderManager()->GetRenderMethod("AdditiveAlphaToAlpha");
	tTVPRect rc(0, 0, w, h);
	iTVPTexture2D *reftex = GetTexture();
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rc), reftex,
		rc, tRenderTexRectArray());
}
//---------------------------------------------------------------------------
void iTVPBaseBitmap::ConvertAlphaToAddAlpha()
{
	// convert additive alpha representation to alpha representation

	if(!Is32BPP()) TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

	tjs_int w = GetWidth();
	tjs_int h = GetHeight();
	static iTVPRenderMethod* method = TVPGetRenderManager()->GetRenderMethod("AlphaToAdditiveAlpha");
	tTVPRect rc(0, 0, w, h);
	iTVPTexture2D *reftex = GetTexture();
	TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rc), reftex,
		rc, tRenderTexRectArray());
}
//---------------------------------------------------------------------------

tTVPBaseBitmap::tTVPBaseBitmap(tjs_uint w, tjs_uint h, tjs_uint bpp /*= 32*/) {
	if (!w) w = 1; if (!h) h = 1;
	Bitmap = TVPGetRenderManager()->CreateTexture2D(nullptr, 0, w, h, bpp == 8 ? TVPTextureFormat::Gray : TVPTextureFormat::RGBA);
}

bool tTVPBaseBitmap::AssignBitmap(tTVPBitmap *bmp) {
	return AssignTexture(TVPGetRenderManager()->CreateTexture2D(bmp));
}

iTVPRenderManager* tTVPBaseBitmap::GetRenderManager() {
	return TVPGetRenderManager();
}

//---------------------------------------------------------------------------
tTVPBaseTexture::tTVPBaseTexture(tjs_uint w, tjs_uint h, tjs_uint bpp /*= 32*/) {
	if (!w) w = 1; if (!h) h = 1;
	Bitmap = TVPGetRenderManager()->CreateTexture2D(nullptr, 0, w, h, bpp == 8 ? TVPTextureFormat::Gray : TVPTextureFormat::RGBA);
}

bool tTVPBaseTexture::AssignBitmap(tTVPBitmap *bmp) {
	assert(false);
	return false;
}

iTVPRenderManager* tTVPBaseTexture::GetRenderManager() {
	return TVPGetRenderManager();
}

void tTVPBaseTexture::Update(const void *pixel, unsigned int pitch, int x, int y, int w, int h)
{
	GetTextureForRender(false, nullptr)->Update(pixel, TVPTextureFormat::RGBA, pitch, tTVPRect(x, y, x + w, y + h));
}
