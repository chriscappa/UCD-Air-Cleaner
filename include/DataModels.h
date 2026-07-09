//This file defines the memory structures, binary-packed payload definitions 
//for high-speed ESP-NOW exchanges, and the bitfield mappings used for the Modbus Status Register.
#pragma once

#include <Arduino.h>

// =============================================================================
// Section 35.1: Status Bitfield Mapping
// =============================================================================
union StatusBitfield {
    struct {
        uint16_t fail_safe_active   : 1; // Bit 0: Fail-Safe Active (1 = Comm loss)
        uint16_t pressure_fault     : 1; // Bit 1: Pressure Sensor Fault (SDP810)
        uint16_t particle_fault     : 1; // Bit 2: Particle Sensor Fault (PMS5003)
        uint16_t node_offline       : 1; // Bit 3: Node Offline Indicator (Client only)
        uint16_t fan_stall_alarm    : 1; // Bit 4: Fan Stall Alarm (>10% dev for >5s)
        uint16_t manual_control     : 1; // Bit 5: Manual Control Active (Local button)
        uint16_t reserved           : 10;// Bits 6–15: Reserved (0)
    } __attribute__((packed)) bits;
    uint16_t raw;
};

// =============================================================================
// ESP-NOW High-Performance Synchronization Structs
// =============================================================================

enum class EspNowMessageType : uint8_t {
    PAIRING_REQUEST  = 0xA1,
    PAIRING_RESPONSE = 0xA2,
    UNPAIR_BEACON    = 0xA3,
    DOWNSTREAM_SYNC  = 0xB1, // Server -> Client Target distribution
    UPSTREAM_TELEMETRY= 0xC1  // Client -> Server State reporting
};

struct EspNowHeader {
    uint8_t protocol_magic[3]; // Always 'A', 'C', 'S'
    EspNowMessageType type;
    uint32_t transaction_id;
} __attribute__((packed));

// Downstream targets (Server -> Client)
struct DownstreamSyncPayload {
    EspNowHeader header;
    uint16_t target_fan_speed;      // Target Fan % (0-100)
    uint16_t ramp_time_seconds;     // Configured linear ramp speed
    uint8_t force_fail_safe;        // 1 = Force Client into 75% default state
} __attribute__((packed));

// Upstream Telemetry (Client -> Server)
struct UpstreamTelemetryPayload {
    EspNowHeader header;
    int16_t differential_pressure;  // Scaled x10
    uint16_t pm1_0;                 // Scaled x10
    uint16_t pm2_5;                 // Scaled x10
    uint16_t pm10;                  // Scaled x10
    uint16_t particle_count;        // Normalized (raw / 100)
    uint16_t actual_fan_speed;      // Normalized 0-100% (Tach multiplexer feedback)
    uint16_t active_local_target;   // Current target taking manual overrides into account
    uint16_t status_bitfield;       // Packed StatusBitfield
    uint16_t remaining_manual_min;  // Timeout remaining (minutes)
} __attribute__((packed));

// Handshake exchange payload
struct PairingPayload {
    EspNowHeader header;
    uint8_t client_mac[6];
} __attribute__((packed));
