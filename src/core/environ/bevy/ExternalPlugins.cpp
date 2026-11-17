#include "ExternalPlugins.h"

#include <dlfcn.h>

#include <algorithm>
#include <memory>
#include <set>
#include <vector>

#include "PluginImpl.h"
#include "ExternalTransitions.h"
#include "ExternalGPU.h"
#include "ScriptMgnIntf.h"
#include "StorageImpl.h"
#include "SysInitImpl.h"

extern std::set<ttstr> TVPRegisteredPlugins;

namespace {

using Link = int32_t (*)(iTVPFunctionExporter *);
using Unlink = int32_t (*)();

struct LoadedPlugin {
    ttstr name;
    void *handle;
    Unlink unlink;
    std::unique_ptr<tTVPPluginHolder> holder;
};

std::vector<LoadedPlugin> loaded;

std::vector<LoadedPlugin>::iterator findPlugin(const ttstr &name) {
    return std::find_if(loaded.begin(), loaded.end(), [&name](const auto &plugin) {
        return plugin.name == name;
    });
}

ttstr resolveName(const ttstr &name) {
    std::basic_string<tjs_char> soname = name.c_str();
    const bool dll = soname.size() >= 4 && soname.substr(soname.size() - 4) == TJS_W(".dll");
#if defined(_WIN32)
    if (dll) return name;
#elif defined(__APPLE__)
    if (dll) soname.replace(soname.size() - 3, 3, TJS_W("dylib"));
#else
    if (dll) soname.replace(soname.size() - 3, 3, TJS_W("so"));
#endif
    if (soname.substr(0, 3) != TJS_W("lib")) soname.insert(0, TJS_W("lib"));
    return ttstr(soname.c_str());
}

} // namespace

bool TVPBevyLoadExternalPlugin(const ttstr &name) {
    if (findPlugin(name) != loaded.end()) return true;

    std::unique_ptr<tTVPPluginHolder> holder;
    ttstr local = resolveName(name);
#if !defined(ANDROID) && !defined(__ANDROID__)
    holder = std::make_unique<tTVPPluginHolder>(local);
    local = holder->GetLocalName();
    if (local.IsEmpty() || !TVPCheckExistentLocalFile(local)) return false;
#endif
    int loadFlags = RTLD_NOW | RTLD_LOCAL;
#ifdef RTLD_NODELETE
    // TJS temporary dispatch references can outlive V2Unlink until engine shutdown.
    loadFlags |= RTLD_NODELETE;
#endif
    void *handle = dlopen(local.AsNarrowStdString().c_str(), loadFlags);
    if (!handle) return false;
    auto link = reinterpret_cast<Link>(dlsym(handle, "V2Link"));
    auto unlink = reinterpret_cast<Unlink>(dlsym(handle, "V2Unlink"));
    try {
        TVPBevyBeginGPUPlugin(name);
        if (link) link(TVPGetFunctionExporter());
        TVPBevyEndGPUPlugin();
        loaded.push_back({name, handle, unlink, std::move(holder)});
        TVPRegisteredPlugins.insert(name);
    } catch (...) {
        TVPBevyEndGPUPlugin();
        TVPBevyReleaseGPUPlugin(name);
        dlclose(handle);
        throw;
    }
    return true;
}

bool TVPBevyUnloadExternalPlugin(const ttstr &name) {
    auto it = findPlugin(name);
    if (it == loaded.end()) return false;
    if (TVPBevyExternalTransitionsActive()) return false;
    if (auto engine = TVPGetScriptEngine()) engine->DoGarbageCollection();
    if (it->unlink && TJS_FAILED(it->unlink())) return false;
    TVPBevyReleaseGPUPlugin(name);
    dlclose(it->handle);
    TVPRegisteredPlugins.erase(it->name);
    loaded.erase(it);
    return true;
}

void TVPBevyUnloadExternalPlugins() {
    TVPBevyStopExternalTransitions();
    std::vector<ttstr> names;
    for (auto it = loaded.rbegin(); it != loaded.rend(); ++it) names.push_back(it->name);
    for (const auto &name : names) TVPBevyUnloadExternalPlugin(name);
}

bool TVPBevyIsExternalPlugin(const ttstr &name) { return findPlugin(name) != loaded.end(); }
