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
| `BevyArchives.cpp` | the vendored 7-Zip (`base/7zArchive.cpp`) and minizip (`base/ZIPArchive.cpp`) archives, which are not part of the open-source release: 7z and ZIP are read through libarchive instead |
| `BevyWindowLayer.cpp` | the platform half of the `Window` class (`visual/win32/WindowImpl.cpp`'s `TTVPWindowForm` / cocos2d's `TVPWindowLayer`): the window renders off-screen and the host shows its pixels and posts input |
| `BevyPlugins.cpp` + `plugins/` | `tTVPApplication::StartApplication`'s plugin loading (`TVPLoadPluigins`): collects the ncbind plugins compiled into the engine so that a script's `Plugins.link` can attach them, and attaches the ones a game expects to find already there (`xp3filter`, and `Scripts.foreach` / `Scripts.getObjectKeys` from `plugins/ScriptsExt.cpp`, the KAGParserEx.dll replacement). Which plugins exist is `PLUGINS` in bevy_krkr's `codegen/src/port.rs`; the wrapper translation units that keep them in the link are generated from it |
| `BevySound.cpp` | the `iTVPSoundBuffer` of `sound/win32/WaveImpl.cpp`: Kirikiri's `WaveSoundBuffer` pushes decoded PCM (RIFF-WAV, Vorbis, Opus) into it, the port keeps it queued and mixes it on request, and the host pulls the mix with `krkr_audio_mix` and writes it to the sound device |
| `BevyUnsupported.cpp` | Subsystems that are not built: menus, DirectSound device entry points, video, BPG/JXR/PVR images. They fail with a clear message. |
| `BevyFileUtils.cpp` + `compat/platform/CCFileUtils.h` | the subset of cocos2d::FileUtils the core uses |
| `compat/` | headers of cocos2d-x and of files missing from the open-source release (`Protect.h`, `tvpgl_route.h`, `aligned_allocator.h`), forwarding headers to system libraries, and `krkr_shim.h` |

Changes to upstream files (all marked `bevy port`):

- `tjs2/tjsConfig.cpp`: system headers moved out of `namespace TJS`; `va_copy`
  in `_vsnprintf` (`(va_list *)&ap` is wrong on x86-64); element/byte count in `pad()`.
- `environ/Platform.h`: `tTVP_stat` with glibc's `st_atime` macros.
- `base/UtilStreams.cpp`: archive extraction for the cocos file browser compiled out.
- `base/StorageIntf.cpp`: `tTVPArchiveCache::Get` reports "cannot find storage"
  when `TVPOpenArchive` returns NULL. Upstream "succeeded" with the NULL and
  dereferenced it, so any file whose format no opener accepts (an archive
  format the build does not have, a corrupt archive) crashed the engine.
- `environ/ConfigManager/LocaleConfigManager.cpp`: cocos UI helpers compiled out;
  no infinite recursion without locale files; public tinyxml2 API.
- `base/win32/SysInitImpl.cpp`: `TVPBeforeSystemUninit` drops the queued events
  and detaches the compiled-in plugins. Both hold references to TJS objects and
  have to be released while those still exist; upstream ends the process right
  after a shutdown and never noticed, a host that shuts the engine down and
  keeps running does (`System.exit` takes exactly this path).

The source list, include paths, flags and system libraries are in
bevy_krkr's `codegen/src/port.rs`. The core must be compiled with
`-fno-delete-null-pointer-checks` (TJS2 calls methods on NULL strings).
