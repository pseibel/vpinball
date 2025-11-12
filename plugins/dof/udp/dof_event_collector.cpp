// license:GPLv3+

#include "dof_event_collector.h"

namespace DOFUDP {

EventCollector::EventCollector(size_t queueSize)
    : m_queue(new LockFreeQueue<Event, DEFAULT_QUEUE_SIZE>())
    , m_segmentQueue(new LockFreeQueue<SegmentDisplayPacket, SEGMENT_QUEUE_SIZE>())
    , m_tableInfoQueue(new LockFreeQueue<TableInfoPacket, TABLE_INFO_QUEUE_SIZE>())
    , m_eventsSubmitted(0)
    , m_eventsDropped(0)
    , m_segmentDisplaysSubmitted(0)
    , m_segmentDisplaysDropped(0)
    , m_tableInfosSubmitted(0)
    , m_tableInfosDropped(0)
{
    // Note: Queue size is fixed at compile time (DEFAULT_QUEUE_SIZE)
    // The queueSize parameter is here for future extensibility
    (void)queueSize; // Suppress unused parameter warning
}

EventCollector::~EventCollector()
{
    delete m_queue;
    delete m_segmentQueue;
    delete m_tableInfoQueue;
}

void EventCollector::Solenoid(uint8_t id, uint16_t value)
{
    Event event = Event::Solenoid(id, value);
    if (!PushEvent(event)) {
        m_eventsDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_eventsSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::Solenoid(uint8_t id, uint16_t value, uint16_t groupId, uint16_t deviceId)
{
    Event event = Event::Solenoid(id, value, groupId, deviceId);
    if (!PushEvent(event)) {
        m_eventsDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_eventsSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::Lamp(uint8_t id, uint16_t value)
{
    Event event = Event::Lamp(id, value);
    if (!PushEvent(event)) {
        m_eventsDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_eventsSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::Lamp(uint8_t id, uint16_t value, uint16_t groupId, uint16_t deviceId)
{
    Event event = Event::Lamp(id, value, groupId, deviceId);
    if (!PushEvent(event)) {
        m_eventsDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_eventsSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::GI(uint8_t id, uint16_t value)
{
    Event event = Event::GI(id, value);
    if (!PushEvent(event)) {
        m_eventsDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_eventsSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::GI(uint8_t id, uint16_t value, uint16_t groupId, uint16_t deviceId)
{
    Event event = Event::GI(id, value, groupId, deviceId);
    if (!PushEvent(event)) {
        m_eventsDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_eventsSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::RGB(uint8_t id, uint8_t r, uint8_t g, uint8_t b)
{
    Event event = Event::RGB(id, r, g, b);
    if (!PushEvent(event)) {
        m_eventsDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_eventsSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::RGB(uint8_t id, uint8_t r, uint8_t g, uint8_t b, uint16_t groupId, uint16_t deviceId)
{
    Event event = Event::RGB(id, r, g, b, groupId, deviceId);
    if (!PushEvent(event)) {
        m_eventsDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_eventsSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::Wire(uint8_t id, uint16_t value)
{
    Event event = Event::Wire(id, value);
    if (!PushEvent(event)) {
        m_eventsDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_eventsSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::Wire(uint8_t id, uint16_t value, uint16_t groupId, uint16_t deviceId)
{
    Event event = Event::Wire(id, value, groupId, deviceId);
    if (!PushEvent(event)) {
        m_eventsDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_eventsSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::TableLoaded(const char* tableName, const char* gameId)
{
    Event event = Event::TableLoaded(tableName, gameId);
    if (!PushEvent(event)) {
        m_eventsDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_eventsSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::TableUnloaded()
{
    Event event = Event::TableUnloaded();
    if (!PushEvent(event)) {
        m_eventsDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_eventsSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::SegmentDisplay(uint64_t displayId, uint64_t groupId, uint32_t frameId,
                                    uint32_t hardware, uint8_t nElements,
                                    const uint8_t* elementTypes, const float* segmentData)
{
    SegmentDisplayPacket packet = SegmentDisplayPacket::Create(
        displayId, groupId, frameId, hardware, nElements, elementTypes, segmentData
    );

    if (!PushSegmentDisplay(packet)) {
        m_segmentDisplaysDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_segmentDisplaysSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCollector::TableInfo(const char* tableName, const char* romName)
{
    TableInfoPacket packet = TableInfoPacket::Create(tableName, romName);

    if (!PushTableInfo(packet)) {
        m_tableInfosDropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_tableInfosSubmitted.fetch_add(1, std::memory_order_relaxed);
    }
}

Statistics EventCollector::GetStatistics() const
{
    Statistics stats;
    stats.eventsSent = m_eventsSubmitted.load(std::memory_order_relaxed) +
                       m_segmentDisplaysSubmitted.load(std::memory_order_relaxed) +
                       m_tableInfosSubmitted.load(std::memory_order_relaxed);
    stats.eventsDropped = m_eventsDropped.load(std::memory_order_relaxed) +
                          m_segmentDisplaysDropped.load(std::memory_order_relaxed) +
                          m_tableInfosDropped.load(std::memory_order_relaxed);
    return stats;
}

void EventCollector::ResetStatistics()
{
    m_eventsSubmitted.store(0, std::memory_order_relaxed);
    m_eventsDropped.store(0, std::memory_order_relaxed);
    m_segmentDisplaysSubmitted.store(0, std::memory_order_relaxed);
    m_segmentDisplaysDropped.store(0, std::memory_order_relaxed);
    m_tableInfosSubmitted.store(0, std::memory_order_relaxed);
    m_tableInfosDropped.store(0, std::memory_order_relaxed);
}

bool EventCollector::PopEvent(Event& event)
{
    return m_queue->Pop(event);
}

size_t EventCollector::PopBatch(Event* events, size_t maxEvents)
{
    return DOFUDP::PopBatch(*m_queue, events, maxEvents);
}

size_t EventCollector::GetQueueSize() const
{
    return m_queue->GetSize();
}

bool EventCollector::IsQueueEmpty() const
{
    return m_queue->IsEmpty();
}

bool EventCollector::PushEvent(const Event& event)
{
    return m_queue->Push(event);
}

bool EventCollector::PopSegmentDisplay(SegmentDisplayPacket& packet)
{
    return m_segmentQueue->Pop(packet);
}

size_t EventCollector::GetSegmentQueueSize() const
{
    return m_segmentQueue->GetSize();
}

bool EventCollector::IsSegmentQueueEmpty() const
{
    return m_segmentQueue->IsEmpty();
}

bool EventCollector::PushSegmentDisplay(const SegmentDisplayPacket& packet)
{
    return m_segmentQueue->Push(packet);
}

bool EventCollector::PopTableInfo(TableInfoPacket& packet)
{
    return m_tableInfoQueue->Pop(packet);
}

size_t EventCollector::GetTableInfoQueueSize() const
{
    return m_tableInfoQueue->GetSize();
}

bool EventCollector::IsTableInfoQueueEmpty() const
{
    return m_tableInfoQueue->IsEmpty();
}

bool EventCollector::PushTableInfo(const TableInfoPacket& packet)
{
    return m_tableInfoQueue->Push(packet);
}

} // namespace DOFUDP
