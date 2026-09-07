# Stream Sounds — Sierra Chart ACSIL Study

A scheduled audio-cue study for Sierra Chart. It watches the clock and plays a WAV file of your choosing at up to eight specific times of day, prints the name of the event on the chart, and logs it. It also watches three "trigger file" paths so that anything else on your machine — a script, a scheduled task, a news feed, another program — can make Sierra Chart play a sound just by creating a file.

It was built for live-streaming and monitoring workflows: audible markers for session opens and known volatility windows, plus a hook for external events like "news in 2 minutes".

The DLL exports as `Stream Sounds DLL`; the study function is `scsf_StreamSounds` and appears in the study list as **Stream Sounds**.

---

## Table of contents

- [What it does](#what-it-does)
- [Building and installing](#building-and-installing)
- [Setting up your sounds](#setting-up-your-sounds)
- [The eight scheduled times](#the-eight-scheduled-times)
- [Trigger files](#trigger-files)
- [The on-screen label](#the-on-screen-label)
- [How the timing works](#how-the-timing-works)
- [Input reference](#input-reference)
- [The bundled string utility library](#the-bundled-string-utility-library)
- [Typical setups](#typical-setups)

---

## What it does

Three things, all independent of price data:

1. **Scheduled sounds.** Eight configurable `HH:MM` times, each paired with a WAV file path and a descriptive name. When the clock reaches a configured time, the matching sound plays once.
2. **Trigger files.** Three configurable file paths. If a file appears at one of those paths, the study plays the associated sound and then deletes the file. This gives any external process a one-line way to make your chart talk.
3. **On-screen label.** The name of the most recent event is drawn in the corner of the chart in large text, so a viewer (or you, glancing over) can see what just fired.

The study attaches to a chart but doesn't read bar data, plot anything on price, or affect any other study. It's a clock and a speaker.

---

## Building and installing

1. Copy the `.cpp` file into `<SierraChart>/ACS_Source`.
2. In Sierra Chart: **Analysis >> Build Custom Studies DLL >> Build**.
3. Add it to any chart: **Analysis >> Studies >> Add Custom Study >> Stream Sounds**.

Add it to **one** chart only. Because the schedule is wall-clock based rather than chart based, running two copies means every sound plays twice.

The study sets `sc.UpdateAlways = 1`, which means Sierra Chart keeps calling it even when no new data arrives. That's essential here — without it, a quiet market could sail straight past your 09:00 cue because nothing triggered a recalculation.

---

## Setting up your sounds

Every sound input takes a **full path to a WAV file**. The defaults all point at:

```
c:\SierraChart\AlertSounds\
```

Create that folder if it doesn't exist and drop your WAV files in it, or point the inputs wherever you keep yours.

A few practical notes:

- **WAV format.** `sc.PlaySound` expects a `.wav` file. MP3s won't play.
- **Backslashes are doubled in the source** (`c:\\SierraChart\\...`) because that's C++ string escaping. When you type a path into the Study Settings dialog, type it normally with single backslashes.
- **Keep them short.** These are cues, not music. One to three seconds works best, especially if you're streaming and talking over them.
- **Nothing happens if the file is missing.** A bad path fails silently — no error, no sound. If a cue isn't firing, check the path first.

---

## The eight scheduled times

Each slot has three parts: a **name** (which is what you see in the Study Settings dialog and on the chart label), a **time** in `HH:MM`, and a **sound path**.

The shipped defaults are a US-session volatility map:

| Slot | Default name | Default time | Default sound |
|---|---|---|---|
| 1 | New York Open | 08:30 | `NYopen.wav` |
| 2 | Market Pivot | 09:00 | `MarketPivot.wav` |
| 3 | Inverse NY moves | 10:30 | `InverseNY.wav` |
| 4 | Euro Move | 10:00 | `EuroMove.wav` |
| 5 | Bond Auctions | 12:00 | `BondAuctions.wav` |
| 6 | Capital Injection | 13:30 | `CapInject.wav` |
| 7 | Rug Pull | 14:45 | `RugPull.wav` |
| 8 | Time 8 | 05:30 | `ES.wav` |

Slots aren't in chronological order (slot 4 at 10:00 fires before slot 3 at 10:30) and that's fine — the study checks all eight independently, so slot order doesn't matter. Put them in whatever order makes sense to you.

### Renaming a slot

The `Name` field of each time input is what appears in the Study Settings dialog **and** what gets drawn on the chart when that time hits. Changing the name is an edit to the source file, in the `sc.SetDefaults` block:

```cpp
Input_Time3.Name = "Inverse NY moves";
Input_Time3.SetString("10:30");
```

Change the string in `.Name` and rebuild. The **time** and the **sound path**, by contrast, are edited directly in Study Settings at runtime — no rebuild needed.

### Changing a time

Open **Study Settings**, find the input by its name, and type a new `HH:MM` value. Use 24-hour format with a leading zero: `08:30`, not `8:30`. The comparison is an exact text match against the current clock, so the format has to line up.

The times are read in your **computer's local time zone**, not the chart's time zone setting. If your Sierra Chart is displaying exchange time but your PC is set to something else, set these times to match your PC clock.

### Slot 8

Slot 8 plays its sound and writes to the log like the others, but doesn't draw the on-screen label. It's useful as a quiet personal reminder that you don't want appearing on a stream overlay.

---

## Trigger files

This is the extensibility hook. Each trigger slot is a **file path** the study watches. On each new minute it tries to open that file; if the file exists, it:

1. Plays the associated sound,
2. Writes a line to the Message Log,
3. Closes the file,
4. **Deletes the file.**

The delete is the important part — it re-arms the trigger. Drop the file again and it fires again.

### Why this is useful

Anything that can create a file can now make your chart play a sound. Some examples:

**A scheduled task for an economic release, two minutes ahead:**

```bat
echo. > c:\SierraChart\AlertSounds\trigger1.txt
```

Schedule that one-liner in Windows Task Scheduler at 08:28 and you get an audible two-minute warning before the 08:30 number.

**From a Python script watching an RSS feed or an API:**

```python
from pathlib import Path
Path(r"c:\SierraChart\AlertSounds\trigger1.txt").touch()
```

**From another application, a broker platform's alert action, a Discord bot, a phone shortcut writing to a synced folder** — anything that lands a file at that path.

The file's **contents don't matter**. It can be empty. Only its existence is checked.

### Defaults

| Slot | Default name | Default path | Default sound |
|---|---|---|---|
| 1 | News in 2 minutes | `trigger1.txt` | `ES.wav` |
| 2 | Trigger File 2 | `trigger2.txt` | `ES.wav` |
| 3 | Trigger File 3 | `trigger3.txt` | `ES.wav` |

All under `c:\SierraChart\AlertSounds\`.

> **Note on the current build:** all three trigger slots read the path from the **first** trigger input, so at present the three slots all watch `trigger1.txt` and each plays its own configured sound. If you want three independently-watched paths, change `Input_Trigger1.GetString()` to `Input_Trigger2.GetString()` and `Input_Trigger3.GetString()` in the second and third blocks and rebuild.

### Permissions

The study deletes the trigger file after firing, so the folder needs to be writable by Sierra Chart. If you put trigger files somewhere protected like `C:\Program Files\`, the delete will fail and the sound will repeat every minute until you remove the file manually. The default `AlertSounds` folder is a safe choice.

---

## The on-screen label

When a scheduled time fires, the study draws the event's name in the top-left corner of the chart:

- **Position:** 10% across, 10% down, in the chart's relative coordinate space — so it stays put when you zoom or scroll.
- **Colour:** light blue, `RGB(151, 190, 252)` by default.
- **Font size:** 12 points by default.

Both are configurable without touching the source. Open **Study Settings >> Subgraphs**, select the **Text** subgraph, and change the **Primary Color** and the **Line Width** field. Line Width is reused as the font size here, so setting it to `20` gives you 20-point text.

Only one label exists at a time — each new event replaces the previous one rather than stacking. The label persists until the next event fires.

On a full chart recalculation (a reload, an INS-key refresh, or a settings change), the label resets to **"Like / Subscribe"**. That's the streaming-overlay default state. To change it, edit this line near the bottom of the file and rebuild:

```cpp
w.Format("Like / Subscribe")
```

---

## How the timing works

Worth understanding, because it explains the study's behaviour around restarts and edge cases.

The study reads `sc.CurrentSystemDateTime` — your **PC's clock**, not chart time and not the timestamp of the current bar — and formats it as `HH:MM`. It remembers the last minute string it saw. When the current minute differs from the remembered one, a new minute has started, and only then does it run the whole check block: all eight time comparisons and all three trigger-file checks.

This design has a few consequences worth knowing:

**One check per minute.** All the work happens once per minute, on the minute boundary. This keeps CPU cost near zero despite `UpdateAlways` running the study continuously.

**Trigger files are polled once a minute.** A trigger file dropped at 09:00:05 fires at 09:01, not immediately. Worst-case latency is just under a minute. If you're using a trigger as a countdown warning, drop the file a minute earlier than you want the sound.

**Seconds are ignored.** A time of `08:30` fires at some point during the 08:30 minute, not precisely at 08:30:00.

**The study must be running.** If Sierra Chart is closed at 08:30, the cue doesn't fire, and it won't fire late when you open up. There's no catch-up.

**Restarting within the same minute is safe.** The events are keyed to the minute boundary, so restarting Sierra Chart at 08:30:40 won't re-trigger the 08:30 sound.

---

## Input reference

Times and paths are editable at runtime in Study Settings. Names require a source edit and rebuild.

| # | Input | Type | Default |
|---|---|---|---|
| 0 | New York Open | Time `HH:MM` | `08:30` |
| 1 | Sound 1 | File path | `NYopen.wav` |
| 2 | Market Pivot | Time `HH:MM` | `09:00` |
| 3 | Sound 2 | File path | `MarketPivot.wav` |
| 4 | Inverse NY moves | Time `HH:MM` | `10:30` |
| 5 | Sound 3 | File path | `InverseNY.wav` |
| 6 | Euro Move | Time `HH:MM` | `10:00` |
| 7 | Sound 4 | File path | `EuroMove.wav` |
| 8 | Bond Auctions | Time `HH:MM` | `12:00` |
| 9 | Sound 5 | File path | `BondAuctions.wav` |
| 10 | Capital Injection | Time `HH:MM` | `13:30` |
| 11 | Sound 6 | File path | `CapInject.wav` |
| 12 | Rug Pull | Time `HH:MM` | `14:45` |
| 13 | Sound 7 | File path | `RugPull.wav` |
| 14 | Time 8 | Time `HH:MM` | `05:30` |
| 15 | Sound 8 | File path | `ES.wav` |
| 16 | News in 2 minutes | Trigger file path | `trigger1.txt` |
| 17 | News sound | File path | `ES.wav` |
| 18 | Trigger File 2 | Trigger file path | `trigger2.txt` |
| 19 | Sound | File path | `ES.wav` |
| 20 | Trigger File 3 | Trigger file path | `trigger3.txt` |
| 21 | Sound | File path | `ES.wav` |
| 99 | Version | Float | `2.7` |

**Version** is a stamp, not a setting — it lets you confirm at a glance which build of the study a chart is running. Changing it does nothing.

### Subgraph

| # | Name | Style | Purpose |
|---|---|---|---|
| 0 | Text | `DRAWSTYLE_CUSTOM_TEXT` | Colour and font size for the on-screen label |

### Message log

Every scheduled cue writes a line like:

```
Time3 matched 10:30 at 10:30
```

and every trigger fire writes:

```
Trigger file found: c:\SierraChart\AlertSounds\trigger1.txt
```

Scheduled cues raise the Message Log window when they fire; trigger fires log silently. If you'd rather scheduled cues stayed quiet on a stream, change the trailing `1` to `0` in the `sc.AddMessageToLog(...)` calls and rebuild.

---

## The bundled string utility library

The top of the file contains a self-contained set of `SCString` helper functions inside a `UTILITIES` region. They're not used by the sound logic — they're carried along as a reusable toolkit, since ACSIL's `SCString` is fairly bare compared to `std::string`, and these fill the common gaps.

If you're writing your own studies, this block can be copied out wholesale.

### Inspection

| Function | Does |
|---|---|
| `StartsWithSC(str, prefix)` | Prefix test |
| `EndsWithSC(str, suffix)` | Suffix test |
| `FindStringSC(str, sub, startPos)` | Index of substring, or `-1` |
| `ContainsStringSC(str, sub)` | Substring presence test |
| `EqualsIgnoreCaseSC(a, b)` | Case-insensitive equality |

### Transformation

| Function | Does |
|---|---|
| `UpperCaseSC(str)` | All caps |
| `LowerCaseSC(str)` | All lowercase |
| `CapitalizeFirstSC(str)` | First character upper, rest lower |
| `TrimSC(str)` | Strips leading and trailing whitespace |
| `PadLeftSC(str, width, padChar)` | Right-aligns to a fixed width; handy for column output |
| `ReplaceAllSC(str, from, to)` | Replaces every occurrence, returns a new string |
| `ReplaceAllStr(str, from, to)` | Same, operating in place on a `std::string` |

### Splitting and joining

| Function | Does |
|---|---|
| `SplitSC(str, delim)` | Splits into a `std::vector<SCString>` |
| `SplitStringSC(str, delim)` | Identical behaviour, alternate name |
| `JoinSC(parts, delim)` | Reassembles a vector into one string |

### Conversion

| Function | Does |
|---|---|
| `ToStringSC(value, decimals)` | Formats a `double` with fixed decimal places |
| `ToDoubleSC(str)` | Parses to `double` |
| `ToFloatSC(str)` | Parses to `float` |
| `ToIntSC(str)` | Parses to `int` |
| `ToBoolSC(str)` | Accepts `true`, `1`, `yes`, `y` — case-insensitive, whitespace-tolerant |

`ToBoolSC` is the most immediately useful of these if you're reading configuration from a text file, since it handles the several ways people write "yes".

---

## Typical setups

**Session markers for a live stream.** Set slots 1–7 to your session opens and known volatility windows, record short voice clips naming each one, and let the label double as an on-screen caption for viewers.

**Economic calendar warnings.** Point trigger 1 at a file, then use Windows Task Scheduler to create that file a couple of minutes before each release you care about. One scheduled task per event, all writing to the same path.

**Cross-platform alerts.** If you run analysis in Python, R, or a spreadsheet elsewhere, have it drop a trigger file when a condition hits. Sierra Chart becomes the notification layer without any integration work.

**Quiet personal reminders.** Use slot 8 for anything you want audible but not captioned — end of your planned trading window, a reminder to journal, a hard stop.

**Minimal install.** If you only want one cue, set the seven times you aren't using to something that never occurs during your session (`23:59` works) and leave them alone.
