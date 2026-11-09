#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "diag/DiagModel.h"
#include "host/HostInfo.h"
#include "params/ParamBridge.h"
#include "params/ParameterLayout.h"
#include "probe/ProbeSynth.h"
#include "probe/SoundSlot.h"
#include "state/StateChunks.h"
#include "state/StateFormat.h"
#include "state/UserSettings.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>

/** StrandLock's processor (M0, the probe build): a VST3 instrument with the 50 v1 parameters, the binary state
    (PARM, INST, SMPL, UIST; CLIP and unknown chunks kept as they are), the diagnostics model, and the probe
    voice playing the starter Saw from MIDI. M2 replaces the probe with the engine.

    Threads (PLUGIN-PLAN 1.3): processBlock is the audio thread and only reads atomics, takes the sound with one
    atomic exchange and feeds the diagnostics lock-free. Everything named "message thread" below is for the
    editor and the host's state calls. */
class StrandLockProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorParameter::Listener
{
public:
    StrandLockProcessor();
    ~StrandLockProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "StrandLock"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==============================================================================================
    // For the editor and the tests (message thread unless it says otherwise).

    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }

    /** Parameter i (0..49, the order of params/ParamIds.h). */
    juce::RangedAudioParameter& getParam (int index) const noexcept { return *paramList[(std::size_t) index]; }
    juce::RangedAudioParameter& getParam (sl::params::P p) const noexcept { return getParam (sl::params::index (p)); }

    /** The instrument the audio thread builds from the parameters now (any thread). */
    sl::core::Inst readInst() const noexcept { return sl::params::readInst (paramRefs, nonParam); }

    sl::diag::DiagModel& getDiag() noexcept { return diag; }
    const sl::host::HostInfo& getHostInfo() const noexcept { return hostInfo; }
    sl::state::UserSettings& getUserSettings() noexcept { return *userSettings; }

    /** The loaded sound (the waveform strip draws it): the newest one, shared; never nullptr after construction. */
    std::shared_ptr<const sl::sounds::StarterSound> getLoadedSound() const { return soundSlot.latest(); }

    /** Loads a starter (synchronously: tens of milliseconds the first time, shared afterwards) and records it in SMPL. */
    void loadStarter (sl::sounds::StarterId id, std::int32_t seed = 1);

    sl::state::InstState getInstState() const;
    void setInstState (const sl::state::InstState& s);

    sl::state::UiState getUiState() const;
    void setUiState (const sl::state::UiState& s);

    sl::state::SampleRef getSampleRef() const;

    /** The theme to draw: the per-user choice when there is one, else the project's UIST copy. */
    sl::state::Theme getTheme() const;

    /** Sets the theme for this person (UserSettings, every project) and in this project's UIST copy. */
    void setTheme (sl::state::Theme theme);

    /** Non-empty while a state this version could not read (all of it, or a chunk) is being written back
        unchanged: the banner text. */
    juce::String getStateBanner() const;

    /** The number of chunks of the loaded state this version does not know (they are written back as they were). */
    int getUnknownChunkCount() const;

    /** The probe's counters (audio thread data; read them while it is not processing). */
    const sl::probe::ProbeSynth::Stats& getProbeStats() const noexcept { return probe.stats(); }
    void resetProbeStats() noexcept { probe.resetStats(); }

    /** The probe's sound slot (the tests check what it holds). */
    const sl::probe::SoundSlot& getSoundSlot() const noexcept { return soundSlot; }

private:
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

    sl::state::Payloads currentPayloads() const;   // under stateLock
    void applyInstToAtomics (const sl::state::InstState& s) noexcept;
    void handleMidi (const juce::uint8* data, int size, int samplePosition) noexcept;
    void loadState (const void* data, std::size_t size);

    sl::params::TextContext textContext;
    juce::AudioProcessorValueTreeState apvts;
    std::array<juce::RangedAudioParameter*, sl::params::kNumParams> paramList {};
    sl::params::ParameterRefs paramRefs;
    sl::params::NonParamAtomics nonParam;

    sl::probe::ProbeSynth probe;
    sl::probe::SoundSlot soundSlot;
    sl::diag::DiagModel diag;
    sl::host::HostInfo hostInfo;
    std::shared_ptr<sl::state::UserSettings> userSettings;

    // The state outside the parameters, and what the last load kept (message side, under stateLock).
    mutable std::mutex stateLock;
    sl::state::InstState instState;
    sl::state::UiState uiState;
    sl::state::SampleRef sampleRef;
    std::vector<sl::state::ParmEntry> parmExtras;   // PARM entries for parameters this version does not have
    sl::state::Bytes parmTrailing;
    sl::state::KeptLayout kept;

    // Diagnostics: which parameters a StrandLock control is changing now (a gesture), and loads in progress.
    std::array<std::atomic<bool>, sl::params::kNumParams> inGesture {};
    std::atomic<int> loadingState { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrandLockProcessor)
};
