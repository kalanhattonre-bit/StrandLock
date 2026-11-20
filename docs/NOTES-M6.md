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
- Done: the decorations drawn through RollPainter and the NoteLayerCache (the chevrons, the thumbnails, the
  own-settings dot, the slide warning), `ui/strip/HeadsOverlay` and the strip click that sets Start,
  `ui/tabs/NoteSettingsTab`, Try this in the hint bar, the Sample chips' tooltips, and the tests `test_heads_overlay`,
  `test_note_settings`, `test_note_decor` and `test_try_this` with their pictures (b9e5dc2, then two compile fixes,
  8ca4fa9 and 3f359da). Run 44778014019 on 3f359da: every M6 row green on both platforms; one check of M0's
  `alloc/hostChangesWithEditorOpen` red in the Debug-CRT run (below).
- Done: the fix (bcdddf5): the Note settings tab measures a group only when its text changed, and does nothing while
  it sleeps; a new test counts it. Run 44784505324 on bcdddf5 is green on every job and step.
- Done: the pictures of run 44778014019 looked at beside the prototype's (Build, Sample mode, Note settings, the strip
  while playing): the 9 `m6_*` pictures T2 adds, and M0-M5's roll and window pictures, which now show the decorations.
  Nothing clipped or overlapping; the chevrons, thumbnails and heads as the prototype draws them; Try this beside the
  mode's line at 75, 100 and 200 % and in the 900 px window.
- Next: these notes on the branch, a green run on that head, then the fast-forward of main (main is still 10dc52a).

### Files

- `Source/ui/roll/NoteDecor`: sl-ui-panels.js tabAt, simulate, peakBins and thumbAmps, ported line by line (JavaScript's
  Math.max, Math.min and `!==` where NaN matters); simPoints, thumbAlpha, chevronStrength, hasOwnSettings and
  slideWarns (decorate's small rules); and `NoteDecor`, the cache the drawing reads: the sound's peak bins (made when a
  sound arrives) and each note's simulation and thumbnail heights (made again only when the note's version, the
  instrument fields simulate reads, the tempo, the zoom or the sound changed), with `key ()`, a hash of everything every
  note's decorations read.
- `Source/ui/roll/RollPainter`: `decorations (note, selected)` (sl-ui-panels.js decorate as data: the thumbnail's
  columns, the chevrons, the Mirror chevrons, the still bars and the silent square, Scatter's jumps, the own-settings
  dot, the slide warning) and `paintDecorations`, called at the end of `paintNote` (sl-ui.js runNoteDecorators: right
  after each note). `PaintState::decor` and `decorKey`. The note bounds reach `decorPad` (18 px) under the line.
- `Source/ui/roll/NoteLayerCache`: the Key holds `decor` (NoteDecor::key); a note's shown state holds where the note it
  slides into starts (its warning mark moves with that note).
- `Source/ui/roll/RollView`: owns the NoteDecor; `refresh` hands it the sound and the clip; the ViewKey holds `decor`,
  so a change repaints the roll. `getDecor ()` for the tests.
- `Source/ui/strip/HeadsOverlay`: the live heads and grains of the newest display frame (sl-ui.js drawStrip), and the
  selected notes' Start markers; `heads ()` and `grains ()` say where each goes.
