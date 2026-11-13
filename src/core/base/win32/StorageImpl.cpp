//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Universal Storage System
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

// #include <cderr.h>
// #include <objbase.h>

#include "MsgIntf.h"

#include "StorageImpl.h"
#include "WindowImpl.h"
#include "GraphicsLoaderImpl.h"
#include "SysInitIntf.h"
#include "DebugIntf.h"
#include "Random.h"
#include "XP3Archive.h"
//#include "SusieArchive.h"
#include "FileSelector.h"
#include "Random.h"

#include <time.h>

#include "Application.h"
#include "StringUtil.h"
#include "FilePathUtil.h"
#include "Platform.h"
#include "platform/CCPlatformConfig.h"
#include "krkr_storage.h"
#include <memory>
#include "TickCount.h"


#include "combase.h"


//---------------------------------------------------------------------------
// tTVPFileMedia
//---------------------------------------------------------------------------
class tTVPFileMedia : public iTVPStorageMedia
{
	tjs_uint RefCount;

public:
	tTVPFileMedia() { RefCount = 1; }
	~tTVPFileMedia() {;}

	void TJS_INTF_METHOD AddRef() { RefCount ++; }
	void TJS_INTF_METHOD Release()
	{
		if(RefCount == 1)
			delete this;
		else
			RefCount --;
	}

	void TJS_INTF_METHOD GetName(ttstr &name) { name = TJS_W("file"); }

	void TJS_INTF_METHOD NormalizeDomainName(ttstr &name);
	void TJS_INTF_METHOD NormalizePathName(ttstr &name);
	bool TJS_INTF_METHOD CheckExistentStorage(const ttstr &name);
	tTJSBinaryStream * TJS_INTF_METHOD Open(const ttstr & name, tjs_uint32 flags);
	void TJS_INTF_METHOD GetListAt(const ttstr &name, iTVPStorageLister *lister);
	void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr &name);

