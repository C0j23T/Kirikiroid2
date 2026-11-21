// Adapted from krkrsdl3 plugins/shrinkCopy.cpp; see LICENSE-THIRD-PARTY.md.
#define NCB_MODULE_NAME TJS_W("shrinkCopy.dll")
#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "BevyRender.h"
#include <array>
#include <vector>
#include <memory>

namespace {
struct LayerPixels {
    tTJSNI_BaseLayer *layer;
    tTVPBaseTexture *image;
    int width, height, pitch;
    std::shared_ptr<iTVPTexture2D> texture;
};

tTJSNI_BaseLayer *LayerObject(iTJSDispatch2 *object) {
    tTJSNI_BaseLayer *layer = nullptr;
    if (!object || TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
            tTJSNC_Layer::ClassID, reinterpret_cast<iTJSNativeInstance **>(&layer))))
        return nullptr;
    return layer;
}

bool ReadPixels(iTJSDispatch2 *object, LayerPixels &pixels) {
    auto layer = LayerObject(object);
    if (!layer || !layer->GetMainImage()) return false;
    auto image = layer->GetMainImage();
    int width = static_cast<int>(image->GetWidth()), height = static_cast<int>(image->GetHeight());
    int pitch = image->GetPitchBytes();
    if (width <= 0 || height <= 0 || pitch == 0) return false;
    pixels = {layer, image, width, height, pitch,
              std::shared_ptr<iTVPTexture2D>(image->GetTexture(), [](iTVPTexture2D *texture) { texture->Release(); })};
    pixels.texture->AddRef();
    return true;
}

bool WritePixels(iTJSDispatch2 *object, LayerPixels &pixels) {
    auto layer = LayerObject(object);
    if (!layer || !layer->GetMainImage()) return false;
    layer->IndependMainImage();
    auto image = layer->GetMainImage();
    int width = static_cast<int>(image->GetWidth()), height = static_cast<int>(image->GetHeight());
    int pitch = image->GetPitchBytes();
    if (width <= 0 || height <= 0 || pitch == 0) return false;
    pixels = {layer, image, width, height, pitch,
              std::shared_ptr<iTVPTexture2D>(image->GetTexture(), [](iTVPTexture2D *texture) { texture->Release(); })};
    pixels.texture->AddRef();
    return true;
}

void Commit(const LayerPixels &pixels) {
    TVPBevyInvalidateTexture(pixels.image->GetTexture(), false);
    pixels.layer->SetImageModified(true);
    pixels.layer->Update();
}

struct ShrinkCopy {
    using real = tTVReal;

    static tjs_error TJS_INTF_METHOD copy(tTJSVariant *, tjs_int count, tTJSVariant **params,
                                          iTJSDispatch2 *dstObject) {
        if (count < 9) return TJS_E_BADPARAMCOUNT;
        ShrinkCopy inst(dstObject, params[0]->AsReal(), params[1]->AsReal(), params[2]->AsReal(),
                        params[3]->AsReal(), params[4]->AsObjectNoAddRef(),
                        (long)params[5]->AsInteger(), (long)params[6]->AsInteger(),
                        (long)params[7]->AsInteger(), (long)params[8]->AsInteger());
        if (!inst.check()) return TJS_E_INVALIDPARAM;
        if (!inst.clip()) return TJS_S_OK;
        inst.copyPixels();
        Commit(inst.dstPixels);
        return TJS_S_OK;
    }

    ShrinkCopy(iTJSDispatch2 *dst, real dx, real dy, real dw, real dh, iTJSDispatch2 *src,
               long sx, long sy, long sw, long sh)
        : dst(dx, dy, dw, dh), src(src), sx(sx), sy(sy), sw(sw), sh(sh) {
        this->dst.object = dst;
    }

    bool check() {
        if (sw <= 0 || sh <= 0 || dst.widthValue <= 0 || dst.heightValue <= 0 ||
            sw < (long)dst.widthValue || sh < (long)dst.heightValue)
            return false;
        return ReadPixels(src, srcPixels) && WritePixels(dst.object, dstPixels);
    }

    static long RtoL(real value) { return value < 0 ? -(long)(-value) : (long)value; }

