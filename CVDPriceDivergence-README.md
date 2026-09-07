# CVD / Price Divergence for Sierra Chart

An ACSIL custom study that finds divergences between **Cumulative Volume Delta** and **price**, and draws a line connecting the two swing points that disagree.

When price makes a higher high but CVD makes a lower high, buyers are paying up for less and less net aggression behind the move. This study finds those moments automatically instead of you eyeballing them.

![Example chart](docs/example.png)

---

## What it detects

| Type | Price | CVD | Meaning |
|------|-------|-----|---------|
| **Regular bearish** | Higher high | Lower high | Rally losing buying pressure |
| **Regular bullish** | Lower low | Higher low | Selloff losing selling pressure |
| **Hidden bearish** | Lower high | Higher high | Continuation signal in a downtrend |
| **Hidden bullish** | Higher low | Lower low | Continuation signal in an uptrend |

Regular divergences are on by default. Hidden divergences are off — turn them on once you're comfortable with the pivot settings, or they'll clutter the chart.

Each detection draws:

- A line on the price region between the two swing points
- An optional matching line on your CVD region (see [Drawing on the CVD pane](#drawing-on-the-cvd-pane))
- An arrow above/below the second pivot
- An optional text label with the CVD difference

---

## Requirements

- Sierra Chart with the **Advanced Custom Study Interface** (any recent version)
- A data feed that provides **bid/ask volume** per bar — needed for CVD. Most futures feeds (Denali, Rithmic, CQG, Teton) provide this. If your chart's Volume-at-Price or Ask/Bid Volume columns are empty, CVD won't work.
- The study must be placed on the **main price region**

---

## Installation

1. Download `CVDPriceDivergence.cpp`
2. Copy it into your Sierra Chart `ACS_Source` folder — usually `C:\SierraChart\ACS_Source\`
3. In Sierra Chart: **Analysis >> Build Custom Studies DLL >> Build Custom Studies DLL**
4. Pick `CVDPriceDivergence.cpp` from the list and build. Watch the Message Log for errors.
5. On a chart: **Analysis >> Studies >> Add Custom Study**, find **CVD / Price Divergence Lines** under the `CVD Divergence` group

The DLL reloads automatically after each successful build, so you can edit and rebuild without restarting Sierra Chart.

---

## Setup

### Step 1 — point it at your CVD

The study defaults to calculating CVD internally as a running sum of `AskVolume - BidVolume`. That works, but it will not match a CVD study you already have on the chart if that study resets on sessions or filters trades differently.

**Recommended:** add a CVD study to your chart first (Sierra's *Cumulative Delta Bars* or *Volume Delta* in its own region), then in this study set:

- **CVD Source** → `Study Subgraph Reference`
- **CVD Study and Subgraph** → your CVD study, and the subgraph holding the value you want (usually `Last` or `Close`)

Now the divergence math runs on exactly the series you're looking at.

### Step 2 — set the pivot sensitivity

**Pivot Left Strength** and **Pivot Right Strength** control what counts as a swing. A bar is a swing high when nothing in the N bars to its left is higher and nothing in the N bars to its right reaches it.

- `3 / 3` — many small swings, noisy, lots of signals
- `5 / 5` — a good starting point on 1–5 minute futures charts
- `10 / 10` — only major structure, few signals

Higher right strength means more confirmation lag. A pivot with right strength 5 can't be confirmed until 5 bars later, so the line appears 5 bars after the swing.

### Step 3 — filter the noise

This is the setting most people skip and then wonder why the chart is covered in lines.

**Minimum CVD Difference** is the gap between the two CVD readings, in contracts. The default of `1` accepts almost everything. On ES with 5/5 pivots, try `200`–`500`. On a slower instrument, scale down proportionally.

**Minimum Price Difference (ticks)** does the same for price. Default `1` tick is fine for most uses; raise it if you want to ignore swings that barely made a new high.

### Step 4 (optional) — draw on the CVD pane

Set **Also Draw Line In Region Number** to the region holding your CVD study.

Regions are **0-based here**, but Sierra's UI labels them starting at 1. So the study in "Region 2" of your chart is region `1` in this input. `-1` turns this off.

---

## Input reference

| Input | Default | Notes |
|---|---|---|
| CVD Source | Calculate Internally | Internal, or reference an existing study |
| CVD Study and Subgraph | — | Only used when source is `Study Subgraph Reference` |
| Reset Internal CVD Each Trading Day | No | Only affects the internal calculation |
| Pivot Left Strength | 5 | Bars to the left that must not exceed the pivot |
| Pivot Right Strength | 5 | Bars to the right; this is your confirmation lag |
| Minimum Bars Between Pivots | 5 | Rejects two swings too close together |
| Maximum Bars Between Pivots | 120 | How far back to look for the paired swing |
| Minimum Price Difference (ticks) | 1.0 | Price noise filter |
| Minimum CVD Difference | 1.0 | **Raise this.** CVD noise filter, in contracts |
| Detect Regular Bearish | Yes | |
| Detect Regular Bullish | Yes | |
| Detect Hidden Bearish | No | |
| Detect Hidden Bullish | No | |
| Also Draw Line In Region Number | -1 | 0-based region; -1 disables |
| Line Width | 1 | |
| Bearish Line Color | Red | |
| Bullish Line Color | Green | |
| Use Dashed Lines For Hidden Divergences | Yes | Visual separation from regular ones |
| Confirm Pivots On Closed Bars Only | Yes | Set to No for one bar less lag, at the cost of a signal that can vanish |
| Arrow Offset From Pivot (ticks) | 4 | Keeps arrows off the bars |
| Show Text Labels | No | Prints the type and CVD difference |
| Enable Alerts | No | Sound plus a line in the Alert Log |

---

## Does it repaint?

No, with the default settings. A pivot is only evaluated after its confirmation bars have closed, and once a line is drawn it is never moved or removed. The tradeoff is that signals arrive `Pivot Right Strength + 1` bars after the actual swing.

Setting **Confirm Pivots On Closed Bars Only** to `No` shaves one bar off that lag but allows a pivot to be confirmed using the still-forming bar. That signal can turn out to be wrong once the bar closes.

---

## Troubleshooting

**Build error: `has no member named 'DeleteACSChartDrawing'`**
Your Sierra Chart version uses the newer name. Change that one line to `sc.DeleteACSILChartDrawing(...)` — same parameters.

**Build error on `DRAWSTYLE_ARROW_UP` / `DRAWSTYLE_ARROW_DOWN`**
Older headers may not have these. Substitute `DRAWSTYLE_TRIANGLE_UP` and `DRAWSTYLE_TRIANGLE_DOWN`.

**"the referenced CVD study/subgraph is empty" in the Message Log**
The **CVD Study and Subgraph** input isn't pointing at a valid study, or the subgraph you selected holds no data. Reopen the input and reselect both the study and the subgraph.

**No lines appear at all**
Usually **Minimum CVD Difference** set too high, or pivot strength set too high for the number of bars on screen. Drop both and see if signals appear, then tune back up.

**Way too many lines**
Raise **Minimum CVD Difference** first, then pivot strength.

**Lines on the CVD pane are floating off the plot**
You're using internal CVD while the pane shows a different CVD study. Switch **CVD Source** to `Study Subgraph Reference` so both use the same numbers.

**Old lines left behind after removing the study**
Drawings are cleared on every full recalculation, but if strays remain, use **Chart >> Delete All Drawings From Chart**.

---

## How it works

The study runs with `sc.AutoLoop = 0` and processes only new bars on each update.

1. Builds or fetches the CVD series into a hidden subgraph.
2. Walks forward through unevaluated bars, testing each candidate as a swing high and a swing low. The right-side test uses `>=` so a flat double top only registers once.
3. Confirmed pivots are flagged in persistent extra arrays, so pivot history survives incremental updates without being recomputed.
4. On each new pivot, it scans backward through the flag array for the previous pivot of the same kind, within the min/max bar window, and compares price against CVD at those two bars.
5. Lines are drawn with `s_UseTool` using `UTAM_ADD_OR_ADJUST` and deterministic line numbers derived from the pivot bar index, so recalculations replace drawings rather than stacking duplicates.

---

## A note on interpretation

Divergence is context, not a signal. CVD divergence at the top of a trend day with no other confluence is a good way to get run over. It's most useful at levels you already care about — prior day high, VWAP bands, a volume node — where it tells you the move into that level is arriving with less conviction than the last one.

---

## License

MIT. Do what you want with it, no warranty. Test on sim before trading it.
