#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "model/ClipCodec.h"
#include "model/Presets.h"

#include <algorithm>
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

    /** The clip a new instance opens on (BLUEPRINT 6: "Strand Demo clip and the saw-that-opens-up sound load on
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
    sampleManager->onCommitted = [this] (const doc::SoundView& v) { textContext.soundSeconds.store (v.seconds()); };
    clipDoc.onClipChanged = [this] (const model::ClipPtr& c, std::uint64_t v) { publisher->submit (c, v); };

    clipDoc.replace (firstOpenClip());
    resetHistory();
    sampleManager->loadStarter (sounds::kDefaultStarter, 1, doc::LoadOrigin::startup);
}

StrandLockProcessor::~StrandLockProcessor()
{
    clipDoc.onClipChanged = nullptr;
    sampleManager.reset();   // stops the worker (it may be making a sound)
    publisher.reset();       // stops the compiler
    cancelPendingUpdate();
    for (auto* p : paramList)
        p->removeListener (this);
}

void StrandLockProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    probe.prepare (sampleRate);
    diag.prepare (sampleRate, samplesPerBlock);

    // The sound and the clip asked for before (a new instance's default, a state load still on its way) are
    // there for the first block. The worker never needs this thread, so the wait cannot deadlock.
    sampleManager->waitIdle (kLoadWaitMs);
    publisher->waitForVersion (clipDoc.version(), kLoadWaitMs);

    snapshots.setPrepared (true);
    samples.setPrepared (true);
}

void StrandLockProcessor::releaseResources()
{
    probe.setSound (nullptr);
    snapshots.setPrepared (false);
    samples.setPrepared (false);
}

void StrandLockProcessor::reset()
{
    probe.reset();
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
// The audio path stays the M0 probe until M2's integration step (plan 7.4): only the sound now comes from the
// sample exchange, and the snapshot is taken (so old versions can be freed) though nothing plays it yet.

void StrandLockProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    diag.audioBlock (sl::diag::readHostBlock (getPlayHead(), n, isNonRealtime()));
    if (awaitedSound.load (std::memory_order_relaxed) > sampleManager->finishedRequest())
        diag.audioWaitingForSound();

    if (samples.acquire())
        probe.setSound (samples.current());
    snapshots.acquire();
    probe.setInst (params::readInst (paramRefs, nonParam));
    probe.setBypass (params::readBypass (paramRefs));

    float* left = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0) : nullptr;
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    // Every event lands on its own sample: render up to it, then apply it (PLUGIN-PLAN 1.10).
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos)
        {
            if (left != nullptr)
                probe.render (left + pos, right != nullptr ? right + pos : nullptr, at - pos);
            pos = at;
        }
        diag.audioMidi (meta.data, meta.numBytes, at);
        if (meta.data != nullptr && meta.numBytes >= 3)
        {
            const int type = meta.data[0] & 0xf0;
            const int d1 = meta.data[1] & 0x7f, d2 = meta.data[2] & 0x7f;
            switch (type)
            {
                case 0x90: probe.noteOn (d1, d2); break;   // velocity 0 is a note off
                case 0x80: probe.noteOff (d1); break;
                case 0xb0:
                    if (d1 == 64)
                        probe.sustain (d2 >= 64);
                    else if (d1 == 123)
                        probe.allNotesOff();
                    else if (d1 == 120)
                        probe.allSoundOff();
                    break;
                case 0xe0: probe.pitchBend (d1 | (d2 << 7)); break;
                default: break;
            }
        }
    }
    if (pos < n && left != nullptr)
        probe.render (left + pos, right != nullptr ? right + pos : nullptr, n - pos);

    // The probe reads only the current sample, and nothing reads a snapshot yet.
    samples.setInUse (samples.currentEpoch());
    snapshots.setInUse (snapshots.currentEpoch());
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
    writeParams (step.params);
    const auto soundBefore = clipDoc.soundRef();
    clipDoc.setNonParam (step.state.np);
    clipDoc.replace (step.state.clip);
    if (! state::sameSound (soundBefore, step.state.np.sound))
        sampleManager->loadRef (step.state.np.sound, doc::LoadOrigin::undo);
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
}

int StrandLockProcessor::deliverSoundLoads()
{
    int published = 0;
    for (auto& p : sampleManager->takePending())
    {
        if (p.view == nullptr)
            continue;
        // The sound's own root and cents and its Region, as host-visible edits (plan 2.4), then the sound.
        const auto changed = params::applyPatch (apvts, p.patch);
        auto np = clipDoc.nonParam();
        np.inst.regionStartExact = p.regionStartExact;
        np.inst.regionEndExact = p.regionEndExact;
        np.sound = p.view->ref;
        const std::string label = (p.view->builtIn ? "Sound: " : "Load ") + p.view->name;
        clipDoc.setNonParam (np);
        if (sampleManager->commit (std::move (p)))
        {
            ++published;
            const std::lock_guard<std::mutex> hold (historyLock);
            history.push (label, clipDoc.current(), np, changed);
        }
    }
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
    return sampleManager->takeReports();
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
    {
        awaitedSound.store (request, std::memory_order_relaxed);
        sampleManager->waitFor (request, kLoadWaitMs);
    }
    publisher->waitForVersion (clipDoc.version(), kLoadWaitMs);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StrandLockProcessor();
}
