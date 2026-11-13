#pragma once

#include "tjsCommHead.h"

bool TVPBevyLoadExternalPlugin(const ttstr &name);
bool TVPBevyUnloadExternalPlugin(const ttstr &name);
void TVPBevyUnloadExternalPlugins();

bool TVPBevyIsExternalPlugin(const ttstr &name);
