# StrandLock: the Cubase checklist

The first test build (M0) was the "probe": the StrandLock window with its final look, all of its knobs, and a
plain saw sound you can play. It was there to find out how Cubase treats StrandLock on your computer, and its
steps below (The checklist) still apply. The M2 build plays the real sound engine: its own steps are in
**M2: the first real sound**. The M3 build adds the grain engines (Voice, Cloud and Lock): its steps are in
**M3: the grain engines**. The M4 build adds the piano roll, where you see, draw and change the notes: its steps
are in **M4: the piano roll**. The M5 build adds the selection tools (selecting with a box, the tools for the
selected notes, the Copy / Paste and Slides tabs): its steps are in **M5: selection tools**. The M6 build finishes
the Sample side (Sample mode, the live playheads on the waveform, the whole Sample tab with its recipes, Dice and
Preview, the Macros and Note settings tabs): its steps are in **M6: the Sample tab** at the end. Please go through
them and send back your answers and the text from the **Copy report** button.

## Getting the test build

1. Open https://github.com/kalanhattonre-bit/StrandLock/actions?query=branch%3Amain (you need to be signed in
   to GitHub).
2. Click the newest run at the top of the list that has a green tick. Its name is the last change that went in.
3. Scroll down to **Artifacts** and click **StrandLock-VST3-Windows**. A zip file downloads.
4. Unzip it. Inside is a folder called `StrandLock.vst3`.
5. Close Cubase. Copy the `StrandLock.vst3` folder into `C:\Program Files\Common Files\VST3`. Windows asks for
   permission; say yes. If an older `StrandLock.vst3` is already there, replace it.
6. Start Cubase. It scans new plug-ins when it starts. If StrandLock does not show up, open
   **Studio > VST Plug-in Manager** and press the rescan button (the circular arrow), then restart Cubase.

## Where things are in the StrandLock window

- **Top bar**, left to right: the purple ring logo and STRANDLOCK; from the M4 build the **Undo** and **Redo**
  arrows; the theme button (it shows **Light** with a sun while the window is dark, and **Dark** with a moon while
  it is light); the **Keys** button (a small keyboard picture; it turns purple when on); **TEMPO (HOST)**, the
  tempo Cubase is sending (read only); from the M4 build **GRID**; and **UI SCALE**, a drop-down from 75 % to
  200 %.
- **Mode bar**: **1 Build**, then 2 Pitch to 8 Sample in their colours, and the four small Sample buttons. In the
  M0 to M3 builds they only changed the line of help at the bottom. From the M4 build they change what the mouse
  does on the piano roll (see M4 below).
