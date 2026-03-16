# Navigation Improvements for Non-Touch Devices

## Summary

5 source files changed, 236 lines added. Comprehensive navigation overhaul for T-Pager (480x222) and other non-touchscreen devices, enabling full rotary encoder and keyboard navigation with visual focus feedback.

## Changes by File

### RotaryEncoderInputDriver.h/cpp

**Header change:**
- Added static member `lastStepTime` for tracking encoder step intervals

**Implementation changes:**
- Encoder acceleration with speed-dependent multiplier
  - Threshold <50ms → 4x multiplier
  - Threshold <100ms → 2x multiplier
  - Threshold >200ms → 1x multiplier (base)
- Updated `task_handler()` to accumulate elapsed time between encoder steps
- Acceleration transparent to upper layers—view code receives multiplied encoder values

### I2CKeyboardInputDriver.cpp

**Sym modifier navigation layer:**
- Added context-aware keyboard navigation in `TLoraPagerKeyboardInputDriver::readKeyboard()`
- Blocks navigation when focused object is a textarea (preserves text input)
- Maps 8 navigation keys through Sym modifier layer:
  - Sym+Q (key '1') → HOME
  - Sym+W (key '2') → UP
  - Sym+E (key '3') → NEXT
  - Sym+A (key '*') → LEFT
  - Sym+S (key '/') → DOWN
  - Sym+D (key '+') → RIGHT
  - Sym+T (key '5') → ENTER
  - Sym+F (key '-') → PREV
- Navigation keys processed at input driver level, transparent to view code

### Themes.cpp

**Focus highlight theme layer:**
- Registered universal focus theme chained on active display theme
- Accent color #67EA94 (colorMesh green) applied across all widget types
- 5 focused style objects initialized in `init_focus_styles_and_theme()`:
  - **Button:** 2px green border with full opacity
  - **Dropdown:** 2px green outline with full opacity
  - **Slider knob:** 3px green outline
  - **Textarea:** 2px green border
  - **Switch:** 2px green outline
- Automatic application via `focus_theme_apply_cb()` for buttons, dropdowns, sliders, textareas, switches
- Theme chaining: new focus theme inherits parent theme's styles, adds focus-specific overrides
- Works universally across VIEW_480x222 and VIEW_320x240 variants

### TFTView_480x222.cpp

**Focus flags in `apply_hotfix()` (21 widgets added):**
- Settings buttons (16): user, role, region, modem preset, channel, wifi, language, timeout, screen lock, brightness, theme, input, alert, backup/restore, reset, reboot
- Settings panel widgets (8): dropdowns (device role, region, modem preset, language, theme, mouse input, reset, backup/restore), sliders (brightness, timeout, frequency slot), switches (screen lock, alert buzzer)
- Map navigation buttons (8): arrow up/down/left/right, zoom in/out, GPS lock, nav button
- All use `LV_OBJ_FLAG_SCROLL_ON_FOCUS` to ensure visibility when focused

**Screen navigation in `ui_events_init()` and `ui_event_GlobalKeyHandler()`:**
- Registered `GlobalKeyHandler` on main_screen for LV_EVENT_KEY
- Screen tab cycling via NEXT/PREV keys cycles through 6 main buttons: home → nodes → groups → messages → map → settings (wraps)
- Disabled during active settings panels (checks `activeSettings != eNone`)
- Uses `lv_event_send()` to trigger button click events for screen transitions

**Node list page-jump in `ui_event_NodeButton()`:**
- NEXT/PREV keys skip 5 nodes forward/backward for fast scrolling in large node lists
- Applies `lv_obj_scroll_to_view()` after focus change to keep focused item visible
- Works only when node list panel is active

**Chat panel focus fix in `setGroupFocus()`:**
- Fixed TODO: now correctly iterates children of chats_panel
- Focuses first button child instead of failing to focus hardcoded index
- Preserves focus behavior across varying chat counts

**Map panel focus:**
- Now focuses nav_button when map_panel gains focus (was previously empty)

