# NOTES-M6

Builder notes for M6 (PLUGIN-PLAN section 4 "M6 Sample mode 8, the Sample tab and Preview" and 7.8). Append only: one
bullet per item, with the reason. Each part writes its own section: T1, T2, T3, Fix.

## T1

The Sample tab's rules and readouts, the full Sample tab and the Macros tab (plan 7.8 T1, decisions 1 and 2). Branch
`m6-sample`, from main 3279614.

### Progress

- Done: `ui/SampleTabRules` (the pure part of sl-ui-sample.js, and the knobs' specs and drag rules), with its fixture
  tests; run 44737596119 green on every job and step.
- Done: the fixtures, appended: `ui/sample-rules.json` gains the whole sound preset x recipe x clip preset product as
  digests; `ui/readouts.json` gains what the real Sample and Macros tabs do, run under the prototype tests' own DOM.
- Done: the full Sample tab and the Macros tab (`ui/tabs/SampleControls`, `SampleTab`, `MacrosTab`), MainView's wiring,
  and their tests in the editor (`Tests/ui/test_sample_tab.cpp`); M0's editor and controls tests follow the new knobs.
- Done: the pictures of run 44744045791 (all eight `m6_*` pictures, and M0-M5's pictures with the tabs folded) looked at:
  nothing clipped or overlapping, the faded controls and the hint line as the prototype's.
- Done: the fix for run 44744045791's three red checks (below): a knob's readout and the knob cases' start. Run
  44756460594 on 54c3178 is green on every job and step; its 98 pictures are byte for byte the ones looked at.
- Next: these notes on the branch, a green run on that head, then the fast-forward of main (main is still 3279614).

### Files

- `Source/ui/SampleTabRules`: sl-ui-sample.js's exported pure part, ported line by line: `contexts`, `fades` (the 22
  RULES in their order, with their sentences), `grainKind`, `headsMulti`, `toneAllOpen`, `randomInUse`, `kindOf`,
  `suggestEngine`, `pickPart`; `macroText`, `offsetText` and `parseNum` / `parseMs` / `parsePct` (one copy each:
  `params/ParamText`'s, which the parameters print with); KNOB_MATH (`toNorm`, `fromNorm`, `qLog`, `qLin`,
  `magnetLog`, `r6`). Also the knobs as buildSampleTab and buildMacros make them (`KnobSpec`, `knobSpec`: range, typed
  range, format, q function, parser, colour, rule, pin field, sentences; the Grain knob's spec follows the engine as
  the prototype's dyn does), their texts (`knobText`, `knobHint`, `knobTitle`, `fadeTitle`, `typingHelp`) and a drag
  (`beginDrag`, `dragTo`, `dragCommits`: 170 px across the knob, Shift 0.12, the 2 px dead zone). No JUCE.
- `Source/ui/tabs/SampleControls`: `SampleKnob` (the prototype's knob), `LockToggle` (Lock lights only with Cloud), the
  `TabActions` the tabs give their controls (one undo step, the hint line, a toast) and `HoverWatch`.
- `Source/ui/tabs/SampleTab`: the tab in the prototype's three rows, `SettingsWatch` (the instrument and the clip the
  fades read, read again only when a parameter, the INST state or the clip changed), `Fader` (a faded control: .42,
  .6 under the mouse, and its hint line), `ControlState` and `controlStates ()` (every control as the fixture names it).
- `Source/ui/tabs/MacrosTab`: the 8 macros with Centre all and the 5 offsets with Reset all.
- `Source/ui/Controls`: small hooks for the tabs: `ParamToggle::onUserChange`; `SegmentedControl::setLabels`,
  `setSegmentAlpha`, `onUserChange`, `onHoverChange`, per-segment tooltips and the caption's; `Knob::onGestureEnd`; the
  padlock icon.
- `Source/ui/MainView`: builds the two tabs with the processor, sends their hints to the hint bar and their toasts to
  the toast, and syncs them with the rows on every tick and after every edit.
- `tools/fixtures/ui.js`: the product digests and `sampleControls ()` (below); `tools/README-fixtures.md` updated.
- Tests: `Tests/ui/test_sample_rules.cpp`, `test_readouts.cpp`, `test_sample_tab.cpp`; `Tests/plugin/test_editor.cpp`
  and `Tests/ui/test_controls.cpp` follow the new knobs.

### Decisions and deviations

- **The fixtures ask the real prototype.** The M6 table wants the fades "for every sound preset x recipe x clip preset";
  the file had a subset because the product (864 pairs) is far over the size budget written out. `sample-rules.json` now
  also holds the product as one FNV-1a digest per pair over a text of the fades, contexts and grain kind (12 KB), so
  every pair is checked. For the knobs, `readouts.json` now holds what the real sl-ui-sample.js does when it runs under
  prototype-v4/tests/test-ui-sample.js's own `boot ()` (its minimal DOM and fake SLApp, read word for word with
  `C.testFunctionSource`): 288 drags (5 moves and a release, as the test file's `drag ()`), 36 double-clicks and 74 typed
  values on every knob, each with the value, the readout, the undo steps and their label and any toast; and every
  control's faded state, tooltip, hint line and readout in four settings. The inline formats and q functions of the
  Cloud and Lock grain, Refresh time, the macros and the offsets are thereby checked against the prototype's own code,
  not retyped. The fixtures are now 4.96 MB of the 5 MB budget (41.5 KB left).
- **toNorm and fromNorm use a port of fdlibm's log** (V8's Math.log), private to SampleTabRules.cpp: the knob maths
  compare exactly with the prototype's, and the C libraries' log rounds some inputs the other way. A JavaScript copy of
  the routine gave Math.log's bits on 2.7 million inputs. core/JsMath (M1's) holds jsExp; jsLog could join it there.
