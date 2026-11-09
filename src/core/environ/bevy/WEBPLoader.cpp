// Replaces visual/LoadWEBP.cpp: `krkr_shim` decodes the bitstream, this only
// asks for the header and moves rows into the bitmap the engine allocated.
#include "tjsCommHead.h"

#include <cstring>
#include <vector>

#include "GraphicsLoaderIntf.h"
#include "MsgIntf.h"
#include "tvpgl.h"
#include "tjsDictionary.h"

#include "krkr_shim.h"

namespace {

std::vector<unsigned char> ReadAll(tTJSBinaryStream *src) {
	const tjs_uint size = src->GetSize();
	std::vector<unsigned char> data(size);
	if (size > 0) src->ReadBuffer(data.data(), size);
	return data;
}

} // namespace

void TVPLoadWEBP(void *formatdata, void *callbackdata, tTVPGraphicSizeCallback sizecallback,
	tTVPGraphicScanLineCallback scanlinecallback, tTVPMetaInfoPushCallback metainfopushcallback,
	tTJSBinaryStream *src, tjs_int keyidx, tTVPGraphicLoadMode mode) {
	if (mode == glmPalettized) {
		TVPThrowExceptionMessage(TJS_W("WebP does not support palettized image"));
	}

	std::vector<unsigned char> data = ReadAll(src);
	KrkrWebpInfo info;
	if (krkr_webp_info(data.data(), data.size(), &info) != 0) {
		TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
	}
	if (info.width == 0 || info.height == 0) {
		TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
	}

	const bool grayscale = mode == glmGrayscale;
	const tTVPGraphicPixelFormat format =
		grayscale ? gpfLuminance : (info.has_alpha ? gpfRGBA : gpfRGB);
	const int pitch = sizecallback(callbackdata, info.width, info.height, format);
	// Decoding goes into a buffer of its own: the bitmap's pitch is only what the
	// size callback reported, and a destination narrower than 4 bytes per pixel
	// needs a row copy anyway.
	const size_t rowBytes = grayscale ? info.width : (size_t)info.width * 4;
	if (pitch < 0 || (size_t)pitch < rowBytes) {
		TVPThrowExceptionMessage(TJS_W("WebP image does not fit the destination bitmap"));
	}

	std::vector<tjs_uint32> pixels((size_t)info.width * info.height);
	const size_t bytes = (size_t)info.width * info.height * sizeof(tjs_uint32);
	if (krkr_webp_decode(data.data(), data.size(), pixels.data(), bytes) != bytes) {
		TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
	}

	for (unsigned int y = 0; y < info.height; y++) {
		void *row = scanlinecallback(callbackdata, y);
		if (!row) break;
		const tjs_uint32 *source = pixels.data() + (size_t)y * info.width;
		if (grayscale) {
			TVPBLConvert32BitTo8Bit((tjs_uint8 *)row, source, info.width);
		} else {
			memcpy(row, source, rowBytes);
		}
		// Every row, before the next one is asked for: this is what binds a
		// loaded line (tiling, color key) to the bitmap.
		scanlinecallback(callbackdata, -1);
	}
}

void TVPLoadHeaderWEBP(void *formatdata, tTJSBinaryStream *src, iTJSDispatch2 **dic) {
	std::vector<unsigned char> data = ReadAll(src);
	KrkrWebpInfo info;
	if (krkr_webp_info(data.data(), data.size(), &info) != 0) {
		TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
	}

	*dic = TJSCreateDictionaryObject();
	tTJSVariant val((tjs_int32)info.width);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), 0, &val, (*dic));
	val = tTJSVariant((tjs_int32)info.height);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), 0, &val, (*dic));
	val = tTJSVariant((tjs_int32)(info.has_alpha ? 32 : 24));
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("bpp"), 0, &val, (*dic));
}
