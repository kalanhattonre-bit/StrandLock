# NOTES-M5

Builder notes for M5 (PLUGIN-PLAN section 4 "M5 Selection tools" and 7.7). Append only: one bullet per item, with the
reason. Each part writes its own section: S1, S2, S3, Fix.

## S1

Box select and the selection bar's direct actions (plan 7.7 S1). Branch `m5-select`, from main a71f28c.

### Progress

- Done: box select in `RollGestures` (with `ui/roll/BoxSelect`), the Nodes switch (picks, the group drag, the
  mode bar's button, the N key, its hint), `doc/RollState` and `ClipDocument` node picks, the picked nodes drawn,
  `ui/roll/SelectionBar` (`SelectionTools` and the bar), `ui/roll/Drawers` (Connect, Tools and the Walk, Chop,
  Humanize and Strum panels), the bar in `MainView`, Select similar's menu in the roll's menu panel, and the tests
  `test_box_select.cpp` and `test_selection_tools.cpp` with pictures for review.
- Done: CI green on 8c6adfb (run 44306677829, figures below); every picture of its artifact looked at (the 10 new
  ones against connect.png, walk.png, tools.png and ns.png, and the M4 pictures the bar now changes).
- Done (second session): the one fault the pictures showed, fixed: on a 900 x 560 window an open drawer had 60 px
  and scrolled, but JUCE drew no scroll bar on its 46 px view (decisions below); test `selectionBarSmallWindow`.
- In progress: CI on this commit, then the fast-forward of main.
- Next: S2 (the Shapes, Pattern and Random drawers, Turn into nodes, the lane-button menus, hover ghosts), from the
  hand-off below.

### Files

- `Source/ui/roll/BoxSelect`: `SelectBox`, `notesInBox`, `combineNotes`, `nodesInBox`, `combineNodes` (sl-ui.js
  applyBox, pure). `RollGestures::Box` is now this `SelectBox`.
- `Source/ui/roll/RollGestures`: the box drag selects as it moves; the Nodes switch (`RollSettings::nodes`,
  `toggleNodes`), the node box, the group drag of picked nodes ("Move nodes"), picks moved along when a node is added
  or deleted, Escape lets go of the picks. `selectionInTimeOrder` is public (the tools act on it).
- `Source/doc/RollState.h`, `ClipDocument`: `NodePick` and `RollState::picks` (sl-ui.js nodeSel and nodeLen),
  `setNodePicks`, `setNotePicks`; every new clip version drops a note's picks when its node count changed
  (validateNodes); a selection without the note, another mode or lane, a state load, Undo and Redo drop them too.
- `Source/ui/roll/SelectionBar`: `SelectionTools` (what every control does; no JUCE in its decisions) and the
  JUCE `SelectionBar` (the line and the open drawer).
- `Source/ui/roll/Drawers`: the drawer contents and their small controls (`NoteText`, `ArrowButton`,
  `LineSeparator`, `SegButtons`, `ToolSlider`, `RatePicker`), `ConnectDrawer`, `ToolsDrawer` and the tool panels.
- `Source/ui/roll/RollPainter`, `RollView`: picked nodes drawn large with a node-sel ring, no segment diamonds while
  nodes are picked; the view owns the `SelectionTools`, has `edited ()`, `toggleNodes ()`, `openMenuAt ()` (a menu of
  the caller's own in the same panel as the right-click menu) and `onToastFor` (a toast with its own time).
- `Source/ui/MainView`: the bar's line under the selection row, an open drawer under it, the inspector under that;
  the Nodes button wired; the Lane list shows a mode through `MainView::showLane`.
- `Source/ui/KeyRouter`: N switches Nodes (with Keys on); a held N toggles once, as B does.
- `Source/ui/HintBar`: Build's line has "drag on empty space = box select (Shift adds, Ctrl toggles)" again, and the
  Nodes switch's line is the prototype's.
- Tests: `Tests/ui/test_box_select.cpp`, `Tests/ui/test_selection_tools.cpp`; `test_keys` (N), `test_roll_live`
  (Build's hint).

### Decisions and deviations

- **The bar shows while notes are selected or a drawer is open, as in the prototype, and the roll stays still under
  the mouse.** The prototype hides the bar when nothing is selected and, while the pointer is on the roll, lets the
  bars above it grow but not shrink, scrolling the roll by as much as they grew (sl-ui.js holdStart, keepRollStill).
  `MainView::updateBarHeight` does the same: the bar's room grows at once (the roll scrolls down by the same amount
  when the mouse is on the roll or a gesture is held, so the notes stay where they were under the mouse, and a drag
  goes on where it was), and shrinks only once the mouse has left the roll (the editor's timer). While it is held,
  the bar's line says how to select ("Select notes (click one, or drag a box around several) to use the selection
  tools."). A first try kept the line always there (as M4 keeps the inspector's line), which took 30 px from the roll
  on every window and cut the 900 x 560 roll to 141 px (M4's snapshot test wants 150; run 44302433699); with nothing
  selected the roll now has M4's room. An open drawer takes room under the line until its button closes it; with
  nothing selected it stays where it is, faded and asleep, as the prototype keeps it.
- **Connect and Tools are drawers, as in the prototype** (sl-ui-panels.js DRAWERS.connect and DRAWERS.tools): their
  buttons are the plan's "direct actions". Shapes, Pattern, Random and Turn into nodes are S2's: their buttons are on
  the line and toast "Shapes, Pattern, Random and Turn into nodes arrive in the next build".
- **Every action is one model call and one undo step** (7.7 decision 1) through `SelectionTools::act`: the document
  applies the model's result (its caps can refuse it: then a toast, no step, the clip unchanged), then one step with
  the prototype's label ("Connect: nearest", "Chop 1/16", "Reverse (Tone)", "Walk Start" ...). A result equal to the
  clip pushes no step (EditHistory ignores an equal state, as the prototype's History does).
- **Walk and Humanize are live after their first click** (sl-ui-panels.js liveRun, 7.7 decision 2): every slider
  move rebuilds the result from the clip the session started from and is shown and played with no step; each
  release is one step; another selection, another mode or the next action ends the session (its last move becomes
  a step when it was not one). Before the first click a slider only sets its value: the prototype then draws a
  ghost, which is S2's.
- **Random stays put until Re-roll** (7.7 decision 3): Connect's seed and Humanize's seed are the bar's, starting at
  1; only their Re-roll buttons move them on by one, as the prototype's S.connectSeed and S.hum.seed.
- **With nothing selected the actions do nothing.** The prototype cannot reach them then (the drawer is asleep);
  `SelectionTools` returns early, so a test or a stray call cannot make a step.
- **The Connect drawer's warning drops "(red mark on the roll)":** the mark is part of the per-note decorations
  (sl-ui-panels.js decorate), which M6 and the Slides work draw; the sentence says the rest.
- **The Nodes switch picks nodes; a press on one of several picked nodes drags them together** (sl-ui.js rollDown and
  rollMove 'nodes'). The inspector's one value for several picked nodes (sl-ui-panels.js activeNodeCtx with a group)
  is not built yet: the inspector still shows the node last clicked. A node added on a full lane (128 nodes) lets go
  of that note's picks when the cap simplified the list, where the prototype keeps them until its next clip change.
- **Select similar opens in the roll's own menu panel** (`RollView::openMenuAt`), not a JUCE menu, so it takes no key
  (M4 fix pass R4T-1).
- **The Lane list and the Walk field are JUCE lists** (ComboBox, with the look and feel's Space guard), as the Grid
  is.
- **Toasts keep the prototype's times:** 3.2 s for Connect's count, 4 s for a refusal, 3.5 s for Legato's reasons,
  2.4 s otherwise.
- **An open drawer keeps at least 62 px, not the prototype's 60 (PART_MIN).** Found in the pictures of run
  44306677829: on a 900 x 560 window the Tools drawer with Walk's panel had its 60 px floor and scrolled, but its
  view was 46 px tall, and JUCE draws no scroll bar thumb on a bar shorter than 32 px plus its smallest thumb (twice
  its 8 px thickness: juce_ScrollBar.cpp resized and paint). So the Walk row was cut in half with nothing to say it
  scrolls. With 62 px the view is 48 px and the thumb shows; the roll gives up the 2 px.
- **The bar's buttons explain themselves in a tooltip** (the editor's TooltipWindow, 700 ms), as M4's buttons do. The
  prototype also shows the same text in the hint bar while the mouse is on a drawer's button (sl-ui-panels.js hintOn);
  that hover belongs with S2's ghosts, which hook the same mouse enter and exit.

### For the architect

- While notes are selected the bar's line takes 30 px from the roll (more with a drawer open); on a small window
  with a tab open the roll is small then. Decision 6 (S3) folds the tabs on a first open.
- The inspector's group value for several picked nodes (sl-ui-panels.js commitNodeValue with a group) is left for a
  later part (S2 or the fix pass); the Nodes switch works without it.
- **A drawer on a small window.** At 900 x 560 with the tabs folded and the Tools drawer open, the drawer keeps 62 px
  (it scrolls) and the roll's canvas about 75 px. The prototype keeps its roll at 150 px there by taking up to 16 px
  from the waveform (WAVE_MIN 40) and then scrolling the page (sl-ui.js fitLayout); the plugin's page cannot scroll,
  and its strip is 104 px at that height (M0's layout). Folding or shrinking something on a short window is decision
  6's question (S3).

### Hand-off for S2 and S3

- **Drawers.** `ToolSettings::Drawer` already names shapes, pattern and random. Add their drawer classes in
  `Drawers` (derive from `Drawer`: `heightFor`, `sync`, `onHeightChange`), build them in `SelectionBar::rebuildDrawer`,
  and give their buttons `openDrawer` in the `SelectionBar` constructor (the first three of `drawerButtons`, and
  `bakeButton`). `SelectionTools::toggleDrawer` already shows the lane's mode for them (showLane).
- **Ghosts.** `SelectionTools::walk (false, false)` and `humanize (false, false)` do nothing without a live session:
  that is where the prototype draws the ghost (`ghost (() => walkClip (A.getClip ()), D.drawer)`). The roll draws a
  ghost with `RollView::setGhost`. Buttons hover: the drawers' buttons are plain FlatButtons; give them mouseEnter /
  mouseExit (or a hover callback) for S2's ghosts.
- **Live sliders.** `SelectionTools::liveRun` is the session S2's Pattern and Random sliders need (decision 2).
- **Menus.** `RollView::openMenuAt (from, at, items, actions)` opens a menu of the caller's own in the panel; the
  lane-button menus (S2) can use it.
- **Copy / Paste and Slides tabs (S3).** Not touched. `RollGestures::copyMask` is still every lane.

### CI results

- Run 44302433699 on aacdf4e (the first push): red, 5 of 1710 checks on Windows and 5 of 1702 on Linux. Two were
  M4's 900 x 560 roll (826 x 141, the test wants 150: the bar's line was always there; now it shows with a
  selection), and three were S1's own tests: the sweep's count of runs that change nothing, a Legato clip whose notes
  already touched, and a Flip on Formant, a lane the fixture clip holds flat (both now use clips the tool changes).
- Run 44306677829 on 8c6adfb, green on every job and step: Windows Release 1713 checks in 285 tests (main had 1670 in
  275), Linux Release 1705 in 285, the sanitizers' contract, model and engine groups 808 in 139 and the state group
  142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE assertions), the Debug snapshots 0 JUCE assertions,
  pluginval strictness 10 SUCCESS, the fixture check passed (26 files, 4.87 MB of the 5 MB budget; S1 adds no
  fixtures). The `ui` group: 74 tests, 373 checks on both platforms (S1 added 10 tests and 43 checks).
- Tools through the UI layer: 71 cases of `model/tools.json` ran through the bar (chop 7, connect 14, disconnect 4,
  flipLane 5, humanize 3, legato 4, reverseLane 6, scaleLane 10, selectSimilar 9, strum 5, walk 4; 28 cases hold
  arguments the bar cannot, such as a More of 1.5 or a Chop with maxNotes 20); 71 of 71 encode to the bytes of the
  direct model call, 55 changed the clip with 1 undo step and the bar's label each, the rest pushed none; Select
  similar 9 of 9 the fixture's ids. The sweep: 27 actions on every fixture clip with every note and a seeded half
  selected, 378 runs, 378 the bytes of the direct model call, 232 changed the clip with 1 step each and 146 pushed 0.
- Chop past 1024: 64 four-beat notes at 1/32 (2048) refused, 0 undo steps, the bytes unchanged, the toast "That would
  make 2048 notes; the limit is 1024. Pick a slower rate or fewer notes." for 4 s; at 1/16 exactly 1024 notes, one
  step "Chop 1/16", every piece selected, "Chopped into 1024 notes".
- Live sliders: Walk's slider before the first click changes nothing; the click is "Walk Start"; a move is live with
  no step; each release is one step; another selection makes the last move a step; Undo lands on the release before.
  Humanize: the click, a live move, its release and Re-roll make 3 steps; Re-roll is the model's Humanize with seed 2.
- Box select: 160 seeded boxes over the 64-note clip in Build, Volume, Tone and Sample (Travel), 104 of them meeting
  some notes but not all: 160 of 160 select exactly the notes whose rectangle meets the box (plain, Shift, Ctrl), with
  the primary kept or the first one; 160 of 160 right half way through the drag; 0 notes changed and 0 undo steps.
  The Nodes switch: 60 node boxes (46 caught nodes), 60 of 60 pick exactly the selected notes' nodes inside; a drag
  on one of 3 picked nodes moves all 3 by 0.25 and no other, one step "Move nodes"; the picks go with Escape, another
  mode and Undo, and move along when a node is added or deleted. Through the editor: a drag across the canvas
  selected the 12 notes it met; the bar's line came in (the roll 30 px lower) and the roll scrolled 30 px, so the
  notes stayed under the box; a click on empty space cleared the selection and the line went.
- The pictures (run 44306677829's artifact, 69). Compared pixel for pixel with main's (run 44294087947): 37 of main's
  59 are unchanged (nothing selected in them, so the bar takes no room); the other 22 have notes selected and now
  show the bar's line, and were looked at with the 10 new ones (those against the v4 screenshots). The bar:
  LANE with the list in the lane's colour, Shapes, Pattern and Random with their small triangles, Turn into nodes, a
  line, Connect and Tools, the open one lit with its triangle up (connect.png, tools.png). The Connect drawer: its
  five buttons and "16 slides among the selected notes. Each one can still be curved with its handles in Build or
  Pitch." The Tools drawer: Walk... and Chop..., "TONE:" with Reverse, Flip, More and Less, Select similar, Humanize...,
  Strum... and Legato; Walk's panel in its dashed box with the field, the two sliders with "0% (0:00.00)" and "90%
  (0:05.40)", and the lit Walk button (walk.png). Chop refused: the warning in the warn colour and the Chop button
  greyed. The box: dashed, over the notes it selects. The picked nodes: large, with the node-sel ring, and the Nodes
  switch lit. M4's pictures with a selection now show the bar's line, 30 px from the roll; nothing cut there. One
  fault: the 900 x 560 Walk picture, whose drawer was cut through the Walk row with no scroll bar (fixed above).
