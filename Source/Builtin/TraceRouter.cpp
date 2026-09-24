#include "TraceRouter.h"
#include <queue>

using Point = TraceBoard::Point;
using Polyline = TraceBoard::Polyline;

namespace
{
    const int directionX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    const int directionY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

    double pointToSegment (double px, double py, double ax, double ay, double bx, double by)
    {
        const double dx = bx - ax, dy = by - ay, lengthSquared = dx * dx + dy * dy;
        double t = lengthSquared > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / lengthSquared : 0.0;
        t = juce::jlimit (0.0, 1.0, t);
        return std::hypot (px - (ax + t * dx), py - (ay + t * dy));
    }

    double pointToRect (double px, double py, const TraceBoard::Obstacle& r)
    {
        const double dx = juce::jmax ((double) r.x - px, 0.0, px - (r.x + r.w));
        const double dy = juce::jmax ((double) r.y - py, 0.0, py - (r.y + r.h));
        return std::hypot (dx, dy);
    }
}

// ---------------------------------------------------------------------------------
bool TraceRouter::route (const TraceBoard& board, int trace, const Polyline& other, Polyline& result)
{
    constexpr int W = TraceBoard::width, H = TraceBoard::height;
    const int otherTrace = 1 - trace;

    // Clearance slack of every cell: how far it is from violating a rule (negative = not allowed).
    std::vector<float> slack ((size_t) (W * H), 1.0e6f);
    const auto at = [] (int x, int y) { return (size_t) (y * W + x); };

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            float s = (x < 1 || y < 1 || x > W - 2 || y > H - 2) ? -1.0f : 1.0e6f;
            for (const auto& o : board.obstacles)
                s = juce::jmin (s, (float) (pointToRect (x, y, o) - TraceBoard::obstacleClearance));
            slack[at (x, y)] = s;
        }

    const auto applyPoint = [&] (int px, int py)
    {
        const int reach = (int) std::ceil (TraceBoard::traceClearance) + 2;
        for (int y = juce::jmax (0, py - reach); y <= juce::jmin (H - 1, py + reach); ++y)
            for (int x = juce::jmax (0, px - reach); x <= juce::jmin (W - 1, px + reach); ++x)
                slack[at (x, y)] = juce::jmin (slack[at (x, y)], (float) (std::hypot (x - px, y - py) - TraceBoard::traceClearance));
    };

    for (auto pad : { board.inPad[otherTrace], board.outPad[otherTrace] })
        applyPoint (pad.x, pad.y);

    const int reach = (int) std::ceil (TraceBoard::traceClearance) + 2;
    for (size_t i = 1; i < other.size(); ++i)
    {
        const auto a = other[i - 1], b = other[i];
        for (int y = juce::jmax (0, juce::jmin (a.y, b.y) - reach); y <= juce::jmin (H - 1, juce::jmax (a.y, b.y) + reach); ++y)
            for (int x = juce::jmax (0, juce::jmin (a.x, b.x) - reach); x <= juce::jmin (W - 1, juce::jmax (a.x, b.x) + reach); ++x)
                slack[at (x, y)] = juce::jmin (slack[at (x, y)],
                                               (float) (pointToSegment (x, y, a.x, a.y, b.x, b.y) - TraceBoard::traceClearance));
    }

    const Point start = board.inPad[trace], goal = board.outPad[trace];
    slack[at (start.x, start.y)] = juce::jmax (slack[at (start.x, start.y)], 0.0f);
    slack[at (goal.x, goal.y)]   = juce::jmax (slack[at (goal.x, goal.y)], 0.0f);

    // Cost of running beside an aggressor lane, per cell and direction (only computed near the lanes).
    std::vector<float> laneCost;
    if (aggressorPenalty > 0.0 && ! board.aggressors.empty())
    {
        laneCost.assign ((size_t) (W * H * 8), 0.0f);
        std::vector<char> near ((size_t) (W * H), 0);
        const int reachCells = (int) TraceBoard::couplingReachCells + 2;

        for (const auto& lane : board.aggressors)
            for (size_t j = 1; j < lane.points.size(); ++j)
            {
                const auto a = lane.points[j - 1], b = lane.points[j];
                for (int y = juce::jmax (0, juce::jmin (a.y, b.y) - reachCells); y <= juce::jmin (H - 1, juce::jmax (a.y, b.y) + reachCells); ++y)
                    for (int x = juce::jmax (0, juce::jmin (a.x, b.x) - reachCells); x <= juce::jmin (W - 1, juce::jmax (a.x, b.x) + reachCells); ++x)
                        near[at (x, y)] = 1;
            }

        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (near[at (x, y)])
                    for (int d = 0; d < 8; ++d)
                    {
                        const int nx = x + directionX[d], ny = y + directionY[d];
                        if (nx >= 0 && ny >= 0 && nx < W && ny < H)
                            laneCost[at (x, y) * 8 + (size_t) d] = (float) (aggressorPenalty * board.stepCouplingToAggressors ({ x, y }, { nx, ny }));
                    }
    }

    // A* over (cell, incoming direction).
    constexpr int states = W * H * 9;
    std::vector<float> cost ((size_t) states, std::numeric_limits<float>::max());
    std::vector<int> parent ((size_t) states, -1);
    const auto index = [] (int x, int y, int d) { return (y * W + x) * 9 + d; };
    const auto heuristic = [&] (int x, int y)
    {
        const float dx = (float) std::abs (goal.x - x), dy = (float) std::abs (goal.y - y);
        return juce::jmax (dx, dy) + 0.41421356f * juce::jmin (dx, dy);
    };

    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;

    const int startIndex = index (start.x, start.y, 8);
    cost[(size_t) startIndex] = 0.0f;
    open.push ({ heuristic (start.x, start.y), startIndex });

    int goalIndex = -1;

    while (! open.empty())
    {
        const auto [f, current] = open.top();
        open.pop();

        const int d = current % 9, cell = current / 9, x = cell % W, y = cell / W;
        if (f - heuristic (x, y) > cost[(size_t) current] + 1.0e-3f)
            continue;   // stale entry

        if (x == goal.x && y == goal.y)
        {
            goalIndex = current;
            break;
        }

        for (int nd = 0; nd < 8; ++nd)
        {
            if (d < 8 && directionX[d] * directionX[nd] + directionY[d] * directionY[nd] < 0)
                continue;   // no turns sharper than 90 degrees

            const int nx = x + directionX[nd], ny = y + directionY[nd];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H)
                continue;

            const float slackHere = slack[at (x, y)], slackThere = slack[at (nx, ny)];
            if (slackThere < 0.0f || (slackHere < 0.0f && ! (x == start.x && y == start.y)))
                continue;

            if (juce::jmin (slackHere, slackThere) < 1.6f && ! board.segmentClear (trace, { x, y }, { nx, ny }, other))
                continue;

            const float step = (directionX[nd] != 0 && directionY[nd] != 0) ? 1.41421356f : 1.0f;
            const float turn = d < 8 && d != nd ? 0.05f : 0.0f;   // a whisper, to prefer few corners between equal routes
            const float lanePenalty = laneCost.empty() ? 0.0f : laneCost[at (x, y) * 8 + (size_t) nd];
            const float newCost = cost[(size_t) current] + step + turn + lanePenalty;
            const int next = index (nx, ny, nd);

            if (newCost < cost[(size_t) next])
            {
                cost[(size_t) next] = newCost;
                parent[(size_t) next] = current;
                open.push ({ newCost + heuristic (nx, ny), next });
            }
        }
    }

    if (goalIndex < 0)
        return false;

    // Walk back, keeping only the corners.
    std::vector<Point> cells;
    for (int s = goalIndex; s >= 0; s = parent[(size_t) s])
    {
        const int cell = s / 9;
        cells.push_back ({ cell % W, cell / W });
    }
    std::reverse (cells.begin(), cells.end());

    result.clear();
    result.push_back (cells.front());
    for (size_t i = 1; i + 1 < cells.size(); ++i)
    {
        const int dx1 = cells[i].x - cells[i - 1].x, dy1 = cells[i].y - cells[i - 1].y;
        const int dx2 = cells[i + 1].x - cells[i].x, dy2 = cells[i + 1].y - cells[i].y;
        if (dx1 != dx2 || dy1 != dy2)
            result.push_back (cells[i]);
    }
    result.push_back (cells.back());

    return board.isValidPolyline (trace, result, other);
}

