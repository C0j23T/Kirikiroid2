#include "tjsCommHead.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <xxhash.h>

#include "BevyPlatform.h"
#include "DebugIntf.h"
#include "EventIntf.h"
#include "Random.h"
#include "RenderManager.h"
#include "TickCount.h"
#include "WindowImpl.h"

class TVPBevyWindowLayer;

namespace {

/// Windows in creation order
std::vector<TVPBevyWindowLayer *> Windows;

tjs_uint32 HostShiftState = 0;

TVPBevyWindowLayer *FrontWindow();

} // namespace

tjs_uint32 TVPGetCurrentShiftKeyState() { return HostShiftState; }

//---------------------------------------------------------------------------
// TVPBevyWindowLayer : iWindowLayer
//---------------------------------------------------------------------------
class TVPBevyWindowLayer : public iWindowLayer {
    tTJSNI_Window *Owner;
    std::string Caption;
    tjs_int PaintBoxWidth = 0, PaintBoxHeight = 0;
    bool Visible = false;

public:
    explicit TVPBevyWindowLayer(tTJSNI_Window *owner) : Owner(owner) {
        Windows.push_back(this);
    }
    ~TVPBevyWindowLayer() {
        Windows.erase(std::remove(Windows.begin(), Windows.end(), this), Windows.end());
    }
    TVPBevyWindowLayer(const TVPBevyWindowLayer &) = delete;
    TVPBevyWindowLayer &operator=(const TVPBevyWindowLayer &) = delete;
    tTJSNI_Window *GetWindow() const { return Owner; }
    iTJSDispatch2 *GetOwnerObject() const { return Owner ? Owner->GetOwnerNoAddRef() : nullptr; }
    //-- iWindowLayer : the paint box -----------------------------------------

    void SetPaintBoxSize(tjs_int w, tjs_int h) override {
        if (w < 0) w = 0;
        if (h < 0) h = 0;
        PaintBoxWidth = w;
        PaintBoxHeight = h;
    }
    void GetSize(tjs_int &w, tjs_int &h) override { w = PaintBoxWidth, h = PaintBoxHeight; }
    tjs_int GetWidth() const override { return PaintBoxWidth; }
    tjs_int GetHeight() const override { return PaintBoxHeight; }
    void GetWinSize(tjs_int &w, tjs_int &h) override { w = PaintBoxWidth, h = PaintBoxHeight; }
    void SetWidth(tjs_int w) override { PaintBoxWidth = w; }
    void SetHeight(tjs_int h) override { PaintBoxHeight = h; }
    void SetSize(tjs_int w, tjs_int h) override { PaintBoxWidth = w, PaintBoxHeight = h; }

    void UpdateDrawBuffer(iTVPTexture2D *) override {}

    //-- iWindowLayer : visibility and lifetime --------------------------------

    bool GetVisible() override { return Visible; }
    void SetVisible(bool bVisible) override {
        Visible = bVisible;
        if (bVisible) BringToFront();
    }
    void SetVisibleFromScript(bool b) override { SetVisible(b); }
    /// Whether the window takes part in event delivery.
    bool GetFormEnabled() override { return Visible; }
    /// A modal window blocks its owner; the host has no stacking to block, so
    /// showing it is the whole job.
    void ShowWindowAsModal() override { SetVisible(true); }
    void BringToFront() override {
        auto it = std::find(Windows.begin(), Windows.end(), this);
        if (it != Windows.end()) {
            Windows.erase(it);
            Windows.push_back(this);
        }
    }
    bool GetWindowActive() override { return FrontWindow() == this; }

    /// Object invalidation (the TJS object went away, e.g. at engine shutdown)
    /// and `Window.close()` both end here; see `Close`.
    void InvalidateClose() override { delete this; }
    void Close() override {
        // `Window.close()`: a native window is destroyed here, and a game's
        // main window going away is what ends the application. The port marks
        // the window instead and destroys it in `TVPBevyTickWindows`: this runs
        // inside the window's own script call, and taking the object apart
        // there would pull the caller's globals from under it -- the reference
        // port for a host without native windows defers it the same way
        // (environ/cocos2d/MainScene.cpp).
        if (ClosePending) return;
        ClosePending = true;
        SetVisible(false);
    }
    void OnCloseQueryCalled(bool) override {}
    /// Whether `close()` was called and the window still has to go away.
    bool IsClosePending() const { return ClosePending; }