- **Tabs** (under the "No notes selected" row; from the M4 build under the inspector's line below that row):
  Sample, Macros, Note settings, Slides, Copy / Paste, Mod wheels, Sound and **Diagnostics**. Sample, Macros and
  Sound have working knobs (from the M5 build Slides and Copy / Paste work too). **Hide** on the right folds the
  tabs away; click it again (it then says **Show**), or click any tab, to bring them back. From the M5 build a new
  StrandLock starts with the tabs folded, and the zoom buttons **H−**, **H+**, **V−** and **V+** sit just left of
  **Hide** / **Show**.
- **The Diagnostics tab** fills the middle of the window. It has a **Copy report** button at its top right and
  these boxes: THIS COMPUTER, BLOCKS, POSITION AND TEMPO, MIDI, CUBASE'S CHANGES AND SAVING, KEYS and DROPS.
  Everything on it updates by itself while you play.
- **The waveform strip** near the bottom shows the sound that plays (the Saw).

## The checklist

1. **Your setup.** Which Cubase version (Help > About Cubase)? Which audio interface? Your sample rate and
   buffer size (Studio > Studio Setup > your ASIO driver > Control Panel). Is ASIO-Guard on, and at which level
   (Studio > Studio Setup > Audio System)? Your Windows display scaling (Settings > System > Display > Scale, for
   example 150 %).

2. **It loads.** Add an Instrument track and pick StrandLock. Does it appear under Instruments? Does the window
   open? Try **UI SCALE** at 75 %, 100 %, 150 % and 200 %: is the text sharp, and does the whole window fit on
   your screen? Drag the window's bottom-right corner to make it bigger and smaller: does everything stay tidy?
   Click the theme button to switch between dark and light. Close the StrandLock window and open it again, then
   open a new project with StrandLock in it: does it remember your theme and your UI Scale?

3. **It plays.** Play C3 on your keyboard (or on Cubase's on-screen keyboard). Do you hear a saw? Play a few
   chords. Is it in tune with a Cubase piano on another track? Open the **Diagnostics** tab and move the pitch
   bend and the mod wheel: do the **Pitch bend** and **Mod wheel** lines in the MIDI box change? Does the
   **Last note** line show the note you played?

4. **Keys, with Keys OFF.** Make sure the **Keys** button is not purple. Open the **Diagnostics** tab. Click
   once on an empty part of the StrandLock window, then press 1, 2, ... 8, Space, Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y,
   Delete, the arrow keys, and A to K. Look at the KEYS box: does **Keys received** stay at 0? Which keys did
   Cubase still react to (Space starts and stops Cubase, 1 to 8 change Cubase's tools)?

5. **Keys, with Keys ON.** Click **Keys** so it turns purple. Click once on an empty part of the StrandLock
   window, then press the same keys as in step 4. The KEYS box lists every key StrandLock received under
   **Last keys**. Which keys reached StrandLock? Which ones did Cubase still take first? Space should still start
   and stop Cubase. On the Diagnostics tab these keys change nothing in StrandLock, in the M4 build too: its
   shortcuts work only while the piano roll shows. (Turn Keys off again afterwards. StrandLock remembers the
   setting for the next window.)

6. **Dropping sounds.** From M2 a dropped sound also loads and plays (M2 step 4); this step is about what Cubase
   hands StrandLock. Drag a WAV file onto the StrandLock window from each of these: Windows Explorer;
   MediaBay; the Pool (Project > Pool); an audio event in the project window (a trimmed piece of a longer
   recording); a Cubase factory loop. The window turns purple with a dashed border while you drag. After each
   drop click the Diagnostics tab (from M2 it no longer opens by itself). In the DROPS box: which file name and folder
   did it receive? How long does it say the file is (frames, seconds, sample rate, channels)? For the trimmed
   event: is that the whole recording's length or only the trimmed piece? If a drop shows **Text** instead of a
   file name, please copy what it says.

7. **Playing with Cubase.** Press Play. In the POSITION AND TEMPO box, **Transport** says playing and
   **Position (ppq)** counts quarter notes from the start of the project (bar 1 is 0.0000, bar 2 is 4.0000 in
   4/4). Does it start at bar 1? Click any other tab (for example Sample): an orange line moves across the roll
   while Cubase plays (in the M0 to M3 builds the roll was empty; from M4 it shows the notes). Then click
   Diagnostics again. Set a 4-bar cycle and let it
   go round 3 times: does **Cycle wraps** go up? **Last wraps** shows the position just before and after each
   wrap: does the "before" look like 16.0000 or slightly off (for example 15.9970)? Does **Wraps inside a block**
   go up, and does it go up more with ASIO-Guard on? Add a tempo change on the tempo track: do **Tempo** and the
   **TEMPO (HOST)** box in the top bar follow it?

8. **ASIO-Guard.** Watch the BLOCKS box (**Size min / max / last**, **Sizes seen** and **Recent sizes**) with the
   track NOT record-enabled, then with the track's red record button on. Do the block sizes change? Does playing
   live feel delayed when the track is not record-enabled?

9. **While Cubase is stopped.** Stop Cubase. In the BLOCKS box, does the "stopped" number in the **Blocks** line
   keep going up? Try it with Cubase's option "Suspend VST 3 plug-in processing when no audio signals are
   received" (Studio > Studio Setup > VST Plug-ins) on and off.

10. **Quick Controls.** Open the track's Quick Controls in the Inspector. Are QC 1 to 8 already Speed, Spread,
    Heads, Refresh, Grain, Spray, Tone, Formant? Turn QC 1: does **Last parameter Cubase changed** (in the
    CUBASE'S CHANGES AND SAVING box) say "Speed = ..."? Does the first knob on the **Macros** tab, SPEED, move?

11. **Automation.** Write a little automation for the Speed knob (the first knob on the Macros tab), then play it
    back. Does the knob move?

12. **Saving.** Change a few knobs, save, close the project and open it again. Are the knobs and the theme as you
    left them? How big is the .cpr file? (The **Saved** line shows how many bytes StrandLock's own part takes.)

13. **Exporting.** Record a few bars of chords on the track, set the locators around them, and export them twice
    with File > Export > Audio Mixdown: once as usual and once with "Real-Time Export" ticked. Send both files.
    (They should be identical. During the first one, the **Offline (export)** line in the BLOCKS box should go
    up.)

14. **CPU.** Hold a 4-note chord and look at Cubase's Audio Performance meter (Studio > Audio Performance).
    Roughly how high is it?

15. **Copy report.** Open the **Diagnostics** tab and press **Copy report** (top right). A message says the
    report was copied. Paste it (Ctrl+V) into your reply.

If something on the StrandLock window looks wrong (text cut off, things overlapping, unreadable colours), a
screenshot helps a lot, with the UI Scale and theme you were using.

## M2: the first real sound

From this build on StrandLock plays with its real sound engine (the Tape engine). The steps above (M0) still
apply; this part is about the sound. Please go through it and send back your answers, the two exported files from
step 10 and the text from **Copy report**.

A few things to know first:
- **StrandLock now plays its own clip.** A new StrandLock opens on the demo clip (Strand Demo) and plays it in
  time with Cubase whenever Cubase plays. In the M2 and M3 builds you could not see or change the notes; from the
  M4 build the piano roll shows them (see M4 below). Your keyboard plays the sound too, at any time.
- **Only the Tape engine played in the M2 build.** (This was true of the M2 build only. From the M3 build on
  every engine plays itself; see the M3 part below.) On the Sample tab the Engine buttons are Tape, Voice and
  Cloud. In the M2 build, notes set to Voice or Cloud played as Tape, and the Diagnostics tab counted them. Tape
  plays the sound like a tape machine: higher notes play it faster.
- **The waveform strip** at the bottom has, on the left, the sound menu and **Browse...**; above the wave, the
  file line with **At the end**, **-**, **+** and **Show all**. The orange box on the wave is the **Region**, the
  part of the sound that plays. Moving lines on the wave show where each playhead is.

1. **Install the new build** the same way as before (Getting the test build, above): close Cubase, replace the
   old `StrandLock.vst3` in `C:\Program Files\Common Files\VST3`, start Cubase.

2. **The Saw that opens up.** Add StrandLock to an Instrument track. The strip shows "Saw that opens up". Play C3
   and then a few chords on your keyboard. Do you hear a saw that gets brighter? Is C3 in tune with a Cubase piano?
   Press Play in Cubase: do you hear the demo clip, in time with Cubase's metronome? Press Stop: does it stop
   cleanly (no click, no note left hanging)?

3. **The other starters.** Open the sound menu on the strip and pick each sound in turn (Saw, Square sweep,
   Evolving pad, Vowel aah to ooh, Pluck arpeggio, Beat loop, Noise). Does each one load quickly and play from
   the keyboard? Does the wave in the strip change to the new sound?

4. **One of your songs, three ways.**
   - Click **Browse...** and pick one of your own songs (a WAV, AIFF, FLAC, OGG or MP3).
   - Drag the same song onto the StrandLock window from Windows Explorer.
   - Drag it again from MediaBay.
   Each time: did it load? A short message appears in the middle of the window, near the top of the piano roll;
   what does it say? The file line shows the name and length. For a song longer than 20 seconds
   StrandLock starts with a 4-second part from about a third of the way in. For a song longer than 10 minutes it
   loads the first 10 minutes and says so. (The Diagnostics tab still lists each drop in its DROPS box.)

5. **Pick a part and play it.** On the strip, drag across the wave to make a new Region around a part you like
   (a chord, a vocal line, a drum hit). Drag the Region's edges to adjust it, or drag the band along its top to move
   the whole box. **-** and **+** zoom the wave, **Show all** shows the whole song again. Hold the mouse button
   down inside the Region to hear it from that point. Then play the part from your keyboard: C3 plays it as
   recorded, other keys play it higher or lower. Does it sound right? Does the Region box follow your mouse
   smoothly? (In the M2 and M3 builds StrandLock had no Undo of its own and Ctrl+Z went to Cubase. From the M4
   build the **Undo** button in the top bar undoes a Region change, and so does Ctrl+Z while **Keys** is on. With
   Keys off Ctrl+Z still goes to Cubase and undoes your last Cubase edit.)

6. **At the end.** Hold a key and try **At the end** on the file line: **Loop** (the part repeats with a soft
   joint), **Hold** (it stops moving at the end and keeps sounding), **Stop** (it goes quiet at the end). Do you
   hear a click at the loop joint with Loop? Does each one do what its name says?

7. **Motion.** On the Sample tab, try the **Motion** buttons while you hold a key: Forward, Backward, Bounce
   (forward and back inside the Region), Mirror (try it with the Heads knob at 2), Freeze (plays like Forward on
   Tape), Scatter (jumps around the part in time with Cubase; **Jump every** sets how often). Does each one sound
   different and do what you expected? Try **Heads**, **Spread** and **Drift** too.

8. **A 4-bar cycle while playing.** Set Cubase's cycle to 4 bars and press Play, so the demo clip goes round at
   least 4 times. Do you hear any click, gap, doubled note or stumble where the cycle jumps back? Try it again with
   a cycle that is not 4 bars long (for example 3 bars): the clip should jump back with Cubase each time. On the
   Diagnostics tab, does **Wraps inside a block** go up?

9. **Save and reopen with your song.** Load your song (step 4) and set a Region you like. Save the project, close
   it and open it again. Is your song back, with the same Region and the same knobs? Now move or rename the song's
   file in Windows Explorer and open the project once more: does a message say which file is missing, and does
   the Saw that opens up play instead? (Put the file back afterwards.)

10. **Export twice.** Record a few bars of chords on the track, set the locators around them (with the demo clip
    playing too), and export them twice with File > Export > Audio Mixdown: once as usual and once with
    "Real-Time Export" ticked. Send both files. They should be identical.

11. **CPU.** On the Sample tab set **Heads** to 8, then hold a 4-note chord while Cubase plays the demo clip. How
    high does Cubase's Audio Performance meter go (Studio > Audio Performance)? Does it crackle? Then stop, and
    press Play in the middle of the demo clip (not at bar 1) a few times: is there a crackle right as it starts?
    (Notes already under way have to catch up at that moment, which is the heaviest work StrandLock does. Your
    buffer size from the M0 part, step 1, tells how much room it has.)

12. **Copy report.** Open the **Diagnostics** tab and press **Copy report**. Paste the text into your reply.

## M3: the grain engines

From this build on every engine plays: Tape, Voice, Cloud, and Cloud with the Lock switch. The M0 and M2 steps
above still apply; this part is about the new engines. Please go through it and send back your answers and the
text from **Copy report**.

A few things to know first:
- **The default sound now plays with the Voice engine**, as in the prototype. (In the M2 build notes set to Voice
  or Cloud played as Tape; that is over.) Voice keeps the character of the sound when you play higher or lower.
  Tape plays it faster or slower, like a tape machine.
- **The controls are on the Sample tab.** First row: the **Engine** buttons (Tape, Voice, Cloud), the **Lock**
  switch, the **Motion** buttons and **Jump every**. Second row: the knobs **Speed**, **Heads**, **Spread**,
  **Voice grain**, **Cloud grain**, **Lock grain**, **Spray**, **Drift**, **Pitch**, **Refresh time** and **Skip**.
  Third row: the **Refresh** buttons (Smooth, Free, Synced), **Every**, **Lock to the beat**, **Tape**, **Each grain
  backwards** and **Head tune**.
- **Lock works with Cloud only.** With the Engine on Tape or Voice the Lock switch does nothing.
- **The budget.** StrandLock keeps its work under a fixed limit, so Cubase never runs out of time because of it.
  With many notes and many heads, the older notes' top heads fade out over 20 ms, and the newest note keeps its
  heads. So older notes may get thinner in a big chord. That is on purpose. A drop-out or a crackle is not.
- **Cuts.** On the Diagnostics tab, the **Engine health** line (THIS COMPUTER box) counts **cuts**: fade tails
  StrandLock had to stop at once because even the fades did not fit beside a new note. This build makes cuts on
  purpose in these cases, so a cut count above 0 is not a fault by itself:
  - when several notes start within a few milliseconds of each other while other notes sound (both hands
    landing a chord together, or a fast strum). This happens at any **Lock grain**, and with Voice and Cloud at 8
    heads too;
  - with **Lock grain** at 11 or more at 48 kHz (12 at 44.1 kHz), or 6 or more at 96 kHz: then nearly every new
    note cuts a tail while other notes sound, however far apart the notes are.

  What I need to know is whether you can hear them. A cut may sound like a small click right when a note starts.
  Steps 8 and 9 ask for the cut count and for what you heard.

1. **Install the new build** the same way as before (Getting the test build, at the top). On the Diagnostics tab,
   the **StrandLock** line in the THIS COMPUTER box should end with "(test build: Tape, Voice, Cloud and Lock
   engines)".

2. **The default sound, on Voice.** Add StrandLock to a new Instrument track. The strip shows "Saw that opens up"
   and the Engine is Voice. Play C3, then single notes up and down the keyboard, then a few chords. Does it sound
   like the prototype did? Is it in tune with a Cubase piano? Play C1 and C5: does the sound keep its character,
   without the "chipmunk" change Tape gives it? Press Play in Cubase: the demo clip now plays with Voice too.

3. **Tape, Voice, Cloud and Lock on a few starters.** From the sound menu on the strip pick **Saw**, **Evolving
   pad**, **Vowel aah to ooh** and **Noise** in turn. For each: hold C3 and then a chord, and switch the Engine
   between Tape, Voice and Cloud. Then, on Cloud, turn **Lock** on and off. Does each engine sound different? With
   Lock on, Noise should turn into a pitched tone that plays in tune: does it?

4. **On one of your songs.** Load one of your songs (**Browse...** or drag it in), set a Region around a part
   you like, and try the same four: Tape, Voice, Cloud, and Cloud with Lock. Which ones do you like? Does anything
   sound broken (clicks, buzzing, silence, a note that does not stop)?

5. **Motion on Voice and on Cloud.** Set the Engine to Voice, hold a key, and try each **Motion** button: Forward,
   Backward, Bounce, Mirror (set **Heads** to 2 or more), Freeze (the sound stands still on one moment, which Tape
   cannot do), and Scatter (it jumps in time with Cubase; press Play to hear it in time, and **Jump every** sets
   how often). Then the same with the Engine on Cloud. Does each one do what its name says?

6. **Heads, Spread and Spray.** On Cloud, hold a chord and turn **Heads** slowly from 1 to 8: the sound gets
   thicker, and between whole numbers the next head fades in. Do you hear a click or a jump while you turn it?
   Turn **Spread** up: the heads move apart in the sound and spread from left to right. Turn **Spray** up: the
   grains scatter around each head. Does each knob do what you expect?

7. **Refresh and Each grain backwards.** On Voice and then on Cloud, try the **Refresh** buttons. **Smooth**: the
   heads move on steadily. **Free**: each head grabs a moment and repeats it, with a new grab every **Refresh
   time**. **Synced**: a new grab every **Every** note value, in time with Cubase (press Play). With Free or Synced,
   turn **Skip** up: some grabs are left out. Then turn **Each grain backwards** on: every grain plays reversed.
   Does each one work?

8. **A big chord at your normal sample rate.** Set the Engine to Cloud, turn **Lock** on and set **Heads** to 8.
   Press Play so the demo clip plays, and hold a big chord (8 notes or more, both hands). Then play fast chords one
   after the other. Does anything drop out, crackle or stop? How high does Cubase's Audio Performance meter go
   (Studio > Audio Performance)? StrandLock has no CPU meter of its own, so this meter is the one to watch. Look at
   **Engine health** on the Diagnostics tab and write down the cut count. Cuts are expected when the notes of a
   chord land within a few milliseconds of each other (see "Cuts" above). Did you hear a click when a chord
   started? Now turn **Lock grain** to 12 waves and do the same. Here nearly every new note cuts a tail, so the
   count goes up with every note you play. How many cuts does it show now, and do you hear clicks as notes start?

9. **The same at 96 kHz, if your interface can.** Make a new empty project and set its sample rate to 96 kHz
   (Project > Project Setup, Sample Rate). Add StrandLock and repeat step 8. At 96 kHz StrandLock has half the room
   per sample, so more heads of the older notes fade out, and with Lock grain at 12 the newest note plays 4 of its
   8 heads. With Lock grain at 6 or more nearly every new note cuts a tail here. Does anything drop out or crackle?
   How high does the Audio Performance meter go? How many cuts does **Engine health** show, and do you hear clicks
   as notes start? Afterwards go back to your usual sample rate.

10. **Copy report.** Open the **Diagnostics** tab and press **Copy report**. Paste the text into your reply.

## M4: the piano roll

From this build on you can see the notes StrandLock plays and change them with the mouse. The steps above still
apply; this part is about the piano roll. Please go through it and send back your answers and the text from
**Copy report**. A screenshot helps whenever something looks wrong.

Where the new things are:
- **The roll** is in the middle of the window: the bar numbers along the top, the piano keys down the left, the
  notes in between. The four small buttons **H−**, **H+**, **V−** and **V+** at the right end of the bar numbers
  (from the M5 build they sit at the right end of the tab row, just left of **Hide** / **Show**)
  zoom in time and in height. The mouse wheel scrolls up and down; hold Shift to scroll sideways, or Ctrl to zoom
  in time.
- **Top bar**: right after STRANDLOCK are the **Undo** button (an arrow curving left) and the **Redo** button (an
  arrow curving right). Hover over them to see what they will undo or redo. Further right is **GRID** with
  1/4, 1/8, 1/16, 1/32 and Free: where notes land when you add, move or stretch them.
- **The row under the mode buttons**: on the left it says how many notes are selected ("No notes selected",
  "1 note selected"), then **Select all**, **Duplicate**, **Delete** and **Clear all**. At its right end are
  **Freehand**, **Slide into…** and **Multi**. They turn purple while they are on.
- **The line under that row** is the inspector: **START**, **LENGTH** (beats), **PITCH** and **VELOCITY** of the
  selected note. In the modes 2 to 8 it also shows **NODE**, with **AT** (beats in) and **VALUE**, for the node you
  last clicked. With no note selected it says "Select a note: its start, length, pitch and velocity show here, and
  you can type new ones."
- **The hint bar** at the bottom says what the mouse does in the mode you are in. When the line is cut short, hold
  the mouse over it to read all of it.

1. **Install the new build** the same way as before (Getting the test build, at the top).

2. **The demo clip as notes.** Add StrandLock to a new Instrument track. The roll shows the demo clip: green lines
   are the notes, with coloured bands above them for each note's lanes. Press Play in Cubase: does an orange line
   move across the roll in time with the music? Do the notes you see match what you hear? Scroll and zoom around
   with the wheel and **H−**, **H+**, **V−**, **V+**. Click a piano key on the left: do you hear that note?

3. **Make, move and stretch notes.** Make sure **1 Build** is lit. Double-click an empty part of the roll: a new
   note of one beat appears there and you hear it. Drag the note to another place: it moves in steps of the
   **GRID** (1/16 at first) and plays each new pitch as you pass it. A small box next to the mouse shows the note
   name and the bar and beat. (Notes will also snap to a scale once the Key and Scale controls arrive in M7.) Drag
   the small dot at the note's right end sideways: the note gets longer or shorter. Drag that dot up or down
   instead: the end of the note bends up or down in semitones. Try **GRID** set to 1/4 and to Free. Does all of
   this feel right? Is anything hard to grab? (Dragging on an empty part of the roll draws a dashed box, but it
   selects nothing yet: selecting notes with a box comes in M5.)

4. **Draw the lanes.** Click **2 Pitch**, then click a note to select it. The note's pitch line now has round
   nodes on it. Drag a node up or down: it moves in whole semitones (hold Shift for in between), and the small box
   says by how much. Double-click the line between two nodes to add a node there. Double-click a node to delete
   it (the first and last ones stay). Between two nodes there is a small diamond: drag it up or down to bend that
   part of the line into a curve (the diamond fills in); double-click a filled diamond to make it straight again.
   Now do the same in **3 Volume**, **4 Vibrato**, **5 Tone**, **6 Formant** and **7 Hollow**. Each mode only
   changes its own lane: the notes themselves stay where they are. Press Play: do you hear what you drew?

5. **Freehand.** In **3 Volume**, select a note and click **Freehand** so it turns purple. Drag across the note,
   up and down: you paint its Volume line. When you let go it turns into nodes. Click **Freehand** again to turn
   it off. (In **1 Build** Freehand says to pick a lane mode first.)

6. **Slide into another note.** In **1 Build**, click a note, then hold Shift and click a later note. A message
   says the first note now slides into the second, and a dashed line joins them. Press Play: does the first note
   glide into the second? With the second note selected, Shift+click the first one again: the slide goes.
   **Slide into…** does the same without Shift: select a note, click **Slide into…**, then click the note it should
   slide into.

7. **The right-click menu.** Right-click a note in **1 Build**: the menu has **Slide into the next note I click**,
   **Remove slide links**, **Jump, no slide**, **Copy lanes**, **Paste lanes**, **Duplicate** and **Delete**.
   Right-click an empty part of the roll for **New note here**. In a lane mode (for example **3 Volume**) the menu
   offers shapes for the selected notes: **Ramp up**, **Ramp down**, **Swell**, **Dip**, **Squiggle**, **Random**,
   **Decay** and **Reset**, with **Each note** or **Across** at the top. Try a few. Does each one do what its name
   says? A click anywhere else closes the menu, and so does Escape while **Keys** is on. Then turn **Keys** on,
   right-click a note, rest the mouse on **Delete** and press Space: Cubase should start (or stop), the note stays
   and the menu stays open. Does it?

8. **Undo and Redo.** After a few changes, click the **Undo** button a few times, then **Redo**. Each click
   should undo or redo exactly one change (one drag, one new note, one typed value), and a short message says
   which. Do they?

9. **Keys on and off.** With **Keys** off (not purple), click on the roll and press 2, Delete and Ctrl+Z: nothing
   in StrandLock should change, and Cubase gets the keys. Click **Keys** so it turns purple and click on the roll
   again. Now 1 to 8 change the mode, Ctrl+Z undoes, Ctrl+Shift+Z (or Ctrl+Y) redoes, Delete deletes the selected
   notes, Ctrl+A selects all, Ctrl+D duplicates, Escape clears the selection and B starts Slide into (holding B
   down counts as one press). Space must still start and stop Cubase, with Keys on or off. Does it? One exception:
   while a drop-down list is open (**GRID**, **UI SCALE** or the sound menu), Space only closes the list, and the
   next Space starts Cubase. Turn Keys off again afterwards.

10. **The inspector.** Select a note and click the **START** box. Type a new position, for example 2.1.1 (bar 2,
    beat 1), and press Enter: the note moves there. Try **LENGTH** (0.5, or a note value like 1/8), **PITCH** (a
    name like D3, or a number like 62) and **VELOCITY** (1 to 127). Escape puts the old value back. In **2 Pitch**,
    click a node, then type a **VALUE** such as +3 (semitones) or **AT** such as 0.5 (beats into the note). Typing
    in these boxes works with Keys off too; after Enter the keys go to Cubase again. Does it? (While you type in a
    box, Space types a space and the shortcuts such as Ctrl+Z and Ctrl+D do nothing in the roll; after Enter Space
    starts and stops Cubase again.)

11. **Play it all back.** Make a short phrase of your own: a few notes, a slide, a drawn Pitch line and a Volume
    line. Press Play in Cubase. Does it play what you see, in time?

12. **Save and reopen.** Save the project, close it and open it again. Are your notes, lanes and slides back? Are
    the mode, the **GRID** and the zoom as you left them?

13. **Copy report.** Open the **Diagnostics** tab and press **Copy report**. Paste the text into your reply.

## M5: selection tools

From this build on you can select notes with a box and change many of them at once: shapes, patterns and random
lines on a lane, slides between notes, and tools such as Walk, Chop, Humanize and Strum. The Copy / Paste and
Slides tabs work too. The steps above still apply; this part is about the new tools. Please go through it and send
back your answers and the text from **Copy report**. A screenshot helps whenever something looks wrong.

What is new, and what moved:
- **The tabs start folded** in a new StrandLock, so the roll has the room. Click **Show** at the right end of the
  tab row (or any tab) to open them. A project you saved with a tab open opens with it open.
- **The zoom buttons** **H−**, **H+**, **V−** and **V+** moved from the end of the bar numbers to the tab row, just
  left of **Hide** / **Show**, so every bar number shows.
- **The selection bar.** While notes are selected, a line appears under the row that says how many are selected.
  It holds **LANE** (a list of the ten lanes), **Shapes**, **Pattern**, **Random**, **Turn into nodes**,
  **Connect** and **Tools**. **Shapes**, **Pattern**, **Random**, **Connect** and **Tools** each open a box of
  controls under the line (a drawer); click the same button again to close it.
- **The dashed preview.** Hold the mouse over a shape, a pattern or most tool buttons (without clicking): the roll
  draws what it would do as dashed lines, and the hint bar at the bottom says what it is. Nothing changes until you
  click.
- **A small window.** When the window is too short for a whole row of a tab's knobs, the tabs stay folded rather
  than cut the row in half; clicking a tab then says so. When the window is short and a drawer is open, the
  inspector's line (START, LENGTH and the rest) steps aside so the roll keeps some room; it comes back when the
  drawer closes.

1. **Install the new build** the same way as before (Getting the test build, at the top). Add StrandLock to a new
   Instrument track. Do the tabs start folded, with a big roll? Click **Show**: does the Sample tab open?

2. **Select with a box.** In **1 Build**, press on an empty part of the roll and drag: a dashed box appears and
   every note it touches is selected as you drag. Let go. Now hold Shift and drag another box: those notes join the
   selection. Hold Ctrl and drag a box over some selected notes: they leave the selection (and the others join).
   Click an empty spot to clear the selection. Try the same in **5 Tone**. Does the box pick exactly the notes it
   touches?

3. **The Nodes switch.** In **3 Volume**, select two or three notes, then click **Nodes** on the mode bar (it turns
   purple). Drag a box around some of their round nodes: they get a ring. Drag one of them up: all the ringed nodes
   move together. With three nodes ringed, the inspector's line says **3 NODES (VOLUME)** with one value box: click
   it, type -6 dB and press Enter: do all three move to that value? Click **Nodes** again to turn it off.

4. **Connect.** In **1 Build**, select a few notes that follow each other, and open **Connect**. Hold the mouse over
   **Nearest** without clicking: dashed lines with a dot at their end show the slides it would make. Over
   **Disconnect** (once there are slides), a small cross sits on each slide it would take away. Try **Nearest**,
   **Furthest** and **Random**: each one makes slides between the selected notes (dashed lines join them), and a
   message says how many. **Re-roll** gives another random set; **Disconnect** removes them. Click **Undo** after
   each one: does each click undo exactly one change? Press Play: do the slides glide?

5. **Tools.** Select a few notes and open **Tools**. Try each button on a few notes and click **Undo** after each:
   - **Walk…** opens a small panel: pick what to walk in its list (Start, Velocity or a lane), set the two sliders,
     and click **Walk**. Then move a slider: the notes change while you drag.
   - **Chop…**: pick a rate and click **Chop**: each note becomes repeats. A rate that would make more than 1024
     notes is refused with a message.
   - **Reverse**, **Flip**, **More** and **Less** change the lane shown in **LANE** (backwards, upside down, bigger
     or smaller swings).
   - **Select similar** opens a list (same pitch, same length, every other note, this bar, or the opposite
     selection).
   - **Humanize…**: set the sliders and click **Humanize**; **Re-roll** gives other small nudges.
   - **Strum…**: pick a gap and click **Strum up** or **Strum down** on a chord.
   - **Legato** stretches each note to reach the next one.

   Does each one do what its name says?

6. **Shapes, Pattern and Random.** Select a few notes and click **5 Tone** (or pick Tone in **LANE**).
   - Open **Shapes**. Hold the mouse over **Ramp up**, **Swell** and the others without clicking: the dashed preview
     shows on the notes. Click one: the notes take it. **Each note** gives every note the whole shape; **Across**
     spreads one shape across all the selected notes.
   - Open **Pattern**. Hold the mouse over a pattern button (**Squiggle**, **Saw up** and the others) to see the
     preview, then click **Apply**. Now drag **DEPTH AT START** or **DEPTH AT END**: the notes change while you drag,
     and letting go is one undo step. Turn **Lock to the beat** off and try **SPEED UP / SLOW DOWN**.
   - Open **Random**, click **Apply**, then press Play: does the random line play the same on every loop? Click
     **Re-roll**: does it change once, and then stay the same again?
   - **Turn into nodes** (on the line, or in the drawers) turns the pattern or random into ordinary nodes you can
     drag. **Remove** takes it off.

   Click **Undo** after each step: one click, one change?

7. **The lane-button arrows.** Each lane button on the mode bar (**2 Pitch** to **8 Sample**) has a small arrow (▾)
   on its right. With notes selected, click the arrow next to **3 Volume**: a menu opens with Volume's shapes,
   **Each note** / **Across**, the pattern and random from the drawers, **Pattern settings…**, **Random
   settings…**, **Re-roll the random**, **Remove pattern / random** and **Turn into nodes**. Hold the mouse over a
   shape to see its preview, then click it. With no note selected the arrow says to select notes first.

8. **Copy / Paste.** Open the **Copy / Paste** tab.
   - Its first row says **LANES TO COPY** and has a button for each lane (all lit). The second row starts with
     **Copy**, **Paste** and what is held (**NOTHING COPIED**).
   - Draw something on one note's Pitch and Volume lines (in **2 Pitch** and **3 Volume**). Select that note and
     click **Copy**: the text says what it holds. Select three other notes of different lengths and click **Paste**:
     do their lanes now look like the first note's, stretched or squeezed to each note's length? **Undo** puts them
     back.
   - Click the **Pitch** lane button so it goes dark. Select the first note again and click **Copy**. Then select two
     notes you have not pasted onto yet and click **Paste**: does their Volume line change while their Pitch line stays
     their own? Save, close the project and open it again: is **Pitch** still dark?
   - The rest of the second row (**NOTES**) is for whole notes: select a few notes and click **Copy notes**, then
     select another note and click **Paste notes**: the copies start where that note ends, and any slides between
     them come along.
     **Duplicate** copies the selected notes right after themselves. In **1 Build**, right-click an empty spot:
     **Paste notes here** puts the copied notes there.

9. **Slides.** Open the **Slides** tab.
   - **AUTO SLIDE**: **Off**, **Legato (touching notes)** or **Always (next note)**: which notes slide into the next
     one by themselves. **SLIDE TIME**: how long a slide takes (1/32 to 1/2). Play a phrase and try each. If you
     automate **Auto Slide** or **Slide Time** in Cubase, do these lists follow while it plays?
   - **SELECTED NOTES**: select a note that slides into another and drag the slider: the slide gets longer or
     shorter while you drag, and letting go is one undo step. **Auto** puts the notes back on **SLIDE TIME**;
     **Jump, no slide** makes them jump straight to the next note.

10. **Play it back.** Make a short phrase with a few of these: a shape, a pattern, a random line, a slide, a pasted
    lane. Press Play in Cubase. Does it play what you see, in time, the same on every loop?

11. **A small window.** Drag the StrandLock window to its smallest size. Open the **Sample** tab: do the tabs stay
    folded rather than show half a row of knobs? Is there a message when you click the tab? Select some notes and
    open **Tools**: is the roll still big enough to use? Click **Walk…**: the drawer may now show only the top half of
    its second row, with a small scroll bar at its right edge. Can you scroll the drawer to reach Walk's sliders, or
    would you rather the drawer took more room from the roll? Make the window bigger again: does everything come back?

12. **Save and reopen.** Save the project, close it and open it again. Are your notes, shapes, patterns, slides and
    the **Copy / Paste** lane buttons as you left them?

13. **Copy report.** Open the **Diagnostics** tab and press **Copy report**. Paste the text into your reply.

## M6: the Sample tab

From this build on the Sample side of StrandLock is complete: Sample mode on the piano roll, the playheads moving on
the waveform, every control on the Sample tab (with the recipes, Dice and Preview), the Macros tab with its offsets,
and the Note settings tab. The steps above still apply; this part is about the new things. Please go through it and
send back your answers and the text from **Copy report**. A screenshot helps whenever something looks wrong.

What is new, and where it is:
- **Sample mode (8).** Click **8 Sample** on the mode bar. The four chips after it, **Travel**, **Heads**, **Grain**
  and **Refresh**, pick which Sample line of the notes you draw. Every note shows small chevrons (>) along it that
  point the way its heads move through the sound (pointing left: backwards; small bars instead: held still), and a
  faint picture of the part of the sound it plays. A small dot at a note's start means the note has settings of its
  own (Note settings). These show in the other modes too, fainter.
- **The waveform strip** shows the playheads while notes sound: a numbered line for each head, with a small arrow for
  the way it moves, and dots for the grains.
- **The Sample tab**, first row: **ENGINE** (**Tape**, **Voice**, **Cloud**), **Lock**, **MOTION** (**▶ Forward**,
  **◀ Backward**, **⇄ Bounce**, **↔ Mirror**, **❄ Freeze**, **⁙ Scatter**), **JUMP EVERY**, **↻ Re-roll** and
  **Preview** (in a window 1100 wide or narrower the last ones sit on a second line). Second row, the knobs:
  **SPEED**, **HEADS**, **SPREAD**, **GRAIN**, **SPRAY**, **DRIFT**, **PITCH**, then **REFRESH TIME** and **SKIP**, then
  **REGION START**, **REGION END** and **LOOP FADE**. Third row: **REFRESH** (**Smooth**, **Free**, **Synced**),
  **EVERY**, **Lock to the beat**, **Tape**, **Each grain backwards**, **HEAD TUNE** (**Same**, **Detune**,
  **Octaves**, **Fifths**) and **AT THE END** (**Loop**, **Hold**, **Stop**). Under a thin line: **RECIPES** with eight
  buttons, **1 Straight** to **8 Octave shimmer**, and under them **Dice**, a line of text, and **Unpin all** (only
  while something is pinned). The one **GRAIN** knob follows the engine: cycles on Voice, milliseconds on Cloud,
  waves on Cloud with Lock.
- **Faded controls.** A control that does nothing with the settings you have now is drawn faded. Hold the mouse over
  it: the line of help at the bottom (the hint bar) says why, and so does its tooltip.
- **Pins.** The small pin next to **ENGINE**, **Lock**, **MOTION**, **JUMP EVERY**, **SPEED**, **HEADS**, **SPREAD**,
  **GRAIN**, **SPRAY**, **DRIFT**, **REFRESH**, **Tape**, **Each grain backwards** and **HEAD TUNE** tells **Dice** to
  leave that setting alone. A pin that is on is purple.
- **The Macros tab:** **MACROS** with eight knobs (**SPEED**, **SPREAD**, **HEADS**, **REFRESH**, **GRAIN**, **SPRAY**,
  **TONE**, **FORMANT**) and **Centre all**; **WHOLE-CLIP OFFSETS** with five knobs (**VOLUME**, **VIBRATO**, **TONE**,
  **FORMANT**, **HOLLOW**) and **Reset all**.
- **Try this:** the right end of the hint bar suggests one thing to try in the mode you are in.

1. **Install the new build** the same way as before (Getting the test build, at the top). Add StrandLock to a new
   Instrument track and click **Show** to open the tabs.

2. **Sample mode.** Click **8 Sample**. Do the notes show chevrons and the faint picture of the sound? Click
   **Travel**, then select a note: its Travel line has round nodes. Drag the line down below the middle: the chevrons
   turn round (the note plays backwards); drag it to the middle: they turn into bars (the heads hold still). Press
   Play: do you hear it? Now click **Heads** and draw the line up on a note (more heads), then **Grain** (longer or
   shorter grains, on Voice or Cloud) and **Refresh**. Does each one change what you hear? Does **Try this** at the
   right of the hint bar suggest something sensible for each chip?

3. **The playheads on the waveform.** Press Play in Cubase. On the waveform strip, numbered lines move along the
   sound: one for each head of each note, with an arrow for its way. On **Cloud**, dots show the grains. Do they move
   in time with what you hear? Set **HEADS** to 3 and **MOTION** to **↔ Mirror**: do you see heads going both ways?

4. **Clicking the waveform to set where notes start.** Select three notes. Click once (a short click) on the waveform
   inside the Region. A message says something like "Start set for 3 notes at 40% of the Region", and small markers
   show the spot on the strip. Press Play: do those three notes now start at that spot? Hold the mouse down on the
   waveform instead: you hear that spot while you hold. A click outside the Region changes nothing and says why.

5. **Every control on the Sample tab.** Let the clip play (or hold a note) and try each control on **Tape**, on
   **Voice**, on **Cloud**, and on **Cloud** with **Lock** on. Clicking **Lock** while the engine is Tape or Voice
   switches to Cloud with Lock (a message says so). For the knobs: drag up or down (hold Shift for fine steps),
   double-click to put the default back, or click the value under the knob and type one (for example, with Lock on,
   type 4.3 into **GRAIN**: it stays 4.3). After each change click **Undo** once: does each change undo in one step?
   Hold the mouse over each control: does the hint bar say what it does?

6. **What the faded controls mean.** On the default settings (**Voice**, **HEADS** at 1, **REFRESH** on **Smooth**,
   **▶ Forward**) several controls are faded: **SPREAD** and **HEAD TUNE** (one head), **SPRAY** (Voice), **REFRESH
   TIME**, **SKIP**, **EVERY** and **Lock to the beat** (Smooth), **JUMP EVERY** (not Scatter), **Lock** (Cloud only) and
   **↻ Re-roll** (nothing random is in use). Hold the mouse over
   each faded one: the hint bar says why it does nothing now. Then change what it names (for example turn **HEADS** to
   2): it comes back. On **Tape**, **GRAIN** shows a dash and **❄ Freeze** is faded. Is every faded control really
   doing nothing, and every bright one doing something?

7. **The Macros tab.** Open **Macros**. Let the clip play and turn each macro knob: every note changes at once, and
   the text under the knob says what it does now (for example "2x faster"). **Centre all** puts them back. Then turn
   each of the **WHOLE-CLIP OFFSETS**: they add to every note's Volume, Vibrato, Tone, Formant or Hollow line. **Reset
   all** puts them back. In Cubase, are Quick Controls 1 to 8 still the eight macros?

8. **Recipes.** Back on the **Sample** tab, hold the mouse over **4 Tape scratch** without clicking: dashed lines on
   the roll show what it would do to the notes, and the hint bar says what it does. Click it: the Sample tab changes,
   and the notes' Travel lines get a pattern. Try all eight, first with no note selected, then with a few notes
   selected (the part that changes lines then touches only those). Click **Undo** after each: does one click undo the
   whole recipe? Do the eight sound different?

9. **Dice and pins.** Click **Dice** a few times: each roll sets new Sample settings, and a message says what it
   rolled. **Undo** steps back. Click the small pin next to **ENGINE**: it turns purple and the text after **Dice**
   says "Pinned: Engine". Roll again: does the engine stay? Pin two or three more settings and roll. **Unpin all**
   lets them all go. Pin something again, then close the StrandLock window and open it, and open another project with
   StrandLock: are your pins still there? (They are yours, not the project's.)

10. **Note settings and "mixed".** Select a note and open **Note settings**. Give it its own **ENGINE** (for example
    **Tape**) or **MOTION**: a dot appears at the note's start on the roll, and that note plays its own setting while
    the others follow the Sample tab. Select a few notes with different settings: the groups where they differ show a
    small **mixed** tag and no button lit. **Follow** puts a setting back on the Sample tab's, and **Follow the Sample
    tab for all** does that for every setting. Click **Undo** after each: one step?

11. **Preview with Cubase stopped.** Stop Cubase. On the **Sample** tab click **Preview** (at the end of the first
    row, or on the line under it). StrandLock plays its own clip at Cubase's tempo from where Cubase's cursor is, and
    the button now says **Stop preview**. Click in Cubase's ruler to move the cursor: does Preview jump there? Turn a
    knob while it plays: do you hear it? Press Play in Cubase: Preview stops, and a message says "Cubase is playing,
    so Preview stopped". Now turn on Cubase's option **Studio > Studio Setup > VST Plug-ins > "Suspend VST 3 plug-in
    processing when no audio signals are received"**, stop Cubase and click **Preview** again. If Cubase pauses
    StrandLock, a box at the top of the Sample tab says so within a second and names that option. What happens on
    your computer with the option on, and with it off?

12. **Play it back.** Make a short phrase with a recipe, a Dice roll, a note with its own settings and a Start set on
    the waveform. Press Play in Cubase. Does it play what you see, the same on every loop?

13. **Save and reopen.** Save the project, close it and open it again. Are the Sample tab, the Macros tab, the notes'
    own settings and their Starts as you left them?

14. **Copy report.** Open the **Diagnostics** tab and press **Copy report**. Paste the text into your reply.
