# NOTES-M4

Builder notes for M4 (PLUGIN-PLAN section 4 "M4 Roll UI" and 7.6). Append only: one bullet per item, with the reason.
Each part writes its own section: R1, R2, R3, Fix.

## R1

The headless half of the roll (plan 7.6 R1): geometry, gestures, the document's roll state, undo steps, the
inspector's pure helpers and key routing. Branch `m4-roll`.

### Progress

- Done: `doc/RollState.h` and the roll state in `ClipDocument` (selection, primary, mode, sub-lane, the
  inspector's node), `ui/roll/RollGeometry.h` (declarations), `ui/roll/InspectorText` (the six pure helpers),
  `tools/fixtures/roll.js` (geometry and gesture fixtures recorded from sl-ui.js itself).
- In progress: the first CI round on this work.
- Next: RollGeometry.cpp, RollGestures, KeyRouter, the four test files, then CI green and the merge.
