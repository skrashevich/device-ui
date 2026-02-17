
#include "input/I2CKeyboardInputDriver.h"
#include "util/ILog.h"
#include <Arduino.h>
#include <Wire.h>

#include "indev/lv_indev_private.h"

I2CKeyboardInputDriver::KeyboardList I2CKeyboardInputDriver::i2cKeyboardList;

namespace {
bool tdeckRussianLayoutEnabled = false;
uint32_t tdeckLayoutChangeCounter = 0;

// T-Deck keyboard can emit modifier-like scan codes for left modifiers.
// We track Alt+Shift press timing to toggle layout once per chord press.
constexpr uint32_t TDECK_LEFT_SHIFT_KEY = 0xE1;
constexpr uint32_t TDECK_LEFT_ALT_KEY = 0xE2;
constexpr uint32_t TDECK_SHIFT_FALLBACK_KEY = 0xE0; // known T-Deck special code (see shift-0 comment below)
// Some keyboard firmwares report modifiers in HID-like 0x80..0x87 range.
constexpr uint32_t TDECK_LEFT_SHIFT_HID_KEY = 0x81;
constexpr uint32_t TDECK_LEFT_ALT_HID_KEY = 0x82;
constexpr uint32_t TDECK_RIGHT_SHIFT_HID_KEY = 0x85;
constexpr uint32_t TDECK_RIGHT_ALT_HID_KEY = 0x86;
constexpr uint32_t TDECK_LAYOUT_CHORD_WINDOW_MS = 400;
constexpr uint32_t TDECK_LAYOUT_TOGGLE_COOLDOWN_MS = 700;
uint32_t tdeckLastLeftShiftMs = 0;
uint32_t tdeckLastLeftAltMs = 0;
uint32_t tdeckLastLayoutToggleMs = 0;

const char *mapLatinToRussianUtf8(uint32_t key)
{
    switch (key) {
    case '`':
        return "\xD1\x91";
    case '~':
        return "\xD0\x81";
    case 'q':
        return "\xD0\xB9";
    case 'w':
        return "\xD1\x86";
    case 'e':
        return "\xD1\x83";
    case 'r':
        return "\xD0\xBA";
    case 't':
        return "\xD0\xB5";
    case 'y':
        return "\xD0\xBD";
    case 'u':
        return "\xD0\xB3";
    case 'i':
        return "\xD1\x88";
    case 'o':
        return "\xD1\x89";
    case 'p':
        return "\xD0\xB7";
    case '[':
        return "\xD1\x85";
    case ']':
        return "\xD1\x8A";
    case 'a':
        return "\xD1\x84";
    case 's':
        return "\xD1\x8B";
    case 'd':
        return "\xD0\xB2";
    case 'f':
        return "\xD0\xB0";
    case 'g':
        return "\xD0\xBF";
    case 'h':
        return "\xD1\x80";
    case 'j':
        return "\xD0\xBE";
    case 'k':
        return "\xD0\xBB";
    case 'l':
        return "\xD0\xB4";
    case ';':
        return "\xD0\xB6";
    case '\'':
        return "\xD1\x8D";
    case 'z':
        return "\xD1\x8F";
    case 'x':
        return "\xD1\x87";
    case 'c':
        return "\xD1\x81";
    case 'v':
        return "\xD0\xBC";
    case 'b':
        return "\xD0\xB8";
    case 'n':
        return "\xD1\x82";
    case 'm':
        return "\xD1\x8C";
    case ',':
        return "\xD0\xB1";
    case '.':
        return "\xD1\x8E";
    case 'Q':
        return "\xD0\x99";
    case 'W':
        return "\xD0\xA6";
    case 'E':
        return "\xD0\xA3";
    case 'R':
        return "\xD0\x9A";
    case 'T':
        return "\xD0\x95";
    case 'Y':
        return "\xD0\x9D";
    case 'U':
        return "\xD0\x93";
    case 'I':
        return "\xD0\xA8";
    case 'O':
        return "\xD0\xA9";
    case 'P':
        return "\xD0\x97";
    case '{':
        return "\xD0\xA5";
    case '}':
        return "\xD0\xAA";
    case 'A':
        return "\xD0\xA4";
    case 'S':
        return "\xD0\xAB";
    case 'D':
        return "\xD0\x92";
    case 'F':
        return "\xD0\x90";
    case 'G':
        return "\xD0\x9F";
    case 'H':
        return "\xD0\xA0";
    case 'J':
        return "\xD0\x9E";
    case 'K':
        return "\xD0\x9B";
    case 'L':
        return "\xD0\x94";
    case ':':
        return "\xD0\x96";
    case '"':
        return "\xD0\xAD";
    case 'Z':
        return "\xD0\xAF";
    case 'X':
        return "\xD0\xA7";
    case 'C':
        return "\xD0\xA1";
    case 'V':
        return "\xD0\x9C";
    case 'B':
        return "\xD0\x98";
    case 'N':
        return "\xD0\xA2";
    case 'M':
        return "\xD0\xAC";
    case '<':
        return "\xD0\x91";
    case '>':
        return "\xD0\xAE";
    default:
        return nullptr;
    }
}

bool insertIntoFocusedTextarea(const char *text)
{
#if LV_USE_TEXTAREA
    lv_group_t *group = InputDriver::getInputGroup();
    if (!group || !text) {
        return false;
    }

    lv_obj_t *focused = lv_group_get_focused(group);
    if (!focused) {
        return false;
    }

    if (!lv_obj_check_type(focused, &lv_textarea_class)) {
        return false;
    }

    lv_textarea_add_text(focused, text);
    return true;
#else
    (void)text;
    return false;
#endif
}

bool isLeftShiftModifier(uint32_t key)
{
    return key == TDECK_LEFT_SHIFT_KEY || key == TDECK_SHIFT_FALLBACK_KEY || key == TDECK_LEFT_SHIFT_HID_KEY ||
           key == TDECK_RIGHT_SHIFT_HID_KEY;
}

bool isLeftAltModifier(uint32_t key)
{
    return key == TDECK_LEFT_ALT_KEY || key == TDECK_LEFT_ALT_HID_KEY || key == TDECK_RIGHT_ALT_HID_KEY;
}

bool decodeHidModifierMask(uint32_t key, bool &shift, bool &alt)
{
    shift = false;
    alt = false;

    if (key < 0x80 || key > 0xFF) {
        return false;
    }

    uint8_t mask = static_cast<uint8_t>(key & 0x7F);
    // HID-like mask: bit1=LShift, bit2=LAlt, bit5=RShift, bit6=RAlt.
    shift = (mask & 0x02) || (mask & 0x20);
    alt = (mask & 0x04) || (mask & 0x40);
    return shift || alt;
}

bool handleLayoutToggleChord(uint32_t key)
{
    uint32_t now = millis();
    bool shiftMask = false;
    bool altMask = false;
    if (decodeHidModifierMask(key, shiftMask, altMask)) {
        if (shiftMask) {
            tdeckLastLeftShiftMs = now;
            ILOG_DEBUG("T-Deck Shift modifier mask detected: 0x%02X", (unsigned int)key);
        }
        if (altMask) {
            tdeckLastLeftAltMs = now;
            ILOG_DEBUG("T-Deck Alt modifier mask detected: 0x%02X", (unsigned int)key);
        }
    } else if (isLeftShiftModifier(key)) {
        tdeckLastLeftShiftMs = now;
        ILOG_DEBUG("T-Deck Left Shift modifier detected: 0x%02X", (unsigned int)key);
    } else if (isLeftAltModifier(key)) {
        tdeckLastLeftAltMs = now;
        ILOG_DEBUG("T-Deck Left Alt modifier detected: 0x%02X", (unsigned int)key);
    } else {
        return false;
    }

    uint32_t diff = (tdeckLastLeftShiftMs > tdeckLastLeftAltMs) ? (tdeckLastLeftShiftMs - tdeckLastLeftAltMs)
                                                                : (tdeckLastLeftAltMs - tdeckLastLeftShiftMs);
    if (tdeckLastLeftShiftMs && tdeckLastLeftAltMs && diff <= TDECK_LAYOUT_CHORD_WINDOW_MS &&
        (now - tdeckLastLayoutToggleMs) > TDECK_LAYOUT_TOGGLE_COOLDOWN_MS) {
        tdeckLastLayoutToggleMs = now;
        bool ru = TDeckKeyboardInputDriver::toggleRussianLayout();
        ILOG_INFO("T-Deck keyboard layout toggled by Left Alt+Shift: %s", ru ? "RU" : "EN");
    }

    // Consume modifier key events, they should not be forwarded as text/navigation keys.
    return true;
}
} // namespace

