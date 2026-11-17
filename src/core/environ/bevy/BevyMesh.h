#pragma once

#include <cstdint>
#include <cstddef>

struct TVPBevyMeshSurface
{
    float control_points[32];
    float models[32][16];
    float placement[4];
    float geometry[4];
    float offset[4];
    uint32_t kind, inherit_mask, reserved[2];
};

struct TVPBevyMeshCommand
{
    uint64_t target, source, mask;
    const TVPBevyMeshSurface* surfaces;
    uint32_t surface_count, division_x, division_y, blend;
    float opacity;
    float tint[4], color[4];
};

extern "C" int krkr_render_mesh(const TVPBevyMeshCommand* command);

static_assert(sizeof(TVPBevyMeshSurface) == 2240);
static_assert(offsetof(TVPBevyMeshSurface, models) == 128);
static_assert(offsetof(TVPBevyMeshSurface, placement) == 2176);
static_assert(offsetof(TVPBevyMeshSurface, geometry) == 2192);
static_assert(offsetof(TVPBevyMeshSurface, offset) == 2208);
static_assert(offsetof(TVPBevyMeshSurface, kind) == 2224);
static_assert(sizeof(TVPBevyMeshCommand) == 88);
static_assert(offsetof(TVPBevyMeshCommand, surfaces) == 24);
static_assert(offsetof(TVPBevyMeshCommand, surface_count) == 32);
static_assert(offsetof(TVPBevyMeshCommand, opacity) == 48);
static_assert(offsetof(TVPBevyMeshCommand, tint) == 52);
static_assert(offsetof(TVPBevyMeshCommand, color) == 68);
