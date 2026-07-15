/**
 * crowpanel_firmware.ino
 * Biometrics Employee Time-In/Time-Out System - Display Node
 *
 * Board   : ESP32-S3 CrowPanel 5.0" (800x480)
 * UART    : IO38 (RX from WROOM GPIO33), IO43 (TX to WROOM GPIO32)
 *
 * Libraries (install via Arduino Library Manager):
 *   - LVGL 8.x/9.x
 *   - ArduinoJson
 *   - Arduino_GFX_Library
 */

#include <Arduino.h>
#include <driver/uart.h>
#include "display_driver.h"
#include <lvgl.h>

#include "data_manager.h"
#include "comm_manager.h"
#include "ui_manager.h"

// ============================================================
// Display configuration (from display_driver.h)
// ============================================================
LGFX lcd;

#ifndef LV_CONF_INCLUDE_SIMPLE
  #define LV_CONF_INCLUDE_SIMPLE
#endif

static const uint16_t LV_BUF_LINES = 100; // ~160KB/buf in SRAM

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
  lv_display_flush_ready(disp);
}

void my_touch_read(lv_indev_t *indev, lv_indev_data_t *data) {
  uint16_t touchX, touchY;
  bool touched = lcd.getTouch(&touchX, &touchY);
  if (touched) {
    data->state   = LV_INDEV_STATE_PRESSED;
    data->point.x = touchX;
    data->point.y = touchY;
  } else {
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
  lv_disp_flush_ready(disp);
}

void my_touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  uint16_t touchX, touchY;
  bool touched = lcd.getTouch(&touchX, &touchY);
  if (touched) {
    data->state   = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}
#endif

// ============================================================
// UART to WROOM
// ============================================================
HardwareSerial WroomSerial(1);
#define WROOM_RX 38   // IO38: UART RX from WROOM GPIO33 (TX)
#define WROOM_TX 43   // IO43: UART TX to WROOM GPIO32 (RX)

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0); // Prevent native USB CDC from blocking the loop if host isn't reading

  delay(500);
  
  if (Serial && Serial.availableForWrite() > 32) {
    Serial.println("\n=== Biometrics CrowPanel Display ===");
  }

  // Init LittleFS and Data
  DataManager::begin();

  // PSRAM check (informational only — do not halt, we fall back to internal RAM)
  size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  if (Serial && Serial.availableForWrite() > 64) {
    Serial.printf("[PSRAM] Free SPIRAM: %u bytes\n", psram_free);
    if (psram_free < 800000) {
      Serial.println("[PSRAM] SPIRAM unavailable or low");
    }
  }

  // Backlight reset sequence
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);
  delay(200);

  // Display init via LovyanGFX
  lcd.init();
  lcd.setRotation(0);
  lcd.fillScreen(TFT_BLACK);
  digitalWrite(2, HIGH);

  // LVGL
  lv_init();
  
#if LVGL_VERSION_MAJOR >= 9
  uint32_t buf_sz = LCD_WIDTH * LV_BUF_LINES * 2;
  buf1 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buf2 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, buf1, buf2, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touch_read);
#else
  uint32_t buf_malloc_flags = (psram_free >= 800000)
    ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  buf1 = (lv_color_t *)heap_caps_malloc(LCD_WIDTH * LV_BUF_LINES * sizeof(lv_color_t), buf_malloc_flags);
  buf2 = (lv_color_t *)heap_caps_malloc(LCD_WIDTH * LV_BUF_LINES * sizeof(lv_color_t), buf_malloc_flags);
  if (!buf1 || !buf2) {
    Serial.println("[LVGL] FATAL: Could not allocate display buffers!");
    // Try minimal single-line internal buffer as last resort
    free(buf1); free(buf2);
    buf1 = (lv_color_t *)heap_caps_malloc(LCD_WIDTH * 10 * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    buf2 = NULL;
  }

  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_WIDTH * LV_BUF_LINES);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = LCD_WIDTH;
  disp_drv.ver_res  = LCD_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_t * my_disp = lv_disp_drv_register(&disp_drv);
  // Guard: refr_timer can be NULL in some LVGL 8.x builds
  if (my_disp && my_disp->refr_timer) {
    lv_timer_set_period(my_disp->refr_timer, 10);
  }

  lv_indev_drv_init(&indev_drv);
  indev_drv.type    = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);
#endif

  // WROOM UART - MUST be initialized before UIManager::begin() because screens send commands on boot
  // We use UART1 to avoid conflicts with ESP-IDF's default UART0 console
  WroomSerial.setRxBufferSize(2048);
  WroomSerial.begin(115200, SERIAL_8N1, WROOM_RX, WROOM_TX);
  
  // Flush the RX software/hardware FIFO to clear any ESP32 boot ROM noise 
  // that accumulated during startup before we start parsing JSON.
  while(WroomSerial.available()) WroomSerial.read();

  CommManager::begin();

  // Init UI Manager (Builds screens and loads activation or idle)
  UIManager::begin();
}

// ============================================================
// Loop
// ============================================================
unsigned long lastLvglTick = 0;

void loop() {
  unsigned long now = millis();
  if (now != lastLvglTick) {
    lv_tick_inc(now - lastLvglTick);
    lastLvglTick = now;
  }
  lv_task_handler();

  CommManager::process();

  yield();
}
