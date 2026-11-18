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
  `test_keys` (all in group `ui`). CI green on 365de7f (run 44146142828, figures below).
- Done (second session): the view part of `RollGeometry` (the roll's size, the view's size, the scroll kept inside
  the roll, view <-> roll pixels, the zoom buttons, Ctrl + wheel zoom, the first view, UIST's view state) and its
  test `rollViewScrollAndZoom`. The first session left `scrollX` and `scrollY` as unused fields, so "beats <-> x with
  zoom and scroll" was not yet there. CI green on 77f2ad7 (run 44176806795, figures below).
- In progress: CI on this commit (notes only), then the fast-forward of main.
- Next: R2 (drawing), from the hand-off below.

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
  are cached by `Note::serial` and the zoom, tempo, vibrato rate and glide. The view part: `contentWidth` and
  `contentHeight` (sl-ui.js `sizeSpacer`), `setViewSize`, `setScroll`, `scrollX`, `scrollY`, `toRoll`, `toView`,
  `zoomBy (h, v)` (the zoom buttons), `wheelZoom (in, viewX)` (Ctrl + wheel), `centreOnNotes` (the first view) and
  `viewState` / `setViewState` (UIST's `zoomH`, `zoomV`, `scrollBeat`, `scrollRow`).
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
- **The view's scroll is the browser's.** In the prototype the roll is a canvas in a scrolling div, and the browser
  keeps `scrollLeft` and `scrollTop` in 0 .. the content less the view. `RollGeometry::setScroll` does the same, and
  a new clip (fewer bars), zoom or view size keeps the scroll inside the roll, as the browser does at the next
  `sizeSpacer`. The test's values come from sl-ui.js's own zoom, wheel and first-view lines run in node over a
  scroller that clamps the same way. A browser may also round the scroll to whole device pixels; the plugin does not.
- **The zoom buttons round the row height** (`Math.round (RH * v)`), so rows stay whole pixels. `setViewState`
  rounds too, so a row height stored through UIST's float32 (17.99999976 for 18) comes back as 18. `setZoom` itself
  does not round (the fixtures set row heights directly).
- **The pan drag (the Scroll switch) and the piano strip's wheel are not here.** The Scroll switch is a touch aid the
  M4 deliverable does not list; the strip's wheel is a plain `setScroll (x, y + deltaY)` for R3.

### Hand-off for R2 (drawing) and R3 (the live roll)

- **One geometry.** `RollGestures::geometry ()` is the `RollGeometry` the gestures hit-test with; the view draws with
  the same one. Gestures take roll pixels (nothing scrolled). The view keeps the geometry's view part current:
  `setViewSize` on every resize, `zoomBy` for the zoom buttons (across 1.3 and 1 / 1.3, rows 1.25 and 0.8),
  `wheelZoom` for Ctrl + wheel, `setScroll` for its scroll bars and the plain wheel; it passes `toRoll (x, y)` to a
  gesture and draws a roll point at `toView`. `sync ()` hands the current clip, instrument and tempo to the geometry
  (every event calls it; the view calls it before a paint).
- **The first view and UIST.** A new instance shows `centreOnNotes ()` once the view has its size (the prototype's
  init). A loaded state's UIST view fields go through `setViewState` (zoom first, then the scroll in beats and rows),
  and the view writes `viewState ()` back. UIST's defaults (zoom 1, scroll 0) cannot tell "never saved" from "saved
  at the top left"; R2 or R3 decides whether a loaded default view centres on the notes (a note for the architect).
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
- **Mouse.** Left press: `const auto p = geo.toRoll (e.x, e.y); pointerDown (p.x, p.y, { shift, ctrl-or-cmd },
  Time::getMillisecondCounterHiRes ())`; drag: `pointerMove` (also through `toRoll`); release (or a lost capture):
  `pointerUp ()`. Right press: `contextMenu (p.x, p.y, time)` returns
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

### CI results

- Run 44146142828 on 365de7f (the gestures, geometry, inspector and keys), green on every job and step: Windows
  Release 1535 checks in 245 tests (main had 1476 in 230), Linux Release 1527 in 245, the sanitizers' contract,
  model and engine groups 808 in 139 and the state group 142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE
  assertions), the Debug snapshots 0 JUCE assertions, pluginval strictness 10 SUCCESS, the fixture check passed
  (26 files, 4.87 MB of the 5 MB budget). The `ui` group: 34 tests, 195 checks on both platforms (R1 added 15).
- Inspector: 378 of 378 cases of `ui/inspector.json` within 1e-9, all 378 bit for bit (fmtPos 23, parsePos 20,
  parseBeats 25, parsePitch 43, rateId 14, fmtNum 8, laneValueFromText 245, each also checking laneText's string);
  the 30 M6 cases left for M6. 1009 of 1010 readouts typed back read as themselves, as in the prototype (tone 0.49
  shows "1.0 kHz" and reads back as "1000 Hz").
- Geometry: `roll/geometry.json` 34 of 34 cases bit for bit (rows 4, notes 26, hit grids 4). Gestures:
  `roll/gestures.json` 70 of 70 cases bit for bit, 664 steps replayed.
- Gestures at model level: 160 of 160 double-clicks made one note at the snapped start, max (1 beat, grid) long,
  one undo step each; 160 of 160 drags as computed (155 moved); 124 of 124 end-dot drags as computed (the 0.4 row
  dead zone). One undo step per finished gesture: 715 gestures, 329 changed the clip and pushed 1 step each, 386
  did not and pushed 0. A drag through the real processor: 3 moves, 3 clip versions sent to the audio thread, 0 undo
  steps until the release, then 1.
- Lane-mode lock: 500 seeded gestures in each of modes 2 to 8, 0 lock breaks in every mode; the gestures changed the
  active lane 265 to 365 times per mode and left it alone 221 to 321 times, every change 1 undo step and every other
  gesture 0 (in Pitch mode 15 more steps are slide changes, which Pitch mode allows).
- Keys: Keys off, 0 of 1016 key events kept and no shortcut ran; Keys on, Space kept 0 times with 8 modifier sets and
  every other key kept (1008 of 1008); 29 shortcut keys each ran once with Keys on and never with Keys off; through
  the roll, Keys off left the document alone and Keys on did each action with its undo step.
- No snapshot changed: R1 draws nothing (the editor, MainView and the painters are untouched), so the snapshot
  artifact holds M3's images.
- Run 44176806795 on 77f2ad7 (the view part), green on every job and step: Windows Release 1540 checks in 246
  tests, Linux Release 1532 in 246, the sanitizers' contract, model and engine groups 808 in 139 and the state group
  142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE assertions), the Debug snapshots 0 JUCE assertions,
  pluginval strictness 10 SUCCESS, the fixture check passed (26 files, 4.87 MB). The `ui` group: 35 tests, 200
  checks on both platforms. `rollViewScrollAndZoom`: 0 of 16 view states differ from sl-ui.js (within 1e-9 px) on
  both platforms; the float32 row height 17.99999976158142 comes back as 18.

## R2

Drawing the roll (plan 7.6 R2): the roll in place of M0's placeholder, its note layer cache, the ruler, the piano
keys, the playhead, zoom and scroll, the snapshots and the drawing cost. Branch `m4-roll`, from R1's b233192.

### Progress

- Done: `ui/roll/RollPainter`, `NoteLayerCache`, `Ruler`, `PianoStrip`, `RollView`; `RollGeometry::glideMap ()`;
  the roll in `MainView` (decision 5) and the display frames to it from `PluginEditor`; the tests
  `test_roll_snapshots` (20 pictures of the table, 6 for review), `test_roll_perf` and `test_roll_view`, all in
  group `ui`. First CI round: run 44191205369 on 2c8ae57.
- Done (second session): the Direct2D partial update made exact (the rings drawn as paths; see the decisions), and
  every picture of the snapshot artifact looked at and compared with the v4 screenshots (below). CI green on
  ffe53f9 (run 44235098470, figures below).
- In progress: CI on this commit (notes only), then the fast-forward of main.
- Next: R3, from the hand-off below.

### Files

- `Source/ui/roll/RollPainter`: sl-ui.js drawRoll (rows in the key's colours, the grid), ribbonPath and drawNote
  (the ribbons in lane order with the active lane last and stronger, the Travel middle line, the note line with
  its squiggle, the start ring and the end dot), drawLinks, drawHandles (the slide diamonds, the active lane's
  nodes and dashed line, the segment diamonds), drawGhost, the box and the readout, read line by line, every colour
  a theme token. `PaintState` is what one picture is drawn from; `noteBounds` and `overlayBounds` say where things
  draw, so the view repaints only those.
- `Source/ui/roll/NoteLayerCache`: every note's drawing in one image the size of the view, in device pixels.
  `noteChanges` compares the new state with what the image shows, note by note (its version pointer, whether it
  is selected or primary, its slide), and marks the old and new bounds of each note that changed; the next paint
  clears each marked rectangle and draws every note that reaches it again, in drawing order, the clear and the
  notes through one clip. The view's size, scroll and zoom, the theme, the mode, the tempo, the vibrato rate, the
  slide settings and the device scale are its Key: a change draws every note again, and so does a change of more
  than 48 notes or of the order of the notes that did not change.
- `Source/ui/roll/RollView`: the ruler with the zoom buttons at its right end, the keys, the canvas (rows, the
  cache's image, the overlay, the readout, the playhead) and two scroll bars. It owns R1's `RollGestures` and
  draws with its geometry. `refresh ()` is the only place it reads the document: it takes the clip, the
  selection, the mode, the instrument, the tempo, the readout and the box as one `PaintState`, and repaints what
  changed (the whole roll for a `ViewKey` change, else the cache's rectangles and the overlay where it was and is).
- `Source/ui/roll/Ruler`, `PianoStrip`: sl-ui.js drawRuler and drawPiano with their scroll; a key press plays
  through the CommandQueue (auditionOn at 0.8 while held, auditionOff on release).
- `MainView`: the `RollView` where the placeholder was; `setMode` and the saved mode reach the document through
  the gestures; `tick` hands it the host's tempo and refreshes it (15 Hz); its toasts go to the view's toast.
  `PluginEditor::takeDisplayFrame` hands it each new display frame (30 Hz): the playhead and the keys played live.

### Decisions and deviations

- **The zoom buttons sit at the right end of the ruler,** not in the top bar as in the prototype. The top bar has
  no room at 900 px (the M0 layout fills about 750 of its 860 px), R3 adds Undo, Redo and the roll's buttons
  there, and M4 touches MainView only to put the roll in. They keep the prototype's labels (H-, H+, V-, V+ with its
  minus sign) and tooltips, and the ruler's ticks stop before them.
- **The playhead is the display slot's (decision 5) and shows only while Cubase plays.** The prototype also shows
  it at its own start marker while stopped; in the plugin Cubase's transport has none to show. The ruler's
  playhead triangle (the prototype's start marker) follows the playhead the same way. The prototype moves its
  playhead every animation frame by extrapolating from the last display message; the plugin draws each frame's
  beat at 30 Hz, which is what the frames carry.
- **The keys played live light up** (the display frame's live voices that are not released), as the prototype
  lights the keys its MIDI input holds.
- **The slide link's mark is a vector chain,** not the link emoji (JUCE's fonts have none; R1 made the same call for
  the menu).
- **The ruler's ticks are rounded** to whole pixels like the grid's lines under them (the prototype rounds the grid
  and not the ticks, so a tick can sit half a pixel off its line).
- **The scroll is kept on whole pixels** in the view (zoom, wheel, scroll bars, the first view), as a browser keeps
  a scroller's: rows and grid lines stay sharp. RollGeometry itself still takes any scroll (R1's tests).
- **The first view and UIST (R1's question).** UIST's defaults (zoom 1, scroll 0) read as "never saved": the view
  centres on the notes then (the prototype's first open). Scroll 0 at zoom 1 is the top of the roll (C6), which
  nobody would save on purpose; the only cost is that such a project opens centred. Every zoom and scroll is
  written back to UIST. A note for the architect: a "view saved" flag in UIST would remove the guess.
- **The note layer is in device pixels, made with the renderer's preferred image type** (Direct2D in a Windows
  window, software in the tests) and drawn with no resampling, so it stays sharp at every UI Scale; the rows, the
  overlay and the playhead are drawn directly on every paint. A Direct2D image whose device went away is drawn
  again in full (JUCE's cached component images do the same).
- **The roll draws its rings as paths, because Direct2D's drawEllipse skips a ring whose circle misses the
  clip.** A partial update of the note layer must give a full redraw's picture with every renderer. JUCE's software
  renderer did from the start. Direct2D (the renderer of a Windows window, and of the Windows test runner's images)
  did not: runs 44195077235 and 44200621878 found 111 pixels along the edges of the redrawn rectangles (the layer
  itself, up to 255 levels), while two full Direct2D redraws agreed. The cause is in JUCE 8.0.15:
  `Direct2DGraphicsContext::drawEllipse` tests only the ellipse's own area against the clip, so a start ring or end
  dot whose circle lay just outside a rectangle was skipped, although its 2 px outline reached in (those pixels
  were the start ring's colour in the full redraw and the ribbons' under it in the update). The rings (start rings,
  end dots, node handles) are now filled paths, the very path JUCE's software renderer fills for drawEllipse, so
  the software pictures are unchanged. Two earlier guesses did not fix it: clearing through the notes' clip (kept:
  the pixels cleared are exactly the pixels drawn again), and drawing each rectangle as a patch at an offset
  (dropped: it rounded the coordinates differently inside, 30 pixels up to 6 levels with both renderers). The notes
  keep the full redraw's transform, and every note within 2 px of a rectangle is drawn again. A row the clip leaves
  out keeps its pixels, which are right: a rectangle's edges hold only notes that did not change (the changed notes'
  bounds carry 14 px of room). The perf test gates the picture with both renderers.
- **The same Direct2D trap, for later parts.** JUCE's Direct2D context also skips `drawLine` with a thickness by the
  line's own box plus 1 px, and `drawRoundedRectangle` and `drawEllipse` outlines by their area. In the roll (cached
  or repainted in parts) every ellipse or rounded outline, and every line thicker than 2 px, goes through a path
  (`strokePath` or `fillPath`); fills and `drawRect` are safe. The slide link's 1.5 px dashes are inside the 1 px
  margin.
- **A note's bounds** are its line, ribbons and dots plus 14 px: JUCE mitres a stroke's joints up to 3 widths (the
  selected line is 3.2 px), and the dots reach 7 px past the line.
- **"Moving one note repaints at most 3 notes' bounds"** is measured on the note layer: a moved note marks its old
  and new bounds (one rectangle when they overlap), and a note whose slide changed marks its bounds (old and new in
  one). Moving a note that another note slides into (and that slides into none) marks exactly 3; the roll repaints
  those and the slide link's old and new place. Inside them every note they touch is drawn again (clipped), so a
  dense clip draws more note drawings than 3. A move that re-voices several automatic slides (a note in a legato
  chord) marks more notes, as it must: their lines changed.
- **The roll's snapshots fold the tool tabs,** as the prototype does when it first opens on a laptop, so the roll
  has the room; the editor's own snapshots (test_editor) keep the tab open and show the roll smaller. The Build
  pictures keep the first view; Pitch, Tone and Sample scroll to their selected notes.
- **The pictures, looked at against the v4 screenshots** (fresh1100.png, glide.png, roll-play.png; every picture of
  the artifact, the 26 roll pictures and the editor's 21, at 75, 100, 150 and 200 % and 900 x 560). The roll draws
  what the prototype draws in M4's part: the rows in the key's colours, the grid, the ribbons in the lane colours
  over every note (the active lane's in front, with its line), the vibrato squiggle, the start rings and end dots,
  the active lane's nodes on a dashed line, the segment diamonds, the slide diamonds (filled at the slide's start,
  hollow at its middle), the dashed slide link with its chain mark, the playhead with its ruler triangle, a key lit
  on the piano, the readout, the dashed box and the ghost. Nothing clipped, overlapping, blurred or off-palette was
  found. What the v4 screenshots show besides, on purpose not here: the chevrons under each note (H4) and the faint
  waveform above it (U7), the dot on notes with their own settings (S7) and the warning for a slide whose target
  starts inside its source. They are sl-ui-panels.js `decorate`, which the plan gives to M6 (chevrons and per-note
  thumbnails) and to the Note settings and Slides work.
- **A note for the architect: the roll is small in a new instance.** At 1100 x 680 with the Sample tab open (a new
  instance's UIST), M0's layout leaves the roll 158 px tall (a 128 px canvas, 9 rows). The prototype folds its tabs
  on a first open when the window is under 900 px high (sl-ui.js init), which gives the roll about 360 px. UIST's
  `folded` cannot tell a new instance from a project saved unfolded (as with the view), and its default is the state
  format's, so R2 leaves it: folding a new instance needs the architect (a UIST default, or a flag).
- **Not R2's:** the hint bar's texts and the selection row's buttons still say "in M4" (R3); the mouse on the roll
  (R3); the Grid menu, Freehand, Multi and Slide into buttons (R3 sets `settings ()`); hover ghosts (M5 calls
  `setGhost`); Sample mode's thumbnails (M6).
- **The JUCE source in the scratchpad** (`juce-8.0.15/modules`) had no files, only its git pack. The tree was
  checked out from that pack into `scratchpad/juce-x` (JUCE 8.0.15's own commit), with no download.

### Hand-off for R3 (the live roll)

- **The mouse.** `RollView::Canvas` (private, in RollView.cpp) is the component under the mouse; it has no mouse
  handlers yet. Give it mouseDown, mouseDrag, mouseUp (and a lost capture) that call the view, which turns
  `e.position` into roll pixels with `getGeometry ().toRoll (x, y)` and calls `getGestures ().pointerDown (x, y,
  { shift, ctrl-or-cmd }, Time::getMillisecondCounterHiRes ())`, `pointerMove`, `pointerUp`; a right press calls
  `contextMenu`. **Call `refresh ()` after every one of them**: refresh reads the document, the readout and the box,
  and repaints only what changed (the moved notes' bounds, the handles, the readout, the links). The wheel is done.
- **Commands and keys.** The buttons and `KeyRouter`'s actions call the gestures' commands (`setMode` goes through
  `MainView::setMode`, which already hands the mode to the roll), then `refresh ()`. Undo and Redo are
  `proc.undo ()` / `proc.redo ()`, then `refresh ()`.
- **Settings.** `getGestures ().settings ()`: `snap` (the Grid menu; write UIST `gridBeats` too; the grid redraws on
  the next refresh), `free` (`toggleFreehand ()`), `multi`. `inst` and `bpm` are refreshed by the view; leave them.
- **Toasts.** The gestures' toasts already reach MainView's toast through `onToast`.
- **The hint bar and the selection row** still carry M0's "in M4" texts; their texts are sl-ui.js `modeHint` and
  `updateSelInfo`.
- **What the view draws by itself:** the readout and the box come from the gestures at each refresh; the slide
  diamonds, nodes and segment diamonds from the document's selection, primary note and inspector node; M5 calls
  `setGhost (clip)` for its hover previews.
- **Tests.** `RollView` runs headless (a processor, a Theme, `setBounds`); `takeRepaintLog ()` says what a refresh
  asked to repaint, and `getCache ().lastRender ()` what the next paint drew again. `test_roll_perf`'s
  `moveOneNote` shows how to check a gesture's repaint against a full repaint pixel for pixel.
- **Drawing in the roll:** draw every ellipse or rounded outline, and every line thicker than 2 px, as a path
  (`RollPainter`'s `ring` and `dashed` helpers). Direct2D's own `drawEllipse`, `drawRoundedRectangle` and thick
  `drawLine` are skipped when their shape misses the clip, which tears outlines at the edges of a partial repaint.

### CI results

- Run 44235098470 on ffe53f9, green on every job and step: Windows Release 1610 checks in 257 tests (main had 1540
  in 246), Linux Release 1602 in 257, the sanitizers' contract, model and engine groups 808 in 139 and the state
  group 142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE assertions), the Debug snapshots 0 JUCE
  assertions, pluginval strictness 10 SUCCESS, the fixture check passed (26 files, 4.87 MB of the 5 MB budget; R2
  adds no fixtures). The `ui` group: 46 tests, 270 checks on both platforms (R2 added 11 tests and 70 checks).
- Drawing cost, a full repaint of the 512-note clip at 1100 x 680 with JUCE's software renderer: median 33.3 ms of
  5 on Windows (min 32.0) and 19.1 ms on Linux, against the 50 ms target (gate 250 ms); the first paint, lines made
  too, 40.8 ms and 20.5 ms. Direct2D (the Windows window's renderer): median 27.6 ms (min 26.9). A repaint with
  nothing changed draws 0 notes (4.2 ms and 1.8 ms: the rows, one image, the handles).
- Moving one note (a drag 2 beats later and 3 rows up, 1 undo step): the note layer marks 3 notes' bounds (gate: at
  most 3), 2 notes' drawings changed; the update draws 89 clipped note drawings in 3 rectangles (17232 device
  pixels) in 1.48 ms (Windows, software), 1.49 ms (Linux) and 6.63 ms (Direct2D); the roll asks to repaint 18448
  pixels, 2.7 % of the canvas. Painted over the picture before, the area gives the full repaint's picture: 0
  pixels differ with the software renderer and 0 with Direct2D (the note layer itself: 0 and 0; it was 111 with
  Direct2D before the rings became paths). Painting that area one rectangle at a time, which a window does not do:
  0 pixels with Direct2D, 2 pixels by 1 level with the software renderer (printed only).
- Snapshots: the table's 20 pictures (dark and light x Build, Pitch, Tone x 75 / 100 / 200 %, and 900 x 560) and 6
  for review, from the Release and the Debug runner. Mean luminance 0.177 to 0.213 dark, 0.888 to 0.917 light; the
  900 x 560 roll's canvas is 826 x 203 with 17 notes in view; the Tone colour at full strength 1143 (dark) and
  1100 (light) pixels in Tone mode, 0 in Build; the slide diamonds 131 and 146 link-colour pixels; while Cubase
  plays, the playhead is at x 404 with 708 pixels of #FF7A00 (dark) and #D86700 (light), none while stopped; the
  box outline 147 pixels.
- The view: in a new instance (the Sample tab open) the roll fills the placeholder's 1076 x 158 (a 1026 x 128
  canvas) and the first view centres C4 (scroll 279), 11 of the 26 notes in view; a piano key sends 1 auditionOn at
  0.8 and 1 auditionOff; H+ makes beats 83.20 px and V+ rows 18 px; a wheel notch scrolls 100 px; Ctrl + a notch
  zooms 1.15 times with the beat under the pointer kept; a playhead move repaints a 5 px strip; selecting a note
  repaints 2.9 % of the roll and draws 6 notes again in 1 rectangle.