public:
	void TJS_INTF_METHOD GetLocalName(ttstr &name);
};
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPFileMedia::NormalizeDomainName(ttstr &name)
{
	// normalize domain name
	// make all characters small
	tjs_char *p = name.Independ();
	while(*p)
	{
		if(*p >= TJS_W('A') && *p <= TJS_W('Z'))
			*p += TJS_W('a') - TJS_W('A');
		p++;
	}
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPFileMedia::NormalizePathName(ttstr &name)
{
	// normalize path name
	// make all characters small
	tjs_char *p = name.Independ();
	while(*p)
	{
		if(*p >= TJS_W('A') && *p <= TJS_W('Z'))
			*p += TJS_W('a') - TJS_W('A');
		p++;
	}
}
//---------------------------------------------------------------------------
bool TJS_INTF_METHOD tTVPFileMedia::CheckExistentStorage(const ttstr &name)
{
	if(name.IsEmpty()) return false;

	ttstr _name(name);
	GetLocalName(_name);

	return TVPCheckExistentLocalFile(_name);
}
//---------------------------------------------------------------------------
tTJSBinaryStream * TJS_INTF_METHOD tTVPFileMedia::Open(const ttstr & name, tjs_uint32 flags)
{
	// open storage named "name".
	// currently only local/network(by OS) storage systems are supported.
	if(name.IsEmpty())
		TVPThrowExceptionMessage(TVPCannotOpenStorage, TJS_W("\"\""));

	ttstr origname = name;
	ttstr _name(name);
	GetLocalName(_name);

	return new tTVPLocalFileStream(origname, _name, flags);
}

void TVPGetLocalFileListAt(const ttstr &name, const std::function<void(const ttstr&, tTVPLocalFileInfo*)>& cb) {
    std::unique_ptr<KrkrDirectory, decltype(&krkr_fs_directory_close)> dir(
        krkr_fs_directory_open(name.AsStdString().c_str()), krkr_fs_directory_close);
    if (!dir) return;
    KrkrFsMetadata meta;
    for (size_t i = 0;; ++i) {
        const char *native = krkr_fs_directory_entry(dir.get(), i, &meta);
        if (!native) break;
        ttstr file(native);
        tjs_char *p = file.Independ();
        for (; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
        tTVPLocalFileInfo info;
        info.NativeName = native;
        info.Mode = meta.kind == 1 ? S_IFREG : meta.kind == 2 ? S_IFDIR : 0;
        info.Size = meta.size;
        info.AccessTime = meta.accessed;
        info.ModifyTime = meta.modified;
        info.CreationTime = meta.changed;
        cb(file, &info);
    }
}

void TVPListDir(const std::string &folder, std::function<void(const std::string&, int)> cb) {
    TVPGetLocalFileListAt(ttstr(folder), [&](const ttstr &, tTVPLocalFileInfo *info) {
        cb(info->NativeName, info->Mode);
    });
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPFileMedia::GetListAt(const ttstr &_name, iTVPStorageLister *lister)
{
	ttstr name(_name);
	GetLocalName(name);

	TVPGetLocalFileListAt(name, [lister](const ttstr &name, tTVPLocalFileInfo* s) {
		if (s->Mode & (S_IFREG)) {
			lister->Add(name);
		}
	});
}

void TJS_INTF_METHOD tTVPFileMedia::GetLocallyAccessibleName(ttstr &name)
{
    std::string native = name.AsStdString();
    if (native.rfind("./", 0) == 0) native.erase(0, 1);
    char *resolved = krkr_fs_resolve(native.c_str(), 1);
    if (!resolved) { name.Clear(); return; }
    name = ttstr(resolved);
    krkr_fs_string_free(resolved);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPFileMedia::GetLocalName(ttstr &name)
{
	ttstr tmp = name;
	GetLocallyAccessibleName(tmp);
	if(tmp.IsEmpty()) TVPThrowExceptionMessage(TVPCannotGetLocalName, name);
	name = tmp;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
iTVPStorageMedia * TVPCreateFileMedia()
{
	return new tTVPFileMedia;
}
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// TVPPreNormalizeStorageName
//---------------------------------------------------------------------------
void TVPPreNormalizeStorageName(ttstr &name)
{
	// if the name is an OS's native expression, change it according with the
	// TVP storage system naming rule.
	tjs_int namelen = name.GetLen();
	if(namelen == 0) return;
#ifdef WIN32
	if(namelen >= 2)
	{
		if((name[0] >= TJS_W('a') && name[0]<=TJS_W('z') ||
			name[0] >= TJS_W('A') && name[0]<=TJS_W('Z') ) &&
			name[1] == TJS_W(':'))
		{
			// Windows drive:path expression
			ttstr newname(TJS_W("file://./"));
			newname += name[0];
			newname += (name.c_str()+2);
            name = newname;
			return;
		}
	}

	if(namelen>=3)
	{
		if(name[0] == TJS_W('\\') && name[1] == TJS_W('\\') ||
			name[0] == TJS_W('/') && name[1] == TJS_W('/'))
		{
			// unc expression
			name = ttstr(TJS_W("file:")) + name;
			return;
		}
	}
#else // posix
    if(namelen>=1) {
        if(name[0] == TJS_W('/')) {
            name = ttstr(TJS_W("file://.")) + name;
            return;
        }
    }
#endif
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// TVPGetTemporaryName
//---------------------------------------------------------------------------
static tjs_int TVPTempUniqueNum = 0;
static tTJSCriticalSection TVPTempUniqueNumCS;
static ttstr TVPTempPath;
bool TVPTempPathInit = false;
static tjs_int TVPProcessID;
ttstr TVPGetTemporaryName()
{
	static tjs_int TVPTempUniqueNum = (tjs_int)TVPGetRoughTickCount32();
	tjs_int num = TVPTempUniqueNum++;
	ttstr TVPTempPath = TVPGetAppPath();
#if 0
	tjs_int num;

	{
		tTJSCriticalSectionHolder holder(TVPTempUniqueNumCS);

		if(!TVPTempPathInit)
		{
			wchar_t tmp[MAX_PATH+1];
			::GetTempPath(MAX_PATH, tmp);
			TVPTempPath = tmp;

			if(TVPTempPath.GetLastChar() != TJS_W('\\')) TVPTempPath += TJS_W("\\");
			TVPProcessID = (tjs_int) GetCurrentProcessId();
			TVPTempUniqueNum = (tjs_int) GetTickCount();
			TVPTempPathInit = true;
		}
		num = TVPTempUniqueNum ++;
	}
#endif
	unsigned char buf[16];
	TVPGetRandomBits128(buf);
	tjs_char random[128];
	TJS_snprintf(random, sizeof(random)/sizeof(tjs_char), TJS_W("%02x%02x%02x%02x%02x%02x"),
		buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5]);

	return TVPTempPath + TJS_W("krkr_") + ttstr(random) +
		TJS_W("_") + ttstr(num) + TJS_W("_") + ttstr(TVPProcessID);
}
//---------------------------------------------------------------------------






//---------------------------------------------------------------------------
// TVPRemoveFile
//---------------------------------------------------------------------------
bool TVPRemoveFile(const ttstr &name)
{
    tTJSNarrowStringHolder holder(name.c_str());
    return krkr_fs_remove(holder, 0) == 0;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TVPRemoveFolder
//---------------------------------------------------------------------------
bool TVPRemoveFolder(const ttstr &name)
{
    tTJSNarrowStringHolder holder(name.c_str());
    return krkr_fs_remove(holder, 1) == 0;
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// TVPGetAppPath
//---------------------------------------------------------------------------
ttstr TVPGetAppPath()
{
#if 0
	static ttstr exepath(TVPExtractStoragePath(TVPNormalizeStorageName(ExePath())));
	return exepath;
#endif
	static ttstr apppath(TVPExtractStoragePath(TVPProjectDir));
	return apppath;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPOpenStream
//---------------------------------------------------------------------------
tTJSBinaryStream * TVPOpenStream(const ttstr & _name, tjs_uint32 flags)
{
	// open storage named "name".
	// currently only local/network(by OS) storage systems are supported.
	if(_name.IsEmpty())
		TVPThrowExceptionMessage(TVPCannotOpenStorage, TJS_W("\"\""));

	ttstr origname = _name;
	ttstr name(_name);
	TVPGetLocalName(name);

	return new tTVPLocalFileStream(origname, name, flags);
}
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// TVPCheckExistantLocalFile
//---------------------------------------------------------------------------
bool TVPCheckExistentLocalFile(const ttstr &name)
{
#if 0
	DWORD attrib = ::GetFileAttributes(name.c_str());
	if(attrib == 0xffffffff || (attrib & FILE_ATTRIBUTE_DIRECTORY))
		return false; // not a file
	else
		return true; // a file
#endif
	tTVP_stat s;
    if(!TVP_stat(name.c_str(), s)) {
        return false; // not exist
    }
    return s.st_mode & S_IFREG;
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// TVPCheckExistantLocalFolder
//---------------------------------------------------------------------------
bool TVPCheckExistentLocalFolder(const ttstr &name)
{
#if 0
	DWORD attrib = GetFileAttributes(name.c_str());
	if(attrib != 0xffffffff && (attrib & FILE_ATTRIBUTE_DIRECTORY))
		return true; // a folder
	else
		return false; // not a folder
#endif
	tTVP_stat s;
	if (!TVP_stat(name.c_str(), s)) {
        return false; // not exist
    }

    return s.st_mode & S_IFDIR;
}
//---------------------------------------------------------------------------



tTVPArchive * TVPOpenZIPArchive(const ttstr & name, tTJSBinaryStream *st, bool normalizeFileName);
tTVPArchive * TVPOpen7ZArchive(const ttstr & name, tTJSBinaryStream *st, bool normalizeFileName);
tTVPArchive * TVPOpenTARArchive(const ttstr & name, tTJSBinaryStream *st, bool normalizeFileName);
static tTVPArchive*(*ArchiveCreators[])(const ttstr & name, tTJSBinaryStream *st, bool normalizeFileName) = {
	TVPOpenZIPArchive,
	TVPOpen7ZArchive,
	TVPOpenTARArchive,
	tTVPXP3Archive::Create
};

//---------------------------------------------------------------------------
// TVPOpenArchive
//---------------------------------------------------------------------------
tTVPArchive * TVPOpenArchive(const ttstr & name, bool normalizeFileName)
{
#if 0
	tTVPArchive * archive = TVPOpenSusieArchive(name); // in SusieArchive.h
	if(!archive) return new tTVPXP3Archive(name); else return archive;
#endif
	tTJSBinaryStream *st = TVPCreateStream(name);
	if (!st) return nullptr;
	for (int i = 0; i < sizeof(ArchiveCreators) / sizeof(ArchiveCreators[0]); ++i) {
		tTVPArchive*(*creator)(const ttstr &, tTJSBinaryStream*, bool) = ArchiveCreators[i];
		tTVPArchive * archive = creator(name, st, normalizeFileName);
		if (archive) return archive;
		st->SetPosition(0);
	}
	delete st;
	return nullptr;
}
//---------------------------------------------------------------------------
int TVPCheckArchive(const ttstr &localname)
{
	tTVPArchive *arc = nullptr;
	int validArchive = 2; // archive but no startup.tjs
	try {
		arc = TVPOpenArchive(TVPNormalizeStorageName(localname), false);
		if (arc) {
			tjs_uint count = arc->GetCount();
			ttstr str_startup_tjs = TJS_W("startup.tjs");
			//ttstr str_sys_init_tjs = TJS_W("system/initialize.tjs");
			for (int i = 0; i < count; ++i) {
				ttstr name = arc->GetName(i);
				if (name.length() == str_startup_tjs.length()) {
					arc->NormalizeInArchiveStorageName(name);
					if (name == str_startup_tjs) {
						validArchive = 1;
						break;
					}
				}
// 				else if (name.length() == str_sys_init_tjs.length()) {
// 					arc->NormalizeInArchiveStorageName(name);
// 					if (name == str_sys_init_tjs) {
// 						validArchive = true;
// 						break;
// 					}
// 				}
			}
		}
	} catch (eTJSError e) {
		//arc = nullptr;
	}
	if (arc) {
		delete arc;
		return validArchive;
	}
	return 0; // not archive
}




//---------------------------------------------------------------------------
// TVPLocalExtrectFilePath
//---------------------------------------------------------------------------
ttstr TVPLocalExtractFilePath(const ttstr & name)
{
	// this extracts given name's path under local filename rule
	const tjs_char *p = name.c_str();
	tjs_int i = name.GetLen() -1;
	for(; i >= 0; i--)
	{
		if(p[i] == TJS_W(':') || p[i] == TJS_W('/') ||
			p[i] == TJS_W('\\'))
			break;
	}
	return ttstr(p, i + 1);
}
//---------------------------------------------------------------------------



#if 0
//---------------------------------------------------------------------------
// TVPCreateFolders
//---------------------------------------------------------------------------
static bool _TVPCreateFolders(const ttstr &folder)
{
	// create directories along with "folder"
	if(folder.IsEmpty()) return true;

	if(TVPCheckExistentLocalFolder(folder))
		return true; // already created

	const tjs_char *p = folder.c_str();
	tjs_int i = folder.GetLen() - 1;

	if(p[i] == TJS_W(':')) return true;

	while(i >= 0 && (p[i] == TJS_W('/') || p[i] == TJS_W('\\'))) i--;

	if(i >= 0 && p[i] == TJS_W(':')) return true;

	for(; i >= 0; i--)
	{
		if(p[i] == TJS_W(':') || p[i] == TJS_W('/') ||
			p[i] == TJS_W('\\'))
			break;
	}

	ttstr parent(p, i + 1);

	if(!_TVPCreateFolders(parent)) return false;

	BOOL res = ::CreateDirectory(folder.c_str(), NULL);
	return 0!=res;
}

bool TVPCreateFolders(const ttstr &folder)
{
	if(folder.IsEmpty()) return true;

	const tjs_char *p = folder.c_str();
	tjs_int i = folder.GetLen() - 1;

	if(p[i] == TJS_W(':')) return true;

	if(p[i] == TJS_W('/') || p[i] == TJS_W('\\')) i--;

	return _TVPCreateFolders(ttstr(p, i+1));
}
//---------------------------------------------------------------------------
#endif




//---------------------------------------------------------------------------
// tTVPLocalFileStream
//---------------------------------------------------------------------------
tTVPLocalFileStream::tTVPLocalFileStream(const ttstr &origname,
    const ttstr &localname, tjs_uint32 flag) : Handle(nullptr), FileName(localname)
{
    tjs_uint32 access = flag & TJS_BS_ACCESS_MASK;
    if (access == TJS_BS_WRITE) {
        ttstr parent = TVPLocalExtractFilePath(localname);
        if (!TVPCreateFolders(parent)) TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
        MemBuffer = new tTVPMemoryStream();
        return;
    }
    unsigned mode = access == TJS_BS_READ ? 0 : access == TJS_BS_APPEND ? 2 : 3;
    Handle = krkr_fs_open(localname.AsStdString().c_str(), mode);
    if (!Handle) TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
}

bool TVPWriteDataToFile(const ttstr &filepath, const void *data, unsigned int len);
tTVPLocalFileStream::~tTVPLocalFileStream()
{
    std::unique_ptr<tTVPMemoryStream> memory(MemBuffer);
    krkr_fs_close(Handle);
    if (memory && !TVPWriteDataToFile(FileName, memory->GetInternalBuffer(), memory->GetSize()))
        TVPThrowExceptionMessage(TJS_W("File Writing Error: %1"), FileName);
}

tjs_uint64 TJS_INTF_METHOD tTVPLocalFileStream::Seek(tjs_int64 offset, tjs_int whence)
{
    if (MemBuffer) return MemBuffer->Seek(offset, whence);
    unsigned origin = whence == TJS_BS_SEEK_SET ? 0 : whence == TJS_BS_SEEK_CUR ? 1 : 2;
    auto result = krkr_fs_seek(Handle, offset, origin);
    if (result == UINT64_MAX) TVPThrowExceptionMessage(TVPReadError);
    return result;
}

tjs_uint TJS_INTF_METHOD tTVPLocalFileStream::Read(void *buffer, tjs_uint size)
{
    if (MemBuffer) return MemBuffer->Read(buffer, size);
    size_t result = krkr_fs_read(Handle, buffer, size);
    if (result == SIZE_MAX) TVPThrowExceptionMessage(TVPReadError);
    return result;
}

tjs_uint TJS_INTF_METHOD tTVPLocalFileStream::Write(const void *buffer, tjs_uint size)
{
    if (MemBuffer) return MemBuffer->Write(buffer, size);
    size_t result = krkr_fs_write(Handle, buffer, size);
    if (result == SIZE_MAX) TVPThrowExceptionMessage(TJS_W("File writing failed"));
    return result;
}

void TJS_INTF_METHOD tTVPLocalFileStream::SetEndOfStorage()
{
    if (MemBuffer) { MemBuffer->SetEndOfStorage(); return; }
    if (krkr_fs_set_len(Handle, Seek(0, TJS_BS_SEEK_CUR)) != 0)
        TVPThrowExceptionMessage(TJS_W("File truncation failed"));
}

tjs_uint64 TJS_INTF_METHOD tTVPLocalFileStream::GetSize()
{
    if (MemBuffer) return MemBuffer->GetSize();
    auto result = krkr_fs_size(Handle);
    if (result == UINT64_MAX) TVPThrowExceptionMessage(TVPReadError);
    return result;
}
//---------------------------------------------------------------------------

#ifdef TJS_SUPPORT_VCL
//---------------------------------------------------------------------------
// TTVPStreamAdapter
//---------------------------------------------------------------------------
__fastcall TTVPStreamAdapter::TTVPStreamAdapter(tTJSBinaryStream *ref)
{
	Stream = ref;
	Stream->SetPosition(0);
}
//---------------------------------------------------------------------------
__fastcall TTVPStreamAdapter::~TTVPStreamAdapter()
{
	delete Stream;
}
//---------------------------------------------------------------------------
int __fastcall TTVPStreamAdapter::Read(void *Buffer, int Count)
{
	return (int)Stream->Read(Buffer, Count);
}
//---------------------------------------------------------------------------
int __fastcall TTVPStreamAdapter::Seek(int Offset, WORD Origin)
{
	tjs_int whence;
	switch(Origin)
	{
	case soFromBeginning:	whence = TJS_BS_SEEK_SET;		break;
	case soFromCurrent:		whence = TJS_BS_SEEK_CUR;		break;
	case soFromEnd:			whence = TJS_BS_SEEK_END;		break;
	}

	return (int)Stream->Seek(Offset, whence);
}
//---------------------------------------------------------------------------
int __fastcall TTVPStreamAdapter::Write(const void *Buffer,int Count)
{
	return (int)Stream->Write(Buffer, Count);
}
//---------------------------------------------------------------------------
#endif



#if 1
//---------------------------------------------------------------------------
// tTVPIStreamAdapter
//---------------------------------------------------------------------------
/*
this class provides COM's IStream adapter for tTJSBinaryStream
*/
class tTVPIStreamAdapter : public IStream
{
private:
	tTJSBinaryStream *Stream;
	ULONG RefCount;

public:
	tTVPIStreamAdapter(tTJSBinaryStream *ref);
	/*
	the stream passed by argument here is freed by this instance'
	destruction.
	*/

	~tTVPIStreamAdapter();


	// IUnknown
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
		void **ppvObject);
	ULONG STDMETHODCALLTYPE AddRef(void);
	ULONG STDMETHODCALLTYPE Release(void);

	// ISequentialStream
	HRESULT STDMETHODCALLTYPE Read(void *pv, ULONG cb, ULONG *pcbRead);
	HRESULT STDMETHODCALLTYPE Write(const void *pv, ULONG cb,
		ULONG *pcbWritten);

	// IStream
	HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove,
		DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition);
	HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize);
	HRESULT STDMETHODCALLTYPE CopyTo(IStream *pstm, ULARGE_INTEGER cb,
		ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten);
	HRESULT STDMETHODCALLTYPE Commit(DWORD grfCommitFlags);
	HRESULT STDMETHODCALLTYPE Revert(void);
	HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER libOffset,
		ULARGE_INTEGER cb, DWORD dwLockType);
	HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER libOffset,
		ULARGE_INTEGER cb, DWORD dwLockType);
	HRESULT STDMETHODCALLTYPE Stat(STATSTG *pstatstg, DWORD grfStatFlag);
	HRESULT STDMETHODCALLTYPE Clone(IStream **ppstm);

	void ClearStream() {
		Stream = NULL;
	}
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPIStreamAdapter
//---------------------------------------------------------------------------
/*
	this class provides adapter for COM's IStream
*/
tTVPIStreamAdapter::tTVPIStreamAdapter(tTJSBinaryStream *ref)
{
	Stream = ref;
	RefCount = 1;
}
//---------------------------------------------------------------------------
tTVPIStreamAdapter::~tTVPIStreamAdapter()
{
	delete Stream;
}
//---------------------------------------------------------------------------
extern "C" const IID IID_IUnknown;
extern "C" const IID IID_IStream;
extern "C" const IID IID_ISequentialStream;
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::QueryInterface(REFIID riid,
		void **ppvObject)
{
	if(!ppvObject) return E_INVALIDARG;

	*ppvObject=NULL;
	if(!memcmp(&riid,&IID_IUnknown,16))
		*ppvObject=(IUnknown*)this;
	else if(!memcmp(&riid,&IID_ISequentialStream,16))
		*ppvObject=(ISequentialStream*)this;
	else if(!memcmp(&riid,&IID_IStream,16))
		*ppvObject=(IStream*)this;

	if(*ppvObject)
	{
		AddRef();
		return S_OK;
	}
	return E_NOINTERFACE;
}
//---------------------------------------------------------------------------
ULONG STDMETHODCALLTYPE tTVPIStreamAdapter::AddRef(void)
{
	return ++ RefCount;
}
//---------------------------------------------------------------------------
ULONG STDMETHODCALLTYPE tTVPIStreamAdapter::Release(void)
{
	if(RefCount == 1)
	{
		delete this;
		return 0;
	}
	else
	{
		return --RefCount;
	}
}
//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Read(void *pv, ULONG cb, ULONG *pcbRead)
{
	try
	{
		ULONG read;
		read = Stream->Read(pv, cb);
		if(pcbRead) *pcbRead = read;
	}
	catch(...)
	{
		return E_FAIL;
	}
	return S_OK;
}
//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Write(const void *pv, ULONG cb,
		ULONG *pcbWritten)
{
	try
	{
		ULONG written;
		written = Stream->Write(pv, cb);
		if(pcbWritten) *pcbWritten = written;
	}
	catch(...)
	{
		return E_FAIL;
	}
	return S_OK;
}
//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Seek(LARGE_INTEGER dlibMove,
	DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition)
{
	try
	{
		switch(dwOrigin)
		{
		case STREAM_SEEK_SET:
			if(plibNewPosition)
				(*plibNewPosition).QuadPart =
					Stream->Seek(dlibMove.QuadPart, TJS_BS_SEEK_SET);
			else
					Stream->Seek(dlibMove.QuadPart, TJS_BS_SEEK_SET);
			break;
		case STREAM_SEEK_CUR:
			if(plibNewPosition)
				(*plibNewPosition).QuadPart =
					Stream->Seek(dlibMove.QuadPart, TJS_BS_SEEK_CUR);
			else
					Stream->Seek(dlibMove.QuadPart, TJS_BS_SEEK_CUR);
			break;
		case STREAM_SEEK_END:
			if(plibNewPosition)
				(*plibNewPosition).QuadPart =
					Stream->Seek(dlibMove.QuadPart, TJS_BS_SEEK_END);
			else
					Stream->Seek(dlibMove.QuadPart, TJS_BS_SEEK_END);
			break;
		default:
			return E_FAIL;
		}
	}
	catch(...)
	{
		return E_FAIL;
	}
	return S_OK;
}
//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::SetSize(ULARGE_INTEGER libNewSize)
{
	return E_NOTIMPL;
}
//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::CopyTo(IStream *pstm, ULARGE_INTEGER cb,
	ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten)
{
	return E_NOTIMPL;
}
//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Commit(DWORD grfCommitFlags)
{
	return E_NOTIMPL;
}
//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Revert(void)
{
	return E_NOTIMPL;
}
//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::LockRegion(ULARGE_INTEGER libOffset,
	ULARGE_INTEGER cb, DWORD dwLockType)
{
	return E_NOTIMPL;
}
//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::UnlockRegion(ULARGE_INTEGER libOffset,
	ULARGE_INTEGER cb, DWORD dwLockType)
{
	return E_NOTIMPL;
}
//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Stat(STATSTG *pstatstg, DWORD grfStatFlag)
{
	// This method imcompletely fills the target structure, because some
	// informations like access mode or stream name are already lost
	// at this point.

	if(pstatstg)
	{
		memset(pstatstg, 0, sizeof(*pstatstg));

		// pwcsName
		// this object's storage pointer does not have a name ...
		if(!(grfStatFlag &  STATFLAG_NONAME))
		{
			// anyway returns an empty string
			pstatstg->pwcsName = (LPOLESTR)TJS_W("");
		}

		// type
		pstatstg->type = STGTY_STREAM;

		// cbSize
		pstatstg->cbSize.QuadPart = Stream->GetSize();

		// mtime, ctime, atime unknown

		// grfMode unknown
		pstatstg->grfMode = STGM_DIRECT | STGM_READWRITE | STGM_SHARE_DENY_WRITE ;
			// Note that this method always returns flags above, regardless of the
			// actual mode.
			// In the return value, the stream is to be indicated that the
			// stream can be written, but of cource, the Write method will fail
			// if the stream is read-only.

		// grfLockSuppoted
		pstatstg->grfLocksSupported = 0;

		// grfStatBits unknown
	}
	else
	{
		return E_INVALIDARG;
	}

	return S_OK;
}
//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Clone(IStream **ppstm)
{
	return E_NOTIMPL;
}
//---------------------------------------------------------------------------





