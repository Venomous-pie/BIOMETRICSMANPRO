#include "ui_result.h"
#include "ui_manager.h"
#include <cstring>
#include <Arduino.h>

LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(lv_font_montserrat_36);
LV_FONT_DECLARE(lv_font_montserrat_48);

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
  
  // Entire screen has a light green background
  lv_obj_set_style_bg_color(scr_result, UIManager::rgb(0xF8FBF9), 0); // Light green background
  lv_obj_set_style_bg_opa(scr_result, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_result, LV_SCROLLBAR_MODE_OFF);

extern const lv_img_dsc_t icon_user_a;

  // Avatar Icon
  lbl_avatar = lv_img_create(scr_result);
  lv_img_set_src(lbl_avatar, &icon_user_a);
  lv_obj_set_style_img_recolor(lbl_avatar, UIManager::rgb(0x000000), 0);
  lv_obj_set_style_img_recolor_opa(lbl_avatar, LV_OPA_COVER, 0);
  lv_obj_align(lbl_avatar, LV_ALIGN_TOP_MID, 0, 40);

  // "SCAN SUCCESSFUL"
  lv_obj_t *lbl_scan_succ = lv_label_create(scr_result);
  lv_label_set_text(lbl_scan_succ, "SCAN SUCCESSFUL");
  UIManager::styleLabel(lbl_scan_succ, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_scan_succ, LV_ALIGN_TOP_MID, 0, 185);

  // "Synced"
  lv_obj_t *lbl_synced = lv_label_create(scr_result);
  lv_label_set_text(lbl_synced, "Synced");
  UIManager::styleLabel(lbl_synced, 0x999999, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_synced, LV_ALIGN_TOP_MID, 0, 210);

  // Name
  lbl_emp_name = lv_label_create(scr_result);
  lv_label_set_text(lbl_emp_name, "---");
  UIManager::styleLabel(lbl_emp_name, 0x000000, &lv_font_montserrat_36, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_emp_name, LV_ALIGN_TOP_MID, 0, 260);
  lv_obj_set_width(lbl_emp_name, 700);
#if LVGL_VERSION_MAJOR >= 9
  lv_label_set_long_mode(lbl_emp_name, LV_LABEL_LONG_CLIP);
#else
  lv_label_set_long_mode(lbl_emp_name, LV_LABEL_LONG_CLIP);
#endif

  // Department
  lbl_emp_dept = lv_label_create(scr_result);
  lv_label_set_text(lbl_emp_dept, "---");
  UIManager::styleLabel(lbl_emp_dept, 0x666666, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_emp_dept, LV_ALIGN_TOP_MID, 0, 310);

  // IN/OUT badge (Pill)
  badge_action = lv_obj_create(scr_result);
  lv_obj_set_size(badge_action, 340, 50);
  lv_obj_align(badge_action, LV_ALIGN_TOP_MID, 0, 370);
  lv_obj_set_style_radius(badge_action, 8, 0);
  lv_obj_set_style_bg_color(badge_action, UIManager::rgb(0xE6F4EA), 0); // Very light green
  lv_obj_set_style_border_color(badge_action, UIManager::rgb(0x2A800F), 0); // Green border
  lv_obj_set_style_border_width(badge_action, 1, 0);
  lv_obj_clear_flag(badge_action, LV_OBJ_FLAG_SCROLLABLE);

  lbl_action = lv_label_create(badge_action);
  lv_label_set_text(lbl_action, "Time in   •   ---");
  UIManager::styleLabel(lbl_action, 0x2A800F, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_action);

  // Bottom prompt
  lbl_emp_ts = lv_label_create(scr_result); // Reusing ts for bottom message
  lv_label_set_text(lbl_emp_ts, "Good morning! Have a great shift.");
  UIManager::styleLabel(lbl_emp_ts, 0x666666, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_emp_ts, LV_ALIGN_BOTTOM_MID, 0, -20);
}

