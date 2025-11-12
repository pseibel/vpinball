# DOF UDP Broadcasting Tests

Comprehensive unit tests for the UDP event broadcasting system.

## Test Coverage

### 1. Event Protocol Tests (6 test cases)
- ✅ Event structure size validation (32 bytes)
- ✅ BatchHeader structure size (16 bytes)
- ✅ Magic number verification
- ✅ Event factory methods (Solenoid, Lamp, GI, RGB, Wire, Table)
- ✅ Batch packet creation and limits
- ✅ Batch packet reset functionality

### 2. Lock-Free Queue Tests (6 test cases)
- ✅ Queue capacity calculation
- ✅ Basic push and pop operations
- ✅ Queue full condition handling
- ✅ Queue size tracking
- ✅ Batch push/pop operations
- ✅ Queue with Event structures

### 3. Event Collector Tests (4 test cases)
- ✅ Collector creation and destruction
- ✅ Event submission (all types)
- ✅ Statistics tracking
- ✅ Queue overflow handling

### 4. UDP System Integration Tests (3 test cases)
- ✅ System initialization and shutdown
- ✅ Event submission and broadcasting
- ✅ Statistics collection

### 5. Thread Safety Tests (1 test case)
- ✅ Concurrent queue access (producer/consumer)

## Total: 19 Test Cases, 151 Assertions

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
    -Iplugins/dof/udp \
    -Ithird-party/include \
    -pthread \
    tests/dof-udp-test.cpp \
    plugins/dof/udp/dof_event_collector.cpp \
    plugins/dof/udp/dof_udp_broadcaster.cpp \
    plugins/dof/udp/dof_udp_system.cpp \
    -o dof-udp-test
```

### Run All Tests

```bash
./dof-udp-test
```

Expected output:
```
[doctest] test cases:  19 |  19 passed | 0 failed | 0 skipped
[doctest] assertions: 151 | 151 passed | 0 failed |
[doctest] Status: SUCCESS!
```

### Run Specific Test Suites

```bash
# Event protocol tests only
./dof-udp-test --test-suite="DOF UDP Event Protocol"

# Lock-free queue tests only
./dof-udp-test --test-suite="Lock-Free Queue"

# Event collector tests only
./dof-udp-test --test-suite="Event Collector"

# UDP system tests only
./dof-udp-test --test-suite="UDP System Integration"

# Thread safety tests only
./dof-udp-test --test-suite="Thread Safety"
```

### Run Specific Test Cases

```bash
# Test event structure size
./dof-udp-test --test-case="Event structure size"

# Test queue operations
./dof-udp-test --test-case="Basic push and pop"

# Test statistics
./dof-udp-test --test-case="Statistics tracking"
```

### Verbose Output

```bash
# Show all successful assertions
./dof-udp-test --success

# Show detailed test execution
./dof-udp-test --success --duration=true
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
./dof-udp-test --duration=true --test-suite="Lock-Free Queue"
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
          g++ -std=c++20 -I. -Iplugins/dof/udp -Ithird-party/include -pthread \
              tests/dof-udp-test.cpp \
              plugins/dof/udp/*.cpp \
              -o dof-udp-test

      - name: Run tests
        run: ./dof-udp-test
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

- [Main UDP Broadcasting Documentation](../plugins/dof/udp/README.md)
- [doctest Documentation](https://github.com/doctest/doctest/blob/master/doc/markdown/readme.md)
- [VPinball Testing Guide](../docs/testing.md)
