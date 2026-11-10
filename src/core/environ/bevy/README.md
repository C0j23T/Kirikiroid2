# bevy port

Platform layer that lets the Kirikiroid2 engine core run inside a host
application ([bevy_krkr](../../../../../README.md)) instead of the cocos2d-x
front end. The host owns the window, frame loop, input and audio output; the
engine sends decoded images, glyph masks and texture commands to Bevy.
Bevy owns textures and performs all pixel operations and layer composition.

License: the files in this directory and the `bevy port:` changes elsewhere on
this branch are GPL-2.0-or-later. The rest of Kirikiroid2 keeps its own license
(`LICENSE` at the repository root).

| File | Replaces |
|---|---|
| `BevyPlatform.h/.cpp` | Linux versions of environ/android, environ/cocos2d/MainScene and environ/win32/Platform functions (log, screen size, files, tick count, dialogs, exit) |
| `Archives.cpp` | the vendored 7-Zip (`base/7zArchive.cpp`) and minizip (`base/ZIPArchive.cpp`) archives, which are not part of the open-source release: `krkr_shim` (`sevenz-rust2` / `zip`) parses them and reports the entry list through `krkr_archive_open`'s visitor, the engine then reads entries with `krkr_archive_read` |
| `BevyRender.cpp` | Texture handles and command dispatch to the host GPU |
| `BevyScene.cpp` | Layer order, clipping, group opacity and transition commands |
| `BevyWindowLayer.cpp` | the platform half of the `Window` class (`visual/win32/WindowImpl.cpp`'s `TTVPWindowForm` / cocos2d's `TVPWindowLayer`): the host owns display and posts input to the script window |
| `BevyPlugins.cpp` + `plugins/` | `tTVPApplication::StartApplication`'s plugin loading (`TVPLoadPluigins`): collects the ncbind plugins compiled into the engine so that a script's `Plugins.link` can attach them, and attaches the ones a game expects to find already there (`xp3filter`, and `Scripts.foreach` / `Scripts.getObjectKeys` from `plugins/ScriptsExt.cpp`, the KAGParserEx.dll replacement). Which plugins exist is `PLUGINS` in bevy_krkr's `codegen/src/port.rs`; the wrapper translation units that keep them in the link are generated from it |
| `BevySound.cpp` | the `iTVPSoundBuffer` of `sound/win32/WaveImpl.cpp`: Kirikiri's `WaveSoundBuffer` pushes decoded PCM (RIFF-WAV, Vorbis, and Opus when built with the `opus` feature) into it, the port keeps it queued and mixes it on request, and the host pulls the mix with `krkr_audio_mix` and writes it to the sound device |
| `BevyVideo.cpp` | VideoOverlay command adapter; Rust owns decoding and uploads. |
| `BevyUnsupported.cpp` | Subsystems that are not built: menus, DirectSound device entry points, BPG/JXR/PVR images. They fail with a clear message. |
| `BevyFileUtils.cpp` + `compat/platform/CCFileUtils.h` | the subset of cocos2d::FileUtils the core uses |
| `WEBPLoader.cpp` | `visual/LoadWEBP.cpp`: `.webp` images are decoded by `krkr_shim` (`image-webp`) through `krkr_webp_*`, so libwebp is not linked |
| `ConfigFiles.h` | tinyxml2 for the preference and locale files (`environ/ConfigManager`): `krkr_shim` (`quick-xml`) parses and writes them, file I/O included, through `krkr_preference_*` / `krkr_locale_*` |
| `compat/` | headers of cocos2d-x and of files missing from the open-source release (`Protect.h`, `tvpgl_route.h`, `aligned_allocator.h`), forwarding headers to system libraries, and `krkr_shim.h` |

Changes to upstream files:

- `tjs2/tjsConfig.cpp`: system headers moved out of `namespace TJS`; `va_copy`
  in `_vsnprintf` (`(va_list *)&ap` is wrong on x86-64); element/byte count in `pad()`.
- `environ/Platform.h`: `tTVP_stat` with glibc's `st_atime` macros.
- `base/UtilStreams.cpp`: archive extraction for the cocos file browser compiled out.
- `base/StorageIntf.cpp`: `tTVPArchiveCache::Get` reports "cannot find storage"
  when `TVPOpenArchive` returns NULL. Upstream "succeeded" with the NULL and
  dereferenced it, so any file whose format no opener accepts (an archive
  format the build does not have, a corrupt archive) crashed the engine.
- `environ/ConfigManager/LocaleConfigManager.cpp`: cocos UI helpers compiled out;
  no infinite recursion without locale files;
- `visual/LoadPNG.cpp`: PNG is decoded and encoded by `krkr_shim` (the `png`
  crate) through `krkr_png_*`, so libpng is not linked;
- `visual/LoadJPEG.cpp`: JPEG is decoded and encoded by `krkr_shim`
  (`zune-jpeg`, `image`) through `krkr_jpeg_*`, so libjpeg-turbo is not linked.
  The `-jpegdec` option that picked a DCT method went with it, as did the
  `subsampling`/`dct`/`progressive` save options;
- `base/XP3Archive.cpp`, `base/TextStream.cpp`, `msg/win32/OptionsDesc.cpp`,
  `base/win32/PluginImpl.cpp`: zlib's one-shot calls go through
  `krkr_zlib_*` (`miniz_oxide`), so zlib is not linked. LayerExSave delegates
  PNG/TLG5 encoding and background tasks to Rust through `krkr_save_image_*`;
  PNG uses the existing Rust `png` crate. The compressed text stream holds its
  text and compresses it when it closes, instead of streaming
  through a deflate state;
- `sound/VorbisWaveDecoder.cpp`: the Opus decoder moved to
  `sound/OpusWaveDecoder.cpp`, which compiles it only under `KRKR_BEVY_OPUS`
  (krkr_sys feature `opus`) and otherwise leaves `OpusWaveDecoderCreator`
  returning NULL, like the ffmpeg creator, so libopusfile is not linked;
- `base/win32/SysInitImpl.cpp`: `TVPBeforeSystemUninit` drops the queued events
  and detaches the compiled-in plugins. Both hold references to TJS objects and
  have to be released while those still exist; upstream ends the process right
  after a shutdown and never noticed, a host that shuts the engine down and
  keeps running does (`System.exit` takes exactly this path).

The source list, include paths, flags and system libraries are in
bevy_krkr's `codegen/src/port.rs`. The core must be compiled with
`-fno-delete-null-pointer-checks` (TJS2 calls methods on NULL strings).
