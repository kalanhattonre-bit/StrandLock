#include "Node.h"

#include "Num.h"
#include "core/Curves.h"

#include <algorithm>
#include <cmath>

namespace sl::model
{
    const char* code (NodeShape shape) noexcept
    {
        const auto i = static_cast<std::size_t> (shape);
        return i < static_cast<std::size_t> (kNumNodeShapes) ? kNodeShapeCodes[i] : "";
    }

    std::optional<NodeShape> parseNodeShape (std::string_view s) noexcept
    {
        for (int i = 0; i < kNumNodeShapes; ++i)
            if (s == kNodeShapeCodes[i])
                return static_cast<NodeShape> (i);
        return std::nullopt;
    }

    Node mkNode (double t, double v, NodeShape shape, double bend) noexcept
    {
        Node q;
        q.t = t;
        q.v = v;
        q.shape = shape;
        q.bend = bend;
        return q;
    }

    NodeList flat (double v)
    {
        return { mkNode (0.0, v), mkNode (1.0, v) };
    }

    namespace
    {
        bool validShape (NodeShape s) noexcept { return static_cast<int> (s) < kNumNodeShapes; }

        /** The list itself when nodesSorted, else its normalized copy in scratch (normalizeNodes (nodes): def 0.5). */
        const NodeList& usable (const NodeList& nodes, NodeList& scratch)
        {
            if (nodesSorted (nodes))
                return nodes;
            scratch = normalizeNodes (nodes);
            return scratch;
        }
    }

    NodeList normalizeNodes (const NodeList& nodes, double def)
    {
        const double dv = def;
        if (nodes.empty())
            return flat (dv);

        NodeList out;
        out.reserve (nodes.size());
        for (const Node& q : nodes)
            out.push_back (mkNode (clamp (num (q.t, 0.0), 0.0, 1.0), clamp (num (q.v, dv), 0.0, 1.0),
                                   validShape (q.shape) ? q.shape : NodeShape::smooth, clamp (num (q.bend, 0.0), -1.0, 1.0)));

        // sort ((a, b) => a.t - b.t || index a - index b): a stable sort by t.
        std::stable_sort (out.begin(), out.end(), [] (const Node& a, const Node& b) { return a.t < b.t; });

        if (out.size() == 1)
        {
            Node a = out[0], b = out[0];
            a.t = 0.0;
            b.t = 1.0;
            out = { a, b };
        }
        out.front().t = 0.0;
        out.back().t = 1.0;
        return capNodes (out);
    }

