#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

/**
 * LVGL UI bindings for Berry scripting engine.
 *
 * Provides a safe subset of LVGL API accessible from Berry scripts:
 *   ui.label(parent, text) -> lv_obj_t*
 *   ui.button(parent, text) -> lv_obj_t*
 *   ui.panel(parent) -> lv_obj_t*
 *   ui.textarea(parent) -> lv_obj_t*
 *   ui.list(parent) -> lv_obj_t*
 *   ui.set_text(obj, text)
 *   ui.set_style(obj, prop, value)
 *
 * These bindings are registered as Berry native modules during
 * BerryEngine::registerBuiltins().
 *
 * TODO: Implement Berry native module registration for UI bindings.
 * This requires defining Berry class/module structures using be_native_module_attr_table.
 */

#include "apps/BerryEngine.h"
#include "lvgl.h"

// Placeholder for Berry native UI module implementation.
// The actual implementation will use Berry's C module API:
//
// static int m_ui_label(bvm *vm) {
//     // get parent from stack, create label, push result
//     lv_obj_t *parent = (lv_obj_t *)be_toint(vm, 1);
//     const char *text = be_tostring(vm, 2);
//     lv_obj_t *label = lv_label_create(parent);
//     lv_label_set_text(label, text);
//     be_pushint(vm, (int)(intptr_t)label);
//     be_return(vm);
// }

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
