#pragma once

#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

class AppManager;
class BerryEngine;

/// Scan a directory for .be files and register each as a ScriptApp.
/// Skips files that are already registered as built-in apps.
/// Returns number of scripts loaded.
int loadScriptApps(AppManager *manager, BerryEngine *engine, const char *directory = "/apps/scripts");

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