I2CKeyboardInputDriver::I2CKeyboardInputDriver(void) {}

void I2CKeyboardInputDriver::init(void)
{
    keyboard = lv_indev_create();
    lv_indev_set_type(keyboard, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(keyboard, keyboard_read);

    if (!inputGroup) {
        inputGroup = lv_group_create();
        lv_group_set_default(inputGroup);
    }
    lv_indev_set_group(keyboard, inputGroup);
}

bool I2CKeyboardInputDriver::registerI2CKeyboard(I2CKeyboardInputDriver *driver, std::string name, uint8_t address)
{
    auto keyboardDef = std::unique_ptr<KeyboardDefinition>(new KeyboardDefinition{driver, name, address});
    i2cKeyboardList.push_back(std::move(keyboardDef));
    ILOG_INFO("Registered I2C keyboard: %s at address 0x%02X", name.c_str(), address);
    return true;
}

void I2CKeyboardInputDriver::keyboard_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    // Read from all registered keyboards
    for (auto &keyboardDef : i2cKeyboardList) {
        keyboardDef->driver->readKeyboard(keyboardDef->address, indev, data);
        if (data->state == LV_INDEV_STATE_PRESSED) {
            // If any keyboard reports a key press, we stop reading further
            return;
        }
    }
}

// ---------- TDeckKeyboardInputDriver Implementation ----------

