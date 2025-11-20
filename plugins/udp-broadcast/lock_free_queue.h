// license:GPLv3+

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

namespace UDPBroadcast {

///////////////////////////////////////////////////////////////////////////////
// Lock-free Single Producer Single Consumer (SPSC) Queue
//
// This queue is designed for high-performance event passing between
// the game thread (producer) and the UDP broadcaster thread (consumer).
//
// Key Features:
// - Lock-free: No mutexes, only atomic operations
// - Wait-free for producer: Push() never blocks
// - Cache-line aligned for optimal performance
// - Fixed size ring buffer
//
// Thread Safety:
// - ONE producer thread (game/DOF thread)
// - ONE consumer thread (UDP broadcaster thread)
// - NOT safe for multiple producers or consumers!
///////////////////////////////////////////////////////////////////////////////

template<typename T, size_t Size>
class LockFreeQueue {
public:
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");

    LockFreeQueue()
        : m_writeIndex(0)
        , m_readIndex(0)
    {
        static_assert(sizeof(std::atomic<size_t>) == sizeof(size_t),
                      "Atomic size_t must be lock-free");
    }

    ~LockFreeQueue() = default;

    // Disable copy/move
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    ///////////////////////////////////////////////////////////////////////////
    // Producer API (called from game thread)
    ///////////////////////////////////////////////////////////////////////////

    // Try to push an item onto the queue
    // Returns true on success, false if queue is full
    // This operation is wait-free (never blocks)
    bool Push(const T& item) {
        const size_t currentWrite = m_writeIndex.load(std::memory_order_relaxed);
        const size_t nextWrite = (currentWrite + 1) & (Size - 1);

        // Check if queue is full
        if (nextWrite == m_readIndex.load(std::memory_order_acquire))
            return false;

        // Write the item
        m_buffer[currentWrite] = item;

        // Publish the write
        m_writeIndex.store(nextWrite, std::memory_order_release);
        return true;
    }

    // Try to push an item with move semantics
    bool Push(T&& item) {
        const size_t currentWrite = m_writeIndex.load(std::memory_order_relaxed);
        const size_t nextWrite = (currentWrite + 1) & (Size - 1);

        if (nextWrite == m_readIndex.load(std::memory_order_acquire))
            return false;

        m_buffer[currentWrite] = std::move(item);
        m_writeIndex.store(nextWrite, std::memory_order_release);
        return true;
    }

    ///////////////////////////////////////////////////////////////////////////
    // Consumer API (called from broadcaster thread)
    ///////////////////////////////////////////////////////////////////////////

    // Try to pop an item from the queue
    // Returns true on success (item is written to 'item'), false if queue is empty
    bool Pop(T& item) {
        const size_t currentRead = m_readIndex.load(std::memory_order_relaxed);

        // Check if queue is empty
        if (currentRead == m_writeIndex.load(std::memory_order_acquire))
            return false;

        // Read the item
        item = m_buffer[currentRead];

        // Publish the read
        const size_t nextRead = (currentRead + 1) & (Size - 1);
        m_readIndex.store(nextRead, std::memory_order_release);
        return true;
    }

    ///////////////////////////////////////////////////////////////////////////
    // Status API (can be called from either thread, but values are approximate)
    ///////////////////////////////////////////////////////////////////////////

    // Get approximate number of items in queue
    size_t GetSize() const {
        const size_t write = m_writeIndex.load(std::memory_order_acquire);
        const size_t read = m_readIndex.load(std::memory_order_acquire);
        return (write - read) & (Size - 1);
    }

    // Check if queue is empty (approximate)
    bool IsEmpty() const {
        return m_readIndex.load(std::memory_order_acquire) ==
               m_writeIndex.load(std::memory_order_acquire);
    }

    // Check if queue is full (approximate)
    bool IsFull() const {
        const size_t write = m_writeIndex.load(std::memory_order_acquire);
        const size_t read = m_readIndex.load(std::memory_order_acquire);
        return ((write + 1) & (Size - 1)) == read;
    }

    // Get maximum capacity
    static constexpr size_t Capacity() {
        return Size - 1; // One slot is always unused (full vs empty detection)
    }

private:
    // Cache-line padding to prevent false sharing
    // Most modern CPUs have 64-byte cache lines
    static constexpr size_t CACHE_LINE_SIZE = 64;

    // Ring buffer storage
    T m_buffer[Size];

    // Write index (producer only)
    // Aligned to cache line to prevent false sharing with read index
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_writeIndex;

    // Read index (consumer only)
    // Aligned to cache line to prevent false sharing with write index
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_readIndex;
};

///////////////////////////////////////////////////////////////////////////////
// Batch-oriented queue operations
///////////////////////////////////////////////////////////////////////////////

// Try to pop multiple items from the queue
// Returns the number of items actually popped (0 to maxItems)
template<typename T, size_t Size>
size_t PopBatch(LockFreeQueue<T, Size>& queue, T* items, size_t maxItems) {
    size_t count = 0;
    while (count < maxItems && queue.Pop(items[count])) {
        count++;
    }
    return count;
}

// Try to push multiple items to the queue
// Returns the number of items actually pushed (0 to count)
template<typename T, size_t Size>
size_t PushBatch(LockFreeQueue<T, Size>& queue, const T* items, size_t count) {
    size_t pushed = 0;
    while (pushed < count && queue.Push(items[pushed])) {
        pushed++;
    }
    return pushed;
}

} // namespace UDPBroadcast
