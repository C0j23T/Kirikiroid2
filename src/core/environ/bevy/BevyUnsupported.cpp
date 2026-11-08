// bevy port: subsystems that bevy_krkr deliberately does not take from the
// Kirikiroid2 core. Each entry point fails with a clear message (or is a
// harmless no-op) instead of being silently missing.
//
//  - Window / input / menus / file dialogs: owned by the Bevy host. Scripts
//    render into BitmapLayerTreeOwner layers; the host reads their pixels.
//  - Sound output: the engine's buffers are mixed in BevySound.cpp and pulled
//    by the host's device (WaveMixer.cpp/SDL is not built). Only the
//    DirectSound entry points are stubbed here.
//  - Video playback (ffmpeg/movie): not built.
//  - BPG / JXR / PVRv3 images: their libraries are not vendored in this
//    checkout. PNG, JPEG, WEBP, TLG, BMP and XP3/TAR/7z/ZIP work.
#include "tjsCommHead.h"

#include <functional>

#include "DebugIntf.h"
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


void TVPShowPopMenu(tTJSNI_MenuItem *) {}

bool TVPGetKeyMouseAsyncState(tjs_uint, bool) { return false; }
bool TVPGetJoyPadAsyncState(tjs_uint, bool) { return false; }

bool TVPSelectFile(iTJSDispatch2 *) { return false; } // "cancelled"

/// The "browse the patch library" button of the dialog the engine shows when
/// `patch.tjs` fails (environ/cocos2d/AppDelegate.cpp opens a page in a
/// browser). A host has no browser to open, so the URL goes to the log.
void TVPOpenPatchLibUrl() {
    TVPAddImportantLog(ttstr(TJS_W("(info) patch library: ")) +
                       ttstr(TJS_W("https://zeas2.github.io/Kirikiroid2_patch/patch")));
}

// ─── sound output ───────────────────────────────────────────────────────────

// The device itself is the host's: `TVPCreateSoundBuffer` is in BevySound.cpp,
// which mixes the engine's buffers for the host to pull.
void TVPInitDirectSound(int) {}
void TVPUninitDirectSound() {}

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
// Not a stub-with-message: TVPInternalLoadTexture (GraphicsLoaderIntf.cpp) calls
// this for *every* image to try the PVRv3 fast path before falling back to the
// bitmap loaders, and expects NULL for "this file is not PVRv3". Throwing here
// made every PNG/TLG/WEBP image fail to load.
iTVPTexture2D *TVPLoadPVRv3(tTJSBinaryStream *, const std::function<void(const ttstr &, const tTJSVariant &)> &) {
    return nullptr;
}

void TVPSaveAsJXR(void *, tTJSBinaryStream *, const iTVPBaseBitmap *, const ttstr &, iTJSDispatch2 *) {
    TVPBevyUnsupported(TJS_W("Saving JPEG XR images"));
}
bool TVPAcceptSaveAsJXR(void *, const ttstr &, iTJSDispatch2 **) { return false; }
