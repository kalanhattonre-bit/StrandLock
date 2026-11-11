# NOTES-M3

Builder notes for M3 (PLUGIN-PLAN section 4 and 7.5). Append only: one bullet per item, with the reason. Each part
writes its own section: G, L, B, Fix.

## G

M3 part G of 3 (plan 7.5 "G"): the grain engines Voice and Cloud. Branch `m3-engine`.

### Progress

- Done: `engine/VoiceGrains.cpp` (Voice::runGrains), the dispatch in Voice::renderRun, `playedEngine` playing
  Voice and Cloud as themselves (Lock still as Tape), the stand-in lines on the Diagnostics page and in the Copy
  report, `Stats::tapeStandIns` (now Lock only), the renders test for the grains cases, the stand-in tests.
  CI run 43091177044 on 19d90f3 green on every job: 28 of 28 grains cases within tolerance.
- Done (not yet through CI): the M3 table rows in `test_tuning_grains`, `test_formant`, `test_motions`,
  `test_refresh_spray`, `test_chase_grains`, with the rig `Tests/engine/GrainRig.*`.
- In progress: CI on the test commit.
- Next: the notes (decisions, figures, hand-off for L), then the merge to main.
