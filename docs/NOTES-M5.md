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
