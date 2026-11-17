// Adapted from krkrsdl3 plugins/PackinOne.cpp; see LICENSE-THIRD-PARTY.md.
#define NCB_MODULE_NAME TJS_W("PackinOne.dll")
#include "ncbind/ncbind.hpp"

static void LoadModule(const tjs_char *name)
{
	try
	{
		ncbAutoRegister::LoadModule(name);
	}
	catch (...)
	{
	}
}

static void InitPlugin_PackinOne()
{
	static const tjs_char *modules[] = {
		TJS_W("layerExBTOA.dll"),
		TJS_W("layerExImage.dll"),
		TJS_W("layerExRaster.dll"),
		TJS_W("shrinkCopy.dll"),
		TJS_W("addFont.dll"),
		TJS_W("scriptsEx.dll"),
		TJS_W("fstat.dll"),
		TJS_W("saveStruct.dll"),
	};
	for (const tjs_char *module : modules)
		LoadModule(module);
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_PackinOne);
