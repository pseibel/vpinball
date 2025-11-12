// license:GPLv3+

#pragma once

#include "dof_udp_event.h"
#include "lock_free_queue.h"

namespace DOFUDP {

///////////////////////////////////////////////////////////////////////////////
// Event Collector
//
// High-level API for collecting DOF events from the game thread.
// Events are pushed to a lock-free queue for processing by the broadcaster.
//
// Thread Safety:
// - All methods are thread-safe (lock-free)
// - Can be called from the game/DOF thread without blocking
//
// Usage:
//   collector->Solenoid(1, 255);  // Solenoid 1 at full power
//   collector->Lamp(10, 128);     // Lamp 10 at half brightness
//   collector->RGB(5, 255, 0, 0); // RGB LED 5 = red
///////////////////////////////////////////////////////////////////////////////

// Default queue size (power of 2)
constexpr size_t DEFAULT_QUEUE_SIZE = 4096;

class EventCollector {
public:
    explicit EventCollector(size_t queueSize = DEFAULT_QUEUE_SIZE);
    ~EventCollector();

    // Disable copy/move
    EventCollector(const EventCollector&) = delete;
    EventCollector& operator=(const EventCollector&) = delete;

    ///////////////////////////////////////////////////////////////////////////
    // Event Submission API (called from game thread)
    ///////////////////////////////////////////////////////////////////////////

    // Submit a solenoid event
    // id: Solenoid ID (1-255)
    // value: Solenoid power (0-255)
    void Solenoid(uint8_t id, uint16_t value);
    void Solenoid(uint8_t id, uint16_t value, uint16_t groupId, uint16_t deviceId);

    // Submit a lamp event
    // id: Lamp ID (1-255)
    // value: Lamp brightness (0-255)
    void Lamp(uint8_t id, uint16_t value);
    void Lamp(uint8_t id, uint16_t value, uint16_t groupId, uint16_t deviceId);

    // Submit a GI (General Illumination) event
    // id: GI string ID (1-255)
    // value: GI brightness (0-255)
    void GI(uint8_t id, uint16_t value);
    void GI(uint8_t id, uint16_t value, uint16_t groupId, uint16_t deviceId);

    // Submit an RGB LED event
    // id: LED ID (1-255)
    // r, g, b: RGB color components (0-255)
    void RGB(uint8_t id, uint8_t r, uint8_t g, uint8_t b);
    void RGB(uint8_t id, uint8_t r, uint8_t g, uint8_t b, uint16_t groupId, uint16_t deviceId);

    // Submit a wire/switch event
    // id: Wire ID (1-255)
    // value: Wire state (0=off, 1=on)
    void Wire(uint8_t id, uint16_t value);
    void Wire(uint8_t id, uint16_t value, uint16_t groupId, uint16_t deviceId);

    // Submit a segment display event
    // displayId: Unique display identifier
    // groupId: Display group identifier
    // frameId: Frame sequence number
    // hardware: Hardware type hint (CTLPI_SEG_HARDWARE_xxx)
    // nElements: Number of display elements (1-32)
    // elementTypes: Array of SegElementType for each element
    // segmentData: Array of float values (16 per element, one per segment)
    void SegmentDisplay(uint64_t displayId, uint64_t groupId, uint32_t frameId,
                       uint32_t hardware, uint8_t nElements,
                       const uint8_t* elementTypes, const float* segmentData);

    // Submit table information (loaded)
    // tableName: Full table name (e.g., "Attack from Mars")
    // romName: ROM identifier (e.g., "afm_113b")
    void TableInfo(const char* tableName, const char* romName);

    ///////////////////////////////////////////////////////////////////////////
    // Statistics API
    ///////////////////////////////////////////////////////////////////////////

    // Get current statistics
    Statistics GetStatistics() const;

    // Reset statistics counters
    void ResetStatistics();

    ///////////////////////////////////////////////////////////////////////////
    // Internal API (used by broadcaster)
    ///////////////////////////////////////////////////////////////////////////

    // Try to pop an event from the queue
    // Returns true if an event was popped, false if queue is empty
    bool PopEvent(Event& event);

    // Try to pop multiple events (batch)
    // Returns number of events actually popped
    size_t PopBatch(Event* events, size_t maxEvents);

    // Try to pop a segment display packet from the segment queue
    // Returns true if a packet was popped, false if queue is empty
    bool PopSegmentDisplay(SegmentDisplayPacket& packet);

    // Try to pop a table info packet from the table info queue
    // Returns true if a packet was popped, false if queue is empty
    bool PopTableInfo(TableInfoPacket& packet);

    // Get queue size (approximate)
    size_t GetQueueSize() const;

    // Get segment queue size (approximate)
    size_t GetSegmentQueueSize() const;

    // Get table info queue size (approximate)
    size_t GetTableInfoQueueSize() const;

    // Check if queue is empty
    bool IsQueueEmpty() const;

    // Check if segment queue is empty
    bool IsSegmentQueueEmpty() const;

    // Check if table info queue is empty
    bool IsTableInfoQueueEmpty() const;

private:
    // Push an event to the queue
    // Returns true on success, false if queue is full
    bool PushEvent(const Event& event);

    // Push a segment display packet to the segment queue
    // Returns true on success, false if queue is full
    bool PushSegmentDisplay(const SegmentDisplayPacket& packet);

    // Push a table info packet to the table info queue
    // Returns true on success, false if queue is full
    bool PushTableInfo(const TableInfoPacket& packet);

    ///////////////////////////////////////////////////////////////////////////
    // Multi-Queue Architecture
    //
    // Regular events (32 bytes):       Queue of 4096 entries = ~128 KB
    // Segment packets (2144 bytes max): Queue of 256 entries  = ~548 KB
    // Table info (340 bytes max):       Queue of 16 entries   = ~5 KB
    //
    // Separate queues allow:
    // 1. Different sizing strategies based on frequency and size
    // 2. Priority handling in broadcaster (rare events checked first)
    // 3. Better memory locality (hot/cold data separation)
    ///////////////////////////////////////////////////////////////////////////

    // The event queue (for small 32-byte events)
    LockFreeQueue<Event, DEFAULT_QUEUE_SIZE>* m_queue;

    // The segment display queue (for large 2144-byte packets)
    // Smaller size since packets are much larger and less frequent
    static constexpr size_t SEGMENT_QUEUE_SIZE = 256;
    LockFreeQueue<SegmentDisplayPacket, SEGMENT_QUEUE_SIZE>* m_segmentQueue;

    // The table info queue (for 340-byte packets)
    // Very small size since table changes are rare (once per game)
    static constexpr size_t TABLE_INFO_QUEUE_SIZE = 16;
    LockFreeQueue<TableInfoPacket, TABLE_INFO_QUEUE_SIZE>* m_tableInfoQueue;

    // Statistics (atomic for thread-safe access)
    std::atomic<uint64_t> m_eventsSubmitted;
    std::atomic<uint64_t> m_eventsDropped;
    std::atomic<uint64_t> m_segmentDisplaysSubmitted;
    std::atomic<uint64_t> m_segmentDisplaysDropped;
    std::atomic<uint64_t> m_tableInfosSubmitted;
    std::atomic<uint64_t> m_tableInfosDropped;
};

} // namespace DOFUDP
