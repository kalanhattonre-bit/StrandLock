# Build prompt: StrandLock, a playable sample instrument with drawable per-note evolution (VST3, Windows)

Paste everything below the line into a Claude Code session connected to the **StrandLock** GitHub repo. The `reference/` folder from this handoff package must already be in that repo.

---

## 1. What you're building

**StrandLock** is a VST3 **instrument** for Cubase Pro on Windows x64. It's the fourth plugin in Kalan Hatton's "Lock" suite, after GrainLock, CycleLock and PulseLock. The user drops in any sample, short or long, and plays it from a keyboard. Every note is drawn on a piano roll as a **strand**, a green line, with translucent colored ribbons stacked on it. Each ribbon is a curve the user can draw for one thing about how that note evolves over time:

- pitch / bend
- volume
- vibrato
- tone
- formant
- hollowness

Notes can bend into each other through glides that are fully adjustable. The goal is to take ordinary sounds and make them into stranger, playable sounds.

The visual idea was inspired by a beta plugin called LINES (screenshots are in `reference/screenshots/inspiration-lines-*.jpg`). **Do not copy LINES' name, layout, colors or branding.** StrandLock's look comes from the Lock suite (section 3).

## 2. Your sources of truth (read these first)

1. **`reference/prototype-v3-strandlock.html`** is a working browser prototype. It's the **behavioral spec** for the UI, the interactions, the data model and the formant-preserving engine. Open it with Playwright, click through it and read its JavaScript. Port its behavior faithfully, not its code style. Where this prompt and the prototype disagree, **this prompt wins**.
2. `reference/prototype-v2.html` and `reference/prototype-v1.html` are earlier versions, kept for history only.
3. `reference/screenshots/v3-*.png` show the intended look in dark and light themes, in Build mode and in a lane mode, on desktop and phone widths.
4. **The suite repos (public; clone them read-only):**
   - <https://github.com/kalanhattonre-bit/GrainLock>: the closest template. Copy its CMake layout, JUCE pin, CI workflow, offline test runner, UI snapshot and sound-fingerprint steps, pluginval step, README style, `Theme.h`, LookAndFeel, `paintLogo`, caption strips and knob controls.
   - <https://github.com/kalanhattonre-bit/PulseLock>: its `ui/PatternEditor` is the suite's existing node/lane editor. Reuse its interaction conventions and code where they fit, especially the **bend diamond** on a sloped segment, Alt-drag to bend, and one gesture = one undo step.
   - <https://github.com/kalanhattonre-bit/CycleLock>: a third example of the shared theme and logo.

## 3. Suite identity (must match)

- **Company and bundle:** `COMPANY_NAME "Kalan Hatton"`, `PLUGIN_MANUFACTURER_CODE Khtn`, `BUNDLE_ID com.kalanhatton.strandlock`, `PLUGIN_CODE Stlk`, `PRODUCT_NAME "StrandLock"`. Use the same JUCE 8 tag GrainLock pins, plus C++20 and the static MSVC runtime.
- **Logo:** the family mark from `paintLogo`, a 2.2 px ring with one dot riding it at 0.28π, drawn in StrandLock's accent. Put it next to the wordmark **`STRANDLOCK`** in `Theme::font(17, bold, 0.2 tracking)`, at the same position as the other plugins.
- **Dark theme:** the suite's exact surfaces and text colors from `Theme.h`:
  - background `#151618`
  - display `#0F1012`
  - panel `#1D1E21`
  - panelRaised `#27282C`
  - outline `#33353A`
  - track `#3A3C42`
  - text `#ECE8E1`
  - textDim `#9A9EA6`
  - textFaint `#5D6169`
- **StrandLock's accent:** orchid **`#B58CFF`**, with `onAccent` `#170A2A`. The other accents are amber for GrainLock, lime for CycleLock and teal for PulseLock.
- **New for the suite: a light theme.** Extend `Theme` so it holds two palettes and a runtime switch, while keeping every color in `Theme.h`. A ☀/☾ toggle in the top bar switches themes. The choice is saved per-user in `juce::PropertiesFile`, so it sticks across projects, and it's also mirrored in plugin state. Light values are in the prototype's CSS `body.light` variables and its `THEMES.light` object. The light accent is `#7B4FE0`.
- **Fonts:** keep `Theme::font` (Segoe UI on Windows).
- **Lane colors** are the same in both themes. Only the alpha changes, and the prototype's `THEMES.*.alphaA` and `alphaI` give it.

