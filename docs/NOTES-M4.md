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

## R3

The live roll (plan 7.6 R3): the mouse and the keys wired to R1's gestures, the inspector panel (T6), Undo and Redo
in the top bar, the hint bar's texts, the toast in its own file, and the selection row. Branch `m4-roll`, from R2's
d3424c6 (main).

### Progress

- Done: `RollView::Canvas` mouse handlers (press, drag, release, right press) and the
  commands; the right-click menu as a JUCE menu; `ui/roll/Inspector`; `ui/HintBar` with `modeHint`; `ui/Toast`;
  `MainView`'s top bar (Undo, Redo, Grid), selection row (wired buttons, Freehand, Slide into, Multi) and the
  inspector's line; the editor's keys through `KeyRouter`; `InspectorText::parseVelocity`; the test file
  `Tests/ui/test_roll_live.cpp` (gestures through the canvas, Slide into, the menu, the selection row, the keys, the
  inspector, the hints, and 8 pictures).
- Done: CI green on dd4da74 (run 44245520234, figures below), every picture of its artifact looked at; the
  inspector's boxes now give way on a short line (900 px cut the value box); the Cubase checklist's M4 section; the
  M4 summary.
- Done: CI green on 54e5481 (run 44248819010: the boxes that give way; figures below), its pictures looked at
  (at 900 px the inspector now shows "0 semitones" whole).
- In progress: CI on this commit (these notes, and Escape closing an open right-click menu), then the fast-forward
  of main.

### Files

- `Source/ui/roll/RollView`: the canvas's mouseDown, mouseDrag and mouseUp call `press`, `dragTo`, `release` and
  `rightPress`, which turn the canvas's pixels into roll pixels (`toRoll`) and call R1's `pointerDown`,
  `pointerMove`, `pointerUp` and `contextMenu`, then `refresh` and `onEdit`. The commands the buttons, the keys and
  the menu call: `undo`, `redo`, `selectAll`, `duplicate`, `deleteSelection`, `clearSelection`, `clearAll`,
  `copyLanes`, `pasteLanes`, `toggleFreehand`, `armLink`, `setMulti`, `setGrid`. The right-click menu
  (`showMenu`, `chooseMenuItem`, `getMenu`, `setMenusShown`) with two custom menu items, `MenuHeader` and `MenuRow`.
- `Source/ui/roll/Inspector`: `InspectorField` (a caption, a box that becomes a text box on a click, a unit) and
  `Inspector` (sl-ui-panels.js buildInspector, syncInspector, activeNodeCtx, fitVal and the six commit functions).
