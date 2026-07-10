#ifndef UI_ENROLL_H
#define UI_ENROLL_H

#include <lvgl.h>

void buildEnrollScreen();
void buildEmpListScreen();
void uiShowEnrollStart(const char *name);
void uiShowEnrollStep(int step, const char *msg);
void uiShowEnrollResult(bool ok, const char *name);

#endif // UI_ENROLL_H
