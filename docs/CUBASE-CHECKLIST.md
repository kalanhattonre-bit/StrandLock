# StrandLock M0: the Cubase checklist

This test build is the "probe": the StrandLock window with its final look, all of its knobs, and a plain saw
sound you can play. The piano roll and the real sound engine come later. This build is here to find out how
Cubase treats StrandLock on your computer. Please go through the steps below and send back your answers and
the text from the **Copy report** button.

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

6. **Dropping sounds.** StrandLock does not load dropped sounds yet (that comes with the next build), but it
   shows what Cubase hands it. Drag a WAV file onto the StrandLock window from each of these: Windows Explorer;
   MediaBay; the Pool (Project > Pool); an audio event in the project window (a trimmed piece of a longer
   recording); a Cubase factory loop. The window turns purple with a dashed border while you drag. After each
   drop the Diagnostics tab opens with a short message at the top. In the DROPS box: which file name and folder
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
