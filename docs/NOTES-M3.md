# NOTES-M3

Builder notes for M3 (PLUGIN-PLAN section 4 and 7.5). Append only: one bullet per item, with the reason. Each part
writes its own section: G, L, B, Fix.

## G

M3 part G of 3 (plan 7.5 "G"): the grain engines Voice and Cloud. Branch `m3-engine`.

### Progress

- Done: `engine/VoiceGrains.cpp` (Voice::runGrains), the dispatch in Voice::renderRun, `playedEngine` playing
  Voice and Cloud as themselves (Lock still as Tape), the stand-in lines on the Diagnostics page and in the Copy
  report, `Stats::tapeStandIns` (now Lock only), the renders test for the grains cases, the stand-in tests.
- In progress: CI on the first push.
- Next: the M3 table rows (tuning, formant, glide, motions, Scatter, Refresh and Skip, Spray, the grain chase).
