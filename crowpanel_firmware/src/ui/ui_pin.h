#ifndef UI_PIN_H
#define UI_PIN_H

#include <lvgl.h>

enum PINMode {
    PIN_MODE_AUTH,
    PIN_MODE_SETUP
};

void buildPinScreen();
void uiShowPinScreen(PINMode mode);

#endif // UI_PIN_H
