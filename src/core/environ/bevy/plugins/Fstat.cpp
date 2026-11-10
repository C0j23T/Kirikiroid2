#include <filesystem>
#include <system_error>
#include <memory>

#include "tjsCommHead.h"
#include "ncbind/ncbind.hpp"
#include "StorageIntf.h"
#include "tjsDictionary.h"

#define NCB_MODULE_NAME TJS_W("fstat.dll")

namespace fs = std::filesystem;

class StoragesFstat {
    static bool existingPath(const tjs_char *name, fs::path &path) {
        ttstr placed = TVPGetPlacedPath(name);
        if (placed.IsEmpty()) return false;
        ttstr local = TVPGetLocallyAccessibleName(placed);
        if (local.IsEmpty()) return false;
        path = local.AsStdString();
        return true;
    }

    static bool targetPath(const tjs_char *name, fs::path &path) {
        ttstr normalized = TVPNormalizeStorageName(name);
        ttstr local = TVPGetLocallyAccessibleName(normalized);
        if (local.IsEmpty()) return false;
        path = local.AsStdString();
        return true;
    }

public:
    static tjs_error TJS_INTF_METHOD fstat(tTJSVariant *result, tjs_int numparams,
                                            tTJSVariant **param, iTJSDispatch2 *) {
        if (numparams < 1) return TJS_E_BADPARAMCOUNT;
        std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(*param[0], TJS_BS_READ));
        if (!stream) return TJS_E_FAIL;
        if (result) {
            tTJSVariant size(static_cast<tjs_int64>(stream->GetSize()));
            iTJSDispatch2 *dict = TJSCreateDictionaryObject();
            if (!dict) return TJS_E_FAIL;
            dict->PropSet(TJS_MEMBERENSURE, TJS_W("size"), NULL, &size, dict);
            *result = tTJSVariant(dict, dict);
            dict->Release();
        }
        return TJS_S_OK;
    }

    static bool deleteFile(const tjs_char *name) {
        fs::path path;
        if (!existingPath(name, path)) return false;
        std::error_code error;
        if (!fs::is_regular_file(path, error) || error || !fs::remove(path, error) || error)
            return false;
        TVPClearStorageCaches();
        return true;
    }

    static bool moveFile(const tjs_char *from, const tjs_char *to) {
        fs::path source;
        fs::path destination;
        if (!existingPath(from, source) || !targetPath(to, destination)) return false;
        std::error_code error;
        if (fs::exists(destination, error) || error) return false;
        fs::rename(source, destination, error);
        if (error) return false;
        TVPClearStorageCaches();
        return true;
    }

    static bool copyFile(const tjs_char *from, const tjs_char *to, bool failIfExist) {
        fs::path source;
        fs::path destination;
        if (!existingPath(from, source) || !targetPath(to, destination)) return false;
        std::error_code error;
        const auto options = failIfExist ? fs::copy_options::none
                                         : fs::copy_options::overwrite_existing;
        if (!fs::copy_file(source, destination, options, error) || error) return false;
        TVPClearStorageCaches();
        return true;
    }

    static bool createDirectory(ttstr directory) {
        if (directory.GetLastChar() != TJS_W('/'))
            TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
        directory = TVPNormalizeStorageName(directory);
        TVPGetLocalName(directory);
        std::error_code error;
        if (!fs::create_directory(directory.AsStdString(), error) || error) return false;
        TVPClearStorageCaches();
        return true;
    }

    static bool removeDirectory(ttstr directory) {
        if (directory.GetLastChar() != TJS_W('/'))
            TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
        directory = TVPNormalizeStorageName(directory);
        TVPGetLocalName(directory);
        std::error_code error;
        if (!fs::remove(directory.AsStdString(), error) || error) return false;
        TVPClearStorageCaches();
        return true;
    }

    static bool isExistentDirectory(ttstr directory) {
        if (directory.GetLastChar() != TJS_W('/'))
            TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
        directory = TVPNormalizeStorageName(directory);
        ttstr local = TVPGetLocallyAccessibleName(directory);
        if (local.IsEmpty()) return false;
        std::error_code error;
        return fs::is_directory(local.AsStdString(), error) && !error;
    }
};

NCB_ATTACH_CLASS(StoragesFstat, Storages) {
    RawCallback("fstat", &StoragesFstat::fstat, TJS_STATICMEMBER);
    NCB_METHOD(deleteFile);
    NCB_METHOD(moveFile);
    NCB_METHOD(copyFile);
    NCB_METHOD(createDirectory);
    NCB_METHOD(removeDirectory);
    NCB_METHOD(isExistentDirectory);
}
