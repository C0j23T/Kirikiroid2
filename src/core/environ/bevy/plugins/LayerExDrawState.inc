// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.

struct DrawState {
    iTJSDispatch2 *owner;
    Matrix transform,viewTransform;
    bool updateWhenDraw=true;
    int smoothingMode=4,textRenderingHint=4;
    bool recording=false;
    std::shared_ptr<DrawRecord> record;
    explicit DrawState(iTJSDispatch2 *owner):owner(owner) {}
    bool getRecord() const { return recording; }
    void setRecord(bool value);
    void clearRecord(tjs_uint32 background);
    bool redrawRecord();
    tTJSVariant getRecordImage();
    bool saveRecord(ttstr name);
    bool loadRecord(ttstr name);
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
    void setViewTransform(tTJSVariant value) { viewTransform=Matrix::read(value); if(recording) redrawRecord(); }
    void resetViewTransform() { viewTransform.reset(); if(recording) redrawRecord(); }
    void rotateViewTransform(double value) { Path::require(viewTransform.rotate(value,1)==0); if(recording) redrawRecord(); }
    void scaleViewTransform(double x,double y) { Path::require(viewTransform.scale(x,y,1)==0); if(recording) redrawRecord(); }
    void translateViewTransform(double x,double y) { Path::require(viewTransform.translate(x,y,1)==0); if(recording) redrawRecord(); }
    static DrawState *get(iTJSDispatch2 *obj) {
        auto state=ncbInstanceAdaptor<DrawState>::GetNativeInstance(obj);
        if(!state) {
            state=new DrawState(obj);
            if(!ncbInstanceAdaptor<DrawState>::SetAdaptorWithNativeInstance(obj,state)) {
                delete state; TVPThrowExceptionMessage(TJS_W("Could not attach drawing state"));
            }
        }
        return state;
    }
};

static tjs_error drawPath(tTJSVariant *result,iTJSDispatch2 *obj,tTJSNI_BaseLayer *layer,const Path &path,const Appearance &appearance,int aliasing=-1) {
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
    operation.flags=aliasing<0 ? state->smoothingMode==0 || state->smoothingMode==1 || state->smoothingMode==3 : aliasing;
    layer->IndependMainImage();
    auto image=layer->GetMainImage(); operation.target=TVPBevyTextureID(image->GetTexture());
    if(krkr_layer_ex_draw(&operation)) TVPThrowExceptionMessage(TJS_W("LayerExDraw GPU operation failed"));
    if(state->recording) {
        std::copy(state->transform.elements.begin(),state->transform.elements.end(),operation.transform);
        if(krkr_layer_ex_record_append(state->record->id,&operation,state->smoothingMode))
            TVPThrowExceptionMessage(TJS_W("Could not append drawing record"));
    }
    TVPBevyInvalidateTexture(image->GetTexture()); layer->SetImageModified(true);
    if(state->updateWhenDraw) layer->Update();
    if(result) *result=rect(operation.bounds[0],operation.bounds[1],operation.bounds[2],operation.bounds[3]);
    return TJS_S_OK;
}

static tTJSNI_BaseLayer *drawingLayer(iTJSDispatch2 *obj) {
    tTJSNI_BaseLayer *layer=nullptr;
    if(!obj || TJS_FAILED(obj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,tTJSNC_Layer::ClassID,reinterpret_cast<iTJSNativeInstance**>(&layer))) || !layer || !layer->GetMainImage())
        TVPThrowExceptionMessage(TJS_W("Layer image is required"));
    return layer;
}

static void clearDrawingLayer(tTJSNI_BaseLayer *layer,tjs_uint32 color) {
    layer->IndependMainImage(); auto image=layer->GetMainImage();
    TVPBevyRenderCommand command{}; command.operation=4; command.method="FillARGB";
    command.target=TVPBevyTextureID(image->GetTexture()); command.rect[2]=image->GetWidth(); command.rect[3]=image->GetHeight();
    for(int i=0;i<4;++i) { int shift=i==0 ? 16 : i==1 ? 8 : i==2 ? 0 : 24; command.parameters[12+i]=double((color>>shift)&255)/255; }
    if(krkr_render_dispatch(&command)) TVPThrowExceptionMessage(TJS_W("Layer clear failed"));
    TVPBevyInvalidateTexture(image->GetTexture()); layer->SetImageModified(true);
}

void DrawState::clearRecord(tjs_uint32 background) {
    if(!recording) return;
    auto image=drawingLayer(owner)->GetMainImage();
    uint64_t id=0;
    if(krkr_layer_ex_record_create(image->GetWidth(),image->GetHeight(),background,&id))
        TVPThrowExceptionMessage(TJS_W("Could not create drawing record"));
    record=std::make_shared<DrawRecord>(id);
}

void DrawState::setRecord(bool value) {
    if(recording==value) return;
    if(value) {
        recording=true;
        try { clearRecord(0); } catch(...) { recording=false; throw; }
    } else { record.reset(); recording=false; }
}

