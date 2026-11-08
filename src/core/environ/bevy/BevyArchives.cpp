// bevy port: 7z / ZIP archive support.
//
// Upstream reads 7z through a vendored copy of the 7-Zip C decoder
// (base/7zArchive.cpp) and ZIP through a vendored minizip
// (base/ZIPArchive.cpp); neither is part of this checkout. Both formats are
// read through libarchive instead, which is a system library like freetype or
// opencv here. Only what the engine needs is used: list the entries, and
// decompress one entry into memory on demand.
//
// Games commonly ship some of their archives as 7z files (e.g. voice/EV/CG
// archives next to a plain XP3 data archive), so without this the storage
// layer cannot find them at all.
#include "tjsCommHead.h"

#include <algorithm>
#include <clocale>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <archive.h>
#include <archive_entry.h>

#include "CharacterSet.h"
#include "StorageIntf.h"
#include "UtilStreams.h"

namespace {

// ── libarchive over a tTJSBinaryStream ─────────────────────────────────────

/// Client data of the libarchive callbacks. The stream stays owned by the
/// caller until the archive has been opened successfully (see
/// `LibArchiveArchive::OwnsStream`): a format opener that rejects the stream
/// must leave it usable for the next opener, which tvp's TVPOpenArchive
/// relies on when it rewinds and tries the next archive format.
struct StreamSource {
	tTJSBinaryStream *Stream;
	std::vector<char> Buffer;
};

la_ssize_t read_callback(struct archive *, void *client, const void **buffer) {
	StreamSource *src = static_cast<StreamSource *>(client);
	if (src->Buffer.empty()) src->Buffer.resize(64 * 1024);
	tjs_uint read = src->Stream->Read(src->Buffer.data(), (tjs_uint)src->Buffer.size());
	*buffer = src->Buffer.data();
	return (la_ssize_t)read;
}

la_int64_t seek_callback(struct archive *, void *client, la_int64_t offset, int whence) {
	StreamSource *src = static_cast<StreamSource *>(client);
	tjs_int tjs_whence = TJS_BS_SEEK_SET;
	switch (whence) {
	case SEEK_CUR: tjs_whence = TJS_BS_SEEK_CUR; break;
	case SEEK_END: tjs_whence = TJS_BS_SEEK_END; break;
	default: break;
	}
	tjs_int64 pos = (tjs_int64)src->Stream->Seek((tjs_int64)offset, tjs_whence);
	return (la_int64_t)pos;
}

/// Formats we take from libarchive. Fixed instead of `archive_read_support_format_all`,
/// which is not distinguishable from a failure of the format we asked for.
/// TAR is not here: `base/TARArchive.cpp` reads it, and each opener is only
/// called for the signature it checks.
void support_formats(struct archive *arc) {
	archive_read_support_filter_all(arc);
	archive_read_support_format_7zip(arc);
	archive_read_support_format_zip(arc);
}

/// libarchive converts 7z/zip entry names from UTF-16 to the character locale
/// of the process, and refuses the entry when that fails. The C locale cannot
/// represent any non-ASCII name, and game archives do name files in Japanese
/// (`face白狗a.txt`, …), so the character locale has to be UTF-8.
///
/// This is safe for the rest of the engine: TJS2 converts narrow strings as
/// UTF-8 on its own (see tjsConfig.cpp `TJS_mbstowcs`), independently of the
/// locale, and nothing else in the core formats numbers or dates through it.
void EnsureUtf8Locale() {
	static bool Done = false;
	if (Done) return;
	Done = true;
	for (const char *name : {"C.UTF-8", "C.utf8", "en_US.UTF-8"}) {
		if (std::setlocale(LC_CTYPE, name)) return;
	}
}

/// Converts a path from libarchive (UTF-8) the way Kirikiri storages are
/// spelled (UTF-16 `ttstr`).
ttstr PathFromUtf8(const char *path) {
	tjs_uint len = (tjs_uint)std::strlen(path);
	tjs_int chars = TVPUtf8ToWideCharString(path, len, nullptr);
	ttstr name;
	TVPUtf8ToWideCharString(path, len, name.AllocBuffer(chars));
	name.FixLen();
	return name;
}

// ── the archive ────────────────────────────────────────────────────────────

/// 7z / ZIP archive read through libarchive.
///
/// libarchive reports entries one after another, so a request for an entry
/// that is behind the last one served restarts the iteration from the top.
class LibArchiveArchive : public tTVPArchive {
	struct Entry {
		ttstr Name;
		tjs_uint Index; // archive_read_next_header position
	};

	StreamSource Source;
	struct archive *Arc = nullptr;
	std::vector<Entry> Entries;
	/// Index of the entry the next `archive_read_next_header` will report;
	/// valid only while `Arc` is open.
	tjs_uint NextIndex = 0;
	/// Set once the archive is open; from then on the stream is ours.
	bool OwnsStream = false;

public:
	LibArchiveArchive(const ttstr &name, tTJSBinaryStream *stream) : tTVPArchive(name) {
		Source.Stream = stream;
	}
	~LibArchiveArchive() override {
		Close();
		if (OwnsStream) delete Source.Stream;
	}

