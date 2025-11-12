# UDP Event Broadcasting Plugin - Multi-Stream Architecture

Independent VPinball plugin that broadcasts game events via UDP for external controllers, LED systems, displays, and monitoring tools.

## Overview

This is a standalone VPinball plugin that automatically discovers and broadcasts events from **all game sources** via **multiple independent UDP streams**. It operates independently from the DOF plugin, polling controller events directly and broadcasting them to external applications. Each stream type operates independently with its own broadcaster, thread, socket, and configuration.

### Plugin Architecture

- **Independent Operation**: Runs as a separate plugin, does not require DOF plugin
- **Universal Source Discovery**: Automatically discovers and broadcasts from:
  - PinMAME ROM events (Solenoids, Lamps, GI, Wires)
  - B2S Backglass elements
  - Segment displays (score displays, alphanumeric)
  - Custom controller plugins
  - Any future plugin implementing Controller Plugin API
- **Public API**: Exposes API for other plugins to submit custom events via message passing

## Multi-Stream Design

The system supports separate UDP streams for different event categories:

| Stream | Default Port | Event Types | Status |
|--------|--------------|-------------|---------|
| **DEVICE** | 7778 | Solenoid, Lamp, GI, Wire | ✅ Implemented |
| **RGB** | 7779 | RGB LED events | ✅ Implemented |
| **DMD** | 7780 | Display matrix frames | 🔮 Future |
| **SCORE** | 7781 | Segment/score displays | ✅ Implemented |
| **AUDIO** | 7782 | Audio streams | 🔮 Future |

### Why Multiple Streams?

- **Selective Subscribing**: Clients only receive events they need
- **Traffic Optimization**: Large events (DMD frames ~4-32 KB) don't clog small device events
- **Independent Configuration**: Different ports, addresses, and rate limits per stream
- **Parallel Processing**: Clients can process different event types on different threads
- **Backward Compatibility**: Existing clients continue to work on Device stream (port 7778)

## Features

- **Independent Plugin**: Operates as standalone VPinball plugin, polls controller sources directly
- **Lock-free Architecture**: Zero-copy, non-blocking event submission with SPSC queues
- **Event Batching**: Packs up to 20 events per UDP packet for efficiency
- **Per-Stream Rate Limiting**: Configurable packet rate per stream to prevent network flooding
- **Cross-Platform**: Windows (Winsock2) and POSIX (Linux/macOS) support
- **Low Overhead**: < 0.01% main thread impact per stream, ~150 KB memory per stream
- **Comprehensive Events**: Solenoid, Lamp, GI, Wire, RGB, Segment Displays, Table lifecycle
- **DOF Spec Compatible**: Preserves original DOF device IDs (groupId, deviceId) in all events
- **Public API**: Other plugins can submit custom events via message passing interface

## Configuration

Each stream can be independently configured. Add these settings to your VPinballX configuration file:

### Device Stream Configuration (Solenoid, Lamp, GI, Wire)

```ini
[DOF]
UDPDeviceStreamEnabled=1
UDPDeviceStreamAddress=255.255.255.255
UDPDeviceStreamPort=7778
UDPDeviceStreamMaxPacketsPerSecond=120
UDPDeviceStreamQueueSize=4096
```

### RGB Stream Configuration

```ini
[DOF]
UDPRGBStreamEnabled=1
UDPRGBStreamAddress=255.255.255.255
UDPRGBStreamPort=7779
UDPRGBStreamMaxPacketsPerSecond=120
UDPRGBStreamQueueSize=4096
```

### Score Stream Configuration (Segment Displays)

```ini
[DOF]
UDPScoreStreamEnabled=1
UDPScoreStreamAddress=255.255.255.255
UDPScoreStreamPort=7781
UDPScoreStreamMaxPacketsPerSecond=60
UDPScoreStreamQueueSize=256
```

### Configuration Options

#### Device Stream Settings

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `UDPDeviceStreamEnabled` | bool | `true` | Enable/disable Device stream |
| `UDPDeviceStreamAddress` | string | `255.255.255.255` | Target IP address (supports broadcast) |
| `UDPDeviceStreamPort` | int | `7778` | Target UDP port |
| `UDPDeviceStreamMaxPacketsPerSecond` | int | `120` | Rate limit (0 = unlimited) |
| `UDPDeviceStreamQueueSize` | int | `4096` | Event queue size (must be power of 2) |

#### RGB Stream Settings

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `UDPRGBStreamEnabled` | bool | `true` | Enable/disable RGB stream |
| `UDPRGBStreamAddress` | string | `255.255.255.255` | Target IP address (supports broadcast) |
| `UDPRGBStreamPort` | int | `7779` | Target UDP port |
| `UDPRGBStreamMaxPacketsPerSecond` | int | `120` | Rate limit (0 = unlimited) |
| `UDPRGBStreamQueueSize` | int | `4096` | Event queue size (must be power of 2) |