    bool clip() {
        real zx = dst.widthValue / (real)sw;
        real zy = dst.heightValue / (real)sh;
        if (sx + sw <= 0 || sy + sh <= 0 || sx >= srcPixels.width || sy >= srcPixels.height)
            return false;
        if (sx < 0) {
            real cut = zx * (real)(-sx);
            sw += sx;
            dst.widthValue -= cut;
            dst.x += cut;
            sx = 0;
        }
        if (sy < 0) {
            real cut = zy * (real)(-sy);
            sh += sy;
            dst.heightValue -= cut;
            dst.y += cut;
            sy = 0;
        }
        long cut;
        if ((cut = sx + sw - srcPixels.width) > 0) {
            sw -= cut;
            dst.widthValue -= zx * (real)cut;
        }
        if ((cut = sy + sh - srcPixels.height) > 0) {
            sh -= cut;
            dst.heightValue -= zy * (real)cut;
        }

        dtx = RtoL(dst.x);
        dty = RtoL(dst.y);
        dtw = RtoL(dst.x + dst.widthValue) - dtx;
        dth = RtoL(dst.y + dst.heightValue) - dty;
        if (dst.x + dst.widthValue > (real)(dtx + dtw)) ++dtw;
        if (dst.y + dst.heightValue > (real)(dty + dth)) ++dth;
        if (dtx + dtw <= 0 || dty + dth <= 0 || dtx >= dstPixels.width || dty >= dstPixels.height)
            return false;

        dsx = dtx < 0 ? -dtx : 0;
        dsy = dty < 0 ? -dty : 0;
        dex = dtx + dtw > dstPixels.width ? dstPixels.width - dtx : dtw;
        dey = dty + dth > dstPixels.height ? dstPixels.height - dty : dth;
        return true;
    }

    using AvgT = unsigned long;
    struct AvgInfoT {
        int offset, step;
        AvgT ta, tc, ba, bc, total;
    };

    void copyPixels() {
        std::vector<AvgInfoT> tables((dex - dsx) + (dey - dsy));
        auto horz = tables.data();
        auto vert = horz + (dex - dsx);
        makeAvgTable(horz, true);
        makeAvgTable(vert, false);
        std::vector<std::array<float, 4>> records;
        records.reserve(tables.size() * 2);
        for (size_t i = 0; i < tables.size(); ++i) {
            const auto &entry = tables[i];
            const int stride = i < size_t(dex - dsx) ? 4 : srcPixels.pitch;
            records.push_back({float(entry.offset / stride), float(entry.step),
                               float(entry.ta), float(entry.tc)});
            records.push_back({float(entry.ba), float(entry.bc), float(entry.total), 0});
        }
        TVPBevyRenderCommand command{};
        command.operation = 4;
        command.method = "ShrinkCopy";
        command.target = TVPBevyTextureID(dstPixels.image->GetTexture());
        command.sources[0] = TVPBevyTextureID(srcPixels.texture.get());
        command.rect[0] = dtx + dsx; command.rect[1] = dty + dsy;
        command.rect[2] = dtx + dex; command.rect[3] = dty + dey;
        command.data = records.data();
        command.pitch = records.size();
        if (krkr_render_dispatch(&command))
            TVPThrowExceptionMessage(TJS_W("Shrink copy GPU operation failed"));
    }

    struct MakeAvgWorkT {
        AvgT unit;
        real max, ratio, diff;
        long stop, ofmul;
    };
    AvgT makeAvgTable(AvgInfoT *table, bool horizontal) {
        MakeAvgWorkT work;
        long pos, end;
        if (horizontal) {
            pos = dsx;
            end = dex - 1;
            work.max = (real)sw;
            work.ratio = work.max / dst.widthValue;
            work.diff = dst.x - (real)dtx;
            work.stop = sx;
            work.ofmul = 4;
        } else {
            pos = dsy;
            end = dey - 1;
            work.max = (real)sh;
            work.ratio = work.max / dst.heightValue;
            work.diff = dst.y - (real)dty;
            work.stop = sy;
            work.ofmul = srcPixels.pitch;
        }
        work.unit = 256;
        if (work.ratio <= 1.0 / 16) {
            work.unit /= (int)((2.0 / 16.0) / work.ratio);
            if (work.unit <= 0) work.unit = 1;
        }
        if (pos == end) {
            setAvgInfoEdge(work, table, pos);
        } else {
            setAvgInfoEdge(work, table++, pos++);
            while (pos < end) setAvgInfo(work, table++, pos++);
            setAvgInfoEdge(work, table++, pos++);
        }
        return work.unit;
    }

