// Algorithms: W.Dee and contributors (extrans); extNagano recovery authors.
// See LICENSE-THIRD-PARTY.md for sources and compatibility limits.
#pragma once
#include "TransIntf.h"
#include "BevyRender.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace BevyExtraTrans {
inline double option(iTVPSimpleOptionProvider *o, const tjs_char *name, double fallback) {
    tTJSVariant v;
    return TJS_SUCCEEDED(o->GetValue(name, &v)) && v.Type()!=tvtVoid ? v.AsReal() : fallback;
}
class Handler : public iTVPDivisibleTransHandler {
    int refs=1, effect, width, height, layer;
    tjs_uint64 duration, start=0, elapsed=0;
    bool first=true;
    double values[9]{};
public:
    Handler(int effect, int width, int height, int layer, tjs_uint64 duration, iTVPSimpleOptionProvider *o)
        : effect(effect), width(width), height(height), layer(layer), duration(duration) {
        if(effect==0) {
            values[0]=option(o,TJS_W("maxh"),50); values[1]=option(o,TJS_W("maxomega"),0.2);
            values[2]=option(o,TJS_W("wavetype"),0);
            values[3]=option(o,TJS_W("bgcolor1"),0); values[4]=option(o,TJS_W("bgcolor2"),0);
            if(values[2]<0 || values[2]>2) TVPThrowExceptionMessage(TJS_W("Invalid wavetype"));
        } else if(effect==1) values[0]=std::max(2.,option(o,TJS_W("maxsize"),30));
        else {
            values[0]=option(o,TJS_W("exponent"),1); if(values[0]<=0) values[0]=1;
            double b1=option(o,TJS_W("blur1"),0), b2=option(o,TJS_W("blur2"),0);
            values[1]=option(o,TJS_W("blur1x"),b1); values[2]=option(o,TJS_W("blur1y"),b1);
            values[3]=option(o,TJS_W("blur2x"),b2); values[4]=option(o,TJS_W("blur2y"),b2);
            for(int i=1;i<=4;i++) if(values[i]<0 || values[i]>=(i%2 ? width : height)/2) values[i]=0;
            values[5]=option(o,TJS_W("type"),0); values[6]=option(o,TJS_W("prerender"),0);
            if(values[5]!=0 && values[5]!=1) TVPThrowExceptionMessage(TJS_W("Invalid blurfade type"));
            if(values[6]!=0) TVPThrowExceptionMessage(TJS_W("blurfade prerender requires 0"));
        }
        if(effect==0 && (std::abs(values[0])>1000000 || std::abs(values[1])>1000000)) TVPThrowExceptionMessage(TJS_W("Wave option is out of range"));
        if(effect==1 && values[0]>1000000) TVPThrowExceptionMessage(TJS_W("Mosaic maxsize is out of range"));
        for(double v:values) if(!std::isfinite(v)) TVPThrowExceptionMessage(TJS_W("Nonfinite transition option"));
    }
    tjs_error TJS_INTF_METHOD AddRef() override { ++refs; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD Release() override { if(!--refs) delete this; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *) override { return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick) override {
        if(first) { first=false; start=tick; }
        elapsed=tick<start ? 0 : std::min(tick-start,duration); return TJS_S_TRUE;
    }
    tjs_error TJS_INTF_METHOD EndProcess() override { return elapsed==duration ? TJS_S_FALSE : TJS_S_TRUE; }
    tjs_error TJS_INTF_METHOD MakeFinalImage(iTVPScanLineProvider **d,iTVPScanLineProvider *,iTVPScanLineProvider *s) override { *d=s; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *d) override {
        if(elapsed==0) { d->Dest=d->Src1; return TJS_S_OK; }
        if(elapsed==duration) { d->Dest=d->Src2; return TJS_S_OK; }
        TVPBevyRenderCommand c{}; c.operation=4;
        c.target=TVPBevyTextureID(d->Dest->GetTextureForRender());
        c.sources[0]=TVPBevyTextureID(d->Src1->GetTexture()); c.sources[1]=TVPBevyTextureID(d->Src2->GetTexture());
        c.rect[0]=d->DestLeft; c.rect[1]=d->DestTop; c.rect[2]=d->DestLeft+d->Width; c.rect[3]=d->DestTop+d->Height;
        c.source_rects[0][0]=d->Src1Left; c.source_rects[0][1]=d->Src1Top;
        c.source_rects[1][0]=d->Src2Left; c.source_rects[1][1]=d->Src2Top;
        for(int i=0;i<2;i++) { c.source_rects[i][2]=c.source_rects[i][0]+d->Width; c.source_rects[i][3]=c.source_rects[i][1]+d->Height; }
        double f=double(elapsed)/duration;
        c.parameters[0]=int(f*255); c.parameters[2]=layer;
        std::vector<float> shifts;
        if(effect==0) {
            c.method="ExtraWave";
            double t=std::min(elapsed,duration-elapsed), envelope=std::sin(1.5707963267948966*t/(duration/2));
            int amplitude=int(envelope*values[0]);
            double omega=values[1]*(values[2]==0 ? envelope : values[2]==1 ? 1-f : f);
            shifts.resize(height);
            for(int y=0;y<height;y++) shifts[y]=std::trunc(std::sin(omega*(y-height/2))*amplitude);
            c.data=shifts.data(); c.pitch=height;
            for(int i=0;i<4;i++) {
                int shift=i==0 ? 16 : i==1 ? 8 : i==2 ? 0 : 24;
                int a=(tjs_uint32(values[3])>>shift)&255, b=(tjs_uint32(values[4])>>shift)&255;
                c.parameters[12+i]=float((a*256+(b-a)*int(f*255))/256)/255;
            }
        } else if(effect==1) {
            c.method="ExtraMosaic";
            int size=int((values[0]-2)*std::min(elapsed,duration-elapsed)/(duration/2))+2;
            c.parameters[4]=size;
            int ox=(width-size)/2-(width/2/size)*size, oy=(height-size)/2-(height/2/size)*size;
            c.parameters[5]=ox>0 ? ox-size : ox; c.parameters[6]=oy>0 ? oy-size : oy;
        } else {
            c.method="ExtraBlurFade"; f=std::pow(f,values[0]); c.parameters[0]=int(f*255+0.5);
            for(int i=1;i<=4;i++) c.parameters[3+i]=int(values[i]*(i<3 ? f : 1-f)+0.5);
            c.parameters[8]=values[5];
        }
        return krkr_render_dispatch(&c)==0 ? TJS_S_OK : TJS_E_FAIL;
    }
};
class Provider : public iTVPTransHandlerProvider {
    int refs=1,effect; const tjs_char *name;
public:
    Provider(int effect,const tjs_char *name):effect(effect),name(name) {}
    tjs_error TJS_INTF_METHOD AddRef() override { ++refs; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD Release() override { if(!--refs) delete this; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD GetName(const tjs_char **out) override { if(out) *out=name; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD StartTransition(iTVPSimpleOptionProvider *o,iTVPSimpleImageProvider *,tTVPLayerType layer,
            tjs_uint w,tjs_uint h,tjs_uint w2,tjs_uint h2,tTVPTransType *type,tTVPTransUpdateType *update,iTVPBaseTransHandler **out) override {
        if(!o || !out || !w || !h || w!=w2 || h!=h2) return TJS_E_INVALIDPARAM;
        double time=option(o,TJS_W("time"),-1);
        if(!std::isfinite(time) || time<0 || time>double(INT64_MAX)) return TJS_E_INVALIDPARAM;
        auto handler=new Handler(effect,w,h,layer,std::max(tjs_uint64(2),tjs_uint64(time)),o);
        if(type) *type=ttExchange; if(update) *update=tutDivisible; *out=handler; return TJS_S_OK;
    }
};
}