// ---------------------------------------------------------------------------------
// Meanders: replace part of a straight run of the trace with a bump that adds length.
//   diagonal bump:   out at 45 degrees, across, back at 45 degrees: adds 2h(sqrt2 - 1) = 0.828 h cells
//   orthogonal bump: out, across, back:                             adds 2h cells
// Mixing the two lets the total be tuned finely (0.828 h steps mod 2 are nearly dense).
bool TraceRouter::matchLengths (TraceBoard& board, int trace, double extraCells)
{
    const auto& other = board.getTrace (1 - trace);
    Polyline vertices = board.getTrace (trace);
    double remaining = extraCells;
    constexpr int maxHeight = 24;

    for (int guard = 0; guard < 60 && remaining > matchToleranceCells; ++guard)
    {
        bool placed = false;

        // Prefer the largest bump that still fits inside what is left.
        for (int h = maxHeight; h >= 1 && ! placed; --h)
            for (int type = 0; type < 2 && ! placed; ++type)   // 0 = diagonal, 1 = orthogonal
            {
                const double increment = type == 0 ? h * 0.82842712 : 2.0 * h;
                if (increment > remaining + matchToleranceCells)
                    continue;

                // longest segments first
                std::vector<size_t> order;
                for (size_t i = 1; i < vertices.size(); ++i)
                    order.push_back (i);
                std::sort (order.begin(), order.end(), [&] (size_t a, size_t b)
                           { return TraceBoard::segmentLength (vertices[a - 1], vertices[a]) > TraceBoard::segmentLength (vertices[b - 1], vertices[b]); });

                for (size_t i : order)
                {
                    const Point v0 = vertices[i - 1], v1 = vertices[i];
                    const int dx = v1.x - v0.x, dy = v1.y - v0.y;
                    if (dx != 0 && dy != 0)
                        continue;   // bumps go on straight orthogonal runs

                    const int length = std::abs (dx) + std::abs (dy);
                    const int ux = (dx > 0) - (dx < 0), uy = (dy > 0) - (dy < 0);

                    for (int extraWidth : { 0, 2, 4 })
                    {
                        const int w = type == 0 ? 2 * h + extraWidth : 2 + extraWidth;
                        if (w > length - 2)
                            continue;

                        for (int offset = 1; offset + w <= length - 1 && ! placed; ++offset)
                            for (int side : { 1, -1 })
                            {
                                const int nx = -uy * side, ny = ux * side;   // normal
                                const Point a { v0.x + ux * offset, v0.y + uy * offset };
                                const Point b { a.x + ux * w, a.y + uy * w };

                                Polyline bump;
                                if (type == 0)
                                {
                                    bump = { { a.x + (ux + nx) * h, a.y + (uy + ny) * h },
                                             { a.x + ux * (w - h) + nx * h, a.y + uy * (w - h) + ny * h } };
                                    if (bump[0] == bump[1])
                                        bump.pop_back();
                                }
                                else
                                    bump = { { a.x + nx * h, a.y + ny * h }, { a.x + nx * h + ux * w, a.y + ny * h + uy * w } };

                                Polyline candidate (vertices.begin(), vertices.begin() + (std::ptrdiff_t) i);
                                candidate.push_back (a);
                                candidate.insert (candidate.end(), bump.begin(), bump.end());
                                candidate.push_back (b);
                                candidate.insert (candidate.end(), vertices.begin() + (std::ptrdiff_t) i, vertices.end());

                                // cheap rejection first: the new pieces against obstacles and the other trace
                                bool clear = true;
                                for (size_t k = i; k + 1 < candidate.size() && k < i + bump.size() + 2 && clear; ++k)
                                    clear = board.segmentClear (trace, candidate[k], candidate[k + 1], other);

                                if (clear && board.isValidPolyline (trace, candidate, other))
                                {
                                    vertices = std::move (candidate);
                                    remaining -= increment;
                                    placed = true;
                                    break;
                                }
                            }

                        if (placed) break;
                    }

                    if (placed) break;
                }
            }

        if (! placed)
            break;
    }

    board.setTrace (trace, vertices);
    return std::abs (remaining) <= matchToleranceCells + 1.0e-9;
}

