#pragma once

#include <juce_core/juce_core.h>
#include <complex>
#include <vector>

// Touchstone (.s1p .. .sNp, version 1) S-parameter data.
//   # <Hz|kHz|MHz|GHz> S <RI|MA|DB> R <ohms>
//   then, per frequency, the S-parameters as (a, b) pairs. Two-port files list S11 S21 S12 S22
//   (the odd one out); N >= 3 lists the matrix row by row and may spread a record over several lines.
class TouchstoneData
{
public:
    using Complex = std::complex<double>;

    int ports = 0;
    double referenceImpedance = 50.0;
    std::vector<double> frequencyHz;       // real-world frequency, ascending
    std::vector<Complex> values;           // [frequency * ports * ports + row * ports + column]

    size_t numFrequencies() const noexcept { return frequencyHz.size(); }
    Complex s (size_t frequency, int row, int column) const { return values[(frequency * (size_t) ports + (size_t) row) * (size_t) ports + (size_t) column]; }

    // Linear interpolation of the complex value; clamps outside the measured band.
    Complex interpolate (int row, int column, double hz) const;

    // `portsHint` (0 = work it out from the data). Returns false and sets `error` when the file can't be used.
    static bool parse (const juce::String& text, int portsHint, TouchstoneData& result, juce::String& error);
    static int portsFromFilename (const juce::String& filename);   // "channel.s4p" -> 4, else 0

    // Real/imaginary, GHz. For writing example channels and for tests.
    juce::String toText (const juce::String& comment = {}) const;
    void setSize (int newPorts, size_t frequencies);
    void set (size_t frequency, int row, int column, Complex v) { values[(frequency * (size_t) ports + (size_t) row) * (size_t) ports + (size_t) column] = v; }
};
