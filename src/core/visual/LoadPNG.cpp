#include "tjsCommHead.h"

#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"
#include "MsgIntf.h"

#include "tjsDictionary.h"

#include "krkr_shim.h"

#include <cstring>
#include <vector>

namespace {

std::vector<unsigned char> ReadAll(tTJSBinaryStream *src) {
	const tjs_uint size = src->GetSize();
	std::vector<unsigned char> data(size);
	if(size > 0) src->ReadBuffer(data.data(), size);
	return data;
}

void ThrowPngError() {
	TVPThrowExceptionMessage(TVPPNGLoadError, ttstr(TJS_W("invalid PNG data")));
}

void PushMeta(void *callbackdata, tTVPMetaInfoPushCallback push, const KrkrPngInfo &info) {
	static const ttstr offs_x(TJS_W("offs_x")), offs_y(TJS_W("offs_y")), offs_unit(TJS_W("offs_unit"));
	static const ttstr reso_x(TJS_W("reso_x")), reso_y(TJS_W("reso_y")), reso_unit(TJS_W("reso_unit"));
	static const ttstr vpag_w(TJS_W("vpag_w")), vpag_h(TJS_W("vpag_h")), vpag_unit(TJS_W("vpag_unit"));
	static const ttstr pixel(TJS_W("pixel")), micrometer(TJS_W("micrometer"));
	static const ttstr meter(TJS_W("meter")), unknown(TJS_W("unknown"));

	if(!push) return;

	if(info.has_offset) {
		push(callbackdata, offs_x, ttstr((tjs_int)info.offset_x));
		push(callbackdata, offs_y, ttstr((tjs_int)info.offset_y));
		push(callbackdata, offs_unit,
			info.offset_unit == 0 ? pixel : info.offset_unit == 1 ? micrometer : unknown);
	}

	if(info.has_resolution) {
		push(callbackdata, reso_x, ttstr((tjs_int)info.resolution_x));
		push(callbackdata, reso_y, ttstr((tjs_int)info.resolution_y));
		push(callbackdata, reso_unit, info.resolution_unit == 1 ? meter : unknown);
	}

	// vpAg holds the virtual page size, as written by ImageMagick's -trim.
	if(info.has_vpage) {
		push(callbackdata, vpag_w, ttstr((tjs_int)info.vpage_width));
		push(callbackdata, vpag_h, ttstr((tjs_int)info.vpage_height));
		push(callbackdata, vpag_unit,
			info.vpage_unit == 0 ? pixel : info.vpage_unit == 1 ? micrometer : unknown);
	}
}

} // namespace

bool TVPAcceptSaveAsPNG(void* formatdata, const ttstr & type, class iTJSDispatch2** dic )
{
	bool result = type.StartsWith(TJS_W("png")) || type == TJS_W(".png");
	if( result && dic ) {
		// No save options.
		*dic = TJSCreateDictionaryObject();
	}
	return result;
}

void TVPLoadPNG(void* formatdata, void *callbackdata, tTVPGraphicSizeCallback sizecallback,
	tTVPGraphicScanLineCallback scanlinecallback, tTVPMetaInfoPushCallback metainfopushcallback,
	tTJSBinaryStream *src, tjs_int keyidx,  tTVPGraphicLoadMode mode)
{
	std::vector<unsigned char> png = ReadAll(src);

	KrkrPngInfo info;
	if( krkr_png_info(png.data(), png.size(), &info) != 0 )
		ThrowPngError();
	if( info.width >= 65536 || info.height >= 65536 )
		TVPThrowExceptionMessage(TVPPNGLoadError, ttstr(TVPTooLargeImage));
	if( mode == glmPalettized ) {
		if( info.bit_depth > 8 )
			TVPThrowExceptionMessage(TVPPNGLoadError, ttstr(TVPUnsupportedColorTypePalette));
		if( info.color_type != KRKR_PNG_COLOR_GRAY && info.color_type != KRKR_PNG_COLOR_PALETTE )
			TVPThrowExceptionMessage(TVPPNGLoadError, ttstr(TVPUnsupportedColorType));
	}

	PushMeta(callbackdata, metainfopushcallback, info);

	int target = KRKR_PNG_RGBA;
	tTVPGraphicPixelFormat format = info.has_alpha ? gpfRGBA : gpfRGB;
	if( mode == glmPalettized ) {
		target = KRKR_PNG_INDEX;
		format = gpfPalette;
	} else if( mode == glmGrayscale ) {
		target = KRKR_PNG_GRAY;
		format = gpfLuminance;
	}

	const size_t bytes_per_pixel = target == KRKR_PNG_RGBA ? 4 : 1;
	const size_t row_bytes = (size_t)info.width * bytes_per_pixel;
	const int pitch = sizecallback(callbackdata, info.width, info.height, format);
	if( pitch < 0 || (size_t)pitch < row_bytes )
		ThrowPngError();

	std::vector<unsigned char> pixels(row_bytes * info.height);
	if( krkr_png_decode(png.data(), png.size(), target, keyidx,
			pixels.data(), pixels.size()) != pixels.size() )
		ThrowPngError();

	for( unsigned y = 0; y < info.height; y++ ) {
		void *scanline = scanlinecallback(callbackdata, y);
		if(!scanline) break;
		memcpy(scanline, pixels.data() + (size_t)y * row_bytes, row_bytes);
		scanlinecallback(callbackdata, -1);
	}
}