- `Source/ui/roll/InspectorText`: `parseVelocity` (commitVel's parseFloat (clean (text)), rounded, 1 to 127).
- `Source/ui/HintBar`: `modeHint` (sl-ui.js modeHint and SUB_HINT) and the bar: the hint on the left, "Try this:" on
  the right, and the whole hint over the bar while the mouse is on a hint that is cut (shell.html #hintHover).
- `Source/ui/Toast`: the toast, moved out of `MainView` (its `ToastView`); `MainView` still places it.
- `Source/ui/roll/PianoStrip`: the pointing hand over the keys (shell.html #pianoWrap).
- `Source/ui/MainView`: the top bar's Undo and Redo (`HistoryButton`, the prototype's two arrows drawn as paths) and
  the Grid menu; the selection row's buttons wired, the roll's Freehand, Slide into and Multi switches at its right
  end, the inspector's line under it; the hint bar; `syncRollControls`; `handleKey` (the KeyRouter's actions).
- `Source/PluginEditor.cpp`: `keyPressed` hands every key to `MainView::handleKey` after recording it.
- `Tests/ui/test_roll_live.cpp`: 8 tests and 8 pictures (below).
- `docs/CUBASE-CHECKLIST.md`: the section "M4: the piano roll".

### Decisions and deviations

- **The mouse reaches the gestures with the event's own time.** `MouseEvent::eventTime` (milliseconds since 1970,
  as the prototype's `Date.now ()`) is the time R1's double-click rule (320 ms, 22 px, the same kind of thing) and
  the menu's Random seed read. The tests set it, so a double-click and two separate clicks are exact. The middle
  button does nothing and a right press opens the menu without a gesture, as in sl-ui.js rollDown and its
  contextmenu handler. A press while a gesture is still held ends that gesture first.
- **A lost capture ends the gesture.** JUCE turns a lost mouse capture (Windows' WM_CAPTURECHANGED) into mouseUp, so
  the gesture ends there with its undo step. The roll also ends a held gesture when it is hidden (the Diagnostics
  tab takes its room) and when the window closes, as a release would.
- **Cursors.** The prototype's roll has no cursor of its own, so the canvas keeps the arrow; the piano keys show
  the pointing hand (shell.html #pianoWrap) and the inspector's boxes the text cursor (input[type=text]). The ruler
  keeps the arrow: the prototype's ruler sets where its own Play starts, which in the plugin is Cubase's.
  Buttons keep M0's arrow.
- **The right-click menu is a JUCE menu built from R1's items.** Headers are small capitals (a lane mode's header in
  its lane's colour), drawn by a custom item. The Each note / Across row is a custom item: a click changes the
  scope and the menu stays open with the other button lit (the prototype's keep); the scope goes to UIST. Paste
  lanes is greyed until something is copied. A press anywhere else closes the menu, as JUCE does.
- **Where the new controls sit (a layout change).** The top bar has room for Undo, Redo and the Grid at 900 px, not
  for Freehand, Slide into and Multi too (the prototype's top bar wraps onto two rows at 1100 px). So Undo and Redo
  come right after the wordmark (decision 3) and the Grid after the host's tempo, as in the prototype (Tempo, Bars,
  Grid); "Test build" at the right end shows only when it fits. Freehand, Slide into and Multi act on the selected
  notes, so they sit at the right end of the selection row, which keeps them still when Clear all asks for its
  second click.
- **The inspector has a line of its own, always there.** At 1100 px the inspector with a node (about 850 px) does
  not fit beside the selection row's buttons. The prototype hides the inspector while nothing is selected and lets
  its rows wrap, so selecting a note moves the roll down under the mouse (the prototype then scrolls the roll back);
  here the line is always there, so a press never moves the roll. With nothing selected it says how to use it. The
  32 px come from the pane and the roll: at 1100 x 680 with the Sample tab open M0's pane rule now cuts the pane a
  row earlier (it scrolls), and the roll gets 204 px (it had 158).
- **Typing needs the keyboard, with Keys off too.** A click on an inspector box opens a text box that takes the
  keyboard (JUCE's editable label); Enter keeps the value, Escape puts the old one back, a click anywhere else keeps
  it (that first click only ends the typing). The boxes do not want the keyboard at rest, so M0's focus rules and
  their tests hold. With Keys off, keys that reach the window after typing still go on to Cubase: the editor keeps
  none of them.
- **The hint bar's texts are sl-ui.js modeHint, less what M5 builds.** Build's "drag on empty space = box select
  (Shift adds, Ctrl toggles)" is left out (box selection is M5's), and the lane modes' "▾ on the lane button =
  shapes for every selected note" says "right-click = shapes for every selected note" (the lane buttons' menus are
  M5's; the right-click menu holds the shapes now). The Nodes and Scroll lines are M5's and not built. The test
  compares every mode's line with the prototype's, run in node with those two changes. A hint cut short shows whole
  over the bar on hover, as the prototype does on short windows (no shadow).
- **Undo and Redo** toast "Undo: Move notes" or "Nothing to undo" (sl-ui.js undo, redo); their tooltips name the
  step. Undo, Redo, Duplicate, Delete, Paste lanes and Clear all do nothing while a drag is held.
- **The Keys button toasts what the keys do** (sl-ui.js), without the prototype's A to K and Z / X (the live
  keyboard, M8).
- **The selection text keeps the room of "4096 notes selected"**, so the buttons after it never move.
- **Escape closes an open right-click menu** as it clears the selection (sl-ui.js: Escape calls closeMenu).
- **Notes snap to the grid; to the scale only with Scale lock**, which has no control until M7 (Key and Scale): the
  gestures already snap to the scale when the instrument's scale lock is on.
- **The pictures, looked at** (run 44245520234: all 55 pictures of the artifact, the 8 new ones and every M0, M2
  and R2 picture the new rows changed, against fresh1100.png, ns.png and tools.png). The top bar shows Undo and
  Redo as the prototype's two arrows (greyed while there is nothing to undo or redo), the Grid after the tempo, and
  "Test build" only where it fits (not at 900 px). The selection row shows the count, its four buttons (Duplicate
  and Delete greyed with nothing selected), and Freehand, Slide into… and Multi at its right end after a line,
  Freehand in the accent while on. The inspector's line shows START, LENGTH beats, PITCH and VELOCITY, and in Pitch
  NODE (PITCH) AT beats in VALUE, as ns.png's inspector row; with nothing selected its sentence. The hint bar's
  line is cut with an ellipsis, and the hover shows all of it over the bar with an accent outline; the hint's
  signs (the dot, the filled and hollow diamonds, the node dot) draw through JUCE's font fallback. Found and fixed:
  at 900 px the inspector's value box was cut ("0 semit..."); the small boxes now give way first. Left as it is:
  at 900 x 560 with the Sample tab open, M0's pane rule now gives the pane 64 px (its first row and the top of the
  knob row, scrolling) and the roll 137 px (it had 116 and 117); folding the tabs on a small window, as the
  prototype does, would be the architect's call (R2's note).

### CI results

- Run 44245520234 on dd4da74, green on every job and step: Windows Release 1646 checks in 265 tests (main had 1610
  in 257), Linux Release 1638 in 265, the sanitizers' contract, model and engine groups 808 in 139 and the state
  group 142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE assertions), the Debug snapshots 0 JUCE
  assertions, pluginval strictness 10 SUCCESS, the fixture check passed (26 files, 4.87 MB of the 5 MB budget; R3
  adds no fixtures). The `ui` group: 54 tests, 306 checks on both platforms (R3 added 8 tests and 36 checks).
- The gestures through the canvas's own mouse handlers: a double-click on empty space at beat 1.05, C3 made one
  note at beat 1, a beat long, "Add note" (the first click alone made nothing); a drag 2 beats later and 3 rows up
  moved it to beat 3, D#3, "Move notes" (mid-drag it was at beat 2 with no new undo step, and a readout showed);
  the end dot dragged a beat right made it 2 beats long; in Pitch a double-click on the line added a node at t 0.25
  (the inspector: Node (Pitch) at 0.5 beats in); the node dragged up 2.1 rows landed on +2 semitones (the readout
  and the inspector both said "+2 semitones"); the segment's diamond dragged 30 px bent it to 50 %; a double-click
  on the node deleted it; Freehand on Volume turned a painted ramp into 2 smooth nodes, 0.200 to 0.900. 8 gestures,
  8 undo steps: Undo landed on the clip before each one (8 of 8) and Redo on the clip after it (8 of 8), each with
  its toast; a click that moved nothing added no step (the Redo was still there).
- The menu: a right press on a note selected it and listed Build's three items, Copy lanes, Paste lanes (greyed),
  Duplicate and Delete; Delete was one step; New note here on empty space made a beat-long note at beat 6.25, F3;
  in Volume the header "Volume shape" was in the lane's colour, Across kept the menu open and lit (UIST scope 1),
  and Ramp up changed Volume ("Ramp up (Volume)").
- The selection row: "No notes selected" with Duplicate and Delete greyed, "3 notes selected" after Select all,
  Duplicate and Delete one step each, Clear all on its second click, Freehand refused in Build and lit in Vibrato,
  Multi selecting two notes with two clicks, the Grid at 1/8 moving a 0.3-beat drag half a beat (UIST 0.5).
- The keys through the editor: with Keys off Ctrl+A, 3 and Delete changed nothing and went on to Cubase; with Keys
  on 3 picked Volume, Ctrl+A selected both notes, Delete removed them, Ctrl+Z and Ctrl+Shift+Z undid and redid with
  their toasts, Ctrl+Y said "Nothing to redo", B armed Slide into, Escape cleared the selection; Space went on to
  Cubase.
- Slide into: Shift+click linked C3 into D3 ("C3 now slides into D3"); the button lit, the hint said SLIDE INTO,
  and the next click linked D3 into E3.
- The inspector: typed 1.2.3, 1/8, D3 and 64 gave start 1.5, length 0.5, pitch 62 and velocity 64/127, one step
  each; "soon" toasted how to type a position and changed nothing; the value shown typed again changed nothing;
  with two notes E3 moved both up 2 and 127 set both; a node's VALUE +3 gave +3 semitones and AT 0.375 moved it to
  t 0.75; "loud" toasted "Type semitones, for example +3 or -2.5"; the first node showed "start", greyed.
- Velocity reading: 20 of 20 texts read as the prototype's parseFloat (clean (text)) reads them. Hints: 15 of 15
  lines equal sl-ui.js modeHint's (run in node, with the two M5 changes); the bar showed each mode's line (8 of 8)
  and Build's showed whole on hover.
- The roll view at 1100 x 680 with the Sample tab open: 1076 x 204 (canvas 1026 x 174; R2 had 1076 x 158), the
  first view centres C4 with 13 of the 26 notes in view; at 900 x 560 with the tabs folded the canvas is 826 x 171
  (R2 had 826 x 203).
- Run 44248819010 on 54e5481 (the inspector's boxes giving way), green on every job and step: Windows Release 1646
  checks in 265 tests, Linux Release 1638 in 265, the sanitizers' groups 808 in 139 and 142 in 20, the Debug-CRT
  allocation run 38 in 7 (0 JUCE assertions), the Debug snapshots 0 JUCE assertions, pluginval strictness 10
  SUCCESS, the fixture check passed (26 files, 4.87 MB). Full repaint 32.6 ms on Windows and 19.1 ms on Linux.
- Drawing cost (R2's tests, unchanged code): a full repaint of the 512-note clip 31.3 ms on Windows and 20.2 ms on
  Linux (software, target 50 ms), Direct2D 28.1 ms; moving one note marks 3 notes' bounds and its partial repaint
  equals a full one with both renderers (0 pixels differ).

## M4 summary

M4 (the roll UI: Build mode and lane modes) is built in three parts on `m4-roll`, each fast-forwarded to main when
green: R1 the headless half, R2 the drawing, R3 the live roll. What Kalan gets: the piano roll with the ruler, the
piano keys (click to hear), the grid and the playhead (#FF7A00, from the engine's display frames); the notes as
strands with their lanes' ribbons; Build mode (double-click to make a note, drag to move it with grid and scale
snap, the end dots with the 0.4-row dead zone, the slide diamonds, Shift+click = slide into, the right-click menu);
the lane modes 2 to 7 (and 8's four lanes) with the lock rule; node editing (drag, double-click to add or delete,
the segments' bend diamonds, the live readout in real units, pitch nodes on semitones unless Shift); Freehand (RDP
0.012); the Keys switch with the shortcuts only while it is on; Undo and Redo buttons; the inspector (T6); the
hint bar; the note layer's image cache.

- **The plan's M4 table, where each row is checked.** Gestures (model level): R1's `test_roll_gestures` (160
  double-clicks, 160 drags, 124 end-dot drags, 715 gestures: 329 changed the clip and pushed 1 step each, 386 did
  not and pushed 0), and R3's `rollLiveGestures` through the real editor's mouse handlers. Lane-mode lock: R1's
  `test_mode_lock_ui` (500 seeded gestures in each of modes 2 to 8, 0 lock breaks). Keys: R1's `test_keys` (Keys
  off: 0 of 1016 kept; Keys on: Space never kept) and R3's `rollLiveKeys` through the editor. Inspector parsing:
  R1's `test_inspector` (378 of 378 fixture cases bit for bit). Snapshots: R2's 20 pictures of the table plus 6,
  and R3's 8 of the live rows. Drawing cost: R2's `test_roll_perf` (31.3 ms on Windows against 50 ms; moving one
  note marks 3 notes' bounds, gated).
- **Deviations from the plan, all in the parts' notes:** the zoom buttons at the right end of the ruler (R2); the
  inspector on a line of its own and the roll's switches on the selection row (R3); the hint bar without M5's box
  select and lane menus (R3); the playhead only while Cubase plays (R2); the menu's slide item without the link
  emoji (R1); the readouts counting the clip's beats per bar (R1).
- **For the architect:** UIST cannot tell "never saved" from "saved at zoom 1, scroll 0", nor a new instance from a
  project saved unfolded (R1, R2); a small window (900 x 560) with a tab open leaves the roll 137 px, and the
  prototype folds its tabs on a first open on a laptop (R2, R3).
- **Handed to M5:** box selection (`RollGestures::box ()` already tracks the rectangle and R2 draws it; `applyBox`
  is to be wired where the gestures' box ends), the Nodes switch (`nodeSel`; the mode bar's Nodes button is greyed),
  the lane buttons' menus (their arrows toast "arrive in M5"), hover ghosts (`RollView::setGhost` draws them), the
  selection bar's tools, the Copy / Paste tab (`RollGestures::copyMask`) and the Slides tab. When M5 adds box
  selection and the lane menus, the hint bar's two lines go back to the prototype's words (`modeHint`, and the
  test's expected strings). The inspector's group of picked nodes (sl-ui-panels.js `activeNodeCtx` with two or
  more picks) comes with the Nodes switch.
- **Handed to M6:** Sample mode's chevrons and per-note thumbnails, the Note settings tab, "Try this" (the hint
  bar's right-hand text is still M0's).

## Fix

The M4 fix pass (plan 7.6: review, then a fix pass): three reviews of main at c96b7bb (gesture parity, the UI against
the prototype, tests and real time) reported 18 findings. Branch `m4-fixes`, from main c96b7bb. Each finding was
checked against the code, the JUCE 8.0.15 source and the prototype before it was fixed or set aside.

### Progress

- Done: F1, R4T-2, F5, then the keys and the menu (R4T-1 with F3 and UI4-1, UI4-2, F2, F4, UI4-4), each with its
  tests (commits df70b4a, 51e4843, cf41fd3, d02ae31); CI green on d02ae31 (run 44283180626, figures below); the
  four pictures of the open menu looked at; the Cubase checklist (UI4-5, UI4-8 and the new keys).
- In progress: CI on this commit (these notes, the checklist, two clearer test messages), then the fast-forward of
  main and CI on main.

### The findings

- **F1 (high), fixed.** Confirmed in the code and in node (sl-ui.js and sl-model.js unmodified): a double-click on a
  lane holding 128 nodes inserts a 129th, setLaneNodes caps the list (RDP), and the drag kept the inserted place.
  On a ramp with 0.02 steps the lane falls to 91 nodes with the new one at 83 while the drag held 115: the first
  move read and wrote past the vector. On a 0.1 / 0.9 line the cap drops the new node and the drag moved another
  one. `addNodeAt` now finds the new node in the stored lane (by its exact values: the cap keeps a node as it is)
  and drags it there, or starts no node drag and sets no inspector node when it is gone, with a toast; the release
  is still one "Add node" step when the lane changed, as in the prototype. A refused edit starts no drag. The node
  drag also stops when its place is not in the list. Test `rollAddNodeOnFullLane`: three cases whose sizes and
  places came from the prototype's model; before the fix the first stops at the out-of-range place and the other
  two move the wrong node.
- **F2 (medium), fixed.** Confirmed: TextEditor::keyPressed returns false for Ctrl+Z and Ctrl+Y when the box has no
  typing to undo or redo (Label::showEditor's setText clears that history) and for Ctrl+D (no mapping); the key then
  goes up the parents to the editor, whose KeyRouter ran the roll's Undo, Redo or Duplicate. `MainView::handleKey`
  now runs nothing while an inspector box is open, Keys on or off. Test `rollLiveTypingKeepsKeys` sends the keys
  as JUCE's window does: the text box first, then each parent.
- **F3 (medium), UI4-1 (medium), R4T-1 (high): one cause, fixed.** Confirmed in juce_PopupMenu.cpp: the menu window
  enters a modal state, ComponentPeer::getTargetForKeyPress gives a blocked component's keys to it, and
  MenuWindow::keyPressed treats Space as Return (it runs the item under the mouse). While any JUCE component is
  modal, HWNDComponentPeer::doKeyDown also reports every key as used, so no key reaches Cubase. R4T-1's first remedy
  (give the keyboard away while the menu is open) would not work: with no focused component the key goes to the
  peer's own component, which the menu blocks too. The right-click menu is now a panel of StrandLock's own on the
  page (`RollView::MenuPanel`), as the prototype's #menu: nothing is modal, Space goes on to Cubase, and the other
  shortcuts work as they do without it. The JUCE drop-down lists (the Grid, UI Scale, the sound and preset menus)
  get a key listener from the look and feel (`StrandLookAndFeel::preparePopupMenuWindow`): Space closes the list and
  picks nothing. Tests `rollLiveMenuTakesNoKeys` and `rollLiveListsSpaceGuard`; four pictures of the open menu.
- **F4 (low), fixed.** Confirmed: JUCE hands every repeat of a held key over as a press, and armLink toggles. The
  KeyRouter runs only the first B until a key goes up (the editor's keyStateChanged) or another key goes down. The
  other repeats change nothing, as the finding says, so only B is guarded. Tests `keysHeldBTogglesOnce` and
  `rollLiveHeldB` (8 presses: toggled each time, Slide into would end off).
- **F5 (low), fixed.** Confirmed: the Start box used 4 beats per bar while the ruler and the readout count the
  clip's. `fmtPos` and `parsePos` take the beats per bar (4 by default, so the fixture's cases are the prototype's);
  the inspector passes the clip's. The reviewer suggested a fixture case with 3 beats per bar, but the fixtures are
  the prototype's own output and its bars always hold 4 beats, so the cases are worked out by hand in
  `inspectorPositionsCountTheClipsBars` instead (and `rollLiveStartCountsTheClipsBars` through the editor). The
  Length box's bar forms ("1 bar", "1.0.0.0") stay 4 beats: they are the model's rate `1bar`, which is 4 beats.
- **UI4-2 (low), partly rejected.** The finding says sl-ui.js clears the selection when Escape closes the menu. It
  does not: the first line of its key handler is `if (menuOpen() && e.key === 'Escape') { closeMenu(); ...; return; }`,
  so Escape with the menu open only closes it; the selection goes on the next Escape. What a JUCE menu did with
  Escape was therefore the prototype's behaviour. The finding is right that R3's note "Escape closes an open
  right-click menu as it clears the selection" was wrong, and that the branch in `RollView::clearSelection` could
  not be reached from a key. With the panel, `MainView::handleKey` follows the prototype's rule (Keys on: Escape
  closes the menu and keeps the selection), and R3's note is corrected here.
- **UI4-3 (low), kept, and given to the architect.** Confirmed: with Keys off a click on an inspector box gives
  StrandLock's window the Windows keyboard focus (Label::showEditor), and nothing hands it back after Enter. Cubase
  then gets keys only through JUCE's forwarding (PostMessage to the host's window), which no Cubase result has
  confirmed yet; checklist step 10 asks. Handing the focus back needs Windows calls outside JUCE and cannot be
  tested here, so it waits for Kalan's answer. Added to the deviations below.
- **UI4-4 (low), fixed.** Confirmed: the shortcuts edited the hidden roll while the Diagnostics tab was open. Now
  they run nothing there (with Keys on the keys are still kept, as M0's rule says, and listed on the page). Test
  `rollLiveKeysWithRollHidden`.
- **UI4-5 (low), fixed.** The checklist's earlier parts now say which lines were true for the M0 to M3 builds only
  (the top bar, the mode bar, the tabs' place, the empty roll, the notes not shown, Undo).
- **UI4-6 (low), deferred to the architect.** Confirmed: the zoom buttons' box (122 px) covers the ruler's last bar
  numbers; at 900 x 560 the default clip's bar 4 has no number. No cheap fix is good: a number moved left of the box
  would sit beside another bar's beat tick, and four buttons cannot be much narrower. Where the buttons go is a
  layout call (R2 put them there because the top bar has no room at 900 px).
- **UI4-7 (low), deferred to the architect.** Confirmed (the pane cut through the knob row at 900 x 560 with a tab
  open). It is R2's and R3's open question: fold the tabs on a small window, as sl-ui.js init does, or a UIST default.
- **UI4-8 (low), fixed in the checklist.** Step 3 now says that a drag on empty space draws a box that selects
  nothing until M5. R2 draws the box for M5, so it stays.
- **UI4-9 (low), handed to M6** (below).
- **R4T-2 (medium), fixed.** Confirmed: `deliverSoundLoads` pushed the sound's step with `clipDoc.current ()`, the
  drag's unfinished clip while a drag is held. The step now keeps the clip of the history's last finished edit.
  Every other change to the document outside an undo step is a gesture's live step, so the history's clip is the
  right one in every case. Test `rollSoundLoadDuringDrag`: before the fix, Undo of the drag landed on its middle.
- **R4T-3 (low), deferred to the architect.** Confirmed by reading: `computeGlides` compares every pair of notes
  and runs on every drag step and twice per compile. The model is not M4's (plan 7.6); see the list below.
- **R4T-4 (low), deferred.** Confirmed: the note layer's key and the line cache's key hold the tempo and the
  vibrato rate, so a tempo ramp redraws every note at each refresh. A fix keys the tempo per note (only notes with a
  vibrato that is not flat, or with automatic slides, depend on it) across R1's line cache and R2's note layer; that
  is more than a fix pass should change without a gate. Rounding the drawing tempo was not taken: the readouts and
  the hit tests read the same geometry. See the list below.

### Decisions and deviations

- **The right-click menu is StrandLock's own panel, not a JUCE menu (R3 used a JUCE menu).** It is drawn as
  shell.html's #menu (the menu colour, a 10 px corner, the line around it, a drop shadow, 6 px of padding, items
  in 13 px bold that light on hover, headers in small capitals, lines, the row of two buttons with the lit one in
  the accent), opens at the click and stays inside the page, and scrolls with the wheel when taller than the page.
  It closes when an item without "keep" runs, on a press anywhere in StrandLock's windows (Desktop's global mouse
  listener; the press still does what it does there, as in the prototype), on Escape with Keys on, and when the
  roll is hidden. It takes no key. Items are 28 px high, not the prototype's 31: the longest menu (Travel, 19
  entries) then fits the smallest window (525 of 544 px).
- **Space with a JUCE drop-down list open closes the list** and does not reach Cubase (JUCE holds every key while a
  list is open). Before, it picked the item under the mouse: a sound, a preset, a UI Scale.
- **Escape with the menu open closes only the menu** (Keys on), as sl-ui.js does. With Keys off the keys are
  Cubase's, so a click closes the menu.
- **While an inspector box is typed in, the keys are the box's**: no shortcut runs and no key goes on to Cubase,
  with Keys on or off. A JUCE text box holds a modal state while open, so on Windows no key reached Cubase then
  anyway; now no shortcut runs either. (Space types a space, as before.)
- **No shortcut runs while the Diagnostics tab hides the roll.**
- **A double-click on a full lane** (128 nodes) toasts "A lane holds at most 128 nodes, so this one has no room for
  another" when the cap drops the new node; the prototype says nothing (its drag then throws or moves another
  node).
- **R3's note on Escape is corrected** (UI4-2 above): Escape with the menu open closes it and keeps the selection.
- **Additions to the M4 summary's deviations:** the right-click menu as a panel of StrandLock's own; Space closing
  an open drop-down list; with Keys off, typing in an inspector box takes the Windows keyboard focus from Cubase
  until Kalan clicks in Cubase again (UI4-3; JUCE forwards the keys meanwhile, unconfirmed in Cubase).

### For the architect

- **UI4-3: the keyboard focus after typing with Keys off.** Hand the Windows focus back to Cubase's window when an
  inspector edit ends (Windows code outside JUCE), or keep JUCE's forwarding. Checklist step 10 will say whether the
  forwarding works in Cubase.
- **UI4-6: where the zoom buttons go.** At the ruler's right end they hide the last 122 px of bar numbers (at
  900 x 560 the 4-bar clip's "4"). The top bar has no room for them at 900 px; the prototype has them there.
- **UI4-7: the roll's room on a small window** (R2, R3): fold the tabs on a first open when the window is small, as
  sl-ui.js init does, or give UIST a folded default.
- **R4T-3: computeGlides at 4096 notes.** It compares every pair (in node 0.2 to 0.4 s at 4096 notes, 4.6 ms at
  512), once per drag step on the message thread and twice per compile on the publisher thread; the audio thread
  never waits, but the roll and the sound would follow the mouse late on a clip near the cap. Searching only the
  notes that start within reach of each note's end (sorted starts and a binary search) gives the same result. The
  model is not M4's.
- **R4T-4: a tempo ramp redraws every note.** Key the drawing's tempo per note (only lines with vibrato or
  automatic slides depend on it), or accept it: 512 notes take about 38 ms per full repaint, the demo's 26 far less.
