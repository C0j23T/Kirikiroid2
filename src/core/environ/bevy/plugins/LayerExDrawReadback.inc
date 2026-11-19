// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.

struct Readback {
    static tjs_error TJS_INTF_METHOD saveImage(tTJSVariant *result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *obj) {
        if(count<1) return TJS_E_BADPARAMCOUNT;
        auto image=drawingLayer(obj)->GetMainImage();
        ttstr mime=count>1 ? ttstr(*p[1]) : ttstr(TJS_W("image/bmp"));
        tTJSVariant options=count>2 ? *p[2] : tTJSVariant();
        auto object=options.Type()==tvtObject ? options.AsObjectNoAddRef() : nullptr;
        int depth=number(options,TJS_W("depth"),24);
        if(mime==TJS_W("image/png") || mime==TJS_W("image/bmp")) {
            std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(ttstr(*p[0]),TJS_BS_WRITE));
            if(mime==TJS_W("image/png")) TVPSaveAsPNG(nullptr,stream.get(),image,TJS_W("png"),object);
            else TVPSaveAsBMP(nullptr,stream.get(),image,depth==8 ? TJS_W("bmp8") : depth==24 ? TJS_W("bmp24") : TJS_W("bmp32"),object);
        } else {
            KrkrDrawingEncoder encoder{};
            if(mime==TJS_W("image/gif")) encoder.format=1;
            else if(mime==TJS_W("image/tiff")) encoder.format=2;
            else if(mime==TJS_W("image/jpeg")) encoder.format=3;
            else TVPThrowExceptionMessage(TJS_W("Unsupported image encoder"));
            encoder.depth=depth;
            encoder.compression=number(options,TJS_W("compression"),-1);
            encoder.scan=number(options,TJS_W("scanmethod"),8);
            encoder.version=number(options,TJS_W("version"),10);
            encoder.render=number(options,TJS_W("render"),12);
            encoder.quality=number(options,TJS_W("quality"),75);
            auto width=image->GetWidth(),height=image->GetHeight();
            size_t stride=size_t(width)*4;
            std::vector<unsigned char> pixels(stride*height);
            for(unsigned y=0;y<height;++y)
                std::memcpy(pixels.data()+y*stride,image->GetScanLine(y),stride);
            DrawingBytes output;
            if(krkr_drawing_bitmap_encode(pixels.data(),pixels.size(),width,height,&encoder,&output.data,&output.size)) {
                if(result) *result=false;
                return TJS_S_OK;
            }
            std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(ttstr(*p[0]),TJS_BS_WRITE));
            stream->WriteBuffer(output.data,output.size);
        }
        if(result) *result=true;
        return TJS_S_OK;
    }
    static tjs_error TJS_INTF_METHOD regions(tTJSVariant *result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *obj) {
        if(count<1) return TJS_E_BADPARAMCOUNT;
        auto image=drawingLayer(obj)->GetMainImage(); auto argb=static_cast<tjs_uint32>(p[0]->AsInteger());
        auto color=(argb&0xff00ff00)|((argb&0xff)<<16)|((argb>>16)&0xff);
        int width=image->GetWidth(),height=image->GetHeight();
        std::vector<GeometryRect> rectangles;
        std::vector<std::size_t> previous;
        for(int y=0;y<height;++y) {
            auto row=static_cast<const tjs_uint32*>(image->GetScanLine(y));
            std::vector<std::size_t> current;
            for(int x=0;x<width;) {
                if(row[x]!=color) { ++x; continue; }
                int left=x++; while(x<width && row[x]==color) ++x;
                auto found=std::find_if(previous.begin(),previous.end(),[&](std::size_t index) {
                    const auto &r=rectangles[index]; return r.x==left && r.width==x-left;
                });
                if(found!=previous.end()) { rectangles[*found].height+=1; current.push_back(*found); }
                else { current.push_back(rectangles.size()); rectangles.push_back({double(left),double(y),double(x-left),1}); }
            }
            previous=std::move(current);
        }
        auto array=TJSCreateArrayObject();
        for(auto r:rectangles) {
            auto inner=TJSCreateArrayObject();
            tTJSVariant x(r.x),y(r.y),w(r.width),h(r.height),*values[]={&x,&y,&w,&h};
            inner->FuncCall(0,TJS_W("push"),nullptr,nullptr,4,values,inner);
            tTJSVariant value(inner,inner),*param=&value; inner->Release();
            array->FuncCall(0,TJS_W("push"),nullptr,nullptr,1,&param,array);
        }
        if(result) *result=tTJSVariant(array,array);
        array->Release(); return TJS_S_OK;
    }
};
