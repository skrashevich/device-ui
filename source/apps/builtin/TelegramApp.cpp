#ifdef HAS_CUSTOM_APPS

#include "apps/builtin/TelegramApp.h"
#include "apps/AppContext.h"
#include "util/ILog.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#if __has_include(<HTTPClient.h>)
#include <HTTPClient.h>
#define HAS_HTTP_CLIENT 1
#endif

// ---------------------------------------------------------------------------
// JSON string escape helper (for user input in JSON bodies)
// ---------------------------------------------------------------------------

static std::string jsonEscape(const char *src)
{
    std::string out;
    out.reserve(strlen(src) + 16);
    while (*src) {
        switch (*src) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if ((unsigned char)*src < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)*src);
                out += buf;
            } else {
                out += *src;
            }
        }
        src++;
    }
    return out;
}

// ---------------------------------------------------------------------------
// int64_t to string helper (ESP32 newlib-nano doesn't support %lld)
// ---------------------------------------------------------------------------

static void int64ToStr(int64_t value, char *buf, size_t bufSize)
{
    if (bufSize < 2) { if (bufSize) buf[0] = '\0'; return; }
    if (value == 0) { buf[0] = '0'; buf[1] = '\0'; return; }

    bool neg = value < 0;
    uint64_t uval = neg ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;

    char tmp[21];
    int pos = 0;
    while (uval > 0) {
        tmp[pos++] = '0' + (int)(uval % 10);
        uval /= 10;
    }
    if (neg) tmp[pos++] = '-';

    size_t i = 0;
    while (pos > 0 && i + 1 < bufSize)
        buf[i++] = tmp[--pos];
    buf[i] = '\0';
}

// ---------------------------------------------------------------------------
// JSON helper: find matching '}' for '{' at *start, skipping strings
// ---------------------------------------------------------------------------

static const char *jsonFindObjectEnd(const char *start)
{
    if (!start || *start != '{')
        return nullptr;
    const char *p = start;
    int depth = 0;
    size_t scanned = 0;
    while (*p && scanned++ < 16384) {
        if (*p == '"') {
            p++; // skip opening quote
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++; // skip escaped char
                p++;
            }
            if (*p == '"') p++; // skip closing quote
            continue;
        }
        if (*p == '{') depth++;
        else if (*p == '}') { depth--; if (depth == 0) return p; }
        p++;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Simple JSON field extraction helpers (no ArduinoJson dependency)
// ---------------------------------------------------------------------------

/** Extract first string value for "key":"VALUE" from json, write into out[outLen]. */
static bool jsonExtractString(const char *json, const char *key, char *out, size_t outLen)
{
    // Build search pattern: "key":
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p)
        return false;
    p += strlen(pattern);

    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '"')
        return false;
    p++;

    size_t len = 0;
    while (*p && *p != '"') {
        char ch = *p++;
        if (ch == '\\' && *p) {
            char escaped = *p++;
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                ch = escaped;
                break;
            case 'b':
                ch = '\b';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            case 'u': {
                // Decode \uXXXX Unicode escape to UTF-8
                if (p[0] && p[1] && p[2] && p[3]) {
                    auto hexVal = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                        return -1;
                    };
                    int h0 = hexVal(p[0]), h1 = hexVal(p[1]), h2 = hexVal(p[2]), h3 = hexVal(p[3]);
                    if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
                        uint32_t codepoint = (h0 << 12) | (h1 << 8) | (h2 << 4) | h3;
                        p += 4;
                        // Handle UTF-16 surrogate pairs
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
                            p += 2; // skip \u
                            if (p[0] && p[1] && p[2] && p[3]) {
                                int l0 = hexVal(p[0]), l1 = hexVal(p[1]), l2 = hexVal(p[2]), l3 = hexVal(p[3]);
                                if (l0 >= 0 && l1 >= 0 && l2 >= 0 && l3 >= 0) {
                                    uint32_t low = (l0 << 12) | (l1 << 8) | (l2 << 4) | l3;
                                    if (low >= 0xDC00 && low <= 0xDFFF) {
                                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                                        p += 4;
                                    }
                                }
                            }
                        }
                        // Encode codepoint as UTF-8
                        if (codepoint < 0x80) {
                            if (len + 1 < outLen) out[len++] = (char)codepoint;
                        } else if (codepoint < 0x800) {
                            if (len + 2 < outLen) {
                                out[len++] = (char)(0xC0 | (codepoint >> 6));
                                out[len++] = (char)(0x80 | (codepoint & 0x3F));
                            }
                        } else if (codepoint < 0x10000) {
                            if (len + 3 < outLen) {
                                out[len++] = (char)(0xE0 | (codepoint >> 12));
                                out[len++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                                out[len++] = (char)(0x80 | (codepoint & 0x3F));
                            }
                        } else if (codepoint < 0x110000) {
                            if (len + 4 < outLen) {
                                out[len++] = (char)(0xF0 | (codepoint >> 18));
                                out[len++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
                                out[len++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                                out[len++] = (char)(0x80 | (codepoint & 0x3F));
                            }
                        }
                        continue;
                    }
                }
                ch = escaped;
                break;
            }
            default:
                ch = escaped;
                break;
            }
        }

        if (len + 1 < outLen)
            out[len++] = ch;
    }

    if (*p != '"')
        return false;
    out[len] = '\0';
    return true;
}

/** Extract first numeric value for "key":NUMBER from json. */
static bool jsonExtractInt64(const char *json, const char *key, int64_t *out)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p)
        return false;
    p += strlen(pattern);
    // skip whitespace
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '"') {
        // quoted number
        p++;
    }
    char *endptr = nullptr;
    *out = (int64_t)strtoll(p, &endptr, 10);
    return (endptr != p);
}

/** Extract first boolean value for "key":true/false from json. */
static bool jsonExtractBool(const char *json, const char *key, bool *out)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p)
        return false;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t')
        p++;
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

TelegramApp::TelegramApp() : serverUrl("http://192.168.1.100:8080") {}

TelegramApp::~TelegramApp() {}

// ---------------------------------------------------------------------------
// ICustomApp lifecycle
// ---------------------------------------------------------------------------

bool TelegramApp::init(AppContext *ctx)
{
    appContext = ctx;
    // Try to load saved server URL and API key
    if (appContext) {
        std::string saved = appContext->kvLoad("tg_server_url");
        if (!saved.empty())
            serverUrl = saved;
        std::string savedKey = appContext->kvLoad("tg_api_key");
        if (!savedKey.empty())
            apiKey = savedKey;
    }
    ILOG_INFO("TelegramApp initialized, server=%s", serverUrl.c_str());
    return true;
}

