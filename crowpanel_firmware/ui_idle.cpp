#include "ui_idle.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"

static lv_obj_t *scr_idle   = NULL;
static lv_obj_t *lbl_time   = NULL;
static lv_obj_t *lbl_ampm   = NULL;
static lv_obj_t *lbl_date   = NULL;
static lv_obj_t *lbl_prompt = NULL;
static lv_obj_t *btn_time_in  = NULL;
static lv_obj_t *btn_time_out = NULL;

extern const lv_img_dsc_t manpro_logo;

int pending_action = 1; // 0=none, 1=IN, 2=OUT

extern lv_obj_t *scr_emp_list;
extern lv_timer_t *returnTimer;

static void prompt_click_cb(lv_event_t * e) {
  pending_action++;
  if (pending_action > 2) pending_action = 1;

  if (pending_action == 1) {
    lv_label_set_text(lbl_prompt, "< Time - In >");
  } else if (pending_action == 2) {
    lv_label_set_text(lbl_prompt, "< Time - Out >");
  }
}

static void scr_idle_click_cb(lv_event_t * e) {
  UIManager::showMainMenu();
}

static void btn_time_in_cb(lv_event_t * e) {
  pending_action = 1;
  lv_obj_set_style_bg_color(btn_time_in, UIManager::rgb(COLOR_IN), 0);
  lv_obj_set_style_bg_color(btn_time_out, UIManager::rgb(COLOR_DIM), 0);
  lv_label_set_text(lbl_prompt, "Ready for TIME IN. Place finger");
  lv_obj_set_style_text_color(lbl_prompt, UIManager::rgb(COLOR_SUBTEXT), 0);
}

static void btn_time_out_cb(lv_event_t * e) {
  pending_action = 2;
  lv_obj_set_style_bg_color(btn_time_in, UIManager::rgb(COLOR_DIM), 0);
  lv_obj_set_style_bg_color(btn_time_out, UIManager::rgb(COLOR_OUT), 0);
  lv_label_set_text(lbl_prompt, "Ready for TIME OUT. Place finger");
  lv_obj_set_style_text_color(lbl_prompt, UIManager::rgb(COLOR_SUBTEXT), 0);
}

static void btn_enroll_main_cb(lv_event_t * e) {
  lv_scr_load_anim(scr_emp_list, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

static void btn_factory_reset_cb(lv_event_t * e) {
  // Tell WROOM to disable fingerprint scanner
  CommManager::sendCommand("{\"cmd\":\"FACTORY_RESET\"}");
  // Clear flags on the CrowPanel side
  DataManager::factoryReset();
}

void buildIdleScreen() {
  scr_idle = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_idle, UIManager::rgb(COLOR_GREEN_DARK), 0);
  lv_obj_set_style_bg_opa(scr_idle, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_idle, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(scr_idle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr_idle, scr_idle_click_cb, LV_EVENT_CLICKED, NULL);

  // Top right pill (Wifi / Battery) - Matched to ui_activation.cpp exactly
  lv_obj_t *status_pill = lv_obj_create(scr_idle);
  lv_obj_set_size(status_pill, 160, 40);
  lv_obj_align(status_pill, LV_ALIGN_TOP_RIGHT, -20, 22);
  lv_obj_set_style_bg_color(status_pill, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
  lv_obj_set_style_radius(status_pill, 20, 0);
  lv_obj_set_style_border_width(status_pill, 0, 0);
  lv_obj_clear_flag(status_pill, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_wifi = lv_label_create(status_pill);
  lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI " Offline");
  UIManager::styleLabel(lbl_wifi, COLOR_GREEN_DARK, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(lbl_wifi, LV_ALIGN_LEFT_MID, 5, 0);

  lv_obj_t *batt_box = lv_obj_create(status_pill);
  lv_obj_set_size(batt_box, 35, 25);
  lv_obj_align(batt_box, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(batt_box, UIManager::rgb(COLOR_GREEN_DARK), 0);
  lv_obj_set_style_radius(batt_box, 4, 0);
  lv_obj_set_style_border_width(batt_box, 0, 0);
  lv_obj_clear_flag(batt_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl_batt = lv_label_create(batt_box);
  lv_label_set_text(lbl_batt, "98");
  UIManager::styleLabel(lbl_batt, 0xFFFFFF, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_batt);

  // Logo
  lv_obj_t *logo = lv_img_create(scr_idle);
  lv_img_set_src(logo, &manpro_logo);
  lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 80);

  // Clock Container
  lv_obj_t *clock_cont = lv_obj_create(scr_idle);
  lv_obj_set_size(clock_cont, 300, 80);
  lv_obj_align(clock_cont, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(clock_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(clock_cont, 0, 0);
  lv_obj_clear_flag(clock_cont, LV_OBJ_FLAG_SCROLLABLE);

  lbl_time = lv_label_create(clock_cont);
  lv_label_set_text(lbl_time, "12:00");
  UIManager::styleLabel(lbl_time, COLOR_STROKE, &lv_font_montserrat_48, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_time, LV_ALIGN_CENTER, -15, 0);

  lbl_ampm = lv_label_create(clock_cont);
  lv_label_set_text(lbl_ampm, "PM");
  UIManager::styleLabel(lbl_ampm, COLOR_STROKE, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align_to(lbl_ampm, lbl_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);

  lbl_date = lv_label_create(scr_idle);
  lv_label_set_text(lbl_date, "Wednesday 7/1/2026");
  UIManager::styleLabel(lbl_date, COLOR_STROKE, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_date, LV_ALIGN_CENTER, 0, 45);

  // Prompt / Bottom Text
  lbl_prompt = lv_label_create(scr_idle);
  lv_label_set_text(lbl_prompt, "< Time - In >");
  UIManager::styleLabel(lbl_prompt, COLOR_STROKE, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_prompt, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_add_flag(lbl_prompt, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(lbl_prompt, prompt_click_cb, LV_EVENT_CLICKED, NULL);
}


void uiShowIdle() {
  if (returnTimer) { lv_timer_del(returnTimer); returnTimer = NULL; }
  pending_action = 1; // Default to IN since we only have < Time - In > right now
  lv_scr_load_anim(scr_idle, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
  // Signal WROOM that device is activated — enable fingerprint scanning
  CommManager::sendCommand("{\"cmd\":\"DEVICE_ACTIVATED\"}");
}

void uiUpdateClock(const char *ts) {
  if (strlen(ts) >= 19) {
    int year = atoi(ts);
    int month = atoi(ts + 5);
    int day = atoi(ts + 8);
    int hour = atoi(ts + 11);
    int minute = atoi(ts + 14);

    int y = year, m = month;
    if (m < 3) { m += 12; y -= 1; }
    int k = y % 100;
    int j = y / 100;
    int h = (day + 13 * (m + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    const char *days[] = {"Saturday", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday"};

    const char *ampm = (hour >= 12) ? "PM" : "AM";
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;

    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%d:%02d", h12, minute);
    lv_label_set_text(lbl_time, timeStr);
    
    if (lbl_ampm) lv_label_set_text(lbl_ampm, ampm);

    char dateStr[32];
    snprintf(dateStr, sizeof(dateStr), "%s %d/%d/%d", days[h], month, day, year);
    lv_label_set_text(lbl_date, dateStr);
  }
}

void uiShowPlaceFinger() {
  if (pending_action != 0) {
    lv_label_set_text(lbl_prompt, "Reading fingerprint...");
  }
}
