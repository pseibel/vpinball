// license:GPLv3+

#include "dof_event_collector.h"

namespace DOFUDP {

EventCollector::EventCollector(size_t queueSize)
    : m_queue(new LockFreeQueue<Event, DEFAULT_QUEUE_SIZE>())
    , m_eventsSubmitted(0)
    , m_eventsDropped(0)
{
    // Note: Queue size is fixed at compile time (DEFAULT_QUEUE_SIZE)
    // The queueSize parameter is here for future extensibility
    (void)queueSize; // Suppress unused parameter warning
}

EventCollector::~EventCollector()
{
    delete m_queue;
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

void EventCollector::Lamp(uint8_t id, uint16_t value)
{
    Event event = Event::Lamp(id, value);
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

void EventCollector::RGB(uint8_t id, uint8_t r, uint8_t g, uint8_t b)
{
    Event event = Event::RGB(id, r, g, b);
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

Statistics EventCollector::GetStatistics() const
{
    Statistics stats;
    stats.eventsSent = m_eventsSubmitted.load(std::memory_order_relaxed);
    stats.eventsDropped = m_eventsDropped.load(std::memory_order_relaxed);
    return stats;
}

void EventCollector::ResetStatistics()
{
    m_eventsSubmitted.store(0, std::memory_order_relaxed);
    m_eventsDropped.store(0, std::memory_order_relaxed);
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

} // namespace DOFUDP
