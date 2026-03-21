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
const char *mapProviderToString(MapTileProvider provider);
MapTileProvider parseMapProvider(const char *provider);
MapTileProvider loadPersistedMapProvider(void);
void persistMapProvider(MapTileProvider provider);
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
