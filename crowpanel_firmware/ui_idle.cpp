#include "ui_idle.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"

static lv_obj_t *scr_idle   = NULL;
static lv_obj_t *lbl_time   = NULL;
static lv_obj_t *lbl_date   = NULL;
static lv_obj_t *lbl_prompt = NULL;
static lv_obj_t *btn_time_in  = NULL;
static lv_obj_t *btn_time_out = NULL;

int pending_action = 0; // 0=none, 1=IN, 2=OUT

extern lv_obj_t *scr_emp_list;
extern lv_timer_t *returnTimer;

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
  lv_obj_set_style_bg_color(scr_idle, UIManager::rgb(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr_idle, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_idle, LV_SCROLLBAR_MODE_OFF);

  // Top title bar
  lv_obj_t *topBar = lv_obj_create(scr_idle);
  lv_obj_set_size(topBar, LCD_WIDTH, 56);
  lv_obj_align(topBar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(topBar, UIManager::rgb(COLOR_DIM), 0);
  lv_obj_set_style_radius(topBar, 0, 0);
  lv_obj_set_style_border_width(topBar, 0, 0);
  lv_obj_set_scrollbar_mode(topBar, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *lbl_title = lv_label_create(topBar);
  lv_label_set_text(lbl_title, "BIOMETRICS PRO");
  UIManager::styleLabel(lbl_title, COLOR_ACCENT, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_title);

  // Large digital clock
  lbl_time = lv_label_create(scr_idle);
  lv_label_set_text(lbl_time, "--:--:--");
  UIManager::styleLabel(lbl_time, COLOR_TEXT, &lv_font_montserrat_48, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, -60);

  // Date below clock
  lbl_date = lv_label_create(scr_idle);
  lv_label_set_text(lbl_date, "---- -- --");
  UIManager::styleLabel(lbl_date, COLOR_SUBTEXT, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
  lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

  // Divider line
  lv_obj_t *divider = lv_obj_create(scr_idle);
  lv_obj_set_size(divider, 300, 2);
  lv_obj_set_style_bg_color(divider, UIManager::rgb(COLOR_ACCENT), 0);
  lv_obj_set_style_border_width(divider, 0, 0);
  lv_obj_set_style_radius(divider, 1, 0);
  lv_obj_align(divider, LV_ALIGN_CENTER, 0, 30);

  // Prompt text
  lbl_prompt = lv_label_create(scr_idle);
  lv_label_set_text(lbl_prompt, "Select Time IN or Time OUT");
  UIManager::styleLabel(lbl_prompt, COLOR_SUBTEXT, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_prompt, LV_ALIGN_CENTER, 0, 70);

  // Pulse animation on prompt
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, lbl_prompt);
  lv_anim_set_values(&a, LV_OPA_50, LV_OPA_COVER);
  lv_anim_set_time(&a, 1400);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_repeat_delay(&a, 200);
  lv_anim_set_playback_time(&a, 1400);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t val) {
    lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)val, 0);
  });
  lv_anim_start(&a);

  // Action Buttons
  btn_time_in = lv_btn_create(scr_idle);
  lv_obj_set_size(btn_time_in, 180, 60);
  lv_obj_align(btn_time_in, LV_ALIGN_CENTER, -110, 140);
  lv_obj_set_style_bg_color(btn_time_in, UIManager::rgb(COLOR_DIM), 0);
  lv_obj_set_style_radius(btn_time_in, 10, 0);
  lv_obj_add_event_cb(btn_time_in, btn_time_in_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_in = lv_label_create(btn_time_in);
  lv_label_set_text(lbl_in, "TIME IN");
  UIManager::styleLabel(lbl_in, COLOR_TEXT, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_in);

  btn_time_out = lv_btn_create(scr_idle);
  lv_obj_set_size(btn_time_out, 180, 60);
  lv_obj_align(btn_time_out, LV_ALIGN_CENTER, 110, 140);
  lv_obj_set_style_bg_color(btn_time_out, UIManager::rgb(COLOR_DIM), 0);
  lv_obj_set_style_radius(btn_time_out, 10, 0);
  lv_obj_add_event_cb(btn_time_out, btn_time_out_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_out = lv_label_create(btn_time_out);
  lv_label_set_text(lbl_out, "TIME OUT");
  UIManager::styleLabel(lbl_out, COLOR_TEXT, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_out);

  // Factory Reset button (top-left corner)
  lv_obj_t *btn_factory = lv_btn_create(scr_idle);
  lv_obj_set_size(btn_factory, 140, 36);
  lv_obj_align(btn_factory, LV_ALIGN_BOTTOM_LEFT, 20, -20);
  lv_obj_set_style_bg_color(btn_factory, UIManager::rgb(COLOR_DANGER), 0);
  lv_obj_set_style_radius(btn_factory, 6, 0);
  lv_obj_add_event_cb(btn_factory, btn_factory_reset_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_factory = lv_label_create(btn_factory);
  lv_label_set_text(lbl_factory, LV_SYMBOL_TRASH " Factory Reset");
  UIManager::styleLabel(lbl_factory, COLOR_TEXT, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_factory);

  // Enroll button
  lv_obj_t * btn_enroll = lv_btn_create(scr_idle);
  lv_obj_align(btn_enroll, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
  lv_obj_set_style_bg_color(btn_enroll, UIManager::rgb(COLOR_DIM), 0);
  lv_obj_add_event_cb(btn_enroll, btn_enroll_main_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t * lbl_btn = lv_label_create(btn_enroll);
  lv_label_set_text(lbl_btn, "Enroll");
  UIManager::styleLabel(lbl_btn, COLOR_TEXT, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
}

void uiShowIdle() {
  if (returnTimer) { lv_timer_del(returnTimer); returnTimer = NULL; }
  pending_action = 0;
  lv_obj_set_style_text_color(lbl_prompt, UIManager::rgb(COLOR_SUBTEXT), 0);
  lv_label_set_text(lbl_prompt, "Select Time IN or Time OUT");
  lv_obj_set_style_bg_color(btn_time_in, UIManager::rgb(COLOR_DIM), 0);
  lv_obj_set_style_bg_color(btn_time_out, UIManager::rgb(COLOR_DIM), 0);
  lv_scr_load_anim(scr_idle, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
  // Signal WROOM that device is activated — enable fingerprint scanning
  CommManager::sendCommand("{\"cmd\":\"DEVICE_ACTIVATED\"}");
}

void uiUpdateClock(const char *ts) {
  if (strlen(ts) >= 19) {
    char timePart[9]; strncpy(timePart, ts + 11, 8); timePart[8] = 0;
    char datePart[11]; strncpy(datePart, ts, 10); datePart[10] = 0;
    lv_label_set_text(lbl_time, timePart);
    lv_label_set_text(lbl_date, datePart);
  }
}

void uiShowPlaceFinger() {
  if (pending_action != 0) {
    lv_label_set_text(lbl_prompt, "Reading fingerprint...");
  }
}
