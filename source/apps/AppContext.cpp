#ifdef HAS_CUSTOM_APPS

#include "apps/AppContext.h"
#include "graphics/common/MeshtasticView.h"
#include "graphics/common/ViewController.h"

#if __has_include(<LittleFS.h>)
#include <LittleFS.h>
#define HAS_LITTLEFS 1
#elif __has_include(<SPIFFS.h>)
#include <SPIFFS.h>
#define HAS_SPIFFS 1
#endif

AppContext::AppContext(ViewController *controller, MeshtasticView *view) : controller(controller), view(view) {}

void AppContext::sendTextMessage(uint32_t to, uint8_t channel, const char *msg)
{
    if (controller) {
        uint32_t now = (uint32_t)time(nullptr);
        controller->sendTextMessage(to, channel, 3, now, 0, false, msg);
    }
}

void AppContext::broadcastMessage(uint8_t channel, const char *msg)
{
    sendTextMessage(0xFFFFFFFF, channel, msg);
}

uint32_t AppContext::getMyNodeNum() const
{
    if (view) {
        return view->getMyNodeNum();
    }
    return 0;
}

bool AppContext::kvStore(const char *key, const char *value)
{
#if HAS_LITTLEFS
    std::string path = std::string("/apps/kv/") + key;
    File f = LittleFS.open(path.c_str(), "w");
    if (!f)
        return false;
    f.print(value);
    f.close();
    return true;
#elif HAS_SPIFFS
    std::string path = std::string("/apps/kv/") + key;
    File f = SPIFFS.open(path.c_str(), "w");
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
#if HAS_LITTLEFS
    std::string path = std::string("/apps/kv/") + key;
    File f = LittleFS.open(path.c_str(), "r");
    if (!f)
        return "";
    std::string result = f.readString().c_str();
    f.close();
    return result;
#elif HAS_SPIFFS
    std::string path = std::string("/apps/kv/") + key;
    File f = SPIFFS.open(path.c_str(), "r");
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
