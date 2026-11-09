
#define NOMINMAX
#include "tjsCommHead.h"
#include "DrawDevice.h"
#include "BasicDrawDevice.h"
#include "LayerIntf.h"
#include "MsgIntf.h"
#include "SysInitIntf.h"
#include "WindowIntf.h"
#include "DebugIntf.h"
#include "ThreadIntf.h"
#include "ComplexRect.h"
#include "EventImpl.h"
#include "WindowImpl.h"

#define ZeroMemory(p,n) memset(p, 0, n);
// #include <d3d9.h>
// #include <mmsystem.h>
#include <algorithm>

//---------------------------------------------------------------------------
// オプション
//---------------------------------------------------------------------------
static tjs_int TVPBasicDrawDeviceOptionsGeneration = 0;
bool TVPZoomInterpolation = true;
//---------------------------------------------------------------------------
static void TVPInitBasicDrawDeviceOptions()
{
	if(TVPBasicDrawDeviceOptionsGeneration == TVPGetCommandLineArgumentGeneration()) return;
	TVPBasicDrawDeviceOptionsGeneration = TVPGetCommandLineArgumentGeneration();

	tTJSVariant val;
	TVPZoomInterpolation = true;
	if(TVPGetCommandLine(TJS_W("-smoothzoom"), &val))
	{
		ttstr str(val);
		if(str == TJS_W("no"))
			TVPZoomInterpolation = false;
		else
			TVPZoomInterpolation = true;
	}
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
tTVPBasicDrawDevice::tTVPBasicDrawDevice()
{
	TVPInitBasicDrawDeviceOptions(); // read and initialize options
	TargetWindow = NULL;
	DrawUpdateRectangle = false;
	BackBufferDirty = true;

	Direct3D = NULL;
	Direct3DDevice = NULL;
	Texture = NULL;
	ShouldShow = false;
	TextureBuffer = NULL;
	TextureWidth = TextureHeight = 0;
	VsyncInterval = 16;
 	ZeroMemory( &D3dPP, sizeof(D3dPP) );
 	ZeroMemory( &DispMode, sizeof(DispMode) );
}
//---------------------------------------------------------------------------
tTVPBasicDrawDevice::~tTVPBasicDrawDevice()
{
	DestroyD3DDevice();
}
//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::InvalidateAll()
{
	// レイヤ演算結果をすべてリクエストする
	// サーフェースが lost した際に内容を再構築する目的で用いる
	RequestInvalidation(tTVPRect(0, 0, DestRect.get_width(), DestRect.get_height()));
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::AddLayerManager(iTVPLayerManager * manager)
{
	if(inherited::Managers.size() > 0)
	{
		// "Basic" デバイスでは２つ以上のLayer Managerを登録できない
		TVPThrowExceptionMessage(TVPBasicDrawDeviceDoesNotSupporteLayerManagerMoreThanOne);
	}
	inherited::AddLayerManager(manager);

	manager->SetDesiredLayerType(ltOpaque); // ltOpaque な出力を受け取りたい
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetDestRectangle(const tTVPRect & rect)
{
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::NotifyLayerResize(iTVPLayerManager * manager)
{
	inherited::NotifyLayerResize(manager);

	BackBufferDirty = true;

	// テクスチャを捨てて作り直す。
//	CreateTexture();
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::Show()
{
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::StartBitmapCompletion(iTVPLayerManager * manager)
{
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::NotifyBitmapCompleted(iTVPLayerManager * manager,
	tjs_int x, tjs_int y, tTVPBaseTexture * bmp,
	const tTVPRect &cliprect, tTVPLayerType type, tjs_int opacity)
{
	// bits, bitmapinfo で表されるビットマップの cliprect の領域を、x, y に描画
	// する。
	// opacity と type は無視するしかないので無視する
	tjs_int w, h;
	GetSrcSize( w, h );
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::EndBitmapCompletion(iTVPLayerManager * manager)
{
	if(!TargetWindow) return;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetShowUpdateRect(bool b)
{
	DrawUpdateRectangle = b;
}






//---------------------------------------------------------------------------
// tTJSNI_BasicDrawDevice : BasicDrawDevice TJS native class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_BasicDrawDevice::ClassID = (tjs_uint32)-1;
tTJSNC_BasicDrawDevice::tTJSNC_BasicDrawDevice() :
	tTJSNativeClass(TJS_W("BasicDrawDevice"))
{
	// register native methods/properties

	TJS_BEGIN_NATIVE_MEMBERS(BasicDrawDevice)
	TJS_DECL_EMPTY_FINALIZE_METHOD
//----------------------------------------------------------------------
// constructor/methods
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/_this, /*var.type*/tTJSNI_BasicDrawDevice,
	/*TJS class name*/BasicDrawDevice)
{
	return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/BasicDrawDevice)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/recreate)
{
	TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_BasicDrawDevice);
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/recreate)
//----------------------------------------------------------------------


//---------------------------------------------------------------------------
//----------------------------------------------------------------------
// properties
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(interface)
{
	TJS_BEGIN_NATIVE_PROP_GETTER
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_BasicDrawDevice);
		*result = reinterpret_cast<tjs_int64>(_this->GetDevice());
		return TJS_S_OK;
	}
	TJS_END_NATIVE_PROP_GETTER

	TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(interface)
//----------------------------------------------------------------------
#define TVP_REGISTER_PTDD_ENUM(name) \
	TJS_BEGIN_NATIVE_PROP_DECL(name) \
	{ \
		TJS_BEGIN_NATIVE_PROP_GETTER \
		{ \
		*result = (tjs_int64)tTVPBasicDrawDevice::name; \
			return TJS_S_OK; \
		} \
		TJS_END_NATIVE_PROP_GETTER \
		TJS_DENY_NATIVE_PROP_SETTER \
	} \
	TJS_END_NATIVE_PROP_DECL(name)
// compatible for old kirikiri2
TVP_REGISTER_PTDD_ENUM(dtNone)
TVP_REGISTER_PTDD_ENUM(dtDrawDib)
TVP_REGISTER_PTDD_ENUM(dtDBGDI)
TVP_REGISTER_PTDD_ENUM(dtDBDD)
TVP_REGISTER_PTDD_ENUM(dtDBD3D)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(preferredDrawer)
{
	TJS_BEGIN_NATIVE_PROP_GETTER
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_BasicDrawDevice);
		*result = (tjs_int64)(_this->GetDevice()->GetPreferredDrawerType());
		return TJS_S_OK;
	}
	TJS_END_NATIVE_PROP_GETTER

	TJS_BEGIN_NATIVE_PROP_SETTER
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_BasicDrawDevice);
		_this->GetDevice()->SetPreferredDrawerType((tTVPBasicDrawDevice::tDrawerType)(tjs_int)*param);
		return TJS_S_OK;
	}
	TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(preferredDrawer)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(drawer)
{
	TJS_BEGIN_NATIVE_PROP_GETTER
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_BasicDrawDevice);
		*result = (tjs_int64)(_this->GetDevice()->GetDrawerType());
		return TJS_S_OK;
	}
	TJS_END_NATIVE_PROP_GETTER

	TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(drawer)
//----------------------------------------------------------------------
	TJS_END_NATIVE_MEMBERS
}
//---------------------------------------------------------------------------
iTJSNativeInstance *tTJSNC_BasicDrawDevice::CreateNativeInstance()
{
	return new tTJSNI_BasicDrawDevice();
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
tTJSNI_BasicDrawDevice::tTJSNI_BasicDrawDevice()
{
	Device = new tTVPBasicDrawDevice();
}
//---------------------------------------------------------------------------
tTJSNI_BasicDrawDevice::~tTJSNI_BasicDrawDevice()
{
	if(Device) Device->Destruct(), Device = NULL;
}
//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD
	tTJSNI_BasicDrawDevice::Construct(tjs_int numparams, tTJSVariant **param,
		iTJSDispatch2 *tjs_obj)
{
	return TJS_S_OK;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTJSNI_BasicDrawDevice::Invalidate()
{
	if(Device) Device->Destruct(), Device = NULL;
}
//---------------------------------------------------------------------------

