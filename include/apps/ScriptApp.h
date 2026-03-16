#pragma once

#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

#include "apps/ICustomApp.h"
#include "apps/ScriptEngine.h"
#include <string>

class AppContext;

/**
 * Bridge between ICustomApp interface and a ScriptEngine.
 * Loads a script file and delegates lifecycle calls to script functions:
 *   app_init(), app_create_ui(parent), app_show(), app_hide(),
 *   app_tick(now_ms), app_on_message(from, text), app_destroy()
 */
class ScriptApp : public ICustomApp
{
  public:
    ScriptApp(const char *name, const char *scriptPath, ScriptEngine *engine);
    ~ScriptApp() override;

    const char *getName() const override { return appName.c_str(); }
    const char *getIcon() const override { return LV_SYMBOL_FILE; }
    bool init(AppContext *ctx) override;
    lv_obj_t *createUI(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onTick(uint32_t now_ms) override;
    void onMeshPacket(const meshtastic_MeshPacket &p) override;
    void destroy() override;

  private:
    std::string appName;
    std::string scriptPath;
    ScriptEngine *engine = nullptr;
    AppContext *ctx = nullptr;
    lv_obj_t *panel = nullptr;
    bool loaded = false;
};

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
