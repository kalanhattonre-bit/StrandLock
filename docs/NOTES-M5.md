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
- In progress: the first CI run.
- Next: CI green, the pictures looked at, the figures below, then the fast-forward of main.

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

- **The bar's line is always there.** The prototype shows the bar only while notes are selected (or a drawer is
  open), and moves the roll back under the mouse while the bars above it change (sl-ui.js keepRollStill). Here, as
  M4 did for the inspector's line, the line stays: with nothing selected and no drawer open it says how to select
  ("Select notes (click one, or drag a box around several) to use the selection tools."), so selecting a note never
  moves the roll under the mouse. The roll gives up the line's 30 px. An open drawer takes room under the line until
  its button closes it (a click on the bar, as in the prototype); with nothing selected it stays where it is, faded
  and asleep, as the prototype keeps it.
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

### For the architect

- The bar's line costs the roll 30 px on every window; on a small window with a tab open the roll is small. Decision
  6 (S3) folds the tabs on a first open.
- The inspector's group value for several picked nodes (sl-ui-panels.js commitNodeValue with a group) is left for a
  later part (S2 or the fix pass); the Nodes switch works without it.

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

- (to come)
