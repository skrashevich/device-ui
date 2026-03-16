#pragma once
#ifdef HAS_CUSTOM_APPS

#include "apps/ICustomApp.h"
#include "apps/AppContext.h"
#include <lvgl.h>
#include <string>

class MqttSettingsApp : public ICustomApp {
public:
    MqttSettingsApp();
    ~MqttSettingsApp();

    const char *getName() const override { return "MQTT"; }
    const char *getIcon() const override { return LV_SYMBOL_WIFI; }
    bool init(AppContext *ctx) override;
    lv_obj_t *createUI(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onTick(uint32_t now_ms) override;
    void destroy() override;

    // Called by view when MQTT config is received from firmware
    void updateConfig(bool enabled, const char *address, uint16_t port,
                      const char *username, const char *password,
                      const char *root, bool tlsEnabled, bool jsonEnabled);

private:
    AppContext *appContext = nullptr;

    // LVGL objects
    lv_obj_t *mainPanel = nullptr;
    lv_obj_t *enabledSwitch = nullptr;
    lv_obj_t *serverInput = nullptr;
    lv_obj_t *portInput = nullptr;
    lv_obj_t *userInput = nullptr;
    lv_obj_t *passwordInput = nullptr;
    lv_obj_t *rootTopicInput = nullptr;
    lv_obj_t *tlsSwitch = nullptr;
    lv_obj_t *jsonSwitch = nullptr;
    lv_obj_t *statusLabel = nullptr;
    lv_obj_t *saveBtn = nullptr;
    lv_obj_t *testBtn = nullptr;

    // Current config values
    bool mqttEnabled = false;
    std::string mqttServer;
    uint16_t mqttPort = 1883;
    std::string mqttUser;
    std::string mqttPassword;
    std::string mqttRoot;
    bool mqttTls = false;
    bool mqttJson = false;
    bool configLoaded = false;

    void createFormRow(lv_obj_t *parent, const char *label, lv_obj_t **input, const char *placeholder, bool isPassword = false);
    void createSwitchRow(lv_obj_t *parent, const char *label, lv_obj_t **sw, bool defaultVal);
    void saveConfig();
    void loadConfigToUI();
    void testConnection();

    static void onSaveBtnClicked(lv_event_t *e);
    static void onTestBtnClicked(lv_event_t *e);
};

#endif // HAS_CUSTOM_APPS
