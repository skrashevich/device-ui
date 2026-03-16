# Custom Apps — Документация

## 1. Обзор

### Что такое Custom Apps Framework

Custom Apps Framework — это система для разработки встроенных приложений на устройствах Meshtastic с поддержкой тонкого клиента. Фреймворк позволяет добавлять новые интерактивные приложения с собственным пользовательским интерфейсом (UI на LVGL) без изменения основного кода device-ui.

Примеры встроенных приложений:
- **TelegramApp** — интеграция с Telegram через компаньон-сервер
- **MqttSettingsApp** — конфигурация MQTT модуля Meshtastic

### Архитектура системы

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

### Зачем нужен companion-сервер

Протокол MTProto требует значительных ресурсов для работы:
- Полная реализация требует ~500KB+ оперативной памяти
- Криптографические операции (TLS, шифрование сеанса)
- Сложная логика авторизации и управления сеансом
- Парсинг и сериализация сложных типов данных

ESP32 имеет ограниченные ресурсы памяти (~320KB доступно для приложения). Вместо этого используется архитектура "тонкий клиент":

1. ESP32 отправляет простые HTTP запросы к компаньон-серверу
2. Компаньон-сервер (Python с Telethon) обрабатывает весь MTProto протокол
3. Компаньон-сервер может работать на отдельной машине (RPi, ноутбук, VPS)

**Преимущества:**
- Минимальный расход памяти на ESP32
- Простой REST API между устройством и сервером
- Сервер можно переиспользовать для нескольких устройств
- Легко дебажить и обновлять логику сервера

---

## 2. Сборка и настройка

### CMake опции

Для включения Custom Apps используйте флаг при конфигурации CMake:

```bash
cmake -DENABLE_CUSTOM_APPS=ON ..
```

Или через ccmake (интерактивный):

```bash
ccmake ..
# Найти опцию ENABLE_CUSTOM_APPS и установить ON
```

### Зависимости

Custom Apps требует следующих компонентов:

| Компонент | Версия | Назначение |
|-----------|--------|-----------|
| LVGL | 8.3+ | UI фреймворк |
| HTTPClient | esp-idf | HTTP запросы к компаньон-серверу |
| ArduinoJson | 6.18+ | Парсинг JSON ответов (опционально) |
| esp-idf | 4.4+ | Базовая платформа |

Все эти зависимости уже включены в device-ui.

### Пример команды сборки

```bash
cd /path/to/device-ui
mkdir -p build
cd build
cmake -DENABLE_CUSTOM_APPS=ON -DCMAKE_BUILD_TYPE=Release ..
make -j4
make flash  # Для прошивки ESP32
```

---

## 3. Framework приложений

### 3.1 ICustomApp — Интерфейс приложения

Все приложения наследуют интерфейс `ICustomApp` и реализуют его виртуальные методы:

```cpp
class ICustomApp
{
  public:
    virtual ~ICustomApp() = default;

    /// Человекочитаемое имя приложения
    virtual const char *getName() const = 0;

    /// Символ LVGL для кнопки приложения (LV_SYMBOL_ENVELOPE, и т.д.)
    virtual const char *getIcon() const = 0;

    /// Инициализация приложения. Возвращает false при ошибке.
    virtual bool init(AppContext *ctx) = 0;

    /// Создание LVGL UI в родительском контейнере.
    /// Вызывается лениво при первом открытии приложения.
    virtual lv_obj_t *createUI(lv_obj_t *parent) = 0;

    /// Вызывается при отображении панели приложения
    virtual void onShow() {}

    /// Вызывается при скрытии панели приложения
    virtual void onHide() {}

    /// Вызывается периодически (~50ms) когда приложение активно
    virtual void onTick(uint32_t now_ms) {}

    /// Вызывается для каждого пакета из mesh сети (всегда, независимо от видимости)
    virtual void onMeshPacket(const meshtastic_MeshPacket &p) {}

    /// Очистка ресурсов перед удалением приложения
    virtual void destroy() = 0;
};
```

