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
    nothing reads any more (collect). Since M3 part L every note plays the engine it asks for (Tape, Voice, Cloud or
    Lock). */
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

    /** The same as the audio thread builds it for this sample (M7 V3: on a Region excerpt the Region maps onto it). */
    sl::core::Inst readInstFor (const sl::engine::SampleData* playing) const noexcept { return sl::params::readInst (paramRefs, nonParam, playing); }

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

    //----------------------------------------------------------------------------------------------
    // Long songs and what listening suggests (M7, plan 7.9 decisions 1 and 2). Message thread.

    /** The part picker: the 10 minutes from startFrame (frames of the file) of a file longer than 10 minutes, loaded
        the way loadSoundFile loads a file (one undo step; SMPL's loaded range says which part). */
    std::uint64_t loadSoundFilePart (const juce::File& file, std::uint64_t startFrame);

    /** E3, the engine suggestion after a file loads (sl-ui-sample.js suggestion). */
    struct EngineSuggestion
    {
        sl::core::Engine engine = sl::core::Engine::cloud;
        std::string text, why;                                    // "Try Cloud", and the sentence that says why
        bool match = false;                                       // the engine that suits it is on already ("... (on)")
        sl::core::Engine shownEngine = sl::core::Engine::voice;   // the engine when it was made (another one ends it)
        bool done = false;                                        // used, hidden, or the engine changed since
    };

    /** What listening to the sound that plays now found, and what it suggests. None of it applies itself (decision 2):
        a root is one click and no undo step, a key one undo step, the engine a button. */
    struct Suggestions
    {
        std::uint64_t soundRequest = 0;            // the load of the sound they belong to
        sl::doc::SoundAnalysis analysis;           // the strip's words (key, no clear pitch) and the part chips
        std::optional<sl::doc::RootHeard> root;    // "Use A2 as the root": shown while the Region is the one it was heard in
        std::optional<sl::analysis::Key> key;      // "Use key A minor": shown while it differs from the Key and Scale
        std::optional<EngineSuggestion> engine;    // "Try Cloud"
        bool listening = false;                    // Detect pitch & key is on its way
    };
    const Suggestions& getSuggestions() const noexcept { return suggestions; }

    /** Whether each suggestion shows now (sl-ui.js updateStripText's and sl-ui-sample.js renderChips' rules). */
    bool showsRootSuggestion() const;
    bool showsKeySuggestion() const;
    bool showsEngineSuggestion() const;

    /** "Detect pitch & key" (sl-ui.js runDetect 'button'): the worker listens to the Region (a whole song has no single
        pitch) and to the sound's key; when it is done the root and cents are set with no undo step (plan 2.4) and the
        key and scale as one undo step "Key from the sound: ...", and a toast says what was found. False (and a toast)
        for a built-in sound, which is tuned to C3 already. */
    bool detectPitchAndKey();

    /** "Use A2 as the root": rootNote and rootCents as host-visible edits, no undo step (the root belongs to the
        sound, plan 2.4). False when no suggestion shows (it was heard in another Region). */
    bool acceptRootSuggestion();

    /** "Use key A minor": the Key and Scale as one undo step "Key from the sound: A minor". */
    bool acceptKeySuggestion();

    /** "Try Cloud": the engine as one undo step "Engine: Cloud (suggested)" (none when it is on already); the toast
        says why. The suggestion is done either way. */
    bool acceptEngineSuggestion();

    /** Hides the engine suggestion (its "(on)" chip says why once, in a toast, when explain is true). */
    void dismissEngineSuggestion (bool explain);

    /** A part chip (D10, decision 2): the Region set to the part, as one undo step "Region: <label>". False when the
        Region is that part already. */
    bool usePart (const sl::analysis::Part& part);

    /** "Use the whole file": the Region set to the whole file, one undo step "Region: whole file". */
    bool useWholeFile();

    /** Sets the Region (the parameters and the exact f64 edges, plan 2.2) as one undo step called label (sl-ui-sample.js
        setRegionTo: nothing when it is already within 1e-9 of that). */
    bool setRegionStep (double start, double end, const std::string& label);

    /** The editor changed the Region (a strip drag): the root suggestion for the old one goes now, and a moment later
        (250 ms, sl-ui.js suggestRoot) the worker listens to the new one. */
    void regionChanged();

    /** The Region StrandLock plays now: each exact f64 edge while its parameter still holds (float) of it, else the
        parameter's value (ParamBridge::readInst's rule, plan 2.2). */
    std::pair<double, double> currentRegion() const;

    /** Message thread: applies the worker's analysis results (Detect, root suggestions) and keeps the suggestions with
        the sound that plays (the processor's AsyncUpdater calls it; the tests, which run no message loop, too). */
    int deliverAnalysis();

    /** Asks now for a root suggestion that is due, waits for the worker's jobs (at most timeoutMs), then delivers
        them. For the tests and the snapshots. */
    bool waitForAnalysis (int timeoutMs = kLoadWaitMs);

    /** The part picker's overview of the playing file (a part of a longer file): asked for once, then kept. */
    std::uint64_t requestOverview();
    std::shared_ptr<const sl::doc::FileOverview> getOverview() const;

    /** How many states the undo history holds (the tests count steps with it). */
    std::size_t historySize() const;

    //----------------------------------------------------------------------------------------------
    // Keeping the sound with the project (M7 V3, PLUGIN-PLAN 3 "Embedding" and "Finding the sound on load", 7.9
    // decision 5). Message thread unless it says otherwise.

    /** "Keep the sound in the project": the project's switch (SMPL keepInProject, on by default). On, a file of up to
        20 MB is kept whole without asking; from 20 to 50 MB StrandLock asks once (Yes / No / Just the Region); above
        50 MB (or after Just the Region) the Region and up to 1 s either side are kept as FLAC 24-bit, never more than
        50 MB. Off, nothing is kept and nothing is asked. Not an undo step: it says how the project holds the sound, not
        what plays. */
    bool getKeepInProject() const;
    void setKeepInProject (bool on);

    /** The answers to the 20-50 MB question (SMPL embedAnswer). */
    enum class EmbedAnswer : std::uint8_t { notAsked = 0, yes = 1, no = 2, region = 3 };

    /** The person's answer for the file playing now: saved in SMPL, so later saves, auto-saves and reloads never ask
        again (loading another file asks again). Not an undo step. False when no question shows. */
    bool answerEmbedQuestion (EmbedAnswer answer);

    /** The library (Documents\StrandLock\Samples): per person (UserSettings), off unless turned on. Turned on, the file
        playing now is copied there (on the worker), and so is every file loaded after; a project's moved file is found
        there by its SHA-256. */
    bool isLibraryOn() const;
    void setLibraryOn (bool on);

    /** The bytes the library holds (-1 until the worker has measured it). Any thread. */
    std::int64_t getLibraryBytes() const noexcept { return sampleManager->libraryBytes(); }

    /** Asks the worker to measure the library, once (the Sound tab shows its size, the library on or off). */
    void measureLibrary();

    /** What the sound line under the top bar says now: the 20-50 MB question; the Locate banner (the project's file was
        found nowhere: every setting is kept until a file is chosen); or that only part of the sound was kept. */
    struct SoundNotice
    {
        enum class Kind : std::uint8_t { none, question, locate, partKept };
        Kind kind = Kind::none;
        std::string text;
    };
    SoundNotice getSoundNotice() const;

    /** Locate sample...: the file the person chose for the project's missing sound (or for the whole of a sound only
        part of which was kept). It plays with every setting as it was (the Region, the root, the part loaded); one undo
        step "Locate <name>". */
    std::uint64_t locateSound (const juce::File& file);

    /** What the project keeps of its sound now, in words (the Sound tab's line). */
    std::string getEmbedStatus() const;

    /** The SAUD payload the next save writes (null: none). Any thread. */
    sl::state::PayloadPtr getEmbeddedPayload() const;

    /** Brings the SAUD the next save writes up to date with the sound, the switch, the answer and the Region (a Region
        excerpt at once, not after the Region has settled), waiting for the worker at most timeoutMs. True when nothing is
        left on its way. getStateInformation calls it (at most kSaveEmbedWaitMs). Any thread. */
    bool waitForEmbed (int timeoutMs = kLoadWaitMs);

    /** How long a save waits for a Region excerpt the worker is making. */
    static constexpr int kSaveEmbedWaitMs = 5000;

    /** Waits until the newest clip version is compiled and handed to the audio thread (at most timeoutMs). */
    bool waitForSnapshot (int timeoutMs = kLoadWaitMs);

    /** UI -> audio commands (doc/CommandQueue.h): processBlock drains it at the start of every block. Auditions,
        "Let go" of a wheel and all notes off play now; the previewStart / previewStop commands and MIDI Learn (M8)
        are taken and ignored (Preview's switch is setPreview below). */
    sl::doc::CommandQueue& getCommandQueue() noexcept { return *commands; }

    //----------------------------------------------------------------------------------------------
    // Preview (U5, plan 1.10, 7.8 decision 3).

    /** Kalan's Preview switch (message thread). While it is on and Cubase is stopped, the audio thread plays the clip
        at Cubase's tempo from Cubase's position (host/HostSync). The audio thread turns it off itself when Cubase
        plays: Preview never runs while the host plays. The switch is one atomic word, the on bit and a count of
        clicks, so that stop never undoes a click that came after the block that stopped it read the switch
        (host::PreviewSwitch). */
    void setPreview (bool on);
    bool isPreviewOn() const noexcept { return previewSwitch.isOn(); }

    /** How many times the audio thread turned Preview off because Cubase played; the Sample tab says why. (At the end
        of a clip that does not loop it turns off too, as the prototype's Play does, and is not counted.) Any thread. */
    std::uint32_t getPreviewStops() const noexcept { return previewStops.load (std::memory_order_acquire); }

    /** The audio thread's last block played the Preview clock. Any thread. */
    bool isPreviewPlaying() const noexcept { return previewPlaying.load (std::memory_order_acquire); }

    /** Blocks processBlock has played since the processor was made (any thread): the Sample tab tells Kalan when
        none arrive while Preview is on (Cubase has paused StrandLock while it is stopped). */
    std::uint64_t getBlocksProcessed() const noexcept { return blocksProcessed.load (std::memory_order_acquire); }

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

    // M7 (message thread).
    void syncSound();                                       // the suggestions and the SMPL cache follow the sound that plays
    void applyDetect (const sl::doc::JobResult& result);    // runDetect 'button' once the worker heard it
    void setRoot (int root, int cents);                     // no undo step; the sound keeps it (SoundMeta, plan 2.4)
    void rememberRoot (const sl::state::SampleRef& sound, int root, float cents);
    std::optional<std::pair<int, float>> rootOf (const sl::state::SampleRef& sound) const;
    void rememberCache (const sl::state::SampleRef& sound);
    void keepCacheOf (sl::state::SampleRef& sound) const;   // the newest cache known for that file
    void say (const std::string& text, int ms);             // a toast of the processor's own
    void updateEngineSuggestion();                          // E3 ends once the engine changes by other means
    int currentRoot() const;
    float currentCents() const;

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

    // Preview (M6): the switch (bit 0 on, the bits above count the clicks), the audio thread's own stops, whether its
    // last block played the Preview clock, and the blocks played (the paused-host check).
    sl::host::PreviewSwitch previewSwitch;
    std::atomic<std::uint32_t> previewStops { 0 };
    std::atomic<bool> previewPlaying { false };
    std::atomic<std::uint64_t> blocksProcessed { 0 };

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

    // M7 (message thread): what listening suggests for the sound that plays, each sound's own root and cents (plan
    // 2.4's SoundMeta), the newest analysis cache of the last few files, and the processor's own toasts.
    Suggestions suggestions;
    std::shared_ptr<const sl::doc::SoundView> seenSound;
    std::vector<std::pair<sl::state::SampleRef, std::pair<int, float>>> soundMeta;
    std::vector<sl::state::SampleRef> knownCaches;
    std::vector<sl::doc::LoadReport> ownReports;
    double rootDueMs = -1.0;   // when the root suggestion for a new Region is asked for, or -1

    // M7 V3: keeping the sound with the project. Under embedLock (any thread: a save brings it up to date); stateLock is
    // never taken while it is held.
    struct Embedded
    {
        sl::state::PayloadPtr payload;     // the SAUD chunk the next save writes (null: none)
        sl::state::SampleRef sound;        // the SMPL record it keeps
        sl::state::SaudHeader header;      // what it holds
        bool fromProject = false;          // the loaded project's own SAUD, kept while its sound is the project's
        bool usable = true;                // false: a SAUD this version cannot play, kept as it was for its sound
        bool fromMemory = false;           // the file could not be read again: the sound as it played
    };
    void syncEmbed (bool now);                     // the SAUD follows the sound, the switch, the answer and the Region
    void deliverEmbed();                           // the worker's embed results
    void askEmbed (bool original, std::int64_t from, std::int64_t frames, std::uint64_t soundRequest, const std::string& key);   // under embedLock
    void setEmbedded (Embedded e);                 // under embedLock: the one before is remembered for Undo
    std::string takeEmbedToast();
    sl::state::PayloadPtr saudFor (const sl::state::SampleRef& sound) const;   // a SAUD this session holds for those bytes
    void keepAnswerOf (sl::state::SampleRef& sound) const;                       // the answer those bytes were given
    void rememberAnswer (const sl::state::SampleRef& sound);
    mutable std::mutex embedLock;
    Embedded embedded;
    std::vector<Embedded> otherSauds;              // the last sounds' SAUDs, so Undo can bring back a sound only they hold
    std::vector<sl::state::SampleRef> embedAnswers;   // message thread: the newest answer given for each file's bytes
    std::uint64_t embedJob = 0;                    // the embed job on its way (0: none)
    std::uint64_t embedJobSound = 0;               // the sound request it is for
    std::string embedJobKey;                       // what it makes
    std::string embedTooLong;                      // the excerpt found too long (not asked for again)
    std::string embedNote;                         // why nothing (or less) is kept now
    std::string embedToast;                        // a toast for the message thread
    std::pair<double, double> embedRegion { -1.0, -1.0 };   // a Region excerpt waits for the Region to settle
    double embedRegionSince = 0.0;
    std::atomic<bool> keepInProject { true };      // the project's switch (a file's SMPL holds it too)
    bool libraryMeasureAsked = false;              // message thread: measureLibrary asked the worker once

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrandLockProcessor)
};
