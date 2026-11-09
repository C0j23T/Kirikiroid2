// bevy port: the engine's sound output.
//
// Kirikiri's `WaveSoundBuffer` (sound/win32/WaveImpl.cpp) decodes with the
// engine's own decoders (RIFF-WAV, Vorbis, Opus) and pushes PCM into an
// `iTVPSoundBuffer` -- on Windows a DirectSound buffer. There is no mixer in
// this build (WaveMixer.cpp is not compiled), so the port implements the buffer
// and mixes the pushed PCM itself; the host pulls the mix with
// `TVPBevyMixAudio` and hands it to the sound device.
//
// That is what makes a game's own `[playbgm]` / `[playse]` audible: KAG's
// scripts build their audio on `WaveSoundBuffer`, so without this the engine
// throws "not supported by the bevy_krkr port" and the game plays silently.
//
// Threading: the engine creates, drives and releases buffers on the main
// thread, while the host mixes on the audio thread, so the registry and every
// buffer's state are behind mutexes. The mixer takes a snapshot of the buffer
// list and never calls into the engine.
#include "tjsCommHead.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "MsgIntf.h"
#include "WaveIntf.h"
#include "win32/WaveMixer.h"

namespace {

/// How much decoded audio the port keeps queued per buffer. The engine fills
/// its own L1 queue (4 access units, ~115 ms) and stops when
/// `GetRemainBuffers()` says the port still has work, so this only has to be
/// larger than that; a second leaves the audio thread plenty of slack without
/// delaying `[stopbgm]` by a noticeable amount.
constexpr size_t kMaxQueuedSeconds = 1;
constexpr double kPi = 3.14159265358979323846;

class BevySoundBuffer;
/// Removes a buffer from the registry; the object itself lives on until the
/// mixer's snapshot lets go of it.
void DropBuffer(BevySoundBuffer *buffer);

/// One `iTVPSoundBuffer`: a queue of decoded PCM plus the playback state the
/// engine steers. No output device here -- `MixInto` reads the queue.
class BevySoundBuffer : public iTVPSoundBuffer {
    /// Format of the queued PCM, as the engine handed it over.
    tTVPWaveFormat Format;

    mutable std::mutex CS;
    std::vector<tjs_uint8> Queue; // PCM in `Format`, `ReadPos` = first unread byte
    size_t ReadPos = 0;
    /// Bytes of one access unit, learned from the first AppendBuffer: the
    /// engine feeds one at a time and asks how many are still queued.
    size_t UnitBytes = 0;
    /// Frames handed to the mixer since the last Reset, which the engine reads
    /// as the playback position.
    tjs_uint PlayedFrames = 0;
    bool Playing = false;
    bool Dead = false;
    float Volume = 1.0f;
    float Pan = 0.0f;

    size_t FrameBytes() const { return (size_t)Format.Channels * Format.BytesPerSample; }
    size_t QueuedBytes() const { return Queue.size() - ReadPos; }
    size_t QueuedFrames() const {
        const size_t frame = FrameBytes();
        return frame ? QueuedBytes() / frame : 0;
    }

    void AppendLocked(const tjs_uint8 *data, size_t len) {
        if (UnitBytes == 0) UnitBytes = len;
        if (ReadPos > 0 && ReadPos >= Queue.size() / 2) {
            // Reclaim the consumed prefix rather than growing forever.
            Queue.erase(Queue.begin(), Queue.begin() + (std::ptrdiff_t)ReadPos);
            ReadPos = 0;
        }
        const size_t frame = FrameBytes();
        const size_t cap = frame * (size_t)Format.SamplesPerSec * kMaxQueuedSeconds;
        Queue.insert(Queue.end(), data, data + len);
        if (cap && QueuedBytes() > cap) {
            // The host is not consuming (no device, or a stalled one): drop the
            // oldest audio instead of growing without bound.
            const size_t drop = QueuedBytes() - cap;
            ReadPos += drop - drop % frame; // keep frames aligned
        }
    }

