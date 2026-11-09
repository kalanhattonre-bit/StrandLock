#include "ParameterLayout.h"

#include "core/JsCompat.h"

#include <cmath>
#include <limits>
#include <string>

namespace sl::params
{
    namespace
    {
        juce::String toJuce (const std::string& s, int maximumLength)
        {
            const auto text = juce::String::fromUTF8 (s.c_str());
            return maximumLength > 0 ? text.substring (0, maximumLength) : text;
        }

        juce::ParameterID pid (int i) { return juce::ParameterID { kParams[i].id, kParamVersionHint }; }

        /** A whole-number parameter's value for a text: what it means, else the default. */
        int wholeFromText (int i, const juce::String& text, const TextContext* ctx)
        {
            const auto v = paramValueFromText (i, text.toStdString(), ctx);
            return (int) legalPlain (i, v ? (float) *v : kParams[i].def);
        }
    }

    //==============================================================================================
    FloatParameter::FloatParameter (int paramIndex, juce::NormalisableRange<float> r, const TextContext* textContext)
        : RangedAudioParameter (pid (paramIndex), kParams[paramIndex].name),
          index (paramIndex),
          range (std::move (r)),
          defaultPlain (kParams[paramIndex].def),
          plain (kParams[paramIndex].def),
          context (textContext)
    {
        static_assert (std::atomic<float>::is_always_lock_free, "the audio thread reads plain values lock-free");
    }

    void FloatParameter::setPlainNotifyingHost (float value)
    {
        const float v = legalPlain (index, value);
        plain.store (v, std::memory_order_relaxed);
        sendValueChangedMessageToListeners (convertTo0to1 (v));
    }

    float FloatParameter::getValue() const          { return convertTo0to1 (get()); }
    void FloatParameter::setValue (float newValue)  { plain.store (convertFrom0to1 (newValue), std::memory_order_relaxed); }
    float FloatParameter::getDefaultValue() const   { return convertTo0to1 (defaultPlain); }

    juce::String FloatParameter::getText (float normalisedValue, int maximumStringLength) const
    {
        return toJuce (paramText (index, (double) convertFrom0to1 (normalisedValue), context), maximumStringLength);
    }

    float FloatParameter::getValueForText (const juce::String& text) const
    {
        const std::string typed = text.trim().toStdString();
        const auto parsed = paramValueFromText (index, typed, context);
        if (! parsed)
            return getValue();

        const float n = convertTo0to1 (legalPlain (index, (float) *parsed));
        if (paramText (index, (double) convertFrom0to1 (n), context) == typed)
            return n;

        // A readout at a step boundary ("1000 ms" is 999.5 to 1000; 1000 itself prints "1 s"): try just inside
        // the printed step on either side of the number read.
        const double below = *parsed - std::abs (*parsed) * 1e-9 - 1e-12;
        for (const double halfStep : { readoutHalfStep (index, *parsed), readoutHalfStep (index, below) })
        {
            if (! (halfStep > 0.0) || ! std::isfinite (halfStep))
                continue;
            for (const double fraction : { -0.45, 0.45, -0.9, 0.9 })
            {
                const float candidate = convertTo0to1 (legalPlain (index, (float) (*parsed + fraction * halfStep)));
                if (paramText (index, (double) convertFrom0to1 (candidate), context) == typed)
                    return candidate;
            }
        }

        // The text is a readout the plain reading does not print back (a coarse or saturating one): take the
        // nearest value that prints exactly this text, so reading a readout and printing it again changes nothing.
        constexpr int steps = 4096;
        float best = n;
        float bestDistance = std::numeric_limits<float>::infinity();
        for (int k = 0; k <= steps; ++k)
        {
            const float candidate = (float) k / (float) steps;
            const float distance = std::abs (candidate - n);
            if (distance < bestDistance && paramText (index, (double) convertFrom0to1 (candidate), context) == typed)
            {
                best = candidate;
                bestDistance = distance;
            }
        }
        return best;
    }

