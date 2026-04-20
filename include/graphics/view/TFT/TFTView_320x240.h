#pragma once

#include "graphics/view/TFT/TFTView_Common.h"

/**
 * @brief GUI view for e.g. T-Deck (320x240 display)
 * Inherits shared functionality from TFTView_Common.
 * Note: due to static callbacks in lvgl this class is modelled as
 *       a singleton with static callback members
 */
class TFTView_320x240 : public TFTView_Common
{
  public:

  private:
    // view creation only via ViewFactory
    friend class ViewFactory;
    static TFTView_320x240 *instance(void);
    static TFTView_320x240 *instance(const DisplayDriverConfig &cfg);
    TFTView_320x240(const DisplayDriverConfig *cfg, DisplayDriver *driver);

    // view-specific virtual overrides
    void configureKeyboardLayouts() override;
    void ui_set_active(lv_obj_t *b, lv_obj_t *p, lv_obj_t *tp) override;
    void onEventsInitExtra() override;
    int getChannelButtonWidth() override;
    int getExitCode() override { return 2; }

    // view-specific static callbacks
    static void ui_event_GlobalKeyHandler(lv_event_t *e);

    static TFTView_320x240 *gui;
};