- `Source/ui/strip/WaveStrip`: draws through the overlay (its own head drawing moved there); `updateStarts` (the
  markers, on every refresh); a short click with notes selected sets their Start (`setStartAt`: model::setNoteSample
  through `editClip`, one undo step "Set start", the prototype's toasts); the click's time is the mouse events' own;
  `onEdit`.
- `Source/ui/tabs/NoteSettingsTab`: the Note settings panel (S7), below.
- `Source/ui/tabs/TabPanel`: `resizeItem` and `layoutChanged` (several widths change, one layout).
- `Source/ui/roll/SelectionBar`: `SelectionTools::liveSession`, the bar's slider session (liveRun) for the Note settings
  tab's sliders, as the prototype's panels share one.
- `Source/ui/HintBar`: `sampleLaneHint` (SUB_HINT), `tryThis` (updateTry), and the hover for a suggestion cut short.
- `Source/ui/MainView`: the Note settings tab in place of M0's "arrive in M6" line, synced with the other tabs; Try this
  on every sync; the Sample chips' tooltips; the strip's onEdit.
- Tests: `test_inspector.cpp` (`noteDecorHelpersMatchFixture`), `test_heads_overlay.cpp`, `test_note_settings.cpp`,
  `test_note_decor.cpp`, `test_try_this.cpp`; `test_roll_perf.cpp` and `test_roll_view.cpp` start from a loaded sound.

### Decisions and deviations

- **Decision 5: the decorations are drawn in every mode with the prototype's alphas** (thumbnails 0.42 / 0.24 selected
  or not in Sample mode and Build, 0.22 / 0.11 elsewhere; chevrons 1 / 0.8 in Sample mode, 0.8 / 0.5 elsewhere), into
  the note layer, note by note after each strand, as sl-ui.js runNoteDecorators does. Each note's chevrons are one path
  (and its Mirror chevrons another), its thumbnail columns, still bars and jumps one rectangle list each: the prototype
  strokes each chevron alone, so where a note's two chevrons overlap (from 2.5x up, 3.6 px apart) it is a little
  darker there than here. Every outline is a path and every fill a rectangle or path, so a partial update of the layer
  gives a full one's picture with Direct2D too (M4's rule; `rollMoveRepaintsThreeNotes` checks it with the
  decorations drawn).
- **The plan's M6 text names the chevrons and thumbnails with mode 8; the prototype draws them in every mode** (M4's
  hand-off UI4-9), and so does StrandLock. Mode 8's chips and lanes were M0's and M4's; T2 gives the chips the
  prototype's sentences as tooltips (sl-ui.js SUB_HINT).
- **What the decorations read redraws every note.** The instrument fields simulate reads (the Region, At the end,
  Speed, the Speed macro, Motion, Engine, Jump every, the seed, the root, Pitch), the Heads and Heads macro (the Mirror
  chevrons) and the sound are hashed into one key; when it changes the note layer draws every note again, as the
  prototype redraws every frame. Cubase automating one of these redraws the roll at the editor's refresh rate.
- **The thumbnails' peak bins are made when a sound arrives**, on the message thread (one pass over the sound, as M2's
  WavePeaks); the prototype makes them 80 ms later on a timer.
- **A slide's warning mark moves with the note it slides into**: the note layer's shown state holds that note's start,
  so moving it redraws the warning (a note's own version does not change when another note moves).
- **HeadsOverlay is a painter the strip owns, not a component.** A transparent component over the wave would repaint
  the wave under it anyway; the strip repaints only its wave area for each new frame, as before.
- **Plan 1.5's mapping of noteIdHash to the clip's notes is not used**: the prototype's strip draws every voice's heads
  alike, and the overlay does too.
- **The strip click (sl-ui.js stripUp)**: a press of at most 300 ms with notes selected sets their Start
  (model::setNoteSample with start mode "region" and the spot in the Region), one undo step "Set start", and toasts
  "Start set for 3 notes at 40% of the Region"; outside the Region with notes selected it toasts the prototype's
  sentence and changes nothing. A longer press only listens. The time is the mouse events' own (as the roll's
  double-click rule), so the tests are exact.
- **Outside the Region the prototype plays the file itself while the mouse is held** (rawAuditionStart). Not built: it
  needs a player of its own on the audio thread and a command the CommandQueue does not have (M2 left it for M6's
  Preview; T3 owns Preview). The readout still says "outside the Region".
