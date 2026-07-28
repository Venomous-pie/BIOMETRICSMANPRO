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
static lv_obj_t *scr_nomatch = NULL;
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
extern void uiShowEmpList(bool isFallback);

static lv_obj_t *btn_fallback = NULL;

static void fallback_click_cb(lv_event_t *e) {
    if (returnTimer) {
        lv_timer_del(returnTimer);
        returnTimer = NULL;
    }
    // Launch the UI for fallback mode
    uiShowEmpList(true);
}

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

  // Fallback Button (Hidden by default)
  btn_fallback = lv_btn_create(scr_result);
  lv_obj_set_size(btn_fallback, 260, 50);
  lv_obj_align(btn_fallback, LV_ALIGN_BOTTOM_MID, 0, -60);
  lv_obj_set_style_bg_color(btn_fallback, UIManager::rgb(0x1976D2), 0); // Blue
  lv_obj_set_style_radius(btn_fallback, 8, 0);
  lv_obj_add_flag(btn_fallback, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(btn_fallback, fallback_click_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_fb = lv_label_create(btn_fallback);
  lv_label_set_text(lbl_fb, "Manual Sign-In");
  UIManager::styleLabel(lbl_fb, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_fb);
}

void uiShowMatch(const char *name, const char *dept, const char *action, const char *ts) {
  if (scr_result == NULL) buildResultScreen();  // Lazy build on first use

  if (dept && strcmp(dept, "Admin") == 0) {
    UIManager::showMainMenu();
    return;
  }

  bool isIn = (strcmp(action, "IN") == 0 || strcmp(action, "OT IN") == 0);
  bool isOT = (strcmp(action, "OT IN") == 0 || strcmp(action, "OT OUT") == 0);

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
      bool is_pm = (strstr(timeStart, "PM") != NULL || strstr(timeStart, "pm") != NULL);
      bool is_am = (strstr(timeStart, "AM") != NULL || strstr(timeStart, "am") != NULL);
      
      if (is_pm && h < 12) h += 12;
      if (is_am && h == 12) h = 0;

      const char *ampm = (h >= 12) ? "pm" : "am";
      int h12 = h % 12;
      if (h12 == 0) h12 = 12;
      snprintf(formattedTime, sizeof(formattedTime), "%d:%02d%s", h12, m, ampm);
    } else {
      strncpy(formattedTime, ts, sizeof(formattedTime) - 1);
    }
  }

  const char* act_lbl = "Time in";
  if (strcmp(action, "OUT") == 0) act_lbl = "Time out";
  else if (strcmp(action, "OT IN") == 0) act_lbl = "OT in";
  else if (strcmp(action, "OT OUT") == 0) act_lbl = "OT out";

  char pillText[64];
  snprintf(pillText, sizeof(pillText), "%s %s   •   %s", 
           isIn ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT, 
           act_lbl, 
           formattedTime);
  lv_label_set_text(lbl_action, pillText);

  if (strcmp(action, "IN") == 0) {
    lv_obj_set_style_border_color(badge_action, UIManager::rgb(0x2A800F), 0);
    lv_obj_set_style_bg_color(badge_action, UIManager::rgb(0xE6F4EA), 0); // Light green
    lv_obj_set_style_text_color(lbl_action, UIManager::rgb(0x2A800F), 0);

    if (h < 12) {
      lv_label_set_text(lbl_emp_ts, "Good morning! Have a great shift.");
    } else if (h < 17) {
      lv_label_set_text(lbl_emp_ts, "Good afternoon! Have a great shift.");
    } else {
      lv_label_set_text(lbl_emp_ts, "Good evening! Have a great shift.");
    }
  } else if (strcmp(action, "OUT") == 0) {
    lv_obj_set_style_border_color(badge_action, UIManager::rgb(0xED6C02), 0);
    lv_obj_set_style_bg_color(badge_action, UIManager::rgb(0xFFF4E5), 0); // Light red/orange
    lv_obj_set_style_text_color(lbl_action, UIManager::rgb(0xED6C02), 0);

    lv_label_set_text(lbl_emp_ts, "Great work today! Have a safe trip home.");
  } else if (strcmp(action, "OT IN") == 0) {
    lv_obj_set_style_border_color(badge_action, UIManager::rgb(0x1565C0), 0);
    lv_obj_set_style_bg_color(badge_action, UIManager::rgb(0xE4F2FC), 0); // Light blue
    lv_obj_set_style_text_color(lbl_action, UIManager::rgb(0x1565C0), 0);

    lv_label_set_text(lbl_emp_ts, "Starting overtime! Keep up the great work.");
  } else if (strcmp(action, "OT OUT") == 0) {
    lv_obj_set_style_border_color(badge_action, UIManager::rgb(0xEF6C00), 0);
    lv_obj_set_style_bg_color(badge_action, UIManager::rgb(0xFCF2E4), 0); // Light orange
    lv_obj_set_style_text_color(lbl_action, UIManager::rgb(0xEF6C00), 0);

    lv_label_set_text(lbl_emp_ts, "Overtime complete! Take a well-deserved rest.");
  }

  lv_obj_add_flag(btn_fallback, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(lbl_emp_ts, LV_OBJ_FLAG_HIDDEN);

  lv_scr_load(scr_result);

  if (returnTimer) lv_timer_del(returnTimer);
  returnTimer = lv_timer_create([](lv_timer_t *t) {
    uiShowIdle();
    returnTimer = NULL;
  }, 4000, NULL);
  lv_timer_set_repeat_count(returnTimer, 1);
}

