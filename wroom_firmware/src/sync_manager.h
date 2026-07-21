#pragma once
#include <Arduino.h>
#include "sync_protocol.h"

enum SyncState {
    SYNC_STATE_IDLE,
    SYNC_STATE_FETCH_WIFI,
    SYNC_STATE_SET_ESPNOW_CHANNEL,
    SYNC_STATE_SEND_PING,
    SYNC_STATE_SEND_SYNC_START,
    SYNC_STATE_SEND_CHUNKS,
    SYNC_STATE_SEND_SYNC_END,
    SYNC_STATE_AWAIT_SYNC_RESULT,
    SYNC_STATE_FAST_RETRY_MODE
};

class SyncManager {
public:
    static void init();
    static void loop();
    
    // Triggered manually (from SYNC_EMP command) or automatically (hourly)
    // If token is empty, uses the last known deviceToken.
    static void triggerSync(const String& token);

    // Incoming ESP-NOW binary packets handler
    static void handleIncomingPacket(const uint8_t* data, size_t len);

private:
    static void fetchEmployeesFromApi();
    static void changeToSyncChannel();
    static void sendPing();
    static void sendSyncStart();
    static void sendChunk(uint16_t chunk_index);
    static void sendSyncEnd();

    // State machine stepping
    static void advanceState(SyncState nextState);
    static void failToFastRetry(const char* reason);
};
