#include "TraceBoard.h"

namespace
{
    double pointSegmentDistance (double px, double py, double ax, double ay, double bx, double by)
    {
        const double dx = bx - ax, dy = by - ay;
        const double lengthSquared = dx * dx + dy * dy;
        double t = lengthSquared > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / lengthSquared : 0.0;
        t = juce::jlimit (0.0, 1.0, t);
        return std::hypot (px - (ax + t * dx), py - (ay + t * dy));
    }

    double cross (double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

    bool segmentsProperlyIntersect (double ax, double ay, double bx, double by,
                                    double cx, double cy, double dx, double dy)
    {
        const double d1 = cross (bx - ax, by - ay, cx - ax, cy - ay);
        const double d2 = cross (bx - ax, by - ay, dx - ax, dy - ay);
        const double d3 = cross (dx - cx, dy - cy, ax - cx, ay - cy);
        const double d4 = cross (dx - cx, dy - cy, bx - cx, by - cy);
        return d1 * d2 < 0.0 && d3 * d4 < 0.0;   // strictly opposite sides both ways
    }
}

TraceBoard::TraceBoard()
{
    resetSandbox();
}

void TraceBoard::setPads (int inY, int outY)
{
    inPad[plus]   = { 4, inY };
    inPad[minus]  = { 4, inY + 6 };
    outPad[plus]  = { width - 5, outY };
    outPad[minus] = { width - 5, outY + 6 };
}

void TraceBoard::clearTraces()
{
    clear (plus);
    clear (minus);
}

void TraceBoard::resetSandbox()
{
    mode = Mode::sandbox;
    obstacles.clear();
    aggressors.clear();
    parLengthInches = 0.0;
    parCouplingIndex = 0.0;
    setPads (height / 2 - 3, height / 2 - 3);
    clearTraces();
}

void TraceBoard::setTrace (int trace, Polyline vertices)
{
    traces[trace] = std::move (vertices);
    if (traces[trace].empty() || traces[trace].front() != inPad[trace])
        traces[trace].insert (traces[trace].begin(), inPad[trace]);
}

void TraceBoard::clear (int trace)
{
    traces[trace] = { inPad[trace] };
}

bool TraceBoard::isComplete (int trace) const
{
    return traces[trace].size() > 1 && traces[trace].back() == outPad[trace];
}

double TraceBoard::segmentLength (Point a, Point b) noexcept
{
    return std::hypot ((double) (b.x - a.x), (double) (b.y - a.y));
}

double TraceBoard::polylineLengthCells (const Polyline& vertices) noexcept
{
    double total = 0.0;
    for (size_t i = 1; i < vertices.size(); ++i)
        total += segmentLength (vertices[i - 1], vertices[i]);
    return total;
}

double TraceBoard::lengthInches (int trace) const
{
    return polylineLengthCells (traces[trace]) * pitchInches;
}

double TraceBoard::effectiveLengthInches (int trace) const
{
    double cells = polylineLengthCells (traces[trace]);
    if (! isComplete (trace))
        cells += segmentLength (traces[trace].back(), outPad[trace]);
    return cells * pitchInches;
}

// ---------------------------------------------------------------------------------
bool TraceBoard::isLegalStep (Point from, Point to) noexcept
{
    const int dx = to.x - from.x, dy = to.y - from.y;
    return (dx != 0 || dy != 0) && (dx == 0 || dy == 0 || std::abs (dx) == std::abs (dy));
}

// Turns of 0, 45 or 90 degrees are fine; sharper ones (acute angles, reversals) are not.
bool TraceBoard::isAcceptableTurn (Point a, Point b, Point c) noexcept
{
    const long dot = (long) (b.x - a.x) * (c.x - b.x) + (long) (b.y - a.y) * (c.y - b.y);
    return dot >= 0;
}

double TraceBoard::segmentDistance (double ax, double ay, double bx, double by,
                                    double cx, double cy, double dx, double dy) noexcept
{
    if (segmentsProperlyIntersect (ax, ay, bx, by, cx, cy, dx, dy))
        return 0.0;

    return juce::jmin (juce::jmin (pointSegmentDistance (ax, ay, cx, cy, dx, dy), pointSegmentDistance (bx, by, cx, cy, dx, dy)),
                       juce::jmin (pointSegmentDistance (cx, cy, ax, ay, bx, by), pointSegmentDistance (dx, dy, ax, ay, bx, by)));
}

double TraceBoard::distanceToRect (double ax, double ay, double bx, double by, const Obstacle& r) noexcept
{
    const double x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    const auto inside = [&] (double px, double py) { return px > x0 && px < x1 && py > y0 && py < y1; };

    if (inside (ax, ay) || inside (bx, by))
        return 0.0;

    return juce::jmin (juce::jmin (segmentDistance (ax, ay, bx, by, x0, y0, x1, y0), segmentDistance (ax, ay, bx, by, x1, y0, x1, y1)),
                       juce::jmin (segmentDistance (ax, ay, bx, by, x1, y1, x0, y1), segmentDistance (ax, ay, bx, by, x0, y1, x0, y0)));
}

bool TraceBoard::segmentClear (int trace, Point a, Point b, const Polyline& other) const
{
    const int margin = 1;
    for (auto p : { a, b })
        if (p.x < margin || p.y < margin || p.x > width - 1 - margin || p.y > height - 1 - margin)
            return false;

    for (const auto& o : obstacles)
        if (distanceToRect (a.x, a.y, b.x, b.y, o) < obstacleClearance)
            return false;

    const int otherTrace = 1 - trace;
    for (auto pad : { inPad[otherTrace], outPad[otherTrace] })
        if (pointSegmentDistance (pad.x, pad.y, a.x, a.y, b.x, b.y) < traceClearance)
            return false;

    for (size_t i = 1; i < other.size(); ++i)
        if (segmentDistance (a.x, a.y, b.x, b.y, other[i - 1].x, other[i - 1].y, other[i].x, other[i].y) < traceClearance)
            return false;

    return true;
}

bool TraceBoard::isValidPolyline (int trace, const Polyline& vertices, const Polyline& other) const
{
    if (vertices.empty() || vertices.front() != inPad[trace])
        return false;

    for (size_t i = 1; i < vertices.size(); ++i)
    {
        if (! isLegalStep (vertices[i - 1], vertices[i]) || ! segmentClear (trace, vertices[i - 1], vertices[i], other))
            return false;

        if (i >= 2 && ! isAcceptableTurn (vertices[i - 2], vertices[i - 1], vertices[i]))
            return false;
    }

    // Own copper: non-adjacent segments must keep their distance.
    for (size_t i = 1; i < vertices.size(); ++i)
        for (size_t j = i + 2; j < vertices.size(); ++j)
            if (segmentDistance (vertices[i - 1].x, vertices[i - 1].y, vertices[i].x, vertices[i].y,
                                 vertices[j - 1].x, vertices[j - 1].y, vertices[j].x, vertices[j].y) < traceClearance)
                return false;

    return true;
}

bool TraceBoard::canAppend (int trace, const Polyline& newVertices) const
{
    if (newVertices.empty() || isComplete (trace))
        return false;

    auto candidate = traces[trace];
    candidate.insert (candidate.end(), newVertices.begin(), newVertices.end());
    return isValidPolyline (trace, candidate, traces[1 - trace]);
}

bool TraceBoard::append (int trace, const Polyline& newVertices)
{
    if (! canAppend (trace, newVertices))
        return false;

    traces[trace].insert (traces[trace].end(), newVertices.begin(), newVertices.end());
    return true;
}

bool TraceBoard::undo (int trace)
{
    if (traces[trace].size() <= 1)
        return false;

    traces[trace].pop_back();
    return true;
}

// ---------------------------------------------------------------------------------
double TraceBoard::couplingWeight (double spacingCells) noexcept
{
    if (spacingCells > couplingReachCells)
        return 0.0;

    return std::exp (-(juce::jmax (spacingCells, traceClearance) - traceClearance) / 3.0);
}

std::vector<TraceBoard::CouplingRun> TraceBoard::couplingRuns() const
{
    std::vector<CouplingRun> runs;

    // An unfinished trace counts as a straight run to its pad, the same assumption the audio makes for length.
    auto effective = [this] (int trace)
    {
        auto v = traces[trace];
        if (! isComplete (trace) && v.back() != outPad[trace])
            v.push_back (outPad[trace]);
        return v;
    };
    const auto p = effective (plus);
    const auto n = effective (minus);

    for (size_t i = 1; i < p.size(); ++i)
        for (size_t j = 1; j < n.size(); ++j)
        {
            const int adx = p[i].x - p[i - 1].x, ady = p[i].y - p[i - 1].y;
            const int bdx = n[j].x - n[j - 1].x, bdy = n[j].y - n[j - 1].y;

            if (adx * bdy - ady * bdx != 0)
                continue;   // not parallel

            const double length = std::hypot ((double) adx, (double) ady);
            const double ux = adx / length, uy = ady / length;

            // where does the - segment sit along the + segment, and how far off to the side?
            const double t0 = (n[j - 1].x - p[i - 1].x) * ux + (n[j - 1].y - p[i - 1].y) * uy;
            const double t1 = (n[j].x - p[i - 1].x) * ux + (n[j].y - p[i - 1].y) * uy;
            const double overlap = juce::jmin (length, juce::jmax (t0, t1)) - juce::jmax (0.0, juce::jmin (t0, t1));
            const double spacing = std::abs (ux * (n[j - 1].y - p[i - 1].y) - uy * (n[j - 1].x - p[i - 1].x));

            if (overlap > 1.0e-9 && spacing <= couplingReachCells)
            {
                const double from = juce::jmax (0.0, juce::jmin (t0, t1)), to = juce::jmin (length, juce::jmax (t0, t1));
                const double side = ux * (n[j - 1].y - p[i - 1].y) - uy * (n[j - 1].x - p[i - 1].x);   // signed distance to the - trace
                CouplingRun run;
                run.x0 = p[i - 1].x + ux * from;  run.y0 = p[i - 1].y + uy * from;
                run.x1 = p[i - 1].x + ux * to;    run.y1 = p[i - 1].y + uy * to;
                run.offsetX = -uy * side;         run.offsetY = ux * side;
                run.overlapCells = overlap;       run.spacingCells = spacing;
                run.weight = couplingWeight (spacing);
                runs.push_back (run);
            }
        }

    return runs;
}

double TraceBoard::coupledLengthInches() const
{
    double total = 0.0;
    for (const auto& r : couplingRuns())
        total += r.overlapCells;
    return total * pitchInches;
}

double TraceBoard::weightedCouplingInches() const
{
    double total = 0.0;
    for (const auto& r : couplingRuns())
        total += r.overlapCells * r.weight;
    return total * pitchInches;
}

// ---------------------------------------------------------------------------------
namespace
{
    // spacing-weighted length over which segment (a0->a1) faces (b0->b1) when they are parallel
    double parallelCoupling (double ax0, double ay0, double ax1, double ay1, double bx0, double by0, double bx1, double by1)
    {
        const double adx = ax1 - ax0, ady = ay1 - ay0, bdx = bx1 - bx0, bdy = by1 - by0;
        if (std::abs (adx * bdy - ady * bdx) > 1.0e-9)
            return 0.0;

        const double length = std::hypot (adx, ady);
        if (length < 1.0e-9)
            return 0.0;

        const double ux = adx / length, uy = ady / length;
        const double t0 = (bx0 - ax0) * ux + (by0 - ay0) * uy, t1 = (bx1 - ax0) * ux + (by1 - ay0) * uy;
        const double overlap = juce::jmin (length, juce::jmax (t0, t1)) - juce::jmax (0.0, juce::jmin (t0, t1));
        const double spacing = std::abs (ux * (by0 - ay0) - uy * (bx0 - ax0));
        return overlap > 0.0 ? overlap * TraceBoard::couplingWeight (spacing) : 0.0;
    }
}

std::vector<TraceBoard::AggressorCoupling> TraceBoard::aggressorCoupling() const
{
    std::vector<AggressorCoupling> result (aggressors.size());

    for (int t = 0; t < 2; ++t)
    {
        auto v = traces[t];
        if (! isComplete (t) && v.back() != outPad[t])
            v.push_back (outPad[t]);

        for (size_t k = 0; k < aggressors.size(); ++k)
        {
            double total = 0.0;
            const auto& lane = aggressors[k].points;

            for (size_t i = 1; i < v.size(); ++i)
                for (size_t j = 1; j < lane.size(); ++j)
                    total += parallelCoupling (v[i - 1].x, v[i - 1].y, v[i].x, v[i].y, lane[j - 1].x, lane[j - 1].y, lane[j].x, lane[j].y);

            (t == plus ? result[k].plus : result[k].minus) = total * pitchInches;
        }
    }

    return result;
}

double TraceBoard::differentialCouplingIndex() const
{
    double sum = 0.0;
    for (const auto& c : aggressorCoupling())
        sum += (c.plus - c.minus) * (c.plus - c.minus);
    return std::sqrt (sum);
}

double TraceBoard::stepCouplingToAggressors (Point from, Point to) const
{
    double total = 0.0;
    for (const auto& lane : aggressors)
        for (size_t j = 1; j < lane.points.size(); ++j)
            total += parallelCoupling (from.x, from.y, to.x, to.y, lane.points[j - 1].x, lane.points[j - 1].y, lane.points[j].x, lane.points[j].y);
    return total;   // cells
}

// ---------------------------------------------------------------------------------
double TraceBoard::totalLengthCells (int trace) const
{
    double cells = polylineLengthCells (traces[trace]);
    if (! isComplete (trace))
        cells += segmentLength (traces[trace].back(), outPad[trace]);
    return cells;
}

std::vector<float> TraceBoard::impedanceProfile (int trace, int sections, double strength, double connectorPercent) const
{
    constexpr double z0 = 50.0;
    std::vector<float> z ((size_t) juce::jmax (0, sections), (float) z0);
    if (sections < 2 || strength <= 0.0)
        return z;

    const auto effective = [this] (int t)
    {
        auto v = traces[t];
        if (! isComplete (t) && v.back() != outPad[t])
            v.push_back (outPad[t]);
        return v;
    };
    const auto mine = effective (trace), other = effective (1 - trace);
    const double total = polylineLengthCells (mine);
    if (total < 1.0)
        return z;

    std::vector<double> cumulative { 0.0 };
    for (size_t i = 1; i < mine.size(); ++i)
        cumulative.push_back (cumulative.back() + segmentLength (mine[i - 1], mine[i]));

    const auto pointAt = [&] (double arc)
    {
        size_t i = 1;
        while (i + 1 < mine.size() && cumulative[i] < arc)
            ++i;
        const double span = juce::jmax (1.0e-9, cumulative[i] - cumulative[i - 1]);
        const double f = juce::jlimit (0.0, 1.0, (arc - cumulative[i - 1]) / span);
        return std::pair<double, double> { mine[i - 1].x + f * (mine[i].x - mine[i - 1].x), mine[i - 1].y + f * (mine[i].y - mine[i - 1].y) };
    };

    const double referenceWeight = couplingWeight (6.0);
    for (int k = 0; k < sections; ++k)
    {
        const auto [px, py] = pointAt ((k + 0.5) / sections * total);

        double nearest = 1.0e9;
        for (size_t j = 1; j < other.size(); ++j)
        {
            const double dx = other[j].x - other[j - 1].x, dy = other[j].y - other[j - 1].y, len2 = dx * dx + dy * dy;
            double f = len2 > 0.0 ? ((px - other[j - 1].x) * dx + (py - other[j - 1].y) * dy) / len2 : 0.0;
            f = juce::jlimit (0.0, 1.0, f);
            nearest = juce::jmin (nearest, std::hypot (px - (other[j - 1].x + f * dx), py - (other[j - 1].y + f * dy)));
        }

        z[(size_t) k] = (float) (z0 * (1.0 - 0.25 * strength * (couplingWeight (nearest) - referenceWeight)));
    }

    // corners: a small capacitive dip in the section containing the vertex
    for (size_t i = 1; i + 1 < mine.size(); ++i)
    {
        const double ax = mine[i].x - mine[i - 1].x, ay = mine[i].y - mine[i - 1].y;
        const double bx = mine[i + 1].x - mine[i].x, by = mine[i + 1].y - mine[i].y;
        const double angle = std::abs (std::atan2 (ax * by - ay * bx, ax * bx + ay * by));   // 0 = straight on
        const double dip = angle > 1.2 ? 0.05 : (angle > 0.5 ? 0.02 : 0.0);
        if (dip > 0.0)
        {
            const int k = juce::jlimit (0, sections - 1, (int) (cumulative[i] / total * sections));
            z[(size_t) k] = (float) (z[(size_t) k] * (1.0 - dip * strength));
        }
    }

    // pads / connectors at both ends
    const double quarterInch = 5.0;   // cells
    const int endSections = juce::jlimit (1, sections / 4, (int) std::lround (quarterInch / total * sections));
    for (int i = 0; i < endSections; ++i)
    {
        z[(size_t) i] = (float) (z[(size_t) i] * (1.0 + 0.01 * connectorPercent * strength));
        z[(size_t) (sections - 1 - i)] = (float) (z[(size_t) (sections - 1 - i)] * (1.0 + 0.01 * connectorPercent * strength));
    }

    return z;
}

// ---------------------------------------------------------------------------------
std::unique_ptr<juce::XmlElement> TraceBoard::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("BOARD");
    xml->setAttribute ("mode", mode == Mode::game ? "game" : "sandbox");
    xml->setAttribute ("seed", gameSeed);
    xml->setAttribute ("difficulty", difficulty);
    xml->setAttribute ("par", parLengthInches);
    xml->setAttribute ("parCoupling", parCouplingIndex);

