#ifdef HAS_CUSTOM_APPS

#include "apps/AppManager.h"
#include "apps/AppContext.h"
#include <cstring>

AppManager::AppManager() {}

AppManager::~AppManager()
{
    for (uint8_t i = 0; i < appCount; i++) {
        if (apps[i]) {
            apps[i]->destroy();
            delete apps[i];
            apps[i] = nullptr;
        }
    }
}

bool AppManager::registerApp(ICustomApp *app)
{
    if (!app || appCount >= MAX_APPS)
        return false;
    apps[appCount++] = app;
    return true;
}

void AppManager::unregisterApp(const char *name)
{
    for (uint8_t i = 0; i < appCount; i++) {
        if (apps[i] && strcmp(apps[i]->getName(), name) == 0) {
            if (activeIndex == i) {
                hideCurrentApp();
            }
            apps[i]->destroy();
            if (panels[i]) {
                lv_obj_delete(panels[i]);
                panels[i] = nullptr;
            }
            // Shift remaining apps
            for (uint8_t j = i; j < appCount - 1; j++) {
                apps[j] = apps[j + 1];
                panels[j] = panels[j + 1];
            }
            apps[appCount - 1] = nullptr;
            panels[appCount - 1] = nullptr;
            appCount--;
            if (activeIndex >= appCount)
                activeIndex = -1;
            return;
        }
    }
}

void AppManager::initAll(AppContext *ctx)
{
    for (uint8_t i = 0; i < appCount; i++) {
        if (apps[i]) {
            apps[i]->init(ctx);
        }
    }
}

void AppManager::tick(uint32_t now_ms)
{
    if (activeIndex >= 0 && activeIndex < appCount && apps[activeIndex]) {
        apps[activeIndex]->onTick(now_ms);
    }
}

void AppManager::dispatchPacket(const meshtastic_MeshPacket &p)
{
    for (uint8_t i = 0; i < appCount; i++) {
        if (apps[i]) {
            apps[i]->onMeshPacket(p);
        }
    }
}

void AppManager::showApp(uint8_t index, lv_obj_t *parent)
{
    if (index >= appCount || !apps[index])
        return;

    // Hide current app
    hideCurrentApp();

    // Create UI lazily
    if (!panels[index] && parent) {
        panels[index] = apps[index]->createUI(parent);
    }

    // Show panel
    if (panels[index]) {
        lv_obj_remove_flag(panels[index], LV_OBJ_FLAG_HIDDEN);
    }

    activeIndex = index;
    apps[index]->onShow();
}

void AppManager::hideCurrentApp()
{
    if (activeIndex >= 0 && activeIndex < appCount) {
        if (apps[activeIndex]) {
            apps[activeIndex]->onHide();
        }
        if (panels[activeIndex]) {
            lv_obj_add_flag(panels[activeIndex], LV_OBJ_FLAG_HIDDEN);
        }
        activeIndex = -1;
    }
}

ICustomApp *AppManager::getApp(uint8_t index) const
{
    if (index >= appCount)
        return nullptr;
    return apps[index];
}

#endif // HAS_CUSTOM_APPS
