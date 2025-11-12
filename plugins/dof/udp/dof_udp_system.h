// license:GPLv3+

#pragma once

#include "dof_udp_broadcaster.h"
#include "dof_event_collector.h"

namespace DOFUDP {

///////////////////////////////////////////////////////////////////////////////
// DOF UDP Broadcasting System
//
// High-level API for initializing and using the UDP broadcasting system.
// This is the main interface used by DOFPlugin.
//
// Usage:
//   1. Configure system with BroadcasterConfig
//   2. Call InitializeDOFUDPBroadcaster()
//   3. Get EventCollector and submit events during gameplay
//   4. Call ShutdownDOFUDPBroadcaster() when done
//
// Example:
//   BroadcasterConfig config;
//   config.enabled = true;
//   config.address = "255.255.255.255";
//   config.port = 7778;
//
//   if (InitializeDOFUDPBroadcaster(config)) {
//       EventCollector* collector = GetDOFEventCollector();
//       collector->Solenoid(1, 255);
//       // ... more events ...
//       ShutdownDOFUDPBroadcaster();
//   }
///////////////////////////////////////////////////////////////////////////////

// Initialize the UDP broadcasting system
// Returns true on success, false on error
// Must be called before using GetDOFEventCollector()
bool InitializeDOFUDPBroadcaster(const BroadcasterConfig& config);

// Shutdown the UDP broadcasting system
// Stops the broadcaster thread and frees resources
// Safe to call even if not initialized
void ShutdownDOFUDPBroadcaster();

// Get the event collector for submitting events
// Returns nullptr if system is not initialized
// The returned pointer is valid until ShutdownDOFUDPBroadcaster() is called
EventCollector* GetDOFEventCollector();

// Check if the system is initialized and running
bool IsDOFUDPBroadcasterRunning();

// Get current statistics
// Returns empty statistics if system is not initialized
Statistics GetDOFUDPStatistics();

// Reset statistics counters
void ResetDOFUDPStatistics();

} // namespace DOFUDP