    static void setAvgInfo(MakeAvgWorkT const &work, AvgInfoT *table, long pos) {
        AvgT const unit = work.unit;
        real r1 = ((real)pos - work.diff) * (work.ratio);
        real r2 = r1 + work.ratio;
        long t1 = RtoL(r1), t2 = RtoL(r2);
        table->offset = (work.stop + t1 + 1) * work.ofmul;
        table->total = ((table->ta = table->tc = (unit - RtoL((r1 - (real)t1) * unit))) +
                        (table->ba = table->bc = (RtoL((r2 - (real)t2) * unit))) +
                        (table->step = t2 - t1 - 1) * unit);
    }

    static void setAvgInfoEdge(MakeAvgWorkT const &work, AvgInfoT *table, long pos) {
        AvgT const unit = work.unit;
        real r1 = ((real)pos - work.diff) * (work.ratio);
        real r2 = r1 + work.ratio;
        real f1 = r1 < 0 ? 0 : r1;
        real f2 = r2 > work.max ? work.max : r2;
        long t1 = RtoL(r1), t2 = RtoL(r2);
        long u1 = RtoL(f1), u2 = RtoL(f2);
        table->offset = (work.stop + u1 + 1) * work.ofmul;
        table->total = (table->tc = (unit - RtoL((r1 - (real)t1) * unit))) +
                       (table->bc = (RtoL((r2 - (real)t2) * unit))) + (t2 - t1 - 1) * unit;
        table->ta = (unit - RtoL((f1 - (real)u1) * unit));
        table->ba = (RtoL((f2 - (real)u2) * unit));
        table->step = u2 - u1 - 1;
    }

    struct Destination {
        iTJSDispatch2 *object;
        real x, y, widthValue, heightValue;
        Destination(real x, real y, real w, real h) : object(nullptr), x(x), y(y), widthValue(w), heightValue(h) {}
    };

    Destination dst;
    iTJSDispatch2 *src;
    long sx, sy, sw, sh;
    long dtx = 0, dty = 0, dtw = 0, dth = 0;
    long dsx = 0, dsy = 0, dex = 0, dey = 0;
    LayerPixels srcPixels{}, dstPixels{};
};

struct LimitedShrink {
    static tjs_error TJS_INTF_METHOD copy(tTJSVariant *, tjs_int count, tTJSVariant **params,
                                          iTJSDispatch2 *dstObject) {
        if (count < 2) return TJS_E_BADPARAMCOUNT;
        LimitedShrink inst(dstObject, params[0]->AsObjectNoAddRef(), (long)params[1]->AsInteger(),
                           count >= 3 ? (long)params[2]->AsInteger() : 0);
        if (!inst.check()) return TJS_E_INVALIDPARAM;
        if (!inst.resize()) return TJS_E_FAIL;
        inst.copyPixels();
        Commit(inst.dstPixels);
        return TJS_S_OK;
    }

    LimitedShrink(iTJSDispatch2 *dst, iTJSDispatch2 *src, long stepx, long stepy)
        : dstObject(dst), srcObject(src), stepx(stepx), stepy(stepy ? stepy : stepx) {}

    bool check() {
        return stepx > 0 && stepy > 0 && ReadPixels(srcObject, srcPixels);
    }

    bool resize() {
        auto layer = LayerObject(dstObject);
        if (!layer) return false;
        layer->IndependMainImage();
        layer->SetImageSize((srcPixels.width + stepx - 1) / stepx,
                            (srcPixels.height + stepy - 1) / stepy);
        return WritePixels(dstObject, dstPixels);
    }

    void copyPixels() {
        TVPBevyRenderCommand command{};
        command.operation = 4;
        command.method = "ShrinkCopyFast";
        command.target = TVPBevyTextureID(dstPixels.image->GetTexture());
        command.sources[0] = TVPBevyTextureID(srcPixels.texture.get());
        command.rect[2] = dstPixels.width; command.rect[3] = dstPixels.height;
        command.parameters[4] = stepx; command.parameters[5] = stepy;
        if (krkr_render_dispatch(&command))
            TVPThrowExceptionMessage(TJS_W("Fast shrink copy GPU operation failed"));
    }

    iTJSDispatch2 *dstObject, *srcObject;
    long stepx, stepy;
    LayerPixels srcPixels{}, dstPixels{};
};
}

NCB_ATTACH_FUNCTION(shrinkCopy, Layer, ShrinkCopy::copy);
NCB_ATTACH_FUNCTION(shrinkCopyFast, Layer, LimitedShrink::copy);
