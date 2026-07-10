#include "ui_activation.h"
#include "ui_manager.h"
#include "data_manager.h"

static lv_obj_t *scr_activation = NULL;
static lv_obj_t *ta_code = NULL;
static lv_obj_t *kb_code = NULL;
static lv_obj_t *lbl_err = NULL;

extern void uiShowIdle();

static void ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = (lv_obj_t*)lv_event_get_target(e);
    
    if (code == LV_EVENT_READY) {
        String input = lv_textarea_get_text(ta);
        if (DataManager::activate(input)) {
            uiShowIdle();
        } else {
            lv_label_set_text(lbl_err, "Invalid Code! Must be 12 uppercase letters.");
        }
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        lv_label_set_text(lbl_err, "");
        // Force uppercase
        const char *txt = lv_textarea_get_text(ta);
        String s = txt;
        s.toUpperCase();
        if (s != txt) {
            lv_textarea_set_text(ta, s.c_str());
        }
    }
}

void buildActivationScreen() {
    scr_activation = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_activation, UIManager::rgb(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr_activation, LV_OPA_COVER, 0);

    lv_obj_t *lbl_title = lv_label_create(scr_activation);
    lv_label_set_text(lbl_title, "DEVICE ACTIVATION");
    UIManager::styleLabel(lbl_title, COLOR_ACCENT, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *lbl_hw = lv_label_create(scr_activation);
    String hwText = "Hardware Code: " + DataManager::getHardwareCode();
    lv_label_set_text(lbl_hw, hwText.c_str());
    UIManager::styleLabel(lbl_hw, COLOR_TEXT, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(lbl_hw, LV_ALIGN_TOP_MID, 0, 70);

    ta_code = lv_textarea_create(scr_activation);
    lv_textarea_set_one_line(ta_code, true);
    lv_textarea_set_max_length(ta_code, 12);
    lv_textarea_set_placeholder_text(ta_code, "ENTER 12-CHAR CODE");
    lv_obj_set_width(ta_code, 300);
    lv_obj_align(ta_code, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_add_event_cb(ta_code, ta_event_cb, LV_EVENT_ALL, NULL);

    lbl_err = lv_label_create(scr_activation);
    lv_label_set_text(lbl_err, "");
    UIManager::styleLabel(lbl_err, COLOR_DANGER, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(lbl_err, LV_ALIGN_TOP_MID, 0, 170);

    kb_code = lv_keyboard_create(scr_activation);
    lv_keyboard_set_textarea(kb_code, ta_code);
    
    // Customize keyboard to only show uppercase
#if LVGL_VERSION_MAJOR >= 9
    // In LVGL 9, mode setting might differ or we just rely on the text area event to capitalize
#else
    lv_keyboard_set_mode(kb_code, LV_KEYBOARD_MODE_TEXT_UPPER);
#endif
}

void uiShowActivation() {
    lv_scr_load(scr_activation);
}
