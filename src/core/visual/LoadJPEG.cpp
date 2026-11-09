#include "tjsCommHead.h"

#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"
#include "MsgIntf.h"

#include "tjsDictionary.h"
#include "ScriptMgnIntf.h"

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

void ThrowJpegLoadError() {
	TVPThrowExceptionMessage(TVPJPEGLoadError, ttstr(TJS_W("invalid JPEG data")));
}

} // namespace

bool TVPAcceptSaveAsJPG(void* formatdata, const ttstr & type, class iTJSDispatch2** dic )
{
	bool result = false;
	if( type.StartsWith(TJS_W("jpg")) ) result = true;
	else if( type == TJS_W(".jpg") ) result = true;
	else if( type == TJS_W(".jpeg") ) result = true;
	else if( type == TJS_W(".jif") ) result = true;
	if( result && dic ) {
		tTJSVariant desc;
		TVPExecuteExpression(
			TJS_W("(const)%[")
			TJS_W("\"quality\"=>(const)%[\"type\"=>\"range\",\"min\"=>1,\"max\"=>100,\"desc\"=>\"100 is high quality, 1 is low quality\",\"default\"=>90]")
			TJS_W("]"),
			NULL, &desc );
		if( desc.Type() == tvtObject ) {
			*dic = desc.AsObject();
		}
	}
	return result;
}

void TVPLoadJPEG(void* formatdata, void *callbackdata, tTVPGraphicSizeCallback sizecallback,
	tTVPGraphicScanLineCallback scanlinecallback, tTVPMetaInfoPushCallback metainfopushcallback,
	tTJSBinaryStream *src, tjs_int keyidx,  tTVPGraphicLoadMode mode)
{
	if(mode == glmPalettized)
		TVPThrowExceptionMessage(TVPJPEGLoadError,
			ttstr(TVPUnsupportedJpegPalette));

	std::vector<unsigned char> jpeg = ReadAll(src);

	unsigned width, height;
	if( krkr_jpeg_info(jpeg.data(), jpeg.size(), &width, &height) != 0 )
		ThrowJpegLoadError();

	const bool grayscale = mode == glmGrayscale;
	const size_t row_bytes = (size_t)width * (grayscale ? 1 : 4);
	sizecallback(callbackdata, width, height, gpfRGB);

	std::vector<unsigned char> pixels(row_bytes * height);
	if( krkr_jpeg_decode(jpeg.data(), jpeg.size(), grayscale,
			pixels.data(), pixels.size()) != pixels.size() )
		ThrowJpegLoadError();

	for( unsigned y = 0; y < height; y++ ) {
		void *scanline = scanlinecallback(callbackdata, y);
		if(!scanline) break;
		memcpy(scanline, pixels.data() + (size_t)y * row_bytes, row_bytes);
		scanlinecallback(callbackdata, -1);
	}
}

void TVPSaveAsJPG(void* formatdata, tTJSBinaryStream* dst, const iTVPBaseBitmap* image, const ttstr & mode, iTJSDispatch2* meta )
{
	tjs_uint quality = 90;
	if( mode.StartsWith(TJS_W("jpg")) && mode.length() > 3 ) {
		quality = 0;
		for( tjs_int len = 3; len < mode.length(); len++ ) {
			tjs_char c = mode[len];
			if( c >= TJS_W('0') && c <= TJS_W('9') ) {
				quality *= 10;
				quality += c-TJS_W('0');
			}
		}
		if( quality <= 0 ) quality = 10;
		if( quality > 100 ) quality = 100;
	}

	{
		struct MetaDictionaryEnumCallback : public tTJSDispatch {
			tjs_uint *quality_;
			MetaDictionaryEnumCallback( tjs_uint *quality ) : quality_(quality) {}
			tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag, const tjs_char * membername,
				tjs_uint32 *hint, tTJSVariant *result, tjs_int numparams,
				tTJSVariant **param, iTJSDispatch2 *objthis) {
				if(numparams < 3) return TJS_E_BADPARAMCOUNT;
				tjs_uint32 flags = (tjs_int)*param[1];
				if(flags & TJS_HIDDENMEMBER) {
					if(result) *result = (tjs_int)1;
					return TJS_S_OK;
				}
				ttstr value = *param[0];
				if( value == TJS_W("quality") ) {
					tjs_int64 v = param[2]->AsInteger();
					v = v < 1 ? 1 : v > 100 ? 100 : v;
					*quality_ = (tjs_uint)v;
				}
				if(result) *result = (tjs_int)1;
				return TJS_S_OK;
			}
		} callback( &quality );
		tTJSVariantClosure clo(&callback, NULL);
		if(meta) meta->EnumMembers(TJS_IGNOREPROP, &clo, meta);
	}

	const tjs_uint width = image->GetWidth();
	const tjs_uint height = image->GetHeight();
	if( width == 0 || height == 0 ) TVPThrowInternalError;

	TVPClearGraphicCache();

	const size_t row_bytes = (size_t)width * 4;
	std::vector<unsigned char> pixels(row_bytes * height);
	for( tjs_uint y = 0; y < height; y++ )
		memcpy(&pixels[(size_t)y * row_bytes], image->GetScanLine(y), row_bytes);

	void *jpeg = NULL;
	size_t jpeg_size = 0;
	// TODO: the shim can also write 4:2:2 and 4:4:4, and the engine used to
	// let a script pick that and progressive coding; add the options back to
	// the descriptor above if a game saves screenshots that need them.
	if( krkr_jpeg_encode(pixels.data(), pixels.size(), width, height,
			(int)quality, KRKR_JPEG_420, &jpeg, &jpeg_size) != 0 )
		TVPThrowExceptionMessage(TVPCompressionFailed);

	try {
		dst->WriteBuffer(jpeg, (tjs_uint)jpeg_size);
	} catch(...) {
		krkr_jpeg_free(jpeg, jpeg_size);
		throw;
	}
	krkr_jpeg_free(jpeg, jpeg_size);
}

void TVPLoadHeaderJPG(void* formatdata, tTJSBinaryStream *src, iTJSDispatch2** dic )
{
	std::vector<unsigned char> jpeg = ReadAll(src);

	unsigned width, height;
	if( krkr_jpeg_info(jpeg.data(), jpeg.size(), &width, &height) != 0 )
		ThrowJpegLoadError();

	*dic = TJSCreateDictionaryObject();
	tTJSVariant val((tjs_int64)width);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), 0, &val, (*dic) );
	val = tTJSVariant((tjs_int64)height);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), 0, &val, (*dic));
	val = tTJSVariant((tjs_int64)24);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("bpp"), 0, &val, (*dic));
}
