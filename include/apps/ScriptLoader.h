#pragma once

#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

class AppManager;

/// Scan a directory for .be files and register each as a ScriptApp.
/// Returns number of scripts loaded.
int loadScriptApps(AppManager *manager, const char *directory = "/apps/scripts");

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
