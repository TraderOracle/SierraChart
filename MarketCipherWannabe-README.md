# Market Cipher Wannabe — VuManChu Cipher B Divergences for Sierra Chart

An ACSIL (Advanced Custom Study Interface and Language) port of the TradingView Pine Script v4 study **"VuManChu B Divergences"** (VMC Cipher_B_Divergences), rewritten in C++ for Sierra Chart.

This is a single-file study that reproduces the WaveTrend oscillator, its RSI/MFI money-flow overlay, a Stochastic RSI, a Schaff Trend Cycle, and the full fractal-based divergence engine (regular and hidden) that the original Pine indicator uses to paint its coloured circles. It adds a native Sierra Chart alert layer on top, which the original does not have.

---

## Table of contents

- [What this study draws](#what-this-study-draws)
- [Building and installing](#building-and-installing)
- [Calculation reference](#calculation-reference)
  - [WaveTrend](#wavetrend)
  - [RSI + MFI money flow](#rsi--mfi-money-flow)
  - [RSI](#rsi)
  - [Stochastic RSI](#stochastic-rsi)
  - [Schaff Trend Cycle](#schaff-trend-cycle)
  - [The divergence engine](#the-divergence-engine)
  - [Signal circles](#signal-circles)
- [Subgraph reference](#subgraph-reference)
- [Input reference](#input-reference)
- [Alerts](#alerts)
- [Alert flag subgraphs](#alert-flag-subgraphs)
- [Porting notes and known differences](#porting-notes-and-known-differences)
- [Not implemented](#not-implemented)
- [Reading the chart](#reading-the-chart)

---

## What this study draws

Everything renders into a single study region (default `GraphRegion = 1`, i.e. a subgraph pane below price). The pane's vertical scale is roughly **-110 to +110**, because several elements are pinned to fixed y-values so they form tidy rows:

| Element | Y position | Meaning |
|---|---|---|
| WT1 / WT2 lines | oscillator value | The WaveTrend waves themselves |
| Fast WT (WT1 − WT2) | oscillator value | Histogram of the WaveTrend spread |
| RSI + MFI area | oscillator value | Money-flow histogram, green above zero |
| RSI line | 0–100 | Standard RSI, recoloured by zone |
| Stoch K / D | 0–100 | Stochastic RSI |
| Schaff Trend Cycle | 0–100 | Off by default |
| Sell circles | +105 | Row of red dots along the top |
| Divergence sell circles | +106 | Row just above the sell row |
| MFI bar dots | −97 | Row of green/red dots along the bottom |
| Divergence buy circles / Gold | −106 | Row near the bottom |
| Buy circles | −107 | Bottom-most row |

Divergence *markers* (as opposed to circles) are plotted at the actual oscillator value where the fractal occurred, so a WT bearish divergence dot sits on the WT2 line at the swing high.

---

## Building and installing

1. Copy the `.cpp` file into your Sierra Chart `ACS_Source` folder.
2. In Sierra Chart: **Analysis >> Build Custom Studies DLL >> Build**.
3. Add the study to a chart: **Analysis >> Studies >> Add Custom Study**, look for **Market Cipher Wannabe**.

The DLL exports under `SCDLLName("Market Cipher Wannabe")` and the study function is `scsf_MarketCipherWannabe`.

`sc.FreeDLL` is set to `0`, so the DLL stays loaded. If you are actively editing and rebuilding, either flip that to `1` in the defaults block or use **Release Custom Studies DLLs and Deny Load** before each rebuild.

`sc.AutoLoop = 1`, so the study runs once per bar with `sc.Index` set to the bar being processed. `sc.CalculationPrecedence = LOW_PREC_LEVEL` makes it calculate after most other studies, which matters if you chain something off its outputs.

---

## Calculation reference

### WaveTrend

The core oscillator. Given a source series (default HLC3):

```
esa    = EMA(src, channelLength)          // default 9
de     = EMA(|src − esa|, channelLength)  // default 9
ci     = (src − esa) / (0.015 × de)
wt1    = EMA(ci, averageLength)           // default 12
wt2    = SMA(wt1, maLength)               // default 3
wtVwap = wt1 − wt2
```

`ci` is a Commodity-Channel-Index-like normalisation: distance from an EMA, scaled by the average absolute distance. The `0.015` constant is inherited from the CCI formula and keeps typical values inside roughly ±100.

Intermediate series (`esa`, `|src−esa|`, `de`, `ci`) live in `SG_WT1.Arrays[0..3]` rather than separate subgraphs, so they are recalculated correctly on every bar without polluting the subgraph list.

Division is guarded by `VMC::SafeDiv`, which returns `0.0f` on a zero denominator instead of producing `inf`/`NaN`. This matters on flat bars and on the first few bars of a chart.

Derived state:

- `wtOversold   = wt2 <= osLevel`   (default −53)
- `wtOverbought = wt2 >= obLevel`   (default 53)
- `wtCross` — true when the sign of `wt1 − wt2` differs from the previous bar's sign
- `wtCrossUp   = (wt2 − wt1) <= 0`  (wt1 is at or above wt2, i.e. bullish orientation)
- `wtCrossDown = (wt2 − wt1) >= 0`

Note that `wtCrossUp` and `wtCrossDown` are *orientation* tests, not cross tests — they are only meaningful when ANDed with `wtCross`. This mirrors the Pine original exactly, including the fact that both are true when the two waves are precisely equal.

### RSI + MFI money flow

This is the green/red histogram, and despite the name it is not a real Money Flow Index. It is a smoothed candle-body ratio:

```
raw   = ((close − open) / (high − low)) × multiplier   // multiplier default 150
mfi   = SMA(raw, period) − posY                        // period 60, posY 2.5
```

Each bar contributes a number in roughly ±`multiplier` depending on where it closed within its range — a full-bodied up bar contributes +150, a full-bodied down bar −150, a doji ~0. Averaging 60 of those gives a slow "who is winning the candle bodies" reading. The `posY` offset shifts the whole thing down by 2.5, so the zero-crossing is slightly biased.

The histogram colours green above zero and red at or below it (`RGB(62,225,69)` / `RGB(255,61,46)`), and the same colour drives the **MFI Bar** dot row pinned at y = −97. That bottom dot row is the thing people watch for "money flow flipped" — the study exposes that flip as an alert.

### RSI

Standard Wilder's RSI (`sc.RSI(..., MOVAVGTYPE_WILDERS, ...)`), default length 14 on Last. Recoloured per bar:

- ≤ oversold (default 30) → green
- ≥ overbought (default 60) → red
- otherwise → purple

The asymmetric 30/60 defaults are from the original — 60 rather than 70, so the "hot" colour appears earlier.

### Stochastic RSI

```
src = useLog ? ln(src) : src
r   = RSI(src, rsiLength)                        // default 14
raw = 100 × (r − lowest(r, stochLength)) / (highest(r, stochLength) − lowest(r, stochLength))
kk  = SMA(raw, kSmooth)                          // default 3
d   = SMA(kk, dSmooth)                           // default 3
k   = useAverage ? (kk + d) / 2 : kk
```

Log transform is on by default, guarded against non-positive prices. The `highest`/`lowest` helpers (`VMC::HighestValue`, `VMC::LowestValue`) clamp their start index to zero so the first bars of the chart use a shorter, partial window instead of reading out of bounds.

The `useAverage` option replaces K with the mean of K and D, which produces a smoother line that lags slightly more — useful if you are using Stoch divergences and want fewer fractals.

### Schaff Trend Cycle

A double-stochastic of MACD, off by default (`Schaff: Show TC line = No`):

```
macd    = EMA(src, fast) − EMA(src, slow)        // 23 / 50
gamma   = 100 × (macd − lowest(macd, len)) / (highest(macd, len) − lowest(macd, len))
delta   = delta[1] + factor × (gamma − delta[1]) // factor 0.5
eta     = 100 × (delta − lowest(delta, len)) / (highest(delta, len) − lowest(delta, len))
stc     = stc[1] + factor × (eta − stc[1])
```

Both stochastic normalisations carry the previous value forward when the range is zero (a flat window), which avoids a divide-by-zero spike. The recursive smoothing is seeded from the first bar.

All six intermediate series live in `SG_STCCalc.Arrays[0..5]`.

### The divergence engine

This is the most intricate part of the port and lives in `VMC::FindDivergences`.

**Fractals.** The original uses Williams-style 5-bar fractals evaluated with a 2-bar lag:

```
top fractal: src[4] < src[2] && src[3] < src[2] && src[2] > src[1] && src[2] > src[0]
bot fractal: src[4] > src[2] && src[3] > src[2] && src[2] < src[1] && src[2] < src[0]
```

So the fractal *point* is at `src[2]` — two bars in the past — and is only confirmed on the current bar. Everything downstream inherits that two-bar lag.

**Optional OB/OS gating.** When `useLimits` is on, a top fractal only counts if `src[2] >= topLimit`, and a bottom fractal only if `src[2] <= botLimit`. This is how the study restricts divergences to meaningful extremes rather than every wiggle.

**The `valuewhen(...)[2]` idiom.** Pine writes:

```pine
highPrev = valuewhen(fractalTop, src[2], 0)[2]
```

which means "the most recent fractal-top source value, as that state stood two bars ago". Because two fractals can never occur fewer than three bars apart, that expression *always* resolves to the **previous** fractal, never the one being formed right now.

The port reproduces this with carry-forward state arrays rather than trying to search backwards. Each divergence context gets its own subgraph whose extra arrays hold:

| Array | Contents |
|---|---|
| `Arrays[0]` | Last fractal-top oscillator value |
| `Arrays[1]` | Last fractal-top bar high (price) |
| `Arrays[2]` | Last fractal-bottom oscillator value |
| `Arrays[3]` | Last fractal-bottom bar low (price) |
| `Arrays[4]` | 1 once any top fractal has occurred |
| `Arrays[5]` | 1 once any bottom fractal has occurred |
| `Arrays[6]` | Free — used by the caller for the Gold-buy RSI history |

On each bar the state is copied forward from the previous bar, then the **previous** fractal is read from index `Index − 2`, then — and only then — the state is updated with any fractal found on the current bar. That ordering is what makes the `[2]` semantics exact. The `Arrays[4]`/`Arrays[5]` "found" flags prevent the first fractal on a chart from being compared against an uninitialised zero.

**Divergence classification.** With `src[2]` as the current fractal value and `high[2]`/`low[2]` as the corresponding price extreme:

| Type | Condition |
|---|---|
| Regular bearish | price made a higher high (`high[2] > highPrice`) but oscillator made a lower high (`src[2] < highPrev`) |
| Hidden bearish | price made a lower high but oscillator made a higher high |
| Regular bullish | price made a lower low (`low[2] < lowPrice`) but oscillator made a higher low (`src[2] > lowPrev`) |
| Hidden bullish | price made a higher low but oscillator made a lower low |

**Six independent contexts** are evaluated every bar, each with its own state subgraph so they never interfere:

| Context | Source | Limits |
|---|---|---|
| `SG_DivWT` | WT2 | OB 45 / OS −65 |
| `SG_DivWTAdd` | WT2 | OB 15 / OS −40 (the "2nd" wider range) |
| `SG_DivWTnl` | WT2 | none |
| `SG_DivRSI` | RSI | OB 60 / OS 30 |
| `SG_DivRSInl` | RSI | none |
| `SG_DivStoch` | Stoch K | none |

The `nl` (no-limit) contexts exist purely to serve the **"Do NOT apply OB/OS limits on Hidden Divergences"** input. When that is Yes (the default), hidden divergences are taken from the unlimited contexts while regular divergences still come from the gated ones.

**Marker placement.** Pine draws these with `offset = -2`. Rather than using a graph displacement, the port writes directly into element `Index − 2` of the marker subgraph. The result is identical on screen and keeps subgraph values aligned with the bar they actually describe — so a spreadsheet study or an alert formula reading the array sees the divergence on the correct bar.

### Signal circles

**Small cross dots** — plotted at the WT2 value on every WaveTrend cross, green if the cross is upward, red if downward.

**Big buy circle** (y = −107): `wtCross && wtCrossUp && wt2 <= osLevel`
**Big sell circle** (y = +105): `wtCross && wtCrossDown && wt2 >= obLevel`

**Divergence circles** (y = ∓106, written at `Index − 2`): fire when any enabled source reports a regular divergence — WT primary, WT secondary range, Stoch, or RSI. The dot is coloured bright green/red when the primary WT context fired, and a darker shade when only the secondary range did, so you can tell a strong divergence from a marginal one at a glance.

**Gold buy circle** (y = −106, written at `Index − 2`) is the rarest signal and requires all of:

1. A bullish regular divergence on WT (or on RSI if RSI divergences are enabled)
2. A valid previous bottom fractal exists
3. That previous fractal was deeply oversold: `lowPrev <= osLevel3` (default −75)
4. The current WT2 has recovered above `osLevel3`
5. The recovery is meaningful: `(lowPrev − wt2) <= −5`
6. RSI at the previous bottom fractal was below 30

Condition 6 uses its own carry-forward slot (`SG_DivWT.Arrays[6]`), replicating `valuewhen(wtFractalBot, rsi[2], 0)[2]`. It is seeded to 100 so no gold signal can fire before a real bottom fractal has been recorded.

---

## Subgraph reference

### Plotted

| # | Name | Default style | Notes |
|---|---|---|---|
| 0 | WT Wave 1 | Line, blue | |
| 1 | WT Wave 2 | Line, violet | Divergence source |
| 2 | Fast WT (VWAP) | Bar, grey | WT1 − WT2 |
| 3 | RSI+MFI Area | Bar, green/red | Per-bar `DataColor` |
| 4 | MFI Bar | Point, green/red | Fixed at −97 |
| 5 | RSI | Line, recoloured | |
| 6 | Stoch K | Line, cyan | |
| 7 | Stoch D | Line, purple | |
| 8 | Schaff Trend Cycle | Line | Hidden by default |
| 9 | Zero Line | Line, grey | |
| 10 | Over Bought Level 2 | Dash | |
| 11 | Over Sold Level 2 | Dash | |
| 12 | Over Bought Level 3 | Dash | |
| 13 | WT Cross Dot | Point | Every cross |
| 14 | Buy Circle | Point, green | −107 |
| 15 | Sell Circle | Point, red | +105 |
| 16 | Divergence Buy Circle | Point | −106 |
| 17 | Divergence Sell Circle | Point | +106 |
| 18 | Gold Buy Circle | Point, gold | −106 |
| 19 | WT Bearish Divergence | Point | On the WT2 line |
| 20 | WT Bullish Divergence | Point | On the WT2 line |
| 21 | WT 2nd Bearish Divergence | Point, dark red | Wider-range context |
| 22 | WT 2nd Bullish Divergence | Point, dark green | Wider-range context |
| 23 | RSI Bearish Divergence | Point | On the RSI line |
| 24 | RSI Bullish Divergence | Point | On the RSI line |
| 25 | Stoch Bearish Divergence | Point | On the Stoch K line |
| 26 | Stoch Bullish Divergence | Point | On the Stoch K line |

Note there is no "Over Sold Level 3" line subgraph — `osLevel3` is used only as the Gold-buy depth threshold, not drawn.

### Internal (hidden, `DRAWSTYLE_IGNORE`)

| # | Name | Purpose |
|---|---|---|
| 27 | (internal) Stoch RSI | RSI feeding the Stochastic; `Arrays[5..7]` hold log source, raw stoch, KK |
| 28 | (internal) Schaff | `Arrays[0..5]`: ema1, ema2, macd, gamma, delta, eta |
| 29–34 | (internal) Div State ×6 | The six divergence contexts described above |

All hidden work arrays are per-bar indexed, so partial recalculation and historical backfill produce identical results to a full recalculation.

---

## Input reference

### WaveTrend

| Input | Default |
|---|---|
| WT: Show WaveTrend | Yes |
| WT: Show Fast WT | Yes |
| WT: Channel Length | 9 |
| WT: Average Length | 12 |
| WT: MA Length | 3 |
| WT: MA Source | HLC3 |
| WT: Overbought Level 1 | 53 |
| WT: Overbought Level 2 | 60 |
| WT: Overbought Level 3 | 100 |
| WT: Oversold Level 1 | −53 |
| WT: Oversold Level 2 | −60 |
| WT: Oversold Level 3 | −75 |

Level 1 gates the big buy/sell circles. Level 2 is drawn as a dashed reference line. Level 3 is drawn on the overbought side and used as the Gold-buy depth threshold on the oversold side.

### Circles

| Input | Default |
|---|---|
| WT: Show Buy dots | Yes |
| WT: Show Gold dots | Yes |
| WT: Show Sell dots | Yes |
| WT: Show Div. dots | Yes |

### WaveTrend divergences

| Input | Default |
|---|---|
| WT: Show Regular Divergences | Yes |
| WT: Show Hidden Divergences | No |
| Do NOT apply OB/OS limits on Hidden Divergences | Yes |
| WT: Bearish Divergence min | 45 |
| WT: Bullish Divergence min | −65 |
| WT: Show 2nd Regular Divergences | Yes |
| WT: 2nd Bearish Divergence | 15 |
| WT: 2nd Bullish Divergence | −40 |

### Money flow

| Input | Default |
|---|---|
| MFI: Show MFI Area | Yes |
| MFI: Period | 60 |
| MFI: Area multiplier | 150 |
| MFI: Area Y Pos | 2.5 |
| MFI: Show MFI Bar | Yes |

Raising the multiplier makes the histogram taller without changing where it crosses zero; raising `Area Y Pos` biases the whole series downward, which makes green readings rarer.

### RSI

| Input | Default |
|---|---|
| RSI: Show RSI | Yes |
| RSI: Source | Last |
| RSI: Length | 14 |
| RSI: Oversold | 30 |
| RSI: Overbought | 60 |
| RSI: Show Regular Divergences | No |
| RSI: Show Hidden Divergences | No |
| RSI: Bearish Divergence min | 60 |
| RSI: Bullish Divergence min | 30 |

Enabling RSI regular divergences also feeds the divergence circles and the Gold-buy condition, so it changes signal frequency, not just what is drawn.

### Stochastic RSI

| Input | Default |
|---|---|
| Stoch: Show Stochastic RSI | Yes |
| Stoch: Use Log Source | Yes |
| Stoch: Use Average of both K & D | No |
| Stoch: Source | Last |
| Stoch: Stochastic Length | 14 |
| Stoch: RSI Length | 14 |
| Stoch: K Smooth | 3 |
| Stoch: D Smooth | 3 |
| Stoch: Show Regular Divergences | No |
| Stoch: Show Hidden Divergences | No |

### Schaff Trend Cycle

| Input | Default |
|---|---|
| Schaff: Show TC line | No |
| Schaff: Source | Last |
| Schaff: TC Length | 10 |
| Schaff: TC Fast Length | 23 |
| Schaff: TC Slow Length | 50 |
| Schaff: TC Factor | 0.5 |

---

## Alerts

Alerts are off by default. Turn on **"Alerts: Enable Alerts"**, then enable the individual categories you want. Each category has its own sound number so you can distinguish them by ear:

- **0** — use whatever is configured on the study's Alerts tab
- **1..N** — play that numbered sound from *Global Settings >> General Settings >> Alert Sounds*

| Category | Default sound | Fires on |
|---|---|---|
| Small Dots | 1 | Every WaveTrend cross, either direction |
| Big Dots | 2 | Buy/sell circle (cross in oversold/overbought) |
| Divergence Dots | 3 | Any enabled regular divergence |
| Gold Dot | 4 | The full Gold-buy condition |
| Bottom Dots Colour Flip | 5 | Money-flow histogram crossing zero |

### Bar close vs intrabar

**"Alerts: Only At Bar Close" = Yes** (default) evaluates using `sc.GetBarHasClosedStatus()`, so the alert only fires for a bar that has finished. A cross that appears mid-bar and then un-crosses before the close will never alert.

**= No** evaluates on the forming bar (`Index == sc.ArraySize - 1`). Faster, but it will alert on conditions that subsequently disappear.

### Suppression rules

Alerts are skipped entirely during full recalculation (`sc.IsFullRecalculation`) and while historical data is downloading, so loading a chart does not machine-gun you with every signal in the history.

Within a live session, each alert type fires at most once per bar. That is done with `sc.GetPersistentInt(key)` storing the last bar index that alerted, per category:

| Key | Category |
|---|---|
| 1 / 2 | Small dot up / down |
| 3 / 4 | Big buy / big sell |
| 5 / 6 | Divergence bullish / bearish |
| 7 | Gold |
| 8 | Money-flow flip (shared — both directions cannot occur on the same bar) |

These trackers are reset to −1 whenever `sc.UpdateStartIndex == 0`, i.e. on every full recalculation.

Alert messages are formatted as:

```
VMC Cipher B | <symbol> | <description>
```

Descriptions for divergence and gold alerts include *"(marks bar -2)"* as a reminder that the visual dot appears two bars back from where the alert fires.

Evaluation order inside a bar is Gold first, then big dots, then divergences, then small dots, then money-flow — so if several conditions coincide, the most significant sound plays first.

---

## Alert flag subgraphs

Every alert condition is mirrored into a hidden numeric subgraph, whether or not alerts are enabled. Values are **+1 bullish / −1 bearish / 0 nothing**.

| # | Name |
|---|---|
| 35 | Alert Flag: Small Dot (+1 up / −1 down) |
| 36 | Alert Flag: Big Dot (+1 buy / −1 sell) |
| 37 | Alert Flag: Divergence Dot (+1 bull / −1 bear) |
| 38 | Alert Flag: Gold Dot (+1) |
| 39 | Alert Flag: Bottom Dots Flip (+1 to green / −1 to red) |

Because they are ordinary subgraph arrays, they can be consumed by:

- **Chart Alerts** — write a formula like `ID1.SG39 = 1`
- **Spreadsheet studies** — reference the subgraph column directly
- **Other custom studies** — via `sc.GetStudyArrayUsingID()`
- **Automated trading systems** — as an entry trigger

They are set to `DRAWSTYLE_IGNORE` with `DrawZeros = 0`, so they never appear on the chart.

Note that these flags are written on the bar where the *condition evaluates*, which for divergence and gold flags is two bars after the fractal they describe. If you are backtesting off them, that is the honest, non-lookahead timing.

---

## Porting notes and known differences

**1. Moving-average seeding.** Pine's `ema()` seeds from the very first bar of the series; Sierra Chart's `sc.ExponentialMovAvg()` seeds with `In[0]`. The two converge within a few multiples of the length, but early bars on a short chart will not match TradingView exactly. Load at least a few hundred bars of history before comparing values side by side.

**2. RSI implementation.** `sc.RSI(..., MOVAVGTYPE_WILDERS, ...)` matches Pine's `rsi()` in method, but again with a different seed on the first bars.

**3. Draw styles.** ACSIL has no direct equivalent of Pine's `plot.style_area` with transparency, so the money-flow area and the Fast WT are drawn as `DRAWSTYLE_BAR` histograms and the waves as lines. Change any of these in *Study Settings >> Subgraphs* to taste.

**4. "Show" toggles.** The various `Show ...` inputs are applied by rewriting `DrawStyle` when `sc.UpdateStartIndex == 0` — that is, on full recalculation. Toggling one takes effect immediately because changing a study input triggers a recalculation, but note that the underlying values are still computed either way. Turning things off saves rendering, not CPU.

**5. Marker persistence on the forming bar.** Divergence and gold circles are written to `Index − 2`. During live updates the current bar recalculates repeatedly, but bar `Index − 2` is already closed and is not re-cleared. In practice this is what you want — a confirmed fractal cannot un-confirm — but it does mean a marker written during an intrabar evaluation stays put even if the very last tick would have changed the outcome. With the default bar-close alert setting this is a non-issue for the alert layer.

**6. Zero-denominator handling.** `SafeDiv` returns 0 rather than infinity. This differs from Pine, which would produce `na` and break the plot. In practice it means flat bars (`high == low`) contribute 0 to the money-flow series instead of creating a gap.

---

## Not implemented

Three features of the original Pine study are deliberately absent, all for the same reason: they depend on `security()` calls to higher timeframes.

- **Sommi flags** — require 720-minute and 60-minute Heikin-Ashi WaveTrend
- **Sommi diamonds** — require 60-minute and 240-minute WaveTrend
- **MACD-based WaveTrend colouring** — requires a higher-timeframe MACD

ACSIL has no direct `security()` equivalent. Implementing these would require either `sc.GetStudyArrayFromChartUsingID()` / `sc.GetChartBaseData()` pointed at separate higher-timeframe charts, or time-and-sales-based aggregation. Both approaches force the user to set up and reference specific chart numbers, so they were left out rather than shipped as a fragile configuration burden.

If you want them, the cleanest route is: create hidden charts at the required timeframes, add this same study to each, and add chart-number inputs so the main instance can pull `SG_WT1`/`SG_WT2` across with `sc.GetStudyArrayFromChartUsingID()`.

---

## Reading the chart

A short field guide for anyone who has not used Cipher B before:

- **Small dots** on the WT2 line mark every wave cross. Most are noise.
- **Big green/red circles** at the top and bottom mark crosses that happened in overbought or oversold territory. These are the basic entry cues.
- **Divergence dots** on the WT2 line mean price and the oscillator disagreed at a swing. Bright colours are the primary (deeper) OB/OS range, dark colours are the secondary (shallower) range.
- **Divergence circles** in the top/bottom rows are the same events, promoted to the signal rows so they line up with the buy/sell circles.
- **The gold circle** is the confluence signal: a deeply oversold WaveTrend, a bullish divergence, a real recovery off the low, and RSI under 30 at the prior swing. It is rare by design.
- **The bottom dot row** is the money-flow read. Green means candle bodies have been net bullish over the lookback; red means net bearish. A flip is a slower, contextual signal rather than an entry.

Everything with the two-bar lag — divergences, divergence circles, gold — is confirmed two bars after the fact and cannot be known earlier. That is inherent to fractal detection, not a limitation of this port.
