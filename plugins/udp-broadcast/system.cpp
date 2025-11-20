// license:GPLv3+

#include "system.h"

#include <mutex>
#include <map>

namespace UDPBroadcast {

///////////////////////////////////////////////////////////////////////////////
// Multi-Stream State
///////////////////////////////////////////////////////////////////////////////

namespace {
    // Stream instance holds all resources for one UDP stream
    struct StreamInstance {
        EventCollector* collector = nullptr;
        UDPBroadcaster* broadcaster = nullptr;
        bool initialized = false;
    };

    // Global map of active streams
    std::map<StreamType, StreamInstance> g_streams;
    std::mutex g_mutex;
}

///////////////////////////////////////////////////////////////////////////////
// Multi-Stream API Implementation
///////////////////////////////////////////////////////////////////////////////

bool InitializeStream(StreamType type, const BroadcasterConfig& config)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    // Check if stream is already initialized
    auto it = g_streams.find(type);
    if (it != g_streams.end() && it->second.initialized) {
        return false; // Already initialized
    }

    // Check if broadcasting is enabled
    if (!config.enabled) {
        return false;
    }

    // Create or get stream instance
    StreamInstance& stream = g_streams[type];

    // Create event collector
    stream.collector = new EventCollector(config.queueSize);
    if (!stream.collector) {
        return false;
    }

    // Create broadcaster
    stream.broadcaster = new UDPBroadcaster();
    if (!stream.broadcaster) {
        delete stream.collector;
        stream.collector = nullptr;
        return false;
    }

    // Start broadcaster
    if (!stream.broadcaster->Start(config, stream.collector)) {
        delete stream.broadcaster;
        delete stream.collector;
        stream.broadcaster = nullptr;
        stream.collector = nullptr;
        return false;
    }

    stream.initialized = true;
    return true;
}

void ShutdownStream(StreamType type)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    auto it = g_streams.find(type);
    if (it == g_streams.end() || !it->second.initialized) {
        return; // Not initialized
    }

    StreamInstance& stream = it->second;

    // Stop broadcaster (this waits for thread to finish)
    if (stream.broadcaster) {
        stream.broadcaster->Stop();
        delete stream.broadcaster;
        stream.broadcaster = nullptr;
    }

    // Delete collector
    if (stream.collector) {
        delete stream.collector;
        stream.collector = nullptr;
    }

    stream.initialized = false;
    g_streams.erase(it);
}

void ShutdownAllStreams()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    for (auto& pair : g_streams) {
        StreamInstance& stream = pair.second;

        if (stream.initialized) {
            // Stop broadcaster
            if (stream.broadcaster) {
                stream.broadcaster->Stop();
                delete stream.broadcaster;
                stream.broadcaster = nullptr;
            }

            // Delete collector
            if (stream.collector) {
                delete stream.collector;
                stream.collector = nullptr;
            }

            stream.initialized = false;
        }
    }

    g_streams.clear();
}

EventCollector* GetEventCollector(StreamType type)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    auto it = g_streams.find(type);
    if (it == g_streams.end() || !it->second.initialized) {
        return nullptr;
    }

    return it->second.collector;
}

bool IsStreamRunning(StreamType type)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    auto it = g_streams.find(type);
    if (it == g_streams.end() || !it->second.initialized) {
        return false;
    }

    const StreamInstance& stream = it->second;
    return stream.broadcaster && stream.broadcaster->IsRunning();
}

Statistics GetStreamStatistics(StreamType type)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    auto it = g_streams.find(type);
    if (it == g_streams.end() || !it->second.initialized) {
        return Statistics();
    }

    const StreamInstance& stream = it->second;
    if (stream.broadcaster) {
        return stream.broadcaster->GetStatistics();
    }

    return Statistics();
}

void ResetStreamStatistics(StreamType type)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    auto it = g_streams.find(type);
    if (it == g_streams.end() || !it->second.initialized) {
        return;
    }

    StreamInstance& stream = it->second;
    if (stream.broadcaster) {
        stream.broadcaster->ResetStatistics();
    }
}

} // namespace UDPBroadcast
