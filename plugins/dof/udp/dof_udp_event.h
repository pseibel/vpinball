// license:GPLv3+

#pragma once

#include <cstdint>
#include <cstring>
#include <chrono>

namespace DOFUDP {

///////////////////////////////////////////////////////////////////////////////
// DOF UDP Broadcasting Protocol
//
// This protocol allows broadcasting DOF (Direct Output Framework) events
// via UDP for external LED controllers, monitoring tools, and other clients.
//
// Packet Format:
// - Single event packets (32 bytes)
// - Batched event packets (header + multiple events)
//
// Network Format: Little-endian, broadcast UDP packets
///////////////////////////////////////////////////////////////////////////////

// Protocol version
constexpr uint8_t PROTOCOL_VERSION = 1;

// Magic numbers for packet identification
constexpr uint32_t MAGIC_SINGLE_EVENT = 0x58464F44; // 'DOFX'
constexpr uint32_t MAGIC_BATCH_HEADER = 0x42464F44; // 'DOFB'

// Maximum events per batch packet
constexpr uint32_t MAX_BATCH_EVENTS = 20;

// Event Types
enum class EventType : uint8_t {
    // PinMAME/ROM-based events
    Solenoid = 1,         // Solenoid state (id, value 0-255)
    Lamp = 2,             // Lamp state (id, value 0-255)
    GI = 3,               // General Illumination (id, value 0-255)
    RGB = 4,              // RGB LED (id, r, g, b)
    Wire = 5,             // Wire/Switch state (id, value 0-1)

    // B2S Backglass events (future extension)
    BackglassLamp = 10,   // Backglass lamp (id, value 0-255)
    BackglassLED = 11,    // Backglass LED (id, value 0-255)
    BackglassRGB = 12,    // Backglass RGB (id, r, g, b)

    // Table lifecycle events
    TableLoaded = 128,    // Table loaded (tableName in payload)
    TableUnloaded = 129,  // Table unloaded
};

///////////////////////////////////////////////////////////////////////////////
// Single Event Structure (32 bytes)
///////////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
struct Event {
    uint32_t magic;           // MAGIC_SINGLE_EVENT (0x58464F44)
    uint64_t timestamp_us;    // Microseconds since epoch
    EventType type;           // Event type
    uint8_t id;               // Device ID (0-255)
    uint16_t value;           // Primary value (0-65535)

    // RGB data (for RGB events) or reserved
    uint8_t r;
    uint8_t g;
    uint8_t b;

    uint8_t reserved[13];     // Reserved for future use

    Event()
        : magic(MAGIC_SINGLE_EVENT)
        , timestamp_us(0)
        , type(EventType::Solenoid)
        , id(0)
        , value(0)
        , r(0)
        , g(0)
        , b(0)
    {
        memset(reserved, 0, sizeof(reserved));
    }

    // Get current timestamp in microseconds
    static uint64_t GetTimestampMicros() {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    }

    // Factory methods for creating events
    static Event Solenoid(uint8_t id, uint16_t value) {
        Event e;
        e.timestamp_us = GetTimestampMicros();
        e.type = EventType::Solenoid;
        e.id = id;
        e.value = value;
        return e;
    }

    static Event Lamp(uint8_t id, uint16_t value) {
        Event e;
        e.timestamp_us = GetTimestampMicros();
        e.type = EventType::Lamp;
        e.id = id;
        e.value = value;
        return e;
    }

    static Event GI(uint8_t id, uint16_t value) {
        Event e;
        e.timestamp_us = GetTimestampMicros();
        e.type = EventType::GI;
        e.id = id;
        e.value = value;
        return e;
    }

    static Event RGB(uint8_t id, uint8_t r, uint8_t g, uint8_t b) {
        Event e;
        e.timestamp_us = GetTimestampMicros();
        e.type = EventType::RGB;
        e.id = id;
        e.r = r;
        e.g = g;
        e.b = b;
        e.value = 0; // Not used for RGB
        return e;
    }

    static Event Wire(uint8_t id, uint16_t value) {
        Event e;
        e.timestamp_us = GetTimestampMicros();
        e.type = EventType::Wire;
        e.id = id;
        e.value = value;
        return e;
    }

    static Event TableLoaded(const char* tableName, const char* gameId) {
        Event e;
        e.timestamp_us = GetTimestampMicros();
        e.type = EventType::TableLoaded;
        // Table info encoded in reserved area (first 13 bytes)
        // Format: "tableName" (limited to 13 chars for now)
        if (tableName) {
            size_t len = strlen(tableName);
            if (len > 13) len = 13;
            memcpy(e.reserved, tableName, len);
        }
        return e;
    }

    static Event TableUnloaded() {
        Event e;
        e.timestamp_us = GetTimestampMicros();
        e.type = EventType::TableUnloaded;
        return e;
    }
};
#pragma pack(pop)

static_assert(sizeof(Event) == 32, "Event structure must be exactly 32 bytes");

///////////////////////////////////////////////////////////////////////////////
// Batch Packet Header (for multiple events in one UDP packet)
///////////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
struct BatchHeader {
    uint32_t magic;           // MAGIC_BATCH_HEADER (0x42464F44)
    uint8_t version;          // Protocol version
    uint8_t eventCount;       // Number of events in this batch (1-20)
    uint16_t reserved;
    uint64_t timestamp_us;    // Batch timestamp (microseconds since epoch)

    BatchHeader()
        : magic(MAGIC_BATCH_HEADER)
        , version(PROTOCOL_VERSION)
        , eventCount(0)
        , reserved(0)
        , timestamp_us(0)
    {}
};
#pragma pack(pop)

static_assert(sizeof(BatchHeader) == 16, "BatchHeader must be exactly 16 bytes");

///////////////////////////////////////////////////////////////////////////////
// Complete Batch Packet Structure
// Total size: 16 + (32 * eventCount) bytes
// Maximum size: 16 + (32 * 20) = 656 bytes
///////////////////////////////////////////////////////////////////////////////
struct BatchPacket {
    BatchHeader header;
    Event events[MAX_BATCH_EVENTS];

    BatchPacket() {
        header.timestamp_us = Event::GetTimestampMicros();
    }

    // Add event to batch (returns false if batch is full)
    bool AddEvent(const Event& event) {
        if (header.eventCount >= MAX_BATCH_EVENTS)
            return false;
        events[header.eventCount++] = event;
        return true;
    }

    // Get actual packet size in bytes
    size_t GetPacketSize() const {
        return sizeof(BatchHeader) + (header.eventCount * sizeof(Event));
    }

    // Reset batch for reuse
    void Reset() {
        header.eventCount = 0;
        header.timestamp_us = Event::GetTimestampMicros();
    }
};

///////////////////////////////////////////////////////////////////////////////
// Statistics structure for monitoring
///////////////////////////////////////////////////////////////////////////////
struct Statistics {
    uint64_t eventsSent;          // Total events successfully sent
    uint64_t eventsDropped;        // Events dropped due to queue full
    uint64_t packetsSent;          // Total UDP packets sent
    uint64_t sendErrors;           // UDP send errors
    uint64_t queueOverflows;       // Queue overflow events

    Statistics()
        : eventsSent(0)
        , eventsDropped(0)
        , packetsSent(0)
        , sendErrors(0)
        , queueOverflows(0)
    {}

    void Reset() {
        eventsSent = 0;
        eventsDropped = 0;
        packetsSent = 0;
        sendErrors = 0;
        queueOverflows = 0;
    }
};

} // namespace DOFUDP
