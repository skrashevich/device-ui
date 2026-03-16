#pragma once

#ifdef HAS_CUSTOM_APPS

#include <lvgl.h>
#include <stdint.h>
#include "mesh-pb-constants.h"

class AppContext;

/**
 * @brief Interface for custom applications in device-ui.
 *
 * Lifecycle: init() -> createUI() -> onShow()/onHide() -> onTick() -> destroy()
 * All LVGL operations must happen on the main thread (same as task_handler).
 */
class ICustomApp
{
  public:
    virtual ~ICustomApp() = default;

    /// Human-readable app name (e.g., "Telegram")
    virtual const char *getName() const = 0;

    /// LVGL symbol for the app button (e.g., LV_SYMBOL_ENVELOPE)
    virtual const char *getIcon() const = 0;

    /// Called once after registration. Return false to indicate init failure.
    virtual bool init(AppContext *ctx) = 0;

    /// Create LVGL UI inside the given parent container. Called lazily on first show.
    virtual lv_obj_t *createUI(lv_obj_t *parent) = 0;

    /// Called when app panel becomes visible
    virtual void onShow() {}

    /// Called when app panel is hidden
    virtual void onHide() {}

    /// Called periodically (~50ms) while the app is active
    virtual void onTick(uint32_t now_ms) {}

    /// Called for every received mesh packet (regardless of visibility)
    virtual void onMeshPacket(const meshtastic_MeshPacket &p) {}

    /// Cleanup resources. Called before unregistration.
    virtual void destroy() = 0;
};

#endif // HAS_CUSTOM_APPS
