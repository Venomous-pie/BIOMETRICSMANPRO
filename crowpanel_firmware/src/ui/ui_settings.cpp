#include "ui_settings.h"
#include "ui_manager.h"

lv_obj_t *scr_settings = NULL;

extern const lv_img_dsc_t icon_clock_network;
extern const lv_img_dsc_t icon_server_device;
extern const lv_img_dsc_t icon_device_info;

extern void uiShowSettingsClock();
extern void uiShowSettingsServer();
extern void uiShowSettingsDanger();

static void btn_back_cb(lv_event_t * e) {
    if (scr_settings) {
        lv_obj_t *to_del = scr_settings;
        scr_settings = NULL;        // clear BEFORE async delete so re-entry rebuilds cleanly
        lv_obj_del_async(to_del);
    }
    UIManager::showMainMenu();
}

static void btn_card_clock_cb(lv_event_t * e) {
    uiShowSettingsClock();
}

static void btn_card_server_cb(lv_event_t * e) {
    uiShowSettingsServer();
}

static void btn_card_danger_cb(lv_event_t * e) {
    uiShowSettingsDanger();
}

void buildSettingsScreen() {
    if (scr_settings != NULL) return;
    
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_scrollbar_mode(scr_settings, LV_SCROLLBAR_MODE_OFF);

    UIManager::buildHeader(scr_settings, "Device Settings", "Settings Hub", btn_back_cb, true);

    // Cards Container (Flex layout)
    lv_obj_t *cards_cont = lv_obj_create(scr_settings);
    lv_obj_set_size(cards_cont, 760, 320);
    lv_obj_align(cards_cont, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(cards_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cards_cont, 0, 0);
    lv_obj_set_style_pad_all(cards_cont, 0, 0);
    lv_obj_set_layout(cards_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cards_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cards_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cards_cont, LV_OBJ_FLAG_SCROLLABLE);

    LV_FONT_DECLARE(lv_font_montserrat_20);
    LV_FONT_DECLARE(lv_font_montserrat_14);

    auto create_card = [](lv_obj_t *parent, const lv_img_dsc_t *icon_img, const char *title, const char *subtitle, lv_event_cb_t cb, bool danger) -> lv_obj_t* {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_size(card, 220, 260);
        lv_obj_set_style_bg_color(card, UIManager::rgb(danger ? 0xffe3e8 : COLOR_GREEN_MAIN), 0);
        lv_obj_set_style_border_color(card, UIManager::rgb(danger ? COLOR_DANGER : COLOR_GREEN_MAIN), 0);
        lv_obj_set_style_border_width(card, danger ? 2 : 0, 0);
        lv_obj_set_style_radius(card, 20, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, NULL);

        if (icon_img) {
            lv_obj_t *img = lv_img_create(card);
            lv_img_set_src(img, icon_img);
            lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 30);
            
            lv_obj_set_style_img_recolor(img, danger ? UIManager::rgb(COLOR_DANGER) : lv_color_white(), 0);
            lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
        }

        lv_obj_t *lbl_title = lv_label_create(card);
        lv_label_set_text(lbl_title, title);
        UIManager::styleLabel(lbl_title, danger ? COLOR_DANGER : 0xFFFFFF, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
        lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 150);

        lv_obj_t *lbl_sub = lv_label_create(card);
        lv_label_set_text(lbl_sub, subtitle);
        UIManager::styleLabel(lbl_sub, danger ? COLOR_DANGER : 0xFFFFFF, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
        lv_obj_align(lbl_sub, LV_ALIGN_TOP_MID, 0, 190);

        return card;
    };

    create_card(cards_cont, &icon_clock_network, "Clock & Network", "Wi-Fi", btn_card_clock_cb, false);
    create_card(cards_cont, &icon_server_device, "Server & Device", "API, Device Token", btn_card_server_cb, false);
    create_card(cards_cont, &icon_device_info, "Device Info", "Status, Factory Reset", btn_card_danger_cb, false);
}

void uiShowSettings() {
    if (scr_settings == NULL) {
        buildSettingsScreen();
    }
    lv_scr_load(scr_settings);
}


