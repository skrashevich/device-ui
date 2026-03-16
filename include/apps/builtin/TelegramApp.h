#pragma once

#ifdef HAS_CUSTOM_APPS

#include "apps/ICustomApp.h"
#include "apps/AppContext.h"
#include <lvgl.h>
#include <string>
#include <vector>

struct TgChat {
    int64_t id;
    std::string title;
    int unread;
    std::string type; // "user", "group", "channel"
    std::string lastMessage;
    uint32_t lastDate;
    bool isForum;
};

struct TgTopic {
    int32_t id;
    std::string title;
    int unread;
};

struct TgMessage {
    int32_t id;
    std::string fromName;
    std::string text;
    uint32_t date;
    bool out; // sent by us
};

struct BridgeRule {
    std::string name;        // unique rule name (server key)
    std::string direction;   // "mesh_to_telegram", "telegram_to_mesh", "both"
    int32_t meshChannel;     // 0-7, stored as string "0"-"7" on server
    int64_t telegramChatId;
    int32_t telegramTopicId; // 0 = no topic
    std::string tgChatTitle; // local display only (not from server)
    std::string formatTemplate;
    bool enabled;
};

class TelegramApp : public ICustomApp
{
  public:
    TelegramApp();
    ~TelegramApp();

    const char *getName() const override { return "Telegram"; }
    const char *getIcon() const override { return LV_SYMBOL_ENVELOPE; }
    bool init(AppContext *ctx) override;
    lv_obj_t *createUI(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onTick(uint32_t now_ms) override;
    void onMeshPacket(const meshtastic_MeshPacket &p) override;
    void destroy() override;

  private:
    AppContext *appContext = nullptr;

    // Server config
    std::string serverUrl;
    std::string apiKey;
    bool connected = false;
    bool authorized = false;

    // Connection state tracking
    enum ConnectionState { CONN_DISCONNECTED, CONN_CONNECTING, CONN_CONNECTED, CONN_ERROR };
    ConnectionState connState = CONN_DISCONNECTED;
    uint8_t retryCount = 0;
    uint32_t lastRetryMs = 0;
    uint32_t retryIntervalMs = 2000;
    static const uint8_t MAX_RETRIES = 3;
    std::string lastError;
    uint32_t disconnectedSinceMs = 0;

    // Data cache
    std::vector<TgChat> chats;
    std::vector<TgTopic> topics;
    std::vector<TgMessage> messages;
    int64_t currentChatId = 0;
    int32_t currentTopicId = 0; // 0 = no topic (regular chat)

    // UI state
    enum Screen { SCREEN_SETTINGS, SCREEN_CHATS, SCREEN_TOPICS, SCREEN_CHAT, SCREEN_BRIDGE_RULES, SCREEN_BRIDGE_EDIT };
    Screen currentScreen = SCREEN_SETTINGS;
    int selectedChatIndex = 0;

    // LVGL objects
    lv_obj_t *mainPanel = nullptr;

    // Settings screen
    lv_obj_t *settingsPanel = nullptr;
    lv_obj_t *serverUrlTextarea = nullptr;
    lv_obj_t *apiKeyTextarea = nullptr;
    lv_obj_t *statusLabel = nullptr;
    lv_obj_t *connectBtn = nullptr;
    lv_obj_t *authStatusLabel = nullptr;

    // Auth screen
    lv_obj_t *authPanel = nullptr;
    lv_obj_t *authInput = nullptr;
    lv_obj_t *authPromptLabel = nullptr;
    lv_obj_t *authQrContainer = nullptr;
    lv_obj_t *authQrCode = nullptr;
    lv_obj_t *authQrStatusLabel = nullptr;
    lv_obj_t *authSubmitBtn = nullptr;
    lv_obj_t *authSubmitLabel = nullptr;
    enum AuthStep { AUTH_NONE, AUTH_QR, AUTH_2FA };
    AuthStep authStep = AUTH_NONE;

    // Chats screen
    lv_obj_t *chatsPanel = nullptr;
    lv_obj_t *chatsList = nullptr;

    // Topics screen (for forum chats)
    lv_obj_t *topicsPanel = nullptr;
    lv_obj_t *topicsList = nullptr;
    lv_obj_t *topicsTitle = nullptr;

    // Bridge rules screen
    lv_obj_t *bridgeRulesPanel = nullptr;
    lv_obj_t *bridgeRulesList = nullptr;

    // Bridge edit screen
    lv_obj_t *bridgeEditPanel = nullptr;
    lv_obj_t *bridgeDirDropdown = nullptr;
    lv_obj_t *bridgeChDropdown = nullptr;
    lv_obj_t *bridgeChatDropdown = nullptr;
    lv_obj_t *bridgeFormatTextarea = nullptr;
    lv_obj_t *bridgeEnabledSwitch = nullptr;

    // Bridge data
    std::vector<BridgeRule> bridgeRules;
    int selectedBridgeRuleIndex = -1; // -1 = adding new rule

    // Chat/messages screen
    lv_obj_t *chatPanel = nullptr;
    lv_obj_t *chatTitle = nullptr;
    lv_obj_t *messagesList = nullptr;
    lv_obj_t *messageInput = nullptr;
    lv_obj_t *sendBtn = nullptr;

    // Timing
    uint32_t lastPollMs = 0;
    uint32_t lastAuthPollMs = 0;
    static const uint32_t POLL_INTERVAL_MS = 5000;
    static const uint32_t AUTH_POLL_INTERVAL_MS = 2000;

    // WebSocket push client (ws://{server}/api/updates)
    // Minimal raw WiFiClient-based implementation; no external WS library needed.
#ifdef HAS_HTTP_CLIENT
    void *wifiClient = nullptr; // opaque WiFiClient* (void* avoids header pollution)
#endif
    bool wsConnected = false;
    std::string wsReadBuf;       // accumulated incomplete frame data
    uint32_t wsLastConnectMs = 0;
    uint32_t wsReconnectIntervalMs = 2000;
    uint8_t wsReconnectCount = 0;

    void wsConnect();
    void wsDisconnect();
    void wsPoll();
    void wsHandleEvent(const std::string &json);

    // Methods
    void createSettingsScreen(lv_obj_t *parent);
    void createAuthScreen(lv_obj_t *parent);
    void createChatsScreen(lv_obj_t *parent);
    void createTopicsScreen(lv_obj_t *parent);
    void createChatScreen(lv_obj_t *parent);
    void createBridgeRulesScreen(lv_obj_t *parent);
    void createBridgeEditScreen(lv_obj_t *parent);

    void showScreen(Screen screen);
    void updateStatus();
    void loadBridgeRules();
    void saveBridgeRule();
    void deleteBridgeRule(int index);
    void populateBridgeChatDropdown();
    bool parseJsonBridgeRules(const std::string &json);
    void loadChats();
    void loadTopics(int64_t chatId);
    void loadMessages(int64_t chatId, int32_t topicId = 0);
    void sendMessage();
    void startAuth();
    void startQrAuth(bool forceRefresh = false);
    void pollQrAuth(uint32_t now_ms);
    void submitAuthInput();
    void showAuthQr(const char *qrUrl);
    void clearAuthQr();
    void switchToPasswordAuth();
    void finishAuth();

    // HTTP helpers
    std::string httpGet(const std::string &path);
    std::string httpPost(const std::string &path, const std::string &body);
    bool httpDelete(const std::string &path);
    bool parseJsonChats(const std::string &json);
    bool parseJsonTopics(const std::string &json);
    bool parseJsonMessages(const std::string &json);

    // Static LVGL callbacks
    static void onConnectBtnClicked(lv_event_t *e);
    static void onAuthSubmitClicked(lv_event_t *e);
    static void onChatItemClicked(lv_event_t *e);
    static void onTopicItemClicked(lv_event_t *e);
    static void onSendBtnClicked(lv_event_t *e);
    static void onBackBtnClicked(lv_event_t *e);
    static void onTextareaClicked(lv_event_t *e);
    static void onBridgeRuleItemClicked(lv_event_t *e);
    static void onBridgeAddBtnClicked(lv_event_t *e);
    static void onBridgeEditBtnClicked(lv_event_t *e);
    static void onBridgeDeleteBtnClicked(lv_event_t *e);
    static void onBridgeSaveBtnClicked(lv_event_t *e);
    static void onBridgeBtnClicked(lv_event_t *e);
};

#endif // HAS_CUSTOM_APPS