    for (int t = 0; t < 2; ++t)
    {
        auto* pad = xml->createNewChildElement ("PADS");
        pad->setAttribute ("trace", t);
        pad->setAttribute ("inX", inPad[t].x);  pad->setAttribute ("inY", inPad[t].y);
        pad->setAttribute ("outX", outPad[t].x); pad->setAttribute ("outY", outPad[t].y);
    }

    for (const auto& o : obstacles)
    {
        auto* e = xml->createNewChildElement ("OBSTACLE");
        e->setAttribute ("x", o.x); e->setAttribute ("y", o.y); e->setAttribute ("w", o.w); e->setAttribute ("h", o.h);
    }

    for (const auto& lane : aggressors)
    {
        juce::String text;
        for (auto p : lane.points)
            text << p.x << "," << p.y << " ";

        auto* e = xml->createNewChildElement ("AGGRESSOR");
        e->setAttribute ("ratio", lane.rateRatio);
        e->setAttribute ("seed", lane.seed);
        e->setAttribute ("points", text.trim());
    }

    for (int t = 0; t < 2; ++t)
    {
        juce::String text;
        for (auto p : traces[t])
            text << p.x << "," << p.y << " ";

        auto* e = xml->createNewChildElement ("TRACE");
        e->setAttribute ("trace", t);
        e->setAttribute ("points", text.trim());
    }

