# NOTES-M8: Performance

Each part of M8 writes its own section here (PLUGIN-PLAN 7.10): W1, W2, W3, W4, Fix.

## W1: notes that hold on across the loop end (F3) and Vibrato Sync

### Progress

- Done: the worktree and branch `m8-live` from main 8fdd6e4; `Tests/engine/test_vibsync.cpp` first, on the unchanged
  engine, so CI prints the M7 build's hash of the Free vibrato render (the "bit for bit with M3" reference).
- In progress: F3 in the model (glides and chains across the loop) and the engine (effLen, the chain links that wrap).
- Next: `Tests/engine/test_tie_loop.cpp`; the switch and the Vibrato sync control on screen; the recorded hashes; CI
  green; this section's decisions and figures; the fast-forward of main.
