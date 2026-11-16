// Adapted from krkrsdl3 plugins/shrinkCopy.cpp; see LICENSE-THIRD-PARTY.md.
#define NCB_MODULE_NAME TJS_W("shrinkCopy.dll")
#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include <cstdlib>

namespace {
struct LayerPixels {
    tTJSNI_BaseLayer *layer;
    tTVPBaseTexture *image;
    int width, height, pitch;
    unsigned char *buffer;
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
              const_cast<unsigned char *>(static_cast<const unsigned char *>(image->GetScanLine(0)))};
    return true;
}

bool WritePixels(iTJSDispatch2 *object, LayerPixels &pixels) {
    auto layer = LayerObject(object);
    if (!layer || !layer->GetMainImage()) return false;
    auto image = layer->GetMainImage();
    int width = static_cast<int>(image->GetWidth()), height = static_cast<int>(image->GetHeight());
    int pitch = image->GetPitchBytes();
    if (width <= 0 || height <= 0 || pitch == 0) return false;
    pixels = {layer, image, width, height, pitch,
              static_cast<unsigned char *>(image->GetScanLineForWrite(0))};
    return true;
}

void Commit(const LayerPixels &pixels) {
    pixels.image->Update(pixels.buffer, pixels.pitch, 0, 0, pixels.width, pixels.height);
    pixels.layer->SetImageModified(true);
    pixels.layer->Update();
}

struct ShrinkCopy {
    using real = tTVReal;
    using PixelT = unsigned int;
    using BufRefT = const unsigned char *;
    using WrtRefT = unsigned char *;

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
    struct ElementT {
        AvgT r, g, b, a;
    };

