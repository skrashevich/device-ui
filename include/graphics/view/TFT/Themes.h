#pragma once

#include "lvgl.h"
class Themes
{
  public:
    enum Theme { eDark, eLight, eRed };

    static void initStyles(void);
    static enum Theme get(void);
    static void set(enum Theme th);
    static void recolorButton(lv_obj_t *obj, bool enabled, lv_opa_t opa = 255);
    static void recolorImage(lv_obj_t *obj, bool enabled);
    static void recolorText(lv_obj_t *obj, bool enabled);
    static void recolorTopLabel(lv_obj_t *obj, bool alert);
    static void recolorTableRow(lv_draw_fill_dsc_t *fill_draw_dsc, bool odd);

    // Focus styles for encoder/keyboard navigation (green #67EA94 highlight)
    static lv_style_t *getFocusStyleBtn(void);
    static lv_style_t *getFocusStyleDropdown(void);
    static lv_style_t *getFocusStyleSliderKnob(void);
    static lv_style_t *getFocusStyleTextarea(void);
    static lv_style_t *getFocusStyleSwitch(void);

  private:
    Themes(void) = default;
};