#include "BevyVideo.h"
#include "BevyRender.h"
#include "LayerBitmapIntf.h"
#include "VideoOvlImpl.h"
#include "combase.h"
#include "krmovie.h"
#include "tjsCommHead.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    class Video;
    std::vector<Video *> Videos;

    void Dispatch(TVPBevyVideoCommand &command)
    {
        uint8_t error[512]{};
        command.error = error;
        command.error_capacity = sizeof(error);
        if (krkr_video_dispatch(&command))
            throw std::runtime_error(error[0] ? reinterpret_cast<const char *>(error)
                                              : "Video backend failed");
    }

    class Video final : public iTVPVideoOverlay
    {
    public:
        uint64_t ID = 0;
        tTJSNI_VideoOverlay *Owner;
        tTJSNI_Window *Window = nullptr;
        TVPBevyVideoState State{};
        std::unique_ptr<tTVPBaseTexture> Image;
        tTVPBaseTexture *Front = nullptr;
        int References = 1;
        int Rect[4]{};
        bool Visible = false;
        // Stop hides the presentation without changing the script's visibility.
        bool PresentationStopped = false;
#if 0 // Not implemented by KRMoviePlayer.
        bool AudioEnabled = true;
        float Alpha = 1;
        unsigned long BGColor = 0xff000000;
#endif
        double Rate = 1;
        long Volume = 100000, Balance = 0;
#if 0 // Not implemented by KRMoviePlayer.
        int StopFrame = -1;
        double Color[4] = {0, 1, 0, 1};
#endif

        void Send(uint32_t operation, double value = 0, uint64_t target = 0,
                  double second = 0)
        {
            TVPBevyVideoCommand command{};
            command.operation = operation;
            command.player = ID;
            command.values[0] = value;
            command.values[1] = second;
            command.target = target;
            command.state = &State;
            Dispatch(command);
        }
        Video(tTJSNI_VideoOverlay *owner, IStream *stream, uint64_t size)
            : Owner(owner)
        {
            if (size == 0 || size > std::numeric_limits<size_t>::max())
                throw std::runtime_error("Invalid video input size");
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            for (size_t offset = 0; offset < bytes.size();)
            {
                ULONG read = 0;
                ULONG amount =
                    static_cast<ULONG>(std::min<size_t>(bytes.size() - offset, 1 << 20));
                if (FAILED(stream->Read(bytes.data() + offset, amount, &read)) || !read)
                    throw std::runtime_error("Cannot read video input");
                offset += read;
            }
            TVPBevyVideoCommand command{};
            command.operation = TVP_BEVY_VIDEO_OPEN;
            command.data = bytes.data();
            command.length = bytes.size();
            command.state = &State;
            Dispatch(command);
            ID = command.player;
            try
            {
                Image = std::make_unique<tTVPBaseTexture>(State.width, State.height);
                Front = Image.get();
                Send(TVP_BEVY_VIDEO_TARGET, 0, TVPBevyTextureID(Image->GetTexture()));
                Rect[2] = State.width;
                Rect[3] = State.height;
                Videos.push_back(this);
            }
            catch (...)
            {
                TVPBevyVideoCommand close{};
                close.operation = TVP_BEVY_VIDEO_CLOSE;
                close.player = ID;
                krkr_video_dispatch(&close);
                throw;
            }
        }
        ~Video()
        {
            SetWindow(nullptr);
            Videos.erase(std::remove(Videos.begin(), Videos.end(), this), Videos.end());
            TVPBevyVideoCommand close{};
            close.operation = TVP_BEVY_VIDEO_CLOSE;
            close.player = ID;
            krkr_video_dispatch(&close);
        }
        void Poll()
        {
            Send(TVP_BEVY_VIDEO_QUERY, 1);
#if 0 // Not implemented by KRMoviePlayer.
            if (StopFrame >= 0 && State.frame >= static_cast<uint64_t>(StopFrame) &&
                State.status == 1)
            Send(TVP_BEVY_VIDEO_PAUSE);
#endif
            if (State.events & 1)
            {
                TVPBevyInvalidateTexture(Front->GetTexture());
                NativeEvent event(WM_GRAPHNOTIFY);
                event.WParam = EC_UPDATE;
                event.LParam = static_cast<intptr_t>(State.frame);
                Owner->PostEvent(event);
            }
            if (State.events & 2)
            {
                NativeEvent event(WM_GRAPHNOTIFY);
                event.WParam = EC_COMPLETE;
                Owner->PostEvent(event);
            }
        }
        void AddRef() override { ++References; }
        void Release() override
        {
            if (!--References)
                delete this;
        }
        void SetWindow(class tTJSNI_Window *window) override { Window = window; }
        void SetMessageDrainWindow(void *window) override {}
        void SetRect(int l, int t, int r, int b) override
        {
#if 0 // MoviePlayerOverlay reads the owner bounds instead.
            Rect[0] = l;
            Rect[1] = t;
            Rect[2] = r;
            Rect[3] = b;
#endif
        }
        void SetVisible(bool b) override { Visible = b; }
        void Play() override
        {
            Send(TVP_BEVY_VIDEO_PLAY);
            PresentationStopped = false;
        }
        void Stop() override
        {
            Send(TVP_BEVY_VIDEO_STOP);
            PresentationStopped = true;
        }
        void Pause() override { Send(TVP_BEVY_VIDEO_PAUSE); }
        void SetPosition(uint64_t tick) override
        {
            Send(TVP_BEVY_VIDEO_SEEK, tick / 1000.0);
        }
        void GetPosition(uint64_t *tick) override
        {
            Send(TVP_BEVY_VIDEO_QUERY);
            *tick = State.position * 1000;
        }
        void GetStatus(tTVPVideoStatus *status) override
        {
            Send(TVP_BEVY_VIDEO_QUERY);
            *status = static_cast<tTVPVideoStatus>(State.status);
        }
        void Rewind() override { Send(TVP_BEVY_VIDEO_SEEK); }
        void SetFrame(int f) override { Send(TVP_BEVY_VIDEO_SEEK, f / State.fps); }
        void GetFrame(int *f) override { *f = State.frame; }
        void GetFPS(double *f) override { *f = State.fps; }
        void GetNumberOfFrame(int *f) override { *f = State.duration * State.fps; }
        void GetTotalTime(int64_t *t) override { *t = State.duration * 1000; }
        void GetVideoSize(long *width, long *height) override
        {
            *width = State.width;
            *height = State.height;
        }
#if 0 // Layer buffers are not implemented by KRMoviePlayer.
        tTVPBaseTexture *GetFrontBuffer() override { return Front; }
        void SetVideoBuffer(tTVPBaseTexture *buff1, tTVPBaseTexture *buff2,
                            long size) override {
            Front = buff1;
            Send(TVP_BEVY_VIDEO_TARGET, 0,
                buff1 ? TVPBevyTextureID(buff1->GetTexture()) : 0);
        }
        void SetStopFrame(int frame) override { StopFrame = frame; }
        void GetStopFrame(int *frame) override { *frame = StopFrame; }
        void SetDefaultStopFrame() override { StopFrame = -1; }
#else
        tTVPBaseTexture *GetFrontBuffer() override { return nullptr; }
        void SetVideoBuffer(tTVPBaseTexture *, tTVPBaseTexture *, long) override {}
        void SetStopFrame(int) override {}
        void GetStopFrame(int *frame) override { *frame = -1; }
        void SetDefaultStopFrame() override {}
#endif
        void SetPlayRate(double rate) override
        {
            Send(TVP_BEVY_VIDEO_RATE, rate);
            Rate = rate;
        }
        void GetPlayRate(double *rate) override { *rate = Rate; }
        void SetAudioBalance(long balance) override
        {
            Balance = balance;
            Send(TVP_BEVY_VIDEO_VOLUME, Volume / 100000.0, 0,
                 Balance / 100000.0);
        }
        void GetAudioBalance(long *balance) override { *balance = Balance; }
        void SetAudioVolume(long volume) override
        {
            Volume = volume;
            Send(TVP_BEVY_VIDEO_VOLUME, Volume / 100000.0, 0,
                 Balance / 100000.0);
        }
        void GetAudioVolume(long *volume) override { *volume = Volume; }
        void GetNumberOfAudioStream(unsigned long *streamCount) override
        {
            *streamCount = State.audio_streams;
        }
        void SelectAudioStream(unsigned long num) override
        {
            Send(TVP_BEVY_VIDEO_AUDIO_STREAM, num);
        }
        void GetEnableAudioStreamNum(long *num) override { *num = State.audio_stream; }
        void DisableAudioStream(void) override
        {
#if 0 // KRMoviePlayer::DisableAudioStream is a TODO.
            AudioEnabled = false;
            Send(TVP_BEVY_VIDEO_VOLUME);
#endif
        }
        void GetNumberOfVideoStream(unsigned long *streamCount) override
        {
            *streamCount = State.video_streams;
        }
        void SelectVideoStream(unsigned long num) override
        {
            Send(TVP_BEVY_VIDEO_VIDEO_STREAM, num);
            Image->GetTexture()->SetSize(State.width, State.height);
            TVPBevyInvalidateTexture(Image->GetTexture());
            Send(TVP_BEVY_VIDEO_TARGET, 0, TVPBevyTextureID(Image->GetTexture()));
        }
        void GetEnableVideoStreamNum(long *num) override { *num = State.video_stream; }
        void SetMixingBitmap(class tTVPBaseTexture *dest, float alpha) override
        {
#if 0 // KRMoviePlayer silently ignores mixing bitmaps.
            throw std::runtime_error("Video mixing bitmap is unavailable");
#endif
        }
        void ResetMixingBitmap() override {}
#if 0 // Mixer properties are not implemented by KRMoviePlayer.
        void SetMixingMovieAlpha(float a) override {
            Alpha = std::clamp(a, 0.0f, 1.0f);
        }
        void GetMixingMovieAlpha(float *a) override { *a = Alpha; }
        void SetMixingMovieBGColor(unsigned long col) override { BGColor = col; }
        void GetMixingMovieBGColor(unsigned long *col) override { *col = BGColor; }
#else
        void SetMixingMovieAlpha(float) override {}
        void GetMixingMovieAlpha(float *a) override { *a = 1.0f; }
        void SetMixingMovieBGColor(unsigned long) override {}
        void GetMixingMovieBGColor(unsigned long *col) override { *col = 0xff000000; }
#endif
        void PresentVideoImage() override {}
#if 0 // Color controls are not implemented by KRMoviePlayer.
        void SetColor(unsigned index, float value) {
            TVPBevyVideoCommand command{};
            command.operation = TVP_BEVY_VIDEO_COLOR;
            command.player = ID;
            std::copy(Color, Color + 4, command.values);
            command.values[index] = value;
            Dispatch(command);
            std::copy(command.values, command.values + 4, Color);
        }
#define COLOR_CONTROL(name, index, minimum, maximum, initial, step)   \
        void Get##name##RangeMin(float *v) override { *v = minimum; }     \
        void Get##name##RangeMax(float *v) override { *v = maximum; }     \
        void Get##name##DefaultValue(float *v) override { *v = initial; } \
        void Get##name##StepSize(float *v) override { *v = step; }        \
        void Get##name(float *v) override { *v = Color[index]; }          \
        void Set##name(float v) override { SetColor(index, v); }
    COLOR_CONTROL(Brightness, 0, -1, 1, 0, 0.01f)
    COLOR_CONTROL(Contrast, 1, 0, 2, 1, 0.01f)
    COLOR_CONTROL(Hue, 2, -180, 180, 0, 1)
    COLOR_CONTROL(Saturation, 3, 0, 2, 1, 0.01f)
#undef COLOR_CONTROL
#else
#define COLOR_CONTROL(name, initial)                                  \
    void Get##name##RangeMin(float *v) override { *v = 0; }           \
    void Get##name##RangeMax(float *v) override { *v = 0; }           \
    void Get##name##DefaultValue(float *v) override { *v = initial; } \
    void Get##name##StepSize(float *v) override { *v = 0; }           \
    void Get##name(float *v) override { *v = initial; }               \
    void Set##name(float) override {}
        COLOR_CONTROL(Brightness, 0)
        COLOR_CONTROL(Contrast, 1)
        COLOR_CONTROL(Hue, 0)
        COLOR_CONTROL(Saturation, 1)
#undef COLOR_CONTROL
#endif
        void SetLoopSegement(int beginFrame, int endFrame) override
        {
            Send(TVP_BEVY_VIDEO_LOOP, beginFrame, 0, endFrame);
        }
    };

    void Open(tTJSNI_VideoOverlay *owner, IStream *stream, uint64_t size,
              iTVPVideoOverlay **out)
    {
        *out = new Video(owner, stream, size);
    }
} // namespace

