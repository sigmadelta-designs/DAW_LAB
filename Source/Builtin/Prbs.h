#pragma once

#include <cstdint>

// Fibonacci LFSR PRBS generators (ITU-T O.150 polynomials), seeded all-ones.
class PrbsGenerator
{
public:
    enum Pattern { prbs7, prbs9, prbs11, prbs13, prbs15, prbs23, prbs31, clockUI, clockHalf, numPatterns };

    static const char* getName (int pattern)
    {
        static const char* names[] = { "PRBS7", "PRBS9", "PRBS11", "PRBS13", "PRBS15", "PRBS23", "PRBS31",
                                       "Clock 1010", "Clock 11001100" };
        return names[pattern];
    }

    void setPattern (int newPattern)
    {
        if (newPattern == pattern)
            return;

        pattern = newPattern;
        state = 0x7fffffffu;
        clockPhase = 0;
    }

    int getPattern() const noexcept { return pattern; }

    int nextBit()
    {
        switch (pattern)
        {
            case clockUI:   return (clockPhase++ & 1) ^ 1;                 // 1,0,1,0...
            case clockHalf: return ((clockPhase++ >> 1) & 1) ^ 1;          // 1,1,0,0,...
            default:        break;
        }

        // taps (n, k): x^n + x^k + 1, with the 13-bit polynomial being x^13+x^12+x^2+x+1.
        struct Poly { int order; uint32_t mask; };
        static const Poly polys[] = {
            { 7,  (1u << 6) | (1u << 5) },                                  // x^7  + x^6  + 1
            { 9,  (1u << 8) | (1u << 4) },                                  // x^9  + x^5  + 1
            { 11, (1u << 10) | (1u << 8) },                                 // x^11 + x^9  + 1
            { 13, (1u << 12) | (1u << 11) | (1u << 1) | (1u << 0) },        // x^13 + x^12 + x^2 + x + 1
            { 15, (1u << 14) | (1u << 13) },                                // x^15 + x^14 + 1
            { 23, (1u << 22) | (1u << 17) },                                // x^23 + x^18 + 1
            { 31, (1u << 30) | (1u << 27) },                                // x^31 + x^28 + 1
        };

        const auto& poly = polys[pattern];
        const uint32_t feedback = (uint32_t) __builtin_parity (state & poly.mask);
        const int out = (int) ((state >> (poly.order - 1)) & 1u);
        state = ((state << 1) | feedback) & (poly.order == 32 ? 0xffffffffu : ((1u << poly.order) - 1u));
        return out;
    }

private:
    int pattern = -1;
    uint32_t state = 0x7fffffffu;
    unsigned clockPhase = 0;
};
