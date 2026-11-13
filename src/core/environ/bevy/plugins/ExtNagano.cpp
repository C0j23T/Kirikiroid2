#define NCB_MODULE_NAME TJS_W("extNagano.dll")
#include "ncbind/ncbind.hpp"
#include "ExtraTrans.h"
// extNagano algorithms: ヤマモト / Shun / chiyoclone.net / シノハラ; Kirikiri license.
// Source and full license: LICENSE-THIRD-PARTY.md.
#pragma once
#include "TransIntf.h"
#include "RenderManager.h"
#include "LayerIntf.h"
#include "GraphicsLoaderIntf.h"
#include "BevyRender.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <array>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
namespace Nagano {
using Records = std::vector<std::array<float,4>>;

// Rule images are decoded on a worker thread (the transition holds at phase 0
// until the pixels arrive), so a handler that samples one has to ask whether
// they are there yet.
class iTVPPendingRule
{
public:
	virtual ~iTVPPendingRule() {}
	virtual bool Ready() = 0;
	virtual void Poll() = 0;
};
namespace Universal {
// Connects a handler to the decoder; the provider itself is defined with the
// 3D universal handler, which is the only user of the decoded pixels as texture.
iTVPScanLineProvider *CreatePendingRuleProvider(const ttstr &name, tjs_uint desh,
	iTVPPendingRule **pending);
}
inline tjs_error Draw(tTVPDivisibleData *d,int w,int h,int kind,std::array<float,8> v={},const Records &records={},uint64_t rule=0) {
 TVPBevyRenderCommand c{}; c.operation=4; c.method="ExtNagano";
 c.target=TVPBevyTextureID(d->Dest->GetTextureForRender());
 c.sources[0]=TVPBevyTextureID(d->Src1->GetTexture()); c.sources[1]=TVPBevyTextureID(d->Src2->GetTexture()); c.sources[2]=rule;
 c.rect[0]=d->DestLeft;c.rect[1]=d->DestTop;c.rect[2]=d->DestLeft+d->Width;c.rect[3]=d->DestTop+d->Height;
 c.parameters[0]=kind;c.parameters[2]=d->Left;c.parameters[3]=d->Top;
 c.parameters[4]=w;c.parameters[5]=h;
 for(int i=0;i<8;i++)c.parameters[6+i]=v[i];
 for(int i=0;i<2;i++){ c.source_rects[i][0]=(i?d->Src2Left:d->Src1Left)-d->Left; c.source_rects[i][1]=(i?d->Src2Top:d->Src1Top)-d->Top; }
 c.data=const_cast<std::array<float,4>*>(records.data());c.pitch=records.size();
 return krkr_render_dispatch(&c)==0?TJS_S_OK:TJS_E_FAIL;
}
inline tjs_error ReadLine(iTVPScanLineProvider *p,int y,const void **out) {
 auto t=p->GetTexture(); if(!t)return TJS_E_FAIL; *out=t->GetScanLineForRead(y);return *out?TJS_S_OK:TJS_E_FAIL;
}

namespace scanline {





class tTVPScanLineTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;
protected:
	tjs_uint64 StartTick;
	tjs_uint64 Time;
	tjs_int Width;
	tjs_int Height;
	tjs_int CurPos;
	tjs_int FrameCount;
	bool First;
public:
	tTVPScanLineTransHandler(tjs_uint64 time, tjs_int width, tjs_int height)
		: Time(time), Width(width), Height(height), CurPos(0), FrameCount(0), First(true)
	{
		RefCount = 1;
	}
	virtual ~tTVPScanLineTransHandler() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *options) { return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick);
	tjs_error TJS_INTF_METHOD EndProcess();
	tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data);
	tjs_error TJS_INTF_METHOD MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2;
		return TJS_S_OK;
	}
};
tjs_error TJS_INTF_METHOD tTVPScanLineTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}
	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed >= Time)
		CurPos = Width;
	else
		CurPos = (tjs_int)(Width * elapsed / Time);
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPScanLineTransHandler::EndProcess()
{
	if(CurPos == Width) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPScanLineTransHandler::Process(tTVPDivisibleData *data) { return Draw(data,Width,Height,0,{float(CurPos)}); }
class tTVPScanLineTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPScanLineTransHandlerProvider() { RefCount = 1; }
	~tTVPScanLineTransHandlerProvider() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_W("scanline");
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD StartTransition(
			iTVPSimpleOptionProvider *options,
			iTVPSimpleImageProvider *imagepro,
			tTVPLayerType layertype,
			tjs_uint src1w, tjs_uint src1h,
			tjs_uint src2w, tjs_uint src2h,
			tTVPTransType *type,
			tTVPTransUpdateType * updatetype,
			iTVPBaseTransHandler ** handler)
	{
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;
		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;
		*handler = new tTVPScanLineTransHandler(time, src1w, src1h);
		return TJS_S_OK;
	}
} static * ScanLineTransHandlerProvider;
void RegisterScanLineTransHandlerProvider()
{
	ScanLineTransHandlerProvider = new tTVPScanLineTransHandlerProvider();
	TVPAddTransHandlerProvider(ScanLineTransHandlerProvider);
}
void UnregisterScanLineTransHandlerProvider()
{
	if(!ScanLineTransHandlerProvider) return;
	TVPRemoveTransHandlerProvider(ScanLineTransHandlerProvider);
	ScanLineTransHandlerProvider->Release();
	ScanLineTransHandlerProvider = nullptr;
}

}

namespace zoomfade {














static inline tjs_int ClampI(tjs_int v, tjs_int lo, tjs_int hi)
{
	if(v < lo) v = lo;
	if(v > hi) v = hi;
	return v;
}
class tTVPZoomFadeTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;
protected:
	tjs_uint64 StartTick;
	tjs_uint64 Time;
	tjs_uint64 CurElapsed;
	tjs_int Width;
	tjs_int Height;
	double Zoom1;
	double Zoom2;
	tjs_int Phase;
	double SrcScale;
	double DestScale;
	bool First;
	tjs_int * XMap1;
	tjs_int * XMap2;
	tjs_int FrameCount;
public:
	tTVPZoomFadeTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			tjs_int zoom1, tjs_int zoom2)
		: StartTick(0), Time(time), CurElapsed(0), Width(width), Height(height),
		  Zoom1(zoom1 / 100.0), Zoom2(zoom2 / 100.0), Phase(0),
		  SrcScale(1.0), DestScale(1.0), First(true),
		  XMap1(0), XMap2(0), FrameCount(0)
	{
		RefCount = 1;
		if(Width > 0)
		{
			XMap1 = new tjs_int[Width];
			XMap2 = new tjs_int[Width];
		}
	}
	virtual ~tTVPZoomFadeTransHandler()
	{
		if(XMap1) delete [] XMap1;
		if(XMap2) delete [] XMap2;
	}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *options) { return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick);
	tjs_error TJS_INTF_METHOD EndProcess();
	tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data);
	tjs_error TJS_INTF_METHOD MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2;
		return TJS_S_OK;
	}
};
tjs_error TJS_INTF_METHOD tTVPZoomFadeTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}
	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed > Time) elapsed = Time;
	CurElapsed = elapsed;

	tjs_int phase = (tjs_int)((elapsed * 255) / Time);
	if(phase > 255) phase = 255;
	Phase = phase;


	SrcScale  = 1.0 / (((Zoom1 - 1.0) * phase) / 255.0 + 1.0);
	DestScale = 1.0 / (Zoom2 - ((Zoom2 - 1.0) * phase) / 255.0);
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPZoomFadeTransHandler::EndProcess()
{
	if(Phase == 255) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPZoomFadeTransHandler::Process(tTVPDivisibleData *data) { return Draw(data,Width,Height,1,{float(Phase),float(SrcScale),float(DestScale)}); }
class tTVPZoomFadeTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPZoomFadeTransHandlerProvider() { RefCount = 1; }
	~tTVPZoomFadeTransHandlerProvider() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_W("zoomfade");
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD StartTransition(
			iTVPSimpleOptionProvider *options,
			iTVPSimpleImageProvider *imagepro,
			tTVPLayerType layertype,
			tjs_uint src1w, tjs_uint src1h,
			tjs_uint src2w, tjs_uint src2h,
			tTVPTransType *type,
			tTVPTransUpdateType * updatetype,
			iTVPBaseTransHandler ** handler)
	{
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;
		tTJSVariant tmp;

		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		tjs_int zoom1 = 100;
		tjs_int zoom2 = 200;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("zoom1"), &tmp)) && tmp.Type() != tvtVoid)
			zoom1 = (tjs_int)(tjs_int64)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("zoom2"), &tmp)) && tmp.Type() != tvtVoid)
			zoom2 = (tjs_int)(tjs_int64)tmp;
		*handler = new tTVPZoomFadeTransHandler(time, src1w, src1h, zoom1, zoom2);
		return TJS_S_OK;
	}
} static * ZoomFadeTransHandlerProvider;
void RegisterZoomFadeTransHandlerProvider()
{
	ZoomFadeTransHandlerProvider = new tTVPZoomFadeTransHandlerProvider();
	TVPAddTransHandlerProvider(ZoomFadeTransHandlerProvider);
}
void UnregisterZoomFadeTransHandlerProvider()
{
	if(!ZoomFadeTransHandlerProvider) return;
	TVPRemoveTransHandlerProvider(ZoomFadeTransHandlerProvider);
	ZoomFadeTransHandlerProvider->Release();
	ZoomFadeTransHandlerProvider = nullptr;
}

}

