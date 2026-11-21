#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "core/JsCompat.h"
#include "core/Music.h"
#include "host/MidiRouter.h"
#include "model/ClipCodec.h"
#include "model/Presets.h"
#include "ui/SampleTabRules.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

using namespace sl;

namespace
{
    const char* const kBanner = "This project was saved by a newer StrandLock (or could not be read): editing will replace it";

    /** Counts a state load in progress for as long as it lives (parameter changes it makes are not the host's). */
    struct LoadScope
    {
        explicit LoadScope (std::atomic<int>& c) noexcept : count (c) { ++count; }
        ~LoadScope() { --count; }
        LoadScope (const LoadScope&) = delete;
        LoadScope& operator= (const LoadScope&) = delete;
        std::atomic<int>& count;
    };

    /** The clip a new instance opens on (BLUEPRINT 5: "Strand Demo clip and the saw-that-opens-up sound load on
        first open"). */
    model::ClipPtr firstOpenClip()
    {
        if (auto demo = model::loadClipPreset ("strand-demo"))
            return std::make_shared<const model::Clip> (std::move (*demo));
        return std::make_shared<const model::Clip> (model::newClip (4.0));
    }

    /** The clip of a state that holds no readable CLIP chunk (an M0 project, a damaged chunk): an empty 4-bar clip,
        the clip's default, as a parameter the state does not mention gets its default. */
    model::ClipPtr defaultClip()
    {
        return std::make_shared<const model::Clip> (model::newClip (4.0));
    }

    /** The prototype's engine names (sl-ui-sample.js ENGINE_NAME). */
    const char* engineName (core::Engine e) noexcept
    {
        return e == core::Engine::tape ? "Tape" : e == core::Engine::voice ? "Voice" : "Cloud";
    }

    /** sl-ui.js fmtCents for the root: "+12 cents", "-5 cents". */
    std::string centsText (int c)
    {
        return (c >= 0 ? "+" : "") + std::to_string (c) + " cents";
    }

    /** sl-ui.js rootText: "A2", or "A2 +12 cents". */
    std::string rootText (int root, int cents)
    {
        return core::noteName ((double) root) + (cents != 0 ? " " + centsText (cents) : std::string());
    }

    /** Two Regions within 1e-9 of each other at both edges (sl-ui.js sugFits, sl-ui-sample.js setRegionTo). */
    bool sameRegion (double a0, double b0, double a1, double b1) noexcept
    {
        return std::abs (a0 - a1) < 1e-9 && std::abs (b0 - b1) < 1e-9;
    }

    /** How many sounds keep their own root (plan 2.4's SoundMeta) and their newest analysis cache, at most. */
    constexpr std::size_t kSoundsRemembered = 16;
}

StrandLockProcessor::StrandLockProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "StrandLock", params::createParameterLayout (&textContext))
{
    for (int i = 0; i < params::kNumParams; ++i)
    {
        paramList[(std::size_t) i] = apvts.getParameter (params::kParams[i].id);
        jassert (paramList[(std::size_t) i] != nullptr && paramList[(std::size_t) i]->getParameterIndex() == i);
    }
    paramRefs = params::makeParameterRefs (apvts);
    for (auto* p : paramList)
        p->addListener (this);

    hostInfo = host::detectHost();
    diag.setHostInfo (hostInfo);
    diag.setParameterDescriber ([this] (int i, float v)
    {
        if (i < 0 || i >= params::kNumParams)
            return juce::String ("#") + juce::String (i);
        return juce::String (params::kParams[i].name) + " (" + params::kParams[i].id + ") = "
             + juce::String::fromUTF8 (params::paramText (i, v, &textContext).c_str());
    });

    userSettings = state::UserSettings::shared();

    // The threads: the clip compiler and the sound worker (plan 1.3). Nothing makes a starter here.
    publisher = std::make_unique<doc::SnapshotPublisher> (snapshots);
    sampleManager = std::make_unique<doc::SampleManager> (samples);
    sampleManager->onPendingReady = [this] { triggerAsyncUpdate(); };
    sampleManager->onJobDone = [this] { triggerAsyncUpdate(); };
    sampleManager->onCommitted = [this] (const doc::SoundView& v) { textContext.soundSeconds.store (v.seconds()); };
    clipDoc.onClipChanged = [this] (const model::ClipPtr& c, std::uint64_t v) { publisher->submit (c, v); };

    clipDoc.replace (firstOpenClip());
    resetHistory();
    sampleManager->loadStarter (sounds::kDefaultStarter, 1, doc::LoadOrigin::startup);

    // A new instance opens with the tool tabs folded, so the roll has the room (plan 7.7 decision 6: the prototype folds
    // them on a first open in a window under 900 px high, and a new window here is 680). A project keeps the fold it
    // was saved with (UIST).
    uiState.folded = 1;

    // Frees the versions the audio thread no longer reads, 10 times a second (plan 1.3).
    startTimerHz (kCollectHz);
}

StrandLockProcessor::~StrandLockProcessor()
{
    stopTimer();
    clipDoc.onClipChanged = nullptr;
    sampleManager.reset();   // stops the worker (it may be making a sound)
    publisher.reset();       // stops the compiler
    cancelPendingUpdate();
    for (auto* p : paramList)
        p->removeListener (this);
}

void StrandLockProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // The audio thread is idle here (a host calls prepareToPlay between blocks, never during one).
    engine->prepare (sampleRate);
    engine->setInst (params::readInst (paramRefs, nonParam));
    engine->setBypass (params::readBypass (paramRefs));
    engine->snapRamps();   // plan 2.3: the first block plays the parameters as they are, with no ramp from the defaults
    hostSync.prepare (engine->sampleRate());
    displayInterval = engine::displayEvery (engine->sampleRate());
    samplesSinceDisplay = 0;
    clipSilenced = false;
    firstBlock = true;
    displayFramesWritten = 0;
    snapRampsPending.store (false);
    diag.prepare (sampleRate, samplesPerBlock);

    // The sound and the clip asked for before (a new instance's default, a state load still on its way) are
    // there for the first block. The worker never needs this thread, so the wait cannot deadlock. A sound the
    // user picked is not waited for: only this thread can publish it, after this returns (fix pass RT2-5).
    sampleManager->waitForSelfPublished (kLoadWaitMs);
    publisher->waitForVersion (clipDoc.version(), kLoadWaitMs);

    // From here the audio thread may run: the first acquire () hands the engine the current versions again
    // (Engine::prepare dropped its pointers).
    snapshots.setPrepared (true);
    samples.setPrepared (true);
}

