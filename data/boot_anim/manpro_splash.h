#ifndef MANPRO_SPLASH_H
#define MANPRO_SPLASH_H

#include "lvgl.h"

/**
 * Shows the ManPro boot splash (~3s) and calls on_complete() when it's done.
 * Call this once from setup(), after lv_init()/display driver init and
 * after your first lv_timer_handler() tick.
 *
 * Example:
 *   void go_to_main_screen(void) { create_main_ui(); }
 *   ...
 *   manpro_show_splash(go_to_main_screen);
 */
void manpro_show_splash(void (*on_complete)(void));

#endif
