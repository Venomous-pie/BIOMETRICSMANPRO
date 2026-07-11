# Known Issues and Troubleshooting

## 1. Serial Forwarding Blocking (The "Pong Problem")

**Symptom:**
The CrowPanel (ESP32-S3) fails to respond to `PING` messages from the WROOM controller with a `PONG` (often resulting in "awaiting PONG" timeout messages on the WROOM serial monitor). The CrowPanel might also appear to freeze or stop updating its UI.

**Cause:**
The CrowPanel uses a "USB Serial forwarder" in its main loop to print WROOM messages to the USB serial monitor. Because the ESP32-S3 uses native USB CDC (`ARDUINO_USB_CDC_ON_BOOT=1`), if the board is plugged into a USB host (like a PC) but the host is **not actively reading the serial port**, the USB TX buffer fills up. 

When the buffer is full, the serial forwarder's calls to `Serial.println()` will **block the entire main loop indefinitely**. Because the loop is blocked by the serial forwarder, the `CommManager` stops processing incoming UART messages from the WROOM, causing it to miss `PING` messages and fail to send `PONG` replies.

**Solution:**
Unblock the serial forwarding by setting a transmit timeout of `0` in `setup()`:

```cpp
void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0); // <--- CRITICAL FIX: Unblocks the serial forwarder
  // ...
}
```

This ensures that if the host stops reading, the serial forwarder will simply drop the outgoing data instead of hanging the processor, allowing the main loop to continue running uninterrupted.

> [!WARNING]
> **Important Note on `setTxTimeoutMs`:** 
> This method is ONLY available for Native USB CDC (`HWCDC`) on ESP32-S3/C3. If you attempt to call `Serial.setTxTimeoutMs(0)` or `cpSerial.setTxTimeoutMs(0)` on a standard ESP32 (like the WROOM-32) that uses hardware UART (`HardwareSerial`), you will get a **compile error** (`'class HardwareSerial' has no member named 'setTxTimeoutMs'`). Hardware UARTs do not suffer from this infinite blocking issue anyway, so this fix is strictly for the CrowPanel's native USB port.
