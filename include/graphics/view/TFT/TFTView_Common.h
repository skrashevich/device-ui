#ifndef _TFTVIEW_COMMON_H_
#define _TFTVIEW_COMMON_H_

#include "graphics/common/MeshtasticView.h"
#include "meshtastic/clientonly.pb.h"
#include <set>

class MapPanel;

/**
 * @brief Common intermediate base class for TFT views (320x240 and 480x222)
 * Contains all shared member variables, enums, structs, typedefs, and method declarations.
 * Note: only ONE view compiles per build, so virtual dispatch has zero overhead.
 */
class TFTView_Common : public MeshtasticView
{
  public:
    enum BasicSettings {
        eNone,
        eSetup,
        eUsername,
        eDeviceRole,
        eRegion,
        eModemPreset,
        eChannel,
        eWifi,
        eLanguage,
        eScreenTimeout,
        eScreenLock,
        eScreenBrightness,
        eTheme,
        eInputControl,
        eAlertBuzzer,
        eBackupRestore,
        eReset,
        eReboot,
        eDisplayMode,
        eModifyChannel
    };

    // MeshtasticView overrides — lifecycle
    void init(IClientBase *client) override;
    bool setupUIConfig(const meshtastic_DeviceUIConfig &uiconfig) override;
    void task_handler(void) override;

    // MeshtasticView overrides — node updates
    void setMyInfo(uint32_t nodeNum) override;
    void setDeviceMetaData(int hw_model, const char *version, bool has_bluetooth, bool has_wifi, bool has_eth,
                           bool can_shutdown) override;
    void addOrUpdateNode(uint32_t nodeNum, uint8_t channel, uint32_t lastHeard, const meshtastic_User &cfg) override;
    void addNode(uint32_t nodeNum, uint8_t channel, const char *userShort, const char *userLong, uint32_t lastHeard, eRole role,
                 bool hasKey, bool unmessagable) override;
    void updateNode(uint32_t nodeNum, uint8_t channel, const meshtastic_User &cfg) override;
    void updatePosition(uint32_t nodeNum, int32_t lat, int32_t lon, int32_t alt, uint32_t sats, uint32_t precision) override;
    void updateMetrics(uint32_t nodeNum, uint32_t bat_level, float voltage, float chUtil, float airUtil) override;
    void updateEnvironmentMetrics(uint32_t nodeNum, const meshtastic_EnvironmentMetrics &metrics) override;
    void updateAirQualityMetrics(uint32_t nodeNum, const meshtastic_AirQualityMetrics &metrics) override;
    void updatePowerMetrics(uint32_t nodeNum, const meshtastic_PowerMetrics &metrics) override;
    void updateSignalStrength(uint32_t nodeNum, int32_t rssi, float snr) override;
    void updateHopsAway(uint32_t nodeNum, uint8_t hopsAway) override;
    void updateConnectionStatus(const meshtastic_DeviceConnectionStatus &status) override;
    void removeNode(uint32_t nodeNum) override;

    // MeshtasticView overrides — config updates
    void updateChannelConfig(const meshtastic_Channel &ch) override;
    void updateDeviceConfig(const meshtastic_Config_DeviceConfig &cfg) override;
    void updatePositionConfig(const meshtastic_Config_PositionConfig &cfg) override;
    void updatePowerConfig(const meshtastic_Config_PowerConfig &cfg) override;
    void updateNetworkConfig(const meshtastic_Config_NetworkConfig &cfg) override;
    void updateDisplayConfig(const meshtastic_Config_DisplayConfig &cfg) override;
    void updateLoRaConfig(const meshtastic_Config_LoRaConfig &cfg) override;
    void updateBluetoothConfig(const meshtastic_Config_BluetoothConfig &cfg, uint32_t id = 0) override;
    void updateSecurityConfig(const meshtastic_Config_SecurityConfig &cfg) override;
    void updateSessionKeyConfig(const meshtastic_Config_SessionkeyConfig &cfg) override;

