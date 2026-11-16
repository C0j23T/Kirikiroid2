#include "ncbind/ncbind.hpp"
#include <set>
extern std::set<ttstr> TVPRegisteredPlugins;

namespace {

const std::set<ttstr> ignoredPlugins = {
    TJS_W("expat.dll"),
    TJS_W("lzfs.dll"),
    TJS_W("kagparserex.dll"),
    TJS_W("krmovie.dll"),
    TJS_W("util.dll"),
    TJS_W("wuvorbis.dll"),
    TJS_W("wumsadp.dll"),
    TJS_W("filter.dll"),
};

} // namespace

void TVPLoadInternalPlugins()
{
    ncbAutoRegister::AllRegist();
	ncbAutoRegister::LoadModule(TJS_W("xp3filter.dll"));
}

void TVPUnloadInternalPlugins()
{
    ncbAutoRegister::AllUnregist();
}

bool TVPLoadInternalPlugin(const ttstr &_name)
{
	auto name = TVPExtractStorageName(_name).AsLowerCase();
	if(ignoredPlugins.find(name) != ignoredPlugins.end()) return true;
	if(name == TJS_W("layerextext.dll")) name = TJS_W("layerexdraw.dll");
	if(TVPRegisteredPlugins.find(name) != TVPRegisteredPlugins.end()) return true;
	return ncbAutoRegister::LoadModule(name);
}
