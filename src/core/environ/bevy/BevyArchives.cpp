// bevy port: 7z / ZIP archive support.
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

class BevyArchive : public tTVPArchive {
	struct Entry {
		ttstr Name;
		tjs_uint Index; // position in the archive's own entry list
	};

	StreamSource Source;
	KrkrArchive *Arc = nullptr;
	std::vector<Entry> Entries;
	bool OwnsStream = false;

public:
	BevyArchive(const ttstr &name, tTJSBinaryStream *stream) : tTVPArchive(name) {
		Source.Stream = stream;
	}
	~BevyArchive() override {
		Close();
		if (OwnsStream) delete Source.Stream;
	}

	bool Open(int kind, bool normalizeFileName) {
		KrkrArchiveReader reader;
		reader.userdata = &Source;
		reader.read = read_callback;
		reader.seek = seek_callback;
		reader.size = (long long)Source.Stream->GetSize();
		Arc = krkr_archive_open(&reader, kind);
		if (!Arc) return false;

		tjs_uint count = (tjs_uint)krkr_archive_count(Arc);
		Entries.reserve(count);
		std::vector<char> name;
		for (tjs_uint i = 0; i < count; ++i) {
			size_t len = krkr_archive_name(Arc, i, nullptr, 0);
			if (len == (size_t)-1) continue;
			name.resize(len + 1);
			krkr_archive_name(Arc, i, name.data(), name.size());
			ttstr storageName = PathFromUtf8(name.data(), (tjs_uint)len);
			if (normalizeFileName) NormalizeInArchiveStorageName(storageName);
			Entries.push_back(Entry{storageName, i});
		}
		if (normalizeFileName) {
			std::stable_sort(Entries.begin(), Entries.end(),
				[](const Entry &a, const Entry &b) { return a.Name < b.Name; });
		}
		if (Entries.empty()) {
			Close();
			return false;
		}
		OwnsStream = true;
		return true;
	}

	tjs_uint GetCount() override { return (tjs_uint)Entries.size(); }
	ttstr GetName(tjs_uint idx) override { return Entries[idx].Name; }

	tTJSBinaryStream *CreateStreamByIndex(tjs_uint idx) override {
		if (!Arc || idx >= Entries.size()) return nullptr;
		tjs_uint index = Entries[idx].Index;
		size_t size = krkr_archive_size(Arc, index);
		if (size == (size_t)-1) return nullptr;
		std::unique_ptr<tTVPMemoryStream> stream(new tTVPMemoryStream());
		if (size > 0) {
			stream->SetSize((tjs_uint)size);
			size_t done = krkr_archive_read(Arc, index, stream->GetInternalBuffer(), size);
			if (done != size) return nullptr;
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

tTVPArchive *OpenWithShim(const ttstr &name, tTJSBinaryStream *stream, bool normalizeFileName,
	int kind, const uint8_t *signature, size_t signature_len) {
	tjs_uint64 pos = stream->GetPosition();
	std::vector<uint8_t> head(signature_len);
	tjs_uint read = stream->Read(head.data(), (tjs_uint)signature_len);
	stream->SetPosition(pos);
	if (read != signature_len || std::memcmp(head.data(), signature, signature_len) != 0) {
		return nullptr;
	}
	BevyArchive *arc = new BevyArchive(name, stream);
	if (!arc->Open(kind, normalizeFileName)) {
		delete arc;
		return nullptr;
	}
	return arc;
}

} // namespace

tTVPArchive *TVPOpen7ZArchive(const ttstr &name, tTJSBinaryStream *st, bool normalizeFileName) {
	static const uint8_t k7zSignature[] = {0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c};
	return OpenWithShim(name, st, normalizeFileName, KRKR_ARCHIVE_7Z, k7zSignature, sizeof(k7zSignature));
}

tTVPArchive *TVPOpenZIPArchive(const ttstr &name, tTJSBinaryStream *st, bool normalizeFileName) {
	static const uint8_t kZipSignature[] = {0x50, 0x4b, 0x03, 0x04};
	return OpenWithShim(name, st, normalizeFileName, KRKR_ARCHIVE_ZIP, kZipSignature, sizeof(kZipSignature));
}
