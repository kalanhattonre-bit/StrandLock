#pragma once

// The APVTS layout of the 50 v1 parameters (PLUGIN-PLAN 2), built from params/ParamIds.h.
//
// Float parameters are FloatParameter, not juce::AudioParameterFloat: it keeps the plain value it was given
// exactly (setPlainNotifyingHost), so a value loaded from the state is the float that was saved, bit for bit,
// and a get -> set -> get of the state is byte-identical. (AudioParameterFloat can only be set through its
// normalised value, and plain -> normalised -> plain moves a skewed value by an ulp.) Host automation still
// sets the normalised value as usual. Choices, switches and whole numbers are JUCE's own classes: their
// values are whole numbers, which survive normalising exactly.

#include "ParamIds.h"
#include "ParamText.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace sl::params
{
    class FloatParameter final : public juce::RangedAudioParameter
    {
    public:
        FloatParameter (int paramIndex, juce::NormalisableRange<float> range, const TextContext* textContext);

        /** The plain value (any thread). */
        float get() const noexcept { return plain.load (std::memory_order_relaxed); }

        /** The plain value's atomic, which the audio thread reads (ParamBridge). */
        const std::atomic<float>& plainValue() const noexcept { return plain; }

        float getDefaultPlain() const noexcept { return defaultPlain; }

        /** Sets exactly this plain value (clamped to the range) and tells the host and the listeners. */
        void setPlainNotifyingHost (float value);

        int getParamIndex() const noexcept { return index; }

        const juce::NormalisableRange<float>& getNormalisableRange() const override { return range; }
        float getValue() const override;
        void setValue (float newValue) override;
        float getDefaultValue() const override;
        juce::String getText (float normalisedValue, int maximumStringLength) const override;
        float getValueForText (const juce::String& text) const override;

    private:
        const int index;
        const juce::NormalisableRange<float> range;
        const float defaultPlain;
        std::atomic<float> plain;
        const TextContext* context;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatParameter)
    };

    /** The NormalisableRange of a float parameter: continuous (interval 0), skewed for its centre when it has one. */
    juce::NormalisableRange<float> rangeFor (const ParamSpec& spec);

    /** Every parameter in the frozen order. textContext must outlive the parameters (the processor owns both). */
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const TextContext* textContext);

    /** A parameter's plain value as the state saves it: the float, a choice's index, a switch's 0 or 1, the
        whole number. */
    float plainValueOf (const juce::RangedAudioParameter& parameter) noexcept;

    /** Sets a parameter to a plain value (clamped; a non-finite value means its default), telling the host. */
    void setPlainValue (juce::RangedAudioParameter& parameter, int paramIndex, float plain);

    /** The default plain value of parameter i. */
    float defaultPlain (int paramIndex) noexcept;

    /** A plain value made legal for parameter i: clamped, rounded to a whole number for choices, switches and
        integers, and the default when not finite. */
    float legalPlain (int paramIndex, float plain) noexcept;
}