    // MeshtasticView overrides — module config
    void updateMQTTModule(const meshtastic_ModuleConfig_MQTTConfig &cfg) override;
    void updateSerialModule(const meshtastic_ModuleConfig_SerialConfig &cfg) override {}
    void updateExtNotificationModule(const meshtastic_ModuleConfig_ExternalNotificationConfig &cfg) override;
    void updateStoreForwardModule(const meshtastic_ModuleConfig_StoreForwardConfig &cfg) override {}
    void updateRangeTestModule(const meshtastic_ModuleConfig_RangeTestConfig &cfg) override {}
    void updateTelemetryModule(const meshtastic_ModuleConfig_TelemetryConfig &cfg) override {}
    void updateCannedMessageModule(const meshtastic_ModuleConfig_CannedMessageConfig &) override {}
    void updateAudioModule(const meshtastic_ModuleConfig_AudioConfig &cfg) override {}
    void updateRemoteHardwareModule(const meshtastic_ModuleConfig_RemoteHardwareConfig &cfg) override {}
    void updateNeighborInfoModule(const meshtastic_ModuleConfig_NeighborInfoConfig &cfg) override {}
    void updateAmbientLightingModule(const meshtastic_ModuleConfig_AmbientLightingConfig &cfg) override {}
    void updateDetectionSensorModule(const meshtastic_ModuleConfig_DetectionSensorConfig &cfg) override {}
    void updatePaxCounterModule(const meshtastic_ModuleConfig_PaxcounterConfig &cfg) override {}
    void updateFileinfo(const meshtastic_FileInfo &fileinfo) override {}
    void updateRingtone(const char rtttl[231]) override;

    // MeshtasticView overrides — time
    void updateTime(uint32_t time) override;

    // MeshtasticView overrides — notifications and responses
    void packetReceived(const meshtastic_MeshPacket &p) override;
    void handleResponse(uint32_t from, uint32_t id, const meshtastic_Routing &routing, const meshtastic_MeshPacket &p) override;
    void handleResponse(uint32_t from, uint32_t id, const meshtastic_RouteDiscovery &route) override;
    void handlePositionResponse(uint32_t from, uint32_t request_id, int32_t rx_rssi, float rx_snr, bool isNeighbor) override;
    void notifyRestoreMessages(int32_t percentage) override;
    void notifyMessagesRestored(void) override;
    void notifyConnected(const char *info) override;
    void notifyDisconnected(const char *info) override;
    void notifyResync(bool show) override;
    void notifyReboot(bool show) override;
    void notifyShutdown(void) override;
    void blankScreen(bool enable) override;
    void screenSaving(bool enabled) override;
    bool isScreenLocked(void) override;
    void newMessage(uint32_t from, uint32_t to, uint8_t ch, const char *msg, uint32_t &msgtime, bool restore = true) override;
    void restoreMessage(const LogMessage &msg) override;

    // virtual methods for view-specific differences (override in subclasses)
    virtual void configureKeyboardLayouts() = 0;
    virtual void ui_set_active(lv_obj_t *b, lv_obj_t *p, lv_obj_t *tp) = 0;
    virtual void onAddNodeExtra(lv_obj_t *node) {}
    virtual void onUpdatePositionExtra() {}
    virtual void onUpdateEnvironmentMetricsExtra() {}
    virtual void onNewMessageExtra(uint32_t from, bool isDM) {}
    virtual void onUnreadMessagesUpdated() {}
    virtual void onConnectionStatusExtra() {}
    virtual void onEventsInitExtra() {}
    virtual void onInitScreensExtra() {}
    virtual int getChannelButtonWidth() { return 70; }
    virtual int getExitCode() { return 0; }

  protected:
    // protected constructor (abstract intermediate class)
    TFTView_Common(const DisplayDriverConfig *cfg, DisplayDriver *driver);

    static TFTView_Common *commonInstance;

    struct NodeFilter {
        bool unknown;  // filter out unknown nodes
        bool mqtt;     // filter out via mqtt nodes
        bool offline;  // filter out offline nodes (>15min lastheard)
        bool position; // filter out nodes without position
        char *name;    // filter by name
        bool active;   // flag for active filter
    };

    struct NodeHighlight {
        bool chat;      // highlight nodes with active chats
        bool position;  // highlight nodes with position
        bool telemetry; // highlight nodes with telemetry
        bool iaq;       // highlight nodes with IAQ
        char *name;     // hightlight by name
        bool active;    // flag for active highlight;
    };

    typedef void (*UserWidgetFunc)(lv_obj_t *, void *, int);

    // screen initialization
    void init_screens(void);
    void updateBootMessage(const char *);
    void requestSetup(void);
    void apply_hotfix(void);
    void ui_events_init(void);