- **Two of the prototype's clicks change two parameters, as in the prototype**, in one undo step: Lock on another engine
  picks Cloud with Lock ("Engine: Cloud with Lock", and the toast; its faded sentence says so), and Mirror with too few
  heads also sets Heads ("Motion: Mirror (2 heads)"). Decision 1's "exactly its own parameter" is read as "exactly the
  parameters the prototype's control changes"; the test lists them. Every other control changes its own parameter only.
- **A knob shows the exact value it set itself** while its parameter still holds that value's float, as the Region's
  exact edges do (plan 2.2); a value the host or Undo set shows as the parameter's float. So a drag's readout and the
  next drag start from the prototype's numbers (fmtPct (0.005) reads "1%", the float 0.0049999 would read "0%").
- **Typed values are clamped to the typed range and then to the parameter's** (normalizeInst's clamps, which the
  parameter ranges equal), as the prototype's patchInst does after the knob's clamp.
- **The Region knobs and Loop fade stay on the Sample tab**, after the Refresh knobs: M0 put them there so every parameter
  has a control (the prototype has the Region on the waveform only and no crossfade control). They are M0's knobs (the
  JUCE slider: 200 px across, Ctrl for fine steps), not the prototype's, and now make one undo step per gesture
  ("Region start", "Region end", "Loop fade").
- **The Lock switch draws a padlock**, not the prototype's emoji (an emoji keeps its own colours in both themes, the
  prototype's own reason for drawing its pin); its hint line reads "Lock: ..." where the prototype's reads "🔒 Lock: ...".
  The Motion buttons keep the prototype's arrows and the Re-roll button its circle arrow.
- **No arrow keys on a knob.** The prototype's knob takes the arrow keys after Tab moves the focus to it; StrandLock's
  controls never take the keyboard (the Keys rule), so typing a value is the keyboard's way in. The value box takes the
  keyboard only while it is typed in, as the inspector's boxes.