void StrandLockProcessor::releaseResources()
{
    // The engine first lets go of every version (its voices stop), so the exchanges may free what they like.
    engine->forgetVersions();
    snapshots.setPrepared (false);
    samples.setPrepared (false);
}

void StrandLockProcessor::reset()
{
    // The host clears the plugin's tails (VST3 setProcessing (false)); never during a block.
    engine->reset();
    hostSync.reset();
}

bool StrandLockProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // An instrument: no audio input at all, and one stereo output.
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorParameter* StrandLockProcessor::getBypassParameter() const
{
    return paramList[(std::size_t) params::index (params::P::bypass)];
}

//==================================================================================================
// The audio path (PLUGIN-PLAN 1.3, 1.10): nothing here allocates, locks, waits, logs or touches a file.

void StrandLockProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    // What the host says about this block, and its MIDI, for the Diagnostics page.
    const auto hostBlock = diag::readHostBlock (getPlayHead(), n, isNonRealtime());
    diag.audioBlock (hostBlock);
    for (const auto meta : midi)
        diag.audioMidi (meta.data, meta.numBytes, juce::jlimit (0, n, meta.samplePosition));

    // The newest versions (one atomic exchange each), the parameters and the editor's commands.
    auto& eng = *engine;
    const bool newSnapshot = snapshots.acquire();

    // Decision 3: while the sound a state load asked for is not there yet, clip notes are silent. Read after the
    // snapshot: a load names its sound before it hands its clip to the compiler, so a block that takes the new
    // clip also sees the wait (fix pass RT2-2).
    const bool waitingForSound = awaitedSound.load (std::memory_order_acquire) > sampleManager->finishedRequest();
    if (waitingForSound)
        diag.audioWaitingForSound();
    if (newSnapshot || waitingForSound != clipSilenced)
    {
        const bool soundArrived = clipSilenced && ! waitingForSound;
        clipSilenced = waitingForSound;
        if (clipSilenced)
            eng.setSnapshot (nullptr, 0);
        else
            eng.setSnapshot (snapshots.current(), snapshots.currentEpoch());
        // the clip notes under way come in where they are, not at their next start (fix pass HMS-3)
        if (soundArrived)
            eng.rechase();
    }
    if (samples.acquire())
        eng.setSample (samples.current(), samples.currentEpoch());

    eng.setInst (params::readInst (paramRefs, nonParam));
    eng.setBypass (params::readBypass (paramRefs));
    if (snapRampsPending.exchange (false, std::memory_order_acq_rel) || firstBlock)
        eng.snapRamps();   // plan 2.3: the first block and a state load play their values at once
    firstBlock = false;

    doc::UiCommand command;
    while (commands->pop (command))
        applyCommand (command);

    // Preview (U5, M6): Kalan's switch, read every block (a click may come at any time).
    const std::uint32_t previewWord = previewSwitch.read();
    hostSync.setPreview (host::PreviewSwitch::isOn (previewWord));

    // The transport (split where a host cycle wraps inside the block, or Preview's own clock while Cubase is
    // stopped) and the MIDI, each at its own sample.
    if (buffer.getNumChannels() >= 2 && n > 0)
    {
        const core::Snapshot* clip = snapshots.current();
        const double clipBeats = clip != nullptr ? (double) std::max (1, clip->bars) * (double) std::max (1, clip->beatsPerBar) : 0.0;
        const auto transport = hostSync.plan (hostBlock, n, clip != nullptr ? clip->offsetBeats : 0.0, clip == nullptr || clip->loop, clipBeats);
        if (transport.previewEnded)
        {
            // Cubase plays (or a clip that does not loop ended): the switch goes off, unless a click changed it after
            // this block read it (the click count in the upper bits then differs, and the click stands). Cubase's Play
            // is counted, so the Sample tab can say why Preview stopped.
            const bool off = previewSwitch.turnOffIf (previewWord);
            if (off && transport.start.playing)
                previewStops.fetch_add (1, std::memory_order_acq_rel);
        }
        const host::MidiSettings settings { nonParam.mod1Cc.load (std::memory_order_relaxed), nonParam.mod2Cc.load (std::memory_order_relaxed) };
        host::MidiRouter::renderBlock (eng, transport, midi, settings, buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    }
    else
    {
        buffer.clear();
    }
    previewPlaying.store (hostSync.isPreviewing(), std::memory_order_release);

    // The versions the engine still reads (a releasing voice's old note, a stolen voice's old sample) stay;
    // the 10 Hz collect frees the rest.
    snapshots.setInUse (eng.minEpochInUse (engine::VersionKind::snapshot));
    samples.setInUse (eng.minEpochInUse (engine::VersionKind::sample));

    // About 30 display frames a second for the waveform strip (the worklet's rule), and the engine's figures for
    // the Diagnostics page.
    samplesSinceDisplay += n;
    if (samplesSinceDisplay >= displayInterval)
    {
        samplesSinceDisplay = 0;
        eng.fillDisplay (displaySlot->beginWrite());
        displaySlot->endWrite();
        ++displayFramesWritten;

        const auto st = eng.stats();
        diag::DiagModel::EngineFigures f;
        f.voices = st.voices;
        f.steals = st.steals;
        f.cuts = st.cuts;
        f.nonFinite = st.nonFinite;
        f.filterResets = st.filterResets;
        diag.audioEngine (f);
    }

    // One more block: the Sample tab's paused-host check counts them (M6, plan 7.8 decision 3).
    blocksProcessed.fetch_add (1, std::memory_order_acq_rel);
}

