// license:GPLv3+

#include "broadcaster.h"

#include <cstring>

// Platform-specific networking headers
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
    #define CLOSE_SOCKET closesocket
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>

    #define INVALID_SOCKET_VALUE -1
    #define SOCKET_ERROR_VALUE -1
    #define CLOSE_SOCKET close
#endif

namespace UDPBroadcast {

UDPBroadcaster::UDPBroadcaster()
    : m_collector(nullptr)
    , m_socket(INVALID_SOCKET_VALUE)
    , m_running(false)
    , m_stopRequested(false)
    , m_packetsSent(0)
    , m_sendErrors(0)
    , m_packetsThisSecond(0)
{
}

UDPBroadcaster::~UDPBroadcaster()
{
    Stop();
}

bool UDPBroadcaster::Start(const BroadcasterConfig& config, EventCollector* collector)
{
    if (m_running.load(std::memory_order_acquire))
        return false; // Already running

    if (!collector)
        return false; // Invalid collector

    m_config = config;
    m_collector = collector;

    // Initialize network stack
    if (!InitNetwork())
        return false;

    // Create UDP socket
    if (!CreateSocket()) {
        CleanupNetwork();
        return false;
    }

    // Start broadcaster thread
    m_stopRequested.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&UDPBroadcaster::BroadcasterThread, this);

    return true;
}

void UDPBroadcaster::Stop()
{
    if (!m_running.load(std::memory_order_acquire))
        return; // Not running

    // Signal thread to stop
    m_stopRequested.store(true, std::memory_order_release);

    // Wait for thread to finish
    if (m_thread.joinable())
        m_thread.join();

    // Cleanup
    CloseSocket();
    CleanupNetwork();

    m_running.store(false, std::memory_order_release);
}

bool UDPBroadcaster::IsRunning() const
{
    return m_running.load(std::memory_order_acquire);
}

Statistics UDPBroadcaster::GetStatistics() const
{
    Statistics stats = m_collector->GetStatistics();
    stats.packetsSent = m_packetsSent.load(std::memory_order_relaxed);
    stats.sendErrors = m_sendErrors.load(std::memory_order_relaxed);
    return stats;
}

void UDPBroadcaster::ResetStatistics()
{
    m_collector->ResetStatistics();
    m_packetsSent.store(0, std::memory_order_relaxed);
    m_sendErrors.store(0, std::memory_order_relaxed);
}

///////////////////////////////////////////////////////////////////////////////
// Network Operations
///////////////////////////////////////////////////////////////////////////////

bool UDPBroadcaster::InitNetwork()
{
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    return (result == 0);
#else
    // No initialization needed on POSIX systems
    return true;
#endif
}

void UDPBroadcaster::CleanupNetwork()
{
#ifdef _WIN32
    WSACleanup();
#else
    // No cleanup needed on POSIX systems
#endif
}

bool UDPBroadcaster::CreateSocket()
{
    // Create UDP socket
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        return false;
    m_socket = static_cast<uintptr_t>(sock);
#else
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return false;
    m_socket = sock;
#endif

    // Enable broadcast
    int broadcastEnable = 1;
#ifdef _WIN32
    if (setsockopt((SOCKET)m_socket, SOL_SOCKET, SO_BROADCAST,
                   (const char*)&broadcastEnable, sizeof(broadcastEnable)) == SOCKET_ERROR) {
        CLOSE_SOCKET((SOCKET)m_socket);
        m_socket = INVALID_SOCKET_VALUE;
        return false;
    }
#else
    if (setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST,
                   &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        CLOSE_SOCKET(m_socket);
        m_socket = INVALID_SOCKET_VALUE;
        return false;
    }
#endif

    // Set non-blocking mode (optional, for robustness)
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket((SOCKET)m_socket, FIONBIO, &mode) == SOCKET_ERROR) {
        // Non-critical, continue anyway
    }
#else
    int flags = fcntl(m_socket, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
    }
#endif

    return true;
}

void UDPBroadcaster::CloseSocket()
{
    if (m_socket != INVALID_SOCKET_VALUE) {
#ifdef _WIN32
        CLOSE_SOCKET((SOCKET)m_socket);
#else
        CLOSE_SOCKET(m_socket);
#endif
        m_socket = INVALID_SOCKET_VALUE;
    }
}

