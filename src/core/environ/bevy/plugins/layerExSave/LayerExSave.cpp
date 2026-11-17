// Interfaces and algorithms: wamsoft/layerExSave; see LICENSE-THIRD-PARTY.md.
#define NCB_MODULE_NAME TJS_W("layerExSave.dll")
#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "EventIntf.h"
#include "BevyRender.h"
#include <algorithm>
#include "krkr_shim.h"
#include <cstring>
#include <memory>
#include <vector>

namespace {
struct SavePixels {
    tTJSNI_BaseLayer *layer;
    std::vector<unsigned char> bytes;
    bool writable = false;
};
thread_local std::vector<SavePixels> SaveBuffers;
tTJSNI_BaseLayer *SaveLayer(iTJSDispatch2 *object) {
    tTJSNI_BaseLayer *layer = nullptr;
    if (!object || TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
            tTJSNC_Layer::ClassID, reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer->GetMainImage())
        TVPThrowExceptionMessage(TJS_W("Layer image is required"));
    return layer;
}
unsigned char *SaveBuffer(iTJSDispatch2 *object, bool writable) {
    auto layer = SaveLayer(object);
    for (auto &buffer : SaveBuffers) if (buffer.layer == layer) {
        buffer.writable |= writable;
        return buffer.bytes.data();
    }
    auto image = layer->GetMainImage();
    int width = image->GetWidth(), height = image->GetHeight();
    SavePixels buffer{layer, std::vector<unsigned char>(size_t(width)*height*4), writable};
    for (int y = 0; y < height; ++y) {
        auto row = static_cast<const unsigned char *>(image->GetScanLine(y));
        std::memcpy(buffer.bytes.data()+size_t(y)*width*4,row,size_t(width)*4);
    }
    int64_t args[6]{},values[7]{};void *bytes=nullptr;size_t length=0;
    if (krkr_image_operation(11,buffer.bytes.data(),buffer.bytes.size(),width,height,nullptr,0,0,0,args,values,&bytes,&length))
        TVPThrowExceptionMessage(TJS_W("Pixel conversion failed"));
    SaveBuffers.push_back(std::move(buffer));
    return SaveBuffers.back().bytes.data();
}
void CommitSaveBuffers() {
    for (auto &buffer : SaveBuffers) if (buffer.writable) {
        auto layer = buffer.layer;
        layer->IndependMainImage();
        auto image = layer->GetMainImage();
        auto &bytes = buffer.bytes;
        int64_t args[6]{},values[7]{};void *out=nullptr;size_t length=0;
        if (krkr_image_operation(11,bytes.data(),bytes.size(),image->GetWidth(),image->GetHeight(),nullptr,0,0,0,args,values,&out,&length))
            TVPThrowExceptionMessage(TJS_W("Pixel conversion failed"));
        image->GetTexture()->Update(bytes.data(), TVPTextureFormat::RGBA, image->GetWidth()*4,
                                  tTVPRect(0,0,image->GetWidth(),image->GetHeight()));
        layer->SetImageModified(true); layer->Update();
    }
    SaveBuffers.clear();
}
}

