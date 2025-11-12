// license:GPLv3+

#pragma once

#include "dof_udp_event.h"
#include "dof_udp_queue.h"

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

    // Submit a table loaded event
    void TableLoaded(const char* tableName, const char* gameId);

    // Submit a table unloaded event
    void TableUnloaded();

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

    // Get queue size (approximate)
    size_t GetQueueSize() const;

    // Check if queue is empty
    bool IsQueueEmpty() const;

private:
    // Push an event to the queue
    // Returns true on success, false if queue is full
    bool PushEvent(const Event& event);

    // The event queue
    LockFreeQueue<Event, DEFAULT_QUEUE_SIZE>* m_queue;

    // Statistics (atomic for thread-safe access)
    std::atomic<uint64_t> m_eventsSubmitted;
    std::atomic<uint64_t> m_eventsDropped;
};

} // namespace DOFUDP
