# God Trades — Sierra Chart ACSIL Study

A multi-confluence price-action study for Sierra Chart, written in C++ (ACSIL). It overlays directly on the price graph and combines nine trend/momentum filters into a single buy/sell arrow, while independently detecting and annotating a set of candlestick and market-structure patterns: volume imbalances, engulfing bars at the Bollinger extremes, three-outside reversals, tweezer tops/bottoms, "trampolines", shaved candles, and squeeze-momentum turns.

It also drives a fairly elaborate Sierra Chart alert layer, including symbol-aware alert IDs (different alert numbers on NQ vs everything else) and tracking of volume-imbalance gaps until they get filled.

The DLL exports as `God Trades DLL`; the study function is `scsf_GodTrades` and appears in the study list as **God Trades**.

> **Read the [Known issues, bugs and quirks](#known-issues-bugs-and-quirks) section before trusting any signal.** This is working-trader code with a number of unfinished paths, dead branches, and several genuine bugs that change behaviour. They are documented individually below rather than glossed over.

---

## Table of contents

- [What it draws on the chart](#what-it-draws-on-the-chart)
- [Building and installing](#building-and-installing)
- [Program structure](#program-structure)
- [Helper function reference](#helper-function-reference)
- [Pattern definitions](#pattern-definitions)
- [The indicator engine](#the-indicator-engine)
- [The buy/sell confluence gate](#the-buysell-confluence-gate)
- [Bar coloring](#bar-coloring)
- [Volume imbalance tracking](#volume-imbalance-tracking)
- [Alerts](#alerts)
- [Execution flow and early returns](#execution-flow-and-early-returns)
- [Input reference](#input-reference)
- [Subgraph reference](#subgraph-reference)
- [Persistent state](#persistent-state)
- [Known issues, bugs and quirks](#known-issues-bugs-and-quirks)
- [Reading the chart](#reading-the-chart)

---

## What it draws on the chart

`sc.GraphRegion = 0`, so everything renders on the main price graph rather than in a separate pane. Nothing is a line plot except KAMA — the rest are markers, bar colours, background shading, text labels, and horizontal rays.

| Visual | How it's drawn | Meaning |
|---|---|---|
| Green triangle below bar | `DRAWSTYLE_TRIANGLE_UP` | All enabled filters agree long |
| Red triangle above bar | `DRAWSTYLE_TRIANGLE_DOWN` | All enabled filters agree short |
| Yellow star | `DRAWSTYLE_STAR` | Squeeze-momentum histogram turned |
| White up/down arrow | `DRAWSTYLE_ARROW_UP` / `_DOWN` | Volume imbalance detected |
| White horizontal ray | `AddLineUntilFutureIntersection` | The volume-imbalance gap price, extended until price fills it |
| Gold line | `DRAWSTYLE_LINE` | KAMA (Kaufman Adaptive MA, 9/2/109) |
| Dark green / dark magenta background | `DRAWSTYLE_BACKGROUND` | Engulfing-style bar outside the Bollinger band |
| Pale blue / pink bar | `DRAWSTYLE_COLOR_BAR` | Shaved candle |
| Gradient green/red bars | `DRAWSTYLE_COLOR_BAR` | Optional bar coloring by Waddah / Linda MACD / Supertrend |
| `3oU` / `3oD` text | `sc.UseTool` DRAWING_TEXT | Three Outside Up / Down |
| `Eq Hi` / `Eq Lo` text | `sc.UseTool` DRAWING_TEXT | Tweezer top / bottom |
| `TR` text | `sc.UseTool` DRAWING_TEXT | Trampoline reversal |
| `FILL` text | `sc.UseTool` DRAWING_TEXT | Volume imbalance filled (currently dead — see quirks) |

Text labels are drawn with `s_UseTool`, not with subgraph draw styles, so they are chart drawings rather than plot values. The corresponding subgraphs (`3oU`, `3oD`, `Equal High`, `Equal Low`, `Trampoline`) are still written with the bar's low so the events are queryable from spreadsheets or other studies.

---

## Building and installing

1. Copy the `.cpp` into `<SierraChart>/ACS_Source`.
2. **Analysis >> Build Custom Studies DLL >> Build**.
3. **Analysis >> Studies >> Add Custom Study >> God Trades**.

`sc.AutoLoop = 1`, so the study body runs once per bar with `sc.Index` set to the bar being processed. The study writes to the main price region, so no separate chart region setup is needed.

Because several alert paths use hardcoded alert numbers, you will want to configure sounds in **Global Settings >> General Settings >> Alert Sounds** for the IDs listed in the [Alerts](#alerts) section before the study is useful for live monitoring.

---

## Program structure

The file is organised with `#pragma region` blocks:

```
COMMON FUNCTIONS   — free functions for candle geometry and pattern tests
scsf_GodTrades
  ├─ INPUTS          — input/subgraph handle declarations
  ├─ DEFAULTS        — sc.SetDefaults block
  ├─ LOCAL VARIABLES — alert ID selection, per-bar candle geometry
  ├─ INDICATORS      — all indicator computation (wrapped in a bare scope block)
  │   ├─ EXTRA ALERTS      — pattern detection + text drawing
  │   ├─ BAR COLORING
  │   ├─ BUY SELL PLOTS
  │   └─ VOLUME IMBALANCE
```

Note the `INDICATORS` region opens a bare `{ ... }` scope where a `for` loop over `sc.UpdateStartIndex .. sc.ArraySize` used to be — the loop header survives as a comment directly above it. Everything from the indicator calls down to the end of the function lives inside that scope. This is why the region markers appear to interleave oddly: `#pragma endregion` for INDICATORS closes before the nested regions, but the brace does not close until the very end of the function.

---

## Helper function reference

All helpers take `SCBaseDataRef` or `SCStudyInterfaceRef` plus an explicit bar index, so they can be evaluated at any bar rather than only the current one.

### Geometry

| Function | Returns |
|---|---|
| `CandleLength(d, i)` | `high − low` (full range) |
| `BodyLength(d, i)` | `abs(open − close)` |
| `PercentOfCandleLength(d, i, p)` | `range × p/100` |
| `PercentOfBodyLength(d, i, p)` | `body × p/100` |
| `UpperWickLength(d, i)` | `high − max(open, close)` |
| `LowerWickLength(d, i)` | `min(open, close) − low` |
| `IsGreen(d, i)` | `close > open` |
| `IsRed(d, i)` | `close < open` |

A bar where `open == close` is neither green nor red under these definitions, and so fails every pattern test that requires a direction.

### Tolerance

```cpp
bool IsNearEqual(v1, v2, InData, index, percent) {
    return abs(v1 - v2) < (3 * percent);
}
```

The `InData` and `index` parameters are ignored — the original percent-of-range implementation is commented out inline. Every call site passes `sc.TickSize` as `percent`, so in practice this means **"within three ticks"**, a fixed absolute tolerance rather than a proportional one.

### Unused helpers

`PercentOfBodyLength`, `IsUpperWickSmall`, `IsLowerWickSmall`, `IsBodyStrong`, `IsDoji`, and `IsStairs` are defined but never called. `IsStairs` is an empty stub that always returns `false`. `IsBodyStrong` has an integer-accumulator bug (see quirks). `IsDoji` is superseded by an inline doji calculation in the main function body.

### `DrawText`

```cpp
void DrawText(sc, screffy, txt, iAboveCandle, iBuffer)
```

Creates a `DRAWING_TEXT` chart drawing:

- **Anchor bar**: `sc.Index`, except for the labels `"Eq Lo"`, `"Eq Hi"`, `"TR"`, which anchor one bar back — those patterns describe a pivot that sits at the previous bar.
- **`iAboveCandle`**: `1` = above the bar, `−1` = below, `0` = automatic (above if the anchor bar is green, below if red).
- **`iBuffer`**: vertical offset expressed as a percentage of the anchor bar's range.
- **Styling is borrowed from the passed subgraph**: `PrimaryColor` → text colour, `SecondaryColor` → background, and `LineWidth` → **font size**. That last reuse is why the text subgraphs are all declared with `LineWidth = 8`. Change the "Line Width" field in Study Settings to resize the labels.
- `"TR"` labels get their background forced to `COLOR_RED` regardless of the subgraph setting.
- `AddMethod = UTAM_ADD_ALWAYS`, meaning each call adds a *new* drawing rather than updating an existing one.

Note also that `Tool.BeginIndex` is set from `sc.CurrentIndex` while the *value* (vertical position) is computed from the possibly-shifted `i`. For the three back-shifted labels this places the text at the current bar's x-position but the previous bar's y-position.

---

## Pattern definitions

### Engulfing (strict)

```
IsBullishEngulfing(i):
    close[i-1] < open[i-1]      // previous bar red
 && close[i]   > open[i]        // current bar green
 && high[i] > high[i-1]         // outside bar
 && low[i]  < low[i-1]
 && close[i] > open[i-1]        // body engulfs body
 && open[i]  < close[i-1]
```

This is stricter than the textbook definition: it requires a full **outside bar** (both a higher high and a lower low) in addition to body engulfment. `IsBearishEngulfing` is the mirror image.

### Three Outside Up / Down

```
IsThreeOutsideUp(i):  green(i) && close[i] > close[i-1] && IsBullishEngulfing(i-1)
```

The engulfing pattern completes at `i−1`, and bar `i` confirms it with a higher close in the same direction. Drawn as the `3oU` / `3oD` label.

### Tweezer top

```
IsTweezerTop(i, UpperBand):
    IsNearEqual(open[i-1], close[i-2])     // within 3 ticks
 && low[i] < low[i-1]
 && red(i) && red(i-1)
 && green(i-2) && green(i-3)
 && (high[i-1] > UpperBand || high[i-2] > UpperBand)
```

The shape is two green bars into a high, then two red bars away from it, with the turn point (the previous bar's open against the bar-before's close) matching to within three ticks, and the swing high poking outside the upper Bollinger band. `IsTweezerBottom` mirrors this against the lower band. Drawn as `Eq Hi` / `Eq Lo`.

### Trampoline

```
IsTrampoline(i, rsi, prsi, pprsi, BBBand, iTickSize):

  bearish variant:
      red(i) && red(i-1) && green(i-2)
   && close[i] < close[i-1]
   && (rsi > 80 || prsi > 80 || pprsi > 80)
   && high[i-2] >= BBBand - iTickSize

  bullish variant:
      green(i) && green(i-1) && red(i-2)
   && close[i] > close[i-1]
   && (rsi < 20 || prsi < 20 || pprsi < 20)
   && low[i-2] <= BBBand + iTickSize
```

Two bars away from a reversal bar, with RSI at an extreme within the last three bars, and the reversal bar touching a Bollinger band. Both variants live in one function and are tested against whichever band the caller passes, which produces some behaviour worth knowing about — see quirks. Drawn as `TR`.

### Volume imbalance

```
IsVolImbGreen(i): green(i) && green(i-1) && open[i] > close[i-1]
IsVolImbRed(i):   red(i)   && red(i-1)   && open[i] < close[i-1]
```

Two same-direction bars where the second opens beyond the first's close, leaving a price gap with no traded overlap between them. The gap price recorded is `open[i]`, and a white ray is projected forward from that bar at that price until price intersects it.

### Shaved candle

```
red(i) && close[i] == low[i]   ->  Subgraph_ShavedRed[i] = 1
```

A red bar that closes exactly on its low. Only the red case is implemented; `Subgraph_ShavedGreen` exists with a colour assigned but is never written.

### Bollinger break with expansion

```
BBGreen: low[i] < LowerBand && low[i-1] < LowerBand && green(i) && body[i] > body[i-1]
BBRed:   high[i] > UpperBand && high[i-1] > UpperBand && red(i)  && body[i] > body[i-1]
```

Two consecutive bars outside the band, with the current bar reversing direction and having a larger body than the previous one. Shaded as a chart background.

---

## The indicator engine

Every indicator is recomputed on each bar. All handles are `DRAWSTYLE_IGNORE` subgraphs used as work arrays, except KAMA.

### Supertrend

```
TR       = sc.TrueRange()
ATR      = HullMovingAverage(TR, 11)      // Hull, not Wilder's
basicUB  = HL_midpoint + 2 × ATR
basicLB  = HL_midpoint − 2 × ATR

UB = (basicUB < UB[-1] || close[-1] > UB[-1]) ? basicUB     : UB[-1]
LB = (basicLB > LB[-1] || close[-1] < LB[-1]) ? basicLB[-1] : LB[-1]   // note the [-1]
```

Then the standard flip logic: whichever band the trend line currently sits on, and whether close has crossed it, decides which band it becomes next. `bSuperUp` is `true` when the trend line sits on the lower band.

Using a Hull moving average for the ATR rather than Wilder's smoothing is a deliberate deviation — it responds faster and produces tighter bands than a conventional Supertrend. The multiplier (2) and period (11) are hardcoded locals, not inputs.

### Squeeze momentum

An approximation of the LazyBear "Squeeze Momentum" histogram:

```
mid   = (highest(high,20) + lowest(low,20)) / 2
basis = EMA(close, 20)
raw   = open − (mid + basis) / 2
hist  = LinearRegressionIndicator(raw, 20)
```

Standard implementations use `close − (mid + basis)/2`; this uses **open**. `sc.DataStartIndex` is set to 20 here so the chart skips the warmup bars.

Turn detection uses persistent state so only the first turn of each swing prints:

- Histogram is ≤ 0 and rising, and the last star was a down star → plot an up star at `low − UpOffset ticks`, flip state.
- Histogram is ≥ 0 and falling, and the last star was an up star → plot a down star at `high + DownOffset ticks`, flip state.

### Fisher transform

Hand-rolled rather than using a built-in:

```
price = HL midpoint
value = 0.66 × ((price − lowest(10)) / range − 0.5) + 0.67 × value[-1]
value = clamp(value, ±0.999)
fish  = 0.5 × (ln((1 + value) / (1 − value)) + Fisher[-1])
```

The clamp prevents the logarithm from blowing up at ±1. `fish` is a local; nothing is ever written back into `Subgraph_Fisher`, so the recursive smoothing term is permanently zero (see quirks).

### Waddah Attar Explosion

```
t1 = ((EMA20 − EMA40) − (EMA20[-1] − EMA40[-1])) × Intensity
```

The rate of change of the MACD-style spread, scaled by the `Waddah Intensity` input (default 150). Sign gives direction; magnitude drives the bar-colour gradient.

### Everything else

| Indicator | Call | Parameters |
|---|---|---|
| KAMA | `sc.AdaptiveMovAvg` | 9, fast 2, slow 109 |
| T3 | `sc.T3MovingAverage` | volume factor 0.84, length 10 |
| ADX | `sc.ADX` | DX 14, DX MA 14 |
| Awesome Oscillator | `sc.AwesomeOscillator` | 0, 0 (defaults, 5/34) |
| Hull MA | `sc.HullMovingAverage` | 10 |
| Bollinger | `sc.BollingerBands` | 20, 2σ, SMA — `Arrays[0]` upper, `Arrays[1]` lower |
| Linda MACD | `sc.MACD` | 3, 9, 16, SMA — histogram read from `Arrays[3]` |
| Parabolic SAR | `sc.Parabolic` | 0.02 / 0.02 / 0.2, on High/Low |
| RSI | `sc.RSI` | SMA smoothing, 14 — plus `prsi` and `pprsi` for the two prior bars |

All of these are hardcoded. Only the Waddah intensity, the minimum ADX, and the two bar-colour offsets are exposed as inputs.

---

## The buy/sell confluence gate

The core signal is a **veto** system rather than a scoring system. Both `bShowUp` and `bShowDown` start `true`, and any disagreeing enabled filter sets them to `false`:

| Filter | Vetoes long when | Vetoes short when | Default |
|---|---|---|---|
| Linda MACD | `linda < 0` | `linda > 0` | On |
| Parabolic SAR | `SAR > close` | `SAR < close` | On |
| Fisher | `fish < 0` | `fish > 0` | On |
| T3 | `t3 > close` | `t3 < close` | Off |
| Waddah | `t1 <= 0` | `t1 > 0` | On |
| Awesome Osc | `ao < 0` | `ao > 0` | Off |
| Hull MA | `hma > close` | `hma < close` | On |
| Supertrend | `!bSuperUp` | `bSuperUp` | Off |
| **ADX** | `adx < minimum` | `adx < minimum` | **Always applied** |

The ADX floor (default 11) has no on/off toggle — it always applies and blocks both directions in low-trend conditions.

A filter being disabled does not make it neutral in a scoring sense; it simply removes its veto. With every toggle off, the only remaining gate is ADX, and the study would print both an up and a down triangle on nearly every bar. Conversely, enabling all nine makes signals rare.

Triangles are placed at `low − UpOffset × TickSize` and `high + DownOffset × TickSize`.

---

## Bar coloring

Selected by the **Bar coloring** custom-string input: `None` / `Waddah` / `Linda MACD` / `Supertrend`.

When any mode other than `None` is active, the study first fills `Subgraph_ColorBar` with a 14-period RSI. The RSI values themselves are irrelevant — the point is that `DRAWSTYLE_COLOR_BAR` only paints a bar when the subgraph value is non-zero, so this is a way of forcing every bar to be non-zero so the per-bar `DataColor` takes effect.

- **Waddah**: intensity `= min(255, |t1| + WaddahOffset)`, green if `t1 > 0` else red. Stronger momentum produces a brighter bar; the offset sets the floor so weak bars are still visible.
- **Linda MACD**: intensity `= min(255, |t1| + LindaOffset)` — still derived from the Waddah value — but the *sign* comes from the Linda MACD histogram. So the hue follows Linda and the brightness follows Waddah, which is either a useful two-signal display or a copy-paste artifact depending on your reading.
- **Supertrend**: flat full green / full red, no gradient.

---

## Volume imbalance tracking

This is the most stateful part of the study and runs in three stages.

### 1. Ray creation (runs for every bar)

```cpp
if (IsVolImbGreen(sc, sc.CurrentIndex))
    sc.AddLineUntilFutureIntersection(i, i, open, RGB(255,255,255), 2, LINESTYLE_SOLID, false, false, "");
```

This deliberately sits **before** the `if (!bIsCurrentBar) return;` guard. The inline comment explains why: if it ran only on the current bar, the rays would vanish after a chart reload or an INS-key full recalculation.

### 2. Marker and state recording (current bar only)

Three parallel subgraphs act as a per-bar record:

| Subgraph | Contents |
|---|---|
| `VolImb Origin Candle` (43) | Bar index where the imbalance formed |
| `VolImb Direction` (41) | `+1` green, `−1` red, `0` once filled |
| `VolImb Price` (42) | The gap price (`open` of the imbalance bar) |

All three are zeroed at the top of each current-bar pass before being conditionally rewritten, so a bar that no longer qualifies does not retain stale state.

### 3. Fill detection (current bar, on bar close)

Walks the study's active `LineUntilFutureIntersection` rays via `sc.GetNumLinesUntilFutureIntersection` and `sc.GetStudyLineUntilFutureIntersectionByIndex`, and for each one:

- If the current bar's **low** dropped below a ray whose origin bar has `Direction == +1`, the green imbalance is filled.
- If the current bar's **high** rose above a ray whose origin bar has `Direction == −1`, the red imbalance is filled.

On a fill, `Direction[StartIndex]` is set to `0` so it cannot re-trigger, and an alert fires **only if the gap is between 3 and 29 bars old** — filtering out both immediate fills and stale gaps. The loop `break`s on the first match, so at most one fill alert per bar.

A second, older implementation of the same logic — scanning the subgraph arrays backwards instead of the line list — survives as a large commented-out block below it.

---

## Alerts

Alert IDs are integers passed to `sc.AlertWithMessage(id, text)`; configure the corresponding sounds under **Global Settings >> General Settings >> Alert Sounds**.

### Symbol-dependent ID mapping

At the top of each bar the study inspects `sc.Symbol` with `strstr` and remaps some IDs so you can distinguish instruments by ear:

| Event | Default | NQ |
|---|---|---|
| Buy signal | 5 | 7 |
| Sell signal | 6 | 8 |
| BB-gap green | 21 | 21 |
| BB-gap red | 22 | 22 |
| VolImb green | 17 | 18 |
| VolImb red | 17 | 18 |
| VolImb fill | 19 | 20 |
| Trampoline green | 23 | 23 |
| Trampoline red | 24 | 24 |
| KAMA wick | 13 | 13 |

The `ES` branch and the fallback `else` branch are both empty — ES uses the defaults. `ALERT_EMA21_WICK` (12) and `ALERT_KAMA` (13) are declared but never used; `ALERT_KAMA_WICK` shadows the same value 13. Note also that the green and red volume-imbalance IDs are identical within each symbol, so those two events would be indistinguishable by sound even if they were enabled.

### Which alerts actually fire

| Event | Fires? | Conditions |
|---|---|---|
| Buy / Sell signal | **Yes** | `bIsCurrentBar` |
| KAMA bounce | **Yes** | NQ only, current bar, bar closed, wick through KAMA with body rejecting |
| BB-gap green | **Yes** | Green volume imbalance where current or previous low is below the lower band |
| BB-gap red | **Yes** | Red volume imbalance where current or previous high is above the upper band |
| VolImb fill | **Yes** | Ray intersected, gap age 3–29 bars |
| Plain VolImb green/red | **No** — logged only | The `AlertWithMessage` calls are commented out |
| Trampoline | **No** — logged only | Commented out |

Every alert path is accompanied by an `sc.AddMessageToLog(..., 1)` call. The `1` means "raise the Message Log window". If you find the log popping up constantly, change those second arguments to `0` to log silently.

### KAMA bounce

```
high > kama && open < kama && close < kama    // wick up through KAMA, rejected
low  < kama && open > kama && close > kama    // wick down through KAMA, rejected
```

A bar that pierced KAMA with a wick but opened and closed on the same side of it. Only evaluated for symbols containing `"NQ"`.

---

## Execution flow and early returns

Two early returns strongly shape what runs where.

**1. Doji skip.**

```cpp
if (Input_IgnoreDoji.GetYesNo() == SC_YES && doji)
    return;
```

Placed immediately after the indicator block. With the default `Ignore Dojis = Yes`, a doji bar skips *everything downstream* — shaved-candle detection, Bollinger shading, all pattern labels, bar colouring, buy/sell triangles, and the entire volume-imbalance section. The input's description ("Ignore when wicks are larger than candle body") describes the doji test, but the effect is broader than filtering signals: the bar is simply not processed at all.

**2. Current-bar guard.**

```cpp
bool bIsCurrentBar = (i == sc.ArraySize - 2);
...
if (!bIsCurrentBar) return;
```

Note this is `ArraySize − 2`, which is the **last fully closed bar**, not the forming bar at `ArraySize − 1`. Everything after this point — the volume-imbalance fill scan, the imbalance markers and state, and the god-trade alerts — only runs on that one bar. Historical bars get indicators, patterns, bar colours, buy/sell triangles, and imbalance rays, but no imbalance markers or state.

That also means `Subgraph_VolImbUp` / `VolImbDown` arrows and the direction/price state arrays only ever populate for bars the study has observed live. On a fresh chart load, historical imbalances will show their rays (drawn before the guard) but not their arrows.

The `3oU` / `3oD` text labels are similarly gated on `bIsCurrentBar` at their call sites, while the tweezer and trampoline labels are not — so those two draw across history and the three-outside labels do not.

---

## Input reference

| # | Input | Default | Notes |
|---|---|---|---|
| 0 | Waddah Intensity | 150 | Scales `t1`; affects both the veto threshold and bar-colour brightness |
| 1 | Use Waddah | Yes | |
| 2 | Use MACD | Yes | Linda MACD 3/9/16 |
| 3 | Use Parabolic Sar | Yes | |
| 4 | Use Supertrend | No | |
| 5 | Use Awesome Oscillator | No | |
| 6 | Use Hull Moving Average | Yes | |
| 7 | Use T3 | No | |
| 8 | Use Fisher Transform | Yes | |
| 9 | Minimum ADX | 11 | Always applied, no toggle |
| 10 | Ignore Dojis | Yes | See the doji early return above |
| 11 | Bar coloring | None | `None;Waddah;Linda MACD;Supertrend` |
| 12 | Bar color Waddah offset | 80 | Brightness floor |
| 13 | Bar color LindaMACD offset | 40 | Brightness floor |
| 14 | Up Offset In Ticks | 2 | Buy triangle, up star, and both volimb arrows |
| 15 | Down Offset In Ticks | 2 | Sell triangle and down star only |
| 16 | Shaved candle buffer | 1 | **Declared but never used** |
| 17 | Show Doji Cities | Yes | **Declared but never used** |

Inputs are read live each bar, so toggles take effect on the next recalculation.

---

## Subgraph reference

### Visible

| # | Name | Style | Colour |
|---|---|---|---|
| 0 | Standard Buy Dot | Triangle up | Green |
| 1 | Standard Sell Dot | Triangle down | Red |
| 2 | Volume Imbalance Up | Arrow up | White |
| 3 | Volume Imbalance Down | Arrow down | White |
| 4 | Squeeze Buy Dot | Star | Yellow |
| 5 | Squeeze Sell Dot | Star | Yellow |
| 6 | Three Outside Up | Custom text | Yellow on dark green |
| 7 | Three Outside Down | Custom text | Yellow on dark red |
| 8 | Equal High | Custom text | Yellow on dark red |
| 9 | Equal Low | Custom text | Yellow on dark green |
| 10 | Trampoline | Custom text | Black on pale blue |
| 11 | KAMA | Line | Gold |
| 17 | Bar Color | Color bar | Per-bar `DataColor` |
| 33 | Engulfing Green BB | Background | Dark green |
| 34 | Engulfing Red BB | Background | Dark magenta |
| 35 | Shaved Green Candle | Color bar | Pale blue — **never written** |
| 36 | Shaved Red Candle | Color bar | Pink |
| 37 | Doji City | Background | Dark blue — **never written** |

`LineWidth` on the five text subgraphs (6–10) is the **font size**, not a stroke width, because `DrawText` reads it as such.

### Hidden work subgraphs

| # | Name | Purpose |
|---|---|---|
| 12, 13 | Waddah Positive / Negative | Declared, unused |
| 14, 15 | SMA Slow / Fast | EMA 40 and EMA 20 for Waddah |
| 16 | Bollinger Bands | `Arrays[0]` upper, `Arrays[1]` lower |
| 18, 19 | Bar Color Up / Down | Colour holders read into `UpColor`/`DownColor` — **both unused** |
| 20 | Linda MACD | `Arrays[3]` is the histogram |
| 21 | Parabolic | SAR |
| 22 | Awesome Oscillator | |
| 23 | Fisher | Named "Awesome Oscillator" by copy-paste; never written |
| 24 | ADX | |
| 25 | T3 | |
| 26 | Hull Moving Average | |
| 27 | Calc | Named "RSI"; also `Arrays[0]` holds the Fisher intermediate value |
| 28 | Squeeze Relaxer 1 | Momentum histogram (linear regression output) |
| 29 | Squeeze Relaxer 2 | EMA 20, later overwritten with a LinReg MA |
| 30 | Squeeze Relaxer 3 | Raw momentum input to the regression |
| 31 | SuperTrend | `Arrays[0..5]`: TR, ATR, basicUB, basicLB, UB, LB |
| 32 | Vol Imb Intersections | **Never written** — the writing code is commented out |
| 38 | Hull ATR | Hull MA of true range |
| 41 | VolImb Direction | `+1` / `−1` / `0` |
| 42 | VolImb Price | Gap price |
| 43 | VolImb Origin Candle | Origin bar index |

Subgraphs 39 and 40 are unallocated — the volume-imbalance tracking group jumps to 41–43.

---

## Persistent state

| Key | Variable | Purpose |
|---|---|---|
| 0 | `r_SqueezeUp` | Last squeeze star direction; prevents repeated stars in the same swing |
| 1 | `PreviousLineCount` | Declared, never meaningfully read or written |
| 2 | `IsInitialized` | Declared, never used |

`r_SqueezeUp` is not reset on full recalculation, so the first squeeze star after a chart reload depends on state left over from the previous pass.

---

## Known issues, bugs and quirks

These are real behavioural issues in the code as written, listed roughly by impact.

**1. Fisher transform never smooths.** `Subgraph_Fisher[i]` is never assigned. The line

```cpp
float fish = .5f * (log((1 + TruncValue) / (1 - TruncValue)) + Subgraph_Fisher[i - 1]);
```

always reads zero for the previous value, so the standard Fisher recursion collapses to `0.5 × ln((1+v)/(1−v))`. Since Fisher is on by default and vetoes signals on its sign, this directly changes buy/sell output. The fix is `Subgraph_Fisher[i] = fish;` before the next bar.

**2. Integer wick variables.** In the local-variables block:

```cpp
auto upperwick{0};
auto lowerwick{0};
```

`auto` deduced from an int literal makes these `int`, so `upperwick = abs(high - close)` truncates a fractional price distance to a whole number. On ES (0.25 tick) a 0.75-point wick becomes `0`. This feeds `doji = upperwick > body && lowerwick > body`, which feeds the doji early return that skips the whole bar. Declaring them `double` would restore the intent.

**3. Supertrend lower-band index mismatch.**

```cpp
Array_LowerBand[i] = Array_LowerBandBasic[i - 1];   // should be [i]
```

The upper band uses `Array_UpperBandBasic[i]`. The lower band lags one bar, making the Supertrend asymmetric between uptrends and downtrends.

**4. No `i == 0` guard.** The Supertrend block reads `Array_UpperBand[i-1]` and `sc.Close[i-1]` on bar 0. The `if (i == 0)` line only seeds `Subgraph_SuperTrend[i]` and does not skip the surrounding reads. Likewise `pclose`/`popen`/`phigh`/`plow` and the pattern helpers index `i-1`, `i-2`, `i-3` with no lower bound check. Sierra's array accessors tolerate out-of-range indices by returning zero rather than faulting, so this shows up as meaningless values on the first few bars rather than a crash.

**5. `IsBodyStrong` integer accumulator.**

```cpp
auto mov_aver{0};                                   // int
mov_aver += static_cast<float>(BodyLength(...));    // truncated each iteration
mov_aver /= k_Body_NUM_OF_CANDLES;                  // integer division
```

Each body length truncates to an integer before summing, then the average uses integer division. The function is currently unused, so this is latent rather than active.

**6. `IsTrampoline` band parameter.** The function contains both the bullish and bearish variants and tests both against whatever single band value it receives. The caller invokes it twice — once with `UpperBand`, once with `LowerBand` — inside an `if / else if`. So a bullish trampoline can be detected while checking against the *upper* band, and then be labelled with `iAboveCandle = -1`, the placement intended for the bearish case. Both branches also emit an identical `TR` label, so the two cases are visually indistinguishable on the chart.

**7. `iTickSize` truncation.** `IsTrampoline` declares `int iTickSize` but is called with `sc.TickSize`, a float. For any instrument with a tick size below 1.0 — which is most of them — this truncates to `0`, removing the intended one-tick tolerance on the band touch.

**8. Duplicate ray creation.** `sc.AddLineUntilFutureIntersection` is called on every pass over a qualifying bar, with no deduplication. On repeated recalculations this can stack multiple identical rays at the same price, which in turn inflates `GetNumLinesUntilFutureIntersection` and slows the fill-detection loop.

**9. Fill-scan off-by-one.**

```cpp
int NumLines = sc.GetNumLinesUntilFutureIntersection(...);
for (NumLines; NumLines >= 0; NumLines--)
    sc.GetStudyLineUntilFutureIntersectionByIndex(..., NumLines, ...);
```

Valid indices are `0 .. NumLines-1`, so the first iteration queries one past the end. The function's return value is not checked, so the resulting uninitialised `StartIndex` and `LineValue` are used in the comparison on that first pass.

**10. Duplicate alerts on the same bar.** Buy and sell alerts fire whenever `bIsCurrentBar` is true, with no once-per-bar guard. Because `sc.Index == sc.ArraySize - 2` is re-evaluated whenever new data arrives, the same signal can alert repeatedly. Storing the last alerted bar index in a `sc.GetPersistentInt` slot and checking it before `AlertWithMessage` is the usual ACSIL fix.

**11. Dead `FILL` label.** The block

```cpp
if (Subgraph_Intersection[i] > Subgraph_Intersection[i-1] && ...)
    DrawText(sc, Subgraph_3oU, "FILL", 0, 5);
```

depends on `Subgraph_Intersection`, which is only written inside the large commented-out `if (false)` block above it. The condition is therefore always false and the `FILL` label never appears. Fill events are still caught by the ray-scan path, which alerts but does not label.

**12. Redundant EMA computation.** The squeeze block calls `sc.ExponentialMovAvg(sc.Close, Subgraph_MomentumHistUpColors, 20)` and then immediately `sc.MovingAverage(sc.Close, Subgraph_MomentumHistUpColors, MOVAVGTYPE_EXPONENTIAL, 20)` on the same output array — the second overwrites the first with an identical result. After the histogram is computed, the same array is overwritten a third time with a linear-regression MA that is never read.

**13. Down-offset unused for imbalance arrows.** `Subgraph_VolImbDown[i] = high + (Input_UpOffset...)` uses the *up* offset. Cosmetic, but inconsistent with the triangles.

**14. Unused declarations.** `Input_ShavedBuffer`, `Input_DojiCity`, `Subgraph_DojiCity`, `Subgraph_ShavedGreen`, `Subgraph_WaddahPos`, `Subgraph_WaddahNeg`, `Subgraph_ColorUp`/`ColorDown` (read into `UpColor`/`DownColor`, then never used), `pdoji`, `sqRelaxUp`, `cl`, `in`, `e1`, `AlreadyAlertedBar`, `dataLoaded`, `NotYetInitialized`, `CurrentLineCount`, and the `sc.GetStudyArray(11, 3, ...)` call are all vestigial. `Subgraph_Fisher.Name` is set to `"Awesome Oscillator"`.

**15. `pdoji` formula.** `pdoji` compares the previous bar's wicks against `pbody` in the first term and `body` — the *current* bar's body — in the second. It is unused, so harmless as written.

---

## Reading the chart

A quick field guide for someone looking at this for the first time:

- **Green/red triangles** are the main signal: every enabled trend filter agrees and ADX is above the floor. Frequent by design in trending conditions, absent in chop.
- **Yellow stars** mark the first turn of the squeeze-momentum histogram in each direction. They lead the triangles and are noisier.
- **White arrows with a white ray** mark a volume imbalance — a gap between one bar's close and the next bar's open in the same direction. The ray extends until price trades back through it. The alert when that happens is the "fill", filtered to gaps 3 to 29 bars old.
- **Background shading** means a bar broke outside a Bollinger band, reversed, and did so with an expanding body. Green at the lower band, magenta at the upper.
- **Coloured bars** (if enabled) show momentum intensity, not signals — brighter means stronger.
- **`3oU` / `3oD`** are three-outside reversals, **`Eq Hi` / `Eq Lo`** are tweezers at a Bollinger extreme, and **`TR`** is a trampoline (RSI extreme plus a two-bar move away from a band touch).
- **The gold line** is KAMA. On NQ specifically, a wick through it that closes back on the original side fires the KAMA bounce alert.

Because of the `ArraySize − 2` guard, imbalance arrows and their state only appear for bars the study observed live. Historical imbalance rays will be present after a reload; their arrows will not.
