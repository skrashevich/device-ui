#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

/**
 * LVGL UI bindings for Berry scripting engine.
 *
 * Implements the ui.* module accessible from Berry scripts.
 * Uses integer handles instead of raw pointers for memory safety.
 *
 * Handle table: static array of up to MAX_WIDGETS lv_obj_t* pointers.
 * Slot 0 is reserved for the root panel (set via ScriptUIBindings::init()).
 * Slots 1..MAX_WIDGETS-1 are allocated on demand.
 *
 * Berry API:
 *   ui.root() -> handle
 *   ui.label(parent, text) -> handle
 *   ui.button(parent, text) -> handle
 *   ui.panel(parent) -> handle
 *   ui.textarea(parent, placeholder) -> handle
 *   ui.list(parent) -> handle
 *   ui.checkbox(parent, text) -> handle
 *   ui.switch_widget(parent) -> handle
 *   ui.set_text(handle, text)
 *   ui.set_size(handle, w, h)
 *   ui.set_pos(handle, x, y)
 *   ui.set_flex_flow(handle, flow)   # 0=ROW, 1=COLUMN
 *   ui.set_style_bg_color(handle, r, g, b)
 *   ui.set_style_text_color(handle, r, g, b)
 *   ui.set_hidden(handle, bool)
 *   ui.on_click(handle, callback_name)
 *   ui.on_value_changed(handle, callback_name)
 *   ui.delete(handle)
 */

#include "apps/BerryEngine.h"
#include "lvgl.h"
#include "berry.h"
#include "util/ILog.h"

#include <string.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Handle table (module-level statics)
// ---------------------------------------------------------------------------

static constexpr int UI_MAX_WIDGETS = 64;
static constexpr int UI_MAX_STATES = 8;
static constexpr int UI_INVALID_HANDLE = -1;

struct ScriptUIState {
    BerryEngine *engine = nullptr;
    lv_obj_t *widgets[UI_MAX_WIDGETS] = {};
    char click_cb[UI_MAX_WIDGETS][64] = {};
    char value_cb[UI_MAX_WIDGETS][64] = {};
};

static ScriptUIState s_states[UI_MAX_STATES];

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static BerryEngine *getEngineFromVm(bvm *vm)
{
    if (!vm)
        return nullptr;

    be_getglobal(vm, "_eng");
    BerryEngine *engine = (BerryEngine *)be_tocomptr(vm, -1);
    be_pop(vm, 1);
    return engine;
}

static void ui_table_clear(ScriptUIState *state)
{
    if (!state)
        return;
    for (int i = 0; i < UI_MAX_WIDGETS; i++) {
        state->widgets[i] = nullptr;
        state->click_cb[i][0] = '\0';
        state->value_cb[i][0] = '\0';
    }
}

static ScriptUIState *ui_find_state(BerryEngine *engine)
{
    if (!engine)
        return nullptr;

    for (int i = 0; i < UI_MAX_STATES; i++) {
        if (s_states[i].engine == engine)
            return &s_states[i];
    }
    return nullptr;
}

static ScriptUIState *ui_get_or_create_state(BerryEngine *engine)
{
    ScriptUIState *state = ui_find_state(engine);
    if (state)
        return state;

    for (int i = 0; i < UI_MAX_STATES; i++) {
        if (!s_states[i].engine) {
            s_states[i].engine = engine;
            ui_table_clear(&s_states[i]);
            return &s_states[i];
        }
    }

    ILOG_ERROR("ScriptUIBindings: state table full");
    return nullptr;
}

static ScriptUIState *ui_state_from_vm(bvm *vm)
{
    return ui_find_state(getEngineFromVm(vm));
}

static int ui_alloc(ScriptUIState *state, lv_obj_t *obj)
{
    if (!state)
        return UI_INVALID_HANDLE;

    for (int i = 1; i < UI_MAX_WIDGETS; i++) {
        if (state->widgets[i] == nullptr) {
            state->widgets[i] = obj;
            state->click_cb[i][0] = '\0';
            state->value_cb[i][0] = '\0';
            return i;
        }
    }
    ILOG_ERROR("ScriptUIBindings: widget table full");
    return UI_INVALID_HANDLE;
}

