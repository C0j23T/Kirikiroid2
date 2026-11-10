#define NCB_MODULE_NAME TJS_W("layerExMovie.dll")
#include "ncbind/ncbind.hpp"
#include "BevyVideo.h"
#include "BevyRender.h"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "EventIntf.h"
#include "StorageIntf.h"
#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

class BevyLayerExMovie : public tTVPContinuousEventCallbackIntf {
    struct Playback {
        uint64_t id = 0;
        TVPBevyVideoState state{};
        std::unique_ptr<tTVPBaseTexture> image;
        bool alpha = false, playing = false, loop = false, ended = false;
        ~Playback() {
            if (id) {
                TVPBevyVideoCommand command{};
                command.operation = TVP_BEVY_VIDEO_CLOSE;
                command.player = id;
                krkr_video_dispatch(&command);
            }
        }
        void send(uint32_t operation, double value = 0, uint64_t target = 0) {
            TVPBevyVideoCommand command{};
            command.operation = operation; command.player = id;
            command.values[0] = value; command.target = target; command.state = &state;
            dispatch(command);
        }
        static void dispatch(TVPBevyVideoCommand &command) {
            uint8_t error[512]{};
            command.error = error; command.error_capacity = sizeof(error);
            if (krkr_video_dispatch(&command))
                TVPThrowExceptionMessage(ttstr(reinterpret_cast<const char *>(error)).c_str());
        }
    };
    iTJSDispatch2 *Owner;
    std::shared_ptr<Playback> Movie;

    static tTJSNI_BaseLayer *layer(iTJSDispatch2 *object) {
        tTJSNI_BaseLayer *result = nullptr;
        if (TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                tTJSNC_Layer::ClassID, reinterpret_cast<iTJSNativeInstance **>(&result))))
            TVPThrowExceptionMessage(TJS_W("Layer instance is required"));
        return result;
    }
    static void callback(iTJSDispatch2 *owner, const tjs_char *name) {
        owner->FuncCall(0, name, nullptr, nullptr, 0, nullptr, owner);
    }
    void detach() {
        TVPRemoveContinuousEventHook(this);
        Movie.reset();
    }
public:
    explicit BevyLayerExMovie(iTJSDispatch2 *owner) : Owner(owner) {}
    ~BevyLayerExMovie() { detach(); }

    void openMovie(const tjs_char *filename, bool alpha) {
        detach();
        std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(filename, TJS_BS_READ));
        const auto size = stream->GetSize();
        if (!size || size > static_cast<uint64_t>(std::numeric_limits<tjs_uint>::max()))
            TVPThrowExceptionMessage(TJS_W("Invalid Layer movie size"));
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        stream->ReadBuffer(bytes.data(), static_cast<tjs_uint>(size));
        auto movie = std::make_shared<Playback>();
        TVPBevyVideoCommand command{};
        command.operation = TVP_BEVY_VIDEO_OPEN;
        command.data = bytes.data(); command.length = bytes.size(); command.state = &movie->state;
        Playback::dispatch(command);
        movie->id = command.player;
        movie->alpha = alpha;
        if (alpha && (movie->state.width < 2 || movie->state.width % 2))
            TVPThrowExceptionMessage(TJS_W("Alpha Layer movie requires an even video width"));
        movie->image = std::make_unique<tTVPBaseTexture>(movie->state.width, movie->state.height);
        movie->send(TVP_BEVY_VIDEO_TARGET, 0, TVPBevyTextureID(movie->image->GetTexture()));
        auto dest = layer(Owner);
        int width = movie->state.width / (alpha ? 2 : 1);
        dest->SetImageSize(width, movie->state.height);
        dest->SetSize(width, movie->state.height);
        dest->SetType(alpha ? ltAlpha : ltOpaque);
        Movie = std::move(movie);
    }
    void startMovie(bool loop) {
        if (!Movie) TVPThrowExceptionMessage(TJS_W("No Layer movie is open"));
        Movie->loop = loop; Movie->ended = false;
        Movie->send(TVP_BEVY_VIDEO_PLAY);
        Movie->playing = true;
        TVPRemoveContinuousEventHook(this);
        TVPAddContinuousEventHook(this);
        callback(Owner, TJS_W("onStartMovie"));
    }
    void stopMovie() {
        bool notify = isPlayingMovie();
        auto owner = Owner;
        detach();
        if (notify) callback(owner, TJS_W("onStopMovie"));
    }
    bool isPlayingMovie() { return Movie && Movie->playing; }

    void TJS_INTF_METHOD OnContinuousCallback(tjs_uint64) override {
        auto movie = Movie;
        if (!movie || !movie->playing) return;
        if (movie->ended) {
            if (movie->loop) {
                movie->send(TVP_BEVY_VIDEO_SEEK);
                movie->send(TVP_BEVY_VIDEO_PLAY);
                movie->ended = false;
            } else {
                stopMovie();
                return;
            }
        }
        movie->send(TVP_BEVY_VIDEO_QUERY, 1);
        movie->ended = (movie->state.events & 2) != 0;
        if (!(movie->state.events & 1)) return;
        auto dest = layer(Owner);
        dest->IndependMainImage();
        auto target = dest->GetMainImage()->GetTexture();
        auto source = movie->image->GetTexture();
        TVPBevyInvalidateTexture(source);
        auto rect = dest->GetClip();
        rect.right = std::min(rect.right, static_cast<int>(movie->state.width / (movie->alpha ? 2 : 1)));
        rect.bottom = std::min(rect.bottom, static_cast<int>(movie->state.height));
        TVPBevyRenderCommand command{};
        command.operation = 4; command.target = TVPBevyTextureID(target);
        command.sources[0] = TVPBevyTextureID(source);
        command.rect[0] = rect.left; command.rect[1] = rect.top;
        command.rect[2] = rect.right; command.rect[3] = rect.bottom;
        for (int i = 0; i < 4; ++i) command.source_rects[0][i] = command.rect[i];
        command.method = movie->alpha ? "LayerMovieAlpha" : "Copy";
        command.parameters[4] = movie->state.width / 2;
        if (krkr_render_dispatch(&command))
            TVPThrowExceptionMessage(TJS_W("Layer movie GPU copy failed"));
        TVPBevyInvalidateTexture(target);
        dest->SetImageModified(true);
        rect.add_offsets(dest->GetImageLeft(), dest->GetImageTop());
        dest->Update(rect);
        // The callback may invalidate this native instance.
        callback(Owner, TJS_W("onUpdateMovie"));
    }
};

NCB_GET_INSTANCE_HOOK(BevyLayerExMovie) {
    NCB_INSTANCE_GETTER(objthis) {
        auto movie = GetNativeInstance(objthis);
        if (!movie) { movie = new ClassT(objthis); SetNativeInstance(objthis, movie); }
        return movie;
    }
};
NCB_ATTACH_CLASS_WITH_HOOK(BevyLayerExMovie, Layer) {
    NCB_METHOD(openMovie);
    NCB_METHOD(startMovie);
    NCB_METHOD(stopMovie);
    NCB_METHOD(isPlayingMovie);
}
