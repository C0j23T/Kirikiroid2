// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.

struct DrawState {
    Matrix transform,viewTransform;
    bool updateWhenDraw=true;
    int smoothingMode=4,textRenderingHint=4;
    bool getUpdate() const { return updateWhenDraw; }
    void setUpdate(bool value) { updateWhenDraw=value; }
    int getSmoothing() const { return smoothingMode; }
    void setSmoothing(int value) {
        if(value<0 || value>6) TVPThrowExceptionMessage(TJS_W("Invalid smoothing mode"));
        smoothingMode=value;
    }
    int getTextHint() const { return textRenderingHint; }
    void setTextHint(int value) {
        if(value<0 || value>5) TVPThrowExceptionMessage(TJS_W("Invalid text rendering hint"));
        textRenderingHint=value;
    }
    void setTransform(tTJSVariant value) { transform=Matrix::read(value); }
    void resetTransform() { transform.reset(); }
    void rotateTransform(double value) { Path::require(transform.rotate(value,1)==0); }
    void scaleTransform(double x,double y) { Path::require(transform.scale(x,y,1)==0); }
    void translateTransform(double x,double y) { Path::require(transform.translate(x,y,1)==0); }
    void setViewTransform(tTJSVariant value) { viewTransform=Matrix::read(value); }
    void resetViewTransform() { viewTransform.reset(); }
    void rotateViewTransform(double value) { Path::require(viewTransform.rotate(value,1)==0); }
    void scaleViewTransform(double x,double y) { Path::require(viewTransform.scale(x,y,1)==0); }
    void translateViewTransform(double x,double y) { Path::require(viewTransform.translate(x,y,1)==0); }
    static DrawState *get(iTJSDispatch2 *obj) {
        auto state=ncbInstanceAdaptor<DrawState>::GetNativeInstance(obj);
        if(!state) {
            state=new DrawState;
            if(!ncbInstanceAdaptor<DrawState>::SetAdaptorWithNativeInstance(obj,state)) {
                delete state; TVPThrowExceptionMessage(TJS_W("Could not attach drawing state"));
            }
        }
        return state;
    }
};

static tjs_error drawPath(tTJSVariant *result,iTJSDispatch2 *obj,tTJSNI_BaseLayer *layer,const Path &path,const Appearance &appearance) {
    auto state=DrawState::get(obj);
    if(path.commands.size()>1000000 || appearance.paints.size()>4096)
        TVPThrowExceptionMessage(TJS_W("Drawing operation is too large"));
    if(path.commands.empty() || appearance.paints.empty()) {
        if(result) *result=rect(0,0,0,0);
        return TJS_S_OK;
    }
    std::vector<TVPBevyDrawPathElement> elements;
    elements.reserve(path.commands.size());
    for(const auto &command:path.commands) {
        TVPBevyDrawPathElement element{};
        element.verb=static_cast<unsigned>(command.verb);
        for(int i=0;i<6;++i) {
            if(!std::isfinite(command.points[i]) || std::abs(command.points[i])>1000000000)
                TVPThrowExceptionMessage(TJS_W("Invalid drawing coordinate"));
            element.points[i]=command.points[i];
        }
        elements.push_back(element);
    }
    std::vector<TVPBevyDrawPaint> paints;
    paints.reserve(appearance.paints.size());
    for(const auto &paint:appearance.paints) paints.push_back(paint.operation());
    TVPBevyLayerExDraw operation{};
    auto matrix=BevyDrawGeometry::Matrix::product(state->transform,state->viewTransform);
    std::copy(matrix.elements.begin(),matrix.elements.end(),operation.transform);
    auto clip=layer->GetClip();
    operation.clip[0]=clip.left; operation.clip[1]=clip.top; operation.clip[2]=clip.right; operation.clip[3]=clip.bottom;
    operation.path=elements.data(); operation.path_count=elements.size(); operation.paints=paints.data(); operation.paint_count=paints.size();
    auto type=layer->GetType();
    operation.alpha_mode=type==ltAddAlpha ? 1 : type==ltOpaque ? 2 : 0;
    operation.flags=state->smoothingMode==0 || state->smoothingMode==1 || state->smoothingMode==3;
    layer->IndependMainImage();
    auto image=layer->GetMainImage(); operation.target=TVPBevyTextureID(image->GetTexture());
    if(krkr_layer_ex_draw(&operation)) TVPThrowExceptionMessage(TJS_W("LayerExDraw GPU operation failed"));
    TVPBevyInvalidateTexture(image->GetTexture()); layer->SetImageModified(true);
    if(state->updateWhenDraw) layer->Update();
    if(result) *result=rect(operation.bounds[0],operation.bounds[1],operation.bounds[2],operation.bounds[3]);
    return TJS_S_OK;
}

