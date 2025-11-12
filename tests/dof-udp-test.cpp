// license:GPLv3+

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "plugins/dof/udp/dof_udp_event.h"
#include "plugins/dof/udp/dof_udp_queue.h"
#include "plugins/dof/udp/dof_event_collector.h"
#include "plugins/dof/udp/dof_udp_system.h"

#include <thread>
#include <chrono>
#include <cstring>

using namespace DOFUDP;

///////////////////////////////////////////////////////////////////////////////
// Event Protocol Tests
///////////////////////////////////////////////////////////////////////////////

TEST_SUITE("DOF UDP Event Protocol") {
    TEST_CASE("Event structure size") {
        // Event must be exactly 32 bytes for network compatibility
        CHECK(sizeof(Event) == 32);
    }

    TEST_CASE("BatchHeader structure size") {
        // BatchHeader must be exactly 16 bytes
        CHECK(sizeof(BatchHeader) == 16);
    }

    TEST_CASE("Event magic numbers") {
        Event e;
        CHECK(e.magic == 0x58464F44); // 'DOFX'

        BatchHeader h;
        CHECK(h.magic == 0x42464F44); // 'DOFB'
    }

    TEST_CASE("Event factory methods") {
        SUBCASE("Solenoid event") {
            Event e = Event::Solenoid(10, 255);
            CHECK(e.type == EventType::Solenoid);
            CHECK(e.id == 10);
            CHECK(e.value == 255);
            CHECK(e.timestamp_us > 0);
        }

        SUBCASE("Lamp event") {
            Event e = Event::Lamp(20, 128);
            CHECK(e.type == EventType::Lamp);
            CHECK(e.id == 20);
            CHECK(e.value == 128);
        }

        SUBCASE("GI event") {
            Event e = Event::GI(5, 200);
            CHECK(e.type == EventType::GI);
            CHECK(e.id == 5);
            CHECK(e.value == 200);
        }

        SUBCASE("RGB event") {
            Event e = Event::RGB(15, 255, 128, 64);
            CHECK(e.type == EventType::RGB);
            CHECK(e.id == 15);
            CHECK(e.r == 255);
            CHECK(e.g == 128);
            CHECK(e.b == 64);
        }

        SUBCASE("Wire event") {
            Event e = Event::Wire(8, 1);
            CHECK(e.type == EventType::Wire);
            CHECK(e.id == 8);
            CHECK(e.value == 1);
        }

        SUBCASE("Table loaded event") {
            Event e = Event::TableLoaded("TestTable", "test_rom");
            CHECK(e.type == EventType::TableLoaded);
            // Table name should be in reserved area
            CHECK(e.reserved[0] == 'T');
        }

        SUBCASE("Table unloaded event") {
            Event e = Event::TableUnloaded();
            CHECK(e.type == EventType::TableUnloaded);
        }
    }

    TEST_CASE("BatchPacket") {
        BatchPacket packet;

        SUBCASE("Initial state") {
            CHECK(packet.header.eventCount == 0);
            CHECK(packet.header.version == PROTOCOL_VERSION);
            CHECK(packet.GetPacketSize() == sizeof(BatchHeader));
        }

        SUBCASE("Add events") {
            Event e1 = Event::Solenoid(1, 100);
            Event e2 = Event::Lamp(2, 200);
            Event e3 = Event::GI(3, 255);

            CHECK(packet.AddEvent(e1) == true);
            CHECK(packet.header.eventCount == 1);

            CHECK(packet.AddEvent(e2) == true);
            CHECK(packet.header.eventCount == 2);

            CHECK(packet.AddEvent(e3) == true);
            CHECK(packet.header.eventCount == 3);

            CHECK(packet.GetPacketSize() == sizeof(BatchHeader) + 3 * sizeof(Event));
        }

        SUBCASE("Maximum batch size") {
            for (uint32_t i = 0; i < MAX_BATCH_EVENTS; i++) {
                Event e = Event::Solenoid(i, 255);
                CHECK(packet.AddEvent(e) == true);
            }

            CHECK(packet.header.eventCount == MAX_BATCH_EVENTS);

            // Try to add one more - should fail
            Event overflow = Event::Solenoid(99, 100);
            CHECK(packet.AddEvent(overflow) == false);
            CHECK(packet.header.eventCount == MAX_BATCH_EVENTS); // Unchanged
        }

        SUBCASE("Reset") {
            Event e = Event::Solenoid(1, 100);
            packet.AddEvent(e);
            CHECK(packet.header.eventCount == 1);

            packet.Reset();
            CHECK(packet.header.eventCount == 0);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// Lock-Free Queue Tests
///////////////////////////////////////////////////////////////////////////////

TEST_SUITE("Lock-Free Queue") {
    TEST_CASE("Queue capacity") {
        LockFreeQueue<int, 16> queue;
        CHECK(queue.Capacity() == 15); // One slot unused for full/empty detection
    }

    TEST_CASE("Basic push and pop") {
        LockFreeQueue<int, 16> queue;

        SUBCASE("Single item") {
            CHECK(queue.IsEmpty() == true);
            CHECK(queue.Push(42) == true);
            CHECK(queue.IsEmpty() == false);

            int value;
            CHECK(queue.Pop(value) == true);
            CHECK(value == 42);
            CHECK(queue.IsEmpty() == true);
        }

        SUBCASE("Multiple items") {
            CHECK(queue.Push(1) == true);
            CHECK(queue.Push(2) == true);
            CHECK(queue.Push(3) == true);

            int value;
            CHECK(queue.Pop(value) == true);
            CHECK(value == 1);

            CHECK(queue.Pop(value) == true);
            CHECK(value == 2);

            CHECK(queue.Pop(value) == true);
            CHECK(value == 3);

            CHECK(queue.IsEmpty() == true);
        }
    }

    TEST_CASE("Queue full condition") {
        LockFreeQueue<int, 8> queue; // Capacity = 7

        // Fill the queue
        for (int i = 0; i < 7; i++) {
            CHECK(queue.Push(i) == true);
        }

        CHECK(queue.IsFull() == true);

        // Try to push one more - should fail
        CHECK(queue.Push(999) == false);

        // Pop one item
        int value;
        CHECK(queue.Pop(value) == true);

        // Now we should be able to push again
        CHECK(queue.IsFull() == false);
        CHECK(queue.Push(777) == true);
    }

    TEST_CASE("Queue size tracking") {
        LockFreeQueue<int, 16> queue;

        CHECK(queue.GetSize() == 0);

        queue.Push(1);
        CHECK(queue.GetSize() == 1);

        queue.Push(2);
        queue.Push(3);
        CHECK(queue.GetSize() == 3);

        int value;
        queue.Pop(value);
        CHECK(queue.GetSize() == 2);

        queue.Pop(value);
        queue.Pop(value);
        CHECK(queue.GetSize() == 0);
    }

    TEST_CASE("Batch operations") {
        LockFreeQueue<int, 32> queue;

        SUBCASE("Batch push") {
            int items[] = {1, 2, 3, 4, 5};
            size_t pushed = PushBatch(queue, items, 5);
            CHECK(pushed == 5);
            CHECK(queue.GetSize() == 5);
        }

        SUBCASE("Batch pop") {
            for (int i = 1; i <= 10; i++) {
                queue.Push(i);
            }

            int items[5];
            size_t popped = PopBatch(queue, items, 5);
            CHECK(popped == 5);
            CHECK(items[0] == 1);
            CHECK(items[4] == 5);
            CHECK(queue.GetSize() == 5);
        }

        SUBCASE("Batch pop more than available") {
            queue.Push(1);
            queue.Push(2);

            int items[10];
            size_t popped = PopBatch(queue, items, 10);
            CHECK(popped == 2); // Only 2 available
            CHECK(items[0] == 1);
            CHECK(items[1] == 2);
        }
    }

    TEST_CASE("Queue with Events") {
        LockFreeQueue<Event, 64> queue;

        Event e1 = Event::Solenoid(1, 100);
        Event e2 = Event::Lamp(2, 200);

        CHECK(queue.Push(e1) == true);
        CHECK(queue.Push(e2) == true);

        Event result;
        CHECK(queue.Pop(result) == true);
        CHECK(result.type == EventType::Solenoid);
        CHECK(result.id == 1);
        CHECK(result.value == 100);

        CHECK(queue.Pop(result) == true);
        CHECK(result.type == EventType::Lamp);
        CHECK(result.id == 2);
        CHECK(result.value == 200);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Event Collector Tests
///////////////////////////////////////////////////////////////////////////////

TEST_SUITE("Event Collector") {
    TEST_CASE("Collector creation and destruction") {
        EventCollector* collector = new EventCollector(4096);
        CHECK(collector != nullptr);
        CHECK(collector->IsQueueEmpty() == true);
        delete collector;
    }

    TEST_CASE("Submit events") {
        EventCollector collector(4096);

        SUBCASE("Solenoid event") {
            collector.Solenoid(10, 255);
            CHECK(collector.IsQueueEmpty() == false);

            Event e;
            CHECK(collector.PopEvent(e) == true);
            CHECK(e.type == EventType::Solenoid);
            CHECK(e.id == 10);
            CHECK(e.value == 255);
        }

        SUBCASE("Lamp event") {
            collector.Lamp(20, 128);

            Event e;
            CHECK(collector.PopEvent(e) == true);
            CHECK(e.type == EventType::Lamp);
            CHECK(e.id == 20);
            CHECK(e.value == 128);
        }

        SUBCASE("Multiple events") {
            collector.Solenoid(1, 100);
            collector.Lamp(2, 150);
            collector.GI(3, 200);

            CHECK(collector.GetQueueSize() >= 3);

            Event events[3];
            size_t count = collector.PopBatch(events, 3);
            CHECK(count == 3);

            CHECK(events[0].type == EventType::Solenoid);
            CHECK(events[1].type == EventType::Lamp);
            CHECK(events[2].type == EventType::GI);
        }
    }

    TEST_CASE("Statistics tracking") {
        EventCollector collector(4096);

        collector.Solenoid(1, 100);
        collector.Lamp(2, 200);
        collector.GI(3, 255);

        Statistics stats = collector.GetStatistics();
        CHECK(stats.eventsSent == 3);
        CHECK(stats.eventsDropped == 0);

        collector.ResetStatistics();
        stats = collector.GetStatistics();
        CHECK(stats.eventsSent == 0);
    }

    TEST_CASE("Queue overflow handling") {
        // Use very small queue to test overflow
        EventCollector collector(16); // Actual capacity will be power of 2

        // Fill queue beyond capacity
        for (int i = 0; i < 10000; i++) {
            collector.Solenoid(i % 255, 255);
        }

        Statistics stats = collector.GetStatistics();
        // Some events should have been dropped
        CHECK(stats.eventsDropped > 0);
        CHECK(stats.eventsSent + stats.eventsDropped == 10000);
    }
}

///////////////////////////////////////////////////////////////////////////////
// System Integration Tests
///////////////////////////////////////////////////////////////////////////////

TEST_SUITE("UDP System Integration") {
    TEST_CASE("System initialization") {
        BroadcasterConfig config;
        config.enabled = true;
        config.address = "127.0.0.1";
        config.port = 7779; // Use different port to avoid conflicts
        config.maxPacketsPerSecond = 100;

        SUBCASE("Successful initialization") {
            bool result = InitializeDOFUDPBroadcaster(config);
            CHECK(result == true);
            CHECK(IsDOFUDPBroadcasterRunning() == true);

            EventCollector* collector = GetDOFEventCollector();
            CHECK(collector != nullptr);

            ShutdownDOFUDPBroadcaster();
            CHECK(IsDOFUDPBroadcasterRunning() == false);
        }

        SUBCASE("Disabled configuration") {
            config.enabled = false;
            bool result = InitializeDOFUDPBroadcaster(config);
            CHECK(result == false);
            CHECK(IsDOFUDPBroadcasterRunning() == false);
        }

        SUBCASE("Double initialization") {
            InitializeDOFUDPBroadcaster(config);
            CHECK(IsDOFUDPBroadcasterRunning() == true);

            // Try to initialize again - should fail
            bool result = InitializeDOFUDPBroadcaster(config);
            CHECK(result == false);

            ShutdownDOFUDPBroadcaster();
        }
    }

    TEST_CASE("Event submission and broadcasting") {
        BroadcasterConfig config;
        config.enabled = true;
        config.address = "127.0.0.1";
        config.port = 7780;
        config.maxPacketsPerSecond = 100;

        InitializeDOFUDPBroadcaster(config);
        EventCollector* collector = GetDOFEventCollector();
        REQUIRE(collector != nullptr);

        // Submit various events
        collector->Solenoid(1, 255);
        collector->Lamp(2, 128);
        collector->GI(3, 200);
        collector->RGB(4, 255, 0, 0);
        collector->Wire(5, 1);
        collector->TableLoaded("TestTable", "test_rom");

        // Give broadcaster thread time to process
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        Statistics stats = GetDOFUDPStatistics();
        CHECK(stats.eventsSent >= 6);

        ShutdownDOFUDPBroadcaster();
    }

    TEST_CASE("Statistics") {
        BroadcasterConfig config;
        config.enabled = true;
        config.address = "127.0.0.1";
        config.port = 7781;

        InitializeDOFUDPBroadcaster(config);
        EventCollector* collector = GetDOFEventCollector();

        // Submit many events
        for (int i = 0; i < 100; i++) {
            collector->Solenoid(i % 255, 255);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        Statistics stats = GetDOFUDPStatistics();
        CHECK(stats.eventsSent > 0);
        CHECK(stats.packetsSent > 0);

        ResetDOFUDPStatistics();
        stats = GetDOFUDPStatistics();
        CHECK(stats.eventsSent == 0);
        CHECK(stats.packetsSent == 0);

        ShutdownDOFUDPBroadcaster();
    }
}

///////////////////////////////////////////////////////////////////////////////
// Thread Safety Tests (basic)
///////////////////////////////////////////////////////////////////////////////

TEST_SUITE("Thread Safety") {
    TEST_CASE("Concurrent queue access") {
        LockFreeQueue<int, 1024> queue;
        std::atomic<int> pushCount{0};
        std::atomic<int> popCount{0};

        // Producer thread
        std::thread producer([&]() {
            for (int i = 0; i < 500; i++) {
                if (queue.Push(i)) {
                    pushCount++;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });

        // Consumer thread
        std::thread consumer([&]() {
            int value;
            for (int i = 0; i < 500; i++) {
                while (!queue.Pop(value)) {
                    std::this_thread::sleep_for(std::chrono::microseconds(5));
                }
                popCount++;
            }
        });

        producer.join();
        consumer.join();

        CHECK(pushCount == 500);
        CHECK(popCount == 500);
        CHECK(queue.IsEmpty() == true);
    }
}