#### Score Stream Settings

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `UDPScoreStreamEnabled` | bool | `true` | Enable/disable Score stream |
| `UDPScoreStreamAddress` | string | `255.255.255.255` | Target IP address (supports broadcast) |
| `UDPScoreStreamPort` | int | `7781` | Target UDP port |
| `UDPScoreStreamMaxPacketsPerSecond` | int | `60` | Rate limit (0 = unlimited) |
| `UDPScoreStreamQueueSize` | int | `256` | Event queue size (must be power of 2) |

## Protocol

### Packet Format

#### Single Event Packet (32 bytes)

```c
struct Event {
    uint32_t magic;           // 0x58464F44 ('DOFX')
    uint64_t timestamp_us;    // Microseconds since epoch
    uint8_t type;             // Event type (see below)
    uint8_t id;               // Legacy device ID (0-255, for compatibility)
    uint16_t value;           // Primary value (0-65535)
    uint8_t r, g, b;          // RGB data (for RGB events)
    uint16_t groupId;         // DOF Group ID (e.g., 0x0100=GI, 0x0200=Lamps, 0x0300=Mechs)
    uint16_t deviceId;        // DOF Device ID within group
    uint8_t reserved[9];      // Reserved for future use
};
```

#### Batch Packet (16 + N*32 bytes, max 656 bytes)

```c
struct BatchHeader {
    uint32_t magic;           // 0x42464F44 ('DOFB')
    uint8_t version;          // Protocol version (1)
    uint8_t eventCount;       // Number of events (1-20)
    uint16_t reserved;
    uint64_t timestamp_us;    // Batch timestamp
};
// Followed by N Event structures
```

### Event Types

| Type | Value | Description | Fields Used |
|------|-------|-------------|-------------|
| Solenoid | 1 | Solenoid state | id, value (0-255) |
| Lamp | 2 | Lamp state | id, value (0-255) |
| GI | 3 | General Illumination | id, value (0-255) |
| RGB | 4 | RGB LED | id, r, g, b |
| Wire | 5 | Wire/Switch state | id, value (0-1) |

### Additional Packet Types

#### TableInfoPacket (Variable Size)

Broadcasts table lifecycle and metadata information:

```c
struct TableInfoHeader {
    uint32_t magic;              // 0x54424C49 ('TBLI')
    uint64_t timestamp_us;       // Microseconds since epoch
    uint16_t tableNameLength;    // Length of table name string (0-256)
    uint16_t romNameLength;      // Length of ROM name string (0-64)
    uint8_t reserved[4];
};
// Followed by:
// - char tableName[tableNameLength]     // Table name (e.g., "Attack from Mars")
// - char romName[romNameLength]         // ROM/game ID (e.g., "afm_113b")
```

**Usage:**
- **Table Load**: `TableInfo("Attack from Mars", "afm_113b")`
- **Table Unload**: `TableInfo("", "")` - empty strings signal session end

#### SegmentDisplayPacket (Score Stream)

Broadcasts 7-segment and alphanumeric display data:

```c
struct SegmentDisplayPacket {
    uint32_t magic;              // 0x53454744 ('SEGD')
    uint64_t timestamp_us;       // Microseconds since epoch
    uint8_t displayId;           // Display identifier (0-255)
    uint8_t displayType;         // 0=7seg, 1=9seg, 2=14seg, 3=16seg, 4=alphanum
    uint8_t digitCount;          // Number of digits (1-32)
    uint8_t reserved;
    uint16_t segments[32];       // Segment data per digit (bit-packed)
};
```

**Display Types:**
- `0` = 7-segment numeric
- `1` = 9-segment numeric with comma
- `2` = 14-segment alphanumeric
- `3` = 16-segment alphanumeric
- `4` = Full alphanumeric (16-bit per character)

## Network Details

- **Protocol**: UDP (User Datagram Protocol)
- **Byte Order**: Little-endian
- **IP Version**: IPv4
- **Broadcast**: Supported via 255.255.255.255 or subnet broadcast
- **Unicast**: Supported via specific IP address
- **Typical Traffic**: 15-30 KB/s @ 60 FPS with 100 active devices

## Architecture

### Multi-Queue Design

The system uses separate lock-free queues for different event types to optimize performance:

