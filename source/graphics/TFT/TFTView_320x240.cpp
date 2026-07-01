#if HAS_TFT && defined(VIEW_320x240) || defined(VIEW_240x320)

#include "graphics/view/TFT/TFTView_320x240.h"
#include "Arduino.h"
#include "graphics/common/BatteryLevel.h"
#include "graphics/common/LoRaPresets.h"
#include "graphics/common/Ringtones.h"
#include "graphics/common/ViewController.h"
#include "graphics/driver/DisplayDriver.h"
#include "graphics/driver/DisplayDriverFactory.h"
#include "graphics/map/MapPanel.h"
#include "graphics/map/MapTileSettings.h"
#include "graphics/view/TFT/Themes.h"
#include "images.h"
#include "input/I2CKeyboardInputDriver.h"
#include "input/InputDriver.h"
#include "lv_i18n.h"
#include "lvgl_private.h"
#include "styles.h"
#include "ui.h"
#include "util/FileLoader.h"
#include "util/ILog.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iomanip>
#include <list>
#include <locale>
#include <random>
#include <sstream>
#include <time.h>

#if defined(ARCH_PORTDUINO)
#include "PortduinoFS.h"
fs::FS &fileSystem = PortduinoFS;
#else
#include "LittleFS.h"
fs::FS &fileSystem = LittleFS;
#endif

#if defined(ARCH_PORTDUINO)
#include "util/LinuxHelper.h"
// #include "graphics/map/LinuxFileSystemService.h"
#include "graphics/map/SDCardService.h"
#elif defined(HAS_SD_MMC)
#include "graphics/map/SDCardService.h"
#else
#include "graphics/map/SdFatService.h"
#endif
#include "graphics/map/URLService.h"
#include "graphics/common/SdCard.h"

#ifndef MAX_NUM_NODES_VIEW
#define MAX_NUM_NODES_VIEW 250
#endif

#ifndef PACKET_LOGS_MAX
#define PACKET_LOGS_MAX 200
#endif

LV_IMAGE_DECLARE(img_circle_image);
LV_IMAGE_DECLARE(img_no_tile_image);
LV_IMAGE_DECLARE(node_location_pin24_image);

#define CR_REPLACEMENT 0x0C              // dummy to record several lines in a one line textarea
#define THIS TFTView_320x240::instance() // need to use this in all static methods

// forward declarations for free functions defined in TFTView_Common.cpp
void syncVirtualKeyboardLayout(lv_obj_t *keyboard);
const lv_font_t *getKeyboardFont(void);

// Color constants and LV_COLOR_HEX defined in TFTView_Common.cpp
// Forward-declare the ones used in this file
extern const lv_color_t colorGray;
extern const lv_color_t colorMesh;
extern const lv_color_t colorDarkGray;

#define KEYBOARD_CTRL_BUTTON_FLAGS                                                                                                 \
    (LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG | LV_BUTTONMATRIX_CTRL_CHECKED)
#define KEYBOARD_CTRL(value) static_cast<lv_buttonmatrix_ctrl_t>(value)
#define KEYBOARD_BTN(width) KEYBOARD_CTRL(LV_BUTTONMATRIX_CTRL_POPOVER | (width))
#define KEYBOARD_BTN_CHECKED(width) KEYBOARD_CTRL(LV_BUTTONMATRIX_CTRL_CHECKED | (width))
#define KEYBOARD_BTN_CHECKED_POPOVER(width)                                                                                        \
    KEYBOARD_CTRL(LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_POPOVER | (width))
#define KEYBOARD_BTN_ACTION(width) KEYBOARD_CTRL(KEYBOARD_CTRL_BUTTON_FLAGS | (width))

static const char *const kb_map_en_lower[] = {"1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[",
                                               LV_SYMBOL_BACKSPACE, "\n", "ABC", "a", "s", "d", "f", "g", "h",
                                               "j", "k", "l", ";", "'", LV_SYMBOL_NEW_LINE, "\n", "/", "z", "x",
                                               "c", "v", "b", "n", "m", ",", ".", "?", "-", "_", "\n",
                                               LV_SYMBOL_KEYBOARD, "RU", ",", " ", ".", LV_SYMBOL_OK, ""};

static const char *const kb_map_en_upper[] = {"1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[",
                                               LV_SYMBOL_BACKSPACE, "\n", "abc", "A", "S", "D", "F", "G", "H",
                                               "J", "K", "L", ";", "'", LV_SYMBOL_NEW_LINE, "\n", "/", "Z", "X",
                                               "C", "V", "B", "N", "M", ",", ".", "?", "-", "_", "\n",
                                               LV_SYMBOL_KEYBOARD, "RU", ",", " ", ".", LV_SYMBOL_OK, ""};

static const char *const kb_map_ru_lower[] = {
    "1#", "\xD0\xB9", "\xD1\x86", "\xD1\x83", "\xD0\xBA", "\xD0\xB5", "\xD0\xBD", "\xD0\xB3", "\xD1\x88", "\xD1\x89",
    "\xD0\xB7", "\xD1\x85", LV_SYMBOL_BACKSPACE, "\n", "ABC", "\xD1\x84", "\xD1\x8B", "\xD0\xB2", "\xD0\xB0", "\xD0\xBF",
    "\xD1\x80", "\xD0\xBE", "\xD0\xBB", "\xD0\xB4", "\xD0\xB6", "\xD1\x8D", LV_SYMBOL_NEW_LINE, "\n", "/", "\xD1\x91",
    "\xD1\x8F", "\xD1\x87", "\xD1\x81", "\xD0\xBC", "\xD0\xB8", "\xD1\x82", "\xD1\x8C", "\xD0\xB1", "\xD1\x8E", "\xD1\x8A",
    ".", "\n", LV_SYMBOL_KEYBOARD, "EN", ",", " ", "-", LV_SYMBOL_OK, ""};

static const char *const kb_map_ru_upper[] = {
    "1#", "\xD0\x99", "\xD0\xA6", "\xD0\xA3", "\xD0\x9A", "\xD0\x95", "\xD0\x9D", "\xD0\x93", "\xD0\xA8", "\xD0\xA9",
    "\xD0\x97", "\xD0\xA5", LV_SYMBOL_BACKSPACE, "\n", "abc", "\xD0\xA4", "\xD0\xAB", "\xD0\x92", "\xD0\x90", "\xD0\x9F",
    "\xD0\xA0", "\xD0\x9E", "\xD0\x9B", "\xD0\x94", "\xD0\x96", "\xD0\xAD", LV_SYMBOL_NEW_LINE, "\n", "/", "\xD0\x81",
    "\xD0\xAF", "\xD0\xA7", "\xD0\xA1", "\xD0\x9C", "\xD0\x98", "\xD0\xA2", "\xD0\xAC", "\xD0\x91", "\xD0\xAE", "\xD0\xAA",
    ".", "\n", LV_SYMBOL_KEYBOARD, "EN", ",", " ", "-", LV_SYMBOL_OK, ""};

static const lv_buttonmatrix_ctrl_t kb_ctrl_map[] = {
    KEYBOARD_BTN_ACTION(5),
    KEYBOARD_BTN(4),
    KEYBOARD_BTN(4),
    KEYBOARD_BTN(4),
    KEYBOARD_BTN(4),
    KEYBOARD_BTN(4),
    KEYBOARD_BTN(4),
    KEYBOARD_BTN(4),
    KEYBOARD_BTN(4),
    KEYBOARD_BTN(4),
    KEYBOARD_BTN(4),
    KEYBOARD_BTN(4),
    KEYBOARD_BTN_CHECKED(7),

    KEYBOARD_BTN_ACTION(6),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN_CHECKED(7),

    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN(3),
    KEYBOARD_BTN_CHECKED_POPOVER(3),

    KEYBOARD_BTN_ACTION(2),
    KEYBOARD_BTN_ACTION(2),
    KEYBOARD_BTN(2),
    KEYBOARD_CTRL(4),
    KEYBOARD_BTN(2),
    KEYBOARD_BTN_ACTION(2),
};

static void configureKeyboardLayouts(lv_obj_t *keyboard)
{
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_en_lower, kb_ctrl_map);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_en_upper, kb_ctrl_map);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_1, kb_map_ru_lower, kb_ctrl_map);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_2, kb_map_ru_upper, kb_ctrl_map);
}

// NodePanelIdx and ScrollDirection enums defined in TFTView_Common.cpp

extern const char *firmware_version;

TFTView_320x240 *TFTView_320x240::gui = nullptr;

TFTView_320x240 *TFTView_320x240::instance(void)
{
    if (!gui) {
        gui = new TFTView_320x240(nullptr, DisplayDriverFactory::create(320, 240));
        commonInstance = gui;
    }
    return gui;
}

TFTView_320x240 *TFTView_320x240::instance(const DisplayDriverConfig &cfg)
{
    if (!gui) {
        gui = new TFTView_320x240(&cfg, DisplayDriverFactory::create(cfg));
        commonInstance = gui;
    }
    return gui;
}

TFTView_320x240::TFTView_320x240(const DisplayDriverConfig *cfg, DisplayDriver *driver)
    : TFTView_Common(cfg, driver)
{
}