lv_obj_t *TelegramApp::createUI(lv_obj_t *parent)
{
    mainPanel = lv_obj_create(parent);
    lv_obj_set_size(mainPanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(mainPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(mainPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(mainPanel, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);

    createSettingsScreen(mainPanel);
    createAuthScreen(mainPanel);
    createChatsScreen(mainPanel);
    createTopicsScreen(mainPanel);
    createChatScreen(mainPanel);
    createBridgeRulesScreen(mainPanel);
    createBridgeEditScreen(mainPanel);

    showScreen(SCREEN_SETTINGS);
    return mainPanel;
}

void TelegramApp::onShow()
{
    ILOG_DEBUG("TelegramApp: onShow");
    updateStatus();
    // Reconnect WebSocket if we were connected before
    if (connState == CONN_CONNECTED && !wsConnected)
        wsConnect();
}

void TelegramApp::onHide()
{
    ILOG_DEBUG("TelegramApp: onHide");
    wsDisconnect();
}

void TelegramApp::onTick(uint32_t now_ms)
{
    // When in error/disconnected state, use backoff timer instead of poll interval
    if (connState == CONN_ERROR || connState == CONN_DISCONNECTED) {
        // Check 30-second server-unavailable threshold
        if (disconnectedSinceMs != 0 && (now_ms - disconnectedSinceMs) > 30000) {
            if (statusLabel) {
                lv_label_set_text(statusLabel, "Server unavailable");
                lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xFF3333), LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
        // Don't spam requests; wait for backoff interval
        if ((now_ms - lastRetryMs) < retryIntervalMs)
            return;
        lastRetryMs = now_ms;
        // Only auto-retry if we previously had a connection
        if (!connected)
            return;
    }

    if (connState != CONN_CONNECTED)
        return;

    // Poll WebSocket for push events (fast path – non-blocking)
    if (wsConnected) {
        wsPoll();
    } else {
        // Try to (re)connect WS with exponential backoff
        if ((now_ms - wsLastConnectMs) >= wsReconnectIntervalMs) {
            wsLastConnectMs = now_ms;
            wsConnect();
        }
    }

    if (authPanel && !lv_obj_has_flag(authPanel, LV_OBJ_FLAG_HIDDEN) && authStep == AUTH_QR)
        pollQrAuth(now_ms);

    if (now_ms - lastPollMs < POLL_INTERVAL_MS)
        return;
    lastPollMs = now_ms;

    // Refresh auth status periodically
    std::string statusJson = httpGet("/api/status");
    if (!statusJson.empty()) {
        bool wasAuthorized = authorized;
        jsonExtractBool(statusJson.c_str(), "authorized", &authorized);
        if (authorized && !wasAuthorized && currentScreen == SCREEN_SETTINGS) {
            showScreen(SCREEN_CHATS);
        }
        if (authStatusLabel) {
            lv_label_set_text(authStatusLabel, authorized ? "Auth: OK" : "Auth: needed");
        }
    } else {
        // Lost connection – also drop WS
        wsDisconnect();
        connected = false;
        connState = CONN_ERROR;
        if (disconnectedSinceMs == 0)
            disconnectedSinceMs = now_ms;
        updateStatus();
        return;
    }

    // When WS is active, skip HTTP polling for messages (push takes over).
    // Still poll chats list to get titles/unread counts.
    if (currentScreen == SCREEN_CHATS) {
        loadChats();
    } else if (currentScreen == SCREEN_CHAT && !wsConnected) {
        // Fallback: only HTTP-poll messages when WS is not available
        loadMessages(currentChatId, currentTopicId);
    }
}

void TelegramApp::onMeshPacket(const meshtastic_MeshPacket &p)
{
    if (connState != CONN_CONNECTED)
        return;

    if (p.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP || p.decoded.payload.size == 0)
        return;

    // Null-terminate payload safely
    char msg[meshtastic_Constants_DATA_PAYLOAD_LEN + 1];
    size_t len = p.decoded.payload.size;
    if (len > meshtastic_Constants_DATA_PAYLOAD_LEN)
        len = meshtastic_Constants_DATA_PAYLOAD_LEN;
    memcpy(msg, p.decoded.payload.bytes, len);
    msg[len] = '\0';

    // Format sender node as hex string
    char nodeName[18];
    snprintf(nodeName, sizeof(nodeName), "Node%08x", p.from);

    // Format channel as string
    char channelStr[4];
    snprintf(channelStr, sizeof(channelStr), "%u", (unsigned)p.channel);

    // Build JSON payload
    std::string escaped = jsonEscape(msg);
    std::string body = std::string("{\"node_name\":\"") + nodeName +
                       "\",\"channel\":\"" + channelStr +
                       "\",\"text\":\"" + escaped + "\"}";

    std::string resp = httpPost("/api/bridge/forward", body);
    if (!resp.empty()) {
        ILOG_INFO("TelegramApp: bridge forwarded packet from %s ch%s", nodeName, channelStr);
    } else {
        ILOG_WARN("TelegramApp: bridge forward failed for packet from %s", nodeName);
    }
}

void TelegramApp::destroy()
{
    wsDisconnect();
    connState = CONN_DISCONNECTED;
    connected = false;
    authorized = false;
    retryCount = 0;
    retryIntervalMs = 2000;
    disconnectedSinceMs = 0;
    lastError.clear();
    chats.clear();
    topics.clear();
    messages.clear();
    currentTopicId = 0;

    if (mainPanel) {
        lv_obj_del(mainPanel);
        mainPanel = nullptr;
    }
    settingsPanel = nullptr;
    authPanel = nullptr;
    chatsPanel = nullptr;
    chatPanel = nullptr;
    serverUrlTextarea = nullptr;
    apiKeyTextarea = nullptr;
    statusLabel = nullptr;
    connectBtn = nullptr;
    authStatusLabel = nullptr;
    authInput = nullptr;
    authPromptLabel = nullptr;
    authQrContainer = nullptr;
    authQrCode = nullptr;
    authQrStatusLabel = nullptr;
    authSubmitBtn = nullptr;
    authSubmitLabel = nullptr;
    chatsList = nullptr;
    topicsPanel = nullptr;
    topicsList = nullptr;
    topicsTitle = nullptr;
    chatTitle = nullptr;
    messagesList = nullptr;
    messageInput = nullptr;
    sendBtn = nullptr;
    bridgeRulesPanel = nullptr;
    bridgeRulesList = nullptr;
    bridgeEditPanel = nullptr;
    bridgeDirDropdown = nullptr;
    bridgeChDropdown = nullptr;
    bridgeChatDropdown = nullptr;
    bridgeFormatTextarea = nullptr;
    bridgeEnabledSwitch = nullptr;
    bridgeRules.clear();
    selectedBridgeRuleIndex = -1;
    ILOG_INFO("TelegramApp destroyed");
}

// ---------------------------------------------------------------------------
// Screen creation
// ---------------------------------------------------------------------------

void TelegramApp::createSettingsScreen(lv_obj_t *parent)
{
    settingsPanel = lv_obj_create(parent);
    lv_obj_set_size(settingsPanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(settingsPanel, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(settingsPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(settingsPanel, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(settingsPanel, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(settingsPanel, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(settingsPanel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Title
    lv_obj_t *title = lv_label_create(settingsPanel);
    lv_label_set_text(title, LV_SYMBOL_ENVELOPE " Telegram");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Server URL label
    lv_obj_t *urlLabel = lv_label_create(settingsPanel);
    lv_label_set_text(urlLabel, "Server URL:");

    // Server URL textarea
    serverUrlTextarea = lv_textarea_create(settingsPanel);
    lv_obj_set_width(serverUrlTextarea, LV_PCT(100));
    lv_textarea_set_one_line(serverUrlTextarea, true);
    lv_textarea_set_placeholder_text(serverUrlTextarea, "http://host:port");
    lv_textarea_set_text(serverUrlTextarea, serverUrl.c_str());
    lv_obj_set_user_data(serverUrlTextarea, this);
    lv_obj_add_event_cb(serverUrlTextarea, onTextareaClicked, LV_EVENT_CLICKED, this);

    // API Key label
    lv_obj_t *apiKeyLabel = lv_label_create(settingsPanel);
    lv_label_set_text(apiKeyLabel, "API Key (optional):");

    // API Key textarea
    apiKeyTextarea = lv_textarea_create(settingsPanel);
    lv_obj_set_width(apiKeyTextarea, LV_PCT(100));
    lv_textarea_set_one_line(apiKeyTextarea, true);
    lv_textarea_set_placeholder_text(apiKeyTextarea, "leave empty to disable auth");
    lv_textarea_set_text(apiKeyTextarea, apiKey.c_str());
    lv_obj_set_user_data(apiKeyTextarea, this);
    lv_obj_add_event_cb(apiKeyTextarea, onTextareaClicked, LV_EVENT_CLICKED, this);

    // Status row
    statusLabel = lv_label_create(settingsPanel);
    lv_label_set_text(statusLabel, "Status: disconnected");

    authStatusLabel = lv_label_create(settingsPanel);
    lv_label_set_text(authStatusLabel, "Auth: unknown");

    // Connect button
    connectBtn = lv_button_create(settingsPanel);
    lv_obj_set_width(connectBtn, LV_PCT(100));
    lv_obj_set_user_data(connectBtn, this);
    lv_obj_t *connectLabel = lv_label_create(connectBtn);
    lv_label_set_text(connectLabel, "Connect");
    lv_obj_center(connectLabel);
    lv_obj_add_event_cb(connectBtn, onConnectBtnClicked, LV_EVENT_CLICKED, this);

    // Bridge Rules button
    lv_obj_t *bridgeBtn = lv_button_create(settingsPanel);
    lv_obj_set_width(bridgeBtn, LV_PCT(100));
    lv_obj_set_user_data(bridgeBtn, this);
    lv_obj_t *bridgeBtnLabel = lv_label_create(bridgeBtn);
    lv_label_set_text(bridgeBtnLabel, LV_SYMBOL_SHUFFLE " Bridge Rules");
    lv_obj_center(bridgeBtnLabel);
    lv_obj_add_event_cb(bridgeBtn, onBridgeBtnClicked, LV_EVENT_CLICKED, this);
}

void TelegramApp::createAuthScreen(lv_obj_t *parent)
{
    authPanel = lv_obj_create(parent);
    lv_obj_set_size(authPanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(authPanel, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(authPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(authPanel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(authPanel, lv_color_hex(0x303030), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(authPanel, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(authPanel, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(authPanel, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(authPanel, LV_OBJ_FLAG_HIDDEN);

    // Prompt label
    authPromptLabel = lv_label_create(authPanel);
    lv_obj_set_width(authPromptLabel, LV_PCT(100));
    lv_label_set_long_mode(authPromptLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(authPromptLabel, "Scan this QR in Telegram > Settings > Devices > Link Desktop Device.");

    authQrContainer = lv_obj_create(authPanel);
    lv_obj_set_size(authQrContainer, 148, 148);
    lv_obj_set_style_pad_all(authQrContainer, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(authQrContainer, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(authQrContainer, lv_color_hex(0xF2F2F2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(authQrContainer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    authQrStatusLabel = lv_label_create(authPanel);
    lv_obj_set_width(authQrStatusLabel, LV_PCT(100));
    lv_label_set_long_mode(authQrStatusLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(authQrStatusLabel, "Requesting QR...");

    // Input
    authInput = lv_textarea_create(authPanel);
    lv_obj_set_width(authInput, LV_PCT(100));
    lv_textarea_set_one_line(authInput, true);
    lv_textarea_set_placeholder_text(authInput, "password");
    lv_obj_set_user_data(authInput, this);
    lv_obj_add_event_cb(authInput, onTextareaClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(authInput, LV_OBJ_FLAG_HIDDEN);

    // Submit button
    authSubmitBtn = lv_button_create(authPanel);
    lv_obj_set_width(authSubmitBtn, LV_PCT(100));
    lv_obj_set_user_data(authSubmitBtn, this);
    authSubmitLabel = lv_label_create(authSubmitBtn);
    lv_label_set_text(authSubmitLabel, "Refresh QR");
    lv_obj_center(authSubmitLabel);
    lv_obj_add_event_cb(authSubmitBtn, onAuthSubmitClicked, LV_EVENT_CLICKED, this);

    // Back button
    lv_obj_t *backBtn = lv_button_create(authPanel);
    lv_obj_set_width(backBtn, LV_PCT(100));
    lv_obj_set_user_data(backBtn, this);
    lv_obj_t *backLabel = lv_label_create(backBtn);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT " Back");
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(backBtn, onBackBtnClicked, LV_EVENT_CLICKED, this);
}

void TelegramApp::createChatsScreen(lv_obj_t *parent)
{
    chatsPanel = lv_obj_create(parent);
    lv_obj_set_size(chatsPanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(chatsPanel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(chatsPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chatsPanel, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(chatsPanel, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(chatsPanel, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(chatsPanel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(chatsPanel, LV_OBJ_FLAG_HIDDEN);

    // Title bar row
    lv_obj_t *titleBar = lv_obj_create(chatsPanel);
    lv_obj_set_size(titleBar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(titleBar, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(titleBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(titleBar, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(titleBar, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(titleBar, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_main_place(titleBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *chatsTitle = lv_label_create(titleBar);
    lv_label_set_text(chatsTitle, LV_SYMBOL_ENVELOPE " Chats");
    lv_obj_set_style_text_font(chatsTitle, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *backBtn = lv_button_create(titleBar);
    lv_obj_set_size(backBtn, 28, 22);
    lv_obj_set_user_data(backBtn, this);
    lv_obj_t *backLabel = lv_label_create(backBtn);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(backBtn, onBackBtnClicked, LV_EVENT_CLICKED, this);

    // Chats list
    chatsList = lv_list_create(chatsPanel);
    lv_obj_set_size(chatsList, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(chatsList, 1);
    lv_obj_set_style_pad_all(chatsList, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void TelegramApp::createTopicsScreen(lv_obj_t *parent)
{
    topicsPanel = lv_obj_create(parent);
    lv_obj_set_size(topicsPanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(topicsPanel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(topicsPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(topicsPanel, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(topicsPanel, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(topicsPanel, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(topicsPanel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(topicsPanel, LV_OBJ_FLAG_HIDDEN);

    // Title bar
    lv_obj_t *titleBar = lv_obj_create(topicsPanel);
    lv_obj_set_size(titleBar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(titleBar, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(titleBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(titleBar, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(titleBar, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(titleBar, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_main_place(titleBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN | LV_STATE_DEFAULT);

    topicsTitle = lv_label_create(titleBar);
    lv_label_set_text(topicsTitle, "Topics");
    lv_obj_set_style_text_font(topicsTitle, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *backBtn = lv_button_create(titleBar);
    lv_obj_set_size(backBtn, 28, 22);
    lv_obj_set_user_data(backBtn, this);
    lv_obj_t *backLabel = lv_label_create(backBtn);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(backBtn, onBackBtnClicked, LV_EVENT_CLICKED, this);

    // Topics list
    topicsList = lv_list_create(topicsPanel);
    lv_obj_set_size(topicsList, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(topicsList, 1);
    lv_obj_set_style_pad_all(topicsList, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void TelegramApp::createChatScreen(lv_obj_t *parent)
{
    chatPanel = lv_obj_create(parent);
    lv_obj_set_size(chatPanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(chatPanel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(chatPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chatPanel, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(chatPanel, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(chatPanel, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(chatPanel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(chatPanel, LV_OBJ_FLAG_HIDDEN);

    // Title bar
    lv_obj_t *titleBar = lv_obj_create(chatPanel);
    lv_obj_set_size(titleBar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(titleBar, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(titleBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(titleBar, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(titleBar, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(titleBar, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_main_place(titleBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN | LV_STATE_DEFAULT);

    chatTitle = lv_label_create(titleBar);
    lv_label_set_text(chatTitle, "");
    lv_obj_set_style_text_font(chatTitle, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *backBtn = lv_button_create(titleBar);
    lv_obj_set_size(backBtn, 28, 22);
    lv_obj_set_user_data(backBtn, this);
    lv_obj_t *backLabel = lv_label_create(backBtn);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(backBtn, onBackBtnClicked, LV_EVENT_CLICKED, this);

    // Messages list (grows to fill space)
    messagesList = lv_list_create(chatPanel);
    lv_obj_set_size(messagesList, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(messagesList, 1);
    lv_obj_set_style_pad_all(messagesList, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Input row at bottom
    lv_obj_t *inputRow = lv_obj_create(chatPanel);
    lv_obj_set_size(inputRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(inputRow, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(inputRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(inputRow, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(inputRow, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(inputRow, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(inputRow, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    messageInput = lv_textarea_create(inputRow);
    lv_obj_set_flex_grow(messageInput, 1);
    lv_obj_set_height(messageInput, 30);
    lv_textarea_set_one_line(messageInput, true);
    lv_textarea_set_placeholder_text(messageInput, "Message...");
    lv_obj_set_user_data(messageInput, this);
    lv_obj_add_event_cb(messageInput, onTextareaClicked, LV_EVENT_CLICKED, this);

    sendBtn = lv_button_create(inputRow);
    lv_obj_set_size(sendBtn, 36, 30);
    lv_obj_set_user_data(sendBtn, this);
    lv_obj_t *sendLabel = lv_label_create(sendBtn);
    lv_label_set_text(sendLabel, LV_SYMBOL_RIGHT);
    lv_obj_center(sendLabel);
    lv_obj_add_event_cb(sendBtn, onSendBtnClicked, LV_EVENT_CLICKED, this);
}

// ---------------------------------------------------------------------------
// Screen switching
// ---------------------------------------------------------------------------

void TelegramApp::showScreen(Screen screen)
{
    // Hide all
    if (settingsPanel)    lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
    if (authPanel)        lv_obj_add_flag(authPanel, LV_OBJ_FLAG_HIDDEN);
    if (chatsPanel)       lv_obj_add_flag(chatsPanel, LV_OBJ_FLAG_HIDDEN);
    if (topicsPanel)      lv_obj_add_flag(topicsPanel, LV_OBJ_FLAG_HIDDEN);
    if (chatPanel)        lv_obj_add_flag(chatPanel, LV_OBJ_FLAG_HIDDEN);
    if (bridgeRulesPanel) lv_obj_add_flag(bridgeRulesPanel, LV_OBJ_FLAG_HIDDEN);
    if (bridgeEditPanel)  lv_obj_add_flag(bridgeEditPanel, LV_OBJ_FLAG_HIDDEN);

    currentScreen = screen;

    switch (screen) {
    case SCREEN_SETTINGS:
        if (settingsPanel) lv_obj_remove_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
        break;
    case SCREEN_CHATS:
        if (chatsPanel) lv_obj_remove_flag(chatsPanel, LV_OBJ_FLAG_HIDDEN);
        if (connState == CONN_CONNECTED)
            loadChats();
        break;
    case SCREEN_TOPICS:
        if (topicsPanel) lv_obj_remove_flag(topicsPanel, LV_OBJ_FLAG_HIDDEN);
        break;
    case SCREEN_CHAT:
        if (chatPanel) lv_obj_remove_flag(chatPanel, LV_OBJ_FLAG_HIDDEN);
        break;
    case SCREEN_BRIDGE_RULES:
        if (bridgeRulesPanel) lv_obj_remove_flag(bridgeRulesPanel, LV_OBJ_FLAG_HIDDEN);
        if (connState == CONN_CONNECTED)
            loadBridgeRules();
        break;
    case SCREEN_BRIDGE_EDIT:
        if (bridgeEditPanel) lv_obj_remove_flag(bridgeEditPanel, LV_OBJ_FLAG_HIDDEN);
        populateBridgeChatDropdown();
        break;
    }
}

// ---------------------------------------------------------------------------
// Data operations
// ---------------------------------------------------------------------------

void TelegramApp::updateStatus()
{
    if (!statusLabel)
        return;

    switch (connState) {
    case CONN_DISCONNECTED:
        lv_label_set_text(statusLabel, "Disconnected");
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    case CONN_CONNECTING:
        lv_label_set_text(statusLabel, "Connecting...");
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xFFCC00), LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    case CONN_CONNECTED:
        lv_label_set_text(statusLabel, "Connected");
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x00CC44), LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    case CONN_ERROR: {
        std::string errText = "Error: " + (lastError.empty() ? "unknown" : lastError);
        lv_label_set_text(statusLabel, errText.c_str());
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xFF3333), LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    }
    }

    if (authStatusLabel)
        lv_label_set_text(authStatusLabel, authorized ? "Auth: OK" : "Auth: needed");
}

void TelegramApp::loadChats()
{
    std::string json = httpGet("/api/chats");
    if (json.empty())
        return;
    if (parseJsonChats(json)) {
        // Rebuild chats list widget
        if (!chatsList)
            return;
        // Remove old items
        lv_obj_clean(chatsList);
        for (size_t i = 0; i < chats.size(); i++) {
            char buf[256];
            if (chats[i].unread > 0)
                snprintf(buf, sizeof(buf), "[%d] %s", chats[i].unread, chats[i].title.c_str());
            else
                snprintf(buf, sizeof(buf), "%s", chats[i].title.c_str());

            lv_obj_t *item = lv_list_add_button(chatsList, nullptr, buf);
            lv_obj_set_user_data(item, (void *)(uintptr_t)i);
            lv_obj_add_event_cb(item, onChatItemClicked, LV_EVENT_CLICKED, this);
        }
    }
}

void TelegramApp::loadTopics(int64_t chatId)
{
    char idStr[24];
    int64ToStr(chatId, idStr, sizeof(idStr));
    char path[96];
    snprintf(path, sizeof(path), "/api/topics?chat_id=%s", idStr);
    std::string json = httpGet(path);
    if (json.empty())
        return;
    if (parseJsonTopics(json)) {
        if (!topicsList)
            return;
        lv_obj_clean(topicsList);
        for (size_t i = 0; i < topics.size(); i++) {
            char buf[256];
            if (topics[i].unread > 0)
                snprintf(buf, sizeof(buf), "[%d] %s", topics[i].unread, topics[i].title.c_str());
            else
                snprintf(buf, sizeof(buf), "%s", topics[i].title.c_str());

            lv_obj_t *item = lv_list_add_button(topicsList, nullptr, buf);
            lv_obj_set_user_data(item, (void *)(uintptr_t)i);
            lv_obj_add_event_cb(item, onTopicItemClicked, LV_EVENT_CLICKED, this);
        }
    }
}

void TelegramApp::loadMessages(int64_t chatId, int32_t topicId)
{
    char idStr[24];
    int64ToStr(chatId, idStr, sizeof(idStr));
    char path[128];
    if (topicId != 0) {
        char tidStr[16];
        snprintf(tidStr, sizeof(tidStr), "%d", topicId);
        snprintf(path, sizeof(path), "/api/messages?chat_id=%s&topic_id=%s", idStr, tidStr);
    } else {
        snprintf(path, sizeof(path), "/api/messages?chat_id=%s", idStr);
    }
    std::string json = httpGet(path);
    if (json.empty())
        return;
    if (parseJsonMessages(json)) {
        if (!messagesList)
            return;
        lv_obj_clean(messagesList);
        // Messages come newest-first from API, reverse for chronological display
        for (int i = (int)messages.size() - 1; i >= 0; i--) {
            const auto &msg = messages[i];
            char buf[512];
            if (msg.out)
                snprintf(buf, sizeof(buf), ">> %s", msg.text.c_str());
            else
                snprintf(buf, sizeof(buf), "%s: %s", msg.fromName.c_str(), msg.text.c_str());
            lv_list_add_text(messagesList, buf);
        }
        // Scroll to bottom
        lv_obj_scroll_to_y(messagesList, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

void TelegramApp::sendMessage()
{
    if (!messageInput || currentChatId == 0)
        return;
    const char *text = lv_textarea_get_text(messageInput);
    if (!text || text[0] == '\0')
        return;

    char path[64];
    snprintf(path, sizeof(path), "/api/send");
    char cidStr[24];
    int64ToStr(currentChatId, cidStr, sizeof(cidStr));
    std::string escaped = jsonEscape(text);
    std::string body;
    if (currentTopicId != 0)
        body = std::string("{\"chat_id\":") + cidStr + ",\"topic_id\":" + std::to_string(currentTopicId) + ",\"text\":\"" + escaped + "\"}";
    else
        body = std::string("{\"chat_id\":") + cidStr + ",\"text\":\"" + escaped + "\"}";

    std::string resp = httpPost(path, body);
    if (!resp.empty()) {
        lv_textarea_set_text(messageInput, "");
        loadMessages(currentChatId, currentTopicId);
    }
}

void TelegramApp::startAuth()
{
    if (authInput)
        lv_textarea_set_text(authInput, "");
    clearAuthQr();
    lastAuthPollMs = 0;

    // Hide all screens, then show only auth panel
    if (settingsPanel) lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
    if (chatsPanel)    lv_obj_add_flag(chatsPanel, LV_OBJ_FLAG_HIDDEN);
    if (chatPanel)     lv_obj_add_flag(chatPanel, LV_OBJ_FLAG_HIDDEN);
    if (authPanel)
        lv_obj_remove_flag(authPanel, LV_OBJ_FLAG_HIDDEN);

    startQrAuth(false);
}

void TelegramApp::showAuthQr(const char *qrUrl)
{
    clearAuthQr();
    if (!authQrContainer || !qrUrl || qrUrl[0] == '\0')
        return;

#if LV_USE_QRCODE
    lv_color_t fgColor = lv_palette_darken(LV_PALETTE_BLUE, 4);
    lv_color_t bgColor = lv_color_hex(0xF2F2F2);
    authQrCode = lv_qrcode_create(authQrContainer);
    lv_qrcode_set_size(authQrCode, 132);
    lv_qrcode_set_dark_color(authQrCode, fgColor);
    lv_qrcode_set_light_color(authQrCode, bgColor);
    lv_qrcode_update(authQrCode, qrUrl, strlen(qrUrl));
    lv_obj_center(authQrCode);
    lv_obj_set_style_border_color(authQrCode, fgColor, 0);
    lv_obj_set_style_border_width(authQrCode, 4, 0);
#endif
}

void TelegramApp::clearAuthQr()
{
    if (!authQrCode)
        return;
    lv_obj_del(authQrCode);
    authQrCode = nullptr;
}

void TelegramApp::finishAuth()
{
    authStep = AUTH_NONE;
    authorized = true;
    clearAuthQr();
    updateStatus();
    showScreen(SCREEN_CHATS);
}

void TelegramApp::switchToPasswordAuth()
{
    authStep = AUTH_2FA;
    clearAuthQr();

    if (authPromptLabel)
        lv_label_set_text(authPromptLabel, "QR confirmed. Enter Telegram 2FA password:");
    if (authQrContainer)
        lv_obj_add_flag(authQrContainer, LV_OBJ_FLAG_HIDDEN);
    if (authQrStatusLabel)
        lv_obj_add_flag(authQrStatusLabel, LV_OBJ_FLAG_HIDDEN);
    if (authInput) {
        lv_obj_remove_flag(authInput, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(authInput, "");
        lv_textarea_set_placeholder_text(authInput, "password");
    }
    if (authSubmitLabel)
        lv_label_set_text(authSubmitLabel, "Submit");
}

void TelegramApp::startQrAuth(bool forceRefresh)
{
    authStep = AUTH_QR;

    if (authPromptLabel)
        lv_label_set_text(authPromptLabel, "Scan this QR in Telegram > Settings > Devices > Link Desktop Device.");
    if (authQrContainer)
        lv_obj_remove_flag(authQrContainer, LV_OBJ_FLAG_HIDDEN);
    if (authQrStatusLabel) {
        lv_obj_remove_flag(authQrStatusLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(authQrStatusLabel, forceRefresh ? "Refreshing QR..." : "Requesting QR...");
    }
    if (authInput) {
        lv_textarea_set_text(authInput, "");
        lv_obj_add_flag(authInput, LV_OBJ_FLAG_HIDDEN);
    }
    if (authSubmitLabel)
        lv_label_set_text(authSubmitLabel, "Refresh QR");
    clearAuthQr();

    const char *body = forceRefresh ? "{\"force\":true}" : "{}";
    std::string resp = httpPost("/api/auth/qr/start", body);
    if (resp.empty()) {
        if (authQrStatusLabel)
            lv_label_set_text(authQrStatusLabel, "Unable to start QR login. Tap Refresh QR.");
        return;
    }

    char status[32] = {};
    char qrUrl[768] = {};
    char instruction[160] = {};
    jsonExtractString(resp.c_str(), "status", status, sizeof(status));
    jsonExtractString(resp.c_str(), "qr_url", qrUrl, sizeof(qrUrl));
    jsonExtractString(resp.c_str(), "instruction", instruction, sizeof(instruction));

    if (strcmp(status, "authorized") == 0) {
        finishAuth();
        return;
    }

    if (strcmp(status, "2fa_required") == 0) {
        switchToPasswordAuth();
        return;
    }

    if (strcmp(status, "pending") == 0) {
        if (instruction[0] != '\0' && authPromptLabel)
            lv_label_set_text(authPromptLabel, instruction);
        if (qrUrl[0] != '\0')
            showAuthQr(qrUrl);
        if (authQrStatusLabel)
            lv_label_set_text(authQrStatusLabel, "Waiting for scan...");
        return;
    }

    if (strcmp(status, "expired") == 0) {
        if (authQrStatusLabel)
            lv_label_set_text(authQrStatusLabel, "QR expired. Tap Refresh QR.");
        return;
    }

    if (authQrStatusLabel)
        lv_label_set_text(authQrStatusLabel, "QR login failed. Tap Refresh QR.");
}

void TelegramApp::pollQrAuth(uint32_t now_ms)
{
    if ((now_ms - lastAuthPollMs) < AUTH_POLL_INTERVAL_MS)
        return;
    lastAuthPollMs = now_ms;

    std::string resp = httpGet("/api/auth/qr/status");
    if (resp.empty())
        return;

    char status[32] = {};
    char qrUrl[768] = {};
    char instruction[160] = {};
    jsonExtractString(resp.c_str(), "status", status, sizeof(status));
    jsonExtractString(resp.c_str(), "qr_url", qrUrl, sizeof(qrUrl));
    jsonExtractString(resp.c_str(), "instruction", instruction, sizeof(instruction));

    if (strcmp(status, "authorized") == 0) {
        finishAuth();
        return;
    }

    if (strcmp(status, "2fa_required") == 0) {
        switchToPasswordAuth();
        return;
    }

    if (strcmp(status, "pending") == 0) {
        if (instruction[0] != '\0' && authPromptLabel)
            lv_label_set_text(authPromptLabel, instruction);
        if (qrUrl[0] != '\0' && !authQrCode)
            showAuthQr(qrUrl);
        if (authQrStatusLabel)
            lv_label_set_text(authQrStatusLabel, "Waiting for scan...");
        return;
    }

    if (strcmp(status, "expired") == 0) {
        startQrAuth(true);
        return;
    }

    if (strcmp(status, "error") == 0 && authQrStatusLabel)
        lv_label_set_text(authQrStatusLabel, "QR login failed. Tap Refresh QR.");
}

void TelegramApp::submitAuthInput()
{
    switch (authStep) {
    case AUTH_QR:
        startQrAuth(true);
        break;
    case AUTH_2FA:
        if (!authInput)
            return;
        {
            const char *input = lv_textarea_get_text(authInput);
            if (!input || input[0] == '\0')
                return;

            std::string escaped = jsonEscape(input);
            std::string body = std::string("{\"password\":\"") + escaped + "\"}";
            std::string resp = httpPost("/api/auth/2fa", body);
            if (!resp.empty()) {
                char respStatus[32] = {};
                jsonExtractString(resp.c_str(), "status", respStatus, sizeof(respStatus));
                if (strcmp(respStatus, "authorized") == 0) {
                    finishAuth();
                } else {
                    // Server returned non-authorized status (wrong password)
                    if (authPromptLabel)
                        lv_label_set_text(authPromptLabel, "Wrong 2FA password. Try again:");
                    lv_textarea_set_text(authInput, "");
                }
            } else {
                if (authPromptLabel)
                    lv_label_set_text(authPromptLabel, "Wrong 2FA password. Try again:");
                lv_textarea_set_text(authInput, "");
            }
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

std::string TelegramApp::httpGet(const std::string &path)
{
    if (serverUrl.empty())
        return "";

#ifdef HAS_HTTP_CLIENT
    HTTPClient http;
    std::string url = serverUrl + path;
    http.begin(url.c_str());
    http.setTimeout(5000);
    if (!apiKey.empty())
        http.addHeader("X-API-Key", apiKey.c_str());
    int code = http.GET();
    if (code == 200) {
        std::string body = http.getString().c_str();
        http.end();
        retryCount = 0;
        retryIntervalMs = 2000;
        return body;
    }
    http.end();
    ILOG_WARN("TelegramApp: GET %s -> %d", path.c_str(), code);
    retryCount++;
    retryIntervalMs = retryIntervalMs * 2 > 8000 ? 8000 : retryIntervalMs * 2;
    if (retryCount >= MAX_RETRIES) {
        connState = CONN_ERROR;
        char errBuf[48];
        snprintf(errBuf, sizeof(errBuf), "Error: HTTP %d", code);
        lastError = errBuf;
        if (statusLabel)
            lv_label_set_text(statusLabel, lastError.c_str());
    }
#endif
    return "";
}

std::string TelegramApp::httpPost(const std::string &path, const std::string &body)
{
    if (serverUrl.empty())
        return "";

#ifdef HAS_HTTP_CLIENT
    HTTPClient http;
    std::string url = serverUrl + path;
    http.begin(url.c_str());
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    if (!apiKey.empty())
        http.addHeader("X-API-Key", apiKey.c_str());
    int code = http.POST(body.c_str());
    if (code == 200 || code == 201) {
        std::string resp = http.getString().c_str();
        http.end();
        retryCount = 0;
        retryIntervalMs = 2000;
        return resp;
    }
    http.end();
    ILOG_WARN("TelegramApp: POST %s -> %d", path.c_str(), code);
    retryCount++;
    retryIntervalMs = retryIntervalMs * 2 > 8000 ? 8000 : retryIntervalMs * 2;
    if (retryCount >= MAX_RETRIES) {
        connState = CONN_ERROR;
        char errBuf[48];
        snprintf(errBuf, sizeof(errBuf), "Error: HTTP %d", code);
        lastError = errBuf;
        if (statusLabel)
            lv_label_set_text(statusLabel, lastError.c_str());
    }
#endif
    return "";
}

bool TelegramApp::httpDelete(const std::string &path)
{
    if (serverUrl.empty())
        return false;

#ifdef HAS_HTTP_CLIENT
    HTTPClient http;
    std::string url = serverUrl + path;
    http.begin(url.c_str());
    http.setTimeout(5000);
    if (!apiKey.empty())
        http.addHeader("X-API-Key", apiKey.c_str());
    int code = http.sendRequest("DELETE");
    http.end();
    if (code == 200 || code == 204) {
        retryCount = 0;
        retryIntervalMs = 2000;
        return true;
    }
    ILOG_WARN("TelegramApp: DELETE %s -> %d", path.c_str(), code);
#endif
    return false;
}

// ---------------------------------------------------------------------------
// WebSocket client (minimal raw implementation for ESP32)
// ---------------------------------------------------------------------------

#ifdef HAS_HTTP_CLIENT
#include <WiFiClient.h>
#include <base64.h>

// Parse "http://host:port" or "https://host:port" from serverUrl into host/port.
static bool parseHostPort(const std::string &url, std::string &host, uint16_t &port)
{
    // Strip scheme
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        return false;
    std::string rest = url.substr(schemeEnd + 3);

    size_t colon = rest.find(':');
    if (colon != std::string::npos) {
        host = rest.substr(0, colon);
        port = (uint16_t)atoi(rest.substr(colon + 1).c_str());
    } else {
        host = rest;
        // Remove trailing path if any
        size_t slash = host.find('/');
        if (slash != std::string::npos)
            host = host.substr(0, slash);
        port = 80;
    }
    return !host.empty();
}
#endif

void TelegramApp::wsConnect()
{
#ifdef HAS_HTTP_CLIENT
    if (wsConnected)
        return;
    if (serverUrl.empty())
        return;

    std::string host;
    uint16_t port = 80;
    if (!parseHostPort(serverUrl, host, port)) {
        ILOG_WARN("TelegramApp WS: cannot parse server URL");
        return;
    }

    WiFiClient *client = new WiFiClient();
    if (!client->connect(host.c_str(), port)) {
        ILOG_WARN("TelegramApp WS: TCP connect failed to %s:%d", host.c_str(), port);
        delete client;
        return;
    }

    // WebSocket upgrade handshake
    // Key: 16 bytes base64-encoded (use a fixed key – server doesn't verify Accept)
    const char *wsKey = "dGhlIHNhbXBsZSBub25jZQ=="; // RFC 6455 example key
    std::string req;
    req += "GET /api/updates HTTP/1.1\r\n";
    req += "Host: ";
    req += host;
    req += "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: ";
    req += wsKey;
    req += "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    if (!apiKey.empty()) {
        req += "X-API-Key: ";
        req += apiKey;
        req += "\r\n";
    }
    req += "\r\n";

    client->print(req.c_str());

    // Read HTTP response headers (wait up to 3 seconds)
    uint32_t deadline = millis() + 3000;
    std::string headers;
    bool upgraded = false;
    while (millis() < deadline) {
        while (client->available()) {
            char c = (char)client->read();
            headers += c;
            // Detect end of headers
            if (headers.size() >= 4 &&
                headers.substr(headers.size() - 4) == "\r\n\r\n") {
                // Check for 101 Switching Protocols
                if (headers.find("101") != std::string::npos ||
                    headers.find("Upgrade") != std::string::npos) {
                    upgraded = true;
                }
                goto done_headers;
            }
        }
        delay(10);
    }
done_headers:
    if (!upgraded) {
        ILOG_WARN("TelegramApp WS: upgrade failed, headers: %.80s", headers.c_str());
        client->stop();
        delete client;
        wsReconnectCount++;
        wsReconnectIntervalMs = wsReconnectIntervalMs * 2 > 30000 ? 30000 : wsReconnectIntervalMs * 2;
        return;
    }

    wifiClient = (void *)client;
    wsConnected = true;
    wsReadBuf.clear();
    wsReconnectCount = 0;
    wsReconnectIntervalMs = 2000;
    ILOG_INFO("TelegramApp WS: connected to %s:%d/api/updates", host.c_str(), port);
#endif
}

void TelegramApp::wsDisconnect()
{
#ifdef HAS_HTTP_CLIENT
    if (wifiClient) {
        WiFiClient *client = (WiFiClient *)wifiClient;
        client->stop();
        delete client;
        wifiClient = nullptr;
    }
#endif
    wsConnected = false;
    wsReadBuf.clear();
}

void TelegramApp::wsPoll()
{
#ifdef HAS_HTTP_CLIENT
    if (!wsConnected || !wifiClient)
        return;

    WiFiClient *client = (WiFiClient *)wifiClient;

    if (!client->connected()) {
        ILOG_WARN("TelegramApp WS: connection dropped");
        wsDisconnect();
        return;
    }

    // Read all available bytes into wsReadBuf
    while (client->available()) {
        wsReadBuf += (char)client->read();
    }

    // Process complete WebSocket frames from wsReadBuf
    // WebSocket frame format (simplified, text frames only, no masking from server):
    //   byte 0: FIN(1) + RSV(3) + opcode(4)
    //   byte 1: MASK(1) + payload_len(7)  [mask bit should be 0 from server]
    //   if payload_len == 126: next 2 bytes = extended length
    //   if payload_len == 127: next 8 bytes = extended length (not handled here)
    //   payload bytes

    while (wsReadBuf.size() >= 2) {
        const uint8_t *buf = (const uint8_t *)wsReadBuf.data();
        uint8_t opcode = buf[0] & 0x0F;
        bool fin = (buf[0] & 0x80) != 0;
        bool masked = (buf[1] & 0x80) != 0;
        size_t payloadLen = buf[1] & 0x7F;
        size_t headerLen = 2;

        if (payloadLen == 126) {
            if (wsReadBuf.size() < 4)
                break; // wait for more data
            payloadLen = ((size_t)buf[2] << 8) | buf[3];
            headerLen = 4;
        } else if (payloadLen == 127) {
            // Very large frame – skip (not expected from our server)
            ILOG_WARN("TelegramApp WS: oversized frame, disconnecting");
            wsDisconnect();
            return;
        }

        if (masked)
            headerLen += 4; // skip mask bytes (server should not mask, but handle gracefully)

        if (wsReadBuf.size() < headerLen + payloadLen)
            break; // wait for complete frame

        if (opcode == 0x8) {
            // Connection close frame
            ILOG_INFO("TelegramApp WS: received close frame");
            wsDisconnect();
            return;
        }

        if (opcode == 0x9) {
            // Ping – send pong (opcode 0xA), no payload
            uint8_t pong[2] = {0x8A, 0x00};
            client->write(pong, 2);
        } else if (opcode == 0x1 && fin) {
            // Text frame (complete)
            std::string payload;
            if (masked) {
                const uint8_t *mask = buf + headerLen - 4;
                payload.resize(payloadLen);
                for (size_t i = 0; i < payloadLen; i++)
                    payload[i] = (char)(buf[headerLen + i] ^ mask[i % 4]);
            } else {
                payload.assign((const char *)(buf + headerLen), payloadLen);
            }
            wsHandleEvent(payload);
        }
        // else: binary/continuation frames ignored

        wsReadBuf.erase(0, headerLen + payloadLen);
    }
#endif
}

void TelegramApp::wsHandleEvent(const std::string &json)
{
    if (json.empty())
        return;

    char eventType[32] = {};
    jsonExtractString(json.c_str(), "type", eventType, sizeof(eventType));

    if (strcmp(eventType, "new_message") == 0) {
        int64_t chatId = 0;
        jsonExtractInt64(json.c_str(), "chat_id", &chatId);
        ILOG_DEBUG("TelegramApp WS: new_message in chat %lld", (long long)chatId);

        // If we're currently viewing this chat, reload messages immediately
        if (currentScreen == SCREEN_CHAT && currentChatId == chatId) {
            loadMessages(currentChatId, currentTopicId);
        }
        // Update unread badge in chats list if present
        for (auto &chat : chats) {
            if (chat.id == chatId) {
                chat.unread++;
                break;
            }
        }
    } else if (strcmp(eventType, "bridge_message") == 0) {
        // Forward to mesh network via appContext
        if (appContext) {
            char meshChannel[8] = {};
            jsonExtractString(json.c_str(), "mesh_channel", meshChannel, sizeof(meshChannel));
            char text[256] = {};
            jsonExtractString(json.c_str(), "text", text, sizeof(text));
            uint8_t channel = (uint8_t)atoi(meshChannel);
            ILOG_INFO("TelegramApp WS: bridge_message -> mesh ch%d: %s", channel, text);
            appContext->broadcastMessage(channel, text);
        }
    }
}

// ---------------------------------------------------------------------------
// JSON parsing
// ---------------------------------------------------------------------------

bool TelegramApp::parseJsonChats(const std::string &json)
{
    chats.clear();
    if (json.empty()) {
        ILOG_WARN("TelegramApp: parseJsonChats: empty input");
        return false;
    }
    const char *p = json.c_str();

    // Check for error response {"error": "..."}
    if (strstr(p, "\"error\"")) {
        char errMsg[128];
        if (jsonExtractString(p, "error", errMsg, sizeof(errMsg)))
            ILOG_WARN("TelegramApp: chats API error: %s", errMsg);
        return false;
    }

    // Expect array: [{ ... }, { ... }]
    p = strchr(p, '[');
    if (!p)
        return false;
    p++;

    const size_t MAX_CHATS = 100;
    while (*p && chats.size() < MAX_CHATS) {
        // Find next object start
        const char *objStart = strchr(p, '{');
        if (!objStart)
            break;
        const char *objEnd = jsonFindObjectEnd(objStart);
        if (!objEnd)
            break;

        // Extract object as substring
        size_t objLen = (size_t)(objEnd - objStart) + 1;
        char obj[512];
        if (objLen >= sizeof(obj)) objLen = sizeof(obj) - 1;
        memcpy(obj, objStart, objLen);
        obj[objLen] = '\0';

        TgChat chat;
        chat.id = 0;
        chat.unread = 0;
        chat.lastDate = 0;
        chat.isForum = false;

        int64_t id64 = 0;
        if (jsonExtractInt64(obj, "id", &id64))
            chat.id = id64;

        char tmp[256];
        if (jsonExtractString(obj, "title", tmp, sizeof(tmp)))
            chat.title = tmp;
        if (jsonExtractString(obj, "type", tmp, sizeof(tmp)))
            chat.type = tmp;
        if (jsonExtractString(obj, "last_message", tmp, sizeof(tmp)))
            chat.lastMessage = tmp;

        jsonExtractBool(obj, "is_forum", &chat.isForum);

        int64_t unread = 0;
        if (jsonExtractInt64(obj, "unread", &unread))
            chat.unread = (int)unread;

        int64_t lastDate = 0;
        if (jsonExtractInt64(obj, "last_date", &lastDate))
            chat.lastDate = (uint32_t)lastDate;

        if (chat.id != 0)
            chats.push_back(chat);

        p = objEnd + 1;
    }
    return !chats.empty();
}

bool TelegramApp::parseJsonTopics(const std::string &json)
{
    topics.clear();
    if (json.empty())
        return false;
    const char *p = json.c_str();

    if (strstr(p, "\"error\"")) {
        char errMsg[128];
        if (jsonExtractString(p, "error", errMsg, sizeof(errMsg)))
            ILOG_WARN("TelegramApp: topics API error: %s", errMsg);
        return false;
    }

    p = strchr(p, '[');
    if (!p)
        return false;
    p++;

    const size_t MAX_TOPICS = 50;
    while (*p && topics.size() < MAX_TOPICS) {
        const char *objStart = strchr(p, '{');
        if (!objStart)
            break;
        const char *objEnd = jsonFindObjectEnd(objStart);
        if (!objEnd)
            break;

        size_t objLen = (size_t)(objEnd - objStart) + 1;
        char obj[512];
        if (objLen >= sizeof(obj)) objLen = sizeof(obj) - 1;
        memcpy(obj, objStart, objLen);
        obj[objLen] = '\0';

        TgTopic topic;
        topic.id = 0;
        topic.unread = 0;

        int64_t id64 = 0;
        if (jsonExtractInt64(obj, "id", &id64))
            topic.id = (int32_t)id64;

        char tmp[256];
        if (jsonExtractString(obj, "title", tmp, sizeof(tmp)))
            topic.title = tmp;

        int64_t unread = 0;
        if (jsonExtractInt64(obj, "unread", &unread))
            topic.unread = (int)unread;

        if (topic.id != 0)
            topics.push_back(topic);

        p = objEnd + 1;
    }
    return !topics.empty();
}

bool TelegramApp::parseJsonMessages(const std::string &json)
{
    messages.clear();
    if (json.empty()) {
        ILOG_WARN("TelegramApp: parseJsonMessages: empty input");
        return false;
    }
    const char *p = json.c_str();

    // Check for error response {"error": "..."}
    if (strstr(p, "\"error\"")) {
        char errMsg[128];
        if (jsonExtractString(p, "error", errMsg, sizeof(errMsg)))
            ILOG_WARN("TelegramApp: messages API error: %s", errMsg);
        return false;
    }

    p = strchr(p, '[');
    if (!p)
        return false;
    p++;

    const size_t MAX_MESSAGES = 100;
    while (*p && messages.size() < MAX_MESSAGES) {
        const char *objStart = strchr(p, '{');
        if (!objStart)
            break;
        const char *objEnd = jsonFindObjectEnd(objStart);
        if (!objEnd)
            break;

        size_t objLen = (size_t)(objEnd - objStart) + 1;
        char obj[512];
        if (objLen >= sizeof(obj)) objLen = sizeof(obj) - 1;
        memcpy(obj, objStart, objLen);
        obj[objLen] = '\0';

        TgMessage msg;
        msg.id = 0;
        msg.date = 0;
        msg.out = false;

        int64_t id64 = 0;
        if (jsonExtractInt64(obj, "id", &id64))
            msg.id = (int32_t)id64;

        char tmp[256];
        if (jsonExtractString(obj, "from_name", tmp, sizeof(tmp)))
            msg.fromName = tmp;
        if (jsonExtractString(obj, "text", tmp, sizeof(tmp)))
            msg.text = tmp;

        int64_t date = 0;
        if (jsonExtractInt64(obj, "date", &date))
            msg.date = (uint32_t)date;

        jsonExtractBool(obj, "out", &msg.out);

        if (msg.id != 0 || !msg.text.empty())
            messages.push_back(msg);

        p = objEnd + 1;
    }
    return !messages.empty();
}

// ---------------------------------------------------------------------------
// Bridge Rules screen
// ---------------------------------------------------------------------------

void TelegramApp::createBridgeRulesScreen(lv_obj_t *parent)
{
    bridgeRulesPanel = lv_obj_create(parent);
    lv_obj_set_size(bridgeRulesPanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(bridgeRulesPanel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bridgeRulesPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bridgeRulesPanel, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(bridgeRulesPanel, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(bridgeRulesPanel, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bridgeRulesPanel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(bridgeRulesPanel, LV_OBJ_FLAG_HIDDEN);

    // Title bar
    lv_obj_t *titleBar = lv_obj_create(bridgeRulesPanel);
    lv_obj_set_size(titleBar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(titleBar, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(titleBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(titleBar, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(titleBar, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(titleBar, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_main_place(titleBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title = lv_label_create(titleBar);
    lv_label_set_text(title, LV_SYMBOL_SHUFFLE " Bridge Rules");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *backBtn = lv_button_create(titleBar);
    lv_obj_set_size(backBtn, 28, 22);
    lv_obj_set_user_data(backBtn, this);
    lv_obj_t *backLabel = lv_label_create(backBtn);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(backBtn, onBackBtnClicked, LV_EVENT_CLICKED, this);

    // Rules list
    bridgeRulesList = lv_list_create(bridgeRulesPanel);
    lv_obj_set_size(bridgeRulesList, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(bridgeRulesList, 1);
    lv_obj_set_style_pad_all(bridgeRulesList, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Action buttons row
    lv_obj_t *btnRow = lv_obj_create(bridgeRulesPanel);
    lv_obj_set_size(btnRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(btnRow, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btnRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(btnRow, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(btnRow, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(btnRow, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *addBtn = lv_button_create(btnRow);
    lv_obj_set_flex_grow(addBtn, 1);
    lv_obj_set_user_data(addBtn, this);
    lv_obj_t *addLabel = lv_label_create(addBtn);
    lv_label_set_text(addLabel, LV_SYMBOL_PLUS " Add");
    lv_obj_center(addLabel);
    lv_obj_add_event_cb(addBtn, onBridgeAddBtnClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *editBtn = lv_button_create(btnRow);
    lv_obj_set_flex_grow(editBtn, 1);
    lv_obj_set_user_data(editBtn, this);
    lv_obj_t *editLabel = lv_label_create(editBtn);
    lv_label_set_text(editLabel, LV_SYMBOL_EDIT " Edit");
    lv_obj_center(editLabel);
    lv_obj_add_event_cb(editBtn, onBridgeEditBtnClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *delBtn = lv_button_create(btnRow);
    lv_obj_set_flex_grow(delBtn, 1);
    lv_obj_set_user_data(delBtn, this);
    lv_obj_t *delLabel = lv_label_create(delBtn);
    lv_label_set_text(delLabel, LV_SYMBOL_TRASH " Del");
    lv_obj_center(delLabel);
    lv_obj_add_event_cb(delBtn, onBridgeDeleteBtnClicked, LV_EVENT_CLICKED, this);
}

void TelegramApp::createBridgeEditScreen(lv_obj_t *parent)
{
    bridgeEditPanel = lv_obj_create(parent);
    lv_obj_set_size(bridgeEditPanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(bridgeEditPanel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bridgeEditPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bridgeEditPanel, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(bridgeEditPanel, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(bridgeEditPanel, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bridgeEditPanel, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(bridgeEditPanel, LV_OBJ_FLAG_HIDDEN);

    // Title bar
    lv_obj_t *titleBar = lv_obj_create(bridgeEditPanel);
    lv_obj_set_size(titleBar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(titleBar, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(titleBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(titleBar, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(titleBar, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(titleBar, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_main_place(titleBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title = lv_label_create(titleBar);
    lv_label_set_text(title, "Edit Rule");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *backBtn = lv_button_create(titleBar);
    lv_obj_set_size(backBtn, 28, 22);
    lv_obj_set_user_data(backBtn, this);
    lv_obj_t *backLabel = lv_label_create(backBtn);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(backBtn, onBackBtnClicked, LV_EVENT_CLICKED, this);

    // Direction
    lv_obj_t *dirLabel = lv_label_create(bridgeEditPanel);
    lv_label_set_text(dirLabel, "Direction:");

    bridgeDirDropdown = lv_dropdown_create(bridgeEditPanel);
    lv_obj_set_width(bridgeDirDropdown, LV_PCT(100));
    lv_dropdown_set_options(bridgeDirDropdown, "mesh -> tg\ntg -> mesh\nboth");

    // Mesh channel
    lv_obj_t *chLabel = lv_label_create(bridgeEditPanel);
    lv_label_set_text(chLabel, "Mesh channel:");

    bridgeChDropdown = lv_dropdown_create(bridgeEditPanel);
    lv_obj_set_width(bridgeChDropdown, LV_PCT(100));
    lv_dropdown_set_options(bridgeChDropdown, "Ch 0\nCh 1\nCh 2\nCh 3\nCh 4\nCh 5\nCh 6\nCh 7");

    // Telegram chat
    lv_obj_t *chatLabel = lv_label_create(bridgeEditPanel);
    lv_label_set_text(chatLabel, "Telegram chat:");

    bridgeChatDropdown = lv_dropdown_create(bridgeEditPanel);
    lv_obj_set_width(bridgeChatDropdown, LV_PCT(100));
    lv_dropdown_set_options(bridgeChatDropdown, "(no chats loaded)");

    // Format template
    lv_obj_t *fmtLabel = lv_label_create(bridgeEditPanel);
    lv_label_set_text(fmtLabel, "Format (optional):");

    bridgeFormatTextarea = lv_textarea_create(bridgeEditPanel);
    lv_obj_set_width(bridgeFormatTextarea, LV_PCT(100));
    lv_textarea_set_one_line(bridgeFormatTextarea, true);
    lv_textarea_set_placeholder_text(bridgeFormatTextarea, "{from}: {text}");
    lv_obj_set_user_data(bridgeFormatTextarea, this);
    lv_obj_add_event_cb(bridgeFormatTextarea, onTextareaClicked, LV_EVENT_CLICKED, this);

    // Enabled row
    lv_obj_t *enableRow = lv_obj_create(bridgeEditPanel);
    lv_obj_set_size(enableRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(enableRow, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(enableRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(enableRow, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(enableRow, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(enableRow, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_cross_place(enableRow, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(enableRow, 6, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *enableLabel = lv_label_create(enableRow);
    lv_label_set_text(enableLabel, "Enabled:");

    bridgeEnabledSwitch = lv_switch_create(enableRow);
    lv_obj_add_state(bridgeEnabledSwitch, LV_STATE_CHECKED);

    // Save/Cancel row
    lv_obj_t *actionRow = lv_obj_create(bridgeEditPanel);
    lv_obj_set_size(actionRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(actionRow, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(actionRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(actionRow, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(actionRow, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(actionRow, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(actionRow, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *saveBtn = lv_button_create(actionRow);
    lv_obj_set_flex_grow(saveBtn, 1);
    lv_obj_set_user_data(saveBtn, this);
    lv_obj_t *saveLabel = lv_label_create(saveBtn);
    lv_label_set_text(saveLabel, LV_SYMBOL_OK " Save");
    lv_obj_center(saveLabel);
    lv_obj_add_event_cb(saveBtn, onBridgeSaveBtnClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *cancelBtn = lv_button_create(actionRow);
    lv_obj_set_flex_grow(cancelBtn, 1);
    lv_obj_set_user_data(cancelBtn, this);
    lv_obj_t *cancelLabel = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLabel, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_center(cancelLabel);
    lv_obj_add_event_cb(cancelBtn, onBackBtnClicked, LV_EVENT_CLICKED, this);
}

// ---------------------------------------------------------------------------
// Bridge data operations
// ---------------------------------------------------------------------------

void TelegramApp::loadBridgeRules()
{
    std::string json = httpGet("/api/bridge/rules");
    if (json.empty())
        return;
    if (!parseJsonBridgeRules(json))
        return;
    if (!bridgeRulesList)
        return;
    lv_obj_clean(bridgeRulesList);
    for (size_t i = 0; i < bridgeRules.size(); i++) {
        const auto &rule = bridgeRules[i];
        char buf[100];
        const char *dirStr = "?";
        if (rule.direction == "mesh_to_telegram")      dirStr = "mesh->tg";
        else if (rule.direction == "telegram_to_mesh") dirStr = "tg->mesh";
        else if (rule.direction == "both")        dirStr = "both";
        snprintf(buf, sizeof(buf), "%s: Ch%d <-> %.30s %s",
                 dirStr, rule.meshChannel,
                 rule.tgChatTitle.empty() ? "?" : rule.tgChatTitle.c_str(),
                 rule.enabled ? "[ON]" : "[OFF]");
        lv_obj_t *item = lv_list_add_button(bridgeRulesList, nullptr, buf);
        lv_obj_set_user_data(item, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(item, onBridgeRuleItemClicked, LV_EVENT_CLICKED, this);
    }
}

void TelegramApp::populateBridgeChatDropdown()
{
    if (!bridgeChatDropdown)
        return;
    if (chats.empty()) {
        lv_dropdown_set_options(bridgeChatDropdown, "(no chats loaded)");
        return;
    }
    std::string opts;
    for (size_t i = 0; i < chats.size(); i++) {
        if (i > 0) opts += '\n';
        opts += chats[i].title;
    }
    lv_dropdown_set_options(bridgeChatDropdown, opts.c_str());

    if (selectedBridgeRuleIndex >= 0 && (size_t)selectedBridgeRuleIndex < bridgeRules.size()) {
        int64_t ruleChat = bridgeRules[selectedBridgeRuleIndex].telegramChatId;
        for (size_t i = 0; i < chats.size(); i++) {
            if (chats[i].id == ruleChat) {
                lv_dropdown_set_selected(bridgeChatDropdown, (uint16_t)i);
                break;
            }
        }
    }
}

void TelegramApp::saveBridgeRule()
{
    if (!bridgeDirDropdown || !bridgeChDropdown || !bridgeChatDropdown || !bridgeFormatTextarea || !bridgeEnabledSwitch)
        return;

    uint16_t dirSel  = lv_dropdown_get_selected(bridgeDirDropdown);
    uint16_t chSel   = lv_dropdown_get_selected(bridgeChDropdown);
    uint16_t chatSel = lv_dropdown_get_selected(bridgeChatDropdown);
    const char *fmt  = lv_textarea_get_text(bridgeFormatTextarea);
    bool enabled     = lv_obj_has_state(bridgeEnabledSwitch, LV_STATE_CHECKED);

    const char *dirStrs[] = { "mesh_to_telegram", "telegram_to_mesh", "both" };
    const char *dir = (dirSel < 3) ? dirStrs[dirSel] : "mesh_to_telegram";

    int64_t chatId = 0;
    std::string chatTitle;
    if (chatSel < chats.size()) {
        chatId = chats[chatSel].id;
        chatTitle = chats[chatSel].title;
    }

    char chatIdStr[24];
    int64ToStr(chatId, chatIdStr, sizeof(chatIdStr));
    std::string escapedFmt   = jsonEscape(fmt ? fmt : "");
    std::string escapedTitle = jsonEscape(chatTitle.c_str());

    // Auto-generate rule name from direction+channel if creating new
    std::string ruleName;
    if (selectedBridgeRuleIndex >= 0 && (size_t)selectedBridgeRuleIndex < bridgeRules.size()) {
        ruleName = bridgeRules[selectedBridgeRuleIndex].name;
    } else {
        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "%s_ch%u", dir, (unsigned)chSel);
        ruleName = nameBuf;
    }
    std::string escapedName = jsonEscape(ruleName.c_str());

    std::string body = std::string("{") +
        "\"name\":\"" + escapedName + "\"," +
        "\"direction\":\"" + dir + "\"," +
        "\"mesh_channel\":\"" + std::to_string(chSel) + "\"," +
        "\"telegram_chat_id\":" + chatIdStr + "," +
        "\"telegram_topic_id\":0," +
        "\"format_template\":\"" + escapedFmt + "\"," +
        "\"enabled\":" + (enabled ? "true" : "false") + "}";

    httpPost("/api/bridge/rules", body);
    showScreen(SCREEN_BRIDGE_RULES);
}

void TelegramApp::deleteBridgeRule(int index)
{
    if (index < 0 || (size_t)index >= bridgeRules.size())
        return;
    std::string path = "/api/bridge/rules/" + bridgeRules[index].name;
    httpDelete(path);
    selectedBridgeRuleIndex = -1;
    loadBridgeRules();
}

bool TelegramApp::parseJsonBridgeRules(const std::string &json)
{
    bridgeRules.clear();
    if (json.empty())
        return false;
    const char *p = json.c_str();

    if (strstr(p, "\"error\"")) {
        char errMsg[128];
        if (jsonExtractString(p, "error", errMsg, sizeof(errMsg)))
            ILOG_WARN("TelegramApp: bridge rules API error: %s", errMsg);
        return false;
    }

    p = strchr(p, '[');
    if (!p)
        return false;
    p++;

    const size_t MAX_RULES = 32;
    while (*p && bridgeRules.size() < MAX_RULES) {
        const char *objStart = strchr(p, '{');
        if (!objStart)
            break;
        const char *objEnd = jsonFindObjectEnd(objStart);
        if (!objEnd)
            break;

        size_t objLen = (size_t)(objEnd - objStart) + 1;
        char obj[512];
        if (objLen >= sizeof(obj)) objLen = sizeof(obj) - 1;
        memcpy(obj, objStart, objLen);
        obj[objLen] = '\0';

        BridgeRule rule;
        rule.meshChannel = 0;
        rule.telegramChatId = 0;
        rule.telegramTopicId = 0;
        rule.enabled = true;

        char tmp[256];
        if (jsonExtractString(obj, "name", tmp, sizeof(tmp)))
            rule.name = tmp;
        if (jsonExtractString(obj, "direction", tmp, sizeof(tmp)))
            rule.direction = tmp;
        if (jsonExtractString(obj, "format_template", tmp, sizeof(tmp)))
            rule.formatTemplate = tmp;

        char chStr[8] = {};
        if (jsonExtractString(obj, "mesh_channel", chStr, sizeof(chStr)))
            rule.meshChannel = (int32_t)atoi(chStr);

        int64_t chatId = 0;
        if (jsonExtractInt64(obj, "telegram_chat_id", &chatId))
            rule.telegramChatId = chatId;

        int64_t topicId = 0;
        if (jsonExtractInt64(obj, "telegram_topic_id", &topicId))
            rule.telegramTopicId = (int32_t)topicId;

        jsonExtractBool(obj, "enabled", &rule.enabled);

        // Try to resolve chat title from cached chats list
        for (const auto &chat : chats) {
            if (chat.id == rule.telegramChatId) {
                rule.tgChatTitle = chat.title;
                break;
            }
        }

        bridgeRules.push_back(rule);
        p = objEnd + 1;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Static LVGL callbacks
// ---------------------------------------------------------------------------

void TelegramApp::onConnectBtnClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (!self)
        return;

    // Read server URL from textarea
    if (self->serverUrlTextarea) {
        const char *url = lv_textarea_get_text(self->serverUrlTextarea);
        if (url && url[0] != '\0') {
            self->serverUrl = url;
            if (self->appContext)
                self->appContext->kvStore("tg_server_url", url);
        }
    }

    // Read API key from textarea
    if (self->apiKeyTextarea) {
        const char *key = lv_textarea_get_text(self->apiKeyTextarea);
        self->apiKey = key ? key : "";
        if (self->appContext)
            self->appContext->kvStore("tg_api_key", self->apiKey.c_str());
    }

    // Probe /api/status
    self->connState = CONN_CONNECTING;
    self->updateStatus();
    std::string statusJson = self->httpGet("/api/status");
    if (!statusJson.empty()) {
        self->connected = true;
        self->connState = CONN_CONNECTED;
        self->retryCount = 0;
        self->retryIntervalMs = 2000;
        self->disconnectedSinceMs = 0;
        jsonExtractBool(statusJson.c_str(), "authorized", &self->authorized);
        self->updateStatus();
        // Start WebSocket push connection
        self->wsConnect();

        if (self->authorized) {
            self->showScreen(SCREEN_CHATS);
        } else {
            self->startAuth();
        }
    } else {
        self->connected = false;
        self->connState = CONN_ERROR;
        self->lastError = "Cannot reach server";
        if (self->disconnectedSinceMs == 0)
            self->disconnectedSinceMs = lv_tick_get();
        self->updateStatus();
    }
}

void TelegramApp::onAuthSubmitClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (self)
        self->submitAuthInput();
}

void TelegramApp::onChatItemClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (!self)
        return;

    // Recover index from the clicked object's user_data
    // Use current_target (the object the callback was registered on), not target (which may be a child label)
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
    uintptr_t idx = (uintptr_t)lv_obj_get_user_data(btn);

    if (idx < self->chats.size()) {
        self->selectedChatIndex = (int)idx;
        self->currentChatId = self->chats[idx].id;
        self->currentTopicId = 0;

        if (self->chats[idx].isForum) {
            // Forum chat: show topics list
            if (self->topicsTitle)
                lv_label_set_text(self->topicsTitle, self->chats[idx].title.c_str());
            self->showScreen(SCREEN_TOPICS);
            self->loadTopics(self->currentChatId);
        } else {
            // Regular chat: show messages directly
            if (self->chatTitle)
                lv_label_set_text(self->chatTitle, self->chats[idx].title.c_str());
            self->showScreen(SCREEN_CHAT);
            self->loadMessages(self->currentChatId, self->currentTopicId);
        }
    }
}

void TelegramApp::onTopicItemClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (!self)
        return;

    lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
    uintptr_t idx = (uintptr_t)lv_obj_get_user_data(btn);

    if (idx < self->topics.size()) {
        self->currentTopicId = self->topics[idx].id;
        if (self->chatTitle)
            lv_label_set_text(self->chatTitle, self->topics[idx].title.c_str());
        self->showScreen(SCREEN_CHAT);
        self->loadMessages(self->currentChatId, self->currentTopicId);
    }
}

void TelegramApp::onSendBtnClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (self)
        self->sendMessage();
}

void TelegramApp::onTextareaClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (self && self->appContext) {
        lv_obj_t *ta = (lv_obj_t *)lv_event_get_current_target(e);
        self->appContext->requestKeyboard(ta);
    }
}

void TelegramApp::onBackBtnClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (!self)
        return;

    switch (self->currentScreen) {
    case SCREEN_CHAT:
        // If we came from topics, go back to topics; otherwise to chats
        if (self->currentTopicId != 0) {
            self->currentTopicId = 0;
            self->showScreen(SCREEN_TOPICS);
            self->loadTopics(self->currentChatId);
        } else {
            self->showScreen(SCREEN_CHATS);
        }
        break;
    case SCREEN_TOPICS:
        self->showScreen(SCREEN_CHATS);
        break;
    case SCREEN_CHATS:
        self->showScreen(SCREEN_SETTINGS);
        break;
    case SCREEN_BRIDGE_EDIT:
        self->showScreen(SCREEN_BRIDGE_RULES);
        break;
    case SCREEN_BRIDGE_RULES:
        self->showScreen(SCREEN_SETTINGS);
        break;
    default:
        // Already on settings, hide auth if visible
        if (self->authPanel)
            lv_obj_add_flag(self->authPanel, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

void TelegramApp::onBridgeRuleItemClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (!self)
        return;
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
    uintptr_t idx = (uintptr_t)lv_obj_get_user_data(btn);
    if (idx < self->bridgeRules.size())
        self->selectedBridgeRuleIndex = (int)idx;
}

void TelegramApp::onBridgeAddBtnClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (!self)
        return;
    self->selectedBridgeRuleIndex = -1;
    // Reset form to defaults
    if (self->bridgeDirDropdown)    lv_dropdown_set_selected(self->bridgeDirDropdown, 0);
    if (self->bridgeChDropdown)     lv_dropdown_set_selected(self->bridgeChDropdown, 0);
    if (self->bridgeFormatTextarea) lv_textarea_set_text(self->bridgeFormatTextarea, "");
    if (self->bridgeEnabledSwitch)  lv_obj_add_state(self->bridgeEnabledSwitch, LV_STATE_CHECKED);
    self->showScreen(SCREEN_BRIDGE_EDIT);
}

void TelegramApp::onBridgeEditBtnClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (!self)
        return;
    if (self->selectedBridgeRuleIndex < 0 ||
        (size_t)self->selectedBridgeRuleIndex >= self->bridgeRules.size())
        return;

    const BridgeRule &rule = self->bridgeRules[self->selectedBridgeRuleIndex];

    if (self->bridgeDirDropdown) {
        uint16_t dirIdx = 0;
        if (rule.direction == "telegram_to_mesh") dirIdx = 1;
        else if (rule.direction == "both")  dirIdx = 2;
        lv_dropdown_set_selected(self->bridgeDirDropdown, dirIdx);
    }
    if (self->bridgeChDropdown)
        lv_dropdown_set_selected(self->bridgeChDropdown, (uint16_t)rule.meshChannel);
    if (self->bridgeFormatTextarea)
        lv_textarea_set_text(self->bridgeFormatTextarea, rule.formatTemplate.c_str());
    if (self->bridgeEnabledSwitch) {
        if (rule.enabled)
            lv_obj_add_state(self->bridgeEnabledSwitch, LV_STATE_CHECKED);
        else
            lv_obj_remove_state(self->bridgeEnabledSwitch, LV_STATE_CHECKED);
    }

    self->showScreen(SCREEN_BRIDGE_EDIT);
}

void TelegramApp::onBridgeDeleteBtnClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (self)
        self->deleteBridgeRule(self->selectedBridgeRuleIndex);
}

void TelegramApp::onBridgeSaveBtnClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (self)
        self->saveBridgeRule();
}

void TelegramApp::onBridgeBtnClicked(lv_event_t *e)
{
    TelegramApp *self = static_cast<TelegramApp *>(lv_event_get_user_data(e));
    if (self)
        self->showScreen(SCREEN_BRIDGE_RULES);
}

#endif // HAS_CUSTOM_APPS