    // UI helpers
    void showKeyboard(lv_obj_t *textArea);
    void hideKeyboard(lv_obj_t *panel);
    lv_obj_t *showQrCode(lv_obj_t *parent, const char *data);
    void enablePanel(lv_obj_t *panel);
    void disablePanel(lv_obj_t *panel);
    void setGroupFocus(lv_obj_t *panel);
    void setInputGroup(void);
    void setInputButtonLabel(void);
    void showUserWidget(UserWidgetFunc createWidget);
    void enterProgrammingMode(void);
    void updateTheme(void);

    // node management
    void purgeNode(uint32_t nodeNum);
    bool applyNodesFilter(uint32_t nodeNum, bool reset = false);
    void setNodeImage(uint32_t nodeNum, eRole role, bool unmessagable, lv_obj_t *img);
    void updateNodesStatus(void);
    void updateNodesFiltered(bool reset);
    void updateLastHeard(uint32_t nodeNum);
    void updateAllLastHeard(void);

    // message/chat functions
    lv_obj_t *newMessageContainer(uint32_t from, uint32_t to, uint8_t ch);
    void newMessage(uint32_t nodeNum, lv_obj_t *container, uint8_t channel, const char *msg);
    void addChat(uint32_t from, uint32_t to, uint8_t ch);
    void showMessages(uint8_t channel);
    void showMessages(uint32_t nodeNum);
    void handleAddMessage(char *msg);
    void addMessage(lv_obj_t *container, uint32_t msgTime, uint32_t requestId, char *msg, LogMessage::MsgStatus status);
    void highlightChat(uint32_t from, uint32_t to, uint8_t ch);
    void updateActiveChats(void);
    void showMessagePopup(uint32_t from, uint32_t to, uint8_t ch, const char *name);
    void hideMessagePopup(void);
    void updateUnreadMessages(void);
    void messageAlert(const char *alert, bool show);
    void handleTextMessageResponse(uint32_t channelOrNode, uint32_t id, bool ack, bool err);

    // status updates
    void updateTime(void);
    void updateFreeMem(void);
    bool updateSDCard(void);
    void formatSDCard(void);
    void updateDistance(uint32_t nodeNum, int32_t lat, int32_t lon);
    void updateSignalStrength(int32_t rssi, float snr);
    int32_t signalStrength2Percent(int32_t rx_rssi, float rx_snr);

    // config helpers
    void updateGroupChannel(uint8_t chId);
    void showLoRaFrequency(const meshtastic_Config_LoRaConfig &cfg);
    void setBellText(bool banner, bool sound);
    void setChannelName(const meshtastic_Channel &ch);
    uint32_t timestamp(char *buf, uint32_t time, bool update);

    // settings helpers
    uint32_t preset2val(meshtastic_Config_LoRaConfig_ModemPreset preset);
    meshtastic_Config_LoRaConfig_ModemPreset val2preset(uint32_t val);
    uint32_t role2val(meshtastic_Config_DeviceConfig_Role role);
    meshtastic_Config_DeviceConfig_Role val2role(uint32_t val);
    uint32_t language2val(meshtastic_Language lang);
    meshtastic_Language val2language(uint32_t val);
    void setLocale(meshtastic_Language lang);
    void setLanguage(meshtastic_Language lang);
    void setTimeout(uint32_t timeout);
    void setBrightness(uint32_t brightness);
    void setTheme(uint32_t theme);
    void storeNodeOptions(void);
    void eraseChat(uint32_t channelOrNode);
    void clearChatHistory(void);

    // backup/restore
    void backup(uint32_t option);
    void restore(uint32_t option);
    bool backupFullConfig(void);
    bool restoreFullConfig(void);

    // scanner/traceroute
    void scanSignal(uint32_t scanNo);
    void handleTraceRouteResponse(const meshtastic_Routing &routing);
    void addNodeToTraceRoute(uint32_t nodeNum, lv_obj_t *panel);
    void removeSpinner(void);
    void packetDetected(const meshtastic_MeshPacket &p);
    void writePacketLog(const meshtastic_MeshPacket &p);
    void updateStatistics(const meshtastic_MeshPacket &p);

    // map functions
    void loadMap(void);
    void addOrUpdateMap(uint32_t nodeNum, int32_t lat, int32_t lon);
    void removeFromMap(uint32_t nodeNum);
    void updateLocationMap(uint32_t objects);
    // response callbacks
    void onTextMessageCallback(const ResponseHandler::Request &, ResponseHandler::EventType, int32_t);
    void onPositionCallback(const ResponseHandler::Request &, ResponseHandler::EventType, int32_t);
    void onTracerouteCallback(const ResponseHandler::Request &, ResponseHandler::EventType, int32_t);

