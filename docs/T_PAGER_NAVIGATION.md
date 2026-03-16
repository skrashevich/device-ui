# T-Pager Navigation Guide

## Overview

The T-Lora Pager is a handheld LoRa communication device with a 480x222 display and a 31-key TCA8418 keyboard. Navigation relies on keyboard shortcuts, a rotary encoder, and modifier keys. There is no touchscreen.

## Hardware

- **Display**: 480x222 pixels
- **Keyboard**: TCA8418 I2C keyboard controller with 31 keys
- **Rotary Encoder**: Integrated for navigation and scrolling
- **Layout**: 4 rows × 10 columns of keys

## Keyboard Layout

The keyboard is organized in a QWERTY-style layout with multiple modifier states:

```
Row 1: 1 2 3 4 5 6 7 8 9 0
Row 2: Q W E R T Y U I O P
Row 3: A S D F G H J K L (Shift)
Row 4: (Sym) Z X C V B N M (Backspace)
```

### Key Modifiers

- **Shift**: Produces uppercase letters and alternate characters
- **Sym**: Accesses symbols and navigation shortcuts
- **Alt**: Used with Shift to toggle keyboard layout (EN/RU)

## Navigation Shortcuts (Sym + Key)

When the **Sym modifier** is held, letter keys produce navigation commands instead of text characters. This applies when NOT in a text input field.

| Shortcut | Action | LVGL Key | Use Case |
|----------|--------|----------|----------|
| Sym+Q (1) | Go Home | LV_KEY_HOME | Jump to home screen |
| Sym+W (2) | Up | LV_KEY_UP | Move focus up in lists/menus |
| Sym+E (3) | Next Screen | LV_KEY_NEXT | Cycle through screens |
| Sym+A (*) | Left | LV_KEY_LEFT | Move focus left |
| Sym+S (/) | Down | LV_KEY_DOWN | Move focus down in lists/menus |
| Sym+D (+) | Right | LV_KEY_RIGHT | Move focus right |
| Sym+T (5) | Confirm/Enter | LV_KEY_ENTER | Select/activate focused item |
| Sym+F (-) | Previous Screen | LV_KEY_PREV | Cycle back through screens |
| Backspace | Back/ESC | LV_KEY_ESC | Exit menu (outside text input) |
| Sym+Shift | Toggle Layout | — | Switch between EN (Latin) and RU (Russian) keyboard layout |

**Note**: Inside text input fields, Sym+key produces the corresponding symbol character (1, 2, 3, *, /, +, 5, -) as defined by the symbol layer, not navigation commands.

## Rotary Encoder

The rotary encoder is the primary input device for precise navigation and scrolling.

### Encoder Actions

- **Rotate Clockwise/Counter-Clockwise**: Navigate between UI elements with acceleration
  - Slower rotation = slower navigation
  - Faster rotation = faster scrolling (acceleration-based)

- **Press (Click)**: Confirm/Enter the currently focused element

- **Alt+Rotate**: Scroll content within a focused element (e.g., scroll within a long list)

The encoder provides smooth, gesture-like control for navigating large lists, maps, and message threads.

## Screen Navigation

### Screen Cycle
The Sym+E and Sym+F shortcuts cycle through the main application screens in a loop:

```
Home → Nodes → Groups → Messages → Map → Settings → Home
```

- **Sym+E**: Move to the next screen in the cycle
- **Sym+F**: Move to the previous screen in the cycle
- **Sym+Q**: Jump directly to Home (any screen)

### Node List Screen
- **Encoder Rotate**: Browse through node list entries
- **Encoder Press**: Expand details for the selected node
- **Tab (Sym+E)**: Page down by 5 nodes at a time
- **Green Border**: Indicates the currently focused node

### Map Screen
- **Arrow Navigation**: Arrow buttons (←, ↑, ↓, →) control map panning
  - Access via encoder Tab navigation to focus arrow buttons
  - Use Sym+A (Left), Sym+W (Up), Sym+S (Down), Sym+D (Right)
- **Zoom Controls**: Focusable zoom in/out buttons
  - Accessible through standard navigation (Tab, Encoder)
- **Encoder Rotate**: Scroll between different layers or overlays on the map

### Settings Screen
- **Encoder Rotate**: Scroll through available settings
- **Encoder Press**: Open the currently focused setting for editing
- **Backspace**: Exit the setting without saving changes
- **Auto-Scroll**: Encoder automatically scrolls to keep the focused setting visible in the viewport

### Chat & Messages Screen
- **Encoder Rotate**: Select which chat conversation to view
- **Encoder Press**: Open the selected chat
- **Keyboard Input**: Type messages using the keyboard (letters, numbers, symbols)
- **Enter (Sym+T)**: Send the composed message
- **Backspace**: Delete text or exit compose mode

## Text Input Mode

When in a text field or compose box:

- **Regular keys** (A-Z, 0-9): Insert the corresponding character
- **Shift+key**: Produce uppercase letters or alternate character
- **Sym+key**: Produce symbols (1, 2, 3, *, /, +, 5, -, etc.)
- **Backspace**: Delete the character to the left of the cursor
- **Enter (Sym+T)**: Send message or confirm input (context-dependent)
- **Navigation keys do NOT function in text input mode** — only character input is processed

## Keyboard Layout Toggle

The T-Pager supports both English (Latin) and Russian (Cyrillic) keyboard layouts.

- **Toggle Layout**: Hold Sym and press Shift simultaneously
- **Layout Change**: After the chord is released, the keyboard layout switches
  - EN mode: QWERTY with standard Latin characters and symbols
  - RU mode: Phonetic Cyrillic mapping (Q→Й, W→Ц, etc.)
- **Indicator**: ILOG messages indicate layout changes (when debug logging is enabled)
- **Cooldown**: Layout changes have a 700ms debounce to prevent accidental re-triggers

## Accessibility

- **No Touchscreen**: All navigation is keyboard and encoder-based
- **Accelerated Scrolling**: Faster encoder rotation provides faster navigation through long lists
- **Consistent Navigation Model**: LVGL key codes ensure consistent behavior across all screens
- **Modifier Awareness**: The device is context-aware — Sym+key behaves differently in text input vs. navigation mode

## Tips & Tricks

1. **Quick Navigation**: Use Sym+Q to instantly return to the home screen from any location
2. **Screen Cycling**: Sym+E/F lets you quickly browse between screens without memorizing each screen's navigation layout
3. **Encoder Acceleration**: Spin the encoder faster to scroll through large lists more quickly
4. **Text Editing**: Once in a text field, use Backspace to delete characters — navigation keys are not available
5. **Map Interaction**: Use the encoder to scroll zoom/pan controls into focus, then press to activate
6. **Layout Persistence**: The keyboard layout (EN/RU) persists until toggled again — your language preference carries across all screens

---

**Device**: T-Lora Pager
**Display Resolution**: 480×222
**Keyboard Controller**: TCA8418
**Last Updated**: 2026-03-16