- **Plan 4 M4 "keys" row, two exceptions for the record:** while an inspector box is typed in, Space types a space;
  while a JUCE drop-down list is open, Space closes it. The roll's own menu no longer holds Space.

### Hand-off

- **M5:** the right-click menu is `RollView::MenuPanel`. The lane buttons' menus (sl-ui.js openLaneMenu) can open
  the same panel, so their keys behave the same, and the hover ghosts can hook into its hover (the panel tracks the
  item under the mouse). A JUCE PopupMenu would take every key while open again.
- **M6 (UI4-9): the per-note decorations are drawn in every mode, not in mode 8 alone.** sl-ui-panels.js decorate
  runs for every visible note (sl-ui.js runNoteDecorators): the H4 direction and speed chevrons under the note and
  the U7 faint waveform above it. The thumbnail's alpha is `mode === 8 || mode === 1 ? (sel ? 0.42 : 0.24) : (sel ?
  0.22 : 0.11)` and the chevrons' strength `mode === 8 ? (sel ? 1 : 0.8) : (sel ? 0.8 : 0.5)`; fresh1100.png shows
  both in Build. The plan's M6 text names them with mode 8; R2 left them out of M4 on purpose.

### CI results

- Run 44283180626 on d02ae31 (every fix and its tests), green on every job and step: Windows Release 1670 checks in
  275 tests (main had 1646 in 265), Linux Release 1662 in 275, the sanitizers' contract, model and engine groups 808
  in 139 and the state group 142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE assertions), the Debug
  snapshots 0 JUCE assertions, pluginval strictness 10 SUCCESS, the fixture check passed (4.87 MB of the 5 MB budget;
  the fix pass adds no fixtures). The `ui` group: 64 tests, 330 checks (the fix pass added 10 tests and 24 checks).
- F1: the ramp near its end leaves 91 nodes with the new one at 83, the inspector on 83 of 91, a move moves it alone,
  1 undo step; near its start 89 nodes, the new one at 11; the 0.1 / 0.9 line keeps 128 nodes without the new one,
  no inspector node, a move moves nothing, 0 steps, and the toast says the lane is full.
- R4T-2: mid-drag the note is 2 rows up and "Sound: Evolving pad" is a step of its own; the release is "Move notes"
  (3 rows up); its Undo puts the note back at pitch 58, where it was before the drag (the middle of the drag was
  60), with the new sound; Undo of the load brings the old sound back.
- The menu: a right press opens the panel on the page with no modal window; with Keys on and the mouse on Delete,
  Space goes on to Cubase and nothing is deleted; Escape closes the menu and keeps the selection; a click on Delete
  is one step; Across keeps the menu open and lit; the opening press and a press on the menu leave it open, a press
  elsewhere closes it, and so do a click on the roll and the Diagnostics tab. The drop-down lists' guard keeps Space
  and leaves Return, the arrows, Escape and letters to the list.
- The keys: with the Start box open and Keys on, Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z, Ctrl+D and Ctrl+A changed neither
  the clip nor the undo history; on the Diagnostics tab 3, Delete, Ctrl+Z, Ctrl+D, B and Escape were kept and
  changed nothing, and Space went on to Cubase; a B held through 11 repeats toggled once (and through the editor,
  through 7 repeats).
- F5: 10 of 10 positions worked out by hand, 679 of 679 sixteenths read back with 1 to 7 beats per bar; through the
  editor a note on beat 3 of a 3-beat clip shows 2.1.1, and 3.2.1 typed puts it on beat 7.
- Drawing cost (R2's test, unchanged code): a full repaint of the 512-note clip 31.8 ms on Windows (software, target
  50 ms), 28.5 ms with Direct2D; moving one note marks 3 notes' bounds and its partial repaint equals a full one.
- The pictures of the open menu (run 44283180626's artifact), looked at against lanemenu.png: the Build menu with
  Duplicate lit (dark) and Delete lit (light), the Volume shapes with Each note lit and the header in Volume's
  colour (light), and the longest menu, Travel in mode 8, whole inside the 900 x 560 window with Rewind lit (dark).
  The menu colour, the line around it, the shadow, the small capitals, the greyed Paste lanes and the row of two
  buttons are the prototype's; nothing is cut or overlapping. The prototype draws its items in 13 px semibold, which
  Segoe UI shows lighter than JUCE's bold; StrandLock's buttons already use the same bold.
