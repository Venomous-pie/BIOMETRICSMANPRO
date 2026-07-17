/*
 * ManPro boot splash — LVGL v8.3 port of the HTML/CSS splash.
 *
 * Sequence (~3.0s total):
 *   0.00s  screen fades in on brand background (#0E1B0A)
 *   0.25s  logo wipes in left->right (clipping container widens)
 *   0.95s  logo settles from a slight over-scale to 100%
 *   1.10s  gold/green underline grows in under the logo
 *   1.40s  "INITIALIZING SYSTEM" + 3 pulsing dots fade in
 *   3.00s  screen is torn down and on_complete() is called
 *
 * Requires in lv_conf.h:
 *   LV_COLOR_DEPTH        16   (must match the RGB565 data in manpro_logo.c)
 *   LV_USE_FLEX           1
 *   LV_FONT_MONTSERRAT_14 1
 */

#include "manpro_splash.h"
#include "manpro_logo.h"

static void (*g_on_complete)(void) = NULL;
static lv_obj_t *g_splash_scr = NULL;

/* ---- generic anim exec callbacks ---- */
static void anim_width_cb(void *var, int32_t v) {
  lv_obj_set_width((lv_obj_t *)var, v);
}
static void anim_zoom_cb(void *var, int32_t v) {
  lv_img_set_zoom((lv_obj_t *)var, (uint16_t)v);
}
static void anim_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static int g_splash_elapsed_ms = 0;
static bool g_system_ready = false;

void manpro_set_ready(void) {
  g_system_ready = true;
}

static void splash_done_timer_cb(lv_timer_t *t) {
  g_splash_elapsed_ms += 100;
  
  // Enforce minimum 3000ms splash time
  if (g_splash_elapsed_ms < 3000) return;

  // After 3000ms, wait for system ready OR a hard timeout (10000ms)
  if (g_system_ready || g_splash_elapsed_ms >= 10000) {
    lv_timer_del(t);

    // CRITICAL: Call the callback FIRST so it can load the new screen.
    // Deleting the active screen before loading a new one causes LVGL to crash!
    if (g_on_complete) g_on_complete();

    if (g_splash_scr) {
      lv_obj_del(g_splash_scr);
      g_splash_scr = NULL;
    }
  }
}

void manpro_show_splash(void (*on_complete)(void)) {
  g_on_complete = on_complete;

  const lv_coord_t logo_w = manpro_logo.header.w;   // 480
  const lv_coord_t logo_h = manpro_logo.header.h;   // 192

  /* ---- screen ---- */
  lv_obj_t *scr = lv_obj_create(NULL);
  g_splash_scr = scr;
  lv_obj_remove_style_all(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0E1B0A), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_scr_load(scr);

  /* ---- clipping container that reveals the logo (CSS clip-path wipe) ---- */
  lv_obj_t *clip_cont = lv_obj_create(scr);
  lv_obj_remove_style_all(clip_cont);
  lv_obj_clear_flag(clip_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(clip_cont, 0, logo_h);              /* starts at width 0 */
  lv_obj_align(clip_cont, LV_ALIGN_CENTER, 0, -30);

  lv_obj_t *img = lv_img_create(clip_cont);
  lv_img_set_src(img, &manpro_logo);
  lv_obj_set_pos(img, 0, 0);                          /* pinned; container reveals it */
  lv_img_set_zoom(img, 264);                          /* start slightly oversized */
  lv_img_set_pivot(img, 0, logo_h / 2);

  lv_anim_t a_wipe;
  lv_anim_init(&a_wipe);
  lv_anim_set_var(&a_wipe, clip_cont);
  lv_anim_set_values(&a_wipe, 0, logo_w);
  lv_anim_set_time(&a_wipe, 700);
  lv_anim_set_delay(&a_wipe, 250);
  lv_anim_set_path_cb(&a_wipe, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a_wipe, anim_width_cb);
  lv_anim_start(&a_wipe);

  lv_anim_t a_zoom;
  lv_anim_init(&a_zoom);
  lv_anim_set_var(&a_zoom, img);
  lv_anim_set_values(&a_zoom, 264, 256);               /* settle to 100% */
  lv_anim_set_time(&a_zoom, 400);
  lv_anim_set_delay(&a_zoom, 950);
  lv_anim_set_path_cb(&a_zoom, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a_zoom, anim_zoom_cb);
  lv_anim_start(&a_zoom);

  /* ---- gold/green underline sweep ---- */
  lv_obj_t *underline = lv_obj_create(scr);
  lv_obj_remove_style_all(underline);
  lv_obj_clear_flag(underline, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(underline, 2, 0);
  lv_obj_set_style_bg_opa(underline, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(underline, lv_color_hex(0x2E7A1E), 0);
  lv_obj_set_style_bg_grad_color(underline, lv_color_hex(0xF0A500), 0);
  lv_obj_set_style_bg_grad_dir(underline, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_size(underline, 0, 3);
  lv_obj_align(underline, LV_ALIGN_CENTER, 0, -30 + logo_h / 2 + 24);

  lv_coord_t underline_target = (lv_coord_t)(logo_w * 0.6f);
  lv_anim_t a_line;
  lv_anim_init(&a_line);
  lv_anim_set_var(&a_line, underline);
  lv_anim_set_values(&a_line, 0, underline_target);
  lv_anim_set_time(&a_line, 450);
  lv_anim_set_delay(&a_line, 1100);
  lv_anim_set_path_cb(&a_line, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a_line, anim_width_cb);
  lv_anim_start(&a_line);

  /* ---- "INITIALIZING SYSTEM" + pulsing dots ---- */
  lv_obj_t *status = lv_obj_create(scr);
  lv_obj_remove_style_all(status);
  lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(status, 8, 0);
  lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -36);
  lv_obj_set_style_opa(status, LV_OPA_TRANSP, 0);

  lv_obj_t *label = lv_label_create(status);
  lv_label_set_text(label, "INITIALIZING SYSTEM");
  lv_obj_set_style_text_color(label, lv_color_hex(0xA9C79B), 0);
  lv_obj_set_style_text_letter_space(label, 2, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);

  for (int i = 0; i < 3; i++) {
    lv_obj_t *dot = lv_obj_create(status);
    lv_obj_remove_style_all(dot);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xF0A500), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    lv_anim_t a_dot;
    lv_anim_init(&a_dot);
    lv_anim_set_var(&a_dot, dot);
    lv_anim_set_values(&a_dot, 80, 255);
    lv_anim_set_time(&a_dot, 500);
    lv_anim_set_playback_time(&a_dot, 500);
    lv_anim_set_repeat_count(&a_dot, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_delay(&a_dot, 1400 + i * 150);
    lv_anim_set_path_cb(&a_dot, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a_dot, anim_opa_cb);
    lv_anim_start(&a_dot);
  }

  lv_anim_t a_status;
  lv_anim_init(&a_status);
  lv_anim_set_var(&a_status, status);
  lv_anim_set_values(&a_status, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_time(&a_status, 400);
  lv_anim_set_delay(&a_status, 1400);
  lv_anim_set_path_cb(&a_status, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a_status, anim_opa_cb);
  lv_anim_start(&a_status);

  /* ---- polling timer ---- */
  g_splash_elapsed_ms = 0;
  g_system_ready = false;
  lv_timer_t *done_timer = lv_timer_create(splash_done_timer_cb, 100, NULL);
  lv_timer_set_repeat_count(done_timer, -1);
}