namespace rgbfade {












static inline tjs_int ClampI(tjs_int v, tjs_int lo, tjs_int hi)
{
	if(v < lo) v = lo;
	if(v > hi) v = hi;
	return v;
}
class tTVPRGBFadeTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;
protected:
	tjs_uint64 StartTick;
	tjs_uint64 Time;
	tjs_uint64 CurElapsed;
	tjs_int Width;
	tjs_int Height;
	tjs_int OpaR;
	tjs_int OpaG;
	tjs_int OpaB;
	tjs_int OpaA;
	tjs_int64 DelayRTime;
	tjs_int64 DelayGTime;
	tjs_int64 DelayBTime;
	tjs_int64 DelayATime;
	tjs_int64 EffDuration;
	bool First;
	tjs_int FrameCount;
public:
	tTVPRGBFadeTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			tjs_int delayR, tjs_int delayG, tjs_int delayB, tjs_int delayA)
		: StartTick(0), Time(time), CurElapsed(0), Width(width), Height(height),
		  OpaR(0), OpaG(0), OpaB(0), OpaA(0), First(true), FrameCount(0)
	{
		RefCount = 1;

		DelayRTime = (tjs_int64)delayR * (tjs_int64)time / 255;
		DelayGTime = (tjs_int64)delayG * (tjs_int64)time / 255;
		DelayBTime = (tjs_int64)delayB * (tjs_int64)time / 255;
		DelayATime = (tjs_int64)delayA * (tjs_int64)time / 255;

		tjs_int maxDelay = delayR;
		if(delayG > maxDelay) maxDelay = delayG;
		if(delayB > maxDelay) maxDelay = delayB;
		if(delayA > maxDelay) maxDelay = delayA;
		EffDuration = (tjs_int64)(255 - maxDelay) * (tjs_int64)time / 255;
		if(EffDuration < 1) EffDuration = 1;
	}
	virtual ~tTVPRGBFadeTransHandler() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *options) { return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick);
	tjs_error TJS_INTF_METHOD EndProcess();
	tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data);
	tjs_error TJS_INTF_METHOD MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2;
		return TJS_S_OK;
	}
};
tjs_error TJS_INTF_METHOD tTVPRGBFadeTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}
	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed > Time) elapsed = Time;
	CurElapsed = elapsed;

	tjs_int64 e = (tjs_int64)elapsed;
	OpaR = ClampI((tjs_int)(((e - DelayRTime) * 255) / EffDuration), 0, 255);
	OpaG = ClampI((tjs_int)(((e - DelayGTime) * 255) / EffDuration), 0, 255);
	OpaB = ClampI((tjs_int)(((e - DelayBTime) * 255) / EffDuration), 0, 255);
	OpaA = ClampI((tjs_int)(((e - DelayATime) * 255) / EffDuration), 0, 255);
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPRGBFadeTransHandler::EndProcess()
{
	if(CurElapsed == Time) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPRGBFadeTransHandler::Process(tTVPDivisibleData *data) { return Draw(data,Width,Height,2,{float(OpaR),float(OpaG),float(OpaB),float(OpaA)}); }
class tTVPRGBFadeTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPRGBFadeTransHandlerProvider() { RefCount = 1; }
	~tTVPRGBFadeTransHandlerProvider() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_W("rgbfade");
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD StartTransition(
			iTVPSimpleOptionProvider *options,
			iTVPSimpleImageProvider *imagepro,
			tTVPLayerType layertype,
			tjs_uint src1w, tjs_uint src1h,
			tjs_uint src2w, tjs_uint src2h,
			tTVPTransType *type,
			tTVPTransUpdateType * updatetype,
			iTVPBaseTransHandler ** handler)
	{
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;
		tTJSVariant tmp;

		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		tjs_int delayR = 0, delayG = 0, delayB = 0, delayA = 0;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("delayR"), &tmp)) && tmp.Type() != tvtVoid)
			delayR = ClampI((tjs_int)(tjs_int64)tmp, 0, 255);
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("delayG"), &tmp)) && tmp.Type() != tvtVoid)
			delayG = ClampI((tjs_int)(tjs_int64)tmp, 0, 255);
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("delayB"), &tmp)) && tmp.Type() != tvtVoid)
			delayB = ClampI((tjs_int)(tjs_int64)tmp, 0, 255);
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("delayA"), &tmp)) && tmp.Type() != tvtVoid)
			delayA = ClampI((tjs_int)(tjs_int64)tmp, 0, 255);
		*handler = new tTVPRGBFadeTransHandler(time, src1w, src1h,
				delayR, delayG, delayB, delayA);
		return TJS_S_OK;
	}
} static * RGBFadeTransHandlerProvider;
void RegisterRGBFadeTransHandlerProvider()
{
	RGBFadeTransHandlerProvider = new tTVPRGBFadeTransHandlerProvider();
	TVPAddTransHandlerProvider(RGBFadeTransHandlerProvider);
}
void UnregisterRGBFadeTransHandlerProvider()
{
	if(!RGBFadeTransHandlerProvider) return;
	TVPRemoveTransHandlerProvider(RGBFadeTransHandlerProvider);
	RGBFadeTransHandlerProvider->Release();
	RGBFadeTransHandlerProvider = nullptr;
}

}

namespace spin {




























#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static inline tjs_int RoundToInt(double v)
{
	return (tjs_int)floor(v + 0.5);
}

class tTVPSpinFadeTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;
protected:
	tjs_uint64 StartTick;
	tjs_uint64 Time;
	tjs_uint64 CurPos;
	tjs_int    Width;
	tjs_int    Height;
	double     Dist;


	tjs_int    Type1;
	tjs_int    Type2;
	tjs_int   *Col1;
	tjs_int   *Col2;
	tjs_int   *Slope1;
	tjs_int   *Slope2;
	tjs_int   *Select;
	bool       First;
	tjs_int    FrameCount;
private:
	void ComputeColumns(tjs_int type, double theta, tjs_int *col, tjs_int *slope);
public:
	tTVPSpinFadeTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			tjs_int type1, tjs_int type2)
		: StartTick(0), Time(time), CurPos(0), Width(width), Height(height),
		  Type1(type1), Type2(type2), First(true), FrameCount(0)
	{
		RefCount = 1;
		Dist = (double)(width * 2);
		Col1   = new tjs_int[width];
		Col2   = new tjs_int[width];
		Slope1 = new tjs_int[width];
		Slope2 = new tjs_int[width];
		Select = new tjs_int[width];
	}
	virtual ~tTVPSpinFadeTransHandler()
	{
		delete [] Col1;
		delete [] Col2;
		delete [] Slope1;
		delete [] Slope2;
		delete [] Select;
	}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *options) { return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick);
	tjs_error TJS_INTF_METHOD EndProcess();
	tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data);
	tjs_error TJS_INTF_METHOD MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2;
		return TJS_S_OK;
	}
};








void tTVPSpinFadeTransHandler::ComputeColumns(tjs_int type, double theta,
		tjs_int *col, tjs_int *slope)
{
	const tjs_int W = Width;
	const double  D = Dist;
	if(type == -1)
	{
		for(tjs_int x = 0; x < W; x++) { col[x] = x; slope[x] = 256; }
		return;
	}

	double axisSrc, axisScreen;
	if(type == 4 || type == 5)      { axisSrc = 0.0;          axisScreen = 0.0;          }
	else if(type == 6 || type == 7) { axisSrc = (double)W;    axisScreen = (double)W;    }
	else                            { axisSrc = W * 0.5;      axisScreen = W * 0.5;      }
	const double c = cos(theta);
	const double s = sin(theta);
	for(tjs_int x = 0; x < W; x++)
	{

		const double u = (double)x - axisScreen;
		const double denom = D * c - u * s;
		bool valid = (fabs(denom) > 1e-6);
		tjs_int sxi = W;
		tjs_int slp = 256;
		if(valid)
		{
			const double X0  = u * D / denom;
			const double sx  = axisSrc + X0;
			const double zpd = X0 * s + D;
			if(zpd > 1e-6 && sx >= 0.0 && sx < (double)W)
			{
				sxi = RoundToInt(sx);
				if(sxi < 0) sxi = 0; else if(sxi >= W) sxi = W - 1;
				double sl = 256.0 * zpd / D;
				slp = RoundToInt(sl);
				if(slp < 1) slp = 1;
			}
			else
			{
				sxi = W;
			}
		}
		col[x]   = sxi;
		slope[x] = slp;
	}
}
tjs_error TJS_INTF_METHOD tTVPSpinFadeTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}
	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed >= Time) elapsed = Time;
	if(elapsed == 0)    elapsed = 1;
	CurPos = elapsed;

	const double r = (double)CurPos / (double)Time;



	const double sign1 = ((Type1 & 1) == 0) ? 1.0 : -1.0;
	const double sign2 = -(((Type2 & 1) == 0) ? 1.0 : -1.0);
	const double theta1 = (Type1 == -1) ? 0.0 : sign1 * (M_PI * 0.5) * r;
	const double theta2 = (Type2 == -1) ? 0.0 : sign2 * (M_PI * 0.5) * (r - 1.0);
	ComputeColumns(Type1, theta1, Col1, Slope1);
	ComputeColumns(Type2, theta2, Col2, Slope2);


	for(tjs_int x = 0; x < Width; x++)
	{
		tjs_int v1 = Col1[x];  bool ok1 = (v1 >= 0 && v1 < Width);
		tjs_int v2 = Col2[x];  bool ok2 = (v2 >= 0 && v2 < Width);
		if(!ok1)
			Select[x] = ok2 ? 1 : -1;
		else if(ok2 && Slope2[x] < Slope1[x])
			Select[x] = 1;
		else
			Select[x] = 0;
	}
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPSpinFadeTransHandler::EndProcess()
{
	if(CurPos == Time) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPSpinFadeTransHandler::Process(tTVPDivisibleData *data) { Records r(Width);for(int x=0;x<Width;x++)r[x]={float(Select[x]),float(Col1[x]),float(Col2[x]),float(Select[x]==0?Slope1[x]:Slope2[x])};return Draw(data,Width,Height,3,{},r); }
class tTVPSpinFadeTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPSpinFadeTransHandlerProvider() { RefCount = 1; }
	~tTVPSpinFadeTransHandlerProvider() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_W("spin");
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD StartTransition(
			iTVPSimpleOptionProvider *options,
			iTVPSimpleImageProvider *imagepro,
			tTVPLayerType layertype,
			tjs_uint src1w, tjs_uint src1h,
			tjs_uint src2w, tjs_uint src2h,
			tTVPTransType *type,
			tTVPTransUpdateType * updatetype,
			iTVPBaseTransHandler ** handler)
	{
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;
		tTJSVariant tmp;

		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		tjs_int type1 = 0;
		tjs_int type2 = 1;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("type1"), &tmp)) && tmp.Type() != tvtVoid)
			type1 = (tjs_int)(tjs_int64)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("type2"), &tmp)) && tmp.Type() != tvtVoid)
			type2 = (tjs_int)(tjs_int64)tmp;
		*handler = new tTVPSpinFadeTransHandler(time, src1w, src1h, type1, type2);
		return TJS_S_OK;
	}
} static * SpinFadeTransHandlerProvider;
void RegisterSpinFadeTransHandlerProvider()
{
	SpinFadeTransHandlerProvider = new tTVPSpinFadeTransHandlerProvider();
	TVPAddTransHandlerProvider(SpinFadeTransHandlerProvider);
}
void UnregisterSpinFadeTransHandlerProvider()
{
	if(!SpinFadeTransHandlerProvider) return;
	TVPRemoveTransHandlerProvider(SpinFadeTransHandlerProvider);
	SpinFadeTransHandlerProvider->Release();
	SpinFadeTransHandlerProvider = nullptr;
}

}