template<int Kind> static tjs_error TJS_INTF_METHOD shapeOperation(tTJSVariant *result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *obj) {
    constexpr int required=Kind==0 ? 2 : Kind==1 || Kind==2 || Kind==3 ? 5 : Kind==4 || Kind==5 ? 7 : Kind==6 ? 9 : Kind==7 || Kind==8 || Kind==9 || Kind==10 ? 2 : Kind==11 || Kind==12 ? 3 : Kind==13 ? 5 : 2;
    if(count<required) return TJS_E_BADPARAMCOUNT;
    if(p[0]->Type()!=tvtObject) return TJS_E_INVALIDPARAM;
    auto appearance=ncbInstanceAdaptor<Appearance>::GetNativeInstance(p[0]->AsObjectNoAddRef());
    if(!appearance) return TJS_E_INVALIDPARAM;
    tTJSNI_BaseLayer *layer=nullptr;
    if(!obj || TJS_FAILED(obj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,tTJSNC_Layer::ClassID,reinterpret_cast<iTJSNativeInstance**>(&layer))) || !layer || !layer->GetMainImage())
        return TJS_E_INVALIDPARAM;
    Path path;
    if constexpr(Kind==0) {
        if(p[1]->Type()!=tvtObject) return TJS_E_INVALIDPARAM;
        auto source=ncbInstanceAdaptor<Path>::GetNativeInstance(p[1]->AsObjectNoAddRef());
        if(!source) return TJS_E_INVALIDPARAM;
        path=*source;
    } else if constexpr(Kind>=1 && Kind<=6) {
        double a[8]{};
        for(int i=1;i<required;++i) a[i-1]=coordinate(*p[i]);
        if constexpr(Kind==1) path.drawRectangle(a[0],a[1],a[2],a[3]);
        else if constexpr(Kind==2) path.drawEllipse(a[0],a[1],a[2],a[3]);
        else if constexpr(Kind==3) path.drawLine(a[0],a[1],a[2],a[3]);
        else if constexpr(Kind==4) path.drawArc(a[0],a[1],a[2],a[3],a[4],a[5]);
        else if constexpr(Kind==5) path.drawPie(a[0],a[1],a[2],a[3],a[4],a[5]);
        else path.drawBezier(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);
    } else if constexpr(Kind==7) path.drawBeziers(*p[1]);
    else if constexpr(Kind==8) path.drawLines(*p[1]);
    else if constexpr(Kind==9) path.drawPolygon(*p[1]);
    else if constexpr(Kind==10) path.drawCurve(*p[1]);
    else if constexpr(Kind==11) path.drawCurve2(*p[1],coordinate(*p[2]));
    else if constexpr(Kind==12) path.drawClosedCurve2(*p[1],coordinate(*p[2]));
    else if constexpr(Kind==13) path.drawCurve3(*p[1],p[2]->AsInteger(),p[3]->AsInteger(),coordinate(*p[4]));
    else if constexpr(Kind==14) path.drawClosedCurve(*p[1]);
    else path.drawRectangles(*p[1]);
    return drawPath(result,obj,layer,path,*appearance);
}

template<int Kind> static tjs_error TJS_INTF_METHOD imageOperation(tTJSVariant *result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *obj) {
    constexpr int required=Kind==0 ? 3 : Kind==1 ? 7 : Kind==2 ? 9 : 12;
    if(count<required) return TJS_E_BADPARAMCOUNT;
    tTJSNI_BaseLayer *layer=nullptr;
    if(!obj || TJS_FAILED(obj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,tTJSNC_Layer::ClassID,reinterpret_cast<iTJSNativeInstance**>(&layer))) || !layer || !layer->GetMainImage())
        return TJS_E_INVALIDPARAM;
    constexpr int source=Kind==0 || Kind==1 ? 2 : Kind==2 ? 4 : 0;
    auto image=Image::read(*p[source]);
    GeometryRect crop{0,0,double(image->GetWidth()),double(image->GetHeight())};
    if constexpr(Kind>0) {
        constexpr int first=Kind==1 ? 3 : Kind==2 ? 5 : 1;
        crop={coordinate(*p[first]),coordinate(*p[first+1]),coordinate(*p[first+2]),coordinate(*p[first+3])};
    }
    if(crop.width<=0 || crop.height<=0) return TJS_E_INVALIDPARAM;
    Matrix matrix;
    if constexpr(Kind==0 || Kind==1) matrix.translate(coordinate(*p[0]),coordinate(*p[1]),1);
    else if constexpr(Kind==2) {
        matrix.scale(coordinate(*p[2])/crop.width,coordinate(*p[3])/crop.height,1);
        matrix.translate(coordinate(*p[0]),coordinate(*p[1]),1);
    } else {
        double a[6]; for(int i=0;i<6;++i) a[i]=coordinate(*p[i+6]);
        if(p[5]->AsInteger()) std::copy(a,a+6,matrix.elements.begin());
        else matrix.elements={(a[2]-a[0])/crop.width,(a[3]-a[1])/crop.width,
                              (a[4]-a[0])/crop.height,(a[5]-a[1])/crop.height,a[0],a[1]};
    }
    Path path;
    auto a=matrix.apply({0,0}),b=matrix.apply({crop.width,0}),c=matrix.apply({crop.width,crop.height}),d=matrix.apply({0,crop.height});
    path.move(a); path.line(b); path.line(c); path.line(d); path.closeFigure();
    Matrix sourceOffset; sourceOffset.translate(-crop.x,-crop.y,1);
    auto mapped=BevyDrawGeometry::Matrix::product(sourceOffset,matrix);
    Appearance appearance;
    Paint paint{}; paint.image=image; paint.x1=crop.x; paint.y1=crop.y; paint.x2=crop.width; paint.y2=crop.height;
    paint.wrap=4; paint.matrix.elements=mapped.elements; appearance.paints.push_back(std::move(paint));
    return drawPath(result,obj,layer,path,appearance);
}
