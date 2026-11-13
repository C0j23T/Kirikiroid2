#include "ncbind/ncbind.hpp"
#include <set>
extern std::set<ttstr> TVPRegisteredPlugins;

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
	if(name == TJS_W("kagparserex.dll")) return true;
	if(name == TJS_W("layerextext.dll")) name = TJS_W("layerexdraw.dll");
	if(TVPRegisteredPlugins.find(name) != TVPRegisteredPlugins.end()) return true;
	return ncbAutoRegister::LoadModule(name);
}
