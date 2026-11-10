#pragma once
#include <cstddef>
#include <cstdint>

struct TVPBevyVideoState
{
    uint32_t width, height;
    double duration, position, fps;
    uint32_t status, events;
    uint64_t frame;
    uint32_t audio_streams, video_streams;
    int32_t audio_stream, video_stream;
};

struct TVPBevyVideoCommand
{
    uint32_t operation;
    uint64_t player;
    const uint8_t *data;
    size_t length;
    uint64_t target;
    double values[4];
    TVPBevyVideoState *state;
    uint8_t *error;
    size_t error_capacity;
};

enum TVPBevyVideoOperation : uint32_t
{
    TVP_BEVY_VIDEO_OPEN,
    TVP_BEVY_VIDEO_PLAY,
    TVP_BEVY_VIDEO_PAUSE,
    TVP_BEVY_VIDEO_STOP,
    TVP_BEVY_VIDEO_SEEK,
    TVP_BEVY_VIDEO_CLOSE,
    TVP_BEVY_VIDEO_QUERY,
    TVP_BEVY_VIDEO_TARGET,
    TVP_BEVY_VIDEO_RATE,
    TVP_BEVY_VIDEO_VOLUME,
    TVP_BEVY_VIDEO_TICK,
    TVP_BEVY_VIDEO_RESET,
    TVP_BEVY_VIDEO_COLOR,
    TVP_BEVY_VIDEO_AUDIO_STREAM,
    TVP_BEVY_VIDEO_VIDEO_STREAM,
    TVP_BEVY_VIDEO_LOOP,
};

extern "C" int krkr_video_dispatch(TVPBevyVideoCommand *command);
void TVPBevyTickVideo();
void TVPBevyResetVideo();
uint64_t TVPBevyCompositeVideo(void *window, uint64_t target);
