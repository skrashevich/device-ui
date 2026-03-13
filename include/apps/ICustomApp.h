#pragma once

#ifdef HAS_CUSTOM_APPS

#include "lvgl.h"
#include "mesh-pb-constants.h"

class AppContext;

/**
 * Interface for custom applications running within device-ui.
 * Each app gets its own LVGL panel and lifecycle management.
 */
class ICustomApp
{
  public:
    virtual ~ICustomApp() = default;

    /** Human-readable app name */
    virtual const char *getName() const = 0;

    /** LVGL symbol icon (e.g. LV_SYMBOL_GPS) or nullptr */
    virtual const char *getIcon() const = 0;

    /** Initialize app with services context. Return false on failure. */
    virtual bool init(AppContext *ctx) = 0;

    /** Create LVGL UI inside the given parent container. Return the root object. */
    virtual lv_obj_t *createUI(lv_obj_t *parent) = 0;

    /** Called when app panel becomes visible */
    virtual void onShow() {}

    /** Called when app panel is hidden */
    virtual void onHide() {}

    /** Called periodically from main loop (every ~50ms) */
    virtual void onTick(uint32_t now_ms) {}

    /** Called when a mesh packet is received */
    virtual void onMeshPacket(const meshtastic_MeshPacket &p) {}

    /** Cleanup resources */
    virtual void destroy() = 0;
};

#endif // HAS_CUSTOM_APPS
