
#include "input/I2CKeyboardInputDriver.h"
#include "util/ILog.h"
#include <Arduino.h>
#include <Wire.h>
#include <cstddef>

#ifdef DEVICEUI_TDECK_KEYLOG
#include <cstdarg>
#include <cstdio>
#endif

#include "indev/lv_indev_private.h"
#include "widgets/textarea/lv_textarea.h"

I2CKeyboardInputDriver::KeyboardList I2CKeyboardInputDriver::i2cKeyboardList;
NavigationCallback I2CKeyboardInputDriver::navigateHomeCallback = nullptr;
bool I2CKeyboardInputDriver::altModifierHeld = false;
ScrollCallback I2CKeyboardInputDriver::scrollCallback = nullptr;
AltIndicatorCallback I2CKeyboardInputDriver::altIndicatorCallback = nullptr;
InputEventCallback I2CKeyboardInputDriver::inputEventCallback = nullptr;

namespace
{
bool tdeckRussianLayoutEnabled = false;
uint32_t tdeckLayoutChangeCounter = 0;

#ifdef DEVICEUI_TDECK_KEYLOG
void tdeckKeyLog(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    printf("[TDECK-KBD] ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}
#define TDECK_KEY_LOG(...) tdeckKeyLog(__VA_ARGS__)
#else
#define TDECK_KEY_LOG(...)
#endif

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
constexpr uint32_t TDECK_DOUBLE_SPACE_WINDOW_MS = 450;
uint32_t tdeckLastLeftShiftMs = 0;
uint32_t tdeckLastLeftAltMs = 0;
uint32_t tdeckLastLayoutToggleMs = 0;
uint32_t tdeckLastSpaceMs = 0;

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

lv_obj_t *getFocusedTextarea()
{
#if LV_USE_TEXTAREA
    lv_group_t *group = InputDriver::getInputGroup();
    if (!group) {
        return nullptr;
    }

    lv_obj_t *focused = lv_group_get_focused(group);
    if (!focused) {
        return nullptr;
    }

    if (!lv_obj_check_type(focused, &lv_textarea_class)) {
        return nullptr;
    }

    return focused;
#else
    return nullptr;
#endif
}

bool insertIntoFocusedTextarea(const char *text)
{
    lv_obj_t *focused = getFocusedTextarea();
    if (!focused || !text) {
        return false;
    }

    lv_textarea_add_text(focused, text);
    return true;
}

size_t utf8ByteIndexFromCharIndex(const char *text, uint32_t charIndex)
{
    size_t byteIndex = 0;
    uint32_t charsSeen = 0;

    while (text && text[byteIndex] != '\0' && charsSeen < charIndex) {
        uint8_t c = static_cast<uint8_t>(text[byteIndex]);
        if ((c & 0x80) == 0x00) {
            byteIndex += 1;
        } else if ((c & 0xE0) == 0xC0) {
            byteIndex += 2;
        } else if ((c & 0xF0) == 0xE0) {
            byteIndex += 3;
        } else if ((c & 0xF8) == 0xF0) {
            byteIndex += 4;
        } else {
            // Invalid lead byte, keep moving to avoid getting stuck.
            byteIndex += 1;
        }
        charsSeen++;
    }

    return byteIndex;
}

bool replacePreviousSpaceWithDotInFocusedTextarea()
{
#if LV_USE_TEXTAREA
    lv_obj_t *focused = getFocusedTextarea();
    if (!focused) {
        return false;
    }

    const char *text = lv_textarea_get_text(focused);
    if (!text) {
        return false;
    }

    uint32_t cursorPos = lv_textarea_get_cursor_pos(focused);
    if (cursorPos == 0) {
        return false;
    }

    size_t prevCharByteIndex = utf8ByteIndexFromCharIndex(text, cursorPos - 1);
    if (text[prevCharByteIndex] != ' ') {
        return false;
    }

    lv_textarea_delete_char(focused);
    lv_textarea_add_text(focused, ".");
    return true;
#else
    return false;
#endif
}

bool handleDoubleSpaceShortcut(uint32_t key)
{
    if (key != ' ') {
        tdeckLastSpaceMs = 0;
        return false;
    }

    uint32_t now = millis();
    if (tdeckLastSpaceMs != 0 && (now - tdeckLastSpaceMs) <= TDECK_DOUBLE_SPACE_WINDOW_MS) {
        tdeckLastSpaceMs = 0;
        if (replacePreviousSpaceWithDotInFocusedTextarea()) {
            TDECK_KEY_LOG("double-space shortcut -> '.'");
            return true;
        }
        return false;
    }

    tdeckLastSpaceMs = now;
    return false;
}

void resetDoubleSpaceShortcutState()
{
    tdeckLastSpaceMs = 0;
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
            TDECK_KEY_LOG("shift mask detected raw=0x%02X (%u)", (unsigned int)key, (unsigned int)key);
        }
        if (altMask) {
            tdeckLastLeftAltMs = now;
            ILOG_DEBUG("T-Deck Alt modifier mask detected: 0x%02X", (unsigned int)key);
            TDECK_KEY_LOG("alt mask detected raw=0x%02X (%u)", (unsigned int)key, (unsigned int)key);
        }
    } else if (isLeftShiftModifier(key)) {
        tdeckLastLeftShiftMs = now;
        ILOG_DEBUG("T-Deck Left Shift modifier detected: 0x%02X", (unsigned int)key);
        TDECK_KEY_LOG("left shift detected raw=0x%02X (%u)", (unsigned int)key, (unsigned int)key);
    } else if (isLeftAltModifier(key)) {
        tdeckLastLeftAltMs = now;
        ILOG_DEBUG("T-Deck Left Alt modifier detected: 0x%02X", (unsigned int)key);
        TDECK_KEY_LOG("left alt detected raw=0x%02X (%u)", (unsigned int)key, (unsigned int)key);
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
        TDECK_KEY_LOG("layout toggled by Alt+Shift -> %s", ru ? "RU" : "EN");
    }

    // Modifiers/chords are not text input, so break "double-space" sequence.
    resetDoubleSpaceShortcutState();
    // Consume modifier key events, they should not be forwarded as text/navigation keys.
    return true;
}
} // namespace