    //==============================================================================================
    juce::NormalisableRange<float> rangeFor (const ParamSpec& s)
    {
        juce::NormalisableRange<float> r (s.min, s.max, 0.0f);
        if (s.hasSkew)
            r.setSkewForCentre (s.skewCentre);
        return r;
    }

    float defaultPlain (int i) noexcept
    {
        return i >= 0 && i < kNumParams ? kParams[i].def : 0.0f;
    }

    float legalPlain (int i, float plain) noexcept
    {
        if (i < 0 || i >= kNumParams)
            return 0.0f;
        const ParamSpec& s = kParams[i];
        if (! std::isfinite (plain))
            return s.def;
        float v = plain < s.min ? s.min : plain > s.max ? s.max : plain;
        if (s.kind != Kind::real)
            v = (float) core::jsRound ((double) v);
        return v;
    }

    float plainValueOf (const juce::RangedAudioParameter& p) noexcept
    {
        if (const auto* f = dynamic_cast<const FloatParameter*> (&p))
            return f->get();
        if (const auto* c = dynamic_cast<const juce::AudioParameterChoice*> (&p))
            return (float) c->getIndex();
        if (const auto* b = dynamic_cast<const juce::AudioParameterBool*> (&p))
            return b->get() ? 1.0f : 0.0f;
        if (const auto* w = dynamic_cast<const juce::AudioParameterInt*> (&p))
            return (float) w->get();
        return p.convertFrom0to1 (p.getValue());
    }

    void setPlainValue (juce::RangedAudioParameter& p, int i, float plain)
    {
        const float v = legalPlain (i, plain);
        if (auto* f = dynamic_cast<FloatParameter*> (&p))
        {
            if (! juce::exactlyEqual (f->get(), v))
                f->setPlainNotifyingHost (v);
            return;
        }
        if (! juce::exactlyEqual (plainValueOf (p), v))
            p.setValueNotifyingHost (p.convertTo0to1 (v));
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const TextContext* ctx)
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        for (int i = 0; i < kNumParams; ++i)
        {
            const ParamSpec& s = kParams[i];
            switch (s.kind)
            {
                case Kind::real:
                    layout.add (std::make_unique<FloatParameter> (i, rangeFor (s), ctx));
                    break;

                case Kind::choice:
                {
                    juce::StringArray choices;
                    for (int c = 0; c < s.numChoices; ++c)
                        choices.add (s.choices[c]);
                    layout.add (std::make_unique<juce::AudioParameterChoice> (
                        pid (i), s.name, choices, (int) s.def,
                        juce::AudioParameterChoiceAttributes()
                            .withStringFromValueFunction ([i, ctx] (int v, int length) { return toJuce (paramText (i, v, ctx), length); })
                            .withValueFromStringFunction ([i, ctx] (const juce::String& t) { return wholeFromText (i, t, ctx); })));
                    break;
                }

                case Kind::toggle:
                    layout.add (std::make_unique<juce::AudioParameterBool> (
                        pid (i), s.name, s.def >= 0.5f,
                        juce::AudioParameterBoolAttributes()
                            .withStringFromValueFunction ([i, ctx] (bool v, int length) { return toJuce (paramText (i, v ? 1.0 : 0.0, ctx), length); })
                            .withValueFromStringFunction ([i, ctx] (const juce::String& t) { return wholeFromText (i, t, ctx) != 0; })));
                    break;

                case Kind::whole:
                    layout.add (std::make_unique<juce::AudioParameterInt> (
                        pid (i), s.name, (int) s.min, (int) s.max, (int) s.def,
                        juce::AudioParameterIntAttributes()
                            .withStringFromValueFunction ([i, ctx] (int v, int length) { return toJuce (paramText (i, v, ctx), length); })
                            .withValueFromStringFunction ([i, ctx] (const juce::String& t) { return wholeFromText (i, t, ctx); })));
                    break;
            }
        }
        return layout;
    }

    int findParam (std::string_view id) noexcept
    {
        for (int i = 0; i < kNumParams; ++i)
            if (id == kParams[i].id)
                return i;
        return -1;
    }
}
