#include "tjsCommHead.h"

#include <algorithm>
#include <cstring>

#include "StorageIntf.h"
#include "VorbisWaveDecoder.h"
#include "WaveIntf.h"

#ifdef KRKR_BEVY_OPUS

#include "opusfile.h"

/// The largest Opus frame, 120 ms at 48 kHz. Decoding into a buffer of this
/// size means one op_read per frame, never a partial one.
static const int OpusMaxFrameSamples = 5760;

class OpusWaveDecoder : public tTVPWaveDecoder
{
	OggOpusFile *InputFile;
	tTJSBinaryStream *InputStream;
	tTVPWaveFormat Format;
	opus_int16 *Buffer, *pBuffer;
	int BufferRemain; // in samples

private:
	int static read_func(void *datasource, unsigned char *ptr, int size) {
		OpusWaveDecoder * decoder = (OpusWaveDecoder*)datasource;
		if(!decoder->InputStream) return -1;
		return decoder->InputStream->Read(ptr, size);
	}
	int static seek_func(void *datasource, opus_int64 offset, int whence) {
		OpusWaveDecoder * decoder = (OpusWaveDecoder*)datasource;
		if(!decoder->InputStream) return -1;

		int seek_type = TJS_BS_SEEK_SET;
		switch(whence) {
		case SEEK_SET: seek_type = TJS_BS_SEEK_SET; break;
		case SEEK_CUR: seek_type = TJS_BS_SEEK_CUR; break;
		case SEEK_END: seek_type = TJS_BS_SEEK_END; break;
		}
		decoder->InputStream->Seek(offset, seek_type);
		return 0;
	}
	int static close_func(void *datasource) {
		OpusWaveDecoder * decoder = (OpusWaveDecoder*)datasource;
		if(!decoder->InputStream) return EOF;
		delete decoder->InputStream;
		decoder->InputStream = NULL;
		return 0;
	}
	opus_int64 static tell_func(void *datasource) {
		OpusWaveDecoder * decoder = (OpusWaveDecoder*)datasource;
		if(!decoder->InputStream) return EOF;
		return decoder->InputStream->GetPosition();
	}

public:
	OpusWaveDecoder() : InputFile(nullptr), InputStream(nullptr), Buffer(nullptr), pBuffer(nullptr), BufferRemain(0) {}
	~OpusWaveDecoder() {
		if(InputFile) op_free(InputFile);
		delete InputStream;
		delete[] Buffer;
	}

	int BufferRead(opus_int16 *buf, int samples_to_read) {
		int res = std::min(BufferRemain, samples_to_read);
		memcpy(buf, pBuffer, res * Format.Channels * sizeof(opus_int16));
		pBuffer += res * Format.Channels;
		BufferRemain -= res;
		if(BufferRemain <= 0) pBuffer = nullptr;
		return res;
	}

public:
	virtual void GetFormat(tTVPWaveFormat & format) { format = Format; }

	virtual bool Render(void *_buf, tjs_uint bufsamplelen, tjs_uint& rendered) {
		if(!InputFile) return false;

		opus_int16 *buf = (opus_int16*)_buf;
		int res;
		int pos = 0;
		int remain = bufsamplelen;

		while(remain) {
			if(pBuffer) {
				res = BufferRead(buf, remain);
			} else if(remain >= OpusMaxFrameSamples) {
				res = op_read(InputFile, buf, remain * Format.Channels, nullptr);
			} else {
				res = op_read(InputFile, Buffer, OpusMaxFrameSamples * Format.Channels, nullptr);
				if(res > remain) {
					memcpy(buf, Buffer, remain * Format.Channels * sizeof(opus_int16));
					BufferRemain = res - remain;
					pBuffer = Buffer + remain * Format.Channels;
					res = remain;
				} else {
					memcpy(buf, Buffer, res * Format.Channels * sizeof(opus_int16));
				}
			}
			if(res <= 0) break;
			pos += res;
			remain -= res;
			buf += res * Format.Channels;
		}

		rendered = pos;
		return !remain;
	}

	virtual bool SetPosition(tjs_uint64 samplepos) {
		if(!InputFile) return false;
		return op_pcm_seek(InputFile, samplepos) == 0;
	}

	bool SetStream(const ttstr & url) {
		InputStream = TVPCreateStream(url, TJS_BS_READ);
		if(!InputStream) return false;

		OpusFileCallbacks callbacks = {read_func, seek_func, tell_func, close_func};
		int error;
		InputFile = op_open_callbacks(this, &callbacks, NULL, 0, &error);
		if(!InputFile) {
			delete InputStream;
			InputStream = NULL;
			return false;
		}

		Format.BitsPerSample = 16;
		Format.BytesPerSample = 2;
		Format.Channels = op_channel_count(InputFile, -1);
		Format.IsFloat = false;
		Format.SamplesPerSec = 48000;
		Format.Seekable = op_seekable(InputFile) ? 0 : 2;
		Format.SpeakerConfig = 0;

		opus_int64 pcmtotal = op_pcm_total(InputFile, -1);
		if(pcmtotal < 0) pcmtotal = 0;
		Format.TotalSamples = pcmtotal;
		Format.TotalTime = pcmtotal * (1000.0 / 48000);

		Buffer = new opus_int16[Format.Channels * OpusMaxFrameSamples];
		BufferRemain = 0;
		pBuffer = nullptr;

		return true;
	}
};

tTVPWaveDecoder * OpusWaveDecoderCreator::Create(const ttstr & storagename, const ttstr &) {
	OpusWaveDecoder * decoder = new OpusWaveDecoder();
	if(!decoder->SetStream(storagename)) {
		delete decoder;
		decoder = nullptr;
	}
	return decoder;
}

#else // !KRKR_BEVY_OPUS

tTVPWaveDecoder * OpusWaveDecoderCreator::Create(const ttstr &, const ttstr &) {
	return nullptr; // other decoders are tried
}

#endif
