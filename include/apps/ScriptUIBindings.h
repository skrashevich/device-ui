#pragma once

#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

#include "lvgl.h"

class BerryEngine;

/**
 * Register the ui.* Berry module with the given engine.
 * Must be called after BerryEngine::init() and before loading any scripts.
 * @param engine  Initialized BerryEngine instance
 * @param root    Root lv_obj_t* for this app — exposed as handle 0 (ui.root())
 */
void ScriptUIBindings_register(BerryEngine *engine, lv_obj_t *root);

/**
 * Release all widget handles tracked by the ui module.
 * Call from ICustomApp::destroy() before deleting the root widget.
 * Does NOT call lv_obj_del — LVGL deletes children automatically with the root.
 */
void ScriptUIBindings_destroy();

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