    /// One source frame as (left, right).
    void SampleAt(size_t frame_index, float &l, float &r) const {
        const tjs_uint8 *base = Queue.data() + ReadPos;
        const size_t bps = Format.BytesPerSample;
        const auto read = [&](tjs_uint channel) -> float {
            const tjs_uint8 *p = base + (frame_index * Format.Channels + channel) * bps;
            if (Format.IsFloat && bps == 4) {
                float v;
                std::memcpy(&v, p, sizeof v);
                return v;
            }
            switch (bps) {
            case 1:
                // 8-bit PCM is unsigned.
                return ((int)p[0] - 128) / 128.0f;
            case 2: {
                int16_t v;
                std::memcpy(&v, p, sizeof v);
                return v / 32768.0f;
            }
            case 3: {
                int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 |
                                      (uint32_t)p[2] << 24) >> 8;
                return v / 8388608.0f;
            }
            case 4: {
                int32_t v;
                std::memcpy(&v, p, sizeof v);
                return (float)((double)v / 2147483648.0);
            }
            default:
                return 0.0f;
            }
        };
        l = read(0);
        r = Format.Channels > 1 ? read(1) : l;
    }

public:
    explicit BevySoundBuffer(const tTVPWaveFormat &format) : Format(format) {
        if (Format.Channels == 0) Format.Channels = 1;
        if (Format.BytesPerSample == 0) Format.BytesPerSample = 2;
    }

    //-- iTVPSoundBuffer -----------------------------------------------------

    void Release() override {
        {
            std::lock_guard<std::mutex> lock(CS);
            if (Dead) return; // released once already
            Dead = true;
            Playing = false;
        }
        DropBuffer(this);
    }

    void Play() override {
        std::lock_guard<std::mutex> lock(CS);
        Playing = true;
    }
    void Pause() override {
        std::lock_guard<std::mutex> lock(CS);
        Playing = false;
    }
    void Stop() override {
        std::lock_guard<std::mutex> lock(CS);
        Playing = false;
        Queue.clear();
        ReadPos = 0;
        PlayedFrames = 0;
    }
    void Reset() override {
        std::lock_guard<std::mutex> lock(CS);
        Queue.clear();
        ReadPos = 0;
        PlayedFrames = 0;
    }
    bool IsPlaying() override {
        std::lock_guard<std::mutex> lock(CS);
        return Playing;
    }
    void SetVolume(float v) override {
        std::lock_guard<std::mutex> lock(CS);
        Volume = std::clamp(v, 0.0f, 1.0f);
    }
    float GetVolume() override {
        std::lock_guard<std::mutex> lock(CS);
        return Volume;
    }
    void SetPan(float v) override {
        std::lock_guard<std::mutex> lock(CS);
        Pan = std::clamp(v, -1.0f, 1.0f);
    }
    float GetPan() override {
        std::lock_guard<std::mutex> lock(CS);
        return Pan;
    }
    void AppendBuffer(const void *buf, unsigned int len) override {
        if (!buf || !len) return;
        std::lock_guard<std::mutex> lock(CS);
        AppendLocked((const tjs_uint8 *)buf, len);
    }
    bool IsBufferValid() override { return true; }
    tjs_uint GetCurrentPlaySamples() override {
        std::lock_guard<std::mutex> lock(CS);
        return PlayedFrames;
    }
    // The port mixes from its own queue and knows nothing about the device's
    // backlog, which the engine only uses to guess at latency.
    tjs_uint GetLatencySamples() override { return 0; }
    int GetRemainBuffers() override {
        std::lock_guard<std::mutex> lock(CS);
        if (UnitBytes == 0) return 0;
        return (int)(QueuedBytes() / UnitBytes);
    }

    //-- the mixer ----------------------------------------------------------

    bool IsDead() {
        std::lock_guard<std::mutex> lock(CS);
        return Dead;
    }

