// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.

struct TextOutline {
    Path path;
    double originX=0,originY=0,advance=0,lineHeight=0;
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
    TextOutline(const Font &font,const ttstr &text,double x,double y,int hint) {
        tFreeTypeFace holder(font.resolvedFamily(),font.native().Flags);
        holder.SetHeight(static_cast<int>(font.size));
        auto face=holder.GetBaseFace()->GetFTFace();
        originX=x; originY=y+holder.GetAscent(); lineHeight=face->size->metrics.height/64.0;
        FT_Int32 loadFlags=FT_LOAD_NO_BITMAP;
        if(hint==2 || hint==4) loadFlags|=FT_LOAD_NO_HINTING|FT_LOAD_NO_AUTOHINT;
        if(hint==1 || hint==2) loadFlags|=FT_LOAD_TARGET_MONO;
        FT_Outline_Funcs funcs{move,line,conic,cubic,0,0};
        auto characters=text.c_str();
        double lineStart=x,maxAdvance=0;
        for(int i=0;i<text.GetLen();++i) {
            unsigned code=characters[i];
            if(code>=0xd800 && code<=0xdbff && i+1<text.GetLen() && characters[i+1]>=0xdc00 && characters[i+1]<=0xdfff)
                code=0x10000+((code-0xd800)<<10)+(characters[++i]-0xdc00);
            if(code=='\r') continue;
            if(code=='\n') {
                decorate(font,holder,lineStart,y,maxAdvance);
                originX=x; y+=lineHeight; originY=y+holder.GetAscent(); continue;
            }
            auto glyph=FT_Get_Char_Index(face,code);
            if(!glyph) glyph=FT_Get_Char_Index(face,holder.GetDefaultChar());
            if(FT_Load_Glyph(face,glyph,loadFlags)) TVPThrowExceptionMessage(TJS_W("Could not load glyph outline"));
            if(font.style&1) FT_GlyphSlot_Embolden(face->glyph);
            if(font.style&2) FT_GlyphSlot_Oblique(face->glyph);
            if(face->glyph->format==FT_GLYPH_FORMAT_OUTLINE && face->glyph->outline.n_points) {
                if(FT_Outline_Decompose(&face->glyph->outline,&funcs,this)) TVPThrowExceptionMessage(TJS_W("Could not decompose glyph outline"));
                path.closeFigure();
            }
            originX+=face->glyph->advance.x/64.0;
            maxAdvance=std::max(maxAdvance,originX-x);
        }
        decorate(font,holder,lineStart,y,maxAdvance); advance=maxAdvance;
    }
    void decorate(const Font &font,const tFreeTypeFace &holder,double x,double y,double width) {
        int position=0,thickness=0;
        if(font.style&4) { holder.GetUnderline(position,thickness); path.addRectangle({x,y+position,width,double(thickness)}); }
        if(font.style&8) { holder.GetStrikeOut(position,thickness); path.addRectangle({x,y+position,width,double(thickness)}); }
    }
};

template<bool Outline> static tjs_error TJS_INTF_METHOD textOperation(tTJSVariant *result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *obj) {
    if(count<5) return TJS_E_BADPARAMCOUNT;
    if(p[0]->Type()!=tvtObject || p[1]->Type()!=tvtObject) return TJS_E_INVALIDPARAM;
    auto font=ncbInstanceAdaptor<Font>::GetNativeInstance(p[0]->AsObjectNoAddRef());
    auto app=ncbInstanceAdaptor<Appearance>::GetNativeInstance(p[1]->AsObjectNoAddRef());
    if(!font || !app) return TJS_E_INVALIDPARAM;
    tTJSNI_BaseLayer *layer=nullptr;
    if(!obj || TJS_FAILED(obj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,tTJSNC_Layer::ClassID,reinterpret_cast<iTJSNativeInstance**>(&layer))) || !layer || !layer->GetMainImage()) return TJS_E_INVALIDPARAM;
    auto state=DrawState::get(obj);
    TextOutline text(*font,ttstr(*p[4]),coordinate(*p[2]),coordinate(*p[3]),state->textRenderingHint);
    if constexpr(Outline) return drawPath(result,obj,layer,text.path,*app);
    else {
        Appearance brushes;
        for(const auto &paint:app->paints) if(!paint.pen) { brushes.paints.push_back(paint); break; }
        return drawPath(result,obj,layer,text.path,brushes);
    }
}
