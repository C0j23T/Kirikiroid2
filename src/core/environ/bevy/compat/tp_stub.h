// Compatibility entry point for Kirikiri plugin sources.
//
// The upstream krkrz tp_stub.h is generated for an independently linked
// plugin and obtains engine functions through V2Link. Bevy currently compiles
// its adapters into the host, so this header exposes the same common source
// include through the host's existing declarations. Backend-specific Bevy
// services remain outside this compatibility surface.
#pragma once

#ifndef STDCALL
#if defined(_WIN32)
#define STDCALL __stdcall
#else
#define STDCALL
#endif
#endif

#include "PluginStub.h"
#include "EventIntf.h"
#include "TransIntf.h"

#ifndef TJS_USERENTRY
#define TJS_USERENTRY TJS_INTF_METHOD
#endif

