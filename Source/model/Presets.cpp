#include "Presets.h"

#include "Generators.h"
#include "Normalize.h"
#include "Recipes.h"
#include "Shapes.h"
#include "Tools.h"

#include <algorithm>
#include <deque>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <utility>

namespace sl::model
{
    using core::Engine;
    using core::LaneId;
    using core::Motion;

    //==============================================================================================
    // Sound presets

    core::InstPatch soundBase()
    {
        const core::Inst d = core::defaultInst();
        core::InstPatch p = straightPatch();
        p.region.start = d.region.start;
        p.region.end = d.region.end;
        p.region.atEnd = d.region.atEnd;
        p.region.xfadeMs = d.region.xfadeMs;
        p.macros = d.macros;
        return p;
    }

    namespace
    {
        std::vector<SoundPreset> makeSoundPresets()
        {
            std::vector<SoundPreset> list;
            auto add = [&list] (const char* id, const char* name, const char* sound) -> core::InstPatch&
            {
                list.push_back (SoundPreset { id, name, sound, {} });
                return list.back().inst;
            };

            add ("init", "Saw that opens up", "sawopen");
            add ("saw-tape", "Plain saw on tape", "saw").engine = Engine::tape;
            {
                auto& p = add ("square-cloud", "Square sweep cloud", "square");
                p.engine = Engine::cloud;
                p.heads = 2.0;
                p.spread = 0.3;
                p.grainCloud = 120.0;
            }
            {
                auto& p = add ("pad-drift", "Evolving pad, drifting", "pad");
                p.engine = Engine::cloud;
                p.heads = 3.0;
                p.spread = 0.4;
                p.drift = 0.3;
                p.spray = 0.15;
            }
            {
                auto& p = add ("vowel-voice", "Vowel voice", "vowel");
                p.engine = Engine::voice;
                p.grainVoice = 2.0;
            }
            {
                auto& p = add ("pluck-tape", "Pluck arpeggio on tape", "pluck");
                p.engine = Engine::tape;
                p.region.atEnd = core::AtEnd::hold;
            }
            {
                auto& p = add ("beat-scatter", "Beat loop scatter", "beat");
                p.engine = Engine::tape;
                p.motion = Motion::scatter;
                p.jump = core::RateId::r1_8;
            }
            {
                auto& p = add ("noise-lock", "Noise locked to the note", "noise");
                p.engine = Engine::cloud;
                p.lock = true;
                p.grainLock = 4.0;
            }
            return list;
        }
    }

    const std::vector<SoundPreset>& soundPresets()
    {
        static const std::vector<SoundPreset> list = makeSoundPresets();
        return list;
    }

    std::optional<SoundPresetResult> applySoundPreset (const core::Inst& inst, std::string_view idOrName)
    {
        const auto& list = soundPresets();
        const auto it = std::find_if (list.begin(), list.end(), [idOrName] (const SoundPreset& p) { return idOrName == p.id || idOrName == p.name; });
        if (it == list.end())
            return std::nullopt;
        SoundPresetResult out;
        out.sound = it->sound;
        out.inst = core::normalizeInst (core::mergeInst (core::mergeInst (core::normalizeInst (inst), soundBase()), it->inst));
        return out;
    }

    //==============================================================================================
    // Clip presets

    namespace
    {
        /** presetBuilder (prefix): add (p, s, l) makes mkNote (p, s, l, prefix + '-' + (++k)). The notes stay
            editable until take (). */
        class Builder
        {
        public:
            explicit Builder (std::string idPrefix) : prefix (std::move (idPrefix)) {}

            Note& add (double p, double s, double l)
            {
                made.push_back (mkNote (p, s, l, prefix + "-" + std::to_string (++k)));
                return made.back();
            }

            std::vector<NotePtr> take() const
            {
                std::vector<NotePtr> out;
                out.reserve (made.size());
                for (const Note& n : made)
                    out.push_back (std::make_shared<const Note> (n));
                return out;
            }

        private:
            std::string prefix;
            int k = 0;
            std::deque<Note> made;   // references stay valid while notes are added
        };

        /** nodesOf ([[t, v, shape?, bend?], ...]) */
        NodeList nodesOf (std::initializer_list<Node> list)
        {
            return NodeList (list);
        }