namespace flutter {

















class tTVPFlutterTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;
protected:
	tjs_uint64 StartTick;
	tjs_uint64 Time;
	tjs_uint64 CurElapsed;
	tjs_int Width;
	tjs_int Height;
	tjs_int Phase;
	tjs_int CurSlip;
	tjs_int Slip;
	tjs_int Alpha;
	tjs_uint32 BackColor;
	tjs_int BackAlpha;
	tjs_uint32 *BackBuf;
	bool First;
	bool Second;
	bool BackBuilt;
	tjs_int FrameCount;
	void BuildBack(tTVPDivisibleData *data);
public:
	tTVPFlutterTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
		tjs_uint32 back, tjs_int alpha, tjs_int slip)
		: StartTick(0), Time(time), CurElapsed(0), Width(width), Height(height),
		  Phase(0), CurSlip(0), Slip(slip), Alpha(alpha & 0xff),
		  BackColor(back | 0xff000000), BackAlpha((back >> 24) & 0xff),
		  First(true), Second(true), BackBuilt(false), FrameCount(0)
	{
		RefCount = 1;
		BackBuf = new tjs_uint32[(size_t)width * height];
	}
	virtual ~tTVPFlutterTransHandler()
	{
		delete [] BackBuf;
	}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *options) { return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick);
	tjs_error TJS_INTF_METHOD EndProcess();
	tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data);
	tjs_error TJS_INTF_METHOD MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2;
		return TJS_S_OK;
	}
};




tjs_error TJS_INTF_METHOD tTVPFlutterTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
		Phase = 0;
		CurSlip = 0;
	}
	else if(Second)
	{

		Second = false;
		StartTick = tick - 1;
	}
	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed > Time) elapsed = Time;
	CurElapsed = elapsed;

	tjs_int L = Width + Height;
	tjs_uint64 t = (tjs_uint64)L * elapsed;
	t = t * elapsed;
	t = t / Time;
	t = t / Time;
	tjs_int phase = (tjs_int)t;
	if(phase > L) phase = L;
	Phase = phase;

	tjs_uint64 s = (tjs_uint64)Slip * elapsed;
	s = s * elapsed;
	s = s * elapsed;
	s = s / Time;
	s = s / Time;
	s = s / Time;
	CurSlip = (tjs_int)s;
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPFlutterTransHandler::EndProcess()
{
	if(CurElapsed == Time) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPFlutterTransHandler::Process(tTVPDivisibleData *data) { return Draw(data,Width,Height,4,{float(Phase),float(CurSlip),float(Alpha),float(BackAlpha),float((BackColor>>16)&255),float((BackColor>>8)&255),float(BackColor&255)}); }
class tTVPFlutterTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPFlutterTransHandlerProvider() { RefCount = 1; }
	~tTVPFlutterTransHandlerProvider() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_W("flutter");
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD StartTransition(
			iTVPSimpleOptionProvider *options,
			iTVPSimpleImageProvider *imagepro,
			tTVPLayerType layertype,
			tjs_uint src1w, tjs_uint src1h,
			tjs_uint src2w, tjs_uint src2h,
			tTVPTransType *type,
			tTVPTransUpdateType * updatetype,
			iTVPBaseTransHandler ** handler)
	{
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;

		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		tjs_uint32 back = 0;
		tjs_int slip = 8;
		tjs_int alpha = 0xff;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("back"), &tmp)) && tmp.Type() != tvtVoid)
			back = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("slip"), &tmp)) && tmp.Type() != tvtVoid)
			slip = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("alpha"), &tmp)) && tmp.Type() != tvtVoid)
			alpha = (tjs_int)tmp;

		tjs_int maxslip = (src1h <= src1w) ? (tjs_int)src1h : (tjs_int)src1w;
		if(slip < 0) slip = 0;
		if(slip > maxslip) slip = maxslip;
		*handler = new tTVPFlutterTransHandler(time, src1w, src1h, back, alpha, slip);
		return TJS_S_OK;
	}
} static * FlutterTransHandlerProvider;
void RegisterFlutterTransHandlerProvider()
{
	FlutterTransHandlerProvider = new tTVPFlutterTransHandlerProvider();
	TVPAddTransHandlerProvider(FlutterTransHandlerProvider);
}
void UnregisterFlutterTransHandlerProvider()
{
	if(!FlutterTransHandlerProvider) return;
	TVPRemoveTransHandlerProvider(FlutterTransHandlerProvider);
	FlutterTransHandlerProvider->Release();
	FlutterTransHandlerProvider = nullptr;
}

}

namespace book {






















static inline tjs_int ClampI(tjs_int v, tjs_int lo, tjs_int hi)
{
	if(v < lo) v = lo;
	if(v > hi) v = hi;
	return v;
}
static inline tjs_uint32 Darken(tjs_uint32 c, tjs_uint32 f)
{
	tjs_uint32 r, t;
	t = c & 0x0000ff; r  = ((t - ((t * f) >> 8)) & 0x0000ff);
	t = c & 0x00ff00; r |= ((t - ((t * f) >> 8)) & 0x00ff00);
	t = c & 0xff0000; r |= ((t - ((t * f) >> 8)) & 0xff0000);
	r |= (c & 0xff000000);
	return r;
}
static inline tjs_uint32 Lighten(tjs_uint32 c, tjs_uint32 f)
{
	tjs_uint32 r, t;
	t = c & 0x0000ff; r  = ((((0x0000ff - t) * f) >> 8) + t) & 0x0000ff;
	t = c & 0x00ff00; r |= ((((0x00ff00 - t) * f) >> 8) + t) & 0x00ff00;
	t = c & 0xff0000; r |= ((((0xff0000 - t) * f) >> 8) + t) & 0xff0000;
	r |= (c & 0xff000000);
	return r;
}
static inline tjs_uint32 BlendAlpha(tjs_uint32 a, tjs_uint32 b, tjs_uint32 alpha)
{

	tjs_uint32 r, ta, tb;
	ta = a & 0x0000ff; tb = b & 0x0000ff; r  = (((tb - ta) * alpha >> 8) + ta) & 0x0000ff;
	ta = a & 0x00ff00; tb = b & 0x00ff00; r |= (((tb - ta) * alpha >> 8) + ta) & 0x00ff00;
	ta = a & 0xff0000; tb = b & 0xff0000; r |= (((tb - ta) * alpha >> 8) + ta) & 0xff0000;
	return r;
}


class tTVPBookTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;
protected:
	tjs_uint64 StartTick;
	tjs_uint64 Time;
	tjs_uint64 CurTime;
	tjs_int Width;
	tjs_int Height;
	tjs_int Phase;
	tjs_int Dir;
	tjs_int FrameCount;
	bool First;
	void PaintLineRL(tjs_uint32 *dp, const tjs_uint32 *src1, const tjs_uint32 *src2,
			tjs_int left, tjs_int right);
	void PaintLineLR(tjs_uint32 *dp, const tjs_uint32 *src1, const tjs_uint32 *src2,
			tjs_int left, tjs_int right);
public:
	tTVPBookTransHandler(tjs_uint64 time, tjs_int width, tjs_int height, tjs_int dir)
		: Time(time), CurTime(0), Width(width), Height(height),
		  Phase(0), Dir(dir), FrameCount(0), First(true)
	{
		RefCount = 1;
	}
	virtual ~tTVPBookTransHandler() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *options) { return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick);
	tjs_error TJS_INTF_METHOD EndProcess();
	tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data);
	tjs_error TJS_INTF_METHOD MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2;
		return TJS_S_OK;
	}
};

tjs_error TJS_INTF_METHOD tTVPBookTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
		Phase = 0;
	}

	CurTime = tick - StartTick;
	if(CurTime > Time) CurTime = Time;

	tjs_uint64 half = (tjs_uint64)(Width / 2);
	tjs_uint64 v = half * CurTime;
	v = v * CurTime;
	v = v / Time;
	v = v / Time;
	Phase = (v < half) ? (tjs_int)v : (tjs_int)(Width / 2);
	return TJS_S_TRUE;
}

tjs_error TJS_INTF_METHOD tTVPBookTransHandler::EndProcess()
{
	if(CurTime == Time) return TJS_S_FALSE;
	return TJS_S_TRUE;
}

tjs_error TJS_INTF_METHOD tTVPBookTransHandler::Process(tTVPDivisibleData *data) { return Draw(data,Width,Height,5,{float(Phase),float(Dir)}); }





class tTVPBookTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPBookTransHandlerProvider() { RefCount = 1; }
	~tTVPBookTransHandlerProvider() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error TJS_INTF_METHOD GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_W("book");
		return TJS_S_OK;
	}

	tjs_error TJS_INTF_METHOD StartTransition(
			iTVPSimpleOptionProvider *options,
			iTVPSimpleImageProvider *imagepro,
			tTVPLayerType layertype,
			tjs_uint src1w, tjs_uint src1h,
			tjs_uint src2w, tjs_uint src2h,
			tTVPTransType *type,
			tTVPTransUpdateType * updatetype,
			iTVPBaseTransHandler ** handler)
	{
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;

		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		tjs_int dir = -1;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("dir"), &tmp)))
			if(tmp.Type() != tvtVoid) dir = (tjs_int)tmp;
		if(dir == -1)
			dir = rand() & 1;
		if(dir != 1) dir = 0;
		*handler = new tTVPBookTransHandler(time, src1w, src1h, dir);
		return TJS_S_OK;
	}
} static * BookTransHandlerProvider;
void RegisterBookTransHandlerProvider()
{
	BookTransHandlerProvider = new tTVPBookTransHandlerProvider();
	TVPAddTransHandlerProvider(BookTransHandlerProvider);
}
void UnregisterBookTransHandlerProvider()
{
	if(!BookTransHandlerProvider) return;
	TVPRemoveTransHandlerProvider(BookTransHandlerProvider);
	BookTransHandlerProvider->Release();
	BookTransHandlerProvider = nullptr;
}

}

