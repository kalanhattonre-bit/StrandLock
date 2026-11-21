// M7 "root" (PLUGIN-PLAN 4 M7, 2.4, 7.9 decisions 1 and 2): the root belongs to the sound.
//   - rootSuggestionsAndDetect: a file loads with root C3 and a root suggestion (nothing retunes by itself); accepting it
//     and "Detect pitch & key" set rootNote and rootCents and push 0 undo steps; a suggested key is 1 undo step; Undo of
//     a sound change brings that sound back with its own root and cents (plan 2.4's SoundMeta), Redo the other's.
//   - analysisCacheReloadsAsSaved: a noise file has no clear pitch and no key; the SMPL cache says so (hasPitch 0,
//     hasKey 0) and a reload reads it as such without analysing again; the same project with its cache emptied is
//     analysed again (so the count can move).

#include "SongRig.h"

#include "PluginEditor.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"
#include "ui/MainView.h"

#include <cmath>

namespace
{
    using namespace sl;
    using namespace sl::test;

    bool writeMono (const juce::File& file, const std::vector<float>& x, double rate)
    {
        return songs::writeWav16 (file, 1, rate, (std::int64_t) x.size(), [&x] (std::int64_t start, int count, float* const* out)
        {
            for (int i = 0; i < count; ++i)
                out[0][i] = x[(std::size_t) (start + i)];
        });
    }

    /** The state with its SMPL analysis cache emptied (a project saved before the analysis, or by M2 .. M6). */
    juce::MemoryBlock withoutCache (const juce::MemoryBlock& saved)
    {
        const auto parsed = state::parseContainer (saved.getData(), saved.getSize());
        state::Bytes chunks;
        for (const auto& c : parsed.chunks)
        {
            state::Bytes payload (c.payload, c.payload + c.payloadLength);
            state::SampleRef r;
            if (c.tag == state::kTagSmpl && state::decodeSmpl (c.payload, c.payloadLength, r))
            {
                r.file.cache = {};
                payload = state::encodeSmpl (r);
            }
            state::appendChunk (chunks, c.tag, c.version, c.flags, payload);
        }
        const auto blob = state::makeContainer (chunks);
        return juce::MemoryBlock (blob.data(), blob.size());
    }
}

SL_TEST ("songs", rootSuggestionsAndDetect)
{
    section ("Root (M7): a root suggestion and Detect set the root with 0 undo steps; a suggested key is 1 step; Undo of a sound change "
             "brings its own root and cents back");

    const auto dir = songs::tempFolder ("root");
    const auto toneFile = dir.getChildFile ("a tone.wav");
    check (writeMono (toneFile, signals::tone (signals::midiHz (45.23), 2.0, 44100.0), 44100.0), "a 2 s tone at A1 +23 cents written at test time");

    StrandLockProcessor p;
    p.waitForSounds();
    p.loadSoundFile (toneFile);
    p.waitForSounds (30000);
    const auto& s = p.getSuggestions();
    check (juce::exactlyEqual (songs::plainOf (p, params::P::rootNote), 60.0f) && juce::exactlyEqual (songs::plainOf (p, params::P::rootCents), 0.0f),
           "the load sets root C3 and 0 cents: nothing retunes by itself (decision 2)");
    check (p.showsRootSuggestion() && s.root.has_value() && s.root->root == 45 && s.root->cents == 23 && s.analysis.pitch.has_value()
               && s.analysis.pitch->midi == 45,
           fmt ("the strip suggests the root it heard: %s", s.root.has_value() ? fmt ("MIDI %d %+d cents", s.root->root, s.root->cents).c_str() : "none"));

    // One click on the suggestion: the root and cents, and no undo step.
    const auto steps0 = p.historySize();
    const auto label0 = p.undoLabel();
    check (p.acceptRootSuggestion() && juce::exactlyEqual (songs::plainOf (p, params::P::rootNote), 45.0f)
               && juce::exactlyEqual (songs::plainOf (p, params::P::rootCents), 23.0f) && p.historySize() == steps0 && p.undoLabel() == label0
               && ! p.showsRootSuggestion(),
           fmt ("accepting it sets root A1 and +23 cents and pushes 0 undo steps (%d states before and after)", (int) steps0));

    // The suggested key: one undo step.
    check (p.showsKeySuggestion() && s.key.has_value() && s.key->key == 9 && s.key->scale == analysis::Scale::major,
           "the strip suggests the key it heard: A major (the prototype's reading of the tone)");
    const bool keyStep = p.acceptKeySuggestion();
    const auto inst = p.getInstState();
    check (keyStep && p.historySize() == steps0 + 1 && p.undoLabel().value_or ("") == "Key from the sound: A major" && inst.key == 9
               && inst.scale == "Major" && ! p.showsKeySuggestion(),
           fmt ("accepting the key is 1 undo step: \"%s\"", p.undoLabel().value_or ("").c_str()));

    // Detect pitch & key, with the root moved away by hand: the root comes back, the key is the same, 0 undo steps.
    songs::setPlain (p, params::P::rootNote, 60.0f);
    songs::setPlain (p, params::P::rootCents, 0.0f);
    const auto steps1 = p.historySize();
    check (p.detectPitchAndKey(), "Detect pitch & key asks the worker");
    p.waitForAnalysis (30000);
    bool said = false;
    for (const auto& r : p.takeSoundReports())
        said = said || r.message.find ("Root set to A1 +23 cents") != std::string::npos;
    check (juce::exactlyEqual (songs::plainOf (p, params::P::rootNote), 45.0f) && juce::exactlyEqual (songs::plainOf (p, params::P::rootCents), 23.0f)
               && p.historySize() == steps1 && said,
           "Detect sets root A1 and +23 cents and pushes 0 undo steps; the toast says what it heard");

    // A built-in sound is tuned to C3 already: Detect does nothing there.
    p.loadStarterSound (sounds::StarterId::saw);
    p.waitForSounds (30000);
    const auto stepsSaw = p.historySize();
    check (juce::exactlyEqual (songs::plainOf (p, params::P::rootNote), 60.0f) && ! p.detectPitchAndKey() && p.historySize() == stepsSaw,
           "the Saw loads at C3 (one undo step), and Detect on a built-in sound does nothing");

    // Undo of the sound change: the tone comes back with its own root and cents (not the C3 the steps recorded).
    p.undo();
    p.waitForSounds (30000);
    const auto back = p.getCurrentSound();
    check (back != nullptr && ! back->builtIn && juce::exactlyEqual (songs::plainOf (p, params::P::rootNote), 45.0f)
               && juce::exactlyEqual (songs::plainOf (p, params::P::rootCents), 23.0f),
           fmt ("Undo brings the tone back with its own root and cents (MIDI %.0f, %+.0f cents)", (double) songs::plainOf (p, params::P::rootNote),
                (double) songs::plainOf (p, params::P::rootCents)));
    p.redo();
    p.waitForSounds (30000);
    check (p.getCurrentSound() != nullptr && p.getCurrentSound()->builtIn && juce::exactlyEqual (songs::plainOf (p, params::P::rootNote), 60.0f)
               && juce::exactlyEqual (songs::plainOf (p, params::P::rootCents), 0.0f),
           "Redo brings the Saw back at C3, 0 cents");
    dir.deleteRecursively();
}

