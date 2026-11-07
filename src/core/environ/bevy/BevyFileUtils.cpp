// bevy port: implementation of the cocos2d::FileUtils stand-in
// (compat/platform/CCFileUtils.h).
#include "platform/CCFileUtils.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

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
    for (const char *dir : {"/usr/share/fonts/droid", "/usr/share/fonts/TTF",
                            "/usr/share/fonts/truetype/droid", "/usr/share/fonts/noto-cjk",
                            "/usr/share/fonts/opentype/noto", "/usr/share/fonts"})
        _searchPaths.emplace_back(dir);
}

FileUtils *FileUtils::getInstance() {
    static FileUtils instance;
    return &instance;
}

void FileUtils::addSearchPath(const std::string &dir) {
    _searchPaths.insert(_searchPaths.begin(), dir);
}

bool FileUtils::isFileExist(const std::string &path) const {
    std::error_code ec;
    return !path.empty() && fs::is_regular_file(fullPathForFilename(path), ec);
}

std::string FileUtils::fullPathForFilename(const std::string &filename) const {
    std::error_code ec;
    if (filename.empty()) return {};
    if (fs::path(filename).is_absolute()) return filename;
    for (const auto &dir : _searchPaths) {
        fs::path p = fs::path(dir) / filename;
        if (fs::is_regular_file(p, ec)) return p.string();
    }
    if (fs::is_regular_file(filename, ec)) return fs::absolute(filename, ec).string();
    return {};
}

Data FileUtils::getDataFromFile(const std::string &filename) const {
    std::string path = fullPathForFilename(filename);
    if (path.empty()) return {};
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return Data(std::vector<unsigned char>(std::istreambuf_iterator<char>(in), {}));
}

std::string FileUtils::getStringFromFile(const std::string &filename) const {
    Data d = getDataFromFile(filename);
    return d.isNull() ? std::string() : std::string(reinterpret_cast<char *>(d.getBytes()), d.getSize());
}

std::string FileUtils::getWritablePath() const {
    fs::path base;
    if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        base = xdg;
    else if (const char *home = std::getenv("HOME"); home && *home)
        base = fs::path(home) / ".local" / "share";
    else
        base = fs::temp_directory_path();
    base /= "bevy_krkr";
    std::error_code ec;
    fs::create_directories(base, ec);
    return base.string() + "/";
}

} // namespace cocos2d