namespace imagewipe {


























class tTVPImageWipeTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;
protected:
	tjs_uint64 StartTick;
	tjs_uint64 Time;
	tjs_int Width;
	tjs_int Height;
	tjs_uint64 Elapsed;
	tjs_int Threshold;
	iTVPScanLineProvider *Rule;
	tjs_int Dir;
	bool First;
	tjs_int FrameCount;
	tjs_int RuleWidth;
	tjs_int RuleHeight;
	tjs_int *RuleEdge;
	iTVPPendingRule *Decoding;
	void Setup();
	bool EnsureRule();
public:
	tTVPImageWipeTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			iTVPScanLineProvider *rule, iTVPPendingRule *decoding, tjs_int dir)
		: StartTick(0), Time(time), Width(width), Height(height),
		  Elapsed(0), Threshold(0), Rule(rule), Decoding(decoding), Dir(dir), First(true),
		  FrameCount(0), RuleWidth(0), RuleHeight(0), RuleEdge(NULL)
	{
		RefCount = 1;
		if(Rule) Rule->AddRef();
	}
	virtual ~tTVPImageWipeTransHandler()
	{
		if(RuleEdge) delete[] RuleEdge;
		if(Rule) Rule->Release();
	}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *options) { return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick);
	tjs_error TJS_INTF_METHOD EndProcess();
	tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data);
	tjs_error TJS_INTF_METHOD MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2;
		return TJS_S_OK;
	}
};
void tTVPImageWipeTransHandler::Setup()
{

	RuleEdge = new tjs_int[Height > 0 ? Height : 1];
	RuleWidth = 0;
	RuleHeight = 0;
	if(Rule)
	{
		Rule->GetWidth(&RuleWidth);
		Rule->GetHeight(&RuleHeight);
	}
	for(tjs_int row = 0; row < Height; row++)
	{
		RuleEdge[row] = RuleWidth / 2;

		tjs_int rrow = row;
		if(RuleHeight > 0 && rrow >= RuleHeight) rrow = RuleHeight - 1;
		const tjs_uint32 *rule;
		if(!Rule || TJS_FAILED(ReadLine(Rule,rrow, (const void**)&rule)))
			continue;
		for(tjs_int col = 0; col < RuleWidth; col++)
		{
			if((rule[col] >> 24) > 0xf0)
				RuleEdge[row] = col;
		}
	}
}
// Builds the per-row table from the rule. Returns false while the rule image is
// still being decoded.
bool tTVPImageWipeTransHandler::EnsureRule()
{
	if(RuleEdge) return true;
	if(Decoding && !Decoding->Ready())
	{
		Decoding->Poll();
		if(!Decoding->Ready()) return false;
	}
	Setup();
	return true;
}
tjs_error TJS_INTF_METHOD tTVPImageWipeTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}
	if(!EnsureRule())
	{
		Elapsed = 0;
		Threshold = 0;
		return TJS_S_TRUE;
	}
	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed >= Time) elapsed = Time;
	Elapsed = elapsed;


	tjs_int64 span = (tjs_int64)(Width + RuleWidth);
	tjs_int64 num = (Dir == 0) ? (tjs_int64)elapsed : (tjs_int64)(Time - elapsed);
	Threshold = (tjs_int)(span * num / (tjs_int64)Time) - RuleWidth;
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPImageWipeTransHandler::EndProcess()
{
	if(Elapsed >= Time) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPImageWipeTransHandler::Process(tTVPDivisibleData *data) { if(!RuleEdge) { data->Dest = data->Src1; return TJS_S_OK; } Records r(Height);for(int y=0;y<Height;y++)r[y]={float(RuleEdge[y]),0,0,0};return Draw(data,Width,Height,6,{float(Threshold),float(Dir)},r,TVPBevyTextureID(Rule->GetTexture())); }
class tTVPImageWipeTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPImageWipeTransHandlerProvider() { RefCount = 1; }
	~tTVPImageWipeTransHandlerProvider() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_W("imagewipe");
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD StartTransition(
			iTVPSimpleOptionProvider *options,
			iTVPSimpleImageProvider *imagepro,
			tTVPLayerType layertype,
			tjs_uint src1w, tjs_uint src1h,
			tjs_uint src2w, tjs_uint src2h,
			tTVPTransType *type,
			tTVPTransUpdateType * updatetype,
			iTVPBaseTransHandler ** handler)
	{
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;

		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		tTJSVariant rulevar;
		if(TJS_FAILED(options->GetValue(TJS_W("rule"), &rulevar))) return TJS_E_FAIL;
		if(rulevar.Type() == tvtVoid) return TJS_E_FAIL;
		iTVPScanLineProvider *scpro = NULL;
		iTVPPendingRule *decoding = NULL;
		if(rulevar.Type() == tvtObject)
		{
			tTJSNI_BaseLayer *layer=nullptr;
 if(TJS_FAILED(rulevar.AsObjectNoAddRef()->NativeInstanceSupport(TJS_NIS_GETINSTANCE,tTJSNC_Layer::ClassID,reinterpret_cast<iTJSNativeInstance**>(&layer))) || !layer->GetMainImage()) return TJS_E_FAIL;
 scpro=new tTVPScanLineProviderForBaseBitmap(layer->GetMainImage(),false);
		}
		else
		{
			const tjs_char *rulename;
			if(TJS_FAILED(options->GetAsString(TJS_W("rule"), &rulename)) || !rulename)
				return TJS_E_FAIL;
			scpro = Universal::CreatePendingRuleProvider(ttstr(rulename), src1h, &decoding);
		}
		if(!scpro) return TJS_E_FAIL;

		tjs_int dir = 0;
		tTJSVariant dirvar;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("dir"), &dirvar)) &&
		   dirvar.Type() != tvtVoid)
			dir = (tjs_int)(tjs_int64)dirvar;
		*handler = new tTVPImageWipeTransHandler(time, src1w, src1h, scpro, decoding, dir);
		scpro->Release();
		return TJS_S_OK;
	}
} static * ImageWipeTransHandlerProvider;
void RegisterImageWipeTransHandlerProvider()
{
	ImageWipeTransHandlerProvider = new tTVPImageWipeTransHandlerProvider();
	TVPAddTransHandlerProvider(ImageWipeTransHandlerProvider);
}
void UnregisterImageWipeTransHandlerProvider()
{
	if(!ImageWipeTransHandlerProvider) return;
	TVPRemoveTransHandlerProvider(ImageWipeTransHandlerProvider);
	ImageWipeTransHandlerProvider->Release();
	ImageWipeTransHandlerProvider = nullptr;
}

}

namespace honeyturn {





























#ifndef M_SQRT3
	#define M_SQRT3 (1.7320508075688772)
#endif
#ifndef M_SQRT2
	#define M_SQRT2 (1.4142135623730951)
#endif



struct tTVPHoneyCell
{
	tjs_int cx;
	tjs_int cy;
	tjs_int half;
	tjs_int delay;
};


static const tjs_int HONEY_MAX_DELAY = 192;
static const tjs_int HONEY_STEPS     = 64;
class tTVPHoneyTurnTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;
protected:
	tjs_uint64 StartTick;
	tjs_uint64 Time;
	tjs_uint64 CurTime;
	tjs_int Width;
	tjs_int Height;
	tjs_int Size;
	tjs_int Twist;
	tjs_int Order;
	tjs_int Dir;
	tjs_int Phase;
	tjs_int FrameCount;
	bool First;
    Records CubeFixups;
    void BuildCubeFixups();

public:
	tTVPHoneyTurnTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			tjs_int size, tjs_int twist, tjs_int order, tjs_int dir)
		: Time(time), Width(width), Height(height), Size(size),
		  Twist(twist), Order(order), Dir(dir),
		  Phase(0), FrameCount(0), First(true)
	{
		RefCount = 1;
		if(Size < 4) Size = 4;
        BuildCubeFixups();
	}
	virtual ~tTVPHoneyTurnTransHandler()
	{
	}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *options) { return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick);
	tjs_error TJS_INTF_METHOD EndProcess();
	tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data);
	tjs_error TJS_INTF_METHOD MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2;
		return TJS_S_OK;
	}
};






// One axis per pixel, chosen by the cube coordinate nearest to the pixel. The
// loop covers the whole frame (two million pixels at 1080p), so the rows are
// spread over the available cores: this runs while a transition starts, and a
// single-threaded pass would stall the frame that begins it.
void tTVPHoneyTurnTransHandler::BuildCubeFixups()
{
    const size_t pixels = size_t(Width) * Height;
    CubeFixups.assign((pixels + 3) / 4, {0, 0, 0, 0});
    const double size = Size;
    const double root3 = 1.7320508075688772;
    const auto rows = [this, size, root3](int first, int last) {
        for(int y = first; y < last; y++) for(int x = 0; x < Width; x++) {
            const double q = (root3 / 3.0 * x - 1.0 / 3.0 * y) / size;
            const double r = (2.0 / 3.0 * y) / size;
            const double cube_y = -q - r;
            int rx = int(std::floor(q + 0.5));
            int ry = int(std::floor(cube_y + 0.5));
            int rz = int(std::floor(r + 0.5));
            const double dx = std::abs(rx - q);
            const double dy = std::abs(ry - cube_y);
            const double dz = std::abs(rz - r);
            uint32_t axis;
            if(dx > dy && dx > dz) axis = 1;
            else if(dy > dz) axis = 2;
            else axis = 3;
            const size_t pixel = size_t(y) * Width + x;
            CubeFixups[pixel / 4][pixel % 4] = float(axis);
        }
    };
    unsigned threads = std::thread::hardware_concurrency();
    if(threads > 8) threads = 8;
    if(threads < 2 || Height < 64)
    {
        rows(0, Height);
        return;
    }
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for(unsigned i = 0; i < threads; i++)
    {
        const int first = (int)((tjs_int64)Height * i / threads);
        const int last = (int)((tjs_int64)Height * (i + 1) / threads);
        workers.emplace_back([&rows, first, last] { rows(first, last); });
    }
    for(auto &worker : workers) worker.join();
}

tjs_error TJS_INTF_METHOD tTVPHoneyTurnTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}
	CurTime = tick - StartTick;
	if(CurTime >= Time)
	{
		CurTime = Time;
		Phase = 255;
	}
	else
	{
		Phase = (tjs_int)(CurTime * 255 / Time);
	}
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPHoneyTurnTransHandler::EndProcess()
{
	if(CurTime == Time) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPHoneyTurnTransHandler::Process(tTVPDivisibleData *data) {
 return Draw(data,Width,Height,7,{float(Phase),float(Size),float(Twist),float(Order),float(Dir)},CubeFixups);
}

class tTVPHoneyTurnTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPHoneyTurnTransHandlerProvider() { RefCount = 1; }
	~tTVPHoneyTurnTransHandlerProvider() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_W("honeyturn");
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD StartTransition(
			iTVPSimpleOptionProvider *options,
			iTVPSimpleImageProvider *imagepro,
			tTVPLayerType layertype,
			tjs_uint src1w, tjs_uint src1h,
			tjs_uint src2w, tjs_uint src2h,
			tTVPTransType *type,
			tTVPTransUpdateType * updatetype,
			iTVPBaseTransHandler ** handler)
	{
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;
		tTJSVariant tmp;

		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;


		tjs_int size  = 40;
		tjs_int twist = 0;
		tjs_int order = 2;
		tjs_int dir   = 6;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("size"), &tmp)) && tmp.Type() != tvtVoid)
			size = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("twist"), &tmp)) && tmp.Type() != tvtVoid)
			twist = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("order"), &tmp)) && tmp.Type() != tvtVoid)
			order = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("dir"), &tmp)) && tmp.Type() != tvtVoid)
			dir = (tjs_int)tmp;
		if(size < 1) size = 1;
		*handler = new tTVPHoneyTurnTransHandler(time, src1w, src1h, size, twist, order, dir);
		return TJS_S_OK;
	}
} static * HoneyTurnTransHandlerProvider;
void RegisterHoneyTurnTransHandlerProvider()
{
	HoneyTurnTransHandlerProvider = new tTVPHoneyTurnTransHandlerProvider();
	TVPAddTransHandlerProvider(HoneyTurnTransHandlerProvider);
}
void UnregisterHoneyTurnTransHandlerProvider()
{
	if(!HoneyTurnTransHandlerProvider) return;
	TVPRemoveTransHandlerProvider(HoneyTurnTransHandlerProvider);
	HoneyTurnTransHandlerProvider->Release();
	HoneyTurnTransHandlerProvider = nullptr;
}

}