static lv_obj_t *ui_resolve(ScriptUIState *state, int handle)
{
    if (!state) {
        ILOG_ERROR("ScriptUIBindings: no UI state for engine");
        return nullptr;
    }
    if (handle < 0 || handle >= UI_MAX_WIDGETS) {
        ILOG_ERROR("ScriptUIBindings: handle %d out of range", handle);
        return nullptr;
    }
    if (!state->widgets[handle]) {
        ILOG_ERROR("ScriptUIBindings: handle %d is null", handle);
        return nullptr;
    }
    return state->widgets[handle];
}

// ---------------------------------------------------------------------------
// LVGL event dispatcher
// ---------------------------------------------------------------------------

static void ui_event_cb(lv_event_t *e)
{
    ScriptUIState *state = static_cast<ScriptUIState *>(lv_event_get_user_data(e));
    if (!state || !state->engine)
        return;

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(e));

    for (int i = 0; i < UI_MAX_WIDGETS; i++) {
        if (state->widgets[i] != target)
            continue;
        if (code == LV_EVENT_CLICKED && state->click_cb[i][0] != '\0') {
            state->engine->callFunction(state->click_cb[i], i);
        } else if (code == LV_EVENT_VALUE_CHANGED && state->value_cb[i][0] != '\0') {
            state->engine->callFunction(state->value_cb[i], i);
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// Berry native functions
// ---------------------------------------------------------------------------

// ui.root() -> 0
static int be_ui_root(bvm *vm)
{
    be_pushint(vm, 0);
    be_return(vm);
}

// ui.label(parent_handle, text) -> handle
static int be_ui_label(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 2) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    lv_obj_t *parent = ui_resolve(state, be_toint(vm, 1));
    if (!parent) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    const char *text = be_tostring(vm, 2);
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    be_pushint(vm, ui_alloc(state, label));
    be_return(vm);
}

// ui.button(parent_handle, text) -> handle
static int be_ui_button(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 2) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    lv_obj_t *parent = ui_resolve(state, be_toint(vm, 1));
    if (!parent) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    const char *text = be_tostring(vm, 2);
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_center(lbl);
    be_pushint(vm, ui_alloc(state, btn));
    be_return(vm);
}

// ui.panel(parent_handle) -> handle
static int be_ui_panel(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 1) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    lv_obj_t *parent = ui_resolve(state, be_toint(vm, 1));
    if (!parent) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    lv_obj_t *panel = lv_obj_create(parent);
    be_pushint(vm, ui_alloc(state, panel));
    be_return(vm);
}

// ui.textarea(parent_handle, placeholder) -> handle
static int be_ui_textarea(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 1) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    lv_obj_t *parent = ui_resolve(state, be_toint(vm, 1));
    if (!parent) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    lv_obj_t *ta = lv_textarea_create(parent);
    if (be_top(vm) >= 2) {
        const char *placeholder = be_tostring(vm, 2);
        if (placeholder && placeholder[0] != '\0')
            lv_textarea_set_placeholder_text(ta, placeholder);
    }
    be_pushint(vm, ui_alloc(state, ta));
    be_return(vm);
}

// ui.list(parent_handle) -> handle
static int be_ui_list(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 1) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    lv_obj_t *parent = ui_resolve(state, be_toint(vm, 1));
    if (!parent) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    lv_obj_t *list = lv_list_create(parent);
    be_pushint(vm, ui_alloc(state, list));
    be_return(vm);
}

// ui.checkbox(parent_handle, text) -> handle
static int be_ui_checkbox(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 1) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    lv_obj_t *parent = ui_resolve(state, be_toint(vm, 1));
    if (!parent) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    lv_obj_t *cb = lv_checkbox_create(parent);
    if (be_top(vm) >= 2) {
        const char *text = be_tostring(vm, 2);
        if (text && text[0] != '\0')
            lv_checkbox_set_text(cb, text);
    }
    be_pushint(vm, ui_alloc(state, cb));
    be_return(vm);
}

// ui.switch_widget(parent_handle) -> handle
static int be_ui_switch(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 1) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    lv_obj_t *parent = ui_resolve(state, be_toint(vm, 1));
    if (!parent) { be_pushint(vm, UI_INVALID_HANDLE); be_return(vm); }
    lv_obj_t *sw = lv_switch_create(parent);
    be_pushint(vm, ui_alloc(state, sw));
    be_return(vm);
}

