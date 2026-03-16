#ifdef HAS_CUSTOM_APPS

#include "apps/builtin/MqttSettingsApp.h"
#include "apps/AppContext.h"
#include "graphics/common/ViewController.h"
#include "util/ILog.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef HAS_HTTP_CLIENT
#include <WiFiClient.h>
#endif

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

MqttSettingsApp::MqttSettingsApp() {}

MqttSettingsApp::~MqttSettingsApp() {}

// ---------------------------------------------------------------------------
// ICustomApp lifecycle
// ---------------------------------------------------------------------------

bool MqttSettingsApp::init(AppContext *ctx)
{
    appContext = ctx;
    ILOG_INFO("MqttSettingsApp initialized");
    return true;
}

lv_obj_t *MqttSettingsApp::createUI(lv_obj_t *parent)
{
    // Main scrollable panel
    mainPanel = lv_obj_create(parent);
    lv_obj_set_size(mainPanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(mainPanel, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(mainPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(mainPanel, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(mainPanel, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(mainPanel, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(mainPanel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(mainPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(mainPanel);
    lv_label_set_text(title, LV_SYMBOL_WIFI " MQTT Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Enabled toggle
    createSwitchRow(mainPanel, "Enabled", &enabledSwitch, mqttEnabled);

    // Server address
    createFormRow(mainPanel, "Server", &serverInput, "broker.example.com");

    // Port
    createFormRow(mainPanel, "Port", &portInput, "1883");

    // Username
    createFormRow(mainPanel, "Username", &userInput, "user");

    // Password
    createFormRow(mainPanel, "Password", &passwordInput, "password", true);

    // Root topic
    createFormRow(mainPanel, "Root Topic", &rootTopicInput, "msh/");

    // TLS toggle
    createSwitchRow(mainPanel, "TLS", &tlsSwitch, mqttTls);

    // JSON output toggle
    createSwitchRow(mainPanel, "JSON Output", &jsonSwitch, mqttJson);

    // Status label
    statusLabel = lv_label_create(mainPanel);
    lv_label_set_text(statusLabel, configLoaded ? "Config loaded" : "Not configured");
    lv_obj_set_style_text_color(statusLabel, configLoaded ? lv_color_hex(0x00AA00) : lv_color_hex(0xAAAAAA),
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    // Save button
    saveBtn = lv_button_create(mainPanel);
    lv_obj_set_width(saveBtn, LV_PCT(100));
    lv_obj_set_user_data(saveBtn, this);
    lv_obj_t *saveBtnLabel = lv_label_create(saveBtn);
    lv_label_set_text(saveBtnLabel, LV_SYMBOL_SAVE " Save");
    lv_obj_center(saveBtnLabel);
    lv_obj_add_event_cb(saveBtn, onSaveBtnClicked, LV_EVENT_CLICKED, this);

    // Test Connection button
    testBtn = lv_button_create(mainPanel);
    lv_obj_set_width(testBtn, LV_PCT(100));
    lv_obj_t *testBtnLabel = lv_label_create(testBtn);
    lv_label_set_text(testBtnLabel, LV_SYMBOL_REFRESH " Test Connection");
    lv_obj_center(testBtnLabel);
    lv_obj_add_event_cb(testBtn, onTestBtnClicked, LV_EVENT_CLICKED, this);

    // Populate UI if config already loaded
    if (configLoaded)
        loadConfigToUI();

    return mainPanel;
}

void MqttSettingsApp::onShow()
{
    ILOG_DEBUG("MqttSettingsApp: onShow");
}

void MqttSettingsApp::onHide()
{
    ILOG_DEBUG("MqttSettingsApp: onHide");
}

void MqttSettingsApp::onTick(uint32_t now_ms)
{
    (void)now_ms;
}

void MqttSettingsApp::destroy()
{
    if (mainPanel) {
        lv_obj_del(mainPanel);
        mainPanel = nullptr;
    }
    enabledSwitch = nullptr;
    serverInput = nullptr;
    portInput = nullptr;
    userInput = nullptr;
    passwordInput = nullptr;
    rootTopicInput = nullptr;
    tlsSwitch = nullptr;
    jsonSwitch = nullptr;
    statusLabel = nullptr;
    saveBtn = nullptr;
    ILOG_INFO("MqttSettingsApp destroyed");
}

// ---------------------------------------------------------------------------
// Config update from firmware
// ---------------------------------------------------------------------------

void MqttSettingsApp::updateConfig(bool enabled, const char *address, uint16_t port,
                                   const char *username, const char *password,
                                   const char *root, bool tlsEnabled, bool jsonEnabled)
{
    mqttEnabled = enabled;
    mqttServer = address ? address : "";
    mqttPort = port;
    mqttUser = username ? username : "";
    mqttPassword = password ? password : "";
    mqttRoot = root ? root : "";
    mqttTls = tlsEnabled;
    mqttJson = jsonEnabled;
    configLoaded = true;

    if (mainPanel)
        loadConfigToUI();

    ILOG_INFO("MqttSettingsApp: config updated, enabled=%d server=%s", enabled, address ? address : "");
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void MqttSettingsApp::createFormRow(lv_obj_t *parent, const char *label, lv_obj_t **input,
                                    const char *placeholder, bool isPassword)
{
    // Row container
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(row, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(row, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_cross_place(row, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(row, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Label
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_width(lbl, 80);

    // Textarea
    *input = lv_textarea_create(row);
    lv_obj_set_flex_grow(*input, 1);
    lv_obj_set_height(*input, 28);
    lv_textarea_set_one_line(*input, true);
    lv_textarea_set_placeholder_text(*input, placeholder);
    if (isPassword)
        lv_textarea_set_password_mode(*input, true);
}

void MqttSettingsApp::createSwitchRow(lv_obj_t *parent, const char *label, lv_obj_t **sw, bool defaultVal)
{
    // Row container
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(row, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(row, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_cross_place(row, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_main_place(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(row, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Label
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);

    // Switch
    *sw = lv_switch_create(row);
    if (defaultVal)
        lv_obj_add_state(*sw, LV_STATE_CHECKED);
    else
        lv_obj_remove_state(*sw, LV_STATE_CHECKED);
}

void MqttSettingsApp::loadConfigToUI()
{
    if (serverInput)
        lv_textarea_set_text(serverInput, mqttServer.c_str());

    if (portInput) {
        char portBuf[8];
        snprintf(portBuf, sizeof(portBuf), "%u", mqttPort);
        lv_textarea_set_text(portInput, portBuf);
    }

    if (userInput)
        lv_textarea_set_text(userInput, mqttUser.c_str());

    if (passwordInput)
        lv_textarea_set_text(passwordInput, mqttPassword.c_str());

    if (rootTopicInput)
        lv_textarea_set_text(rootTopicInput, mqttRoot.c_str());

    if (enabledSwitch) {
        if (mqttEnabled)
            lv_obj_add_state(enabledSwitch, LV_STATE_CHECKED);
        else
            lv_obj_remove_state(enabledSwitch, LV_STATE_CHECKED);
    }

    if (tlsSwitch) {
        if (mqttTls)
            lv_obj_add_state(tlsSwitch, LV_STATE_CHECKED);
        else
            lv_obj_remove_state(tlsSwitch, LV_STATE_CHECKED);
    }

    if (jsonSwitch) {
        if (mqttJson)
            lv_obj_add_state(jsonSwitch, LV_STATE_CHECKED);
        else
            lv_obj_remove_state(jsonSwitch, LV_STATE_CHECKED);
    }

    if (statusLabel) {
        lv_label_set_text(statusLabel, "Config loaded");
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x00AA00), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void MqttSettingsApp::saveConfig()
{
    if (!appContext || !appContext->getController())
        return;

    meshtastic_ModuleConfig_MQTTConfig cfg = meshtastic_ModuleConfig_MQTTConfig_init_zero;

    // Enabled
    cfg.enabled = enabledSwitch && lv_obj_has_state(enabledSwitch, LV_STATE_CHECKED);

    // Server address
    if (serverInput) {
        const char *addr = lv_textarea_get_text(serverInput);
        if (addr)
            strncpy(cfg.address, addr, sizeof(cfg.address) - 1);
    }

    // Username
    if (userInput) {
        const char *user = lv_textarea_get_text(userInput);
        if (user)
            strncpy(cfg.username, user, sizeof(cfg.username) - 1);
    }

    // Password
    if (passwordInput) {
        const char *pass = lv_textarea_get_text(passwordInput);
        if (pass)
            strncpy(cfg.password, pass, sizeof(cfg.password) - 1);
    }

    // Root topic
    if (rootTopicInput) {
        const char *root = lv_textarea_get_text(rootTopicInput);
        if (root)
            strncpy(cfg.root, root, sizeof(cfg.root) - 1);
    }

    // TLS
    cfg.tls_enabled = tlsSwitch && lv_obj_has_state(tlsSwitch, LV_STATE_CHECKED);

    // JSON output
    cfg.json_enabled = jsonSwitch && lv_obj_has_state(jsonSwitch, LV_STATE_CHECKED);

    appContext->getController()->sendConfig(std::move(cfg));

    if (statusLabel) {
        lv_label_set_text(statusLabel, "Saved!");
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x00AA00), LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    ILOG_INFO("MqttSettingsApp: config saved");
}

// ---------------------------------------------------------------------------
// Static LVGL callbacks
// ---------------------------------------------------------------------------

void MqttSettingsApp::onSaveBtnClicked(lv_event_t *e)
{
    MqttSettingsApp *self = static_cast<MqttSettingsApp *>(lv_event_get_user_data(e));
    if (self)
        self->saveConfig();
}

void MqttSettingsApp::onTestBtnClicked(lv_event_t *e)
{
    MqttSettingsApp *self = static_cast<MqttSettingsApp *>(lv_event_get_user_data(e));
    if (self)
        self->testConnection();
}

void MqttSettingsApp::testConnection()
{
    if (!statusLabel)
        return;

    // Read current values from UI
    const char *server = serverInput ? lv_textarea_get_text(serverInput) : "";
    const char *portStr = portInput ? lv_textarea_get_text(portInput) : "1883";
    uint16_t port = (uint16_t)atoi(portStr);
    bool tls = tlsSwitch && lv_obj_has_state(tlsSwitch, LV_STATE_CHECKED);

    if (!server || strlen(server) == 0) {
        lv_label_set_text(statusLabel, "Error: no server address");
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        return;
    }

    if (port == 0)
        port = tls ? 8883 : 1883;

    lv_label_set_text(statusLabel, "Testing...");
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xAAAA00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_refr_now(NULL); // force UI update before blocking call

    // Attempt a raw TCP connection to the broker
#ifdef HAS_HTTP_CLIENT
    WiFiClient testClient;
    bool connected = testClient.connect(server, port, 5000); // 5s timeout
    testClient.stop();

    if (connected) {
        char buf[64];
        snprintf(buf, sizeof(buf), "OK: %s:%u reachable", server, port);
        lv_label_set_text(statusLabel, buf);
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x00AA00), LV_PART_MAIN | LV_STATE_DEFAULT);
        ILOG_INFO("MqttSettingsApp: test OK — %s:%u reachable", server, port);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "FAIL: %s:%u unreachable", server, port);
        lv_label_set_text(statusLabel, buf);
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        ILOG_WARN("MqttSettingsApp: test FAIL — %s:%u unreachable", server, port);
    }
#else
    lv_label_set_text(statusLabel, "Test unavailable (no WiFi)");
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xAAAAAA), LV_PART_MAIN | LV_STATE_DEFAULT);
#endif
}

#endif // HAS_CUSTOM_APPS
