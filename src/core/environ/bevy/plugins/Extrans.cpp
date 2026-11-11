#define NCB_MODULE_NAME TJS_W("extrans.dll")
#include "ncbind/ncbind.hpp"
#include "ExtraTrans.h"
static BevyExtraTrans::Provider *providers[7]{};
static void registerProviders() {
    const int effects[]={0,1,2,3,4,5,6};
    const tjs_char *names[]={TJS_W("wave"),TJS_W("mosaic"),TJS_W("turn"),TJS_W("rotatezoom"),TJS_W("rotatevanish"),TJS_W("rotateswap"),TJS_W("ripple")};
    for(int i=0;i<7;i++) { providers[i]=new BevyExtraTrans::Provider(effects[i],names[i]); TVPAddTransHandlerProvider(providers[i]); }
}
static void unregisterProviders() {
    for(auto &p:providers) { if(p) { TVPRemoveTransHandlerProvider(p); p->Release(); p=nullptr; } }
}
NCB_PRE_REGIST_CALLBACK(registerProviders);
NCB_POST_UNREGIST_CALLBACK(unregisterProviders);