TraceRouter::Result TraceRouter::solve (TraceBoard& board)
{
    const auto start = juce::Time::getMillisecondCounterHiRes();
    Result result;

    board.clearTraces();

    Polyline best[2];
    double bestTotal = 1.0e18;

    for (int first : { TraceBoard::plus, TraceBoard::minus })
    {
        const int second = 1 - first;
        Polyline a, b;

        if (! route (board, first, { board.inPad[second] }, a))
            continue;
        if (! route (board, second, a, b))
            continue;

        const double total = TraceBoard::polylineLengthCells (a) + TraceBoard::polylineLengthCells (b);
        if (total < bestTotal)
        {
            bestTotal = total;
            best[first] = a;
            best[second] = b;
        }
    }

    if (bestTotal < 1.0e17)
    {
        board.setTrace (TraceBoard::plus, best[TraceBoard::plus]);
        board.setTrace (TraceBoard::minus, best[TraceBoard::minus]);
        result.routed = true;

        const double lp = TraceBoard::polylineLengthCells (board.getTrace (TraceBoard::plus));
        const double ln = TraceBoard::polylineLengthCells (board.getTrace (TraceBoard::minus));
        const int shorter = lp < ln ? TraceBoard::plus : TraceBoard::minus;
        result.matched = matchLengths (board, shorter, std::abs (lp - ln));
    }

    for (int t = 0; t < 2; ++t)
        result.lengthInches[t] = board.lengthInches (t);
    result.skewInches = std::abs (result.lengthInches[0] - result.lengthInches[1]);
    result.milliseconds = juce::Time::getMillisecondCounterHiRes() - start;
    return result;
}

