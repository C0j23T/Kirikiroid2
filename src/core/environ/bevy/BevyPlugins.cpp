// bevy port: internal (compiled-in) ncbind plugins.
//
// Kirikiroid2 compiles its plugins into the engine rather than loading DLLs:
// each `src/plugins/*.cpp` registers itself with ncbind's auto-register tables
// under its module name (`NCB_MODULE_NAME`, e.g. "addFont.dll"). A script
// attaches one when it needs it, `Plugins.link("addFont.dll")`, which reaches
// `ncbAutoRegister::LoadModule`; the tables it looks in are filled by
// `ncbAutoRegister::AllRegist`.
//
// Upstream `tTVPApplication::StartApplication` runs `AllRegist` from
// `TVPLoadPluigins`; that function also pulls in the cocos2d-x Application
// (and ffmpeg through it), so the port collects the plugins itself.
//
// A plugin a game does not link but expects to be there is attached at
// start-up instead, the way upstream loads `xp3filter.dll`.
//
// Which plugins exist is `PLUGINS` in bevy_krkr's `codegen/src/port.rs`: one
// entry per plugin, which is also what decides the module names below and the
// wrapper translation units the build script generates.
#include "tjsCommHead.h"

#include <set>

#include "krkr_plugins.h" // generated from PLUGINS by krkr_sys's build script
#include "ncbind/ncbind.hpp"
#include "ExternalPlugins.h"

#define KRKR_KEEP_ALIVE(stem, module) TVPBevyPlugin_##stem();

#define KRKR_LOAD_MODULE(module) ncbAutoRegister::LoadModule(TJS_W(module));

extern std::set<ttstr> TVPRegisteredPlugins;

void TVPBevyLoadInternalPlugins() {
    KRKR_PLUGINS(KRKR_KEEP_ALIVE)
    static bool collected = false;
    if (!collected) {
        collected = true;
        ncbAutoRegister::AllRegist();
    }
    KRKR_INTERNAL_PLUGINS(KRKR_LOAD_MODULE)
}

void TVPBevyUnloadInternalPlugins() {
    TVPBevyUnloadExternalPlugins();
    ncbAutoRegister::AllUnregist();
    TVPRegisteredPlugins.clear();
}
