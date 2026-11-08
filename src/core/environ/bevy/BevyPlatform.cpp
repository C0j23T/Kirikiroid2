// bevy port: Linux implementations of the platform functions that the
// cocos2d-x front end (environ/cocos2d, environ/android, environ/win32/
// Platform.cpp) provides upstream. See BevyPlatform.h.
#include "tjsCommHead.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "BevyPlatform.h"
#include "ConfigManager/LocaleConfigManager.h"
#include "Platform.h"
#include "SysInitImpl.h"
#include "TVPScreen.h"
#include "platform/CCFileUtils.h"

// ─── log sink ───────────────────────────────────────────────────────────────

static TVPBevyLogSink TVPBevyLogSinkFn = nullptr;
static void *TVPBevyLogSinkData = nullptr;

void TVPBevySetLogSink(TVPBevyLogSink sink, void *userdata) {
    TVPBevyLogSinkFn = sink;
    TVPBevyLogSinkData = userdata;
}

// Called by DebugIntf.cpp for every log line.
void TVPConsoleLog(const ttstr &l, bool important) {
    std::string utf8 = l.AsStdString();
    if (TVPBevyLogSinkFn)
        TVPBevyLogSinkFn(utf8.c_str(), important, TVPBevyLogSinkData);
    else
        std::fprintf(stderr, "[krkr]%s %s\n", important ? "!" : "", utf8.c_str());
}

// ─── screen ─────────────────────────────────────────────────────────────────

static int TVPBevyScreenWidth = 1280;
static int TVPBevyScreenHeight = 720;

void TVPBevySetScreenSize(int width, int height) {
    if (width > 0) TVPBevyScreenWidth = width;
    if (height > 0) TVPBevyScreenHeight = height;
}

tTVPScreen::tTVPScreen() {}
int tTVPScreen::GetWidth() { return TVPBevyScreenWidth; }
int tTVPScreen::GetHeight() { return TVPBevyScreenHeight; }
int tTVPScreen::GetDesktopLeft() { return 0; }
int tTVPScreen::GetDesktopTop() { return 0; }
int tTVPScreen::GetDesktopWidth() { return TVPBevyScreenWidth; }
int tTVPScreen::GetDesktopHeight() { return TVPBevyScreenHeight; }

// ─── system information ─────────────────────────────────────────────────────

ttstr TVPGetPlatformName() { return TJS_W("Linux"); }
ttstr TVPGetOSName() { return TJS_W("Linux (bevy_krkr)"); }
std::string TVPGetPackageVersionString() { return "bevy_krkr"; }

const std::string &TVPGetInternalPreferencePath() {
    static const std::string path = cocos2d::FileUtils::getInstance()->getWritablePath();
    return path;
}

tjs_uint32 TVPGetRoughTickCount32() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (tjs_uint32)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// ─── dialogs ────────────────────────────────────────────────────────────────
// There is no modal UI. Messages are logged; the answer is the last button,
// which by Kirikiroid2's convention is the cancelling one ("cancel", "no").

static int TVPBevyDialogAnswer(size_t buttons) { return buttons ? (int)buttons - 1 : 0; }

int TVPShowSimpleMessageBox(const ttstr &text, const ttstr &caption, const std::vector<ttstr> &vecButtons) {
    TVPConsoleLog(TJS_W("[message box] ") + caption + TJS_W(": ") + text, true);
    return TVPBevyDialogAnswer(vecButtons.size());
}

// TVPShowSimpleMessageBox(text, caption) is defined in environ/Application.cpp.

extern "C" int TVPShowSimpleMessageBox(const char *text, const char *caption, unsigned int nButton, const char **) {
    TVPConsoleLog(ttstr(TJS_W("[message box] ")) + ttstr(caption ? caption : "") + TJS_W(": ") + ttstr(text ? text : ""), true);
    return TVPBevyDialogAnswer(nButton);
}

int TVPShowSimpleInputBox(ttstr &, const ttstr &caption, const ttstr &prompt, const std::vector<ttstr> &vecButtons) {
    TVPConsoleLog(TJS_W("[input box] ") + caption + TJS_W(": ") + prompt, true);
    return TVPBevyDialogAnswer(vecButtons.size());
}

// ─── files ──────────────────────────────────────────────────────────────────

bool TVP_stat(const char *name, tTVP_stat &s) {
    struct stat t;
    if (stat(name, &t) != 0) return false;
    s.st_mode = t.st_mode;
    s.st_size = t.st_size;
#ifdef st_atime
    s.st_atim = t.st_atim;
    s.st_mtim = t.st_mtim;
    s.st_ctim = t.st_ctim;
#else
    s.st_atime = t.st_atime;
    s.st_mtime = t.st_mtime;
    s.st_ctime = t.st_ctime;
#endif
    return true;
}

bool TVP_stat(const tjs_char *name, tTVP_stat &s) {
    return TVP_stat(ttstr(name).AsStdString().c_str(), s);
}

bool TVPCreateFolders(const ttstr &folder) {
    std::string path = folder.AsStdString();
    if (path.empty()) return true;
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            std::string part = path.substr(0, i);
            if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
    }
    return true;
}

bool TVPWriteDataToFile(const ttstr &filepath, const void *data, unsigned int len) {
    // Write to a temporary file first so a failed write never truncates the
    // existing file (save data).
    std::string path = filepath.AsStdString();
    std::string tmp = path + ".tmp";
    FILE *fp = std::fopen(tmp.c_str(), "wb");
    if (!fp) return false;
    bool ok = std::fwrite(data, 1, len, fp) == len;
    ok = (std::fclose(fp) == 0) && ok;
    if (ok) ok = std::rename(tmp.c_str(), path.c_str()) == 0;
    if (!ok) std::remove(tmp.c_str());
    return ok;
}

// ─── process exit ───────────────────────────────────────────────────────────

void TVPExitApplication(int code) {
    // Upstream calls exit(), which ends the process. The host has to survive
    // that, and the engine may already have released itself (`System.exit`
    // runs TVPSystemUninit before exiting), so the call is abandoned at the C
    // ABI boundary instead of unwound: the frames above are left over freed
    // engine objects and their cleanup would dereference them.
    TVPTerminated = true;
    TVPTerminateCode = code;
    if (TVPBevyLeaveThroughExitBoundary()) return;
    throw TVPBevyExitRequest{code};
}

// ─── pixel routines ─────────────────────────────────────────────────────────

// Upstream installs ARM NEON blend routines here (visual/ARM/tvpgl_arm.cpp,
// built for Android only). The portable C routines installed by
// TVPInitTVPGL() (visual/tvpgl.cpp) are used instead.
extern "C" void TVPGL_ASM_Init() {}
