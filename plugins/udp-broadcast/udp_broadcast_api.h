// license:GPLv3+

#pragma once

#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// UDP Broadcast Plugin API
//
// Public API for submitting events to the UDP broadcasting system.
// Other plugins can use this API to broadcast their events.
//
// Usage:
//   1. Get the API via MsgPlugin message passing
//   2. Call methods to submit events
//
// Thread Safety: All methods are thread-safe and lock-free
///////////////////////////////////////////////////////////////////////////////

#define UDPBCAST_NAMESPACE "UDPBroadcast"
#define UDPBCAST_MSG_GET_API "GetAPI"

struct UDPBroadcastAPI {
    // Device Events (DEVICE stream, port 7778)
    void (*Solenoid)(uint8_t id, uint16_t value);
    void (*Lamp)(uint8_t id, uint16_t value);
    void (*GI)(uint8_t id, uint16_t value);
    void (*RGB)(uint8_t id, uint8_t r, uint8_t g, uint8_t b);
    void (*Wire)(uint8_t id, uint16_t value);

    // Table Information (broadcasted on table load/unload)
    void (*TableInfo)(const char* tableName, const char* romName);

    // Segment Displays (SCORE stream, port 7781)
    // displayType: 0=7seg, 1=9seg, 2=14seg, 3=16seg, 4=alphanum
    void (*SegmentDisplay)(uint8_t displayId, uint8_t displayType, uint8_t digitCount, const uint16_t* segments);

    // Statistics
    struct {
        uint64_t (*GetEventsSubmitted)();
        uint64_t (*GetEventsDropped)();
        uint64_t (*GetSegmentDisplaysSubmitted)();
        uint64_t (*GetSegmentDisplaysDropped)();
        uint64_t (*GetTableInfosSubmitted)();
        uint64_t (*GetTableInfosDropped)();
    } Stats;
};
