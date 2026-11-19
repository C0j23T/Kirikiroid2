// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.

struct TextOutline {
    Path path;
    double originX=0,originY=0,advance=0,lineHeight=0,height=0;
    GeometryRect bounds{};
    GeometryPoint convert(const FT_Vector &p) const { return {originX+p.x/64.0,originY-p.y/64.0}; }
    static int move(const FT_Vector *to,void *user) {
        auto &self=*static_cast<TextOutline*>(user);
        self.path.closeFigure(); self.path.move(self.convert(*to)); return 0;
    }
    static int line(const FT_Vector *to,void *user) {
        auto &self=*static_cast<TextOutline*>(user); self.path.line(self.convert(*to)); return 0;
    }
    static int conic(const FT_Vector *control,const FT_Vector *to,void *user) {
        auto &self=*static_cast<TextOutline*>(user);
        auto a=self.path.current,b=self.convert(*control),c=self.convert(*to);
        self.path.cubic({a.x+(b.x-a.x)*2/3,a.y+(b.y-a.y)*2/3},
                        {c.x+(b.x-c.x)*2/3,c.y+(b.y-c.y)*2/3},c); return 0;
    }
    static int cubic(const FT_Vector *a,const FT_Vector *b,const FT_Vector *to,void *user) {
        auto &self=*static_cast<TextOutline*>(user);
        self.path.cubic(self.convert(*a),self.convert(*b),self.convert(*to)); return 0;
    }
    GeometryRect layout{},internal{};
    TextOutline(const Font &font,const ttstr &text,double x,double y,int hint) {
        const auto &holder=font.face();
        lineHeight=holder.lineSpacing();
        double margin=font.forceSelfPathDraw ? std::floor(0.167*font.size) : font.size/6;
        double left=x+margin-(font.forceSelfPathDraw ? 0.5 : 0);
        double top=y-(font.forceSelfPathDraw ? 0.5 : 0);
        FT_Int32 loadFlags=FT_LOAD_NO_BITMAP;
        if(hint==2 || hint==4) loadFlags|=FT_LOAD_NO_HINTING|FT_LOAD_NO_AUTOHINT;
        if(hint==1 || hint==2) loadFlags|=FT_LOAD_TARGET_MONO;
        const FT_Outline_Funcs funcs{move,line,conic,cubic,0,0};
        int length=text.GetLen(), lines=0;
        std::vector<DrawingFace> faces;
        auto addFace=[&](const PrivateFontData &data) {
            faces.push_back({data.bytes->data(),data.bytes->size(),static_cast<uint32_t>(data.index)});
        };
        addFace(*holder.data);
        for(auto &face:privateFonts()) if(face!=holder.data) addFace(*face);
        for(int start=0;start<length;) {
            int end=start;
            while(end<length && text[end]!='\r' && text[end]!='\n') ++end;
            double baseline=top+lines*lineHeight+holder.ascent();
            double pen=left;
            if(end>start) {
                auto render=[&](const unsigned char *bytes,size_t size,unsigned index,const PositionedGlyph *glyphs,size_t count) {
                    std::unique_ptr<FontFace> fallback;
                    if(bytes!=faces[0].bytes || index!=faces[0].index)
                        fallback=std::make_unique<FontFace>(bytes,size,index,font.size,font.style);
                    const auto &face=fallback ? *fallback : holder;
                    for(size_t i=0;i<count;++i) {
                        const auto &g=glyphs[i];
                        originX=pen+face.scale(g.xOffset);
                        originY=baseline-face.scale(g.yOffset);
                        face.load(g.glyph,loadFlags);
                        auto slot=face.face->glyph;
                        if(slot->format==FT_GLYPH_FORMAT_OUTLINE && slot->outline.n_points) {
                            if(FT_Outline_Decompose(&slot->outline,&funcs,this))
                                TVPThrowExceptionMessage(TJS_W("Could not decompose glyph outline"));
                            path.closeFigure();
                        }
                        pen+=face.scale(g.advance);
                    }
                };
                struct Run { decltype(render) &visit; std::exception_ptr error; } run{render,{}};
                auto visit=[](void *context,const unsigned char *bytes,size_t size,uint32_t index,const PositionedGlyph *glyphs,size_t count) noexcept {
                    auto &run=*static_cast<Run*>(context);
                    try { run.visit(bytes,size,index,glyphs,count); return true; }
                    catch(...) { run.error=std::current_exception(); return false; }
                };
                if(!krkr_host_shape_line(faces.data(),faces.size(),text.c_str()+start,end-start,font.style,&run,visit)) {
                    if(run.error) std::rethrow_exception(run.error);
                    TVPThrowExceptionMessage(TJS_W("Could not shape text"));
                }
            }
            double width=pen-left;
            decorate(font,holder,left,top+lines*lineHeight,width);
            advance=std::max(advance,width);
            ++lines;
            if(end==length) break;
            if(text[end]=='\r' && end+1<length && text[end+1]=='\n') ++end;
            start=end+1;
        }
        bounds=path.bounds();
        height=lineHeight*lines;
        if(font.forceSelfPathDraw) {
            layout={x,y,bounds.width+0.334*font.size,lineHeight*1.124};
            internal={x+margin,y,bounds.width,lineHeight};
        } else if(lines>0) {
            layout={x,y,advance+2*margin,height};
            internal={left,y,advance,height};
        }
    }
    void decorate(const Font &font,const FontFace &holder,double x,double y,double width) {
        if(width<=0) return;
        double position=0,thickness=0;
        if(font.style&4) { holder.underline(position,thickness); path.addRectangle({x,y+position,width,thickness}); }
        if(font.style&8) { holder.strikeout(position,thickness); path.addRectangle({x,y+position,width,thickness}); }
    }
};