| Lane | Color |
|---|---|
| Pitch (the strand) | `#2ECF98` (dark-theme line `#3EE6A8`, light-theme line `#0B9E74`) |
| Volume | `#4F8BFF` |
| Vibrato | `#A66BFF` |
| Tone | `#FF9F1C` |
| Formant | `#FF4D8D` |
| Hollow | `#22C3E6` |

## 4. Plugin type, routing and timing

- VST3 only, Windows x64. Settings: `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, stereo out, zero latency (the granular engine renders ahead per voice, see 8.2). Load it on a Cubase **Instrument track**.
- **Tempo and position always come from the host** through `AudioPlayHead`: bpm, ppqPosition and isPlaying. There is **no tempo control**. Show the host tempo read-only.
- StrandLock owns an internal **clip** (the notes on its piano roll). It plays in sync with the host transport and loops over a clip length of 1–32 bars. The loop start is aligned to host bar 1. Add an "Offset bars" option.
- **Live MIDI** from the track also plays the sample in real time (section 9).
- **Rec** (arm in the plugin) captures, while the host plays, the incoming MIDI notes, both mod wheels and pitch bend into the clip as notes and lane curves. Quantize note starts to the grid if snap is on.

## 5. Data model (port it from the prototype)

Each note has:

- **`id`** (stable string)
- **`pitch`** (MIDI note number)
- **`start` and `length`** in beats
- **`glide`**: `null` means use the default, `0` means no glide, `>0` is the glide length in beats
- **`glideCurve`**: −1 to 1
- **`glideTo`**: the id of another note, or null. This is an explicit bend link.
- **`lanes`**: a map from lane id to a **node list** `[{t: 0..1, v: 0..1, bend: −1..1}]`, sorted by t. The first node is always at t = 0 and the last at t = 1.

How the lanes are interpolated:

- Segments use **monotone cubic** interpolation (Fritsch–Carlson). That's the prototype's `sampleLane`.
- A segment can instead be marked linear (`lin` in the prototype) or bent with PulseLock's bend shape. Store this as `bend` with `0 = default curve`, so it can be converted to and from PulseLock's model.

| Lane id | Default v | What v (0..1) does |
|---|---|---|
| `pitch` | 0.5 | offset of ±12 semitones: `(v−0.5)·24` |
| `amp` | 0.8 | gain `v²·1.3` |
| `vib` | 0 | vibrato depth, up to ±1 semitone (at the global vibrato rate) |
| `tone` | 0.75 | low-pass cutoff `60·2^(v·8.3)` Hz |
| `formant` | 0.5 | formant shift of ±12 semitones: `(v−0.5)·24`. True formant shift in the granular engine; a moving peaking EQ `250·2^(v·3.8)` Hz, +9 dB, Q 4 in the classic engine |
| `hollow` | 0 | peaking cut at 950 Hz, Q 0.8, gain `−v·28` dB |

Keep the lane list **data-driven** (a registry of lanes), because more lanes are coming. **Leave room for a future `scan` lane** (playback position, direction and speed inside the sample). It's out of scope now, but the voice and lane architecture must be able to take it without a rewrite.

The clip, the sample reference and the UI state live in the plugin's state ValueTree, not in parameters. Undo uses `juce::UndoManager`, with one finished gesture equal to one undo step.

## 6. Modes: this is the core of the UX

There are **seven modes**, picked with buttons that show their hotkeys, or with keys **1–7**:

1. **Build**
2. **Pitch**
3. **Volume**
4. **Vibrato**
5. **Tone**
6. **Formant**
7. **Hollow**

**Build mode (1)** creates and edits the *structure* of the notes:

- **Double-click empty space** creates a note. Its length is one beat or the grid size, whichever is larger, and you can keep dragging to set the length.
- **Drag a note** to move it in time and pitch. It snaps to the grid, and to the scale when scale lock is on.
- **End dots:** the start dot is a ring and the end dot is filled.
  - Drag one **sideways** to change the start time or the length.
  - Drag it **up or down** to bend that end of the note. This sets the first or last pitch-lane node, in semitones when snap is on. There's a dead zone of about 0.4 row before the vertical drag kicks in.
- **Glide diamonds** (see 7).
- **Shift+click a second note** (or tap 🔗 "Bend into…" and then the target note) makes the earlier note **bend into** the later one. Doing it again on the same pair removes the link. A dashed accent-colored line with a 🔗 shows the link.
- **Ctrl+click** adds a note to the selection or removes it. On touch, a "Multi" toggle does the same.
- **Right-click** (or long-press about 550 ms on touch) opens the menu: Bend into…, Remove bend link, Copy lanes, Paste lanes, Duplicate, Glide off, Delete, and New note here.

**Lane modes (2–7):**

- **The notes are locked.** They can't be created, moved, resized or bent. Clicking a note only selects it.
- **Only the active lane can be edited.** The other lanes are drawn faint and can't be clicked. For example, in Volume mode nothing you do can change tone or formant.
- **Nodes:** drag a node (the first and last nodes are fixed at t = 0 and t = 1). **Double-click the lane** to add a node there. **Double-click a node** to delete it.
- **Segment bend diamonds** work the PulseLock way.
- **Freehand (F):** paint across the selected note. When you lift, the stroke is simplified into editable nodes (Ramer–Douglas–Peucker, epsilon about 0.012).
- **Right-click or long-press** gives the shape menu for the active lane: Ramp ↗, Ramp ↘, Swell, Dip, Wobble, Decay and Reset, plus copy/paste/duplicate/delete. The node values for each shape are in the prototype's `shapeNodes`.
- **Pitch mode** additionally shows the glide diamonds and allows Shift+click bend links.
- Trying to edit structure in a lane mode shows a short message: "Notes are locked in this mode — press 1 (Build) to add notes."

**Hint bar:** the bottom of the window always explains the current mode in one line. The text is in the prototype's `updateHint`.

**Keyboard focus:** take keyboard focus only while the mouse is over the roll, and never take the spacebar, because Cubase uses it for its transport. Shortcuts:

| Key | Action |
|---|---|
| 1–7 | modes |
| F | freehand |
| Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y | undo / redo |
| Ctrl+C / Ctrl+V | copy / paste lanes |
| Ctrl+D | duplicate |
| Ctrl+A | select all |
| Delete | delete |
| Esc | deselect |

## 7. Glide

- **Global auto glide:** Off / Legato (notes that touch, within about 0.13 beat) / Always (the next note within 4 beats). There's also a default glide time: 1/32, 1/16, 1/8, 1/4 or 1/2.
- **Voice leading:** when chords move, each ending note glides to the note at the matching rank in the next chord. Auto glide only ever targets notes within **±12 semitones**, so chords never dive into the bass.
- **Explicit `glideTo` links override auto glide** and still work when auto glide is Off.
- **Per-note override:** a slider, plus Auto and No-glide buttons.
- **Diamonds on the selected note:**
  - the **start diamond** sits where the glide begins. Drag it sideways to change the glide length.
  - the **mid diamond** bends the glide curve when dragged up or down. The shaping is in the prototype's `gShape`.
- The glide is drawn as part of the strand.

## 8. Sound engines

### 8.1 Classic (resample)

Plays the sample at the rate `2^((note + semis − root − cents/100)/12)` using 4-point Hermite interpolation, with the per-voice filter chain. The formant lane uses the moving peaking EQ in this engine.

### 8.2 Formant-preserving (granular), the default

Port the prototype's `renderGran` to real time, per voice. It's pitch-synchronous overlap-add:

- **Grains** are Hann-windowed and **2 source periods** long. The source period is `Ts = sampleRate / rootHz`.
- **Where grains are read:** the source read position advances with real time, but it's **quantized to whole multiples of Ts** so grains stay phase-coherent.
- **Spacing:** grains are placed at the target period `sr / f(t)`.
- **Formant shift:** each grain is read at the rate `F = 2^(formantSemis/12)`, so the formant moves while the pitch stays put.
- **Normalization:** multiply each grain by `min(2, 2·Pout/grainLen)`.
- **Real-time version:** keep a per-voice grain scheduler with up to about 4 active grains, all in preallocated buffers.

Both engines:

- **Per-voice chain:** hollow peaking cut → (EQ formant, classic only) → low-pass → gain → envelope. The envelope has a 6–10 ms attack and a 60 ms release, so there are no clicks.
- **Lane curves** are evaluated per sample or per small sub-block, and parameter changes are smoothed.
- **Vibrato** is the vib-lane depth × sin(2π·rate·t). The rate is in Hz or tempo-synced.
- **Sustain loop:** Start → end of the sample, with a crossfade length parameter.
- **Start offset:** set from the waveform strip.

General audio settings:

- **Polyphony:** 16 voices with oldest-voice stealing and a fast fade on steal.
- **Velocity → gain**, with a velocity sensitivity setting.
- **Output:** gain plus a soft limiter, as in GrainLock.

**Real-time safety, as in the suite:** no allocations, locks or logging in `processBlock`, and `ScopedNoDenormals`. The UI edits a copy of the clip and publishes an **immutable snapshot** that the audio thread swaps in atomically. Old snapshots are released off the audio thread, for example with a release pool.

### 8.3 Parameters (APVTS, automatable; IDs stay stable once shipped)

- `engine` (Formant/Classic)
- `outGain`
- `velSens`
- `polyphony`
- `vibRate`, `vibSync`
- `glideMode`, `glideTime`
- `sustainLoop`, `loopXfade`, `startOffset`
- `rootNote`, `rootCents`
- `scaleKey`, `scaleType`, `scaleLock`
- `mod1Target`, `mod2Target`
- `bendRange` (default ±2)
- per-lane **global offsets**: `ampOffset`, `vibOffset`, `toneOffset`, `formantOffset`, `hollowOffset`, each −1..1 and added on top of every note's curve. These let the DAW automate the whole clip.

## 9. Live play, mod wheels and MIDI

- **Live MIDI notes** play voices immediately. Lanes sit at their defaults unless a mod wheel is assigned to them.
- There are **two assignable mod wheels**. Each has a **target lane** (any lane, or Off) and **MIDI Learn** for its CC number. The defaults are CC1 → Vibrato and CC2 → Tone. A mod wheel takes over its lane only after it has moved. Live voices respond within about 15 ms.
- **Pitch bend** works over ±bendRange semitones.
- **With Rec armed**, the wheel movements, the bend and the note timing are written into the recorded note's lanes, simplified with RDP.
- **On-screen controls** for Mod 1, Mod 2 and a spring-back Bend slider live in the "Mod wheels" tab.
- **Velocity** scales the recorded note's starting volume.

## 10. Sample handling

- **Drag-and-drop** from Cubase (audio events and MediaBay) and from Windows Explorer, using `FileDragAndDropTarget`. Test that dragging an audio event out of Cubase gives a file path you can load. The waveform strip also has a **Browse** button.
- **Formats:** wav, aiff, flac, mp3 and ogg (`AudioFormatManager`). Samples can be any length; stream long files or cap memory at a sensible limit, and document that limit.
- **State:** save the file path, and **embed the audio in the plugin state** when the file is under 50 MB. Make this a toggle. If the file is missing on reload, show a "Locate sample…" prompt.
- **Waveform strip** at the bottom:
  - shows the waveform, the file name, the length and the detected root and key
  - has a draggable **start marker**
  - its space is reserved for the future Scan lane
- **Detection** runs on a background thread after each load and on demand ("🎯 Detect pitch & key"):
  - **Pitch:** YIN over the 5 loudest 2048-sample frames, with the median result, giving a root note and cents.
  - **Key:** a chroma vector from Goertzel energies (MIDI 36–95 over about 40 frames), correlated against the Krumhansl–Kessler major and minor profiles.
  - The results fill in Root, Key and Scale, and can be undone.
  - Unpitched samples should report "no clear pitch" rather than a wrong root.
- **Scale lock:** out-of-scale rows are shaded, the root rows are tinted, and new or moved notes snap to the scale. Scales: Chromatic, Major, Minor, Dorian, Mixolydian, Harmonic minor, Pentatonic major, Pentatonic minor.

## 11. Patterns, shapes, copy/paste

- **Pattern tab:**
  - shape: Sine, Triangle, Saw, Square or Random steps
  - rate, synced to beats: 1/2 to 1/32
  - center
  - **depth start → depth end**, so the peaks and valleys evolve across the note
  - **speed-up / slow-down**, a time warp
  - after **Apply**, slider changes update the selected notes live, and the first change is one undo step
  - for the pitch lane, depth is scaled to ±2 semitones at full
- **Apply to: selected / ALL notes** toggle.
- **Copy/paste:** chips choose **which lanes** to copy. Pasting stretches the curves to each target note's length. Duplicate keeps bend links inside the duplicated group.

## 12. Layout

The window is resizable. The default is about **1100×680**, it scales from 75% to 200%, and everything is drawn in code with the suite's LookAndFeel. From top to bottom:

1. **Top bar:**
   - logo and wordmark
   - Rec, Loop length (bars), Undo, Redo
   - Freehand, Bend into…, Multi
   - Grid (1/4, 1/8, 1/16, Free)
   - zoom H± and V±
   - theme toggle
   - preset menu
   - host tempo, read-only
2. **Mode bar:** Build plus the six lane buttons. Each button has its hotkey in a small badge and its lane color as the border. The active button is filled with its color.
3. **Tool tabs**, using the suite's caption-strip style: Shape · Pattern · Glide · Copy/Paste · Mod wheels · Sound (engine, root and cents, detect, key and scale, scale lock, sustain loop, loop crossfade, vibrato rate and sync, polyphony, output).
4. **The roll:**
   - a piano strip on the left that lights the held notes and can be clicked to audition
   - a bar ruler on top
   - the grid follows the theme
   - the playhead is drawn in the accent color
   - it scrolls and zooms
5. **Waveform strip.**
6. **Hint bar.**

How the roll draws each note: the ribbons sit above the strand, with the active lane on top and strongest. The strand is drawn with pitch and glide, and with **vibrato shown as a real squiggle** whose height is the depth and whose frequency is the vibrato rate. The start dot is a ring and the end dot is filled. The selected note shows the active lane's nodes, joined by a dashed line, plus its diamonds.

## 13. Presets

- **"Strand Demo":** the prototype's `demo()` clip.
- **Pad Swell**, **Pluck Decay**, **Vowel Morph** (a formant lane sweep), **Legato Lead** (Always-glide with vibrato swells) and **Hollow Drone**.

Presets load from the top-bar menu.

## 14. Verification (you can't listen, so prove it numerically, as in GrainLock)

Write an offline test runner that runs in CI and checks:

1. **Tuning, both engines:** play a synthetic harmonic sample with root C4. The output f0 must be within ±3 cents for C2, C4 and C6. For the granular engine, test this with the formant lane at its center.
2. **Formant engine:**
   - With the formant lane at +6 semitones, the f0 stays within ±3 cents while the spectral centroid of the envelope rises by more than 25%.
   - At −6 semitones, the centroid falls.
3. **Glide:** a legato C4→G4 glide reaches G4 within ±5 cents by the end of the note. An explicit `glideTo` link works with auto glide Off.
4. **Lanes:** the mapping functions match the table in section 5. Monotone interpolation never overshoots its neighbors.
5. **Mode lock:** a model-level test showing that edits in lane mode can't change start, length, pitch, or any other lane.
6. **Stability:** no NaN or inf values, and the peak stays ≤ 0 dBFS with the limiter, across a dense 16-voice clip.
7. **State:** save and restore round-trips the parameters, the clip (ids, links and nodes), the theme and the embedded sample.
8. **Allocations:** none on the audio thread over a 10-minute render, using the Debug-CRT allocation check like GrainLock.
9. **Host sync:** with a fake playhead at 120 and then 87 bpm, notes start within 1 sample of their ppq positions, and the clip loops seamlessly.
10. **UI snapshots** (uploaded as artifacts): **dark and light themes**, in Build mode and in the Tone lane mode, plus a narrow window size. Compare them against `reference/screenshots/v3-*.png` for layout intent.
11. **Detection:** synthetic tones are detected within ±10 cents. A C-major chord progression is detected as C major. Noise gives "no clear pitch".

Run the tests and **pluginval at strictness 10** in CI, with the same workflow structure as GrainLock: `windows-latest`, Release build, tests, snapshots, sound fingerprints, a Debug allocation check, pluginval, and a release created on `v*` tags.

## 15. README (written for a non-developer; mirror GrainLock's README)

1. How to download from Releases or Actions.
2. How to install into `C:\Program Files\Common Files\VST3` and rescan in Cubase.
3. **Setup:** create an Instrument track with StrandLock, drag a sample in, and play.
4. **The modes** (Build versus the lane modes), with a table of every gesture and hotkey.
5. **Mod wheel setup**, including MIDI Learn, and recording.
6. **How to report problems:** the Cubase version, sample rate, buffer size, the sample used, and what was heard.

## 16. Working style and milestones

Commit in small, logical steps, and keep CI green at every milestone:

1. Get a plugin that loads, with the suite's theme (both palettes), the logo and the empty layout.
2. Add the clip model, the undo system and the Build-mode roll, with the end-dot and glide-diamond editing. Snapshot test.
3. Add lane modes with the locking rules, node editing, freehand, shapes and patterns.
4. Add the classic engine and host-synced playback. Tuning and sync tests.
5. Add the granular formant engine. Formant tests.
6. Add live MIDI, mod wheels and Rec.
7. Add samples: drag-and-drop, embedding, the waveform strip and detection.
8. Finish with presets, the README, and pluginval at strictness 10.

**Out of scope for v1:** the Scan lane (design for it, but don't build it), MPE input (keep the per-note data model ready for it), and Mac/AU builds.

When you're done, report:
- what was built
- the CI status
- the download link
- known limitations
- any place you deviated from the prototype and why
- three settings for a first listening test
