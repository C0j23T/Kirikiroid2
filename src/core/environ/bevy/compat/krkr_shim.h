// bevy port: the libraries the engine core uses, implemented in Rust by the
// `krkr_shim` crate (crates/krkr_shim)
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int XXH32_hash_t;
typedef unsigned long long XXH64_hash_t;

XXH32_hash_t XXH32(const void *input, size_t length, XXH32_hash_t seed);
XXH64_hash_t XXH3_64bits_withSeed(const void *input, size_t length, XXH64_hash_t seed);

int LZ4_compress_default(const char *src, char *dst, int srcSize, int dstCapacity);

size_t krkr_lz4_decompress(const char *src, size_t srcSize, char *dst, size_t dstSize);

// Archives (environ/bevy/Archives.cpp). A visitor runs on Rust's stack: it must
// not throw, and must not call back into the shim.
typedef struct KrkrStream {
	void *userdata;
	long long (*read)(void *userdata, void *buf, long long len);
	long long (*seek)(void *userdata, long long offset, int whence);
	long long size;
} KrkrStream;

typedef struct KrkrArchiveEntries {
	void *userdata;
	// `index` is what krkr_archive_read takes; `name` is UTF-8, NUL-terminated,
	// and valid only for the call.
	void (*entry)(void *userdata, size_t index, const char *name, size_t size);
} KrkrArchiveEntries;

#define KRKR_ARCHIVE_7Z 1
#define KRKR_ARCHIVE_ZIP 2

typedef struct KrkrArchive KrkrArchive;
// Null when the stream is not an archive of `kind` or holds no files.
KrkrArchive *krkr_archive_open(const KrkrStream *stream, int kind,
	const KrkrArchiveEntries *entries);
void krkr_archive_close(KrkrArchive *archive);
// Bytes written, or SIZE_MAX.
size_t krkr_archive_read(KrkrArchive *archive, size_t index, void *buf, size_t len);

typedef struct KrkrWebpInfo {
	unsigned width;
	unsigned height;
	int has_alpha;
} KrkrWebpInfo;

// 0 on success, -1 when the data is not a WebP image.
int krkr_webp_info(const void *src, size_t srcLen, KrkrWebpInfo *info);
// Bytes written to `dst` (width * height * 4), or SIZE_MAX.
size_t krkr_webp_decode(const void *src, size_t srcLen, void *dst, size_t dstLen);

// JPEG (visual/LoadJPEG.cpp). `gray` selects one byte per pixel instead of four.
// 0 on success, -1 when the data is not a JPEG.
int krkr_jpeg_info(const void *src, size_t srcLen, unsigned *width, unsigned *height);
// Bytes written, or SIZE_MAX.
size_t krkr_jpeg_decode(const void *src, size_t srcLen, int gray, void *dst, size_t dstLen);

// Chroma subsampling for krkr_jpeg_encode.
#define KRKR_JPEG_420 0
#define KRKR_JPEG_422 1
#define KRKR_JPEG_444 2

// 0 on success; the file is allocated by the shim and released with
// krkr_jpeg_free.
int krkr_jpeg_encode(const void *pixels, size_t pixelsLen, unsigned width,
	unsigned height, int quality, int subsampling, void **jpeg, size_t *jpegLen);
void krkr_jpeg_free(void *jpeg, size_t jpegLen);

// PNG (visual/LoadPNG.cpp). The decode target is what a row of the destination
// bitmap holds, and the return value is the number of bytes written.
#define KRKR_PNG_RGBA 0 // 4 bytes per pixel
#define KRKR_PNG_GRAY 1 // 1 byte per pixel
#define KRKR_PNG_INDEX 2 // 1 byte per pixel: palette index, or gray value

// IHDR color types, as KrkrPngInfo.color_type carries them.
#define KRKR_PNG_COLOR_GRAY 0
#define KRKR_PNG_COLOR_PALETTE 3

typedef struct KrkrPngInfo {
	unsigned width;
	unsigned height;
	int bit_depth; // IHDR bit depth
	int bpp; // bit depth times the channel count
	int color_type; // IHDR color type
	int has_alpha;
	// Non-zero when the chunk is there. `offset_unit` and `vpage_unit` are 0
	// (pixel) or 1 (micrometer); `resolution_unit` is 0 (unknown) or 1 (meter).
	int has_offset;
	int offset_x, offset_y, offset_unit;
	int has_resolution;
	unsigned resolution_x, resolution_y;
	int resolution_unit;
	int has_vpage;
	unsigned vpage_width, vpage_height;
	int vpage_unit;
} KrkrPngInfo;

// 0 on success, -1 when the data is not a PNG.
int krkr_png_info(const void *src, size_t srcLen, KrkrPngInfo *info);
// Bytes written, or SIZE_MAX. `key` is a palette index to make transparent.
size_t krkr_png_decode(const void *src, size_t srcLen, int target, int key,
	void *dst, size_t dstLen);
// 0 on success; the file is allocated by the shim and released with
// krkr_png_free.
int krkr_png_encode(const void *pixels, size_t pixelsLen, unsigned width,
	unsigned height, int bpp, void **png, size_t *pngLen);
void krkr_png_free(void *png, size_t pngLen);

// zlib streams, for XP3, TextStream and the ZLIB_* plugin functions. `bound`
// is the most `compress` can write for that many input bytes; the other two
// return the bytes written, or SIZE_MAX.
size_t krkr_zlib_bound(size_t srcLen);
size_t krkr_zlib_compress(void *dst, size_t dstLen, const void *src, size_t srcLen, int level);
size_t krkr_zlib_uncompress(void *dst, size_t dstLen, const void *src, size_t srcLen);

// Config files (environ/bevy/ConfigFiles.h). A visitor runs on Rust's stack: it
// must not throw, and must not call back into the shim.
#define KRKR_PREFERENCE_ITEM 0
#define KRKR_PREFERENCE_CUSTOM 1
#define KRKR_PREFERENCE_KEYMAP 2

typedef struct KrkrPreferenceVisitor {
	void *userdata;
	void (*entry)(void *userdata, int list, const char *key, const char *value);
	void (*keymap)(void *userdata, int key, int value);
} KrkrPreferenceVisitor;

typedef struct KrkrTextVisitor {
	void *userdata;
	void (*text)(void *userdata, const char *id, const char *text);
} KrkrTextVisitor;

typedef struct KrkrPreferenceRecord {
	int list;
	int key; // KRKR_PREFERENCE_KEYMAP: key code before the remap
	int value; // ... and after it
	const char *key_text; // ITEM / CUSTOM: UTF-8, read during the call
	const char *value_text;
} KrkrPreferenceRecord;

// 0 when the file was read and is well-formed XML.
int krkr_preference_read(const char *path, const KrkrPreferenceVisitor *visitor);
// 0 when the file was written.
int krkr_preference_write(const char *path, const KrkrPreferenceRecord *records, size_t count);

// The number of messages; a document that is not well-formed XML has none.
size_t krkr_locale_read(const void *data, size_t len, const KrkrTextVisitor *visitor);

#ifdef __cplusplus
}
#endif
