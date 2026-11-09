#include "tjsCommHead.h"

#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"
#include "MsgIntf.h"

#include "tjsDictionary.h"
#include "ScriptMgnIntf.h"

#include <cstring>
#include <vector>

#include <turbojpeg.h>

namespace {

std::vector<unsigned char> ReadAll(tTJSBinaryStream *src) {
	const tjs_uint size = src->GetSize();
	std::vector<unsigned char> data(size);
	if(size > 0) src->ReadBuffer(data.data(), size);
	return data;
}

// The error string belongs to the handle and is gone once it is destroyed.
void ThrowJpegError(const tjs_char *message, tjhandle handle) {
	ttstr reason( tjGetErrorStr2( handle ) );
	tjDestroy( handle );
	TVPThrowExceptionMessage( message, reason );
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
			TJS_W("\"quality\"=>(const)%[\"type\"=>\"range\",\"min\"=>1,\"max\"=>100,\"desc\"=>\"100 is high quality, 1 is low quality\",\"default\"=>90],")
			TJS_W("\"subsampling\"=>(const)%[\"type\"=>\"select\",\"items\"=>(const)[\"4:2:0\",\"4:2:2\",\"4:4:4\"],\"desc\"=>\"subsampling\",\"default\"=>0],")
			TJS_W("\"dct\"=>(const)%[\"type\"=>\"select\",\"items\"=>(const)[\"islow\",\"ifast\"],\"desc\"=>\"DCT method\",\"default\"=>0],")
			TJS_W("\"progressive\"=>(const)%[\"type\"=>\"boolean\",\"desc\"=>\"progressive encoding\",\"default\"=>true]")
			TJS_W("]"),
			NULL, &desc );
		if( desc.Type() == tvtObject ) {
			*dic = desc.AsObject();
		}
	}
	return result;
}

tTVPJPEGLoadPrecision TVPJPEGLoadPrecision = jlpMedium;

void TVPLoadJPEG(void* formatdata, void *callbackdata, tTVPGraphicSizeCallback sizecallback,
	tTVPGraphicScanLineCallback scanlinecallback, tTVPMetaInfoPushCallback metainfopushcallback,
	tTJSBinaryStream *src, tjs_int keyidx,  tTVPGraphicLoadMode mode)
{
	if(mode == glmPalettized)
		TVPThrowExceptionMessage(TVPJPEGLoadError,
			ttstr(TVPUnsupportedJpegPalette));

	std::vector<unsigned char> jpeg = ReadAll(src);

	int width, height, subsamp;
	tjhandle decompressor = tjInitDecompress();
	if( !decompressor ) TVPThrowExceptionMessage(TVPInsufficientMemory);
	if( tjDecompressHeader2( decompressor, jpeg.data(), jpeg.size(), &width, &height, &subsamp ) != 0 )
		ThrowJpegError(TVPJPEGLoadError, decompressor);

	const bool grayscale = mode == glmGrayscale;
	const int components = grayscale ? 1 : 4;
	sizecallback(callbackdata, width, height, gpfRGB);

	int flags = TJFLAG_FASTDCT;
	switch(TVPJPEGLoadPrecision)
	{
	case jlpLow:  flags = TJFLAG_FASTDCT|TJFLAG_FASTUPSAMPLE; break;
	case jlpHigh: flags = TJFLAG_ACCURATEDCT; break;
	}

	std::vector<unsigned char> pixels((size_t)width * height * components);
	if( tjDecompress2( decompressor, jpeg.data(), jpeg.size(), pixels.data(), width,
			width * components, height, grayscale ? TJPF_GRAY : TJPF_RGBA, flags ) != 0 )
		ThrowJpegError(TVPJPEGLoadError, decompressor);
	tjDestroy( decompressor );

	for( int y = 0; y < height; y++ ) {
		void *scanline = scanlinecallback(callbackdata, y);
		if(!scanline) break;
		memcpy( scanline, pixels.data() + (size_t)y * width * components, (size_t)width * components );
		scanlinecallback(callbackdata, -1);
	}
}

