#if HAS_TFT && defined(VIEW_480x222)

#include "graphics/view/TFT/TFTView_480x222.h"
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
#define THIS TFTView_480x222::instance() // need to use this in all static methods

// forward declarations for free functions defined in TFTView_Common.cpp
void syncVirtualKeyboardLayout(lv_obj_t *keyboard);
const lv_font_t *getKeyboardFont(void);

// Color constants defined in TFTView_Common.cpp with external linkage
extern const lv_color_t colorGray;
extern const lv_color_t colorDarkGray;
extern const lv_color_t colorMesh;

#define KEYBOARD_CTRL_BUTTON_FLAGS                                                                                               \
    (LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG | LV_BUTTONMATRIX_CTRL_CHECKED)
#define KEYBOARD_CTRL(value) static_cast<lv_buttonmatrix_ctrl_t>(value)
#define KEYBOARD_BTN(width) KEYBOARD_CTRL(LV_BUTTONMATRIX_CTRL_POPOVER | (width))
#define KEYBOARD_BTN_CHECKED(width) KEYBOARD_CTRL(LV_BUTTONMATRIX_CTRL_CHECKED | (width))
#define KEYBOARD_BTN_CHECKED_POPOVER(width) KEYBOARD_CTRL(LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_POPOVER | (width))
#define KEYBOARD_BTN_ACTION(width) KEYBOARD_CTRL(KEYBOARD_CTRL_BUTTON_FLAGS | (width))

// English lower: Row1=10 letters, Row2=9 letters+Enter, Row3=Shift+7 letters+Bksp, Row4=6 controls
static const char *const kb_map_en_lower[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
    "1#", "RU", ",", " ", ".", LV_SYMBOL_OK,
    ""};

// English upper: same layout with uppercase letters and "abc" shift label
static const char *const kb_map_en_upper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
    "1#", "RU", ",", " ", ".", LV_SYMBOL_OK,
    ""};

// Russian lower: Row1=11 (й-з х), Row2=11 (ф-д ж э), Row3=Shift+9+Bksp=11, Row4=8 controls
static const char *const kb_map_ru_lower[] = {
    "\xD0\xB9", "\xD1\x86", "\xD1\x83", "\xD0\xBA", "\xD0\xB5",
    "\xD0\xBD", "\xD0\xB3", "\xD1\x88", "\xD1\x89", "\xD0\xB7", "\xD1\x85", "\n",
    "\xD1\x84", "\xD1\x8B", "\xD0\xB2", "\xD0\xB0", "\xD0\xBF",
    "\xD1\x80", "\xD0\xBE", "\xD0\xBB", "\xD0\xB4", "\xD0\xB6", "\xD1\x8D", "\n",
    "ABC", "\xD1\x8F", "\xD1\x87", "\xD1\x81", "\xD0\xBC",
    "\xD0\xB8", "\xD1\x82", "\xD1\x8C", "\xD0\xB1", "\xD1\x8E", LV_SYMBOL_BACKSPACE, "\n",
    "1#", "EN", "\xD1\x91", "\xD1\x8A", " ", ".", LV_SYMBOL_NEW_LINE, LV_SYMBOL_OK,
    ""};

// Russian upper: same structure with uppercase Cyrillic
static const char *const kb_map_ru_upper[] = {
    "\xD0\x99", "\xD0\xA6", "\xD0\xA3", "\xD0\x9A", "\xD0\x95",
    "\xD0\x9D", "\xD0\x93", "\xD0\xA8", "\xD0\xA9", "\xD0\x97", "\xD0\xA5", "\n",
    "\xD0\xA4", "\xD0\xAB", "\xD0\x92", "\xD0\x90", "\xD0\x9F",
    "\xD0\xA0", "\xD0\x9E", "\xD0\x9B", "\xD0\x94", "\xD0\x96", "\xD0\xAD", "\n",
    "abc", "\xD0\xAF", "\xD0\xA7", "\xD0\xA1", "\xD0\x9C",
    "\xD0\x98", "\xD0\xA2", "\xD0\xAC", "\xD0\x91", "\xD0\xAE", LV_SYMBOL_BACKSPACE, "\n",
    "1#", "EN", "\xD0\x81", "\xD0\xAA", " ", ".", LV_SYMBOL_NEW_LINE, LV_SYMBOL_OK,
    ""};

// Ctrl map for English layouts: Row1=10, Row2=9+Enter, Row3=Shift+7+Bksp, Row4=6
static const lv_buttonmatrix_ctrl_t kb_ctrl_en[] = {
    // Row 1: 10 letter keys
    KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4),
    KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4),
    // Row 2: 9 letters + Enter
    KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4),
    KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN_ACTION(8),
    // Row 3: Shift + 7 letters + Backspace
    KEYBOARD_BTN_ACTION(6), KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4),
    KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN(4), KEYBOARD_BTN_CHECKED(6),
    // Row 4: 1# RU , SPACE . OK
    KEYBOARD_BTN_ACTION(4), KEYBOARD_BTN_ACTION(4), KEYBOARD_BTN(4), KEYBOARD_CTRL(12),
    KEYBOARD_BTN(4), KEYBOARD_BTN_ACTION(4),
};

