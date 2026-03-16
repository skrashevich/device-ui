#include "apps/ScriptLoader.h"

#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

#include "apps/AppManager.h"
#include "apps/BerryEngine.h"
#include "apps/ScriptApp.h"
#include "configuration.h"
#include "util/ILog.h"
#include <cctype>
#include <cstring>

#if defined(ARCH_PORTDUINO)
#include "PortduinoFS.h"
#define FS_HANDLE PortduinoFS
#elif __has_include("LittleFS.h")
#include "LittleFS.h"
#define FS_HANDLE LittleFS
#elif __has_include(<LittleFS.h>)
#include <LittleFS.h>
#define FS_HANDLE LittleFS
#elif __has_include("SPIFFS.h")
#include "SPIFFS.h"
#define FS_HANDLE SPIFFS
#elif __has_include(<SPIFFS.h>)
#include <SPIFFS.h>
#define FS_HANDLE SPIFFS
#else
#define SCRIPT_LOADER_NO_FS
#endif

int loadScriptApps(AppManager *manager, const char *directory)
{
    if (!manager)
        return 0;

#ifdef SCRIPT_LOADER_NO_FS
    ILOG_WARN("ScriptLoader: no filesystem backend available, skipping auto-load");
    (void)directory;
    return 0;
#else
    // Open directory
    File dir = FS_HANDLE.open(directory);
    if (!dir || !dir.isDirectory()) {
        ILOG_INFO("ScriptLoader: directory %s not found, skipping auto-load", directory);
        return 0;
    }

    int loaded = 0;
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            const char *entryPath = entry.name();
            const char *filename = strrchr(entryPath, '/');
            filename = filename ? filename + 1 : entryPath;
            // Filter by .be extension
            size_t len = strlen(filename);
            if (len > 3 && strcmp(filename + len - 3, ".be") == 0) {
                // Some FS backends report the full path in entry.name(), others only the leaf.
                char fullPath[128];
                if (entryPath[0] == '/') {
                    snprintf(fullPath, sizeof(fullPath), "%s", entryPath);
                } else {
                    snprintf(fullPath, sizeof(fullPath), "%s/%s", directory, filename);
                }

                // Derive a friendly app name from the script file name.
                char appName[64];
                size_t out = 0;
                bool capitalize = true;
                for (size_t i = 0; i + 3 <= len && out + 1 < sizeof(appName); i++) {
                    char ch = filename[i];
                    if (i == len - 3 && strcmp(filename + i, ".be") == 0)
                        break;
                    if (ch == '_' || ch == '-') {
                        if (out > 0 && appName[out - 1] != ' ')
                            appName[out++] = ' ';
                        capitalize = true;
                        continue;
                    }
                    if (capitalize)
                        ch = (char)toupper((unsigned char)ch);
                    appName[out++] = ch;
                    capitalize = (ch == ' ');
                }
                while (out > 0 && appName[out - 1] == ' ')
                    out--;
                appName[out] = '\0';

                ScriptApp *app = new ScriptApp(appName, fullPath, new BerryEngine());
                if (manager->registerApp(app)) {
                    ILOG_INFO("ScriptLoader: loaded script app '%s' from %s", appName, fullPath);
                    loaded++;
                } else {
                    ILOG_WARN("ScriptLoader: could not register '%s' (registry full?)", appName);
                    delete app;
                }
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    return loaded;
#endif
}

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
