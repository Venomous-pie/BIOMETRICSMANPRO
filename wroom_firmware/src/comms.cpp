#include "comms.h"
#include "config.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "sync_manager.h"
#include "sync_protocol.h"

uint8_t CROWPANEL_MAC[6]  = {0x30, 0xED, 0xA0, 0x31, 0x70, 0xEC}; // 30:ed:a0:31:70:ec
uint8_t lastKnownChannel   = ESPNOW_CHANNEL;

// ── Lock-free ring buffer ─────────────────────────────────────────────────────
// Single-producer (Core 0, WiFi task) / single-consumer (Core 1, loop()).
// Only the producer advances s_cpQTail; only the consumer advances s_cpQHead.
// No mutex needed as long as both pointers are read/written atomically (uint8_t).
#define ESPNOW_QUEUE_SIZE 8
struct EspNowMsg { char data[ESPNOW_PAYLOAD_MAX]; };
static EspNowMsg        s_cpQueue[ESPNOW_QUEUE_SIZE];
static volatile uint8_t s_cpQHead = 0;
static volatile uint8_t s_cpQTail = 0;

bool cpQueueEmpty() { return s_cpQHead == s_cpQTail; }

String cpQueuePop() {
  if (cpQueueEmpty()) return "";
  String msg(s_cpQueue[s_cpQHead].data);
  s_cpQHead = (s_cpQHead + 1) % ESPNOW_QUEUE_SIZE;
  return msg;
}

// ── ESP-NOW receive callback (Core 0, WiFi task) ──────────────────────────────
// Copies the raw payload into the ring buffer and returns immediately.
// All JSON parsing happens in loop() on Core 1 to keep this callback fast.
static void onDataRecvFromCP(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (len <= 0 || len > ESPNOW_PAYLOAD_MAX) return;

  // Route binary sync protocol packets directly to SyncManager
  if (data[0] == SYNC_MAGIC_BYTE) {
    SyncManager::handleIncomingPacket(data, len);
    return;
  }

  // Otherwise, treat as JSON and push to the ring buffer
  if (len >= ESPNOW_PAYLOAD_MAX) return; // Prevent buffer overflow for JSON strings

  uint8_t next = (s_cpQTail + 1) % ESPNOW_QUEUE_SIZE;
  if (next == s_cpQHead) return; // queue full — drop this message
  memcpy(s_cpQueue[s_cpQTail].data, data, len);
  s_cpQueue[s_cpQTail].data[len] = '\0';
  s_cpQTail = next;
}

// ── ESP-NOW send callback ─────────────────────────────────────────────────────
// Throttles failure logs to avoid flooding Serial during a channel mismatch.
// The CrowPanel has an independent channel scanner that restores the link within ~15 s.
static void onDataSentToCP(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  static uint32_t s_failCount     = 0;
  static uint32_t s_lastFailLogMs = 0;

  if (status != ESP_NOW_SEND_SUCCESS) {
    s_failCount++;
    uint32_t now = (uint32_t)millis();
    if (s_failCount == 1 || now - s_lastFailLogMs >= 10000) {
      Serial.printf("[ESP-NOW] SEND FAIL #%u on ch %d — CrowPanel may have rebooted. "
                    "Channel scanner will recover in ~15 s.\n",
                    s_failCount, lastKnownChannel);
      s_lastFailLogMs = now;
    }
  } else if (s_failCount > 0) {
    Serial.printf("[ESP-NOW] Link recovered after %u failure(s) on ch %d\n",
                  s_failCount, lastKnownChannel);
    s_failCount = 0;
  }
}

// ── Send ──────────────────────────────────────────────────────────────────────

void send(const String &json) {
  if (json.length() >= ESPNOW_PAYLOAD_MAX) {
    Serial.printf("[ESP-NOW] TX SKIP: payload too large (%d bytes)\n", json.length());
    return;
  }
  esp_now_send(CROWPANEL_MAC, (const uint8_t*)json.c_str(), json.length());
  Serial.println("[->CP] " + json);
}

