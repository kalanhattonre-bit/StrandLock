# NOTES-M8: Performance

Each part of M8 writes its own section here (PLUGIN-PLAN 7.10): W1, W2, W3, W4, Fix.

## W1: notes that hold on across the loop end (F3) and Vibrato Sync

### Progress

- Done: the worktree and branch `m8-live` from main 8fdd6e4. `Tests/engine/test_vibsync.cpp` ran first on the unchanged
  engine (run 45489545875): the M7 build's Free hash is 578be5fd on Linux (Release and sanitizers).
- Done (not yet through CI): F3 in the model (`model::Glides`: glides and chains across the loop, `buildChainLinks`),
  the snapshot's wrap flags, the engine (`Clock::effLen`, the chain steps that wrap in tick, ffChain, startClip and
  doChase), `Tests/engine/test_tie_loop.cpp`, the shared render comparison (`Tests/engine/RenderCompare.*`), the
  Slides tab's "Hold over the loop end", the Sound tab's Vibrato sync beside Vibrato speed, the roll's vibrato drawn at
  the synced rate, `Tests/ui/test_loop_vibrato_controls.cpp`.
- Next: the Windows hash from run 45489545875 into test_vibsync; CI green; the snapshots looked at; this section's
  decisions and figures; the fast-forward of main.