SL_TEST ("songs", analysisCacheReloadsAsSaved)
{
    section ("The SMPL analysis cache (M7, decision 1): \"no clear pitch\" and \"no key\" reload as such, without a second analysis");

    const auto dir = songs::tempFolder ("cache");
    const auto noiseFile = dir.getChildFile ("noise.wav");
    check (writeMono (noiseFile, signals::noise (2.0, 44100.0, 3.0, 0.5), 44100.0), "2 s of white noise written at test time");

    StrandLockProcessor p;
    p.waitForSounds();
    p.loadSoundFile (noiseFile);
    p.waitForSounds (30000);
    const auto ref = p.getSampleRef();
    const auto& c = ref.file.cache;
    check (c.analysed == 1 && c.hasPitch == 0 && c.hasKey == 0 && p.getSuggestions().analysis.detectedText() == "no clear pitch",
           "white noise: no clear pitch and no key (detectKey's E minor is dropped for noise); SMPL's cache says analysed, hasPitch 0, hasKey 0");

    const auto saved = songs::saveOf (p);
    StrandLockProcessor q;
    q.waitForSounds();
    const auto before = q.getSampleManager().analysesRun();
    songs::loadInto (q, saved);
    q.waitForSounds (30000);
    const auto& qa = q.getSuggestions().analysis;
    const auto qc = q.getSampleRef().file.cache;
    check (q.getSampleManager().analysesRun() == before && qa.analysed && ! qa.pitch.has_value() && ! qa.key.has_value()
               && qa.detectedText() == "no clear pitch" && qc.analysed == 1 && qc.hasPitch == 0 && qc.hasKey == 0,
           "a reload reads \"no clear pitch\" and \"no key\" from the cache, as such, and analyses nothing");
    {
        StrandLockEditor editor (q);
        editor.refreshNow();
        const auto header = editor.getView().getStrip().headerText();
        check (header.endsWith ("no clear pitch"), fmt ("the strip's file line says so: \"%s\"", header.toRawUTF8()));
    }

    // A no-op is not a pass: the same project with its cache emptied is analysed again (and finds the same).
    StrandLockProcessor r;
    r.waitForSounds();
    const auto rBefore = r.getSampleManager().analysesRun();
    songs::loadInto (r, withoutCache (saved));
    r.waitForSounds (30000);
    r.deliverAnalysis();
    check (r.getSampleManager().analysesRun() == rBefore + 1 && r.getSuggestions().analysis.detectedText() == "no clear pitch"
               && r.getSampleRef().file.cache.analysed == 1,
           "negative control: with its cache emptied the project's file is analysed again, and the cache is written back into SMPL");
    dir.deleteRecursively();
}