#### Жизненный цикл приложения

```
1. registerApp(app)
        ↓
2. init(ctx)  ← инициализация, загрузка конфига
        ↓
3. createUI(parent)  ← создание UI (ленивое, при первом открытии)
        ↓
4. onShow()  ← приложение становится видимым
        ↓
5. onTick(now_ms)  ← вызывается каждые ~50ms
   onMeshPacket(packet)  ← поступили пакеты из сети
        ↓
6. onHide()  ← приложение скрыто
        ↓
7. onShow()/onHide() ...  ← может переключаться много раз
        ↓
8. destroy()  ← очистка ресурсов
```

#### Важные особенности

- **Thread safety:** Все LVGL операции должны происходить в основном потоке (task_handler)
- **Lazy UI creation:** createUI вызывается только когда пользователь впервые открывает приложение
- **Memory:** Используйте kvStore/kvLoad для сохранения конфигурации между перезагрузками

### 3.2 AppManager — Менеджер приложений

`AppManager` управляет регистрацией, инициализацией и лайфциклом приложений.

```cpp
class AppManager
{
  public:
    static const uint8_t MAX_APPS = 8;  // Максимум 8 приложений

    /// Регистрация нового приложения. Возвращает false если реестр переполнен.
    bool registerApp(ICustomApp *app);

    /// Отмена регистрации и удаление приложения по имени
    void unregisterApp(const char *name);

    /// Инициализация всех зарегистрированных приложений
    void initAll(AppContext *ctx);

    /// Отправка tick текущему активному приложению
    void tick(uint32_t now_ms);

    /// Отправка mesh пакета всем зарегистрированным приложениям
    void dispatchPacket(const meshtastic_MeshPacket &p);

    /// Отображение приложения по индексу, создание UI если необходимо
    void showApp(uint8_t index, lv_obj_t *parent);

    /// Скрытие текущего активного приложения
    void hideCurrentApp();

    /// Получить приложение по индексу (nullptr если вне диапазона)
    ICustomApp *getApp(uint8_t index) const;

    /// Получить количество зарегистрированных приложений
    uint8_t getAppCount() const;

    /// Получить индекс текущего активного приложения (-1 если нет)
    int8_t getActiveIndex() const;
};
```

#### Особенности

- **Лимит MAX_APPS = 8:** Максимум 8 приложений одновременно
- **Ленивое создание UI:** UI создаётся только при первом открытии приложения
- **Кэширование UI:** Созданный UI сохраняется в памяти для быстрого переключения

### 3.3 AppContext — API для приложений

`AppContext` предоставляет приложениям доступ к функциям mesh-сети, персистентному хранилищу и контроллеру устройства.

```cpp
class AppContext
{
  public:
    AppContext(ViewController *ctrl, MeshtasticView *view);

    /// Отправить текстовое сообщение конкретному узлу
    void sendTextMessage(uint32_t to, uint8_t channel, const char *msg);

    /// Трансляция текстового сообщения на канал
    void broadcastMessage(uint8_t channel, const char *msg);

    /// Получить номер нашего узла
    uint32_t getMyNodeNum() const;

    /// Сохранить пару ключ-значение персистентно
    bool kvStore(const char *key, const char *value);

    /// Загрузить значение по ключу из персистентного хранилища
    std::string kvLoad(const char *key);

    /// Установить функцию для запроса экранной клавиатуры
    typedef void (*KeyboardRequestFn)(lv_obj_t *textarea, void *ctx);
    void setKeyboardRequestFn(KeyboardRequestFn fn, void *ctx);

    /// Запросить экранную клавиатуру для текстового поля
    void requestKeyboard(lv_obj_t *textarea);

    /// Доступ к контроллеру (для продвинутого использования)
    ViewController *getController();

    /// Доступ к view объекту (для продвинутого использования)
    MeshtasticView *getView();
};
```

#### Примеры использования

