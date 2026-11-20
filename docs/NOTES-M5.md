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
  and scrolled, but JUCE drew no scroll bar on its 46 px view (decisions below); test `selectionBarSmallWindow`. CI
  green on 3540a5f (run 44321283196, figures below), its pictures compared and the changed one looked at.
- In progress: CI on this commit (notes only), then the fast-forward of main.
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
  (it scrolls) and the roll's canvas 75 px (run 44321283196). The prototype keeps its roll at 150 px there by taking
  up to 16 px from the waveform (WAVE_MIN 40) and then scrolling the page (sl-ui.js fitLayout); the plugin's page
  cannot scroll, and its strip is 104 px at that height (M0's layout). Folding or shrinking something on a short
  window is decision 6's question (S3).

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
- **Small windows.** An open drawer gets at least `SelectionBar::drawerMinHeight` (62 px) and scrolls inside it;
  `selectionBarSmallWindow` checks that its scroll bar shows. A new drawer needs nothing more for that.
- **The hint bar on hover.** The prototype shows a drawer button's title in the hint bar while the mouse is on it
  (sl-ui-panels.js hintOn); S1's buttons use tooltips. S2's hover hooks (for the ghosts) are the place to add it.

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
- Run 44321283196 on 3540a5f (the 62 px drawer floor and its test), green on every job and step: Windows Release
  1714 checks in 286 tests, Linux Release 1706 in 286, the sanitizers' groups 808 in 139 and 142 in 20, the Debug-CRT
  allocation run 38 in 7 (0 JUCE assertions), the Debug snapshots 0 JUCE assertions, pluginval strictness 10
  SUCCESS, the fixture check passed (26 files, 4.87 MB). The `ui` group: 75 tests, 374 checks on both platforms (S1
  added 11 tests and 44 checks to main's 64 and 330). `selectionBarSmallWindow`: at 900 x 560 with the tabs folded
  and Walk's panel open, the drawer's box is 62 px, its 88 px of contents scroll in a 48 px view, the scroll bar is
  shown and 48 px long, and the roll's canvas keeps 75 px. Its pictures against run 44306677829's: 68 of 69 the same
  pixel for pixel; the 900 x 560 Walk picture now shows the drawer's scroll bar thumb at its right edge.

## S2

The drawers, the lane-button menus and the hover ghosts (plan 7.7 S2). Branch `m5-select`, from S1's 71b9fe7 (main).

### Progress

- Done: the work of the paused session (11/18, uncommitted in the worktree) read through and committed (3e79077):
  `ui/roll/Drawers` (Shapes, Pattern, Random), `ui/roll/LaneMenu`, `ui/roll/GhostPreview`, the bar's Turn into
  nodes, the hint bar's hover text, and `Tests/ui/test_ghost.cpp`. It compiled on both platforms the first time
  (run 44675856276); two of its checks were wrong in the test, not in the code (below).
- Done: the tests of the M5 table's S2 rows in `test_selection_tools.cpp` (f3db408): the drawers and lane menus
  through the UI layer, live sliders, seeded Random and Re-roll, the lane menu's items, the drawers in the editor,
  and pictures of the Shapes, Pattern and Random drawers.
- Done: the inspector's one value for several picked nodes (S1's open item), with its test in `test_box_select.cpp`
  (9ba763f), and no ghost on a greyed control (ac4ee27).
- Done: CI green on ac4ee27 (run 44681435341, figures below); every new and changed picture of its artifact looked at.
- In progress: CI on this commit (notes only), then the fast-forward of main.
- Next: S3 (the Copy / Paste and Slides tabs, the M5 snapshots, the checklist), from the hand-off below.

### Files

- `Source/ui/roll/Drawers`: `ShapesDrawer` (sl-ui-panels.js DRAWERS.shapes) and `GenDrawer` (DRAWERS.pattern and
  DRAWERS.random in one class: the generator's kind picks which controls it has), `RatePicker::setRate`, the
  controls' hover hooks (`Drawer::getHooks`, `ToolPanel::getHooks`), `Drawer::findButton`.
- `Source/ui/roll/SelectionBar`: `SelectionTools` gained the lane drawers' half: `shapeKeys`, `shapeOpts`, `scope`,
  `applyShape`, `putGen`, `drawerGen`, `primaryGen`, `anyGen`, `isEditing`, `genChanged`, `applyGen`, `rerollGen`,
  `removeGen`, `bake`, `loadGenValues`, `openDrawer`, `showLane`, and one `preview...` per control (each the model call
  the click makes, never applied). `act` is public (the lane menu acts through it). The live session's key is the
  prototype's string ("walk", "hum", "gen:pattern:tone"). `ToolSettings` holds `pat`, `rnd` and `shapeSeed`
  (sl-ui-panels.js S.pat, S.rnd, S.shapeSeed). The JUCE bar builds the three drawers, gives Shapes, Pattern, Random
  and Turn into nodes their clicks, rebuilds a lane drawer when its lane (or, on Travel, the notes' kind) changes,
  and hooks its buttons for the hint bar; `showDrawer` opens a drawer from a menu.
- `Source/ui/roll/LaneMenu`: `laneMenu` (sl-ui-panels.js openLaneMenu's items, actions and ghosts) and
  `openLaneMenu` (the roll shows the lane, then the menu under the arrow, or the toast with nothing selected).
- `Source/ui/roll/GhostPreview`: `GhostPreview` (which preview is drawn and who owns it: sl-ui-panels.js ghost,
  clearGhost and ghostOwner) and `HoverHooks` (ghostOn and hintOn: a `juce::MouseListener` on each control).
- `Source/ui/roll/RollView`: owns the `GhostPreview` and draws through `setGhost`; `menuHover` gives a menu item's
  ghost (the lane menus' and the right-click menu's shapes); `openMenuAt` takes the items' ghosts; `showHint` and
  `onHint`; `setScope`.
- `Source/ui/roll/RollGestures`: the right-click menu's shapes have ghosts (`MenuItem::hover`, `menuItemGhost`).
- `Source/ui/roll/Inspector`: two or more picked nodes show as one value box (sl-ui-panels.js activeNodeCtx with a
  group, commitNodeVal).
- `Source/ui/HintBar`: `setHelp` (sl-ui.js setHint), and a lane mode's line points at the lane button's menu again.
- `Source/ui/MainView`: the lane buttons' arrows open their menus; `getLaneArrow`; the roll's hints go to the hint bar.
- Tests: `Tests/ui/ToolRig.h` (S1's rig, shared, now counting clip versions and ghosts), `test_ghost.cpp`, and in
  `test_selection_tools.cpp` the S2 tests and pictures; `test_box_select.cpp` (`boxSelectNodesInspector`);
  `test_roll_live.cpp` (the hint's words).

### Decisions and deviations

- **Every drawer action is one model call and one undo step** (decision 1), through `SelectionTools::act` or a
  live session: a shape is `model::applyShape`, Apply is the prototype's putGen (`model::makeGen` and `normalizeGen` on
  each note through `model::editNotes`), Re-roll `model::reroll`, Remove `model::clearGen`, Turn into nodes
  `model::bakeGen`. The labels are the prototype's: "Swell (Tone)", "Pattern (Tone)", "Random (Tone)", "Re-roll
  (Tone)", "Remove pattern (Tone)", "Turn into nodes (Tone)"; a live control adds its word: "Pattern depth (Tone)",
  "Random amount (Tone)".
- **Pattern and Random are live once the primary note has that kind of generator** (decision 2; sl-ui-panels.js
  genChanged and isEditing). Apply starts the session; from then on every slider move is a new clip version sent to
  the engine with no undo step, and the release is one step; a chip, the rate, Lock to the beat and Same or
  Different are one step each. Before Apply a control's change only draws a ghost, which the open drawer owns: it
  stays while the mouse goes from one control to another and goes when the mouse leaves the drawer (the prototype's
  D.drawer mouseleave). Walk's and Humanize's sliders before their first click now draw their ghosts too.
- **Random stays put until Re-roll** (decision 3): Apply keeps each note's own seed and 'Different' key (and its
  invert when it had the same kind), as putGen does, so changing Amount or the rate never deals new values; only
  Re-roll moves every selected note's seed on by one. The Random shape (Shapes) deals new values on each click, as
  the prototype's does: its seed is the bar's and each click moves it on by one; the nodes it makes then stay.
- **The drawers read the primary note's generator** into their settings whenever the clip, the selection, the
  primary note or the lane changes (sl-ui-panels.js loadGenValues). The prototype skips it after the bar's own edits;
  here it reads then too, which gives the same values (the bar's own edits write the drawer's values into the
  primary note, or remove the generator, which reads nothing).
- **A drawer is never rebuilt by its own controls** (S1's rule). A lane drawer is built for its lane, and Shapes on
  Travel for the notes' kind; the bar rebuilds it when either changes (the Lane list, a mode, a selection of the
  other kind), as the prototype's renderDrawer does on every change. Everything else changes in place. The open
  drawer also follows Each note / Across when a menu changes it.
- **The lane menus are sl-ui-panels.js openLaneMenu** (the panels' version, which replaces sl-ui.js's basic one):
  the header "Formant on 4 notes" in the lane's colour, Each note / Across (the menu stays open), the lane's shapes
  (on Travel those of the notes' kind, both kinds' when mixed), then the drawers' pattern and random with their
  settings in the label ("Pattern: squiggle 1/16 triplet", "Random 1/16 triplet"), Pattern settings... and Random
  settings... (they open that drawer on this lane), Re-roll the random, Remove pattern / random and Turn into nodes
  (greyed while no selected note has a pattern or random on the lane). It opens 4 px under the arrow in the roll's
  own menu panel (M4 fix pass: no JUCE menu, so no key is held). The roll shows the lane first; with nothing
  selected it says "Select notes first: click one, or drag a box on empty space".
- **The menus' items have no tooltips.** The prototype's lane menu gives some items a title (the shapes' sentences,
  "Rate, shape, depth, Lock to the beat"); M4's menu panel has no tooltips, and the Shapes drawer's buttons carry the
  same sentences.
- **Ghosts are drawn, never applied** (decision 2): every preview is the click's own model call made off to the
  side, handed to `RollView::setGhost`; it never reaches the ClipDocument. Owners as in the prototype: a control (its
  preview goes when the mouse leaves it), the open drawer, the open menu (its item's preview goes with the mouse, a
  click or the menu closing). A click takes any preview away before it acts. The right-click menu's shapes have
  ghosts too (sl-ui.js shapeItems). Recipes: `SelectionTools::previewRecipe` is the hover of M6's recipe buttons
  (`model::applyRecipe` on the selected notes, or every note), tested here.
- **A greyed control draws no ghost**, and still shows its text in the hint bar: a click on it does nothing, so its
  preview would show a change that cannot happen (Random's Re-roll with nothing on the lane drew every selected note
  as an unchanged ghost, since the model's reroll copies them). A greyed menu item already drew none (M4's panel).
- **The hint bar shows a control's text while the mouse is on it** (sl-ui.js setHint): its tooltip, or a slider's
  title, over the bar the way a cut hint shows whole, changing no size. A lane mode's line says "▾ on the lane button
  = shapes for every selected note" again, the prototype's words.
- **The inspector's value for several picked nodes** (S1's open item, sl-ui-panels.js activeNodeCtx with a group):
  with two or more nodes picked the inspector shows "3 nodes (Volume)" and one value box (no time box); a typed value
  goes to every picked node, each note reading it with its own Travel kind, one undo step "Type value for 3 nodes"
  (`model::setLaneNodesMany`).
- **Two test errors in the paused session's test_ghost.cpp, fixed in the test:** the lane menu's ghosts read the
  scope at hover time and the shapes before them in the list had left it on Across; and the editor test skipped
  every control, because a test editor has no window, so `isShowing ()` is false.

### For the architect

- **The menus' items have no tooltips** (above): the prototype's lane menu titles some items; M4's menu panel shows
  none. The Shapes drawer's buttons carry the same sentences in their tooltips and in the hint bar.
- **A drawer on a small window** (S1's note, now with taller drawers): at 900 x 560 with the tabs folded, the Pattern
  drawer (three lines and its note, about 120 px of controls) keeps its 62 px and scrolls, and the roll keeps 75 px
  (picture `m5_bar_pattern_narrow_dark`). Decision 6 (S3) is where a short window gives room back.
- **Ghosts draw every note the preview copied.** The roll draws a ghost for each note that is not the same object as
  the clip's (sl-ui.js drawGhost), so a model call that copies notes without changing them shows them dashed over
  themselves. The greyed-control rule removes the one case the drawers met (Re-roll with nothing to re-roll).

### Hand-off for S3

- **Copy / Paste tab.** `RollGestures::copyMask` is still every lane (M4); the tab sets it (sl-ui.js's Copy / Paste
  panel) and pastes through `model::pasteLanes` / `pasteNotes` with the 4096 cap's toast (decision 5).
- **Slides tab.** `glideMode` and `glideTime` with the M2 30 Hz polling, no attachments (decision 4).
- **The M5 snapshots** (dark and light at 100 %: the bar with the Shapes drawer open, a hover ghost, a box select in
  progress): S2's review pictures are close to them (`m5_ghost_shapes_dark` / `_light` show the Shapes drawer with
  Swell's ghost and its hint; `m5_box_select_*` the box; `m5_bar_pattern_*`, `m5_bar_random_dark`,
  `m5_bar_shapes_travel_light` and `m5_ghost_lane_menu_*` the other S2 parts). S3 names the table's own.
- **The checklist's M5 section** can take S2's steps: a lane button's arrow opens its menu (a shape on every selected
  note, one undo step; the dashed preview while the mouse is on an item); Shapes, Pattern and Random in the bar
  (Apply, then a slider changes the notes while it moves and is one undo step when let go; Random stays the same on
  every loop until Re-roll); Turn into nodes; hovering any of them draws the dashed preview and changes nothing;
  three nodes picked with Nodes take one typed value in the inspector.
- **Recipes (M6).** `SelectionTools::previewRecipe (n)` is the recipe buttons' hover; hook each button with
  `HoverHooks` (its preview, and sl-ui-sample.js's hint text) and the ghost rows hold.
- **Small windows** (decision 6): see "For the architect".

### CI results

- Run 44675856276 on 3e79077 (the paused session's work as it was): both platforms compiled it the first time; 2 of
  1726 checks red on Windows and 2 of 1718 on Linux, both in test_ghost.cpp's own logic (the lane menu's ghosts read
  the scope the shapes before them had left on Across, and the editor test skipped every control because a test
  editor is not showing). Every other step green, pluginval included.
- Run 44678655129 on f3db408 (the S2 tests): 1 of 1746 red on Windows and 1 of 1738 on Linux: the editor ghost test
  still skipped every control (it asked the editor itself to be visible). Run 44681076870 on 9ba763f: green.
- Run 44681435341 on ac4ee27, green on every job and step: Windows Release 1748 checks in 295 tests (main had 1714
  in 286), Linux Release 1740 in 295, the sanitizers' contract, model and engine groups 808 in 139 and the state group
  142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE assertions), the Debug snapshots 0 JUCE assertions,
  pluginval strictness 10 SUCCESS, the fixture check passed (26 files, 4.87 MB of the 5 MB budget; S2 adds no
  fixtures). The `ui` group: 84 tests, 408 checks on both platforms (S2 added 9 tests and 34 checks).
- Tools through the UI layer (`selectionToolsDrawerSweep`): every drawer action and lane-menu item on the 7 fixture
  clips with notes, with every note and with a seeded half selected, on all 10 lanes: 6075 runs (shapes Each and
  Across 2370, Pattern 1374, Random 407, Re-roll 140, Remove 140, Turn into nodes 140, lane-menu items 1504), 6075
  the bytes of the direct model call; 5198 changed the clip with 1 undo step and its label each, 381 two-click
  chains (Apply, then Re-roll, Remove or Turn into nodes) 2 steps each, the rest none; 381 lane-menu items greyed
  with nothing on the lane; Turn into nodes with nothing to turn 127 of 127 toasted the prototype's sentence.
- Through the editor (`selectionBarDrawersInTheEditor`): Shapes on Tone, 8 of 8 buttons one step equal to
  `model::applyShape`; Pattern's Apply, its 7 chips after Apply (one live step each), Re-roll on Smooth random,
  Remove (the buttons grey), Apply and Turn into nodes, Random's Apply and the bar's Turn into nodes, each the bytes
  of its model call; 7 of 7 lane arrows open their lane's menu and show the lane; with nothing selected the toast.
- Ghosts (`ghostsChangeNothing`): 8769 hovers in 140 runs (2370 shapes, 1120 patterns, 1120 recipes, 1499 lane-menu
  items, and every tool preview): 8413 drew a ghost (6641 of them unlike the clip), 356 drew none where the click
  would change nothing to see; 8769 of 8769 the bytes of the direct model call; 140 of 140 runs pushed 0 undo steps
  and made 0 clip versions; 8769 of 8769 ghosts went when the mouse left; the same controls' clicks made 3 steps and
  3 versions. In the editor (`ghostsInTheEditor`): 20 drawers on 4 lanes, 234 hooked controls hovered (132 drew a
  ghost), 7 lane menus with 126 items, 8 right-click shapes, 8 recipes: the SnapshotPublisher took 0 new clips, the
  clip kept its version, no undo step; the roll drew exactly the ghost while the mouse was there and none after
  (234 of 234), the hint bar showed each control's text (234 of 234); a click on Swell was 1 submit and 1 step.
- Live sliders (`selectionToolsGenLive`): before Apply a Pattern slider drew the ghost and changed nothing; Apply 1
  step; 6 moves of Depth at start, 6 clip versions and no step, each the bytes of the drawer's pattern; the release 1
  step "Pattern depth (Tone)"; Undo back to Apply's clip. Random: 3 Amount moves, 3 versions, no step, the release 1
  step; Smooth the same; a move left without its release became 1 step when the selection changed. Through the
  editor: 5 moves of the real slider, 5 clips handed to the SnapshotPublisher, no new undo step until the release,
  which was exactly 1 (one Undo back to Apply's clip).
- Random and Re-roll (`selectionToolsRandomSeeds`): Random on Tone, Different per note, on two windows: the same
  bytes and the same lane tables, seed 1 on every note, each note its own line; Undo and Random again: the same
  bytes; Re-roll: seed 2 on every note, other tables, the model's reroll; Amount afterwards kept seed 2. The Random
  shape: the same nodes on two windows with seed 1; the second click used seed 2 and dealt other nodes.
- Lane menus (`selectionToolsLaneMenu`): Tone on 3 notes lists the prototype's 18 items in its order (the header in
  Tone's colour, Each note lit, 8 shapes, the line, "Pattern: squiggle 1/16 triplet", Pattern settings..., "Random
  1/16 triplet", Random settings..., and Re-roll the random, Remove pattern / random and Turn into nodes greyed); 12
  items draw a ghost; Across reopens the menu; the settings items open their drawer on the lane; Saw up at 1/8
  dotted reads "Pattern: saw up 1/8 dotted"; on Travel 12 shapes for Speed notes, 11 for a Path note, 15 for both.
- The inspector's group (`boxSelectNodesInspector`): three picked Volume nodes on two notes show "3 nodes (Volume)"
  with no time box; "-6 dB" set all three to 0.6209 and no other node, one undo step "Type value for 3 nodes"; "loud"
  toasted "Type a level in dB, for example -6" and changed nothing; one picked node shows "Node (Volume)" with its
  time box.
- The pictures (run 44681435341's artifact, 78), compared pixel for pixel with S1's (run 44325854672): 9 new
  (`m5_bar_pattern_dark`, `_light`, `_narrow_dark`, `m5_bar_random_dark`, `m5_bar_shapes_travel_light`,
  `m5_ghost_shapes_dark`, `_light`, `m5_ghost_lane_menu_dark`, `_light`), 36 changed, 33 the same. The changed
  ones: with notes selected, the bar's Turn into nodes is now greyed while no selected note has a pattern or random on
  the lane (S1's always looked clickable); the hint's hover shows the lane menus' words; the M4 right-click menu
  picture with Swell under the mouse now draws Swell's ghost; and the picked-nodes picture's inspector shows "3 NODES
  (VOLUME)" and one value. Looked at against lanemenu.png, ghost.png and tools.png: the Shapes drawer (SHAPE TONE,
  Each note lit, the 8 shapes; on Travel the 12 with the Speed sentence), the Pattern drawer after Apply on Random
  steps (the 7 chips, the rate with Triplet lit, both depths at 30%, Lock to the beat lit, the speed-up slider faded,
  Same / Different, Apply unlit, Re-roll, Remove, Turn into nodes, "Moving these changes the pattern..."), the Random
  drawer on Pitch ("±1 semitone", "steps"), the Formant lane menu under its arrow with Ramp up's dashed ghost, and
  Swell's ghost with its sentence over the hint bar. Nothing cut, overlapping or off-palette; at 900 x 560 the
  Pattern drawer scrolls in its 62 px with its scroll bar showing (decision 6).

## S3

The Copy / Paste and Slides tabs, decision 6 (M4's layout leftovers), the M5 pictures and the checklist (plan 7.7 S3).
Branch `m5-select`, from S2's a84a8b4 (main).

### Progress

- Done (first push): `ui/tabs/CopyPasteTab`, `ui/tabs/SlidesTab`, the gestures' Copy notes and Paste notes (and the
  right-click menu's Paste notes here), the selection tools' slide time, Auto and Jump, decision 6 in `MainView` (a new
  instance folded, whole rows or folded, the inspector's line on a short window with a drawer, the zoom buttons in the
  tab row), `Tests/ui/test_copy_paste.cpp` with the M5 table's pictures, and the tests the changes touch
  (`test_editor`, `test_controls`).
- Done: run 44692540376 on 9d6360a (the first push): both platforms built it; every new test passed but the
  decision 6 one, which had written down layouts that depend on the fonts (3 checks on Linux, 1 on Windows; below).
  Fixed in the test. JUCE assertions in the Debug pictures 0, pluginval SUCCESS. Its 88 pictures compared with S2's
  and looked at (below).
- Done: the Cubase checklist's M5 section.
- Done: run 44695294607 on ba326b7 (the decision 6 test made font-independent, the box picture in Tone), green on
  every job and step (below). Its list of folded tabs showed Copy / Paste folding on the smallest window (its chips'
  line wrapped), so the tab now keeps Copy and Paste on its second line; run 44697009302 (notes) was cancelled for it.
- Done: run 44698619192 on 578967f (the Copy / Paste tab in two rows, a check that it and Slides show at 900 x 560),
  green on every job and step; its 5 changed pictures looked at (below).
- Done: the M5 summary (the end of this file).
- In progress: CI on this commit (notes only), then the fast-forward of main.

### Files

- `Source/ui/tabs/CopyPasteTab`: shell.html's "copy" pane (sl-ui.js copyLanes, pasteLanes, the chips): LANES TO COPY
  with a chip per lane in its colour (the gestures' `copyMask`, kept in UIST `copyMask`); on a second line Copy, Paste
  and what is held ("nothing copied", "holding: Pitch, Tone", "holding: 9 lanes"), a line, then NOTES with Copy notes,
  Paste notes, Duplicate and what is held. `lanesHeld` is the text, for the tests.
- `Source/ui/tabs/SlidesTab`: shell.html's "glide" pane (sl-ui.js syncGlideUI, wirePanels): AUTO SLIDE (a `ChoiceBox` on
  `glideMode` with the prototype's words), SLIDE TIME (`SlideTimeBox`, the prototype's 1/32 .. 1/2 on the float
  `glideTime`), a line, SELECTED NOTES (a `ToolSlider`, 0 .. 2 beats in 1/64 steps), Auto and Jump, no slide.
- `Source/ui/tabs/TabPanel`: `add` (any component as an item of the flow), `shortSeparator` (a button's height, not a
  knob's), `bound` (a parameter one of the panel's own controls holds), `setItemWidth` and `onLayoutChange` (a text
  that grows lays the pane out again).
- `Source/ui/Controls`: `ChoiceBox::setLabels` (other words for a choice parameter's items).
- `Source/ui/roll/RollGestures`: `copyNotes`, `pasteNotes`, `pasteNotesAt`, `noteClipboard`, `laneClipboard`, and
  `placeCopies` (Duplicate's and Paste notes' second half: the copies past the clip's end left out, the rest cut, one
  document edit and one undo step); the right-click menu's Paste notes here (Build, empty space, once notes are
  copied).
- `Source/ui/roll/RollView`: `copyNotes`, `pasteNotes`; the zoom buttons stay in the ruler only while they are the
  view's own children.
- `Source/ui/roll/SelectionBar`: `SelectionTools::slideTime` (a live session, key "slide"), `slideAuto`, `slideJump`.
- `Source/ui/roll/Drawers`: `ToolSlider::preferredWidthFor`; the slider ignores the mouse while disabled.
- `Source/ui/MainView`: the two tabs, decision 6 (`paneHeightFor`, `isAutoFolded`, `isInspectorFolded`,
  `rollFloor`), the zoom buttons in the tab row, the fold button's tooltip, `getFoldButton`, `getTabsArea`.
- `Source/PluginProcessor.cpp`: a new instance's UIST starts folded.
- Tests: `Tests/ui/test_copy_paste.cpp` (below); `test_editor` (a new instance folded; the Slides tab's two parameters
  bound, every parameter but bypass now has a control; at 900 x 560 with the banner the tabs fold, at 900 x 640 the
  pane scrolls whole rows with its thumb) and `test_controls` (the timer moves the Slides tab's two controls too).
- `docs/CUBASE-CHECKLIST.md`: the section "M5: selection tools", and notes on what moved in the earlier parts.

### Decisions and deviations

- **Every paste and duplicate is one model call and one undo step** (decisions 1 and 5): Paste lanes is
  `model::pasteLanes` with `model::copyLanes` of the primary note and the chips' lanes, Duplicate
  `model::duplicateNotes`, Paste notes `model::pasteNotes`; each is one `ClipDocument` edit and one step ("Paste
  lanes", "Duplicate", "Paste notes"). Past 4096 notes the model refuses it, the toast says "That would make 4097
  notes; the limit is 4096." and nothing changes (no bytes, no step, no clip version); the document's own caps (32
  MiB, the snapshot budget) refuse the same way.
- **Paste notes is new, so its place is chosen to match Duplicate.** The prototype pastes only lanes (M1 added
  `model::pasteNotes` for the plan's cap). The copies start where the selected notes end (where Duplicate puts its
  copies), or right after the copied notes when nothing is selected, so Copy notes then Paste notes again and again
  lays the phrase out one copy after another; the pasted notes are selected. A place of one's own: the Build
  right-click menu on empty space gets Paste notes here (the grid line at or before the click, as New note here),
  only once notes are copied, so M4's menus (and the gesture fixtures) are unchanged. As Duplicate (sl-ui.js
  duplicateSel), copies that would start at or after the clip's end are left out and the rest end at it; the toast
  says how many were pasted. Ctrl+C and Ctrl+V stay the prototype's lanes.
- **A Duplicate the document refuses keeps the selection.** M4's Duplicate set the selection to the copies even when
  the document refused the edit (the 32 MiB cap), which left nothing selected; it now returns with the toast.
- **The clipboards live as long as the editor window** (the gestures'), as the bar's settings do; the lanes Copy
  takes are the project's (UIST `copyMask`, which M0's format already had and nothing used).
- **The Copy / Paste tab is two whole rows from 900 px wide up.** The prototype's pane is one wrapping line (the label,
  the chips, Copy, Paste, what is held); here Copy starts a second line and the notes follow on it. With the
  prototype's order the chips' line wrapped at 900 px and the tab folded on the smallest window on both platforms
  (run 44695294607's list: Copy / Paste among the folds at 900 x 560); in two rows it shows there.
- **"holding: ..." lists the lanes in the registry's order.** The prototype lists them in the order the chips were
  last switched on (a JavaScript Set keeps insertion order); the lanes copied are the same either way. With no lane
  chosen it says "holding: " and nothing, as the prototype.
- **Auto slide and Slide time** (decision 4) are lists as in the prototype: AUTO SLIDE with its words ("Legato
  (touching notes)", "Always (next note)") over the parameter's Off / Legato / Always, SLIDE TIME with 1/32 .. 1/2.
  Slide time is a float parameter (0 .. 16 beats): a value the list does not hold (automation, a v3 save) shows as the
  parameter's own text ("0.700 beats"). A pick is one host gesture and one undo step "Auto slide" / "Slide time" (the
  prototype's patchInst labels); the 30 Hz timer moves them when Cubase's automation does, never an attachment.
- **SELECTED NOTES is a live slider** (decision 2): `SelectionTools::slideTime` runs a live session as Walk's sliders
  do (each move rebuilt from the clip the drag started from, shown and played, no undo step; the release one step "Slide
  time (notes)"). With no note selected it is greyed and reads an em dash; a `ToolSlider` now ignores the mouse while
  disabled (JUCE hands a disabled component its mouse events). Auto ("Slide: auto": glide null, curve 0) and Jump, no
  slide ("Slide: jump": glide 0) are one step each, through `SelectionTools::act`.
- **No hover ghosts on the two tabs.** The prototype's Slides and Copy / Paste panes preview nothing (U6 is shapes,
  patterns and recipes); their buttons explain themselves in tooltips, as the M0 tabs' do.
- **The slide warning on the roll stays with M6.** S1 left the Connect drawer's "(red mark on the roll)" to M6 and
  the Slides work. The prototype draws that mark in sl-ui-panels.js decorate, note by note on the roll, with the
  chevrons and the thumbnails (UI4-9, handed to M6) and the own-settings dot (S7, M6's Note settings), so it goes with
  them; the Slides tab changes slides and draws nothing on the roll. The drawer's sentence still says what happens to
  the note.
- **Decision 6, a first open:** a new instance's UIST starts with the tabs folded (the processor sets it; the state
  format's default is unchanged). The prototype folds on a first open in a window under 900 px high; a new StrandLock
  window is always 680 px high, so it always folds. A project keeps the fold it was saved with.
- **Decision 6, a short window:** the pane shows whole rows only, and the roll keeps `MainView::rollFloor` (110 px, the
  number M0's pane rule used) under it: the whole panel when it fits in 2/5 of the room, else whole rows up to the
  first row end past 2/5 (or the last before it), at least 64 px so the scroll bar has a thumb (fix pass R2-7). When no
  whole row fits, the tabs show folded (`isAutoFolded`): the person's fold in UIST is unchanged, the tab opens again by
  itself when the window is taller, and a click on a tab or Show says "This window is too short to show the tab. Make
  the window taller and the tab opens by itself." The prototype instead cuts the row (its pane's least share is 60 px,
  fresh1100tab.png cuts the knob row). Which tab folds where depends on the fonts: on Windows the Sample tab folds at
  900 x 560 (a whole knob row would leave the roll under 110 px); Linux's wider fonts wrap its first line, so two whole
  rows show there. Of 180 cases (5 tabs, 900 and 1100 wide, 560 to 900 high) 3 fold on Windows and 1 on Linux.
- **Decision 6, the 75 px roll at 900 x 560 with a drawer open** (S1's and S2's note): with the tabs folded and a drawer
  open, a window too short for the roll's floor gives the inspector's line to the roll (`isInspectorFolded`): at 900 x
  560 the roll has 137 px (a 107 px canvas) instead of 105 (75). The inspector comes back when the drawer closes or the
  window grows. The prototype keeps its roll at 150 px there by shrinking the waveform to 40 px and then scrolling the
  page; the plugin's page cannot scroll, and the strip's sound column (three 26 px buttons) needs its 98 px. The other
  rows were not touched: the tab row is how the tabs come back, the hint bar shows the hovered control's text, and the
  mode bar's second row at 900 px is M0's.
- **Decision 6, the bar numbers (UI4-6):** the zoom buttons H−, H+, V− and V+ moved from the ruler's right end to the
  tab row, just left of Hide / Show (22 px high there), and the ruler runs the whole width, so no bar number is hidden.
  The roll view still owns them (MainView takes them as its children; a roll view on its own, as in the tests, keeps
  them in its ruler). They hide while the Diagnostics tab takes the roll's place. The tab row was chosen because it
  sits right above the roll on a first open (the tabs folded), near where the buttons were; the top bar and the
  selection row have no room for them at 900 px.
- **The M5 table's pictures** are `m5_table_shapes_drawer_*` (the Shapes drawer on Volume), `m5_table_hover_ghost_*`
  (the mouse on Ramp up: its ghost and its words in the hint bar), `m5_table_box_select_*` (a Shift box in Tone
  adding to two selected notes, held), `m5_tab_copy_paste_*` and `m5_tab_slides_*`, dark and light at 100 %; S1's
  `m5_box_select_*` and S2's `m5_ghost_*` stay. `m5SnapshotPictures` checks what each must show, so the table's
  snapshot row is gated too.

### For the architect

- **Paste notes' place** (the prototype has no note paste): after the selected notes, as Duplicate, or at the right-
  click menu's Paste notes here. A key for it (Ctrl+C and Ctrl+V are the prototype's lane keys) or a paste at Cubase's
  cursor would need a decision.
- **A tab on a window too short for it** folds, and a click says why; nothing forces it open. Letting a click open it
  anyway (the roll then under its floor) is the other way.
- **The inspector's line gives way to an open drawer on a short window.** The alternatives touch M2's waveform strip
  (a shorter strip with a compact sound column), or the mode bar's second row at 900 px (M0's).
- **The zoom buttons in the tab row.** The prototype has them in its top bar, which has no room at 900 px (M4 R2).
- **The clipboards live with the window:** closing the StrandLock window forgets what was copied. Keeping them in the
  processor (or sharing them between StrandLock instances) is a small change if Kalan wants it.

### CI results

- Run 44692540376 on 9d6360a (the first push): both platforms built it the first time. Red: 1 of 1797 checks on Windows
  and 3 of 1789 on Linux, all in `decisionSixLayout`, all the test's own assumptions about layouts that depend on the
  fonts: the Tools drawer without a tool panel is only 40 px, so at 900 x 560 the roll keeps 129 px and nothing needs
  to fold (the test now opens Walk's panel, as S1's small-window test does, so the drawer takes its 62 px); and Linux's
  wider fonts wrap the Sample tab's first line, so two whole rows fit at 900 x 560 and it does not fold there (the test
  now works out from each panel's own rows whether each fold was needed, and finds a folded tab to click). Every other
  step green: the fixture check, the Debug-CRT allocation run 38 checks in 7 tests, the Debug pictures with 0 JUCE
  assertions, the sanitizers' groups 808 in 139 and 142 in 20, pluginval strictness 10 SUCCESS. The `ui` group: 92
  tests, 457 checks (S2: 84 and 408).
- Copy / paste (run 44692540376, the same on both platforms): Paste lanes from the 1-beat source onto notes of 4, 0.5
  and 2 beats: 30 of 30 lanes the source's, the node times bit for bit, so its bend node (t 0.3) lands at beats 3.20,
  7.15 and 12.60 of notes 2-6, 7-7.5 and 12-14; one step "Paste lanes", the bytes of `model::pasteLanes`; Undo puts the
  targets' own lanes back. With only Pitch and Tone chosen ("holding: Pitch, Tone") 2 lanes change and 8 keep the
  note's own. Duplicate of A -> B, C -> D (D not selected) and E: the copy of A slides into the copy of B, the copy of C
  into none, the originals keep theirs, the copies start at beat 3 where the group ends; the bytes of
  `model::duplicateNotes`. Paste notes after E: the copies start at beat 2, links as Duplicate, "Pasted 3 notes"; at
  beat 30.5 of 32: 2 of 3 copies, the second cut to 0.5 beat, "Pasted 2 of 3 notes: the rest would start after the
  clip's end"; past the end nothing and "No room there: add bars first". The right-click menu has no Paste notes here
  until notes are copied; then its copies start on the grid line at the click (beat 20.5 for a click at 20.6).
- Caps: Duplicate of 97 notes on a 4000-note clip refused, "That would make 4097 notes; the limit is 4096.", the bytes,
  the clip version and the selection unchanged, 0 undo steps; 96 done (exactly 4096 notes, one step). Paste notes the
  same (97 refused, 96 done). In the editor the tab's Duplicate and Paste notes past the cap show the toast in the
  window and hand nothing to the SnapshotPublisher.
- The Copy / Paste tab in the editor: 10 chips lit; Pitch off writes UIST copyMask 0x3fe and a new window shows it off;
  Copy then reads "holding: 9 lanes" and Paste onto 2 notes is the bytes of the direct call without Pitch; Copy notes
  ("holding: 2 notes"), Paste notes after the selected notes (t1 ends at beat 7.5) and Duplicate, one step each.
- The Slides tab: AUTO SLIDE offers Off, Legato (touching notes), Always (next note); SLIDE TIME 1/32 .. 1/2 shows 1/8;
  Always picked is one host gesture and one undo step "Auto slide", Undo gives Legato back; Slide time 1/4 one gesture,
  one step "Slide time"; Cubase's Off and 0.7 beats wait for the 30 Hz timer, then both move ("Off", "0.700 beats").
  SELECTED NOTES: greyed and "—" with nothing selected (a press changes nothing); "auto" at 0.5 for a note that
  follows Slide time; 3 moves are 3 of 3 the bytes of `model::updateNotes` on the clip the drag started from, 3 clips
  handed to the SnapshotPublisher and no undo step; the release one step "Slide time (notes)" ("0.75 beats"); Jump, no
  slide ("jump") and Auto ("auto") one step each; Undo walks back to the clip before the drag.
- Decision 6: a new instance folded (UIST 1, the roll 352 px at 1100 x 680); the Sample tab opened gives the pane 148
  px and the roll 204; a project saved with Macros open opens with it open. 180 tab and window cases (5 tabs, 900 and
  1100 wide, 560 to 900 high): on Windows 177 show whole rows and 3 fold, on Linux 179 and 1 (run 44698619192; with
  the Copy / Paste tab in one wrapping line 176 and 4, 178 and 2); 0 cut a row, 0 leave the roll under 110 px, 0 fold
  where a whole row fits. At 900 x 560 with the banner the tabs show folded and UIST keeps the Sample tab open; at 900
  x 640 the pane scrolls whole rows with its thumb (116 px of 282 on Windows, 70 of 392 on Linux). The zoom buttons:
  4 of 4 in the tab row, 0 over the ruler, the ruler reaching the canvas's right edge (876 >= 866 at 900 x 560), H+
  1.3 times. With Walk's panel open at 900 x 560 the roll's canvas has 107 px on both platforms (S1 and S2: 75).
- The pictures (run 44692540376's artifact, 88), compared pixel for pixel with S2's (run 44681435341): 10 new, 76
  changed, 2 the same (the Diagnostics pages: the tab hides the zoom buttons). The changes are the ones meant: the zoom
  buttons in the tab row (every picture with the roll; at 900 x 560 bar 4's number now shows), the first view centred
  in the folded roll (a new instance is folded, so the roll pictures that fold the tabs start where the first view now
  puts them), the default pictures (strandlock_*, strip_*) with the tabs folded and a big roll, and at 900 x 560 with
  a drawer open no inspector line. Looked at: the 10 new ones whole, the changed ones in 2 x 2 sheets and the 200 %
  tab row in a crop. The Copy / Paste tab: LANES TO COPY, the ten chips in their colours (Vibrato and Hollow off:
  dark with their coloured edge), Copy, Paste, HOLDING: 8 LANES; NOTES, Copy notes, Paste notes, Duplicate, HOLDING: 1
  NOTE (shell.html's copy pane). The Slides tab: AUTO SLIDE (Legato (touching notes)), SLIDE TIME (1/8), a line,
  SELECTED NOTES with the slider at 0.75 beats, Auto, Jump, no slide (shell.html's glide pane); at 150 % greyed with
  "—". The Shapes drawer on Volume, the hover ghost of Ramp up with its sentence in the hint bar (ghost.png), the box.
  Nothing cut, overlapping or off-palette. One picture improved for the next run: S1's Chop refused picture lost its
  notes from view (its clip sits lower than the first view now centres), so it scrolls to them.
- Run 44695294607 on ba326b7, green on every job and step: Windows Release 1797 checks in 303 tests, Linux Release
  1789 in 303, the sanitizers' groups 808 in 139 and 142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE
  assertions), the Debug pictures 0 JUCE assertions, pluginval strictness 10 SUCCESS, the fixture check passed (4.87 MB
  of the 5 MB budget; S3 adds no fixtures). The `ui` group: 92 tests, 457 checks on both platforms (S3 added 8 tests
  and 49 checks). Decision 6's sweep: 0 cut rows, 0 rolls under 110 px, 0 needless folds; folded on Windows: 900 x 560
  Sample, Macros and Copy / Paste, 900 x 580 Sample; on Linux: 900 x 560 Macros and Copy / Paste. The first folded
  tab's click said the sentence and kept the choice, and at 900 x 900 the tab opened by itself. With Walk's panel open,
  900 x 560 to 760 in 8 px steps: the inspector's line went to the roll at 1 height (560: the roll 137 px) and stayed
  at 25, 0 wrong, on both platforms.
- Run 44698619192 on 578967f (the Copy / Paste tab in two rows), green on every job and step, with the same counts:
  Windows Release 1797 checks in 303 tests, Linux Release 1789 in 303, the `ui` group 92 tests and 457 checks on both
  platforms, the sanitizers' groups 808 in 139 and 142 in 20, the Debug-CRT allocation run 38 in 7 (0 JUCE
  assertions), the Debug pictures 0 JUCE assertions, pluginval strictness 10 SUCCESS, the fixture check passed (4.87
  MB of the 5 MB budget). Decision 6's sweep: on Windows 177 of 180 cases show whole rows and 3 fold (900 x 560
  Sample and Macros, 900 x 580 Sample), on Linux 179 and 1 (900 x 560 Macros); 0 cut a row, 0 leave the roll under
  110 px, 0 fold where a whole row fits; Copy / Paste and Slides show at 900 x 560 on both. The first folded tab's
  click (Sample on Windows, Macros on Linux) said the sentence and kept the choice, and at 900 x 900 the tab opened by
  itself (258 and 186 px). The inspector's line and the zoom buttons as in run 44695294607.
- Its pictures against run 44692540376's: 83 of 88 the same, 5 changed, the 5 meant, each looked at. The Copy / Paste
  tab (dark and light): LANES TO COPY and the ten chips on the first row; Copy, Paste, HOLDING: 8 LANES, a line,
  NOTES, Copy notes, Paste notes, Duplicate and HOLDING: 1 NOTE on the second, the toast "Copied 1 note" over the
  roll. The box select (dark and light): in Tone, a Shift box held across the notes, the selection row reading 6 notes
  selected. Chop refused (light): the warning in the drawer and the notes in view under it. Nothing cut, overlapping
  or off-palette.

## M5 summary

M5 (the selection tools) is built in three parts on `m5-select`, each fast-forwarded to main when green: S1 box select
and the selection bar's direct actions; S2 the drawers, the lane buttons' menus and the hover ghosts; S3 the Copy /
Paste and Slides tabs, M4's layout leftovers (decision 6), the M5 pictures and the checklist. What Kalan gets: a box
on empty space that selects the notes it meets in every mode (Shift adds, Ctrl toggles) and, with the Nodes switch,
picks the selected notes' nodes to drag together; the selection bar under the selection row (LANE, Shapes with Each
note / Across, Pattern, Random with Re-roll, Turn into nodes, Connect with Nearest / Furthest / Random / Re-roll /
Disconnect, Tools with Walk, Chop, Reverse, Flip, More, Less, Select similar, Humanize, Strum and Legato); the lane
buttons' menus (▾); a dashed preview of what a shape, pattern or tool would do while the mouse is on it; the
Copy / Paste tab (the lanes to copy, Copy and Paste lanes, Copy notes, Paste notes and Duplicate) and the Slides tab
(Auto slide, Slide time, and the selected notes' own slide time); a new instance with the tabs folded, tabs that fold
rather than cut a row on a short window, and the zoom buttons in the tab row.

- **The plan's M5 table, where each row is checked.** Tools through the UI layer: S1's `selectionToolsFixtureCases`
  and `selectionToolsSweep` (71 of 71 tools.json cases and 378 of 378 sweep runs the bytes of the direct model call)
  and S2's `selectionToolsDrawerSweep` (6075 of 6075). Box select: S1's `boxSelectSeededBoxes` (160 of 160 seeded
  boxes, plain, Shift and Ctrl). Ghosts: S2's `ghostsChangeNothing` (8769 hovers, 0 undo steps, 0 clip versions) and
  `ghostsInTheEditor` (0 submits to the SnapshotPublisher). Live sliders: S2's `selectionToolsGenLive` (a Pattern
  slider live, 1 undo step on release) and S3's `slidesTabSelectedNotes` (the slide time slider the same way). Copy /
  paste: S3's `copyPasteLanesStretch`, `copyPasteNotesKeepLinks`, `copyPasteRefusedPast4096` and
  `copyPasteTabInTheEditor`. Snapshots: S3's ten `m5_table_*` and `m5_tab_*` pictures, checked by
  `m5SnapshotPictures`, with S1's and S2's pictures beside them.
- **Decisions 1 to 6** (plan 7.7): every tool one model call and one undo step (S1, S2, S3); ghosts drawn, never
  applied (S2); Random stays put until Re-roll (S1, S2); Auto slide and Slide time bound the M2 way (S3,
  `slidesTabAutoSlideAndTime`); the 4096 cap on paste and duplicate (S3); M4's layout leftovers (S3,
  `decisionSixLayout`).
- **Deviations from the plan and the prototype, all in the parts' notes:** the Connect drawer's warning without its
  red mark (S1; the mark waits for M6, S3); the menus' items without tooltips (S2); a drawer's least room 62 px, not
  60 (S1); Paste notes, which the prototype does not have, placed as Duplicate places its copies, and Paste notes here
  in the right-click menu (S3); the Copy / Paste tab in two rows (S3); the tabs folding on a short window where the
  prototype cuts a row, and the inspector's line giving way to an open drawer there (S3); the zoom buttons in the tab
  row (S3).
- **For the architect:** see S1, S2 and S3's lists; a key for Paste notes is still open (S3).
- **Handed to M6:** the recipe buttons' hover (`SelectionTools::previewRecipe`, hooked with `HoverHooks`); the
  per-note decorations of sl-ui-panels.js decorate (UI4-9's chevrons and thumbnails, the own-settings dot, and the
  warning mark where a slide goes into a note that starts well inside the one before it); the Note settings tab.