    return xml;
}

void TraceBoard::fromXml (const juce::XmlElement& xml)
{
    mode = xml.getStringAttribute ("mode") == "game" ? Mode::game : Mode::sandbox;
    gameSeed = xml.getIntAttribute ("seed");
    difficulty = xml.getIntAttribute ("difficulty", 3);
    parLengthInches = xml.getDoubleAttribute ("par");
    parCouplingIndex = xml.getDoubleAttribute ("parCoupling");
    obstacles.clear();
    aggressors.clear();

    for (auto* pad : xml.getChildWithTagNameIterator ("PADS"))
    {
        const int t = juce::jlimit (0, 1, pad->getIntAttribute ("trace"));
        inPad[t]  = { pad->getIntAttribute ("inX"),  pad->getIntAttribute ("inY") };
        outPad[t] = { pad->getIntAttribute ("outX"), pad->getIntAttribute ("outY") };
    }

    for (auto* e : xml.getChildWithTagNameIterator ("OBSTACLE"))
        obstacles.push_back ({ e->getIntAttribute ("x"), e->getIntAttribute ("y"), e->getIntAttribute ("w"), e->getIntAttribute ("h") });

    for (auto* e : xml.getChildWithTagNameIterator ("AGGRESSOR"))
    {
        Aggressor lane;
        lane.rateRatio = e->getDoubleAttribute ("ratio", 1.0);
        lane.seed = e->getIntAttribute ("seed");

        for (auto& token : juce::StringArray::fromTokens (e->getStringAttribute ("points"), " ", ""))
        {
            auto parts = juce::StringArray::fromTokens (token, ",", "");
            if (parts.size() == 2)
                lane.points.push_back ({ parts[0].getIntValue(), parts[1].getIntValue() });
        }

        if (lane.points.size() >= 2 && (int) aggressors.size() < maxAggressors)
            aggressors.push_back (std::move (lane));
    }

    clearTraces();

    for (auto* e : xml.getChildWithTagNameIterator ("TRACE"))
    {
        const int t = juce::jlimit (0, 1, e->getIntAttribute ("trace"));
        Polyline points;

        for (auto& token : juce::StringArray::fromTokens (e->getStringAttribute ("points"), " ", ""))
        {
            auto parts = juce::StringArray::fromTokens (token, ",", "");
            if (parts.size() == 2)
                points.push_back ({ parts[0].getIntValue(), parts[1].getIntValue() });
        }

        // Only accept it if it is still a legal drawing on this board.
        if (! points.empty() && isValidPolyline (t, points, traces[1 - t]))
            traces[t] = std::move (points);
    }
}