**Отправка сообщения:**
```cpp
// Отправить сообщение узлу с номером 0x12345678 на канал 0
appContext->sendTextMessage(0x12345678, 0, "Hello from app!");

// Трансляция для всех
appContext->broadcastMessage(0, "Broadcast message");
```

**Персистентное хранилище:**
```cpp
// Сохранить строку
appContext->kvStore("telegram_server", "http://192.168.1.100:8000");

// Загрузить строку
std::string server = appContext->kvLoad("telegram_server");
if (server.empty()) {
    server = "http://localhost:8000";  // значение по умолчанию
}
```

**Экранная клавиатура:**
```cpp
// В onShow() или createUI():
appContext->requestKeyboard(myTextarea);

// Клавиатура отобразится и пользователь сможет вводить текст
```

---

## 4. Встроенные приложения

### 4.1 TelegramApp

#### Архитектура

TelegramApp реализует интеграцию с Telegram через компаньон-сервер:

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

Преимущества такого подхода:
- ESP32 работает как простой HTTP клиент
- Companion-сервер обрабатывает весь MTProto протокол
- Сервер может быть на отдельной машине

#### Экраны UI

TelegramApp имеет несколько экранов, между которыми пользователь навигирует:

| Экран | Назначение |
|-------|-----------|
| **Settings** | Конфигурация URL сервера и API ключа |
| **Auth** | QR-код для авторизации, поддержка 2FA |
| **Chats** | Список всех чатов и непрочитанные счётчики |
| **Topics** | Форумные топики в выбранном чате |
| **Chat** | История сообщений и отправка новых |
| **Bridge Rules** | Управление правилами маршрутизации Mesh ↔ Telegram |
| **Bridge Edit** | Редактирование отдельного правила маршрутизации |

#### Авторизация