    bool nodesSorted (const NodeList& nodes) noexcept
    {
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            const Node& q = nodes[i];
            if (! std::isfinite (q.t) || ! std::isfinite (q.v) || (i > 0 && q.t < nodes[i - 1].t))
                return false;
        }
        return true;
    }

    std::vector<double> tangents (const NodeList& nd)
    {
        const std::size_t n = nd.size();
        std::vector<double> m (n, 0.0);
        if (n < 2)
            return m;   // the prototype's values here are undefined; nothing reads them

        std::vector<double> d (n - 1, 0.0);
        for (std::size_t k = 0; k + 1 < n; ++k)
            d[k] = (nd[k + 1].v - nd[k].v) / jsMax (1e-6, nd[k + 1].t - nd[k].t);

        m[0] = d[0];
        m[n - 1] = d[n - 2];
        for (std::size_t k = 1; k + 1 < n; ++k)
            m[k] = d[k - 1] * d[k] <= 0.0 ? 0.0 : (d[k - 1] + d[k]) / 2.0;

        for (std::size_t k = 0; k + 1 < n; ++k)
        {
            if (d[k] == 0.0)
            {
                m[k] = 0.0;
                m[k + 1] = 0.0;
                continue;
            }
            const double a = m[k] / d[k], b = m[k + 1] / d[k], s = a * a + b * b;
            if (s > 9.0)
            {
                const double t = 3.0 / std::sqrt (s);
                m[k] = t * a * d[k];
                m[k + 1] = t * b * d[k];
            }
        }
        return m;
    }

    double evalSeg (const NodeList& nd, const std::vector<double>& m, std::size_t k, double t) noexcept
    {
        const Node& A = nd[k];
        const Node& B = nd[k + 1];
        const double h = B.t - A.t;
        if (h <= 1e-9)
            return B.v;

        const double u = clamp ((t - A.t) / h, 0.0, 1.0);
        switch (A.shape)
        {
            case NodeShape::linear:
                return A.v + (B.v - A.v) * u;
            case NodeShape::bend:
                return A.v + (B.v - A.v) * core::bendShape (u, A.bend);
            case NodeShape::step:
                return u >= 1.0 ? B.v : A.v;
            case NodeShape::smooth:
            default:
            {
                const double u2 = u * u, u3 = u2 * u;
                return (2.0 * u3 - 3.0 * u2 + 1.0) * A.v + (u3 - 2.0 * u2 + u) * h * m[k] + (-2.0 * u3 + 3.0 * u2) * B.v
                     + (u3 - u2) * h * m[k + 1];
            }
        }
    }

    std::size_t segIndex (const NodeList& nd, double t, bool right) noexcept
    {
        std::ptrdiff_t lo = 0, hi = static_cast<std::ptrdiff_t> (nd.size()) - 2;
        while (lo < hi)
        {
            const std::ptrdiff_t mid = (lo + hi + 1) / 2;
            const double mt = nd[static_cast<std::size_t> (mid)].t;
            if (right ? mt <= t : mt < t)
                lo = mid;
            else
                hi = mid - 1;
        }
        return static_cast<std::size_t> (lo);
    }

    double evalNodes (const NodeList& nodes, double t, bool left)
    {
        NodeList scratch;
        const NodeList& nd = usable (nodes, scratch);
        if (nd.empty())
            return 0.5;
        if (nd.size() == 1)
            return nd[0].v;
        const auto m = tangents (nd);
        return clamp (evalSeg (nd, m, segIndex (nd, t, ! left), t), 0.0, 1.0);
    }

    std::vector<float> sampleNodes (const NodeList& nodes, int M)
    {
        M = std::max (1, M);
        std::vector<float> o (static_cast<std::size_t> (M), 0.0f);
        if (nodes.empty())
            return o;

        NodeList scratch;
        const NodeList& nd = usable (nodes, scratch);
        const std::size_t n = nd.size();
        if (n == 1)
        {
            std::fill (o.begin(), o.end(), static_cast<float> (nd[0].v));
            return o;
        }

        const auto m = tangents (nd);
        std::size_t k = 0;
        for (int j = 0; j < M; ++j)
        {
            const double t = M == 1 ? 0.0 : static_cast<double> (j) / static_cast<double> (M - 1);
            while (k < n - 2 && t >= nd[k + 1].t)
                ++k;
            o[static_cast<std::size_t> (j)] = static_cast<float> (clamp (evalSeg (nd, m, k, t), 0.0, 1.0));
        }
        return o;
    }

    std::vector<std::size_t> rdpIdx (const std::vector<double>& ts, const std::vector<double>& vs, double eps)
    {
        const std::size_t n = ts.size();
        std::vector<std::size_t> out;
        if (n < 3)
        {
            for (std::size_t i = 0; i < n; ++i)
                out.push_back (i);
            return out;
        }

        std::vector<std::uint8_t> keep (n, 0);
        keep[0] = 1;
        keep[n - 1] = 1;
        std::vector<std::size_t> stack { 0, n - 1 };
        while (! stack.empty())
        {
            const std::size_t b = stack.back();
            stack.pop_back();
            const std::size_t a = stack.back();
            stack.pop_back();

            std::size_t idx = 0;
            bool found = false;
            double dm = 0.0;
            const double ta = ts[a], va = vs[a];
            double dt = ts[b] - ta;
            if (dt == 0.0 || std::isnan (dt))   // (ts[b] - ta) || 1
                dt = 1.0;
            const double dvv = vs[b] - va;
            for (std::size_t i = a + 1; i < b; ++i)
            {
                const double dv = std::fabs (vs[i] - (va + dvv * ((ts[i] - ta) / dt)));
                if (dv > dm)
                {
                    dm = dv;
                    idx = i;
                    found = true;
                }
            }
            if (dm > eps && found && idx > 0)
            {
                keep[idx] = 1;
                stack.push_back (a);
                stack.push_back (idx);
                stack.push_back (idx);
                stack.push_back (b);
            }
        }
        for (std::size_t i = 0; i < n; ++i)
            if (keep[i] != 0)
                out.push_back (i);
        return out;
    }

    NodeList rdp (const NodeList& points, double eps)
    {
        if (points.size() < 3)
            return points;
        std::vector<double> ts, vs;
        ts.reserve (points.size());
        vs.reserve (points.size());
        for (const Node& q : points)
        {
            ts.push_back (q.t);
            vs.push_back (q.v);
        }
        NodeList out;
        for (const std::size_t i : rdpIdx (ts, vs, eps))
            out.push_back (points[i]);
        return out;
    }

    NodeList capNodes (const NodeList& nd)
    {
        if (nd.size() <= static_cast<std::size_t> (kMaxNodes))
            return nd;

        std::vector<double> ts, vs;
        ts.reserve (nd.size());
        vs.reserve (nd.size());
        for (const Node& q : nd)
        {
            ts.push_back (q.t);
            vs.push_back (q.v);
        }
        double eps = 0.001;
        auto idx = rdpIdx (ts, vs, eps);
        while (idx.size() > static_cast<std::size_t> (kMaxNodes))
        {
            eps *= 1.25;
            idx = rdpIdx (ts, vs, eps);
        }
        NodeList out;
        out.reserve (idx.size());
        for (const std::size_t i : idx)
            out.push_back (nd[i]);
        return out;
    }

    NodeList sliceNodes (const NodeList& nodes, double u0, double u1)
    {
        NodeList scratch;
        const NodeList& nd = usable (nodes, scratch);
        u0 = clamp (u0, 0.0, 1.0);
        u1 = clamp (u1, 0.0, 1.0);
        if (nd.size() < 2)
            return flat (nd.empty() ? 0.5 : nd[0].v);
        if (u1 - u0 < 1e-9)
            return flat (evalNodes (nd, u0));
        if (u0 <= 0.0 && u1 >= 1.0)
            return nd;

        const auto m = tangents (nd);
        const double span = u1 - u0;

        // Each piece remembers the original segment it lies in (-1 for the end node), for the refit.
        struct Piece
        {
            Node q;
            std::ptrdiff_t k;
        };
        std::vector<Piece> out;

        auto valAt = [&] (double t, bool left) { return clamp (evalSeg (nd, m, segIndex (nd, t, ! left), t), 0.0, 1.0); };
        auto piece = [&] (std::size_t k, double x0, double x1, double v0)
        {
            const Node& A = nd[k];
            const double h = nd[k + 1].t - A.t;
            const double f = h > 1e-9 ? clamp ((x1 - x0) / h, 0.0, 1.0) : 1.0;
            out.push_back ({ mkNode ((x0 - u0) / span, v0, A.shape, A.shape == NodeShape::bend ? clamp (A.bend * f, -1.0, 1.0) : A.bend),
                             static_cast<std::ptrdiff_t> (k) });
        };

        std::size_t k = segIndex (nd, u0, true);
        double x = u0;
        double v = valAt (u0, false);
        for (;;)
        {
            const double segEnd = nd[k + 1].t;
            piece (k, x, std::min (segEnd, u1), v);
            if (segEnd >= u1 - 1e-12 || k >= nd.size() - 2)
                break;
            ++k;
            x = nd[k].t;
            v = nd[k].v;
        }
        out.push_back ({ mkNode (1.0, valAt (u1, true)), -1 });
        out[0].q.t = 0.0;

        // Refit the smooth pieces against the original curve (each piece lies inside one original segment).
        auto orig = [&] (std::ptrdiff_t sk, double t)
        {
            return clamp (evalSeg (nd, m, static_cast<std::size_t> (sk), u0 + t * span), 0.0, 1.0);
        };
        auto nodesOfPieces = [&out]
        {
            NodeList list;
            list.reserve (out.size());
            for (const Piece& p : out)
                list.push_back (p.q);
            return list;
        };

        for (int iter = 0; iter < 10; ++iter)
        {
            const NodeList current = nodesOfPieces();
            const auto mo = tangents (current);
            std::vector<std::size_t> split;
            for (std::size_t j = 0; j + 1 < out.size(); ++j)
            {
                const Piece& A = out[j];
                const Piece& B = out[j + 1];
                if (A.q.shape != NodeShape::smooth || A.k < 0 || B.q.t - A.q.t < 1e-4)
                    continue;
                double worst = 0.0;
                for (int i = 1; i < 8; ++i)
                {
                    const double t = A.q.t + (B.q.t - A.q.t) * static_cast<double> (i) / 8.0;
                    worst = jsMax (worst, std::fabs (clamp (evalSeg (current, mo, j, t), 0.0, 1.0) - orig (A.k, t)));
                }
                if (worst > kSliceTol)
                    split.push_back (j);
            }
            if (split.empty() || out.size() + split.size() > static_cast<std::size_t> (kMaxNodes))
                break;
            for (std::size_t i = split.size(); i-- > 0;)
            {
                const std::size_t j = split[i];
                const Piece A = out[j];
                const double t = (A.q.t + out[j + 1].q.t) / 2.0;
                out.insert (out.begin() + static_cast<std::ptrdiff_t> (j) + 1, Piece { mkNode (t, orig (A.k, t)), A.k });
            }
        }
        return capNodes (nodesOfPieces());
    }

    NodeList reverseNodes (const NodeList& nodes)
    {
        NodeList scratch;
        const NodeList& nd = usable (nodes, scratch);
        if (nd.empty())
            return flat (0.5);   // the prototype throws on an empty list

        NodeList out;
        for (std::size_t i = nd.size(); i-- > 0;)
        {
            const Node& q = nd[i];
            const double t = 1.0 - q.t;
            if (i == 0)
            {
                out.push_back (mkNode (t, q.v));
                break;
            }
            const Node& P = nd[i - 1];
            if (P.shape == NodeShape::step)
            {
                out.push_back (mkNode (t, q.v, NodeShape::linear));
                out.push_back (mkNode (t, P.v, NodeShape::linear));
            }
            else
            {
                out.push_back (mkNode (t, q.v, P.shape, P.shape == NodeShape::bend ? -P.bend : P.bend));
            }
        }
        // the edge node made for a step that ended at the last node sits at t = 0 next to the first: drop it
        if (out.size() > 2 && out[0].t == 0.0 && out[1].t == 0.0)
            out.erase (out.begin());
        out.front().t = 0.0;
        out.back().t = 1.0;
        return capNodes (out);
    }

    NodeList flipNodes (const NodeList& nodes)
    {
        NodeList out = nodes;
        for (Node& q : out)
            q.v = clamp (1.0 - q.v, 0.0, 1.0);
        return out;
    }

    NodeList scaleNodes (const NodeList& nodes, double def, double f)
    {
        NodeList out = nodes;
        for (Node& q : out)
            q.v = clamp (def + (q.v - def) * f, 0.0, 1.0);
        return out;
    }
}
