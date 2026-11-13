#include "tjsCommHead.h"
#include "StorageIntf.h"
#include "PluginImpl.h"
#include "EventIntf.h"
#include <memory>
#include <unordered_map>

namespace KrkrzABI {
class Stream {
public:
    virtual ~Stream() = default;
    virtual tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64,tjs_int) = 0;
    virtual tjs_uint TJS_INTF_METHOD Read(void *,tjs_uint) = 0;
    virtual tjs_uint TJS_INTF_METHOD Write(const void *,tjs_uint) = 0;
    virtual void TJS_INTF_METHOD SetEndOfStorage() = 0;
    virtual tjs_uint64 TJS_INTF_METHOD GetSize() = 0;
    virtual void TJS_INTF_METHOD Destruct() { delete this; }
};
class Media {
public:
    virtual void TJS_INTF_METHOD AddRef() = 0;
    virtual void TJS_INTF_METHOD Release() = 0;
    virtual void TJS_INTF_METHOD GetName(ttstr &) = 0;
    virtual void TJS_INTF_METHOD NormalizeDomainName(ttstr &) = 0;
    virtual void TJS_INTF_METHOD NormalizePathName(ttstr &) = 0;
    virtual bool TJS_INTF_METHOD CheckExistentStorage(const ttstr &) = 0;
    virtual Stream *TJS_INTF_METHOD Open(const ttstr &,tjs_uint32) = 0;
    virtual void TJS_INTF_METHOD GetListAt(const ttstr &,iTVPStorageLister *) = 0;
    virtual void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr &) = 0;
};
}
namespace {
class PluginStream final : public KrkrzABI::Stream {
    std::unique_ptr<tTJSBinaryStream> stream;
public:
    explicit PluginStream(tTJSBinaryStream *s) : stream(s) {}
    tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 o,tjs_int w) override { return stream->Seek(o,w); }
    tjs_uint TJS_INTF_METHOD Read(void *b,tjs_uint n) override { return stream->Read(b,n); }
    tjs_uint TJS_INTF_METHOD Write(const void *b,tjs_uint n) override { return stream->Write(b,n); }
    void TJS_INTF_METHOD SetEndOfStorage() override { stream->SetEndOfStorage(); }
    tjs_uint64 TJS_INTF_METHOD GetSize() override { return stream->GetSize(); }
};
class HostStream final : public tTJSBinaryStream {
    KrkrzABI::Stream *stream;
public:
    explicit HostStream(KrkrzABI::Stream *s) : stream(s) {}
    ~HostStream() override { stream->Destruct(); }
    tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 o,tjs_int w) override { return stream->Seek(o,w); }
    tjs_uint TJS_INTF_METHOD Read(void *b,tjs_uint n) override { return stream->Read(b,n); }
    tjs_uint TJS_INTF_METHOD Write(const void *b,tjs_uint n) override { return stream->Write(b,n); }
    void TJS_INTF_METHOD SetEndOfStorage() override { stream->SetEndOfStorage(); }
    tjs_uint64 TJS_INTF_METHOD GetSize() override { return stream->GetSize(); }
};
class Media final : public iTVPStorageMedia {
    int refs=1;
    KrkrzABI::Media *media;
public:
    explicit Media(KrkrzABI::Media *m) : media(m) { media->AddRef(); }
    ~Media() override { media->Release(); }
    void TJS_INTF_METHOD AddRef() override { ++refs; }
    void TJS_INTF_METHOD Release() override { if(!--refs) delete this; }
    void TJS_INTF_METHOD GetName(ttstr &s) override { media->GetName(s); }
    void TJS_INTF_METHOD NormalizeDomainName(ttstr &s) override { media->NormalizeDomainName(s); }
    void TJS_INTF_METHOD NormalizePathName(ttstr &s) override { media->NormalizePathName(s); }
    bool TJS_INTF_METHOD CheckExistentStorage(const ttstr &s) override { return media->CheckExistentStorage(s); }
    tTJSBinaryStream *TJS_INTF_METHOD Open(const ttstr &s,tjs_uint32 flags) override {
        auto stream=media->Open(s,flags);
        return stream ? new HostStream(stream) : nullptr;
    }
    void TJS_INTF_METHOD GetListAt(const ttstr &s,iTVPStorageLister *l) override { media->GetListAt(s,l); }
    void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr &s) override { media->GetLocallyAccessibleName(s); }
};
std::unordered_map<KrkrzABI::Media *,Media *> media;
KrkrzABI::Stream *CreateStream(const ttstr &name,tjs_uint32 flags) { return new PluginStream(TVPCreateStream(name,flags)); }
void RegisterMedia(KrkrzABI::Media *external) {
    auto wrapper=new Media(external);
    try { TVPRegisterStorageMedia(wrapper); media.emplace(external,wrapper); }
    catch(...) { wrapper->Release(); throw; }
}
void UnregisterMedia(KrkrzABI::Media *external) {
    auto it=media.find(external);
    if(it==media.end()) return;
    TVPUnregisterStorageMedia(it->second); it->second->Release(); media.erase(it);
}
}

void TVPBevyExportStorageFunctions() {
#define EXPORT(signature, function) TVPAddExportFunction(signature,reinterpret_cast<void *>(function))
    EXPORT("iTJSBinaryStream * ::TVPCreateStream(const ttstr &,tjs_uint32)",CreateStream);
    EXPORT("void ::TVPRegisterStorageMedia(iTVPStorageMedia *)",RegisterMedia);
    EXPORT("void ::TVPUnregisterStorageMedia(iTVPStorageMedia *)",UnregisterMedia);
    EXPORT("void ::TVPAddCompactEventHook(tTVPCompactEventCallbackIntf *)",TVPAddCompactEventHook);
    EXPORT("void ::TVPRemoveCompactEventHook(tTVPCompactEventCallbackIntf *)",TVPRemoveCompactEventHook);
    EXPORT("ttstr ::TVPGetPlacedPath(const ttstr &)",TVPGetPlacedPath);
    EXPORT("ttstr ::TVPGetLocallyAccessibleName(const ttstr &)",TVPGetLocallyAccessibleName);
    EXPORT("bool ::TVPIsExistentStorageNoSearch(const ttstr &)",TVPIsExistentStorageNoSearch);
    EXPORT("bool ::TVPIsExistentStorage(const ttstr &)",TVPIsExistentStorage);
    EXPORT("ttstr ::TVPNormalizeStorageName(const ttstr &)",TVPNormalizeStorageName);
#undef EXPORT
}
