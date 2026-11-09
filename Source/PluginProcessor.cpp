#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "core/Lanes.h"
#include "core/Music.h"

#include <algorithm>
#include <optional>
#include <set>

using namespace sl;

namespace
{
    /** The probe build's sound (PLUGIN-PLAN 4 M0: "the starter saw"); M2 opens on sawopen (D9). */
    constexpr auto kProbeStarter = sounds::StarterId::saw;

    const char* const kBanner = "This project was saved by a newer StrandLock (or could not be read): editing will replace it";

    /** A mod-wheel target's lane index for a saved id: "off" is off, a lane id is that lane, anything else plays
        as normalizeInst's default for that wheel. */
    std::int8_t modTargetIndex (const std::string& id, core::LaneId fallback) noexcept
    {
        if (id == "off")
            return core::kModOff;
        if (const auto lane = core::findLane (id))
            return static_cast<std::int8_t> (*lane);
        return static_cast<std::int8_t> (fallback);
    }

    /** Counts a state load in progress for as long as it lives (parameter changes it makes are not the host's). */
    struct LoadScope
    {
        explicit LoadScope (std::atomic<int>& c) noexcept : count (c) { ++count; }
        ~LoadScope() { --count; }
        LoadScope (const LoadScope&) = delete;
        LoadScope& operator= (const LoadScope&) = delete;
        std::atomic<int>& count;
    };
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
    loadStarter (kProbeStarter, 1);
}

StrandLockProcessor::~StrandLockProcessor()
{
    for (auto* p : paramList)
        p->removeListener (this);
}

void StrandLockProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    probe.prepare (sampleRate);
    diag.prepare (sampleRate, samplesPerBlock);
}

void StrandLockProcessor::releaseResources() {}

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
void StrandLockProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    diag.audioBlock (sl::diag::readHostBlock (getPlayHead(), n, isNonRealtime()));

    probe.setSound (soundSlot.acquire());
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
        handleMidi (meta.data, meta.numBytes, at);
    }
    if (pos < n && left != nullptr)
        probe.render (left + pos, right != nullptr ? right + pos : nullptr, n - pos);
}

void StrandLockProcessor::handleMidi (const juce::uint8* data, int size, int samplePosition) noexcept
{
    diag.audioMidi (data, size, samplePosition);
    if (data == nullptr || size < 3)
        return;

    const int type = data[0] & 0xf0;
    const int d1 = data[1] & 0x7f, d2 = data[2] & 0x7f;
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
// The state outside the parameters.

void StrandLockProcessor::applyInstToAtomics (const state::InstState& s) noexcept
{
    nonParam.seed.store (s.seed);
    nonParam.mod1Target.store (modTargetIndex (s.mod1Target, core::LaneId::vib));
    nonParam.mod2Target.store (modTargetIndex (s.mod2Target, core::LaneId::tone));
    nonParam.mod1Cc.store (static_cast<std::int8_t> (std::min<int> (s.mod1Cc, 127)));
    nonParam.mod2Cc.store (static_cast<std::int8_t> (std::min<int> (s.mod2Cc, 127)));
    nonParam.key.store (static_cast<std::int8_t> (std::min<int> (s.key, 11)));
    const int scale = core::findScale (s.scale);
    nonParam.scale.store (static_cast<std::uint8_t> (scale < 0 ? 0 : scale));
    nonParam.scaleLock.store (s.scaleLock != 0);
    nonParam.regionStartExact.store (s.regionStartExact);
    nonParam.regionEndExact.store (s.regionEndExact);
}

void StrandLockProcessor::loadStarter (sounds::StarterId id, std::int32_t seed)
{
    auto sound = sounds::sharedStarterSound (id, seed);
    textContext.soundSeconds.store (sound->seconds());
    soundSlot.publish (std::move (sound));

    const std::lock_guard<std::mutex> hold (stateLock);
    sampleRef = state::SampleRef {};
    sampleRef.kind = 0;
    sampleRef.starterId = sounds::starterInfo (id).code;
    sampleRef.seed = seed;
}

state::InstState StrandLockProcessor::getInstState() const
{
    const std::lock_guard<std::mutex> hold (stateLock);
    return instState;
}

void StrandLockProcessor::setInstState (const state::InstState& s)
{
    {
        const std::lock_guard<std::mutex> hold (stateLock);
        instState = s;
    }
    applyInstToAtomics (s);
}

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

state::SampleRef StrandLockProcessor::getSampleRef() const
{
    const std::lock_guard<std::mutex> hold (stateLock);
    return sampleRef;
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

state::Payloads StrandLockProcessor::currentPayloads() const
{
    std::array<float, params::kNumParams> values {};
    for (int i = 0; i < params::kNumParams; ++i)
        values[(std::size_t) i] = params::plainValueOf (*paramList[(std::size_t) i]);

    state::Payloads p;
    p[state::kTagParm] = state::encodeParm (values, parmExtras, parmTrailing);
    p[state::kTagInst] = state::encodeInst (instState);
    p[state::kTagSmpl] = state::encodeSmpl (sampleRef);
    p[state::kTagUist] = state::encodeUist (uiState);
    return p;
}

void StrandLockProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    try
    {
        const std::lock_guard<std::mutex> hold (stateLock);
        const auto bytes = kept.write (currentPayloads());
        destData.replaceAll (bytes.data(), bytes.size());
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
    std::optional<state::SampleRef> smpl;
    std::optional<state::UiState> ui;
    std::set<std::uint32_t> seen;
    for (std::size_t i = 0; i < parsed.chunks.size(); ++i)
    {
        const auto& c = parsed.chunks[i];
        const bool readTag = c.tag == state::kTagParm || c.tag == state::kTagInst || c.tag == state::kTagSmpl || c.tag == state::kTagUist;
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
        else if (c.tag == state::kTagSmpl)
        {
            state::SampleRef v;
            if (state::decodeSmpl (c.payload, c.payloadLength, v) && sounds::parseStarter (v.starterId))
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

    const state::InstState newInst = inst ? *inst : state::InstState {};
    const state::UiState newUi = ui ? *ui : state::UiState {};
    state::SampleRef newSample = smpl ? *smpl : state::SampleRef {};
    if (! smpl)
        newSample.starterId = sounds::starterInfo (kProbeStarter).code;

    const auto oldSample = getSampleRef();
    const auto starter = sounds::parseStarter (newSample.starterId).value_or (kProbeStarter);
    if (oldSample.starterId != newSample.starterId || oldSample.seed != newSample.seed)
        loadStarter (starter, newSample.seed);

    applyInstToAtomics (newInst);

    const std::lock_guard<std::mutex> hold (stateLock);
    instState = newInst;
    uiState = newUi;
    sampleRef = newSample;
    parmExtras = std::move (extras);
    parmTrailing = parm ? parm->trailing : state::Bytes {};
    kept = state::KeptLayout::fromContainer (parsed, decoded, currentPayloads());
    soundSlot.collect();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StrandLockProcessor();
}