// Ctrl map for Russian layouts: Row1=11, Row2=11, Row3=Shift+9+Bksp=11, Row4=8
static const lv_buttonmatrix_ctrl_t kb_ctrl_ru[] = {
    // Row 1: 11 letter keys (й-з х)
    KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3),
    KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3),
    // Row 2: 11 letters (ф-д ж э)
    KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3),
    KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3),
    // Row 3: Shift + 9 letters + Backspace
    KEYBOARD_BTN_ACTION(4), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3),
    KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3), KEYBOARD_BTN_CHECKED(4),
    // Row 4: 1# EN ё ъ SPACE . ENTER OK
    KEYBOARD_BTN_ACTION(3), KEYBOARD_BTN_ACTION(3), KEYBOARD_BTN(3), KEYBOARD_BTN(3),
    KEYBOARD_CTRL(8), KEYBOARD_BTN(3), KEYBOARD_BTN_ACTION(3), KEYBOARD_BTN_ACTION(3),
};

static void configureKeyboardLayouts(lv_obj_t *keyboard)
{
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_en_lower, kb_ctrl_en);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_en_upper, kb_ctrl_en);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_1, kb_map_ru_lower, kb_ctrl_ru);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_2, kb_map_ru_upper, kb_ctrl_ru);
}

// NodePanelIdx and ScrollDirection enums defined in TFTView_Common.cpp

extern const char *firmware_version;

TFTView_480x222 *TFTView_480x222::gui = nullptr;
static lv_obj_t *messagesBadge = nullptr;

TFTView_480x222 *TFTView_480x222::instance(void)
{
    if (!gui) {
        gui = new TFTView_480x222(nullptr, DisplayDriverFactory::create(480, 222));
        commonInstance = gui;
    }
    return gui;
}

TFTView_480x222 *TFTView_480x222::instance(const DisplayDriverConfig &cfg)
{
    if (!gui) {
        gui = new TFTView_480x222(&cfg, DisplayDriverFactory::create(cfg));
        commonInstance = gui;
    }
    return gui;
}

TFTView_480x222::TFTView_480x222(const DisplayDriverConfig *cfg, DisplayDriver *driver)
    : TFTView_Common(cfg, driver)
{
}


