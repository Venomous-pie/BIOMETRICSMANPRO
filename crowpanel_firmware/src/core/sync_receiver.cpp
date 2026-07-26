#include "sync_receiver.h"
#include "comm_manager.h"
#include "data_manager.h"
#include "../ui/ui_manager.h"
#include "../ui/ui_sync_status.h"
#include <rom/crc.h>

// Use the shared cap from sync_protocol.h (BUG-05 fix — was locally redefined as 81).
static uint8_t* s_scratchBuffer = nullptr;
static uint32_t s_expectedBytes = 0;
static uint16_t s_expectedChunks = 0;
static uint32_t s_currentSyncId = 0;
static bool*    s_chunkReceived = nullptr;
static bool     s_syncInProgress = false;
static uint32_t s_lastPacketMs = 0;

// Starting chunk index for the current CRC-mismatch NACK round.
// On a CRC mismatch we can only request 64 chunks per NACK packet.
// s_nackRoundStart tracks where the next round begins so we page
// through all chunks instead of endlessly NACKing only 0–63 (BUG-11 fix).
static uint16_t s_nackRoundStart = 0;

// Pending TX state (to avoid calling esp_now_send from within rx_cb)
static volatile bool s_pendingPong = false;

static volatile bool s_pendingChunkAck = false;
static volatile uint32_t s_pendingAckSyncId = 0;
static volatile uint16_t s_pendingAckChunk = 0;

static volatile bool s_pendingSyncResult = false;
static volatile uint32_t s_pendingResultSyncId = 0;
static volatile SyncStatus s_pendingResultStatus = SYNC_STATUS_OK;
static volatile uint16_t s_pendingMissingIndices[64];
static volatile uint8_t s_pendingMissingCount = 0;

void SyncReceiver::init() {
    // Initialized from CommManager
    s_syncInProgress = false;
}

void SyncReceiver::loop() {
    // Process pending ESP-NOW responses safely outside the RX interrupt context
    if (s_pendingPong) {
        s_pendingPong = false;
        sendPong();
    }
    
    if (s_pendingChunkAck) {
        s_pendingChunkAck = false;
        sendChunkAck(s_pendingAckSyncId, s_pendingAckChunk);
    }
    
    if (s_pendingSyncResult) {
        s_pendingSyncResult = false;
        sendSyncResult(s_pendingResultSyncId, s_pendingResultStatus, (const uint16_t*)s_pendingMissingIndices, s_pendingMissingCount);
    }

    // Timeout logic if a sync was started but abandoned mid-way
    if (s_syncInProgress && (millis() - s_lastPacketMs > 5000)) {
        Serial.println("[SYNC_RX] Sync timed out. Aborting.");
        UIManager::showToast("Employee Sync Failed (Timeout)", true);
        uiSyncStatusOnSyncResult(false);
        s_syncInProgress = false;
        if (s_scratchBuffer) {
            free(s_scratchBuffer);
            s_scratchBuffer = nullptr;
        }
        if (s_chunkReceived) {
            free(s_chunkReceived);
            s_chunkReceived = nullptr;
        }
    }
}

void SyncReceiver::sendPong() {
    SyncPongPacket pkt;
    pkt.header.magic = SYNC_MAGIC_BYTE;
    pkt.header.type = SYNC_PONG;
    CommManager::sendSyncPacket((const uint8_t*)&pkt, sizeof(pkt));
}

void SyncReceiver::sendChunkAck(uint32_t sync_id, uint16_t chunk_index) {
    SyncChunkAckPacket pkt;
    pkt.header.magic = SYNC_MAGIC_BYTE;
    pkt.header.type = SYNC_CHUNK_ACK;
    pkt.sync_id = sync_id;
    pkt.chunk_index = chunk_index;
    CommManager::sendSyncPacket((const uint8_t*)&pkt, sizeof(pkt));
}

void SyncReceiver::sendSyncResult(uint32_t sync_id, SyncStatus status, const uint16_t* missing_indices, uint8_t missing_count) {
    SyncResultPacket pkt;
    pkt.header.magic = SYNC_MAGIC_BYTE;
    pkt.header.type = SYNC_RESULT;
    pkt.sync_id = sync_id;
    pkt.status = status;
    
    // Send max 64 missing indices
    uint8_t count = (missing_count > 64) ? 64 : missing_count;
    pkt.missing_count = count;
    
    if (count > 0 && missing_indices != nullptr) {
        memcpy(pkt.missing_indices, missing_indices, count * sizeof(uint16_t));
    }
    
    CommManager::sendSyncPacket((const uint8_t*)&pkt, sizeof(SyncHeader) + 6 + (count * sizeof(uint16_t)));
}