void StrandLockProcessor::applyCommand (const doc::UiCommand& command) noexcept
{
    auto& eng = *engine;
    switch (command.kind)
    {
        case doc::UiCommandKind::auditionOn:
        {
            engine::NoteOnOpts opts;
            opts.startPos = command.startPos;
            eng.noteOn (static_cast<double> (command.note), command.velocity, opts);
            break;
        }
        case doc::UiCommandKind::auditionOff:
            eng.noteOff (static_cast<double> (command.note));
            break;
        case doc::UiCommandKind::letGoWheel:
            eng.releaseController (command.wheel == 1 ? engine::Ctrl::mod2 : engine::Ctrl::mod1);
            break;
        case doc::UiCommandKind::allNotesOff:
            eng.allNotesOff();
            break;
        case doc::UiCommandKind::previewStart:   // Preview's switch is setPreview's atomic word, read every block (M6)
        case doc::UiCommandKind::previewStop:
        case doc::UiCommandKind::midiLearnArm:   // MIDI Learn is M8
            break;
    }
}

void StrandLockProcessor::setPreview (bool on)
{
    // A new click count above the on bit: the audio thread turns the switch off only while it still holds the word it
    // read at the start of its block, so a click made after that block began always stands (host::PreviewSwitch).
    previewSwitch.set (on);
}

void StrandLockProcessor::timerCallback()
{
    collectVersions();

    // M7: the suggestions follow the sound that plays; a root suggestion for a new Region is asked for once the Region
    // has stayed put for 250 ms (sl-ui.js suggestRoot); E3 ends once the engine changes by other means.
    syncSound();
    if (rootDueMs >= 0.0 && juce::Time::getMillisecondCounterHiRes() >= rootDueMs)
    {
        rootDueMs = -1.0;
        const auto r = currentRegion();
        sampleManager->suggestRoot (r.first, r.second);
    }
    updateEngineSuggestion();
}

juce::AudioProcessorEditor* StrandLockProcessor::createEditor()
{
    return new StrandLockEditor (*this);
}

//==================================================================================================
// Diagnostics: which parameter changes are the host's.

void StrandLockProcessor::parameterValueChanged (int parameterIndex, float newValue)
{
    // A change while StrandLock's own control holds a gesture is the user's; one during a state load is the
    // load's. Anything else came from the host (automation, Quick Controls, its generic editor).
    if (parameterIndex < 0 || parameterIndex >= params::kNumParams || loadingState.load() > 0
        || inGesture[(std::size_t) parameterIndex].load())
        return;
    diag.hostParameterChanged (parameterIndex, paramList[(std::size_t) parameterIndex]->convertFrom0to1 (newValue));
}

void StrandLockProcessor::parameterGestureChanged (int parameterIndex, bool gestureIsStarting)
{
    if (parameterIndex >= 0 && parameterIndex < params::kNumParams)
        inGesture[(std::size_t) parameterIndex].store (gestureIsStarting);
}

//==================================================================================================
// The document.

doc::ParamValues StrandLockProcessor::currentParamValues() const
{
    doc::ParamValues v {};
    for (int i = 0; i < params::kNumParams; ++i)
        v[(std::size_t) i] = params::plainValueOf (*paramList[(std::size_t) i]);
    return v;
}

void StrandLockProcessor::resetHistory()
{
    doc::DocState start;
    start.clip = clipDoc.current();
    start.np = clipDoc.nonParam();
    start.pv = currentParamValues();
    const std::lock_guard<std::mutex> hold (historyLock);
    history.reset (std::move (start));
}

doc::EditOutcome StrandLockProcessor::editClip (const std::string& label, model::EditResult proposed)
{
    auto out = clipDoc.apply (std::move (proposed));
    if (out.accepted)
    {
        const std::lock_guard<std::mutex> hold (historyLock);
        history.push (label, clipDoc.current(), clipDoc.nonParam(), {});
    }
    return out;
}

bool StrandLockProcessor::pushUndoStep (const std::string& label, const std::vector<params::P>& changed)
{
    std::vector<std::pair<int, float>> values;
    for (const auto p : changed)
    {
        const int i = params::index (p);
        if (i >= 0 && i < params::kNumParams && i != params::index (params::P::bypass))
            values.emplace_back (i, params::plainValueOf (*paramList[(std::size_t) i]));
    }
    const std::lock_guard<std::mutex> hold (historyLock);
    return history.push (label, clipDoc.current(), clipDoc.nonParam(), values);
}

doc::EditOutcome StrandLockProcessor::editClip (const std::string& label, model::Clip proposed)
{
    model::EditResult r;
    r.clip = std::move (proposed);
    return editClip (label, std::move (r));
}

void StrandLockProcessor::writeParams (const std::vector<std::pair<int, float>>& values)
{
    for (const auto& [i, v] : values)
    {
        if (i < 0 || i >= params::kNumParams || i == params::index (params::P::bypass))
            continue;
        auto& p = *paramList[(std::size_t) i];
        p.beginChangeGesture();
        params::setPlainValue (p, i, v);
        p.endChangeGesture();
    }
}

void StrandLockProcessor::applyStep (const doc::EditHistory::Step& step)
{
    const auto soundBefore = clipDoc.soundRef();
    const bool soundChanges = ! state::sameSound (soundBefore, step.state.np.sound);
    const auto regionBefore = currentRegion();
    auto values = step.params;
    if (soundChanges)
    {
        // Plan 2.4 (sl-ui.js applyState): the root belongs to the sound. The sound leaving keeps the root and cents it
        // has now, and the sound coming back brings its own, whatever the steps' parameter values say.
        rememberRoot (soundBefore, currentRoot(), currentCents());
        if (const auto own = rootOf (step.state.np.sound))
        {
            const int rootIndex = params::index (params::P::rootNote), centsIndex = params::index (params::P::rootCents);
            values.erase (std::remove_if (values.begin(), values.end(),
                                          [rootIndex, centsIndex] (const std::pair<int, float>& v) { return v.first == rootIndex || v.first == centsIndex; }),
                          values.end());
            values.emplace_back (rootIndex, (float) own->first);
            values.emplace_back (centsIndex, own->second);
        }
    }
    writeParams (values);
    auto np = step.state.np;
    keepCacheOf (np.sound);   // the analysis cache is the file's (M7): the newest one known stays
    clipDoc.setNonParam (np);
    clipDoc.replace (step.state.clip);
    if (soundChanges)
        sampleManager->loadRef (np.sound, doc::LoadOrigin::undo);

    // A root suggestion belongs to the Region it was heard in (sl-ui.js applyState: if (regionMoved) suggestRoot ()).
    const auto regionAfter = currentRegion();
    if (! sameRegion (regionBefore.first, regionBefore.second, regionAfter.first, regionAfter.second))
        regionChanged();
}