        Node nd (double t, double v, NodeShape shape = NodeShape::smooth, double bend = 0.0)
        {
            return mkNode (t, v, shape, bend);
        }

        /** shapeNodes (key, lane) with no options: a Speed-kind Travel, no note id, no Fit length. */
        NodeList sh (ShapeKey key, LaneId lane)
        {
            return shapeNodes (key, lane, ShapeOpts {}, TravelKind::speed, 0, 1.0).value_or (NodeList {});
        }

        Gen pattern (GenShape shape, core::RateId rate, double d0, double d1, std::optional<bool> lockToBeat = std::nullopt)
        {
            GenPatch g;
            g.shape = shape;
            g.rate = rate;
            g.d0 = d0;
            g.d1 = d1;
            g.lockToBeat = lockToBeat;
            return makeGen (GenKind::pattern, g);
        }

        struct PSL
        {
            double p, s, l;
        };

        std::vector<ClipPreset> makeClipPresets()
        {
            std::vector<ClipPreset> out;
            auto def = [&out] (const char* id, const char* name, int bars, auto build)
            {
                Builder b (id);
                build (b);
                out.push_back (ClipPreset { id, name, bars, b.take() });
            };

            {
                ClipPreset demo = strandDemo();
                demo.id = "strand-demo";
                demo.name = "Strand Demo";
                out.push_back (std::move (demo));
            }

            def ("pad-swell", "Pad Swell", 4, [] (Builder& b)
            {
                const double chords[2][4] = { { 60, 64, 67, 71 }, { 57, 60, 64, 67 } };
                for (int i = 0; i < 2; ++i)
                {
                    for (const double p : chords[i])
                    {
                        Note& n = b.add (p, i * 8.0, 8.0);
                        n.lane (LaneId::amp) = sh (ShapeKey::swell, LaneId::amp);
                        n.lane (LaneId::tone) = nodesOf ({ nd (0, 0.45), nd (0.6, 0.85), nd (1, 0.6) });
                    }
                }
            });

            def ("pluck-decay", "Pluck Decay", 2, [] (Builder& b)
            {
                const double ps[] = { 60, 64, 67, 72, 67, 64, 60, 55, 57, 60, 64, 69, 64, 60, 57, 52 };
                int k = 0;
                for (const double p : ps)
                {
                    Note& n = b.add (p, k * 0.5, 0.5);
                    n.lane (LaneId::amp) = sh (ShapeKey::decay, LaneId::amp);
                    n.lane (LaneId::tone) = nodesOf ({ nd (0, 0.95), nd (0.3, 0.55), nd (1, 0.4) });
                    ++k;
                }
            });

            def ("vowel-morph", "Vowel Morph", 4, [] (Builder& b)
            {
                const PSL list[] = { { 48, 0, 8 }, { 55, 0, 8 }, { 53, 8, 8 }, { 60, 8, 8 } };
                int k = 0;
                for (const PSL& e : list)
                {
                    Note& n = b.add (e.p, e.s, e.l);
                    n.lane (LaneId::formant) = k % 2 != 0 ? sh (ShapeKey::down, LaneId::formant) : sh (ShapeKey::up, LaneId::formant);
                    n.genOf (LaneId::formant) = pattern (GenShape::squiggle, core::RateId::r1_4, 0.05, 0.25, true);
                    ++k;
                }
            });

            def ("legato-lead", "Legato Lead", 4, [] (Builder& b)
            {
                const PSL list[] = { { 72, 0, 1.5 }, { 74, 1.5, 0.5 }, { 76, 2, 2 }, { 79, 4, 1 }, { 77, 5, 1 },
                                     { 76, 6, 2 }, { 74, 8, 1.5 }, { 72, 9.5, 0.5 }, { 71, 10, 2 }, { 72, 12, 4 } };
                for (const PSL& e : list)
                {
                    Note& n = b.add (e.p, e.s, e.l);
                    n.lane (LaneId::vib) = nodesOf ({ nd (0, 0), nd (0.4, 0), nd (1, 0.45) });
                    n.lane (LaneId::amp) = nodesOf ({ nd (0, 0.7), nd (0.5, 0.85), nd (1, 0.75) });
                }
            });

            def ("hollow-drone", "Hollow Drone", 4, [] (Builder& b)
            {
                for (const double p : { 36.0, 43.0 })
                {
                    Note& n = b.add (p, 0, 16);
                    n.lane (LaneId::hollow) = sh (ShapeKey::swell, LaneId::hollow);
                    n.genOf (LaneId::tone) = pattern (GenShape::squiggle, core::RateId::r1bar, 0.3, 0.3);
                    n.lane (LaneId::tone) = flat (0.6);
                }
            });

            def ("saw-mirror-pad", "Saw Mirror Pad", 4, [] (Builder& b)
            {
                for (const double p : { 48.0, 55.0, 60.0, 64.0 })
                {
                    Note& n = b.add (p, 0, 16);
                    n.sample.motion = Motion::mirror;
                    n.lane (LaneId::heads) = flat (0.72);
                    n.lane (LaneId::amp) = sh (ShapeKey::swell, LaneId::amp);
                    n.lane (LaneId::tone) = nodesOf ({ nd (0, 0.5), nd (1, 0.9) });
                }
            });

            def ("backward-vowel", "Backward Vowel", 4, [] (Builder& b)
            {
                const PSL list[] = { { 60, 0, 4 }, { 63, 4, 4 }, { 67, 8, 4 }, { 65, 12, 4 } };
                for (const PSL& e : list)
                {
                    Note& n = b.add (e.p, e.s, e.l);
                    n.sample.motion = Motion::backward;
                    n.lane (LaneId::formant) = sh (ShapeKey::down, LaneId::formant);
                    n.lane (LaneId::amp) = sh (ShapeKey::swell, LaneId::amp);
                }
            });

            def ("scatter-18t", "Scatter 1/8T", 2, [] (Builder& b)
            {
                const PSL list[] = { { 48, 0, 8 }, { 60, 0, 8 }, { 67, 0, 8 } };
                for (const PSL& e : list)
                {
                    Note& n = b.add (e.p, e.s, e.l);
                    n.sample.motion = Motion::scatter;
                    n.sample.jump = core::RateId::r1_8T;
                    n.sample.engine = Engine::cloud;
                    n.lane (LaneId::amp) = nodesOf ({ nd (0, 0.6), nd (0.5, 0.85), nd (1, 0.6) });
                }
            });

            def ("tape-stop-lead", "Tape Stop Lead", 4, [] (Builder& b)
            {
                const PSL list[] = { { 72, 0, 2 }, { 75, 2, 2 }, { 79, 4, 4 }, { 77, 8, 2 }, { 75, 10, 2 }, { 72, 12, 4 } };
                const int count = static_cast<int> (std::size (list));
                for (int k = 0; k < count; ++k)
                {
                    Note& n = b.add (list[k].p, list[k].s, list[k].l);
                    n.sample.engine = Engine::tape;
                    if (k == count - 1 || k == 2)
                        n.lane (LaneId::travel) = sh (ShapeKey::tapestop, LaneId::travel);
                }
            });

            def ("bloom", "Bloom", 4, [] (Builder& b)
            {
                const PSL list[] = { { 48, 0, 16 }, { 55, 0, 16 }, { 64, 0, 16 }, { 71, 4, 12 } };
                for (const PSL& e : list)
                {
                    Note& n = b.add (e.p, e.s, e.l);
                    n.sample.engine = Engine::cloud;
                    n.lane (LaneId::heads) = sh (ShapeKey::up, LaneId::heads);
                    n.lane (LaneId::amp) = sh (ShapeKey::swell, LaneId::amp);
                }
            });

            {
                Builder b ("wt");
                const double ps[] = { 60, 60, 60, 60, 60, 60, 60, 60, 67, 67, 67, 67, 65, 65, 64, 64 };
                int k = 0;
                for (const double p : ps)
                    b.add (p, k++, 1);
                Clip clip = newClip (4);
                clip.notes = b.take();
                const Clip walked = walk (clip, Ids::all(), WalkField::start(), 0, 0.9375);
                out.push_back (ClipPreset { "walk-through", "Walk Through", 4, walked.notes });
            }
            return out;
        }
    }

