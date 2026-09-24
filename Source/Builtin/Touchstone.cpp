#include "Touchstone.h"

void TouchstoneData::setSize (int newPorts, size_t frequencies)
{
    ports = newPorts;
    frequencyHz.assign (frequencies, 0.0);
    values.assign (frequencies * (size_t) ports * (size_t) ports, Complex());
}

TouchstoneData::Complex TouchstoneData::interpolate (int row, int column, double hz) const
{
    if (frequencyHz.empty())
        return {};

    if (hz <= frequencyHz.front()) return s (0, row, column);
    if (hz >= frequencyHz.back())  return s (frequencyHz.size() - 1, row, column);

    const auto upper = (size_t) (std::upper_bound (frequencyHz.begin(), frequencyHz.end(), hz) - frequencyHz.begin());
    const double span = frequencyHz[upper] - frequencyHz[upper - 1];
    const double f = span > 0.0 ? (hz - frequencyHz[upper - 1]) / span : 0.0;
    return s (upper - 1, row, column) * (1.0 - f) + s (upper, row, column) * f;
}

int TouchstoneData::portsFromFilename (const juce::String& filename)
{
    const auto name = filename.toLowerCase();
    const int dot = name.lastIndexOfChar ('.');
    if (dot < 0)
        return 0;

    const auto extension = name.substring (dot + 1);   // "s4p"
    if (extension.length() >= 3 && extension.startsWithChar ('s') && extension.endsWithChar ('p') && extension.substring (1, extension.length() - 1).containsOnly ("0123456789"))
        return extension.substring (1, extension.length() - 1).getIntValue();

    return 0;
}

bool TouchstoneData::parse (const juce::String& text, int portsHint, TouchstoneData& result, juce::String& error)
{
    double unit = 1.0e9;             // defaults from the specification: GHz, S, MA, 50 ohm
    enum { ma, db, ri } format = ma;
    double reference = 50.0;
    bool sawOptions = false;
    std::vector<double> numbers;

    for (auto line : juce::StringArray::fromLines (text))
    {
        const int comment = line.indexOfChar ('!');
        if (comment >= 0)
            line = line.substring (0, comment);
        line = line.trim();

        if (line.isEmpty())
            continue;

        if (line.startsWithChar ('#'))
        {
            if (sawOptions)
                continue;   // only the first option line counts

            sawOptions = true;
            const auto tokens = juce::StringArray::fromTokens (line.substring (1).toLowerCase(), " \t", "");
            for (int i = 0; i < tokens.size(); ++i)
            {
                const auto& t = tokens[i];
                if (t == "hz")       unit = 1.0;
                else if (t == "khz") unit = 1.0e3;
                else if (t == "mhz") unit = 1.0e6;
                else if (t == "ghz") unit = 1.0e9;
                else if (t == "ma")  format = ma;
                else if (t == "db")  format = db;
                else if (t == "ri")  format = ri;
                else if (t == "r" && i + 1 < tokens.size()) reference = tokens[++i].getDoubleValue();
                else if (t == "y" || t == "z" || t == "h" || t == "g")
                {
                    error = "Only S-parameter files are supported (this one holds " + t.toUpperCase() + " parameters).";
                    return false;
                }
            }
            continue;
        }

        for (auto& token : juce::StringArray::fromTokens (line, " \t,", ""))
            if (token.containsAnyOf ("0123456789"))
                numbers.push_back (token.getDoubleValue());
    }

    if (numbers.empty())
    {
        error = "No data found.";
        return false;
    }

    // Without a hint, the right port count is the one that makes the first number of every record a
    // frequency: strictly increasing down the file.
    int ports = portsHint;
    if (ports <= 0)
        for (int n : { 1, 2, 4, 3, 6, 8 })
        {
            const size_t rec = (size_t) (1 + 2 * n * n);
            if (numbers.size() % rec != 0 || numbers.size() / rec < 2)
                continue;

            bool increasing = true;
            for (size_t f = 1; f < numbers.size() / rec && increasing; ++f)
                increasing = numbers[f * rec] > numbers[(f - 1) * rec];

            if (increasing)
            {
                ports = n;
                break;
            }
        }

    if (ports <= 0 || ports > 16)
    {
        error = "Can't tell how many ports this file has (use a .sNp extension).";
        return false;
    }

    const size_t record = (size_t) (1 + 2 * ports * ports);
    if (numbers.size() % record != 0)
    {
        error = "The data doesn't divide into whole records of a " + juce::String (ports) + "-port file.";
        return false;
    }

    const size_t count = numbers.size() / record;
    if (count < 2)
    {
        error = "Needs at least two frequency points.";
        return false;
    }

    result.setSize (ports, count);
    result.referenceImpedance = reference;

    for (size_t f = 0; f < count; ++f)
    {
        const double* r = &numbers[f * record];
        result.frequencyHz[f] = r[0] * unit;

        for (int index = 0; index < ports * ports; ++index)
        {
            const double a = r[1 + 2 * index], b = r[2 + 2 * index];
            Complex value;
            if (format == ri)      value = { a, b };
            else if (format == db) value = std::polar (std::pow (10.0, a / 20.0), b * juce::MathConstants<double>::pi / 180.0);
            else                   value = std::polar (a, b * juce::MathConstants<double>::pi / 180.0);

            // two-port files: S11 S21 S12 S22
            int row = index / ports, column = index % ports;
            if (ports == 2)
            {
                row = index % 2;
                column = index / 2;
            }

            result.set (f, row, column, value);
        }
    }

    for (size_t f = 1; f < count; ++f)
        if (result.frequencyHz[f] <= result.frequencyHz[f - 1])
        {
            error = "Frequencies must increase down the file.";
            return false;
        }

    return true;
}

juce::String TouchstoneData::toText (const juce::String& comment) const
{
    juce::String text;
    if (comment.isNotEmpty())
        text << "! " << comment << "\n";
    text << "# GHz S RI R " << juce::String (referenceImpedance, 0) << "\n";

    for (size_t f = 0; f < numFrequencies(); ++f)
    {
        text << juce::String (frequencyHz[f] * 1.0e-9, 6);

        // the inverse of the reading order above
        for (int index = 0; index < ports * ports; ++index)
        {
            int row = index / ports, column = index % ports;
            if (ports == 2) { row = index % 2; column = index / 2; }

            if (ports > 2 && index > 0 && index % 4 == 0)
                text << "\n";

            const auto v = s (f, row, column);
            text << " " << juce::String (v.real(), 9) << " " << juce::String (v.imag(), 9);
        }
        text << "\n";
    }

    return text;
}
