#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/Snapshot.h"
#include "diag/DiagModel.h"
#include "doc/ClipDocument.h"
#include "doc/CommandQueue.h"
#include "doc/EditHistory.h"
#include "doc/SampleManager.h"
#include "doc/SnapshotPublisher.h"
#include "engine/DisplayFrame.h"
#include "engine/Engine.h"
#include "engine/SampleData.h"
#include "host/HostInfo.h"
#include "host/HostSync.h"
#include "params/ParamBridge.h"
#include "params/ParameterLayout.h"
#include "rt/VersionExchange.h"
#include "state/StateChunks.h"
#include "state/StateFormat.h"
#include "state/UserSettings.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/** StrandLock's processor: a VST3 instrument with the 50 v1 parameters, the document (the clip and the INST
    state, with undo), the binary state (PARM, INST, CLIP through model::ClipCodec, SMPL starter and file
    records, UIST; unknown chunks kept as they are), the sound loader, the engine and the diagnostics.

    Threads (PLUGIN-PLAN 1.3): processBlock is the audio thread and only reads atomics and takes versions with one
    atomic exchange. The SnapshotPublisher compiles clips on its thread, the SampleManager makes and decodes sounds
    on its worker. Everything named "message thread" below is for the editor and the host's state calls.

    The audio path (plan 1.3, 1.10), once per block: the snapshot and the sample the exchanges hold go to the
    engine, the parameters (ParamBridge::readInst), the editor's commands (CommandQueue), then HostSync's transport
    and the block's MIDI, each at its own sample (MidiRouter renders up to each one and splits at a cycle wrap
    inside the block), then the epochs the engine still reads go back to the exchanges (setInUse) and, about 30
    times a second, a display frame goes to the DisplaySlot. A 10 Hz timer on the message thread frees the versions
    nothing reads any more (collect). Notes set to Lock play as Tape until M3 part L; the diagnostics count them. */
class StrandLockProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorParameter::Listener,
                                  private juce::AsyncUpdater,
                                  private juce::Timer
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

    /** How long a state load (and prepareToPlay) waits for the sound and the clip it asked for (decision 3). */
    static constexpr int kLoadWaitMs = 10000;

    /** The message-thread timer that frees old versions (plan 1.3: collect () at 10 Hz). */
    static constexpr int kCollectHz = 10;

    /** Whether the exchanges poison what they free (rt::VersionExchange): in Debug and sanitizer builds, and
        always in the test runner, whose exchange test checks that no version a voice still reads was freed. */
   #if defined (STRANDLOCK_TESTS)
    static constexpr bool kPoisonFreedVersions = true;
   #else
    static constexpr bool kPoisonFreedVersions = sl::rt::kPoisonByDefault;
   #endif

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

    //----------------------------------------------------------------------------------------------
    // The document.

    /** The clip and the INST state (any thread reads; edits through editClip). */
    sl::doc::ClipDocument& getDocument() noexcept { return clipDoc; }
    const sl::doc::ClipDocument& getDocument() const noexcept { return clipDoc; }

    /** An edit of the clip as one undo step called label, through the document's caps (4096 notes, 32 MiB, the
        snapshot budget). A refused edit changes nothing and its outcome holds the toast. */
    sl::doc::EditOutcome editClip (const std::string& label, sl::model::EditResult proposed);
    sl::doc::EditOutcome editClip (const std::string& label, sl::model::Clip proposed);

    /** Undo and Redo of the document (clip, INST state, sound, and the parameters the steps changed). */
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    std::optional<std::string> undoLabel() const;
    std::optional<std::string> redoLabel() const;

    /** One undo step called label for an edit the editor made itself, outside editClip (the strip's Region, At
        the end): the clip and the INST state as they are now, and the values the named parameters have now
        (plan 2.4: every other parameter keeps the previous step's value). False when nothing changed. */
    bool pushUndoStep (const std::string& label, const std::vector<sl::params::P>& changed);

    sl::state::InstState getInstState() const { return clipDoc.instState(); }
    void setInstState (const sl::state::InstState& s) { clipDoc.setInstState (s); }

    sl::state::UiState getUiState() const;
    void setUiState (const sl::state::UiState& s);

    /** The sound the project names (SMPL): a starter, or a file (kept as it was when the file is missing). */
    sl::state::SampleRef getSampleRef() const { return clipDoc.soundRef(); }

    //----------------------------------------------------------------------------------------------
    // Sounds (doc/SampleManager.h).

    /** The sound that plays now: what the waveform strip draws (its samples, name, whether the project's file
        is missing). Null only until the first sound is ready, a few tens of milliseconds after construction. */
    std::shared_ptr<const sl::doc::SoundView> getCurrentSound() const { return sampleManager->current(); }

    /** The user picked a starter (the menu) or a file (Browse, a drop). The worker makes or decodes it; when it
        is ready the processor sets the root, the cents and the Region (host-visible edits), records the sound,
        makes one undo step and publishes it, on the message thread (handleAsyncUpdate, or deliverSoundLoads). */
    std::uint64_t loadStarterSound (sl::sounds::StarterId id, std::int32_t seed = 1);
    std::uint64_t loadSoundFile (const juce::File& file);

    /** Message thread: applies the user loads that are ready (called by the processor's AsyncUpdater; the tests,
        which run no message loop, call it themselves). Returns how many were published. */
    int deliverSoundLoads();

    /** Waits until the sound loads asked for so far are made (at most timeoutMs), then delivers the user ones
        (message thread). For the tests and the editor snapshots. */
    bool waitForSounds (int timeoutMs = kLoadWaitMs);

    /** The load reports since the last call (the toasts: a missing file, a file cut to 10 minutes, a failure). */
    std::vector<sl::doc::LoadReport> takeSoundReports();

    sl::doc::SampleManager& getSampleManager() noexcept { return *sampleManager; }
    sl::doc::SnapshotPublisher& getSnapshotPublisher() noexcept { return *publisher; }

    /** Waits until the newest clip version is compiled and handed to the audio thread (at most timeoutMs). */
    bool waitForSnapshot (int timeoutMs = kLoadWaitMs);

    /** UI -> audio commands (doc/CommandQueue.h): processBlock drains it at the start of every block. Auditions,
        "Let go" of a wheel and all notes off play now; Preview (M6) and MIDI Learn (M8) are taken and ignored. */
    sl::doc::CommandQueue& getCommandQueue() noexcept { return *commands; }

    /** Frees every snapshot and sample version the audio thread no longer reads (plan 1.3's 10 Hz collect; the
        processor's own timer calls it, and the tests, which run no message loop, call it themselves). */
    void collectVersions();

    /** The engine's display frames for the editor (plan 1.5): the waveform strip draws the heads from them. The
        audio thread writes one after a block once engine::displayEvery (sr) samples have passed (about 30 a
        second); the editor's 30 Hz timer takes the newest. Never null. */
    sl::engine::DisplaySlot* getDisplaySlot() noexcept { return displaySlot.get(); }

    /** The engine (read it, and for the tests drive it, only from the audio thread or while it is not
        processing). */
    sl::engine::Engine& getEngine() noexcept { return *engine; }
    const sl::engine::Engine& getEngine() const noexcept { return *engine; }

    /** The engine's figures (read them while the audio thread is not processing). */
    sl::engine::Stats getEngineStats() const noexcept { return engine->stats(); }

    /** The two hand-offs to the audio thread (for the tests: the exchange test publishes into them directly). */
    sl::rt::VersionExchange<sl::core::Snapshot>& getSnapshotExchange() noexcept { return snapshots; }
    sl::rt::VersionExchange<sl::engine::SampleData>& getSampleExchange() noexcept { return samples; }

    //----------------------------------------------------------------------------------------------
    // Theme, banner, diagnostics.

    /** The theme to draw: the per-user choice when there is one, else the project's UIST copy. */
    sl::state::Theme getTheme() const;

    /** Sets the theme for this person (UserSettings, every project) and in this project's UIST copy. */
    void setTheme (sl::state::Theme theme);

    /** Non-empty while a state this version could not read (all of it, or a chunk) is being written back
        unchanged: the banner text. */
    juce::String getStateBanner() const;

    /** The number of chunks of the loaded state this version does not know (they are written back as they were). */
    int getUnknownChunkCount() const;

    /** The display frames the audio thread wrote since prepareToPlay (read it while it is not processing). */
    std::uint64_t getDisplayFramesWritten() const noexcept { return displayFramesWritten; }

    /** The sample and the snapshot the audio thread took last (read them while it is not processing). */
    const sl::engine::SampleData* getAudioSample() const noexcept { return samples.current(); }
    const sl::core::Snapshot* getAudioSnapshot() const noexcept { return snapshots.current(); }

