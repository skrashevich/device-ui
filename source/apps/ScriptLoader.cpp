#include "apps/ScriptLoader.h"

#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

#include "apps/AppManager.h"
#include "apps/BerryEngine.h"
#include "apps/ScriptApp.h"
#include "configuration.h"
#include <cstring>

#ifdef USE_LITTLEFS
#include <LittleFS.h>
#define FS_HANDLE LittleFS
#else
#include <SPIFFS.h>
#define FS_HANDLE SPIFFS
#endif

int loadScriptApps(AppManager *manager, BerryEngine *engine, const char *directory)
{
    if (!manager || !engine)
        return 0;

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
            const char *filename = entry.name();
            // Filter by .be extension
            size_t len = strlen(filename);
            if (len > 3 && strcmp(filename + len - 3, ".be") == 0) {
                // Build full path
                char fullPath[128];
                snprintf(fullPath, sizeof(fullPath), "%s/%s", directory, filename);

                // Derive app name: strip .be extension
                char appName[64];
                size_t nameLen = len - 3;
                if (nameLen >= sizeof(appName))
                    nameLen = sizeof(appName) - 1;
                strncpy(appName, filename, nameLen);
                appName[nameLen] = '\0';

                ScriptApp *app = new ScriptApp(appName, fullPath, engine);
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
}

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
