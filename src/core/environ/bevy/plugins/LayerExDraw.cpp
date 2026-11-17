// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.
#define NCB_MODULE_NAME TJS_W("layerExDraw.dll")
#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "FreeType.h"
#include "FreeTypeFace.h"
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H
#include "FontSystem.h"
#include "FontImpl.h"
#include "GraphicsLoaderIntf.h"
#include "BevyRender.h"
#include "BevyLayerExDraw.h"
#include "LayerExDrawGeometry.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <memory>

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
#include "LayerExDrawObjects.inc"
#include "LayerExDrawImage.inc"
struct Font {
    ttstr family; double size; int style; bool forceSelfPathDraw=false;
    Font(ttstr family,double size,int style):family(family),size(size),style(style) { validate(); }
    void validate() { if(!std::isfinite(size) || size<1 || size>4096) TVPThrowExceptionMessage(TJS_W("Invalid font size")); }
    ttstr getFamily() const { return family; } void setFamily(ttstr v) { family=v; }
    double getSize() const { return size; } void setSize(double v) { auto old=size; size=v; try { validate(); } catch(...) { size=old; throw; } }
    int getStyle() const { return style; } void setStyle(int v) { style=v; }
    bool getForceSelfPathDraw() const { return forceSelfPathDraw; }
    void setForceSelfPathDraw(bool value) { forceSelfPathDraw=value; }
    ttstr resolvedFamily() const { return TVPFindFont(family) ? family : TVPFontSystem->GetBeingFont(family); }
    tTVPFont native() const { return { -int(size), tjs_uint32(((style&1)?TVP_TF_BOLD:0)|((style&2)?TVP_TF_ITALIC:0)|((style&4)?TVP_TF_UNDERLINE:0)|((style&8)?TVP_TF_STRIKEOUT:0)),0,family}; }
    double metric(int kind) const {
        tFreeTypeFace face(resolvedFamily(),native().Flags);
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
struct Paint {
    tjs_uint32 a,b;
    double x1=0,y1=0,x2=1,y2=0,width=0,ox=0,oy=0;
    int wrap=0; bool gradient=false,pen=false; unsigned brushType=0,hatch=0,brushFlags=0;
    Matrix matrix;
    double miterLimit=10,dashOffset=0;
    unsigned startCap=0,endCap=0,dashCap=0,join=0,alignment=0;
    std::vector<double> dashes;
    std::vector<TVPBevyDrawColorStop> stops;
    std::shared_ptr<tTVPBaseTexture> image;
    std::array<double,4> tile{},startArrow{},endArrow{};
    std::array<double,2> center{},focus{};
    std::vector<TVPBevyDrawPoint> brushPoints;
    std::vector<uint32_t> surroundColors;
    std::vector<double> blendPositions,blendFactors,compound;
    TVPBevyDrawPaint operation() const {
        TVPBevyDrawPaint p{};
        p.brush_type=image ? 2 : gradient ? 4 : brushType; p.stroke=pen; p.colors[0]=a; p.colors[1]=b; p.hatch=hatch;
        p.image=image ? TVPBevyTextureID(image->GetTexture()) : 0;
        p.wrap=wrap; p.coordinates[0]=x1; p.coordinates[1]=y1; p.coordinates[2]=x2; p.coordinates[3]=y2;
        std::copy(matrix.elements.begin(),matrix.elements.end(),p.paint_transform);
        p.offset[0]=ox; p.offset[1]=oy; p.width=width; p.miter_limit=miterLimit; p.dash_offset=dashOffset;
        p.start_cap=startCap; p.end_cap=endCap; p.dash_cap=dashCap; p.join=join; p.alignment=alignment;
        p.stop_count=stops.size(); p.stops=stops.data(); p.dash_count=dashes.size(); p.dashes=dashes.data();
        std::copy(tile.begin(),tile.end(),p.tile); std::copy(center.begin(),center.end(),p.center); std::copy(focus.begin(),focus.end(),p.focus);
        p.brush_point_count=brushPoints.size(); p.brush_points=brushPoints.data();
        p.surround_count=surroundColors.size(); p.surround_colors=surroundColors.data();
        p.blend_count=blendPositions.size(); p.blend_positions=blendPositions.data(); p.blend_factors=blendFactors.data(); p.brush_flags=brushFlags;
        p.compound_count=compound.size(); p.compound=compound.data();
        std::copy(startArrow.begin(),startArrow.end(),p.start_arrow); std::copy(endArrow.begin(),endArrow.end(),p.end_arrow);
        return p;
    }
};
struct Appearance {
    std::vector<Paint> paints;
    void clear() { paints.clear(); }
    static unsigned cap(const tTJSVariant &value,double width,std::array<double,4> &arrow) {
        if(value.Type()!=tvtObject) return value.Type()==tvtVoid ? 0 : value.AsInteger();
        arrow={number(value,TJS_W("width"),width),number(value,TJS_W("height"),width),
               number(value,TJS_W("middleInset"),0),number(value,TJS_W("filled"),1)!=0 ? 1.0 : 0.0};
        for(double n:arrow) if(!std::isfinite(n)) TVPThrowExceptionMessage(TJS_W("Invalid arrow cap"));
        return 255;
    }
    void add(tTJSVariant color,double width,double ox,double oy,bool pen=false) {
        Paint p{}; p.width=width; p.ox=ox; p.oy=oy; p.pen=pen;
        if(!std::isfinite(width) || width<0 || width>256 || !std::isfinite(ox) || !std::isfinite(oy)) TVPThrowExceptionMessage(TJS_W("Invalid appearance dimensions"));
        if(color.Type()!=tvtObject) p.a=p.b=tjs_uint32(color.AsInteger());
        else {
            int type=number(color,TJS_W("type"),0);
            p.brushType=type;
            if(type==0) p.a=p.b=number(color,TJS_W("color"),0xffffffff);
            else if(type==1) {
                p.hatch=number(color,TJS_W("hatchStyle"),0); p.a=number(color,TJS_W("foreColor"),0xffffffff); p.b=number(color,TJS_W("backColor"),0xff000000);
            } else if(type==3) {
                auto points=pointValues(member(color,TJS_W("points")));
                if(points.size()<3) TVPThrowExceptionMessage(TJS_W("Path gradient requires at least three points"));
                GeometryRect bounds{points[0].x,points[0].y,0,0}; double right=bounds.x,bottom=bounds.y;
                for(auto point:points) { p.brushPoints.push_back({point.x,point.y}); bounds.x=std::min(bounds.x,point.x); bounds.y=std::min(bounds.y,point.y); right=std::max(right,point.x); bottom=std::max(bottom,point.y); }
                bounds.width=right-bounds.x; bounds.height=bottom-bounds.y; p.tile={bounds.x,bounds.y,bounds.width,bounds.height};
                double area=0,cx=0,cy=0;
                for(std::size_t i=0;i<points.size();++i) {
                    auto a=points[i],b=points[(i+1)%points.size()]; double cross=a.x*b.y-b.x*a.y;
                    area+=cross; cx+=(a.x+b.x)*cross; cy+=(a.y+b.y)*cross;
                }
                GeometryPoint defaultCenter{};
                if(std::abs(area)>1e-12) defaultCenter={cx/(3*area),cy/(3*area)};
                else { for(auto point:points) { defaultCenter.x+=point.x; defaultCenter.y+=point.y; } defaultCenter.x/=points.size(); defaultCenter.y/=points.size(); }
                auto center=member(color,TJS_W("centerPoint")); auto value=center.Type()==tvtVoid ? defaultCenter : pointValue(center);
                p.center={value.x,value.y}; p.a=number(color,TJS_W("centerColor"),0xffffffff); p.b=0xff000000;
                auto focus=member(color,TJS_W("focusScales"));
                if(focus.Type()!=tvtVoid) p.focus={component(focus,0,TJS_W("xScale")),component(focus,1,TJS_W("yScale"))};
                auto surrounds=member(color,TJS_W("surroundColors"));
                if(surrounds.Type()!=tvtVoid) for(int i=0,count=arrayCount(surrounds);i<count;++i) p.surroundColors.push_back(arrayValue(surrounds,i).AsInteger());
                if(p.surroundColors.empty()) p.surroundColors.push_back(0xff000000);
                p.wrap=number(color,TJS_W("wrapMode"),0);
            } else if(type==2) {
                p.image=Image::read(member(color,TJS_W("image"))); p.wrap=number(color,TJS_W("wrapMode"),0);
                auto dest=member(color,TJS_W("dstRect"));
                auto area=dest.Type()==tvtVoid ? GeometryRect{0,0,double(p.image->GetWidth()),double(p.image->GetHeight())} : rectValue(dest);
                p.x1=area.x; p.y1=area.y; p.x2=area.width; p.y2=area.height;
                auto transform=member(color,TJS_W("transform"));
                if(transform.Type()!=tvtVoid) p.matrix=Matrix::read(transform);
            } else if(type==4) {
                p.gradient=true; p.a=number(color,TJS_W("color1"),0); p.b=number(color,TJS_W("color2"),0);
                auto a=member(color,TJS_W("point1")), b=member(color,TJS_W("point2"));
                auto r=member(color,TJS_W("rect"));
                if(a.Type()==tvtObject && b.Type()==tvtObject) {
                    auto from=pointValue(a),to=pointValue(b);
                    p.x1=from.x; p.y1=from.y; p.x2=to.x; p.y2=to.y;
                } else if(r.Type()==tvtObject) {
                    auto rect=rectValue(r); double x=rect.x,y=rect.y,w=rect.width,h=rect.height;
                    p.tile={x,y,w,h};
                    auto specifiedAngle=member(color,TJS_W("angle"));
                    if(specifiedAngle.Type()==tvtVoid) {
                        int mode=number(color,TJS_W("mode"),0);
                        if(mode<0 || mode>3) TVPThrowExceptionMessage(TJS_W("Invalid gradient mode"));
                        p.x1=x+(mode==3 ? w : 0); p.y1=y;
                        p.x2=x+(mode==1 ? 0 : mode==3 ? 0 : w); p.y2=y+(mode==0 ? 0 : h);
                    } else {
                        double angle=coordinate(specifiedAngle);
                        double radians=angle*0.017453292519943295;
                        double dx=std::cos(radians),dy=std::sin(radians),extent=std::abs(dx*w)+std::abs(dy*h);
                        double cx=x+w/2,cy=y+h/2;
                        p.x1=cx-dx*extent/2; p.y1=cy-dy*extent/2; p.x2=cx+dx*extent/2; p.y2=cy+dy*extent/2;
                    }
                } else TVPThrowExceptionMessage(TJS_W("Linear gradient requires points or rect"));
                if(p.tile[2]==0 || p.tile[3]==0) p.tile={std::min(p.x1,p.x2),std::min(p.y1,p.y2),std::abs(p.x2-p.x1),std::abs(p.y2-p.y1)};
                p.wrap=number(color,TJS_W("wrapMode"),0);
                if(p.wrap<0 || p.wrap>4) TVPThrowExceptionMessage(TJS_W("Invalid gradient wrap mode"));
                auto transform=member(color,TJS_W("transform"));
                if(transform.Type()!=tvtVoid) p.matrix=Matrix::read(transform);
                auto interpolation=member(color,TJS_W("interpolationColors"));
                if(interpolation.Type()==tvtObject) {
                    auto colors=member(interpolation,TJS_W("presetColors"));
                    auto positions=member(interpolation,TJS_W("blendPositions"));
                    if(colors.Type()==tvtVoid) colors=arrayValue(interpolation,0);
                    if(positions.Type()==tvtVoid) positions=arrayValue(interpolation,1);
                    int count=arrayCount(colors);
                    if(count<2 || count!=arrayCount(positions)) TVPThrowExceptionMessage(TJS_W("Invalid gradient color stops"));
                    double previous=-1;
                    for(int i=0;i<count;++i) {
                        double position=coordinate(arrayValue(positions,i));
                        if(position<previous || position<0 || position>1) TVPThrowExceptionMessage(TJS_W("Invalid gradient stop position"));
                        p.stops.push_back({position,static_cast<tjs_uint32>(arrayValue(colors,i).AsInteger()),0}); previous=position;
                    }
                    if(p.stops.front().position!=0 || p.stops.back().position!=1) TVPThrowExceptionMessage(TJS_W("Gradient stops must span zero to one"));
                }
            } else TVPThrowExceptionMessage(TJS_W("Unsupported brush type"));
            if(type==3 || type==4) {
                auto transform=member(color,TJS_W("transform")); if(transform.Type()!=tvtVoid) p.matrix=Matrix::read(transform);
                p.brushFlags=number(color,TJS_W("useGammaCorrection"),0)!=0;
                auto blend=member(color,TJS_W("blend"));
                if(blend.Type()!=tvtVoid) {
                    auto factors=member(blend,TJS_W("blendFactors")),positions=member(blend,TJS_W("blendPositions"));
                    if(factors.Type()==tvtVoid) factors=arrayValue(blend,0);
                    if(positions.Type()==tvtVoid) positions=arrayValue(blend,1);
                    int count=arrayCount(factors);
                    if(count<2 || count!=arrayCount(positions)) TVPThrowExceptionMessage(TJS_W("Invalid gradient blend"));
                    for(int i=0;i<count;++i) { p.blendFactors.push_back(coordinate(arrayValue(factors,i))); p.blendPositions.push_back(coordinate(arrayValue(positions,i))); }
                }
            }
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
        app->add(*p[0],width,n>2 ? p[2]->AsReal() : 0,n>3 ? p[3]->AsReal() : 0,true);
        try {
            if(p[1]->Type()==tvtObject) {
                auto &paint=app->paints.back();
                paint.startCap=cap(member(*p[1],TJS_W("startCap")),width,paint.startArrow); paint.endCap=cap(member(*p[1],TJS_W("endCap")),width,paint.endArrow);
                paint.dashCap=number(*p[1],TJS_W("dashCap"),0); paint.join=number(*p[1],TJS_W("lineJoin"),0);
                paint.alignment=number(*p[1],TJS_W("alignment"),0); paint.miterLimit=number(*p[1],TJS_W("miterLimit"),10);
                paint.dashOffset=number(*p[1],TJS_W("dashOffset"),0);
                auto compound=member(*p[1],TJS_W("compoundArray"));
                if(compound.Type()!=tvtVoid) {
                    int count=arrayCount(compound);
                    if(count<2 || count%2) TVPThrowExceptionMessage(TJS_W("Invalid compound pen"));
                    for(int i=0;i<count;++i) paint.compound.push_back(coordinate(arrayValue(compound,i)));
                }
                auto style=member(*p[1],TJS_W("dashStyle"));
                if(style.Type()==tvtObject) {
                    for(int i=0,count=arrayCount(style);i<count;++i) {
                        double dash=coordinate(arrayValue(style,i));
                        if(dash<=0) TVPThrowExceptionMessage(TJS_W("Invalid pen dash"));
                        paint.dashes.push_back(dash);
                    }
                    if(paint.dashes.empty()) TVPThrowExceptionMessage(TJS_W("Empty pen dash pattern"));
                } else if(style.Type()!=tvtVoid) {
                    switch(style.AsInteger()) {
                    case 0: break;
                    case 1: paint.dashes={3,1}; break;
                    case 2: paint.dashes={1,1}; break;
                    case 3: paint.dashes={3,1,1,1}; break;
                    case 4: paint.dashes={3,1,1,1,1,1}; break;
                    default: TVPThrowExceptionMessage(TJS_W("Invalid pen dash style"));
                    }
                }
                if(!std::isfinite(paint.miterLimit) || paint.miterLimit<=0 || !std::isfinite(paint.dashOffset))
                    TVPThrowExceptionMessage(TJS_W("Invalid pen dimensions"));
            }
        } catch(...) { app->paints.pop_back(); throw; }
        return TJS_S_OK;
    }
};
struct GdiPlus {
    static std::vector<ttstr> &privateFonts() { static std::vector<ttstr> names; return names; }
    static tjs_error TJS_INTF_METHOD getFontList(tTJSVariant *result,tjs_int count,tTJSVariant **params,iTJSDispatch2 *) {
        bool privateOnly=count>0 && params[0]->AsInteger()!=0;
        auto array=TJSCreateArrayObject();
        {
            std::vector<ttstr> names=privateFonts();
            if(!privateOnly) { names.clear(); TVPGetAllFontList(names); }
            for(const auto &name:names) {
                tTJSVariant value(name), *param=&value;
                array->FuncCall(0,TJS_W("add"),nullptr,0,1,&param,array);
            }
        }
        *result=tTJSVariant(array,array); array->Release(); return TJS_S_OK;
    }
    static tjs_error TJS_INTF_METHOD addPrivateFont(tTJSVariant *,tjs_int count,tTJSVariant **p,iTJSDispatch2 *) {
        if(count<1) return TJS_E_BADPARAMCOUNT;
        ttstr name=TVPGetPlacedPath(ttstr(*p[0]));
        if(name.IsEmpty() || !TVPEnumFontsProc(name)) TVPThrowExceptionMessage(TJS_W("Could not register private font"));
        auto last=TVPFontNames.GetLast();
        for(auto it=TVPFontNames.GetFirst();it!=last;++it) if(it.GetValue().Path==name) {
            auto family=it.GetKey();
            auto &names=privateFonts(); if(std::find(names.begin(),names.end(),family)==names.end()) names.push_back(family);
        }
        return TJS_S_OK;
    }
};
static tTJSVariant rect(double x,double y,double w,double h) {
    auto obj=TJSCreateDictionaryObject();
    const tjs_char *keys[]={TJS_W("x"),TJS_W("y"),TJS_W("left"),TJS_W("top"),TJS_W("width"),TJS_W("height"),TJS_W("right"),TJS_W("bottom")};
    double values[]={x,y,x,y,w,h,x+w,y+h};
    for(int i=0;i<8;i++) { tTJSVariant v(values[i]); obj->PropSet(TJS_MEMBERENSURE,keys[i],nullptr,&v,obj); }
    tTJSVariant out(obj,obj); obj->Release(); return out;
}
#include "LayerExDrawState.inc"
#include "LayerExDrawText.inc"
#include "LayerExDrawReadback.inc"
struct Layer {
    static tjs_error TJS_INTF_METHOD clear(tTJSVariant *,tjs_int count,tTJSVariant **p,iTJSDispatch2 *obj) {
        if(count<1) return TJS_E_BADPARAMCOUNT;
        tTJSNI_BaseLayer *layer=nullptr;
        if(TJS_FAILED(obj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,tTJSNC_Layer::ClassID,(iTJSNativeInstance**)&layer))) return TJS_E_INVALIDPARAM;
        layer->IndependMainImage(); auto image=layer->GetMainImage();
        const tjs_uint32 color=tjs_uint32(p[0]->AsInteger());
        TVPBevyRenderCommand c{}; c.operation=4; c.method="FillARGB";
        c.target=TVPBevyTextureID(image->GetTexture());
        c.rect[2]=image->GetWidth(); c.rect[3]=image->GetHeight();
        c.parameters[12]=float((color>>16)&255)/255;
        c.parameters[13]=float((color>>8)&255)/255;
        c.parameters[14]=float(color&255)/255;
        c.parameters[15]=float((color>>24)&255)/255;
        if(krkr_render_dispatch(&c)) return TJS_E_FAIL;
        TVPBevyInvalidateTexture(image->GetTexture()); layer->SetImageModified(true);
        auto state=DrawState::get(obj);
        if(state->recording && !state->replaying) { state->records.clear(); state->recordBackground=color; }
        if(state->updateWhenDraw) layer->Update();
        return TJS_S_OK;
    }
    static tjs_error measureText(tTJSVariant *result,tjs_int count,tTJSVariant **p,bool internal) {
        if(count<2) return TJS_E_BADPARAMCOUNT;
        if(p[0]->Type()!=tvtObject) return TJS_E_INVALIDPARAM;
        auto font=ncbInstanceAdaptor<Font>::GetNativeInstance(p[0]->AsObjectNoAddRef());
        if(!font) return TJS_E_INVALIDPARAM;
        ttstr text=*p[1];
        tTVPBaseTexture metrics(1,1); metrics.SetFont(font->native());
        tTVPRect bounds; metrics.GetFontGlyphDrawRect(text,bounds);
        if(result) *result=internal ? rect(bounds.left,bounds.top,bounds.get_width(),bounds.get_height()) : rect(0,0,metrics.GetTextWidth(text),metrics.GetTextHeight(text));
        return TJS_S_OK;
    }
    static tjs_error TJS_INTF_METHOD measure(tTJSVariant *r,tjs_int n,tTJSVariant **p,iTJSDispatch2 *) { return measureText(r,n,p,false); }
    static tjs_error TJS_INTF_METHOD measureInternal(tTJSVariant *r,tjs_int n,tTJSVariant **p,iTJSDispatch2 *) { return measureText(r,n,p,true); }
};
}
NCB_REGISTER_SUBCLASS(BevyDraw::Point) {
    NCB_CONSTRUCTOR((double,double));
    NCB_PROPERTY(x,getX,setX); NCB_PROPERTY(y,getY,setY); NCB_METHOD(Equals);
}
NCB_REGISTER_SUBCLASS(BevyDraw::Rect) {
    NCB_CONSTRUCTOR((double,double,double,double));
    NCB_PROPERTY(x,getX,setX); NCB_PROPERTY(y,getY,setY);
    NCB_PROPERTY(width,getWidth,setWidth); NCB_PROPERTY(height,getHeight,setHeight);
    NCB_PROPERTY_RO(left,getX); NCB_PROPERTY_RO(top,getY);
    NCB_PROPERTY_RO(right,getRight); NCB_PROPERTY_RO(bottom,getBottom);
    NCB_PROPERTY_RO(location,getLocation); NCB_PROPERTY_RO(bounds,getBounds);
    NCB_METHOD(Clone); NCB_METHOD(Equals); NCB_METHOD(Inflate); NCB_METHOD(InflatePoint);
    NCB_METHOD(Offset); NCB_METHOD(IntersectsWith); NCB_METHOD(IsEmptyArea); NCB_METHOD(Union);
}
NCB_REGISTER_SUBCLASS(BevyDraw::Matrix) {
    Factory(BevyDraw::Matrix::create);
    NCB_METHOD(OffsetX); NCB_METHOD(OffsetY); NCB_METHOD(Equals); NCB_METHOD(SetElements);
    NCB_METHOD(GetLastStatus); NCB_METHOD(Invert); NCB_METHOD(IsIdentity); NCB_METHOD(IsInvertible); NCB_METHOD(Reset);
    RawCallback("Multiply",BevyDraw::Matrix::change<0>,0); RawCallback("Rotate",BevyDraw::Matrix::change<1>,0);
    RawCallback("RotateAt",BevyDraw::Matrix::change<2>,0); RawCallback("Scale",BevyDraw::Matrix::change<3>,0);
    RawCallback("Shear",BevyDraw::Matrix::change<4>,0); RawCallback("Translate",BevyDraw::Matrix::change<5>,0);
}
NCB_REGISTER_SUBCLASS(BevyDraw::Path) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(startFigure); NCB_METHOD(closeFigure); NCB_METHOD(drawArc); NCB_METHOD(drawPie);
    NCB_METHOD(drawBezier); NCB_METHOD(drawBeziers); NCB_METHOD(drawClosedCurve); NCB_METHOD(drawClosedCurve2);
    NCB_METHOD(drawCurve); NCB_METHOD(drawCurve2); NCB_METHOD(drawCurve3); NCB_METHOD(drawEllipse);
    NCB_METHOD(drawLine); NCB_METHOD(drawLines); NCB_METHOD(drawPolygon); NCB_METHOD(drawRectangle); NCB_METHOD(drawRectangles);
}
NCB_REGISTER_SUBCLASS(BevyDraw::Image) {
    Factory(BevyDraw::Image::create);
    NCB_METHOD(load); NCB_METHOD(Clone); NCB_METHOD(GetBounds); NCB_METHOD(GetWidth); NCB_METHOD(GetHeight);
    NCB_METHOD(GetHorizontalResolution); NCB_METHOD(GetVerticalResolution); NCB_METHOD(GetType);
    NCB_METHOD(GetPixelFormat); NCB_METHOD(GetFlags); NCB_METHOD(GetLastStatus); NCB_METHOD(RotateFlip);
}
NCB_REGISTER_SUBCLASS(BevyDraw::Font) {
    NCB_CONSTRUCTOR((ttstr,double,int));
    NCB_PROPERTY(familyName,getFamily,setFamily); NCB_PROPERTY(emSize,getSize,setSize); NCB_PROPERTY(style,getStyle,setStyle);
    NCB_PROPERTY(forceSelfPathDraw,getForceSelfPathDraw,setForceSelfPathDraw);
    NCB_PROPERTY_RO(ascent,ascent); NCB_PROPERTY_RO(descent,descent); NCB_PROPERTY_RO(lineSpacing,spacing);
    NCB_PROPERTY_RO(ascentLeading,ascentLeading); NCB_PROPERTY_RO(descentLeading,descentLeading);
}
NCB_REGISTER_SUBCLASS(BevyDraw::Appearance) {
    NCB_CONSTRUCTOR(()); NCB_METHOD(clear); RawCallback("addBrush",BevyDraw::Appearance::addBrush,0); RawCallback("addPen",BevyDraw::Appearance::addPen,0);
}
using BevyGdiPlus=BevyDraw::GdiPlus;
using BevyDrawLayer=BevyDraw::Layer;
using BevyDrawState=BevyDraw::DrawState;
NCB_REGISTER_CLASS_DIFFER(GdiPlus,BevyGdiPlus) {
#include "LayerExDrawConstants.inc"
    NCB_SUBCLASS(Font,BevyDraw::Font); NCB_SUBCLASS(Appearance,BevyDraw::Appearance);
    NCB_SUBCLASS(PointF,BevyDraw::Point); NCB_SUBCLASS(RectF,BevyDraw::Rect);
    NCB_SUBCLASS(Matrix,BevyDraw::Matrix); NCB_SUBCLASS(Path,BevyDraw::Path);
    NCB_SUBCLASS(Image,BevyDraw::Image);
    Variant(TJS_W("MatrixOrderPrepend"),tTJSVariant(0)); Variant(TJS_W("MatrixOrderAppend"),tTJSVariant(1));
    Variant(TJS_W("Ok"),tTJSVariant(0)); Variant(TJS_W("InvalidParameter"),tTJSVariant(2));
    RawCallback("getFontList",BevyGdiPlus::getFontList,0);
    RawCallback("addPrivateFont",BevyGdiPlus::addPrivateFont,0);
    Variant(TJS_W("FontStyleRegular"),tTJSVariant(0)); Variant(TJS_W("FontStyleBold"),tTJSVariant(1)); Variant(TJS_W("FontStyleItalic"),tTJSVariant(2));
    Variant(TJS_W("FontStyleUnderline"),tTJSVariant(4)); Variant(TJS_W("FontStyleStrikeout"),tTJSVariant(8)); Variant(TJS_W("BrushTypeSolidColor"),tTJSVariant(0)); Variant(TJS_W("BrushTypeLinearGradient"),tTJSVariant(4));
}
NCB_ATTACH_CLASS(BevyDrawLayer,Layer) {
    RawCallback("clear",BevyDraw::Layer::clear,0);
    RawCallback("drawPath",BevyDraw::shapeOperation<0>,0);
    RawCallback("drawRectangle",BevyDraw::shapeOperation<1>,0); RawCallback("drawEllipse",BevyDraw::shapeOperation<2>,0);
    RawCallback("drawLine",BevyDraw::shapeOperation<3>,0); RawCallback("drawArc",BevyDraw::shapeOperation<4>,0);
    RawCallback("drawPie",BevyDraw::shapeOperation<5>,0); RawCallback("drawBezier",BevyDraw::shapeOperation<6>,0);
    RawCallback("drawBeziers",BevyDraw::shapeOperation<7>,0); RawCallback("drawLines",BevyDraw::shapeOperation<8>,0);
    RawCallback("drawPolygon",BevyDraw::shapeOperation<9>,0); RawCallback("drawCurve",BevyDraw::shapeOperation<10>,0);
    RawCallback("drawCurve2",BevyDraw::shapeOperation<11>,0); RawCallback("drawClosedCurve2",BevyDraw::shapeOperation<12>,0);
    RawCallback("drawCurve3",BevyDraw::shapeOperation<13>,0); RawCallback("drawClosedCurve",BevyDraw::shapeOperation<14>,0);
    RawCallback("drawRectangles",BevyDraw::shapeOperation<15>,0);
    RawCallback("drawImage",BevyDraw::imageOperation<0>,0); RawCallback("drawImageRect",BevyDraw::imageOperation<1>,0);
    RawCallback("drawImageStretch",BevyDraw::imageOperation<2>,0); RawCallback("drawImageAffine",BevyDraw::imageOperation<3>,0);
    RawCallback("drawString",BevyDraw::textOperation<false>,0); RawCallback("drawPathString",BevyDraw::textOperation<true>,0);
    RawCallback("measureString",BevyDraw::Layer::measure,0); RawCallback("measureStringInternal",BevyDraw::Layer::measureInternal,0);
    RawCallback("saveImage",BevyDraw::Readback::saveImage,0); RawCallback("getColorRegionRects",BevyDraw::Readback::regions,0);
}
NCB_GET_INSTANCE_HOOK(BevyDrawState) {
    NCB_INSTANCE_GETTER(objthis) { return BevyDraw::DrawState::get(objthis); }
};
NCB_ATTACH_CLASS_WITH_HOOK(BevyDrawState,Layer) {
    NCB_PROPERTY(updateWhenDraw,getUpdate,setUpdate);
    NCB_PROPERTY(smoothingMode,getSmoothing,setSmoothing); NCB_PROPERTY(textRenderingHint,getTextHint,setTextHint);
    NCB_METHOD(setTransform); NCB_METHOD(resetTransform); NCB_METHOD(rotateTransform);
    NCB_METHOD(scaleTransform); NCB_METHOD(translateTransform);
    NCB_METHOD(setViewTransform); NCB_METHOD(resetViewTransform); NCB_METHOD(rotateViewTransform);
    NCB_METHOD(scaleViewTransform); NCB_METHOD(translateViewTransform);
    NCB_PROPERTY(record,getRecord,setRecord); NCB_METHOD(redrawRecord); NCB_METHOD(getRecordImage);
    NCB_METHOD(saveRecord); NCB_METHOD(loadRecord);
}