I2CKeyboardInputDriver::I2CKeyboardInputDriver(void) {}

void I2CKeyboardInputDriver::setAltModifierHeld(bool held)
{
    altModifierHeld = held;
    if (altIndicatorCallback) {
        altIndicatorCallback(held);
    }
}

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
            lv_display_trigger_activity(NULL);
            if (inputEventCallback) {
                inputEventCallback();
            }
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
    uint8_t bytes = Wire.requestFrom(static_cast<uint8_t>(address), static_cast<uint8_t>(1));
    if (Wire.available() > 0 && bytes > 0) {
        keyValue = Wire.read();
        TDECK_KEY_LOG("raw key from i2c addr=0x%02X: 0x%02X (%u)", (unsigned int)address, (unsigned int)keyValue,
                      (unsigned int)keyValue);
        if (handleLayoutToggleChord(keyValue)) {
            TDECK_KEY_LOG("key consumed as modifier/chord: 0x%02X (%u)", (unsigned int)keyValue, (unsigned int)keyValue);
            keyValue = 0;
        }
        // ignore empty reads and keycode 224(E0, shift-0 on T-Deck) which causes internal issues
        if (keyValue != 0x00 && keyValue != 0xE0) {
            data->state = LV_INDEV_STATE_PRESSED;
            ILOG_DEBUG("key press value: %d", (int)keyValue);
            TDECK_KEY_LOG("forward key to lvgl: 0x%02X (%u)", (unsigned int)keyValue, (unsigned int)keyValue);

            if (handleDoubleSpaceShortcut(keyValue)) {
                keyValue = 0;
                data->state = LV_INDEV_STATE_RELEASED;
            }

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

// ---------- TCA8418 Register Definitions ----------
#define TCA8418_REG_CFG 0x01
#define TCA8418_REG_INT_STAT 0x02
#define TCA8418_REG_KEY_LCK_EC 0x03
#define TCA8418_REG_KEY_EVENT_A 0x04
#define TCA8418_REG_KP_GPIO_1 0x1D
#define TCA8418_REG_KP_GPIO_2 0x1E
#define TCA8418_REG_KP_GPIO_3 0x1F
#define TCA8418_REG_GPIO_DIR_1 0x23
#define TCA8418_REG_GPIO_DIR_2 0x24
#define TCA8418_REG_GPIO_DIR_3 0x25
#define TCA8418_REG_GPI_EM_1 0x20
#define TCA8418_REG_GPI_EM_2 0x21
#define TCA8418_REG_GPI_EM_3 0x22
#define TCA8418_REG_GPIO_INT_LVL_1 0x26
#define TCA8418_REG_GPIO_INT_LVL_2 0x27
#define TCA8418_REG_GPIO_INT_LVL_3 0x28
#define TCA8418_REG_GPIO_INT_EN_1 0x1A
#define TCA8418_REG_GPIO_INT_EN_2 0x1B
#define TCA8418_REG_GPIO_INT_EN_3 0x1C
#define TCA8418_REG_DEBOUNCE_DIS_1 0x29
#define TCA8418_REG_DEBOUNCE_DIS_2 0x2A
#define TCA8418_REG_DEBOUNCE_DIS_3 0x2B

namespace
{
// Helper to write a register
void tca8418WriteReg(uint8_t address, uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

// Helper to read a register
uint8_t tca8418ReadReg(uint8_t address, uint8_t reg)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom(address, (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0;
}

// T-Pager keyboard layout: 4 rows x 10 columns = 31 keys
// Key mapping from TCA8418 key codes to characters [normal, shift, sym]
const char TLoraPagerKeyMap[31][3] = {
    {'q', 'Q', '1'},    // Key 1
    {'w', 'W', '2'},    // Key 2
    {'e', 'E', '3'},    // Key 3
    {'r', 'R', '4'},    // Key 4
    {'t', 'T', '5'},    // Key 5
    {'y', 'Y', '6'},    // Key 6
    {'u', 'U', '7'},    // Key 7
    {'i', 'I', '8'},    // Key 8
    {'o', 'O', '9'},    // Key 9
    {'p', 'P', '0'},    // Key 10
    {'a', 'A', '*'},    // Key 11
    {'s', 'S', '/'},    // Key 12
    {'d', 'D', '+'},    // Key 13
    {'f', 'F', '-'},    // Key 14
    {'g', 'G', '='},    // Key 15
    {'h', 'H', ':'},    // Key 16
    {'j', 'J', '\''},   // Key 17
    {'k', 'K', '"'},    // Key 18
    {'l', 'L', '@'},    // Key 19
    {0x0D, 0x09, 0x0D}, // Key 20: Enter, Tab (shift), Enter (sym)
    {0, 0, 0},          // Key 21: Sym modifier (no output)
    {'z', 'Z', '_'},    // Key 22
    {'x', 'X', '$'},    // Key 23
    {'c', 'C', ';'},    // Key 24
    {'v', 'V', '?'},    // Key 25
    {'b', 'B', '!'},    // Key 26
    {'n', 'N', ','},    // Key 27
    {'m', 'M', '.'},    // Key 28
    {0, 0, 0},          // Key 29: Shift modifier (no output)
    {0x08, 0x08, 0x1B}, // Key 30: Backspace, Backspace (shift), ESC (sym)
    {' ', ' ', ' '}     // Key 31: Space
};

// Modifier key indices (0-based)
constexpr uint8_t MODIFIER_SYM_KEY = 20;   // Key 21
constexpr uint8_t MODIFIER_SHIFT_KEY = 28; // Key 29

// Modifier state (sticky toggles)
uint8_t modifierState = 0; // 0=normal, 1=shift, 2=sym

uint8_t tca8418Address = 0x34;
} // namespace

TCA8418KeyboardInputDriver::TCA8418KeyboardInputDriver(uint8_t address)
{
    tca8418Address = address;
    registerI2CKeyboard(this, "TCA8418 Keyboard", address);
}

void TCA8418KeyboardInputDriver::init(void)
{
    I2CKeyboardInputDriver::init();

    // Initialize TCA8418 - set up keyboard matrix and key event FIFO.
    tca8418WriteReg(tca8418Address, TCA8418_REG_GPIO_DIR_1, 0x00);
    tca8418WriteReg(tca8418Address, TCA8418_REG_GPIO_DIR_2, 0x00);
    tca8418WriteReg(tca8418Address, TCA8418_REG_GPIO_DIR_3, 0x00);

    tca8418WriteReg(tca8418Address, TCA8418_REG_GPI_EM_1, 0xFF);
    tca8418WriteReg(tca8418Address, TCA8418_REG_GPI_EM_2, 0xFF);
    tca8418WriteReg(tca8418Address, TCA8418_REG_GPI_EM_3, 0xFF);

    tca8418WriteReg(tca8418Address, TCA8418_REG_GPIO_INT_LVL_1, 0x00);
    tca8418WriteReg(tca8418Address, TCA8418_REG_GPIO_INT_LVL_2, 0x00);
    tca8418WriteReg(tca8418Address, TCA8418_REG_GPIO_INT_LVL_3, 0x00);

    tca8418WriteReg(tca8418Address, TCA8418_REG_GPIO_INT_EN_1, 0xFF);
    tca8418WriteReg(tca8418Address, TCA8418_REG_GPIO_INT_EN_2, 0xFF);
    tca8418WriteReg(tca8418Address, TCA8418_REG_GPIO_INT_EN_3, 0xFF);

    tca8418WriteReg(tca8418Address, TCA8418_REG_DEBOUNCE_DIS_1, 0x00);
    tca8418WriteReg(tca8418Address, TCA8418_REG_DEBOUNCE_DIS_2, 0x00);
    tca8418WriteReg(tca8418Address, TCA8418_REG_DEBOUNCE_DIS_3, 0x00);

    while (tca8418ReadReg(tca8418Address, TCA8418_REG_KEY_EVENT_A) != 0) {
    }

    tca8418WriteReg(tca8418Address, TCA8418_REG_INT_STAT, 0x03);
    uint8_t cfg = tca8418ReadReg(tca8418Address, TCA8418_REG_CFG);
    cfg |= 0x01; // KE_IEN - Key events interrupt enable
    tca8418WriteReg(tca8418Address, TCA8418_REG_CFG, cfg);
}

void TCA8418KeyboardInputDriver::readKeyboard(uint8_t address, lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = 0;

    Wire.beginTransmission(address);
    Wire.write(TCA8418_REG_KEY_LCK_EC);
    Wire.endTransmission();
    Wire.requestFrom(address, (uint8_t)1);
    if (Wire.available()) {
        uint8_t keyCount = Wire.read() & 0x0F;
        if (keyCount > 0) {
            Wire.beginTransmission(address);
            Wire.write(TCA8418_REG_KEY_EVENT_A);
            Wire.endTransmission();
            Wire.requestFrom(address, (uint8_t)1);
            if (Wire.available()) {
                uint8_t keyEvent = Wire.read();
                uint8_t keyCode = keyEvent & 0x7F;
                bool pressed = (keyEvent & 0x80) != 0;
                if (pressed && keyCode > 0) {
                    data->state = LV_INDEV_STATE_PRESSED;
                    data->key = keyCode;
                }
            }
        }
    }
}

// ---------- TLoraPagerKeyboardInputDriver Implementation ----------

TLoraPagerKeyboardInputDriver::TLoraPagerKeyboardInputDriver(uint8_t address) : TCA8418KeyboardInputDriver(address)
{
    // Parent constructor already registers this device.
}

void TLoraPagerKeyboardInputDriver::init(void)
{
    TCA8418KeyboardInputDriver::init();

    // Set up T-Pager keyboard matrix: 4 rows x 10 columns.
    tca8418WriteReg(tca8418Address, TCA8418_REG_KP_GPIO_1, 0x0F); // Rows 0-3
    tca8418WriteReg(tca8418Address, TCA8418_REG_KP_GPIO_2, 0xFF); // Columns 0-7
    tca8418WriteReg(tca8418Address, TCA8418_REG_KP_GPIO_3, 0x03); // Columns 8-9
}

void TLoraPagerKeyboardInputDriver::readKeyboard(uint8_t address, lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = 0;

    Wire.beginTransmission(address);
    Wire.write(TCA8418_REG_KEY_LCK_EC);
    Wire.endTransmission();
    Wire.requestFrom(address, (uint8_t)1);
    if (Wire.available()) {
        uint8_t keyCount = Wire.read() & 0x0F;
        if (keyCount == 0) {
            return;
        }

        Wire.beginTransmission(address);
        Wire.write(TCA8418_REG_KEY_EVENT_A);
        Wire.endTransmission();
        Wire.requestFrom(address, (uint8_t)1);
        if (!Wire.available()) {
            return;
        }

        uint8_t keyEvent = Wire.read();
        uint8_t keyCode = keyEvent & 0x7F;
        bool pressed = (keyEvent & 0x80) != 0;
        if (keyCode == 0 || keyCode > 31) {
            return;
        }

        uint8_t keyIndex = keyCode - 1;

        // Sym released - disable temporary scroll mode while keeping modifier state.
        if (!pressed && keyIndex == MODIFIER_SYM_KEY) {
            I2CKeyboardInputDriver::setAltModifierHeld(false);
            return;
        }
        if (!pressed) {
            return;
        }

        if (keyIndex == MODIFIER_SHIFT_KEY) {
            if (I2CKeyboardInputDriver::isAltModifierHeld()) {
                bool ru = TDeckKeyboardInputDriver::toggleRussianLayout();
                ILOG_INFO("T-Pager keyboard layout toggled by Sym+Shift: %s", ru ? "RU" : "EN");
                modifierState = 0;
                I2CKeyboardInputDriver::setAltModifierHeld(false);
                return;
            }
            modifierState = (modifierState == 1) ? 0 : 1;
            return;
        }
        if (keyIndex == MODIFIER_SYM_KEY) {
            modifierState = (modifierState == 2) ? 0 : 2;
            I2CKeyboardInputDriver::setAltModifierHeld(true);
            return;
        }

        char keyChar = TLoraPagerKeyMap[keyIndex][modifierState];
        if (keyChar == 0) {
            return;
        }

        // Sym modifier navigation: WASD-style when not in a textarea
        if (modifierState == 2) {
            lv_group_t *defGroup = lv_group_get_default();
            lv_obj_t *focused = defGroup ? lv_group_get_focused(defGroup) : nullptr;
            bool isTextarea = focused && lv_obj_check_type(focused, &lv_textarea_class);
            if (!isTextarea) {
                uint32_t navKey = 0;
                switch (keyChar) {
                    case '1': navKey = LV_KEY_HOME; break;  // sym+q -> HOME
                    case '2': navKey = LV_KEY_UP; break;    // sym+w -> UP
                    case '3': navKey = LV_KEY_NEXT; break;  // sym+e -> NEXT
                    case '4': navKey = LV_KEY_ESC; break;   // sym+r -> ESC (close/cancel/back)
                    case '*': navKey = LV_KEY_LEFT; break;  // sym+a -> LEFT
                    case '/': navKey = LV_KEY_DOWN; break;  // sym+s -> DOWN
                    case '+': navKey = LV_KEY_RIGHT; break; // sym+d -> RIGHT
                    case '5': navKey = LV_KEY_ENTER; break; // sym+t -> ENTER (confirm)
                    case '-': navKey = LV_KEY_PREV; break;  // sym+f -> PREV
                }
                if (navKey) {
                    data->key = navKey;
                    data->state = LV_INDEV_STATE_PRESSED;
                    modifierState = 0;
                    I2CKeyboardInputDriver::setAltModifierHeld(false);
                    return;
                }
            }
        }

        data->state = LV_INDEV_STATE_PRESSED;

        // In RU layout mode, map key output to Cyrillic when entering text.
        if (TDeckKeyboardInputDriver::isRussianLayoutEnabled()) {
            const char *ruChar = nullptr;

            if (modifierState == 2) {
                // Sym layer: map specific Sym+key combos to missing Russian letters
                // These 7 letters are inaccessible via normal keys on T-Pager
                switch (keyChar) {
                case ';':
                    ruChar = "\xD0\xB6";
                    break; // Sym+C -> ж
                case '\'':
                    ruChar = "\xD1\x8D";
                    break; // Sym+J -> э
                case ',':
                    ruChar = "\xD0\xB1";
                    break; // Sym+N -> б
                case '.':
                    ruChar = "\xD1\x8E";
                    break; // Sym+M -> ю
                case ':':
                    ruChar = "\xD1\x85";
                    break; // Sym+H -> х
                case '"':
                    ruChar = "\xD1\x8A";
                    break; // Sym+K -> ъ
                case '=':
                    ruChar = "\xD1\x91";
                    break; // Sym+G -> ё
                }
            } else {
                // Normal/Shift layer: standard ЙЦУКЕН mapping
                ruChar = mapLatinToRussianUtf8(static_cast<uint32_t>(static_cast<unsigned char>(keyChar)));
            }

            if (ruChar && insertIntoFocusedTextarea(ruChar)) {
                data->state = LV_INDEV_STATE_RELEASED;
                data->key = 0;
                modifierState = 0;
                return;
            }
        }

        switch (keyChar) {
        case 0x0D: // Enter
            data->key = LV_KEY_ENTER;
            break;
        case 0x09: // Tab
            data->key = LV_KEY_NEXT;
            break;
        case 0x08: // Backspace
        {
            lv_group_t *defGroup = lv_group_get_default();
            lv_obj_t *focused = defGroup ? lv_group_get_focused(defGroup) : nullptr;
            if (focused && lv_obj_check_type(focused, &lv_textarea_class)) {
                data->key = LV_KEY_BACKSPACE;
            } else if (I2CKeyboardInputDriver::navigateHomeCallback) {
                I2CKeyboardInputDriver::navigateHomeCallback();
                data->state = LV_INDEV_STATE_RELEASED;
                return;
            } else {
                data->key = LV_KEY_ESC;
            }
        } break;
        case 0x1B: // ESC
            data->key = LV_KEY_ESC;
            break;
        default:
            data->key = (uint32_t)keyChar;
            break;
        }

        // One-shot modifiers: clear after emitting a regular key.
        modifierState = 0;
    }
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
    uint8_t bytes = Wire.requestFrom(static_cast<uint8_t>(address), static_cast<uint8_t>(1));
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
    Wire.requestFrom(static_cast<uint8_t>(address), static_cast<uint8_t>(1));
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
