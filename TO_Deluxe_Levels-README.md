# TO Deluxe Levels — Sierra Chart ACSIL Study

A key-levels study for Sierra Chart. It draws the previous day's, week's, and month's high, low, close, and volume profile levels (VAH, VAL, POC), plus four intraday session ranges: the 15-minute initial balance, the 1-hour opening range, the Asian session, and the London session.

Every level is a labelled horizontal line with its name at the right edge and its price on the values scale. The volume profiles are built inside the study from the chart's own Volume At Price data, so you don't need to add a separate profile study or configure profile periods.

The DLL exports as `TO Deluxe Levels`; the study function is `scsf_TODeluxeLevels` and appears in the study list as **TO Deluxe Levels**.

---

## Table of contents

- [What it draws](#what-it-draws)
- [Building and first-time setup](#building-and-first-time-setup)
- [How days, weeks, and months are defined](#how-days-weeks-and-months-are-defined)
- [How the volume profiles are built](#how-the-volume-profiles-are-built)
- [The four intraday ranges](#the-four-intraday-ranges)
- [Historical mode vs Latest Only mode](#historical-mode-vs-latest-only-mode)
- [Input reference](#input-reference)
- [Customizing the appearance](#customizing-the-appearance)
- [Tuning notes](#tuning-notes)
- [Reading the chart](#reading-the-chart)

---

## What it draws

Twenty-six horizontal levels, in four colour-coded families. Lines are drawn **underneath** the price bars so candles stay readable.

### Previous period levels

| Label | Level | Style |
|---|---|---|
| `PDH` / `PDL` / `PDC` | Previous day high, low, close | Solid, cyan |
| `PD VAH` / `PD VAL` / `PD POC` | Previous day value area high, value area low, point of control | Dashed, cyan/teal |
| `PWH` / `PWL` / `PWC` | Previous week high, low, close | Solid, orange |
| `PW VAH` / `PW VAL` / `PW POC` | Previous week value area | Dashed, amber |
| `PMH` / `PML` / `PMC` | Previous month high, low, close | Solid, magenta |
| `PM VAH` / `PM VAL` / `PM POC` | Previous month value area | Dashed, purple |

High/Low/Close are drawn solid; the three profile-derived levels are drawn dashed, so you can tell structural extremes from volume-derived levels at a glance.

### Intraday ranges

| Label | Window | Colour |
|---|---|---|
| `15m High` / `15m Low` | Initial balance, 09:30–09:45 | Green |
| `1H High` / `1H Low` | Opening range, 09:30–10:30 | White |
| `Asia High` / `Asia Low` | Asian session, 18:00–03:00 | Light blue |
| `London High` / `London Low` | London session, 03:00–09:30 | Salmon |

All windows are configurable.

Every line carries its name at the far right of the chart, aligned above the line, and its price value on the values scale. Levels you switch off simply disappear — the study writes zero for them and zeros aren't drawn.

---

## Building and first-time setup

1. Copy the `.cpp` into `<SierraChart>\ACS_Source`.
2. **Analysis >> Build Custom Studies DLL >> Build**.
3. **Analysis >> Studies >> Add Custom Study >> TO Deluxe Levels**.

Then two setup steps that matter:

### Set your chart to New York time

All times in this study — the day boundary, the opening range, the session windows — are **chart times**, read from the bar timestamps. The defaults are built around US Eastern time. Set the chart's time zone to New York under **Chart >> Chart Settings >> Time Zone** and the defaults work as labelled. If you prefer to run charts in a different zone, the study still works fine — just translate every time input into that zone.

### Reload the chart once

The study sets `sc.MaintainVolumeAtPriceData = 1`, which tells Sierra Chart to keep per-bar volume-at-price data available. That setting takes effect going forward, but historical bars already in memory won't have the data attached. Reload the chart once after adding the study (**Chart >> Reload Chart Data**, or press the INS key) so the historical VAP data is present. Until you do, the profiles will be computed from the fallback method described below, which is a reasonable approximation but not tick-accurate.

---

## How days, weeks, and months are defined

This is the part worth understanding, because it's what makes the levels line up with how futures actually trade.

### The boundary time

The **Day/Week/Month Boundary Time** input (default `18:00`) is when one period ends and the next begins. 18:00 chart time is 6:00 PM ET — the standard futures session open. Everything traded from 18:00 tonight through 17:00 tomorrow afternoon belongs to a single "day".

The **close** of a period is the last trade printed before that boundary, not the calendar-midnight close. For ES, that's the 17:00 ET settlement-ish print, which is what most traders mean by "yesterday's close".

### Trade dates

Because the session starts in the evening, an evening session's bars carry the **next** calendar day as their trade date — the standard futures convention. Monday evening's 18:00 open is part of Tuesday's trading day.

The study handles this automatically: any boundary time at or after noon causes the trade date to roll forward. If you set the boundary to a morning time instead (say `09:30` for a cash-session-only view), the trade date stays on the calendar day, which is the correct behaviour for equities.

### Weeks and months

- **Weeks** run Monday through Sunday, grouped by trade date. Since Sunday evening's session carries Monday's trade date, the Sunday open belongs to the coming week — so `PWH`/`PWL` reflect a full Sunday-evening-to-Friday-afternoon week.
- **Months** are grouped by the trade date's month, so the last evening session of a month belongs to the following month.

### No lookahead

On any historical bar, the previous-period lines show the values that were actually known at that moment. When you scroll back through the chart, the previous-day levels on Wednesday's bars are Tuesday's levels, not some later revision. This makes the study honest for replay and for visually reviewing past sessions.

---

## How the volume profiles are built

The VAH, VAL, and POC lines come from a volume profile the study builds itself over each completed period.

### Data source

For each bar, the study reads the bar's **Volume At Price** ladder — the actual traded volume at each individual price — and adds it into the period's profile. This is the same data Sierra Chart uses for its own volume profile studies.

If a bar has no VAP data attached (common on very old history, or before you've reloaded the chart), the study falls back to spreading that bar's total volume evenly across its high-to-low range. On a chart with no volume data at all, each bar contributes a count of 1 per price level, which turns the profile into a TPO-style time profile instead.

### Point of control

The POC is the price level that traded the most volume over the period. When two or more levels tie for the highest volume, the study picks the one closest to the **middle of the period's range**, which avoids the POC jumping to an extreme on a tie.

### Value area

Starting from the POC, the study expands outward two price levels at a time. At each step it compares the volume in the two levels above the current area against the two levels below, and adds whichever side holds more. It keeps expanding until the accumulated volume reaches the target percentage of the period's total — 70% by default, matching the market-profile convention.

`VAH` is the top level included; `VAL` is the bottom. Setting **Value Area Percent** to 100 makes VAH and VAL converge on the period's high and low.

### Price grouping

**Ticks Per Profile Price Level** (default 1) controls how finely the profile is bucketed. At 1, every tick is its own row. Raising it merges ticks into wider rows, which smooths noisy profiles and can produce more stable POC placement on thin instruments or long periods. For a month profile on a liquid future, values of 2 to 4 often give a cleaner read than tick-by-tick.

---

## The four intraday ranges

Each range is a high/low pair computed from bars whose **start time** falls inside a configured window.

### They build live, then freeze

While the window is open, the lines extend on every tick as new highs and lows print. When the window closes, the lines stop moving and stay flat for the rest of the trading day.

### They're flat from the start of the day

When a range extends, the study back-fills the new value across every bar since the start of the trade date. That means the initial balance lines are already at their final level when you look back at the 08:00 bars — you get clean, unbroken horizontal lines rather than a staircase that climbs as the session progresses.

### They reset each trade date

Every new trade date clears all four ranges, so each day gets its own initial balance, opening range, and session highs and lows.

### Windows can cross midnight

The Asian session default runs `18:00` to `03:00`, which wraps past midnight. The study handles this correctly, and because 18:00 is also the day boundary, the whole Asian session belongs to one trade date. If you configure a window that crosses your day boundary, it will be split across two trade dates — so keep windows inside the boundary if you want them treated as a single session.

### Initial balance and opening range share a start

Both `15m` and `1H` ranges start from the **Opening Range Start Time** input (default `09:30`) and differ only in their end times (`09:45` and `10:30`). Changing the start time moves both. This is deliberate: they're two views of the same session open.

### Bar timestamp convention

Sierra Chart can timestamp bars at their start or their end. If your chart uses **end** timestamps, set **Bars Are Timestamped At Bar End** to Yes so the window boundaries land on the correct bars. Getting this wrong shifts every range by one bar period. Most Sierra Chart setups use start timestamps, which is why the default is No.

---

## Historical mode vs Latest Only mode

**Historical mode** (default, `Draw Only Most Recent Values Across Entire Chart` = No) shows each bar's own contemporaneous levels. Lines step at each period boundary, so you see yesterday's levels across yesterday's bars, today's across today's. This is the mode for reviewing how price interacted with levels over time.

**Latest Only mode** (set the input to Yes) draws one continuous straight line per level all the way across the chart, using only the current values. This is the mode for live trading — clean, unbroken lines at today's relevant prices with no historical clutter. The study only redraws when a value actually changes, so it stays light even on large charts.

---

## Input reference

| # | Input | Default | Notes |
|---|---|---|---|
| 0 | Day/Week/Month Boundary Time (Chart Time) | `18:00` | Where one period ends and the next starts |
| 1 | Value Area Percent | `70` | 1–100; 100 makes VAH/VAL equal the period high/low |
| 2 | Show Previous Day Levels | Yes | Toggles all six day lines |
| 3 | Show Previous Week Levels | Yes | Toggles all six week lines |
| 4 | Show Previous Month Levels | Yes | Toggles all six month lines |
| 5 | Show High / Low / Close | Yes | Toggles the solid lines across all three periods |
| 6 | Show VAH / VAL / POC | Yes | Toggles the dashed lines across all three periods |
| 7 | Use Volume At Price Data When Available | Yes | Turn off to force the even-spread approximation |
| 8 | Ticks Per Profile Price Level | `1` | Profile row size |
| 9 | Draw Only Most Recent Values Across Entire Chart | No | Latest Only mode |
| 10 | Show 15 Minute Initial Balance (IBH / IBL) | Yes | |
| 11 | Show 1 Hour Opening Range (ORBH / ORBL) | Yes | |
| 12 | Opening Range Start Time (Chart Time) | `09:30` | Shared by IB and ORB |
| 13 | Initial Balance End Time (Chart Time) | `09:45` | |
| 14 | Opening Range End Time (Chart Time) | `10:30` | |
| 15 | Bars Are Timestamped At Bar End | No | Match your chart's convention |
| 16 | Show Asian Session High / Low | Yes | |
| 17 | Asian Session Start Time (Chart Time) | `18:00` | |
| 18 | Asian Session End Time (Chart Time) | `03:00` | Wraps midnight |
| 19 | Show London Session High / Low | Yes | |
| 20 | London Session Start Time (Chart Time) | `03:00` | |
| 21 | London Session End Time (Chart Time) | `09:30` | |

Inputs 5 and 6 act across all three period groups at once — useful for stripping the chart down to just structural highs and lows, or just profile levels, without unchecking nine boxes.

---

## Customizing the appearance

Everything visual is a normal Sierra Chart subgraph setting. Open **Study Settings >> Subgraphs** and select any level.

- **Primary Color** — line colour. The defaults group day/week/month into cyan/orange/magenta families, but nothing depends on that.
- **Line Width** — thickness. Bumping the previous-day close to width 2 is a common tweak.
- **Draw Style** — solid, dash, dot, and so on. Swap the value-area lines to dotted if dashed is too heavy for you.
- **Line Label** — the study enables the name at the far right, positioned above the line, plus the price on the values scale. Turn off the value display here if your scale gets crowded.

To hide a single level rather than a whole group — say, you want `PDH` and `PDL` but not `PDC` — set that one subgraph's draw style to Ignore.

The study also sets `DrawStudyUnderneathMainPriceGraph`, so lines render behind the bars. If you'd rather they sit on top, uncheck that in **Study Settings >> Settings and Inputs**.

---

## Tuning notes

**Different products, different boundaries.** The 18:00 default fits CME futures. For a cash-equities view, set the boundary to `09:30` and the periods align to regular trading hours. For FX or crypto, `17:00` (the conventional rollover) or `00:00` may suit you better.

**Adjusting the session windows for your product.** The Asian and London defaults are FX/futures conventions in Eastern time. If you trade a European product, you might set "Asia" to the actual overnight window and "London" to your own morning session — the labels are just names, and the windows are yours to define.

**Chart length matters for month levels.** The study builds its profiles from the bars on the chart. To get an accurate previous-month POC you need the whole previous month of intraday bars loaded. If your chart only holds ten days of history, the month levels will be computed from whatever is there. Increase **Days To Load** in Chart Settings if the month levels look wrong.

**Value area percent.** 70% is the market-profile standard and what most traders' levels will match. Lower values (60%) tighten the area around the POC; higher values (80%) widen it toward the extremes.

**Turning off VAP.** Setting **Use Volume At Price Data** to No forces the even-spread approximation on every bar. This is mainly useful for comparison, or on data feeds where the VAP data is unreliable.

---

## Reading the chart

- **Solid lines are structural**: where the previous day, week, or month actually traded to. `PDH` and `PDL` are the classic reversion and breakout references.
- **Dashed lines are volume-derived**: `POC` is where the most business got done, and the `VAH`/`VAL` pair brackets the 70% of volume around it. Price returning to a prior POC is a different kind of event from price returning to a prior high.
- **Colour tells you the timeframe**: cyan for the day, orange for the week, magenta for the month. When a weekly and a monthly level sit on top of each other, that confluence is usually more significant than either alone.
- **The green 15m lines** are the initial balance — the first fifteen minutes of the New York session. Breaks of the IB early in the day are a common structure trade.
- **The white 1H lines** are the full opening-range hour, a slower and more widely-watched version of the same idea.
- **The blue Asia and salmon London lines** carry the overnight structure into the US session. They freeze at 03:00 and 09:30 respectively, so by the New York open both are fixed reference levels for the day.

If you want the least cluttered live setup: turn on Latest Only mode, keep the day levels and the two session ranges, and switch off week and month value areas until you specifically need them.
