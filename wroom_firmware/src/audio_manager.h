#pragma once

#include <Arduino.h>

void audioManagerInit();
void playTrack(int trackNum);
void beep(int durationMs = 100);
void setVolume(int vol);