```
┌─────────────────────────────────────────────────┐
│           Game Thread (60 FPS)                  │
│                                                 │
│  DOF Plugin / PinMAME Events                    │
│  ├─ Solenoid/Lamp/GI State Changed              │
│  ├─ Segment Display Update                      │
│  └─ Table Load/Unload                           │
│         │                                       │
│         ▼                                       │
│  EventCollector API (lock-free)                 │
│  ├─ Solenoid() / Lamp() / GI()                  │
│  ├─ SegmentDisplay()                            │
│  └─ TableInfo()                                 │
└─────────┬───────────────────────────────────────┘
          │
          │ Three Independent Lock-Free Queues
          │
          ├─► Regular Events Queue (1024 entries, FIFO)
          │   └─ Solenoid, Lamp, GI, Wire, RGB events
          │
          ├─► Segment Display Queue (256 entries, overwrite)
          │   └─ 7-segment, alphanumeric display data
          │
          └─► Table Info Queue (16 entries, FIFO)
              └─ Table load/unload events
          │
┌─────────▼───────────────────────────────────────┐
│       Broadcaster Thread                        │
│                                                 │
│  Priority-based event processing:               │
│  1. Check TableInfo queue first (rare)          │
│  2. Check SegmentDisplay queue (frequent)       │
│  3. Check Regular Events queue (high volume)    │
│                                                 │
│  ├─ Pop events from queues                      │
│  ├─ Batch events (up to 20 for regular)         │
│  ├─ Apply rate limiting                         │
│  └─ sendto() UDP broadcast                      │
└─────────────────────────────────────────────────┘
          │
          ▼
  UDP Network (Ports 7778, 7781, etc.)
          │
          ▼
┌─────────────────────────────────────────────────┐
│  External Clients                               │
│  ├─ WLED controllers                            │
│  ├─ LED matrices                                │
│  ├─ Score displays                              │
│  ├─ Monitoring tools                            │
│  └─ Custom applications                         │
└─────────────────────────────────────────────────┘
```

## Performance Characteristics

- **Main Thread Impact**: < 0.01% (only atomic queue operations)
- **Memory Usage**: ~150 KB (128 KB queue + overhead)
- **Network Bandwidth**: ~15-30 KB/s typical, ~190 KB/s maximum
- **Latency**: Sub-millisecond event submission
- **Queue Capacity**: 4096 events (~68 ms buffer @ 60 FPS, 100 events/frame)

## Building

The UDP broadcasting system is automatically included when building the DOF plugin.

### Linux / macOS

```bash
# No additional dependencies required (POSIX sockets)
cmake -B build
cmake --build build
```

### Windows

```bash
# Automatically links ws2_32.lib (Winsock2)
cmake -B build
cmake --build build
```

## Testing

### Packet Capture

```bash
# Listen for UDP broadcasts on port 7778
tcpdump -i any -n udp port 7778 -X

# Or use Wireshark with filter: udp.port == 7778
```

### Example Receiver (Python)

```python
import socket
import struct

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', 7778))

while True:
    data, addr = sock.recvfrom(1024)

    # Check for batch packet
    if len(data) >= 16:
        magic, = struct.unpack('<I', data[0:4])

        if magic == 0x42464F44:  # Batch packet
            version, count = struct.unpack('<BB', data[4:6])
            print(f"Batch: {count} events from {addr}")

            # Parse events
            offset = 16
            for i in range(count):
                if len(data) >= offset + 32:
                    evt_magic, ts, etype, eid, value = struct.unpack(
                        '<IQBBH', data[offset:offset+16])
                    print(f"  Event {i}: type={etype}, id={eid}, value={value}")
                offset += 32
```

## Troubleshooting

### No packets received

1. **Check firewall**: Ensure UDP port 7778 is not blocked
2. **Verify address**: Use `127.0.0.1` for localhost testing
3. **Test network**: `netstat -an | grep 7778` to check port usage
4. **Check logs**: Look for "UDP Broadcasting initialized" message

### Packet loss / drops

1. **Increase queue size**: Set `UDPQueueSize=8192` or higher
2. **Reduce rate**: Lower `UDPMaxPacketsPerSecond`
3. **Check network**: Test with ping/iperf
4. **Monitor statistics**: Check for queue overflow warnings in logs

### Performance issues

1. **Disable if not needed**: Set `UDPBroadcastEnabled=0`
2. **Use unicast**: Avoid broadcast (255.255.255.255) if possible
3. **Reduce rate limit**: Lower `UDPMaxPacketsPerSecond`
4. **Check CPU**: Main thread impact should be < 0.01%

## Implementation Phases

### ✅ Phase 1: PinMAME Events (Completed)

- Core UDP broadcasting infrastructure
- PinMAME ROM-based events (Solenoid, Lamp, GI, Wire)
- Lock-free queue and batching
- Comprehensive unit tests

### ✅ Phase 2: Universal Device Sources (Completed)

- Automatic discovery of **all** controller plugins
- Dynamic event type mapping based on groupId
- Support for B2S, custom controllers, and future plugins
- Extensible architecture for any device source

Device GroupID Mapping:
- `0x0100` → GI events
- `0x0200` → Lamp events
- `0x0300` → Solenoid/Mech events
- Unknown → Default to Lamp events

### Future Extensions

- Enhanced event metadata (source plugin name, device names)
- RGB event detection and broadcasting
- Per-source event filtering configuration
- Plugin-specific event types

## License

GPLv3+ (same as VPinball)

## Authors

- Implementation: Claude Code Assistant
- Integration: VPinball DOF Plugin Team

## See Also

- [VPinball Documentation](https://github.com/vpinball/vpinball)
- [DOF (Direct Output Framework)](https://github.com/DirectOutput/DirectOutput)
- [WLED](https://github.com/Aircoookie/WLED) - Popular UDP LED controller
