// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.

struct Image {
    std::shared_ptr<tTVPBaseTexture> texture;
    double dpiX=96,dpiY=96;
    mutable int status=0;
    Image()=default;
    explicit Image(std::shared_ptr<tTVPBaseTexture> value):texture(std::move(value)) {}
    void load(ttstr name) {
        auto value=std::make_shared<tTVPBaseTexture>(1,1);
        iTJSDispatch2 *metadata=nullptr;
        TVPLoadGraphic(value.get(),name,TVP_clNone,0,0,glmNormal,nullptr,&metadata);
        double x=96,y=96;
        if(metadata) {
            tTJSVariant info(metadata,metadata); metadata->Release();
            if(ttstr(member(info,TJS_W("reso_unit")))==TJS_W("meter")) {
                x=number(info,TJS_W("reso_x"),96/0.0254)*0.0254;
                y=number(info,TJS_W("reso_y"),96/0.0254)*0.0254;
            }
        }
        dpiX=x; dpiY=y;
        texture=std::move(value);
    }
    static tjs_error TJS_INTF_METHOD create(Image **result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *) {
        if(count>1) return TJS_E_BADPARAMCOUNT;
        auto value=std::make_unique<Image>();
        if(count) value->load(ttstr(*p[0]));
        *result=value.release(); return TJS_S_OK;
    }
    unsigned GetWidth() const { return texture ? texture->GetWidth() : 0; }
    unsigned GetHeight() const { return texture ? texture->GetHeight() : 0; }
    double GetHorizontalResolution() const { return dpiX; }
    double GetVerticalResolution() const { return dpiY; }
    int GetType() const { return texture ? 1 : 0; }
    int GetPixelFormat() const { return 0x26200a; }
    int GetFlags() const { return texture ? 2 : 0; }
    int GetLastStatus() const { int result=status; status=0; return result; }
    tTJSVariant Clone() const {
        auto copy=new Image(*this); if(texture) copy->texture=std::make_shared<tTVPBaseTexture>(*texture);
        return boxed(copy);
    }
    tTJSVariant GetBounds() const { return boxed(new Rect(0,0,GetWidth(),GetHeight())); }
    static std::shared_ptr<tTVPBaseTexture> read(const tTJSVariant &value) {
        if(value.Type()==tvtString) {
            Image image; image.load(ttstr(value)); return image.texture;
        }
        if(value.Type()==tvtObject) {
            auto object=value.AsObjectNoAddRef();
            auto image=ncbInstanceAdaptor<Image>::GetNativeInstance(object);
            if(image && image->texture) return image->texture;
            tTJSNI_BaseLayer *layer=nullptr;
            if(object && TJS_SUCCEEDED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,tTJSNC_Layer::ClassID,reinterpret_cast<iTJSNativeInstance**>(&layer))) && layer && layer->GetMainImage())
                return std::make_shared<tTVPBaseTexture>(*layer->GetMainImage());
        }
        TVPThrowExceptionMessage(TJS_W("Expected an image, Layer or Storage image name"));
        return nullptr;
    }
    int RotateFlip(int type) {
        if(type<0 || type>7 || !texture) { status=2; return 2; }
        unsigned width=GetWidth(),height=GetHeight();
        auto output=std::make_shared<tTVPBaseTexture>(type%2 ? height : width,type%2 ? width : height);
        double transforms[4][6]={{1,0,0,1,0,0},{0,1,-1,0,double(height),0},
                                 {-1,0,0,-1,double(width),double(height)},{0,-1,1,0,0,double(width)}};
        Matrix matrix; std::copy(transforms[type%4],transforms[type%4]+6,matrix.elements.begin());
        if(type>=4) { matrix.scale(-1,1,1); matrix.translate(output->GetWidth(),0,1); }
        std::array<GeometryPoint,4> points={matrix.apply({0,0}),matrix.apply({double(width),0}),
            matrix.apply({double(width),double(height)}),matrix.apply({0,double(height)})};
        TVPBevyDrawPathElement path[5]{};
        for(int i=0;i<4;++i) { path[i].verb=i ? 1 : 0; path[i].points[0]=points[i].x; path[i].points[1]=points[i].y; }
        path[4].verb=3;
        TVPBevyDrawPaint paint{};
        paint.brush_type=2; paint.image=TVPBevyTextureID(texture->GetTexture()); paint.wrap=4;
        paint.coordinates[2]=width; paint.coordinates[3]=height;
        std::copy(matrix.elements.begin(),matrix.elements.end(),paint.paint_transform);
        TVPBevyLayerExDraw operation{}; operation.target=TVPBevyTextureID(output->GetTexture());
        operation.clip[2]=output->GetWidth(); operation.clip[3]=output->GetHeight();
        operation.transform[0]=operation.transform[3]=1;
        operation.path=path; operation.path_count=5; operation.paints=&paint; operation.paint_count=1;
        if(krkr_layer_ex_draw(&operation)) { status=1; return 1; }
        if(type%2) std::swap(dpiX,dpiY);
        texture=std::move(output); return 0;
    }
};