- **Knob widths:** the macros and offsets are 96 px (the prototype's max-width), so "+6 semitones formant" and "+0.25
  semitones of vibrato" fit; the Sample knobs are 64 to 96 px by their name and their widest readout. The prototype's
  flexbox lets a knob grow with its text, which would move the row while a knob is dragged.
- **Not built in T1:** the engine suggestion line (E3) needs M7's analysis; the pins and Recipes and Dice are T3's (each
  `KnobSpec` already names its Dice field in `pin`).
- **Where the M6 table's T1 rows run.** "fade rules": `test_sample_rules.cpp` (`sampleRulesMatchFixture`,
  `sampleRulesWholeProduct`). "readouts": `test_readouts.cpp` (`readoutsMatchFixture`, `knobReadoutsMatchFixture`).
  "knobs" in two halves: the rules alone in `test_sample_rules.cpp` (`knobGesturesMatchFixture`), and the real knobs in
  the real editor in a third file, `test_sample_tab.cpp` (`sampleTabKnobsMatchFixture`, `sampleTabEachControlOwnParameter`,
  `lockGrainTypedPlaysFourTaps`, and `sampleTabControlsMatchFixture` for the faded look). The plan names two files; the
  editor half needs a processor and an editor, which the pure rules files do not build, so it has a file of its own
  (one test file per area, 7.3).

### For the architect

- core/JsMath could take `jsLog` (now private to `ui/SampleTabRules.cpp`).
- The fixture budget has 41.5 KB left after T1.
- The Region knobs and Loop fade drag differently from the prototype's knobs next to them (M0's slider). Making them
  prototype knobs needs a q function and a readout the prototype does not have.

### Hand-off for T2 and T3

- **Fades and contexts:** `SettingsWatch` reads the instrument and the clip once per change; `rules::contexts` and
  `rules::fades` give per-rule `notes` counts (how many notes use a control through their own settings), which the
  Note settings panel (T2) can show; `rules::fadeTitle` already writes "N notes use it through its own settings (Note
  settings)." `MainView::syncRollControls` syncs both tabs on every tick and after every roll event.
- **Recipes and Dice (T3)** go under the Sample tab's rows, as the prototype's #recipesBox shares the pane: add them in
  `SampleTab`'s constructor after At the end (`newRow ()`, then the buttons), hover each recipe with
  `SelectionTools::previewRecipe` (M5's hand-off). A recipe changes the clip and parameters: one undo step for both.
- **Pins (T3):** each `KnobSpec::pin` names the knob's Dice field; the rows' captions (Engine, Motion, Jump every,
  Refresh, Head tune) and the Lock, Tape and Each grain backwards switches need theirs too (sl-ui-sample.js pinBtn).
  Decision 4 keeps them in UserSettings.
- **Undo labels** are the prototype's patchInst labels; `SampleTab::getSteps ()` and `MacrosTab::getSteps ()` list the
  steps the tabs made, for tests. A test that counts steps records its start first (`TabRig::record ()` in
  test_sample_tab.cpp): the history leaves out a step equal to its newest.
- **A knob's two values:** `SampleKnob::value ()` is its parameter's value (the exact one while the knob set it),
  `readout ()` the text under the dial, which only the editor's timer (`syncToParameter`) and the knob's own gestures
  change. A test that sets a parameter and then reads a knob calls `MainView::syncControls ()` in between.
- **Pictures:** `m6_sample_tab_*` and `m6_macros_tab_*` (test_sample_tab.cpp) show both tabs whole in a tall window;
  T3 names the M6 table's own.

### CI results

- Run 44737596119 on ee20020 (the rules and their fixture tests), green on every job and step: Windows Release 1833
  checks in 315 tests, Linux Release 1825 in 315 (main had 1815 and 1807 in 310), the sanitizers' groups 808 in 139
  and 142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE assertions), the Debug pictures 0 JUCE assertions,
  pluginval strictness 10 SUCCESS, the fixture check passed (26 files, 4,958,478 bytes). Readouts: 907 of 907 cases of
  `ui/readouts.json` (macroText 361, offsetText 231, the formats, parsing, toNorm 40, fromNorm 35, qLog 42, qLin 56,
  magnetLog 4) exact; 486 of 486 knob readouts the prototype's (50 of them the Cloud and Lock grain formats). Fade
  rules: 309 of 309 cases of `ui/sample-rules.json` exact, and 864 of 864 product pairs the prototype's digest. Knob
  gestures on the rules: 398 of 398 (288 drags, 36 double-clicks, 74 typed values).
