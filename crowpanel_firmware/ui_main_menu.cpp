#include "ui_main_menu.h"
#include "ui_manager.h"
#include "ui_enroll.h"

static lv_obj_t *scr_main_menu = NULL;

extern const lv_img_dsc_t icon_manpro;
extern const lv_img_dsc_t icon_people;
extern const lv_img_dsc_t icon_schedule;
extern const lv_img_dsc_t icon_settings;
extern const lv_img_dsc_t icon_battery;

extern lv_obj_t *scr_emp_list;

static void btn_emp_cb(lv_event_t * e) {
    // Lazy-build the emp list screen if it hasn't been built yet
    if (scr_emp_list == NULL) buildEmpListScreen();
    lv_scr_load_anim(scr_emp_list, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

static void btn_att_cb(lv_event_t * e) {
    // Placeholder for Attendance Logs screen
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
    lv_obj_align(cards_cont, LV_ALIGN_BOTTOM_MID, 0, -20);
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
        lv_obj_set_size(card, 220, 300);
        lv_obj_set_style_bg_color(card, UIManager::rgb(COLOR_GREEN_MAIN), 0);
        lv_obj_set_style_radius(card, 20, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, NULL);

        // Icon — white recolor, centered in the upper portion of card
        lv_obj_t *img = lv_img_create(card);
        lv_img_set_src(img, icon);
        lv_obj_set_style_img_recolor(img, lv_color_white(), 0);
        lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
        lv_obj_align(img, LV_ALIGN_CENTER, 0, -40);

        lv_obj_t *lbl_title = lv_label_create(card);
        lv_label_set_text(lbl_title, title);
        UIManager::styleLabel(lbl_title, 0xFFFFFF, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
        lv_obj_align(lbl_title, LV_ALIGN_BOTTOM_MID, 0, -60);

        lv_obj_t *lbl_sub = lv_label_create(card);
        lv_label_set_text(lbl_sub, subtitle);
        UIManager::styleLabel(lbl_sub, 0xFFFFFF, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
        lv_obj_align(lbl_sub, LV_ALIGN_BOTTOM_MID, 0, -30);

        return card;
    };

    create_card(cards_cont, &icon_people, "EMPLOYEES", "4 enrolled", btn_emp_cb);
    create_card(cards_cont, &icon_schedule, "ATTENDANCE LOGS", "Search logs", btn_att_cb);
    create_card(cards_cont, &icon_settings, "DEVICE SETTINGS", "Wi-Fi and server", btn_set_cb);
}

void uiShowMainMenu() {
    lv_scr_load_anim(scr_main_menu, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}
