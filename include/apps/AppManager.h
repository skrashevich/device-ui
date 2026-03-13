#pragma once

#ifdef HAS_CUSTOM_APPS

#include "apps/ICustomApp.h"
#include <array>
#include <stdint.h>

class AppContext;

/**
 * Registry and lifecycle manager for custom apps.
 * Manages up to MAX_APPS applications.
 */
class AppManager
{
  public:
    static constexpr uint8_t MAX_APPS = 8;

    AppManager();

    /** Register a custom app. Returns false if registry is full. */
    bool registerApp(ICustomApp *app);

    /** Unregister app by name. Calls destroy(). */
    void unregisterApp(const char *name);

    /** Initialize all registered apps with the given context. */
    void initAll(AppContext *ctx);

    /** Called from main loop - ticks the active (visible) app. */
    void tick(uint32_t now_ms);

    /** Forward incoming mesh packet to all apps. */
    void dispatchPacket(const meshtastic_MeshPacket &p);

    /** Show app at given index (creates UI lazily). */
    void showApp(uint8_t index, lv_obj_t *parent);

    /** Hide currently shown app. */
    void hideCurrentApp();

    /** Get number of registered apps. */
    uint8_t getAppCount() const { return appCount; }

    /** Get app by index. */
    ICustomApp *getApp(uint8_t index);

    /** Get index of currently active app (-1 if none). */
    int8_t getActiveIndex() const { return activeIndex; }

  private:
    std::array<ICustomApp *, MAX_APPS> apps;
    std::array<lv_obj_t *, MAX_APPS> panels; // lazily created LVGL panels
    uint8_t appCount;
    int8_t activeIndex;
};

#endif // HAS_CUSTOM_APPS
