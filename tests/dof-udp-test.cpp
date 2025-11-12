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
            bool result = InitializeStream(StreamType::DEVICE, config);
            CHECK(result == true);
            CHECK(IsStreamRunning(StreamType::DEVICE) == true);

            EventCollector* collector = GetEventCollector(StreamType::DEVICE);
            CHECK(collector != nullptr);

            ShutdownStream(StreamType::DEVICE);
            CHECK(IsStreamRunning(StreamType::DEVICE) == false);
        }

        SUBCASE("Disabled configuration") {
            config.enabled = false;
            bool result = InitializeStream(StreamType::DEVICE, config);
            CHECK(result == false);
            CHECK(IsStreamRunning(StreamType::DEVICE) == false);
        }

        SUBCASE("Double initialization") {
            InitializeStream(StreamType::DEVICE, config);
            CHECK(IsStreamRunning(StreamType::DEVICE) == true);

            // Try to initialize again - should fail
            bool result = InitializeStream(StreamType::DEVICE, config);
            CHECK(result == false);

            ShutdownStream(StreamType::DEVICE);
        }
    }

    TEST_CASE("Event submission and broadcasting") {
        BroadcasterConfig config;
        config.enabled = true;
        config.address = "127.0.0.1";
        config.port = 7780;
        config.maxPacketsPerSecond = 100;

        InitializeStream(StreamType::DEVICE, config);
        EventCollector* collector = GetEventCollector(StreamType::DEVICE);
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

        Statistics stats = GetStreamStatistics(StreamType::DEVICE);
        CHECK(stats.eventsSent >= 6);

        ShutdownStream(StreamType::DEVICE);
    }

    TEST_CASE("Statistics") {
        BroadcasterConfig config;
        config.enabled = true;
        config.address = "127.0.0.1";
        config.port = 7781;

        InitializeStream(StreamType::DEVICE, config);
        EventCollector* collector = GetEventCollector(StreamType::DEVICE);

        // Submit many events
        for (int i = 0; i < 100; i++) {
            collector->Solenoid(i % 255, 255);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        Statistics stats = GetStreamStatistics(StreamType::DEVICE);
        CHECK(stats.eventsSent > 0);
        CHECK(stats.packetsSent > 0);

        ResetStreamStatistics(StreamType::DEVICE);
        stats = GetStreamStatistics(StreamType::DEVICE);
        CHECK(stats.eventsSent == 0);
        CHECK(stats.packetsSent == 0);

        ShutdownStream(StreamType::DEVICE);
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

///////////////////////////////////////////////////////////////////////////////
// Multi-Stream Architecture Tests
///////////////////////////////////////////////////////////////////////////////

TEST_SUITE("Multi-Stream Architecture") {
    TEST_CASE("Initialize multiple streams independently") {
        BroadcasterConfig deviceConfig;
        deviceConfig.enabled = true;
        deviceConfig.address = "127.0.0.1";
        deviceConfig.port = 7782;
        deviceConfig.maxPacketsPerSecond = 100;

        BroadcasterConfig rgbConfig;
        rgbConfig.enabled = true;
        rgbConfig.address = "127.0.0.1";
        rgbConfig.port = 7783;
        rgbConfig.maxPacketsPerSecond = 100;

        SUBCASE("Initialize DEVICE and RGB streams") {
            bool deviceResult = InitializeStream(StreamType::DEVICE, deviceConfig);
            bool rgbResult = InitializeStream(StreamType::RGB, rgbConfig);

            CHECK(deviceResult == true);
            CHECK(rgbResult == true);
            CHECK(IsStreamRunning(StreamType::DEVICE) == true);
            CHECK(IsStreamRunning(StreamType::RGB) == true);

            ShutdownStream(StreamType::DEVICE);
            ShutdownStream(StreamType::RGB);
        }

        SUBCASE("Independent stream shutdown") {
            InitializeStream(StreamType::DEVICE, deviceConfig);
            InitializeStream(StreamType::RGB, rgbConfig);

            // Shutdown only DEVICE stream
            ShutdownStream(StreamType::DEVICE);
            CHECK(IsStreamRunning(StreamType::DEVICE) == false);
            CHECK(IsStreamRunning(StreamType::RGB) == true);

            // RGB stream should still work
            EventCollector* rgbCollector = GetEventCollector(StreamType::RGB);
            CHECK(rgbCollector != nullptr);

            ShutdownStream(StreamType::RGB);
        }

        SUBCASE("ShutdownAllStreams") {
            InitializeStream(StreamType::DEVICE, deviceConfig);
            InitializeStream(StreamType::RGB, rgbConfig);

            CHECK(IsStreamRunning(StreamType::DEVICE) == true);
            CHECK(IsStreamRunning(StreamType::RGB) == true);

            ShutdownAllStreams();

            CHECK(IsStreamRunning(StreamType::DEVICE) == false);
            CHECK(IsStreamRunning(StreamType::RGB) == false);
        }
    }

    TEST_CASE("Stream-specific event collectors") {
        BroadcasterConfig deviceConfig;
        deviceConfig.enabled = true;
        deviceConfig.address = "127.0.0.1";
        deviceConfig.port = 7784;
        deviceConfig.maxPacketsPerSecond = 100;

        BroadcasterConfig rgbConfig;
        rgbConfig.enabled = true;
        rgbConfig.address = "127.0.0.1";
        rgbConfig.port = 7785;
        rgbConfig.maxPacketsPerSecond = 100;

        InitializeStream(StreamType::DEVICE, deviceConfig);
        InitializeStream(StreamType::RGB, rgbConfig);

        EventCollector* deviceCollector = GetEventCollector(StreamType::DEVICE);
        EventCollector* rgbCollector = GetEventCollector(StreamType::RGB);

        REQUIRE(deviceCollector != nullptr);
        REQUIRE(rgbCollector != nullptr);
        CHECK(deviceCollector != rgbCollector); // Different collectors

        // Submit events to different collectors
        deviceCollector->Solenoid(1, 255, 0x0300, 1);
        deviceCollector->Lamp(2, 128, 0x0200, 2);
        deviceCollector->GI(3, 200, 0x0100, 3);

        rgbCollector->RGB(1, 255, 0, 0, 0x0400, 1);
        rgbCollector->RGB(2, 0, 255, 0, 0x0400, 2);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Both streams should have sent events
        Statistics deviceStats = GetStreamStatistics(StreamType::DEVICE);
        Statistics rgbStats = GetStreamStatistics(StreamType::RGB);

        CHECK(deviceStats.eventsSent > 0);
        CHECK(rgbStats.eventsSent > 0);

        ShutdownAllStreams();
    }

    TEST_CASE("Stream isolation - events don't cross streams") {
        BroadcasterConfig deviceConfig;
        deviceConfig.enabled = true;
        deviceConfig.address = "127.0.0.1";
        deviceConfig.port = 7786;

        BroadcasterConfig rgbConfig;
        rgbConfig.enabled = true;
        rgbConfig.address = "127.0.0.1";
        rgbConfig.port = 7787;

        InitializeStream(StreamType::DEVICE, deviceConfig);
        InitializeStream(StreamType::RGB, rgbConfig);

        EventCollector* deviceCollector = GetEventCollector(StreamType::DEVICE);
        EventCollector* rgbCollector = GetEventCollector(StreamType::RGB);

        // Submit 10 events to device stream
        for (int i = 0; i < 10; i++) {
            deviceCollector->Solenoid(i, 255, 0x0300, i);
        }

        // Submit 5 events to RGB stream
        for (int i = 0; i < 5; i++) {
            rgbCollector->RGB(i, 255, 128, 64, 0x0400, i);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        Statistics deviceStats = GetStreamStatistics(StreamType::DEVICE);
        Statistics rgbStats = GetStreamStatistics(StreamType::RGB);

        // Device stream should have ~10 events, RGB should have ~5
        CHECK(deviceStats.eventsSent >= 10);
        CHECK(rgbStats.eventsSent >= 5);

        ShutdownAllStreams();
    }

    TEST_CASE("Double initialization of same stream") {
        BroadcasterConfig config;
        config.enabled = true;
        config.address = "127.0.0.1";
        config.port = 7789;

        bool firstInit = InitializeStream(StreamType::DEVICE, config);
        CHECK(firstInit == true);

        // Second initialization should fail
        bool secondInit = InitializeStream(StreamType::DEVICE, config);
        CHECK(secondInit == false);

        ShutdownStream(StreamType::DEVICE);
    }

    TEST_CASE("Stream statistics independence") {
        BroadcasterConfig config1;
        config1.enabled = true;
        config1.address = "127.0.0.1";
        config1.port = 7790;

        BroadcasterConfig config2;
        config2.enabled = true;
        config2.address = "127.0.0.1";
        config2.port = 7791;

        InitializeStream(StreamType::DEVICE, config1);
        InitializeStream(StreamType::RGB, config2);

        EventCollector* deviceCollector = GetEventCollector(StreamType::DEVICE);
        EventCollector* rgbCollector = GetEventCollector(StreamType::RGB);

        // Submit different numbers of events
        for (int i = 0; i < 20; i++) {
            deviceCollector->Solenoid(i, 255, 0x0300, i);
        }

        for (int i = 0; i < 10; i++) {
            rgbCollector->RGB(i, 255, 0, 0, 0x0400, i);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        Statistics deviceStats = GetStreamStatistics(StreamType::DEVICE);
        Statistics rgbStats = GetStreamStatistics(StreamType::RGB);

        // Stats should be independent
        CHECK(deviceStats.eventsSent > rgbStats.eventsSent);

        // Reset only RGB stats
        ResetStreamStatistics(StreamType::RGB);

        Statistics deviceStats2 = GetStreamStatistics(StreamType::DEVICE);
        Statistics rgbStats2 = GetStreamStatistics(StreamType::RGB);

        CHECK(deviceStats2.eventsSent > 0); // Device stats unchanged
        CHECK(rgbStats2.eventsSent == 0);   // RGB stats reset

        ShutdownAllStreams();
    }

    TEST_CASE("Segment display support") {
        BroadcasterConfig config;
        config.enabled = true;
        config.address = "127.0.0.1";
        config.port = 7788;
        config.maxPacketsPerSecond = 60;

        InitializeStream(StreamType::DEVICE, config);
        EventCollector* collector = GetEventCollector(StreamType::DEVICE);
        REQUIRE(collector != nullptr);

        SUBCASE("SegmentDisplay packet creation") {
            uint8_t elementTypes[5] = {
                (uint8_t)SegElementType::SEG_7,
                (uint8_t)SegElementType::SEG_7,
                (uint8_t)SegElementType::SEG_7,
                (uint8_t)SegElementType::SEG_7,
                (uint8_t)SegElementType::SEG_7
            };
            float segmentData[5 * 16]; // 5 elements * 16 segments each

            // Fill with test data
            for (int i = 0; i < 5 * 16; i++) {
                segmentData[i] = (float)(i % 2); // Alternating 0/1
            }

            SegmentDisplayPacket packet = SegmentDisplayPacket::Create(
                0x1234567890ABCDEF,  // displayId
                0xFEDCBA0987654321,  // groupId
                42,                   // frameId
                0x00030001,          // hardware (GTS1_4DIGIT)
                5,                    // nElements
                elementTypes,
                segmentData
            );

            CHECK(packet.header.magic == MAGIC_SEGMENT_DISPLAY);
            CHECK(packet.header.displayId == 0x1234567890ABCDEF);
            CHECK(packet.header.groupId == 0xFEDCBA0987654321);
            CHECK(packet.header.frameId == 42);
            CHECK(packet.header.hardware == 0x00030001);
            CHECK(packet.header.nElements == 5);

            // Verify element types
            for (int i = 0; i < 5; i++) {
                CHECK(packet.header.elementTypes[i] == (uint8_t)SegElementType::SEG_7);
            }

            // Verify segment data
            for (int i = 0; i < 5 * 16; i++) {
                CHECK(packet.segmentData[i] == (float)(i % 2));
            }

            // Check packet size calculation
            size_t expectedSize = sizeof(SegmentDisplayHeader) + (5 * 16 * sizeof(float));
            CHECK(packet.GetPacketSize() == expectedSize);
        }

        SUBCASE("SegmentDisplay submission and queuing") {
            uint8_t elementTypes[2] = {
                (uint8_t)SegElementType::SEG_14,
                (uint8_t)SegElementType::SEG_14
            };
            float segmentData[2 * 16];

            // Fill with test pattern
            for (int i = 0; i < 2 * 16; i++) {
                segmentData[i] = 0.5f;
            }

            // Submit segment display
            collector->SegmentDisplay(
                0x1111111111111111,  // displayId
                0x2222222222222222,  // groupId
                100,                  // frameId
                0x00020000,          // hardware (RED_LED)
                2,                    // nElements
                elementTypes,
                segmentData
            );

            // Check queue is not empty
            CHECK(!collector->IsSegmentQueueEmpty());
            CHECK(collector->GetSegmentQueueSize() > 0);

            // Pop the packet
            SegmentDisplayPacket packet;
            CHECK(collector->PopSegmentDisplay(packet));

            // Verify packet contents
            CHECK(packet.header.displayId == 0x1111111111111111);
            CHECK(packet.header.groupId == 0x2222222222222222);
            CHECK(packet.header.frameId == 100);
            CHECK(packet.header.nElements == 2);

            // Queue should now be empty
            CHECK(collector->IsSegmentQueueEmpty());
        }

        SUBCASE("Mixed event types - regular events and segment displays") {
            // Submit regular events
            for (int i = 0; i < 10; i++) {
                collector->Solenoid(i, 255, 0x0300, i);
            }

            // Submit segment display
            uint8_t elementTypes[1] = {(uint8_t)SegElementType::SEG_7D};
            float segmentData[16];
            for (int i = 0; i < 16; i++) {
                segmentData[i] = 1.0f;
            }

            collector->SegmentDisplay(
                0xAAAAAAAAAAAAAAAA,
                0xBBBBBBBBBBBBBBBB,
                999,
                0x00040000,
                1,
                elementTypes,
                segmentData
            );

            // Both queues should have data
            CHECK(!collector->IsQueueEmpty());
            CHECK(!collector->IsSegmentQueueEmpty());

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Both types should be sent (statistics combined)
            Statistics stats = GetStreamStatistics(StreamType::DEVICE);
            CHECK(stats.eventsSent >= 11); // At least 10 regular + 1 segment
        }

        SUBCASE("Segment display statistics") {
            ResetStreamStatistics(StreamType::DEVICE);

            uint8_t elementTypes[1] = {(uint8_t)SegElementType::SEG_9};
            float segmentData[16];
            std::memset(segmentData, 0, sizeof(segmentData));

            // Submit multiple segment displays
            for (int i = 0; i < 5; i++) {
                collector->SegmentDisplay(
                    i, i, i, 0, 1, elementTypes, segmentData
                );
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            Statistics stats = GetStreamStatistics(StreamType::DEVICE);
            CHECK(stats.eventsSent >= 5);
        }

        ShutdownAllStreams();
    }

    TEST_CASE("Table information support") {
        BroadcasterConfig config;
        config.enabled = true;
        config.address = "127.0.0.1";
        config.port = 7789;
        config.maxPacketsPerSecond = 60;

        InitializeStream(StreamType::DEVICE, config);
        EventCollector* collector = GetEventCollector(StreamType::DEVICE);
        REQUIRE(collector != nullptr);

        SUBCASE("TableInfoPacket creation") {
            TableInfoPacket packet = TableInfoPacket::Create("Attack from Mars", "afm_113b");

            CHECK(packet.header.magic == MAGIC_TABLE_INFO);
            CHECK(packet.header.tableNameLength == 16);  // strlen("Attack from Mars")
            CHECK(packet.header.romNameLength == 8);      // strlen("afm_113b")
            CHECK(std::string(packet.tableName) == "Attack from Mars");
            CHECK(std::string(packet.romName) == "afm_113b");

            // Verify packet size calculation
            size_t expectedSize = sizeof(TableInfoHeader) + 16 + 8;  // header + tableName + romName
            CHECK(packet.GetPacketSize() == expectedSize);
        }

        SUBCASE("TableInfo submission and queuing") {
            // Submit table info
            collector->TableInfo("Medieval Madness", "mm_109c");

            // Check queue is not empty
            CHECK(!collector->IsTableInfoQueueEmpty());
            CHECK(collector->GetTableInfoQueueSize() > 0);

            // Pop the packet
            TableInfoPacket packet;
            CHECK(collector->PopTableInfo(packet));

            // Verify packet contents
            CHECK(std::string(packet.tableName) == "Medieval Madness");
            CHECK(std::string(packet.romName) == "mm_109c");

            // Queue should now be empty
            CHECK(collector->IsTableInfoQueueEmpty());
        }

        SUBCASE("Long table names truncation") {
            // Create very long table name (over 256 chars)
            std::string longName(300, 'X');
            std::string longRom(100, 'Y');

            collector->TableInfo(longName.c_str(), longRom.c_str());

            TableInfoPacket packet;
            CHECK(collector->PopTableInfo(packet));

            // Verify truncation to max lengths
            CHECK(packet.header.tableNameLength <= MAX_TABLE_NAME_LENGTH - 1);
            CHECK(packet.header.romNameLength <= MAX_ROM_NAME_LENGTH - 1);
        }

        SUBCASE("Empty and null strings") {
            collector->TableInfo("", "");

            TableInfoPacket packet;
            CHECK(collector->PopTableInfo(packet));
            CHECK(packet.header.tableNameLength == 0);
            CHECK(packet.header.romNameLength == 0);

            // Test null pointers
            collector->TableInfo(nullptr, nullptr);
            CHECK(collector->PopTableInfo(packet));
            CHECK(packet.header.tableNameLength == 0);
            CHECK(packet.header.romNameLength == 0);
        }

        SUBCASE("TableInfo statistics") {
            ResetStreamStatistics(StreamType::DEVICE);

            collector->TableInfo("The Addams Family", "taf_l7");

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            Statistics stats = GetStreamStatistics(StreamType::DEVICE);
            CHECK(stats.eventsSent >= 1);
        }

        ShutdownAllStreams();
    }
}
