#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// The physical side of the trace channel: a small board on a 0.05 inch grid with two
// differential traces (+ and -), keep-out obstacles, and the design rules that decide
// what may be drawn. Pure geometry, no audio and no GUI, so it can be tested on its own.
//
// Coordinates are grid cells; cell (0,0) is the top-left corner. Routing is PCB style:
// segments run at multiples of 45 degrees and turns are at most 90 degrees.
class TraceBoard
{
public:
    static constexpr int width = 160, height = 100;      // 8.0 x 5.0 inches
    static constexpr double pitchInches = 0.05;

    // Clearances, in cells (centreline to keep-out / centreline to other copper).
    static constexpr double obstacleClearance = 1.0;
    static constexpr double traceClearance = 2.0;

    static constexpr int plus = 0, minus = 1;

    struct Point
    {
        int x = 0, y = 0;
        bool operator== (const Point& o) const noexcept { return x == o.x && y == o.y; }
        bool operator!= (const Point& o) const noexcept { return ! (*this == o); }
    };

    struct Obstacle { int x, y, w, h; };   // covers cells x..x+w, y..y+h (continuous)

    using Polyline = std::vector<Point>;

    enum class Mode { sandbox, game };

    TraceBoard();

    // --- layout
    Point inPad[2], outPad[2];
    std::vector<Obstacle> obstacles;
    Mode mode = Mode::sandbox;
    int gameSeed = 0, difficulty = 3;
    double parLengthInches = 0.0;    // what the auto-solver achieves (average of both traces), game mode only

    void resetSandbox();                         // empty board, default pads, straight-line traces
    void setPads (int inY, int outY);            // + above -, 6 cells apart
    void clearTraces();

    // --- traces (each always starts at its input pad)
    const Polyline& getTrace (int trace) const noexcept { return traces[trace]; }
    void setTrace (int trace, Polyline vertices);    // no validation: for the solver and state restore
    bool isComplete (int trace) const;
    double lengthInches (int trace) const;           // drawn length only
    // Drawn length plus, if unfinished, the straight line to the output pad: what the audio uses.
    double effectiveLengthInches (int trace) const;

    // --- editing with design-rule checking
    bool canAppend (int trace, const Polyline& newVertices) const;
    bool append (int trace, const Polyline& newVertices);
    bool undo (int trace);                           // removes the last vertex (never the pad)
    void clear (int trace);

    // Whole-polyline check (start pad, angles, bounds, clearances against the other trace,
    // obstacles and itself). `other` may be empty.
    bool isValidPolyline (int trace, const Polyline& vertices, const Polyline& other) const;

    // --- helpers shared with the router
    static double segmentLength (Point a, Point b) noexcept;
    static double polylineLengthCells (const Polyline& vertices) noexcept;
    static double distanceToRect (double ax, double ay, double bx, double by, const Obstacle& r) noexcept;
    static double segmentDistance (double ax, double ay, double bx, double by,
                                   double cx, double cy, double dx, double dy) noexcept;
    static bool isLegalStep (Point from, Point to) noexcept;        // 8-way, non-zero
    static bool isAcceptableTurn (Point a, Point b, Point c) noexcept;

    // Does the segment a-b respect obstacles, the given other polyline, and the other trace's pads?
    bool segmentClear (int trace, Point a, Point b, const Polyline& other) const;

    // --- coupling between the + and - traces
    // Where the two traces run parallel and close, they couple. A "run" is one pair of parallel
    // segments; its weight falls off with spacing (1.0 at the minimum spacing, e^-1 three cells further).
    struct CouplingRun
    {
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // the stretch of the + segment that faces the - trace
        double offsetX = 0, offsetY = 0;         // from there across to the - trace
        double overlapCells = 0.0;         // length over which the two actually face each other
        double spacingCells = 0.0;         // centre to centre
        double weight = 0.0;
    };

    static constexpr double couplingReachCells = 12.0;
    static double couplingWeight (double spacingCells) noexcept;

    std::vector<CouplingRun> couplingRuns() const;
    double coupledLengthInches() const;      // total overlap length
    double weightedCouplingInches() const;   // overlap x weight, summed: what the mode model uses

    // --- aggressor lanes
    // Noisy traces belonging to something else. They are not obstacles (a trace may cross one, as if on
    // another layer), but running alongside one couples noise in: a perpendicular crossing barely does,
    // a long parallel run does a lot, and the nearer of the two traces picks up more, which turns
    // common-mode pickup into differential noise.
    struct Aggressor
    {
        Polyline points;
        double rateRatio = 1.0;   // its data rate relative to the link's
        int seed = 0;             // selects its pattern
    };

    static constexpr int maxAggressors = 4;
    std::vector<Aggressor> aggressors;

    struct AggressorCoupling { double plus = 0.0, minus = 0.0; };    // spacing-weighted parallel inches
    std::vector<AggressorCoupling> aggressorCoupling() const;        // one per lane
    // Scale-free crosstalk figure for scoring: the differential part, sqrt(sum over lanes (plus - minus)^2).
    double differentialCouplingIndex() const;
    double parCouplingIndex = 0.0;   // what the solver achieves (game mode)

    // coupling of one unit step (from `from` to `to`) to the lanes, for the router
    double stepCouplingToAggressors (Point from, Point to) const;

    // --- impedance along a trace (for reflections)
    // The line is cut into `sections` equal lengths and each gets a single-ended impedance, starting
    // from 50 ohm:
    //   * proximity to the other trace: closer than the nominal pair spacing (6 cells) lowers it,
    //     further apart raises it (up to +/-25% x strength at the extremes);
    //   * a corner dips the section it sits in (45 degrees: 2%, 90 degrees: 5%, x strength);
    //   * the first and last quarter inch (pad / connector) are offset by `connectorPercent` x strength.
    // strength = 0 gives a perfectly uniform 50 ohm line.
    std::vector<float> impedanceProfile (int trace, int sections, double strength, double connectorPercent) const;
    double totalLengthCells (int trace) const;     // effective (unfinished traces run straight to the pad)

    // --- state
    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement&);

private:
    Polyline traces[2];
};