// ui.set_text(handle, text)
static int be_ui_set_text(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 2) be_return_nil(vm);
    lv_obj_t *obj = ui_resolve(state, be_toint(vm, 1));
    if (!obj) be_return_nil(vm);
    const char *text = be_tostring(vm, 2);
    if (!text) be_return_nil(vm);
    const lv_obj_class_t *cls = lv_obj_get_class(obj);
    if (cls == &lv_label_class) {
        lv_label_set_text(obj, text);
    } else if (cls == &lv_button_class) {
        lv_obj_t *lbl = lv_obj_get_child(obj, 0);
        if (lbl) lv_label_set_text(lbl, text);
    } else if (cls == &lv_textarea_class) {
        lv_textarea_set_text(obj, text);
    } else if (cls == &lv_checkbox_class) {
        lv_checkbox_set_text(obj, text);
    }
    be_return_nil(vm);
}

// ui.set_size(handle, w, h)
static int be_ui_set_size(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 3) be_return_nil(vm);
    lv_obj_t *obj = ui_resolve(state, be_toint(vm, 1));
    if (obj) lv_obj_set_size(obj, be_toint(vm, 2), be_toint(vm, 3));
    be_return_nil(vm);
}

// ui.set_pos(handle, x, y)
static int be_ui_set_pos(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 3) be_return_nil(vm);
    lv_obj_t *obj = ui_resolve(state, be_toint(vm, 1));
    if (obj) lv_obj_set_pos(obj, be_toint(vm, 2), be_toint(vm, 3));
    be_return_nil(vm);
}

// ui.set_flex_flow(handle, flow)  0=ROW, 1=COLUMN
static int be_ui_set_flex_flow(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 2) be_return_nil(vm);
    lv_obj_t *obj = ui_resolve(state, be_toint(vm, 1));
    if (obj) {
        int flow = be_toint(vm, 2);
        lv_obj_set_flex_flow(obj, flow == 0 ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    }
    be_return_nil(vm);
}

// ui.set_style_bg_color(handle, r, g, b)
static int be_ui_set_style_bg_color(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 4) be_return_nil(vm);
    lv_obj_t *obj = ui_resolve(state, be_toint(vm, 1));
    if (obj) {
        lv_color_t c = lv_color_make(
            (uint8_t)be_toint(vm, 2),
            (uint8_t)be_toint(vm, 3),
            (uint8_t)be_toint(vm, 4));
        lv_obj_set_style_bg_color(obj, c, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    }
    be_return_nil(vm);
}

// ui.set_style_text_color(handle, r, g, b)
static int be_ui_set_style_text_color(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 4) be_return_nil(vm);
    lv_obj_t *obj = ui_resolve(state, be_toint(vm, 1));
    if (obj) {
        lv_color_t c = lv_color_make(
            (uint8_t)be_toint(vm, 2),
            (uint8_t)be_toint(vm, 3),
            (uint8_t)be_toint(vm, 4));
        lv_obj_set_style_text_color(obj, c, LV_PART_MAIN);
    }
    be_return_nil(vm);
}

// ui.set_hidden(handle, bool)
static int be_ui_set_hidden(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 2) be_return_nil(vm);
    lv_obj_t *obj = ui_resolve(state, be_toint(vm, 1));
    if (obj) {
        bool hidden = be_tobool(vm, 2);
        if (hidden)
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    be_return_nil(vm);
}

// ui.on_click(handle, callback_name)
static int be_ui_on_click(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 2) be_return_nil(vm);
    int handle = be_toint(vm, 1);
    lv_obj_t *obj = ui_resolve(state, handle);
    if (!obj) be_return_nil(vm);
    const char *cb = be_tostring(vm, 2);
    if (!cb) be_return_nil(vm);
    strncpy(state->click_cb[handle], cb, sizeof(state->click_cb[0]) - 1);
    state->click_cb[handle][sizeof(state->click_cb[0]) - 1] = '\0';
    lv_obj_add_event_cb(obj, ui_event_cb, LV_EVENT_CLICKED, state);
    be_return_nil(vm);
}