struct tTVPJPGOption
{
	tjs_uint quality;
	int subsampling;
	bool fast_dct;
	bool progressive;
};

void TVPSaveAsJPG(void* formatdata, tTJSBinaryStream* dst, const iTVPBaseBitmap* image, const ttstr & mode, iTJSDispatch2* meta )
{
	tTVPJPGOption opt = { 90, 0, false, true };
	if( mode.StartsWith(TJS_W("jpg")) && mode.length() > 3 ) {
		opt.quality = 0;
		for( tjs_int len = 3; len < mode.length(); len++ ) {
			tjs_char c = mode[len];
			if( c >= TJS_W('0') && c <= TJS_W('9') ) {
				opt.quality *= 10;
				opt.quality += c-TJS_W('0');
			}
		}
		if( opt.quality <= 0 ) opt.quality = 10;
		if( opt.quality > 100 ) opt.quality = 100;
	}

	{
		struct MetaDictionaryEnumCallback : public tTJSDispatch {
			tTVPJPGOption *opt_;
			MetaDictionaryEnumCallback( tTVPJPGOption *opt ) : opt_(opt) {}
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
					opt_->quality = (tjs_uint)v;
				} else if( value == TJS_W("subsampling") ) {
					tjs_int64 v = param[2]->AsInteger();
					v = v < 0 ? 0 : v > 2 ? 2 : v;
					opt_->subsampling = (int)v;
				} else if( value == TJS_W("dct") ) {
					opt_->fast_dct = param[2]->AsInteger() != 0;
				} else if( value == TJS_W("progressive") ) {
					opt_->progressive = param[2]->AsInteger() != 0;
				}
				if(result) *result = (tjs_int)1;
				return TJS_S_OK;
			}
		} callback( &opt );
		tTJSVariantClosure clo(&callback, NULL);
		meta->EnumMembers(TJS_IGNOREPROP, &clo, meta);
	}

	const tjs_uint width = image->GetWidth();
	const tjs_uint height = image->GetHeight();
	if( width == 0 || height == 0 ) TVPThrowInternalError;

	TVPClearGraphicCache();

	static const int subsampling[] = { TJSAMP_420, TJSAMP_422, TJSAMP_444 };
	int flags = opt.fast_dct ? TJFLAG_FASTDCT : TJFLAG_ACCURATEDCT;
	if( opt.progressive ) flags |= TJFLAG_PROGRESSIVE;

	unsigned char *jpeg = NULL;
	unsigned long jpegSize = 0;
	tjhandle compressor = tjInitCompress();
	if( !compressor ) TVPThrowExceptionMessage(TVPInsufficientMemory);
	if( tjCompress2( compressor, (const unsigned char*)image->GetScanLine(0), width,
			image->GetPitchBytes(), height, TJPF_RGBX, &jpeg, &jpegSize,
			subsampling[opt.subsampling], opt.quality, flags ) != 0 )
		ThrowJpegError(TVPCompressionFailed, compressor);
	tjDestroy( compressor );
	dst->WriteBuffer( jpeg, (tjs_uint)jpegSize );
	tjFree( jpeg );
}

void TVPLoadHeaderJPG(void* formatdata, tTJSBinaryStream *src, iTJSDispatch2** dic )
{
	std::vector<unsigned char> jpeg = ReadAll(src);

	int width, height, subsamp, colorspace;
	tjhandle decompressor = tjInitDecompress();
	if( !decompressor ) TVPThrowExceptionMessage(TVPInsufficientMemory);
	if( tjDecompressHeader3( decompressor, jpeg.data(), jpeg.size(), &width, &height, &subsamp, &colorspace ) != 0 )
		ThrowJpegError(TVPJPEGLoadError, decompressor);
	tjDestroy( decompressor );

	*dic = TJSCreateDictionaryObject();
	tTJSVariant val((tjs_int64)width);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), 0, &val, (*dic) );
	val = tTJSVariant((tjs_int64)height);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), 0, &val, (*dic));
	val = tTJSVariant((tjs_int64)24);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("bpp"), 0, &val, (*dic));
}
