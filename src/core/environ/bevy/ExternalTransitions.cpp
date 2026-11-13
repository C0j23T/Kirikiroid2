#include "ExternalTransitions.h"
#include "TransIntf.h"
#include "RenderManager.h"
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
int activeHandlers = 0;
class Handler;
std::unordered_set<Handler *> handlers;

class Pixels final : public KrkrzABI::ScanLine {
    int width, height, refs=1;
    bool owned=false;
public:
    std::vector<tjs_uint32> pixels;
    Pixels(int w, int h, bool owned=false) : width(w), height(h), owned(owned), pixels(size_t(w)*h) {}
    void Capture(iTVPScanLineProvider *source) {
        auto texture = source->GetTexture();
        for (int y=0; y<height; ++y)
            std::memcpy(pixels.data()+size_t(y)*width, texture->GetScanLineForRead(y), size_t(width)*4);
        for(auto &pixel : pixels) pixel=(pixel&0xff00ff00)|((pixel&0xff)<<16)|((pixel>>16)&0xff);
    }
    tjs_error TJS_INTF_METHOD AddRef() override { ++refs; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD Release() override { if(!--refs && owned) delete this; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD GetWidth(tjs_int *out) override { *out=width; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD GetHeight(tjs_int *out) override { *out=height; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD GetPixelFormat(tjs_int *out) override { *out=32; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD GetPitchBytes(tjs_int *out) override { *out=width*4; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD GetScanLine(tjs_int y, const void **out) override {
        if(y<0 || y>=height || !out) return TJS_E_INVALIDPARAM;
        *out=pixels.data()+size_t(y)*width; return TJS_S_OK;
    }
    tjs_error TJS_INTF_METHOD GetScanLineForWrite(tjs_int y, void **out) override {
        const void *row=nullptr;
        auto result=GetScanLine(y,&row); *out=const_cast<void *>(row); return result;
    }
};

class ImageProvider final : public KrkrzABI::ImageProvider {
    iTVPSimpleImageProvider *host;
public:
    explicit ImageProvider(iTVPSimpleImageProvider *p) : host(p) {}
    tjs_error TJS_INTF_METHOD LoadImage(const tjs_char *name,tjs_int bpp,tjs_uint32 key,
        tjs_uint w,tjs_uint h,KrkrzABI::ScanLine **out) override {
        if(!out || bpp!=32) return TJS_E_INVALIDPARAM;
        *out=nullptr;
        iTVPScanLineProvider *image=nullptr;
        auto result=host->LoadImage(name,bpp,key,w,h,&image);
        if(TJS_FAILED(result)) return result;
        Pixels *pixels=nullptr;
        try {
            tjs_int width=0,height=0; image->GetWidth(&width); image->GetHeight(&height);
            pixels=new Pixels(width,height,true); pixels->Capture(image);
        } catch(...) { if(pixels) pixels->Release(); image->Release(); throw; }
        image->Release(); *out=pixels; return TJS_S_OK;
    }
};

class Handler final : public iTVPDivisibleTransHandler {
    int refs=1;
    KrkrzABI::Handler *external;
    Pixels source1, source2, output;
    std::vector<tjs_uint32> rgba;
    bool captured=false;
public:
    Handler(KrkrzABI::Handler *h, int w, int height)
        : external(h), source1(w,height), source2(w,height), output(w,height), rgba(size_t(w)*height) { ++activeHandlers; handlers.insert(this); }
    ~Handler() { Detach(); handlers.erase(this); }
    void Detach() {
        if(!external) return;
        external->Release(); external=nullptr; --activeHandlers;
    }
    tjs_error TJS_INTF_METHOD AddRef() override { ++refs; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD Release() override { if(!--refs) delete this; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *o) override { return external ? external->SetOption(o) : TJS_E_FAIL; }
    tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick) override { return external ? external->StartProcess(tick) : TJS_S_FALSE; }
    tjs_error TJS_INTF_METHOD EndProcess() override { return external ? external->EndProcess() : TJS_S_FALSE; }
    tjs_error TJS_INTF_METHOD MakeFinalImage(iTVPScanLineProvider **d, iTVPScanLineProvider *, iTVPScanLineProvider *s) override {
        *d=s; return TJS_S_OK;
    }
    tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data) override {
        if(!external) return TJS_E_FAIL;
        if(!captured) { source1.Capture(data->Src1); source2.Capture(data->Src2); captured=true; }
        KrkrzABI::Data d{data->Left,data->Top,data->Width,data->Height,&output,
            data->Left,data->Top,&source1,data->Src1Left,data->Src1Top,
            &source2,data->Src2Left,data->Src2Top};
        auto result=external->Process(&d);
        if(TJS_FAILED(result)) return result;
        if(d.Dest==&source1) { data->Dest=data->Src1; data->DestLeft=d.DestLeft; data->DestTop=d.DestTop; }
        else if(d.Dest==&source2) { data->Dest=data->Src2; data->DestLeft=d.DestLeft; data->DestTop=d.DestTop; }
        else {
            const void *row=nullptr;
            if(TJS_FAILED(d.Dest->GetScanLine(d.DestTop,&row))) return TJS_E_FAIL;
            int pitch=0; d.Dest->GetPitchBytes(&pitch);
            rgba.resize(size_t(data->Width)*data->Height);

            // TODO: Move BGRA -> RGBA to GPU
            for(int y=0; y<data->Height; ++y) {
                const auto *source=reinterpret_cast<const tjs_uint32 *>(static_cast<const char *>(row)+y*pitch)+d.DestLeft;
                for(int x=0; x<data->Width; ++x) {
                    auto p=source[x];
                    rgba[size_t(y)*data->Width+x]=(p&0xff00ff00)|((p&0xff)<<16)|((p>>16)&0xff);
                }
            }
            data->Dest->GetTextureForRender()->Update(rgba.data(),
                TVPTextureFormat::RGBA,data->Width*4,tTVPRect(data->DestLeft,data->DestTop,
                data->DestLeft+data->Width,data->DestTop+data->Height));
        }
        return result;
    }
};

class Provider final : public iTVPTransHandlerProvider {
    int refs=1;
    KrkrzABI::Provider *external;
public:
    explicit Provider(KrkrzABI::Provider *p) : external(p) { external->AddRef(); }
    ~Provider() override { external->Release(); }
    tjs_error TJS_INTF_METHOD AddRef() override { ++refs; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD Release() override { if(!--refs) delete this; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD GetName(const tjs_char **name) override { return external->GetName(name); }
    tjs_error TJS_INTF_METHOD StartTransition(iTVPSimpleOptionProvider *options,iTVPSimpleImageProvider *image,
        tTVPLayerType layer,tjs_uint w,tjs_uint h,tjs_uint w2,tjs_uint h2,
        tTVPTransType *type,tTVPTransUpdateType *update,iTVPBaseTransHandler **out) override {
        iTVPBaseTransHandler *handler=nullptr;
        ImageProvider images(image);
        auto result=external->StartTransition(options,&images,layer,w,h,w2,h2,type,update,&handler);
        if(TJS_FAILED(result)) return result;
        if(!handler || *type!=ttExchange || *update==tutGiveUpdate || w!=w2 || h!=h2) {
            if(handler) handler->Release();
            return TJS_E_INVALIDPARAM;
        }
        try { *out=new Handler(static_cast<KrkrzABI::Handler *>(handler),w,h); }
        catch(...) { handler->Release(); throw; }
        return result;
    }
};
std::unordered_map<KrkrzABI::Provider *,Provider *> providers;
}

void TVPBevyAddExternalTransProvider(KrkrzABI::Provider *external) {
    auto p=new Provider(external);
    try { TVPAddTransHandlerProvider(p); providers.emplace(external,p); }
    catch(...) { p->Release(); throw; }
}
void TVPBevyRemoveExternalTransProvider(KrkrzABI::Provider *external) {
    auto it=providers.find(external);
    if(it==providers.end()) return;
    TVPRemoveTransHandlerProvider(it->second);
    it->second->Release(); providers.erase(it);
}
bool TVPBevyExternalTransitionsActive() { return activeHandlers!=0; }

void TVPBevyStopExternalTransitions() { for(auto handler : handlers) handler->Detach(); }