void TVPSaveAsPNG(void* formatdata, tTJSBinaryStream* dst, const iTVPBaseBitmap* image, const ttstr & mode, iTJSDispatch2* meta )
{
	const tjs_uint width = image->GetWidth();
	const tjs_uint height = image->GetHeight();
	if( width == 0 || height == 0 ) TVPThrowInternalError;

	const bool with_alpha = mode != TJS_W("png24");

	TVPClearGraphicCache();

	const size_t row_bytes = (size_t)width * (with_alpha ? 4 : 3);
	std::vector<unsigned char> pixels(row_bytes * height);
	for( tjs_uint y = 0; y < height; y++ ) {
		const unsigned char *src = (const unsigned char*)image->GetScanLine(y);
		unsigned char *row = &pixels[(size_t)y * row_bytes];
		if( with_alpha ) {
			memcpy(row, src, row_bytes);
		} else {
			for( tjs_uint x = 0; x < width; x++ ) memcpy(row + x * 3, src + x * 4, 3);
		}
	}

	void *png = NULL;
	size_t png_size = 0;
	if( krkr_png_encode(pixels.data(), pixels.size(), width, height,
			with_alpha ? 32 : 24, &png, &png_size) != 0 )
		TVPThrowExceptionMessage(TVPPngSaveError);

	try {
		dst->WriteBuffer(png, (tjs_uint)png_size);
	} catch(...) {
		krkr_png_free(png, png_size);
		throw;
	}
	krkr_png_free(png, png_size);
}

void TVPLoadHeaderPNG(void* formatdata, tTJSBinaryStream *src, iTJSDispatch2** dic )
{
	std::vector<unsigned char> png = ReadAll(src);

	KrkrPngInfo info;
	if( krkr_png_info(png.data(), png.size(), &info) != 0 )
		ThrowPngError();

	*dic = TJSCreateDictionaryObject();
	tTJSVariant val((tjs_int64)info.width);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), 0, &val, (*dic) );
	val = tTJSVariant((tjs_int64)info.height);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), 0, &val, (*dic) );
	val = tTJSVariant((tjs_int)info.bpp);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("bpp"), 0, &val, (*dic) );

	if( info.has_offset ) {
		val = tTJSVariant((tjs_int)info.offset_x);
		(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("offset x"), 0, &val, (*dic) );
		val = tTJSVariant((tjs_int)info.offset_y);
		(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("offset y"), 0, &val, (*dic) );
		val = tTJSVariant( info.offset_unit == 0 ? TJS_W("pixel")
			: info.offset_unit == 1 ? TJS_W("micro meter") : TJS_W("unknown") );
		(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("offset unit"), 0, &val, (*dic) );
	}

	if( info.has_resolution ) {
		val = tTJSVariant((tjs_int64)info.resolution_x);
		(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("resolution x"), 0, &val, (*dic) );
		val = tTJSVariant((tjs_int64)info.resolution_y);
		(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("resolution y"), 0, &val, (*dic) );
		val = tTJSVariant( info.resolution_unit == 1 ? TJS_W("meter") : TJS_W("unknown") );
		(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("resolution unit"), 0, &val, (*dic) );
	}
}
