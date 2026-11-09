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
