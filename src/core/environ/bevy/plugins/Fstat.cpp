#include "krkr_storage.h"

#include <memory>

#include "tjsCommHead.h"
#include "ncbind/ncbind.hpp"
#include "StorageIntf.h"
#include "tjsDictionary.h"

#define NCB_MODULE_NAME TJS_W("fstat.dll")


class StoragesFstat {
    static bool existingPath(const tjs_char *name, std::string &path) {
        ttstr placed = TVPGetPlacedPath(name);
        if (placed.IsEmpty()) return false;
        ttstr local = TVPGetLocallyAccessibleName(placed);
        if (local.IsEmpty()) return false;
        path = local.AsStdString();
        return true;
    }

    static bool targetPath(const tjs_char *name, std::string &path) {
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
        std::string path;
        if (!existingPath(name, path)) return false;
        if (krkr_fs_remove(path.c_str(), 0) != 0) return false;
        TVPClearStorageCaches();
        return true;
    }

    static bool moveFile(const tjs_char *from, const tjs_char *to) {
        std::string source;
        std::string destination;
        if (!existingPath(from, source) || !targetPath(to, destination)) return false;
        if (krkr_fs_rename(source.c_str(), destination.c_str(), 0) != 0) return false;
        TVPClearStorageCaches();
        return true;
    }

    static bool copyFile(const tjs_char *from, const tjs_char *to, bool failIfExist) {
        std::string source;
        std::string destination;
        if (!existingPath(from, source) || !targetPath(to, destination)) return false;
        if (krkr_fs_copy(source.c_str(), destination.c_str(), !failIfExist) != 0) return false;
        TVPClearStorageCaches();
        return true;
    }

    static bool createDirectory(ttstr directory) {
        if (directory.GetLastChar() != TJS_W('/'))
            TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
        directory = TVPNormalizeStorageName(directory);
        TVPGetLocalName(directory);
        if (krkr_fs_create_dir(directory.AsStdString().c_str(), 0) != 0) return false;
        TVPClearStorageCaches();
        return true;
    }

    static bool removeDirectory(ttstr directory) {
        if (directory.GetLastChar() != TJS_W('/'))
            TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
        directory = TVPNormalizeStorageName(directory);
        TVPGetLocalName(directory);
        if (krkr_fs_remove(directory.AsStdString().c_str(), 1) != 0) return false;
        TVPClearStorageCaches();
        return true;
    }

    static bool isExistentDirectory(ttstr directory) {
        if (directory.GetLastChar() != TJS_W('/'))
            TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
        directory = TVPNormalizeStorageName(directory);
        ttstr local = TVPGetLocallyAccessibleName(directory);
        if (local.IsEmpty()) return false;
        KrkrFsMetadata meta;
        return krkr_fs_metadata(local.AsStdString().c_str(), &meta) == 0 && meta.kind == 2;
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
