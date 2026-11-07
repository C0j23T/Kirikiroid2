// bevy port: subsystems that bevy_krkr deliberately does not take from the
// Kirikiroid2 core. Each entry point fails with a clear message (or is a
// harmless no-op) instead of being silently missing.
//
//  - Window / input / menus / file dialogs: owned by the Bevy host. Scripts
//    render into BitmapLayerTreeOwner layers; the host reads their pixels.
//  - Sound output: bevy_krkr plays audio through bevy_kira_audio. There is no
//    second mixer in the engine core (WaveMixer.cpp/SDL is not built).
//  - Video playback (ffmpeg/movie): not built.
//  - BPG / JXR / PVRv3 images, ZIP / 7z archives: their libraries are not
//    vendored in this checkout. PNG, JPEG, WEBP, TLG, BMP and XP3/TAR work.
#include "tjsCommHead.h"

#include <functional>

#include "GraphicsLoaderIntf.h"
#include "MsgIntf.h"
#include "PhaseVocoderDSP.h"
#include "StorageIntf.h"
#include "WaveIntf.h"
#include "FFWaveDecoder.h"
#include "win32/WaveMixer.h"

class tTJSNI_Window;
class tTJSNI_MenuItem;
class tTJSNI_VideoOverlay;
class iTVPVideoOverlay;
class iWindowLayer;
struct IStream;

[[noreturn]] static void TVPBevyUnsupported(const tjs_char *what) {
    TVPThrowExceptionMessage(TJS_W("%1 is not supported by the bevy_krkr port"), ttstr(what));
    throw; // unreachable: TVPThrowExceptionMessage always throws
}

// ─── window / input / UI ────────────────────────────────────────────────────

iWindowLayer *TVPCreateAndAddWindow(tTJSNI_Window *) {
    TVPBevyUnsupported(TJS_W("Window (render into BitmapLayerTreeOwner instead)"));
}

void TVPShowPopMenu(tTJSNI_MenuItem *) {}

bool TVPGetKeyMouseAsyncState(tjs_uint, bool) { return false; }
bool TVPGetJoyPadAsyncState(tjs_uint, bool) { return false; }

bool TVPSelectFile(iTJSDispatch2 *) { return false; } // "cancelled"

// ─── sound output ───────────────────────────────────────────────────────────

void TVPInitDirectSound(int) {}
void TVPUninitDirectSound() {}

iTVPSoundBuffer *TVPCreateSoundBuffer(tTVPWaveFormat &, int) {
    TVPBevyUnsupported(TJS_W("WaveSoundBuffer playback (use bevy_krkr's audio channels)"));
}

tTVPWaveDecoder *FFWaveDecoderCreator::Create(const ttstr &, const ttstr &) {
    return nullptr; // ffmpeg decoder not built; other decoders are tried
}

// PhaseVocoderDSP.cpp needs xmmlib.h (SSE helpers), which is not part of the
// Kirikiroid2 repository. Creating a WaveEffect.PhaseVocoder fails here.
tRisaPhaseVocoderDSP::tRisaPhaseVocoderDSP(unsigned int, unsigned int, unsigned int)
    : InputBuffer(0), OutputBuffer(0) {
    TVPBevyUnsupported(TJS_W("WaveEffect.PhaseVocoder"));
}
tRisaPhaseVocoderDSP::~tRisaPhaseVocoderDSP() {}
void tRisaPhaseVocoderDSP::SetTimeScale(float) {}
void tRisaPhaseVocoderDSP::SetFrequencyScale(float) {}
void tRisaPhaseVocoderDSP::SetOverSampling(unsigned int) {}
size_t tRisaPhaseVocoderDSP::GetInputFreeSize() { return 0; }
bool tRisaPhaseVocoderDSP::GetInputBuffer(size_t, float *&, size_t &, float *&, size_t &) { return false; }
size_t tRisaPhaseVocoderDSP::GetOutputReadySize() { return 0; }
bool tRisaPhaseVocoderDSP::GetOutputBuffer(size_t, const float *&, size_t &, const float *&, size_t &) { return false; }
tRisaPhaseVocoderDSP::tStatus tRisaPhaseVocoderDSP::Process() { return psNoError; }

// ─── video ──────────────────────────────────────────────────────────────────

#define TVP_BEVY_NO_VIDEO(fn)                                                                  \
    void fn(tTJSNI_VideoOverlay *, IStream *, const tjs_char *, const tjs_char *, uint64_t,     \
            iTVPVideoOverlay **) {                                                             \
        TVPBevyUnsupported(TJS_W("VideoOverlay"));                                             \
    }
TVP_BEVY_NO_VIDEO(GetVideoOverlayObject)
TVP_BEVY_NO_VIDEO(GetVideoLayerObject)
TVP_BEVY_NO_VIDEO(GetMixingVideoOverlayObject)
TVP_BEVY_NO_VIDEO(GetMFVideoOverlayObject)

// ─── image formats without a vendored library ───────────────────────────────

#define TVP_BEVY_NO_IMAGE(fmt, name)                                                           \
    void TVPLoad##fmt(void *, void *, tTVPGraphicSizeCallback, tTVPGraphicScanLineCallback,     \
                      tTVPMetaInfoPushCallback, tTJSBinaryStream *, tjs_int, tTVPGraphicLoadMode) { \
        TVPBevyUnsupported(TJS_W(name " images"));                                             \
    }                                                                                          \
    void TVPLoadHeader##fmt(void *, tTJSBinaryStream *, iTJSDispatch2 **) {                     \
        TVPBevyUnsupported(TJS_W(name " images"));                                             \
    }
TVP_BEVY_NO_IMAGE(BPG, "BPG")
TVP_BEVY_NO_IMAGE(JXR, "JPEG XR")
TVP_BEVY_NO_IMAGE(PVRv3, "PVRv3")

class iTVPTexture2D;
iTVPTexture2D *TVPLoadPVRv3(tTJSBinaryStream *, const std::function<void(const ttstr &, const tTJSVariant &)> &) {
    TVPBevyUnsupported(TJS_W("PVRv3 images"));
}

void TVPSaveAsJXR(void *, tTJSBinaryStream *, const iTVPBaseBitmap *, const ttstr &, iTJSDispatch2 *) {
    TVPBevyUnsupported(TJS_W("Saving JPEG XR images"));
}
bool TVPAcceptSaveAsJXR(void *, const ttstr &, iTJSDispatch2 **) { return false; }

// ─── archive formats without a vendored library ─────────────────────────────
// StorageImpl tries every opener in turn; nullptr means "not this format".

tTVPArchive *TVPOpenZIPArchive(const ttstr &, tTJSBinaryStream *, bool) { return nullptr; }
tTVPArchive *TVPOpen7ZArchive(const ttstr &, tTJSBinaryStream *, bool) { return nullptr; }
