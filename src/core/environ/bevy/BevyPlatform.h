#pragma once
#include "tjsCommHead.h"

/// Receives every engine log line (TVPAddLog / TVPAddImportantLog ...).
/// `utf8` is NUL-terminated and only valid during the call.
typedef void (*TVPBevyLogSink)(const char *utf8, bool important, void *userdata);

/// Install the log sink. NULL restores the default (stderr).
void TVPBevySetLogSink(TVPBevyLogSink sink, void *userdata);

/// Size reported by System.screenWidth/Height and System.desktop*.
/// Defaults to 1280x720.
void TVPBevySetScreenSize(int width, int height);

/// Attach the ncbind plugins compiled into the engine to their TJS classes
/// (e.g. System.addFont). Call while starting an instance, after the script
/// engine exists and before any script runs.
void TVPBevyLoadInternalPlugins();

/// Detach those plugins again. Call while shutting down, *before* the script
/// engine is uninitialised.
void TVPBevyUnloadInternalPlugins();

/// Round-trip to the TJS `Window` event handlers of the front-most window.
/// Coordinates are in the window's own pixels (the paint box), and `button` is
/// 0 left, 1 right, 2 middle. Ignored when no window is visible.
void TVPBevyWindowMouseMove(int x, int y);
void TVPBevyWindowMouseDown(int x, int y, int button);
void TVPBevyWindowMouseUp(int x, int y, int button);
/// `vk` is a Win32 virtual-key code, as Kirikiri scripts expect (13 = Enter).
void TVPBevyWindowKeyDown(int vk, unsigned int shift);
void TVPBevyWindowKeyUp(int vk, unsigned int shift);

/// Forget every window. Called when the engine shuts down: the TJS objects
/// they belonged to are gone, so the port must not keep pointers to them.
void TVPBevyResetWindows();

/// Primary layer of the front-most visible Window, or NULL. This is the layer
/// tree `TVPBevyBuildScene` walks when a game brings its own window.
class tTJSNI_BaseLayer;
tTJSNI_BaseLayer *TVPBevyFrontWindowLayer();

uint64_t TVPBevyBuildScene(void *screen, int *width, int *height);
void TVPBevyResetScene();

/// Tell every window it is closing (`onCloseQuery`), the notification a native
/// window manager would send. A KAG game saves its system variables there, so
/// call this before the engine shuts down.
void TVPBevyCloseWindows();

/// Deliver window events and finish deferred closes.
void TVPBevyTickWindows();

/// Mix every sound buffer the engine is playing into `out`, as `frames`
/// interleaved stereo float frames at `out_rate` Hz. `out` is overwritten. Safe
/// to call from any thread: the host's audio callback is the caller.
void TVPBevyMixAudio(float *out, unsigned int frames, double out_rate);

/// Forget every sound buffer. Called when the engine shuts down.
void TVPBevyResetSound();

/// Thrown by TVPExitApplication: the engine asked the process to exit.
/// The port never calls exit(); the host decides what to do. The exit code is
/// in TVPTerminateCode and TVPTerminated is set.
struct TVPBevyExitRequest {
    int code;
};

/// Leave the engine call that is running through its C ABI entry point
/// (defined by the shim, codegen/templates/shim_prelude.cpp). False when no
/// entry point is waiting, in which case the caller unwinds with
/// [`TVPBevyExitRequest`] instead.
bool TVPBevyLeaveThroughExitBoundary();
