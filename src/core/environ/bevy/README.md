# bevy port

Platform layer that lets the Kirikiroid2 engine core run inside a host
application ([bevy_krkr](../../../../../README.md)) instead of the cocos2d-x
front end. The host owns the window, frame loop, input and audio output; the
engine renders off-screen (`BitmapLayerTreeOwner`) and the host uploads the
composited pixels as a texture.

License: the files in this directory and the `bevy port:` changes elsewhere on
this branch are GPL-2.0-or-later. The rest of Kirikiroid2 keeps its own license
(`LICENSE` at the repository root).

| File | Replaces |
|---|---|
| `BevyPlatform.h/.cpp` | Linux versions of environ/android, environ/cocos2d/MainScene and environ/win32/Platform functions (log, screen size, files, tick count, dialogs, exit) |
| `BevyUnsupported.cpp` | Subsystems the host provides or that are not built: Window, input, sound output, video, BPG/JXR/PVR images, ZIP/7z archives. They fail with a clear message. |
| `BevyFileUtils.cpp` + `compat/platform/CCFileUtils.h` | the subset of cocos2d::FileUtils the core uses |
| `compat/` | headers of cocos2d-x and of files missing from the open-source release (`Protect.h`, `tvpgl_route.h`, `aligned_allocator.h`), forwarding headers to system libraries |

Changes to upstream files (all marked `bevy port`):

- `tjs2/tjsConfig.cpp`: system headers moved out of `namespace TJS`; `va_copy`
  in `_vsnprintf` (`(va_list *)&ap` is wrong on x86-64); element/byte count in `pad()`.
- `environ/Platform.h`: `tTVP_stat` with glibc's `st_atime` macros.
- `base/UtilStreams.cpp`: archive extraction for the cocos file browser compiled out.
- `environ/ConfigManager/LocaleConfigManager.cpp`: cocos UI helpers compiled out;
  no infinite recursion without locale files; public tinyxml2 API.

The source list, include paths, flags and system libraries are in
bevy_krkr's `codegen/src/port.rs`. The core must be compiled with
`-fno-delete-null-pointer-checks` (TJS2 calls methods on NULL strings).
