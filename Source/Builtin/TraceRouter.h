#pragma once

#include "TraceBoard.h"

// Automatic routing for the trace board:
//   * route():        shortest legal route (A* over 8 directions, 45/90 degree turns, clearances)
//   * matchLengths(): lengthen the shorter trace with meanders until the pair is matched
//   * solve():        both of the above, for a whole board
//   * generateGame(): random obstacles, kept only if solve() can route the board, which is
//                     what guarantees every game is solvable
class TraceRouter
{
public:
    using Polyline = TraceBoard::Polyline;

    struct Result
    {
        bool routed = false;      // both traces complete
        bool matched = false;     // residual skew within tolerance
        double lengthInches[2] {};
        double skewInches = 0.0;
        double milliseconds = 0.0;
    };

    static constexpr double matchToleranceCells = 0.4;   // about 0.02 inch (3.5 ps on FR4)

    // How much route length the router will give up to avoid one cell of spacing-weighted parallel run
    // beside an aggressor lane. 0 turns aggressor avoidance off.
    static inline double aggressorPenalty = 0.6;

    static bool route (const TraceBoard&, int trace, const Polyline& other, Polyline& result);
    static bool matchLengths (TraceBoard&, int shorterTrace, double extraCells);
    static Result solve (TraceBoard&);
    static bool generateGame (TraceBoard&, int seed, int difficulty);
};

// The game rules: budgets that get tighter with difficulty, and the scoring.
struct TraceGame
{
    static constexpr int maxDifficulty = 5;

    static double skewBudgetUI (int difficulty)    { static const double v[] = { 0.30, 0.20, 0.15, 0.10, 0.07 }; return v[juce::jlimit (1, 5, difficulty) - 1]; }
    static double lengthBudgetFactor (int difficulty) { static const double v[] = { 1.7, 1.5, 1.35, 1.25, 1.15 }; return v[juce::jlimit (1, 5, difficulty) - 1]; }
    static int obstacleCount (int difficulty)      { return 3 + 2 * juce::jlimit (1, 5, difficulty); }

    // Differential crosstalk pickup allowed relative to what the solver achieves (plus a small allowance).
    static double crosstalkBudgetFactor (int difficulty) { static const double v[] = { 2.5, 2.0, 1.6, 1.3, 1.15 }; return v[juce::jlimit (1, 5, difficulty) - 1]; }
    static constexpr double crosstalkAllowance = 0.15;   // weighted inches

    struct Score
    {
        bool complete = false, passed = false;
        int stars = 0;                 // 0 = not passed, 1..3
        double skewUI = 0.0, skewBudgetUI = 0.0, averageLength = 0.0, lengthBudget = 0.0;
        bool hasCrosstalk = false;     // the board has aggressor lanes
        double crosstalkIndex = 0.0, crosstalkBudget = 0.0;
    };

    // `skewPs` and `unitIntervalPs` come from the audio side (velocity and line rate).
    static Score evaluate (const TraceBoard&, double skewPs, double unitIntervalPs);
};
