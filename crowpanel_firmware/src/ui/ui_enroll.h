#ifndef UI_ENROLL_H
#define UI_ENROLL_H

#include <lvgl.h>

extern lv_obj_t *scr_enroll;
extern lv_obj_t *scr_choose_finger;
void buildEnrollScreen();
void buildEmpListScreen();
void buildChooseFingerScreen();
void uiShowEmpList();
void uiShowChooseFinger(int emp_id, const char *name, const char *dept);
void uiShowEnrollStart(const char *name);
void uiShowEnrollStep(int step, const char *msg);
void uiShowEnrollResult(bool ok, const char *name);
void cleanupEnrollScreens();

#endif // UI_ENROLL_H
