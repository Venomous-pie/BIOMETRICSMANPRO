/**
 * crowpanel_firmware.ino
 * Biometrics Employee Time-In/Time-Out System - Display Node
 *
 * Board   : ESP32-S3 CrowPanel 5.0" (800x480)
 * Comms   : ESP-NOW wireless link to WROOM (no UART wire)
 *
 * Libraries (install via Arduino Library Manager):
 *   - LVGL 8.x/9.x
 *   - ArduinoJson
 *   - Arduino_GFX_Library
 */

#include <Arduino.h>
#include "src/core/display_driver.h"
#include <lvgl.h>
#include <esp_system.h>

#include "src/core/data_manager.h"
#include "src/core/comm_manager.h"
#include "src/ui/ui_manager.h"
#include "src/splash/manpro_splash.h"


// Display configuration
LGFX lcd;
unsigned long last_flush_time = 0; // Tracks the last time LVGL pushed a frame


// Screen blanking state
bool screen_is_awake = true;
unsigned long last_touch_time = 0;
static bool wait_for_release = false;

void manpro_wake_display() {
  if (!screen_is_awake) {
    screen_is_awake = true;
    lcd.setBrightness(DataManager::getBrightness());
  }
  last_touch_time = millis();
}

#ifndef LV_CONF_INCLUDE_SIMPLE
  #define LV_CONF_INCLUDE_SIMPLE
#endif

// Partial render buffer — 240 lines per buf in PSRAM. Blocking ops are off the main thread.
static const uint16_t LV_BUF_LINES = 240;

#if LVGL_VERSION_MAJOR >= 9
static void *buf1;
static void *buf2;
static lv_display_t *disp;
static lv_indev_t   *indev;

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  lcd.startWrite();
  lcd.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)px_map);
  lcd.endWrite();
  lcd.waitDisplay();
  last_flush_time = millis(); // Record exact time render finished
  lv_display_flush_ready(disp);
}

void my_touch_read(lv_indev_t *indev, lv_indev_data_t *data) {
  uint16_t touchX, touchY;
  bool touched = lcd.getTouch(&touchX, &touchY);
  if (touched) {
    if (!screen_is_awake) {
      manpro_wake_display();
      wait_for_release = true;
    }
    
    if (wait_for_release) {
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }
    
    last_touch_time = millis();
    data->state   = LV_INDEV_STATE_PRESSED;
    data->point.x = touchX;
    data->point.y = touchY;
  } else {
    wait_for_release = false;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}
#else
static lv_disp_draw_buf_t draw_buf;
static lv_color_t        *buf1;
static lv_color_t        *buf2;
static lv_disp_drv_t      disp_drv;
static lv_indev_drv_t     indev_drv;

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  lcd.startWrite();
  lcd.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
  lcd.endWrite();
  lcd.waitDisplay();
  lv_disp_flush_ready(disp);
}

void my_touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  uint16_t touchX, touchY;
  bool touched = lcd.getTouch(&touchX, &touchY);
  if (touched) {
    if (!screen_is_awake) {
      manpro_wake_display();
      wait_for_release = true;
    }
    
    if (wait_for_release) {
      data->state = LV_INDEV_STATE_REL;
      return;
    }
    
    last_touch_time = millis();
    data->state   = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  } else {
    wait_for_release = false;
    data->state = LV_INDEV_STATE_REL;
  }
}
#endif

SemaphoreHandle_t g_lvglMutex = NULL;

// Initialization
void setup() {
  Serial.begin(115200);
  Serial.printf("[BOOT] Reset reason: %d\n", esp_reset_reason());

  delay(500);
  if (Serial && Serial.availableForWrite() > 32) {
    Serial.println("\n=== Biometrics CrowPanel Display ===");
  }

  // Initialize filesystem and data
  DataManager::begin();
  
  

  // Check PSRAM availability
  size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  if (Serial && Serial.availableForWrite() > 64) {
    Serial.printf("[PSRAM] Free SPIRAM: %u bytes\n", psram_free);
    if (psram_free < 800000) Serial.println("[PSRAM] SPIRAM unavailable or low");
  }

  // Initialize communications before LCD to avoid hardware conflicts
  CommManager::begin();

  // Reset backlight
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);
  delay(200);

  // Initialize display
  lcd.init();
  lcd.setRotation(0);
  lcd.fillScreen(TFT_BLACK);

  // LVGL
  lv_init();
  
  // Partial render double buffer: 800x240 lines, allocated in PSRAM.
#if LVGL_VERSION_MAJOR >= 9
  uint32_t buf_sz = LCD_WIDTH * LV_BUF_LINES * 2; // 2 bytes per pixel (RGB565)
  buf1 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buf2 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf1 || !buf2) {
    Serial.println("[LVGL] FATAL: Could not allocate PSRAM buffers!");
  }

  disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, buf1, buf2, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touch_read);
#else
  // Partial render double buffer: 800*240 pixels at sizeof(lv_color_t) each, in PSRAM.
  buf1 = (lv_color_t *)heap_caps_malloc(LCD_WIDTH * LV_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buf2 = (lv_color_t *)heap_caps_malloc(LCD_WIDTH * LV_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf1 || !buf2) {
    Serial.println("[LVGL] FATAL: Could not allocate PSRAM buffers!");
    free(buf1); free(buf2);
    buf1 = (lv_color_t *)heap_caps_malloc(LCD_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    buf2 = NULL;
  }

  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf1 && buf2 ? LCD_WIDTH * LV_BUF_LINES : LCD_WIDTH * 40);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = LCD_WIDTH;
  disp_drv.ver_res  = LCD_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_t * my_disp = lv_disp_drv_register(&disp_drv);
  // Guard for LVGL 8.x
  if (my_disp && my_disp->refr_timer) {
    lv_timer_set_period(my_disp->refr_timer, 10);
  }

  lv_indev_drv_init(&indev_drv);
  indev_drv.type    = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);
#endif

  // CommManager initialized earlier

  // Show boot splash screen
  manpro_show_splash(UIManager::loadInitialScreen);

  // Draw first splash frame immediately
  lv_timer_handler();

  // Apply user-configured brightness
  lcd.setBrightness(DataManager::getBrightness());
  last_touch_time = millis(); // Initialize inactivity timer

  // Build UI screens in background
  UIManager::begin();
  
  if (g_lvglMutex == NULL) {
      g_lvglMutex = xSemaphoreCreateRecursiveMutex();
  }
}

// Main Loop
unsigned long lastLvglTick = 0;

void loop() {
  unsigned long now = millis();
  if (now != lastLvglTick) {
    lv_tick_inc(now - lastLvglTick);
    lastLvglTick = now;
  }
  
  if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
  lv_task_handler();
  if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);

  CommManager::process();

  int timeout_sec = DataManager::getScreenTimeout();
  if (timeout_sec > 0 && screen_is_awake && millis() - last_touch_time > (unsigned long)timeout_sec * 1000) {
    screen_is_awake = false;
    lcd.setBrightness(0);
  }

  yield();
}