bool StrandLockProcessor::undo()
{
    std::optional<doc::EditHistory::Step> step;
    {
        const std::lock_guard<std::mutex> hold (historyLock);
        step = history.undo();
    }
    if (step)
        applyStep (*step);
    return step.has_value();
}

bool StrandLockProcessor::redo()
{
    std::optional<doc::EditHistory::Step> step;
    {
        const std::lock_guard<std::mutex> hold (historyLock);
        step = history.redo();
    }
    if (step)
        applyStep (*step);
    return step.has_value();
}

bool StrandLockProcessor::canUndo() const
{
    const std::lock_guard<std::mutex> hold (historyLock);
    return history.canUndo();
}

bool StrandLockProcessor::canRedo() const
{
    const std::lock_guard<std::mutex> hold (historyLock);
    return history.canRedo();
}

std::optional<std::string> StrandLockProcessor::undoLabel() const
{
    const std::lock_guard<std::mutex> hold (historyLock);
    return history.undoLabel();
}

std::optional<std::string> StrandLockProcessor::redoLabel() const
{
    const std::lock_guard<std::mutex> hold (historyLock);
    return history.redoLabel();
}

bool StrandLockProcessor::waitForSnapshot (int timeoutMs)
{
    return publisher->waitForVersion (clipDoc.version(), timeoutMs);
}

void StrandLockProcessor::collectVersions()
{
    snapshots.collect();
    samples.collect();
}

//==================================================================================================
// Sounds.

std::uint64_t StrandLockProcessor::loadStarterSound (sounds::StarterId id, std::int32_t seed)
{
    return sampleManager->loadStarter (id, seed, doc::LoadOrigin::user);
}

std::uint64_t StrandLockProcessor::loadSoundFile (const juce::File& file)
{
    return sampleManager->loadFile (file, doc::LoadOrigin::user);
}

void StrandLockProcessor::handleAsyncUpdate()
{
    deliverSoundLoads();
    deliverAnalysis();
}

int StrandLockProcessor::deliverSoundLoads()
{
    int published = 0;
    for (auto& p : sampleManager->takePending())
    {
        // A load that a newer request overtook (a project or an Undo that came in meanwhile, another pick) changes
        // nothing: not the parameters, not the document. commit () would refuse it, but only after the patch
        // (fix pass RT2-1).
        if (p.view == nullptr || ! sampleManager->isNewest (p.request))
            continue;
        // The sound leaving keeps its own root and cents (plan 2.4's SoundMeta), so Undo brings it back with them.
        rememberRoot (clipDoc.soundRef(), currentRoot(), currentCents());

        // The sound's own root and cents and its Region, as host-visible edits (plan 2.4), then the sound.
        const auto changed = params::applyPatch (apvts, p.patch);
        auto np = clipDoc.nonParam();
        np.inst.regionStartExact = p.regionStartExact;
        np.inst.regionEndExact = p.regionEndExact;
        np.sound = p.view->ref;
        std::string label = (p.view->builtIn ? "Sound: " : "Load ") + p.view->name;
        if (p.view->cut && p.view->ref.file.loadedStartFrame > 0 && p.view->ref.file.fileSampleRate > 0.0)
        {
            const auto from = (long long) std::floor ((double) p.view->ref.file.loadedStartFrame / p.view->ref.file.fileSampleRate);
            label += " (from " + std::to_string (from / 60) + ":" + (from % 60 < 10 ? "0" : "") + std::to_string (from % 60) + ")";
        }
        clipDoc.setNonParam (np);

        // What listening found (M7, decision 2): suggestions only, nothing applies itself.
        Suggestions next;
        next.soundRequest = p.request;
        next.analysis = p.view->analysis;
        if (! p.view->builtIn)
        {
            const auto heard = p.rootHeard;
            if (heard.has_value() && std::abs ((double) (heard->root * 100 + heard->cents) - (double) (60 * 100)) >= 20.0)
                next.root = heard;   // the load set the root to C3 (60, 0); a root within 20 cents of it is no news
            const auto& k = p.view->analysis.key;
            if (k.has_value() && (k->key != (int) np.inst.key || np.inst.scale != sl::analysis::scaleName (k->scale)))
                next.key = k;
            // E3 (sl-ui-sample.js onParts): suggestEngine ({ pitch, seconds, parts }) against the engine playing now.
            ui::rules::SoundInfo info;
            info.pitch = p.view->analysis.pitch.has_value();
            info.seconds = p.view->seconds();
            for (const auto& part : p.view->analysis.parts)
            {
                ui::rules::Part q;
                q.kind = sl::analysis::partKindName (part.kind);
                q.label = part.label;
                q.start = part.start;
                q.end = part.end;
                q.tempoFound = part.tempoFound;
                q.bpm = part.bpm;
                info.parts.emplace_back (std::move (q));
            }
            const auto s = ui::rules::suggestEngine (&info);
            EngineSuggestion e;
            e.engine = s.engine;
            e.text = s.text;
            e.why = s.why;
            e.shownEngine = readInst().engine;
            e.match = e.engine == e.shownEngine;
            next.engine = e;
        }

        if (sampleManager->commit (std::move (p)))
        {
            ++published;
            suggestions = std::move (next);
            seenSound = sampleManager->current();
            rememberCache (np.sound);
            const std::lock_guard<std::mutex> hold (historyLock);
            // The step keeps the clip of the last finished edit, not the document's: a load can finish while a roll
            // drag is held, and the document then shows the drag's unfinished clip, which no undo step may keep. The
            // drag's own step comes on its release (fix pass R4T-2).
            const auto last = history.current();
            history.push (label, last.has_value() ? last->clip : clipDoc.current(), np, changed);
        }
    }
    syncSound();   // a project's or Undo's sound that the worker published itself
    return published;
}

