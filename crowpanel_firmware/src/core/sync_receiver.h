#pragma once
#include <Arduino.h>
#include "sync_protocol.h"

class SyncReceiver {
public:
    static void init();
    static void loop();
    static void handleIncomingPacket(const uint8_t* data, size_t len);

private:
    static void sendPong();
    static void sendChunkAck(uint32_t sync_id, uint16_t chunk_index);
    static void sendSyncResult(uint32_t sync_id, SyncStatus status, const uint16_t* missing_indices, uint8_t missing_count);
};
