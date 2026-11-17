# NOTES-M4

Builder notes for M4 (PLUGIN-PLAN section 4 "M4 Roll UI" and 7.6). Append only: one bullet per item, with the reason.
Each part writes its own section: R1, R2, R3, Fix.

## R1

The headless half of the roll (plan 7.6 R1): geometry, gestures, the document's roll state, undo steps, the
inspector's pure helpers and key routing. Branch `m4-roll`. Nothing here draws or sees a mouse.

### Progress

- Done: `doc/RollState.h` and the roll state in `ClipDocument` (selection, primary note, mode, sub-lane, the
  inspector's node), `ui/roll/RollGeometry`, `ui/roll/RollGestures`, `ui/roll/InspectorText` (the six pure helpers,
  plus `rateId` and `fmtNum`), `ui/KeyRouter`, `tools/fixtures/roll.js` with `Tests/fixtures/roll/geometry.json` and
  `gestures.json`, and the tests `test_inspector`, `test_roll_geometry`, `test_roll_gestures`, `test_mode_lock_ui`,
  `test_keys` (all in group `ui`).
- In progress: CI on the gestures, the geometry and the keys.
- Next: CI green, the figures below, rebase on main, fast-forward main.

### Files

- `Source/doc/RollState.h`: `RollState` (selected ids in the order they were added, primary, mode 1..8, sub-lane,
  the inspector's `ActiveNode { id, lane, k, len }`) and `activeLane ()`. Plain C++, so the pure roll code reads it.
- `Source/doc/ClipDocument`: `rollState ()`, `setSelection (ids, primary)`, `setPrimary`, `setMode`, `setSubLane`,
  `setActiveNode`. These are sl-ui.js `setSel`, `setPrimary`, `setMode`, `setSubLane` and `setActiveNode`. Every new
  clip version drops selected notes that are gone and the inspector's node when its note or node count changed
  (`pruneSelection`, `validateNodes`). `replace` (a state load, Undo, Redo) also lets the inspector's node go, as
  `applyState` does.
- `Source/ui/roll/RollGeometry`: sl-ui.js `cy`, `pitchAt`, `geom` (the note line: pitch, glide and the vibrato
  squiggle, Float32 as in the prototype), `baseYAt`, `valueY`, `laneValue`, `nodeXY`, `segXY`, `glideHandles`,
  `noteBox`, the end and start dots, `onNoteLine`, `selectedNotes`, `handleNotes`, `hitNote`, `hitNode`, `hitSeg`,
  and V8's `Math.hypot` (`jsHypot`), so a press at the edge of a hit size decides as the prototype does. Note lines
  are cached by `Note::serial` and the zoom, tempo, vibrato rate and glide.
- `Source/ui/roll/RollGestures`: the pure state machine (decision 1); see the hand-off below.
- `Source/ui/roll/InspectorText`: `fmtPos`, `parsePos`, `parseBeats`, `parsePitch`, `rateId`, `fmtNum`, `plainUnits`,
  `laneText`, `laneValueFromText`, and `laneReadout` (sl-ui.js `fmtLane`, the roll's live readout). The regular
  expressions are matched by hand in the order a backtracking engine tries them.
- `Source/ui/KeyRouter`: `route (key, keysOn)` and `shortcutFor (key)`.
- `tools/fixtures/roll.js` (a new fixture area, found by `make-fixtures.js` without editing it).

### Decisions and deviations

- **The gesture fixtures come from the prototype itself.** `roll.js` runs sl-ui.js unmodified in a stub page (no
  canvas is drawn) with two injections: a list of its roll functions on its API, and a hook in `openMenu` that
  records the items. Its pointer handlers are driven with made-up events, and after every step the case records the
  clip, the note order, the undo steps and label, the selection, the primary note, the inspector's node, the
  readout, the toast and Slide into's arming. 70 cases: the table's gestures by hand, plus seeded sequences in every
  mode (2 per mode, 4 in Sample mode, one per sub-lane). New notes get the same ids in both (`addNote` and
  `uniqueId` are deterministic), so the replay compares every note by id.
- **The fixture size budget is nearly full.** With `roll/` (0.19 MB) the fixtures hold 4.87 MB of the plan's 5 MB
  (5.4). The seeded sequences were cut from 4 per mode to 2 to keep room for the later milestones.
- **Box selection is M5's, so the seeded sequences never drag a box.** A press on empty space starts a box in the
  prototype, and a drag selects what it touches (`applyBox`). M5 builds BoxSelect, so here a drag on empty space is
  tracked (`RollGestures::box ()` gives its rectangle for R2 to draw) but selects nothing, and a click there clears
  the selection as in the prototype. roll.js leaves out the moves after a press that starts a box.
- **Not built (later milestones):** the Nodes switch and node picks (`nodeSel`; M5), the lane-button menus and hover
  ghosts (M5), Sample-mode thumbnails (M6). Their branches in rollDown are simply absent; the switch is never on.
- **Copy lanes and Paste lanes are in the right-click menu**, as in the prototype, with every lane in the mask
  (`RollGestures::copyMask`). The Copy / Paste tab that changes the mask is M5's.
- **The menu's "Slide into the next note I click" has no link emoji.** JUCE's fonts have none (M2's sound menu made
  the same call). The gesture test strips it from the fixture's label.
- **Readouts and the bar label count the clip's beats per bar.** The prototype's bars are always 4 beats; the plugin's
  clip has `beatsPerBar` (4 unless a project says otherwise), so `beatLabel` and the clip length use it.
- **One undo step per finished gesture, through the processor's existing API.** A live step is `ClipDocument::apply`
  (published to the engine, no history); the release calls `RollHost::commit (label)`, which is
  `StrandLockProcessor::pushUndoStep (label, {})` in the plugin. `EditHistory` already ignores a state equal to the
  last one, so a gesture that changes nothing adds none, and a drag that ends within 7 decimals of where it started
  is put back exactly (sl-ui.js `sameClip`). No change to `EditHistory` was needed.
- **A refused edit inside a gesture** (the note cap, 32 MiB, the snapshot budget) leaves the clip as it was and
  toasts the document's refusal text. The prototype has no caps.
- **Double-clicks** are the prototype's: two presses within 320 ms and 22 px on the same kind of thing (a node, a
  segment diamond, a note, empty space). The gestures take a time in milliseconds with every press.
- **The menu's Random seed** is `(timeMs % 100000) + 1`, as the prototype's `(Date.now () % 100000) + 1`.
- **Keys.** With Keys off `route` returns false for every key and runs nothing. With Keys on Space is never kept (with
  any modifier) and every other key is kept, M0's rule ("it keeps the keys it receives, except Space"). The
  shortcuts: Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y, Delete and Backspace, Ctrl+A, Ctrl+D, Ctrl+C and Ctrl+V (lanes),
  1 to 8 and the number pad's 1 to 8, Escape, B (Slide into). Shift with a digit and Alt with a key run nothing, as
  in the prototype. The prototype's A to K note keys and Z / X octave keys are the live keyboard (M8) and N is the
  Nodes switch (M5): not routed here.
- **The lane-mode lock test includes Sample mode 8** (125 gestures on each sub-lane) and a shape from the right-click
  menu now and then (a shape changes only the active lane). In Pitch mode the slide handles and Shift+click may change
  a note's slide; in every other lane mode the slide must not change either.

### Hand-off for R2 (drawing) and R3 (the live roll)

- **One geometry.** `RollGestures::geometry ()` is the `RollGeometry` the gestures hit-test with; the view draws with
  the same one. Set the zoom with `setZoom (beatWidth, rowHeight)` (12..400 and 8..30). Coordinates are roll pixels
  with nothing scrolled: the view adds its scroll before calling a gesture and subtracts it when drawing. `sync ()`
  hands the current clip, instrument and tempo to the geometry (every event calls it; the view calls it before a
  paint).
- **What R2 draws from:** `geom (note)` (K points; `base` and `vib` in semitones above the note's row; `smin`,
  `smax`; the glide), `laneTable (note, lane)` (the ribbons, at the line's resolution), `baseYAt`, `valueY`,
  `nodeXY`, `segXY` (null under 20 px), `glideHandles` (the primary note's two diamonds), `noteBox (note,
  withRibbon)`, `endDot` and `startDot`, `RollGeometry::handleNotes (clip, rollState)` (whose nodes show: the
  selection when at most 16, else the primary), `RollGestures::box ()` and `readout ()` (text and where).
- **The host.** `RollHost { doc, commit, audition, toast }`: `doc` = `proc.getDocument ()`; `commit` =
  `[&proc] (const std::string& l) { return proc.pushUndoStep (l, {}); }`; `audition` = an `auditionOn` through the
  command queue and an `auditionOff` 220 ms later at velocity 0.7 (sl-ui.js `audition`); `toast` = the view's toast.
- **Settings the view keeps current** (`RollGestures::settings ()`): `snap` (the Grid menu, beats, 0 = Free, kept in
  UIST), `free` (the Freehand button; use `toggleFreehand ()` for its toast in Build), `multi` (the Multi button),
  `inst` (`proc.readInst ()` with the INST state's key, scale and scale lock), `bpm` (the host's tempo, 120 without
  one), `sampleSeconds` (the current sound's length, for Fit), `scope` (Each note / Across, kept in UIST).
- **Mouse.** Left press: `pointerDown (x + scrollX, y + scrollY, { shift, ctrl-or-cmd }, Time::getMillisecondCounterHiRes ())`;
  drag: `pointerMove`; release (or a lost capture): `pointerUp ()`. Right press: `contextMenu (x, y, time)` returns
  `MenuItem`s (item, header in a lane colour, separator, a row of two buttons); show them as a PopupMenu, and call
  `runMenuItem (item)`; when it returns a menu (Each note / Across), show that one in its place.
- **Commands for the buttons and keys:** `setMode`, `setSubLane`, `toggleFreehand`, `armLink`, `duplicateSelection`,
  `deleteSelection`, `selectAll`, `clearSelection`, `copyLanes`, `pasteLanes`. Undo and Redo are the processor's
  (`proc.undo ()`, `proc.redo ()`); the top bar's buttons call them with Keys on or off.
- **Keys.** The editor's `keyPressed` returns `KeyRouter::route (key, keysOn)` with `KeyActions` wired to the calls
  above (it still records the key on the Diagnostics page first). Undo and Redo toast "Undo: label" in the prototype.
- **The inspector (R3)** reads `rollState ().primary` and `activeNode` and parses with `InspectorText`; a typed value
  is one undo step through `proc.editClip (label, clip)`.
- **The hint bar (R3)** texts are sl-ui.js `modeHint`; they depend on the mode, Freehand, Slide into's arming
  (`isLinkArmed ()`) and the Nodes switch (M5).