void uiShowMatch(const char *name, const char *dept, const char *action, const char *ts) {
  if (scr_result == NULL) buildResultScreen();  // Lazy build on first use

  if (dept && strcmp(dept, "Admin") == 0) {
    UIManager::showMainMenu();
    return;
  }

  bool isIn = (pending_action == 0) ? (strcmp(action, "IN") == 0) : (pending_action == 1);
  pending_action = 0; 

  lv_obj_set_style_img_recolor(lbl_avatar, UIManager::rgb(0x000000), 0);
  lv_obj_set_style_img_recolor_opa(lbl_avatar, LV_OPA_COVER, 0);

  lv_label_set_text(lbl_emp_name, name   ? name : "Unknown");
  lv_label_set_text(lbl_emp_dept, dept   ? dept : "");
  
  char formattedTime[32] = "00:00am";
  int h = 0, m = 0; // Declare h and m at the function level scope
  if (ts) {
    const char *timeStart = strchr(ts, ' ');
    if (timeStart) timeStart++; // skip date if present
    else timeStart = ts;

    if (sscanf(timeStart, "%d:%d", &h, &m) >= 2) {
      const char *ampm = (h >= 12) ? "pm" : "am";
      int h12 = h % 12;
      if (h12 == 0) h12 = 12;
      snprintf(formattedTime, sizeof(formattedTime), "%d:%02d%s", h12, m, ampm);
    } else {
      strncpy(formattedTime, ts, sizeof(formattedTime) - 1);
    }
  }

  char pillText[64];
  snprintf(pillText, sizeof(pillText), "%s %s   •   %s", 
           isIn ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT, 
           isIn ? "Time in" : "Time out", 
           formattedTime);
  lv_label_set_text(lbl_action, pillText);

  if (isIn) {
    if (h < 12) {
      lv_label_set_text(lbl_emp_ts, "Good morning! Have a great shift.");
    } else if (h < 17) {
      lv_label_set_text(lbl_emp_ts, "Good afternoon! Have a great shift.");
    } else {
      lv_label_set_text(lbl_emp_ts, "Good evening! Have a great shift.");
    }
  } else {
    lv_label_set_text(lbl_emp_ts, "Great work today! Have a safe trip home.");
  }

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
  lv_obj_set_style_bg_color(scr_result, UIManager::rgb(0xFDEDED), 0); // Light red

  Serial.println("[UI_RESULT] Setting labels...");
  lv_obj_set_style_img_recolor(lbl_avatar, UIManager::rgb(COLOR_DANGER), 0);
  lv_obj_set_style_img_recolor_opa(lbl_avatar, LV_OPA_COVER, 0);
  lv_label_set_text(lbl_emp_name, "Unknown");
  lv_label_set_text(lbl_emp_dept, "Fingerprint not registered");
  
  lv_label_set_text(lbl_action, LV_SYMBOL_WARNING " DENIED");
  lv_obj_set_style_border_color(badge_action, UIManager::rgb(COLOR_DANGER), 0);
  lv_obj_set_style_bg_color(badge_action, UIManager::rgb(0xFDEDED), 0);
  lv_obj_set_style_text_color(lbl_action, UIManager::rgb(COLOR_DANGER), 0);
  lv_label_set_text(lbl_emp_ts, "Please consult HR or Administration.");

  Serial.println("[UI_RESULT] Loading screen...");
  lv_scr_load_anim(scr_result, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
  Serial.println("[UI_RESULT] Screen loaded.");

  if (returnTimer) lv_timer_del(returnTimer);
  returnTimer = lv_timer_create([](lv_timer_t *t) {
    // Reset colors back to normal for next scan
    lv_obj_set_style_bg_color(scr_result, UIManager::rgb(0xF8FBF9), 0);
    lv_obj_set_style_border_color(badge_action, UIManager::rgb(0x2A800F), 0);
    lv_obj_set_style_bg_color(badge_action, UIManager::rgb(0xE6F4EA), 0);
    lv_obj_set_style_text_color(lbl_action, UIManager::rgb(0x2A800F), 0);
    lv_obj_set_style_img_recolor(lbl_avatar, UIManager::rgb(0x000000), 0);
    
    uiShowIdle();
    returnTimer = NULL;
  }, 2500, NULL);
  lv_timer_set_repeat_count(returnTimer, 1);
}
