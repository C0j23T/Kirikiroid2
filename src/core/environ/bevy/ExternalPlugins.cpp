#include "ExternalPlugins.h"

#include <dlfcn.h>

#include <algorithm>
#include <filesystem>
#include "krkr_storage.h"
#include <string>
#include <set>
#include <unordered_map>
#include <vector>

#include "PluginImpl.h"
#include "ExternalTransitions.h"
#include "ncbind/ncbind.hpp"
#include "StorageImpl.h"
#include "SysInitImpl.h"

extern std::set<ttstr> TVPRegisteredPlugins;

namespace {

using Link = int32_t (*)(iTVPFunctionExporter *);
using Unlink = int32_t (*)();

struct LoadedPlugin {
    void *handle;
    Unlink unlink;
};

std::unordered_map<std::string, LoadedPlugin> loaded;

std::string keyFor(const ttstr &name) {
    return TVPExtractStorageName(name).AsLowerCase().AsNarrowStdString();
}

std::vector<std::filesystem::path> candidates(const ttstr &name) {
    const std::filesystem::path project(TVPNativeProjectDir.AsNarrowStdString());
    const std::string requested = name.AsNarrowStdString();
    const std::filesystem::path given(requested);
    std::vector<std::filesystem::path> result;
    auto add = [&result](const std::filesystem::path &path) {
        if (!path.empty() && std::find(result.begin(), result.end(), path) == result.end())
            result.push_back(path);
    };
    add(given);
    add(project / given);
    add(project / "plugin" / given);

    std::filesystem::path stem = given;
    if (stem.extension() == ".dll" || stem.extension() == ".DLL")
        stem.replace_extension(".so");
    if (stem.extension() != ".so") stem += ".so";
    add(stem);
    add(project / stem);
    add(project / "plugin" / stem);
    if (stem.filename().string().rfind("lib", 0) != 0)
        add(project / ("lib" + stem.filename().string()));
    return result;
}

} // namespace

bool TVPBevyLoadExternalPlugin(const ttstr &name) {
    const std::string key = keyFor(name);
    if (key.empty() || loaded.find(key) != loaded.end()) return true;

    for (const auto &path : candidates(name)) {
        KrkrFsMetadata meta;
        if (krkr_fs_metadata(path.string().c_str(), &meta) != 0 || meta.kind != 1) continue;
        void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) continue;
        auto link = reinterpret_cast<Link>(dlsym(handle, "V2Link"));
        auto unlink = reinterpret_cast<Unlink>(dlsym(handle, "V2Unlink"));
        if (!link || link(TVPGetFunctionExporter()) != 0) {
            dlclose(handle);
            continue;
        }
        loaded.emplace(key, LoadedPlugin{handle, unlink});
        TVPRegisteredPlugins.insert(ttstr(key.c_str()));
        return true;
    }
    return false;
}

bool TVPBevyUnloadExternalPlugin(const ttstr &name) {
    const std::string key = keyFor(name);
    auto it = loaded.find(key);
    if (it == loaded.end()) return false;
    if (TVPBevyExternalTransitionsActive()) return false;
    if (it->second.unlink && it->second.unlink() != 0) return false;
    dlclose(it->second.handle);
    loaded.erase(it);
    TVPRegisteredPlugins.erase(ttstr(key.c_str()));
    return true;
}

void TVPBevyUnloadExternalPlugins() {
    TVPBevyStopExternalTransitions();
    std::vector<std::string> keys;
    for (const auto &entry : loaded) keys.push_back(entry.first);
    for (const auto &key : keys) TVPBevyUnloadExternalPlugin(ttstr(key.c_str()));
}

bool TVPBevyIsExternalPlugin(const ttstr &name) { return loaded.find(keyFor(name)) != loaded.end(); }