bool StrandLockProcessor::waitForSounds (int timeoutMs)
{
    const bool done = sampleManager->waitIdle (timeoutMs);
    deliverSoundLoads();
    return done;
}

std::vector<doc::LoadReport> StrandLockProcessor::takeSoundReports()
{
    auto out = sampleManager->takeReports();
    for (auto& r : ownReports)
        out.push_back (std::move (r));
    ownReports.clear();
    return out;
}

//==================================================================================================
// Long songs and what listening suggests (M7, plan 7.9 decisions 1 and 2). Message thread.

std::uint64_t StrandLockProcessor::loadSoundFilePart (const juce::File& file, std::uint64_t startFrame)
{
    return sampleManager->loadFile (file, doc::LoadOrigin::user, startFrame);
}

int StrandLockProcessor::currentRoot() const
{
    return juce::roundToInt (params::plainValueOf (getParam (params::P::rootNote)));
}

float StrandLockProcessor::currentCents() const
{
    return params::plainValueOf (getParam (params::P::rootCents));
}

std::pair<double, double> StrandLockProcessor::currentRegion() const
{
    const auto s = clipDoc.instState();
    const float ps = params::plainValueOf (getParam (params::P::regionStart));
    const float pe = params::plainValueOf (getParam (params::P::regionEnd));
    const auto edge = [] (float param, double exact)
    {
        return std::isfinite (exact) && juce::exactlyEqual ((float) exact, param) ? exact : (double) param;
    };
    auto r = std::make_pair (edge (ps, s.regionStartExact), edge (pe, s.regionEndExact));
    if (r.second < r.first)
        std::swap (r.first, r.second);
    return r;
}

void StrandLockProcessor::say (const std::string& text, int ms)
{
    doc::LoadReport r;
    r.origin = doc::LoadOrigin::user;
    r.ok = true;
    r.message = text;
    r.toastMs = ms;
    ownReports.push_back (std::move (r));
    while (ownReports.size() > doc::SampleManager::kMaxReports)
        ownReports.erase (ownReports.begin());
}

void StrandLockProcessor::rememberRoot (const state::SampleRef& sound, int root, float cents)
{
    for (auto it = soundMeta.begin(); it != soundMeta.end(); ++it)
        if (state::sameSound (it->first, sound))
        {
            soundMeta.erase (it);
            break;
        }
    soundMeta.emplace_back (sound, std::make_pair (root, cents));
    while (soundMeta.size() > kSoundsRemembered)
        soundMeta.erase (soundMeta.begin());
}

std::optional<std::pair<int, float>> StrandLockProcessor::rootOf (const state::SampleRef& sound) const
{
    for (const auto& m : soundMeta)
        if (state::sameSound (m.first, sound))
            return m.second;
    return std::nullopt;
}

void StrandLockProcessor::rememberCache (const state::SampleRef& sound)
{
    if (sound.kind != state::kSampleFile || sound.file.cache.analysed != 1)
        return;
    for (auto it = knownCaches.begin(); it != knownCaches.end(); ++it)
        if (state::sameSound (*it, sound))
        {
            knownCaches.erase (it);
            break;
        }
    knownCaches.push_back (sound);
    while (knownCaches.size() > kSoundsRemembered)
        knownCaches.erase (knownCaches.begin());
}

void StrandLockProcessor::keepCacheOf (state::SampleRef& sound) const
{
    if (sound.kind != state::kSampleFile)
        return;
    for (const auto& k : knownCaches)
        if (state::sameSound (k, sound))
            sound.file.cache = k.file.cache;
}

void StrandLockProcessor::setRoot (int root, int cents)
{
    core::InstPatch p;
    p.root = (double) root;
    p.cents = (double) cents;
    params::applyPatch (apvts, p);
    rememberRoot (clipDoc.soundRef(), currentRoot(), currentCents());
}

void StrandLockProcessor::syncSound()
{
    const auto now = sampleManager->current();
    if (now == nullptr || now == seenSound)
        return;
    seenSound = now;
    if (suggestions.soundRequest != now->request)
    {
        // A sound the processor did not hand over itself (a project, Undo, a new instance's default): what was found
        // for it, and no suggestion (they belong to a load the user made).
        suggestions = {};
        suggestions.soundRequest = now->request;
        suggestions.analysis = now->analysis;
    }
    // A project's file analysed after its load (its cache was empty or could not be read): the document's SMPL record
    // takes the cache, so the next save keeps it and a reload does not analyse again (decision 1).
    if (now->ref.kind == state::kSampleFile && ! now->missing && now->ref.file.cache.analysed == 1)
    {
        rememberCache (now->ref);
        auto np = clipDoc.nonParam();
        if (state::sameSound (np.sound, now->ref))
        {
            auto withCache = np.sound;
            withCache.file.cache = now->ref.file.cache;
            if (state::encodeSmpl (withCache) != state::encodeSmpl (np.sound))
            {
                np.sound = std::move (withCache);
                clipDoc.setNonParam (np);
            }
        }
    }
}

bool StrandLockProcessor::showsRootSuggestion() const
{
    const auto& g = suggestions.root;
    if (! g.has_value())
        return false;
    const auto r = currentRegion();
    return sameRegion (g->regionStart, g->regionEnd, r.first, r.second);
}

bool StrandLockProcessor::showsKeySuggestion() const
{
    const auto& k = suggestions.key;
    if (! k.has_value())
        return false;
    const auto s = clipDoc.instState();
    return k->key != (int) s.key || s.scale != analysis::scaleName (k->scale);
}