private:
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void applyCommand (const sl::doc::UiCommand& command) noexcept;   // audio thread

    sl::state::Payloads currentPayloads() const;   // under stateLock
    sl::state::PayloadPtr clipPayload() const;     // under stateLock: the current clip's CLIP payload, encoded once per version
    void loadState (const void* data, std::size_t size);
    sl::doc::ParamValues currentParamValues() const;
    void resetHistory();
    void applyStep (const sl::doc::EditHistory::Step& step);
    void writeParams (const std::vector<std::pair<int, float>>& values);

    sl::params::TextContext textContext;
    juce::AudioProcessorValueTreeState apvts;
    std::array<juce::RangedAudioParameter*, sl::params::kNumParams> paramList {};
    sl::params::ParameterRefs paramRefs;
    sl::params::NonParamAtomics nonParam;

    sl::diag::DiagModel diag;
    sl::host::HostInfo hostInfo;
    std::shared_ptr<sl::state::UserSettings> userSettings;

    // The audio path (plan 1.3). The engine (tens of KB of fixed arrays, its voice pool made by the first
    // prepare) and the display slot (three 8.5 KB frames) are on the heap, so a processor stays small
    // (doc/processorFootprint).
    std::unique_ptr<sl::engine::Engine> engine = std::make_unique<sl::engine::Engine>();
    sl::host::HostSync hostSync;
    std::unique_ptr<sl::engine::DisplaySlot> displaySlot = std::make_unique<sl::engine::DisplaySlot>();
    int displayInterval = sl::engine::displayEvery (48000.0);   // audio thread (set in prepareToPlay)
    int samplesSinceDisplay = 0;                                // audio thread
    bool clipSilenced = false;                                  // audio thread: decision 3's wait for a loaded sound
    bool firstBlock = true;                                     // audio thread: the first block after prepareToPlay
    std::uint64_t displayFramesWritten = 0;                     // audio thread
    std::atomic<bool> snapRampsPending { false };               // a state load asks the next block to snap the ramps (plan 2.3)

    // The document and its hand-offs. Declared before the two threads below, so they outlive them.
    sl::doc::ClipDocument clipDoc { nonParam };
    sl::rt::VersionExchange<sl::core::Snapshot> snapshots { kPoisonFreedVersions };
    sl::rt::VersionExchange<sl::engine::SampleData> samples { kPoisonFreedVersions };
    std::unique_ptr<sl::doc::CommandQueue> commands = std::make_unique<sl::doc::CommandQueue>();   // on the heap (6 KB)
    mutable std::mutex historyLock;
    sl::doc::EditHistory history;
    std::unique_ptr<sl::doc::SnapshotPublisher> publisher;
    std::unique_ptr<sl::doc::SampleManager> sampleManager;

    // The state outside the parameters and the document, and what the last load kept (under stateLock).
    mutable std::mutex stateLock;
    sl::state::UiState uiState;
    std::vector<sl::state::ParmEntry> parmExtras;   // PARM entries for parameters this version does not have
    sl::state::Bytes parmTrailing;
    sl::state::KeptLayout kept;
    mutable sl::model::ClipPtr encodedClip;          // the clip encodedClipPayload was made from
    mutable sl::state::PayloadPtr encodedClipPayload;

    // Diagnostics: which parameters a StrandLock control is changing now (a gesture), and loads in progress.
    std::array<std::atomic<bool>, sl::params::kNumParams> inGesture {};
    std::atomic<int> loadingState { 0 };
    std::atomic<std::uint64_t> awaitedSound { 0 };   // the sound request the last state load waits for

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrandLockProcessor)
};
