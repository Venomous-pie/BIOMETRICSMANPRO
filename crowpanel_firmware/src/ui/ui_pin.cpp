#include "ui_pin.h"
#include "ui_manager.h"
#include "../core/data_manager.h"
#include "../core/comm_manager.h"
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(lv_font_montserrat_36);

static lv_obj_t *scr_pin = NULL;
static lv_obj_t *lbl_title = NULL;
static lv_obj_t *pin_boxes[4];
static lv_obj_t *lbl_pins[4];
static lv_obj_t *btnm_numpad = NULL;
static lv_obj_t *btn_set_admin_fp = NULL;
static lv_obj_t *btn_del_admin_fp = NULL;

static PINMode current_mode = PIN_MODE_AUTH;
static String input_pin  = "";
static String first_pin  = "";   // stores the first entry during PIN_MODE_SETUP confirmation
static int    setup_step = 0;    // 0 = first entry, 1 = confirm entry
static lv_obj_t *scr_pin_header = NULL; // current header — kept so we can swap the subtitle

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
    input_pin  = "";
    first_pin  = "";
    setup_step = 0;
    if (current_mode == PIN_MODE_AUTH) {
        UIManager::showIdle();
    } else {
        UIManager::showSettings();
    }
}

static int failed_attempts = 0;
static unsigned long lockout_start = 0;
static const int MAX_ATTEMPTS = 5;
static const unsigned long LOCKOUT_DURATION_MS = 60000; // 60 seconds

static lv_timer_t *defer_nav_timer = NULL;
static void deferred_nav_cb(lv_timer_t *t) {
    int target = (int)(intptr_t)t->user_data;
    if (target == 1) UIManager::showMainMenu();
    else if (target == 2) UIManager::showSettings();
    defer_nav_timer = NULL;
}

static void process_pin_submission() {
    if (current_mode == PIN_MODE_AUTH) {
        if (input_pin == DataManager::getAdminPin()) {
            failed_attempts = 0; // Reset on success
            input_pin = "";
            if (defer_nav_timer) lv_timer_del(defer_nav_timer);
            defer_nav_timer = lv_timer_create(deferred_nav_cb, 30, (void*)(intptr_t)1);
            lv_timer_set_repeat_count(defer_nav_timer, 1);
        } else {
            failed_attempts++;
            if (failed_attempts >= MAX_ATTEMPTS) {
                lockout_start = millis();
                UIManager::showToast("Too many attempts! Locked for 60s.", true);
            } else {
                String msg = "Incorrect PIN! (" + String(MAX_ATTEMPTS - failed_attempts) + " left)";
                UIManager::showToast(msg.c_str(), true);
            }
            input_pin = "";
            update_pin_display();
        }
    } else if (current_mode == PIN_MODE_SETUP) {
        if (setup_step == 0) {
            // ── Step 1: user entered their desired new PIN ────────────────────
            // Store it and ask for confirmation.
            first_pin  = input_pin;
            setup_step = 1;
            input_pin  = "";
            update_pin_display();

            // Swap the header subtitle to "Confirm New PIN"
            if (scr_pin_header) { lv_obj_del(scr_pin_header); scr_pin_header = NULL; }
            scr_pin_header = UIManager::buildHeader(
                scr_pin, "Security Settings", "Confirm New PIN", btn_back_cb, false);
            lv_obj_move_to_index(scr_pin_header, 0);

            UIManager::showToast("Re-enter your new PIN to confirm");
        } else {
            // ── Step 2: user confirmed — check match ──────────────────────────
            if (input_pin == first_pin) {
                DataManager::setAdminPin(input_pin);
                UIManager::showToast("PIN Updated Successfully!");
                first_pin  = "";
                setup_step = 0;
                input_pin  = "";
                if (defer_nav_timer) lv_timer_del(defer_nav_timer);
                defer_nav_timer = lv_timer_create(deferred_nav_cb, 30, (void*)(intptr_t)2);
                lv_timer_set_repeat_count(defer_nav_timer, 1);
            } else {
                UIManager::showToast("PINs don\'t match! Try again.", true);
                // Reset — user must start over from step 0
                first_pin  = "";
                setup_step = 0;
                input_pin  = "";
                update_pin_display();

                // Restore the original "Set New Admin PIN" subtitle
                if (scr_pin_header) { lv_obj_del(scr_pin_header); scr_pin_header = NULL; }
                scr_pin_header = UIManager::buildHeader(
                    scr_pin, "Security Settings", "Set New Admin PIN", btn_back_cb, false);
                lv_obj_move_to_index(scr_pin_header, 0);
            }
        }
    }
}