void sendQuiet(const String &json) {
  if (json.length() >= ESPNOW_PAYLOAD_MAX) return;
  esp_now_send(CROWPANEL_MAC, (const uint8_t*)json.c_str(), json.length());
}

void sendDoc(JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  send(out);
}

void sendSyncPacket(const uint8_t* payload, size_t len) {
  if (len > 250) return; // ESP-NOW max payload size limit
  esp_now_send(CROWPANEL_MAC, payload, len);
}

// ── Channel sync ──────────────────────────────────────────────────────────────

void resyncEspNow(bool force) {
  uint8_t curCh = WiFi.channel();
  if (curCh == 0) curCh = ESPNOW_CHANNEL;

  uint8_t oldChannel = lastKnownChannel;

  if (curCh != oldChannel || force) {
    if (curCh != oldChannel) {
      Serial.printf("[ESP-NOW] Channel changed %d → %d. Notifying CrowPanel before switching.\n",
                    oldChannel, curCh);

      // Temporarily hop back to the old channel so the CrowPanel can still
      // receive the hop notification before we switch away from it.
      esp_wifi_set_channel(oldChannel, WIFI_SECOND_CHAN_NONE);

      StaticJsonDocument<64> hop;
      hop["type"] = "CHANNEL_HOP";
      hop["ch"]   = curCh;
      String hopOut; serializeJson(hop, hopOut);

      esp_now_del_peer(CROWPANEL_MAC);
      esp_now_peer_info_t peerOld = {};
      memcpy(peerOld.peer_addr, CROWPANEL_MAC, 6);
      peerOld.channel = oldChannel;
      peerOld.encrypt = false;
      esp_now_add_peer(&peerOld);

      send(hopOut);
      delay(100); // allow ESP-NOW to physically transmit before channel switch

    } else {
      Serial.printf("[ESP-NOW] Re-syncing peer registration on channel %d\n", curCh);
    }

    // Switch to the new channel and re-register the peer on it.
    lastKnownChannel = curCh;
    esp_wifi_set_channel(curCh, WIFI_SECOND_CHAN_NONE);
    esp_now_del_peer(CROWPANEL_MAC);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, CROWPANEL_MAC, 6);
    peerInfo.channel = curCh;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    // Second hop notification on the new channel confirms the switch completed.
    if (curCh != oldChannel) {
      StaticJsonDocument<64> hop;
      hop["type"] = "CHANNEL_HOP";
      hop["ch"]   = curCh;
      String hopOut; serializeJson(hop, hopOut);
      send(hopOut);
    }
  }
}

// ── Init ──────────────────────────────────────────────────────────────────────

void espNowInit() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_ps(WIFI_PS_NONE); // Disable modem sleep to prevent missing ESP-NOW packets

  // Drop any AP association without powering the radio off.
  // esp_wifi_set_channel() is silently ignored while the driver is connected,
  // so the radio must reach a clean idle state before we set the channel.
  WiFi.disconnect(false);
  delay(100);

  // Lock to the fixed channel before esp_now_init() so the peer registration
  // uses the correct channel from the start.
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // Tear down any prior ESP-NOW state before re-initializing.
  // Required when called after a full radio teardown (e.g. WiFi.disconnect(true)).
  esp_now_deinit();

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init FAILED!");
    return;
  }

  esp_now_register_recv_cb(onDataRecvFromCP);
  esp_now_register_send_cb(onDataSentToCP);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, CROWPANEL_MAC, 6);
  peer.channel = 0; // 0 = use current radio channel
  peer.encrypt  = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ESP-NOW] add_peer FAILED — verify CROWPANEL_MAC matches the CrowPanel boot log.");
  }

  Serial.printf("[ESP-NOW] Initialized on channel %d\n", ESPNOW_CHANNEL);
  Serial.printf("[BOOT] WROOM MAC: %s\n", WiFi.macAddress().c_str());
}