#define VIDEO_FACTORY(name)                                                  \
    void name(tTJSNI_VideoOverlay *owner, IStream *stream, const tjs_char *, \
              const tjs_char *, uint64_t size, iTVPVideoOverlay **out)       \
    {                                                                        \
        Open(owner, stream, size, out);                                      \
    }
VIDEO_FACTORY(GetVideoOverlayObject)
VIDEO_FACTORY(GetVideoLayerObject)
VIDEO_FACTORY(GetMixingVideoOverlayObject)
VIDEO_FACTORY(GetMFVideoOverlayObject)

void TVPBevyTickVideo()
{
    TVPBevyVideoCommand tick{};
    tick.operation = TVP_BEVY_VIDEO_TICK;
    Dispatch(tick);
    for (auto video : Videos)
        video->Poll();
}

void TVPBevyResetVideo()
{
    TVPBevyVideoCommand command{};
    command.operation = TVP_BEVY_VIDEO_RESET;
    krkr_video_dispatch(&command);
}

uint64_t TVPBevyCompositeVideo(void *window, uint64_t target)
{
    for (auto video : Videos)
    {
        if (!video->Window || video->Window != window || !video->Visible ||
            video->PresentationStopped || !video->Image)
            continue;
        TVPBevyRenderCommand command{};
        command.operation = 5;
        command.target = target;
        command.sources[0] = TVPBevyTextureID(video->Image->GetTexture());
#if 0 // MoviePlayerOverlay uses the owner bounds and opaque video.
        std::copy(video->Rect, video->Rect + 4, command.rect);
        command.parameters[0] = video->Alpha * 255;
#else
        const auto &bounds = video->Owner->GetBounds();
        command.rect[0] = bounds.left;
        command.rect[1] = bounds.top;
        command.rect[2] = bounds.right;
        command.rect[3] = bounds.bottom;
        command.parameters[0] = 255;
#endif
        command.parameters[1] = 1;
        if (krkr_render_dispatch(&command))
            throw std::runtime_error("Video composition failed");
    }
    return target;
}
