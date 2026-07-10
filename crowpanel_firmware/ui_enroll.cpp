#include "ui_enroll.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"

lv_obj_t *scr_enroll = NULL;
static lv_obj_t *lbl_enroll_title  = NULL;
static lv_obj_t *lbl_enroll_msg    = NULL;
static lv_obj_t *lbl_enroll_step   = NULL;
static lv_obj_t *bar_enroll        = NULL;

lv_obj_t *scr_emp_list = NULL;
static lv_obj_t *emp_list_obj = NULL;
static lv_obj_t *ta_search = NULL;
static lv_obj_t *kb_search = NULL;

extern void uiShowIdle();
extern lv_timer_t *returnTimer;

static void btn_back_cb(lv_event_t * e) {
  uiShowIdle();
}

static void btn_emp_click_cb(lv_event_t * e) {
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  char buf[32];
  snprintf(buf, sizeof(buf), "ENROLL:%d", id);
  CommManager::sendCommand(String(buf));
}

static void populate_emp_list(const char* filter) {
  lv_obj_clean(emp_list_obj);
  
  const Employee* db = DataManager::getEmployees();
  int count = DataManager::getEmployeeCount();
  
  String fStr = filter ? String(filter) : "";
  fStr.toLowerCase();
  
  for (int i = 0; i < count; i++) {
    String nStr = db[i].name;
    nStr.toLowerCase();
    
    if (fStr.length() > 0 && nStr.indexOf(fStr) == -1) {
        continue; // Skip if it doesn't match filter
    }
    
    char buf[128];
    snprintf(buf, sizeof(buf), "ID: %d   |   %s   (%s)", db[i].id, db[i].name.c_str(), db[i].dept.c_str());
    
    lv_obj_t * btn = lv_list_add_btn(emp_list_obj, NULL, buf);
    lv_obj_set_style_bg_color(btn, UIManager::rgb(COLOR_CARD), 0);
    lv_obj_set_style_text_color(btn, UIManager::rgb(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_all(btn, 15, 0);
    
    lv_obj_add_event_cb(btn, btn_emp_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)db[i].id);
  }
}

static void search_ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = (lv_obj_t*)lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        const char * txt = lv_textarea_get_text(ta);
        populate_emp_list(txt);
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb_search, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(emp_list_obj, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_FOCUSED) {
        lv_obj_clear_flag(kb_search, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(emp_list_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void buildEmpListScreen() {
  scr_emp_list = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_emp_list, UIManager::rgb(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr_emp_list, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_emp_list, LV_SCROLLBAR_MODE_OFF);

  // Top title bar
  lv_obj_t *topBar = lv_obj_create(scr_emp_list);
  lv_obj_set_size(topBar, LCD_WIDTH, 56);
  lv_obj_align(topBar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(topBar, UIManager::rgb(COLOR_DIM), 0);
  lv_obj_set_style_radius(topBar, 0, 0);
  lv_obj_set_style_border_width(topBar, 0, 0);
  lv_obj_set_scrollbar_mode(topBar, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *lbl_title = lv_label_create(topBar);
  lv_label_set_text(lbl_title, "SELECT EMPLOYEE TO ENROLL");
  UIManager::styleLabel(lbl_title, COLOR_ACCENT, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_title);

  // Back button
  lv_obj_t * btn_back = lv_btn_create(topBar);
  lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 10, 0);
  lv_obj_set_style_bg_color(btn_back, UIManager::rgb(COLOR_DIM), 0);
  lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t * lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, "< Back");
  UIManager::styleLabel(lbl_back, COLOR_TEXT, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);

  // Search box
  ta_search = lv_textarea_create(scr_emp_list);
  lv_textarea_set_one_line(ta_search, true);
  lv_textarea_set_placeholder_text(ta_search, "Search name...");
  lv_obj_set_width(ta_search, 400);
  lv_obj_align(ta_search, LV_ALIGN_TOP_MID, 0, 70);
  lv_obj_add_event_cb(ta_search, search_ta_event_cb, LV_EVENT_ALL, NULL);

  // List container
  emp_list_obj = lv_list_create(scr_emp_list);
  lv_obj_set_size(emp_list_obj, 600, 310);
  lv_obj_align(emp_list_obj, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_color(emp_list_obj, UIManager::rgb(COLOR_CARD), 0);
  lv_obj_set_style_border_color(emp_list_obj, UIManager::rgb(COLOR_DIM), 0);
  lv_obj_set_style_radius(emp_list_obj, 10, 0);

  // Keyboard
  kb_search = lv_keyboard_create(scr_emp_list);
  lv_keyboard_set_textarea(kb_search, ta_search);
  lv_obj_add_flag(kb_search, LV_OBJ_FLAG_HIDDEN); // Hidden initially

  // Initial population
  populate_emp_list("");
}

void buildEnrollScreen() {
  scr_enroll = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_enroll, UIManager::rgb(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr_enroll, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_enroll, LV_SCROLLBAR_MODE_OFF);

  // Title
  lbl_enroll_title = lv_label_create(scr_enroll);
  lv_label_set_text(lbl_enroll_title, "FINGERPRINT ENROLLMENT");
  UIManager::styleLabel(lbl_enroll_title, COLOR_ACCENT, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_enroll_title, LV_ALIGN_TOP_MID, 0, 40);

  // Employee name
  lbl_enroll_msg = lv_label_create(scr_enroll);
  lv_label_set_text(lbl_enroll_msg, "---");
  UIManager::styleLabel(lbl_enroll_msg, COLOR_TEXT, &lv_font_montserrat_36, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_enroll_msg, LV_ALIGN_CENTER, 0, -40);

  // Step instruction
  lbl_enroll_step = lv_label_create(scr_enroll);
  lv_label_set_text(lbl_enroll_step, "");
  UIManager::styleLabel(lbl_enroll_step, COLOR_SUBTEXT, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
  lv_obj_align_to(lbl_enroll_step, lbl_enroll_msg, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

  // Progress bar
  bar_enroll = lv_bar_create(scr_enroll);
  lv_obj_set_size(bar_enroll, 400, 20);
  lv_obj_align(bar_enroll, LV_ALIGN_BOTTOM_MID, 0, -60);
  lv_obj_set_style_bg_color(bar_enroll, UIManager::rgb(COLOR_DIM), 0);
  lv_obj_set_style_bg_color(bar_enroll, UIManager::rgb(COLOR_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar_enroll, 10, 0);
  lv_obj_set_style_radius(bar_enroll, 10, LV_PART_INDICATOR);
  lv_bar_set_range(bar_enroll, 0, 3);
  lv_bar_set_value(bar_enroll, 0, LV_ANIM_OFF);
}

void uiShowEnrollStart(const char *name) {
  if (returnTimer) { lv_timer_del(returnTimer); returnTimer = NULL; }
  
  lv_obj_set_style_bg_color(scr_enroll, UIManager::rgb(COLOR_BG), 0);
  lv_label_set_text(lbl_enroll_title, "FINGERPRINT ENROLLMENT");
  UIManager::styleLabel(lbl_enroll_title, COLOR_ACCENT, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);

  lv_label_set_text(lbl_enroll_msg, name ? name : "");
  lv_label_set_text(lbl_enroll_step, "Preparing...");
  lv_bar_set_value(bar_enroll, 0, LV_ANIM_OFF);
  lv_scr_load_anim(scr_enroll, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

void uiShowEnrollStep(int step, const char *msg) {
  lv_label_set_text(lbl_enroll_step, msg ? msg : "");
  lv_bar_set_value(bar_enroll, step, LV_ANIM_ON);
}

void uiShowEnrollResult(bool ok, const char *name) {
  if (ok) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Enrolled: %s", name ? name : "");
    lv_label_set_text(lbl_enroll_step, buf);
    lv_bar_set_value(bar_enroll, 3, LV_ANIM_ON);
    
    lv_obj_set_style_bg_color(scr_enroll, UIManager::rgb(COLOR_IN), 0);
    lv_label_set_text(lbl_enroll_title, "SUCCESS!");
    UIManager::styleLabel(lbl_enroll_title, COLOR_TEXT, &lv_font_montserrat_48, LV_TEXT_ALIGN_CENTER);
  } else {
    lv_label_set_text(lbl_enroll_step, "Enrollment failed. Try again.");
    
    lv_obj_set_style_bg_color(scr_enroll, UIManager::rgb(COLOR_DANGER), 0);
    lv_label_set_text(lbl_enroll_title, "FAILED!");
    UIManager::styleLabel(lbl_enroll_title, COLOR_TEXT, &lv_font_montserrat_48, LV_TEXT_ALIGN_CENTER);
  }

  if (returnTimer) lv_timer_del(returnTimer);
  returnTimer = lv_timer_create([](lv_timer_t *t) {
    uiShowIdle();
    returnTimer = NULL;
  }, 3000, NULL);
  lv_timer_set_repeat_count(returnTimer, 1);
}
