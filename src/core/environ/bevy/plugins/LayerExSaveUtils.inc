// TJS adapters for wamsoft/layerExSave pixel operations.
struct BevySavePixels {
    template<unsigned Operation>
    static tjs_error TJS_INTF_METHOD apply(tTJSVariant *result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *object) {
        constexpr bool binary=Operation==2 || Operation==3 || Operation==5;
        constexpr int required=binary || Operation==4 ? 1 : Operation==6 || Operation==8 ? 4 : 0;
        if (count<required) return TJS_E_BADPARAMCOUNT;
        auto layer=SaveLayer(object);auto image=layer->GetMainImage();
        unsigned w=image->GetWidth(),h=image->GetHeight();
        SaveBuffers.clear();
        try {
            int64_t args[6]{},values[7]{};
            if (Operation==3) args[0]=args[1]=-1;
            if (Operation==4) args[1]=1;
            if (Operation==9) args[0]=1;
            if (Operation==10) args[0]=args[1]=16;
            for (int i=binary?1:0;i<count && i<(binary?3:6);++i)
                if (p[i]->Type()!=tvtVoid) args[i-(binary?1:0)]=p[i]->AsInteger();
            auto pixels=SaveBuffer(object,Operation==3 || Operation==4 || Operation==5 || Operation==7);
            const void *source=nullptr;size_t sourceLength=0;unsigned bw=0,bh=0;
            if (binary) {
                auto base=p[0]->AsObjectNoAddRef();auto bitmap=SaveLayer(base)->GetMainImage();
                bw=bitmap->GetWidth();bh=bitmap->GetHeight();sourceLength=size_t(bw)*bh*4;
                source=SaveBuffer(base,false);
            }
            void *bytes=nullptr;size_t length=0;
            int status=krkr_image_operation(Operation,pixels,size_t(w)*h*4,w,h,source,sourceLength,bw,bh,args,values,&bytes,&length);
            if (status) TVPThrowExceptionMessage(TJS_W("Invalid image operation parameters"));
            if (result) {
                if (Operation<=2) {
                    result->Clear();
                    if (values[4]) {
                        ncbDictionaryAccessor dict;
                        const tjs_char *names[]={TJS_W("x"),TJS_W("y"),TJS_W("w"),TJS_W("h")};
                        for (int i=0;i<4;++i) dict.SetValue(names[i],tjs_int(values[i]));
                        *result=dict;
                    }
                } else if (Operation==3) *result=tjs_int64(values[4]);
                else if (Operation==6 || Operation==8 || Operation==9) *result=tjs_int64(values[0]);
                else if (Operation==10) *result=tTJSVariant(static_cast<tjs_uint8 *>(bytes),length);
            }
            krkr_png_free(bytes,length);
            CommitSaveBuffers();
            return TJS_S_OK;
        } catch (...) {SaveBuffers.clear();throw;}
    }
};
NCB_ATTACH_CLASS(BevySavePixels, Layer) {
    RawCallback("getCropRect",BevySavePixels::apply<0>,0);
    RawCallback("getCropRectZero",BevySavePixels::apply<1>,0);
    RawCallback("getDiffRect",BevySavePixels::apply<2>,0);
    RawCallback("getDiffPixel",BevySavePixels::apply<3>,0);
    RawCallback("oozeColor",BevySavePixels::apply<4>,0);
    RawCallback("copyBlueToAlpha",BevySavePixels::apply<5>,0);
    RawCallback("isBlank",BevySavePixels::apply<6>,0);
    RawCallback("clearAlpha",BevySavePixels::apply<7>,0);
    RawCallback("getAverageColor",BevySavePixels::apply<8>,0);
    RawCallback("getFingerPrintValue",BevySavePixels::apply<9>,0);
    RawCallback("getShrinkVectorOctet",BevySavePixels::apply<10>,0);
}
struct BevyOctetVector {
    static tjs_error TJS_INTF_METHOD sum(tTJSVariant *result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *) {
        if (count<1) return TJS_E_BADPARAMCOUNT;
        if (p[0]->Type()!=tvtOctet || (count>1 && p[1]->Type()!=tvtVoid && p[1]->Type()!=tvtOctet)) return TJS_E_INVALIDPARAM;
        auto a=p[0]->AsOctetNoAddRef();
        auto b=count>1 && p[1]->Type()==tvtOctet?p[1]->AsOctetNoAddRef():nullptr;
        int64_t values[7]{};
        if (krkr_octet_vector_sum(a->GetData(),a->GetLength(),b?b->GetData():nullptr,b?b->GetLength():0,values)) return TJS_E_INVALIDPARAM;
        if (result) {
            auto array=TJSCreateArrayObject();
            for (int i=0;i<7;++i) {tTJSVariant value{tjs_int64(values[i])};array->PropSetByNum(TJS_MEMBERENSURE,i,&value,array);}
            *result=tTJSVariant(array,array);array->Release();
        }
        return TJS_S_OK;
    }
};
NCB_ATTACH_CLASS(BevyOctetVector, Math) { RawCallback("octetVectorSum",BevyOctetVector::sum,0); }
