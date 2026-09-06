# Combined Chart Dashboard

A single Sierra Chart ACSIL study that replaces nine separate ones. It prints nine
lines of live status text in the upper-left corner of the chart and draws two
reference levels, all from one DLL export.

```
ESZ5  LAST 6842.25   BID/ASK 6842.00 / 6842.25   BARS 1440
MIDNIGHT 09/05 00:00 6830.50   DIST +11.75 (47 t)   MIDNIGHT BELOW PRICE
SUNDAY OPEN 08/31 17:00 6801.00   DIST +41.25 (165 t)   SUNDAY OPEN BELOW PRICE
PREV CLOSE 09/04 16:59 6835.75   DIST +6.50 (26 t)   MOVING AWAY   TREND UP
1ST HOUR 08:30-09:30   O 6836.00 C 6844.50   NET +8.50 (34 t)   GREEN DAY
OR 08:30-09:30   H 6848.75 @08:52  L 6833.25 @08:34   LOW FIRST -> Breakout GREEN
FRIDAY 09/05   GREEN 52.00% / RED 48.00%   SKEW +4.00%   GREEN DAY BIAS
SLOT 09:15-09:20   VALUE 22  RED   next 09:20 -> 12
IB SCAN (prev completed daily) 2 of 12: NQ 09/04, CL 09/04
```

## The nine lines

| # | Line | Replaces | What it tells you |
|---|------|----------|-------------------|
| 0 | Quote | `FourLineChartText` | Symbol, last, bid/ask, bar count. Colored by the last bar's direction. |
| 1 | Midnight level | `MidnightLevel` | Close of the first bar of the current calendar date, and the distance to it. Also draws a horizontal line with a right-hand label. |
| 2 | Sunday open | `SundayOpenLevel` | Open of the first bar of the Sunday evening reopen — the first print of the trading week — plus distance. Also drawn as a level. |
| 3 | Previous session close | `PrevSessionCloseTracker` | Close of the last bar before the most recent 17:00 boundary, the distance to it, whether price is approaching or leaving it, and short-term trend. |
| 4 | First hour trend | `FirstHourTrend` | Open-to-close direction of the first hour after the RTH open. Green day / red day. |
| 5 | Opening range sequence | `OneHourOR_HighLow_First` | Whether the opening range low or high was put in first, and the implied breakout direction. |
| 6 | Day of week bias | `DayOfWeekBias` | Historical green/red percentage for the current weekday, with the skew. |
| 7 | Time slot value | `TimeSlotValue` | The value for the current time slot, read from a CSV table, colored by threshold band. |
| 8 | Inside bar scan | `InsideBarScanner` | Which of up to 12 Daily charts in the chartbook printed an inside bar on the last completed daily bar. |

Every line has its own show/hide input. Hidden lines collapse — the ones below
move up rather than leaving a gap.

## Install

1. Copy `CombinedDashboard.cpp` into your Sierra Chart `ACS_Source` folder
   (usually `C:\SierraChart\ACS_Source`).
2. In Sierra Chart: **Analysis >> Build Custom Studies DLL**, select the file,
   and build. The output DLL is named `Combined Chart Dashboard`.
3. Open an **intraday** chart, then **Analysis >> Studies >> Add Custom Study**
   and pick *Combined Chart Dashboard*.

## Chart setup

- **Timezone: Central.** The defaults assume 08:30 is the NY open and 17:00 is
  the settlement / evening reopen. If your chart is in another timezone, change
  the `RTH Open`, `RTH Close` and `Evening Session Open` inputs instead of the
  chart.
- **Session times must include the evening session** (a 23-hour futures
  session) for the previous-close and Sunday-open lines to resolve correctly.
- **Days to Load: 7 or more.** By Friday, the Sunday open is five days back.
- For the inside-bar line, open one Daily chart per symbol in the same
  chartbook — they can be hidden via *Window >> Hide Window* — and set the
  `IB Chart 1..12` inputs to those chart numbers. Leave unused slots at 0.

## Time slot CSV format

One row per slot. Blank lines and lines starting with `#` are ignored. Times may
be `HH:MM` or `HHMM`.

```
# time,value
09:30,22
09:35,12
09:40,18
```

`Time Slot: Subtract From File Times` shifts the file's clock to the chart's.
It defaults to 60, so a `09:30` row is matched and displayed as `08:30`.
Toggling `Time Slot: Reload Data File` re-reads the file without a restart.