static GeometryRect transformTextRect(GeometryRect r,const BevyDrawGeometry::Matrix &matrix) {
    auto a=matrix.apply({r.x,r.y}), b=matrix.apply({r.right(),r.y});
    auto c=matrix.apply({r.x,r.bottom()}), d=matrix.apply({r.right(),r.bottom()});
    double left=std::min({a.x,b.x,c.x,d.x}), top=std::min({a.y,b.y,c.y,d.y});
    return {left,top,std::max({a.x,b.x,c.x,d.x})-left,std::max({a.y,b.y,c.y,d.y})-top};
}

template<bool Outline> static tjs_error TJS_INTF_METHOD textOperation(tTJSVariant *result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *obj) {
    if(count<5) return TJS_E_BADPARAMCOUNT;
    if(p[0]->Type()!=tvtObject || p[1]->Type()!=tvtObject) return TJS_E_INVALIDPARAM;
    auto font=ncbInstanceAdaptor<Font>::GetNativeInstance(p[0]->AsObjectNoAddRef());
    auto app=ncbInstanceAdaptor<Appearance>::GetNativeInstance(p[1]->AsObjectNoAddRef());
    if(!font || !app) return TJS_E_INVALIDPARAM;
    auto layer=drawingLayer(obj);
    auto state=DrawState::get(obj);
    TextOutline text(*font,ttstr(*p[4]),coordinate(*p[2]),coordinate(*p[3]),state->textRenderingHint);
    if(Outline || font->forceSelfPathDraw) {
        auto status=drawPath(result,obj,layer,text.path,*app);
        if(result && font->forceSelfPathDraw) {
            auto width=number(*result,TJS_W("width"),0)+0.334*font->size;
            *result=rect(text.layout.x,text.layout.y,width,text.layout.height);
        }
        return status;
    }
    Appearance brushes;
    for(const auto &paint:app->paints) if(!paint.pen) { brushes.paints.push_back(paint); break; }
    int aliasing=state->textRenderingHint==1 || state->textRenderingHint==2;
    auto status=drawPath(result,obj,layer,text.path,brushes,aliasing);
    if(result && !brushes.paints.empty() && text.layout.height>0) {
        auto r=text.layout;
        r.x+=brushes.paints[0].ox;
        r.y+=brushes.paints[0].oy;
        r=transformTextRect(r,BevyDrawGeometry::Matrix::product(state->transform,state->viewTransform));
        *result=rect(r.x,r.y,r.width,r.height);
    }
    return status;
}
