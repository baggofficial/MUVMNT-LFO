# MUVMNT LFO

A free VST3 volume-shaper effect: draw a curve, and the plugin ducks your sound along it, locked to the tempo of your DAW. It is made for sidechain-style pumping, gating, tremolo and rhythmic volume patterns, and it has a **multiband mode** that gives the lows, mids and highs their own curves.

It is an independent, from-scratch implementation inspired by the workflow of Xfer Records' LFOTool and the volume shaper in Cableguys' ShaperBox. It contains no code from either.

![Full Band: the curve over the incoming audio (grey) and what the plugin lets through (coloured by pitch)](docs/images/single-dark.png)

> The screenshots in this README are rendered by the project's own code without a DAW, playing synthetic audio, so colours may differ a little from a real window.

## Contents

- [Features](#features)
- [Install](#install)
- [Using it](#using-it)
- [For producers: the trade-offs](#for-producers-the-trade-offs)
- [How it sounds: the design rules](#how-it-sounds-the-design-rules)
- [Performance](#performance)
- [Testing](#testing)
- [Building from source](#building-from-source)
- [Project layout](#project-layout)
- [Known limitations and roadmap](#known-limitations-and-roadmap)
- [License and credits](#license-and-credits)

## Features

**Curve editor**
- Draw any shape: double-click to add points, drag to move them.
- **Bezier handles** on every point, After Effects style: all handles are visible at once, smooth by default, with Shift to break them apart.
- **Simple mode**: one draggable diamond per segment instead of handles. It reaches much sharper bends than the handles can, and switching between the two styles converts your curve so it stays editable.
- Delete a point with a double-click (the first and last points stay), straighten a Simple-mode segment by double-clicking its diamond, toggle easy-ease by double-clicking the first or last point.
- **Grid and snapping**: snapping is off by default; hold **Option (macOS) / Alt (Windows)** to snap, or turn the **Snap** switch on to reverse that. A draggable number sets how many grid lines there are (2, 3, 4, 6, 8, 12, 16, 24, 32, 36, 48, 64) and snapping lands exactly on the drawn lines.
- Up to **64 points per curve**. At the limit, double-clicking shows a short note instead of adding a point. Pasted curves and preset files with more are refused whole, never cut short, and switching to Simple mode never splits a curve past the limit. A project saved with more points (up to 512) loads unchanged.
- In Sync mode, **very small note values under the graph** mark the quarters of the cycle (at a 1/4 rate: 1/16, 1/8, 3/16, 1/4; dotted and triplet rates add D or T), so you can find the middle at a glance. They are hidden in Hz mode.
- **Undo / redo** for curve edits (Ctrl/Cmd+Z, Shift+Ctrl/Cmd+Z or Ctrl+Y, or the arrows in the top bar): one step per drag, includes **Reset Curve** and the Bezier switch, and keeps the last 50 steps (about 40 KB).
- **Presets and copy/paste** from the **Presets** button in the second bar, beside **Reset Curve**: 19 factory shapes in three groups (**Pumps**: house pump, soft and hard duck, long release, pump x2; **Duck shapes**: nine rise, hold and drop shapes; **Shapes**: sine, triangle, two saws and flat), shown as small curve thumbnails, plus **Save as...** for your own, **Copy** and **Paste** (works between plugin instances and projects). A preset or pasted curve replaces only the curve you are editing (the selected band in Multiband) and is undoable.
- **Auto Curve**: press **Simple** or **Detailed** (at the end of the control strip) and the last cycle of your sidechain kick becomes an editable curve. **Simple** (3 to 6 points) gives the kick plenty of room; **Detailed** (up to 16 points) also looks at your sound and ducks only as much as needed for the two to fit. The button you pressed glows and fades. See [Using it](#using-it).
- **All-band presets** (Multiband only, in their own column, drawn in the band colours): four for 3 bands (Low-end focus, Waterfall, Gentle glue, Off-beat tops) and four for 2 bands (Lows only, Soft top, Long lows with quick highs, Seesaw). One click sets every band's curve and switches to that band count (with 2 bands, the mid curve is left as it was). Rates, depths and split frequencies stay as you set them, and one undo brings back both the curves and the band count.
- Click empty space to hide every point and handle and see the bare curve; click the curve to bring them back. Points cast a soft shadow, which helps you spot two that nearly cover each other.
- The first and last points always share one height, so the loop has no click where it wraps around.

**Timing**
- **Sync** to the host tempo and song position with note values from 8 bars to 1/64, plus **dotted** and **triplet** buttons. The LFO position depends only on the song position, so it stays locked through jumps, loops and tempo changes.
- **Hz** mode: a free-running rate from 0.01 to 40 Hz, optionally restarted by MIDI notes.
- **Offset** shifts the LFO within its cycle, **Smooth** (0 to 200 ms) rounds off hard edges, **Depth** sets how deep the ducking goes.

**Multiband**
- Split into **low / mid / high** (or just low / high) with draggable split lines on a frequency strip. A **live spectrum** runs behind the band curves, so you can see where to split: the input as a grey fill, and the output (after the ducking) as a bright blue line, so you can see what the LFO does to your balance. With two bands the split goes up to 12 kHz; with three, the low split tops out at 1 kHz.
- Two **multiband types**, chosen in the second bar: **Crossover** (the default) splits the audio into bands and adds them back together, with steep, selectable slopes. **Dynamic EQ** leaves the audio unsplit and turns ranges down with moving shelves: zero latency, and while the curves are fully open (band volumes and gain knobs at 0 dB, clipper and Ø IN off) the output is the input bit for bit, so there is no phase shift to soften transients. Phase only shifts while the bands are at different levels. The trade-off is a fixed, gentle slope (about 12 dB/octave), so ducking one band reaches further into its neighbours, and with three bands and splits close together the mid band gets less deep and less distinct.
- In Crossover, each split has its own slope: **6, 12, 24, 48, 72 or 96 dB/octave** (Linkwitz-Riley, so the bands add back to a flat response).
- Every band has its **own curve**, its own **rate** (or link them all), its own depth, smoothing and phase offset, and a **solo** button; solo several bands at once. A small chain beside the Rate, Depth, Smooth and Offset captions links that control, so every band follows one value; the chain lights up, the knob turns neutral, and hovering the chain explains it. While a control is linked, the parameter behind it is named **All Bands** (for example "All Bands Depth") for your DAW; it is the Mid parameter (Low with two bands), so automate it with the link on.
- The strip draws each band's real filter response, so you can see how the bands overlap at each slope.
- Each band has its own **volume**, from -12 to +6 dB: drag its line on the strip up or down. It sets the balance between the bands without changing the curves (Depth only changes how strong the ducking is). Volumes are never linked, changing one glides without clicks, the DAW can automate them ("Low Volume", "Mid Volume", "High Volume"), and presets and **Reset Curve** leave them alone. Projects saved before this feature open with every band at 0 dB.
- Defaults are a house-style sidechain pump where the lows stay ducked longest and the highs recover fastest.

**Feedback while you work**
- The input waveform is drawn folded onto the LFO cycle, faintly, with the **output** (what the plugin actually lets through) drawn over it in the curve's colour, so you can see where the kick and the hats land against your curve and what the ducking does to them. In the Full Band tab the output is coloured like a rainbow by pitch (red up to 100 Hz, then orange, yellow, green, turquoise and blue for the highs; a mix of lots of bass and highs tints pink or purple), a rough zero-crossing estimate; the input stays grey. In Multiband each band shows its own signal in its own colour (coral lows, teal mids, blue highs). The **Audio view** knob sets how strong this is.
- An optional sidechain input is drawn in gold: route a kick from another mixer channel into the plugin's sidechain and you see it against your curve. It never changes the sound by itself; the only other thing that reads it is **Auto Curve**, when you press it. Where the kick and the plugin's output are both loud at the same position in the cycle, that stretch is **hatched**: a conflict between the two. It goes by each signal's loudness over time (not by single wave peaks), so a low kick or a busy sound shows as a solid stretch. The output is measured after the ducking, so a spot where your curve really ducks under the kick is not marked. In Multiband the kick is split into the same bands, so each band is compared with the matching part of the kick (the lows against the kick's lows).
- A thin **end-of-cycle meter** beside the curve shows where the output signal (the input after the plugin's ducking, -1 to +1, same scale as the scope) was at the last point of the cycle, right when the curve drops: the big arrow pointing at the curve is the newest cycle, the small ones the cycles before. An arrow near the centre line is safe (white; dark grey on the light theme); one far from it turns yellow, orange, then red, and means a bass note is still swinging when the gain falls, which is where clicks come from. Duck harder at the end of the curve, ease the fall, or add a little Smooth, and watch the arrow settle at the middle. Click the meter to show an **end-zone bar** next to the arrow (hidden by default, and remembered with the project): a thin bar that spans the lowest to the highest output level over the last 4% of the cycle (it grows at once and falls back slowly, and the arrow glides to each new value), to catch heavy ducking that happens just before the end: a long red bar means the signal is still loud while the gain drops, even if the arrow itself sits at the middle. In Multiband the meter follows the selected band.
- **IN / OUT meters** on the right are narrow studio-style bars, cut into fine segments. In **Full Band** they end at **0 dB**: ducking only turns the sound down, so a full bar sits at the top, level with the top of the curve (the bars span the same height as the graph; their scale is in dB). In **Multiband**, where re-summed bands and band boosts can go over, they read up to **+6 dB**, with a bright line at 0 dB, a faintly red zone above it, and the part of the bar above the line in red. The bar turns from muted green to yellow and orange as the level nears 0 dB. In both tabs the red lamp on top lights for any peak above 0 dBFS, even a single sample, and stays lit for two seconds so you can catch it. The bars show the peak level going in and coming out, with ticks at -6, -12 and -24 dB, move in half-decibel steps from -36 dB and fall fast (80 dB/s), so they keep moving on a loud, pumping mix. The bars themselves have no peak hold.
- Two small **input and output gain** knobs sit right under the meters (-24 to +24 dB, 0 dB pointing up, with the value written under each; double-click, Ctrl-click (Cmd on macOS) or Alt/Option-click returns them there). Input gain comes before everything else, so the meters and displays show the adjusted signal. Output gain comes before the clipper.
- **Ø IN**, under those knobs, **reverses the polarity** of the input (flips the waveform upside down). On its own it sounds the same; it matters when the sound plays together with another one, such as a bass under a kick. It adds no delay, stores no audio and costs at most one multiplication per sample, switching glides through zero so it doesn't click, bypass passes the original polarity, and it is saved with the project (older projects open with it off). It affects only the plugin's input, not the sidechain.
- In the multiband view, a **Clipper** button under the gain knobs (off by default) cycles an output clipper: Off, Soft (linear up to -2 dB, then a smooth shoulder) or Hard (flat at 0 dB). Re-summing the bands can push the output past the input's peaks, and this keeps it from going over 0 dB.
- A **bypass** button (power icon) in the top right corner passes the audio through untouched, with a 10 ms crossfade so it never clicks, and no CPU use while it is bypassed. It is also the host's bypass: the host's own bypass button drives the same switch.
- A grey line shows the **actual gain** after Depth, Smooth and Offset; the output waveform is the input multiplied by exactly that gain.
- Light and dark themes (dark is the default, the choice is remembered), and a **60 / 30 fps** switch for the window's animation.
- **Resizable window** (880x500 up to 2000x1300): drag the bottom-right corner; the size is remembered.

![Multiband: draggable splits and band volumes over a live input / output spectrum](docs/images/multiband-dark.png)

![Multiband, Dynamic EQ: one fixed gentle slope, so there are no slope boxes](docs/images/multiband-dyneq-dark.png)

![A kick routed through the sidechain; the stretch where it competes with the bass is hatched](docs/images/kick-overlap-dark.png)

![The Presets popup in Multiband: all-band presets on the left, shapes for the selected band on the right](docs/images/presets-dark.png)

![Full Band, light theme](docs/images/single-light.png)

## Install

**Windows (VST3)**
1. Open the repository's [**Actions** tab](https://github.com/baggofficial/MUVMNT-LFO/actions), open the latest successful run, and download the `MUVMNT-LFO-windows-vst3` artifact.
2. Copy `MUVMNT LFO.vst3` to `C:\Program Files\Common Files\VST3\`.
3. Rescan plugins in your DAW.

**macOS (VST3 and standalone app)**: build from source (see below) and copy `MUVMNT LFO.vst3` to `~/Library/Audio/Plug-Ins/VST3/`. The build is not code-signed, so macOS may ask you to allow it.

Only the VST3 format is built (plus a standalone app for development). AU, AAX and CLAP are not available yet.

## Using it

| Do this | To get this |
|---|---|
| Double-click a knob, or right-click it | Type an exact value (rates accept `1/8`, `8`, `1 bar`...). Right-click also offers Reset to default, followed by your DAW's own options for that parameter (such as automation) if it provides them to VST3 plugins; Cmd/Ctrl-click resets too |
| Double-click empty space | Add a point (new points start smooth, with their handles spread out) |
| Drag a point | Move it (the first and last points move together vertically) |
| Drag a handle | Bend the curve; the opposite handle follows on smooth points |
| Shift while dragging a handle | Break the handles apart |
| Option/Alt or Ctrl while dragging a handle | Snap the handle's tip to the grid |
| Double-click a point | Delete it (the first and last points stay) |
| Double-click the first or last point | Toggle easy-ease (flat tangents) |
| Hold Option/Alt while dragging or adding | Snap to the grid (reversed if the Snap switch is on) |
| Click empty space / click the curve | Hide / show all points and handles |
| **Bezier handles** switch | Flip between handle editing and one-diamond Simple mode (curves are converted) |
| Drag a diamond (Simple mode) | Bend that segment through the point; double-click to straighten |
| Drag the number next to **Snap** | Change the number of grid lines |
| **Full Band / Multiband** selector (left of the second bar) | Click a half to switch between one curve for the whole sound and a curve per band |
| **Note / Hz** switch (left of the Rate knob) | Rate in note values synced to the song, or in Hz |
| Drag a band's line on the frequency strip (Multiband) | That band's volume; Shift drags finer. Double-click, Ctrl-click, Cmd-click or Alt-click resets it to 0 dB; right-click for reset and your DAW's options |
| Small knobs under the meters | Input / output gain (right-click for reset and your DAW's options); double-click, Ctrl-click (Cmd on macOS) or Alt/Option-click resets to 0 dB |
| **Ø IN**, under the small gain knobs | Reverse the polarity of the input (filled while on) |
| Ctrl-click (Cmd on macOS) or Alt-click a knob | Reset it to its default |
| **•** / **3** (right of the Rate knob, in Sync) | Dotted / triplet |
| **S** beside a band's name | Solo that band (several can be on) |
| Drag a split line, its slope box or its frequency (Multiband) | Move the split; click the slope box without dragging to choose the slope |
| **Simple** / **Detailed** under Auto Curve | Make a curve from the sidechain kick |
| Clipper button (multiband, under the gain knobs) | Cycle the output clipper: Off / Soft / Hard |
| Power button (top right) | Bypass |
| Click the end-of-cycle meter | Show or hide the end-zone bar |
| Half-moon button | Light / dark theme |
| **60 / 30 fps** button (beside it) | Animation speed of the window. 60 (the default) is smoother; 30 uses less CPU. The choice is remembered and the sound is not affected |
| **Reset Curve** (right end of the second bar) | Restore the current curve to its default |

**Auto Curve.** Route your kick into the plugin's sidechain, set **Rate** so that one cycle holds one hit (usually 1/4 for a kick on every beat), play the project, and press **Simple** or **Detailed** while it plays. It reads the loudness once, when pressed: the last full cycle of the kick and, for **Detailed**, how loud your sound has been in each part of the last four cycles (so a curve also fits beats that differ from the last one). It does not keep following the audio. In Multiband it shapes only the selected band, from the matching part of the kick. One undo brings your old curve back. Both end with a short drop just before the loop point. If your kick is still ringing there, **Detailed** stays at the room it leaves rather than jumping to full first. A sharp kick right on the beat can seem to start a few milliseconds early: **Simple** then plays it safe and stays silent from there to the loop point (two points close together at the end), while **Detailed** just ends its drop at the loop point (one point). It changes only the curve: Depth, Smooth, Offset and Rate stay as they are, so lower **Depth** for gentler ducking, and remember that **Smooth** delays the shape. Treat the result as a starting point and adjust it by ear.

| | Simple | Detailed |
|---|---|---|
| Looks at | the kick only | the kick **and** your sound |
| Curve | 3 to 6 points: down while the kick is strong, one smooth rise | up to 16 points: follows how much room is left, including dips where both are loud again. It reads the kick at its loudest around each moment, and your sound with gaps of up to about 60 ms filled in |
| Gives you | a predictable duck that leaves the kick plenty of room | more of your sound, mostly when it is plucked, rhythmic or fading in. With a steady, loud sound the two keep about the same amount |
| Watch out for | ducking more than strictly needed | it is fitted to the **last four cycles** of your sound. If a later part plays different notes or a different rhythm, it can leave too little or too much room: press it again there. It is also a more detailed shape to edit by hand |

Both always duck fully at the kick's loudest moment (at Depth 1.000). Detailed measures the kick and your sound each against its own peak, so sidechain send levels don't matter; in effect it assumes the two are about equally loud in your mix. It compares peak levels, not frequencies or perceived loudness, so it is not a guarantee against clipping or masking. If your sound is silent when you press it, it assumes full level. To switch between the two, just press the other button: both read your incoming audio from before the ducking.

When it works, the new curve simply appears, with no message. If it can't make a sensible curve it says why and changes nothing: no sidechain, silence, less than a full cycle recorded since the window opened or the rate or offset changed, a hit still ringing at the loop point, (Multiband) a band with very little of the kick in it, or a signal that never fades far enough or has more than one hit in a cycle.

**Long kicks and several hits (Detailed only).** For those last two, **Detailed** has a second way: it follows the room the kick leaves right through the cycle. That covers kicks still ringing when the next one comes (many hard-techno kicks and 808s at fast tempos) and up to four hits per cycle (double kicks, ghost hits, off-beat patterns). A kick that stays loud leaves little room, so expect a curve that holds your sound well down and only rises late. It is only offered when the result follows the kick closely: a kick that never dies down (a flat-out hardstyle tail), a steady bass line, more than four hits, or short hits too close together for the curve's 16 points are still refused. With one kick on every beat, a **1/4** rate still gives the most accurate curve.

**Rate knob.** The switch on its left picks **Sync** (the note icon) or **Hz**. In Sync mode the knob steps through the straight note values from slow to fast, and the **•** (dotted) and **3** (triplet) buttons on its right modify the value. In Hz mode it is a continuous frequency, those two buttons are hidden, and in Full Band a **MIDI retrigger** switch appears in the second bar.

**Multiband tab.** Click a coloured region on the strip to edit that band. To move a split, drag its line (or just beside it), its slope box or its frequency. Click the slope box without dragging to pick the slope (Crossover only). Drag a band's line up or down to set its volume (the value shows beside the band's name; 0 dB is the dashed line), and use the **S** button next to a band's name to solo it. The knobs below belong to the selected band. The chain beside each of the Rate, Depth, Smooth and Offset captions links that control: every band then follows one value (the mid band's, or the low band's in 2-band mode), and turning a link on copies the value you were looking at.

## For producers: the trade-offs

**Short version:** use **Full Band** unless you need different ducking for the lows, mids and highs. Multiband gives you more control, but it uses filters, and filters have side effects.

### Full Band

Full Band is only a volume knob that follows your curve. It has no filters and adds no latency or phase shift. Where the curve is at 100%, your audio passes through bit for bit (with the IN and OUT gain knobs at 0 dB and Ø IN off).

What to watch comes from the curve itself, as with any volume shaper:

- **Clicks.** A very fast drop or rise can click, especially on bass and 808s, if it cuts the wave in the middle of a swing. Make the edge a little less steep or add some **Smooth**. The end-of-cycle meter helps you check the point where the curve wraps around.
- **Too much Smooth** rounds off your shape and makes it react later, so the pump can feel less tight and the duck may arrive late for the kick. Use only as much as you need.
- **Very fast, deep movement** (fast Hz rates, hard gates) changes the tone as well as the volume. That can be the effect you want.
- **It ducks everything together.** Hats and vocals in the same audio dip along with the bass. If that is a problem, try Multiband.

These also apply to each band in Multiband.

### Multiband: Crossover (default)

**Good for:** the clearest separation between bands, for example ducking the lows hard while leaving the highs almost untouched.

- **It changes the wave even when the curves are flat.** The volume of each frequency stays the same, but frequencies near a split are shifted in time (phase shift). The plugin reports no latency, because the shift differs from frequency to frequency and your DAW can't compensate for it.
- **Drums can feel a bit softer**, snares for example. It is often subtle. Compare with the bypass button, which skips the filters. Setting Depth to 0 does not.
- **Peaks can get higher**, even while nothing is being ducked. In a test with a clipped 55 Hz bass and flat curves, peaks rose by about 4 to 6.5 dB (6 dB slope: no rise). Watch the OUT meter. Lower the OUT knob, or use the **Clipper**, which stops peaks at 0 dB but adds distortion when it works.
- **Be careful with parallel processing.** If you blend the output with an unprocessed copy of the same sound, some frequencies can partly or almost completely cancel (every slope except 6 dB). This includes layering it with a dry duplicate or using a wet/dry mix outside the plugin. Use the plugin's **Depth** control instead if you want less effect.

**Choosing a slope:** steeper slopes keep the bands more separate, but shift more and use more CPU. With a split at 150 Hz, the delay around the split is roughly 3 ms at 24 dB, up to about 8 ms at 48 dB and up to about 19 ms at 96 dB. At a 2.5 kHz split with 24 dB it is only about 0.2 ms. **24 dB is a good starting point.** The 6 dB slope has no phase shift with flat curves, but its bands overlap a lot.

### Multiband: Dynamic EQ

**Good for:** keeping your sound as close to the original as possible, especially full mixes and drums. It works like an EQ that moves with your curves.

- **Flat curves leave the audio untouched**, bit for bit (with the band volumes and gain knobs at 0 dB, and the clipper and Ø IN off). It adds no latency.
- **It still shifts phase while ducking**, like a normal EQ, when the bands are at different levels. When all bands are at the same level, it is a plain volume change.
- **The bands are less separate.** The slope is fixed and gentle, so ducking one band also affects its neighbours more.
- **It doesn't duck as deeply.** Example: with a 200 Hz split and the low band fully ducked, a 60 Hz tone drops by about 21 dB in Dynamic EQ and about 42 dB in Crossover at 24 dB. At 100 Hz it is about 12 dB and 25 dB. If you need the low end almost gone, use Crossover or move the split higher (which also affects more of the sound above it).
- **Keep three-band splits far apart.** If they are close together, the mid band has a weaker, less focused effect.

### Band volumes

The volumes work with both types, after the ducking. In Dynamic EQ the bands overlap more, so a band's volume reaches further into its neighbours. When the volumes differ, Dynamic EQ shifts phase like any EQ, even with flat curves. Boosting a band raises your peaks: watch the OUT meter, lower the OUT knob, or use the Clipper.

### Which one should I use?

| You want | Try |
|---|---|
| Classic sidechain pumping on one sound | Full Band |
| Hard, clean ducking of only the lows | Multiband, Crossover |
| Gentle movement on a full mix, keeping drums close to the original | Multiband, Dynamic EQ |
| Blending with a dry copy outside the plugin | Full Band |

Trust your ears: match the volume and compare with bypass while the whole track is playing.

## How it sounds: the design rules

These are the promises the plugin is built to keep, and the test suite checks each of them.

- **The LFO can only turn things down.** Its gain is `1 - depth x (1 - curve)`, always between 0 and 1. Only the input and output gain knobs and the Multiband band volumes can add level, and at 0 dB they don't touch the audio.
- **100% is a perfect null.** With a curve at 100% or with Depth at 0 (in Full Band, gain knobs at 0 dB, Ø IN off), the output is bit-for-bit identical to the input, including after you change settings and they settle.
- **0% is silence.** A curve at 0 gives exactly zero output, not a faint residue.
- **No clicks.** Anything that makes the LFO value jump (new rate, mode, offset, depth, a transport jump or a MIDI restart) glides over a few milliseconds, even with Smooth at 0. Solo and band changes fade. Moving a split frequency glides. Switching the tab, the multiband type, the band count or a slope dips the output for about 8 ms instead of clicking.
- **The result does not depend on the host's block size.** One block of 8192 samples and 1-sample blocks give the same audio.
- **Multiband is transparent.** With flat curves and the band volumes at 0 dB, Crossover adds the bands back to the input's level to within 0.001 dB; only the phase is shifted, as with any crossover (the 6 dB slope shifts none). Dynamic EQ with flat curves is bit-for-bit identical to the input (clipper and Ø IN off).
- **Damaged projects are refused.** Saved project text is checked before it is read: anything incomplete or not laid out like the plugin's own projects changes nothing (this also keeps JUCE 8.0.6's XML reader away from damaged text that can make it read past its end).
- **It cannot blow up.** The crossover is stable at every slope, split and sample rate (22 to 192 kHz), NaN and infinity in the input are removed before they reach the filters (and are never accepted from typed values, settings, curves, projects or the host's tempo), and an unstable filter is detected and restarted.
- **Real-time safe.** No memory is allocated on the audio thread, the curve editor hands curves to the audio thread without locks, and flush-to-zero keeps denormals from costing CPU.

### Under the hood

- **Curve model**: cubic Bezier segments between points (plus an optional exponential bend per segment for Simple mode). The curve is baked into a 2048-entry table whenever you edit it; the audio thread picks up a finished table with a try-lock and never waits.
- **Timing**: in Sync mode the LFO phase is computed from the host's song position, so it is sample-accurate and survives any relocation. In Hz mode (or when the transport is stopped) a phase accumulator runs freely.
- **Crossover**: Linkwitz-Riley filters built from cascaded Butterworth biquads in double precision (a first-order pair for 6 dB). In three-band mode the low band also passes through an allpass copy of the upper split, which keeps the sum flat. The two splits can have different slopes.
- **Dynamic EQ**: one 2nd-order state-variable filter per split. The filters depend only on the split frequencies; the band gains only change how their outputs are mixed with the input (the geometric mean of two neighbouring gains weights the middle term). So the gains can move every sample, all the way to 0, with nothing to destabilise, equal gains are a plain volume change, and in the tests no combination of gains boosts a tone by more than 0.01 dB.
- **Smoothing**: a one-pole smoother that is guaranteed to land exactly on its target (a plain one-pole stalls a few units short, which would break the null).
- **UI**: the grid, curve and handles are drawn once into cached images; each frame only blits those and draws the playhead and waveform. Everything that moves (graph, meters and spectrum) is redrawn from one clock tied to the display refresh, at about 60 fps, or 30 if you pick that with the header button (the spectrum is always 30), because each window update has a large fixed cost and one shared step keeps the number of updates down. The graph stops redrawing when the host stops sending audio and nothing has changed; dragging redraws straight away.

## Performance

**Audio processing**: share of **one CPU core**, measured by the offline test suite on an Apple Silicon Mac (lower is better; stereo, Release build). These figures do **not** include drawing the window, which costs far more (see below). "Editor open" here means only the extra audio-side analysis that feeds the displays.

| Mode | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| Full Band, editor closed | 0.03% | 0.03% | 0.06% | 0.12% |
| Full Band, editor open | 0.07% | 0.07% | 0.15% | 0.31% |
| Multiband 24 dB/oct, editor closed | 0.17% | 0.18% | 0.37% | 0.74% |
| Multiband 96 dB/oct (heaviest), editor closed | 0.46% | 0.51% | 1.01% | 2.01% |
| Multiband 96 dB/oct, editor open | 0.54% | 0.59% | 1.15% | 2.27% |
| Multiband Dynamic EQ, editor closed | 0.09% | 0.09% | 0.19% | 0.38% |
| Multiband Dynamic EQ, editor open | 0.16% | 0.17% | 0.33% | 0.63% |
| Bypassed | ~0% | ~0% | ~0% | ~0% |

(512-sample blocks; 32-sample blocks cost more, up to about 45% in the lightest modes.) A bypassed plugin does no audio processing. These figures are from one machine and will differ on yours: Full Band is almost free, and the heaviest multiband setting at 192 kHz is still around 2.3% of a core.

**The open window** is the expensive part. On an Apple M1 Pro with audio running, a test program showing the window at its default size used about **24% of one core in Full Band** and **30% in 3-band Multiband** at 60 fps (whole process, audio included; two runs each, while other programs were also running). Much of that is a fixed cost per window update, so the **30 fps** switch in the top bar helps: the same test then used about 15 to 16% and 24%. Closing the plugin window removes that cost. It has **not been measured on Windows**, which draws the window differently. The macOS (Apple Silicon) VST3 is about 5 MB on disk.

## Testing

Everything below runs offline, without a DAW or a sound card. A small **fake DAW** feeds the real plugin code with a transport at **130 bpm** and renders audio through it.

| Program | What it checks | How to run |
|---|---|---|
| `LfoAudioTest` | **263 audio checks**: bit-exact null at 100%, exact silence at 0%, transparency of every crossover slope, the Dynamic EQ (bit-exact when open, never boosting, band isolation), block-size independence, clicks on 29 kinds of parameter change, filter stability across 4 sample rates, NaN/Inf and zero-length blocks, a fuzz test that randomises every parameter and the transport, tempo accuracy at 130 bpm, Hz accuracy, MIDI restart, the sidechain bus, bypass (bit-exact, click-free, allocation-free), the output clipper, the input and output gain knobs, reversed input polarity, the Multiband band volumes, the peak meters (with the count of peaks above 0 dBFS) and the spectrum analyser, the "All Bands" names of linked controls, curve from kick (the history of your sound's last four cycles; Simple and Detailed with a sidechain kick through the whole plugin, at Depth 1.00 and 0.25, in Full Band and both Multiband types, with its refusals), state save/load (including projects over the point limit and damaged project data), settings, curves and projects containing NaN or infinity, zero allocations on the audio thread, thread safety while the curve is edited, CPU worst cases and denormals | `build/LfoAudioTest_artefacts/Release/LfoAudioTest` |
| `tests/envelope_test.cpp` | The curve model: Bezier evaluation, Simple-mode bends, conversions between the two, the default curves, saving and loading, the 64-point limit; the factory and multiband presets; the curve-from-kick fitter against many kinds of kick, the kick overlap marker, and the check on saved project text | `clang++ -std=c++17 tests/envelope_test.cpp -o envtest && ./envtest` |
| `tests/crossover_test.cpp` | The crossover maths for all six slopes, mixed slopes, and the drawn response curves | `clang++ -std=c++17 tests/crossover_test.cpp -o xotest && ./xotest` |
| `LfoMonkey` | Random clicks, drags, resizing, closing and reopening the window, automation, undo, and normal and damaged project loads, while audio runs on a second thread. Checks that the audio and curves stay valid. Most useful built with sanitizers (`-fsanitize=address,undefined` or `-fsanitize=thread`) | `build/LfoMonkey_artefacts/Release/LfoMonkey [seed] [actions]` |
| `LfoBench` | CPU per block (including the bypassed and editor-open cases), plus quick null and multiband sanity checks | `build/LfoBench_artefacts/Release/LfoBench` |
| `LfoSnap` | Renders the editor to PNG files without a DAW and drives it with synthetic mouse events: editing, snapping, handles, hiding, themes, layout, menus, split and band-volume dragging, the meters, the Auto Curve buttons and Ø IN | `build/LfoSnap_artefacts/Release/LfoSnap <output-folder>` |

The four programs that need JUCE are built with `-DFREELFO_BENCH=ON` (see below). GitHub Actions builds the Windows and macOS plugins and runs `LfoAudioTest` on both for every push; timing limits are relaxed there because shared machines are noisy.

## Building from source

You need CMake 3.22 or newer, a C++17 compiler (Xcode command line tools, or Visual Studio with the C++ workload) and an internet connection: JUCE 8 is downloaded by CMake the first time.

```bash
# plugin (VST3) and standalone app
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel

# the same, plus the test programs
cmake -B build -DCMAKE_BUILD_TYPE=Release -DFREELFO_BENCH=ON
cmake --build build --config Release --parallel
build/LfoAudioTest_artefacts/Release/LfoAudioTest
```

On Windows, pass `-A x64` to the configure step and use the `.exe` paths. Build outputs land in `build/FreeLfoTool_artefacts/Release/`.

## Project layout

```
Source/
  PluginProcessor.*     audio engine: single band, multiband, smoothing, declick, state
  Envelope.h            the curve model (no JUCE; unit-testable)
  Crossover.h           the Linkwitz-Riley band splitter (no JUCE; unit-testable)
  DynamicEq.h           the zero-latency Dynamic EQ multiband type (no JUCE)
  ControlStrip.h        the layout of the knobs, the Rate group and Auto Curve under the graph, shared by both tabs
  Theme.h               the design system: every colour, size and type size (hue only ever means band, kick or level)
  BrandMark.h           the MUVMNT LFO name in the top left (placeholder until the final wordmark)
  KickToCurve.h         "curve from kick": the sidechain analysis (no JUCE; unit-testable)
  SavedText.h           checks saved project text before JUCE reads it (no JUCE; unit-testable)
  Overlap.h             where the kick and the output compete, for the hatching (no JUCE; unit-testable)
  Spectrum.h            FFT analyser for the live spectrum (no JUCE; allocation-free)
  PluginEditor.*        the window: top bar, Full Band / Multiband selector, knobs
  EnvelopeEditor.*      the curve editor (points, handles, snapping, drawing)
  MultibandPanel.*      the frequency strip and the multiband tab
  LevelMeter.h          the IN / OUT peak meters (segmented bars with a clip lamp)
  PaperLookAndFeel.h    how everything is drawn: knobs, menus and the custom controls
  Presets.h             the factory curves and multiband presets (no JUCE)
  PresetMenu.*          the Presets popup and your saved presets
  Fonts.h               the bundled Inter font
Assets/Fonts/           Inter Regular and SemiBold, plus its license
tests/                  the test programs above and the shared fake DAW
docs/images/            screenshots used in this README
.github/workflows/      CI: builds and tests on Windows and macOS
```

## Known limitations and roadmap

- **Tested mostly in FL Studio on Windows.** It is developed on macOS and checked offline against a simulated host; Windows builds come from CI and have been used in FL Studio. Other DAWs (Ableton, Logic, Reaper, Bitwig) and other operating systems have not been tried by hand yet.
- **The sidechain doesn't trigger the LFO.** It is shown against your curve and read by Auto Curve when you press it. The LFO follows the host tempo, or MIDI in Hz mode.
- **Auto Curve only knows the last few cycles** (one of the kick, four of your sound for Detailed). If later bars differ, the curve may leave too little or too much room. Some signals are still refused (see [Using it](#using-it)); **Simple** only handles one fading hit per cycle.
- **The overlap hatching is a guide.** It compares how loud the kick and your sound are over time, each against its own peak. It does not compare frequencies in Full Band or how loud the two sound in your mix, and at very short cycles it gets patchier with low notes.
- **Structural switches dip the audio** for about 8 ms (the tab, the multiband type, the band count and the crossover slopes). It is click-free but not seamless; change them when the plugin is not playing a critical passage.
- **Bypass stops the LFO.** While bypassed nothing is processed, so the free-running phase (Hz mode) pauses; in Sync mode the LFO follows the host position and stays on the beat.
- **Not built yet:** a full preset browser (folders, rename), copy/paste of whole multiband setups, audio or envelope triggering, other formats (AU, AAX, CLAP).

## License and credits

MUVMNT LFO is open source under the [GNU Affero General Public License v3](LICENSE) (AGPL-3.0), the same license JUCE 8 is offered under. You can use, study, modify and share it, and if you distribute a modified version you must share its source under the same license.

VST is a trademark of Steinberg Media Technologies GmbH. The VST3 SDK is used through JUCE. LFOTool and ShaperBox are products of their respective owners, mentioned only as inspiration.

The interface font is [Inter](https://rsms.me/inter/) by Rasmus Andersson, bundled under the SIL Open Font License 1.1 (`Assets/Fonts/Inter-LICENSE.txt`).
