#pragma once

#include "BitLog.h"

// Compares the bits the transmitter sent with the bits the receiver recovered.
//
// The receiver's bits are the transmitter's bits delayed by some unknown amount (the CDR's lock
// time plus every stage's latency), so the first job is finding that offset: it looks for the
// offset at which a window of received bits matches the sent stream. Once aligned, every new
// received bit is checked against the bit it should be, and errors are counted continuously. A burst of
// errors (a cycle slip, or the link falling over) drops the alignment and it searches again.
class BitCompare
{
public:
    struct Stats
    {
        bool synced = false;
        int64_t offset = 0;           // sent bit number minus received bit number, when synced
        uint64_t compared = 0, errors = 0;
        uint64_t sinceLastError = 0;  // bits since the last error
        int resyncs = 0;              // times alignment was lost after being found
        uint64_t longestBurst = 0;    // most consecutive errors
        uint64_t bursts = 0;          // groups of errors
    };

    static constexpr int searchWindow = 256;
    static constexpr int recentSize = 512;

    void reset();
    // `searchNow` false skips the (costly) alignment search this time: callers on a timer can search less often.
    void update (const BitLog& sent, const BitLog& received, bool searchNow = true);

    const Stats& getStats() const noexcept { return stats; }
    double ber() const noexcept { return stats.compared > 0 ? (double) stats.errors / (double) stats.compared : 0.0; }
    // With no errors seen, the BER is below this at 95% confidence ("rule of three").
    double berUpperBound95() const noexcept { return stats.compared > 0 ? 3.0 / (double) stats.compared : 1.0; }

    // The most recent `count` compared pairs, oldest first. `firstReceivedIndex` is the received-bit number of the first one.
    void recent (int count, std::vector<uint8_t>& sent, std::vector<uint8_t>& received, uint64_t& firstReceivedIndex) const;

private:
    bool findOffset (const BitLog& sent, const BitLog& received, uint64_t receivedTotal);

    Stats stats;
    uint64_t receivedNext = 0, seenSent = 0, seenReceived = 0, currentBurst = 0;
    uint8_t sentRecent[recentSize] {}, receivedRecent[recentSize] {}, errorWindow[searchWindow] {};
    uint64_t recentCount = 0;
    int errorsInWindow = 0;
};