namespace morphing {
























struct tTVPMorphPatch
{
	tjs_int BeforeX[3], BeforeY[3];
	tjs_int AfterX[3],  AfterY[3];
};
static inline tjs_int Min3(tjs_int a, tjs_int b, tjs_int c)
{
	tjs_int m = a; if(b < m) m = b; if(c < m) m = c; return m;
}
static inline tjs_int Max3(tjs_int a, tjs_int b, tjs_int c)
{
	tjs_int m = a; if(b > m) m = b; if(c > m) m = c; return m;
}
class tTVPMorphingTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;
protected:
	tjs_uint64 StartTick;
	tjs_uint64 Time;
	tjs_int Width;
	tjs_int Height;
	tjs_int Alpha;
	tjs_int FrameCount;
	bool First;
	tjs_int NumTri;
	tTVPMorphPatch *Patches;
	tjs_int *CurX;
	tjs_int *CurY;
public:
	tTVPMorphingTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			tTVPMorphPatch *patches, tjs_int numtri)
		: Time(time), Width(width), Height(height), Alpha(0), FrameCount(0),
		  First(true), NumTri(numtri), Patches(patches)
	{
		RefCount = 1;
		CurX = new tjs_int[NumTri > 0 ? NumTri * 3 : 1];
		CurY = new tjs_int[NumTri > 0 ? NumTri * 3 : 1];
	}
	virtual ~tTVPMorphingTransHandler()
	{
		delete [] Patches;
		delete [] CurX;
		delete [] CurY;
	}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *options) { return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick);
	tjs_error TJS_INTF_METHOD EndProcess();
	tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data);
	tjs_error TJS_INTF_METHOD MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2;
		return TJS_S_OK;
	}
private:
	tjs_uint32 SamplePixel(iTVPScanLineProvider *src, tjs_int u, tjs_int v);
	void RasterizePatch(tTVPDivisibleData *data, tjs_int idx);
};
tjs_error TJS_INTF_METHOD tTVPMorphingTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}
	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed >= Time) elapsed = Time;

	Alpha = (tjs_int)(elapsed * 255 / Time);
	if(Alpha > 255) Alpha = 255;

	for(tjs_int i = 0; i < NumTri; i++)
	{
		const tTVPMorphPatch &p = Patches[i];
		for(tjs_int v = 0; v < 3; v++)
		{
			tjs_int k = i * 3 + v;
			CurX[k] = p.BeforeX[v] + (tjs_int)((tjs_int64)(p.AfterX[v] - p.BeforeX[v]) * (tjs_int64)elapsed / (tjs_int64)Time);
			CurY[k] = p.BeforeY[v] + (tjs_int)((tjs_int64)(p.AfterY[v] - p.BeforeY[v]) * (tjs_int64)elapsed / (tjs_int64)Time);
		}
	}
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPMorphingTransHandler::EndProcess()
{
	if(Alpha == 255) return TJS_S_FALSE;
	return TJS_S_TRUE;
}


tjs_error TJS_INTF_METHOD tTVPMorphingTransHandler::Process(tTVPDivisibleData *data) {
 Records r;
 r.reserve(NumTri*6);
 for(int i=0;i<NumTri;i++) for(int v=0;v<3;v++) {
  const auto &p=Patches[i];
  r.push_back({float(CurX[i*3+v]),float(CurY[i*3+v]),float(p.BeforeX[v]),float(p.BeforeY[v])});
  r.push_back({float(p.AfterX[v]),float(p.AfterY[v]),0,0});
 }
 return Draw(data,Width,Height,10,{float(Alpha),float(NumTri)},r);
}
static tjs_int ReadIntArray(iTVPSimpleOptionProvider *options,
		const tjs_char *name, std::vector<tjs_int> &out)
{
	tTJSVariant var;
	if(TJS_FAILED(options->GetValue(name, &var))) return -1;
	if(var.Type() != tvtObject) return -1;
	iTJSDispatch2 *arr = var.AsObjectNoAddRef();
	if(!arr) return -1;

	tTJSVariant cntvar;
	if(TJS_FAILED(arr->PropGet(0, TJS_W("count"), NULL, &cntvar, arr))) return -1;
	tjs_int count = (tjs_int)cntvar;
	if(count < 0) count = 0;
	out.clear();
	out.reserve(count);
	for(tjs_int i = 0; i < count; i++)
	{
		tTJSVariant e;
		if(TJS_FAILED(arr->PropGetByNum(0, i, &e, arr)))
			out.push_back(0);
		else
			out.push_back((tjs_int)e);
	}
	return count;
}
class tTVPMorphingTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPMorphingTransHandlerProvider() { RefCount = 1; }
	~tTVPMorphingTransHandlerProvider() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_W("morphing");
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD StartTransition(
			iTVPSimpleOptionProvider *options,
			iTVPSimpleImageProvider *imagepro,
			tTVPLayerType layertype,
			tjs_uint src1w, tjs_uint src1h,
			tjs_uint src2w, tjs_uint src2h,
			tTVPTransType *type,
			tTVPTransUpdateType * updatetype,
			iTVPBaseTransHandler ** handler)
	{
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;

		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		std::vector<tjs_int> before, after;
		tjs_int nb = ReadIntArray(options, TJS_W("before"), before);
		tjs_int na = ReadIntArray(options, TJS_W("after"),  after);
		tjs_int tb = (nb > 0) ? nb / 6 : 0;
		tjs_int ta = (na > 0) ? na / 6 : 0;
		tjs_int numtri = (tb < ta) ? tb : ta;
		if(numtri > 256) numtri = 256;
		if(numtri < 0) numtri = 0;
		tTVPMorphPatch *patches = new tTVPMorphPatch[numtri > 0 ? numtri : 1];
		for(tjs_int i = 0; i < numtri; i++)
		{
			for(tjs_int v = 0; v < 3; v++)
			{
				patches[i].BeforeX[v] = before[i*6 + v*2 + 0];
				patches[i].BeforeY[v] = before[i*6 + v*2 + 1];
				patches[i].AfterX[v]  = after [i*6 + v*2 + 0];
				patches[i].AfterY[v]  = after [i*6 + v*2 + 1];
			}
		}
		*handler = new tTVPMorphingTransHandler(time, src1w, src1h, patches, numtri);
		return TJS_S_OK;
	}
} static * MorphingTransHandlerProvider;
void RegisterMorphingTransHandlerProvider()
{
	MorphingTransHandlerProvider = new tTVPMorphingTransHandlerProvider();
	TVPAddTransHandlerProvider(MorphingTransHandlerProvider);
}
void UnregisterMorphingTransHandlerProvider()
{
	if(!MorphingTransHandlerProvider) return;
	TVPRemoveTransHandlerProvider(MorphingTransHandlerProvider);
	MorphingTransHandlerProvider->Release();
	MorphingTransHandlerProvider = nullptr;
}

}

namespace multiripple {


















#ifndef M_PI
	#define M_PI (3.14159265358979323846)
#endif


static inline tjs_uint32 BlendRGBOpaque(tjs_uint32 a, tjs_uint32 b, tjs_int opa)
{
	tjs_uint32 ret;
	tjs_uint32 tmp;
	tmp = a & 0x000000ff;  ret  = 0x000000ff & (tmp + (((b & 0x000000ff) - tmp) * opa >> 8));
	tmp = a & 0x0000ff00;  ret |= 0x0000ff00 & (tmp + (((b & 0x0000ff00) - tmp) * opa >> 8));
	tmp = a & 0x00ff0000;  ret |= 0x00ff0000 & (tmp + (((b & 0x00ff0000) - tmp) * opa >> 8));
	return ret | 0xff000000;
}





class tTVPMRDistMap
{
public:
    tjs_int Width;
    tjs_int Height;
    float Roundness;

    tTVPMRDistMap(tjs_int width, tjs_int height, float roundness)
        : Width(width), Height(height), Roundness(roundness) {}

    tjs_int Get(tjs_int dx, tjs_int dy) const
    {
        return std::min<tjs_int>(65535, tjs_int(std::sqrt(double(dx) * dx + double(dy) * dy * Roundness * Roundness) + 0.5));
    }

