// license:GPLv3+

#pragma once

#include "broadcaster.h"
#include "event_collector.h"

namespace UDPBroadcast {

///////////////////////////////////////////////////////////////////////////////
// DOF UDP Broadcasting System - Multi-Stream Architecture
//
// High-level API for initializing and using multiple independent UDP streams.
// Each stream type has its own broadcaster, thread, socket, and configuration.
//
// Stream Types:
//   DEVICE - Solenoid, Lamp, GI, Wire events (default port 7778)
//   RGB    - RGB LED events (default port 7779)
//   DMD    - Display matrix frames (default port 7780) - future
//   SCORE  - Segment/score displays (default port 7781) - future
//   AUDIO  - Audio streams (default port 7782) - future
//
// Usage:
//   BroadcasterConfig deviceConfig;
//   deviceConfig.enabled = true;
//   deviceConfig.address = "255.255.255.255";
//   deviceConfig.port = 7778;
//
//   BroadcasterConfig rgbConfig;
//   rgbConfig.enabled = true;
//   rgbConfig.port = 7779;
//
//   InitializeStream(StreamType::DEVICE, deviceConfig);
//   InitializeStream(StreamType::RGB, rgbConfig);
//
//   EventCollector* deviceCollector = GetEventCollector(StreamType::DEVICE);
//   EventCollector* rgbCollector = GetEventCollector(StreamType::RGB);
//
//   deviceCollector->Solenoid(1, 255);
//   rgbCollector->RGB(1, 255, 0, 0);
//
//   ShutdownStream(StreamType::DEVICE);
//   ShutdownStream(StreamType::RGB);
///////////////////////////////////////////////////////////////////////////////

// Stream types for independent UDP broadcasting
enum class StreamType {
    DEVICE,  // Solenoid, Lamp, GI, Wire events
    RGB,     // RGB LED events
    DMD,     // Display matrix frames (future)
    SCORE,   // Segment/score display events (future)
    AUDIO    // Audio stream events (future)
};

///////////////////////////////////////////////////////////////////////////////
// Multi-Stream API
///////////////////////////////////////////////////////////////////////////////

// Initialize a specific stream
// Returns true on success, false on error
// Can be called multiple times for different stream types
bool InitializeStream(StreamType type, const BroadcasterConfig& config);

// Shutdown a specific stream
// Stops the broadcaster thread and frees resources
// Safe to call even if stream is not initialized
void ShutdownStream(StreamType type);

// Shutdown all active streams
// Convenience function to shutdown everything
void ShutdownAllStreams();

// Get the event collector for a specific stream
// Returns nullptr if stream is not initialized
// The returned pointer is valid until ShutdownStream() is called
EventCollector* GetEventCollector(StreamType type);

// Check if a specific stream is initialized and running
bool IsStreamRunning(StreamType type);

// Get current statistics for a specific stream
// Returns empty statistics if stream is not initialized
Statistics GetStreamStatistics(StreamType type);

// Reset statistics counters for a specific stream
void ResetStreamStatistics(StreamType type);

} // namespace UDPBroadcast