bool StrandLockProcessor::showsEngineSuggestion() const
{
    // renderChips: sugOn = !done && (engine !== I.engine || match)
    const auto& e = suggestions.engine;
    return e.has_value() && ! e->done && (e->engine != readInst().engine || e->match);
}

void StrandLockProcessor::updateEngineSuggestion()
{
    // syncAll: the suggestion goes once the engine is changed by any other means
    auto& e = suggestions.engine;
    if (e.has_value() && ! e->done && e->shownEngine != readInst().engine)
        e->done = true;
}

bool StrandLockProcessor::detectPitchAndKey()
{
    const auto sound = sampleManager->current();
    if (sound == nullptr)
        return false;
    if (sound->builtIn)
    {
        say ("Built-in sounds are tuned to C3 already", 2600);
        return false;
    }
    const auto r = currentRegion();
    if (sampleManager->detect (r.first, r.second) == 0)
        return false;
    suggestions.listening = true;
    say ("Listening...", 2600);
    return true;
}

void StrandLockProcessor::applyDetect (const doc::JobResult& res)
{
    // sl-ui.js runDetect 'button': the root at once (no undo step), the key as one undo step, and a toast.
    suggestions.listening = false;
    const auto now = sampleManager->current();
    if (now == nullptr || now->request != res.soundRequest)
        return;
    std::vector<std::string> said;
    const std::string from = res.whole ? "" : " (from the Region)";
    suggestions.root.reset();
    suggestions.key.reset();
    const int rootBefore = currentRoot();
    if (res.pitch.has_value())
    {
        const auto rc = doc::pitchToRoot (*res.pitch);
        setRoot (rc.root, rc.cents);
        said.push_back ("Root set to " + rootText (rc.root, rc.cents) + from + ".");
    }
    else if (res.longFile && res.whole)
        said.push_back ("This song has no single pitch, so pick a part and StrandLock suggests its root.");
    else if (res.noisy)
        said.push_back ("No clear pitch or key (it sounds like noise), so the root stays " + core::noteName ((double) rootBefore) + ".");
    else
        said.push_back ("No clear pitch (a texture or a chord), so the root stays " + core::noteName ((double) rootBefore) + ".");

    // What was heard is kept with the sound (S.pitch, S.key), in SMPL's cache too (decision 1).
    suggestions.analysis.analysed = true;
    suggestions.analysis.pitch = res.pitch;
    suggestions.analysis.key = res.key;
    auto np = clipDoc.nonParam();
    if (np.sound.kind == state::kSampleFile && state::sameSound (np.sound, now->ref))
    {
        np.sound.file.cache = doc::toCache (suggestions.analysis);
        clipDoc.setNonParam (np);
        rememberCache (np.sound);
    }

    if (res.key.has_value())
    {
        const auto name = doc::keyName (res.key->key, res.key->scale);
        auto s = clipDoc.instState();
        if (res.key->key != (int) s.key || s.scale != analysis::scaleName (res.key->scale))
        {
            s.key = (std::uint8_t) res.key->key;
            s.scale = analysis::scaleName (res.key->scale);
            clipDoc.setInstState (s);
            pushUndoStep ("Key from the sound: " + name, {});
            said.push_back ("Key set to " + name + ".");
        }
        else
        {
            said.push_back ("Key: " + name + ".");
        }
    }
    std::string text;
    for (const auto& s : said)
        text += (text.empty() ? "" : " ") + s;
    say (text, 5000);
}

bool StrandLockProcessor::acceptRootSuggestion()
{
    // sl-ui.js useRootSug: a tip heard in another Region is dropped; else the root and cents, no undo step.
    if (! showsRootSuggestion())
    {
        suggestions.root.reset();
        return false;
    }
    const auto g = *suggestions.root;
    suggestions.root.reset();
    setRoot (g.root, g.cents);
    say ("Root " + rootText (g.root, g.cents) + ": the notes now play this part in tune", 2600);
    return true;
}

bool StrandLockProcessor::acceptKeySuggestion()
{
    // sl-ui.js useKeySug: patchInst ({ key, scale }, 'Key from the sound: ' + keyName): one undo step.
    if (! suggestions.key.has_value())
        return false;
    const auto k = *suggestions.key;
    suggestions.key.reset();
    auto s = clipDoc.instState();
    s.key = (std::uint8_t) k.key;
    s.scale = analysis::scaleName (k.scale);
    clipDoc.setInstState (s);
    return pushUndoStep ("Key from the sound: " + doc::keyName (k.key, k.scale), {});
}

bool StrandLockProcessor::acceptEngineSuggestion()
{
    // sl-ui-sample.js acceptSuggestion: the engine as one undo step (when it is not on already), and why.
    if (! suggestions.engine.has_value())
        return false;
    auto& e = *suggestions.engine;
    e.done = true;
    bool pushed = false;
    if (readInst().engine != e.engine)
    {
        core::InstPatch p;
        p.engine = e.engine;
        params::applyPatch (apvts, p);
        pushed = pushUndoStep ("Engine: " + std::string (engineName (e.engine)) + " (suggested)", { params::P::engine });
    }
    say (std::string (engineName (e.engine)) + ": " + e.why, 4000);
    return pushed;
}

void StrandLockProcessor::dismissEngineSuggestion (bool explain)
{
    if (! suggestions.engine.has_value())
        return;
    suggestions.engine->done = true;
    if (explain)
        say (std::string (engineName (suggestions.engine->engine)) + ": " + suggestions.engine->why, 4000);
}

bool StrandLockProcessor::setRegionStep (double start, double end, const std::string& label)
{
    const auto r = currentRegion();
    if (sameRegion (r.first, r.second, start, end))
        return false;
    // The parameters as host-visible edits and the exact f64 edges StrandLock plays (plan 2.2), then one undo step.
    core::InstPatch p;
    p.region.start = start;
    p.region.end = end;
    params::applyPatch (apvts, p);
    auto s = clipDoc.instState();
    s.regionStartExact = start;
    s.regionEndExact = end;
    clipDoc.setInstState (s);
    const bool pushed = pushUndoStep (label, { params::P::regionStart, params::P::regionEnd });
    regionChanged();
    return pushed;
}