    tjs_int MaxCornerDist(tjs_int cx, tjs_int cy) const
    {
        tjs_int rx = Width - cx - 1; if(rx < 0) rx = 0;
        tjs_int ry = Height - cy - 1; if(ry < 0) ry = 0;
        cx = std::clamp(cx, 0, Width - 1);
        cy = std::clamp(cy, 0, Height - 1);
        return std::max({Get(cx, cy), Get(rx, cy), Get(cx, ry), Get(rx, ry)});
    }
};

class tTVPMRDriftTable
{
public:
	tjs_int *A;
	tjs_int *B;
	tjs_int Len;
	tTVPMRDriftTable(tjs_int len, tjs_int travel, tjs_int rwidth,
		tjs_int maxdrift, tjs_int blendunit)
	{
		if(len < travel) len = travel;
		if(len < 1) len = 1;
		Len = len;
		A = new tjs_int[len];
		B = new tjs_int[len];
		double rcp_rw = (rwidth > 0) ? (1.0 / (double)rwidth) : 0.0;
		for(tjs_int i = 0; i < len; i++)
		{
			if(i < travel)
			{
				double rad = (double)i * rcp_rw * (M_PI * 2.0);
				double env = 1.0 - (double)i / (double)travel;
				double s = sin(rad) * env;
				double v = (double)maxdrift * s;
				A[i] = (tjs_int)(v < 0 ? v - 0.5 : v + 0.5);
				B[i] = (tjs_int)((tjs_int64)i * blendunit / travel);
			}
			else
			{
				A[i] = 0;
				B[i] = blendunit;
			}
		}
	}
	~tTVPMRDriftTable()
	{
		if(A) delete [] A;
		if(B) delete [] B;
	}
};



struct tTVPMRRipple
{
	tjs_int CenterX;
	tjs_int CenterY;
	tjs_int Phase;
	tjs_int Speed;
	tjs_int StartTime;
	tjs_int Duration;
	tjs_int EndTime;
	tjs_int Drift;
	tjs_int State;
};



class tTVPMultiRippleTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;
protected:
	tjs_uint64 StartTick;
	tjs_uint64 Time;
	tjs_int Width;
	tjs_int Height;
	tjs_int64 CurTime;
	tjs_int Count;
	tjs_int BlendUnit;
	tjs_int MaxDist;
	tjs_int RippleTravel;
	tjs_int MaxDrift;
	tjs_int FinishedCount;
	bool First;
	tjs_int FrameCount;
	tTVPMRDistMap    *DistMap;
	tTVPMRDriftTable *DriftTable;
	tTVPMRRipple     *Ripples;
public:
	tTVPMultiRippleTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
		tjs_int count, tjs_int rwidth, tjs_int wavecount, tjs_int maxdrift,
		float roundness, double delaylast)
	{
		RefCount = 1;
		First = true;
		FrameCount = 0;
		CurTime = 0;
		FinishedCount = 0;
		Time = time;
		Width = width;
		Height = height;

		if(count < 1) count = 1;
		else if(count > 20) count = 20;
		Count = count;
		if(rwidth < 1) rwidth = 1;
		if(wavecount < 1) wavecount = 1;
		if(maxdrift < 0) maxdrift = 0;
		MaxDrift = maxdrift;
		RippleTravel = rwidth * wavecount;
		BlendUnit = (tjs_int)(0x2fd / (tjs_int64)Count);
		MaxDist = (tjs_int)(sqrt((double)width * width +
			(double)height * height) + 0.5);

		DistMap = new tTVPMRDistMap(width, height, roundness);
		tjs_int driftlen = MaxDist * 2;
		DriftTable = new tTVPMRDriftTable(driftlen, RippleTravel, rwidth, maxdrift, BlendUnit);
		Ripples = new tTVPMRRipple[Count];
		BuildRipples(delaylast);
	}
	virtual ~tTVPMultiRippleTransHandler()
	{
		if(DistMap)    delete DistMap;
		if(DriftTable) delete DriftTable;
		if(Ripples)    delete [] Ripples;
	}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *options) { return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick);
	tjs_error TJS_INTF_METHOD EndProcess();
	tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data);
	tjs_error TJS_INTF_METHOD MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2;
		return TJS_S_OK;
	}
private:
	void BuildRipples(double delaylast);
};
void tTVPMultiRippleTransHandler::BuildRipples(double delaylast)
{

	tjs_int cx = Width >> 1;
	tjs_int cy = Height >> 1;
	if(Count < 2)
	{
		tjs_int speed = (tjs_int)(((tjs_int64)(MaxDist + RippleTravel) * 0x400) / (tjs_int64)Time);
		if(speed < 1) speed = 1;
		tjs_int dirval = DistMap->MaxCornerDist(cx, cy);
		tTVPMRRipple &r = Ripples[0];
		r.CenterX   = cx;
		r.CenterY   = cy;
		r.Phase     = 0;
		r.Speed     = speed;
		r.StartTime = 0;
		r.Duration  = (tjs_int)(((tjs_int64)(dirval + RippleTravel) * 0x400) / (tjs_int64)speed);
		r.EndTime   = r.Duration;
		r.Drift     = 0;
		r.State     = 0;
		return;
	}

	tjs_int speed = (tjs_int)(((tjs_int64)(MaxDist + RippleTravel) * 0x400) / (tjs_int64)(Time >> 1));
	if(speed < 1) speed = 1;
	tjs_int last = Count - 1;

	{
		tTVPMRRipple &r = Ripples[last];
		r.CenterX  = cx;
		r.CenterY  = cy;
		r.Phase    = 0;
		r.Speed    = speed;
		tjs_int dirval = DistMap->MaxCornerDist(cx, cy);
		r.Duration = (tjs_int)(((tjs_int64)(dirval + RippleTravel) * 0x400) / (tjs_int64)speed);
		r.Drift    = 0;
		r.State    = 0;
	}

	tjs_int mindist = (Width + Height) / 20;
	for(tjs_int j = last - 1; j >= 0; j--)
	{
		tjs_int rx, ry;
		for(;;)
		{
			rx = rand() % Width;
			ry = rand() % Height;
			tjs_int ddx = rx - Ripples[j + 1].CenterX; if(ddx < 0) ddx = -ddx;
			tjs_int ddy = ry - Ripples[j + 1].CenterY; if(ddy < 0) ddy = -ddy;
			if(ddx + ddy >= mindist) break;
		}
		tTVPMRRipple &r = Ripples[j];
		r.CenterX  = rx;
		r.CenterY  = ry;
		r.Phase    = 0;
		r.Speed    = speed;
		tjs_int dirval = DistMap->MaxCornerDist(rx, ry);
		r.Duration = (tjs_int)(((tjs_int64)(dirval + RippleTravel) * 0x400) / (tjs_int64)speed);
		r.Drift    = 0;
		r.State    = 0;
	}

	tjs_int64 dtmin = (tjs_int64)Time;
	for(tjs_int j = 1; j <= last - 1; j++)
	{
		tjs_int64 v = ((tjs_int64)Time - Ripples[j].Duration) / j;
		if(v < dtmin) dtmin = v;
	}
	if(dtmin < 0) dtmin = 0;

	tjs_int64 acc = 0;
	for(tjs_int j = 0; j < last; j++)
	{
		Ripples[j].StartTime = (tjs_int)acc;
		Ripples[j].EndTime   = (tjs_int)(acc + Ripples[j].Duration);
		acc += dtmin;
	}

	{
		double startd = ((double)last + (delaylast - 1.0)) * (double)dtmin;
		tjs_int st = (tjs_int)(startd < 0 ? startd - 0.5 : startd + 0.5);
		if(st < 0) st = 0;
		Ripples[last].StartTime = st;
		Ripples[last].EndTime   = st + Ripples[last].Duration;
	}
}
tjs_error TJS_INTF_METHOD tTVPMultiRippleTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}
	CurTime = (tjs_int64)(tick - StartTick);
	if(CurTime > (tjs_int64)Time) CurTime = (tjs_int64)Time;

	FinishedCount = 0;
	for(tjs_int i = 0; i < Count; i++)
	{
		tTVPMRRipple &r = Ripples[i];
		if(CurTime >= r.EndTime)
		{
			r.State = 2;
			FinishedCount++;
		}
		else if(CurTime >= r.StartTime)
		{
			r.State = 1;
			tjs_int64 dt = CurTime - r.StartTime;
			r.Phase = (tjs_int)((dt * r.Speed) >> 10);
			tjs_int dur = r.Duration > 0 ? r.Duration : 1;
			tjs_int prog = (tjs_int)((dt * 0x100) / dur);
			r.Drift = (~prog) & 0xff;
		}
		else
		{
			r.State = 0;
		}
	}
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPMultiRippleTransHandler::EndProcess()
{
	if(CurTime >= (tjs_int64)Time) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVPMultiRippleTransHandler::Process(tTVPDivisibleData *data) {
 Records r;
 for(int i=0;i<Count;i++) {const auto &q=Ripples[i];r.push_back({float(q.CenterX),float(q.CenterY),float(q.Phase),float(q.State)});r.push_back({float(q.Drift),0,0,0});}
 for(int i=0;i<DriftTable->Len;i++) r.push_back({float(DriftTable->A[i]),float(DriftTable->B[i]),0,0});
 return Draw(data,Width,Height,9,{float(Count),float(FinishedCount*BlendUnit),DistMap->Roundness,float(DriftTable->Len)},r);
}



class tTVPMultiRippleTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPMultiRippleTransHandlerProvider() { RefCount = 1; }
	~tTVPMultiRippleTransHandlerProvider() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_W("multiripple");
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD StartTransition(
			iTVPSimpleOptionProvider *options,
			iTVPSimpleImageProvider *imagepro,
			tTVPLayerType layertype,
			tjs_uint src1w, tjs_uint src1h,
			tjs_uint src2w, tjs_uint src2h,
			tTVPTransType *type,
			tTVPTransUpdateType * updatetype,
			iTVPBaseTransHandler ** handler)
	{
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;

		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		tjs_int count     = 1;
		tjs_int wavecount = 2;
		tjs_int rwidth    = 32;
		tjs_int maxdrift  = 24;
		float   roundness = 1.0f;
		double  delaylast = 1.0;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("count"), &tmp)) && tmp.Type() != tvtVoid)
			count = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("wavecount"), &tmp)) && tmp.Type() != tvtVoid)
			wavecount = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("rwidth"), &tmp)) && tmp.Type() != tvtVoid)
			rwidth = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("maxdrift"), &tmp)) && tmp.Type() != tvtVoid)
			maxdrift = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("roundness"), &tmp)) && tmp.Type() != tvtVoid)
			roundness = (float)(double)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("delaylast"), &tmp)) && tmp.Type() != tvtVoid)
			delaylast = (double)tmp;
		if(roundness <= 0.0f) roundness = 1.0f;
		*handler = new tTVPMultiRippleTransHandler(time, src1w, src1h,
			count, rwidth, wavecount, maxdrift, roundness, delaylast);
		return TJS_S_OK;
	}
} static * MultiRippleTransHandlerProvider;
void RegisterMultiRippleTransHandlerProvider()
{
	MultiRippleTransHandlerProvider = new tTVPMultiRippleTransHandlerProvider();
	TVPAddTransHandlerProvider(MultiRippleTransHandlerProvider);
}
void UnregisterMultiRippleTransHandlerProvider()
{
	if(!MultiRippleTransHandlerProvider) return;
	TVPRemoveTransHandlerProvider(MultiRippleTransHandlerProvider);
	MultiRippleTransHandlerProvider->Release();
	MultiRippleTransHandlerProvider = nullptr;
}

}

namespace Universal {
































static inline tjs_uint32 CanonicalRulePixel(tjs_uint32 pixel)
{
	return (pixel & 0xff00ff00u) | ((pixel >> 16) & 0xffu) | ((pixel & 0xffu) << 16);
}

struct tTVPRulePixels
{
	tjs_int Width = 0;
	tjs_int Height = 0;
	std::vector<tjs_uint32> Pixels;
};

// Rule images are decoded on a worker thread. A 1080p rule takes about ten
// milliseconds to decode, and the engine's texture path costs as much again
// because the pixels have to be read back from the GPU, which waits for the
// frame in flight. Neither belongs in the frame that starts a transition, so
// the decode is requested here and the transition holds at phase 0, showing the
// same image as the frame before, until the pixels arrive.
class tTVPRuleDecoder
{
	struct Job
	{
		uint64_t Token;
		ttstr Name;
		tjs_uint DesW, DesH;
	};
	std::mutex Mutex;
	std::condition_variable Wake;
	std::deque<Job> Queue;
	std::map<uint64_t, tTVPRulePixels> Finished; // empty pixels: decode failed
	uint64_t NextToken = 1;
	bool Stopping = false;
	std::thread Worker; // declared last: the thread reads the members above

