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
    void addOrUpdateNode(uint32_t nodeNum, uint8_t channel, uint32_t lastHeard, const meshtastic_User &cfg) override;
    void addNode(uint32_t nodeNum, uint8_t channel, const char *userShort, const char *userLong, uint32_t lastHeard, eRole role,
                 bool hasKey, bool unmessagable) override;
    void updateNode(uint32_t nodeNum, uint8_t channel, const meshtastic_User &cfg) override;
    void updateMetrics(uint32_t nodeNum, uint32_t bat_level, float voltage, float chUtil, float airUtil) override;
    void updateSignalStrength(uint32_t nodeNum, int32_t rssi, float snr) override;
    void updateHopsAway(uint32_t nodeNum, uint8_t hopsAway) override;
    void updateConnectionStatus(const meshtastic_DeviceConnectionStatus &status) override;
    void removeNode(uint32_t nodeNum) override;

    void updateChannelConfig(const meshtastic_Channel &ch) override;
    void updateTime(uint32_t time) override;

    void newMessage(uint32_t from, uint32_t to, uint8_t ch, const char *msg, uint32_t &msgtime, bool restore = true) override;
    void restoreMessage(const LogMessage &msg) override;

  protected:
    void setGroupFocus(lv_obj_t *panel);

    void purgeNode(uint32_t nodeNum);
    bool applyNodesFilter(uint32_t nodeNum, bool reset = false);
    void setNodeImage(uint32_t nodeNum, eRole role, bool unmessagable, lv_obj_t *img);
    void updateNodesStatus(void);

    lv_obj_t *newMessageContainer(uint32_t from, uint32_t to, uint8_t ch);
    void newMessage(uint32_t nodeNum, lv_obj_t *container, uint8_t channel, const char *msg);
    void addChat(uint32_t from, uint32_t to, uint8_t ch);
    void showMessages(uint8_t channel);
    void showMessages(uint32_t nodeNum);
    void highlightChat(uint32_t from, uint32_t to, uint8_t ch);
    void updateActiveChats(void);
    void showMessagePopup(uint32_t from, uint32_t to, uint8_t ch, const char *name);

    void updateTime(void);
    void updateSignalStrength(int32_t rssi, float snr);

    void backup(uint32_t option);
    void restore(uint32_t option);

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
