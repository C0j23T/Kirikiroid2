#include "Platform.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/resource.h>

#include "krkr_shim.h"
#include "krkr_storage.h"
void TVPGetMemoryInfo(TVPMemoryInfo &m)
{
    uint64_t values[6] = {};
    krkr_host_memory(values);
    m.MemTotal = values[0];
    m.MemFree = values[1];
    m.SwapTotal = values[2];
    m.SwapFree = values[3];
    m.VirtualTotal = values[4];
    m.VirtualUsed = values[5];
}

#include <sched.h>
void TVPRelinquishCPU(){
	sched_yield();
}

void TVP_utime(const char *name, time_t modtime) {
    krkr_fs_set_modified(name, modtime);
}
