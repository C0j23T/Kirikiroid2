#include "ExternalGPU.h"
#include "BevyRender.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "PluginImpl.h"
#include "MsgIntf.h"

#include <algorithm>
#include <map>
#include <vector>

extern "C" int krkr_render_register_method(const char *owner, const char *method,
                                            const char *wesl, uint32_t *opcode);
extern "C" int krkr_render_unregister_owner(const char *owner);

namespace {
struct Method {
    ttstr owner;
    tjs_uint32 opcode;
};
std::map<ttstr, Method> methods;
thread_local std::vector<ttstr> loading;

tjs_uint32 TJS_INTF_METHOD InitializeGPU(const tjs_char *name, const char *wesl) {
    if (loading.empty() || !name || !*name || !wesl)
        TVPThrowExceptionMessage(TJS_W("GPU methods must be initialized while linking a plugin"));
    const ttstr method(name);
    if (methods.count(method)) TVPThrowExceptionMessage(TJS_W("GPU method is already registered"));
    auto owner = loading.back().AsNarrowStdString();
    auto narrow = method.AsNarrowStdString();
    tjs_uint32 opcode = 0;
    if (krkr_render_register_method(owner.c_str(), narrow.c_str(), wesl, &opcode))
        TVPThrowExceptionMessage(TJS_W("GPU method compilation failed"));
    methods.emplace(method, Method{loading.back(), opcode});
    return opcode;
}

tTJSNI_BaseLayer *Layer(iTJSDispatch2 *object) {
    tTJSNI_BaseLayer *layer = nullptr;
    if (!object || TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
            tTJSNC_Layer::ClassID, reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer->GetMainImage())
        TVPThrowExceptionMessage(TJS_W("GPU method requires a Layer image"));
    return layer;
}

void TJS_INTF_METHOD ApplyGPU(const tjs_char *name, iTJSDispatch2 *target,
                              iTJSDispatch2 *source, const tTVPRect &rect,
                              const tTVPRect &sourceRect, const float *parameters,
                              tjs_uint count) {
    if (!name || !methods.count(ttstr(name)))
        TVPThrowExceptionMessage(TJS_W("GPU method is not registered"));
    if (count > 16 || (count && !parameters))
        TVPThrowExceptionMessage(TJS_W("GPU method accepts at most 16 parameters"));
    if (rect.is_empty()) return;
    auto destination = Layer(target);
    auto input = source ? Layer(source) : destination;
    destination->IndependMainImage();
    auto texture = destination->GetMainImage()->GetTexture();
    TVPBevyRenderCommand command{};
    auto method = ttstr(name).AsNarrowStdString();
    command.operation = 4;
    command.method = method.c_str();
    command.target = TVPBevyTextureID(texture);
    command.sources[0] = TVPBevyTextureID(input->GetMainImage()->GetTexture());
    command.sources[1] = command.target;
    command.rect[0] = rect.left; command.rect[1] = rect.top;
    command.rect[2] = rect.right; command.rect[3] = rect.bottom;
    command.source_rects[0][0] = sourceRect.left;
    command.source_rects[0][1] = sourceRect.top;
    command.source_rects[0][2] = sourceRect.right;
    command.source_rects[0][3] = sourceRect.bottom;
    for (int i = 0; i < 4; ++i) command.source_rects[1][i] = command.rect[i];
    if (count) std::copy_n(parameters, count, command.parameters);
    if (krkr_render_dispatch(&command))
        TVPThrowExceptionMessage(TJS_W("External GPU operation failed"));
    TVPBevyInvalidateTexture(texture);
    destination->SetImageModified(true);
    auto dirty = rect;
    dirty.add_offsets(destination->GetImageLeft(), destination->GetImageTop());
    destination->Update(dirty);
}
}

void TVPBevyBeginGPUPlugin(const ttstr &name) { loading.push_back(name); }
void TVPBevyEndGPUPlugin() { if (!loading.empty()) loading.pop_back(); }
void TVPBevyReleaseGPUPlugin(const ttstr &name) {
    bool found = false;
    for (auto it = methods.begin(); it != methods.end();) {
        if (it->second.owner == name) {
            it = methods.erase(it);
            found = true;
        } else ++it;
    }
    if (found) krkr_render_unregister_owner(name.AsNarrowStdString().c_str());
}

void TVPBevyExportGPUFunctions() {
    TVPAddExportFunction("tjs_uint32 ::TVPInitializeGPU(const tjs_char *,const char *)",
                         reinterpret_cast<void *>(InitializeGPU));
    TVPAddExportFunction("void ::TVPApplyGPU(const tjs_char *,iTJSDispatch2 *,iTJSDispatch2 *,const tTVPRect &,const tTVPRect &,const float *,tjs_uint)",
                         reinterpret_cast<void *>(ApplyGPU));
}