#endif

IStream * TVPCreateIStream(tTJSBinaryStream *s) { return new tTVPIStreamAdapter(s); }
//---------------------------------------------------------------------------
// IStream creator
//---------------------------------------------------------------------------
IStream * TVPCreateIStream(const ttstr &name, tjs_uint32 flags)
{
	// convert tTJSBinaryStream to IStream thru TStream

	tTJSBinaryStream *stream0 = NULL;
	try
	{
		stream0 = TVPCreateStream(name, flags);
	}
	catch(...)
	{
		if(stream0) delete stream0;
		return NULL;
	}

	IStream *istream = new tTVPIStreamAdapter(stream0);

	return istream;
}
//---------------------------------------------------------------------------




#if 0
//---------------------------------------------------------------------------
// tTVPBinaryStreamAdapter
//---------------------------------------------------------------------------
tTVPBinaryStreamAdapter::tTVPBinaryStreamAdapter(IStream *ref)
{
	Stream = ref;
	Stream->AddRef();
}
//---------------------------------------------------------------------------
tTVPBinaryStreamAdapter::~tTVPBinaryStreamAdapter()
{
	Stream->Release();
}
//---------------------------------------------------------------------------
tjs_uint64 TJS_INTF_METHOD tTVPBinaryStreamAdapter::Seek(tjs_int64 offset, tjs_int whence)
{
	DWORD origin;

	switch(whence)
	{
	case TJS_BS_SEEK_SET:			origin = STREAM_SEEK_SET;		break;
	case TJS_BS_SEEK_CUR:			origin = STREAM_SEEK_CUR;		break;
	case TJS_BS_SEEK_END:			origin = STREAM_SEEK_END;		break;
	default:						origin = STREAM_SEEK_SET;		break;
	}

	LARGE_INTEGER ofs;
	ULARGE_INTEGER newpos;

	ofs.QuadPart = 0;
	HRESULT hr = Stream->Seek(ofs, STREAM_SEEK_CUR, &newpos);
	bool orgpossaved;
	LARGE_INTEGER orgpos;
	if(FAILED(hr))
	{
		orgpossaved = false;
	}
	else
	{
		orgpossaved = true;
		*(LARGE_INTEGER*)&orgpos = *(LARGE_INTEGER*)&newpos;
	}

	ofs.QuadPart = offset;

	hr = Stream->Seek(ofs, origin, &newpos);
	if(FAILED(hr))
	{
		if(orgpossaved)
		{
			Stream->Seek(orgpos, STREAM_SEEK_SET, &newpos);
		}
		else
		{
			TVPThrowExceptionMessage(TVPSeekError);
		}
	}

	return newpos.QuadPart;
}
//---------------------------------------------------------------------------
tjs_uint TJS_INTF_METHOD tTVPBinaryStreamAdapter::Read(void *buffer, tjs_uint read_size)
{
	ULONG cb = read_size;
	ULONG read;
	HRESULT hr = Stream->Read(buffer, cb, &read);
	if(FAILED(hr)) read = 0;
	return read;
}
//---------------------------------------------------------------------------
tjs_uint TJS_INTF_METHOD tTVPBinaryStreamAdapter::Write(const void *buffer, tjs_uint write_size)
{
	ULONG cb = write_size;
	ULONG written;
	HRESULT hr = Stream->Write(buffer, cb, &written);
	if(FAILED(hr)) written = 0;
	return written;
}
//---------------------------------------------------------------------------
tjs_uint64 TJS_INTF_METHOD tTVPBinaryStreamAdapter::GetSize()
{
	HRESULT hr;
	STATSTG stg;

	hr = Stream->Stat(&stg, STATFLAG_NONAME);
	if(FAILED(hr))
	{
		return inherited::GetSize(); // use default routine
	}

	return stg.cbSize.QuadPart;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBinaryStreamAdapter::SetEndOfStorage()
{
	ULARGE_INTEGER pos;
	pos.QuadPart = GetPosition();
	HRESULT hr = Stream->SetSize(pos);
	if(FAILED(hr)) TVPThrowExceptionMessage(TVPTruncateError);
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPCreateBinaryStreamAdapter
//---------------------------------------------------------------------------
tTJSBinaryStream * TVPCreateBinaryStreamAdapter(IStream *refstream)
{
	return new  tTVPBinaryStreamAdapter(refstream);
}
//---------------------------------------------------------------------------
#endif








//---------------------------------------------------------------------------
// tTVPPluginHolder
//---------------------------------------------------------------------------
tTVPPluginHolder::tTVPPluginHolder(const ttstr &aname)
{
	LocalTempStorageHolder = NULL;

	// search in TVP storage system
	ttstr place(TVPGetPlacedPath(aname));
	if(!place.IsEmpty())
	{
		LocalTempStorageHolder = new tTVPLocalTempStorageHolder(place);
	}
	else
	{
		// not found in TVP storage system; search exepath, exepath\system, exepath\plugin
#if 0
		ttstr exepath =
			IncludeTrailingBackslash(ExtractFileDir(ExePath()));
		ttstr pname = exepath + aname;
		if(TVPCheckExistentLocalFile(pname))
		{
			LocalName = pname;
			return;
		}

		pname = exepath + TJS_W("system\\") + aname;
		if(TVPCheckExistentLocalFile(pname))
		{
			LocalName = pname;
			return;
		}

#ifdef TJS_64BIT_OS
		pname = exepath + TJS_W("plugin64\\") + aname;
#else
		pname = exepath + TJS_W("plugin\\") + aname;
#endif
		if(TVPCheckExistentLocalFile(pname))
		{
			LocalName = pname;
			return;
		}
#endif
	}
}
//---------------------------------------------------------------------------
tTVPPluginHolder::~tTVPPluginHolder()
{
	if(LocalTempStorageHolder)
	{
		delete LocalTempStorageHolder;
	}
}
//---------------------------------------------------------------------------
const ttstr & tTVPPluginHolder::GetLocalName() const
{
	if(LocalTempStorageHolder) return LocalTempStorageHolder->GetLocalName();
	return LocalName;
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// TVPSearchCD
//---------------------------------------------------------------------------
ttstr TVPSearchCD(const ttstr & name)
{
	// search CD which has specified volume label name.
	// return drive letter ( such as 'A' or 'B' )
	// return empty string if not found.
#if 0
	std::wstring narrow_name = name.AsStdString();

	wchar_t dr[4];
	for(dr[0]=L'A',dr[1]=L':',dr[2]=L'\\',dr[3]=0;dr[0]<=L'Z';dr[0]++)
	{
		if(::GetDriveType(dr) == DRIVE_CDROM)
		{
			wchar_t vlabel[256];
			wchar_t fs[256];
			DWORD mcl = 0,sfs = 0;
			GetVolumeInformation(dr, vlabel, 255, NULL, &mcl, &sfs, fs, 255);
			if( icomp(std::wstring(vlabel),narrow_name) )
			//if(std::string(vlabel).AnsiCompareIC(narrow_name)==0)
				return ttstr((tjs_char)dr[0]);
		}
	}
#endif
	return ttstr();
}
//---------------------------------------------------------------------------


tTJSBinaryStream * TVPGetCachedArchiveHandle(void * pointer, const ttstr & name);
void TVPReleaseCachedArchiveHandle(void * pointer, tTJSBinaryStream * stream);
TArchiveStream::TArchiveStream(tTVPArchive *owner, tjs_uint64 off, tjs_uint64 len) : Owner(owner), StartPos(off), DataLength(len) {
	Owner->AddRef();
	_instr = TVPGetCachedArchiveHandle(Owner, Owner->ArchiveName);
	CurrentPos = 0;
	_instr->SetPosition(off);
}

TArchiveStream::~TArchiveStream() {
	TVPReleaseCachedArchiveHandle(Owner, _instr);
	Owner->Release();
}

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Storages
//---------------------------------------------------------------------------
tTJSNativeClass * TVPCreateNativeClass_Storages()
{
	tTJSNC_Storages *cls = new tTJSNC_Storages();


	// setup some platform-specific members
//----------------------------------------------------------------------

//-- methods

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/searchCD)
{
	if(numparams < 1) return TJS_E_BADPARAMCOUNT;

	if(result)
		*result = TVPSearchCD(*param[0]);

	return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/cls,
	/*func. name*/searchCD)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getLocalName)
{
	if(numparams < 1) return TJS_E_BADPARAMCOUNT;

	if(result)
	{
		ttstr str(TVPNormalizeStorageName(*param[0]));
		TVPGetLocalName(str);
		*result = str;
	}

	return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/cls,
	/*func. name*/getLocalName)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/selectFile)
{
	if(numparams < 1) return TJS_E_BADPARAMCOUNT;

	iTJSDispatch2 * dsp =  param[0]->AsObjectNoAddRef();

	bool res = TVPSelectFile(dsp);

	if(result) *result = (tjs_int)res;

	return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/cls,
	/*func. name*/selectFile)
//----------------------------------------------------------------------


	return cls;

}
//---------------------------------------------------------------------------

bool TVPSaveStreamToFile(tTJSBinaryStream *st, tjs_uint64 offset, tjs_uint64 size, ttstr outpath) {
    if (!TVPCreateFolders(TVPExtractStoragePath(outpath))) return false;
    std::unique_ptr<KrkrFile, decltype(&krkr_fs_close)> file(
        krkr_fs_open(outpath.AsStdString().c_str(), 1), krkr_fs_close);
    if (!file) return false;
    auto original = st->GetPosition();
    bool ok = true;
    try {
        st->SetPosition(offset);
        std::vector<char> buffer(2 * 1024 * 1024);
        while (size) {
            auto n = st->Read(buffer.data(), (tjs_uint)std::min<tjs_uint64>(size, buffer.size()));
            if (!n || krkr_fs_write(file.get(), buffer.data(), n) != n) { ok = false; break; }
            size -= n;
        }
        st->SetPosition(original);
    } catch (...) { st->SetPosition(original); throw; }
    return ok && krkr_fs_flush(file.get()) == 0;
}