TDeckKeyboardInputDriver::TDeckKeyboardInputDriver(uint8_t address)
{
    registerI2CKeyboard(this, "T-Deck Keyboard", address);
}

void TDeckKeyboardInputDriver::setRussianLayoutEnabled(bool enabled)
{
    if (tdeckRussianLayoutEnabled == enabled) {
        return;
    }
    tdeckRussianLayoutEnabled = enabled;
    tdeckLayoutChangeCounter++;
    ILOG_INFO("T-Deck keyboard layout: %s", enabled ? "RU" : "EN");
}

bool TDeckKeyboardInputDriver::isRussianLayoutEnabled(void)
{
    return tdeckRussianLayoutEnabled;
}

bool TDeckKeyboardInputDriver::toggleRussianLayout(void)
{
    setRussianLayoutEnabled(!isRussianLayoutEnabled());
    return isRussianLayoutEnabled();
}

uint32_t TDeckKeyboardInputDriver::getLayoutChangeCounter(void)
{
    return tdeckLayoutChangeCounter;
}

/******************************************************************
    LV_KEY_NEXT: Focus on the next object
    LV_KEY_PREV: Focus on the previous object
    LV_KEY_ENTER: Triggers LV_EVENT_PRESSED, LV_EVENT_CLICKED, or LV_EVENT_LONG_PRESSED etc. events
    LV_KEY_UP: Increase value or move upwards
    LV_KEY_DOWN: Decrease value or move downwards
    LV_KEY_RIGHT: Increase value or move to the right
    LV_KEY_LEFT: Decrease value or move to the left
    LV_KEY_ESC: Close or exit (E.g. close a Drop down list)
    LV_KEY_DEL: Delete (E.g. a character on the right in a Text area)
    LV_KEY_BACKSPACE: Delete a character on the left (E.g. in a Text area)
    LV_KEY_HOME: Go to the beginning/top (E.g. in a Text area)
    LV_KEY_END: Go to the end (E.g. in a Text area)

    LV_KEY_UP        = 17,  // 0x11
    LV_KEY_DOWN      = 18,  // 0x12
    LV_KEY_RIGHT     = 19,  // 0x13
    LV_KEY_LEFT      = 20,  // 0x14
    LV_KEY_ESC       = 27,  // 0x1B
    LV_KEY_DEL       = 127, // 0x7F
    LV_KEY_BACKSPACE = 8,   // 0x08
    LV_KEY_ENTER     = 10,  // 0x0A, '\n'
    LV_KEY_NEXT      = 9,   // 0x09, '\t'
    LV_KEY_PREV      = 11,  // 0x0B, '
    LV_KEY_HOME      = 2,   // 0x02, STX
    LV_KEY_END       = 3,   // 0x03, ETX
*******************************************************************/

