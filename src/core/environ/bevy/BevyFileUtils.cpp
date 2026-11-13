// bevy port: implementation of the cocos2d::FileUtils stand-in
// (compat/platform/CCFileUtils.h).
#include "platform/CCFileUtils.h"

#include <cstdlib>
#include "krkr_storage.h"
#include <memory>
#include <iterator>
#include <sstream>



namespace cocos2d {

static void split_into(const char *list, std::vector<std::string> &out) {
    if (!list) return;
    std::stringstream ss(list);
    std::string item;
    while (std::getline(ss, item, ':'))
        if (!item.empty()) out.push_back(item);
}

FileUtils::FileUtils() {
    split_into(std::getenv("KRKR_RESOURCE_DIR"), _searchPaths);
}

FileUtils *FileUtils::getInstance() {
    static FileUtils instance;
    return &instance;
}

void FileUtils::addSearchPath(const std::string &dir) {
    _searchPaths.insert(_searchPaths.begin(), dir);
}

bool FileUtils::isFileExist(const std::string &path) const {
    KrkrFsMetadata meta;
    auto resolved = fullPathForFilename(path);
    return !resolved.empty() && krkr_fs_metadata(resolved.c_str(), &meta) == 0 && meta.kind == 1;
}

std::string FileUtils::fullPathForFilename(const std::string &filename) const {
    if (filename.empty()) return {};
    auto resolve = [](const std::string &p) {
        char *value = krkr_fs_resolve(p.c_str(), 0);
        std::string result = value ? value : "";
        krkr_fs_string_free(value);
        return result;
    };
    if (filename[0] == '/') return resolve(filename);
    KrkrFsMetadata meta;
    for (const auto &dir : _searchPaths) {
        std::string p = dir + "/" + filename;
        if (krkr_fs_metadata(p.c_str(), &meta) == 0 && meta.kind == 1) return resolve(p);
    }
    if (krkr_fs_metadata(filename.c_str(), &meta) == 0 && meta.kind == 1) return resolve(filename);
    return {};
}

Data FileUtils::getDataFromFile(const std::string &filename) const {
    auto path = fullPathForFilename(filename);
    std::unique_ptr<KrkrFile, decltype(&krkr_fs_close)> file(krkr_fs_open(path.c_str(), 0), krkr_fs_close);
    if (!file) return {};
    auto size = krkr_fs_size(file.get());
    if (size == UINT64_MAX || size > SIZE_MAX) return {};
    std::vector<unsigned char> bytes((size_t)size);
    size_t done = 0;
    while (done < bytes.size()) {
        size_t n = krkr_fs_read(file.get(), bytes.data() + done, bytes.size() - done);
        if (n == SIZE_MAX || !n) return {};
        done += n;
    }
    return Data(std::move(bytes));
}

std::string FileUtils::getStringFromFile(const std::string &filename) const {
    Data d = getDataFromFile(filename);
    return d.isNull() ? std::string() : std::string(reinterpret_cast<char *>(d.getBytes()), d.getSize());
}

std::string FileUtils::getWritablePath() const {
    char *value = krkr_fs_writable_root();
    std::string result = value ? value : "";
    krkr_fs_string_free(value);
    if (!result.empty() && result.back() != '/') result += '/';
    return result;
}

} // namespace cocos2d
