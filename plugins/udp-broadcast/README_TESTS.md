# UDP Broadcasting Plugin Tests

Comprehensive unit tests for the UDP event broadcasting plugin.

## Test Coverage

### 1. Event Protocol Tests
- ✅ Event structure size validation (32 bytes)
- ✅ BatchHeader structure size (16 bytes)
- ✅ Magic number verification
- ✅ Event factory methods (Solenoid, Lamp, GI, RGB, Wire)
- ✅ Batch packet creation and limits
- ✅ Batch packet reset functionality

### 2. Lock-Free Queue Tests
- ✅ Queue capacity calculation
- ✅ Basic push and pop operations
- ✅ Queue full condition handling
- ✅ Queue size tracking
- ✅ Batch push/pop operations
- ✅ Queue with Event structures

### 3. Event Collector Tests
- ✅ Collector creation and destruction
- ✅ Event submission (all types)
- ✅ Statistics tracking
- ✅ Queue overflow handling

### 4. Segment Display Tests
- ✅ SegmentDisplayPacket creation and validation
- ✅ Display type support (7-seg, 9-seg, 14-seg, 16-seg, alphanumeric)
- ✅ Segment data encoding
- ✅ Queue operations and statistics

### 5. Table Information Tests
- ✅ TableInfoPacket creation with variable-size strings
- ✅ String truncation for long table/ROM names
- ✅ Empty string handling (table unload signal)
- ✅ Statistics tracking

### 6. UDP System Integration Tests
- ✅ System initialization and shutdown
- ✅ Event submission and broadcasting
- ✅ Multi-queue priority processing
- ✅ Statistics collection

### 7. Thread Safety Tests
- ✅ Concurrent queue access (producer/consumer)

## Building and Running Tests

### Prerequisites

Install doctest (header-only library):

```bash
# Download doctest header
curl -L https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h \
     -o third-party/include/doctest/doctest.h

# Or manually download from:
# https://github.com/doctest/doctest
```

### Compile Tests

```bash
# From vpinball root directory
g++ -std=c++20 \
    -I. \
    -Iplugins/udp-broadcast \
    -Ithird-party/include \
    -pthread \
    tests/udp-broadcast-test.cpp \
    plugins/udp-broadcast/dof_event_collector.cpp \
    plugins/udp-broadcast/dof_udp_broadcaster.cpp \
    plugins/udp-broadcast/dof_udp_system.cpp \
    -o udp-broadcast-test
```

### Run All Tests

```bash
./udp-broadcast-test
```

Expected output on success:
```
[doctest] test cases:  N |  N passed | 0 failed | 0 skipped
[doctest] assertions: M | M passed | 0 failed |
[doctest] Status: SUCCESS!
```

### Run Specific Test Suites

```bash
# Event protocol tests
./udp-broadcast-test --test-suite="DOF UDP Event Protocol"

# Lock-free queue tests
./udp-broadcast-test --test-suite="Lock-Free Queue"

# Event collector tests
./udp-broadcast-test --test-suite="Event Collector"

# Segment display tests
./udp-broadcast-test --test-suite="Segment Display Support"

# Table information tests
./udp-broadcast-test --test-suite="Table information support"

# UDP system integration tests
./udp-broadcast-test --test-suite="UDP System Integration"

# Thread safety tests
./udp-broadcast-test --test-suite="Thread Safety"
```

### Run Specific Test Cases

```bash
# Test event structure size
./udp-broadcast-test --test-case="Event structure size"

# Test queue operations
./udp-broadcast-test --test-case="Basic push and pop"

# Test statistics
./udp-broadcast-test --test-case="Statistics tracking"
```

### Verbose Output

```bash
# Show all successful assertions
./udp-broadcast-test --success

# Show detailed test execution
./udp-broadcast-test --success --duration=true
```

## Test Details

### Event Protocol Tests

Validates the binary protocol format:
- Ensures structures are packed correctly (no padding)
- Verifies magic numbers for packet identification
- Tests all event factory methods
- Validates batch packet limits (max 20 events)

### Lock-Free Queue Tests

Tests the SPSC (Single Producer Single Consumer) queue:
- Push/Pop operations
- Full/Empty detection
- Size tracking
- Batch operations
- Power-of-2 capacity handling

### Event Collector Tests

Tests the high-level API:
- Event submission for all types
- Statistics collection (sent/dropped)
- Queue overflow behavior
- Memory management

### UDP System Integration Tests

End-to-end system tests:
- Initialization with configuration
- Event broadcasting over network
- Graceful shutdown
- Statistics tracking
- Thread lifecycle management

### Thread Safety Tests

Multi-threaded testing:
- Concurrent producer/consumer access
- 500 push operations from producer thread
- 500 pop operations from consumer thread
- Validates lock-free correctness

## Performance Benchmarks

Run with timing to measure performance:

```bash
./udp-broadcast-test --duration=true --test-suite="Lock-Free Queue"
```

Expected performance (typical hardware):
- Queue push: < 100 ns
- Queue pop: < 100 ns
- Event submission: < 200 ns
- Batch operations: < 500 ns per event

## Troubleshooting

### Compilation Errors

**doctest.h not found:**
```bash
# Download doctest header
mkdir -p third-party/include/doctest
curl -L https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h \
     -o third-party/include/doctest/doctest.h
```

**Undefined reference errors:**
```bash
# Ensure all source files are included in compilation
# Check that -pthread flag is present
```

### Test Failures

**Network tests failing:**
- Check if ports 7779-7781 are available
- Verify firewall settings
- Try running with elevated privileges

**Thread safety test timeout:**
- May indicate deadlock or race condition
- Check system load
- Increase timeout in test code if needed

**Statistics mismatch:**
- UDP send may fail due to network conditions
- Check for "sendErrors" in statistics
- Verify network interface is up

## Continuous Integration

### GitHub Actions Example

```yaml
name: Test UDP Broadcasting

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2

      - name: Install doctest
        run: |
          mkdir -p third-party/include/doctest
          curl -L https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h \
               -o third-party/include/doctest/doctest.h

      - name: Build tests
        run: |
          g++ -std=c++20 -I. -Iplugins/udp-broadcast -Ithird-party/include -pthread \
              tests/udp-broadcast-test.cpp \
              plugins/udp-broadcast/*.cpp \
              -o udp-broadcast-test

      - name: Run tests
        run: ./udp-broadcast-test
```

## Adding New Tests

To add new tests, follow this pattern:

```cpp
TEST_SUITE("Your Test Suite Name") {
    TEST_CASE("Your test case description") {
        // Arrange
        EventCollector collector(4096);

        // Act
        collector.Solenoid(1, 255);

        // Assert
        Event e;
        CHECK(collector.PopEvent(e) == true);
        CHECK(e.type == EventType::Solenoid);
        CHECK(e.id == 1);
        CHECK(e.value == 255);
    }
}
```

## License

GPLv3+ (same as VPinball)

## See Also

- [Main UDP Broadcasting Plugin Documentation](README.md)
- [doctest Documentation](https://github.com/doctest/doctest/blob/master/doc/markdown/readme.md)
- [VPinball Testing Guide](../../docs/testing.md)
