// license:GPLv3+

#include "dof_udp_system.h"

#include <mutex>

namespace DOFUDP {

///////////////////////////////////////////////////////////////////////////////
// Global State
///////////////////////////////////////////////////////////////////////////////

namespace {
    // Global instances (initialized on first use, destroyed at shutdown)
    EventCollector* g_collector = nullptr;
    UDPBroadcaster* g_broadcaster = nullptr;
    std::mutex g_mutex;
    bool g_initialized = false;
}

///////////////////////////////////////////////////////////////////////////////
// Public API Implementation
///////////////////////////////////////////////////////////////////////////////

bool InitializeDOFUDPBroadcaster(const BroadcasterConfig& config)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    // Already initialized?
    if (g_initialized)
        return false;

    // Check if broadcasting is enabled
    if (!config.enabled)
        return false;

    // Create event collector
    g_collector = new EventCollector(config.queueSize);
    if (!g_collector)
        return false;

    // Create broadcaster
    g_broadcaster = new UDPBroadcaster();
    if (!g_broadcaster) {
        delete g_collector;
        g_collector = nullptr;
        return false;
    }

    // Start broadcaster
    if (!g_broadcaster->Start(config, g_collector)) {
        delete g_broadcaster;
        delete g_collector;
        g_broadcaster = nullptr;
        g_collector = nullptr;
        return false;
    }

    g_initialized = true;
    return true;
}

void ShutdownDOFUDPBroadcaster()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_initialized)
        return;

    // Stop broadcaster (this waits for thread to finish)
    if (g_broadcaster) {
        g_broadcaster->Stop();
        delete g_broadcaster;
        g_broadcaster = nullptr;
    }

    // Delete collector
    if (g_collector) {
        delete g_collector;
        g_collector = nullptr;
    }

    g_initialized = false;
}

EventCollector* GetDOFEventCollector()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_collector;
}

bool IsDOFUDPBroadcasterRunning()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_initialized && g_broadcaster && g_broadcaster->IsRunning();
}

Statistics GetDOFUDPStatistics()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_initialized && g_broadcaster) {
        return g_broadcaster->GetStatistics();
    }

    return Statistics();
}

void ResetDOFUDPStatistics()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_initialized && g_broadcaster) {
        g_broadcaster->ResetStatistics();
    }
}

} // namespace DOFUDP
