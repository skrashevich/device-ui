#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

/**
 * SD card filesystem bindings for Berry scripting engine.
 *
 * Implements the fs.* module accessible from Berry scripts.
 * Supports three SD backends via preprocessor guards:
 *   - ARCH_PORTDUINO  : PortduinoFS (fs::FS interface)
 *   - HAS_SD_MMC      : SD_MMC      (fs::FS interface)
 *   - HAS_SDCARD      : SdFat       (SdFs interface)
 *
 * Berry API:
 *   fs.list_dir(path [, ext]) -> list   filenames; optional ext filter e.g. ".mp3"
 *   fs.file_size(path)        -> int    file size in bytes; -1 on error
 *   fs.exists(path)           -> bool   true if file/directory exists
 *
 * Security: paths containing ".." are rejected to prevent path traversal.
 */

#include "apps/ScriptFsBindings.h"
#include "berry.h"
#include "util/ILog.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Select SD backend
// ---------------------------------------------------------------------------

#if defined(ARCH_PORTDUINO)
#include "PortduinoFS.h"
extern fs::FS &SDFs;
#define FS_BACKEND_ARDUINO 1

#elif defined(HAS_SD_MMC)
#include "SD_MMC.h"
extern fs::SDMMCFS &SDFs;
#define FS_BACKEND_ARDUINO 1

#elif defined(HAS_SDCARD)
#include "SdFat.h"
extern SdFs SDFs;
#define FS_BACKEND_SDFAT 1

#else
#define FS_BACKEND_NONE 1
#endif

// ---------------------------------------------------------------------------
// Security helper
// ---------------------------------------------------------------------------

/** Returns true if path contains ".." — reject to prevent traversal. */
static bool fs_path_unsafe(const char *path)
{
    return path == nullptr || strstr(path, "..") != nullptr;
}

// ---------------------------------------------------------------------------
// Backend helpers
// ---------------------------------------------------------------------------

/** Returns file size in bytes, or -1 on error. */
static int32_t fs_file_size(const char *path)
{
#if defined(FS_BACKEND_ARDUINO)
    File f = SDFs.open(path, FILE_READ);
    if (!f)
        return -1;
    int32_t sz = (int32_t)f.size();
    f.close();
    return sz;
#elif defined(FS_BACKEND_SDFAT)
    FsFile f = SDFs.open(path, O_RDONLY);
    if (!f)
        return -1;
    int32_t sz = (int32_t)f.fileSize();
    f.close();
    return sz;
#else
    (void)path;
    return -1;
#endif
}

/** Returns true if path exists (file or directory). */
static bool fs_path_exists(const char *path)
{
#if defined(FS_BACKEND_ARDUINO)
    return SDFs.exists(path);
#elif defined(FS_BACKEND_SDFAT)
    return SDFs.exists(path);
#else
    (void)path;
    return false;
#endif
}

/** Returns true if name ends with ext (case-sensitive). ext must include dot, e.g. ".mp3". */
static bool fs_name_has_ext(const char *name, const char *ext)
{
    if (!ext || ext[0] == '\0')
        return true; // no filter
    size_t nlen = strlen(name);
    size_t elen = strlen(ext);
    if (nlen < elen)
        return false;
    return strcmp(name + nlen - elen, ext) == 0;
}

// ---------------------------------------------------------------------------
// Berry native functions
// ---------------------------------------------------------------------------

