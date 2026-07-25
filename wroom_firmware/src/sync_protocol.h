#pragma once
#include <Arduino.h>

#define SYNC_MAGIC_BYTE     0x5A
#define MAX_CHUNK_SIZE      200
// Maximum employees that can be transferred in one binary sync session.
// Capped at 127 to respect the AS608 sensor's physical slot limit.
// Both WROOM and CrowPanel must use this constant — do NOT redefine locally.
#define MAX_SYNC_EMPLOYEES  127

enum SyncPacketType : uint8_t {
    SYNC_PING = 0x01,
    SYNC_PONG = 0x02,
    SYNC_START = 0x03,
    SYNC_DATA = 0x04,
    SYNC_CHUNK_ACK = 0x05,
    SYNC_END = 0x06,
    SYNC_RESULT = 0x07
};

enum SyncStatus : uint8_t {
    SYNC_STATUS_OK = 0,
    SYNC_STATUS_NACK = 1
};

struct EmployeeSync {
    char name[40];
    char role[24];
    char branch[32];
    char department[24];
} __attribute__((packed));

// All packets start with these 2 bytes
struct SyncHeader {
    uint8_t magic; // Always SYNC_MAGIC_BYTE
    uint8_t type;  // SyncPacketType
} __attribute__((packed));

struct SyncPingPacket {
    SyncHeader header;
} __attribute__((packed));

struct SyncPongPacket {
    SyncHeader header;
} __attribute__((packed));

struct SyncStartPacket {
    SyncHeader header;
    uint32_t sync_id;       // timestamp or version
    uint16_t total_chunks;
    uint32_t total_bytes;
} __attribute__((packed));

struct SyncDataPacket {
    SyncHeader header;
    uint32_t sync_id;
    uint16_t chunk_index;
    uint8_t  payload_len;
    uint8_t  payload[MAX_CHUNK_SIZE];
} __attribute__((packed));

struct SyncChunkAckPacket {
    SyncHeader header;
    uint32_t sync_id;
    uint16_t chunk_index;
} __attribute__((packed));

struct SyncEndPacket {
    SyncHeader header;
    uint32_t sync_id;
    uint32_t crc32;
} __attribute__((packed));

struct SyncResultPacket {
    SyncHeader header;
    uint32_t sync_id;
    uint8_t  status; // SyncStatus
    uint8_t  missing_count;
    uint16_t missing_indices[64];
} __attribute__((packed));

// ── Packet size safety assertions ──────────────────────────────────────────
// SyncDataPacket is the largest packet sent per-chunk. The actual transmitted
// size is (sizeof(SyncDataPacket) - MAX_CHUNK_SIZE + payload_len), which is at
// most (sizeof header + MAX_CHUNK_SIZE). This must never exceed 250 bytes.
static_assert(sizeof(SyncDataPacket) - MAX_CHUNK_SIZE + MAX_CHUNK_SIZE <= 250,
    "SyncDataPacket + MAX_CHUNK_SIZE exceeds ESP-NOW 250-byte hard limit!");
// SyncResultPacket (with 64 missing indices) can be large but only flows
// CrowPanel -> WROOM, so it does not count against the WROOM send budget.
