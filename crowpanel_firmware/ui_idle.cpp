#include "ui_idle.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"

LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_28);

// Custom 144-px Montserrat Bold font
extern const lv_font_t lv_font_montserrat_144;

static lv_obj_t *scr_idle   = NULL;
static lv_obj_t *lbl_time   = NULL;
static lv_obj_t *lbl_ampm   = NULL;
static lv_obj_t *lbl_date   = NULL;
static lv_obj_t *lbl_prompt = NULL;
static lv_obj_t *btn_time_in  = NULL;
static lv_obj_t *btn_time_out = NULL;

extern const lv_img_dsc_t manpro_logo;
extern const lv_img_dsc_t icon_charging;
extern const lv_img_dsc_t icon_arrow_left;
extern const lv_img_dsc_t icon_arrow_right;

int pending_action = 1; // 0=none, 1=IN, 2=OUT
static lv_obj_t *cont_prompt = NULL;
static lv_obj_t *img_arrow_left_obj = NULL;
static lv_obj_t *img_arrow_right_obj = NULL;

extern lv_timer_t *returnTimer;
extern void uiShowEmpList();

static void prompt_click_cb(lv_event_t * e) {
  pending_action++;
  if (pending_action > 2) pending_action = 1;

  if (pending_action == 1) {
    lv_label_set_text(lbl_prompt, " Time - In ");
  } else if (pending_action == 2) {
    lv_label_set_text(lbl_prompt, " Time - Out ");
  }
  lv_obj_clear_flag(img_arrow_left_obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(img_arrow_right_obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(lbl_prompt, UIManager::rgb(COLOR_STROKE), 0);
}

static void btn_time_in_cb(lv_event_t * e) {
  pending_action = 1;
  lv_label_set_text(lbl_prompt, "Ready for TIME IN. Place finger");
  lv_obj_add_flag(img_arrow_left_obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(img_arrow_right_obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(lbl_prompt, UIManager::rgb(COLOR_SUBTEXT), 0);
}

static void btn_time_out_cb(lv_event_t * e) {
  pending_action = 2;
  lv_label_set_text(lbl_prompt, "Ready for TIME OUT. Place finger");
  lv_obj_add_flag(img_arrow_left_obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(img_arrow_right_obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(lbl_prompt, UIManager::rgb(COLOR_SUBTEXT), 0);
}

static void btn_enroll_main_cb(lv_event_t * e) {
  // Always go through uiShowEmpList() — it handles the case where scr_emp_list
  // was deleted (NULL) after an enrollment, rebuilds the screen, and populates
  // the employee list with fresh data. Direct lv_scr_load on scr_emp_list
  // would crash if NULL, or show a blank list if the screen exists but was
  // never re-populated after an enrollment cycle.
  uiShowEmpList();
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
  // Status pill is now managed globally by UIManager on lv_layer_top()

  // ── ManPro Logo ─────────────────────────────────────────
  lv_obj_t *logo = lv_img_create(scr_idle);
  lv_img_set_src(logo, &manpro_logo);
  lv_img_set_zoom(logo, 256); // 100% scale
  lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 10);

  lbl_time = lv_label_create(scr_idle);
  lv_label_set_text(lbl_time, "12:00");
  UIManager::styleLabel(lbl_time, COLOR_STROKE, &lv_font_montserrat_144, LV_TEXT_ALIGN_CENTER);
  // Time is moved closer to the logo
  lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, 15);

  // AM/PM superscript — small font, anchored to the right-bottom of the time
  lbl_ampm = lv_label_create(scr_idle);
  lv_label_set_text(lbl_ampm, "PM");
  UIManager::styleLabel(lbl_ampm, COLOR_STROKE, &lv_font_montserrat_28, LV_TEXT_ALIGN_LEFT);
  lv_obj_align_to(lbl_ampm, lbl_time, LV_ALIGN_OUT_RIGHT_BOTTOM, -5, -22);

  // ── Date label ───────────────────────────────────────────
  lbl_date = lv_label_create(scr_idle);
  lv_label_set_text(lbl_date, "Wednesday    7/1/2026");
  UIManager::styleLabel(lbl_date, COLOR_STROKE, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_date, LV_ALIGN_CENTER, 0, 105);

  // ── Prompt / Bottom Text Container ───────────────────────
  cont_prompt = lv_obj_create(scr_idle);
  lv_obj_set_size(cont_prompt, 400, 60);
  lv_obj_align(cont_prompt, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_set_style_bg_opa(cont_prompt, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont_prompt, 0, 0);
  lv_obj_set_style_pad_all(cont_prompt, 0, 0);
  lv_obj_clear_flag(cont_prompt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(cont_prompt, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cont_prompt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(cont_prompt, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cont_prompt, prompt_click_cb, LV_EVENT_CLICKED, NULL);

  img_arrow_left_obj = lv_img_create(cont_prompt);
  lv_img_set_src(img_arrow_left_obj, &icon_arrow_left);
  lv_obj_set_style_img_recolor(img_arrow_left_obj, UIManager::rgb(COLOR_STROKE), 0);
  lv_obj_set_style_img_recolor_opa(img_arrow_left_obj, LV_OPA_COVER, 0);

  lbl_prompt = lv_label_create(cont_prompt);
  lv_label_set_text(lbl_prompt, " Time - In ");
  UIManager::styleLabel(lbl_prompt, COLOR_STROKE, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);
  
  img_arrow_right_obj = lv_img_create(cont_prompt);
  lv_img_set_src(img_arrow_right_obj, &icon_arrow_right);
  lv_obj_set_style_img_recolor(img_arrow_right_obj, UIManager::rgb(COLOR_STROKE), 0);
  lv_obj_set_style_img_recolor_opa(img_arrow_right_obj, LV_OPA_COVER, 0);
}


bool uiIsIdleScreenActive() {
  return scr_idle != NULL && lv_scr_act() == scr_idle;
}

void uiShowIdle() {
  if (returnTimer) { lv_timer_del(returnTimer); returnTimer = NULL; }
  // Only default to IN if we haven't set it yet, but auto-clock updates will fix it.
  if (pending_action == 1) {
    lv_label_set_text(lbl_prompt, " Time - In ");
  } else {
    lv_label_set_text(lbl_prompt, " Time - Out ");
  }
  lv_obj_clear_flag(img_arrow_left_obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(img_arrow_right_obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(lbl_prompt, UIManager::rgb(COLOR_STROKE), 0);
  lv_scr_load(scr_idle);
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

    // Auto-switch attendance mode on hour change
    static int last_hour = -1;
    if (hour != last_hour) {
        last_hour = hour;
        int expected_action = 1;
        if ((hour >= 12 && hour < 13) || (hour >= 17)) {
            expected_action = 2; // Time Out
        } else {
            expected_action = 1; // Time In
        }
        if (pending_action != expected_action) {
            pending_action = expected_action;
            if (pending_action == 1) {
                if (lbl_prompt) lv_label_set_text(lbl_prompt, " Time - In ");
            } else {
                if (lbl_prompt) lv_label_set_text(lbl_prompt, " Time - Out ");
            }
        }
    }

    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%d:%02d", h12, minute);
    lv_label_set_text(lbl_time, timeStr);
    
    if (lbl_ampm) lv_label_set_text(lbl_ampm, ampm);

    char dateStr[40];
    // Four spaces between day name and date — matches reference design
    snprintf(dateStr, sizeof(dateStr), "%s    %d/%d/%d", days[h], month, day, year);
    lv_label_set_text(lbl_date, dateStr);
  }
}

void uiShowPlaceFinger() {
  if (pending_action != 0) {
    lv_obj_add_flag(img_arrow_left_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(img_arrow_right_obj, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lbl_prompt, "Reading fingerprint...");
  }
}
