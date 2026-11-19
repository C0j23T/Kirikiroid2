#pragma once
#include "RenderManager.h"
#include <array>

struct TVPBevyRenderCommand
{
    uint32_t operation, format;
    uint64_t target, sources[3];
    int32_t rect[4];
    float source_rects[3][4];
    float parameters[16];
    const char *method;
    void *data;
    int32_t pitch;
    uint32_t geometry;
    float points[4][8];
};

extern "C" int krkr_render_dispatch(TVPBevyRenderCommand *command);
uint64_t TVPBevyTextureID(iTVPTexture2D *texture);

void TVPBevyInvalidateTexture(iTVPTexture2D *texture, bool updateScene = true);

void *TVPBevyPluginImageBuffer(iTVPTexture2D *texture, bool write);

void TVPBevyCopyTextureChannels(iTVPTexture2D *target, iTVPTexture2D *source,
                               const std::array<unsigned, 4> &channels,
                               const tTVPRect &rect, const tTVPRect &sourceRect);
