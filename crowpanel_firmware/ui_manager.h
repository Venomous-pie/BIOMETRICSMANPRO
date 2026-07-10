#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <lvgl.h>

// ============================================================
// Display configuration
// ============================================================
#define LCD_WIDTH    800
#define LCD_HEIGHT   480

// ============================================================
// Palette
// ============================================================
#define COLOR_BG            0x0d0d1a  // deep navy background
#define COLOR_CARD          0x1a1a35  // card surface
#define COLOR_ACCENT        0x6c63ff  // purple accent
#define COLOR_IN            0x00d4a3  // teal - Time IN
#define COLOR_OUT           0xff7f50  // coral - Time OUT
#define COLOR_DANGER        0xff4d6d  // red - no match
#define COLOR_TEXT          0xffffff  // white text
#define COLOR_SUBTEXT       0x9999bb  // muted text
#define COLOR_DIM           0x2a2a50  // dim panel

// New Palette Colors from PDF
#define COLOR_GREEN_DARK    0x14261C
#define COLOR_GREEN_MAIN    0x2A800F
#define COLOR_GREEN_LIGHT   0xE4F3E7
#define COLOR_STROKE        0xE6EEE9
#define COLOR_TEXT_MAIN     0x1A1A1A
#define COLOR_WIFI_BG       0xF8FAF9

class UIManager {
public:
    static void begin();
    static lv_color_t rgb(uint32_t c);
    static void styleLabel(lv_obj_t *obj, uint32_t color, const lv_font_t *font, lv_text_align_t align);

    // Common screen management
    static void buildAllScreens();
    static void showIdle();
    static void showActivation();
    static void showWifiSetup();
};

#endif // UI_MANAGER_H