#include "LayerExSaveUtils.inc"
struct SaveOptions {
    KrkrSaveOptions value{};
    std::string tags;
    explicit SaveOptions(unsigned format, tTJSVariant *info) {
        value.format = format; value.level = -1;
        if (!info || info->Type() == tvtVoid) return;
        if (info->Type() != tvtObject) { value.level = info->AsInteger(); return; }
        auto object = info->AsObjectNoAddRef();
        if (!object) return;
        ncbPropAccessor dic(object);
        value.level = dic.getIntValue(TJS_W("comp_lv"), -1);
        auto &m = value.meta;
        m.has_offset = dic.HasValue(TJS_W("offs_x")) || dic.HasValue(TJS_W("offs_y"));
        m.offset_x = dic.getIntValue(TJS_W("offs_x")); m.offset_y = dic.getIntValue(TJS_W("offs_y"));
        m.offset_unit = dic.getStrValue(TJS_W("offs_unit")) == TJS_W("micrometer");
        m.has_resolution = dic.HasValue(TJS_W("reso_x")) || dic.HasValue(TJS_W("reso_y"));
        m.resolution_x = dic.getIntValue(TJS_W("reso_x")); m.resolution_y = dic.getIntValue(TJS_W("reso_y"));
        m.resolution_unit = dic.getStrValue(TJS_W("reso_unit")) == TJS_W("meter");
        m.has_vpage = dic.HasValue(TJS_W("vpag_w")) || dic.HasValue(TJS_W("vpag_h"));
        m.vpage_width = dic.getIntValue(TJS_W("vpag_w")); m.vpage_height = dic.getIntValue(TJS_W("vpag_h"));
        m.vpage_unit = dic.getStrValue(TJS_W("vpag_unit")) == TJS_W("micrometer");
        if (format == 1) {
            class Collect : public tTJSDispatch {
                std::string &tags;
            public:
                Collect(std::string &tags) : tags(tags) {}
                tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32,const tjs_char *,tjs_uint32 *,
                    tTJSVariant *result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *) override {
                    if (count >= 3 && !(p[1]->AsInteger() & TJS_HIDDENMEMBER)) {
                        auto n=ttstr(*p[0]).AsStdString(), v=ttstr(*p[2]).AsStdString();
                        tags += std::to_string(n.size())+":"+n+"="+std::to_string(v.size())+":"+v+",";
                    }
                    if (result) *result=true;
                    return TJS_S_OK;
                }
            };
            auto collect=new Collect(tags); tTJSVariantClosure closure(collect);
            object->EnumMembers(TJS_IGNOREPROP,&closure,object); collect->Release();
            value.tags=tags.data(); value.tags_len=tags.size();
        }
    }
};
struct SaveSnapshot {
    unsigned width,height;
    std::vector<unsigned char> pixels;
    SaveSnapshot(iTJSDispatch2 *object,bool province) {
        auto layer=SaveLayer(object); auto image=layer->GetMainImage();
        width=image->GetWidth(); height=image->GetHeight();
        const unsigned char *buffer;
        int pitch, row=width*(province?1:4);
        if (province) {
            buffer=static_cast<const unsigned char *>(layer->GetProvinceImagePixelBuffer());
            pitch=layer->GetProvinceImagePixelBufferPitch();
            if (!buffer) TVPThrowExceptionMessage(TJS_W("Layer has no province image"));
        } else {
            buffer=static_cast<const unsigned char *>(image->GetScanLine(0));
            pitch=image->GetPitchBytes();
        }
        pixels.resize(size_t(row)*height);
        for (unsigned y=0;y<height;++y) std::memcpy(pixels.data()+size_t(y)*row,buffer+ptrdiff_t(y)*pitch,row);
    }
};
struct EncodedImage {
    void *bytes=nullptr; size_t length=0;
    ~EncodedImage() { krkr_png_free(bytes,length); }
    void write(const ttstr &filename) {
        std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(filename,TJS_BS_WRITE));
        stream->WriteBuffer(bytes,length);
    }
};
struct BevySaveImage {
    template<unsigned Format,bool Octet=false>
    static tjs_error TJS_INTF_METHOD save(tTJSVariant *result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *object) {
        if (!Octet && count<1) return TJS_E_BADPARAMCOUNT;
        SaveSnapshot image(object,Format==2);
        SaveOptions options(Format,count>(Octet?0:1)?p[Octet?0:1]:nullptr);
        EncodedImage output;
        if (krkr_save_image_encode(image.pixels.data(),image.pixels.size(),image.width,image.height,&options.value,&output.bytes,&output.length))
            TVPThrowExceptionMessage(TJS_W("Image encoding failed"));
        if (Octet) { if (result) *result=tTJSVariant(static_cast<tjs_uint8 *>(output.bytes),output.length); }
        else output.write(ttstr(*p[0]));
        return TJS_S_OK;
    }
};
NCB_ATTACH_CLASS(BevySaveImage, Layer) {
    RawCallback("saveLayerImagePng",BevySaveImage::save<0>,0);
    RawCallback("saveLayerImagePngOctet",BevySaveImage::save<0,true>,0);
    RawCallback("saveLayerImageTlg5",BevySaveImage::save<1>,0);
    RawCallback("saveProvinceImage",BevySaveImage::save<2>,0);
}

