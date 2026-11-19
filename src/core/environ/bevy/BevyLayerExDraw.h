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

struct TVPBevyDrawPoint { double x,y; };

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
    double tile[4];
    double center[2];
    double focus[2];
    uint32_t brush_point_count;
    uint32_t surround_count;
    const TVPBevyDrawPoint *brush_points;
    const uint32_t *surround_colors;
    uint32_t blend_count;
    uint32_t brush_flags;
    const double *blend_positions;
    const double *blend_factors;
    uint32_t compound_count;
    uint32_t reserved2;
    const double *compound;
    double start_arrow[4];
    double end_arrow[4];
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

struct TVPBevyDrawRecordInfo {
    uint32_t width,height,background_argb,operation_count;
    double bounds[4],dpi[2];
};

extern "C" int krkr_layer_ex_record_create(uint32_t width,uint32_t height,uint32_t background,uint64_t *record);
extern "C" int krkr_layer_ex_record_capture(uint64_t source,const double *dpi,uint64_t *record);
extern "C" int krkr_layer_ex_record_append(uint64_t record,const TVPBevyLayerExDraw *operation,uint32_t smoothing);
extern "C" int krkr_layer_ex_record_append_image(uint64_t record,uint64_t image,const double *source_rect,const double *transform);
extern "C" int krkr_layer_ex_record_decode(const void *bytes,size_t length,uint64_t *record);
extern "C" int krkr_layer_ex_record_encode(uint64_t record,void **bytes,size_t *length);
extern "C" int krkr_layer_ex_record_replay(uint64_t record,uint64_t target,const int32_t *clip,const double *view,uint32_t alpha_mode);
extern "C" int krkr_layer_ex_record_draw(uint64_t record,uint64_t target,const int32_t *clip,const double *view,uint32_t alpha_mode,const double *source_rect);
extern "C" int krkr_layer_ex_record_clone(uint64_t record,uint64_t *copy);
extern "C" int krkr_layer_ex_record_query(uint64_t record,TVPBevyDrawRecordInfo *info);
extern "C" void krkr_layer_ex_record_release(uint64_t record);
extern "C" void krkr_layer_ex_record_free(void *bytes,size_t length);

static_assert(sizeof(TVPBevyDrawPathElement)==56);
static_assert(sizeof(TVPBevyDrawColorStop)==16);
static_assert(sizeof(TVPBevyDrawPaint)==392);
static_assert(offsetof(TVPBevyDrawPaint,tile)==200);
static_assert(offsetof(TVPBevyDrawPaint,start_arrow)==328);
static_assert(sizeof(TVPBevyLayerExDraw)==144);
static_assert(sizeof(TVPBevyDrawRecordInfo)==64);
