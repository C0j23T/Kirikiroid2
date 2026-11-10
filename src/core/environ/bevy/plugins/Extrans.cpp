#define NCB_MODULE_NAME TJS_W("extrans.dll")
#include "ncbind/ncbind.hpp"
#include "ExtraTrans.h"
static BevyExtraTrans::Provider *providers[2]{};
static void registerProviders() {
    providers[0]=new BevyExtraTrans::Provider(0,TJS_W("wave")); TVPAddTransHandlerProvider(providers[0]);
    providers[1]=new BevyExtraTrans::Provider(1,TJS_W("mosaic")); TVPAddTransHandlerProvider(providers[1]);
}
static void unregisterProviders() {
    for(auto &p:providers) { if(p) { TVPRemoveTransHandlerProvider(p); p->Release(); p=nullptr; } }
}
NCB_PRE_REGIST_CALLBACK(registerProviders);
NCB_POST_UNREGIST_CALLBACK(unregisterProviders);
