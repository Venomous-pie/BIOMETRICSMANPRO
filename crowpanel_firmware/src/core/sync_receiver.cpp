#include "sync_receiver.h"
#include "comm_manager.h"
#include "data_manager.h"
#include "../ui/ui_manager.h"
#include <rom/crc.h>

#define MAX_EMPLOYEES 81
static uint8_t* s_scratchBuffer = nullptr;
static uint32_t s_expectedBytes = 0;
static uint16_t s_expectedChunks = 0;
static uint32_t s_currentSyncId = 0;
static bool*    s_chunkReceived = nullptr;
static bool     s_syncInProgress = false;
static uint32_t s_lastPacketMs = 0;

void SyncReceiver::init() {
    // Initialized from CommManager
    s_syncInProgress = false;
}

void SyncReceiver::loop() {
    // Timeout logic if a sync was started but abandoned mid-way
    if (s_syncInProgress && (millis() - s_lastPacketMs > 5000)) {
        Serial.println("[SYNC_RX] Sync timed out. Aborting.");
        UIManager::showToast("Employee Sync Failed (Timeout)", true);
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
            sendPong();
            break;
            
        case SYNC_START: {
            if (len >= sizeof(SyncStartPacket)) {
                const SyncStartPacket* pkt = (const SyncStartPacket*)data;
                Serial.printf("[SYNC_RX] SYNC_START received. ID: %u, Chunks: %u, Bytes: %u\n", pkt->sync_id, pkt->total_chunks, pkt->total_bytes);
                UIManager::showToast("Receiving Employee Data...", false);
                
                // Allocate or clear scratch buffer
                if (s_scratchBuffer) free(s_scratchBuffer);
                if (s_chunkReceived) free(s_chunkReceived);
                
                // Prevent crazy allocations
                if (pkt->total_bytes > MAX_EMPLOYEES * sizeof(EmployeeSync)) {
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
                            sendChunkAck(pkt->sync_id, pkt->chunk_index);
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
                        sendSyncResult(s_currentSyncId, SYNC_STATUS_NACK, missing, missingCount);
                    } else {
                        // All chunks received, verify CRC
                        uint32_t calcCrc = crc32_le(0, s_scratchBuffer, s_expectedBytes);
                        if (calcCrc == pkt->crc32) {
                            Serial.println("[SYNC_RX] CRC match. Applying sync buffer.");
                            DataManager::applySyncBuffer(s_scratchBuffer, s_expectedBytes);
                            sendSyncResult(s_currentSyncId, SYNC_STATUS_OK, nullptr, 0);
                            s_syncInProgress = false; // completed
                            UIManager::showToast("Employee Sync Successful", false);
                        } else {
                            Serial.printf("[SYNC_RX] CRC MISMATCH! Expected %08X, got %08X\n", pkt->crc32, calcCrc);
                            // Nack with chunk 0 just to force a retry, or we can just nack everything?
                            // According to spec, NACK on CRC mismatch should request specific corrupted chunks, but we don't know which one.
                            // So we just NACK everything.
                            missingCount = (s_expectedChunks > 64) ? 64 : s_expectedChunks;
                            for (uint8_t i = 0; i < missingCount; i++) missing[i] = i;
                            sendSyncResult(s_currentSyncId, SYNC_STATUS_NACK, missing, missingCount);
                        }
                    }
                }
            }
            break;
        }
    }
}
