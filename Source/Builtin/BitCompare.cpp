#include "BitCompare.h"

void BitCompare::reset()
{
    stats = {};
    receivedNext = seenSent = seenReceived = currentBurst = recentCount = 0;
    std::fill (std::begin (errorWindow), std::end (errorWindow), (uint8_t) 0);
    errorsInWindow = 0;
}

// Look for an offset at which the most recent `searchWindow` received bits match the sent stream.
bool BitCompare::findOffset (const BitLog& sent, const BitLog& received, uint64_t receivedTotal)
{
    const uint64_t sentTotal = sent.total();
    if (receivedTotal < (uint64_t) 2 * searchWindow || sentTotal < (uint64_t) searchWindow)
        return false;

    const uint64_t first = receivedTotal - searchWindow;   // received bit that starts the window
    uint8_t window[searchWindow];
    for (int i = 0; i < searchWindow; ++i)
        if (! received.get (first + (uint64_t) i, window[i]))
            return false;

    // Candidate: sent bit number s = first + offset. Start from the most recent sent bits, which is where
    // the answer usually is (the receiver is a little behind the transmitter).
    const int64_t highest = (int64_t) sentTotal - searchWindow - (int64_t) first;
    const int64_t lowest = std::max<int64_t> ((int64_t) sentTotal - (int64_t) BitLog::capacity + 2 - (int64_t) first, -(int64_t) first);
    for (int64_t offset = highest; offset >= lowest; --offset)
    {
        const uint64_t s0 = (uint64_t) ((int64_t) first + offset);
        int mismatches = 0;
        bool ok = true;

        for (int i = 0; i < searchWindow && ok; ++i)
        {
            uint8_t bit;
            if (! sent.get (s0 + (uint64_t) i, bit))
            {
                ok = false;
                break;
            }
            mismatches += bit != window[i];
            ok = mismatches <= 24;   // well past chance (128) even for a rough link
        }

        if (ok)
        {
            stats.synced = true;
            stats.offset = offset;
            receivedNext = receivedTotal;   // count from here on; the window used for the search is not scored
            std::fill (std::begin (errorWindow), std::end (errorWindow), (uint8_t) 0);
            errorsInWindow = 0;
            return true;
        }
    }

    return false;
}

void BitCompare::update (const BitLog& sent, const BitLog& received, bool searchNow)
{
    const uint64_t receivedTotal = received.total(), sentTotal = sent.total();

    // either side restarted (the chain was rebuilt, or a stage was prepared again): start over
    if (receivedTotal < seenReceived || sentTotal < seenSent)
        reset();

    seenReceived = receivedTotal;
    seenSent = sentTotal;

    if (! stats.synced && (! searchNow || ! findOffset (sent, received, receivedTotal)))
        return;

    while (stats.synced && receivedNext < receivedTotal)
    {
        uint8_t rxBit, txBit;
        const int64_t s = (int64_t) receivedNext + stats.offset;

        if (s < 0 || ! received.get (receivedNext, rxBit) || ! sent.get ((uint64_t) s, txBit))
        {
            stats.synced = false;   // fell out of a window: look again
            break;
        }

        const bool wrong = rxBit != txBit;
        ++stats.compared;
        ++stats.sinceLastError;

        if (wrong)
        {
            stats.errors++;
            stats.sinceLastError = 0;
            if (currentBurst == 0) ++stats.bursts;
            ++currentBurst;
            stats.longestBurst = std::max (stats.longestBurst, currentBurst);
        }
        else
            currentBurst = 0;

        const size_t slot = (size_t) (recentCount % recentSize);
        sentRecent[slot] = txBit;
        receivedRecent[slot] = rxBit;
        ++recentCount;

        uint8_t& old = errorWindow[(size_t) (stats.compared % searchWindow)];
        errorsInWindow += (wrong ? 1 : 0) - old;
        old = wrong ? 1 : 0;
        ++receivedNext;

        if (stats.compared > (uint64_t) searchWindow && errorsInWindow > 90)
        {
            stats.synced = false;   // alignment lost (a cycle slip, or the link fell over)
            ++stats.resyncs;
            receivedNext = receivedTotal;
            currentBurst = 0;
            break;
        }
    }
}

void BitCompare::recent (int count, std::vector<uint8_t>& sent, std::vector<uint8_t>& received, uint64_t& firstReceivedIndex) const
{
    const int n = (int) std::min<uint64_t> ((uint64_t) std::min (count, recentSize), recentCount);
    sent.assign ((size_t) n, 0);
    received.assign ((size_t) n, 0);
    firstReceivedIndex = receivedNext >= (uint64_t) n ? receivedNext - (uint64_t) n : 0;

    for (int i = 0; i < n; ++i)
    {
        const size_t slot = (size_t) ((recentCount - (uint64_t) n + (uint64_t) i) % recentSize);
        sent[(size_t) i] = sentRecent[slot];
        received[(size_t) i] = receivedRecent[slot];
    }
}