- Run 44740808009 on 8717d5b (the tabs): red; one string in test_controls lost its escaped quotes (MSVC and GCC both
  stopped there; MSVC compiled every other file). Fixed in acf39b7.
- Run 44744045791 on acf39b7: red on 3 of 1849 checks (Windows) and 3 of 1841 (Linux), in 319 tests; every other job
  and step green (the sanitizers' groups 808 in 139 and 142 in 20, the Debug-CRT allocation run 38 in 7, the Debug
  pictures 0 JUCE assertions, pluginval SUCCESS). Two were in test_controls: a knob's readout read its parameter
  directly, so a host change showed at once and not on the editor's timer (M2's rule). `SampleKnob::readout ()` is now
  the text under the dial, which only the timer and the knob's own gestures change. The third: 7 of 398 knob cases in
  the editor made no undo step where the prototype made one. Each case set its start without a step, and the history
  leaves out a step equal to its newest, so typing 4.3 into Cloud's Grain (5 ms) after the case before had left 5 ms
  there pushed nothing. The prototype's harness counts each case's pushes from a fresh start, so each case now records
  its start first. In the plugin this can only happen after a host change: the value typed is then already the
  history's newest step.
- Run 44756460594 on 54c3178 (the fix), green on every job and step: Windows Release 1849 checks in 319 tests, Linux
  Release 1841 in 319 (main had 1815 and 1807 in 310; the ui group is now 509 checks in 108 tests on Windows), the
  sanitizers' groups 808 in 139 and 142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE assertions), the Debug
  pictures 0 JUCE assertions, pluginval strictness 10 SUCCESS, the fixture check passed (26 files, 4,958,478 bytes).
  The M6 rows: fade rules 309 of 309 cases exact and 864 of 864 product pairs the prototype's digest (8321 faded
  controls in all, 190 pairs with no grains); readouts 907 of 907 exact (macroText 361, offsetText 231) and 486 of 486
  knob readouts; knobs on the rules 398 of 398 (227 drags moved a knob, 165 of them on the q grid, 62 fine; 49 typed
  values kept, 11 refused); the real tabs: 200 of 200 controls in 4 settings with the prototype's faded state,
  tooltip, hint line and readout, 398 of 398 knob cases in the editor (300 undo steps, 227 drags wrote their parameter
  while they moved), 81 of 81 control gestures changed exactly their own parameters with one undo step and its label
  (and Undo put every parameter back), Lock Grain typed 4.3 holds 4.30000019 and plays 4 taps (a 10 px drag from 4
  lands on 5 and plays 5); the timer test: a host change shows only on the timer, never during a drag.

## T2

Sample mode 8 in the roll (the chevrons and per-note thumbnails, the own-settings dot and the slide warning), the
Sample editor strip's live heads (ui/strip/HeadsOverlay), the strip click that sets the selected notes' Start, the
Note settings panel (S7) and the "Try this" line (U3) (plan 7.8 T2, decisions 5 and 6). Branch `m6-sample`, from main
10dc52a (T1).

### Progress

- Done: `ui/roll/NoteDecor` (sl-ui-panels.js tabAt, simulate, peakBins and thumbAmps, ported line by line) and its
  fixture test `noteDecorHelpersMatchFixture`: run 44767434482 on af7ca15 green on every job and step, the 30 cases
  of `ui/inspector.json` that M4 left for M6 30 of 30 bit for bit on both platforms.
- In progress (second push): the decorations drawn through RollPainter and the NoteLayerCache (chevrons, thumbnails,
  the own-settings dot, the slide warning), `ui/strip/HeadsOverlay` and the strip click that sets Start,
  `ui/tabs/NoteSettingsTab`, Try this in the hint bar, the Sample chips' tooltips, and the tests
  `test_heads_overlay`, `test_note_settings`, `test_note_decor`, `test_try_this` with their pictures.
- Next: CI on that push, every new picture looked at, these notes, then the fast-forward of main.