    // lvgl timer callbacks
    static void timer_event_reboot(lv_timer_t *timer);
    static void timer_event_shutdown(lv_timer_t *timer);
    static void timer_event_programming_mode(lv_timer_t *timer);

    // lvgl event callbacks
    static void ui_event_LogoButton(lv_event_t *e);
    static void ui_event_BluetoothButton(lv_event_t *e);
    static void ui_event_NodesButton(lv_event_t *e);
    static void ui_event_GroupsButton(lv_event_t *e);
    static void ui_event_MessagesButton(lv_event_t *e);
    static void ui_event_MapButton(lv_event_t *e);
    static void ui_event_SettingsButton(lv_event_t *e);
    static void ui_event_NodeButton(lv_event_t *e);
    static void ui_event_ChannelButton(lv_event_t *e);
    static void ui_event_ChatButton(lv_event_t *e);
    static void ui_event_ChatDelButton(lv_event_t *e);
    static void ui_event_chatNodeButton(lv_event_t *e);
    static void ui_event_MsgPopupButton(lv_event_t *e);
    static void ui_event_MsgRestoreButton(lv_event_t *e);
    static void ui_event_AlertButton(lv_event_t *e);
    static void ui_event_EnvelopeButton(lv_event_t *e);
    static void ui_event_OnlineNodesButton(lv_event_t *e);
    static void ui_event_TimeButton(lv_event_t *e);
    static void ui_event_LoRaButton(lv_event_t *e);
    static void ui_event_BellButton(lv_event_t *e);
    static void ui_event_LocationButton(lv_event_t *e);
    static void ui_event_WLANButton(lv_event_t *e);
    static void ui_event_MQTTButton(lv_event_t *e);
    static void ui_event_SDCardButton(lv_event_t *e);
    static void ui_event_MemoryButton(lv_event_t *e);
    static void ui_event_QrButton(lv_event_t *e);
    static void ui_event_CancelQrButton(lv_event_t *e);
    static void ui_event_BlankScreenButton(lv_event_t *e);
    static void ui_event_KeyboardButton(lv_event_t *e);
    static void ui_event_Keyboard(lv_event_t *e);
    static void ui_event_message_ready(lv_event_t *e);
    static void ui_event_user_button(lv_event_t *e);
    static void ui_event_role_button(lv_event_t *e);
    static void ui_event_region_button(lv_event_t *e);
    static void ui_event_preset_button(lv_event_t *e);
    static void ui_event_wifi_button(lv_event_t *e);
    static void ui_event_language_button(lv_event_t *e);
    static void ui_event_channel_button(lv_event_t *e);
    static void ui_event_brightness_button(lv_event_t *e);
    static void ui_event_theme_button(lv_event_t *e);
    static void ui_event_calibration_button(lv_event_t *e);
    static void ui_event_timeout_button(lv_event_t *e);
    static void ui_event_screen_lock_button(lv_event_t *e);
    static void ui_event_input_button(lv_event_t *e);
    static void ui_event_alert_button(lv_event_t *e);
    static void ui_event_backup_button(lv_event_t *e);
    static void ui_event_reset_button(lv_event_t *e);
    static void ui_event_reboot_button(lv_event_t *e);
    static void ui_event_device_reboot_button(lv_event_t *e);
    static void ui_event_device_progmode_button(lv_event_t *e);
    static void ui_event_device_shutdown_button(lv_event_t *e);
    static void ui_event_device_cancel_button(lv_event_t *e);
    static void ui_event_shutdown_button(lv_event_t *e);
    static void ui_event_modify_channel(lv_event_t *e);
    static void ui_event_delete_channel(lv_event_t *e);
    static void ui_event_generate_psk(lv_event_t *e);
    static void ui_event_qr_code(lv_event_t *e);
    static void ui_event_screen_timeout_slider(lv_event_t *e);
    static void ui_event_brightness_slider(lv_event_t *e);
    static void ui_event_frequency_slot_slider(lv_event_t *e);
    static void ui_event_modem_preset_dropdown(lv_event_t *e);
    static void ui_event_setup_region_dropdown(lv_event_t *e);
    static void ui_event_map_style_dropdown(lv_event_t *e);
    static void ui_event_calibration_screen_loaded(lv_event_t *e);
    static void ui_event_mesh_detector(lv_event_t *e);
    static void ui_event_mesh_detector_start(lv_event_t *e);
    static void ui_event_signal_scanner(lv_event_t *e);
    static void ui_event_signal_scanner_node(lv_event_t *e);
    static void ui_event_signal_scanner_start(lv_event_t *e);
    static void ui_event_trace_route(lv_event_t *e);
    static void ui_event_trace_route_to(lv_event_t *e);
    static void ui_event_trace_route_start(lv_event_t *e);
    static void ui_event_trace_route_node(lv_event_t *e);
    static void ui_event_node_details(lv_event_t *e);
    static void ui_event_statistics(lv_event_t *e);
    static void ui_event_packet_log(lv_event_t *e);
    static void ui_event_pin_screen_button(lv_event_t *e);
    static void ui_event_statistics_table(lv_event_t *e);
    static void ui_event_ok(lv_event_t *e);
    static void ui_event_cancel(lv_event_t *e);
    static void ui_event_backup_restore_radio_button(lv_event_t *e);
    static void ui_screen_event_cb(lv_event_t *e);
    static void ui_event_arrow(lv_event_t *e);
    static void ui_event_navHome(lv_event_t *e);
    static void ui_event_zoomSlider(lv_event_t *e);
    static void ui_event_zoomIn(lv_event_t *e);
    static void ui_event_zoomOut(lv_event_t *e);
    static void ui_event_lockGps(lv_event_t *e);
    static void ui_event_mapBrightnessSlider(lv_event_t *e);
    static void ui_event_mapContrastSlider(lv_event_t *e);
    static void ui_event_mapNodeButton(lv_event_t *e);
    static void ui_event_positionButton(lv_event_t *e);

