// bevy port: minimal stand-in for cocos2d-x's FileUtils.
//
// File access and writable-directory selection are owned by krkr_storage.
#pragma once
#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <string>
#include <vector>

namespace cocos2d {

class Data {
public:
    Data() = default;
    explicit Data(std::vector<unsigned char> bytes) : _bytes(std::move(bytes)) {}
    unsigned char *getBytes() const { return _bytes.empty() ? nullptr : const_cast<unsigned char *>(_bytes.data()); }
    ssize_t getSize() const { return static_cast<ssize_t>(_bytes.size()); }
    bool isNull() const { return _bytes.empty(); }

private:
    std::vector<unsigned char> _bytes;
};

class FileUtils {
public:
    static FileUtils *getInstance();

    bool isFileExist(const std::string &path) const;
    std::string fullPathForFilename(const std::string &filename) const;
    Data getDataFromFile(const std::string &filename) const;
    std::string getStringFromFile(const std::string &filename) const;
    std::string getWritablePath() const;

    void addSearchPath(const std::string &dir);

private:
    FileUtils();
    std::vector<std::string> _searchPaths;
};

} // namespace cocos2d