void TDeckKeyboardInputDriver::readKeyboard(uint8_t address, lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint32_t keyValue = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    uint8_t bytes = Wire.requestFrom(address, 1);
    if (Wire.available() > 0 && bytes > 0) {
        keyValue = Wire.read();
        if (handleLayoutToggleChord(keyValue)) {
            keyValue = 0;
        }
        // ignore empty reads and keycode 224(E0, shift-0 on T-Deck) which causes internal issues
        if (keyValue != 0x00 && keyValue != 0xE0) {
            data->state = LV_INDEV_STATE_PRESSED;
            ILOG_DEBUG("key press value: %d", (int)keyValue);

            switch (keyValue) {
            case 0x0D:
                keyValue = LV_KEY_ENTER;
                break;
            default:
                if (isRussianLayoutEnabled()) {
                    const char *ruChar = mapLatinToRussianUtf8(keyValue);
                    if (ruChar && insertIntoFocusedTextarea(ruChar)) {
                        keyValue = 0;
                        data->state = LV_INDEV_STATE_RELEASED;
                    }
                }
                break;
            }
        } else {
            keyValue = 0;
        }
    }
    data->key = keyValue;
}

// ---------- TCA8418KeyboardInputDriver Implementation ----------

TCA8418KeyboardInputDriver::TCA8418KeyboardInputDriver(uint8_t address)
{
    registerI2CKeyboard(this, "TCA8418 Keyboard", address);
}

void TCA8418KeyboardInputDriver::init(void)
{
    // Additional initialization for TCA8418 if needed
    I2CKeyboardInputDriver::init();
}

void TCA8418KeyboardInputDriver::readKeyboard(uint8_t address, lv_indev_t *indev, lv_indev_data_t *data)
{
    // TODO
    char keyValue = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = (uint32_t)keyValue;
}

// ---------- TLoraPagerKeyboardInputDriver Implementation ----------

TLoraPagerKeyboardInputDriver::TLoraPagerKeyboardInputDriver(uint8_t address) : TCA8418KeyboardInputDriver(address)
{
    registerI2CKeyboard(this, "TLora Pager Keyboard", address);
}

void TLoraPagerKeyboardInputDriver::init(void)
{
    // Additional initialization for TLora-Pager if needed
    TCA8418KeyboardInputDriver::init();
}

void TLoraPagerKeyboardInputDriver::readKeyboard(uint8_t address, lv_indev_t *indev, lv_indev_data_t *data)
{
    // TODO
    char keyValue = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = (uint32_t)keyValue;
}

// ---------- TDeckProKeyboardInputDriver Implementation ----------

TDeckProKeyboardInputDriver::TDeckProKeyboardInputDriver(uint8_t address) : TCA8418KeyboardInputDriver(address)
{
    registerI2CKeyboard(this, "T-Deck Pro Keyboard", address);
}

void TDeckProKeyboardInputDriver::init(void)
{
    // Additional initialization for TLora-Pager if needed
    TCA8418KeyboardInputDriver::init();
}

void TDeckProKeyboardInputDriver::readKeyboard(uint8_t address, lv_indev_t *indev, lv_indev_data_t *data)
{
    // TODO
    char keyValue = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = (uint32_t)keyValue;
}

