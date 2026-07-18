#ifndef UI_ACTIVATION_H
#define UI_ACTIVATION_H

#include <lvgl.h>

void buildActivationScreen();
void uiShowActivation();
void uiActivationResult(bool success, const char* err);  // Called by CommManager on ACTIVATION_RESULT

#endif // UI_ACTIVATION_H
