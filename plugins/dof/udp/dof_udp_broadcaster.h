// license:GPLv3+

#pragma once

#include "dof_udp_event.h"
#include "dof_event_collector.h"

#include <string>
#include <thread>
#include <atomic>
#include <chrono>

namespace DOFUDP {

///////////////////////////////////////////////////////////////////////////////
// UDP Broadcaster Configuration
///////////////////////////////////////////////////////////////////////////////

struct BroadcasterConfig {
    bool enabled;                   // Enable/disable broadcasting
    std::string address;            // Target IP address (supports broadcast)
    uint16_t port;                  // Target UDP port
    uint32_t maxPacketsPerSecond;   // Rate limiting (0 = unlimited)
    uint32_t queueSize;             // Event queue size (must be power of 2)

    BroadcasterConfig()
        : enabled(true)
        , address("255.255.255.255") // Broadcast by default
        , port(7778)
        , maxPacketsPerSecond(120)   // 120 packets/sec = ~8ms interval
        , queueSize(DEFAULT_QUEUE_SIZE)
    {}
};

///////////////////////////////////////////////////////////////////////////////
// UDP Broadcaster
//
// Background thread that reads events from the collector queue and
// broadcasts them via UDP.
//
// Features:
// - Batches multiple events per packet for efficiency
// - Rate limiting to prevent network flooding
// - Non-blocking: network errors don't affect game thread
// - Cross-platform (Windows, Linux, macOS)
//
// Thread Safety:
// - Start/Stop can be called from any thread
// - Internal processing runs on dedicated thread
///////////////////////////////////////////////////////////////////////////////

class UDPBroadcaster {
public:
    UDPBroadcaster();
    ~UDPBroadcaster();

    // Disable copy/move
    UDPBroadcaster(const UDPBroadcaster&) = delete;
    UDPBroadcaster& operator=(const UDPBroadcaster&) = delete;

    ///////////////////////////////////////////////////////////////////////////
    // Lifecycle Management
    ///////////////////////////////////////////////////////////////////////////

    // Start the broadcaster with the given configuration and event collector
    // Returns true on success, false on error
    bool Start(const BroadcasterConfig& config, EventCollector* collector);

    // Stop the broadcaster
    // Waits for thread to finish (may take up to 100ms)
    void Stop();

    // Check if broadcaster is running
    bool IsRunning() const;

    ///////////////////////////////////////////////////////////////////////////
    // Statistics
    ///////////////////////////////////////////////////////////////////////////

    // Get current statistics
    Statistics GetStatistics() const;

    // Reset statistics counters
    void ResetStatistics();

private:
    ///////////////////////////////////////////////////////////////////////////
    // Network Operations
    ///////////////////////////////////////////////////////////////////////////

    // Initialize network stack (platform-specific)
    bool InitNetwork();

    // Cleanup network stack (platform-specific)
    void CleanupNetwork();

    // Create and configure UDP socket
    bool CreateSocket();

    // Close UDP socket
    void CloseSocket();

    // Send a batch packet
    // Returns true on success, false on error
    bool SendBatchPacket(const BatchPacket& packet);

    // Send a segment display packet
    // Returns true on success, false on error
    bool SendSegmentDisplayPacket(const SegmentDisplayPacket& packet);

    ///////////////////////////////////////////////////////////////////////////
    // Broadcaster Thread
    ///////////////////////////////////////////////////////////////////////////

    // Main broadcaster thread function
    void BroadcasterThread();

    // Process events from queue
    void ProcessEvents();

    ///////////////////////////////////////////////////////////////////////////
    // Member Variables
    ///////////////////////////////////////////////////////////////////////////

    // Configuration
    BroadcasterConfig m_config;

    // Event collector (owned by system, not by broadcaster)
    EventCollector* m_collector;

    // Network socket (platform-specific handle)
#ifdef _WIN32
    uintptr_t m_socket; // SOCKET type
#else
    int m_socket;
#endif

    // Thread control
    std::thread m_thread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_stopRequested;

    // Statistics
    std::atomic<uint64_t> m_packetsSent;
    std::atomic<uint64_t> m_sendErrors;

    // Batch packet (reused for efficiency)
    BatchPacket m_batchPacket;

    // Rate limiting
    std::chrono::high_resolution_clock::time_point m_lastSendTime;
    uint32_t m_packetsThisSecond;
};

} // namespace DOFUDP