static void btnm_event_cb(lv_event_t * e) {
    if (current_mode == PIN_MODE_AUTH && failed_attempts >= MAX_ATTEMPTS) {
        unsigned long elapsed = millis() - lockout_start;
        if (elapsed < LOCKOUT_DURATION_MS) {
            unsigned long remaining_sec = (LOCKOUT_DURATION_MS - elapsed) / 1000;
            String msg = "Locked out. Try again in " + String(remaining_sec) + "s";
            UIManager::showToast(msg.c_str(), true);
            return; // Ignore keypresses during lockout
        } else {
            failed_attempts = 0; // Lockout expired, reset attempts
        }
    }

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

    // Admin Fingerprint buttons
    btn_set_admin_fp = lv_btn_create(scr_pin);
    lv_obj_set_size(btn_set_admin_fp, 200, 40);
    lv_obj_align(btn_set_admin_fp, LV_ALIGN_TOP_RIGHT, -20, 100);
    lv_obj_set_style_bg_color(btn_set_admin_fp, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_set_admin_fp, 8, 0);
    auto btn_set_admin_cb = [](lv_event_t * e) {
        extern void uiShowChooseFinger(String emp_id, const char *name, const char *dept, bool isFallback);
        uiShowChooseFinger("ADMIN", "Admin", "Admin", false);
    };
    lv_obj_add_event_cb(btn_set_admin_fp, btn_set_admin_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_set_admin = lv_label_create(btn_set_admin_fp);
    lv_label_set_text(lbl_set_admin, "Set Admin FP");
    UIManager::styleLabel(lbl_set_admin, 0xffffff, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_set_admin);
    lv_obj_add_flag(btn_set_admin_fp, LV_OBJ_FLAG_HIDDEN); // Hidden by default

    btn_del_admin_fp = lv_btn_create(scr_pin);
    lv_obj_set_size(btn_del_admin_fp, 200, 40);
    lv_obj_align(btn_del_admin_fp, LV_ALIGN_TOP_RIGHT, -20, 150);
    lv_obj_set_style_bg_color(btn_del_admin_fp, UIManager::rgb(0xffe3e8), 0);
    lv_obj_set_style_border_color(btn_del_admin_fp, UIManager::rgb(COLOR_DANGER), 0);
    lv_obj_set_style_border_width(btn_del_admin_fp, 1, 0);
    lv_obj_set_style_radius(btn_del_admin_fp, 8, 0);
    auto btn_del_admin_cb = [](lv_event_t * e) {
        // Delete all possible admin finger templates (any of fingers 0-9 may have been enrolled)
        CommManager::sendCommand("DELETE_FP:1"); // Always slot 1 on WROOM
        for (int f = 0; f < 10; f++) DataManager::deleteTemplate("ADMIN", f);
        DataManager::updateEmployeeFpEnrolled("ADMIN", false, -1); // clear all bits
        UIManager::showToast("Admin fingerprint deleted.", true);
        if (btn_del_admin_fp) {
            lv_obj_add_state(btn_del_admin_fp, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(btn_del_admin_fp, UIManager::rgb(0xeeeeee), LV_STATE_DISABLED);
            lv_obj_set_style_border_width(btn_del_admin_fp, 0, LV_STATE_DISABLED);
            lv_obj_set_style_text_color(lv_obj_get_child(btn_del_admin_fp, 0), UIManager::rgb(0x999999), LV_STATE_DISABLED);
        }
    };
    lv_obj_add_event_cb(btn_del_admin_fp, btn_del_admin_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_del_admin = lv_label_create(btn_del_admin_fp);
    lv_label_set_text(lbl_del_admin, "Delete Admin FP");
    UIManager::styleLabel(lbl_del_admin, COLOR_DANGER, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_del_admin);
    lv_obj_add_flag(btn_del_admin_fp, LV_OBJ_FLAG_HIDDEN); // Hidden by default
}

void uiShowPinScreen(PINMode mode) {
    if (scr_pin == NULL) {
        buildPinScreen();
    }
    
    current_mode = mode;
    input_pin  = "";
    first_pin  = "";
    setup_step = 0;
    update_pin_display();
    


    if (mode == PIN_MODE_AUTH) {
        if (scr_pin_header) { lv_obj_del(scr_pin_header); scr_pin_header = NULL; }
        scr_pin_header = UIManager::buildHeader(scr_pin, "Admin Authentication", "Enter PIN to Unlock", btn_back_cb, false);
        lv_obj_add_flag(btn_set_admin_fp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btn_del_admin_fp, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (scr_pin_header) { lv_obj_del(scr_pin_header); scr_pin_header = NULL; }
        scr_pin_header = UIManager::buildHeader(scr_pin, "Security Settings", "Set New Admin PIN", btn_back_cb, false);
        lv_obj_clear_flag(btn_set_admin_fp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btn_del_admin_fp, LV_OBJ_FLAG_HIDDEN);
        
        if (!DataManager::adminTemplateExists()) {
            lv_obj_add_state(btn_del_admin_fp, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(btn_del_admin_fp, UIManager::rgb(0xeeeeee), LV_STATE_DISABLED);
            lv_obj_set_style_border_width(btn_del_admin_fp, 0, LV_STATE_DISABLED);
            lv_obj_set_style_text_color(lv_obj_get_child(btn_del_admin_fp, 0), UIManager::rgb(0x999999), LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(btn_del_admin_fp, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(btn_del_admin_fp, UIManager::rgb(0xffe3e8), 0);
            lv_obj_set_style_border_width(btn_del_admin_fp, 1, 0);
            lv_obj_set_style_text_color(lv_obj_get_child(btn_del_admin_fp, 0), UIManager::rgb(COLOR_DANGER), 0);
        }
    }
    
    // Move header to the top of the children list so it renders above other elements
    lv_obj_move_to_index(scr_pin_header, 0);

    lv_scr_load(scr_pin);
}
