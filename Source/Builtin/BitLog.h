#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

// A rolling record of the last 65536 bits a stage has produced, written by one thread (the audio
// thread) and read by another (the message thread). The transmitter logs the bits it sends and the
// receiver the bits it recovers; the bit-compare engine lines the two up.
class BitLog
{
public:
    static constexpr int capacity = 1 << 16;

    void reset() noexcept { count.store (0, std::memory_order_release); }

    void push (int bit) noexcept
    {
        const uint64_t c = count.load (std::memory_order_relaxed);
        ring[(size_t) (c & (capacity - 1))] = (uint8_t) (bit != 0);
        count.store (c + 1, std::memory_order_release);
    }

    uint64_t total() const noexcept { return count.load (std::memory_order_acquire); }

    // Bit number `index`, if it is still in the window. (A writer that laps the reader can make a bit
    // change under it; for a statistics tool that is acceptable.)
    bool get (uint64_t index, uint8_t& bit) const noexcept
    {
        const uint64_t t = total();
        if (index >= t || t - index > (uint64_t) capacity - 1)
            return false;

        bit = ring[(size_t) (index & (capacity - 1))];
        return true;
    }

private:
    std::atomic<uint64_t> count { 0 };
    uint8_t ring[capacity] {};
};