- **The Note settings tab keeps every control in its place.** The prototype hides the groups while nothing is selected
  and the Range and Re-roll while the Start is not Random. Here the groups stay, faded (0.45) and taking no clicks while
  nothing is selected (the selection bar's idle drawer), the Spot faded for Carry on and With the song, the Range and
  Re-roll faded or greyed unless the Start is Random: the pane keeps its rows, so a click that selects a note never
  moves the roll under the mouse (decision 6 of M5 shares the window by whole rows).
- **Every Note settings change is one model call and one undo step** with the prototype's label (setNS, setMotion,
  setTravelKind, Re-roll, the sliders' liveRun), "Jump every : follow" included. Mirror on notes whose Heads never reach
  1.5 sets their Heads lane to 2 heads in the same step and toasts it; Path or Speed on a flat Travel line resets it to
  Fit or 1x in the same step, with the prototype's toast. The sliders run through the selection bar's live session.
- **A faded group says why** in its tooltip, and its buttons with no text of their own show the same reason (an HTML
  title on the group does that for its children).
- **Try this (decision 6)** is sl-ui-panels.js updateTry: a tip for each lane mode, in Sample mode one per Sample lane,
  on Travel one per Motion (the primary note's own, else the Sample tab's) with its next step once done, and the
  engine's own tip where the general one does nothing (Freeze or Grain on Tape). It keeps M4's layout: at most half the
  bar, the mode's line keeps the rest. A suggestion cut short shows whole while the mouse is on it, over the right half
  only, growing upwards: it never covers the mode's line. The texts point at controls StrandLock has (Tools, Strum,
  Shapes, Pattern, Random, Across, the Sample tab's Refresh, Jump every, Head tune, Re-roll, the waveform).
- **Three M4 tests now start from a loaded sound** (`rollFullRepaintCost` and `rollMoveRepaintsThreeNotes` through
  their rig, and `rollViewRepaintsWhatChanged`): the starter sound arrives from the worker some tens of milliseconds
  after a processor is made, and since the thumbnails read it, its arrival redraws every note.
- **The Note settings tab measures a group only when its text changed.** The editor syncs the tabs on every tick, and
  the host's automation changes the instrument on most ticks. Measuring a text shapes it, which is slow in a Debug
  build: measuring all ten groups again on each tick let the editor tick only 60 times in the Debug-CRT run of
  `alloc/hostChangesWithEditorOpen` (it needs 240 to close the editor and open it again). Each group now keeps what it
  was measured with (its caption, its buttons' texts, its "mixed" tag) and is measured again only when one of them
  changed; the first line keeps its width while its text stays; and while nothing is selected a sync does nothing (the
  sleeping tab shows nothing of the instrument). `noteSettingsMeasuresOnlyWhatChanged` counts both.

### For the architect

- **The raw-file audition outside the Region is not built** (sl-ui.js rawAuditionStart: holding the mouse on the strip
  outside the Region plays the file itself). It needs a player of its own on the audio thread and a command the
  CommandQueue does not have; M2 left it for M6's Preview, which is T3's.
- **The decorations made M4's full repaint of 512 notes slower.** Linux: 10.6 ms before T2 (run 44767434482), 23.5
  and 26.4 ms with them (runs 44778014019 and 44784505324). Windows varies more from runner to runner: the software
  renderer 34.9 ms before, 43.1 and 23.2 ms with them; Direct2D 26.8 ms before, 67.9 and 29.8 ms with them. The target
  is 50 ms (printed) and the gate 250 ms, so Direct2D was over the target once. The runner has no GPU, so Direct2D
  draws in software there. Each note's chevrons are a stroked path, which Direct2D probably turns into a new geometry
  on every paint; if a real machine is slow here, the chevrons could be kept per note as filled outlines.
- **Automating a setting the decorations read redraws the whole roll** on each editor refresh while it moves. The
  prototype redraws every frame anyway. A cheaper key would hash, per note, only the fields its own simulation reads.
- **Plan 1.5's noteIdHash is not used by the strip**: the prototype draws every voice's heads alike. It stays in the
  frame for a later per-note view.
- **The Note settings tab keeps its controls while nothing is selected** (faded, as the selection bar's idle drawer);
  the prototype hides them. The plan does not say; the reason is under the decisions above.

### Hand-off for T3

- **Pictures:** T2's `m6_mode8_*`, `m6_build_decor_dark`, `m6_heads_overlay_*` and `m6_note_settings_*`
  (test_note_decor.cpp, test_heads_overlay.cpp, test_note_settings.cpp) are review shots, like T1's. T3 names the M6
  table's own snapshots.
- **Note settings:** `NoteSettingsTab::sync ()` runs on every MainView sync and returns at once when the selection, the
  clip and the instrument fields it shows are unchanged (`sync (true)` after its own edits). Its controls act through
  `SelectionTools::act` (one undo step) and `liveSession` (the sliders), so a recipe or Dice that changes the notes'
  settings shows there on the next tick with no extra wiring.
- **Work on the editor's tick must stay cheap.** `alloc/hostChangesWithEditorOpen` needs at least 240 ticks in the
  Debug-CRT run's minute of host automation, and shaping text (`Theme::textWidth`, a GlyphArrangement) on every tick
  starved it (60 ticks). Measure a text when it changes, not on every sync.
- **Decorations:** `NoteDecor::key (inst)` hashes everything the decorations read, so a recipe that changes the Region,
  At the end, Speed, Motion, Engine, Jump every, the seed, Heads or the sound redraws every note by itself.
  `RollView::getDecor ().simulationsMade ()` counts the work for tests.
- **Preview:** the strip's press already plays the spot through the CommandQueue while it is held (M2's audition), and
  a short click sets Start (`WaveStrip::setStartAt`). The raw-file audition (above) would start in the outside branch
  of `WaveStrip::mouseDown`.
- **Try this:** `ui::tryThis (mode, subLane, primary, inst)` is pure, and MainView sets it on every sync.
- **A test that counts the roll's drawing starts from a loaded sound** (`proc.waitForSounds ()`): the starter sound
  arrives from the worker after the processor is made, and since the thumbnails read it, its arrival redraws every
  note.
- **Fixtures:** T2 added none (`ui/inspector.json` has held the 30 M6 cases since M4; test_try_this.cpp holds its 75
  cases inline, made by running sl-ui-panels.js's own updateTry in node). The budget still has 41.5 KB left.

### CI results

- Run 44767434482 on af7ca15 (NoteDecor and its fixture test), green on every job and step: Windows Release 1854
  checks in 320 tests, Linux Release 1846 in 320 (T1's head had 1849 and 1841 in 319), the sanitizers' groups 808 in
  139 and 142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE assertions), the Debug pictures 0 JUCE assertions,
  pluginval strictness 10 SUCCESS. `ui/inspector.json`'s M6 cases: 30 of 30 bit for bit (simulate 19, peakBins 2,
  thumbAmps 4, tabAt 5; 71 silent points, 12 Scatter jumps, 78 still points and 191 backward points among them).
- Run 44773412451 on b9e5dc2 (the rest of T2): red at the build. `SelectionTools::live`, the new name for the slider
  session, clashed with the member `live` (GCC and MSVC both stopped there); renamed `liveSession` in 8ca4fa9.
- Run 44775417577 on 8ca4fa9: red at the build. The Note settings tab called a one-argument lambda with two; fixed in
  3f359da, with two warnings (an unused function in WaveStrip.cpp, a shadowed name in MainView.cpp).
- Run 44778014019 on 3f359da: Windows Release 1915 checks in 333 tests and Linux Release 1907 in 333, all passed; the
  sanitizers' groups 808 in 139 and 142 in 20; the Debug pictures 0 JUCE assertions; pluginval strictness 10 SUCCESS;
  the fixture check passed (26 files, 4,958,478 bytes, as T1 left them). Red: 1 of 38 checks of the Debug-CRT
  allocation run (0 JUCE assertions, 0 allocations on the audio thread): `hostChangesWithEditorOpen` saw the editor
  tick 60 times and never close (1175 in run 44767434482). The editor's tick also took longer in Release (767 ticks on
  Windows, 1637 before; 1169 on Linux, 1729 before). The fix is bcdddf5 (decisions above).
- The M6 rows in run 44778014019 (Linux; Windows the same unless given):
  - Display slot: 10,000 blocks of 1600 samples (333 s of audio) written while the UI thread was paused, in 1160 ms on
    Linux and 1410 ms on Windows; the longest block 0.17 ms (Windows 0.30 ms) against its 33.3 ms of real time; 10,000
    frames written, 0 allocations and 0 frees; the frame the paused UI held never written into; its next take the last
    frame written (sample 16001600, the engine's), whole (1 voice, 1 head); a second take finds nothing newer.
  - Heads overlay: 5 of 5 heads of a fed frame at the strip's x for their place, with their number, way and strength;
    each solid head's line down the wave (3 of 3); 0 head-coloured pixels half way between two heads; the 5 grains;
    zoomed to 30 .. 70 %, the 2 heads in view and no others; no frame, nothing drawn.
  - Strip click: the Start of exactly the 3 selected notes set at 0.4000 of the Region (the bytes of
    model::setNoteSample), the other 23 notes as they were; one undo step "Set start", the toast "Start set for 3
    notes at 40% of the Region", 3 markers; the press played the spot through the command queue (2 commands); a press
    held 400 ms only listens; outside the Region no change and the prototype's sentence.
  - Note settings: 240 of 240 seeded selections show "mixed" exactly where model::getMixed says the notes differ (every
    group seen both mixed and shared); 28 of 28 clicks give the bytes of the direct model call with one undo step and
    the prototype's label; Velocity 3 of 3 live moves with no step, then one step on release; Mirror, Path and the
    faded groups as the prototype's.
  - Decorations: 16 of 16 mode and selection cases with the prototype's alphas (under the line 66.6 against 41.9:
    1.591 times, 0.8 / 0.5 = 1.6); the chevrons of Forward, Backward, Freeze, Mirror, Scatter, 3x and Stop; a 43-column
    thumbnail; the own-settings dot on 4 of 4 notes; the slide warning; 9 simulations made, then none on a refresh
    with nothing changed.
  - Try this: 75 of 75 cases the prototype's text (24 different suggestions); 1200 of 1200 layouts keep the mode's
    line and the suggestion apart; 151 cut suggestions show whole over the right half only.
  - M4's rows with the decorations: moving one note marks 3 notes' bounds, 107 note drawings in 3 rectangles, 2.8 % of
    the roll, 0 pixels different from a full repaint with both renderers; selecting a note repaints 3.0 %.
- Run 44784505324 on bcdddf5 (the fix), green on every job and step: Windows Release 1918 checks in 334 tests and
  Linux Release 1910 in 334 (T1's head had 1849 and 1841 in 319; the ui group is now 578 checks in 123 tests), the
  sanitizers' groups 808 in 139 and 142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE assertions), the Debug
  pictures 0 JUCE assertions, pluginval strictness 10 SUCCESS, the fixture check passed (26 files, 4,958,478 bytes).
  `hostChangesWithEditorOpen`: the editor ticked 1027 times in the Debug-CRT run (60 before the fix), 1613 times in
  Windows Release (767) and 1709 times on Linux (1169). `noteSettingsMeasuresOnlyWhatChanged`: nothing selected, 40
  instrument changes, 0 syncs did work and 0 groups were measured; 3 notes selected and the Region moved 40 times, 40
  syncs did work and 0 groups were measured (the Spot read "50% (0:03.00)", then "50% (0:03.60)"); Engine Cloud, 1
  group measured, then none. M4's full repaint of 512 notes: Windows software renderer 23.2 ms, Direct2D 29.8 ms,
  Linux 26.4 ms (target 50, gate 250). The display slot: 10,000 blocks in 806 ms on Windows and 1245 ms on Linux, the
  longest block 0.14 ms and 0.23 ms. 106 of the 107 pictures are byte for byte the ones looked at; in
  `m6_mode8_light_200` 2 pixels of the hint line's text differ by at most 8 levels.

## T3

Recipes 1-8 and Dice with pins, Preview while Cubase is stopped, the M6 table's pictures and the checklist (plan 7.8 T3,
decisions 3 and 4). Branch `m6-sample`, from main dd0aef2 (T2).

### Progress

- Done: Preview's clock in `host/HostSync` and the processor's switch, with `Tests/engine/test_preview.cpp` (024f25c). Run
  44796391576: every Preview row green on both platforms and under the sanitizers; one count in the test was wrong (7
  notes, not 6: the last playing block runs past beat 6), fixed.
- In progress: the Dice pins in UserSettings; Recipes, Dice, the pins, the Preview button and the paused notice on the
  Sample tab; `Tests/ui/test_recipes_dice.cpp` and the M6 table's pictures (`Tests/ui/test_m6_snapshots.cpp`).
- Next: a green run, the pictures looked at, the checklist, these notes, the fast-forward of main.
