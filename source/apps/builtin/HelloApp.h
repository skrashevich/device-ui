#pragma once

#ifdef HAS_CUSTOM_APPS

#include "apps/ICustomApp.h"
#include <string>

/**
 * Simple "Hello Mesh" app to validate the custom apps framework.
 * Displays node info and recent mesh messages.
 */
class HelloApp : public ICustomApp
{
  public:
    const char *getName() const override { return "Hello Mesh"; }
    const char *getIcon() const override { return LV_SYMBOL_GPS; }
    bool init(AppContext *ctx) override;
    lv_obj_t *createUI(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onTick(uint32_t now_ms) override;
    void onMeshPacket(const meshtastic_MeshPacket &p) override;
    void destroy() override;

  private:
    AppContext *ctx = nullptr;
    lv_obj_t *panel = nullptr;
    lv_obj_t *nodeLabel = nullptr;
    lv_obj_t *msgList = nullptr;
    lv_obj_t *statusLabel = nullptr;
    uint32_t lastUpdate = 0;
    uint32_t msgCount = 0;
};

#endif // HAS_CUSTOM_APPS
