#include "ui_main_menu.h"
#include "ui_manager.h"
#include "ui_enroll.h"
#include "ui_logs.h"
#include "data_manager.h"

static lv_obj_t *scr_main_menu = NULL;
static lv_obj_t *lbl_emp_subtitle = NULL;

extern const lv_img_dsc_t icon_employees;
extern const lv_img_dsc_t icon_attendance;
extern const lv_img_dsc_t icon_settings_gear;
extern const lv_img_dsc_t icon_charging;


static void btn_emp_cb(lv_event_t * e) {
    uiShowEmpList();
}

static void btn_att_cb(lv_event_t * e) {
    uiShowLogs();
}

static void btn_set_cb(lv_event_t * e) {
    UIManager::showSettings();
}

static void logo_click_cb(lv_event_t * e) {
    UIManager::showIdle();
}

void buildMainMenuScreen() {
    scr_main_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main_menu, UIManager::rgb(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(scr_main_menu, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr_main_menu, LV_SCROLLBAR_MODE_OFF);

    UIManager::buildHeader(scr_main_menu, "Main Menu", "Choose an option", logo_click_cb, true);

    // Cards Container (Flex layout)
    lv_obj_t *cards_cont = lv_obj_create(scr_main_menu);
    lv_obj_set_size(cards_cont, 760, 320);
    lv_obj_align(cards_cont, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(cards_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cards_cont, 0, 0);
    lv_obj_set_style_pad_all(cards_cont, 0, 0);
    lv_obj_set_layout(cards_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cards_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cards_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cards_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Helper lambda to create a card
    auto create_card = [](lv_obj_t *parent, const lv_img_dsc_t *icon, const char *title, const char *subtitle, lv_event_cb_t cb) -> lv_obj_t* {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_size(card, 220, 260);
        lv_obj_set_style_bg_color(card, UIManager::rgb(COLOR_GREEN_MAIN), 0);
        lv_obj_set_style_radius(card, 20, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, NULL);

        // Icon — white recolor
        lv_obj_t *img = lv_img_create(card);
        lv_img_set_src(img, icon);
        lv_obj_set_style_img_recolor(img, lv_color_white(), 0);
        lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 15);

        lv_obj_t *lbl_title = lv_label_create(card);
        lv_label_set_text(lbl_title, title);
        UIManager::styleLabel(lbl_title, 0xFFFFFF, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
        lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 155);

        lv_obj_t *lbl_sub = lv_label_create(card);
        lv_label_set_text(lbl_sub, subtitle);
        UIManager::styleLabel(lbl_sub, 0xFFFFFF, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
        lv_obj_align(lbl_sub, LV_ALIGN_TOP_MID, 0, 195);

        return card;
    };

    lv_obj_t *emp_card = create_card(cards_cont, &icon_employees, "EMPLOYEES", "", btn_emp_cb);
    lbl_emp_subtitle = lv_obj_get_child(emp_card, 2);
    create_card(cards_cont, &icon_attendance, "ATTENDANCE LOGS", "Search logs", btn_att_cb);
    create_card(cards_cont, &icon_settings_gear, "DEVICE SETTINGS", "Wi-Fi, server, and clock", btn_set_cb);
}

void uiShowMainMenu() {
    lv_scr_load_anim(scr_main_menu, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    // Refresh WiFi status to show current connection state
    UIManager::updateHeaderWifi(DataManager::isWifiConnected());

    if (lbl_emp_subtitle) {
        String subtitle = String(DataManager::getEmployeeCount()) + " employees";
        lv_label_set_text(lbl_emp_subtitle, subtitle.c_str());
    }

    // Safely delete heavy screens async when returning to main menu to keep RAM free globally
    extern lv_obj_t *scr_emp_list;
    if (scr_emp_list != NULL) {
        extern lv_obj_t *emp_list_obj;
        extern lv_obj_t *ta_search;
        extern lv_obj_t *kb_search;
        extern lv_timer_t *search_debounce_timer;
        
        if (search_debounce_timer) {
            lv_timer_del(search_debounce_timer);
            search_debounce_timer = NULL;
        }
        lv_obj_del_async(scr_emp_list);
        scr_emp_list = NULL;
        emp_list_obj = NULL;
        ta_search = NULL;
        kb_search = NULL;
    }

    extern lv_obj_t *scr_logs;
    if (scr_logs != NULL) {
        extern lv_obj_t *logs_list_obj;
        extern lv_obj_t *ta_search_name;
        extern lv_obj_t *ta_search_date;
        extern lv_obj_t *kb_logs;
        extern lv_timer_t *logs_search_debounce_timer;
        
        if (logs_search_debounce_timer) {
            lv_timer_del(logs_search_debounce_timer);
            logs_search_debounce_timer = NULL;
        }
        lv_obj_del_async(scr_logs);
        scr_logs = NULL;
        logs_list_obj = NULL;
        ta_search_name = NULL;
        ta_search_date = NULL;
        kb_logs = NULL;
    }
}
