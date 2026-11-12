# StrandLock: the Cubase checklist

The first test build (M0) was the "probe": the StrandLock window with its final look, all of its knobs, and a
plain saw sound you can play. It was there to find out how Cubase treats StrandLock on your computer, and its
steps below (The checklist) still apply. The M2 build plays the real sound engine: its own steps are in
**M2: the first real sound**. The M3 build adds the grain engines (Voice, Cloud and Lock): its steps are in
**M3: the grain engines** at the end. Please go through them and send back your answers and the text from the
**Copy report** button.

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

- **Top bar**, left to right: the purple ring logo and STRANDLOCK; the theme button (it shows **Light** with a
  sun while the window is dark, and **Dark** with a moon while it is light); the **Keys** button (a small
  keyboard picture; it turns purple when on); **TEMPO (HOST)**, the tempo Cubase is sending (read only); and
  **UI SCALE**, a drop-down from 75 % to 200 %.
- **Mode bar**: **1 Build**, then 2 Pitch to 8 Sample in their colours, and the four small Sample buttons. They
  only change the line of help at the bottom for now.
- **Tabs** (under the "No notes selected" row): Sample, Macros, Note settings, Slides, Copy / Paste, Mod wheels,
  Sound and **Diagnostics**. Sample, Macros and Sound have working knobs. **Hide** on the right folds the tabs
  away; click it again (it then says **Show**), or click any tab, to bring them back.
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
   and stop Cubase. (Turn Keys off again afterwards. StrandLock remembers the setting for the next window.)

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
   4/4). Does it start at bar 1? Click any other tab (for example Sample): an orange line moves across the empty
   roll while Cubase plays. Then click Diagnostics again. Set a 4-bar cycle and let it
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
  time with Cubase whenever Cubase plays. You cannot see or change the notes yet: the piano roll comes in a later
  build. Your keyboard plays the sound too, at any time.
- **Only the Tape engine plays in this build.** On the Sample tab the Engine buttons are Tape, Voice and Cloud.
  Notes set to Voice or Cloud play as Tape for now, and the Diagnostics tab says how many do (the **Voices** line
  in the THIS COMPUTER box). Tape plays the sound like a tape machine: higher notes play it faster.
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
   Each time: did it load? A short message appears in the middle of the window, near the top of the empty
   piano roll; what does it say? The file line shows the name and length. For a song longer than 20 seconds
   StrandLock starts with a 4-second part from about a third of the way in. For a song longer than 10 minutes it
   loads the first 10 minutes and says so. (The Diagnostics tab still lists each drop in its DROPS box.)

5. **Pick a part and play it.** On the strip, drag across the wave to make a new Region around a part you like
   (a chord, a vocal line, a drum hit). Drag the Region's edges to adjust it, or drag the band along its top to move
   the whole box. **-** and **+** zoom the wave, **Show all** shows the whole song again. Hold the mouse button
   down inside the Region to hear it from that point. Then play the part from your keyboard: C3 plays it as
   recorded, other keys play it higher or lower. Does it sound right? Does the Region box follow your mouse
   smoothly? (Undo for StrandLock's own changes comes in a later build. For now Ctrl+Z goes to Cubase and undoes
   your last Cubase edit, so do not use it to undo a Region change.)

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
  heads. So older notes may get thinner in a big chord. That is on purpose. A drop-out or a crackle is not. On the
  Diagnostics tab, the **Engine health** line (THIS COMPUTER box) counts **cuts**: fade tails it had to stop at
  once because even the fades did not fit. They should stay at 0, except with **Lock grain** at 12 or more, where
  a fast big chord can make a few.

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
   (Studio > Audio Performance)? StrandLock has no CPU meter of its own, so this meter is the one to watch. Now turn
   **Lock grain** to 12 waves and do the same. Then look at **Engine health** on the Diagnostics tab: how many
   cuts does it show?

9. **The same at 96 kHz, if your interface can.** Make a new empty project and set its sample rate to 96 kHz
   (Project > Project Setup, Sample Rate). Add StrandLock and repeat step 8. At 96 kHz StrandLock has half the room
   per sample, so more heads of the older notes fade out, and with Lock grain at 12 the newest note plays 4 of its
   8 heads. Does anything drop out or crackle? How high does the Audio Performance meter go? Afterwards go back to
   your usual sample rate.

10. **Copy report.** Open the **Diagnostics** tab and press **Copy report**. Paste the text into your reply.
