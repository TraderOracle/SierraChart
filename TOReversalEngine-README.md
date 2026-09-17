# TO Reversal Engine

A Sierra Chart ACSIL study for Numbers Bars / footprint charts. It reads the
bid/ask volume inside every bar and surfaces three things: where aggressive
orders got absorbed, how one-sided the bar was, and where those readings line
up as a potential turn.

---

## Requirements

- The chart must carry **Volume at Price** data. If your Numbers Bars display
  works, this will too.
- Add the study to the **same chart region as the price bars** (Region 0).
- Build: drop `AbsorptionDetector.cpp` into `SierraChart\ACS_Source\`, then
  **Analysis >> Build Custom Studies DLL >> Build**.

If the log says `no Volume at Price data available on this chart`, the data
source isn't providing footprint data and nothing will draw.

---

## What you see on screen

### 1. Absorption bubbles

A translucent oval sitting at one specific price row inside a bar.

- **Green** — bullish absorption. Aggressive selling piled into a level near
  the bar's low and passive buyers ate all of it. Price didn't break down.
- **Red** — bearish absorption. Aggressive buying piled into a level near the
  high and passive sellers ate all of it. Price didn't break up.

Bigger and more opaque means a stronger reading. Size and opacity are both
driven by the same absorption score, so a faint little oval is a marginal
event and a fat bright one is not.

Up to two bubbles per bar by default, kept at least a few ticks apart so a
single heavy zone doesn't produce a cluster.

### 2. Bar coloring

Bars are tinted from neutral gray toward green or red based on one order flow
metric that you choose. Quiet bars keep their normal chart coloring rather
than being painted gray, so only bars with something to say stand out.

Two rendering paths, because Numbers Bars draws its own cells over the price
bar and can hide ordinary bar coloring:

- **Price Bar Color** — the standard Sierra Chart bar recolor.
- **Background Highlight** — a transparent rectangle spanning the bar's range,
  drawn underneath the main graph. Visible regardless of what Numbers Bars
  paints on top.

Default is **Both**. If the price bar coloring shows fine on your setup,
switch to *Price Bar Color Only* and the rectangles stop being drawn.

### 3. Reversal markers

A marker below the low when the bar scores as a potential turn **up**, and
above the high when it scores as a potential turn **down**.

These are plain subgraphs, so their **draw style, color and size come from the
Subgraphs tab** — switch them to Arrow Up / Arrow Down, Square, Circle,
Triangle or whatever you prefer without recompiling. They ship as a white
Point at line width 6.

---

## The logic

### Absorption detection

For every price row in the bar, a level has to clear all of these to be
flagged:

1. **Heavy** — the row's volume is at least 8% of the bar's total volume *and*
   at least 2× the bar's average per-row volume.
2. **One-sided** — aggressive volume beats passive volume by the imbalance
   ratio. Two comparison modes:
   - *Diagonal* (default, standard footprint convention): ask volume at price
     P is compared against bid volume at P−1, since those two rest side by
     side on the book when the spread is one tick.
   - *Same Level*: ask vs bid at the identical price.
3. **At the extreme** — within a few ticks of the bar's high (for bearish) or
   low (for bullish). Absorption in the middle of a bar isn't absorption.
4. **Failed** — price rejected away from the level by the close. Optionally
   the close must finish on the absorbing side of the level entirely.
5. **No continuation** (optional) — if Confirmation Bars is above zero, the
   following bars must not extend past the level. This delays the bubble but
   removes the ones that were only a pause.

Qualifying levels are scored on relative volume × imbalance × rejection
distance, ranked, and the top ones are drawn.

### Per-bar metrics

Computed in one pass over the footprint, exposed as subgraphs, and used to
drive the coloring and the reversal score:

| Metric | Meaning | Sign convention |
|---|---|---|
| **Bar Delta** | Ask volume minus bid volume | + = net aggressive buying |
| **Net Trapped Volume** | Aggressive fills that finished on the wrong side of the close by at least the buffer distance | + = trapped shorts (bullish) |
| **Net Imbalance Count** | Buy imbalance rows minus sell imbalance rows | + = buy imbalances dominate |
| **Net Absorbed Volume** | Aggressive volume at the flagged bubble levels | + = bullish absorption |

Trapped traders are worth spelling out: buys stacked *above* where the bar
closed are trapped longs, sells stacked *below* it are trapped shorts. The
buffer keeps fills that are a tick off the close from counting.

### Reversal score

Runs −1 (turn down) to +1 (turn up). Every component measures the same thing:
**effort that didn't produce the result it should have.**

- **Absorption** — already directional. Bullish absorption pushes the score up.
- **Trapped traders** — net trapped volume over bar volume.
- **Delta divergence** — the delta ratio multiplied by how badly the close
  contradicted it. Heavy buying that closes on the low scores hard negative.
  Heavy buying that closes on the high scores zero, because nothing failed.
- **Failed imbalances** — the same treatment applied to the imbalance tally.

The weighted average of those four is multiplied by the **sensitivity** input,
because the raw components are ratios of bar volume and rarely exceed 0.2 on
their own.

Then two context filters:

- **Location gate** — the bar must be at a lookback extreme in the direction
  it would reverse *from*. A down signal is discarded unless the bar made the
  highest high of the lookback window; a sell signal mid-range isn't a
  reversal.
- **Volume weighting** — the score scales by this bar's volume against the
  lookback average, so climax bars count more and thin bars count less.

---

## Settings

### Absorption detection

| Setting | Default | Notes |
|---|---|---|
| Detect Bullish / Bearish Absorption | Yes / Yes | Turn off a side entirely |
| Min Aggressive Volume At Level | 150 | **Tune this first.** ES-ish default; drop it hard for thin products |
| Min Level Volume As % Of Bar Volume | 8 | Level must be heavy relative to the bar |
| Min Level Volume vs Avg Level Volume | 2.0× | Second heaviness test |
| Imbalance Comparison | Diagonal | Footprint standard vs same-level |
| Min Imbalance Ratio | 2.5 | Aggressive vs passive |
| Max Ticks From Bar High/Low | 3 | How close to the extreme the level must sit |
| Min Rejection From Level At Close | 2 ticks | How far price pulled away |
| Require Close On Absorbing Side | No | Stricter version of the above |
| Confirmation Bars | 0 | Above zero delays bubbles until following bars confirm |
| Max Continuation Beyond Level | 2 ticks | How far price may extend during confirmation |

### Bubble appearance

| Setting | Default |
|---|---|
| Max Bubbles Per Bar | 2 |
| Min Tick Separation Between Bubbles | 3 |
| Score For Smallest / Largest Bubble | 3.0 / 15.0 |
| Smallest / Largest Bubble Height | 2 / 10 ticks |
| Largest Bubble Width | 0.9 of a bar |
| Transparency At Min / Max Score | 72 / 18 |
| Bullish / Bearish Color | green / red |
| Bubble Outline Width | 1 |
| Show Absorbed Volume Text In Bubble | No |
| Bubble Text Font Size / Color | 8 / white |
| Draw Bubbles Under Price Bars | Yes |

Turn the volume text on for a few sessions to see what scores your instrument
actually produces, then set the two score inputs so weak readings barely show
and real ones are fat and bright.

### Metric calculation

| Setting | Default | Notes |
|---|---|---|
| Trapped: Min Ticks Beyond Close | 2 | Distance before a fill counts as trapped |
| Trapped: Min Volume To Count | 0 | Floor below which net trapped reads zero |
| Imbalance Count: Min Volume At Level | 20 | Keeps single-lot noise out of the tally |
| Compact Number Format In Input Titles | Yes | 1234 → 1.2k in the history ranges |

### Bar coloring

| Setting | Default | Notes |
|---|---|---|
| Color Price Bars By Metric | Yes | Master switch |
| Color Bars By | Net Imbalance Count | Or Delta, Absorbed, Trapped, Bar Volume |
| Full Color At Value | 4.0 | Value at which the color saturates |
| Leave Bar Uncolored Below Abs Value | 1.0 | Dead zone |
| Bar Color Mode | Gradient | Or solid once past the dead zone |
| Positive / Negative / Neutral Color | green / red / gray | |
| Bar Color Method | Both | See rendering paths above |
| Background Highlight Transparency | 78 | |
| Background Highlight Width | 0.9 of a bar | |

Bar Volume is treated as one-sided — it only ever shades toward the positive
color, since there's no negative volume.

### Reversal markers

| Setting | Default | Notes |
|---|---|---|
| Show Reversal Markers | Yes | |
| Min Reversal Score To Mark | 0.30 | Title reports the observed peak — see below |
| Reversal Lookback | 10 bars | Window for the extreme test and volume average |
| Require Bar At Lookback Extreme | Yes | The location gate |
| Swing Extreme Tolerance | 1 tick | Slack in the extreme test |
| Weight: Absorption | 4.0 | |
| Weight: Trapped Traders | 1.0 | |
| Weight: Delta Divergence | 1.0 | |
| Weight: Failed Imbalances | 1.0 | |
| Scale Score By Relative Bar Volume | Yes | Climax weighting |
| Reversal Marker Offset From Bar | 2.5 ticks | |
| Reversal Score Sensitivity | 4.0 | Lifts raw components into a usable range |
| Enable Alert On Reversal Marker | No | |

Set a weight to 0 to remove that component from the score entirely.

### General

| Setting | Default | Notes |
|---|---|---|
| Number Of Bars To Calculate | 500 | 0 = all loaded bars |
| Enable Alert On New Absorption | No | Fires on the newest bar only |

---

## Self-reporting input titles

Three coloring inputs and the reversal threshold **rewrite their own names**
with the range actually observed across the calculated history:

```
Color Bars By  [history -9 to 12]
Full Color At Value  [history peak 12]
Leave Bar Uncolored Below Abs Value  [history peak 12]
Min Reversal Score To Mark  [history peak 0.62]
```

Reopen Study Settings to refresh them — Sierra reads the names when the window
opens. Only closed bars feed the ranges, so a forming bar can't drag them
around, and they reset on a full recalculation or when you switch metrics.

This is how you calibrate. Don't guess thresholds; read what your instrument
actually produces and set them against that.

---

## Subgraph outputs

All of these are available for spreadsheet studies, alert conditions, and
plotting in a separate region.

| # | Subgraph | Default draw style |
|---|---|---|
| 0 | Bullish Absorption Price | Ignore |
| 1 | Bearish Absorption Price | Ignore |
| 2 | Absorption Score | Ignore |
| 3 | Absorbed Volume | Ignore |
| 4 | Bar Delta | Ignore |
| 5 | Net Trapped Volume | Ignore |
| 6 | Net Imbalance Count | Ignore |
| 7 | Net Absorbed Volume | Ignore |
| 8 | Bar Color | Color Bar |
| 9 | Reversal Score | Ignore |
| 10 | Reversal Up Marker | Point |
| 11 | Reversal Down Marker | Point |

---

## Suggested tuning order

1. **Get bubbles appearing at a sane rate.** Adjust *Min Aggressive Volume At
   Level* until you're flagging a handful of levels per session — not every
   bar, and not none.
2. **Calibrate bubble sizing.** Turn on the volume text, watch the scores,
   then set the min/max score inputs.
3. **Pick a coloring metric and read its history range** from the input title.
   Set full-scale near the peak and the dead zone low enough that meaningful
   bars still color.
4. **Read the reversal peak** from its title. Near 0.9 means you're saturating
   — lower the sensitivity. At 0.15, raise it. Then set the threshold in the
   upper part of your observed range.
5. **Adjust the weights last**, once you can see which components are actually
   earning their place on your instrument.

If no markers appear at all, set *Require Bar At Lookback Extreme* to No
temporarily. That gate alone discards most bars, and turning it off tells you
whether the scoring or the location filter is what's zeroing things out.

---

## What this is and isn't

Absorption, trapped traders and delta divergence are real conditions that
often precede turns. But a confluence score fires on failed turns just as
readily as on real ones, and the ratio between those depends entirely on your
instrument, session and settings.

The reversal score describes what already happened inside a completed bar. It
is not a forecast. Before weighting any decision on it, log the Reversal Score
subgraph to a spreadsheet study and check the hit rate across a few hundred
bars of your own data. The weights are exposed precisely so you can kill the
components that don't earn their keep.

---

## Performance notes

- Each bar owns up to 13 drawing slots, so raising *Number Of Bars To
  Calculate* increases the drawing count linearly.
- Drawings are keyed to stable line numbers and adjusted in place rather than
  deleted and re-added, so live updating is cheap.
- A full recalculation clears the previously drawn range before rebuilding.
- The study runs on a manual loop (`sc.AutoLoop = 0`) because the confirmation
  logic needs forward lookups.
