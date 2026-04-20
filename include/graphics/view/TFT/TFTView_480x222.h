#pragma once

#include "graphics/view/TFT/TFTView_Common.h"

/**
 * @brief GUI view for T-Lora Pager (480x222 display)
 * Inherits shared functionality from TFTView_Common.
 * Note: due to static callbacks in lvgl this class is modelled as
 *       a singleton with static callback members
 */
class TFTView_480x222 : public TFTView_Common
{
  public:

  private:
    // view creation only via ViewFactory
    friend class ViewFactory;
    static TFTView_480x222 *instance(void);
    static TFTView_480x222 *instance(const DisplayDriverConfig &cfg);
    TFTView_480x222(const DisplayDriverConfig *cfg, DisplayDriver *driver);

    // view-specific virtual overrides
    void configureKeyboardLayouts() override;
    void ui_set_active(lv_obj_t *b, lv_obj_t *p, lv_obj_t *tp) override;
    void onEventsInitExtra() override;
    void onInitScreensExtra() override;
    void onNewMessageExtra(uint32_t from, bool isDM) override;
    void onUnreadMessagesUpdated() override;
    int getExitCode() override { return 0; }

    // view-specific static callbacks
    static void ui_event_MainButtonFocus(lv_event_t *e);
    static void ui_event_GlobalKeyHandler(lv_event_t *e);

    static TFTView_480x222 *gui;
};