void buildNoMatchScreen() {
  if (scr_nomatch != NULL) return;
  
  scr_nomatch = lv_obj_create(NULL);
  if (!scr_nomatch) return;
  
  lv_obj_set_style_bg_color(scr_nomatch, UIManager::rgb(0xFFFFFF), 0); // White background
  lv_obj_set_style_bg_opa(scr_nomatch, LV_OPA_COVER, 0);
  
  lv_obj_t *lbl_nomatch = lv_label_create(scr_nomatch);
  lv_label_set_text(lbl_nomatch, "NO MATCH");
  UIManager::styleLabel(lbl_nomatch, COLOR_DANGER, &lv_font_montserrat_36, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_nomatch, LV_ALIGN_CENTER, 0, -20);
  lv_obj_t *lbl_nomatch_msg = lv_label_create(scr_nomatch);
  lv_label_set_text(lbl_nomatch_msg, "Please contact an admin if you think this is a mistake.");
  UIManager::styleLabel(lbl_nomatch_msg, COLOR_SUBTEXT, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_nomatch_msg, LV_ALIGN_CENTER, 0, 20);
}

void uiShowNoMatch() {
  Serial.println("[UI_RESULT] uiShowNoMatch called");
  if (scr_nomatch == NULL) buildNoMatchScreen();

  lv_scr_load(scr_nomatch);

  if (returnTimer) lv_timer_del(returnTimer);
  returnTimer = lv_timer_create([](lv_timer_t *t) {
    uiShowIdle();
    returnTimer = NULL;
  }, 3000, NULL);
  lv_timer_set_repeat_count(returnTimer, 1);
}

void uiShowActionDenied(const char *name, uint8_t action_type) {
  Serial.println("[UI_RESULT] uiShowActionDenied called");
  if (scr_result == NULL) buildResultScreen();
  
  lv_obj_set_style_bg_color(scr_result, UIManager::rgb(0xFDEDED), 0); // Light red

  lv_obj_set_style_img_recolor(lbl_avatar, UIManager::rgb(COLOR_DANGER), 0);
  lv_obj_set_style_img_recolor_opa(lbl_avatar, LV_OPA_COVER, 0);
  lv_label_set_text(lbl_emp_name, name ? name : "Unknown");
  lv_label_set_text(lbl_emp_dept, "Action Denied");
  
  lv_label_set_text(lbl_action, LV_SYMBOL_WARNING " DENIED");
  lv_obj_set_style_border_color(badge_action, UIManager::rgb(COLOR_DANGER), 0);
  lv_obj_set_style_bg_color(badge_action, UIManager::rgb(0xFDEDED), 0);
  lv_obj_set_style_text_color(lbl_action, UIManager::rgb(COLOR_DANGER), 0);
  
  if (action_type == 1 || action_type == 3) {
      lv_label_set_text(lbl_emp_ts, "You must Time Out first.");
  } else {
      lv_label_set_text(lbl_emp_ts, "You must Time In first.");
  }

  lv_scr_load(scr_result);

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