## Inputs

Grouped roughly in the order they appear in Study Settings.

**Shared display** — horizontal/vertical position, line spacing, font size,
bold, transparent background, and four colors (green, red, neutral, info) used
by every line.

**Shared session times** — `RTH Open`, `RTH Close`, `Evening Session Open /
Prev Close`, and `Boundary Detect Window`, which is how wide a window around a
boundary time still counts as that boundary.

**Line visibility** — one yes/no per line.

**Levels** — draw on/off, line width, dashed, label text for each level,
`Require Sunday`, `Measure From Last Completed Bar`, and `Invert Colors`.

**Per-line behavior** — trend lookback and RTH-only display for the previous
close; window length, minimum ticks, and same-bar resolution for the first hour
and opening range; the five weekday green percentages; the time-slot file path,
slot length, and thresholds; and the inside-bar strict/skip-today flags plus how
many symbols to name on the line.

## Notes on the design

Each of the nine originals carried its own copy of the same plumbing. This
version has exactly one of each:

- **`SetTextLine()`** — one text renderer. Every line owns a fixed
  `LineNumber` and is redrawn with `UTAM_ADD_OR_ADJUST`, so text is replaced in
  place and can never overlap or be left behind. The color is part of the cache
  key, otherwise a red-to-green flip on identical text would never repaint.
- **`DrawLevel()` / `EraseLevel()`** — one horizontal-line-plus-label renderer,
  shared by the midnight and Sunday levels.
- **`FindBoundaryIndex()`** — one backwards session-boundary search replacing
  four near-identical finders. A mode flag (window entry vs. date change), a
  detect window, and an optional required weekday cover every case:

  | Use | Start | Mode | Weekday |
  |-----|-------|------|---------|
  | Midnight | 00:00 | date change | any |
  | Sunday open | 17:00 | date change | Sunday |
  | RTH open | 08:30 | window entry | any |
  | Previous close | 17:00 | window entry, minus one bar | any |

  Searching by boundary crossing rather than by calendar date is what makes
  Monday work without weekday math: walking back from Monday morning, the bar
  before the Sunday reopen is Friday's final bar, which is the close a trader
  actually means. The detect window is what stops a 17:00 reopen from being
  mistaken for midnight.
- One copy each of `HHMMToMinutes`, `MinutesOfDay`, `AbsValue`,
  `DayOfWeekFromDate` (Sakamoto's algorithm, so no date library is needed),
  plus `TicksBetween` and `FormatSigned` for the repeated
  `+/- price (N ticks)` formatting.

The inside-bar scan is the only expensive operation — it reads up to 12 other
charts — so its result is cached and recomputed only when a new bar opens or on
a full recalculation. Daily inside-bar status cannot change faster than that.

## Differences from the originals

- Each study was condensed from three or four lines to one.
- The time-slot colored rectangle is gone. The threshold band (RED / GREY /
  GREEN) now shows as the line's text color, with the band name in the text.
- The inside-bar scanner has 12 chart slots instead of 20, to keep the total
  input count low. Change `MAX_SYMBOLS` at the top of the file if you need more.
- The midnight and Sunday levels recompute every tick rather than only on bar
  open, since the rest of the dashboard is tick-driven. Draw caching means
  `sc.UseTool` still only fires when something actually changed.
- The time-slot CSV table lives in file-scope globals, so multiple charts
  running this study share one loaded table. This matches the original.

## Troubleshooting

| Message | Cause |
|---------|-------|
| `Apply this study to an intraday chart` | The study is on a Daily chart. |
| `SUNDAY OPEN: not in loaded data` | Raise *Days to Load* to 7 or more, or the chart has no evening session. |
| `1ST HOUR: RTH open not found` | The RTH open time doesn't match the chart's timezone or session times. |
| `PREV CLOSE: outside the RTH display window` | Expected before 08:30 and after 15:00. Turn off *Prev Close: Only During RTH Window* to show it all day. |
| `TIME SLOT: cannot open ...` | Bad file path, or the file isn't readable by Sierra Chart. |
| `IB SCAN: set the IB Chart 1-12 inputs` | No chart numbers configured, or the referenced charts have fewer than two bars. |

If `sc.GetChartName()` doesn't exist in your Sierra Chart version, set
`HAVE_GET_CHART_NAME` to `0` near the top of the file; the inside-bar line then
falls back to `Chart #N` labels.