bool UDPBroadcaster::SendBatchPacket(const BatchPacket& packet)
{
    if (m_socket == INVALID_SOCKET_VALUE)
        return false;

    if (packet.header.eventCount == 0)
        return true; // Nothing to send

    // Prepare destination address
    struct sockaddr_in destAddr;
    memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(m_config.port);

    // Parse IP address
#ifdef _WIN32
    destAddr.sin_addr.s_addr = inet_addr(m_config.address.c_str());
    if (destAddr.sin_addr.s_addr == INADDR_NONE) {
        // Try inet_pton for IPv6/better parsing
        if (inet_pton(AF_INET, m_config.address.c_str(), &destAddr.sin_addr) != 1) {
            return false;
        }
    }
#else
    if (inet_pton(AF_INET, m_config.address.c_str(), &destAddr.sin_addr) != 1) {
        return false;
    }
#endif

    // Send packet
    size_t packetSize = packet.GetPacketSize();
#ifdef _WIN32
    int result = sendto((SOCKET)m_socket, (const char*)&packet, (int)packetSize, 0,
                        (struct sockaddr*)&destAddr, sizeof(destAddr));
    if (result == SOCKET_ERROR_VALUE) {
        m_sendErrors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
#else
    ssize_t result = sendto(m_socket, &packet, packetSize, 0,
                           (struct sockaddr*)&destAddr, sizeof(destAddr));
    if (result < 0) {
        m_sendErrors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
#endif

    m_packetsSent.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool UDPBroadcaster::SendSegmentDisplayPacket(const SegmentDisplayPacket& packet)
{
    if (m_socket == INVALID_SOCKET_VALUE)
        return false;

    // Prepare destination address
    struct sockaddr_in destAddr;
    memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(m_config.port);

    // Parse IP address
#ifdef _WIN32
    destAddr.sin_addr.s_addr = inet_addr(m_config.address.c_str());
    if (destAddr.sin_addr.s_addr == INADDR_NONE) {
        if (inet_pton(AF_INET, m_config.address.c_str(), &destAddr.sin_addr) != 1) {
            return false;
        }
    }
#else
    if (inet_pton(AF_INET, m_config.address.c_str(), &destAddr.sin_addr) != 1) {
        return false;
    }
#endif

    // Send packet
    size_t packetSize = packet.GetPacketSize();
#ifdef _WIN32
    int result = sendto((SOCKET)m_socket, (const char*)&packet, (int)packetSize, 0,
                        (struct sockaddr*)&destAddr, sizeof(destAddr));
    if (result == SOCKET_ERROR_VALUE) {
        m_sendErrors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
#else
    ssize_t result = sendto(m_socket, &packet, packetSize, 0,
                           (struct sockaddr*)&destAddr, sizeof(destAddr));
    if (result < 0) {
        m_sendErrors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
#endif

    m_packetsSent.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool UDPBroadcaster::SendTableInfoPacket(const TableInfoPacket& packet)
{
    if (m_socket == INVALID_SOCKET_VALUE)
        return false;

    // Prepare destination address
    struct sockaddr_in destAddr;
    memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(m_config.port);

    // Parse IP address
#ifdef _WIN32
    destAddr.sin_addr.s_addr = inet_addr(m_config.address.c_str());
    if (destAddr.sin_addr.s_addr == INADDR_NONE) {
        if (inet_pton(AF_INET, m_config.address.c_str(), &destAddr.sin_addr) != 1) {
            return false;
        }
    }
#else
    if (inet_pton(AF_INET, m_config.address.c_str(), &destAddr.sin_addr) != 1) {
        return false;
    }
#endif

    // Send packet (only actual data, not full buffer)
    size_t packetSize = packet.GetPacketSize();
#ifdef _WIN32
    int result = sendto((SOCKET)m_socket, (const char*)&packet, (int)packetSize, 0,
                        (struct sockaddr*)&destAddr, sizeof(destAddr));
    if (result == SOCKET_ERROR_VALUE) {
        m_sendErrors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
#else
    ssize_t result = sendto(m_socket, &packet, packetSize, 0,
                           (struct sockaddr*)&destAddr, sizeof(destAddr));
    if (result < 0) {
        m_sendErrors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
#endif

    m_packetsSent.fetch_add(1, std::memory_order_relaxed);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Broadcaster Thread
///////////////////////////////////////////////////////////////////////////////

void UDPBroadcaster::BroadcasterThread()
{
    // Initialize rate limiting
    m_lastSendTime = std::chrono::high_resolution_clock::now();
    m_packetsThisSecond = 0;

    while (!m_stopRequested.load(std::memory_order_acquire)) {
        ProcessEvents();

        // Sleep briefly to avoid busy-waiting
        // Adjust based on expected event rate
        std::this_thread::sleep_for(std::chrono::microseconds(1000)); // 1ms
    }
}

void UDPBroadcaster::ProcessEvents()
{
    // Check rate limiting
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSendTime);

    if (elapsed.count() >= 1000) {
        // Reset counter every second
        m_packetsThisSecond = 0;
        m_lastSendTime = now;
    }

    // Check if we can send more packets this second
    if (m_config.maxPacketsPerSecond > 0 &&
        m_packetsThisSecond >= m_config.maxPacketsPerSecond) {
        // Rate limit reached, wait
        return;
    }

    // Check for table info packets first (highest priority, rarest - once per game)
    TableInfoPacket tableInfoPacket;
    if (m_collector->PopTableInfo(tableInfoPacket)) {
        if (SendTableInfoPacket(tableInfoPacket)) {
            m_packetsThisSecond++;
        }
        return; // Process one packet per iteration
    }

    // Second, check for segment display packets (high priority, less frequent)
    SegmentDisplayPacket segmentPacket;
    if (m_collector->PopSegmentDisplay(segmentPacket)) {
        if (SendSegmentDisplayPacket(segmentPacket)) {
            m_packetsThisSecond++;
        }
        return; // Process one packet per iteration
    }

    // Try to fill a batch packet from the regular event queue
    m_batchPacket.Reset();

    Event events[MAX_BATCH_EVENTS];
    size_t eventCount = m_collector->PopBatch(events, MAX_BATCH_EVENTS);

    if (eventCount == 0)
        return; // Both queues are empty

    // Add events to batch
    for (size_t i = 0; i < eventCount; i++) {
        m_batchPacket.AddEvent(events[i]);
    }

    // Send batch packet
    if (SendBatchPacket(m_batchPacket)) {
        m_packetsThisSecond++;
    }
}

} // namespace UDPBroadcast
