// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.

struct DrawingBytes {
    void *data=nullptr;
    size_t size=0;
    ~DrawingBytes() { krkr_png_free(data,size); }
};

struct DrawRecord {
    uint64_t id;
    explicit DrawRecord(uint64_t id):id(id) {}
    ~DrawRecord() { krkr_layer_ex_record_release(id); }
    TVPBevyDrawRecordInfo info() const {
        TVPBevyDrawRecordInfo result{};
        if(krkr_layer_ex_record_query(id,&result)) TVPThrowExceptionMessage(TJS_W("Could not query drawing record"));
        return result;
    }
};

struct Image {
    std::shared_ptr<tTVPBaseTexture> texture;
    std::shared_ptr<DrawRecord> record;
    double dpiX=96,dpiY=96;
    int pixelFormat=0x26200a,flags=2;
    mutable int status=0;
    Image()=default;
    explicit Image(std::shared_ptr<tTVPBaseTexture> value):texture(std::move(value)) {}
    explicit Image(std::shared_ptr<DrawRecord> value):record(std::move(value)) {}
    void load(ttstr name) {
        std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(name,TJS_BS_READ));
        unsigned char header[4]{};
        auto read=stream->Read(header,4);
        if(read==4 && header[0]==1 && header[1]==0 && header[2]==0 && header[3]==0) {
            auto length=stream->GetSize();
            if(length>256*1024*1024) TVPThrowExceptionMessage(TJS_W("Metafile is too large"));
            stream->SetPosition(0); std::vector<unsigned char> bytes(static_cast<size_t>(length)); stream->ReadBuffer(bytes.data(),bytes.size());
            uint64_t id=0;
            if(krkr_layer_ex_record_decode(bytes.data(),bytes.size(),&id)) TVPThrowExceptionMessage(TJS_W("Could not decode metafile image"));
            record=std::make_shared<DrawRecord>(id); texture.reset(); return;
        }
        if(read==4 && ((header[0]==0x89 && !std::memcmp(header+1,"PNG",3)) ||
                (header[0]==0xff && header[1]==0xd8) || !std::memcmp(header,"BM",2) || !std::memcmp(header,"GIF8",4) || !std::memcmp(header,"II*\0",4) ||
                !std::memcmp(header,"MM\0*",4) || !std::memcmp(header,"\0\0\1\0",4))) {
            auto length=stream->GetSize();
            if(length>256*1024*1024) TVPThrowExceptionMessage(TJS_W("Drawing image is too large"));
            std::vector<unsigned char> bytes(static_cast<size_t>(length));
            stream->SetPosition(0); stream->ReadBuffer(bytes.data(),bytes.size());
            KrkrDrawingBitmapInfo info{};
            DrawingBytes pixels;
            if(krkr_drawing_bitmap_decode(bytes.data(),bytes.size(),&info,&pixels.data,&pixels.size))
                TVPThrowExceptionMessage(TJS_W("Could not decode drawing image"));
            auto value=std::make_shared<tTVPBaseTexture>(info.width,info.height);
            value->Update(pixels.data,info.width*4,0,0,info.width,info.height);
            texture=std::move(value); record.reset(); dpiX=info.dpi[0]; dpiY=info.dpi[1];
            pixelFormat=info.pixel_format; flags=info.flags;
            return;
        }
        stream.reset();
        auto value=std::make_shared<tTVPBaseTexture>(1,1);
        TVPLoadGraphic(value.get(),name,TVP_clNone,0,0,glmNormal,nullptr,nullptr);
        dpiX=dpiY=96; pixelFormat=0x26200a; flags=2;
        texture=std::move(value);
        record.reset();
    }
    static tjs_error TJS_INTF_METHOD create(Image **result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *) {
        if(count>1) return TJS_E_BADPARAMCOUNT;
        auto value=std::make_unique<Image>();
        if(count) value->load(ttstr(*p[0]));
        *result=value.release(); return TJS_S_OK;
    }
    unsigned GetWidth() const { return record ? record->info().width : texture ? texture->GetWidth() : 0; }
    unsigned GetHeight() const { return record ? record->info().height : texture ? texture->GetHeight() : 0; }
    double GetHorizontalResolution() const { return record ? record->info().dpi[0] : dpiX; }
    double GetVerticalResolution() const { return record ? record->info().dpi[1] : dpiY; }
    int GetType() const { return record ? 2 : texture ? 1 : 0; }
    int GetPixelFormat() const { return texture ? pixelFormat : 0; }
    int GetFlags() const { return texture ? flags : record ? 1 : 0; }
    int GetLastStatus() const { int result=status; status=0; return result; }
    tTJSVariant Clone() const {
        if(!texture && !record) return tTJSVariant();
        auto copy=new Image(*this); if(texture) copy->texture=std::make_shared<tTVPBaseTexture>(*texture);
        if(record) {
            uint64_t id=0;
            if(krkr_layer_ex_record_clone(record->id,&id)) { delete copy; TVPThrowExceptionMessage(TJS_W("Could not clone metafile image")); }
            copy->record=std::make_shared<DrawRecord>(id);
        }
        return boxed(copy);
    }
    tTJSVariant GetBounds() const {
        if(record) {
            auto info=record->info();
            return boxed(new Rect(info.bounds[0],info.bounds[1],info.bounds[2],info.bounds[3]));
        }
        return texture ? boxed(new Rect(0,0,GetWidth(),GetHeight())) : tTJSVariant();
    }
    std::shared_ptr<tTVPBaseTexture> bitmap() const {
        if(texture) return texture;
        if(!record) TVPThrowExceptionMessage(TJS_W("Image is empty"));
        auto info=record->info();
        auto output=std::make_shared<tTVPBaseTexture>(info.width,info.height);
        output->Fill(tTVPRect(0,0,info.width,info.height),0);
        int32_t clip[]={0,0,int32_t(info.width),int32_t(info.height)};
        Matrix view; view.translate(-info.bounds[0],-info.bounds[1],1);
        if(krkr_layer_ex_record_replay(record->id,TVPBevyTextureID(output->GetTexture()),clip,view.elements.data(),0))
            TVPThrowExceptionMessage(TJS_W("Could not rasterize metafile texture"));
        return output;
    }
    static std::shared_ptr<tTVPBaseTexture> read(const tTJSVariant &value) {
        if(value.Type()==tvtString) {
            Image image; image.load(ttstr(value)); return image.bitmap();
        }
        if(value.Type()==tvtObject) {
            auto object=value.AsObjectNoAddRef();
            auto image=ncbInstanceAdaptor<Image>::GetNativeInstance(object);
            if(image) return image->bitmap();
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
        output->Fill(tTVPRect(0,0,output->GetWidth(),output->GetHeight()),0);
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
