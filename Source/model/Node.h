#pragma once

// Lane nodes (BLUEPRINT 2.1, sl-model.js "nodes"): a lane is a 0..1 curve over a note, a list of nodes from
// t = 0 to t = 1. A node's shape and bend describe the segment AFTER it. The functions keep the prototype's
// names and do its arithmetic in its order (PLUGIN-PLAN 1.2, 1.6); they are implemented in Nodes.cpp.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace sl::model
{
    /** The segment shapes, in the order of their codes (fixtures and the CLIP chunk: 0 smooth, 1 linear,
        2 bend, 3 step). 'smooth' is the Fritsch-Carlson monotone cubic, 'bend' PulseLock's bend curve,
        'step' holds until the next node. */
    enum class NodeShape : std::uint8_t { smooth, linear, bend, step };

    inline constexpr const char* kNodeShapeCodes[] = { "smooth", "linear", "bend", "step" };
    inline constexpr int kNumNodeShapes = 4;

    const char* code (NodeShape shape) noexcept;
    std::optional<NodeShape> parseNodeShape (std::string_view code) noexcept;

    struct Node
    {
        double t = 0.0;                       // 0..1 of the note
        double v = 0.0;                       // 0..1 lane value
        NodeShape shape = NodeShape::smooth;  // of the segment after this node
        double bend = 0.0;                    // -1..1, used by 'bend'
    };

    using NodeList = std::vector<Node>;

    /** At most 128 nodes per lane (MAX_NODES); capNodes simplifies longer lists. */
    inline constexpr int kMaxNodes = 128;

    /** sliceNodes refits a smooth piece that strays more than this from the original curve. */
    inline constexpr double kSliceTol = 0.002;

    //==============================================================================================
    /** mkNode (t, v, shape, bend). */
    Node mkNode (double t, double v, NodeShape shape = NodeShape::smooth, double bend = 0.0) noexcept;

    /** flat (v): two smooth nodes at v (t 0 and 1). */
    NodeList flat (double v);

    /** Clean any node list (normalizeNodes): t clamped 0..1 (a non-finite t reads 0), v clamped 0..1 (a
        non-finite v reads def), bend clamped -1..1 (non-finite: 0), a shape outside the four reads smooth;
        sorted by t (stable); a single node becomes two at t 0 and 1; first t = 0, last t = 1; at most 128
        nodes (capNodes). An empty list is flat (def). The prototype's raw inputs (v3 lin:true, strings,
        missing fields) are turned into a NodeList by nodesFromJson (Normalize.h). */
    NodeList normalizeNodes (const NodeList& nodes, double def = 0.5);

    /** True when the list can be used as it is: every t and v finite and t never decreasing (nodesSorted). */
    bool nodesSorted (const NodeList& nodes) noexcept;

    /** Fritsch-Carlson tangents over the whole list, v3's sampleLane verbatim (its circle limit is applied
        segment by segment from the left). One per node; empty for an empty list. */
    std::vector<double> tangents (const NodeList& nodes);

    /** The value of segment k (nodes k .. k+1) at t, unclamped: linear, bend, step or the monotone cubic. */
    double evalSeg (const NodeList& nodes, const std::vector<double>& m, std::size_t k, double t) noexcept;

    /** The segment holding t. right: at a node's exact t, the segment after it (the value at a vertical
        edge is the value after it); else the segment before it. */
    std::size_t segIndex (const NodeList& nodes, double t, bool right) noexcept;

    /** The lane's value at t, clamped 0..1. left: at a vertical edge, the value before it. A list that is
        not sorted is normalized first (with def 0.5, as the prototype). An empty list gives 0.5 (the
        prototype throws there). */
    double evalNodes (const NodeList& nodes, double t, bool left = false);

    /** M samples at t = j / (M - 1), each clamp (evalSeg, 0, 1) rounded to Float32 (PLUGIN-PLAN 1.6 rule 1).
        M below 1 gives 1 sample; an empty list gives zeros. */
    std::vector<float> sampleNodes (const NodeList& nodes, int M);

    /** Ramer-Douglas-Peucker on parallel t / v arrays: the kept indices, first and last always kept. */
    std::vector<std::size_t> rdpIdx (const std::vector<double>& ts, const std::vector<double>& vs, double eps);

    /** v3's rdp (pts, eps): the kept nodes (copies). */
    NodeList rdp (const NodeList& points, double eps);

    /** At most 128 nodes: RDP from eps 0.001, raised by 1.25x until it fits. The kept nodes keep their shapes. */
    NodeList capNodes (const NodeList& nodes);

    /** The part of the list between u0 and u1, remapped to 0..1. Linear, step and bend segments are cut
        exactly (a cut bend keeps its curve: bend' = bend x covered fraction); smooth segments are refitted,
        split at their middle while they stray more than kSliceTol from the original curve (up to 10 passes
        and 128 nodes). At u0 the value after a vertical edge is used, at u1 the value before it. */
    NodeList sliceNodes (const NodeList& nodes, double u0, double u1);

    /** Mirror in time: bend -> -bend; a step becomes a vertical edge then a hold. */
    NodeList reverseNodes (const NodeList& nodes);

    /** v -> clamp (1 - v, 0, 1). */
    NodeList flipNodes (const NodeList& nodes);

    /** v -> clamp (def + (v - def) * f, 0, 1). */
    NodeList scaleNodes (const NodeList& nodes, double def, double f);
}
