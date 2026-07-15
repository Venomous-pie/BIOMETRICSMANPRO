#include "ui_result.h"
#include "ui_manager.h"
#include <cstring>
#include <Arduino.h>

static lv_obj_t *scr_result = NULL;
static lv_obj_t *card_result  = NULL;
static lv_obj_t *lbl_avatar   = NULL;
static lv_obj_t *lbl_emp_name = NULL;
static lv_obj_t *lbl_emp_dept = NULL;
static lv_obj_t *lbl_emp_ts   = NULL;
static lv_obj_t *badge_action = NULL;
static lv_obj_t *lbl_action   = NULL;

lv_timer_t *returnTimer = NULL;
extern int pending_action;
extern void uiShowIdle();

void buildResultScreen() {
  if (scr_result != NULL) return;  // Already built, skip
  Serial.println("[UI_RESULT] Building result screen...");
  
  scr_result = lv_obj_create(NULL);
  if (!scr_result) { Serial.println("[UI_RESULT] FATAL: scr_result is NULL (OOM)"); return; }
  
  lv_obj_set_style_bg_color(scr_result, UIManager::rgb(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr_result, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_result, LV_SCROLLBAR_MODE_OFF);

  Serial.println("[UI_RESULT] Creating card_result...");
  // Employee card
  card_result = lv_obj_create(scr_result);
  if (!card_result) { Serial.println("[UI_RESULT] FATAL: card_result is NULL (OOM)"); return; }
  lv_obj_set_size(card_result, 680, 300);
  lv_obj_align(card_result, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card_result, UIManager::rgb(COLOR_CARD), 0);
  lv_obj_set_style_border_width(card_result, 2, 0);
  lv_obj_set_style_border_color(card_result, UIManager::rgb(COLOR_ACCENT), 0);
  lv_obj_set_style_radius(card_result, 20, 0);
  lv_obj_set_style_pad_all(card_result, 30, 0);
  lv_obj_set_scrollbar_mode(card_result, LV_SCROLLBAR_MODE_OFF);

  // Avatar circle (left side)
  lv_obj_t *avatar_bg = lv_obj_create(card_result);
  lv_obj_set_size(avatar_bg, 120, 120);
  lv_obj_align(avatar_bg, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(avatar_bg, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(avatar_bg, UIManager::rgb(COLOR_ACCENT), 0);
  lv_obj_set_style_border_width(avatar_bg, 0, 0);

  lbl_avatar = lv_label_create(avatar_bg);
  lv_label_set_text(lbl_avatar, "?");
  UIManager::styleLabel(lbl_avatar, COLOR_TEXT, &lv_font_montserrat_48, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_avatar);

  // Name
  lbl_emp_name = lv_label_create(card_result);
  lv_label_set_text(lbl_emp_name, "---");
  UIManager::styleLabel(lbl_emp_name, COLOR_TEXT, &lv_font_montserrat_36, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(lbl_emp_name, LV_ALIGN_TOP_LEFT, 150, 20);
  
#if LVGL_VERSION_MAJOR >= 9
  // In LV 9 long mode constant might differ
  lv_label_set_long_mode(lbl_emp_name, LV_LABEL_LONG_CLIP);
#else
  lv_label_set_long_mode(lbl_emp_name, LV_LABEL_LONG_CLIP);
#endif
  lv_obj_set_width(lbl_emp_name, 350);

  // Department
  lbl_emp_dept = lv_label_create(card_result);
  lv_label_set_text(lbl_emp_dept, "---");
  UIManager::styleLabel(lbl_emp_dept, COLOR_SUBTEXT, &lv_font_montserrat_20, LV_TEXT_ALIGN_LEFT);
  lv_obj_align_to(lbl_emp_dept, lbl_emp_name, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

  // Timestamp
  lbl_emp_ts = lv_label_create(card_result);
  lv_label_set_text(lbl_emp_ts, "");
  UIManager::styleLabel(lbl_emp_ts, COLOR_SUBTEXT, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(lbl_emp_ts, LV_ALIGN_BOTTOM_LEFT, 150, -20);

  // IN/OUT badge
  badge_action = lv_obj_create(card_result);
  lv_obj_set_size(badge_action, 110, 50);
  lv_obj_align(badge_action, LV_ALIGN_BOTTOM_RIGHT, 0, -10);
  lv_obj_set_style_radius(badge_action, 10, 0);
  lv_obj_set_style_border_width(badge_action, 0, 0);

  lbl_action = lv_label_create(badge_action);
  lv_label_set_text(lbl_action, "IN");
  UIManager::styleLabel(lbl_action, COLOR_TEXT, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_action);

  // Bottom prompt
  lv_obj_t *lbl_back = lv_label_create(scr_result);
  lv_label_set_text(lbl_back, "Returning to standby...");
  UIManager::styleLabel(lbl_back, COLOR_SUBTEXT, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_back, LV_ALIGN_BOTTOM_MID, 0, -20);
}

void uiShowMatch(const char *name, const char *dept, const char *action, const char *ts) {
  if (scr_result == NULL) buildResultScreen();  // Lazy build on first use
  bool isIn;
  if (pending_action == 0) {
    isIn = (strcmp(action, "IN") == 0);
  } else {
    isIn = (pending_action == 1);
    pending_action = 0; 
  }

  char initials[3] = {"?"};
  if (name && strlen(name) > 0) {
    initials[0] = name[0];
    const char *sp = strchr(name, ' ');
    if (sp && *(sp+1)) initials[1] = *(sp+1), initials[2] = 0;
    else initials[1] = 0;
  }

  lv_label_set_text(lbl_avatar, initials);
  lv_label_set_text(lbl_emp_name, name   ? name : "Unknown");
  lv_label_set_text(lbl_emp_dept, dept   ? dept : "");
  lv_label_set_text(lbl_emp_ts, ts       ? ts   : "");
  lv_label_set_text(lbl_action, isIn ? "TIME IN" : "TIME OUT");

  uint32_t badgeColor = isIn ? COLOR_IN : COLOR_OUT;
  lv_obj_set_style_bg_color(badge_action, UIManager::rgb(badgeColor), 0);
  lv_obj_set_style_border_color(card_result, UIManager::rgb(badgeColor), 0);
  lv_obj_set_style_border_width(card_result, 3, 0);

  lv_scr_load_anim(scr_result, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

  if (returnTimer) lv_timer_del(returnTimer);
  returnTimer = lv_timer_create([](lv_timer_t *t) {
    uiShowIdle();
    returnTimer = NULL;
  }, 4000, NULL);
  lv_timer_set_repeat_count(returnTimer, 1);
}

void uiShowNoMatch() {
  Serial.println("[UI_RESULT] uiShowNoMatch called");
  if (scr_result == NULL) buildResultScreen();  // Lazy build on first use
  
  Serial.println("[UI_RESULT] Setting danger styles...");
  lv_obj_set_style_bg_color(card_result, UIManager::rgb(COLOR_DANGER), 0);
  lv_obj_set_style_border_color(card_result, UIManager::rgb(COLOR_DANGER), 0);
  
  Serial.println("[UI_RESULT] Setting labels...");
  lv_label_set_text(lbl_avatar, "!");
  lv_label_set_text(lbl_emp_name, "Unknown");
  lv_label_set_text(lbl_emp_dept, "Fingerprint not registered");
  lv_label_set_text(lbl_emp_ts, "");
  lv_label_set_text(lbl_action, "DENIED");
  lv_obj_set_style_bg_color(badge_action, UIManager::rgb(COLOR_DANGER), 0);

  Serial.println("[UI_RESULT] Loading screen...");
  lv_scr_load_anim(scr_result, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
  Serial.println("[UI_RESULT] Screen loaded.");

  if (returnTimer) lv_timer_del(returnTimer);
  returnTimer = lv_timer_create([](lv_timer_t *t) {
    lv_obj_set_style_bg_color(card_result, UIManager::rgb(COLOR_CARD), 0);
    uiShowIdle();
    returnTimer = NULL;
  }, 2500, NULL);
  lv_timer_set_repeat_count(returnTimer, 1);
}
