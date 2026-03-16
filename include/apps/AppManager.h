#pragma once

#ifdef HAS_CUSTOM_APPS

#include "apps/ICustomApp.h"
#include <stdint.h>

class AppContext;

/**
 * @brief Registry and lifecycle manager for custom apps.
 * Manages up to MAX_APPS concurrent applications.
 */
class AppManager
{
  public:
    static const uint8_t MAX_APPS = 8;

    AppManager();
    ~AppManager();

    /// Register a new app. Returns false if registry is full.
    bool registerApp(ICustomApp *app);

    /// Unregister and destroy an app by name.
    void unregisterApp(const char *name);

    /// Initialize all registered apps with the given context.
    void initAll(AppContext *ctx);

    /// Tick the currently active app.
    void tick(uint32_t now_ms);

    /// Dispatch a mesh packet to all registered apps.
    void dispatchPacket(const meshtastic_MeshPacket &p);

    /// Show app at index, creating UI if needed. Parent is the content panel.
    void showApp(uint8_t index, lv_obj_t *parent);

    /// Hide the currently active app.
    void hideCurrentApp();

    /// Get app at index (nullptr if out of range).
    ICustomApp *getApp(uint8_t index) const;

    /// Get number of registered apps.
    uint8_t getAppCount() const { return appCount; }

    /// Get index of currently active app (-1 if none).
    int8_t getActiveIndex() const { return activeIndex; }

  private:
    ICustomApp *apps[MAX_APPS] = {};
    lv_obj_t *panels[MAX_APPS] = {};
    uint8_t appCount = 0;
    int8_t activeIndex = -1;
};

#endif // HAS_CUSTOM_APPS
