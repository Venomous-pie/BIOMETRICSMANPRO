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

/**
 * Signals that the background initialization is complete.
 * The splash screen will stay up for a minimum of 3 seconds.
 * If this is called before 3s, the splash will wait until 3s.
 * If this is called after 3s, it will dismiss immediately.
 * A hard timeout of 10s will dismiss the splash automatically if this is never called.
 */
void manpro_set_ready(void);

#endif