	void Run()
	{
		for(;;)
		{
			Job job;
			{
				std::unique_lock<std::mutex> lock(Mutex);
				Wake.wait(lock, [this] { return Stopping || !Queue.empty(); });
				if(Queue.empty()) return;
				job = Queue.front();
				Queue.pop_front();
			}
			// Kept in the stored order: imagewipe samples these pixels as a texture,
			// and the GPU path always uploaded them unconverted.
			tTVPRulePixels pixels;
			TVPLoadGraphicPixels(job.Name, 0x02ffffff, job.DesW, job.DesH,
					&pixels.Pixels, &pixels.Width, &pixels.Height);
			std::lock_guard<std::mutex> lock(Mutex);
			Finished[job.Token] = std::move(pixels);
		}
	}
	tTVPRuleDecoder() : Worker([this] { Run(); }) {}

public:
	static tTVPRuleDecoder &Get()
	{
		static tTVPRuleDecoder *decoder = new tTVPRuleDecoder();
		return *decoder;
	}
	uint64_t Request(const ttstr &name, tjs_uint desw, tjs_uint desh)
	{
		std::lock_guard<std::mutex> lock(Mutex);
		const uint64_t token = NextToken++;
		Queue.push_back(Job{token, name, desw, desh});
		Wake.notify_all();
		return token;
	}
	// True once the token's pixels are decoded; they stay empty when the image
	// could not be decoded.
	bool Take(uint64_t token, tTVPRulePixels &pixels)
	{
		std::lock_guard<std::mutex> lock(Mutex);
		auto found = Finished.find(token);
		if(found == Finished.end()) return false;
		pixels = std::move(found->second);
		Finished.erase(found);
		return true;
	}
};

class tRuleScanLineProvider : public iTVPScanLineProvider, public iTVPPendingRule
{
	tjs_int RefCount;
	tjs_int W, H;
	tjs_uint32 *Buf;
	iTVPTexture2D *Texture;
	uint64_t Decoding; // token while the pixels are still being decoded
public:
 iTVPTexture2D *GetTexture() override
	{
		// Only handlers that feed the rule to the shader as a texture ask for this.
		if(!Texture && Buf && W > 0 && H > 0)
			Texture = TVPGetRenderManager()->CreateTexture2D(Buf, W * 4, W, H,
				TVPTextureFormat::RGBA);
		return Texture;
	}
 iTVPTexture2D *GetTextureForRender() override { return nullptr; }
	tRuleScanLineProvider(tjs_int w, tjs_int h)
		: RefCount(1), W(w), H(h), Buf(0), Texture(0), Decoding(0)
	{
		if(W > 0 && H > 0) Buf = (tjs_uint32*)malloc(sizeof(tjs_uint32) * W * H);
	}
	explicit tRuleScanLineProvider(uint64_t decoding)
		: RefCount(1), W(0), H(0), Buf(0), Texture(0), Decoding(decoding)
	{
	}
	~tRuleScanLineProvider()
	{
		if(Texture) Texture->Release();
		if(Buf) free(Buf);
	}
	bool Ready() override { return Decoding == 0; }
	void Poll() override
	{
		if(!Decoding) return;
		tTVPRulePixels pixels;
		if(!tTVPRuleDecoder::Get().Take(Decoding, pixels)) return;
		Decoding = 0;
		W = pixels.Width;
		H = pixels.Height;
		if(W <= 0 || H <= 0) return;
		Buf = (tjs_uint32*)malloc(sizeof(tjs_uint32) * (size_t)W * (size_t)H);
		if(Buf) std::memcpy(Buf, pixels.Pixels.data(), sizeof(tjs_uint32) * (size_t)W * (size_t)H);
	}
	tjs_uint32 * GetBuffer() { return Buf; }
	tjs_int GetW() const { return W; }
	tjs_int GetH() const { return H; }
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetWidth(tjs_int *width)   { if(width)  *width  = W; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetHeight(tjs_int *height) { if(height) *height = H; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetPixelFormat(tjs_int *bpp) { if(bpp) *bpp = 32; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetPitchBytes(tjs_int *pitch) { if(pitch) *pitch = W * 4; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetScanLine(tjs_int line, const void ** scanline)
	{
		if(!Buf || !scanline) return TJS_E_FAIL;
		if(line < 0) line = 0; else if(line >= H) line = H - 1;
		*scanline = Buf + line * W;
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD GetScanLineForWrite(tjs_int line, void ** scanline)
	{
		if(!Buf || !scanline) return TJS_E_FAIL;
		if(line < 0) line = 0; else if(line >= H) line = H - 1;
		*scanline = Buf + line * W;
		return TJS_S_OK;
	}
};





static tjs_error CreateRuleProvider(iTVPSimpleOptionProvider *options,
		tRuleScanLineProvider **out)
{
	*out = 0;
	if(!options) return TJS_E_FAIL;
	tTJSVariant rule;
	if(TJS_FAILED(options->GetValue(TJS_W("rule"), &rule))) return TJS_E_FAIL;
	if(rule.Type() == tvtVoid) return TJS_E_FAIL;
	if(rule.Type() == tvtObject)
	{
		iTJSDispatch2 *obj = rule.AsObjectNoAddRef();
		if(!obj) return TJS_E_FAIL;
		tTJSVariant val;
		tjs_int rw = 0, rh = 0, pitch = 0;
		const tjs_uint8 *buffer = 0;
		if(TJS_FAILED(obj->PropGet(0, TJS_W("imageWidth"), 0, &val, obj))) return TJS_E_FAIL;
		rw = (tjs_int)val;
		if(TJS_FAILED(obj->PropGet(0, TJS_W("imageHeight"), 0, &val, obj))) return TJS_E_FAIL;
		rh = (tjs_int)val;
		if(TJS_FAILED(obj->PropGet(0, TJS_W("mainImageBufferPitch"), 0, &val, obj))) return TJS_E_FAIL;
		pitch = (tjs_int)val;
		if(TJS_FAILED(obj->PropGet(0, TJS_W("mainImageBuffer"), 0, &val, obj))) return TJS_E_FAIL;
		buffer = reinterpret_cast<const tjs_uint8 *>(
			static_cast<tjs_intptr_t>((tjs_int64)val));
		if(rw <= 0 || rh <= 0 || !buffer) return TJS_E_FAIL;
		tRuleScanLineProvider *p = new tRuleScanLineProvider(rw, rh);
		if(!p->GetBuffer()) { p->Release(); return TJS_E_FAIL; }
		tjs_uint32 *dst = p->GetBuffer();
		for(tjs_int y = 0; y < rh; y++)
		{
			const tjs_uint32 *src = reinterpret_cast<const tjs_uint32 *>(buffer + y * pitch);
			for(tjs_int x = 0; x < rw; x++) dst[y * rw + x] = src[x];
		}
		*out = p;
		return TJS_S_OK;
	}
	else
	{
		const tjs_char *name = 0;
		if(TJS_FAILED(options->GetAsString(TJS_W("rule"), &name)) || !name)
			return TJS_E_FAIL;
		*out = new tRuleScanLineProvider(
			tTVPRuleDecoder::Get().Request(ttstr(name), 0, 0));
		return TJS_S_OK;
	}
}

iTVPScanLineProvider *CreatePendingRuleProvider(const ttstr &name, tjs_uint desh,
		iTVPPendingRule **pending)
{
	tRuleScanLineProvider *provider =
		new tRuleScanLineProvider(tTVPRuleDecoder::Get().Request(name, 0, desh));
	*pending = provider;
	return provider;
}

static inline tjs_uint32 LoadRulePixelHSB(tjs_uint32 argb)
{
	tjs_int R = (argb >> 16) & 0xff;
	tjs_int G = (argb >> 8) & 0xff;
	tjs_int B = argb & 0xff;
	tjs_int mx = R; if(G > mx) mx = G; if(B > mx) mx = B;
	tjs_int mn = R; if(G < mn) mn = G; if(B < mn) mn = B;
	tjs_int chroma = mx - mn;

	tjs_int hue = 0;
	if(chroma != 0)
	{
		tjs_int h6;
		if(G == mx)      h6 = (B - R) * 60 / chroma + 120;
		else if(B == mx) h6 = (R - G) * 60 / chroma + 240;
		else if(G < B)   h6 = (G - B) * 60 / chroma + 360;
		else             h6 = (G - B) * 60 / chroma;
		if(h6 < 0) h6 += 360;
		hue = h6 * 255 / 360;
		if(hue > 255) hue = 255;
	}
	tjs_int sat = (mx == 0) ? 0 : (chroma * 255 / mx);
	sat = sat * 2; if(sat > 255) sat = 255;
	tjs_int inv_v = 255 - mx;

	return (tjs_uint32)hue | ((tjs_uint32)sat << 8) | ((tjs_uint32)inv_v << 16);
}
static inline tjs_int ClampI(tjs_int v, tjs_int lo, tjs_int hi)
{
	if(v < lo) v = lo;
	if(v > hi) v = hi;
	return v;
}
static inline tjs_int LRound(double x)
{
	return (tjs_int)floor(x + 0.5);
}

class tTVP3duniversalTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;
protected:
	tjs_uint64 StartTick;
	tjs_uint64 Time;
	tjs_int Width;
	tjs_int Height;
	tjs_uint32 *RuleBuf;
 iTVPTexture2D *RuleTexture = nullptr;
	tRuleScanLineProvider *Rule;
	bool HSB;
	bool Active;
	tjs_int Phase;
	bool First;
	bool Src1Static;
	bool Src2Static;
	double Accel1, Speed1;
	double Accel2, Speed2;
	tjs_int Bound1, Bound2;
	tjs_int CosTable[256];
	tjs_int SinTable[256];
	tjs_int MoveTable1[256];
	tjs_int MoveTable2[256];
    Records Tables;
public:
	tTVP3duniversalTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			tRuleScanLineProvider *rule, bool hsb,
			double accel1, double speed1, tjs_int bound1,
			double accel2, double speed2, tjs_int bound2)
		: RefCount(1), StartTick(0), Time(time), Width(width), Height(height),
		  RuleBuf(0), Rule(rule), HSB(hsb), Active(false), Phase(0), First(true),
		  Accel1(accel1), Speed1(speed1), Accel2(accel2), Speed2(speed2),
		  Bound1(ClampI(bound1, 0, 0x7f)), Bound2(ClampI(bound2, 0, 0x7f))
	{
		Src1Static = (accel1 == 0.0 && speed1 == 0.0);
		Src2Static = (accel2 == 0.0 && speed2 == 0.0);
		if(Rule) Rule->AddRef();

		const double DENOM = 255.0;
		const double TWO_PI = 6.283185307179586;
		const double TRIG_SCALE = 1024.0;
		for(tjs_int i = 0; i < 256; i++)
		{
			double angle = (double)i * TWO_PI / DENOM;
			CosTable[i] = LRound(cos(angle) * TRIG_SCALE);
			SinTable[i] = LRound(sin(angle) * TRIG_SCALE);
		}

		BuildMoveTable(MoveTable1, Accel1, Speed1, Bound1);
		BuildMoveTable(MoveTable2, Accel2, Speed2, Bound2);
		Tables.assign(512, std::array<float, 4>{0, 0, 0, 0});
		for(tjs_int i = 0; i < 256; i++)
		{
			Tables[i] = {float(CosTable[i]), float(SinTable[i]), 0.0f, 0.0f};
			Tables[256 + i] = {float(MoveTable1[i]), float(MoveTable2[i]), 0.0f, 0.0f};
		}
	}
	// Copies the decoded rule into the buffer the shader samples. Returns false
	// while the rule image is still being decoded.
	bool EnsureRule()
	{
		if(Active) return true;
		if(Rule && !Rule->Ready())
		{
			Rule->Poll();
			if(!Rule->Ready()) return false;
		}
		free(RuleBuf);
		RuleBuf = 0;
		if(Width > 0 && Height > 0)
			RuleBuf = (tjs_uint32*)malloc(sizeof(tjs_uint32) * Width * Height);
		if(RuleBuf && Rule && Rule->GetBuffer())
		{
			tjs_int rw = Rule->GetW();
			tjs_int rh = Rule->GetH();
			const tjs_uint32 *rbuf = Rule->GetBuffer();
			for(tjs_int y = 0; y < Height; y++)
			{
				tjs_int sy = (y < rh) ? y : (rh - 1);
				const tjs_uint32 *src = rbuf + sy * rw;
				tjs_uint32 *dst = RuleBuf + y * Width;
				for(tjs_int x = 0; x < Width; x++)
				{
					tjs_int sx = (x < rw) ? x : (rw - 1);
					// The shader expects the rule in canonical (R/B swapped) order.
					const tjs_uint32 pixel = CanonicalRulePixel(src[sx]);
					if(HSB) dst[x] = LoadRulePixelHSB(pixel);
					else    dst[x] = pixel;
				}
			}
		}
		if(RuleTexture) RuleTexture->Release();
		RuleTexture=TVPGetRenderManager()->CreateTexture2D(RuleBuf,Width*4,Width,Height,TVPTextureFormat::RGBA);
		Active = true;
		return true;
	}
	virtual ~tTVP3duniversalTransHandler()
	{
		if(RuleBuf) free(RuleBuf);
        if(RuleTexture) RuleTexture->Release();
		if(Rule) Rule->Release();
	}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *options) { return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick);
	tjs_error TJS_INTF_METHOD EndProcess();
	tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data);
	tjs_error TJS_INTF_METHOD MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2;
		return TJS_S_OK;
	}
private:
	void BuildMoveTable(tjs_int *table, double accel, double speed, tjs_int bound);
	void CopySrcToOut(iTVPScanLineProvider *src);
	void ScatterMove(iTVPScanLineProvider *src, tjs_int phase,
			const tjs_int *moveTable, const tjs_int *opaTable);
};






void tTVP3duniversalTransHandler::BuildMoveTable(tjs_int *table, double accel, double speed, tjs_int bound)
{
	tjs_int period = 255 - bound;
	if(period < 1) period = 1;
	for(tjs_int t = 0; t < 256; t++)
	{
		tjs_int te = (bound > 0) ? (t % period) : t;
		double v = accel * (double)te * (double)te / 2.0 + speed * (double)te;
		table[t] = LRound(v);
	}
}








tjs_error TJS_INTF_METHOD tTVP3duniversalTransHandler::StartProcess(tjs_uint64 tick)
{
	if(First)
	{
		First = false;
		StartTick = tick;
	}
	if(!EnsureRule())
	{
		Phase = 0;
		return TJS_S_TRUE;
	}
	tjs_uint64 elapsed = tick - StartTick;
	tjs_int phase;
	if(elapsed >= Time) phase = 0xff;
	else                phase = (tjs_int)(elapsed * 0xff / Time);
	Phase = phase;

	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVP3duniversalTransHandler::EndProcess()
{
	if(Phase == 0xff) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
tjs_error TJS_INTF_METHOD tTVP3duniversalTransHandler::Process(tTVPDivisibleData *data)
{
    if(!Active) { data->Dest = data->Src1; return TJS_S_OK; }
    const size_t pixels = size_t(Width) * Height;
    const int base = Src2Static ? 1 : Src1Static ? 0 : -1;
    const int mask = (Src1Static ? 0 : 1) | (Src2Static ? 0 : 2);
    return Draw(data, Width, Height, 8,
        {float(Phase), float(base), float(pixels), float(mask)},
        Tables, TVPBevyTextureID(RuleTexture));
}

class tTVP3duniversalTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVP3duniversalTransHandlerProvider() { RefCount = 1; }
	~tTVP3duniversalTransHandlerProvider() {}
	tjs_error TJS_INTF_METHOD AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }
	tjs_error TJS_INTF_METHOD GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_W("3duniversal");
		return TJS_S_OK;
	}

