# Custom Apps — Documentation

## 1. Overview

### What is Custom Apps Framework

Custom Apps Framework is a system for developing embedded applications on Meshtastic devices with thin client support. The framework allows adding new interactive applications with their own user interface (LVGL-based UI) without modifying the core device-ui code.

Built-in application examples:
- **TelegramApp** — Telegram integration via a companion server
- **MqttSettingsApp** — Meshtastic MQTT module configuration

### System Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                         ESP32 Device                          │
│  ┌────────────────────────────────────────────────────────┐  │
│  │                    device-ui Application               │  │
│  │  ┌──────────────────────────────────────────────────┐  │  │
│  │  │              AppManager                          │  │  │
│  │  │  ┌────────────┐  ┌────────────┐  ┌────────────┐ │  │  │
│  │  │  │TelegramApp │  │ MqttApp    │  │ CustomApp  │ │  │  │
│  │  │  └────────────┘  └────────────┘  └────────────┘ │  │  │
│  │  └──────────────────────────────────────────────────┘  │  │
│  │                         ↓ HTTPClient                     │  │
│  │                    (REST API)                            │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
                              ↓
                   HTTP (tcp://host:port/api/)
                              ↓
┌──────────────────────────────────────────────────────────────┐
│           Companion Telegram Proxy (Python)                  │
│  ┌────────────────────────────────────────────────────────┐  │
│  │              Telethon MTProto Client                   │  │
│  │   (MTProto protocol, user account auth, full API)     │  │
│  └────────────────────────────────────────────────────────┘  │
│                         ↓ MTProto                             │
└──────────────────────────────────────────────────────────────┘
                              ↓
                        Telegram Servers
```

### Why a Companion Server

The MTProto protocol requires significant resources:
- Full implementation needs ~500KB+ of RAM
- Cryptographic operations (TLS, session encryption)
- Complex authorization and session management logic
- Parsing and serialization of complex data types

ESP32 has limited memory resources (~320KB available for the application). Instead, a "thin client" architecture is used:

1. ESP32 sends simple HTTP requests to the companion server
2. The companion server (Python with Telethon) handles the entire MTProto protocol
3. The companion server can run on a separate machine (RPi, laptop, VPS)

**Advantages:**
- Minimal memory footprint on ESP32
- Simple REST API between device and server
- Server can be reused for multiple devices
- Easy to debug and update server logic

---

## 2. Building and Setup

### CMake Options

To enable Custom Apps, use the flag when configuring CMake:

```bash
cmake -DENABLE_CUSTOM_APPS=ON ..
```

Or via ccmake (interactive):

```bash
ccmake ..
# Find the ENABLE_CUSTOM_APPS option and set it to ON
```

### Dependencies

Custom Apps requires the following components:

| Component | Version | Purpose |
|-----------|---------|---------|
| LVGL | 8.3+ | UI framework |
| HTTPClient | esp-idf | HTTP requests to the companion server |
| ArduinoJson | 6.18+ | JSON response parsing (optional) |
| esp-idf | 4.4+ | Base platform |

All these dependencies are already included in device-ui.

### Build Command Example

```bash
cd /path/to/device-ui
mkdir -p build
cd build
cmake -DENABLE_CUSTOM_APPS=ON -DCMAKE_BUILD_TYPE=Release ..
make -j4
make flash  # To flash ESP32
```

---

## 3. Application Framework

### 3.1 ICustomApp — Application Interface

All applications inherit the `ICustomApp` interface and implement its virtual methods:

```cpp
class ICustomApp
{
  public:
    virtual ~ICustomApp() = default;

    /// Human-readable application name
    virtual const char *getName() const = 0;

    /// LVGL symbol for the app button (LV_SYMBOL_ENVELOPE, etc.)
    virtual const char *getIcon() const = 0;

    /// Initialize the application. Returns false on error.
    virtual bool init(AppContext *ctx) = 0;

    /// Create LVGL UI in the parent container.
    /// Called lazily on first app open.
    virtual lv_obj_t *createUI(lv_obj_t *parent) = 0;

    /// Called when the app panel becomes visible
    virtual void onShow() {}

    /// Called when the app panel is hidden
    virtual void onHide() {}

    /// Called periodically (~50ms) when the app is active
    virtual void onTick(uint32_t now_ms) {}

    /// Called for every mesh network packet (always, regardless of visibility)
    virtual void onMeshPacket(const meshtastic_MeshPacket &p) {}

    /// Clean up resources before app deletion
    virtual void destroy() = 0;
};
```

#### Application Lifecycle

```
1. registerApp(app)
        ↓
2. init(ctx)  ← initialization, config loading
        ↓
3. createUI(parent)  ← UI creation (lazy, on first open)
        ↓
4. onShow()  ← app becomes visible
        ↓
5. onTick(now_ms)  ← called every ~50ms
   onMeshPacket(packet)  ← network packets received
        ↓
6. onHide()  ← app is hidden
        ↓
7. onShow()/onHide() ...  ← may toggle many times
        ↓
8. destroy()  ← resource cleanup
```

#### Key Notes

- **Thread safety:** All LVGL operations must happen on the main thread (task_handler)
- **Lazy UI creation:** createUI is called only when the user first opens the app
- **Memory:** Use kvStore/kvLoad to persist configuration across reboots

### 3.2 AppManager — Application Manager

`AppManager` manages registration, initialization, and lifecycle of applications.

```cpp
class AppManager
{
  public:
    static const uint8_t MAX_APPS = 8;  // Maximum 8 apps

    /// Register a new application. Returns false if the registry is full.
    bool registerApp(ICustomApp *app);

    /// Unregister and delete an application by name
    void unregisterApp(const char *name);

    /// Initialize all registered applications
    void initAll(AppContext *ctx);

    /// Send tick to the current active application
    void tick(uint32_t now_ms);

    /// Send a mesh packet to all registered applications
    void dispatchPacket(const meshtastic_MeshPacket &p);

    /// Show an application by index, creating UI if necessary
    void showApp(uint8_t index, lv_obj_t *parent);

    /// Hide the current active application
    void hideCurrentApp();

    /// Get an application by index (nullptr if out of range)
    ICustomApp *getApp(uint8_t index) const;

    /// Get the number of registered applications
    uint8_t getAppCount() const;

    /// Get the index of the current active application (-1 if none)
    int8_t getActiveIndex() const;
};
```

#### Features

- **MAX_APPS = 8 limit:** Maximum 8 applications simultaneously
- **Lazy UI creation:** UI is created only on first app open
- **UI caching:** Created UI is kept in memory for fast switching

### 3.3 AppContext — Application API

`AppContext` provides applications with access to mesh network functions, persistent storage, and the device controller.

```cpp
class AppContext
{
  public:
    AppContext(ViewController *ctrl, MeshtasticView *view);

    /// Send a text message to a specific node
    void sendTextMessage(uint32_t to, uint8_t channel, const char *msg);

    /// Broadcast a text message on a channel
    void broadcastMessage(uint8_t channel, const char *msg);

    /// Get our node number
    uint32_t getMyNodeNum() const;

    /// Store a key-value pair persistently
    bool kvStore(const char *key, const char *value);

    /// Load a value by key from persistent storage
    std::string kvLoad(const char *key);

    /// Set the function for requesting the on-screen keyboard
    typedef void (*KeyboardRequestFn)(lv_obj_t *textarea, void *ctx);
    void setKeyboardRequestFn(KeyboardRequestFn fn, void *ctx);

    /// Request the on-screen keyboard for a text field
    void requestKeyboard(lv_obj_t *textarea);

    /// Access the controller (for advanced use)
    ViewController *getController();

    /// Access the view object (for advanced use)
    MeshtasticView *getView();
};
```

#### Usage Examples

**Sending a message:**
```cpp
// Send a message to node 0x12345678 on channel 0
appContext->sendTextMessage(0x12345678, 0, "Hello from app!");

// Broadcast to all
appContext->broadcastMessage(0, "Broadcast message");
```

**Persistent storage:**
```cpp
// Store a string
appContext->kvStore("telegram_server", "http://192.168.1.100:8000");

// Load a string
std::string server = appContext->kvLoad("telegram_server");
if (server.empty()) {
    server = "http://localhost:8000";  // default value
}
```

**On-screen keyboard:**
```cpp
// In onShow() or createUI():
appContext->requestKeyboard(myTextarea);

// The keyboard will appear and the user can type
```

---

## 4. Built-in Applications

### 4.1 TelegramApp

#### Architecture

TelegramApp implements Telegram integration via a companion server:

```
┌─────────────┐
│  ESP32      │
│  TelegramApp│──── HTTP REST ────→ ┌─────────────────────────┐
│             │                      │ Companion Telegram Proxy│
│             │←── JSON Response ─── │ (Python + Telethon)     │
└─────────────┘                      │                         │
                                     │ ↔ Telegram Servers      │
                                     └─────────────────────────┘
```

Advantages of this approach:
- ESP32 works as a simple HTTP client
- The companion server handles the entire MTProto protocol
- The server can be on a separate machine

#### UI Screens

TelegramApp has several screens the user navigates between:

| Screen | Purpose |
|--------|---------|
| **Settings** | Server URL and API key configuration |
| **Auth** | QR code for authorization, 2FA support |
| **Chats** | List of all chats with unread counters |
| **Topics** | Forum topics in the selected chat |
| **Chat** | Message history and sending new messages |
| **Bridge Rules** | Mesh ↔ Telegram routing rules management |
| **Bridge Edit** | Editing a single routing rule |

#### Authorization

**QR code method (recommended):**
1. The user presses "Start Auth" in the app
2. The app sends POST /api/auth/qr/start and receives a `qr_url` (tg:// link)
3. ESP32 renders the QR code from `qr_url` on the T-Deck 320x240 screen
4. The app periodically polls GET /api/auth/qr/status
5. The user scans the QR in Telegram: Settings → Devices → Link Desktop Device
6. Status changes to `authorized` (or `2fa_required` if 2FA is enabled)
7. TelegramApp starts working with the account

**2FA support:**
- If two-factor authentication is enabled on the account
- The companion server returns `2fa_required` status
- The app shows a password input screen
- The password is sent via POST /api/auth/2fa with the `password` field
- After entering the password, authorization completes with `authorized` status

#### Chat Management

**Chat list:**
- Request: GET /api/chats?limit=20&offset_id=0
- Display: list with names, avatars (if available), unread counter
- Sorting: by last message time

**Forum topics:**
- If the chat is a forum
- Request: GET /api/topics?chat_id=123
- Displays topic hierarchy

#### Sending Messages

**Text input:**
1. The user opens a chat
2. Message history is displayed on screen
3. A text field at the bottom for input
4. Tapping the field triggers the on-screen keyboard (requestKeyboard)

**Sending:**
```cpp
// In TelegramApp::onSendMessage():
appContext->sendTextMessage(telegramServerId, channel, messageText);
// or
appContext->broadcastMessage(channel, messageText);

// In parallel, an HTTP request is sent to the companion server:
// POST /api/send {"chat_id": 123, "text": "user message"}
```

#### Bridge Rules — Mesh ↔ Telegram Routing

Bridge allows automatic routing of messages between the Meshtastic mesh network and Telegram.

**Directions:**
- `mesh_to_telegram`: messages from mesh to Telegram
- `telegram_to_mesh`: messages from Telegram to mesh
- `both`: bidirectional routing

**Rule example:**
```json
{
  "name": "general",
  "direction": "both",
  "mesh_channel": "0",
  "telegram_chat_id": 123456789,
  "telegram_topic_id": null,
  "format_template": "{node_name}: {text}",
  "enabled": true
}
```

**Available template variables:**
- `{node_name}` — mesh node name
- `{text}` — message text
- `{sender}` — alias for `{node_name}` (sender name)
- `{channel}` — mesh channel

**CRUD operations:**
- GET /api/bridge/rules — get all rules
- POST /api/bridge/rules — create a new rule
- DELETE /api/bridge/rules/{name} — delete a rule
- POST /api/bridge/forward — forward a mesh message to Telegram (called by ESP32)

#### Settings

TelegramApp saves configuration via kvStore:

| Key | Value | Example |
|-----|-------|---------|
| `tg_server_url` | Companion server URL | `http://192.168.1.100:2704` |

Loading at init():
```cpp
std::string serverUrl = appContext->kvLoad("tg_server_url");
if (serverUrl.empty()) {
    serverUrl = "http://localhost:2704";
}
```

If the `PROXY_API_KEY` environment variable is set on the server, ESP32 must pass the `X-API-Key` header with every request. The key is configured in the TelegramApp Settings interface.

### 4.2 MqttSettingsApp

#### Purpose

MqttSettingsApp provides a graphical interface for configuring the Meshtastic MQTT module. Instead of editing configs via protobuf directly, the user enters parameters in UI forms.

#### Configuration Fields

| Field | Type | Description | Example |
|-------|------|-------------|---------|
| **Enabled** | Toggle | Enable MQTT module | ON/OFF |
| **Server** | Text | MQTT broker address | broker.example.com |
| **Port** | Number | MQTT broker port | 1883 |
| **Username** | Text | Authorization username | mqtt_user |
| **Password** | Text | Authorization password | secret123 |
| **Root Topic** | Text | Root topic for messages | msh/ |
| **TLS** | Toggle | Use TLS/SSL | ON/OFF |
| **JSON Output** | Toggle | Output in JSON format | ON/OFF |

#### Save Mechanism

MqttSettingsApp uses the Meshtastic Admin API (protobuf):

1. The user fills in the UI form
2. On pressing "Save", the config structure is assembled
3. The config is sent via Admin API in protobuf format
4. Meshtastic saves the config on the device
5. On reboot, the config is loaded automatically

```cpp
// Simplified save example:
meshtastic_Config config = {};
config.has_mqtt = true;
config.mqtt.enabled = mqttEnabled;
config.mqtt.tls_enabled = tlsEnabled;
// ... filling in the remaining fields ...

// Sending via Admin API (details in device-ui code)
controller->sendRawConfig(config);
```

#### UI Features

- **Scrollable container**: the form has scroll for devices with small screens
- **Lazy loading**: config is loaded on first app open
- **Validation**: simple port and URL validation before saving
- **Feedback**: status label shows save result

---

## 5. Companion Telegram Proxy

### 5.1 Installation

Companion Telegram Proxy is a Python server that acts as an MTProto proxy between ESP32 and Telegram.

#### Docker Installation (Recommended)

Prerequisites: Docker and docker-compose installed

**1. Navigate to the directory:**
```bash
cd device-ui/companion/telegram-proxy
```

**2. Create the config (config.yaml):**
```bash
cp "config example.yaml" config.yaml
```

**3. Edit config.yaml:**
```yaml
server:
  host: "0.0.0.0"
  port: 2704           # Server port

telegram:
  api_id: 1234567      # Get from https://my.telegram.org
  api_hash: "abcdef..."  # Get from https://my.telegram.org
  session_name: "meshtastic_companion"  # Session file name (optional)
```

The API key for request authentication is set via the `PROXY_API_KEY` environment variable. If set, all requests to `/api/` must include the `X-API-Key: <value>` header.

**4. Start the container:**
```bash
docker-compose up -d
```

**5. Check status:**
```bash
curl http://localhost:8000/api/status
```

#### Manual Installation (Python)

Prerequisites: Python 3.8+, venv

**1. Create a virtual environment:**
```bash
cd companion/telegram-proxy
python3 -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
```

**2. Install dependencies:**
```bash
pip install -r requirements.txt
```

**3. Create and fill in config.yaml (see above)**

**4. Start the server:**
```bash
python server.py
```

The server will start on the port specified in `config.yaml` (default 8080)

### 5.2 API Reference

Companion Telegram Proxy provides a REST API with JSON requests and responses.

#### Status Check

**GET /api/status**

Check if the server is running and the account is authorized.

```bash
curl http://localhost:8000/api/status
```

**Response (authorized):**
```json
{
  "authorized": true,
  "phone": "+1234567890"
}
```

or if not authorized:
```json
{
  "authorized": false,
  "phone": null
}
```

#### QR Code Authorization

**POST /api/auth/qr/start**

Start QR code authorization. Optional request body `{"force": true}` to force QR refresh.

```bash
curl -X POST http://localhost:8000/api/auth/qr/start \
  -H "Content-Type: application/json" \
  -d '{}'
```

**Response (QR ready for scanning):**
```json
{
  "status": "pending",
  "qr_url": "tg://login?token=...",
  "expires_at": 1710000300,
  "instruction": "Scan this QR in Telegram: Settings > Devices > Link Desktop Device."
}
```

**Response (already authorized):**
```json
{
  "status": "authorized"
}
```

**Response (2FA required after scanning):**
```json
{
  "status": "2fa_required",
  "instruction": "QR confirmed. Enter your Telegram 2FA password."
}
```

The QR code is passed as a `tg://` link in the `qr_url` field. It is rendered as a QR image on the device screen.

**GET /api/auth/qr/status**

Check the QR code authorization status.

```bash
curl http://localhost:8000/api/auth/qr/status
```

**Response (waiting for scan):**
```json
{
  "status": "pending",
  "qr_url": "tg://login?token=...",
  "expires_at": 1710000300,
  "instruction": "Scan this QR in Telegram: Settings > Devices > Link Desktop Device."
}
```

**Response (authorization complete):**
```json
{
  "status": "authorized"
}
```

**Response (QR expired):**
```json
{
  "status": "expired",
  "instruction": "QR code expired. Refresh to get a new one."
}
```

#### Phone Number Authorization (QR Alternative)

**POST /api/auth/phone**

Request a confirmation code to a phone number.

```bash
curl -X POST http://localhost:8000/api/auth/phone \
  -H "Content-Type: application/json" \
  -d '{"phone": "+1234567890"}'
```

**Response:**
```json
{
  "status": "code_required",
  "code_type": "sms",
  "delivery_hint": "Check SMS messages for the Telegram login code.",
  "code_length": 5
}
```

**POST /api/auth/code**

Confirm the code from SMS/Telegram.

```bash
curl -X POST http://localhost:8000/api/auth/code \
  -H "Content-Type: application/json" \
  -d '{"code": "12345"}'
```

**Response:**
```json
{
  "status": "authorized"
}
```

or if 2FA password is needed:
```json
{
  "status": "2fa_required"
}
```

#### Two-Factor Authentication

**POST /api/auth/2fa**

If a 2FA password is required during authorization (after QR or phone/code flow).

```bash
curl -X POST http://localhost:8000/api/auth/2fa \
  -H "Content-Type: application/json" \
  -d '{"password": "my2fapassword"}'
```

**Response:**
```json
{
  "status": "authorized"
}
```

#### Logout

**POST /api/auth/logout**

```bash
curl -X POST http://localhost:8000/api/auth/logout
```

**Response:**
```json
{
  "status": "logged_out"
}
```

#### Current User Information

**GET /api/me**

```bash
curl http://localhost:8000/api/me
```

**Response:**
```json
{
  "id": 123456789,
  "first_name": "John",
  "phone": "+1234567890"
}
```

#### Get Chat List

**GET /api/chats**

Get the user's dialog list.

**Parameters:**
- `limit` (int, default 20): number of chats
- `offset_id` (int, default 0): ID for pagination

```bash
curl "http://localhost:8000/api/chats?limit=20&offset_id=0"
```

**Response** (array of dialogs):
```json
[
  {
    "id": 123456789,
    "title": "John Doe",
    "type": "user",
    "is_forum": false,
    "unread": 3,
    "last_message": "Hello!",
    "last_date": 1710000000
  },
  {
    "id": -1001234567890,
    "title": "My Group",
    "type": "channel",
    "is_forum": true,
    "unread": 0,
    "last_message": "Welcome",
    "last_date": 1710000000
  }
]
```

#### Get Forum Topics

**GET /api/topics**

Get a list of topics in a forum (if the chat is a forum).

**Parameters:**
- `chat_id` (int, required): forum chat ID

```bash
curl "http://localhost:8000/api/topics?chat_id=-1001234567890"
```

**Response** (array of topics):
```json
[
  {
    "id": 1,
    "title": "General",
    "unread": 0
  },
  {
    "id": 2,
    "title": "Announcements",
    "unread": 5
  }
]
```

#### Get Message History

**GET /api/messages**

Get message history from a chat.

**Parameters:**
- `chat_id` (int, required): chat ID
- `topic_id` (int, optional): topic ID (for forums)
- `limit` (int, default 20): number of messages
- `offset_id` (int, default 0): ID for pagination

```bash
curl "http://localhost:8000/api/messages?chat_id=123456789&limit=20"
```

**Response** (array of messages):
```json
[
  {
    "id": 42,
    "from_name": "John Doe",
    "text": "Hello there!",
    "date": 1710000000,
    "out": false
  },
  {
    "id": 43,
    "from_name": "Me",
    "text": "Hi!",
    "date": 1710000100,
    "out": true
  }
]
```

#### Send Message

**POST /api/send**

Send a text message to a chat.

```bash
curl -X POST http://localhost:8000/api/send \
  -H "Content-Type: application/json" \
  -d '{
    "chat_id": 123456789,
    "text": "Hello from ESP32!",
    "topic_id": 0
  }'
```

**Response:**
```json
{
  "status": "sent",
  "message_id": 100
}
```

### 5.3 WebSocket Events

Companion Telegram Proxy supports WebSocket for real-time updates.

#### Connection

```javascript
// Client (e.g., on ESP32 with a WebSocket library):
ws = new WebSocket("ws://localhost:8000/api/updates");
ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log("Event:", data);
};
```

#### Event Types

**new_message**
```json
{
  "type": "new_message",
  "chat_id": 123456789,
  "message": {
    "id": 45,
    "from_name": "John",
    "text": "New message",
    "date": 1710000300,
    "out": false
  }
}
```

**bridge_message** (Telegram → Mesh, for forwarding to ESP32)
```json
{
  "type": "bridge_message",
  "rule": "general",
  "mesh_channel": "0",
  "from_name": "John",
  "text": "TG/John: Hello",
  "original_text": "Hello"
}
```

**Note:** WebSocket support on ESP32 requires an additional library. The current version uses HTTP polling in onTick().

### 5.4 Bridge Rules — Routing

Bridge Rules allow automatic routing of messages between the mesh network and Telegram.

#### Rule Structure

```json
{
  "name": "general",
  "enabled": true,
  "direction": "both",
  "mesh_channel": "0",
  "telegram_chat_id": 123456789,
  "telegram_topic_id": null,
  "format_template": "{node_name}: {text}"
}
```

#### Rule Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Unique rule name |
| `enabled` | boolean | Whether the rule is active |
| `direction` | string | `mesh_to_telegram`, `telegram_to_mesh`, `both` |
| `mesh_channel` | string | Mesh channel (string, e.g. `"0"`) |
| `telegram_chat_id` | int | Telegram chat ID |
| `telegram_topic_id` | int or null | Forum topic ID (null for regular chats) |
| `format_template` | string | Message formatting template |

#### Available Template Variables

| Variable | Source | Example |
|----------|--------|---------|
| `{node_name}` | mesh | "Node-A123" |
| `{text}` | message | "Hello world" |
| `{sender}` | alias for node_name | "Node-A123" |
| `{channel}` | mesh | "0" |

#### Get All Rules

**GET /api/bridge/rules**

```bash
curl http://localhost:8000/api/bridge/rules
```

**Response** (array of rules):
```json
[
  {
    "name": "general",
    "enabled": true,
    "direction": "both",
    "mesh_channel": "0",
    "telegram_chat_id": 123456789,
    "telegram_topic_id": null,
    "format_template": "{node_name}: {text}"
  }
]
```

#### Create a New Rule

**POST /api/bridge/rules**

Required fields: `name`, `mesh_channel`, `telegram_chat_id`. The `direction` field defaults to `mesh_to_telegram`.

```bash
curl -X POST http://localhost:8000/api/bridge/rules \
  -H "Content-Type: application/json" \
  -d '{
    "name": "general",
    "enabled": true,
    "direction": "both",
    "mesh_channel": "0",
    "telegram_chat_id": 123456789,
    "telegram_topic_id": null,
    "format_template": "{node_name}: {text}"
  }'
```

**Response** (created rule):
```json
{
  "name": "general",
  "enabled": true,
  "direction": "both",
  "mesh_channel": "0",
  "telegram_chat_id": 123456789,
  "telegram_topic_id": null,
  "format_template": "{node_name}: {text}"
}
```

#### Delete a Rule

**DELETE /api/bridge/rules/{name}**

```bash
curl -X DELETE http://localhost:8000/api/bridge/rules/general
```

**Response:**
```json
{
  "status": "deleted",
  "name": "general"
}
```

#### Forward a Mesh Message to Telegram

**POST /api/bridge/forward**

Called by ESP32 to forward a message from mesh to Telegram according to rules.

```bash
curl -X POST http://localhost:8000/api/bridge/forward \
  -H "Content-Type: application/json" \
  -d '{
    "node_name": "Node-A123",
    "channel": "0",
    "text": "Hello from mesh!"
  }'
```

**Response:**
```json
{
  "status": "ok",
  "forwarded": 1
}
```

#### Persistence

Bridge Rules are saved in the `bridge_rules.yaml` file on the server:

```yaml
- name: general
  enabled: true
  direction: both
  mesh_channel: '0'
  telegram_chat_id: 123456789
  telegram_topic_id: null
  format_template: '{node_name}: {text}'
```

On server restart, all rules are loaded from this file.

---

## 6. Creating Your Own Application

### Step-by-Step Guide

#### Step 1: Create the Header File

Create `/include/apps/builtin/MyCustomApp.h`:

```cpp
#pragma once

#ifdef HAS_CUSTOM_APPS

#include "apps/ICustomApp.h"
#include "lvgl.h"

class AppContext;

class MyCustomApp : public ICustomApp
{
  public:
    MyCustomApp();
    ~MyCustomApp() override;

    const char *getName() const override { return "My App"; }
    const char *getIcon() const override { return LV_SYMBOL_SETTINGS; }
    bool init(AppContext *ctx) override;
    lv_obj_t *createUI(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onTick(uint32_t now_ms) override;
    void onMeshPacket(const meshtastic_MeshPacket &p) override;
    void destroy() override;

  private:
    AppContext *appContext = nullptr;
    lv_obj_t *mainPanel = nullptr;
    uint32_t lastTickTime = 0;

    // Method for LVGL callback (static lambdas cannot capture this)
    void onSendPressed() {
        if (appContext) appContext->broadcastMessage(0, "Hello from MyCustomApp!");
    }
};

#endif // HAS_CUSTOM_APPS
```

#### Step 2: Create the Implementation

Create `/source/apps/builtin/MyCustomApp.cpp`:

```cpp
#ifdef HAS_CUSTOM_APPS

#include "apps/builtin/MyCustomApp.h"
#include "apps/AppContext.h"
#include "util/ILog.h"

MyCustomApp::MyCustomApp() {}

MyCustomApp::~MyCustomApp() {}

bool MyCustomApp::init(AppContext *ctx)
{
    appContext = ctx;
    ILOG_INFO("MyCustomApp initialized");
    return true;
}

lv_obj_t *MyCustomApp::createUI(lv_obj_t *parent)
{
    mainPanel = lv_obj_create(parent);
    lv_obj_set_size(mainPanel, LV_PCT(100), LV_PCT(100));

    // Title
    lv_obj_t *title = lv_label_create(mainPanel);
    lv_label_set_text(title, "My Custom App");

    // Button for sending a message
    lv_obj_t *btn = lv_btn_create(mainPanel);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Send Message");
    // Pass this as user_data — accessible in callback via lv_event_get_user_data
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        auto *self = static_cast<MyCustomApp *>(lv_event_get_user_data(e));
        if (self) self->onSendPressed();
    }, LV_EVENT_CLICKED, this);

    return mainPanel;
}

void MyCustomApp::onShow()
{
    ILOG_DEBUG("MyCustomApp: onShow");
    lastTickTime = millis();
}

void MyCustomApp::onHide()
{
    ILOG_DEBUG("MyCustomApp: onHide");
}

void MyCustomApp::onTick(uint32_t now_ms)
{
    // Update UI every 1000ms
    if (now_ms - lastTickTime >= 1000) {
        lastTickTime = now_ms;
        // Update UI elements here
    }
}

void MyCustomApp::onMeshPacket(const meshtastic_MeshPacket &p)
{
    // Handle mesh network packets
    if (p.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        ILOG_INFO("MyCustomApp received packet from node %u", p.from);
    }
}

void MyCustomApp::destroy()
{
    ILOG_DEBUG("MyCustomApp: destroy");
    // Resource cleanup
}

#endif // HAS_CUSTOM_APPS
```

#### Step 3: Register the Application

In the file where AppManager is created (usually `TFTView_320x240.cpp`), add the registration:

```cpp
// In constructor or initialization:
appManager = new AppManager();

// Register built-in apps
appManager->registerApp(new TelegramApp());
appManager->registerApp(new MqttSettingsApp());
appManager->registerApp(new MyCustomApp());  // Add our app

appManager->initAll(appContext);
```

#### Step 4: Update CMakeLists.txt

In `/CMakeLists.txt`, add the source file to the sources list:

```cmake
set(SOURCES
    source/apps/builtin/TelegramApp.cpp
    source/apps/builtin/MqttSettingsApp.cpp
    source/apps/builtin/MyCustomApp.cpp  # Add here
    # ... other files ...
)
```

#### Step 5: Build the Project

```bash
cd build
cmake -DENABLE_CUSTOM_APPS=ON ..
make -j4
make flash
```

### Example: Simple "Node Info" Application

An application for displaying information about the current node in the mesh network.

**node-info.h:**
```cpp
#pragma once
#ifdef HAS_CUSTOM_APPS
#include "apps/ICustomApp.h"

class NodeInfoApp : public ICustomApp {
  public:
    const char *getName() const override { return "Node Info"; }
    const char *getIcon() const override { return LV_SYMBOL_INFO; }
    bool init(AppContext *ctx) override;
    lv_obj_t *createUI(lv_obj_t *parent) override;
    void onTick(uint32_t now_ms) override;
    void destroy() override {}

  private:
    AppContext *appContext;
    lv_obj_t *nodeIdLabel, *nodeNameLabel, *signalLabel;
};
#endif
```

**node-info.cpp:**
```cpp
#ifdef HAS_CUSTOM_APPS
#include "apps/builtin/NodeInfoApp.h"
#include "apps/AppContext.h"

bool NodeInfoApp::init(AppContext *ctx) {
    appContext = ctx;
    return true;
}

lv_obj_t *NodeInfoApp::createUI(lv_obj_t *parent) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(panel, 10, 0);
    lv_obj_set_style_layout(panel, LV_LAYOUT_FLEX, 0);
    lv_obj_set_style_flex_flow(panel, LV_FLEX_FLOW_COLUMN, 0);

    nodeIdLabel = lv_label_create(panel);
    nodeNameLabel = lv_label_create(panel);
    signalLabel = lv_label_create(panel);

    lv_label_set_text(nodeIdLabel, "Node ID: -");
    lv_label_set_text(nodeNameLabel, "Name: -");
    lv_label_set_text(signalLabel, "Signal: -");

    return panel;
}

void NodeInfoApp::onTick(uint32_t now_ms) {
    uint32_t myNode = appContext->getMyNodeNum();
    char buf[64];
    snprintf(buf, sizeof(buf), "Node ID: %08lX", myNode);
    lv_label_set_text(nodeIdLabel, buf);
}
#endif
```

Result: a compact application of ~50 lines of code showing node information.

---

## 7. Architectural Decisions

### Why a Companion Server Instead of Direct Telegram Connection

**Telegram Bot API vs MTProto:**
- Bot API has limitations (no access to personal chats, slow updates)
- MTProto — full access, but requires ~500KB+ of memory

**Solution: Telethon + companion server**
- Telethon (Python) has a complete MTProto implementation
- The companion server can be on a separate machine
- ESP32 works as a simple HTTP client
- Scalability: one server can serve multiple devices

### Why Telethon (User Account) Instead of Telegram Bot API

**Telethon advantages:**
- Full access to chats and contacts
- Forum and topic support
- Session management with 2FA
- Ideal for personal use

**Bot API limitations:**
- Only public chats and channels
- No access to personal dialogs
- Limited functionality

### Why Berry for Scripting (Future)

Custom Apps Framework is prepared for Berry support (a lightweight scripting language):
- Tasmota uses Berry successfully
- Small memory footprint
- LVGL integration
- Hot-loading scripts capability

**Alternatives considered:**
- Lua: older, less optimized
- MicroPython: requires ~500KB memory (too much for ESP32)
- Elk: AGPL license (restricts commercial use)

---

## 8. Known Limitations and TODO

### Current Limitations

| Limitation | Status | Reason |
|-----------|--------|--------|
| WebSocket on ESP32 | NOT IMPLEMENTED | Requires a WebSocket client library, uses HTTP polling |
| Berry LVGL bindings | PLACEHOLDER | Requires wrappers for each LVGL method |
| Berry mesh bindings | PLACEHOLDER | Requires exporting all mesh APIs to Berry |
| Maximum 8 apps | BY DESIGN | ESP32 memory constraint |
| Data synchronization | MANUAL | App must manage UI updates on data changes itself |
| Hot-reloading apps | NOT SUPPORTED | Requires dynamic loading and unloading |

### TODO for Future Versions

- [ ] WebSocket support on ESP32 (replace HTTP polling with push updates)
- [ ] Berry interpreter integration in device-ui
- [ ] Berry LVGL bindings (lv_obj_create, lv_label_set_text, etc.)
- [ ] Berry mesh API bindings
- [ ] Dynamic loading of .berry scripts as applications
- [ ] UI Designer for creating LVGL forms without code
- [ ] Memory and CPU profiling/testing
- [ ] Application optimization documentation
- [ ] Examples: GPS tracker, climate sensor, alert system

---

## Conclusion

Custom Apps Framework provides a powerful and flexible system for extending Meshtastic device functionality. The "thin client + companion server" architecture allows adding complex features (such as Telegram integration) without overloading ESP32 memory.

To get started with development:
1. Study the ICustomApp interface
2. Look at the TelegramApp or MqttSettingsApp implementation
3. Create your own implementation following the step-by-step guide (section 6)
4. Register it in AppManager
5. Build the project with the ENABLE_CUSTOM_APPS=ON flag
