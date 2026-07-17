#include "ui_enroll.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"

LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(lv_font_montserrat_36);
LV_FONT_DECLARE(lv_font_montserrat_48);

lv_obj_t *scr_enroll = NULL;
static lv_obj_t *btn_enroll_back = NULL;
static lv_obj_t *btn_enroll_done = NULL;
static lv_obj_t *lbl_enroll_main_title = NULL;
static lv_obj_t *lbl_enroll_sub_title = NULL;
static lv_obj_t *lbl_enroll_instruction = NULL;
static lv_obj_t *box_scan = NULL;
static lv_obj_t *img_scan_icon = NULL;
static lv_obj_t *lbl_scan_text = NULL;
static lv_obj_t *lbl_scan_subtext = NULL;
static lv_obj_t *dot_1 = NULL;
static lv_obj_t *dot_2 = NULL;
static lv_obj_t *dot_3 = NULL;

// Deferred state for Choose Finger screen (moved up so enroll can use it)
static int defer_emp_id = 0;
static String defer_name = "";
static String defer_dept = "";

extern const lv_img_dsc_t icon_biometrics;
extern const lv_img_dsc_t icon_check;

const char* getFingerName(int index) {
  switch(index) {
    case 0: return "left pinky";
    case 1: return "left ring";
    case 2: return "left middle";
    case 3: return "left index";
    case 4: return "left thumb";
    case 5: return "right thumb";
    case 6: return "right index";
    case 7: return "right middle";
    case 8: return "right ring";
    case 9: return "right pinky";
    default: return "unknown";
  }
}

static void enroll_back_cb(lv_event_t * e) {
  uiShowChooseFinger(defer_emp_id, defer_name.c_str(), defer_dept.c_str());
}

static void enroll_done_cb(lv_event_t * e) {
  uiShowEmpList();
}

lv_obj_t *scr_emp_list = NULL;
lv_obj_t *emp_list_obj = NULL;
lv_obj_t *ta_search = NULL;
lv_obj_t *ta_dept_search = NULL;
lv_obj_t *kb_search = NULL;
lv_timer_t *search_debounce_timer = NULL;  // fires 400 ms after last keystroke

static int current_page = 0;
static lv_obj_t *lbl_page_info = NULL;
static lv_obj_t *btn_prev_page = NULL;
static lv_obj_t *btn_next_page = NULL;

static void populate_emp_list(const char* name_filter, const char* dept_filter);

static void prev_page_cb(lv_event_t * e) {
    if (current_page > 0) {
        current_page--;
        const char *n = ta_search       ? lv_textarea_get_text(ta_search)       : "";
        const char *d = ta_dept_search  ? lv_textarea_get_text(ta_dept_search)  : "";
        populate_emp_list(n, d);
    }
}

static void next_page_cb(lv_event_t * e) {
    current_page++;
    const char *n = ta_search       ? lv_textarea_get_text(ta_search)       : "";
    const char *d = ta_dept_search  ? lv_textarea_get_text(ta_dept_search)  : "";
    populate_emp_list(n, d);
}

lv_obj_t *scr_choose_finger = NULL;
static int selected_emp_id = 0;
static int selected_finger_index = -1;
static lv_obj_t *btn_start_scan = NULL;
static lv_obj_t *lbl_choose_info = NULL;
static lv_obj_t *finger_objs[10];

extern void uiShowIdle();
extern lv_timer_t *returnTimer;
extern const lv_img_dsc_t icon_people;
extern const lv_img_dsc_t icon_people_small;

static void btn_back_cb(lv_event_t * e) {
  UIManager::showMainMenu();
}

static void btn_emp_click_cb(lv_event_t * e) {
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  const Employee* db = DataManager::getEmployees();
  int count = DataManager::getEmployeeCount();
  String name = "", dept = "";
  for (int i=0; i<count; i++) {
    if (db[i].id == id) {
      name = db[i].name;
      dept = db[i].dept;
      break;
    }
  }
  uiShowChooseFinger(id, name.c_str(), dept.c_str());
}

