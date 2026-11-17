# NOTES-M4

Builder notes for M4 (PLUGIN-PLAN section 4 "M4 Roll UI" and 7.6). Append only: one bullet per item, with the reason.
Each part writes its own section: R1, R2, R3, Fix.

## R1

The headless half of the roll (plan 7.6 R1): geometry, gestures, the document's roll state, undo steps, the
inspector's pure helpers and key routing. Branch `m4-roll`.

### Progress

- Done:  and the roll state in  (selection, primary, mode, sub-lane, the
  inspector's node), , ,  (the six pure helpers),
  ,  (geometry and gesture fixtures recorded from sl-ui.js itself), and the
  tests ( green in CI run 44135943909; , ,
  ,  written).
- In progress: the first CI round on the gestures, the geometry and the keys.
- Next: CI green, the decisions and figures below, the hand-off for R2 and R3, rebase and merge.
