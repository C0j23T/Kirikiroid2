// bevy port: the libraries the engine core uses, implemented in Rust by the
// `krkr_shim` crate (crates/krkr_shim)
//
// Keep in sync with crates/krkr_shim/src/lib.rs and src/archive.rs.
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

typedef struct KrkrArchiveReader {
	void *userdata;
	long long (*read)(void *userdata, void *buf, long long len);
	long long (*seek)(void *userdata, long long offset, int whence);
	long long size;
} KrkrArchiveReader;

#define KRKR_ARCHIVE_7Z 1
#define KRKR_ARCHIVE_ZIP 2

typedef struct KrkrArchive KrkrArchive;
KrkrArchive *krkr_archive_open(const KrkrArchiveReader *reader, int kind);
void krkr_archive_close(KrkrArchive *archive);

size_t krkr_archive_count(const KrkrArchive *archive);
size_t krkr_archive_size(const KrkrArchive *archive, size_t index);
size_t krkr_archive_name(const KrkrArchive *archive, size_t index, char *buf, size_t len);
size_t krkr_archive_read(KrkrArchive *archive, size_t index, void *buf, size_t len);

#ifdef __cplusplus
}
#endif
