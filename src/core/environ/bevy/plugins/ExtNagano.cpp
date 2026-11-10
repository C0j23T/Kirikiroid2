#define NCB_MODULE_NAME TJS_W("extNagano.dll")
#include "ncbind/ncbind.hpp"
#include "ExtraTrans.h"
static BevyExtraTrans::Provider *providers[1]{};
static void registerProviders() {
    providers[0]=new BevyExtraTrans::Provider(2,TJS_W("blurfade")); TVPAddTransHandlerProvider(providers[0]);
}
static void unregisterProviders() {
    for(auto &p:providers) { if(p) { TVPRemoveTransHandlerProvider(p); p->Release(); p=nullptr; } }
}
NCB_PRE_REGIST_CALLBACK(registerProviders);
NCB_POST_UNREGIST_CALLBACK(unregisterProviders);