class BevyWindowSave : public tTVPContinuousEventCallbackIntf {
    struct Job {
        int handle,reported=-1,status=0;
        unsigned long long task=0;
        tTJSVariant layer,filename;
        EncodedImage output;
        ~Job() { if (task) krkr_save_image_cancel(task,1); }
    };
    iTJSDispatch2 *owner;
    int next = 0;
    std::vector<std::shared_ptr<Job>> jobs;
    void cancel(int handle, bool stop) {
        for (auto it = jobs.begin(); it != jobs.end(); ++it) if ((*it)->handle == handle) {
            if ((*it)->task) krkr_save_image_cancel((*it)->task,stop);
            else (*it)->status=2;
            if (stop) { jobs.erase(it); if (jobs.empty()) TVPRemoveContinuousEventHook(this); }
            return;
        }
    }
public:
    explicit BevyWindowSave(iTJSDispatch2 *owner) : owner(owner) {}
    ~BevyWindowSave() {
        TVPRemoveContinuousEventHook(this);
        jobs.clear();
    }
    int startSaveLayerImage(tTJSVariant layer, const tjs_char *filename, tTJSVariant tags) {
        auto job = std::make_shared<Job>();
        job->handle = next++; job->layer = layer; job->filename = filename;
        SaveSnapshot image(layer.AsObjectNoAddRef(),false);
        ttstr ext=TVPExtractStorageExt(filename); ext.ToLowerCase();
        SaveOptions options(ext==TJS_W(".png")?0:1,&tags);
        job->task=krkr_save_image_start(image.pixels.data(),image.pixels.size(),image.width,image.height,&options.value);
        if (!job->task) TVPThrowExceptionMessage(TJS_W("Could not start image export"));
        jobs.push_back(job);
        TVPRemoveContinuousEventHook(this); TVPAddContinuousEventHook(this);
        return job->handle;
    }
    void cancelSaveLayerImage(int handle) { cancel(handle,false); }
    void stopSaveLayerImage(int handle) { cancel(handle,true); }
    void TJS_INTF_METHOD OnContinuousCallback(tjs_uint64) override {
        auto pending = jobs;
        auto object = owner;
        object->AddRef();
        struct Release { iTJSDispatch2 *object; ~Release() { object->Release(); } } release{object};
        for (auto &job : pending) {
            auto found = std::find(jobs.begin(),jobs.end(),job);
            if (found == jobs.end()) continue;
            int percent=100;
            if (!job->status) {
                job->status=krkr_save_image_poll(job->task,&percent,&job->output.bytes,&job->output.length);
                if (job->status) job->task=0;
            }
            bool done=job->status!=0;
            if (percent != job->reported) {
                job->reported = percent;
                tTJSVariant handle(job->handle), progress(percent);
                tTJSVariant *p[] = {&handle,&progress,&job->layer,&job->filename};
                object->FuncCall(0,TJS_W("onSaveLayerImageProgress"),nullptr,nullptr,4,p,object);
                return;
            }
            if (done) {
                jobs.erase(found);
                if (jobs.empty()) TVPRemoveContinuousEventHook(this);
                if (job->status<0) TVPThrowExceptionMessage(TJS_W("Image export failed"));
                if (job->status==1) job->output.write(ttstr(job->filename));
            }
            tTJSVariant handle(job->handle), value(done ? int(job->status==2) : percent);
            tTJSVariant *p[] = {&handle,&value,&job->layer,&job->filename};
            if (done || percent != job->reported) {
                job->reported = percent;
                object->FuncCall(0, done ? TJS_W("onSaveLayerImageDone") : TJS_W("onSaveLayerImageProgress"), nullptr,nullptr,4,p,object);
                // A script callback can invalidate this native instance.
                return;
            }
        }
    }
};
NCB_GET_INSTANCE_HOOK(BevyWindowSave) {
    NCB_INSTANCE_GETTER(objthis) {
        auto value = GetNativeInstance(objthis);
        if (!value) { value = new ClassT(objthis); SetNativeInstance(objthis,value); }
        return value;
    }
};
NCB_ATTACH_CLASS_WITH_HOOK(BevyWindowSave, Window) {
    NCB_METHOD(startSaveLayerImage);
    NCB_METHOD(cancelSaveLayerImage);
    NCB_METHOD(stopSaveLayerImage);
}