**QR-код метод (рекомендуется):**
1. Пользователь нажимает "Start Auth" в приложении
2. Приложение отправляет POST /api/auth/qr/start, получает `qr_url` (tg:// ссылку)
3. ESP32 рендерит QR-код из `qr_url` на экране T-Deck 320x240
4. Приложение периодически опрашивает GET /api/auth/qr/status
5. Пользователь сканирует QR в Telegram: Настройки → Устройства → Подключить устройство
6. Статус меняется на `authorized` (или `2fa_required` если включена 2FA)
7. TelegramApp начинает работу с аккаунтом

**2FA поддержка:**
- Если на аккаунте включена двухфакторная аутентификация
- Компаньон-сервер вернёт статус `2fa_required`
- Приложение покажет экран ввода пароля
- Пароль отправляется через POST /api/auth/2fa с полем `password`
- После ввода пароля авторизация завершится со статусом `authorized`

#### Управление чатами

**Список чатов:**
- Запрос: GET /api/chats?limit=20&offset_id=0
- Отображение: список с названиями, аватарами (если есть), счётчиком непрочитанных
- Сортировка: по времени последнего сообщения

**Форумные топики:**
- Если чат является форумом
- Запрос: GET /api/topics?chat_id=123
- Отображение иерархии топиков и подтопиков

#### Отправка сообщений

**Ввод текста:**
1. Пользователь открывает чат
2. История сообщений отображается на экране
3. Внизу текстовое поле для ввода
4. Нажатие на поле вызывает экранную клавиатуру (requestKeyboard)

**Отправка:**
```cpp
// В TelegramApp::onSendMessage():
appContext->sendTextMessage(telegramServerId, channel, messageText);
// или
appContext->broadcastMessage(channel, messageText);

// Параллельно отправляется HTTP запрос к компаньон-серверу:
// POST /api/send {"chat_id": 123, "text": "user message"}
```

#### Bridge Rules — маршрутизация Mesh ↔ Telegram

Bridge позволяет автоматически маршрутизировать сообщения между mesh-сетью Meshtastic и Telegram.

**Направления:**
- `mesh_to_telegram`: сообщения из mesh в Telegram
- `telegram_to_mesh`: сообщения из Telegram в mesh
- `both`: двусторонняя маршрутизация

**Пример правила:**
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

**Доступные переменные шаблона:**
- `{node_name}` — имя узла из mesh
- `{text}` — текст сообщения
- `{sender}` — синоним `{node_name}` (имя отправителя)
- `{channel}` — канал mesh

**CRUD операции:**
- GET /api/bridge/rules — получить все правила
- POST /api/bridge/rules — создать новое правило
- DELETE /api/bridge/rules/{name} — удалить правило
- POST /api/bridge/forward — переслать mesh-сообщение в Telegram (вызывается ESP32)

#### Настройки

TelegramApp сохраняет конфигурацию через kvStore:

| Ключ | Значение | Пример |
|------|----------|--------|
| `tg_server_url` | URL компаньон-сервера | `http://192.168.1.100:2704` |

Загрузка при init():
```cpp
std::string serverUrl = appContext->kvLoad("tg_server_url");
if (serverUrl.empty()) {
    serverUrl = "http://localhost:2704";
}
```

Если на сервере установлена переменная окружения `PROXY_API_KEY`, ESP32 должен передавать заголовок `X-API-Key` в каждом запросе. Ключ настраивается в интерфейсе Settings TelegramApp.

### 4.2 MqttSettingsApp

#### Назначение

MqttSettingsApp предоставляет графический интерфейс для настройки MQTT модуля Meshtastic. Вместо редактирования конфигов через протобуф напрямую, пользователь вводит параметры в UI формы.

#### Поля конфигурации

| Поле | Тип | Описание | Пример |
|------|-----|---------|--------|
| **Enabled** | Toggle | Включить MQTT модуль | ON/OFF |
| **Server** | Text | Адрес MQTT брокера | broker.example.com |
| **Port** | Number | Порт MQTT брокера | 1883 |
| **Username** | Text | Пользователь для авторизации | mqtt_user |
| **Password** | Text | Пароль для авторизации | secret123 |
| **Root Topic** | Text | Корневой топик для сообщений | msh/ |
| **TLS** | Toggle | Использовать TLS/SSL | ON/OFF |
| **JSON Output** | Toggle | Выводить в формате JSON | ON/OFF |

#### Механизм сохранения

MqttSettingsApp использует Admin API Meshtastic (протобуф):

1. Пользователь заполняет форму в UI
2. При нажатии "Save" собирается структура конфига
3. Конфиг отправляется через Admin API в protobuf формате
4. Meshtastic сохраняет конфиг на устройстве
5. При перезагрузке конфиг загружается автоматически

```cpp
// Упрощённый пример сохранения:
meshtastic_Config config = {};
config.has_mqtt = true;
config.mqtt.enabled = mqttEnabled;
config.mqtt.tls_enabled = tlsEnabled;
// ... заполнение остальных полей ...

// Отправка через Admin API (детали в device-ui коде)
controller->sendRawConfig(config);
```

#### UI особенности

- **Scrollable контейнер**: форма имеет scroll для устройств с малыми экранами
- **Lazy loading**: конфиг загружается при первом открытии приложения
- **Validation**: простая валидация портов и URL перед сохранением
- **Feedback**: статус лейбл показывает успешность сохранения

---

## 5. Companion Telegram Proxy — компаньон-сервер

### 5.1 Установка

Companion Telegram Proxy — это Python-сервер, который работает как MTProto proxy между ESP32 и Telegram.

#### Docker установка (рекомендуется)

Предусловия: Docker и docker-compose установлены

**1. Клонируйте репозиторий:**
```bash
cd device-ui/companion/telegram-proxy
```

**2. Создайте конфиг (config.yaml):**
```bash
cp "config example.yaml" config.yaml
```

**3. Отредактируйте config.yaml:**
```yaml
server:
  host: "0.0.0.0"
  port: 2704           # Порт сервера

telegram:
  api_id: 1234567      # Получить с https://my.telegram.org
  api_hash: "abcdef..."  # Получить с https://my.telegram.org
  session_name: "meshtastic_companion"  # Имя файла сессии (опционально)
```

API ключ для аутентификации запросов задаётся переменной окружения `PROXY_API_KEY`. Если она установлена, все запросы к `/api/` должны содержать заголовок `X-API-Key: <значение>`.

**4. Запустите контейнер:**
```bash
docker-compose up -d
```

**5. Проверьте статус:**
```bash
curl http://localhost:8000/api/status
```

#### Ручная установка (Python)

Предусловия: Python 3.8+, venv

**1. Создайте виртуальное окружение:**
```bash
cd companion/telegram-proxy
python3 -m venv venv
source venv/bin/activate  # На Windows: venv\Scripts\activate
```

**2. Установите зависимости:**
```bash
pip install -r requirements.txt
```

**3. Создайте и заполните config.yaml (см. выше)**

**4. Запустите сервер:**
```bash
python server.py
```

Сервер запустится на порту, указанном в `config.yaml` (по умолчанию 8080)

### 5.2 API Reference

Companion Telegram Proxy предоставляет REST API с JSON запросами и ответами.

#### Проверка статуса

**GET /api/status**

Проверить, работает ли сервер и авторизован ли аккаунт.

```bash
curl http://localhost:8000/api/status
```

**Ответ (авторизован):**
```json
{
  "authorized": true,
  "phone": "+1234567890"
}
```

или если не авторизован:
```json
{
  "authorized": false,
  "phone": null
}
```

#### Авторизация через QR-код

**POST /api/auth/qr/start**

Начать авторизацию через QR-код. Опционально тело запроса `{"force": true}` для принудительного обновления QR.

```bash
curl -X POST http://localhost:8000/api/auth/qr/start \
  -H "Content-Type: application/json" \
  -d '{}'
```

**Ответ (QR готов к сканированию):**
```json
{
  "status": "pending",
  "qr_url": "tg://login?token=...",
  "expires_at": 1710000300,
  "instruction": "Scan this QR in Telegram: Settings > Devices > Link Desktop Device."
}
```

**Ответ (уже авторизован):**
```json
{
  "status": "authorized"
}
```

**Ответ (требуется 2FA после сканирования):**
```json
{
  "status": "2fa_required",
  "instruction": "QR confirmed. Enter your Telegram 2FA password."
}
```

QR-код передаётся как `tg://` ссылка в поле `qr_url`. На экране устройства рендерится как QR-изображение.

**GET /api/auth/qr/status**

Проверить статус авторизации через QR-код.

```bash
curl http://localhost:8000/api/auth/qr/status
```

**Ответ (ожидание сканирования):**
```json
{
  "status": "pending",
  "qr_url": "tg://login?token=...",
  "expires_at": 1710000300,
  "instruction": "Scan this QR in Telegram: Settings > Devices > Link Desktop Device."
}
```

**Ответ (авторизация завершена):**
```json
{
  "status": "authorized"
}
```

**Ответ (QR устарел):**
```json
{
  "status": "expired",
  "instruction": "QR code expired. Refresh to get a new one."
}
```

#### Авторизация через номер телефона (альтернатива QR)

**POST /api/auth/phone**

Запросить код подтверждения на номер телефона.

```bash
curl -X POST http://localhost:8000/api/auth/phone \
  -H "Content-Type: application/json" \
  -d '{"phone": "+1234567890"}'
```

**Ответ:**
```json
{
  "status": "code_required",
  "code_type": "sms",
  "delivery_hint": "Check SMS messages for the Telegram login code.",
  "code_length": 5
}
```

**POST /api/auth/code**

Подтвердить код из SMS/Telegram.

```bash
curl -X POST http://localhost:8000/api/auth/code \
  -H "Content-Type: application/json" \
  -d '{"code": "12345"}'
```

**Ответ:**
```json
{
  "status": "authorized"
}
```

или если нужен 2FA пароль:
```json
{
  "status": "2fa_required"
}
```

#### Двухфакторная аутентификация

**POST /api/auth/2fa**

Если при авторизации требуется пароль 2FA (после QR или phone/code flow).

```bash
curl -X POST http://localhost:8000/api/auth/2fa \
  -H "Content-Type: application/json" \
  -d '{"password": "my2fapassword"}'
```

**Ответ:**
```json
{
  "status": "authorized"
}
```

#### Выход из аккаунта

**POST /api/auth/logout**

```bash
curl -X POST http://localhost:8000/api/auth/logout
```

**Ответ:**
```json
{
  "status": "logged_out"
}
```

#### Информация о текущем пользователе

**GET /api/me**

```bash
curl http://localhost:8000/api/me
```

**Ответ:**
```json
{
  "id": 123456789,
  "first_name": "John",
  "phone": "+1234567890"
}
```

#### Получить список чатов

**GET /api/chats**

Получить список диалогов пользователя.

**Параметры:**
- `limit` (int, default 20): количество чатов
- `offset_id` (int, default 0): ID для пагинации

```bash
curl "http://localhost:8000/api/chats?limit=20&offset_id=0"
```

**Ответ** (массив диалогов):
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

#### Получить топики форума

**GET /api/topics**

Получить список топиков в форуме (если чат является форумом).

**Параметры:**
- `chat_id` (int, required): ID чата-форума

```bash
curl "http://localhost:8000/api/topics?chat_id=-1001234567890"
```

**Ответ** (массив топиков):
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

#### Получить историю сообщений

**GET /api/messages**

Получить историю сообщений из чата.

**Параметры:**
- `chat_id` (int, required): ID чата
- `topic_id` (int, optional): ID топика (для форумов)
- `limit` (int, default 20): количество сообщений
- `offset_id` (int, default 0): ID для пагинации

```bash
curl "http://localhost:8000/api/messages?chat_id=123456789&limit=20"
```

**Ответ** (массив сообщений):
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

#### Отправить сообщение

**POST /api/send**

Отправить текстовое сообщение в чат.

```bash
curl -X POST http://localhost:8000/api/send \
  -H "Content-Type: application/json" \
  -d '{
    "chat_id": 123456789,
    "text": "Hello from ESP32!",
    "topic_id": 0
  }'
```

**Ответ:**
```json
{
  "status": "sent",
  "message_id": 100
}
```

### 5.3 WebSocket Events

Companion Telegram Proxy поддерживает WebSocket для real-time обновлений.

#### Подключение

```javascript
// Клиент (например, на ESP32 с WebSocket библиотекой):
ws = new WebSocket("ws://localhost:8000/api/updates");
ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log("Event:", data);
};
```

#### Event типы

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

**bridge_message** (Telegram → Mesh, для пересылки на ESP32)
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

**Note:** WebSocket поддержка на ESP32 требует дополнительную библиотеку. Текущая версия использует HTTP polling в onTick().

### 5.4 Bridge Rules — маршрутизация

Bridge Rules позволяют автоматически маршрутизировать сообщения между mesh-сетью и Telegram.

#### Структура правила

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

#### Поля правила

| Поле | Тип | Описание |
|------|-----|---------|
| `name` | string | Уникальное имя правила |
| `enabled` | boolean | Включено ли правило |
| `direction` | string | `mesh_to_telegram`, `telegram_to_mesh`, `both` |
| `mesh_channel` | string | Канал mesh (строка, например `"0"`) |
| `telegram_chat_id` | int | ID чата в Telegram |
| `telegram_topic_id` | int или null | ID топика форума (null для обычных чатов) |
| `format_template` | string | Шаблон форматирования сообщения |

#### Доступные переменные шаблона

| Переменная | Источник | Пример |
|------------|----------|--------|
| `{node_name}` | mesh | "Node-A123" |
| `{text}` | сообщение | "Hello world" |
| `{sender}` | синоним node_name | "Node-A123" |
| `{channel}` | mesh | "0" |

#### Получить все правила

**GET /api/bridge/rules**

```bash
curl http://localhost:8000/api/bridge/rules
```

**Ответ** (массив правил):
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

#### Создать новое правило

**POST /api/bridge/rules**

Обязательные поля: `name`, `mesh_channel`, `telegram_chat_id`. Поле `direction` по умолчанию `mesh_to_telegram`.

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

**Ответ** (созданное правило):
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

#### Удалить правило

**DELETE /api/bridge/rules/{name}**

```bash
curl -X DELETE http://localhost:8000/api/bridge/rules/general
```

**Ответ:**
```json
{
  "status": "deleted",
  "name": "general"
}
```

#### Переслать mesh-сообщение в Telegram

**POST /api/bridge/forward**

Вызывается ESP32 для пересылки сообщения из mesh в Telegram согласно правилам.

```bash
curl -X POST http://localhost:8000/api/bridge/forward \
  -H "Content-Type: application/json" \
  -d '{
    "node_name": "Node-A123",
    "channel": "0",
    "text": "Hello from mesh!"
  }'
```

**Ответ:**
```json
{
  "status": "ok",
  "forwarded": 1
}
```

#### Персистентность

Bridge Rules сохраняются в файле `bridge_rules.yaml` на сервере:

```yaml
- name: general
  enabled: true
  direction: both
  mesh_channel: '0'
  telegram_chat_id: 123456789
  telegram_topic_id: null
  format_template: '{node_name}: {text}'
```

При перезагрузке сервера все правила загружаются из этого файла.

---

## 6. Создание своего приложения

### Пошаговый гайд

#### Шаг 1: Создайте заголовочный файл

Создайте `/include/apps/builtin/MyCustomApp.h`:

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

    // Метод для вызова из LVGL callback (статические лямбды не могут захватывать this)
    void onSendPressed() {
        if (appContext) appContext->broadcastMessage(0, "Hello from MyCustomApp!");
    }
};