    //-- iWindowLayer : update -------------------------------------------------

    void UpdateWindow(tTVPUpdateType) override {}
    void TickBeat() override {}

    //-- iWindowLayer : input --------------------------------------------------

    void InternalKeyDown(tjs_uint16 key, tjs_uint32 shift) override {
        tjs_uint32 tick = TVPGetRoughTickCount32();
        TVPPushEnvironNoise(&tick, sizeof(tick));
        TVPPushEnvironNoise(&key, sizeof(key));
        TVPPushEnvironNoise(&shift, sizeof(shift));
    }
    void OnKeyUp(tjs_uint16 vk, int shift) override {
        if (Owner) TVPPostInputEvent(new tTVPOnKeyUpInputEvent(Owner, vk, (tjs_uint32)shift));
    }
    void OnKeyPress(tjs_uint16 vk, int, bool, bool) override {
        if (Owner && vk) TVPPostInputEvent(new tTVPOnKeyPressInputEvent(Owner, (tjs_char)vk));
    }

    void GetCursorPos(tjs_int &x, tjs_int &y) override { x = CursorX, y = CursorY; }
    void SetCursorPos(tjs_int x, tjs_int y) override { CursorX = x, CursorY = y; }
    void SetDefaultMouseCursor() override {}
    void SetHintText(const ttstr &) override {}
    void SetAttentionPoint(tjs_int, tjs_int, const struct tTVPFont *) override {}
    void ResetMouseVelocity() override {}
    void ResetTouchVelocity(tjs_int) override {}
    bool GetMouseVelocity(float &x, float &y, float &speed) const override {
        x = y = speed = 0;
        return false;
    }
    bool GetUseMouseKey() const override { return false; }
    void SetUseMouseKey(bool) override {}

    //-- iWindowLayer : host-only concepts -------------------------------------

    /// Mapping from the primary layer's coordinates to the host's window. The
    /// host shows the paint box 1:1, so there is nothing to map.
    void ZoomRectangle(tjs_int &, tjs_int &, tjs_int &, tjs_int &) override {}
    void SetZoom(tjs_int numer, tjs_int denom) override {
        if (numer > 0 && denom > 0) ZoomNumer = numer, ZoomDenom = denom;
    }
    /// No native tooltips, IME or cursor shapes: the host draws the window and
    /// handles its own input.
    void SetImeMode(tTVPImeMode) override {}
    void ResetImeMode() override {}
    tTVPImeMode GetDefaultImeMode() const override { return imDisable; }
    cocos2d::Node *GetPrimaryArea() override { return nullptr; }
    const char *GetCaption() override { return Caption.c_str(); }
    void SetCaption(const std::string &caption) override { Caption = caption; }

private:
    tjs_int CursorX = 0, CursorY = 0;
    /// `close()` was called; the window is destroyed in `TVPBevyTickWindows`.
    bool ClosePending = false;
};

