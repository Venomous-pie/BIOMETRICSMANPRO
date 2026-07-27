#include "ui_pin.h"
#include "ui_manager.h"
#include "../core/data_manager.h"

LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(lv_font_montserrat_36);

static lv_obj_t *scr_pin = NULL;
static lv_obj_t *lbl_title = NULL;
static lv_obj_t *pin_boxes[4];
static lv_obj_t *lbl_pins[4];
static lv_obj_t *btnm_numpad = NULL;

static PINMode current_mode = PIN_MODE_AUTH;
static String input_pin = "";

static void update_pin_display() {
    for (int i = 0; i < 4; i++) {
        if (i < input_pin.length()) {
            lv_label_set_text(lbl_pins[i], "•");
        } else {
            lv_label_set_text(lbl_pins[i], "");
        }
    }
}

static void btn_back_cb(lv_event_t * e) {
    input_pin = "";
    if (current_mode == PIN_MODE_AUTH) {
        UIManager::showIdle();
    } else {
        UIManager::showSettings();
    }
}

static void process_pin_submission() {
    if (current_mode == PIN_MODE_AUTH) {
        if (input_pin == DataManager::getAdminPin()) {
            input_pin = "";
            UIManager::showMainMenu();
        } else {
            UIManager::showToast("Incorrect PIN!", true);
            input_pin = "";
            update_pin_display();
        }
    } else if (current_mode == PIN_MODE_SETUP) {
        DataManager::setAdminPin(input_pin);
        UIManager::showToast("PIN Updated Successfully!");
        input_pin = "";
        UIManager::showSettings();
    }
}

static void btnm_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    uint32_t id = lv_btnmatrix_get_selected_btn(obj);
    const char * txt = lv_btnmatrix_get_btn_text(obj, id);
    if (!txt) return;

    if (strcmp(txt, "DEL") == 0) {
        if (input_pin.length() > 0) {
            input_pin.remove(input_pin.length() - 1);
        }
    } else if (strcmp(txt, "CLR") == 0) {
        input_pin = "";
    } else {
        if (input_pin.length() < 4) {
            input_pin += txt;
        }
    }
    
    update_pin_display();
    
    // Auto-submit when 4 digits are entered
    if (input_pin.length() == 4) {
        process_pin_submission();
    }
}

void buildPinScreen() {
    if (scr_pin != NULL) return;
    
    scr_pin = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_pin, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_scrollbar_mode(scr_pin, LV_SCROLLBAR_MODE_OFF);

    // Header will be rebuilt dynamically, just place a placeholder container
    // Or we can rebuild header on show
    
    // Numpad configuration
    static const char * btnm_map[] = {"1", "2", "3", "\n",
                                      "4", "5", "6", "\n",
                                      "7", "8", "9", "\n",
                                      "CLR", "0", "DEL", ""};
    
    btnm_numpad = lv_btnmatrix_create(scr_pin);
    lv_btnmatrix_set_map(btnm_numpad, btnm_map);
    lv_obj_set_size(btnm_numpad, 340, 280);
    lv_obj_align(btnm_numpad, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_text_font(btnm_numpad, &lv_font_montserrat_28, 0);
    lv_obj_add_event_cb(btnm_numpad, btnm_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_bg_opa(btnm_numpad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnm_numpad, 0, 0);
    
    // Style the buttons
    lv_obj_set_style_bg_color(btnm_numpad, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnm_numpad, UIManager::rgb(COLOR_TEXT_MAIN), LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(btnm_numpad, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(btnm_numpad, 10, LV_PART_ITEMS);

    // PIN Boxes Container
    lv_obj_t * cont_boxes = lv_obj_create(scr_pin);
    lv_obj_set_size(cont_boxes, 300, 70);
    lv_obj_align(cont_boxes, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(cont_boxes, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont_boxes, 0, 0);
    lv_obj_set_layout(cont_boxes, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont_boxes, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_boxes, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont_boxes, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; i++) {
        pin_boxes[i] = lv_obj_create(cont_boxes);
        lv_obj_set_size(pin_boxes[i], 60, 60);
        lv_obj_set_style_radius(pin_boxes[i], 10, 0);
        lv_obj_set_style_bg_color(pin_boxes[i], lv_color_white(), 0);
        lv_obj_set_style_border_color(pin_boxes[i], UIManager::rgb(COLOR_SUBTEXT), 0);
        lv_obj_set_style_border_width(pin_boxes[i], 2, 0);
        lv_obj_clear_flag(pin_boxes[i], LV_OBJ_FLAG_SCROLLABLE);

        lbl_pins[i] = lv_label_create(pin_boxes[i]);
        lv_label_set_text(lbl_pins[i], "");
        lv_obj_set_style_text_font(lbl_pins[i], &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(lbl_pins[i], UIManager::rgb(COLOR_TEXT_MAIN), 0);
        lv_obj_align(lbl_pins[i], LV_ALIGN_CENTER, 0, 0);
    }
}

void uiShowPinScreen(PINMode mode) {
    if (scr_pin == NULL) {
        buildPinScreen();
    }
    
    current_mode = mode;
    input_pin = "";
    update_pin_display();
    
    // Clear old children from screen to rebuild header cleanly
    // But we only want to delete the header, not the numpad or boxes.
    // Instead of deleting, we can just call buildHeader which creates a new header.
    // To prevent memory leaks, we should delete the first child if it's the header.
    if (lv_obj_get_child_cnt(scr_pin) > 2) {
        lv_obj_del(lv_obj_get_child(scr_pin, 0)); // Remove old header
    }

    if (mode == PIN_MODE_AUTH) {
        UIManager::buildHeader(scr_pin, "Admin Authentication", "Enter PIN to Unlock", btn_back_cb, false);
    } else {
        UIManager::buildHeader(scr_pin, "Security Settings", "Set New Admin PIN", btn_back_cb, false);
    }
    
    // Move header to the top of the children list so it doesn't overlap weirdly
    lv_obj_move_to_index(lv_obj_get_child(scr_pin, -1), 0);

    lv_scr_load(scr_pin);
}