#endif // HAS_CUSTOM_APPS
```

#### Шаг 2: Создайте реализацию

Создайте `/source/apps/builtin/MyCustomApp.cpp`:

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

    // Заголовок
    lv_obj_t *title = lv_label_create(mainPanel);
    lv_label_set_text(title, "My Custom App");

    // Кнопка для отправки сообщения
    lv_obj_t *btn = lv_btn_create(mainPanel);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Send Message");
    // Передаём this как user_data — доступен в callback через lv_event_get_user_data
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
    // Обновление UI каждые 1000ms
    if (now_ms - lastTickTime >= 1000) {
        lastTickTime = now_ms;
        // Обновите UI элементы здесь
    }
}

void MyCustomApp::onMeshPacket(const meshtastic_MeshPacket &p)
{
    // Обработка пакетов из mesh сети
    if (p.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        ILOG_INFO("MyCustomApp received packet from node %u", p.from);
    }
}

void MyCustomApp::destroy()
{
    ILOG_DEBUG("MyCustomApp: destroy");
    // Очистка ресурсов
}

#endif // HAS_CUSTOM_APPS
```

#### Шаг 3: Регистрируйте приложение

В файле, где создаётся AppManager (обычно в `TFTView_320x240.cpp`), добавьте регистрацию:

```cpp
// В конструкторе или инициализации:
appManager = new AppManager();

// Регистрируем встроенные приложения
appManager->registerApp(new TelegramApp());
appManager->registerApp(new MqttSettingsApp());
appManager->registerApp(new MyCustomApp());  // Добавляем наше приложение

appManager->initAll(appContext);
```