**Scroll-to-view in 15 settings handlers:**
- `ui_event_user_button()`, `ui_event_role_button()`, `ui_event_region_button()`
- `ui_event_preset_button()`, `ui_event_wifi_button()`, `ui_event_language_button()`
- `ui_event_channel_button()`, `ui_event_brightness_button()`, `ui_event_theme_button()`
- `ui_event_timeout_button()`, `ui_event_screen_lock_button()`, `ui_event_input_button()`
- `ui_event_alert_button()`, `ui_event_backup_button()`, `ui_event_reset_button()`
- Each applies `lv_obj_scroll_to_view(focused_obj, LV_ANIM_ON)` after focus

**Visual focus feedback in `addNode()` and `addChat()`:**
- Node buttons: green border (2px, #67EA94) on LV_STATE_FOCUSED
- Chat buttons: green border (2px, #67EA94) on LV_STATE_FOCUSED
- Border styling applied only in focused state, no runtime overhead for unfocused items

## Architecture Notes

**Layered design:**
- **Input drivers:** Keyboard and encoder acceleration/mapping (transparent to views)
- **Theme layer:** Focus styles auto-applied via theme callback (no per-widget styling)
- **View layer:** Navigation logic, scroll flags, focus management
- **LVGL built-in:** `LV_OBJ_FLAG_SCROLL_ON_FOCUS` and `lv_obj_scroll_to_view()` handle scroll positioning automatically

**Focus flow:**
1. Input driver processes encoder/keyboard → generates LV_KEY_* events
2. Focused object receives LV_EVENT_KEY
3. Event handlers navigate via `lv_group_focus_next/prev()` or direct `lv_group_focus_obj()`
4. LVGL theme applies focus styles from `Themes.cpp` automatically
5. View code ensures scroll-on-focus flags and scroll-to-view calls for all interactive elements

**Screen navigation design:**
- Global key handler on main_screen catches NEXT/PREV at top level
- Only active when `activeSettings == eNone` (no active sub-panel)
- Prevents accidental screen switching while editing settings
- Wrapping implemented via modulo arithmetic for seamless looping

**Performance considerations:**
- Encoder multiplier computed only on step events (no per-frame overhead)
- Focus styles registered once at init, reused across all widgets
- Theme chaining avoids duplicate callbacks via parent theme check
- Scroll-to-view calls use LVGL's built-in animation (not custom logic)

## Testing Notes

**Encoder acceleration:**
- Rotate slowly: verify 1x multiplier (standard navigation)
- Rotate at medium speed: verify 2x multiplier kicks in <100ms
- Rotate rapidly: verify 4x multiplier kicks in <50ms
- Test encoder wraparound at screen and list boundaries

**Keyboard navigation (Sym+WASD):**
- Verify HOME/NEXT/PREV cycle through main screens correctly
- Test text input: Sym+WASD should NOT navigate when textarea focused
- Test all 8 key mappings respond correctly
- Verify modifiers release properly after navigation key sent

**Focus visibility:**
- Navigate through buttons, dropdowns, sliders, textareas, switches
- Verify green focus border appears on all widget types
- Verify focused widget scrolls into view automatically (especially in settings panels)
- Test focus wrapping at list boundaries
- Verify long node/chat lists page-jump (5-skip) works smoothly

**Screen switching:**
- Verify NEXT/PREV cycle through all 6 main screens
- Verify cycling wraps in both directions
- Verify no screen switching during active settings (`activeSettings != eNone`)
- Test rapid NEXT/PREV presses don't cause focus loss

**Visual consistency:**
- Verify focus border color (#67EA94) consistent across all widget types
- Verify focus outline shows on sliders without interfering with knob drag
- Verify textareas show focus border without affecting text rendering
- Verify dropdown outlines don't clip menu items

**Cross-device testing:**
- Test on VIEW_480x222 (primary target)
- Test on VIEW_320x240 (compact variant)
- Verify focus styles render correctly at different resolutions
- Verify scroll-to-view logic handles small screen heights gracefully