void TFTView_480x222::ui_set_active(lv_obj_t *b, lv_obj_t *p, lv_obj_t *tp)
{
    if (activeButton) {
        // Reset previous button styling
        lv_obj_set_style_border_width(activeButton, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(activeButton, colorDarkGray, LV_PART_MAIN | LV_STATE_DEFAULT);
        if (Themes::get() == Themes::eDark)
            lv_obj_set_style_bg_img_recolor_opa(activeButton, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_img_recolor(activeButton, colorGray, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    // Highlight new active button with green background and dark icon
    lv_obj_set_style_border_width(b, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(b, colorMesh, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_recolor(b, colorDarkGray, LV_PART_MAIN | LV_STATE_DEFAULT);
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
        // Always focus input area - KEY handler in ui_event_message_ready scrolls when empty
        lv_group_focus_obj(objects.message_input_area);
    } else if (inputdriver->hasKeyboardDevice() || inputdriver->hasEncoderDevice()) {
        setGroupFocus(activePanel);
    }

    lv_obj_add_flag(objects.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.msg_popup_panel, LV_OBJ_FLAG_HIDDEN);
}

void TFTView_480x222::ui_event_MainButtonFocus(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);

    if (event_code == LV_EVENT_FOCUSED) {
        // Apply green highlight when button receives focus
        lv_obj_set_style_bg_color(btn, colorMesh, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_img_recolor(btn, colorDarkGray, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_img_recolor_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (event_code == LV_EVENT_DEFOCUSED) {
        // Remove highlight when focus leaves (unless it's the active button)
        if (btn != THIS->activeButton) {
            lv_obj_set_style_bg_color(btn, colorDarkGray, LV_PART_MAIN | LV_STATE_DEFAULT);
            if (Themes::get() == Themes::eDark)
                lv_obj_set_style_bg_img_recolor_opa(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_img_recolor(btn, colorGray, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

// Global key handler for navigation - catches LV_KEY_HOME to focus side menu
void TFTView_480x222::ui_event_GlobalKeyHandler(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);

        // Map panel keyboard navigation: arrow keys pan, Enter/Esc zoom
        if (THIS->activePanel == objects.map_panel && THIS->map && THIS->map->redrawComplete()) {
            uint16_t deltaX = 0;
            uint16_t deltaY = 0;
            bool handled = true;
            switch (key) {
            case LV_KEY_UP:    deltaX = 0;  deltaY = 1;  break;
            case LV_KEY_DOWN:  deltaX = 0;  deltaY = -1; break;
            case LV_KEY_LEFT:  deltaX = 1;  deltaY = 0;  break;
            case LV_KEY_RIGHT: deltaX = -1; deltaY = 0;  break;
            case LV_KEY_ENTER:
                THIS->map->setZoom(MapTileSettings::getZoomLevel() + 1);
                THIS->updateLocationMap(THIS->map->getObjectsOnMap());
                return;
            case LV_KEY_ESC:
                THIS->map->setZoom(MapTileSettings::getZoomLevel() - 1);
                THIS->updateLocationMap(THIS->map->getObjectsOnMap());
                return;
            default: handled = false; break;
            }
            if (handled) {
                if (!THIS->map->scroll(deltaX, deltaY))
                    THIS->map->forceRedraw();
                THIS->updateLocationMap(THIS->map->getObjectsOnMap());
                return;
            }
        }

        if (key == LV_KEY_HOME) {
            // Focus the home button to enable side menu navigation
            if (objects.home_button) {
                lv_group_focus_obj(objects.home_button);
            }
        } else if (key == LV_KEY_NEXT || key == LV_KEY_PREV) {
            // Screen tab switching: cycle through main screens
            // Only switch tabs when we're at the main button level (not inside a sub-panel)
            if (THIS->activeSettings != eNone)
                return;

            struct TabMapping {
                lv_obj_t *button;
                lv_obj_t *panel;
                lv_obj_t *topPanel;
            };
            TabMapping tabMap[] = {
                {objects.home_button, objects.home_panel, objects.top_panel},
                {objects.nodes_button, objects.nodes_panel, objects.top_nodes_panel},
                {objects.groups_button, objects.groups_panel, objects.top_groups_panel},
                {objects.messages_button, objects.chats_panel, objects.top_chats_panel},
                {objects.map_button, objects.map_panel, objects.top_map_panel},
                {objects.settings_button, objects.controller_panel, objects.top_settings_panel}
            };
            const int numTabs = 6;

            // Find current active tab
            int currentIdx = -1;
            for (int i = 0; i < numTabs; i++) {
                if (tabMap[i].button == THIS->activeButton) {
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

                THIS->ui_set_active(tabMap[nextIdx].button, tabMap[nextIdx].panel, tabMap[nextIdx].topPanel);
            }
        }
    }
}

void TFTView_480x222::onEventsInitExtra()
{
    // Global key handler for screen navigation
    lv_obj_add_event_cb(objects.main_screen, this->ui_event_GlobalKeyHandler, LV_EVENT_KEY, NULL);

    // Main button focus highlight callbacks
    lv_obj_add_event_cb(objects.home_button, this->ui_event_MainButtonFocus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(objects.home_button, this->ui_event_MainButtonFocus, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(objects.nodes_button, this->ui_event_MainButtonFocus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(objects.nodes_button, this->ui_event_MainButtonFocus, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(objects.groups_button, this->ui_event_MainButtonFocus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(objects.groups_button, this->ui_event_MainButtonFocus, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(objects.messages_button, this->ui_event_MainButtonFocus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(objects.messages_button, this->ui_event_MainButtonFocus, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(objects.map_button, this->ui_event_MainButtonFocus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(objects.map_button, this->ui_event_MainButtonFocus, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(objects.settings_button, this->ui_event_MainButtonFocus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(objects.settings_button, this->ui_event_MainButtonFocus, LV_EVENT_DEFOCUSED, NULL);

    // Initialize focus style for main buttons (dark background by default)
    lv_obj_t *mainButtons[] = {objects.home_button, objects.nodes_button, objects.groups_button,
                               objects.messages_button, objects.map_button, objects.settings_button};
    for (auto *btn : mainButtons) {
        lv_obj_set_style_bg_color(btn, colorDarkGray, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void TFTView_480x222::onInitScreensExtra()
{
    // Create message counter badge on messages button (hidden by default)
    messagesBadge = lv_label_create(objects.messages_button);
    lv_label_set_text(messagesBadge, "");
    lv_obj_set_style_text_color(messagesBadge, lv_color_hex(0xFF0000), LV_PART_MAIN); // Red
    lv_obj_set_style_text_font(messagesBadge, &ui_font_montserrat_20, LV_PART_MAIN);
    // Add grey rounded background for readability
    lv_obj_set_style_bg_color(messagesBadge, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(messagesBadge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(messagesBadge, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(messagesBadge, 4, LV_PART_MAIN);
    lv_obj_align(messagesBadge, LV_ALIGN_TOP_RIGHT, -2, 2);
    lv_obj_add_flag(messagesBadge, LV_OBJ_FLAG_HIDDEN);
}

void TFTView_480x222::onNewMessageExtra(uint32_t from, bool isDM)
{
    // messagesBadge is updated via onUnreadMessagesUpdated() which is called from updateUnreadMessages()
}

void TFTView_480x222::onUnreadMessagesUpdated()
{
    // Update messages button badge
    if (messagesBadge) {
        if (unreadMessages > 0) {
            char badgeBuf[8];
            sprintf(badgeBuf, "%d", unreadMessages > 99 ? 99 : unreadMessages);
            lv_label_set_text(messagesBadge, badgeBuf);
            lv_obj_clear_flag(messagesBadge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(messagesBadge, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void TFTView_480x222::configureKeyboardLayouts()
{
    ::configureKeyboardLayouts(objects.keyboard);
}

#endif
