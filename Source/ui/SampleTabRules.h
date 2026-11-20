#pragma once

// The pure part of the Sample and Macros tabs (PLUGIN-PLAN 1.2 "UI modules", 4 M6, 7.8 T1): sl-ui-sample.js's exported
// helpers ported exactly (contexts, fades, grainKind, headsMulti, toneAllOpen, randomInUse, macroText, offsetText,
// suggestEngine, pickPart, parseNum / parseMs / parsePct, KNOB_MATH and its q functions, RULES, kindOf), and the knobs'
// specs and drag rules from its buildSampleTab, knob () and buildMacros, so every knob's numbers and texts can be tested
// without a window. Checked against Tests/fixtures/ui/sample-rules.json and ui/readouts.json.
//
// A "context" is one way the instrument's settings get used: the instrument itself (live keys, and every note that
// follows it), plus one per distinct mix of a note's own settings (Note settings, S7). A control is faded when it does
// nothing in any context; its reason is written for the instrument. No JUCE here, and no state: message thread or tests.

#include "core/Inst.h"
#include "model/Clip.h"
#include "params/ParamIds.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sl::ui::rules
{
    //==============================================================================================
    // Contexts.

    /** What a context plays: Tape, Voice, Cloud, or Cloud with Lock. */
    enum class Kind : std::uint8_t { tape, voice, cloud, lock };

    /** The prototype's word for a kind ("tape", "voice", "cloud", "lock"). */
    const char* code (Kind) noexcept;

    /** SLSampleUI.kindOf (engine, lock): Tape and Voice whatever lock says; Cloud is Lock when lock is on. */
    Kind kindOf (core::Engine engine, bool lock) noexcept;

    /** Which of a context's settings come from the instrument (a note without its own). */
    struct From
    {
        bool lock = false, motion = false, tape = false, rev = false, jump = false;
    };

    struct Context
    {
        bool inst = false;          // who: the instrument itself ('inst'), or notes with their own mix ('notes')
        int count = 0;              // how many notes play this way (0 for the instrument)
        Kind kind = Kind::voice;
        core::Engine engine = core::Engine::voice;
        core::Motion motion = core::Motion::forward;   // Tape plays Freeze as Forward
        model::TravelKind travel = model::TravelKind::speed;
        From from;
    };

    /** SLSampleUI.contexts (I, clip): the instrument first, then each distinct mix of the notes' own settings, in the
        order the notes first show it. clip may be null (no notes). */
    std::vector<Context> contexts (const core::Inst& inst, const model::Clip* clip);

    /** SLSampleUI.headsMulti: can more than one head ever sound? (Spread and Head tune act on heads 2 to 8.) */
    bool headsMulti (const core::Inst& inst, const model::Clip* clip);

    /** SLSampleUI.toneAllOpen: is every Tone line (and the live keys' default) fully open? */
    bool toneAllOpen (const core::Inst& inst, const model::Clip* clip);

    /** SLSampleUI.randomInUse: does anything random play now (what Re-roll changes)? */
    bool randomInUse (const core::Inst& inst, const model::Clip* clip);

    //==============================================================================================
    // RULES and fades.

    /** The rules, in RULES' order. */
    enum class Rule : std::uint8_t
    {
        lock, freeze, speed, drift, spread, headTune, grain, spray, tape, rev, atEnd, jump, refreshMs, refreshRate, skip,
        mSpeed, mSpread, mRefresh, mGrain, mSpray, mTone, reroll
    };

    inline constexpr int kNumRules = 22;

    /** The rule's id in RULES ("lock", "headTune", "mSpeed" ...). */
    const char* ruleId (Rule) noexcept;

    /** What a rule's control does now. notes: how many notes use it through their own settings when the instrument
        does not; reason: why it does nothing for the instrument (empty when the instrument uses it). */
    struct Fade
    {
        bool faded = false;
        int notes = 0;
        std::string reason;
    };

    using Fades = std::array<Fade, kNumRules>;

    inline const Fade& fadeOf (const Fades& f, Rule r) noexcept { return f[static_cast<std::size_t> (r)]; }

    /** SLSampleUI.fades (I, clip): every rule's state. */
    Fades fades (const core::Inst& inst, const model::Clip* clip);

    /** The reason a rule gives for the instrument's own context (RULES[id].why, called with it when it is a
        function). */
    std::string ruleWhy (Rule rule, const Context& instContext);

    /** SLSampleUI.grainKind: the grain setting the Grain knob shows: the instrument's engine; in Tape, the grain engine
        most notes use on their own (ties: Voice, Cloud, Lock); nothing when nothing plays grains. */
    std::optional<Kind> grainKind (const core::Inst& inst, const model::Clip* clip);

    // The faded controls' reasons that are not a rule's own (sl-ui-sample.js).
    inline constexpr const char* kGrainWhy = "Tape plays the sound straight through, with no grains. Pick Voice or Cloud to use Grain.";

    //==============================================================================================
    // Suggestions (E3, U2; used from M7).

    /** One suggested part of a file (D10; SLAnalysis.suggestParts' items, as the prototype passes them). */
    struct Part
    {
        std::string kind;            // 'steady', 'loop', 'quiet', 'hit'
        std::string label;
        double start = 0.0, end = 0.0;
        bool tempoFound = false;
        double bpm = 0.0;
    };

    /** What a loaded file is like: { pitch, seconds, parts }; seconds NaN = missing; a part may be missing (null). */
    struct SoundInfo
    {
        bool pitch = false;
        double seconds = std::numeric_limits<double>::quiet_NaN();
        std::vector<std::optional<Part>> parts;
    };

    struct EngineSuggestion
    {
        core::Engine engine = core::Engine::voice;
        std::string text, why;
    };

    /** SLSampleUI.suggestEngine (info); info may be null. */
    EngineSuggestion suggestEngine (const SoundInfo* info);

    /** SLSampleUI.pickPart (parts): a held note first, then a loop, a quiet texture, a hit; else the first part with
        some length; nothing when there is none. parts may be null. */
    std::optional<Part> pickPart (const std::vector<std::optional<Part>>* parts);

    //==============================================================================================
    // Readouts and parsing (one copy each: params/ParamText's, which the parameters print with).

    std::string macroText (int i, double m);
    std::string offsetText (int k, double o);   // k: 0 amp, 1 vib, 2 tone, 3 formant, 4 hollow; another k prints the number
    std::optional<double> parseNum (std::string_view text);
    std::optional<double> parseMs (std::string_view text);
    std::optional<double> parsePct (std::string_view text);

    //==============================================================================================
    // KNOB_MATH.

    struct KnobRange
    {
        double min = 0.0, max = 1.0;
        bool log = false;
        double def = 0.0;
    };

    /** toNorm (s, v): 0..1 along the knob (logarithmic when s.log); a value that is not a number reads as the default. */
    double toNorm (const KnobRange& s, double v) noexcept;

    /** fromNorm (s, n). */
    double fromNorm (const KnobRange& s, double n) noexcept;

    /** magnetLog (v, marks, tol): the first mark within tol octaves of v, else v. */
    double magnetLog (double v, std::span<const double> marks, double tol) noexcept;

    /** qLog (marks, digits) (v, fine): fine = 4 significant digits; else digits (0 = 3) pulled to a mark within 0.06 octave. */
    double qLog (std::span<const double> marks, int digits, double v, bool fine);

    /** qLin (step, fineStep, marks, tol) (v, fine): rounded to the step (6 decimals); the last mark within tol wins. */
    double qLin (double step, double fineStep, std::span<const double> marks, double tol, double v, bool fine) noexcept;

    /** r6: Math.round (v * 1e6) / 1e6. */
    double r6 (double v) noexcept;

    //==============================================================================================
    // The knobs (buildSampleTab, buildMacros).

    enum class KnobId : std::uint8_t
    {
        speed, heads, spread, grain, spray, drift, pitch, refreshMs, skip,
        macro1, macro2, macro3, macro4, macro5, macro6, macro7, macro8,
        ampOffset, vibOffset, toneOffset, formantOffset, hollowOffset
    };

    inline constexpr int kNumKnobs = 22;

    enum class KnobFormat : std::uint8_t { x, heads, pct, cycles, cloudMs, lockWaves, st, ms, macro, offset };
    enum class KnobQuant : std::uint8_t { speed, heads, pct, voiceGrain, cloudGrain, lockGrain, pitch, refreshMs, macro, offset };
    enum class KnobParse : std::uint8_t { none, num, ms, pct };
    enum class KnobColour : std::uint8_t { sample, pitch, amp, vib, tone, formant, hollow };

    /** One knob as the tab shows it now (its spec merged with the Grain knob's engine, sl-ui-sample.js dyn). */
    struct KnobSpec
    {
        KnobId id = KnobId::speed;
        const char* label = "";        // the name over the dial: "Speed"
        const char* undo = "";         // the undo step's name: "Speed", "Macro Speed", "Offset Volume"
        const char* title = "";        // what it does: the tooltip, and the hint line's first part
        const char* example = "";      // the typing toast's example ("" = the default's readout)
        const char* pin = nullptr;     // the Dice field a pin keeps (T3), or none
        params::P param = params::P::speed;
        KnobRange range;
        double tmin = 0.0, tmax = 1.0; // what a typed value is clamped to
        bool bipolar = false;
        KnobFormat format = KnobFormat::x;
        int index = 0;                 // the macro (0..7) or offset (0 amp .. 4 hollow)
        KnobQuant quant = KnobQuant::speed;
        KnobParse parse = KnobParse::none;
        KnobColour colour = KnobColour::sample;
        std::optional<Rule> rule;      // the fade rule it follows
        bool off = false;              // nothing to do at all now (the Grain knob in Tape with no grain notes)
        const char* offWhy = "";
        bool macroRow = false;         // drawn with the macros' thicker arc (.sls-macros)
    };

    /** The knob, resolved for the grain kind the Grain knob follows (others ignore it; nothing = the Grain knob is off). */
    KnobSpec knobSpec (KnobId id, std::optional<Kind> grain = Kind::voice);

    /** Every knob's id, the Sample tab's then the Macros tab's (the macros, then the offsets). */
    const std::array<KnobId, kNumKnobs>& allKnobs() noexcept;

    /** The parameters a knob can write (the Grain knob: all three grain settings). */
    std::vector<params::P> knobParams (KnobId id);

    /** The readout at v (cur.fmt). */
    std::string knobText (const KnobSpec& s, double v);

    /** The value a drag lands on (cur.q), before the range clamp. */
    double knobQuant (const KnobSpec& s, double v, bool fine);

    /** A typed text read (cur.parse); nothing when it is not a number, or the knob takes no typing. */
    std::optional<double> knobParse (const KnobSpec& s, std::string_view text);

    /** The value a typed number sets: rounded to 6 decimals and clamped to the typed range. */
    double typedValue (const KnobSpec& s, double parsed) noexcept;

    /** "Type a number, for example 0.5": the toast for a typed text that is not a number. */
    std::string typingHelp (const KnobSpec& s);

    /** What the hint line says while the mouse is on the knob (helpOn), given its fade state (null: none). */
    std::string knobHint (const KnobSpec& s, const Fade* state);

    /** The knob's tooltip (its title, with the fade's sentence: fadeTitle; off: "Does nothing now: ..."). */
    std::string knobTitle (const KnobSpec& s, const Fade* state);

    /** fadeTitle (base, st): base, with "Does nothing now: <reason>" before it when faded, or how many notes use it
        through their own settings after it. */
    std::string fadeTitle (const std::string& base, const Fade* state);

    //==============================================================================================
    // A knob drag (knob ()'s pointer handlers): the pointer's y in the knob's own pixels, up = more.

    inline constexpr double kDragPixels = 170.0;    // a drag this far crosses the whole knob
    inline constexpr double kFineFactor = 0.12;     // Shift
    inline constexpr double kDeadZone = 2.0;        // a press that moves less than this changes nothing

    struct KnobDrag
    {
        double y0 = 0.0, n0 = 0.0, v0 = 0.0, last = 0.0;
        bool moved = false;
    };

    /** The press: where it starts, at value v (the value the knob shows). */
    KnobDrag beginDrag (const KnobSpec& s, double y, double v) noexcept;

    /** A move to y: the value to set when it changed (the knob writes it at once, no undo step), else nothing. */
    std::optional<double> dragTo (const KnobSpec& s, KnobDrag& d, double y, bool fine);

    /** The release: true when the drag moved and ended away from where it started (one undo step). */
    inline bool dragCommits (const KnobDrag& d) noexcept { return d.moved && std::islessgreater (d.last, d.v0); }
}
