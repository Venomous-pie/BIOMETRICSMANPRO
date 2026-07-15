// crowpanel_test_pingpong.ino — FINAL CONFIRMED VERSION
// Board: ESP32-S3 (CrowPanel 5.0")
//
// LOOPBACK CONFIRMED: IO38 (RX) and IO43 (TX) both work.
//
// Wiring (WROOM <-> CrowPanel):
//   WROOM GPIO33 (TX) --> CrowPanel IO38 (RX)
//   WROOM GPIO32 (RX) <-- CrowPanel IO43 (TX)
//   WROOM GND         --- CrowPanel GND  *** CRITICAL ***
//
// Listens for {"type":"PING"} → replies with {"type":"PONG"}

#include <Arduino.h>
#include <driver/uart.h>
#include <string.h>

#define TX_PIN  43   // IO43: CrowPanel TX -> WROOM GPIO32 (RX)
#define RX_PIN  38   // IO38: CrowPanel RX <- WROOM GPIO33 (TX)

static char rxBuf[256];
static int  rxLen = 0;

void idfSend(const char* msg) {
  uart_write_bytes(UART_NUM_0, msg, strlen(msg));
}

void setup() {
  delay(500);

  uart_driver_delete(UART_NUM_0);

  uart_config_t cfg = {
    .baud_rate  = 115200,
    .data_bits  = UART_DATA_8_BITS,
    .parity     = UART_PARITY_DISABLE,
    .stop_bits  = UART_STOP_BITS_1,
    .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
  };
  uart_driver_install(UART_NUM_0, 512, 0, 0, NULL, 0);
  uart_param_config(UART_NUM_0, &cfg);
  uart_set_pin(UART_NUM_0, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_flush_input(UART_NUM_0);

  idfSend("\r\n[CP] CROWPANEL READY - Waiting for PING...\r\n");
}

void loop() {
  uint8_t byte;
  while (uart_read_bytes(UART_NUM_0, &byte, 1, 0) == 1) {
    if (byte == '\n') {
      rxBuf[rxLen] = '\0';
      if (rxLen > 0 && rxBuf[rxLen-1] == '\r') rxBuf[--rxLen] = '\0';

      if (rxLen > 0) {
        if (strstr(rxBuf, "\"PING\"")) {
          idfSend("{\"type\":\"PONG\"}\r\n");
          idfSend("[CP] PONG sent!\r\n");
        }
      }
      rxLen = 0;
    } else if (rxLen < (int)sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = (char)byte;
    }
  }
}