    void copyPixels() {
        AvgInfoT *horz = nullptr, *vert = nullptr;
        void *buf = allocAvgBuffer(horz, vert, dex - dsx, dey - dsy);
        if (!buf) return;
        AvgT hu = makeAvgTable(horz, true);
        AvgT vu = makeAvgTable(vert, false);
        AvgT unit = hu * vu;
        int spch = srcPixels.pitch, dpch = dstPixels.pitch;
        WrtRefT pl = dstPixels.buffer + ((dtx + dsx) << 2) + ((dty + dsy) * dpch);
        AvgInfoT const *vi = vert;
        for (long y = dsy; y < dey; ++y, ++vi, pl += dpch) {
            BufRefT rl = srcPixels.buffer + vi->offset;
            AvgInfoT const *hi = horz;
            long x = dsx;
            for (WrtRefT p = pl; x < dex; ++x, ++hi, p += 4) {
                ElementT sum = {0, 0, 0, 0};
                BufRefT r = rl + hi->offset;
                const int w = hi->step;
                const int h = vi->step;
                const int ox = w << 2;
                const int oy = h * spch;
                addPoint(sum, r - 4 - spch, hi->ta * vi->ta, hi->tc * vi->tc);
                addHorz(sum, r - spch, w, hu * vi->ta, hu * vi->tc);
                addPoint(sum, r + ox - spch, hi->ba * vi->ta, hi->bc * vi->tc);
                addVert(sum, r - 4, h, hi->ta * vu, hi->tc * vu);
                addRect(sum, r, w, h, unit);
                addVert(sum, r + ox, h, hi->ba * vu, hi->bc * vu);
                addPoint(sum, r - 4 + oy, hi->ta * vi->ba, hi->tc * vi->bc);
                addHorz(sum, r + oy, w, hu * vi->ba, hu * vi->bc);
                addPoint(sum, r + ox + oy, hi->ba * vi->ba, hi->bc * vi->bc);
                AvgT div = hi->total * vi->total;
                p[0] = (unsigned char)(sum.r / div);
                p[1] = (unsigned char)(sum.g / div);
                p[2] = (unsigned char)(sum.b / div);
                p[3] = (unsigned char)(sum.a / div);
            }
        }
        std::free(buf);
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

    static void *allocAvgBuffer(AvgInfoT *&horizontal, AvgInfoT *&vertical, long w, long h) {
        void *buffer = std::malloc(sizeof(AvgInfoT) * w * h);
        horizontal = static_cast<AvgInfoT *>(buffer);
        vertical = horizontal + w;
        return buffer;
    }

    void addRect(ElementT &sum, BufRefT p, int w, int h, AvgT mul) const {
        if (!mul) return;
        for (; h > 0; --h, p += srcPixels.pitch) {
            auto r = reinterpret_cast<const PixelT *>(p);
            for (int n = w; n > 0; --n, ++r) {
                PixelT col = *r;
                sum.r += (col & 0xff) * mul;
                sum.g += ((col >> 8) & 0xff) * mul;
                sum.b += ((col >> 16) & 0xff) * mul;
                sum.a += (col >> 24) * mul;
            }
        }
    }
    void addVert(ElementT &sum, BufRefT p, int h, AvgT alphaMul, AvgT colorMul) const {
        if (!alphaMul) return;
        for (; h > 0; --h, p += srcPixels.pitch) {
            PixelT col = *reinterpret_cast<const PixelT *>(p);
            sum.r += (col & 0xff) * colorMul;
            sum.g += ((col >> 8) & 0xff) * colorMul;
            sum.b += ((col >> 16) & 0xff) * colorMul;
            sum.a += (col >> 24) * alphaMul;
        }
    }
    static void addHorz(ElementT &sum, BufRefT p, int w, AvgT alphaMul, AvgT colorMul) {
        if (!alphaMul) return;
        auto r = reinterpret_cast<const PixelT *>(p);
        for (; w > 0; --w, ++r) {
            PixelT col = *r;
            sum.r += (col & 0xff) * colorMul;
            sum.g += ((col >> 8) & 0xff) * colorMul;
            sum.b += ((col >> 16) & 0xff) * colorMul;
            sum.a += (col >> 24) * alphaMul;
        }
    }
    static void addPoint(ElementT &sum, BufRefT p, AvgT alphaMul, AvgT colorMul) {
        if (!alphaMul) return;
        PixelT col = *reinterpret_cast<const PixelT *>(p);
        sum.r += (col & 0xff) * colorMul;
        sum.g += ((col >> 8) & 0xff) * colorMul;
        sum.b += ((col >> 16) & 0xff) * colorMul;
        sum.a += (col >> 24) * alphaMul;
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
        layer->SetImageSize((srcPixels.width + stepx - 1) / stepx,
                            (srcPixels.height + stepy - 1) / stepy);
        return WritePixels(dstObject, dstPixels);
    }

    void copyPixels() {
        using PixelT = unsigned int;
        long xdiv = srcPixels.width / stepx;
        long xrem = srcPixels.width - xdiv * stepx;
        auto shrinkLine = [](unsigned char *w, const unsigned char *r, long len, long shrink,
                             long step, long tstep) {
            if (shrink == 1) {
                for (; len > 0; --len, r += step) {
                    w[0] = r[0];
                    w[1] = r[1];
                    w[2] = r[2];
                    w[3] = 255;
                    w += 4;
                }
                return;
            }
            for (; len > 0; --len, r += step) {
                unsigned int sr = 0, sg = 0, sb = 0;
                const unsigned char *tr = r;
                for (long sub = shrink; sub > 0; --sub, tr += tstep) {
                    sr += tr[0];
                    sg += tr[1];
                    sb += tr[2];
                }
                w[0] = (unsigned char)(sr / shrink);
                w[1] = (unsigned char)(sg / shrink);
                w[2] = (unsigned char)(sb / shrink);
                w[3] = 255;
                w += 4;
            }
        };
        auto shrinkLineX = [&](unsigned char *w, const unsigned char *r) {
            shrinkLine(w, r, xdiv, stepx, stepx * 4, 4);
            if (xrem > 0) shrinkLine(w, r, 1, xrem, 0, 4);
        };

        auto ps = srcPixels.buffer;
        auto pd = dstPixels.buffer;
        if (stepy <= 1) {
            for (long y = 0; y < srcPixels.height; ++y, pd += dstPixels.pitch, ps += srcPixels.pitch)
                shrinkLineX(pd, ps);
            return;
        }

        long bpch = dstPixels.width * 4;
        auto buf = static_cast<unsigned char *>(std::malloc(bpch * stepy));
        if (!buf) return;
        long div = srcPixels.height / stepy;
        for (long len = div; len > 0; --len, pd += dstPixels.pitch) {
            for (long sub = stepy, ofs = 0; sub > 0; --sub, ofs += bpch, ps += srcPixels.pitch)
                shrinkLineX(buf + ofs, ps);
            shrinkLine(pd, buf, dstPixels.width, stepy, 4, bpch);
        }
        div *= stepy;
        if (div < srcPixels.height) {
            long yrem = srcPixels.height - div;
            for (long sub = yrem, ofs = 0; sub > 0; --sub, ofs += bpch, ps += srcPixels.pitch)
                shrinkLineX(buf + ofs, ps);
            shrinkLine(pd, buf, dstPixels.width, yrem, 4, bpch);
        }
        std::free(buf);
    }

    iTJSDispatch2 *dstObject, *srcObject;
    long stepx, stepy;
    LayerPixels srcPixels{}, dstPixels{};
};
}

NCB_ATTACH_FUNCTION(shrinkCopy, Layer, ShrinkCopy::copy);
NCB_ATTACH_FUNCTION(shrinkCopyFast, Layer, LimitedShrink::copy);