    // animations
    static void ui_anim_node_panel_cb(void *var, int32_t v);
    static void ui_anim_radar_cb(void *var, int32_t r);

    std::function<void(uint32_t id, uint16_t x, uint16_t y, uint8_t)> drawObjectCB;

    NodeFilter filter;
    NodeHighlight highlight;

    // shared member variables
    lv_obj_t *activeButton = nullptr;
    lv_obj_t *activePanel = nullptr;
    lv_obj_t *activeTopPanel = nullptr;
    lv_obj_t *activeMsgContainer = nullptr;
    lv_obj_t *activeWidget = nullptr;
    lv_obj_t *activeTextInput = nullptr;
    lv_group_t *input_group = nullptr;

    enum BasicSettings activeSettings = eNone;

    bool screensInitialised;
    uint32_t nodesFiltered;
    bool nodesChanged;
    bool processingFilter;
    bool packetLogEnabled;
    bool detectorRunning;
    bool cardDetected;
    bool formatSD;
    uint16_t buttonSize;
    uint16_t statisticTableRows;
    uint16_t packetCounter;
    time_t lastrun60, lastrun10, lastrun5, lastrun1;
    time_t actTime, uptime, lastHeard;
    bool hasPosition;
    int32_t myLatitude, myLongitude;
    void *topNodeLL;
    uint32_t scans;
    lv_anim_t radar;
    static uint32_t currentNode;
    static lv_obj_t *currentPanel;
    static lv_obj_t *spinnerButton;
    static time_t startTime;
    static uint32_t pinKeys;
    static bool screenLocked;
    static bool screenUnlockRequest;
    uint32_t selectedHops;
    bool chooseNodeSignalScanner;
    bool chooseNodeTraceRoute;
    char old_val1_scratch[64], old_val2_scratch[64];
    std::array<lv_obj_t *, c_max_channels> ch_label;
    meshtastic_Channel *channel_scratch;
    lv_obj_t *qr;
    MapPanel *map = nullptr;
    std::unordered_map<uint32_t, lv_obj_t *> nodeObjects;

    // extended default device profile struct with additional required data
    struct meshtastic_DeviceProfile_ext : meshtastic_DeviceProfile {
        meshtastic_User user;
        meshtastic_Channel channel[c_max_channels];
        meshtastic_DeviceUIConfig uiConfig;
    };

    // additional local ui data (non-persistent)
    struct meshtastic_DeviceProfile_full : meshtastic_DeviceProfile_ext {
        bool silent;
        meshtastic_DeviceConnectionStatus connectionStatus;
    };

    meshtastic_DeviceProfile_full db{};
};

#endif // _TFTVIEW_COMMON_H_