// fs.list_dir(path [, ext]) -> list
// Returns Berry list of filenames. Skips directories.
// Optional second argument filters by extension (e.g. ".mp3").
static int be_fs_list_dir(bvm *vm)
{
    // Always return a list (possibly empty)
    be_newlist(vm); // list on stack top

    int argc = be_top(vm);
    if (argc < 1 || !be_isstring(vm, 1)) {
        be_return(vm);
    }
    const char *path = be_tostring(vm, 1);
    if (fs_path_unsafe(path)) {
        ILOG_WARN("fs.list_dir: unsafe path rejected");
        be_return(vm);
    }

    const char *ext = nullptr;
    if (argc >= 2 && be_isstring(vm, 2)) {
        ext = be_tostring(vm, 2);
    }

#if defined(FS_BACKEND_ARDUINO)
    File dir = SDFs.open(path);
    if (!dir || !dir.isDirectory()) {
        ILOG_WARN("fs.list_dir: cannot open directory '%s'", path);
        be_return(vm);
    }
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            const char *name = entry.name();
            if (fs_name_has_ext(name, ext)) {
                be_pushstring(vm, name);
                be_data_push(vm, -2); // push into list
                be_pop(vm, 1);
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

#elif defined(FS_BACKEND_SDFAT)
    FsFile dir = SDFs.open(path, O_RDONLY);
    if (!dir || !dir.isDir()) {
        ILOG_WARN("fs.list_dir: cannot open directory '%s'", path);
        dir.close();
        be_return(vm);
    }
    FsFile entry;
    char nameBuf[256];
    dir.rewind();
    while (entry.openNext(&dir, O_RDONLY)) {
        if (!entry.isDir()) {
            entry.getName(nameBuf, sizeof(nameBuf));
            if (fs_name_has_ext(nameBuf, ext)) {
                be_pushstring(vm, nameBuf);
                be_data_push(vm, -2);
                be_pop(vm, 1);
            }
        }
        entry.close();
    }
    dir.close();

#else
    (void)ext;
    ILOG_WARN("fs.list_dir: no SD backend available");
#endif

    be_return(vm);
}

// fs.file_size(path) -> int  (-1 on error)
static int be_fs_file_size(bvm *vm)
{
    if (be_top(vm) < 1 || !be_isstring(vm, 1)) {
        be_pushint(vm, -1);
        be_return(vm);
    }
    const char *path = be_tostring(vm, 1);
    if (fs_path_unsafe(path)) {
        ILOG_WARN("fs.file_size: unsafe path rejected");
        be_pushint(vm, -1);
        be_return(vm);
    }
    be_pushint(vm, (int)fs_file_size(path));
    be_return(vm);
}

// fs.exists(path) -> bool
static int be_fs_exists(bvm *vm)
{
    if (be_top(vm) < 1 || !be_isstring(vm, 1)) {
        be_pushbool(vm, false);
        be_return(vm);
    }
    const char *path = be_tostring(vm, 1);
    if (fs_path_unsafe(path)) {
        ILOG_WARN("fs.exists: unsafe path rejected");
        be_pushbool(vm, false);
        be_return(vm);
    }
    be_pushbool(vm, fs_path_exists(path));
    be_return(vm);
}

// ---------------------------------------------------------------------------
// Registration helper (same pattern as BerryEngine::registerBuiltins)
// ---------------------------------------------------------------------------

static void registerModule(bvm *vm, const char *modName,
                            const char *funcName, bntvfunc fn)
{
    be_getglobal(vm, modName);
    if (!be_ismap(vm, -1)) {
        be_pop(vm, 1);
        be_newmap(vm);
        be_setglobal(vm, modName);
        be_getglobal(vm, modName);
    }
    be_pushstring(vm, funcName);
    be_pushntvfunction(vm, fn);
    be_data_insert(vm, -3);
    be_pop(vm, 2);
    be_pop(vm, 1);
}

// ---------------------------------------------------------------------------
// Public C interface
// ---------------------------------------------------------------------------

void ScriptFSBindings_register(bvm *vm)
{
    if (!vm)
        return;

    registerModule(vm, "fs", "list_dir",  be_fs_list_dir);
    registerModule(vm, "fs", "file_size", be_fs_file_size);
    registerModule(vm, "fs", "exists",    be_fs_exists);

    ILOG_INFO("ScriptFSBindings: fs module registered"
#if defined(FS_BACKEND_ARDUINO)
              " (Arduino FS)"
#elif defined(FS_BACKEND_SDFAT)
              " (SdFat)"
#else
              " (no SD backend)"
#endif
    );
}

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