	/// Reads the entry list. `false` when libarchive cannot open the stream;
	/// the caller then tries the next archive format, as upstream does.
	bool Open(bool normalizeFileName) {
		if (!Restart()) return false;
		struct archive_entry *entry;
		tjs_uint index = 0;
		while (archive_read_next_header(Arc, &entry) == ARCHIVE_OK) {
			const char *path = archive_entry_pathname(entry);
			if (path && archive_entry_filetype(entry) != AE_IFDIR) {
				ttstr name = PathFromUtf8(path);
				if (normalizeFileName) NormalizeInArchiveStorageName(name);
				Entries.push_back(Entry{name, index});
			}
			archive_read_data_skip(Arc);
			index++;
		}
		// tTVPArchive requires the entries sorted by name for its hash lookup.
		if (normalizeFileName) {
			std::stable_sort(Entries.begin(), Entries.end(),
				[](const Entry &a, const Entry &b) { return a.Name < b.Name; });
		}
		if (Entries.empty()) return false;
		OwnsStream = true;
		return true;
	}

	tjs_uint GetCount() override { return (tjs_uint)Entries.size(); }
	ttstr GetName(tjs_uint idx) override { return Entries[idx].Name; }

	tTJSBinaryStream *CreateStreamByIndex(tjs_uint idx) override {
		if (idx >= Entries.size()) return nullptr;
		tjs_uint target = Entries[idx].Index;
		// The iteration only moves forward, so an entry behind the current
		// position needs a new handle. Stepping over the entries in front of
		// the wanted one is cheap: these archives store one block per file
		// (non-solid), so skipping does not decompress anything.
		if (!Arc || NextIndex > target) {
			if (!Restart()) return nullptr;
		}
		struct archive_entry *entry = nullptr;
		while (NextIndex < target) {
			if (archive_read_next_header(Arc, &entry) != ARCHIVE_OK) {
				Close();
				return nullptr;
			}
			archive_read_data_skip(Arc);
			NextIndex++;
		}
		if (archive_read_next_header(Arc, &entry) != ARCHIVE_OK) {
			Close();
			return nullptr;
		}
		NextIndex++;

		tjs_uint64 size = (tjs_uint64)archive_entry_size(entry);
		std::unique_ptr<tTVPMemoryStream> stream(new tTVPMemoryStream());
		if (size > 0) {
			stream->SetSize((tjs_uint)size);
			char *out = (char *)stream->GetInternalBuffer();
			tjs_uint64 done = 0;
			while (done < size) {
				la_ssize_t got = archive_read_data(Arc, out + done, (size_t)(size - done));
				if (got <= 0) break;
				done += (tjs_uint64)got;
			}
			if (done != size) stream->SetSize((tjs_uint)done);
		}
		return stream.release();
	}

private:
	/// Drops the libarchive handle and rewinds the stream.
	bool Restart() {
		Close();
		Source.Stream->SetPosition(0);
		Arc = archive_read_new();
		if (!Arc) return false;
		support_formats(Arc);
		archive_read_set_callback_data(Arc, &Source);
		archive_read_set_read_callback(Arc, read_callback);
		archive_read_set_seek_callback(Arc, seek_callback);
		if (archive_read_open1(Arc) != ARCHIVE_OK) {
			Close();
			return false;
		}
		NextIndex = 0;
		return true;
	}

	void Close() {
		if (Arc) archive_read_free(Arc);
		Arc = nullptr;
		NextIndex = 0;
	}
};

/// `signature` is what the format starts with, or null to accept any.
tTVPArchive *OpenWithLibarchive(const ttstr &name, tTJSBinaryStream *stream, bool normalizeFileName,
	const uint8_t *signature, size_t signature_len) {
	if (signature && signature_len) {
		tjs_uint64 pos = stream->GetPosition();
		std::vector<uint8_t> head(signature_len);
		tjs_uint read = stream->Read(head.data(), (tjs_uint)signature_len);
		stream->SetPosition(pos);
		if (read != signature_len || std::memcmp(head.data(), signature, signature_len) != 0) {
			return nullptr;
		}
	}
	EnsureUtf8Locale();
	LibArchiveArchive *arc = new LibArchiveArchive(name, stream);
	if (!arc->Open(normalizeFileName)) {
		delete arc;
		return nullptr;
	}
	return arc;
}

} // namespace

// ── what StorageImpl.cpp's archive table calls ─────────────────────────────

tTVPArchive *TVPOpen7ZArchive(const ttstr &name, tTJSBinaryStream *st, bool normalizeFileName) {
	static const uint8_t k7zSignature[] = {0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c};
	return OpenWithLibarchive(name, st, normalizeFileName, k7zSignature, sizeof(k7zSignature));
}

tTVPArchive *TVPOpenZIPArchive(const ttstr &name, tTJSBinaryStream *st, bool normalizeFileName) {
	static const uint8_t kZipSignature[] = {0x50, 0x4b, 0x03, 0x04};
	return OpenWithLibarchive(name, st, normalizeFileName, kZipSignature, sizeof(kZipSignature));
}