    ClipPreset strandDemo()
    {
        Builder b ("sd");
        {
            const double ps[] = { 55, 60, 64, 67 };
            for (int k = 0; k < 4; ++k)
            {
                Note& n = b.add (ps[k], 0, 4);
                n.lane (LaneId::tone) = sh (ShapeKey::up, LaneId::tone);
                n.lane (LaneId::amp) = nodesOf ({ nd (0, 0.35), nd (0.6, 0.9), nd (1, 0.75) });
                if (k == 3)
                    n.lane (LaneId::vib) = nodesOf ({ nd (0, 0), nd (0.4, 0), nd (1, 0.55) });
            }
        }
        {
            const double ps[] = { 57, 60, 64, 69 };
            for (int k = 0; k < 4; ++k)
            {
                Note& n = b.add (ps[k], 4, 4);
                n.lane (LaneId::hollow) = sh (ShapeKey::swell, LaneId::hollow);
                if (k == 3)
                    n.lane (LaneId::vib) = nodesOf ({ nd (0, 0.3), nd (1, 0.6) });
            }
        }
        Note& b1 = b.add (36, 0, 4);
        Note& b2 = b.add (33, 4, 4);
        {
            PatternOpts o;
            o.shape = PatternShape::sine;
            o.cycles = 4;
            o.center = 0.5;
            o.d0 = 0.1;
            o.d1 = 0.7;
            o.acc = 0;
            b1.lane (LaneId::formant) = patternNodes (o);
        }
        b2.glide = 0.0;
        {
            const double ps[] = { 72, 76, 79, 76, 74, 72, 71, 74 };
            for (int k = 0; k < 8; ++k)
            {
                Note& n = b.add (ps[k], 8.5 + k * 0.5, 0.5);
                n.lane (LaneId::amp) = sh (ShapeKey::decay, LaneId::amp);
                n.lane (LaneId::tone) = sh (ShapeKey::down, LaneId::tone);
                n.glide = 0.0;
            }
        }
        {
            Note* c3[4] = {};
            const double ps[] = { 55, 59, 62, 67 };
            for (int k = 0; k < 4; ++k)
                c3[k] = &b.add (ps[k], 12, 4);
            for (Note* n : c3)
            {
                PatternOpts o;
                o.shape = PatternShape::sine;
                o.cycles = 8;
                o.center = 0.55;
                o.d0 = 0.05;
                o.d1 = 0.8;
                o.acc = 0.6;
                n->lane (LaneId::tone) = patternNodes (o);
                NodeList amp;
                for (const Node& q : sh (ShapeKey::down, LaneId::amp))
                    amp.push_back (mkNode (q.t, 0.3 + 0.6 * q.v, q.shape, q.bend));
                n->lane (LaneId::amp) = std::move (amp);
            }
        }
        Note& b3 = b.add (31, 8, 4);
        Note& b4 = b.add (31, 12, 4);
        b3.lane (LaneId::pitch) = nodesOf ({ nd (0, 0.5 - 5.0 / 24.0), nd (0.25, 0.5), nd (1, 0.5) });
        b4.lane (LaneId::vib) = nodesOf ({ nd (0, 0), nd (1, 0.4) });
        Note& lead = b.add (84, 6, 2);
        Note& lead2 = b.add (79, 10, 2);
        lead.glideTo = lead2.id;
        lead.glide = 1.0;
        lead2.lane (LaneId::vib) = nodesOf ({ nd (0, 0.2), nd (1, 0.5) });
        lead.lane (LaneId::pitch) = nodesOf ({ nd (0, 0.5 - 2.0 / 24.0), nd (0.3, 0.5), nd (1, 0.5) });
        lead.lane (LaneId::vib) = nodesOf ({ nd (0, 0), nd (0.35, 0), nd (1, 0.7) });
        lead.lane (LaneId::amp) = sh (ShapeKey::swell, LaneId::amp);

        ClipPreset out;
        out.id = "strand-demo";
        out.name = "Strand Demo";
        out.bars = 4;
        out.notes = b.take();
        return out;
    }

    const std::vector<ClipPreset>& clipPresets()
    {
        static const std::vector<ClipPreset> list = makeClipPresets();
        return list;
    }

    std::optional<Clip> loadClipPreset (std::string_view idOrName)
    {
        const auto& list = clipPresets();
        const auto it = std::find_if (list.begin(), list.end(), [idOrName] (const ClipPreset& p) { return idOrName == p.id || idOrName == p.name; });
        if (it == list.end())
            return std::nullopt;
        Clip raw = newClip (it->bars);
        raw.notes = it->notes;   // normalizeClip makes new notes; the preset's own stay untouched
        return normalizeClip (raw);
    }
}
