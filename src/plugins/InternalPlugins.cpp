#include "ncbind/ncbind.hpp"
//extern void InitPlugin_CSVParser();

void TVPLoadInternalPlugins()
{
    ncbAutoRegister::AllRegist();
	ncbAutoRegister::LoadModule(TJS_W("xp3filter.dll"));
    //InitPlugin_CSVParser();
}

void TVPUnloadInternalPlugins()
{
    ncbAutoRegister::AllUnregist();
}

bool TVPLoadInternalPlugin(const ttstr &_name)
{
	auto name = TVPExtractStorageName(_name).AsLowerCase();
	if(name == TJS_W("layerextext.dll")) name = TJS_W("layerExDraw.dll");
	return ncbAutoRegister::LoadModule(name);
}