bool DrawState::redrawRecord() {
    if(!recording || !record) return false;
    auto layer=drawingLayer(owner); clearDrawingLayer(layer,0);
    auto clip=layer->GetClip(); int32_t rect[4]={clip.left,clip.top,clip.right,clip.bottom};
    auto type=layer->GetType(); unsigned alpha=type==ltAddAlpha ? 1 : type==ltOpaque ? 2 : 0;
    if(krkr_layer_ex_record_replay(record->id,TVPBevyTextureID(layer->GetMainImage()->GetTexture()),rect,viewTransform.elements.data(),alpha))
        TVPThrowExceptionMessage(TJS_W("Could not replay drawing record"));
    TVPBevyInvalidateTexture(layer->GetMainImage()->GetTexture()); layer->SetImageModified(true);
    if(updateWhenDraw) layer->Update();
    return true;
}

tTJSVariant DrawState::getRecordImage() {
    if(!recording || !record) return tTJSVariant();
    uint64_t id=0;
    if(krkr_layer_ex_record_clone(record->id,&id)) TVPThrowExceptionMessage(TJS_W("Could not clone drawing record"));
    return boxed(new Image(std::make_shared<DrawRecord>(id)));
}

bool DrawState::saveRecord(ttstr name) {
    if(!recording || !record) return false;
    void *bytes=nullptr; size_t length=0;
    if(krkr_layer_ex_record_encode(record->id,&bytes,&length)) TVPThrowExceptionMessage(TJS_W("Could not encode EMF+ record"));
    try {
        std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(name,TJS_BS_WRITE));
        stream->WriteBuffer(bytes,length);
    } catch(...) { krkr_layer_ex_record_free(bytes,length); throw; }
    krkr_layer_ex_record_free(bytes,length); return true;
}

bool DrawState::loadRecord(ttstr name) {
    std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(name,TJS_BS_READ));
    auto length=stream->GetSize();
    if(length>256*1024*1024) TVPThrowExceptionMessage(TJS_W("Metafile is too large"));
    std::vector<unsigned char> bytes(static_cast<size_t>(length)); stream->ReadBuffer(bytes.data(),bytes.size());
    uint64_t id=0;
    if(krkr_layer_ex_record_decode(bytes.data(),bytes.size(),&id)) TVPThrowExceptionMessage(TJS_W("Could not decode EMF+ record"));
    auto previous=record; bool wasRecording=recording;
    record=std::make_shared<DrawRecord>(id); recording=true;
    try { redrawRecord(); } catch(...) { record=previous; recording=wasRecording; throw; }
    return true;
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
    std::unique_ptr<Image> loaded;
    Image *native=nullptr;
    if(p[source]->Type()==tvtObject) native=ncbInstanceAdaptor<Image>::GetNativeInstance(p[source]->AsObjectNoAddRef());
    else if(p[source]->Type()==tvtString) { loaded=std::make_unique<Image>(); loaded->load(ttstr(*p[source])); native=loaded.get(); }
    std::shared_ptr<tTVPBaseTexture> image;
    if(!native || !native->record) image=native && native->texture ? native->texture : Image::read(*p[source]);
    GeometryRect crop{0,0,native ? double(native->GetWidth()) : double(image->GetWidth()),native ? double(native->GetHeight()) : double(image->GetHeight())};
    if(native && native->record) { auto info=native->record->info(); crop={info.bounds[0],info.bounds[1],info.bounds[2],info.bounds[3]}; }
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
    if(native && native->record) {
        auto state=DrawState::get(obj);
        if(state->recording) TVPThrowExceptionMessage(TJS_W("Recording nested metafile images is not supported"));
        Matrix offset; offset.translate(-crop.x,-crop.y,1);
        auto view=BevyDrawGeometry::Matrix::product(BevyDrawGeometry::Matrix::product(offset,matrix),
            BevyDrawGeometry::Matrix::product(state->transform,state->viewTransform));
        auto clip=layer->GetClip(); int32_t clipRect[4]={clip.left,clip.top,clip.right,clip.bottom};
        double sourceRect[4]={crop.x,crop.y,crop.width,crop.height};
        auto type=layer->GetType(); unsigned alpha=type==ltAddAlpha ? 1 : type==ltOpaque ? 2 : 0;
        layer->IndependMainImage(); auto target=layer->GetMainImage();
        if(krkr_layer_ex_record_draw(native->record->id,TVPBevyTextureID(target->GetTexture()),clipRect,view.elements.data(),alpha,sourceRect))
            TVPThrowExceptionMessage(TJS_W("Could not draw metafile image"));
        TVPBevyInvalidateTexture(target->GetTexture()); layer->SetImageModified(true); if(state->updateWhenDraw) layer->Update();
        auto a=view.apply({crop.x,crop.y}),b=view.apply({crop.right(),crop.y}),c=view.apply({crop.right(),crop.bottom()}),d=view.apply({crop.x,crop.bottom()});
        double left=std::min({a.x,b.x,c.x,d.x}),top=std::min({a.y,b.y,c.y,d.y});
        if(result) *result=rect(left,top,std::max({a.x,b.x,c.x,d.x})-left,std::max({a.y,b.y,c.y,d.y})-top);
        return TJS_S_OK;
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
