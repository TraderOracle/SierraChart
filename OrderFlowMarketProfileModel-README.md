# Order Flow + Market Profile Model

A Sierra Chart (ACSIL) study that implements a three-step discretionary trading process as a chart indicator: **Location → Refinement → Trigger**.

It is not a strategy that fires on a fixed pattern. It builds a volume profile of a reference balance area, measures whether current activity is supported by aggressive order flow, and only prints a signal when a level has been broken *and* retested with confirming aggression.

The study draws levels, arrows, and labels, and fires alerts. **It places no orders.**

> Nothing here is financial advice. Test on simulated data before risking money.

https://www.youtube.com/watch?v=tvERE-Beu2U&t=87s
---

## Table of contents

- [The model](#the-model)
- [Requirements](#requirements)
- [Installation](#installation)
- [Reading the chart](#reading-the-chart)
- [How a signal is produced](#how-a-signal-is-produced)
- [Input reference](#input-reference)
- [Diagnostic subgraphs](#diagnostic-subgraphs)
- [Tuning guide](#tuning-guide)
- [How the profile is calculated](#how-the-profile-is-calculated)
- [Alerts](#alerts)
- [Troubleshooting](#troubleshooting)
- [Limitations and scope](#limitations-and-scope)

---

## The model

**1. Location — where is the market out of balance?**
A volume profile is built over a reference period (prior session by default). From it the study extracts the POC, the value area, high volume nodes (HVN) and low volume nodes (LVN). HVNs are areas of acceptance and act as magnets and targets. LVNs are areas of rejection where price previously moved fast, and they act as reaction levels.

**2. Refinement — is the move supported?**
Each bar is measured with volume spread analysis: cumulative volume delta and its slope, the delta/volume ratio (how one-sided the bar is), volume against its own average, and average trade size as a proxy for who is trading. In real time it can also count individual large prints from Time & Sales.

**3. Trigger — no anticipation.**
A signal is never produced on an approach to a level. Price must close through the level with aggression, come back to retest it, and hold with aggression again while cumulative delta agrees. Targets default to the nearest HVN or POC in the direction of the trade — the market seeking balance.

---

## Requirements

| | |
|---|---|
| Platform | Sierra Chart with the Advanced Custom Study Interface (any recent version) |
| Chart type | Intraday |
| Data | Bid/ask volume required for accurate delta |
| Storage unit | `1 tick` strongly recommended (Chart Settings → Intraday Data Storage Time Unit) |
| Compiler | Sierra Chart's built-in remote or local build |

If your intraday storage unit is not 1 tick, both the volume profile and the delta calculations degrade. If a data feed supplies no bid/ask volume at all, the study falls back to signing each bar's total volume by its close vs. open, which is a crude approximation — treat signals with suspicion in that case.

---

## Installation

1. Copy `OrderFlowMarketProfileModel.cpp` into your Sierra Chart `ACS_Source` folder (usually `C:\SierraChart\ACS_Source`).
2. In Sierra Chart: **Analysis → Build Custom Studies DLL → Build → Select `OrderFlowMarketProfileModel.cpp`**.
3. On a chart: **Analysis → Studies → Add Custom Study → "Order Flow + Market Profile Model"**.

The study overlays the main price graph (`GraphRegion = 0`). Nothing needs to be moved to a separate region unless you choose to plot the diagnostic subgraphs.

---

## Reading the chart

### Lines

| Line | Appearance | Meaning | How to use it |
|---|---|---|---|
| **Balance POC** | Yellow solid, 2px | Point of control of the reference profile — the single most traded price | Primary target. Price leaving balance tends to return here |
| **VAH / VAL** | Grey dashed | Value area high/low (70% of volume by default) | Edges of accepted value. Acceptance outside is the "out of balance" condition |
| **HVN 1–3** | Blue solid, thin | High volume nodes — local peaks in the profile | Magnets and secondary targets; expect price to slow and rotate here |
| **LVN 1–3** | Pink dotted | Low volume nodes — local valleys in the profile | Reaction levels. Price either rejects them cleanly or moves through them fast |

Levels are horizontal by nature but are plotted as subgraph values per bar, so they extend across the session and **break at the session boundary** (the first bar of a new session is intentionally left blank so the old and new levels don't join with a diagonal). When new levels are computed, any setup waiting for a retest is discarded — the map changed, so the pending trade is stale.

In *Rolling N Bars* mode the levels recompute every bar and will therefore drift and step rather than sit flat. That is expected; use *Prior Session* mode if you want static reference levels.

### Symbols

| Symbol | Appearance | Meaning |
|---|---|---|
| **Long trigger** | Green up arrow below the bar low | Breakout above a level was retested and held with buy aggression |
| **Short trigger** | Red down arrow above the bar high | Breakdown below a level was retested and rejected with sell aggression |
| **Text label** | `L 5432.25 \| S 5429.75 \| T 5441.00` | Entry / stop / target for that signal (`L` = long, `S` = short in the first field) |

The label reads *entry price | stop price | target price*. Entry is the close of the trigger bar, stop sits a configurable distance beyond the broken level, and target is the nearest HVN or POC far enough away in the trade's direction.

### What the chart is telling you, in order

1. **Where are we relative to the profile?** Inside value = balance, likely rotation between VAH and VAL. Outside value = imbalance, and the POC becomes the obvious target if price fails to accept the new area.
2. **Which level is in play?** The nearest LVN above or below is where a reaction is likely. The nearest HVN is where a move is likely to stall.
3. **Is the current move real?** Check the delta ratio and CVD slope (plot them if you want them visible). A push through an LVN on thin, two-sided volume is not the same event as a push through it on heavy one-sided volume.
4. **Wait for the arrow.** The arrow is the point at which the study considers the narrative confirmed rather than anticipated.

---

## How a signal is produced

### Aggression (evaluated on every bar)

```
Delta          = AskVolume − BidVolume
DeltaRatio     = |Delta| / Volume
AvgTradeSize   = Volume / NumberOfTrades
CVDSlope       = CVD[i] − CVD[i − CVDLookback]

VolumeOK       = Volume >= AverageVolume × VolumeMultiple
RatioOK        = DeltaRatio >= MinDeltaRatio
TradeSizeOK    = MinAvgTradeSize is 0
                 OR AvgTradeSize >= MinAvgTradeSize
                 OR a large print occurred on this bar

BuyAggression  = Delta > 0 AND VolumeOK AND RatioOK AND TradeSizeOK
SellAggression = Delta < 0 AND VolumeOK AND RatioOK AND TradeSizeOK
```

### State machine (long side; short is the mirror)

```
IDLE
 └─ Close > Level + BreakoutTicks
    AND previous close was at or below that threshold
    AND BuyAggression
        → ARMED   (remembers the level and the bar index)

ARMED
 ├─ Close < Level − InvalidationTicks          → IDLE   (breakout failed)
 ├─ bars since breakout > MaxRetestBars        → IDLE   (retest never came)
 └─ Low <= Level + RetestTolerance
    AND Close > Level
    AND BuyAggression
    AND CVDSlope >= 0
        → TRIGGER: arrow, label, alert         → IDLE
```

Only one setup per side is tracked at a time. When several levels are broken on the same bar, the long side takes the highest level broken and the short side takes the lowest, so the setup is anchored to the most significant break.

Entry, stop, and target on trigger:

```
Entry  = close of the trigger bar
Stop   = Level − StopTicks           (long)   |  Level + StopTicks   (short)
Target = nearest level > Entry + MinTargetTicks (long)
         nearest level < Entry − MinTargetTicks (short)
         fallback: 2 × risk if no level qualifies
```

Candidate levels for both breakouts and targets are POC, VAH, VAL, all HVNs, all LVNs, and optionally the profile high and low.

---

## Input reference

### 1. Location

| Input | Default | Notes |
|---|---|---|
| Profile Source | Prior Session | `Prior Session`, `Prior N Sessions`, or `Rolling N Bars` |
| Number of Prior Sessions | 1 | Used in *Prior N Sessions* mode; composite profile across N days |
| Rolling Lookback (bars) | 240 | Used in *Rolling N Bars* mode |
| Value Area Percent | 70 | Standard market profile convention |
| HVN Threshold (% of max row volume) | 70 | A local peak must reach this share of the profile's busiest row |
| LVN Threshold (% of max row volume) | 30 | A local valley must be at or below this share |
| Node Detection Window (ticks) | 4 | A row must be the extreme within ±N ticks to qualify as a node |
| Profile Smoothing (ticks each side) | 2 | Averages the profile before node detection; raises it to reduce noise |
| Minimum Node Separation (ticks) | 8 | Prevents clusters of near-identical levels |
| Max Nodes Per Type | 3 | Caps HVN and LVN counts (1–3) |
| Include Profile High/Low as Levels | Yes | Adds the extremes of the reference profile to the level set |

### 2. Refinement

| Input | Default | Notes |
|---|---|---|
| Reset CVD Each Session | Yes | Session-anchored cumulative delta |
| CVD Slope Lookback (bars) | 5 | Window used for the "is delta agreeing" check |
| Min \|Delta\| / Volume Ratio | 0.15 | How one-sided a bar must be to count as aggressive |
| Min Bar Volume vs Average | 1.2 | Bar volume must exceed this multiple of its moving average |
| Average Volume Length (bars) | 20 | Length of that moving average |
| Min Average Trade Size (0 = off) | 0 | Historical proxy for large participants; instrument-specific |
| Track Large Prints via Time & Sales | Yes | Real-time only; counts prints at or above the size threshold |
| Large Print Size (contracts) | 25 | What counts as a large print for your instrument |
| Require Large Print on Trigger Bar | No | Strictest filter. Leave off when studying history |

### 3. Trigger

| Input | Default | Notes |
|---|---|---|
| Breakout Confirmation Beyond Level (ticks) | 2 | How far past the level the close must be to arm a setup |
| Retest Tolerance (ticks) | 2 | How close the retest wick must come to the level |
| Max Bars to Wait for Retest | 12 | After this the armed setup expires |
| Invalidation Beyond Level (ticks) | 4 | A close this far back through the level cancels the setup |
| Stop Offset Beyond Level (ticks) | 6 | Distance from the level to the stop |
| Min Distance to Target (ticks) | 8 | Prevents selecting a target that is effectively at the entry |
| Evaluate on Bar Close Only | Yes | Off = faster signals that can repaint intrabar |

### Display

| Input | Default |
|---|---|
| Draw Entry/Stop/Target Labels | Yes |
| Signal Arrow Offset (ticks) | 4 |
| Enable Alerts | Yes |

---

## Diagnostic subgraphs

Subgraphs 12–23 are set to `DRAWSTYLE_IGNORE`, so they are calculated and stored but not drawn. Change the draw style in Study Settings to chart them (move the study to a separate region first — the scales are incompatible with price), or reference them from a Spreadsheet study, an alert condition, or another custom study via `sc.GetStudyArrayUsingID`.

| SG # | Name | Contents |
|---|---|---|
| 1 | Balance POC | Plotted |
| 2–3 | VAH / VAL | Plotted |
| 4–6 | HVN 1–3 | Plotted, 0 when unused |
| 7–9 | LVN 1–3 | Plotted, 0 when unused |
| 10–11 | Long / Short Trigger | Arrow price, 0 on non-signal bars |
| 12 | CVD (session) | Session cumulative volume delta |
| 13 | Bar Delta | Ask volume − bid volume |
| 14 | Delta/Volume Ratio | 0 to 1 |
| 15 | Avg Trade Size | Volume ÷ number of trades |
| 16 | Avg Volume | Moving average used by the volume filter |
| 17–18 | Large Buy / Sell Prints | Real-time print counts on the current bar |
| 19–20 | Long / Short State | 0 = idle, 1 = armed and awaiting retest |
| 21–23 | Entry / Stop / Target | Written on signal bars only |

Subgraph numbers above are the 1-based numbering Sierra Chart shows in study selectors; internally they are indices 0–22.

---

## Tuning guide

**Start with location.** Set `Profile Source` and adjust `HVN Threshold`, `LVN Threshold`, `Node Detection Window`, and `Profile Smoothing` until the drawn levels match where you would mark the profile by hand. If you get too many nodes, raise smoothing and minimum separation. If you get none, lower the HVN threshold and raise the LVN threshold.

**Then tune aggression.** Plot the delta ratio and average trade size for your instrument for a few sessions and look at their typical range. `Min |Delta| / Volume Ratio` around 0.15 is reasonable on liquid index futures; illiquid instruments print higher ratios naturally, so raise it. `Large Print Size` should be set from what actually constitutes a block in your market, not copied from another instrument.

**Then loosen or tighten the trigger.** `Max Bars to Wait for Retest` is the input that most changes signal frequency. Short bar intervals need a larger value; a 12-bar window on a 30-second chart is six minutes, on a 5-minute chart it is an hour.

Rough starting points by instrument class:

| | Delta ratio | Volume multiple | Large print | Node window |
|---|---|---|---|---|
| Index futures (ES, NQ) | 0.12–0.18 | 1.2 | 25–50 | 4–8 ticks |
| Energy (CL) | 0.15–0.25 | 1.3 | 10–20 | 3–6 ticks |
| Treasuries (ZN, ZB) | 0.10–0.15 | 1.2 | 100+ | 2–4 ticks |
| Equities | 0.15–0.25 | 1.5 | varies widely | scale to price |

---

## How the profile is calculated

The study aggregates Sierra Chart's per-bar volume-at-price container (`sc.VolumeAtPriceForBars`) across the reference bar range into a price-keyed map, then converts it into a contiguous array of rows, one per tick.

- **POC** — the row with the highest raw volume.
- **Value area** — expands from the POC one row at a time, always taking the heavier of the two adjacent rows, until the accumulated volume reaches the configured percentage of total profile volume.
- **Node detection** — runs on a smoothed copy of the profile. A row is an HVN if it is the maximum within ±*window* ticks and reaches the HVN threshold relative to the busiest row; an LVN if it is the minimum within that window and falls at or below the LVN threshold. Candidates are then ranked (strongest HVNs, thinnest LVNs) and filtered by minimum separation.

Recalculation timing:

- *Prior Session* and *Prior N Sessions*: recomputed once, on the first bar of each new trading day, using `sc.GetTradingDayDate` to detect the boundary.
- *Rolling N Bars*: recomputed once per new bar over the preceding N closed bars.

`sc.MaintainVolumeAtPriceData` is enabled by the study, so no additional configuration is needed to make the VAP data available.

---

## Alerts

Alerts fire with `sc.AlertOnlyOncePerBar` enabled and are suppressed during full recalculations, so reloading a chart will not spray old alerts at you.

- Alert 1: long trigger, naming the level that was retested
- Alert 2: short trigger, naming the level that was rejected

Configure sound and behaviour in **Study Settings → Alerts** as with any Sierra Chart study.

---

## Troubleshooting

**Build fails with `expected unqualified-id before '(' token` on a `std::max` line.**
`sierrachart.h` defines `min` and `max` as function-like macros, which breaks any namespace-qualified call. This source uses local `MaxInt` / `MaxDouble` / `AbsInt` helpers instead; if you extend the file, do the same, or wrap the call as `(std::max)(a, b)`.

**No levels are drawn.**
The profile needs a completed reference period. In *Prior Session* mode nothing appears until the chart contains at least one full prior session. Also confirm the chart has volume-at-price data — set the intraday storage unit to 1 tick and redownload.

**Levels are drawn but no arrows ever appear.**
Usually the aggression filters. Set `Min Bar Volume vs Average` to 1.0 and `Min |Delta| / Volume Ratio` to 0.05 temporarily; if signals appear, raise them back gradually. Also check `Require Large Print on Trigger Bar` is off for historical bars, since Time & Sales data is only available live.

**Bar Delta is always ±total volume.**
That is the no-bid/ask-volume fallback path. Your data source or storage unit is not providing bid/ask volume.

**Signals appear and then disappear intrabar.**
Set `Evaluate on Bar Close Only` to Yes.

**Levels form diagonal lines across the session boundary.**
Confirm `DrawZeros` is off for the level subgraphs (it is by default). The study writes a zero on the first bar of a session specifically to break the line.

---

## Limitations and scope

- **Indicator only.** No orders are placed and no position is tracked.
- **Risk management is out of scope.** Sizing rules — including the practice of funding more aggressive trades later in the day out of earlier realized profit — depend on account state the study cannot see. Those belong in your trading plan or a trade-management study.
- **Large print detection is real-time only.** Historical bars use average trade size as a substitute, which is a weaker proxy.
- **Nodes are levels, not zones.** The model treats HVN/LVN as prices; in practice they are areas. `Retest Tolerance` is the only allowance for that.
- **One setup per side.** A second breakout while a setup is armed is ignored until the first resolves.

---

## License

Add your preferred license here (MIT is a common choice for study source).