void SyncReceiver::handleIncomingPacket(const uint8_t* data, size_t len) {
    if (len < sizeof(SyncHeader)) return;
    const SyncHeader* hdr = (const SyncHeader*)data;
    
    s_lastPacketMs = millis();
    
    switch (hdr->type) {
        case SYNC_PING:
            // Always respond to ping to verify channel alignment
            s_pendingPong = true;
            break;
            
        case SYNC_START: {
            if (len >= sizeof(SyncStartPacket)) {
                const SyncStartPacket* pkt = (const SyncStartPacket*)data;
                Serial.printf("[SYNC_RX] SYNC_START received. ID: %u, Chunks: %u, Bytes: %u\n", pkt->sync_id, pkt->total_chunks, pkt->total_bytes);
                UIManager::showToast("Receiving Employee Data...", false);
                
                // Allocate or clear scratch buffer
                if (s_scratchBuffer) free(s_scratchBuffer);
                if (s_chunkReceived) free(s_chunkReceived);
                
                // Prevent crazy allocations — use MAX_SYNC_EMPLOYEES (BUG-05 fix)
                if (pkt->total_bytes > MAX_SYNC_EMPLOYEES * sizeof(EmployeeSync)) {
                    Serial.println("[SYNC_RX] Total bytes too large, ignoring sync.");
                    return;
                }
                
                s_scratchBuffer = (uint8_t*)malloc(pkt->total_bytes);
                s_chunkReceived = (bool*)malloc(pkt->total_chunks * sizeof(bool));
                
                if (!s_scratchBuffer || !s_chunkReceived) {
                    Serial.println("[SYNC_RX] Allocation failed!");
                    if (s_scratchBuffer) { free(s_scratchBuffer); s_scratchBuffer = nullptr; }
                    if (s_chunkReceived) { free(s_chunkReceived); s_chunkReceived = nullptr; }
                    return;
                }
                
                memset(s_scratchBuffer, 0, pkt->total_bytes);
                memset(s_chunkReceived, 0, pkt->total_chunks * sizeof(bool));
                
                s_expectedBytes = pkt->total_bytes;
                s_expectedChunks = pkt->total_chunks;
                s_currentSyncId = pkt->sync_id;
                s_syncInProgress = true;
                s_nackRoundStart = 0; // reset paging state for any fresh sync
            }
            break;
        }
            
        case SYNC_DATA: {
            if (s_syncInProgress && len >= sizeof(SyncHeader) + 7) {
                const SyncDataPacket* pkt = (const SyncDataPacket*)data;
                if (pkt->sync_id == s_currentSyncId && pkt->chunk_index < s_expectedChunks) {
                    if (len >= sizeof(SyncHeader) + 7 + pkt->payload_len) {
                        uint32_t offset = pkt->chunk_index * MAX_CHUNK_SIZE;
                        if (offset + pkt->payload_len <= s_expectedBytes) {
                            memcpy(s_scratchBuffer + offset, pkt->payload, pkt->payload_len);
                            s_chunkReceived[pkt->chunk_index] = true;
                            s_pendingAckSyncId = pkt->sync_id;
                            s_pendingAckChunk = pkt->chunk_index;
                            s_pendingChunkAck = true;
                        }
                    }
                }
            }
            break;
        }
            
        case SYNC_END: {
            if (s_syncInProgress && len >= sizeof(SyncEndPacket)) {
                const SyncEndPacket* pkt = (const SyncEndPacket*)data;
                if (pkt->sync_id == s_currentSyncId) {
                    // Check for missing chunks
                    uint16_t missing[64];
                    uint8_t missingCount = 0;
                    for (uint16_t i = 0; i < s_expectedChunks; i++) {
                        if (!s_chunkReceived[i]) {
                            if (missingCount < 64) {
                                missing[missingCount++] = i;
                            }
                        }
                    }
                    
                    if (missingCount > 0) {
                        Serial.printf("[SYNC_RX] NACK: %d missing chunks.\n", missingCount);
                        s_pendingResultSyncId = s_currentSyncId;
                        s_pendingResultStatus = SYNC_STATUS_NACK;
                        s_pendingMissingCount = missingCount;
                        memcpy((void*)s_pendingMissingIndices, missing, missingCount * sizeof(uint16_t));
                        s_pendingSyncResult = true;
                    } else {
                        // All chunks received, verify CRC
                        uint32_t calcCrc = crc32_le(0, s_scratchBuffer, s_expectedBytes);
                        if (calcCrc == pkt->crc32) {
                            Serial.println("[SYNC_RX] CRC match. Applying sync buffer.");
                            DataManager::applySyncBuffer(s_scratchBuffer, s_expectedBytes);
                            
                            s_pendingResultSyncId = s_currentSyncId;
                            s_pendingResultStatus = SYNC_STATUS_OK;
                            s_pendingMissingCount = 0;
                            s_pendingSyncResult = true;
                            
                            s_syncInProgress = false; // completed
                            UIManager::showToast("Employee Sync Successful", false);
                            uiSyncStatusOnSyncResult(true);
                        } else {
                            // CRC mismatch: request the next page of 64 chunks (BUG-11 fix).
                            // Instead of always requesting chunks 0..63, we page through
                            // all chunks in rounds so large payloads eventually converge.
                            Serial.printf("[SYNC_RX] CRC MISMATCH! Expected %08X, got %08X. NACK round from chunk %u.\n",
                                          pkt->crc32, calcCrc, s_nackRoundStart);

                            uint16_t rangeEnd = s_nackRoundStart + 64;
                            if (rangeEnd > s_expectedChunks) rangeEnd = s_expectedChunks;
                            uint8_t pageCount = (uint8_t)(rangeEnd - s_nackRoundStart);

                            uint16_t missing[64];
                            for (uint8_t i = 0; i < pageCount; i++)
                                missing[i] = s_nackRoundStart + i;

                            // Advance for next round; wrap around if we reach the end
                            s_nackRoundStart = (rangeEnd >= s_expectedChunks) ? 0 : rangeEnd;

                            s_pendingResultSyncId = s_currentSyncId;
                            s_pendingResultStatus = SYNC_STATUS_NACK;
                            s_pendingMissingCount = pageCount;
                            memcpy((void*)s_pendingMissingIndices, missing, pageCount * sizeof(uint16_t));
                            s_pendingSyncResult = true;
                        }
                    }
                }
            }
            break;
        }
    }
}
