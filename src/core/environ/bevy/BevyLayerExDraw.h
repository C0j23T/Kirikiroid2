#pragma once
#include <cstddef>
#include <cstdint>

struct TVPBevyDrawPathElement {
    uint32_t verb;
    uint32_t reserved;
    double points[6];
};

struct TVPBevyDrawColorStop {
    double position;
    uint32_t color;
    uint32_t reserved;
};

struct TVPBevyDrawPaint {
    uint32_t brush_type;
    uint32_t stroke;
    uint32_t colors[2];
    uint32_t wrap;
    uint32_t hatch;
    uint64_t image;
    double coordinates[4];
    double paint_transform[6];
    double offset[2];
    double width;
    double miter_limit;
    double dash_offset;
    uint32_t start_cap;
    uint32_t end_cap;
    uint32_t dash_cap;
    uint32_t join;
    uint32_t alignment;
    uint32_t stop_count;
    const TVPBevyDrawColorStop *stops;
    uint32_t dash_count;
    uint32_t reserved;
    const double *dashes;
};

struct TVPBevyLayerExDraw {
    uint64_t target;
    int32_t clip[4];
    double transform[6];
    const TVPBevyDrawPathElement *path;
    uint32_t path_count;
    uint32_t fill_rule;
    const TVPBevyDrawPaint *paints;
    uint32_t paint_count;
    uint32_t alpha_mode;
    uint32_t flags;
    uint32_t reserved;
    double bounds[4];
};

// All pointers remain readable until return; the host copies complete operations.
extern "C" int krkr_layer_ex_draw(TVPBevyLayerExDraw *operation);

static_assert(sizeof(TVPBevyDrawPathElement)==56);
static_assert(sizeof(TVPBevyDrawColorStop)==16);
static_assert(sizeof(TVPBevyDrawPaint)==200);
static_assert(sizeof(TVPBevyLayerExDraw)==144);
