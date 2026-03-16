#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

#include "apps/ScriptApp.h"
#include "apps/AppContext.h"
#include "apps/BerryEngine.h"
#include "apps/ScriptUIBindings.h"
#include "util/ILog.h"

ScriptApp::ScriptApp(const char *name, const char *scriptPath, ScriptEngine *engine)
    : appName(name), scriptPath(scriptPath), engine(engine), ctx(nullptr), panel(nullptr), loaded(false)
{
}

bool ScriptApp::init(AppContext *ctx)
{
    this->ctx = ctx;

    // Initialize engine if not already done
    if (!engine->init(32 * 1024)) { // 32KB heap
        ILOG_ERROR("ScriptApp '%s': failed to init engine", appName.c_str());
        return false;
    }

    // Set app context on Berry engine for native bindings
    BerryEngine *berry = dynamic_cast<BerryEngine *>(engine);
    if (berry) {
        berry->setAppContext(ctx);
    }

    // Load the script file
    if (!engine->loadScript(scriptPath.c_str())) {
        ILOG_ERROR("ScriptApp '%s': failed to load script '%s'", appName.c_str(), scriptPath.c_str());
        return false;
    }

    loaded = true;

    // Call app_init() if defined in script
    engine->callFunction("app_init");

    ILOG_INFO("ScriptApp '%s' loaded from '%s'", appName.c_str(), scriptPath.c_str());
    return true;
}

lv_obj_t *ScriptApp::createUI(lv_obj_t *parent)
{
    panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(panel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (loaded) {
        // Register ui.* module bindings with this panel as root (handle 0)
        BerryEngine *berry = dynamic_cast<BerryEngine *>(engine);
        if (berry) {
            ScriptUIBindings_register(berry, panel);
        }
        // Call app_create_ui() in the script
        // The script creates LVGL widgets using the ui module
        engine->callFunction("app_create_ui");
    } else {
        lv_obj_t *label = lv_label_create(panel);
        lv_label_set_text(label, "Script not loaded");
        lv_obj_center(label);
    }

    return panel;
}

void ScriptApp::onShow()
{
    if (loaded) {
        engine->callFunction("app_show");
    }
}

void ScriptApp::onHide()
{
    if (loaded) {
        engine->callFunction("app_hide");
    }
}

void ScriptApp::onTick(uint32_t now_ms)
{
    if (loaded) {
        engine->callFunction("app_tick", (int)now_ms);
    }
}

void ScriptApp::onMeshPacket(const meshtastic_MeshPacket &p)
{
    if (!loaded)
        return;

    // Extract text message if present
    if (p.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && p.decoded.payload.size > 0) {
        char from[12];
        snprintf(from, sizeof(from), "%08x", p.from);
        // Null-terminate the payload safely
        char msg[meshtastic_Constants_DATA_PAYLOAD_LEN + 1];
        size_t len = p.decoded.payload.size;
        if (len > meshtastic_Constants_DATA_PAYLOAD_LEN)
            len = meshtastic_Constants_DATA_PAYLOAD_LEN;
        memcpy(msg, p.decoded.payload.bytes, len);
        msg[len] = '\0';
        engine->callFunction("app_on_message", from, msg);
    }
}

void ScriptApp::destroy()
{
    if (loaded) {
        engine->callFunction("app_destroy");
    }
    ScriptUIBindings_destroy();
    if (panel) {
        lv_obj_del(panel);
        panel = nullptr;
    }
    loaded = false;
}

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