// ---------- BBQ10KeyboardInputDriver Implementation ----------

BBQ10KeyboardInputDriver::BBQ10KeyboardInputDriver(uint8_t address)
{
    registerI2CKeyboard(this, "BBQ10 Keyboard", address);
}

void BBQ10KeyboardInputDriver::init(void)
{
    I2CKeyboardInputDriver::init();
    // Additional initialization for BBQ10 if needed
}

void BBQ10KeyboardInputDriver::readKeyboard(uint8_t address, lv_indev_t *indev, lv_indev_data_t *data)
{
    char keyValue = 0;
    uint8_t bytes = Wire.requestFrom(address, 1);
    if (Wire.available() > 0 && bytes > 0) {
        keyValue = Wire.read();
        // ignore empty reads and keycode 224(E0, shift-0 on T-Deck) which causes internal issues
        if (keyValue != (char)0x00 && keyValue != (char)0xE0) {
            data->state = LV_INDEV_STATE_PRESSED;
            ILOG_DEBUG("key press value: %d", (int)keyValue);

            switch (keyValue) {
            case 0x0D:
                keyValue = LV_KEY_ENTER;
                break;
            default:
                break;
            }
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    }
    data->key = (uint32_t)keyValue;
}

// ---------- CardKBInputDriver Implementation ----------

CardKBInputDriver::CardKBInputDriver(uint8_t address)
{
    registerI2CKeyboard(this, "Card Keyboard", address);
}

void CardKBInputDriver::readKeyboard(uint8_t address, lv_indev_t *indev, lv_indev_data_t *data)
{
    char keyValue = 0;
    Wire.requestFrom(address, 1);
    if (Wire.available() > 0) {
        keyValue = Wire.read();
        // ignore empty reads and keycode 224 which causes internal issues
        if (keyValue != (char)0x00 && keyValue != (char)0xE0) {
            data->state = LV_INDEV_STATE_PRESSED;
            ILOG_DEBUG("key press value: %d", (int)keyValue);

            switch (keyValue) {
            case 0x0D:
                keyValue = LV_KEY_ENTER;
                break;
            case 0xB4:
                keyValue = LV_KEY_LEFT;
                break;
            case 0xB5:
                keyValue = LV_KEY_UP;
                break;
            case 0xB6:
                keyValue = LV_KEY_DOWN;
                break;
            case 0xB7:
                keyValue = LV_KEY_RIGHT;
                break;
            case 0x99: // Fn+UP
                keyValue = LV_KEY_HOME;
                break;
            case 0xA4: // Fn+DOWN
                keyValue = LV_KEY_END;
                break;
            case 0x8B: // Fn+BS
                keyValue = LV_KEY_DEL;
                break;
            case 0x8C: // Fn+TAB
                keyValue = LV_KEY_PREV;
                break;
            case 0xA3: // Fn+ENTER
                // simulate a long press on Fn+ENTER (see indev_keypad_proc() in indev.c)
                indev->wait_until_release = 0;
                indev->pr_timestamp = lv_tick_get() - indev->long_press_time - 1;
                indev->long_pr_sent = 0;
                indev->keypad.last_state = LV_INDEV_STATE_PRESSED;
                indev->keypad.last_key = LV_KEY_ENTER;
                keyValue = LV_KEY_ENTER;
                break;
            default:
                break;
            }
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    }
    data->key = (uint32_t)keyValue;
}

// ---------- MPR121KeyboardInputDriver Implementation ----------

MPR121KeyboardInputDriver::MPR121KeyboardInputDriver(uint8_t address)
{
    registerI2CKeyboard(this, "MPR121 Keyboard", address);
}

void MPR121KeyboardInputDriver::init(void)
{
    I2CKeyboardInputDriver::init();
    // Additional initialization for MPR121 if needed
}

void MPR121KeyboardInputDriver::readKeyboard(uint8_t address, lv_indev_t *indev, lv_indev_data_t *data)
{
    // TODO
    char keyValue = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = (uint32_t)keyValue;
}