#### Шаг 4: Обновите CMakeLists.txt

В `/CMakeLists.txt` добавьте исходный файл в список источников:

```cmake
set(SOURCES
    source/apps/builtin/TelegramApp.cpp
    source/apps/builtin/MqttSettingsApp.cpp
    source/apps/builtin/MyCustomApp.cpp  # Добавите здесь
    # ... другие файлы ...
)
```

#### Шаг 5: Соберите проект

```bash
cd build
cmake -DENABLE_CUSTOM_APPS=ON ..
make -j4
make flash
```

### Пример: Простое приложение "Node Info"

Приложение для отображения информации о текущем узле в mesh сети.

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

Результат: компактное приложение из ~50 строк кода, показывающее информацию о узле.

---

## 7. Архитектурные решения

### Почему companion-сервер, а не прямое подключение к Telegram

**Telegram Bot API vs MTProto:**
- Bot API имеет ограничения (нет доступа к личным чатам, медленные обновления)
- MTProto — полный доступ, но требует ~500KB+ памяти

**Решение: Telethon + companion-сервер**
- Telethon (Python) имеет полную MTProto реализацию
- Компаньон-сервер может быть на отдельной машине
- ESP32 работает как простой HTTP клиент
- Масштабируемость: один сервер может обслуживать много устройств

