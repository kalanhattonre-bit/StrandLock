#include "EditHistory.h"

#include <cstring>

namespace sl::doc
{
    namespace
    {
        bool sameBits (float a, float b) noexcept { return std::memcmp (&a, &b, sizeof a) == 0; }
    }

    bool SameDocState::operator() (const DocState& a, const DocState& b) const
    {
        if (! model::SameClipPtr {} (a.clip, b.clip))
            return false;
        for (std::size_t i = 0; i < a.pv.size(); ++i)
            if (! sameBits (a.pv[i], b.pv[i]))
                return false;
        return sameNonParam (a.np, b.np);
    }

    EditHistory::EditHistory (int limit)
        : history (limit)
    {
    }

    void EditHistory::reset (DocState state)
    {
        state.pv[(std::size_t) params::index (params::P::bypass)] = params::kParams[params::index (params::P::bypass)].def;
        history.clear (std::move (state));
    }

    bool EditHistory::push (std::string label, model::ClipPtr clip, NonParamState np, const std::vector<std::pair<int, float>>& changedParams)
    {
        const auto previous = history.current();
        if (! previous)
            return false;

        DocState next;
        next.clip = std::move (clip);
        next.np = std::move (np);
        next.pv = previous->pv;
        const int bypass = params::index (params::P::bypass);
        for (const auto& [i, v] : changedParams)
            if (i >= 0 && i < params::kNumParams && i != bypass)
                next.pv[(std::size_t) i] = v;
        return history.push (std::move (next), std::move (label));
    }

    std::vector<std::pair<int, float>> EditHistory::differences (const ParamValues& from, const ParamValues& to)
    {
        std::vector<std::pair<int, float>> out;
        for (int i = 0; i < params::kNumParams; ++i)
            if (! sameBits (from[(std::size_t) i], to[(std::size_t) i]))
                out.emplace_back (i, to[(std::size_t) i]);
        return out;
    }

    std::optional<EditHistory::Step> EditHistory::undo()
    {
        const auto from = history.current();
        auto label = history.undoLabel();
        auto to = history.undo();
        if (! from || ! to)
            return std::nullopt;
        Step s;
        s.params = differences (from->pv, to->pv);
        s.state = std::move (*to);
        s.label = label.value_or (std::string());
        return s;
    }

    std::optional<EditHistory::Step> EditHistory::redo()
    {
        const auto from = history.current();
        auto label = history.redoLabel();
        auto to = history.redo();
        if (! from || ! to)
            return std::nullopt;
        Step s;
        s.params = differences (from->pv, to->pv);
        s.state = std::move (*to);
        s.label = label.value_or (std::string());
        return s;
    }
}
