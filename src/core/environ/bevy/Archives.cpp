// 7z / ZIP archives, parsed by krkr_shim (crates/krkr_shim/src/archives.rs).
#include "tjsCommHead.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "CharacterSet.h"
#include "StorageIntf.h"
#include "UtilStreams.h"

#include "krkr_shim.h"

namespace {

struct StreamSource {
	tTJSBinaryStream *Stream;
};

long long read_callback(void *client, void *buf, long long len) {
	StreamSource *src = static_cast<StreamSource *>(client);
	return (long long)src->Stream->Read(buf, (tjs_uint)len);
}

long long seek_callback(void *client, long long offset, int whence) {
	StreamSource *src = static_cast<StreamSource *>(client);
	tjs_int tjs_whence = TJS_BS_SEEK_SET;
	switch (whence) {
	case SEEK_CUR: tjs_whence = TJS_BS_SEEK_CUR; break;
	case SEEK_END: tjs_whence = TJS_BS_SEEK_END; break;
	default: break;
	}
	return (long long)src->Stream->Seek((tjs_int64)offset, tjs_whence);
}

ttstr PathFromUtf8(const char *path, tjs_uint len) {
	tjs_int chars = TVPUtf8ToWideCharString(path, len, nullptr);
	ttstr name;
	TVPUtf8ToWideCharString(path, len, name.AllocBuffer(chars));
	name.FixLen();
	return name;
}

struct Entry {
	ttstr Name;
	size_t Index; // position in the archive's own entry list
	size_t Size;
};

void collect_entry(void *userdata, size_t index, const char *name, size_t size) {
	auto *entries = static_cast<std::vector<Entry> *>(userdata);
	entries->push_back(Entry{PathFromUtf8(name, (tjs_uint)strlen(name)), index, size});
}

class Archive : public tTVPArchive {
	StreamSource Source;
	KrkrArchive *Arc = nullptr;
	std::vector<Entry> Entries;
	bool OwnsStream = false;

public:
	Archive(const ttstr &name, tTJSBinaryStream *stream) : tTVPArchive(name) {
		Source.Stream = stream;
	}
	~Archive() override {
		Close();
		if (OwnsStream) delete Source.Stream;
	}

	bool Open(int kind, bool normalizeFileName) {
		KrkrStream stream;
		stream.userdata = &Source;
		stream.read = read_callback;
		stream.seek = seek_callback;
		stream.size = (long long)Source.Stream->GetSize();
		KrkrArchiveEntries entries;
		entries.userdata = &Entries;
		entries.entry = collect_entry;
		Arc = krkr_archive_open(&stream, kind, &entries);
		if (!Arc) return false;
		if (normalizeFileName) {
			for (Entry &entry : Entries) NormalizeInArchiveStorageName(entry.Name);
			std::stable_sort(Entries.begin(), Entries.end(),
				[](const Entry &a, const Entry &b) { return a.Name < b.Name; });
		}
		OwnsStream = true;
		return true;
	}

	tjs_uint GetCount() override { return (tjs_uint)Entries.size(); }
	ttstr GetName(tjs_uint idx) override { return Entries[idx].Name; }

	tTJSBinaryStream *CreateStreamByIndex(tjs_uint idx) override {
		if (!Arc || idx >= Entries.size()) return nullptr;
		const Entry &entry = Entries[idx];
		std::unique_ptr<tTVPMemoryStream> stream(new tTVPMemoryStream());
		if (entry.Size > 0) {
			stream->SetSize((tjs_uint)entry.Size);
			size_t done = krkr_archive_read(Arc, entry.Index, stream->GetInternalBuffer(), entry.Size);
			if (done != entry.Size) return nullptr;
		}
		return stream.release();
	}

private:
	void Close() {
		if (Arc) {
			krkr_archive_close(Arc);
			Arc = nullptr;
		}
	}
};

tTVPArchive *OpenArchive(const ttstr &name, tTJSBinaryStream *stream, bool normalizeFileName,
	int kind, const uint8_t *signature, size_t signature_len) {
	tjs_uint64 pos = stream->GetPosition();
	std::vector<uint8_t> head(signature_len);
	tjs_uint read = stream->Read(head.data(), (tjs_uint)signature_len);
	stream->SetPosition(pos);
	if (read != signature_len || std::memcmp(head.data(), signature, signature_len) != 0) {
		return nullptr;
	}
	Archive *arc = new Archive(name, stream);
	if (!arc->Open(kind, normalizeFileName)) {
		delete arc;
		return nullptr;
	}
	return arc;
}

} // namespace

tTVPArchive *TVPOpen7ZArchive(const ttstr &name, tTJSBinaryStream *st, bool normalizeFileName) {
	static const uint8_t k7zSignature[] = {0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c};
	return OpenArchive(name, st, normalizeFileName, KRKR_ARCHIVE_7Z, k7zSignature, sizeof(k7zSignature));
}

tTVPArchive *TVPOpenZIPArchive(const ttstr &name, tTJSBinaryStream *st, bool normalizeFileName) {
	static const uint8_t kZipSignature[] = {0x50, 0x4b, 0x03, 0x04};
	return OpenArchive(name, st, normalizeFileName, KRKR_ARCHIVE_ZIP, kZipSignature, sizeof(kZipSignature));
}