### Почему Telethon (user account), а не Telegram Bot API

**Advantages of Telethon:**
- Полный доступ к чатам и контактам
- Поддержка форумов и топиков
- Возможность управления сеансом через 2FA
- Идеален для личного использования

**Limitations of Bot API:**
- Только публичные чаты и каналы
- Нет доступа к личным диалогам
- Ограниченная функциональность

### Почему Berry для скриптинга (будущее)

Custom Apps Framework подготовлен для поддержки Berry (лёгкий язык скриптинга):
- Таsmota использует Berry успешно
- Малый footprint памяти
- Интеграция с LVGL
- Возможность горячей загрузки скриптов

**Альтернативы, которые были рассмотрены:**
- Lua: старше, менее оптимизирован
- MicroPython: требует ~500KB памяти (слишком много для ESP32)
- Elk: AGPL лицензия (ограничивает коммерческое использование)

---

## 8. Известные ограничения и TODO

### Текущие ограничения

| Ограничение | Статус | Причина |
|------------|--------|---------|
| WebSocket на ESP32 | NOT IMPLEMENTED | Требует WebSocket клиентскую библиотеку, используется HTTP polling |
| Berry биндинги для LVGL | PLACEHOLDER | Требует обёртки для каждого LVGL метода |
| Berry биндинги для mesh | PLACEHOLDER | Требует экспорта всех mesh API в Berry |
| Максимум 8 приложений | BY DESIGN | Ограничение памяти ESP32 |
| Синхронизация данных | MANUAL | Приложение должно сам управлять обновлением UI при изменении данных |
| Горячая перезагрузка приложений | NOT SUPPORTED | Требует динамической загрузки и выгрузки |

### TODO для будущих версий

- [ ] WebSocket support на ESP32 (заменить HTTP polling на push updates)
- [ ] Berry interpreter интеграция в device-ui
- [ ] Berry LVGL биндинги (лv_obj_create, lv_label_set_text и т.д.)
- [ ] Berry mesh API биндинги
- [ ] Динамическая загрузка .berry скриптов как приложений
- [ ] UI Designer для создания LVGL форм без кода
- [ ] Тестирование на памяти и CPU профилирование
- [ ] Документация по оптимизации приложений
- [ ] Примеры: GPS трекер, датчик климата, система оповещений

---

## Заключение

Custom Apps Framework предоставляет мощную и гибкую систему для расширения функциональности Meshtastic устройств. Архитектура "тонкий клиент + companion-сервер" позволяет добавлять сложные функции (как интеграция с Telegram) без перегрузки памяти ESP32.

Для начала разработки:
1. Изучите ICustomApp интерфейс
2. Посмотрите на реализацию TelegramApp или MqttSettingsApp
3. Создайте свою реализацию, следуя пошаговому гайду (раздел 6)
4. Регистрируйте её в AppManager
5. Собирайте проект с флагом ENABLE_CUSTOM_APPS=ON
