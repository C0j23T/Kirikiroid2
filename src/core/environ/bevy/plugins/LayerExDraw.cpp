// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.
#define NCB_MODULE_NAME TJS_W("layerExDraw.dll")
#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "FreeType.h"
#include "FreeTypeFace.h"
#include "FontSystem.h"
#include "BevyRender.h"
#include <cmath>
#include <algorithm>
#include <vector>

extern FontSystem *TVPFontSystem;
extern void TVPGetAllFontList(std::vector<ttstr>& list);

namespace BevyDraw {
static tTJSVariant member(const tTJSVariant &v,const tjs_char *key) {
    tTJSVariant out;
    if(v.Type()==tvtObject) v.AsObjectNoAddRef()->PropGet(0,key,nullptr,&out,v.AsObjectNoAddRef());
    return out;
}
static double number(const tTJSVariant &v,const tjs_char *key,double fallback) {
    auto n=member(v,key); return n.Type()==tvtVoid ? fallback : n.AsReal();
}
struct Font {
    ttstr family; double size; int style;
    Font(ttstr family,double size,int style):family(family),size(size),style(style) { validate(); }
    void validate() { if(!std::isfinite(size) || size<1 || size>4096) TVPThrowExceptionMessage(TJS_W("Invalid font size")); }
    ttstr getFamily() const { return family; } void setFamily(ttstr v) { family=v; }
    double getSize() const { return size; } void setSize(double v) { auto old=size; size=v; try { validate(); } catch(...) { size=old; throw; } }
    int getStyle() const { return style; } void setStyle(int v) { style=v; }
    tTVPFont native() const { return { -int(size), tjs_uint32(((style&1)?TVP_TF_BOLD:0)|((style&2)?TVP_TF_ITALIC:0)|((style&4)?TVP_TF_UNDERLINE:0)|((style&8)?TVP_TF_STRIKEOUT:0)),0,family}; }
    double metric(int kind) const {
        tFreeTypeFace face(TVPFontSystem->GetBeingFont(family),native().Flags);
        face.SetHeight(int(size));
        auto metrics=face.GetBaseFace()->GetFTFace()->size->metrics;
        return double(kind==0 ? metrics.ascender : kind==1 ? -metrics.descender : metrics.height)/64.;
    }
    double ascent() const { return metric(0); }
    double descent() const { return metric(1); }
    double spacing() const { return metric(2); }
    double ascentLeading() const { return std::max(0.0, (spacing()-ascent()-descent())/2.0); }
    double descentLeading() const { return std::max(0.0, spacing()-ascent()-descent()-ascentLeading()); }
};
struct Paint { tjs_uint32 a,b; double x1=0,y1=0,x2=1,y2=0,width=0,ox=0,oy=0; int wrap=0; bool gradient=false; };
struct Appearance {
    std::vector<Paint> paints;
    void clear() { paints.clear(); }
    void add(tTJSVariant color,double width,double ox,double oy) {
        Paint p{}; p.width=width; p.ox=ox; p.oy=oy;
        if(!std::isfinite(width) || width<0 || width>256 || !std::isfinite(ox) || !std::isfinite(oy)) TVPThrowExceptionMessage(TJS_W("Invalid appearance dimensions"));
        if(color.Type()!=tvtObject) p.a=p.b=tjs_uint32(color.AsInteger());
        else {
            int type=number(color,TJS_W("type"),0);
            if(type==0) p.a=p.b=number(color,TJS_W("color"),0);
            else if(type==4) {
                p.gradient=true; p.a=number(color,TJS_W("color1"),0); p.b=number(color,TJS_W("color2"),0);
                auto a=member(color,TJS_W("point1")), b=member(color,TJS_W("point2"));
                auto point=[](const tTJSVariant &v,int i) { tTJSVariant out; auto obj=v.AsObjectNoAddRef(); if(TJS_FAILED(obj->PropGetByNum(0,i,&out,obj))) out=member(v,i ? TJS_W("y") : TJS_W("x")); return out.AsReal(); };
                auto r=member(color,TJS_W("rect"));
                if(a.Type()==tvtObject && b.Type()==tvtObject) {
                    p.x1=point(a,0); p.y1=point(a,1); p.x2=point(b,0); p.y2=point(b,1);
                } else if(r.Type()==tvtObject) {
                    double x=number(r,TJS_W("x"),point(r,0)), y=number(r,TJS_W("y"),point(r,1));
                    double w=number(r,TJS_W("width"),point(r,2)), h=number(r,TJS_W("height"),point(r,3));
                    double angle=number(color,TJS_W("angle"),-1);
                    if(angle<0) {
                        int mode=number(color,TJS_W("mode"),0);
                        if(mode<0 || mode>3) TVPThrowExceptionMessage(TJS_W("Invalid gradient mode"));
                        p.x1=x+(mode==0 || mode==2 ? 0 : w); p.y1=y+(mode==1 || mode==2 ? 0 : h);
                        p.x2=x+(mode==0 || mode==2 ? w : 0); p.y2=y+(mode==1 || mode==2 ? h : 0);
                    } else {
                        double radians=angle*0.017453292519943295;
                        double dx=std::cos(radians),dy=std::sin(radians),extent=std::abs(dx*w)+std::abs(dy*h);
                        double cx=x+w/2,cy=y+h/2;
                        p.x1=cx-dx*extent/2; p.y1=cy-dy*extent/2; p.x2=cx+dx*extent/2; p.y2=cy+dy*extent/2;
                    }
                } else TVPThrowExceptionMessage(TJS_W("Linear gradient requires points or rect"));
                p.wrap=number(color,TJS_W("wrapMode"),0);
                if(p.wrap<0 || p.wrap>4) TVPThrowExceptionMessage(TJS_W("Invalid gradient wrap mode"));
            } else TVPThrowExceptionMessage(TJS_W("Unsupported brush type"));
        }
        for(double v:{p.x1,p.y1,p.x2,p.y2}) if(!std::isfinite(v)) TVPThrowExceptionMessage(TJS_W("Invalid gradient point"));
        paints.push_back(p);
    }
    static tjs_error TJS_INTF_METHOD addBrush(tTJSVariant *,tjs_int n,tTJSVariant **p,iTJSDispatch2 *obj) {
        if(n<1) return TJS_E_BADPARAMCOUNT;
        auto app=ncbInstanceAdaptor<Appearance>::GetNativeInstance(obj);
        if(!app) return TJS_E_INVALIDPARAM;
        app->add(*p[0],0,n>1 ? p[1]->AsReal() : 0,n>2 ? p[2]->AsReal() : 0); return TJS_S_OK;
    }
    static tjs_error TJS_INTF_METHOD addPen(tTJSVariant *,tjs_int n,tTJSVariant **p,iTJSDispatch2 *obj) {
        if(n<2) return TJS_E_BADPARAMCOUNT;
        auto app=ncbInstanceAdaptor<Appearance>::GetNativeInstance(obj);
        if(!app) return TJS_E_INVALIDPARAM;
        double width=p[1]->Type()==tvtObject ? number(*p[1],TJS_W("width"),1) : p[1]->AsReal();
        app->add(*p[0],width,n>2 ? p[2]->AsReal() : 0,n>3 ? p[3]->AsReal() : 0); return TJS_S_OK;
    }
};
struct GdiPlus {
    static tjs_error TJS_INTF_METHOD getFontList(tTJSVariant *result,tjs_int count,tTJSVariant **params,iTJSDispatch2 *) {
        bool privateOnly=count>0 && params[0]->AsInteger()!=0;
        auto array=TJSCreateArrayObject();
        if(!privateOnly) {
            std::vector<ttstr> names;
            TVPGetAllFontList(names);
            for(const auto &name:names) {
                tTJSVariant value(name), *param=&value;
                array->FuncCall(0,TJS_W("add"),nullptr,0,1,&param,array);
            }
        }
        *result=tTJSVariant(array,array); array->Release(); return TJS_S_OK;
    }
    static tjs_error TJS_INTF_METHOD addPrivateFont(tTJSVariant *,tjs_int count,tTJSVariant **,iTJSDispatch2 *) {
        if(count<1) return TJS_E_BADPARAMCOUNT;
        TVPThrowExceptionMessage(TJS_W("Private fonts are not supported by this engine"));
        return TJS_E_FAIL;
    }
};
static tTJSVariant rect(double x,double y,double w,double h) {
    auto obj=TJSCreateDictionaryObject();
    const tjs_char *keys[]={TJS_W("x"),TJS_W("y"),TJS_W("left"),TJS_W("top"),TJS_W("width"),TJS_W("height"),TJS_W("right"),TJS_W("bottom")};
    double values[]={x,y,x,y,w,h,x+w,y+h};
    for(int i=0;i<8;i++) { tTJSVariant v(values[i]); obj->PropSet(TJS_MEMBERENSURE,keys[i],nullptr,&v,obj); }
    tTJSVariant out(obj,obj); obj->Release(); return out;
}
struct Layer {
    static tjs_error render(tTJSVariant *result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *obj,bool draw,bool internal) {
        if(count<(draw ? 5 : 2)) return TJS_E_BADPARAMCOUNT;
        auto font=ncbInstanceAdaptor<Font>::GetNativeInstance(p[0]->AsObjectNoAddRef());
        if(!font) return TJS_E_INVALIDPARAM;
        tTJSNI_BaseLayer *layer=nullptr;
        if(TJS_FAILED(obj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,tTJSNC_Layer::ClassID,(iTJSNativeInstance**)&layer))) return TJS_E_INVALIDPARAM;
        ttstr text=*p[draw ? 4 : 1];
        tTVPBaseTexture metrics(1,1); metrics.SetFont(font->native());
        tTVPRect bounds; metrics.GetFontGlyphDrawRect(text,bounds);
        double x=draw ? p[2]->AsReal() : 0, y=draw ? p[3]->AsReal() : 0;
        if(!std::isfinite(x) || !std::isfinite(y) || std::abs(x)>1000000 || std::abs(y)>1000000) return TJS_E_INVALIDPARAM;
        if(draw) {
            auto app=ncbInstanceAdaptor<Appearance>::GetNativeInstance(p[1]->AsObjectNoAddRef());
            if(!app) return TJS_E_INVALIDPARAM;
            layer->IndependMainImage(); auto image=layer->GetMainImage();
            int w=image->GetWidth(),h=image->GetHeight();
            tTVPBaseTexture mask(w,h); mask.Fill(tTVPRect(0,0,w,h),0); mask.SetFont(font->native());
            mask.DrawText(tTVPRect(0,0,w,h),int(x),int(y),text,0xffffff,bmAlphaOnAlpha,255,false,true);
            auto clip=layer->GetClip();
            for(auto paint:app->paints) {
                TVPBevyRenderCommand c{}; c.operation=4; c.method="LayerExTextPaint";
                c.target=TVPBevyTextureID(image->GetTexture()); c.sources[0]=TVPBevyTextureID(mask.GetTexture());
                c.rect[0]=clip.left; c.rect[1]=clip.top; c.rect[2]=clip.right; c.rect[3]=clip.bottom;
                c.source_rects[0][2]=w; c.source_rects[0][3]=h;
                c.parameters[4]=paint.width/2; c.parameters[5]=paint.ox; c.parameters[6]=paint.oy;
                c.parameters[7]=paint.gradient; c.parameters[8]=paint.x1; c.parameters[9]=paint.y1; c.parameters[10]=paint.x2; c.parameters[11]=paint.y2;
                c.parameters[14]=paint.wrap;
                for(int i=0;i<4;i++) { int shift=i==0 ? 16 : i==1 ? 8 : i==2 ? 0 : 24; c.parameters[i]=float((paint.a>>shift)&255)/255; c.parameters[12+i]=float((paint.b>>shift)&255)/255; }
                if(krkr_render_dispatch(&c)) return TJS_E_FAIL;
            }
            TVPBevyInvalidateTexture(image->GetTexture()); layer->SetImageModified(true); layer->Update();
        }
        if(result) *result=internal ? rect(bounds.left,bounds.top,bounds.get_width(),bounds.get_height()) : rect(x,y,metrics.GetTextWidth(text),metrics.GetTextHeight(text));
        return TJS_S_OK;
    }
    static tjs_error TJS_INTF_METHOD draw(tTJSVariant *r,tjs_int n,tTJSVariant **p,iTJSDispatch2 *o) { return render(r,n,p,o,true,false); }
    static tjs_error TJS_INTF_METHOD measure(tTJSVariant *r,tjs_int n,tTJSVariant **p,iTJSDispatch2 *o) { return render(r,n,p,o,false,false); }
    static tjs_error TJS_INTF_METHOD measureInternal(tTJSVariant *r,tjs_int n,tTJSVariant **p,iTJSDispatch2 *o) { return render(r,n,p,o,false,true); }
};
}
NCB_REGISTER_SUBCLASS(BevyDraw::Font) {
    NCB_CONSTRUCTOR((ttstr,double,int));
    NCB_PROPERTY(familyName,getFamily,setFamily); NCB_PROPERTY(emSize,getSize,setSize); NCB_PROPERTY(style,getStyle,setStyle);
    NCB_PROPERTY_RO(ascent,ascent); NCB_PROPERTY_RO(descent,descent); NCB_PROPERTY_RO(lineSpacing,spacing);
    NCB_PROPERTY_RO(ascentLeading,ascentLeading); NCB_PROPERTY_RO(descentLeading,descentLeading);
}
NCB_REGISTER_SUBCLASS(BevyDraw::Appearance) {
    NCB_CONSTRUCTOR(()); NCB_METHOD(clear); RawCallback("addBrush",BevyDraw::Appearance::addBrush,0); RawCallback("addPen",BevyDraw::Appearance::addPen,0);
}
using BevyGdiPlus=BevyDraw::GdiPlus;
using BevyDrawLayer=BevyDraw::Layer;
NCB_REGISTER_CLASS_DIFFER(GdiPlus,BevyGdiPlus) {
    NCB_SUBCLASS(Font,BevyDraw::Font); NCB_SUBCLASS(Appearance,BevyDraw::Appearance);
    RawCallback("getFontList",BevyGdiPlus::getFontList,0);
    RawCallback("addPrivateFont",BevyGdiPlus::addPrivateFont,0);
    Variant(TJS_W("FontStyleRegular"),tTJSVariant(0)); Variant(TJS_W("FontStyleBold"),tTJSVariant(1)); Variant(TJS_W("FontStyleItalic"),tTJSVariant(2));
    Variant(TJS_W("FontStyleUnderline"),tTJSVariant(4)); Variant(TJS_W("FontStyleStrikeout"),tTJSVariant(8)); Variant(TJS_W("BrushTypeSolidColor"),tTJSVariant(0)); Variant(TJS_W("BrushTypeLinearGradient"),tTJSVariant(4));
}
NCB_ATTACH_CLASS(BevyDrawLayer,Layer) {
    RawCallback("drawString",BevyDraw::Layer::draw,0); RawCallback("drawPathString",BevyDraw::Layer::draw,0);
    RawCallback("measureString",BevyDraw::Layer::measure,0); RawCallback("measureStringInternal",BevyDraw::Layer::measureInternal,0);
}
