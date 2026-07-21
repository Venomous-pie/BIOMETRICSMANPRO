#include "ui_settings_display.h"
#include "ui_settings.h"
#include "ui_manager.h"
#include "../core/data_manager.h"
#include "../core/display_driver.h"

extern LGFX lcd;

static lv_obj_t *scr_settings_display = NULL;
static lv_obj_t *slider_brightness;
static lv_obj_t *dd_timeout;
static int original_brightness = 200;

static void close_screen() {
    extern void uiShowSettings();
    if (scr_settings_display) {
        lv_obj_t *to_del = scr_settings_display;
        scr_settings_display = NULL;
        lv_obj_del_async(to_del);
    }
    uiShowSettings();
}

static void btn_cancel_cb(lv_event_t * e) {
    lcd.setBrightness(original_brightness); // Restore original
    close_screen();
}

static void btn_save_cb(lv_event_t * e) {
    int brightness = lv_slider_get_value(slider_brightness);
    DataManager::setBrightness(brightness);

    uint16_t opt = lv_dropdown_get_selected(dd_timeout);
    int timeouts[] = {15, 30, 60, 120, 300, 0};
    DataManager::setScreenTimeout(timeouts[opt]);
    
    UIManager::showToast("Display Settings Saved", false);
    close_screen();
}

static void btn_back_cb(lv_event_t * e) {
    // Treat header back button as cancel
    btn_cancel_cb(e);
}

static void slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_current_target(e);
    int value = lv_slider_get_value(slider);
    lcd.setBrightness(value);
}

void uiShowSettingsDisplay() {
    if (scr_settings_display != NULL) return;
    
    original_brightness = DataManager::getBrightness();

    scr_settings_display = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings_display, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_scrollbar_mode(scr_settings_display, LV_SCROLLBAR_MODE_OFF);

    UIManager::buildHeader(scr_settings_display, "Display Settings", "Brightness & Timeout", btn_back_cb, true);

    LV_FONT_DECLARE(lv_font_montserrat_16);
    LV_FONT_DECLARE(lv_font_montserrat_20);
    
    // Main container to center content
    lv_obj_t *cont = lv_obj_create(scr_settings_display);
    lv_obj_set_size(cont, 700, 240);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    lv_obj_t * lbl_bright = lv_label_create(cont);
    lv_label_set_text(lbl_bright, "Screen Brightness");
    UIManager::styleLabel(lbl_bright, COLOR_TEXT_MAIN, &lv_font_montserrat_20, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_bright, LV_ALIGN_TOP_LEFT, 20, 20);

    slider_brightness = lv_slider_create(cont);
    lv_obj_set_size(slider_brightness, 400, 24);
    lv_obj_align(slider_brightness, LV_ALIGN_TOP_LEFT, 20, 60);
    lv_slider_set_range(slider_brightness, 50, 255); 
    lv_slider_set_value(slider_brightness, original_brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_brightness, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    lv_obj_set_style_bg_color(slider_brightness, UIManager::rgb(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_brightness, UIManager::rgb(COLOR_GREEN_MAIN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_brightness, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_brightness, 4, LV_PART_KNOB); // Larger knob

    lv_obj_t * lbl_timeout = lv_label_create(cont);
    lv_label_set_text(lbl_timeout, "Screen Timeout (Sleep)");
    UIManager::styleLabel(lbl_timeout, COLOR_TEXT_MAIN, &lv_font_montserrat_20, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_timeout, LV_ALIGN_TOP_LEFT, 20, 120);

    dd_timeout = lv_dropdown_create(cont);
    lv_obj_set_width(dd_timeout, 300);
    lv_obj_align(dd_timeout, LV_ALIGN_TOP_LEFT, 20, 160);
    lv_dropdown_set_options(dd_timeout, "15 Seconds\n30 Seconds\n1 Minute\n2 Minutes\n5 Minutes\nNever");
    
    int currentTimeout = DataManager::getScreenTimeout();
    int sel = 1; 
    if (currentTimeout == 15) sel = 0;
    else if (currentTimeout == 30) sel = 1;
    else if (currentTimeout == 60) sel = 2;
    else if (currentTimeout == 120) sel = 3;
    else if (currentTimeout == 300) sel = 4;
    else if (currentTimeout == 0) sel = 5;
    lv_dropdown_set_selected(dd_timeout, sel);

    // Bottom Action Bar
    lv_obj_t *bottom = lv_obj_create(scr_settings_display);
    lv_obj_set_size(bottom, 760, 60);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom, 0, 0);
    lv_obj_set_style_pad_all(bottom, 0, 0);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *btn_cancel = lv_btn_create(bottom);
    lv_obj_set_size(btn_cancel, 370, 40);
    lv_obj_align(btn_cancel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_white(), 0);
    lv_obj_set_style_border_color(btn_cancel, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_radius(btn_cancel, 8, 0);
    lv_obj_add_event_cb(btn_cancel, btn_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    UIManager::styleLabel(lbl_cancel, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_cancel);

    lv_obj_t *btn_save = lv_btn_create(bottom);
    lv_obj_set_size(btn_save, 370, 40);
    lv_obj_align(btn_save, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_save, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_border_width(btn_save, 0, 0);
    lv_obj_set_style_radius(btn_save, 8, 0);
    lv_obj_add_event_cb(btn_save, btn_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Save changes");
    UIManager::styleLabel(lbl_save, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_save);

    lv_scr_load(scr_settings_display);
}