namespace {

TVPBevyWindowLayer *FrontWindow() {
    for (auto it = Windows.rbegin(); it != Windows.rend(); ++it) {
        TVPBevyWindowLayer *window = *it;
        if (window->GetVisible()) return window;
    }
    return nullptr;
}

/// Converts the host's button index to the engine's bit flag.
tTVPMouseButton MouseButtonFlag(int button) {
    switch (button) {
    case 1: return mbRight;
    case 2: return mbMiddle;
    default: return mbLeft;
    }
}

/// Takes the windows whose script called `close()` off the list (see
/// `TVPBevyWindowLayer::Close`).
void DestroyClosePendingWindows() {
    // Copied first: a window going away runs its `onCloseQuery` handler, which
    // saves the game's settings and may well open or close windows of its own.
    std::vector<TVPBevyWindowLayer *> pending;
    for (TVPBevyWindowLayer *window : Windows) {
        if (window->IsClosePending() && window->GetOwnerObject()) pending.push_back(window);
    }
    for (TVPBevyWindowLayer *window : pending) {
        iTJSDispatch2 *owner = window->GetOwnerObject();
        owner->AddRef();
        // What a native window manager tells the window before destroying it;
        // this is where a KAG game persists its system variables.
        static ttstr eventname(TJS_W("onCloseQuery"));
        tTJSVariant arg[1] = {true};
        try {
            TVPPostEvent(owner, owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
        } catch (...) {
            // A handler that throws must not keep the window alive.
        }
        // Invalidating the TJS object unregisters the window -- which ends the
        // application when it was the main one -- and deletes the layer.
        try {
            owner->Invalidate(0, NULL, NULL, owner);
        } catch (...) {
        }
        owner->Release();
    }
}

} // namespace

//---------------------------------------------------------------------------
// what the engine calls
//---------------------------------------------------------------------------

iWindowLayer *TVPCreateAndAddWindow(tTJSNI_Window *w) {
    // Starts hidden: the script shows it (`visible = true`) when it is built.
    return new TVPBevyWindowLayer(w);
}

//---------------------------------------------------------------------------
// what the host calls
//---------------------------------------------------------------------------

tTJSNI_BaseLayer *TVPBevyFrontWindowLayer() {
    TVPBevyWindowLayer *window = FrontWindow();
    if (!window) return nullptr;
    tTJSNI_Window *owner = window->GetWindow();
    iTVPDrawDevice *device = owner ? owner->GetDrawDevice() : nullptr;
    return device ? device->GetPrimaryLayer() : nullptr;
}

void TVPBevyWindowMouseMove(int x, int y) {
    TVPBevyWindowLayer *window = FrontWindow();
    if (!window) return;
    TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(window->GetWindow(), x, y,
        TVPGetCurrentShiftKeyState()));
}

void TVPBevyWindowMouseDown(int x, int y, int button) {
    TVPBevyWindowLayer *window = FrontWindow();
    if (!window) return;
    TVPPostInputEvent(new tTVPOnMouseDownInputEvent(window->GetWindow(), x, y,
        MouseButtonFlag(button), TVPGetCurrentShiftKeyState()));
}

void TVPBevyWindowMouseUp(int x, int y, int button) {
    TVPBevyWindowLayer *window = FrontWindow();
    if (!window) return;
    TVPPostInputEvent(new tTVPOnMouseUpInputEvent(window->GetWindow(), x, y,
        MouseButtonFlag(button), TVPGetCurrentShiftKeyState()));
}

void TVPBevyWindowKeyDown(int vk, unsigned int shift) {
    HostShiftState = shift;
    TVPBevyWindowLayer *window = FrontWindow();
    if (!window) return;
    TVPPostInputEvent(new tTVPOnKeyDownInputEvent(window->GetWindow(), (tjs_uint)vk, shift));
}

void TVPBevyWindowKeyUp(int vk, unsigned int shift) {
    HostShiftState = shift;
    TVPBevyWindowLayer *window = FrontWindow();
    if (!window) return;
    TVPPostInputEvent(new tTVPOnKeyUpInputEvent(window->GetWindow(), (tjs_uint)vk, shift));
}

void TVPBevyTickWindows() {
    TVPDeliverWindowUpdateEvents();
    DestroyClosePendingWindows();
}

void TVPBevyCloseWindows() {
    // A KAG game persists its system variables in its `onCloseQuery` handler
    // (`saveSystemVariables`), so the host tells the windows they are closing
    // before the engine goes away -- the notification a native window manager
    // would send. Without it, settings the game only stores on exit (volumes,
    // message speed) are lost, and the next session reads them back as void.
    static ttstr eventname(TJS_W("onCloseQuery"));
    for (TVPBevyWindowLayer *window : Windows) {
        iTJSDispatch2 *owner = window->GetOwnerObject();
        if (!owner) continue;
        tTJSVariant arg[1] = {true};
        try {
            TVPPostEvent(owner, owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
        } catch (...) {
            // A game that throws from its close handler must not keep the host
            // from shutting down.
        }
    }
}

void TVPBevyResetWindows() {
    // The TJS objects are gone with the engine; the windows were destroyed
    // through `InvalidateClose`, this only guards against a leftover entry.
    Windows.clear();
}

void *TVPBevyFrontWindow() {
    auto window = FrontWindow();
    return window ? window->GetWindow() : nullptr;
}