    /// Adds this buffer to `out` (`frames` interleaved stereo float frames),
    /// resampling from its own rate to `out_rate`.
    void MixInto(float *out, unsigned int frames, double out_rate) {
        std::lock_guard<std::mutex> lock(CS);
        if (!Playing || Format.SamplesPerSec == 0 || out_rate <= 0.0) return;
        const size_t available = QueuedFrames();
        if (available == 0) return;

        const double step = (double)Format.SamplesPerSec / out_rate;
        // Equal power: a centred source keeps both channels at full level.
        const float angle = (float)((Pan + 1.0f) * 0.25f * kPi);
        const float left_gain = std::cos(angle) * Volume;
        const float right_gain = std::sin(angle) * Volume;

        double pos = Position;
        for (unsigned int i = 0; i < frames; i++) {
            const size_t index = (size_t)pos;
            if (index + 1 >= available) {
                // Ran dry mid-block: leave the rest silent and stop advancing.
                pos = (double)available;
                break;
            }
            const double frac = pos - (double)index;
            float l0, r0, l1, r1;
            SampleAt(index, l0, r0);
            SampleAt(index + 1, l1, r1);
            const float l = (float)(l0 + (l1 - l0) * frac) * left_gain;
            const float r = (float)(r0 + (r1 - r0) * frac) * right_gain;
            out[i * 2] += l;
            out[i * 2 + 1] += r;
            pos += step;
        }
        Position = pos;

        // Consume whole frames; keep the fractional part for the next block.
        const size_t consume = std::min((size_t)pos, available);
        if (consume) {
            ReadPos += consume * FrameBytes();
            PlayedFrames += (tjs_uint)consume;
            Position -= (double)consume;
        }
    }

private:
    /// Fractional read position, in frames.
    double Position = 0.0;
};

std::mutex SoundMutex;
std::vector<std::shared_ptr<BevySoundBuffer>> SoundBuffers;

void DropBuffer(BevySoundBuffer *buffer) {
    std::lock_guard<std::mutex> lock(SoundMutex);
    auto it = std::find_if(SoundBuffers.begin(), SoundBuffers.end(),
        [buffer](const std::shared_ptr<BevySoundBuffer> &entry) {
            return entry.get() == buffer;
        });
    if (it != SoundBuffers.end()) SoundBuffers.erase(it);
}

} // namespace

// The device is the host's, which pulls the mix with `TVPBevyMixAudio`, so
// there is no engine-side device to open or close.
void TVPInitDirectSound(int) {}
void TVPUninitDirectSound() {}

iTVPSoundBuffer *TVPCreateSoundBuffer(tTVPWaveFormat &fmt, int bufcount) {
    // The format is taken as the engine hands it over and mixed from there: the
    // engine converts the decoder's output to it, so leaving it alone keeps
    // `tTJSNI_WaveSoundBuffer`'s own sample bookkeeping consistent.
    if (bufcount <= 0) return nullptr;
    if (fmt.Channels == 0 || fmt.Channels > 8 || fmt.BytesPerSample == 0 ||
        fmt.SamplesPerSec == 0)
        return nullptr;
    auto buffer = std::make_shared<BevySoundBuffer>(fmt);
    {
        std::lock_guard<std::mutex> lock(SoundMutex);
        SoundBuffers.push_back(buffer);
    }
    // The engine owns the reference it is given; the registry keeps the object
    // alive while the audio thread may still be reading from it.
    return buffer.get();
}

void TVPBevyMixAudio(float *out, unsigned int frames, double out_rate) {
    if (!out || !frames) return;
    std::memset(out, 0, sizeof(float) * 2 * frames);

    // Copy the list so a buffer released on the main thread cannot vanish under
    // the mixer; `Release` deletes only after removing it from the registry,
    // and every entry here is still in it. Thread-local to keep the audio
    // thread from allocating on every callback.
    static thread_local std::vector<std::shared_ptr<BevySoundBuffer>> snapshot;
    {
        std::lock_guard<std::mutex> lock(SoundMutex);
        snapshot = SoundBuffers;
    }
    for (const auto &buffer : snapshot) {
        if (!buffer->IsDead()) buffer->MixInto(out, frames, out_rate);
    }
    snapshot.clear();
}

void TVPBevyResetSound() {
    std::lock_guard<std::mutex> lock(SoundMutex);
    SoundBuffers.clear();
}