// ui.on_value_changed(handle, callback_name)
static int be_ui_on_value_changed(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 2) be_return_nil(vm);
    int handle = be_toint(vm, 1);
    lv_obj_t *obj = ui_resolve(state, handle);
    if (!obj) be_return_nil(vm);
    const char *cb = be_tostring(vm, 2);
    if (!cb) be_return_nil(vm);
    strncpy(state->value_cb[handle], cb, sizeof(state->value_cb[0]) - 1);
    state->value_cb[handle][sizeof(state->value_cb[0]) - 1] = '\0';
    lv_obj_add_event_cb(obj, ui_event_cb, LV_EVENT_VALUE_CHANGED, state);
    be_return_nil(vm);
}

// ui.delete(handle)
static int be_ui_delete(bvm *vm)
{
    ScriptUIState *state = ui_state_from_vm(vm);
    if (be_top(vm) < 1) be_return_nil(vm);
    int handle = be_toint(vm, 1);
    if (handle <= 0 || handle >= UI_MAX_WIDGETS) be_return_nil(vm); // root protected
    if (!state)
        be_return_nil(vm);
    lv_obj_t *obj = state->widgets[handle];
    if (obj) {
        lv_obj_del(obj);
        state->widgets[handle] = nullptr;
        state->click_cb[handle][0] = '\0';
        state->value_cb[handle][0] = '\0';
    }
    be_return_nil(vm);
}

// ---------------------------------------------------------------------------
// Module helper (same pattern as BerryEngine::registerBuiltins)
// ---------------------------------------------------------------------------

static void reg(bvm *vm, const char *mod, const char *fn, bntvfunc f)
{
    be_getglobal(vm, mod);
    if (!be_ismap(vm, -1)) {
        be_pop(vm, 1);
        be_newmap(vm);
        be_setglobal(vm, mod);
        be_getglobal(vm, mod);
    }
    be_pushstring(vm, fn);
    be_pushntvfunction(vm, f);
    be_data_insert(vm, -3);
    be_pop(vm, 2);
    be_pop(vm, 1);
}

// ---------------------------------------------------------------------------
// Public C interface called from BerryEngine::registerBuiltins()
// ---------------------------------------------------------------------------

/**
 * Initialize the ui module.
 * @param engine  BerryEngine instance
 * @param root    Root lv_obj_t* for the app (stored as handle 0)
 */
void ScriptUIBindings_register(BerryEngine *engine, lv_obj_t *root)
{
    ScriptUIState *state = ui_get_or_create_state(engine);
    if (!state)
        return;
    ui_table_clear(state);
    state->widgets[0] = root; // handle 0 = root

    bvm *vm = engine->getVM();
    if (!vm)
        return;

    reg(vm, "ui", "root",                 be_ui_root);
    reg(vm, "ui", "label",                be_ui_label);
    reg(vm, "ui", "button",               be_ui_button);
    reg(vm, "ui", "panel",                be_ui_panel);
    reg(vm, "ui", "textarea",             be_ui_textarea);
    reg(vm, "ui", "list",                 be_ui_list);
    reg(vm, "ui", "checkbox",             be_ui_checkbox);
    reg(vm, "ui", "switch_widget",        be_ui_switch);
    reg(vm, "ui", "set_text",             be_ui_set_text);
    reg(vm, "ui", "set_size",             be_ui_set_size);
    reg(vm, "ui", "set_pos",              be_ui_set_pos);
    reg(vm, "ui", "set_flex_flow",        be_ui_set_flex_flow);
    reg(vm, "ui", "set_style_bg_color",   be_ui_set_style_bg_color);
    reg(vm, "ui", "set_style_text_color", be_ui_set_style_text_color);
    reg(vm, "ui", "set_hidden",           be_ui_set_hidden);
    reg(vm, "ui", "on_click",             be_ui_on_click);
    reg(vm, "ui", "on_value_changed",     be_ui_on_value_changed);
    reg(vm, "ui", "delete",               be_ui_delete);

    ILOG_INFO("ScriptUIBindings: ui module registered (%d slots)", UI_MAX_WIDGETS);
}

/**
 * Release all widget handles.
 * Call from ICustomApp::destroy() before deleting the root widget.
 */
void ScriptUIBindings_destroy(BerryEngine *engine)
{
    ScriptUIState *state = ui_find_state(engine);
    if (!state)
        return;

    ui_table_clear(state);
    state->engine = nullptr;
    ILOG_INFO("ScriptUIBindings: all handles released");
}

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
