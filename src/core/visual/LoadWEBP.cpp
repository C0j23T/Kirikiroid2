#include "tjsCommHead.h"
#include "GraphicsLoaderIntf.h"
#include "MsgIntf.h"
#include "tjsDictionary.h"
#include <memory>
#include <cstring>

#include "decode.h"
void TVPLoadWEBP(void* formatdata, void *callbackdata,
	tTVPGraphicSizeCallback sizecallback, tTVPGraphicScanLineCallback scanlinecallback,
	tTVPMetaInfoPushCallback metainfopushcallback, tTJSBinaryStream *src, tjs_int keyidx,
	tTVPGraphicLoadMode mode)
{
	WebPDecoderConfig config;
	if (WebPInitDecoderConfig(&config) == 0) {
		TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
	}

	int datasize = src->GetSize();
	std::unique_ptr<uint8_t[]> data(new uint8_t[datasize]);
	src->ReadBuffer(data.get(), datasize);
	if (WebPGetFeatures(data.get(), datasize, &config.input) != VP8_STATUS_OK) {
		TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
	}

	unsigned int stride = sizecallback(callbackdata, config.input.width, config.input.height,
		config.input.has_alpha ? gpfRGBA : gpfRGB);
#if 0
	WebPData webp_data = { data, datasize };
	WebPDemuxer* demux = WebPDemux(&webp_data);
	WebPChunkIterator chunk_iter;
	if (WebPDemuxGetChunk(demux, "USER", 1, &chunk_iter)) {
		chunk_iter.chunk.bytes;
		WebPDemuxReleaseChunkIterator(&chunk_iter);
	}

	WebPDemuxDelete(demux);
#endif
	uint8_t* scanline = (uint8_t*)scanlinecallback(callbackdata, 0);
	if (glmNormal == mode) {
		// bevy port: decode into a buffer of its own and copy the rows out. The
		// bitmap's pitch is only what the size callback reported, so writing RGBA
		// straight into it assumed a stride of width*4 that a non-32bpp destination
		// does not have; the decoder then ran past the end of the bitmap and
		// corrupted the heap (AddressSanitizer: WebPCopyPlane wrote past the
		// allocation, and the damage surfaced later as a crash somewhere else).
		const unsigned int width = config.input.width;
		const unsigned int height = config.input.height;
		if (height == 0 || stride < width * 4) {
			TVPThrowExceptionMessage(TJS_W("WebP image does not fit the destination bitmap"));
		}
		std::unique_ptr<uint8_t[]> rgba(new uint8_t[(size_t)width * height * 4]);
		config.output.colorspace = MODE_RGBA;
		config.output.u.RGBA.rgba = rgba.get();
		config.output.u.RGBA.stride = width * 4;
		config.output.u.RGBA.size = width * height * 4;
		config.output.is_external_memory = 1;
		if (WebPDecode(data.get(), datasize, &config) != VP8_STATUS_OK) {
			TVPThrowExceptionMessage(TJS_W("Invalid WebP image(RGBA mode)"));
		}
		for (unsigned int y = 0; y < height; y++) {
			void *row = scanlinecallback(callbackdata, y);
			if (!row) break;
			memcpy(row, rgba.get() + (size_t)y * width * 4, width * 4);
		}
	} else if (glmGrayscale == mode) {
		// bevy port: same treatment as the colour path above. This branch used
		// to hand the decoder the first scanline as the whole image: a mask
		// destination only has one line buffer, so the decoder wrote
		// height*stride bytes into it and ran past the end.
		const unsigned int width = config.input.width;
		const unsigned int height = config.input.height;
		if (height == 0 || stride < width) {
			TVPThrowExceptionMessage(TJS_W("WebP image does not fit the destination bitmap"));
		}
		const unsigned int uvSize = width * height / 4 + width + width;
		std::unique_ptr<uint8_t[]> luma(new uint8_t[(size_t)width * height]);
		std::unique_ptr<uint8_t[]> dummy(new uint8_t[uvSize]);
		config.output.colorspace = MODE_YUV;
		config.output.u.YUVA.y = luma.get();
		config.output.u.YUVA.u = dummy.get();
		config.output.u.YUVA.v = dummy.get();
		config.output.u.YUVA.a = nullptr;
		config.output.u.YUVA.y_stride = width;
		config.output.u.YUVA.u_stride = width / 2;
		config.output.u.YUVA.v_stride = width / 2;
		config.output.u.YUVA.a_stride = 0;
		config.output.u.YUVA.y_size = width * height;
		config.output.u.YUVA.u_size = uvSize;
		config.output.u.YUVA.v_size = uvSize;
		config.output.u.YUVA.a_size = 0;
		config.output.is_external_memory = 1;

		if (WebPDecode(data.get(), datasize, &config) != VP8_STATUS_OK) {
			TVPThrowExceptionMessage(TJS_W("Invalid WebP image(Grayscale Mode)"));
		}
		for (unsigned int y = 0; y < height; y++) {
			void *row = scanlinecallback(callbackdata, y);
			if (!row) break;
			memcpy(row, luma.get() + (size_t)y * width, width);
		}
	} else {
		TVPThrowExceptionMessage(TJS_W("WebP does not support palettized image"));
	}
	scanlinecallback(callbackdata, -1); // image was written
}

void TVPLoadHeaderWEBP(void* formatdata, tTJSBinaryStream *src, iTJSDispatch2** dic) {
	WebPDecoderConfig config;
	if (WebPInitDecoderConfig(&config) == 0) {
		TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
	}

	int datasize = src->GetSize();
	std::unique_ptr<uint8_t[]> data(new uint8_t[datasize]);
	src->ReadBuffer(data.get(), datasize);
	if (WebPGetFeatures(data.get(), datasize, &config.input) != VP8_STATUS_OK) {
		TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
	}

	*dic = TJSCreateDictionaryObject();
	tTJSVariant val((tjs_int32)config.input.width);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), 0, &val, (*dic));
	val = tTJSVariant((tjs_int32)config.input.height);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), 0, &val, (*dic));
	val = tTJSVariant(config.input.has_alpha ? 32 : 24);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("bpp"), 0, &val, (*dic));
}
