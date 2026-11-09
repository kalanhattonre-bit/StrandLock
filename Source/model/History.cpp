#include "History.h"

#include <cmath>
#include <cstring>

namespace sl::model
{
    bool sameClip (const Clip& a, const Clip& b) noexcept
    {
        if (a.bars != b.bars || a.beatsPerBar != b.beatsPerBar || a.loop != b.loop || a.tieAcrossLoop != b.tieAcrossLoop
            || a.extras != b.extras || a.notes.size() != b.notes.size())
            return false;
        if (! (a.offsetBeats == b.offsetBeats || (std::isnan (a.offsetBeats) && std::isnan (b.offsetBeats))))
            return false;
        for (std::size_t i = 0; i < a.notes.size(); ++i)
        {
            const NotePtr& x = a.notes[i];
            const NotePtr& y = b.notes[i];
            if (x == y)
                continue;
            if (x == nullptr || y == nullptr || x->extras != y->extras || ! sameContent (*x, *y))
                return false;
        }
        return true;
    }
}