static void populate_emp_list(const char* name_filter, const char* dept_filter) {
  if (!emp_list_obj) return; // Prevent crash if screen creation failed due to OOM
  lv_obj_clean(emp_list_obj);

  const Employee* db = DataManager::getEmployees();
  int count = DataManager::getEmployeeCount();

  String nFilt = name_filter ? String(name_filter) : "";
  String dFilt = dept_filter ? String(dept_filter) : "";
  nFilt.toLowerCase();
  dFilt.toLowerCase();

  int items_per_page = 4;
  int filtered_count = 0;

  for (int i = 0; i < count; i++) {
    String nStr = db[i].name; nStr.toLowerCase();
    String dStr = db[i].dept; dStr.toLowerCase();
    if (nFilt.length() > 0 && nStr.indexOf(nFilt) == -1) continue;
    if (dFilt.length() > 0 && dStr.indexOf(dFilt) == -1) continue;
    filtered_count++;
  }

  int total_pages = (filtered_count + items_per_page - 1) / items_per_page;
  if (total_pages == 0) total_pages = 1;
  if (current_page >= total_pages) current_page = total_pages - 1;

  if (lbl_page_info) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Page %d of %d", current_page + 1, total_pages);
    lv_label_set_text(lbl_page_info, buf);
  }
  if (btn_prev_page) {
    if (current_page == 0) lv_obj_add_state(btn_prev_page, LV_STATE_DISABLED);
    else lv_obj_clear_state(btn_prev_page, LV_STATE_DISABLED);
  }
  if (btn_next_page) {
    if (current_page >= total_pages - 1) lv_obj_add_state(btn_next_page, LV_STATE_DISABLED);
    else lv_obj_clear_state(btn_next_page, LV_STATE_DISABLED);
  }

  int start_idx = current_page * items_per_page;
  int end_idx = start_idx + items_per_page;
  int current_idx = 0;

  for (int i = 0; i < count; i++) {
    String nStr = db[i].name; nStr.toLowerCase();
    String dStr = db[i].dept; dStr.toLowerCase();

    if (nFilt.length() > 0 && nStr.indexOf(nFilt) == -1) continue;
    if (dFilt.length() > 0 && dStr.indexOf(dFilt) == -1) continue;

    if (current_idx >= start_idx && current_idx < end_idx) {
      bool enrolled = db[i].fp_enrolled;

      // Row container
    lv_obj_t *row = lv_obj_create(emp_list_obj);
    if (!row) break; // If OOM, stop creating rows instead of crashing
    lv_obj_set_size(row, lv_pct(100), 52);
    lv_obj_set_style_bg_color(row, UIManager::rgb(0xFFFFFF), 0);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, UIManager::rgb(0xE0E0E0), 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, btn_emp_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)db[i].id);

      // Name
      lv_obj_t *lbl_name = lv_label_create(row);
      lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_CLIP); // Handled by string truncation
      lv_obj_set_width(lbl_name, 170);
      String displayName = db[i].name;
      if (displayName.length() > 15) {
          displayName = displayName.substring(0, 15) + "...";
      }
      lv_label_set_text(lbl_name, displayName.c_str());
      UIManager::styleLabel(lbl_name, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
      lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 20, 0);

      // Job Title
      lv_obj_t *lbl_job = lv_label_create(row);
      lv_label_set_long_mode(lbl_job, LV_LABEL_LONG_DOT);
      lv_obj_set_width(lbl_job, 170);
      lv_label_set_text(lbl_job, db[i].job_title.c_str());
      UIManager::styleLabel(lbl_job, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
      lv_obj_align(lbl_job, LV_ALIGN_LEFT_MID, 200, 0);

      // Branch
      lv_obj_t *lbl_branch = lv_label_create(row);
      lv_label_set_long_mode(lbl_branch, LV_LABEL_LONG_DOT);
      lv_obj_set_width(lbl_branch, 130);
      lv_label_set_text(lbl_branch, db[i].branch.c_str());
      UIManager::styleLabel(lbl_branch, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
      lv_obj_align(lbl_branch, LV_ALIGN_LEFT_MID, 380, 0);

      // Dept
      lv_obj_t *lbl_dept = lv_label_create(row);
      lv_label_set_long_mode(lbl_dept, LV_LABEL_LONG_DOT);
      lv_obj_set_width(lbl_dept, 120);
      lv_label_set_text(lbl_dept, db[i].dept.c_str());
      UIManager::styleLabel(lbl_dept, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
      lv_obj_align(lbl_dept, LV_ALIGN_LEFT_MID, 520, 0);

      // Status badge
    lv_obj_t *badge = lv_obj_create(row);
    lv_obj_set_size(badge, 100, 30);
    lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_radius(badge, 6, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(badge, UIManager::rgb(enrolled ? 0xD4EDDA : 0xE0E0E0), 0);

      lv_obj_t *lbl_status = lv_label_create(badge);
      lv_label_set_text(lbl_status, enrolled ? "Enrolled" : "Unenrolled");
      UIManager::styleLabel(lbl_status, enrolled ? 0x155724 : 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
      lv_obj_center(lbl_status);
    }
    current_idx++;
  }
}

// Debounce timer callback — runs 400 ms after the last keystroke
static void search_debounce_cb(lv_timer_t *t) {
    lv_timer_del(search_debounce_timer);
    search_debounce_timer = NULL;
    current_page = 0;
    const char *n = ta_search       ? lv_textarea_get_text(ta_search)       : "";
    const char *d = ta_dept_search  ? lv_textarea_get_text(ta_dept_search)  : "";
    populate_emp_list(n, d);
}

static void search_ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Debounce: reset 400 ms timer on every keystroke instead of
        // calling populate_emp_list() (which does lv_obj_clean + full
        // widget recreate) on every single character typed.
        if (search_debounce_timer) {
            lv_timer_reset(search_debounce_timer);
        } else {
            search_debounce_timer = lv_timer_create(search_debounce_cb, 400, NULL);
            lv_timer_set_repeat_count(search_debounce_timer, 1);
        }
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        // Dismiss keyboard, restore full list height
        lv_obj_add_flag(kb_search, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(emp_list_obj, 210); // Fits 4 rows perfectly (4*52 + 2 border)
        lv_obj_align(emp_list_obj, LV_ALIGN_TOP_MID, 0, 185);
    } else if (code == LV_EVENT_FOCUSED) {
        // Show keyboard but keep list visible above it (keyboard ~200px tall)
        lv_keyboard_set_textarea(kb_search, (lv_obj_t*)lv_event_get_target(e));
        lv_obj_clear_flag(kb_search, LV_OBJ_FLAG_HIDDEN);
        // Shrink list so it fits above the keyboard
        lv_obj_set_height(emp_list_obj, 140);
        lv_obj_align(emp_list_obj, LV_ALIGN_TOP_MID, 0, 185);
    }
}


void buildEmpListScreen() {
  if (scr_emp_list != NULL) return;  // Already built, skip
  scr_emp_list = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_emp_list, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(scr_emp_list, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_emp_list, LV_SCROLLBAR_MODE_OFF);

  // â”€â”€ Header â”€â”€
  // Back button (green, left)
  lv_obj_t *btn_back = lv_btn_create(scr_emp_list);
  lv_obj_set_size(btn_back, 56, 44);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 20, 18);
  lv_obj_set_style_bg_color(btn_back, UIManager::rgb(COLOR_GREEN_MAIN), 0);
  lv_obj_set_style_radius(btn_back, 10, 0);
  lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
  UIManager::styleLabel(lbl_back, 0xFFFFFF, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_back);

  // Center: "Employees" title
  lv_obj_t *lbl_title = lv_label_create(scr_emp_list);
  lv_label_set_text(lbl_title, "Employees");
  UIManager::styleLabel(lbl_title, COLOR_TEXT_MAIN, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 18);

  // Horizontal separator
  lv_obj_t *sep = lv_obj_create(scr_emp_list);
  lv_obj_set_size(sep, 760, 1);
  lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 76);
  lv_obj_set_style_bg_color(sep, UIManager::rgb(0xE0E0E0), 0);
  lv_obj_set_style_border_width(sep, 0, 0);

  // â”€â”€ Search Row â”€â”€
  ta_search = lv_textarea_create(scr_emp_list);
  lv_textarea_set_one_line(ta_search, true);
  lv_textarea_set_placeholder_text(ta_search, "Search by name");
  lv_obj_set_size(ta_search, 360, 44);
  lv_obj_align(ta_search, LV_ALIGN_TOP_LEFT, 20, 90);
  lv_obj_set_style_radius(ta_search, 8, 0);
  lv_obj_set_style_border_color(ta_search, UIManager::rgb(0xCCCCCC), 0);
  lv_obj_set_style_bg_color(ta_search, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_add_event_cb(ta_search, search_ta_event_cb, LV_EVENT_ALL, NULL);

  ta_dept_search = lv_textarea_create(scr_emp_list);
  lv_textarea_set_one_line(ta_dept_search, true);
  lv_textarea_set_placeholder_text(ta_dept_search, "Search by department");
  lv_obj_set_size(ta_dept_search, 360, 44);
  lv_obj_align(ta_dept_search, LV_ALIGN_TOP_RIGHT, -20, 90);
  lv_obj_set_style_radius(ta_dept_search, 8, 0);
  lv_obj_set_style_border_color(ta_dept_search, UIManager::rgb(0xCCCCCC), 0);
  lv_obj_set_style_bg_color(ta_dept_search, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_add_event_cb(ta_dept_search, search_ta_event_cb, LV_EVENT_ALL, NULL);

  // â”€â”€ Column Headers â”€â”€
  lv_obj_t *col_hdr = lv_obj_create(scr_emp_list);
  lv_obj_set_size(col_hdr, 760, 36);
  lv_obj_align(col_hdr, LV_ALIGN_TOP_MID, 0, 148);
  lv_obj_set_style_bg_color(col_hdr, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_border_width(col_hdr, 0, 0);
  lv_obj_set_style_pad_all(col_hdr, 0, 0);
  lv_obj_clear_flag(col_hdr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *ch_name = lv_label_create(col_hdr);
  lv_label_set_text(ch_name, "Name");
  UIManager::styleLabel(ch_name, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_name, LV_ALIGN_LEFT_MID, 20, 0);

  lv_obj_t *ch_job = lv_label_create(col_hdr);
  lv_label_set_text(ch_job, "Job Title");
  UIManager::styleLabel(ch_job, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_job, LV_ALIGN_LEFT_MID, 200, 0);

  lv_obj_t *ch_branch = lv_label_create(col_hdr);
  lv_label_set_text(ch_branch, "Branch");
  UIManager::styleLabel(ch_branch, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_branch, LV_ALIGN_LEFT_MID, 380, 0);

  lv_obj_t *ch_dept = lv_label_create(col_hdr);
  lv_label_set_text(ch_dept, "Department");
  UIManager::styleLabel(ch_dept, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_dept, LV_ALIGN_LEFT_MID, 520, 0);

  lv_obj_t *ch_status = lv_label_create(col_hdr);
  lv_label_set_text(ch_status, "Status");
  UIManager::styleLabel(ch_status, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
  lv_obj_align(ch_status, LV_ALIGN_RIGHT_MID, -30, 0);

  // ─── Employee List Container ───
  emp_list_obj = lv_obj_create(scr_emp_list);
  lv_obj_set_size(emp_list_obj, 760, 210); // 4 rows perfectly (4*52 + 2 border)
  lv_obj_align(emp_list_obj, LV_ALIGN_TOP_MID, 0, 185);
  lv_obj_set_style_bg_color(emp_list_obj, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_border_color(emp_list_obj, UIManager::rgb(0xE0E0E0), 0);
  lv_obj_set_style_border_width(emp_list_obj, 1, 0);
  lv_obj_set_style_radius(emp_list_obj, 10, 0);
  lv_obj_set_style_pad_all(emp_list_obj, 0, 0);
  lv_obj_set_style_pad_row(emp_list_obj, 0, 0);
  lv_obj_set_flex_flow(emp_list_obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_layout(emp_list_obj, LV_LAYOUT_FLEX);
  lv_obj_set_scrollbar_mode(emp_list_obj, LV_SCROLLBAR_MODE_OFF);

  // Pagination container
  lv_obj_t *pagination_cont = lv_obj_create(scr_emp_list);
  lv_obj_set_size(pagination_cont, 760, 50);
  lv_obj_align(pagination_cont, LV_ALIGN_TOP_MID, 0, 410); // Center in empty bottom gap
  lv_obj_set_style_bg_color(pagination_cont, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_border_width(pagination_cont, 0, 0);
  lv_obj_clear_flag(pagination_cont, LV_OBJ_FLAG_SCROLLABLE);

  btn_prev_page = lv_btn_create(pagination_cont);
  lv_obj_set_size(btn_prev_page, 120, 40);
  lv_obj_align(btn_prev_page, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_prev_page, UIManager::rgb(COLOR_GREEN_MAIN), 0);
  lv_obj_add_event_cb(btn_prev_page, prev_page_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_prev = lv_label_create(btn_prev_page);
  lv_label_set_text(lbl_prev, "Previous");
  lv_obj_center(lbl_prev);

  btn_next_page = lv_btn_create(pagination_cont);
  lv_obj_set_size(btn_next_page, 120, 40);
  lv_obj_align(btn_next_page, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_next_page, UIManager::rgb(COLOR_GREEN_MAIN), 0);
  lv_obj_add_event_cb(btn_next_page, next_page_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_next = lv_label_create(btn_next_page);
  lv_label_set_text(lbl_next, "Next");
  lv_obj_center(lbl_next);

  lbl_page_info = lv_label_create(pagination_cont);
  lv_label_set_text(lbl_page_info, "Page 1 of 1");
  UIManager::styleLabel(lbl_page_info, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_page_info);

  // Keyboard (hidden initially)
  kb_search = lv_keyboard_create(scr_emp_list);
  lv_keyboard_set_textarea(kb_search, ta_search);
  lv_obj_add_flag(kb_search, LV_OBJ_FLAG_HIDDEN);
  // NOTE: do NOT call populate_emp_list() here.
  // Ownership of initial population belongs to uiShowEmpList(), which defers
  // it to after the screen is active so it never runs inside an event callback.
}

void buildEnrollScreen() {
  if (scr_enroll != NULL) return;
  scr_enroll = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_enroll, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(scr_enroll, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_enroll, LV_SCROLLBAR_MODE_OFF);

  // Top Left Back Button
  btn_enroll_back = lv_btn_create(scr_enroll);
  lv_obj_set_size(btn_enroll_back, 56, 44);
  lv_obj_align(btn_enroll_back, LV_ALIGN_TOP_LEFT, 20, 18);
  lv_obj_set_style_bg_color(btn_enroll_back, UIManager::rgb(0x2A800F), 0); // Green
  lv_obj_set_style_radius(btn_enroll_back, 10, 0);
  lv_obj_add_event_cb(btn_enroll_back, enroll_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_back = lv_label_create(btn_enroll_back);
  lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
  UIManager::styleLabel(lbl_back, 0xFFFFFF, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_back);

  // Top Right Done Button (Hidden by default)
  btn_enroll_done = lv_btn_create(scr_enroll);
  lv_obj_set_size(btn_enroll_done, 120, 44);
  lv_obj_align(btn_enroll_done, LV_ALIGN_TOP_RIGHT, -20, 18);
  lv_obj_set_style_bg_color(btn_enroll_done, UIManager::rgb(0x2A800F), 0); // Green
  lv_obj_set_style_radius(btn_enroll_done, 10, 0);
  lv_obj_add_event_cb(btn_enroll_done, enroll_done_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_done = lv_label_create(btn_enroll_done);
  lv_label_set_text(lbl_done, "Done " LV_SYMBOL_RIGHT);
  UIManager::styleLabel(lbl_done, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_done);
  lv_obj_add_flag(btn_enroll_done, LV_OBJ_FLAG_HIDDEN);

  // Top Center Titles
  lbl_enroll_main_title = lv_label_create(scr_enroll);
  lv_label_set_text(lbl_enroll_main_title, "Enroll fingerprint");
  UIManager::styleLabel(lbl_enroll_main_title, 0x1A1A1A, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_enroll_main_title, LV_ALIGN_TOP_MID, 0, 15);

  lbl_enroll_sub_title = lv_label_create(scr_enroll);
  lv_label_set_text(lbl_enroll_sub_title, "• • • Scan finger");
  UIManager::styleLabel(lbl_enroll_sub_title, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_enroll_sub_title, LV_ALIGN_TOP_MID, 0, 45);

  // Instruction Label
  lbl_enroll_instruction = lv_label_create(scr_enroll);
  lv_label_set_text(lbl_enroll_instruction, "");
  UIManager::styleLabel(lbl_enroll_instruction, 0x333333, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_label_set_long_mode(lbl_enroll_instruction, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_enroll_instruction, 700);
  lv_obj_align(lbl_enroll_instruction, LV_ALIGN_TOP_MID, 0, 100);

  // Central Gray Box
  box_scan = lv_obj_create(scr_enroll);
  lv_obj_set_size(box_scan, 300, 200);
  lv_obj_align(box_scan, LV_ALIGN_CENTER, 0, 30);
  lv_obj_set_style_bg_color(box_scan, UIManager::rgb(0xD9D9D9), 0);
  lv_obj_set_style_border_width(box_scan, 0, 0);
  lv_obj_set_style_radius(box_scan, 0, 0);
  lv_obj_clear_flag(box_scan, LV_OBJ_FLAG_SCROLLABLE);

  // Elements inside box
  img_scan_icon = lv_img_create(box_scan);
  lv_img_set_src(img_scan_icon, &icon_biometrics);
  lv_obj_align(img_scan_icon, LV_ALIGN_TOP_MID, 0, 20);

  lbl_scan_text = lv_label_create(box_scan);
  lv_label_set_text(lbl_scan_text, "Scan 1 of 3");
  UIManager::styleLabel(lbl_scan_text, 0x333333, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_scan_text, LV_ALIGN_TOP_MID, 0, 100);

  lbl_scan_subtext = lv_label_create(box_scan);
  lv_label_set_text(lbl_scan_subtext, "");
  UIManager::styleLabel(lbl_scan_subtext, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_scan_subtext, LV_ALIGN_TOP_MID, 0, 125);
  lv_obj_add_flag(lbl_scan_subtext, LV_OBJ_FLAG_HIDDEN);

  // Dots
  dot_1 = lv_obj_create(box_scan);
  lv_obj_set_size(dot_1, 6, 6);
  lv_obj_align(dot_1, LV_ALIGN_BOTTOM_MID, -12, -15);
  lv_obj_set_style_radius(dot_1, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot_1, 0, 0);

  dot_2 = lv_obj_create(box_scan);
  lv_obj_set_size(dot_2, 6, 6);
  lv_obj_align(dot_2, LV_ALIGN_BOTTOM_MID, 0, -15);
  lv_obj_set_style_radius(dot_2, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot_2, 0, 0);

  dot_3 = lv_obj_create(box_scan);
  lv_obj_set_size(dot_3, 6, 6);
  lv_obj_align(dot_3, LV_ALIGN_BOTTOM_MID, 12, -15);
  lv_obj_set_style_radius(dot_3, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot_3, 0, 0);
}

void uiShowEnrollStart(const char *name) {
  if (scr_enroll == NULL) buildEnrollScreen();
  if (returnTimer) { lv_timer_del(returnTimer); returnTimer = NULL; }
  
  lv_obj_clear_flag(btn_enroll_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(btn_enroll_done, LV_OBJ_FLAG_HIDDEN);

  lv_label_set_text(lbl_enroll_sub_title, "• • • Scan finger");
  UIManager::styleLabel(lbl_enroll_sub_title, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

  String inst = "Have ";
  inst += defer_name;
  inst += " from ";
  inst += defer_dept;
  inst += " place their ";
  inst += getFingerName(selected_finger_index);
  inst += " finger on the device sensor";
  lv_label_set_text(lbl_enroll_instruction, inst.c_str());

  lv_img_set_src(img_scan_icon, &icon_biometrics);
  
  lv_label_set_text(lbl_scan_text, "Scan 1 of 3");
  UIManager::styleLabel(lbl_scan_text, 0x333333, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  
  lv_obj_add_flag(lbl_scan_subtext, LV_OBJ_FLAG_HIDDEN);
  
  lv_obj_clear_flag(dot_1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(dot_2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(dot_3, LV_OBJ_FLAG_HIDDEN);

  lv_obj_set_style_bg_color(dot_1, UIManager::rgb(0x00A3FF), 0); // Blue
  lv_obj_set_style_bg_color(dot_2, UIManager::rgb(0x999999), 0); // Gray
  lv_obj_set_style_bg_color(dot_3, UIManager::rgb(0x999999), 0); // Gray

  lv_scr_load_anim(scr_enroll, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

void uiShowEnrollStep(int step, const char *msg) {
  char buf[32];
  snprintf(buf, sizeof(buf), "Scan %d of 3", step);
  lv_label_set_text(lbl_scan_text, buf);

  if (msg && strlen(msg) > 0) {
    lv_label_set_text(lbl_scan_subtext, msg);
    lv_obj_clear_flag(lbl_scan_subtext, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(lbl_scan_subtext, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_set_style_bg_color(dot_1, UIManager::rgb(step >= 1 ? 0x00A3FF : 0x999999), 0);
  lv_obj_set_style_bg_color(dot_2, UIManager::rgb(step >= 2 ? 0x00A3FF : 0x999999), 0);
  lv_obj_set_style_bg_color(dot_3, UIManager::rgb(step >= 3 ? 0x00A3FF : 0x999999), 0);
}

void uiShowEnrollResult(bool ok, const char *name) {
  lv_obj_add_flag(dot_1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(dot_2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(dot_3, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(lbl_scan_subtext, LV_OBJ_FLAG_HIDDEN);

  if (ok) {
    DataManager::updateEmployeeFpEnrolled(selected_emp_id, true);

    lv_obj_add_flag(btn_enroll_back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_enroll_done, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(lbl_enroll_sub_title, "• • • Done");
    UIManager::styleLabel(lbl_enroll_sub_title, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    lv_img_set_src(img_scan_icon, &icon_check);
    
    lv_label_set_text(lbl_scan_text, "Fingerprint Enrolled");
    UIManager::styleLabel(lbl_scan_text, 0x1A1A1A, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    
    String sub = defer_name;
    sub += " - ";
    sub += getFingerName(selected_finger_index);
    lv_label_set_text(lbl_scan_subtext, sub.c_str());

  } else {
    // If you have a failure icon, use it here. For now, keep fallback symbol or just hide it.
    // Assuming we fallback to the wifi icon or hide it since we don't have icon_close image?
    // Actually, we can use the same check icon or hide it. Let's just use icon_biometrics for fail.
    lv_img_set_src(img_scan_icon, &icon_biometrics);
    
    lv_label_set_text(lbl_scan_text, "Enrollment Failed");
    UIManager::styleLabel(lbl_scan_text, 0x1A1A1A, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    
    lv_label_set_text(lbl_scan_subtext, "Please try again.");
    
    if (returnTimer) lv_timer_del(returnTimer);
    returnTimer = lv_timer_create([](lv_timer_t *t) {
      uiShowChooseFinger(defer_emp_id, defer_name.c_str(), defer_dept.c_str());
      returnTimer = NULL;
    }, 3000, NULL);
    lv_timer_set_repeat_count(returnTimer, 1);
  }
}

static void choose_back_cb(lv_event_t * e) {
  uiShowEmpList();
}

static void finger_click_cb(lv_event_t * e) {
  int f_idx = (int)(intptr_t)lv_event_get_user_data(e);
  selected_finger_index = f_idx;
  
  for (int i=0; i<10; i++) {
    lv_obj_set_style_bg_color(finger_objs[i], UIManager::rgb(0xE8F5E9), 0); // Light green
  }
  lv_obj_set_style_bg_color(finger_objs[f_idx], UIManager::rgb(0x2E7D32), 0); // Dark green
  
  if (btn_start_scan) {
    lv_obj_clear_state(btn_start_scan, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(btn_start_scan, UIManager::rgb(0x2E7D32), 0);
  }
}

static void start_scan_cb(lv_event_t * e) {
  if (selected_finger_index < 0 || selected_emp_id <= 0) return;
  char buf[32];
  snprintf(buf, sizeof(buf), "ENROLL:%d:%d", selected_emp_id, selected_finger_index);
  CommManager::sendCommand(String(buf));
  
  String n = "";
  const Employee* db = DataManager::getEmployees();
  int count = DataManager::getEmployeeCount();
  for (int i=0; i<count; i++) {
    if (db[i].id == selected_emp_id) { n = db[i].name; break; }
  }
  uiShowEnrollStart(n.c_str());
}


void buildChooseFingerScreen() {
  if (scr_choose_finger != NULL) return;
  
  scr_choose_finger = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_choose_finger, UIManager::rgb(0xF8FAF9), 0); // Standard light background

  // 1. Standard Header (Handles title, subtitle, back button, and Wifi/Battery pill!)
  UIManager::buildHeader(scr_choose_finger, "Enroll fingerprint", "Select finger", choose_back_cb, true);

  // 2. Start Scan Button
  btn_start_scan = lv_btn_create(scr_choose_finger);
  lv_obj_set_size(btn_start_scan, 140, 44);
  lv_obj_align(btn_start_scan, LV_ALIGN_TOP_RIGHT, -30, 95);
  lv_obj_set_style_bg_color(btn_start_scan, UIManager::rgb(0x2A800F), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_start_scan, UIManager::rgb(0x6B7280), LV_STATE_DISABLED);
  lv_obj_set_style_radius(btn_start_scan, 8, 0);
  lv_obj_add_state(btn_start_scan, LV_STATE_DISABLED);
  lv_obj_add_event_cb(btn_start_scan, start_scan_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_start = lv_label_create(btn_start_scan);
  lv_label_set_text(lbl_start, "Start scan " LV_SYMBOL_RIGHT);
  UIManager::styleLabel(lbl_start, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_start);

  // 3. Dynamic Info Label (who we are enrolling)
  lbl_choose_info = lv_label_create(scr_choose_finger);
  lv_label_set_text(lbl_choose_info, "");
  UIManager::styleLabel(lbl_choose_info, 0x1A1A1A, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(lbl_choose_info, LV_ALIGN_TOP_LEFT, 50, 105);

  // 4. Draw Hands
  lv_obj_t *left_palm = lv_obj_create(scr_choose_finger);
  lv_obj_set_size(left_palm, 210, 100);
  lv_obj_align(left_palm, LV_ALIGN_BOTTOM_LEFT, 70, -40);
  lv_obj_set_style_bg_color(left_palm, UIManager::rgb(0xE4F3E7), 0);
  lv_obj_set_style_radius(left_palm, 16, 0);
  lv_obj_set_style_border_width(left_palm, 0, 0);
  lv_obj_clear_flag(left_palm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl_lp = lv_label_create(left_palm);
  lv_label_set_text(lbl_lp, "Left hand");
  UIManager::styleLabel(lbl_lp, 0x166534, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_lp, LV_ALIGN_BOTTOM_MID, 0, -20);

  lv_obj_t *right_palm = lv_obj_create(scr_choose_finger);
  lv_obj_set_size(right_palm, 210, 100);
  lv_obj_align(right_palm, LV_ALIGN_BOTTOM_RIGHT, -70, -40);
  lv_obj_set_style_bg_color(right_palm, UIManager::rgb(0xE4F3E7), 0);
  lv_obj_set_style_radius(right_palm, 16, 0);
  lv_obj_set_style_border_width(right_palm, 0, 0);
  lv_obj_clear_flag(right_palm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl_rp = lv_label_create(right_palm);
  lv_label_set_text(lbl_rp, "Right hand");
  UIManager::styleLabel(lbl_rp, 0x166534, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_rp, LV_ALIGN_BOTTOM_MID, 0, -20);

  // 5. Fingers
  // Translated +30 X and +80 Y to match the old main_panel offset relative to the full screen.
  int lx[5] = {70, 120, 170, 220, 290};
  int ly[5] = {270, 210, 200, 210, 310};
  int lh[5] = {100, 160, 170, 160, 90};
  const char* l_labels[5] = {"LP", "LR", "LM", "LI", "LT"};

  int rx[5] = {470, 540, 590, 640, 690};
  int ry[5] = {310, 210, 200, 210, 270};
  int rh[5] = {90, 160, 170, 160, 100};
  const char* r_labels[5] = {"RT", "RI", "RM", "RR", "RP"};

  for (int i = 0; i < 10; i++) {
    finger_objs[i] = lv_obj_create(scr_choose_finger);
    lv_obj_set_size(finger_objs[i], 36, i < 5 ? lh[i] : rh[i-5]);
    lv_obj_align(finger_objs[i], LV_ALIGN_TOP_LEFT, i < 5 ? lx[i] : rx[i-5], i < 5 ? ly[i] : ry[i-5]);
    lv_obj_set_style_bg_color(finger_objs[i], UIManager::rgb(0xE4F3E7), 0);
    lv_obj_set_style_radius(finger_objs[i], 18, 0);
    lv_obj_set_style_border_width(finger_objs[i], 0, 0);
    lv_obj_clear_flag(finger_objs[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(finger_objs[i], finger_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    lv_obj_t *l = lv_label_create(finger_objs[i]);
    lv_label_set_text(l, i < 5 ? l_labels[i] : r_labels[i-5]);
    UIManager::styleLabel(l, 0x166534, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 10);
  }
}

// (Moved deferred state for Choose Finger screen to top)

void uiShowChooseFinger(int emp_id, const char *name, const char *dept) {
  defer_emp_id = emp_id;
  defer_name = name;
  defer_dept = dept;

  lv_timer_t *defer_timer = lv_timer_create([](lv_timer_t *t) {
    // 1. Create a tiny temporary screen and make it active.
    // This allows us to safely delete the heavy Employee screen without crashing LVGL.
    lv_obj_t *temp_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(temp_scr, UIManager::rgb(0x1A1A1A), 0);
    lv_scr_load_anim(temp_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    // 2. Synchronously delete the heavy Employee List screen to completely free RAM.
    if (scr_emp_list != NULL) {
      if (search_debounce_timer) {
        lv_timer_del(search_debounce_timer);
        search_debounce_timer = NULL;
      }
      lv_obj_del(scr_emp_list);
      scr_emp_list = NULL;
      emp_list_obj = NULL;
      ta_search = NULL;
      kb_search = NULL;
    }

    // 3. Now that we have plenty of RAM, build the new Choose Finger screen.
    if (scr_choose_finger == NULL) {
      buildChooseFingerScreen();
    }

    selected_emp_id = defer_emp_id;
    selected_finger_index = -1;

    char buf[256];
    snprintf(buf, sizeof(buf), "Enrolling %s from %s -- pick a finger", defer_name.c_str(), defer_dept.c_str());
    lv_label_set_text(lbl_choose_info, buf);

    if (btn_start_scan) {
      lv_obj_add_state(btn_start_scan, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(btn_start_scan, UIManager::rgb(0x2A800F), 0);
    }

    for (int i = 0; i < 10; i++) {
      if (finger_objs[i]) {
        lv_obj_set_style_bg_color(finger_objs[i], UIManager::rgb(0xE4F3E7), 0);
      }
    }

    // 4. Load the new screen and auto-delete the temporary screen.
    lv_scr_load_anim(scr_choose_finger, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
  }, 10, NULL);
  
  lv_timer_set_repeat_count(defer_timer, 1);
}

// Show the employee list screen with a clean, up-to-date state.
// Called on first entry from Main Menu, and when navigating back from Choose Finger.
void uiShowEmpList() {
  if (search_debounce_timer) {
    lv_timer_del(search_debounce_timer);
    search_debounce_timer = NULL;
  }

  lv_timer_t *defer = lv_timer_create([](lv_timer_t *t) {
    // 1. Create a tiny temporary screen and make it active.
    lv_obj_t *temp_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(temp_scr, UIManager::rgb(0x1A1A1A), 0);
    lv_scr_load_anim(temp_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    // 2. Synchronously delete the heavy Choose Finger screen to completely free RAM.
    if (scr_choose_finger != NULL) {
      lv_obj_del(scr_choose_finger);
      scr_choose_finger = NULL;
      btn_start_scan = NULL;
      lbl_choose_info = NULL;
      for (int i = 0; i < 10; i++) finger_objs[i] = NULL;
    }

    // 3. Now that we have plenty of RAM, build the Employee List screen.
    if (scr_emp_list == NULL) {
      buildEmpListScreen();
    }

    if (ta_search)      { lv_obj_clear_state(ta_search, LV_STATE_FOCUSED);      lv_textarea_set_text(ta_search, ""); }
    if (ta_dept_search) { lv_obj_clear_state(ta_dept_search, LV_STATE_FOCUSED); lv_textarea_set_text(ta_dept_search, ""); }

    if (kb_search) lv_obj_add_flag(kb_search, LV_OBJ_FLAG_HIDDEN);
    current_page = 0;
    if (emp_list_obj) {
      lv_obj_set_height(emp_list_obj, 208);
      lv_obj_align(emp_list_obj, LV_ALIGN_TOP_MID, 0, 185);
    }

    // 4. Load the Employee List screen and auto-delete the temporary screen.
    lv_scr_load_anim(scr_emp_list, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);

    // 5. Populate the list in a SECOND deferred timer so it runs AFTER LVGL
    //    has fully committed the screen load (prevents freeze from running
    //    lv_obj_clean + widget creation inside the same callback as lv_scr_load_anim).
    lv_timer_t *pop_timer = lv_timer_create([](lv_timer_t *t) {
      populate_emp_list("", "");
    }, 20, NULL);
    lv_timer_set_repeat_count(pop_timer, 1);
  }, 10, NULL);
  
  lv_timer_set_repeat_count(defer, 1);
}