	static double GetOptDouble(iTVPSimpleOptionProvider *opt,
			const tjs_char *primary, const tjs_char *alias, double def)
	{
		double v = def;
		tTJSVariant tmp;
		if(TJS_SUCCEEDED(opt->GetValue(primary, &tmp)) && tmp.Type() != tvtVoid)
			v = (double)(tTVReal)tmp;
		if(alias && TJS_SUCCEEDED(opt->GetValue(alias, &tmp)) && tmp.Type() != tvtVoid)
			v = (double)(tTVReal)tmp;
		return v;
	}
	static tjs_int GetOptInt(iTVPSimpleOptionProvider *opt, const tjs_char *name, tjs_int def)
	{
		tTJSVariant tmp;
		if(TJS_SUCCEEDED(opt->GetValue(name, &tmp)) && tmp.Type() != tvtVoid)
			return (tjs_int)(tjs_int64)tmp;
		return def;
	}
	tjs_error TJS_INTF_METHOD StartTransition(
			iTVPSimpleOptionProvider *options,
			iTVPSimpleImageProvider *imagepro,
			tTVPLayerType layertype,
			tjs_uint src1w, tjs_uint src1h,
			tjs_uint src2w, tjs_uint src2h,
			tTVPTransType *type,
			tTVPTransUpdateType * updatetype,
			iTVPBaseTransHandler ** handler)
	{
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;
		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		bool hsb = false;
		const tjs_char *typestr = 0;
		if(TJS_SUCCEEDED(options->GetAsString(TJS_W("type"), &typestr)) && typestr)
		{
			if(TJS_strcmp(typestr, TJS_W("HSB")) == 0) hsb = true;
		}

		double accel1 = GetOptDouble(options, TJS_W("accel1"), TJS_W("a1"), 0.0);
		double speed1 = GetOptDouble(options, TJS_W("speed1"), TJS_W("s1"), 0.0);
		double accel2 = GetOptDouble(options, TJS_W("accel2"), TJS_W("a2"), 0.0);
		double speed2 = GetOptDouble(options, TJS_W("speed2"), TJS_W("s2"), 0.0);
		tjs_int bound1 = GetOptInt(options, TJS_W("bound1"), 0);
		tjs_int bound2 = GetOptInt(options, TJS_W("bound2"), 0);

		tRuleScanLineProvider *rule = 0;
		if(TJS_FAILED(CreateRuleProvider(options, &rule)))
			return TJS_E_FAIL;
		*handler = new tTVP3duniversalTransHandler(time, src1w, src1h, rule, hsb,
				accel1, speed1, bound1, accel2, speed2, bound2);

		if(rule) rule->Release();
		return TJS_S_OK;
	}
} static * TDUniversalTransHandlerProvider;
void Register3duniversalTransHandlerProvider()
{
	TDUniversalTransHandlerProvider = new tTVP3duniversalTransHandlerProvider();
	TVPAddTransHandlerProvider(TDUniversalTransHandlerProvider);
}
void Unregister3duniversalTransHandlerProvider()
{
	if(!TDUniversalTransHandlerProvider) return;
	TVPRemoveTransHandlerProvider(TDUniversalTransHandlerProvider);
	TDUniversalTransHandlerProvider->Release();
	TDUniversalTransHandlerProvider = nullptr;
}

}
}

static BevyExtraTrans::Provider *blur;
static void registerProviders() {
 blur=new BevyExtraTrans::Provider(100,TJS_W("blurfade"));TVPAddTransHandlerProvider(blur);
 Nagano::scanline::RegisterScanLineTransHandlerProvider();
 Nagano::zoomfade::RegisterZoomFadeTransHandlerProvider();
 Nagano::rgbfade::RegisterRGBFadeTransHandlerProvider();
 Nagano::spin::RegisterSpinFadeTransHandlerProvider();
 Nagano::flutter::RegisterFlutterTransHandlerProvider();
 Nagano::book::RegisterBookTransHandlerProvider();
 Nagano::imagewipe::RegisterImageWipeTransHandlerProvider();
 Nagano::honeyturn::RegisterHoneyTurnTransHandlerProvider();
 Nagano::morphing::RegisterMorphingTransHandlerProvider();
 Nagano::multiripple::RegisterMultiRippleTransHandlerProvider();
 Nagano::Universal::Register3duniversalTransHandlerProvider();
}
static void unregisterProviders() {
 Nagano::Universal::Unregister3duniversalTransHandlerProvider();
 Nagano::multiripple::UnregisterMultiRippleTransHandlerProvider();
 Nagano::morphing::UnregisterMorphingTransHandlerProvider();
 Nagano::honeyturn::UnregisterHoneyTurnTransHandlerProvider();
 Nagano::imagewipe::UnregisterImageWipeTransHandlerProvider();
 Nagano::book::UnregisterBookTransHandlerProvider();
 Nagano::flutter::UnregisterFlutterTransHandlerProvider();
 Nagano::spin::UnregisterSpinFadeTransHandlerProvider();
 Nagano::rgbfade::UnregisterRGBFadeTransHandlerProvider();
 Nagano::zoomfade::UnregisterZoomFadeTransHandlerProvider();
 Nagano::scanline::UnregisterScanLineTransHandlerProvider();
 if(blur){TVPRemoveTransHandlerProvider(blur);blur->Release();blur=nullptr;}
}
NCB_PRE_REGIST_CALLBACK(registerProviders);
NCB_POST_UNREGIST_CALLBACK(unregisterProviders);