void TFTView_320x240::ui_set_active(lv_obj_t *b, lv_obj_t *p, lv_obj_t *tp)
{
    if (activeButton) {
        lv_obj_set_style_border_width(activeButton, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        if (Themes::get() == Themes::eDark)
            lv_obj_set_style_bg_img_recolor_opa(activeButton, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_img_recolor(activeButton, colorGray, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_set_style_border_width(b, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_recolor(b, colorMesh, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_recolor_opa(b, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (activePanel) {
        lv_obj_add_flag(activePanel, LV_OBJ_FLAG_HIDDEN);
        if (activePanel == objects.messages_panel) {
            lv_obj_remove_state(objects.message_input_area, LV_STATE_FOCUSED);
            if (!lv_obj_has_flag(objects.keyboard, LV_OBJ_FLAG_HIDDEN)) {
                hideKeyboard(objects.messages_panel);
            }
            uint32_t channelOrNode = (unsigned long)activeMsgContainer->user_data;
            // remove empty messageContainer if we are leaving messages panel
            if (channelOrNode >= c_max_channels) {
                if (activeMsgContainer->spec_attr->child_cnt == 0) {
                    eraseChat(channelOrNode);
                    updateActiveChats();
                    activeMsgContainer = objects.messages_container;
                }
            }
            unreadMessages = 0; // TODO: not all messages may be actually read
            updateUnreadMessages();
        } else if (activePanel == objects.node_options_panel) {
            // we're moving away from node options panel, so save latest settings
            storeNodeOptions();
        }
    }

    lv_obj_clear_flag(p, LV_OBJ_FLAG_HIDDEN);

    if (tp) {
        if (activeTopPanel) {
            lv_obj_add_flag(activeTopPanel, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(tp, LV_OBJ_FLAG_HIDDEN);
        activeTopPanel = tp;
    }

    activeButton = b;
    activePanel = p;
    if (activePanel == objects.messages_panel) {
        lv_group_focus_obj(objects.message_input_area);
    } else if (inputdriver->hasKeyboardDevice() || inputdriver->hasEncoderDevice()) {
        setGroupFocus(activePanel);
    }

    lv_obj_add_flag(objects.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.msg_popup_panel, LV_OBJ_FLAG_HIDDEN);
}

void TFTView_320x240::ui_event_GlobalKeyHandler(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);

        if (key == LV_KEY_HOME) {
            // Focus the home button to enable side menu navigation
            if (objects.home_button) {
                lv_group_focus_obj(objects.home_button);
            }
        } else if (key == LV_KEY_NEXT || key == LV_KEY_PREV) {
            // Screen tab switching: cycle through main screens
            lv_obj_t *tabs[] = {
                objects.home_button, objects.nodes_button, objects.groups_button,
                objects.messages_button, objects.map_button, objects.settings_button
            };
            const int numTabs = 6;

            // Only switch tabs when we're at the main button level (not inside a sub-panel)
            if (THIS->activeSettings != eNone)
                return;

            // Find current active tab
            int currentIdx = -1;
            for (int i = 0; i < numTabs; i++) {
                if (tabs[i] == THIS->activeButton) {
                    currentIdx = i;
                    break;
                }
            }

            if (currentIdx >= 0) {
                int nextIdx;
                if (key == LV_KEY_NEXT)
                    nextIdx = (currentIdx + 1) % numTabs;
                else
                    nextIdx = (currentIdx - 1 + numTabs) % numTabs;

                lv_obj_send_event(tabs[nextIdx], LV_EVENT_CLICKED, NULL);
            }
        }
    }
}

#if 0
date signal strength text and image for home screen
 */
void TFTView_320x240::updateSignalStrength(int32_t rssi, float snr)
{
    // remember time we last heard a node
    time(&lastHeard);

    if (rssi != 0 || snr != 0.0) {
        char buf[40];
        sprintf(buf, "SNR: %.1f\nRSSI: %" PRId32, snr, rssi);
        lv_label_set_text(objects.home_signal_label, buf);

        uint32_t pct = signalStrength2Percent(rssi, snr);
        sprintf(buf, "(%d%%)", pct);
        lv_label_set_text(objects.home_signal_pct_label, buf);
        if (pct > 80) {
            lv_obj_set_style_bg_image_src(objects.home_signal_button, &img_home_signal_button_image,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
        } else if (pct > 60) {
            lv_obj_set_style_bg_image_src(objects.home_signal_button, &img_home_strong_signal_image,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
        } else if (pct > 40) {
            lv_obj_set_style_bg_image_src(objects.home_signal_button, &img_home_good_signal_image,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
        } else if (pct > 20) {
            lv_obj_set_style_bg_image_src(objects.home_signal_button, &img_home_fair_signal_image,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
        } else if (pct > 1) {
            lv_obj_set_style_bg_image_src(objects.home_signal_button, &img_home_weak_signal_image,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_obj_set_style_bg_image_src(objects.home_signal_button, &img_home_no_signal_image, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

/**
 * Translate proto modem preset enum value to numerical position in dropdown menu
 */
/**
 * Translate value from dropdown menu to modem preset proto enum
 */
/**
 * Translate proto role enum value to numerical position in dropdown menu
 */
/**
 * Translate value from dropdown menu to role proto enum
 */
/**
 * Translate language proto enum value to (alphabetical) position in dropdown menu
 */
/**
 * Translate value from dropdown menu to language proto enum
 */
/**
 * @brief Set lv_i18n language
 */
/**
 * @brief Set language (using dropdown strings)
 */
/**
 * @brief Set timeout
 */
/**
 * @brief Set brightness
 */
/**
 * @brief Set theme to new value
 */
/**
 * @brief Save all data from node options panel
 */
/**
 * @brief erase chat and all its resources
 */
/**
 * @brief clears all (persistent) chat messages
 */
/**
 * @brief User widget OK button handling
 *
 * @param e
 */
/**
 * @brief Cancel button user widget handling
 *
 * @param e
 */
// end button event handlers
nimations
**
 * @brief Dynamically show user widget
 *        First a panel is created where the widget is located in, then the widget is drawn.
 *        "active_widget" contains the surrounding panel which must be destroyed
 *        to remove the widget from the screen (e.g. by pressing OK/Cancel).
 *
 * @param func
 */
**
 * display message that has just been written and sent out
 */
void TFTView_320x240::addNode(uint32_t nodeNum, uint8_t ch, const char *userShort, const char *userLong, uint32_t lastHeard,
                              eRole role, bool hasKey, bool unmessagable)
{
    // lv_obj nodesPanel children  |  user data (4 bytes)
    // ==================================================
    // [0]: img                    | role
    // [1]: btn                    | ll group
    // [2]: lbl user long          | nodeNum
    // [3]: lbl user short         | userShort (4 chars)
    // [4]: lbl battery            | hasKey
    // [5]: lbl lastHeard          | lastHeard / curtime
    // [6]: lbl signal (or hops)   | hops away
    // [7]: lbl position 1         | lat
    // [8]: lbl position 2         | lon
    // [9]: lbl telemetry 1        |
    // [10]: lbl telemetry 2       | iaq
    // panel user_data: ch

    ILOG_DEBUG("addNode(%d): num=0x%08x, lastseen=%d, name=%s(%s), role=%d", nodeCount, nodeNum, lastHeard, userLong, userShort,
               role);
    while (nodeCount >= MAX_NUM_NODES_VIEW) {
        purgeNode(nodeNum);
    }

    lv_obj_t *p = lv_obj_create(objects.nodes_panel);
    lv_ll_t *lv_group_ll = &lv_group_get_default()->obj_ll;

    p->user_data = (void *)(uint32_t)ch;
    nodes[nodeNum] = p;
    nodeCount++;

    // NodePanel
    lv_obj_set_pos(p, LV_PCT(0), 0);
    lv_obj_set_size(p, LV_PCT(100), 53);
    lv_obj_set_align(p, LV_ALIGN_CENTER);
    lv_obj_set_style_pad_top(p, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(p, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(p, lv_obj_flag_t(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_CLICK_FOCUSABLE |
                                        LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_SCROLLABLE));
    add_style_node_panel_style(p);

    // NodeImage
    lv_obj_t *img = lv_img_create(p);
    setNodeImage(nodeNum, role, unmessagable, img);
    lv_obj_set_pos(img, -5, 3);
    lv_obj_set_size(img, 32, 32);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(img, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(img, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (!hasKey) {
        lv_obj_set_style_border_color(img, colorRed, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (unmessagable) {
        // node role icon is not clickable and replaced with a cancelled icon
        img->user_data = (void *)eRole::unmessagable;
    } else {
        img->user_data = (void *)role;
    }

    // NodeButton
    lv_obj_t *nodeButton = lv_btn_create(p);
    lv_obj_set_pos(nodeButton, 0, 0);
    lv_obj_set_size(nodeButton, LV_PCT(106), LV_PCT(100));
    add_style_node_button_style(nodeButton);
    lv_obj_set_align(nodeButton, LV_ALIGN_CENTER);
    lv_obj_add_flag(nodeButton, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_shadow_width(nodeButton, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_max_height(nodeButton, 132, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_min_height(nodeButton, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
    nodeButton->user_data = _lv_ll_get_tail(lv_group_ll);

    // UserNameLabel
    lv_obj_t *ln_lbl = lv_label_create(p);
    lv_obj_set_pos(ln_lbl, -5, 35);
    lv_obj_set_size(ln_lbl, LV_PCT(80), LV_SIZE_CONTENT);
    lv_label_set_long_mode(ln_lbl, LV_LABEL_LONG_SCROLL);
    lv_label_set_text(ln_lbl, userLong);
    ln_lbl->user_data = (void *)nodeNum;
    lv_obj_set_style_align(ln_lbl, LV_ALIGN_TOP_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    // UserNameShortLabel
    lv_obj_t *sn_lbl = lv_label_create(p);
    lv_obj_set_pos(sn_lbl, 30, 10);
    lv_obj_set_size(sn_lbl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_label_set_long_mode(sn_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_align(sn_lbl, LV_ALIGN_TOP_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(sn_lbl, &ui_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    // if short name contains only non-printable glyphs replace with short id
    if (lv_txt_get_width(userShort, strlen(userShort), &ui_font_montserrat_14, 0) <= 4) {
        lv_label_set_text_fmt(sn_lbl, "%04x", nodeNum & 0xffff);
    } else {
        lv_label_set_text(sn_lbl, userShort);
    }
    char *modUserShort = lv_label_get_text(sn_lbl);

    // keep a copy of the (4-byte) short name for use in many other widgets
    char *userData = (char *)&(sn_lbl->user_data);
    userData[0] = modUserShort[0];
    if (userData[0] == 0x00)
        userData[0] = ' ';
    userData[1] = modUserShort[1];
    if (userData[1] == 0x00)
        userData[1] = ' ';
    userData[2] = modUserShort[2];
    if (userData[2] == 0x00)
        userData[2] = ' ';
    userData[3] = modUserShort[3];
    if (userData[3] == 0x00)
        userData[3] = ' ';

    //  BatteryLabel
    lv_obj_t *ui_BatteryLabel = lv_label_create(p);
    lv_obj_set_pos(ui_BatteryLabel, 8, 17);
    lv_obj_set_size(ui_BatteryLabel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_BatteryLabel, LV_ALIGN_TOP_RIGHT);
    lv_label_set_text(ui_BatteryLabel, "");
    lv_obj_set_style_text_align(ui_BatteryLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_BatteryLabel->user_data = (void *)hasKey;
    // LastHeardLabel
    lv_obj_t *ui_lastHeardLabel = lv_label_create(p);
    lv_obj_set_pos(ui_lastHeardLabel, 8, 33);
    lv_obj_set_size(ui_lastHeardLabel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_align(ui_lastHeardLabel, LV_ALIGN_TOP_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(ui_lastHeardLabel, LV_LABEL_LONG_CLIP);

    // TODO: devices without actual time will report all nodes as lastseen = now
    if (lastHeard) {
        lastHeard = std::min(curtime, (time_t)lastHeard); // adapt values too large

        char buf[20];
        bool isOnline = lastHeardToString(lastHeard, buf);
        lv_label_set_text(ui_lastHeardLabel, buf);
        if (isOnline) {
            nodesOnline++;
        }
    } else {
        lv_label_set_text(ui_lastHeardLabel, "");
    }

    lv_obj_set_style_text_align(ui_lastHeardLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_lastHeardLabel->user_data = (void *)lastHeard;
    // SignalLabel / hopsAway
    lv_obj_t *ui_SignalLabel = lv_label_create(p);
    lv_obj_set_width(ui_SignalLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SignalLabel, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_SignalLabel, 8, 1);
    lv_obj_set_align(ui_SignalLabel, LV_ALIGN_TOP_RIGHT);
    lv_label_set_text(ui_SignalLabel, "");
    ui_SignalLabel->user_data = (void *)-1; // TODO viaMqtt; // used for filtering (applyNodesFilter)
    // PositionLabel
    lv_obj_t *ui_PositionLabel = lv_label_create(p);
    lv_obj_set_pos(ui_PositionLabel, -5, 49);
    lv_obj_set_size(ui_PositionLabel, 120, LV_SIZE_CONTENT);
    lv_label_set_long_mode(ui_PositionLabel, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ui_PositionLabel, "");
    lv_obj_set_style_align(ui_PositionLabel, LV_ALIGN_TOP_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_PositionLabel, colorBlueGreen, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_PositionLabel->user_data = 0; // store latitude
    // Position2Label
    lv_obj_t *ui_Position2Label = lv_label_create(p);
    lv_obj_set_pos(ui_Position2Label, -5, 63);
    lv_obj_set_size(ui_Position2Label, 108, LV_SIZE_CONTENT);
    lv_label_set_long_mode(ui_Position2Label, LV_LABEL_LONG_SCROLL);
    lv_label_set_text(ui_Position2Label, "");
    lv_obj_set_style_align(ui_Position2Label, LV_ALIGN_TOP_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_Position2Label->user_data = 0; // store longitude
    // Telemetry1Label
    lv_obj_t *ui_Telemetry1Label = lv_label_create(p);
    lv_obj_set_pos(ui_Telemetry1Label, 8, 49);
    lv_obj_set_size(ui_Telemetry1Label, 130, LV_SIZE_CONTENT);
    lv_label_set_long_mode(ui_Telemetry1Label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ui_Telemetry1Label, "");
    lv_obj_set_style_align(ui_Telemetry1Label, LV_ALIGN_TOP_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_Telemetry1Label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    // Telemetry2Label
    lv_obj_t *ui_Telemetry2Label = lv_label_create(p);
    lv_obj_set_pos(ui_Telemetry2Label, 8, 63);
    lv_obj_set_size(ui_Telemetry2Label, 130, LV_SIZE_CONTENT);
    lv_label_set_long_mode(ui_Telemetry2Label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ui_Telemetry2Label, "");
    lv_obj_set_style_align(ui_Telemetry2Label, LV_ALIGN_TOP_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_Telemetry2Label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);

    // optimisation: hide all 6ix extended labels by default; enable only when set
    // lv_obj_add_flag(ui_lastHeardLabel, LV_OBJ_FLAG_HIDDEN); // lastHeard
    lv_obj_add_flag(ui_BatteryLabel, LV_OBJ_FLAG_HIDDEN); // Autohide battery
    lv_obj_add_flag(ui_SignalLabel, LV_OBJ_FLAG_HIDDEN);  // Autohide signal/hops
    lv_obj_add_flag(ui_PositionLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_Position2Label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_Telemetry1Label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_Telemetry2Label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(nodeButton, ui_event_NodeButton, LV_EVENT_ALL, (void *)nodeNum);

    // move node into new position within nodePanel
    if (lastHeard) {
        lv_obj_t **children = objects.nodes_panel->spec_attr->children;
        int i = objects.nodes_panel->spec_attr->child_cnt - 1;
        while (i > 1) {
            if (lastHeard <= (time_t)(children[i - 1]->LV_OBJ_IDX(node_lh_idx)->user_data))
                break;
            i--;
        }
        if (i >= 1 && i < objects.nodes_panel->spec_attr->child_cnt - 1) {
            lv_obj_move_to_index(p, i);
            // re-arrange the group linked list by moving the new button (now at the tail) into the right position
            void *after = children[i + 1]->LV_OBJ_IDX(node_btn_idx)->user_data;
            _lv_ll_move_before(lv_group_ll, nodeButton->user_data, after);
        }
    }

    if (!nodesChanged) {
        applyNodesFilter(nodeNum);
        updateNodesStatus();
    }
}
void TFTView_320x240::addOrUpdateNode(uint32_t nodeNum, uint8_t channel, uint32_t lastHeard, const meshtastic_User &cfg)
{
    if (nodes.find(nodeNum) == nodes.end()) {
        addNode(nodeNum, channel, cfg.short_name, cfg.long_name, lastHeard, (MeshtasticView::eRole)cfg.role,
                cfg.public_key.size != 0, cfg.has_is_unmessagable && cfg.is_unmessagable);
    } else {
        updateNode(nodeNum, channel, cfg);
    }
}

/**
 * @brief update node userName and image
 *
 * @param nodeNum
 * @param ch
 * @param userShort
 * @param userLong
 * @param lastHeard
 * @param role
 * @param viaMqtt
 */
// void TFTView_320x240::updateNode(uint32_t nodeNum, uint8_t ch, const char *userShort, const char *userLong, uint32_t lastHeard,
//                                  eRole role, bool hasKey, bool viaMqtt)
void TFTView_320x240::updateNode(uint32_t nodeNum, uint8_t ch, const meshtastic_User &cfg)
{
    db.user = cfg;
    auto it = nodes.find(nodeNum);
    if (it != nodes.end() && it->second) {
        if (it->first == ownNode) {
            // update related settings buttons and store role in image user data
            char buf[30];
            lv_snprintf(buf, sizeof(buf), _("User name: %s"), cfg.short_name);
            lv_label_set_text(objects.basic_settings_user_label, buf);

            char buf1[30], buf2[40];
            lv_dropdown_set_selected(objects.settings_device_role_dropdown,
                                     role2val(meshtastic_Config_DeviceConfig_Role(cfg.role)));
            lv_dropdown_get_selected_str(objects.settings_device_role_dropdown, buf1, sizeof(buf1));
            lv_snprintf(buf2, sizeof(buf2), _("Device Role: %s"), buf1);
            lv_label_set_text(objects.basic_settings_role_label, buf2);

            // update DB
            strcpy(db.short_name, cfg.short_name);
            strcpy(db.long_name, cfg.long_name);
            db.config.device.role = cfg.role;
        }
        lv_label_set_text(it->second->LV_OBJ_IDX(node_lbl_idx), cfg.long_name);
        it->second->LV_OBJ_IDX(node_lbl_idx)->user_data = (void *)nodeNum;
        lv_label_set_text(it->second->LV_OBJ_IDX(node_lbs_idx), cfg.short_name);
        char *userData = (char *)&(it->second->LV_OBJ_IDX(node_lbs_idx)->user_data);
        userData[0] = cfg.short_name[0];
        if (userData[0] == 0x00)
            userData[0] = ' ';
        userData[1] = cfg.short_name[1];
        if (userData[1] == 0x00)
            userData[1] = ' ';
        userData[2] = cfg.short_name[2];
        if (userData[2] == 0x00)
            userData[2] = ' ';
        userData[3] = cfg.short_name[3];
        if (userData[3] == 0x00)
            userData[3] = ' ';

        setNodeImage(nodeNum, (MeshtasticView::eRole)cfg.role, cfg.has_is_unmessagable && cfg.is_unmessagable,
                     it->second->LV_OBJ_IDX(node_img_idx));

        if (cfg.public_key.size != 0) {
            // set border color to bg color
            lv_color_t color = lv_obj_get_style_bg_color(it->second->LV_OBJ_IDX(node_img_idx), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(it->second->LV_OBJ_IDX(node_img_idx), color, LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_obj_set_style_border_color(it->second->LV_OBJ_IDX(node_img_idx), colorRed, LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        // update chat name
        auto ct = chats.find(it->first);
        if (ct != chats.end()) {
            char buf[64];
            lv_snprintf(buf, sizeof(buf), "%s: %s", lv_label_get_text(it->second->LV_OBJ_IDX(node_lbs_idx)),
                        lv_label_get_text(it->second->LV_OBJ_IDX(node_lbl_idx)));
            lv_label_set_text(ct->second->spec_attr->children[0], buf);
        }
    }
}
**
 * @brief Update battery level and air utilisation
 *
 * @param nodeNum
 * @param bat_level
 * @param voltage
 * @param chUtil
 * @param airUtil
 */
void TFTView_320x240::updateMetrics(uint32_t nodeNum, uint32_t bat_level, float voltage, float chUtil, float airUtil)
{
    auto it = nodes.find(nodeNum);
    if (it != nodes.end()) {
        char buf[48];
        if (it->first == ownNode) {
            sprintf(buf, _("Util %0.1f%%  Air %0.1f%%"), chUtil, airUtil);
            lv_label_set_text(it->second->LV_OBJ_IDX(node_sig_idx), buf);

            // update battery percentage and symbol
            if (bat_level != 0 || voltage != 0) {
                uint32_t shown_level = std::min(bat_level, (uint32_t)100);
                sprintf(buf, "%d%%", shown_level);
                bool alert = false;

                BatteryLevel level;
                BatteryLevel::Status status = level.calcStatus(bat_level, voltage);
                switch (status) {
                case BatteryLevel::Plugged:
                    lv_obj_set_style_bg_image_src(objects.battery_image, &img_battery_plug_image,
                                                  LV_PART_MAIN | LV_STATE_DEFAULT);
                    if (shown_level == 100)
                        buf[0] = '\0';
                    break;
                case BatteryLevel::Charging:
                    lv_obj_set_style_bg_image_src(objects.battery_image, &img_battery_bolt_image,
                                                  LV_PART_MAIN | LV_STATE_DEFAULT);
                    break;
                case BatteryLevel::Full:
                    lv_obj_set_style_bg_image_src(objects.battery_image, &img_battery_full_image,
                                                  LV_PART_MAIN | LV_STATE_DEFAULT);
                    break;
                case BatteryLevel::Mid:
                    lv_obj_set_style_bg_image_src(objects.battery_image, &img_battery_mid_image, LV_PART_MAIN | LV_STATE_DEFAULT);
                    break;
                case BatteryLevel::Low:
                    lv_obj_set_style_bg_image_src(objects.battery_image, &img_battery_low_image, LV_PART_MAIN | LV_STATE_DEFAULT);
                    break;
                case BatteryLevel::Empty:
                    lv_obj_set_style_bg_image_src(objects.battery_image, &img_battery_empty_image,
                                                  LV_PART_MAIN | LV_STATE_DEFAULT);
                    break;
                case BatteryLevel::Warn:
                    lv_obj_set_style_bg_image_src(objects.battery_image, &img_battery_empty_warn_image,
                                                  LV_PART_MAIN | LV_STATE_DEFAULT);
                    buf[0] = '\0';
                    alert = true;
                    break;
                default:
                    ILOG_ERROR("unhandled battery level %d", status);
                    break;
                }
                Themes::recolorTopLabel(objects.battery_percentage_label, alert);
                lv_obj_set_style_bg_image_recolor_opa(objects.battery_image, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(objects.battery_percentage_label, buf);
            }
        }

        if (bat_level != 0 || voltage != 0) {
            bat_level = std::min(bat_level, (uint32_t)100);
            sprintf(buf, "%d%% %0.2fV", bat_level, voltage);
            lv_label_set_text(it->second->LV_OBJ_IDX(node_bat_idx), buf);
            lv_obj_remove_flag(it->second->LV_OBJ_IDX(node_bat_idx), LV_OBJ_FLAG_HIDDEN);
        }
    }
}
*
 * update signal strength for direct neighbors
 */
void TFTView_320x240::updateSignalStrength(uint32_t nodeNum, int32_t rssi, float snr)
{
    if (nodeNum != ownNode) {
        auto it = nodes.find(nodeNum);
        if (it != nodes.end()) {
            char buf[32];
            if (rssi == 0 && snr == 0.0) {
                buf[0] = '\0';
            } else {
                sprintf(buf, "rssi: %d snr: %.1f", rssi, snr);
            }
            lv_label_set_text(it->second->LV_OBJ_IDX(node_sig_idx), buf);
            it->second->LV_OBJ_IDX(node_sig_idx)->user_data = 0;
            lv_obj_remove_flag(it->second->LV_OBJ_IDX(node_sig_idx), LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void TFTView_320x240::updateHopsAway(uint32_t nodeNum, uint8_t hopsAway)
{
    if (nodeNum != ownNode) {
        auto it = nodes.find(nodeNum);
        if (it != nodes.end()) {
            char buf[32];
            sprintf(buf, _("hops: %d"), (int)hopsAway);
            lv_label_set_text(it->second->LV_OBJ_IDX(node_sig_idx), buf);
            it->second->LV_OBJ_IDX(node_sig_idx)->user_data = (void *)(unsigned long)hopsAway;
            lv_obj_remove_flag(it->second->LV_OBJ_IDX(node_sig_idx), LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void TFTView_320x240::updateConnectionStatus(const meshtastic_DeviceConnectionStatus &status)
{
    db.connectionStatus = status;
    if (status.has_wifi) {
        if (db.config.network.wifi_enabled || db.config.network.eth_enabled) {
            if (status.wifi.has_status) {
                char buf[20];
                uint32_t ip = status.wifi.status.ip_address;
                sprintf(buf, "%d.%d.%d.%d", ip & 0xff, (ip & 0xff00) >> 8, (ip & 0xff0000) >> 16, (ip & 0xff000000) >> 24);
                lv_label_set_text(objects.home_wlan_label, buf);
                Themes::recolorButton(objects.home_wlan_button, true);
                Themes::recolorText(objects.home_wlan_label, true);
                if (status.wifi.status.is_connected) {
                    lv_obj_set_style_bg_img_src(objects.home_wlan_button, &img_home_wlan_button_image,
                                                LV_PART_MAIN | LV_STATE_DEFAULT);
                } else {
                    lv_obj_set_style_bg_img_src(objects.home_wlan_button, &img_home_wlan_off_image,
                                                LV_PART_MAIN | LV_STATE_DEFAULT);
                }

                if (status.wifi.status.is_mqtt_connected) {
                    Themes::recolorButton(objects.home_mqtt_button, true, 255);
                    Themes::recolorText(objects.home_mqtt_label, true);
                } else {
                    Themes::recolorButton(objects.home_mqtt_button, db.module_config.mqtt.enabled);
                    Themes::recolorText(objects.home_mqtt_label, false);
                }
            }
        } else {
            Themes::recolorButton(objects.home_wlan_button, false);
            Themes::recolorText(objects.home_wlan_label, false);
            if (status.wifi.status.is_mqtt_connected) {
                Themes::recolorButton(objects.home_mqtt_button, true, 255);
                Themes::recolorText(objects.home_mqtt_label, true);
            } else {
                Themes::recolorButton(objects.home_mqtt_button, db.module_config.mqtt.enabled, 100);
                Themes::recolorText(objects.home_mqtt_label, false);
            }
            lv_obj_set_style_bg_img_src(objects.home_wlan_button, &img_home_wlan_off_image, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    } else {
        lv_obj_add_flag(objects.home_wlan_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(objects.home_wlan_button, LV_OBJ_FLAG_HIDDEN);
    }

    if (status.has_bluetooth) {
        if (db.config.bluetooth.enabled) {
            if (status.bluetooth.is_connected) {
                char buf[20];
                uint32_t mac = ownNode;
                lv_obj_set_style_text_color(objects.home_bluetooth_label, colorLightGray, LV_PART_MAIN | LV_STATE_DEFAULT);
                sprintf(buf, "??:??:%02x:%02x:%02x:%02x", mac & 0xff, (mac & 0xff00) >> 8, (mac & 0xff0000) >> 16,
                        (mac & 0xff000000) >> 24);
                lv_label_set_text(objects.home_bluetooth_label, buf);
                lv_obj_set_style_bg_opa(objects.home_bluetooth_button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_img_src(objects.home_bluetooth_button, &img_home_bluetooth_on_button_image,
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
            } else {
                lv_obj_set_style_text_color(objects.home_bluetooth_label, colorMidGray, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_img_src(objects.home_bluetooth_button, &img_home_bluetooth_on_button_image,
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_img_recolor_opa(objects.home_bluetooth_button, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        } else {
            lv_obj_set_style_text_color(objects.home_bluetooth_label, colorMidGray, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_img_src(objects.home_bluetooth_button, &img_home_bluetooth_off_button_image,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_img_recolor_opa(objects.home_bluetooth_button, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    } else {
        lv_obj_add_flag(objects.home_bluetooth_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(objects.home_bluetooth_button, LV_OBJ_FLAG_HIDDEN);
    }

    if (status.has_ethernet) {
        if (status.ethernet.status.is_connected) {
            char buf[20];
            uint32_t mac = ownNode;
            sprintf(buf, "??:??:%02x:%02x:%02x:%02x", mac & 0xff000000, mac & 0xff0000, mac & 0xff00, mac & 0xff);
            lv_label_set_text(objects.home_ethernet_label, buf);
            lv_obj_set_style_text_color(objects.home_ethernet_label, colorLightGray, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(objects.home_ethernet_button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_obj_set_style_bg_img_recolor_opa(objects.home_ethernet_button, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(objects.home_ethernet_label, colorMidGray, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    } else {
        lv_obj_add_flag(objects.home_ethernet_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(objects.home_ethernet_button, LV_OBJ_FLAG_HIDDEN);
    }
}

// ResponseHandler callbacks
*
 * handle response from routing
 */
/**
 * Signal scanner
 */
**
 * Trace Route: handle  ack or timeout
 */
*
 * @brief purge oldest node from node list (and all its memory)
 * @param nodeNum node that is being added and already contained in nodes[], so don't remove it!
 */
void TFTView_320x240::purgeNode(uint32_t nodeNum)
{
    if (nodeCount <= 1)
        return;

    lv_obj_t **children = objects.nodes_panel->spec_attr->children;
    int last = objects.nodes_panel->spec_attr->child_cnt - 1;
    int i = last;

#ifndef ALWAYS_PURGE_OLDEST_NODE
    time_t curr_time;
#ifdef ARCH_PORTDUINO
    time(&curr_time);
#else
    curr_time = actTime;
#endif
    // prefer purging older unknown nodes first (but not the brand new ones)
    while ((eRole)(long)(children[i]->LV_OBJ_IDX(node_img_idx)->user_data) != eRole::unknown ||
           curr_time < (time_t)(children[i]->LV_OBJ_IDX(node_lh_idx)->user_data) + 120 ||
           (unsigned long)(children[i]->LV_OBJ_IDX(node_lbl_idx)->user_data) == nodeNum ||
           chats.find((unsigned long)(children[i]->LV_OBJ_IDX(node_lbl_idx)->user_data)) != chats.end()) {
        if (i < (last + 1) / 5) { // keep 80% named nodes and 20% unknown (not fresh) nodes
            i = last;
            break;
        }
        i--;
    }
#endif
    lv_obj_t *p = children[i];
    uint32_t oldest = (unsigned long)(p->LV_OBJ_IDX(node_lbl_idx)->user_data);
    uint32_t lastHeard = (unsigned long)p->LV_OBJ_IDX(node_lh_idx)->user_data;
    if (lastHeard > 0 && (curtime - lastHeard <= secs_until_offline))
        nodesOnline--;

    ILOG_INFO("removing oldest node 0x%08x", oldest);
    lv_obj_delete(p);
    {
        auto it = messages.find(oldest);
        if (it != messages.end()) {
            lv_obj_delete(it->second);
            messages.erase(oldest);
        }
    }

    {
        auto it = chats.find(oldest);
        if (it != chats.end()) {
            lv_obj_delete(it->second);
            chats.erase(oldest);
            updateActiveChats();
        }
    }
    removeFromMap(oldest);
    nodes.erase(oldest);
    nodeCount--;
    nodesChanged = true; // flag to force re-apply node filter
}

/**
 * @brief apply enabled filters and highlight node
 *
 * @param nodeNum
 * @param reset : set true when filter has changed (to recalculate number of filtered nodes)
 * @return true
 * @return false
 */
bool TFTView_320x240::applyNodesFilter(uint32_t nodeNum, bool reset)
{
    lv_obj_t *panel = nodes[nodeNum];
    bool hide = false;
    if (nodeNum != ownNode /* && filter.active*/) { // TODO
        if (lv_obj_has_state(objects.nodes_filter_unknown_switch, LV_STATE_CHECKED)) {
            if (lv_img_get_src(panel->LV_OBJ_IDX(node_img_idx)) == &img_circle_question_image) {
                hide = true;
            }
        }
        if (lv_obj_has_state(objects.nodes_filter_offline_switch, LV_STATE_CHECKED)) {
            time_t lastHeard = (time_t)panel->LV_OBJ_IDX(node_lh_idx)->user_data;
            if (lastHeard == 0 || curtime - lastHeard > secs_until_offline)
                hide = true;
        }
        if (lv_obj_has_state(objects.nodes_filter_public_key_switch, LV_STATE_CHECKED)) {
            bool hasKey = (unsigned long)panel->LV_OBJ_IDX(node_bat_idx)->user_data == 1;
            if (!hasKey)
                hide = true;
        }
        if (lv_dropdown_get_selected(objects.nodes_filter_channel_dropdown) != 0) {
            int selected = lv_dropdown_get_selected(objects.nodes_filter_channel_dropdown);
            if (selected != 0) {
                uint8_t ch = (uint8_t)(unsigned long)panel->user_data;
                if (selected - 1 != ch)
                    hide = true;
            }
        }
        if (lv_dropdown_get_selected(objects.nodes_filter_hops_dropdown) != 0) {
            int32_t hopsAway = (signed long)panel->LV_OBJ_IDX(node_sig_idx)->user_data;
            int selected = lv_dropdown_get_selected(objects.nodes_filter_hops_dropdown) - 7;
            if (hopsAway < 0)
                hide = true;
            else if (selected <= 0) {
                if (hopsAway > -selected)
                    hide = true;
            } else {
                if (hopsAway < selected)
                    hide = true;
            }
        }
#if 0
        if (lv_obj_has_state(objects.nodes_filter_mqtt_switch, LV_STATE_CHECKED)) {
            bool viaMqtt = false; // TODO (unsigned long)panel->LV_OBJ_IDX(node_sig_idx)->user_data;
            if (viaMqtt)
                hide = true;
        }
#endif
        if (lv_obj_has_state(objects.nodes_filter_position_switch, LV_STATE_CHECKED)) {
            if (lv_label_get_text(panel->LV_OBJ_IDX(node_pos1_idx))[0] == '\0')
                hide = true;
        }
        const char *name = lv_textarea_get_text(objects.nodes_filter_name_area);
        if (name[0] != '\0') {
            if (name[0] != '!') { // use '!' char to negate search result
                if (!strcasestr(lv_label_get_text(panel->LV_OBJ_IDX(node_lbl_idx)), name) &&
                    !strcasestr(lv_label_get_text(panel->LV_OBJ_IDX(node_lbs_idx)), name)) {
                    hide = true;
                }
            } else {
                if (strcasestr(lv_label_get_text(panel->LV_OBJ_IDX(node_lbl_idx)), &name[1]) ||
                    strcasestr(lv_label_get_text(panel->LV_OBJ_IDX(node_lbs_idx)), &name[1])) {
                    hide = true;
                }
            }
        }
    }
    if (hide) {
        if (reset || !lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
            nodesFiltered++;
        }
    } else {
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }

    // hide node location if filtered
    if (map)
        map->update(nodeNum, hide);

    bool highlight = false;
    if (true /*highlight.active*/) { // TODO
        if (lv_obj_has_state(objects.nodes_hl_active_chat_switch, LV_STATE_CHECKED)) {
            auto it = chats.find(nodeNum);
            if (it != nodes.end()) {
                lv_obj_set_style_border_color(panel, colorOrange, LV_PART_MAIN | LV_STATE_DEFAULT);
                highlight = true;
            }
        }
        if (lv_obj_has_state(objects.nodes_hl_position_switch, LV_STATE_CHECKED)) {
            if (lv_label_get_text(panel->LV_OBJ_IDX(node_pos1_idx))[0] != '\0') {
                lv_obj_set_style_border_color(panel, colorBlueGreen, LV_PART_MAIN | LV_STATE_DEFAULT);
                highlight = true;
            }
        }
        if (lv_obj_has_state(objects.nodes_hl_telemetry_switch, LV_STATE_CHECKED)) {
            if (lv_label_get_text(panel->LV_OBJ_IDX(node_tm1_idx))[0] != '\0') {
                lv_obj_set_style_border_color(panel, colorBlue, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                highlight = true;
            }
        }
        if (lv_obj_has_state(objects.nodes_hliaq_switch, LV_STATE_CHECKED)) {
            if (lv_label_get_text(panel->LV_OBJ_IDX(node_tm2_idx))[0] != '\0') {
                uint32_t iaq = (unsigned long)panel->LV_OBJ_IDX(node_tm2_idx)->user_data;
                // IAQ color code
                lv_color_t fg, bg;
                if (iaq <= 50) {
                    fg = lv_color_hex(0x00000000);
                    bg = lv_color_hex(0x000ce810);
                } else if (iaq <= 100) {
                    fg = lv_color_hex(0x00000000);
                    bg = lv_color_hex(0x00faf646);
                } else if (iaq <= 150) {
                    fg = lv_color_hex(0x00000000);
                    bg = lv_color_hex(0x00f98204);
                } else if (iaq <= 200) {
                    fg = lv_color_hex(0x00000000);
                    bg = lv_color_hex(0x00e42104);
                } else if (iaq <= 300) {
                    fg = lv_color_hex(0xffffffff);
                    bg = lv_color_hex(0x009b2970);
                } else {
                    fg = lv_color_hex(0xffffffff);
                    bg = lv_color_hex(0x001d1414);
                }
                lv_obj_set_style_text_color(panel->LV_OBJ_IDX(node_tm2_idx), fg, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_color(panel->LV_OBJ_IDX(node_tm2_idx), bg, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_opa(panel->LV_OBJ_IDX(node_tm2_idx), 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(panel, bg, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                highlight = true;
            }
        }
        const char *name = lv_textarea_get_text(objects.nodes_hl_name_area);
        if (name[0] != '\0') {
            if (strcasestr(lv_label_get_text(panel->LV_OBJ_IDX(node_lbl_idx)), name) ||
                strcasestr(lv_label_get_text(panel->LV_OBJ_IDX(node_lbs_idx)), name)) {
                lv_obj_set_style_border_color(panel, colorMesh, LV_PART_MAIN | LV_STATE_DEFAULT);
                highlight = true;
            }
        }
    }
    if (!highlight) {
        lv_obj_set_style_border_color(panel, colorMidGray, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    return hide; // TODO || filter.active;
}
/**
 * @brief mark the sent message as either heard or acknowledged or failed
 *
 * @param channelOrNode
 * @param id
 * @param ack
 */
TView_320x240::screenSaving(bool enabled)
{
    if (enabled) {
        // overlay main screen with blank screen to prevent accidentally pressing buttons
        lv_screen_load_anim(objects.blank_screen, LV_SCR_LOAD_ANIM_FADE_OUT, 0, 0, false);
        lv_group_focus_obj(objects.blank_screen_button);
        screenLocked = true;
        screenUnlockRequest = false;
    } else {
        if (THIS->db.uiConfig.screen_lock) {
            ILOG_DEBUG("showing lock screen");
            lv_screen_load_anim(objects.lock_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        } else if (objects.main_screen) {
            ILOG_DEBUG("showing main screen");
            lv_screen_load_anim(objects.main_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
            if (THIS->activeSettings != eNone) {
                lv_event_t e = {.code = LV_EVENT_CLICKED};
                ui_event_cancel(&e);
            }
            screenLocked = false;
        } else {
            ILOG_DEBUG("showing boot screen");
            lv_screen_load_anim(objects.boot_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
            screenLocked = false;
        }
    }
}
void TFTView_320x240::updateChannelConfig(const meshtastic_Channel &ch)
{
    static lv_obj_t *btn[c_max_channels] = {objects.channel_button0, objects.channel_button1, objects.channel_button2,
                                            objects.channel_button3, objects.channel_button4, objects.channel_button5,
                                            objects.channel_button6, objects.channel_button7};
    db.channel[ch.index] = ch;

    if (ch.role != meshtastic_Channel_Role_DISABLED) {
        setChannelName(ch);

        lv_obj_set_width(btn[ch.index], lv_pct(80));
        lv_obj_set_style_pad_left(btn[ch.index], 8, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *lockImage = NULL;
        if (lv_obj_get_child_cnt(btn[ch.index]) == 1)
            lockImage = lv_img_create(btn[ch.index]);
        else
            lockImage = lv_obj_get_child(btn[ch.index], 1);

        uint32_t recolor = 0;

        if (memcmp(ch.settings.psk.bytes, "\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000", 16) == 0) {
            lv_image_set_src(lockImage, &img_groups_key_image);
            recolor = 0xF2E459; // yellow
        } else if (memcmp(ch.settings.psk.bytes, "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000", 16) == 0) {
            lv_image_set_src(lockImage, &img_groups_unlock_image);
            recolor = 0xF72B2B; // reddish
        } else {
            lv_image_set_src(lockImage, &img_groups_lock_image);
            recolor = 0x1EC174; // green
        }
        lv_obj_set_width(lockImage, LV_SIZE_CONTENT);  /// 1
        lv_obj_set_height(lockImage, LV_SIZE_CONTENT); /// 1
        lv_obj_set_align(lockImage, LV_ALIGN_LEFT_MID);
        lv_obj_add_flag(lockImage, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
        lv_obj_clear_flag(lockImage, LV_OBJ_FLAG_SCROLLABLE); /// Flags
        lv_obj_set_style_img_recolor(lockImage, lv_color_hex(recolor), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_img_recolor_opa(lockImage, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *bellImage = NULL;
        if (lv_obj_get_child_cnt(btn[ch.index]) < 3)
            bellImage = lv_img_create(btn[ch.index]);
        else
            bellImage = lv_obj_get_child(btn[ch.index], 2);
        lv_obj_set_width(bellImage, LV_SIZE_CONTENT);  /// 1
        lv_obj_set_height(bellImage, LV_SIZE_CONTENT); /// 1
        lv_obj_set_align(bellImage, LV_ALIGN_RIGHT_MID);
        lv_obj_add_flag(bellImage, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
        lv_obj_clear_flag(bellImage, LV_OBJ_FLAG_SCROLLABLE); /// Flags
        lv_obj_set_style_img_recolor_opa(bellImage, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        updateGroupChannel(ch.index);
    } else {
        // display smaller button with just the channel number
        char buf[10];
        lv_snprintf(buf, sizeof(buf), "%d", ch.index);
        lv_label_set_text(channel[ch.index], buf);
        lv_obj_set_width(btn[ch.index], lv_pct(30));

        if (lv_obj_get_child_cnt(btn[ch.index]) == 2) {
            lv_obj_delete(lv_obj_get_child(btn[ch.index], 1));
        }
    }
}

// redraw bell icons and color
uto set primary(secondary) channel name (based on region)
 */
void TFTView_320x240::backup(uint32_t option)
{
#if defined(HAS_SDCARD) || defined(HAS_SD_MMC) || defined(ARCH_PORTDUINO)
    meshtastic_Config_SecurityConfig_public_key_t &pubkey = db.config.security.public_key;
    meshtastic_Config_SecurityConfig_private_key_t &privkey = db.config.security.private_key;

    std::stringstream path;
    path << "/keys/" << std::hex << std::setw(8) << std::setfill('0') << ownNode << ".yml";
#if defined(ARCH_PORTDUINO) || defined(HAS_SD_MMC)
    SDFs.mkdir("/keys");
    File sd = SDFs.open(path.str().c_str(), FILE_WRITE);
#else
    SDFs.mkdir("/keys");
    FsFile sd = SDFs.open(path.str().c_str(), O_RDWR | O_CREAT);
#endif
    if (sd) {
        sd.println("config:");
        sd.println("  security:");
        sd.print("      privateKey: base64:");
        sd.println(pskToBase64(privkey.bytes, privkey.size).c_str());
        sd.print("      publicKey: base64:");
        sd.println(pskToBase64(pubkey.bytes, pubkey.size).c_str());
        ILOG_INFO("backup pub/priv keys done.");
    } else {
        ILOG_ERROR("open file %s for backup failed", path.str().c_str());
        messageAlert(_("Failed to write keys!"), true);
    }
    sd.close();
#endif
}

void TFTView_320x240::restore(uint32_t option)
{
#if defined(HAS_SDCARD) || defined(HAS_SD_MMC) || defined(ARCH_PORTDUINO)
    meshtastic_Config_SecurityConfig_public_key_t &pubkey = db.config.security.public_key;
    meshtastic_Config_SecurityConfig_private_key_t &privkey = db.config.security.private_key;

    std::stringstream path;
    path << "/keys/" << std::hex << std::setw(8) << std::setfill('0') << ownNode << ".yml";

#if defined(ARCH_PORTDUINO) || defined(HAS_SD_MMC)
    File sd = SDFs.open(path.str().c_str(), FILE_READ);
#else
    FsFile sd = SDFs.open(path.str().c_str(), O_RDONLY);
#endif
    if (sd) {
        // TODO: improve parsing file contents
        sd.readStringUntil('\n');                  // config:
        sd.readStringUntil('\n');                  // security:
        String privKey = sd.readStringUntil('\n'); // privateKey: base64:
        String pubKey = sd.readStringUntil('\n');  // publicKey: base64:
        if (privKey.indexOf("privateKey:") > 0 && pubKey.indexOf("publicKey:") > 0) {
            String b64priv = privKey.substring(privKey.lastIndexOf(":") + 1);
            String b64pub = pubKey.substring(pubKey.lastIndexOf(":") + 1);
            b64priv.trim();
            b64pub.trim();
            if (base64ToPsk(b64priv.c_str(), privkey.bytes, privkey.size) &&
                base64ToPsk(b64pub.c_str(), pubkey.bytes, pubkey.size) &&
                controller->sendConfig(meshtastic_Config_SecurityConfig{db.config.security})) {
                ILOG_INFO("restore pub/priv keys sent to radio");
            } else {
                ILOG_ERROR("decoding keys failed");
                messageAlert(_("Failed to restore keys!"), true);
            }
        } else {
            ILOG_ERROR("file %s contents don't match backup", path.str().c_str());
            messageAlert(_("Failed to parse keys!"), true);
        }
    } else {
        ILOG_ERROR("open file %s failed", path.str().c_str());
        messageAlert(_("Failed to retrieve keys!"), true);
    }
    sd.close();
#endif
}

/**
 * @brief write local time stamp into buffer
 *        if date is not current also add day/month
 *        Note: time string ends with linefeed
 *
 * @param buf allocated buffer
 * @param datetime date/time to write
 * @param update update with actual time, otherwise using time from parameter 'time'
 * @return length of time string
 */
/**
 * calculate percentage value from rssi and snr
 * Note: ranges are based on the axis values of the signal scanner
 */
 ---- module updates ----
void TFTView_320x240::updateTime(uint32_t timeVal)
{
    time_t localtime;
    time(&localtime);

    if (VALID_TIME(localtime)) {
        if (actTime != localtime) {
            ILOG_DEBUG("update (local)time: %d -> %d", actTime, localtime);
            actTime = localtime;
        }
    } else {
        if (timeVal > actTime) {
            ILOG_DEBUG("update (act)time: %d -> %d", actTime, timeVal);
            actTime = timeVal;
        }
    }
}

/**
 * @brief Create a new container for a node or group channel if it does not exist
 *
 * @param from
 * @param to: UINT32_MAX for broadcast, ownNode (= us) otherwise
 * @param channel
 */
lv_obj_t *TFTView_320x240::newMessageContainer(uint32_t from, uint32_t to, uint8_t ch)
{
    if (to == UINT32_MAX || from == 0) {
        if (channelGroup[ch] != nullptr)
            return channelGroup[ch];
    } else {
        auto it = messages.find(from);
        if (it != messages.end() && it->second)
            return it->second;
    }

    // create container for new messages
    lv_obj_t *container = lv_obj_create(objects.messages_panel);
    lv_obj_remove_style_all(container);
    lv_obj_set_width(container, lv_pct(100));
    lv_obj_set_height(container, lv_pct(88));
    lv_obj_set_x(container, 0);
    lv_obj_set_y(container, 0);
    lv_obj_set_align(container, LV_ALIGN_TOP_MID);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(container, lv_obj_flag_t(LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
                                               LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_SCROLL_ELASTIC)); /// Flags
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_style_pad_left(container, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(container, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(container, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // store new message container
    if (to == UINT32_MAX || from == 0) {
        channelGroup[ch] = container;
    } else {
        messages[from] = container;
    }

    // optionally add chat to chatPanel to access the container
    addChat(from, to, ch);

    return container;
}

/**
 * @brief insert a mew message that arrived into a <channel group> or <from node> container
 *
 * @param from source node
 * @param to destination node
 * @param ch channel
 * @param size length of msg
 * @param msg text message
 * @param time in/out: message time (maybe overwritten when 0)
 * @param restore if restoring then skip banners and highlight
 */
void TFTView_320x240::newMessage(uint32_t from, uint32_t to, uint8_t ch, const char *msg, uint32_t &msgTime, bool restore)
{
    ILOG_DEBUG("newMessage: from:0x%08x, to:0x%08x, ch:%d, time:%d", from, to, ch, msgTime);
    int pos = 0;
    char buf[284]; // 237 + 4 + 40 + 2 + 1
    lv_obj_t *container = nullptr;
    if (to == UINT32_MAX) { // message for group, prepend short name to msg
        if (nodes.find(from) == nodes.end()) {
            pos += sprintf(buf, "%04x ", from & 0xffff);
        } else {
            // original short name is held in userData, extract it and add msg
            char *userData = (char *)&(nodes[from]->LV_OBJ_IDX(node_lbs_idx)->user_data);
            while (pos < 4 && userData[pos] != 0) {
                buf[pos] = userData[pos];
                pos++;
            }
        }
        buf[pos++] = ' ';
        container = channelGroup[ch];
    } else { // message for us
        container = messages[from];
    }

    // if it's the first message we need a container
    if (!container) {
        container = newMessageContainer(from, to, ch);
    }

    pos += timestamp(&buf[pos], msgTime, !restore);
    sprintf(&buf[pos], "%s", msg);

    // place message into container
    newMessage(from, container, ch, buf);

    if (!restore) {
        // display msg popup if not already viewing the messages
        if (container != activeMsgContainer || activePanel != objects.messages_panel) {
            unreadMessages++;
            updateUnreadMessages();
            if (activePanel != objects.messages_panel && db.uiConfig.alert_enabled &&
                !db.channel[ch].settings.module_settings.is_muted) {
                showMessagePopup(from, to, ch, lv_label_get_text(nodes[from]->LV_OBJ_IDX(node_lbl_idx)));
            }
            lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
        }
        if (container != activeMsgContainer)
            highlightChat(from, to, ch);
    } else {
        if (container != activeMsgContainer)
            lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Display message bubble in related message container
 *
 * @param nodeNum
 * @param container
 * @param ch
 * @param msg
 */
void TFTView_320x240::newMessage(uint32_t nodeNum, lv_obj_t *container, uint8_t ch, const char *msg)
{
    lv_obj_t *hiddenPanel = lv_obj_create(container);
    lv_obj_set_width(hiddenPanel, lv_pct(100));
    lv_obj_set_height(hiddenPanel, LV_SIZE_CONTENT); /// 50
    lv_obj_set_align(hiddenPanel, LV_ALIGN_CENTER);
    lv_obj_clear_flag(hiddenPanel, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_set_style_radius(hiddenPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    add_style_panel_style(hiddenPanel);
    lv_obj_set_style_pad_left(hiddenPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(hiddenPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(hiddenPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(hiddenPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *msgLabel = lv_label_create(hiddenPanel);
    // calculate expected size of text bubble, to make it look nicer
    lv_coord_t width = lv_txt_get_width(msg, strlen(msg), &ui_font_montserrat_14, 0);
    lv_obj_set_width(msgLabel, std::max<int32_t>(std::min<int32_t>((int32_t)(width), 160) + 10, 40));
    lv_obj_set_height(msgLabel, LV_SIZE_CONTENT);
    lv_obj_set_align(msgLabel, LV_ALIGN_LEFT_MID);
    lv_label_set_text(msgLabel, msg);
    add_style_new_message_style(msgLabel);
    lv_obj_add_flag(msgLabel, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(msgLabel, ui_event_chatNodeButton, LV_EVENT_CLICKED, (void *)nodeNum);

    if (state == MeshtasticView::eRunning) {
        lv_obj_scroll_to_view(hiddenPanel, LV_ANIM_ON);
        lv_obj_move_foreground(objects.message_input_area);
    }
}

/**
 * restore messages from persistent log
 */
void TFTView_320x240::restoreMessage(const LogMessage &msg)
{
    //((uint8_t *)msg.bytes)[msg._size] = 0;
    // ILOG_DEBUG("restoring msg from:0x%08x, to:0x%08x, ch:%d, time:%d, status:%d, trash:%d, size:%d, '%s'", msg.from, msg.to,
    //           msg.ch, msg.time, (int)msg.status, msg.trashFlag, msg._size, msg.bytes);

    if (msg.from == ownNode) {
        lv_obj_t *container = nullptr;
        if (msg.to == UINT32_MAX) {
            if (msg.trashFlag && chats.find(msg.ch) != chats.end()) {
                ILOG_DEBUG("trashFlag set for channel %d", msg.ch);
                eraseChat(msg.ch);
                return;
            } else {
                container = newMessageContainer(msg.from, msg.to, msg.ch);
            }
        } else {
            if (nodes.find(msg.to) != nodes.end()) {
                if (msg.trashFlag && chats.find(msg.to) != chats.end()) {
                    ILOG_DEBUG("trashFlag set for node %08x", msg.to);
                    eraseChat(msg.to);
                    return;
                } else {
                    container = newMessageContainer(msg.to, msg.from, msg.ch);
                }
            } else {
                ILOG_DEBUG("to node 0x%08x not in db", msg.to);
                MeshtasticView::addOrUpdateNode(msg.to, msg.ch, 0, eRole::unknown, false, false);
            }
        }
        if (container) {
            if (container != activeMsgContainer)
                lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
            addMessage(container, msg.time, 0, (char *)msg.bytes, msg.status);
        }
    } else if (nodes.find(msg.from) != nodes.end()) {
        if (msg.trashFlag && chats.find(msg.from) != chats.end()) {
            ILOG_DEBUG("trashFlag set for node %08x", msg.from);
            eraseChat(msg.from);
            return;
        } else {
            uint32_t time = msg.time ? msg.time : UINT32_MAX; // don't overwrite 0 with actual time
            newMessage(msg.from, msg.to, msg.ch, (const char *)msg.bytes, time);
        }
    } else {
        int pos = 0;
        char buf[284]; // 237 + 4 + 40 + 2 + 1
        if (msg.to != UINT32_MAX) {
            // from node not in db
            ILOG_DEBUG("from node 0x%08x not in db", msg.from);
            MeshtasticView::addOrUpdateNode(msg.from, msg.ch, 0, eRole::unknown, false, false);
        } else {
            ILOG_DEBUG("from node 0x%08x not in db and no need to insert", msg.from);
            pos += sprintf(buf, "%04x ", msg.from & 0xffff);
        }
        uint32_t len = timestamp(buf + pos, msg.time, false);
        memcpy(buf + pos + len, msg.bytes, msg.length());
        buf[pos + len + msg.length()] = 0;

        lv_obj_t *container = newMessageContainer(msg.from, msg.to, msg.ch);
        lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
        newMessage(msg.from, container, msg.ch, buf);
    }
}

/**
 * @brief Add a new chat to the chat panel to access the message container
 *
 * @param from
 * @param to
 * @param ch
 */
void TFTView_320x240::addChat(uint32_t from, uint32_t to, uint8_t ch)
{
    uint32_t index = ((to == UINT32_MAX || from == 0) ? ch : from);
    auto it = chats.find(index);
    if (it != chats.end())
        return;

    lv_obj_t *chatDelBtn = nullptr;
    lv_obj_t *parent_obj = objects.chats_panel;

    // ChatsButton
    lv_obj_t *chatBtn = lv_btn_create(parent_obj);
    lv_obj_set_pos(chatBtn, 0, 0);
    lv_obj_set_size(chatBtn, LV_PCT(100), buttonSize);
    lv_obj_add_flag(chatBtn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(chatBtn, LV_OBJ_FLAG_SCROLLABLE);
    add_style_home_button_style(chatBtn);
    lv_obj_set_style_align(chatBtn, LV_ALIGN_TOP_MID, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(chatBtn, colorMidGray, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(chatBtn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(chatBtn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(chatBtn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(chatBtn, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(chatBtn, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(chatBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(chatBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(chatBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(chatBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(chatBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_move_to_index(chatBtn, 0);

    char buf[64];
    if (to == UINT32_MAX || from == 0) {
        sprintf(buf, "%d: %s", (int)ch, lv_label_get_text(channel[ch]));
    } else {
        auto it = nodes.find(from);
        if (it != nodes.end()) {
            sprintf(buf, "%s: %s", lv_label_get_text(it->second->LV_OBJ_IDX(node_lbs_idx)),
                    lv_label_get_text(it->second->LV_OBJ_IDX(node_lbl_idx)));
        } else {
            sprintf(buf, "!%08x", from);
        }
    }

    {
        lv_obj_t *parent_obj = chatBtn;
        {
            // ChatsButtonLabel
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.chats_button_label = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
            lv_label_set_text(obj, buf);
            lv_obj_set_style_align(obj, LV_ALIGN_LEFT_MID, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            // ChatDelButton
            lv_obj_t *obj = lv_btn_create(parent_obj);
            chatDelBtn = obj;
            lv_obj_set_pos(obj, -3, -1);
            lv_obj_set_size(obj, 40, 23);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_align(obj, LV_ALIGN_RIGHT_MID, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, colorDarkRed, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // DelLabel
                    lv_obj_t *chatDelBtn = lv_label_create(parent_obj);
                    lv_obj_set_pos(chatDelBtn, 0, 0);
                    lv_obj_set_size(chatDelBtn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_label_set_text(chatDelBtn, _("DEL"));
                    lv_obj_set_style_align(chatDelBtn, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
            }
        }
    }

    chats[index] = chatBtn;
    updateActiveChats();
    if (index > c_max_channels) {
        if (nodes.find(index) != nodes.end())
            applyNodesFilter(index);
    }

    lv_obj_add_event_cb(chatBtn, ui_event_ChatButton, LV_EVENT_ALL, (void *)index);
    lv_obj_add_event_cb(chatDelBtn, ui_event_ChatDelButton, LV_EVENT_CLICKED, (void *)index);
}

void TFTView_320x240::highlightChat(uint32_t from, uint32_t to, uint8_t ch)
{
    uint32_t index = ((to == UINT32_MAX || from == 0) ? ch : from);
    auto it = chats.find(index);
    if (it != chats.end()) {
        // mark chat in color
        lv_obj_set_style_border_color(it->second, colorOrange, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void TFTView_320x240::updateActiveChats(void)
{
    char buf[48];
    sprintf(buf, _p("%d active chat(s)", chats.size()), chats.size());
    lv_label_set_text(objects.top_chats_label, buf);
}

/**
 * @brief Display banner showing to be patient while restoring messages
 */
**
 * @brief display new message popup panel
 *
 * @param from sender (NULL for removing popup)
 * @param to individual or group message
 * @param ch received channel
 */
void TFTView_320x240::showMessagePopup(uint32_t from, uint32_t to, uint8_t ch, const char *name)
{
    if (name) {
        static char buf[64];
        sprintf(buf, _("New message from \n%s"), name);
        buf[38] = '\0'; // cut too long userName
        lv_label_set_text(objects.msg_popup_label, buf);
        if (to == UINT32_MAX)
            objects.msg_popup_button->user_data = (void *)(uint32_t)ch; // store the channel in the button's data
        else
            objects.msg_popup_button->user_data = (void *)from; // store the node in the button's data
        lv_obj_clear_flag(objects.msg_popup_panel, LV_OBJ_FLAG_HIDDEN);

        if (db.module_config.external_notification.alert_message)
            lv_disp_trig_activity(NULL);

        lv_group_focus_obj(objects.msg_popup_button);
    }
}
/**
 * @brief Display messages of a group channel
 *
 * @param ch
 */
void TFTView_320x240::showMessages(uint8_t ch)
{
    if (!messagesRestored) {
        // display message restoration progress banner
        lv_obj_clear_flag(objects.msg_popup_panel, LV_OBJ_FLAG_HIDDEN);
        lv_group_focus_obj(objects.msg_popup_button);
        return;
    }

    lv_obj_add_flag(activeMsgContainer, LV_OBJ_FLAG_HIDDEN);
    activeMsgContainer = channelGroup[ch];
    if (!activeMsgContainer) {
        activeMsgContainer = newMessageContainer(0, UINT32_MAX, ch);
    }

    activeMsgContainer->user_data = (void *)(uint32_t)ch;
    lv_obj_clear_flag(activeMsgContainer, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(objects.top_group_chat_label, lv_label_get_text(channel[ch]));
    ui_set_active(objects.messages_button, objects.messages_panel, objects.top_group_chat_panel);
}

/**
 * @brief Display messages from a node
 *
 * @param nodeNum
 */
void TFTView_320x240::showMessages(uint32_t nodeNum)
{
    lv_obj_add_flag(activeMsgContainer, LV_OBJ_FLAG_HIDDEN);
    activeMsgContainer = messages[nodeNum];
    if (!activeMsgContainer) {
        activeMsgContainer = newMessageContainer(nodeNum, 0, 0);
    }
    activeMsgContainer->user_data = (void *)nodeNum;
    lv_obj_clear_flag(activeMsgContainer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *p = nodes[nodeNum];
    if (p) {
        lv_label_set_text(objects.top_messages_node_label, lv_label_get_text(p->LV_OBJ_IDX(node_lbl_idx)));
        ui_set_active(objects.messages_button, objects.messages_panel, objects.top_messages_panel);
        switch ((unsigned long)p->LV_OBJ_IDX(node_bat_idx)->user_data) {
        case 0:
            lv_obj_set_style_bg_image_src(objects.top_messages_node_image, &img_lock_channel_image,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            break;
        case 1:
            lv_obj_set_style_bg_image_src(objects.top_messages_node_image, &img_lock_secure_image,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            break;
        default:
            lv_obj_set_style_bg_image_src(objects.top_messages_node_image, &img_lock_slash_image,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            break;
        }
        unreadMessages = 0; // TODO: not all messages may be actually read
        updateUnreadMessages();
    } else {
        // TODO: log error
    }
}

/**
 * @brief Place keyboard at a suitable space above or below the text input area
 *
 * @param textArea
 */
*
 * Enable underlying panel, buttons and scrollbar after it was disabled
 */
/**
 * Disable underlying panel with it's children buttons and scrollbar
 */
/**
 * Set focus to first button of a panel
 */
void TFTView_320x240::setGroupFocus(lv_obj_t *panel)
{
    if (panel == objects.home_panel) {
        lv_group_focus_obj(objects.home_mail_button);
    } else if (panel == objects.nodes_panel) {
        lv_group_focus_obj(objects.node_button);
    } else if (panel == objects.groups_panel) {
        lv_group_focus_obj(objects.channel_button0);
    } else if (panel == objects.messages_panel) {
        lv_group_focus_obj(objects.message_input_area);
    } else if (panel == objects.chats_panel) {
        if (chats.size() > 0) {
            lv_group_focus_obj(panel->spec_attr->children[1]); // TODO: does not work
        }
    } else if (panel == objects.map_panel) {

    } else if (panel == objects.settings_screen_lock_panel) {
        lv_group_focus_obj(objects.screen_lock_button_matrix);
    } else if (panel == objects.controller_panel) {
        lv_group_focus_obj(objects.basic_settings_user_button);
    } else {
        for (int i = 0; i < lv_obj_get_child_count(panel); i++) {
            if (panel->spec_attr->children[i]->class_p == &lv_button_class) {
                lv_group_focus_obj(panel->spec_attr->children[i]);
                break;
            }
        }
    }
}

/**
 * input group used by keyboard and/or pointer for dynamic assignment
 */
/ -------- helpers --------

void TFTView_320x240::removeNode(uint32_t nodeNum)
{
    auto it = nodes.find(nodeNum);
    if (it != nodes.end()) {
    }
}

void TFTView_320x240::setNodeImage(uint32_t nodeNum, eRole role, bool unmessagable, lv_obj_t *img)
{
    uint32_t bgColor, fgColor;
    std::tie(bgColor, fgColor) = nodeColor(nodeNum);
    if (unmessagable) {
        lv_image_set_src(img, &img_unmessagable_image);
        lv_obj_set_style_border_color(img, lv_color_hex(bgColor), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(img, lv_color_hex(0x202020), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_img_recolor(img, lv_color_hex(0xFF5555), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_img_recolor_opa(img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        return;
    } else {
        switch (role) {
        case client:
        case client_mute:
        case client_hidden:
        case tak: {
            lv_image_set_src(img, &img_node_client_image);
            break;
        }
        case router_client: {
            lv_image_set_src(img, &img_top_nodes_image);
            break;
        }
        case repeater:
        case router:
        case router_late: {
            lv_image_set_src(img, &img_node_router_image);
            break;
        }
        case tracker:
        case sensor:
        case lost_and_found:
        case tak_tracker: {
            lv_image_set_src(img, &img_node_sensor_image);
            break;
        }
        case unknown: {
            lv_image_set_src(img, &img_circle_question_image);
            break;
        }
        default:
            lv_image_set_src(img, &img_node_client_image);
            break;
        }
    }
    lv_obj_set_style_bg_color(img, lv_color_hex(bgColor), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(img, lv_color_hex(bgColor), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(img, fgColor ? 0 : 255, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void TFTView_320x240::updateNodesStatus(void)
{
    char buf[40];
    lv_snprintf(buf, sizeof(buf), _p("%d of %d nodes online", nodeCount), nodesOnline, nodeCount);
    lv_label_set_text(objects.home_nodes_label, buf);
}

#endif // why not :))) 

int TFTView_320x240::getChannelButtonWidth()
{
    return 80;
}

void TFTView_320x240::onEventsInitExtra()
{
    lv_obj_add_event_cb(objects.main_screen, this->ui_event_GlobalKeyHandler, LV_EVENT_KEY, NULL);
}

void TFTView_320x240::configureKeyboardLayouts()
{
    ::configureKeyboardLayouts(objects.keyboard);
}

#endif