bool TraceRouter::generateGame (TraceBoard& board, int seed, int difficulty)
{
    juce::Random random (seed);
    difficulty = juce::jlimit (1, TraceGame::maxDifficulty, difficulty);

    for (int attempt = 0; attempt < 120; ++attempt)
    {
        board.resetSandbox();
        board.mode = TraceBoard::Mode::game;
        board.gameSeed = seed;
        board.difficulty = difficulty;
        board.setPads (random.nextInt ({ 10, TraceBoard::height - 22 }), random.nextInt ({ 10, TraceBoard::height - 22 }));
        board.clearTraces();

        // Aggressor lanes first (on the empty board), then obstacles that keep clear of them.
        const int lanes = (difficulty + 1) / 2;
        for (int tries = 0; tries < 200 && (int) board.aggressors.size() < lanes; ++tries)
        {
            TraceBoard::Aggressor lane;
            const int x0 = random.nextInt ({ 12, 50 }), y0 = random.nextInt ({ 8, TraceBoard::height - 8 });
            const int x1 = x0 + random.nextInt ({ 30, 60 });
            const int jog = random.nextInt ({ -16, 17 });
            const int x2 = juce::jmin (TraceBoard::width - 12, x1 + std::abs (jog) + random.nextInt ({ 25, 60 }));
            lane.points = { { x0, y0 }, { x1, y0 } };
            if (jog != 0)
                lane.points.push_back ({ x1 + std::abs (jog), y0 + jog });
            lane.points.push_back ({ x2, lane.points.back().y });
            lane.rateRatio = 1.0 + 0.03 * (random.nextDouble() * 2.0 - 1.0);
            lane.seed = random.nextInt (1000);

            bool ok = lane.points.back().y >= 6 && lane.points.back().y <= TraceBoard::height - 6;
            for (size_t j = 1; j < lane.points.size() && ok; ++j)
            {
                for (auto pad : { board.inPad[0], board.inPad[1], board.outPad[0], board.outPad[1] })
                {
                    const double d = TraceBoard::segmentDistance (pad.x, pad.y, pad.x, pad.y, lane.points[j - 1].x, lane.points[j - 1].y, lane.points[j].x, lane.points[j].y);
                    ok = ok && d > 7.0;
                }
                for (const auto& other : board.aggressors)
                    for (size_t k = 1; k < other.points.size() && ok; ++k)
                        ok = ok && TraceBoard::segmentDistance (lane.points[j - 1].x, lane.points[j - 1].y, lane.points[j].x, lane.points[j].y,
                                                                 other.points[k - 1].x, other.points[k - 1].y, other.points[k].x, other.points[k].y) > 6.0;
            }

            if (ok)
                board.aggressors.push_back (std::move (lane));
        }


        const int wanted = juce::jmax (3, TraceGame::obstacleCount (difficulty) - attempt / 25);
        for (int tries = 0; tries < 400 && (int) board.obstacles.size() < wanted; ++tries)
        {
            TraceBoard::Obstacle o;
            o.w = random.nextInt ({ 5, 19 });
            o.h = random.nextInt ({ 8, 37 });
            o.x = random.nextInt ({ 16, TraceBoard::width - 16 - o.w });
            o.y = random.nextInt ({ 3, TraceBoard::height - 3 - o.h });

            bool overlaps = false;
            for (const auto& e : board.obstacles)
                overlaps = overlaps || (o.x < e.x + e.w + 5 && e.x < o.x + o.w + 5 && o.y < e.y + e.h + 5 && e.y < o.y + o.h + 5);

            for (const auto& lane : board.aggressors)
                for (size_t j = 1; j < lane.points.size(); ++j)
                    overlaps = overlaps || TraceBoard::distanceToRect (lane.points[j - 1].x, lane.points[j - 1].y, lane.points[j].x, lane.points[j].y, o) < 3.0;

            if (! overlaps)
                board.obstacles.push_back (o);
        }

        const auto solution = solve (board);
        if (! solution.routed || ! solution.matched)
            continue;

        // A board where the straight line is clear is not much of a puzzle.
        const double straight = TraceBoard::segmentLength (board.inPad[0], board.outPad[0]) * TraceBoard::pitchInches;
        if (attempt < 60 && solution.lengthInches[0] < straight * 1.06 && solution.lengthInches[1] < straight * 1.06)
            continue;

        board.parLengthInches = 0.5 * (solution.lengthInches[0] + solution.lengthInches[1]);
        board.parCouplingIndex = board.differentialCouplingIndex();
        board.clearTraces();
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------------
TraceGame::Score TraceGame::evaluate (const TraceBoard& board, double skewPs, double unitIntervalPs)
{
    Score s;
    s.complete = board.isComplete (TraceBoard::plus) && board.isComplete (TraceBoard::minus);
    s.skewUI = unitIntervalPs > 0.0 ? std::abs (skewPs) / unitIntervalPs : 0.0;
    s.skewBudgetUI = skewBudgetUI (board.difficulty);
    s.averageLength = 0.5 * (board.effectiveLengthInches (TraceBoard::plus) + board.effectiveLengthInches (TraceBoard::minus));
    s.lengthBudget = board.parLengthInches * lengthBudgetFactor (board.difficulty);

    s.hasCrosstalk = ! board.aggressors.empty();
    s.crosstalkIndex = board.differentialCouplingIndex();
    s.crosstalkBudget = board.parCouplingIndex * crosstalkBudgetFactor (board.difficulty) + crosstalkAllowance;
    const bool crosstalkOk = ! s.hasCrosstalk || s.crosstalkIndex <= s.crosstalkBudget;

    s.passed = s.complete && s.skewUI <= s.skewBudgetUI && s.averageLength <= s.lengthBudget && crosstalkOk;
    if (s.passed)
    {
        s.stars = 1;
        if (s.averageLength <= board.parLengthInches * 1.15) s.stars = 2;
        const bool quiet = ! s.hasCrosstalk || s.crosstalkIndex <= board.parCouplingIndex * 1.15 + 0.05;
        if (s.averageLength <= board.parLengthInches * 1.05 && s.skewUI <= 0.5 * s.skewBudgetUI && quiet) s.stars = 3;
    }

    return s;
}