bool StrandLockProcessor::usePart (const analysis::Part& part)
{
    return setRegionStep (part.start, part.end, "Region: " + part.label);
}

bool StrandLockProcessor::useWholeFile()
{
    return setRegionStep (0.0, 1.0, "Region: whole file");
}

void StrandLockProcessor::regionChanged()
{
    // sl-ui.js suggestRoot: the old tip goes at once (it was heard in another Region); the new one follows a moment later.
    suggestions.root.reset();
    rootDueMs = juce::Time::getMillisecondCounterHiRes() + 250.0;
}

int StrandLockProcessor::deliverAnalysis()
{
    syncSound();
    int delivered = 0;
    for (const auto& res : sampleManager->takeResults())
    {
        if (res.soundRequest != suggestions.soundRequest)
            continue;   // heard in a sound that is no longer the one playing
        if (res.kind == doc::JobKind::detect)
        {
            applyDetect (res);
            ++delivered;
            continue;
        }
        if (res.kind == doc::JobKind::parts)
        {
            suggestions.analysis.analysed = true;   // a starter's parts, made right after it was handed on
            suggestions.analysis.parts = res.parts;
            ++delivered;
            continue;
        }
        // A root suggestion: only for the Region it was asked for (a newer Region asks again), and only when it says
        // something new (sl-ui.js suggestRoot: within 20 cents of the root is the same).
        const auto r = currentRegion();
        if (! sameRegion (res.regionStart, res.regionEnd, r.first, r.second))
            continue;
        suggestions.root.reset();
        if (res.pitch.has_value())
        {
            auto rc = doc::pitchToRoot (*res.pitch);
            rc.regionStart = res.regionStart;
            rc.regionEnd = res.regionEnd;
            const bool same = std::abs ((double) (rc.root * 100 + rc.cents) - ((double) currentRoot() * 100.0 + (double) currentCents())) < 20.0;
            if (! same)
                suggestions.root = rc;
        }
        ++delivered;
    }
    return delivered;
}

bool StrandLockProcessor::waitForAnalysis (int timeoutMs)
{
    if (rootDueMs >= 0.0)
    {
        rootDueMs = -1.0;
        const auto r = currentRegion();
        sampleManager->suggestRoot (r.first, r.second);
    }
    const bool done = sampleManager->waitJobs (timeoutMs);
    deliverAnalysis();
    return done;
}

std::uint64_t StrandLockProcessor::requestOverview()
{
    const auto have = sampleManager->overview();
    const auto now = sampleManager->current();
    if (have != nullptr && now != nullptr && have->path == now->ref.file.path && have->ok)
        return 0;
    return sampleManager->requestOverview();
}

std::shared_ptr<const doc::FileOverview> StrandLockProcessor::getOverview() const
{
    return sampleManager->overview();
}

std::size_t StrandLockProcessor::historySize() const
{
    const std::lock_guard<std::mutex> hold (historyLock);
    return history.size();
}

//==================================================================================================
// The state outside the parameters and the document.

state::UiState StrandLockProcessor::getUiState() const
{
    const std::lock_guard<std::mutex> hold (stateLock);
    return uiState;
}

void StrandLockProcessor::setUiState (const state::UiState& s)
{
    const std::lock_guard<std::mutex> hold (stateLock);
    uiState = s;
}

state::Theme StrandLockProcessor::getTheme() const
{
    if (const auto mine = userSettings->theme())
        return *mine;
    const std::lock_guard<std::mutex> hold (stateLock);
    return uiState.theme == 1 ? state::Theme::light : state::Theme::dark;
}

void StrandLockProcessor::setTheme (state::Theme theme)
{
    userSettings->setTheme (theme);
    const std::lock_guard<std::mutex> hold (stateLock);
    uiState.theme = theme == state::Theme::light ? 1 : 0;
}

juce::String StrandLockProcessor::getStateBanner() const
{
    const std::lock_guard<std::mutex> hold (stateLock);
    return kept.keepsUnreadable (currentPayloads()) ? juce::String (kBanner) : juce::String();
}

int StrandLockProcessor::getUnknownChunkCount() const
{
    const std::lock_guard<std::mutex> hold (stateLock);
    return kept.unknownChunkCount();
}

//==================================================================================================
// The binary state (PLUGIN-PLAN 3).

state::PayloadPtr StrandLockProcessor::clipPayload() const
{
    // Cubase saves (and auto-saves) often, and the banner check reads the payloads twice a second: the clip is
    // encoded once per version (up to 32 MiB), never on every call.
    const auto c = clipDoc.current();
    if (c != encodedClip || encodedClipPayload == nullptr)
    {
        encodedClipPayload = state::payload (model::encodeClip (*c));
        encodedClip = c;
    }
    return encodedClipPayload;
}

state::Payloads StrandLockProcessor::currentPayloads() const
{
    std::array<float, params::kNumParams> values {};
    for (int i = 0; i < params::kNumParams; ++i)
        values[(std::size_t) i] = params::plainValueOf (*paramList[(std::size_t) i]);

    const auto np = clipDoc.nonParam();
    state::Payloads p;
    p[state::kTagParm] = state::payload (state::encodeParm (values, parmExtras, parmTrailing));
    p[state::kTagInst] = state::payload (state::encodeInst (np.inst));
    p[state::kTagClip] = clipPayload();
    p[state::kTagSmpl] = state::payload (state::encodeSmpl (np.sound));
    p[state::kTagUist] = state::payload (state::encodeUist (uiState));
    return p;
}

void StrandLockProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    try
    {
        const std::lock_guard<std::mutex> hold (stateLock);
        const auto bytes = kept.write (currentPayloads());
        destData.replaceAll (bytes.data(), bytes.size());
        diag.stateSaved (bytes.size());
    }
    catch (...)
    {
        destData.reset();   // never let anything escape into the host
    }
}

void StrandLockProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // An empty state (some hosts send one) holds nothing to keep or load.
    if (data == nullptr || sizeInBytes <= 0)
        return;
    try
    {
        loadState (data, (std::size_t) sizeInBytes);
    }
    catch (...)
    {
        // A load that fails half way leaves what it had set; nothing escapes into the host.
    }
}

void StrandLockProcessor::loadState (const void* data, std::size_t size)
{
    const LoadScope loading (loadingState);
    const auto parsed = state::parseContainer (data, size);
    diag.stateLoaded (size, parsed.status == state::HeaderStatus::ok);

    if (parsed.status != state::HeaderStatus::ok)
    {
        // Nothing can be read: keep everything as it is and write the blob back until something is edited.
        const std::lock_guard<std::mutex> hold (stateLock);
        kept = state::KeptLayout::unreadable (data, size, currentPayloads());
        return;
    }

    // The first chunk of each tag this version reads is the one that counts (KeptLayout keeps the rest).
    std::vector<bool> decoded (parsed.chunks.size(), false);
    std::optional<state::ParmChunk> parm;
    std::optional<state::InstState> inst;
    std::optional<model::Clip> clip;
    std::optional<state::SampleRef> smpl;
    std::optional<state::UiState> ui;
    std::set<std::uint32_t> seen;
    for (std::size_t i = 0; i < parsed.chunks.size(); ++i)
    {
        const auto& c = parsed.chunks[i];
        const bool readTag = std::find (std::begin (state::kWrittenTags), std::end (state::kWrittenTags), c.tag) != std::end (state::kWrittenTags);
        if (! readTag || ! seen.insert (c.tag).second)
            continue;
        if (c.truncated || ! c.crcOk || ! c.withinLimits)
            continue;

        if (c.tag == state::kTagParm)
        {
            state::ParmChunk v;
            if (state::decodeParm (c.payload, c.payloadLength, v))
            {
                parm = std::move (v);
                decoded[i] = true;
            }
        }
        else if (c.tag == state::kTagInst)
        {
            state::InstState v;
            if (state::decodeInst (c.payload, c.payloadLength, v))
            {
                inst = std::move (v);
                decoded[i] = true;
            }
        }
        else if (c.tag == state::kTagClip)
        {
            // model::decodeClip refuses what the limits table refuses, and rebuilds the notes through
            // normalizeClip, so any payload it accepts is a valid clip.
            auto d = model::decodeClip (c.payload, c.payloadLength);
            if (d.clip.has_value())
            {
                clip = std::move (d.clip);
                decoded[i] = true;
            }
        }
        else if (c.tag == state::kTagSmpl)
        {
            state::SampleRef v;
            const bool known = state::decodeSmpl (c.payload, c.payloadLength, v)
                            && (v.kind == state::kSampleFile || sounds::parseStarter (v.starterId).has_value());
            if (known)
            {
                smpl = std::move (v);
                decoded[i] = true;
            }
        }
        else if (c.tag == state::kTagUist)
        {
            state::UiState v;
            if (state::decodeUist (c.payload, c.payloadLength, v))
            {
                ui = std::move (v);
                decoded[i] = true;
            }
        }
    }

    // Parameters: by id; one the state does not mention gets its default (never the current value). Bypass is
    // the host's and is never loaded.
    const int bypass = params::index (params::P::bypass);
    std::array<float, params::kNumParams> values {};
    for (int i = 0; i < params::kNumParams; ++i)
        values[(std::size_t) i] = params::defaultPlain (i);
    std::vector<state::ParmEntry> extras;
    if (parm)
    {
        for (const auto& e : parm->entries)
        {
            const int i = params::findParam (e.id);
            if (i < 0)
                extras.push_back (e);
            else if (i != bypass)
                values[(std::size_t) i] = params::legalPlain (i, e.value);
        }
    }
    for (int i = 0; i < params::kNumParams; ++i)
        if (i != bypass)
            params::setPlainValue (*paramList[(std::size_t) i], i, values[(std::size_t) i]);

    doc::NonParamState np;
    np.inst = inst ? *inst : state::InstState {};
    np.sound = smpl ? *smpl : state::SampleRef {};

    // The sound (decision 3): handed to the worker, unless it is the one that plays already and nothing else
    // is on its way. A file that is missing keeps its record (np.sound) while the default starter plays.
    std::uint64_t request = 0;
    {
        const auto playing = sampleManager->current();
        const bool same = playing != nullptr && ! playing->missing && state::sameSound (playing->ref, np.sound) && ! sampleManager->busy();
        if (! same)
            request = sampleManager->loadRef (np.sound, doc::LoadOrigin::project);
    }
    // Before the clip goes to the compiler: from here the audio thread keeps clip notes silent until the sound is
    // there, so the new clip never plays on the old sound (fix pass RT2-2).
    if (request != 0)
        awaitedSound.store (request, std::memory_order_release);

    clipDoc.setNonParam (np);
    clipDoc.replace (clip ? std::make_shared<const model::Clip> (std::move (*clip)) : defaultClip());

    {
        const std::lock_guard<std::mutex> hold (stateLock);
        uiState = ui ? *ui : state::UiState {};
        parmExtras = std::move (extras);
        parmTrailing = parm ? parm->trailing : state::Bytes {};
        kept = state::KeptLayout::fromContainer (parsed, decoded, currentPayloads());
    }
    resetHistory();

    // Wait (on this thread; the worker and the compiler never need it) so the first block after the load, or an
    // offline export started right after it, plays the loaded sound and clip. If the sound is late the
    // diagnostics count the blocks that play without it.
    if (request != 0)
        sampleManager->waitFor (request, kLoadWaitMs);
    publisher->waitForVersion (clipDoc.version(), kLoadWaitMs);

    // Plan 2.3: the loaded parameters play at once, with no ramp from the values before the load.
    snapRampsPending.store (true, std::memory_order_release);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StrandLockProcessor();
}
