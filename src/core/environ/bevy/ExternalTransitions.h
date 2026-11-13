/*
 * Transition interfaces derived from TVP2, Copyright (C) 2000 W.Dee
 * <dee@kikyou.info> and contributors. See license.txt.
 */
#pragma once
#include "tjsCommHead.h"
#include "transhandler.h"

namespace KrkrzABI {
class ScanLine {
public:
    virtual tjs_error TJS_INTF_METHOD AddRef() = 0;
    virtual tjs_error TJS_INTF_METHOD Release() = 0;
    virtual tjs_error TJS_INTF_METHOD GetWidth(tjs_int *) = 0;
    virtual tjs_error TJS_INTF_METHOD GetHeight(tjs_int *) = 0;
    virtual tjs_error TJS_INTF_METHOD GetPixelFormat(tjs_int *) = 0;
    virtual tjs_error TJS_INTF_METHOD GetPitchBytes(tjs_int *) = 0;
    virtual tjs_error TJS_INTF_METHOD GetScanLine(tjs_int, const void **) = 0;
    virtual tjs_error TJS_INTF_METHOD GetScanLineForWrite(tjs_int, void **) = 0;
};
class ImageProvider {
public:
    virtual tjs_error TJS_INTF_METHOD LoadImage(const tjs_char *, tjs_int, tjs_uint32, tjs_uint, tjs_uint, ScanLine **) = 0;
};
struct Data {
    tjs_int Left, Top, Width, Height;
    ScanLine *Dest;
    tjs_int DestLeft, DestTop;
    ScanLine *Src1;
    tjs_int Src1Left, Src1Top;
    ScanLine *Src2;
    tjs_int Src2Left, Src2Top;
};
class Handler : public iTVPBaseTransHandler {
public:
    virtual tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64) = 0;
    virtual tjs_error TJS_INTF_METHOD EndProcess() = 0;
    virtual tjs_error TJS_INTF_METHOD Process(Data *) = 0;
    virtual tjs_error TJS_INTF_METHOD MakeFinalImage(ScanLine **, ScanLine *, ScanLine *) = 0;
};
class Provider {
public:
    // krkrz has no virtual destructor in this interface.
    virtual tjs_error TJS_INTF_METHOD AddRef() = 0;
    virtual tjs_error TJS_INTF_METHOD Release() = 0;
    virtual tjs_error TJS_INTF_METHOD GetName(const tjs_char **) = 0;
    virtual tjs_error TJS_INTF_METHOD StartTransition(iTVPSimpleOptionProvider *, ImageProvider *,
        tTVPLayerType, tjs_uint, tjs_uint, tjs_uint, tjs_uint,
        tTVPTransType *, tTVPTransUpdateType *, iTVPBaseTransHandler **) = 0;
};
}
void TVPBevyAddExternalTransProvider(KrkrzABI::Provider *);
void TVPBevyRemoveExternalTransProvider(KrkrzABI::Provider *);
bool TVPBevyExternalTransitionsActive();

void TVPBevyStopExternalTransitions();
