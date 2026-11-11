#define NCB_MODULE_NAME TJS_W("extNagano.dll")
#include "ncbind/ncbind.hpp"
#include "ExtraTrans.h"
static BevyExtraTrans::Provider *providers[3]{};
static void registerProviders() {
    const int effects[]={100,101,102};
    const tjs_char *names[]={TJS_W("blurfade"),TJS_W("book"),TJS_W("morphing")};
    for(int i=0;i<3;i++) { providers[i]=new BevyExtraTrans::Provider(effects[i],names[i]); TVPAddTransHandlerProvider(providers[i]); }
}
static void unregisterProviders() {
    for(auto &p:providers) { if(p) { TVPRemoveTransHandlerProvider(p); p->Release(); p=nullptr; } }
}
NCB_PRE_REGIST_CALLBACK(registerProviders);
NCB_POST_UNREGIST_CALLBACK(unregisterProviders);
