#ifndef UI_RESULT_H
#define UI_RESULT_H

#include <lvgl.h>

void buildResultScreen();
void uiShowMatch(const char *name, const char *dept, const char *action, const char *ts);
void uiShowNoMatch();

#endif // UI_RESULT_H
