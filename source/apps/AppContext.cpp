#ifdef HAS_CUSTOM_APPS

#include "apps/AppContext.h"
#include "graphics/common/ViewController.h"
#include "graphics/common/MeshtasticView.h"
#include "util/ILog.h"
#include <cstring>
#include <cstdio>

#if __has_include(<LittleFS.h>)
#include <LittleFS.h>
#define HAS_LITTLEFS
#elif __has_include(<SPIFFS.h>)
#include <SPIFFS.h>
#define HAS_SPIFFS
#endif

AppContext::AppContext(ViewController *ctrl, MeshtasticView *view) : controller(ctrl), view(view) {}

void AppContext::sendTextMessage(uint32_t to, uint8_t channel, const char *msg)
{
    if (controller && msg) {
        time_t now;
        time(&now);
        controller->sendTextMessage(to, channel, 3, (uint32_t)now, 0, false, msg);
    }
}

void AppContext::broadcastMessage(uint8_t channel, const char *msg)
{
    sendTextMessage(0xFFFFFFFF, channel, msg);
}

uint32_t AppContext::getMyNodeNum() const
{
    return myNodeNum;
}

void AppContext::setKeyboardRequestFn(KeyboardRequestFn fn, void *ctx)
{
    kbRequestFn = fn;
    kbRequestCtx = ctx;
}

void AppContext::requestKeyboard(lv_obj_t *textarea)
{
    if (kbRequestFn)
        kbRequestFn(textarea, kbRequestCtx);
}

static bool isValidKvKey(const char *key)
{
    if (!key || key[0] == '\0')
        return false;
    size_t len = strlen(key);
    if (len > 32)
        return false;
    for (size_t i = 0; i < len; i++) {
        if (key[i] == '/' || key[i] == '\\')
            return false;
        if (key[i] == '.' && key[i + 1] == '.' )
            return false;
    }
    return true;
}

bool AppContext::kvStore(const char *key, const char *value)
{
    if (!isValidKvKey(key)) {
        ILOG_WARN("kvStore: rejected invalid key");
        return false;
    }
    if (!value) {
        ILOG_WARN("kvStore: null value for key '%s'", key);
        return false;
    }
#ifdef HAS_LITTLEFS
    LittleFS.mkdir("/apps");
    LittleFS.mkdir("/apps/kv");
    char path[64];
    snprintf(path, sizeof(path), "/apps/kv/%s", key);
    File f = LittleFS.open(path, "w");
    if (!f)
        return false;
    f.print(value);
    f.close();
    return true;
#elif defined(HAS_SPIFFS)
    char path[64];
    snprintf(path, sizeof(path), "/apps/kv/%s", key);
    File f = SPIFFS.open(path, "w");
    if (!f)
        return false;
    f.print(value);
    f.close();
    return true;
#else
    (void)key;
    (void)value;
    return false;
#endif
}

std::string AppContext::kvLoad(const char *key)
{
    if (!isValidKvKey(key)) {
        ILOG_WARN("kvLoad: rejected invalid key");
        return "";
    }
#ifdef HAS_LITTLEFS
    char path[64];
    snprintf(path, sizeof(path), "/apps/kv/%s", key);
    File f = LittleFS.open(path, "r");
    if (!f)
        return "";
    std::string result = f.readString().c_str();
    f.close();
    return result;
#elif defined(HAS_SPIFFS)
    char path[64];
    snprintf(path, sizeof(path), "/apps/kv/%s", key);
    File f = SPIFFS.open(path, "r");
    if (!f)
        return "";
    std::string result = f.readString().c_str();
    f.close();
    return result;
#else
    (void)key;
    return "";
#endif
}

#endif // HAS_CUSTOM_APPS
