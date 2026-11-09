# StrandLock handoff: how to use this package

1. On GitHub, create a new **empty** repo named **StrandLock** (same account as GrainLock).
2. Upload this whole folder's contents into it. On github.com, use **Add file → Upload files**, then drag in `PROMPT.md`, `HOW-TO-USE.md` and the `reference` folder. Commit to `main`.
3. Open Claude Code, connect it to the StrandLock repo, and paste in everything below the line in `PROMPT.md`.
4. When CI goes green, download the build from the repo's Actions or Releases tab, the same way you got GrainLock.

What's inside `reference/`:
- `prototype-v3-strandlock.html`: the working spec. Open it in any browser to play with it.
- `prototype-v2.html` and `prototype-v1.html`: earlier versions, for history.
- `screenshots/`: the v3 look in dark and light themes, plus the LINES screenshots that inspired the idea.
