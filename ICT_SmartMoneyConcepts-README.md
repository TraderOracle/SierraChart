# ICT Smart Money Concepts — Sierra Chart Study

A single-file ACSIL study that maps market structure, institutional supply/demand arrays, price imbalances and liquidity pools directly on a Sierra Chart price graph, with an individually configurable alert for every major event.

**DLL name:** `ICT_SmartMoneyConcepts` · **Study name:** `ICT Smart Money Concepts`

---

## Contents

- [Features](#features)
- [Installation](#installation)
- [Quick start](#quick-start)
- [How it works](#how-it-works)
- [Inputs](#inputs)
- [Alerts](#alerts)
- [Colors](#colors)
- [Performance notes](#performance-notes)
- [Troubleshooting](#troubleshooting)
- [Disclaimer](#disclaimer)

---

## Features

### 1. Market structure mapping

| Element | Behavior |
|---|---|
| **Break of Structure (BOS)** | Trend continuation — price takes out a prior swing point in the direction of the existing trend. |
| **Change of Character (CHoCH / MSS)** | Early reversal — price takes out the *opposite* swing point, flipping the tracked trend state. |
| **External structure** | Major swing points from a long pivot length, drawn as solid lines with `BOS` / `CHoCH` labels. |
| **Internal structure** | Sub-structure from a short pivot length, drawn as dotted lines with `iBOS` / `iCHoCH` labels. |
| **Strong / weak highs and lows** | The swing that produced the break is labeled strong; the swing that failed to hold is labeled weak. Labels follow the trend. |

External and internal structure are tracked as two fully independent pivot + trend state machines, so internal shifts can be read against the higher-level bias rather than being conflated with it.

### 2. Institutional supply & demand arrays (PD arrays)

| Element | Definition used |
|---|---|
| **Order Blocks (OB)** | Built at each structure break. Either the last opposite-direction candle before the impulse, or the extreme candle of the leg — selectable. |
| **Breaker Blocks** | A violated order block is not deleted; it flips polarity and continues as a breaker (support becomes resistance and vice versa). |
| **Mitigation Blocks** | Failure swings — a higher low before a bullish break, or a lower high before a bearish break — anchored to the last opposite candle at the failed swing. |
| **Rejection Blocks** | Wick-dominant candles, filtered by both wick-to-range ratio and absolute wick size in ATR. The zone spans the wick. |

Zone bounds are configurable as full candle range, body only, or wick only. Invalidation can be evaluated on candle close or on wick.

### 3. Price imbalances & inefficiencies

| Element | Definition used |
|---|---|
| **Fair Value Gaps** | Classic three-candle imbalance — the gap between candle 1's extreme and candle 3's opposite extreme, filtered by minimum ATR size. |
| **Opening / Volume Gaps** | Unfilled space between a candle's close and the next candle's open, filtered by minimum ATR size. |
| **Liquidity Voids** | Runs of consecutive same-direction candles that meet both a minimum bar count and a minimum ATR displacement, boxed across the whole leg. |

Each imbalance tracks its own fill state; fills are detected on wick touch or candle close.

### 4. Liquidity pools & sweeps

| Element | Behavior |
|---|---|
| **Equal Highs / Equal Lows** | Consecutive pivots clustered within an ATR-scaled tolerance and a maximum bar distance. Drawn as dashed levels labeled `EQH` / `EQL`. |
| **Liquidity sweeps / stop hunts** | A candle pierces a swing point or an EQH/EQL pool but closes back inside it. Marked with a triangle and the level switches to a dotted `x` style. |
| **Previous D / W / M high & low** | Prior period extremes carried forward as levels, with raid alerts when they are traded through. |

### 5. Premium / discount pricing

Premium and discount boxes split at equilibrium (0.5) across the active dealing range, plus an optional OTE band at the 0.62–0.79 retracement, oriented automatically to the direction of the leg.

---

## Installation

**Requirements:** Sierra Chart on Windows with the bundled build tools installed (Sierra Chart's remote build service also works).

1. Copy `ICT_SmartMoneyConcepts.cpp` into your Sierra Chart `ACS_Source\` folder.
2. In Sierra Chart: **Analysis → Build Custom Studies DLL**, select the file, and build.
3. On a chart: **Analysis → Studies → Add Custom Study → ICT Smart Money Concepts**.

To rebuild after editing, use **Analysis → Build Custom Studies DLL** again — Sierra Chart releases and reloads the DLL automatically.

---

## Quick start

The defaults are tuned for intraday futures charts. Two settings are worth checking first:

- **Draw underneath the bars.** Zones render in front of price by default. In the study settings, enable **Draw Study Underneath Main Price Graph** for a cleaner chart.
- **Pivot lengths drive everything.** `Swing (External) Pivot Length` (default 25) sets what counts as major structure; `Internal Pivot Length` (default 5) sets sub-structure. On higher timeframes, lower the external length; on very fast charts, raise it.

If the chart feels crowded, reduce **Max Zones Per Type** and turn off the imbalance families you don't trade.

---

## How it works

**No repainting.** All structure, PD array and liquidity logic is evaluated on *closed* bars only. State advances one bar at a time and is never recomputed from the forming bar, so a drawing that appeared on a historical bar will still be there tomorrow.

**Incremental state.** The study runs with `sc.AutoLoop = 0` and keeps its zone list, liquidity list and pivot state in a persistent structure across calls. Each new bar is processed exactly once. A full recalculation clears all drawings and rebuilds from scratch.

**ATR-scaled filters.** Every size threshold (gap size, wick size, equal-high tolerance, sweep penetration, void displacement) is expressed as a multiple of ATR rather than in ticks, so a single set of inputs behaves consistently across instruments and timeframes.

**Alert gating.** Alerts never fire while historical bars are being processed — the study checks `sc.IsFullRecalculation` and the distance from the end of the chart before triggering. Loading a year of history won't produce a flood of alerts.

---

## Inputs

### Structure

| Input | Default |
|---|---|
| Swing (External) Pivot Length | 25 |
| Internal Pivot Length | 5 |
| Break Confirmation | Candle Close / Wick |
| Show External BOS / CHoCH | Yes |
| Show Internal BOS / CHoCH | Yes |
| Show Strong / Weak Highs & Lows | Yes |
| Max Structure Labels Kept | 30 |

### PD arrays

| Input | Default |
|---|---|
| Show Order Blocks | Yes |
| Order Block Candle | Last Opposite Candle / Extreme Candle |
| Zone Definition | Full Candle Range / Body Only / Wick Only |
| Zone Invalidation | Candle Close / Wick |
| Show Breaker Blocks | Yes |
| Show Mitigation Blocks | Yes |
| Show Rejection Blocks | Yes |
| Rejection Wick Ratio (%) | 60 |
| Rejection Min Wick (ATR x) | 0.75 |
| Also Build Order Blocks From Internal Structure | No |

### Imbalances

| Input | Default |
|---|---|
| Show Fair Value Gaps | Yes |
| FVG Min Size (ATR x) | 0.10 |
| Show Opening / Volume Gaps | Yes |
| Opening Gap Min Size (ATR x) | 0.05 |
| Show Liquidity Voids | Yes |
| Void Min Consecutive Bars | 3 |
| Void Min Leg Size (ATR x) | 2.0 |
| Fill / Invalidate Method | Wick Touch / Candle Close |

### Liquidity

| Input | Default |
|---|---|
| Show Equal Highs / Lows | Yes |
| EQH / EQL Pivot Length | 3 |
| EQH / EQL Tolerance (ATR x) | 0.10 |
| EQH / EQL Max Bars Between | 100 |
| Show Sweeps / Stop Hunts | Yes |
| Sweep Min Penetration (ATR x) | 0.0 |
| Max EQH / EQL Levels Kept | 20 |

### Levels

| Input | Default |
|---|---|
| Show Premium / Discount / Equilibrium | Yes |
| Show OTE Band (0.62 – 0.79) | Yes |
| Show Previous Day High / Low | Yes |
| Show Previous Week High / Low | Yes |
| Show Previous Month High / Low | No |

### Display

| Input | Default |
|---|---|
| ATR Length (sizing filters) | 200 |
| Zone Right Extension (bars) | 12 |
| Zone Transparency (0–100) | 78 |
| Max Zones Per Type | 8 |
| Label Font Size | 8 |
| Show Text Labels | Yes |
| Delete Invalidated Zones | Yes |

Setting **Delete Invalidated Zones** to No keeps invalidated zones on the chart frozen at their invalidation bar, drawn with a dotted border — useful for reviewing how price treated a level after the fact.

---

## Alerts

Three global controls plus eighteen individual event toggles:

- **Master Enable** — turns the whole alert system on or off.
- **Sound Number** — maps to Sierra Chart's alert sounds. Set to `0` to log alerts without playing a sound.
- **Allow Intrabar (Unconfirmed) Alerts** — off by default. When enabled, structure breaks and sweeps also alert on the forming bar, before the close confirms them. Faster, but subject to reversal within the bar.

| Event | Default |
|---|---|
| External BOS | On |
| External CHoCH / MSS | On |
| Internal BOS | Off |
| Internal CHoCH | On |
| New Order Block | On |
| Zone Tap / Mitigation Entry | On |
| Zone Invalidated | Off |
| Breaker Block Formed | On |
| Mitigation Block Formed | On |
| Rejection Block Formed | Off |
| New Fair Value Gap | Off |
| Fair Value Gap Filled | Off |
| New Opening / Volume Gap | Off |
| New Liquidity Void | Off |
| Equal Highs / Lows Formed | On |
| Liquidity Sweep / Stop Hunt | On |
| Premium / Discount Entry | Off |
| Previous D/W/M Level Taken | On |

Alert messages carry the symbol, the event and the relevant price, formatted to the chart's value format — for example:

```
ICT SMC [ESZ5]: BULLISH CHoCH / MSS - close above swing high 5842.25
ICT SMC [ESZ5]: Buy-side liquidity SWEEP above swing high 5851.00 (stop hunt)
ICT SMC [ESZ5]: Price tapped Bull OB zone 5820.50 - 5824.75
```

Alerts appear in the Alerts Log (**Window → Alerts Log**) and can be routed to sound, email or SMS through Sierra Chart's alert settings.

---

## Colors

Every visual element takes its color from a named entry on the study's **Subgraphs** tab — bullish and bearish structure, each zone family, EQH/EQL, sweeps, premium/discount/equilibrium/OTE, the D/W/M levels, and strong/weak labels. Restyling the study to match a chartbook theme requires no code changes.

Sweep markers are real subgraphs (`Sweep - Sell Side Taken` / `Sweep - Buy Side Taken`), so their values can be referenced by other studies, spreadsheet systems or alert conditions.

---

## Performance notes

Zone and level counts are capped per family (`Max Zones Per Type`, `Max EQH / EQL Levels Kept`, `Max Structure Labels Kept`); the oldest drawing is deleted when a cap is exceeded, so a long chart doesn't accumulate thousands of chart drawings.

Drawing refresh — extending active zones to the right edge — runs only when the bar count changes or when state actually changed, not on every incoming tick.

---

## Troubleshooting

**Build fails with `invalid conversion from 'int' to 'SubgraphLineStyles'`**
Sierra Chart's bundled gcc compiles without `-fpermissive`, so typed enum members (`LineStyle`, `DrawingType`, `AddMethod`, `DrawStyle`) can't accept a plain `int`. Any helper function that forwards one of these needs the enum type in its parameter list, not `int`. The shipped source already does this.

**Zones cover the price bars**
Enable **Draw Study Underneath Main Price Graph** in the study settings.

**No alerts on historical bars**
By design. Alerts are suppressed during full recalculation so that loading history doesn't trigger them. Live and newly closed bars alert normally.

**Too many / too few order blocks**
Order blocks are generated at structure breaks, so their frequency is governed by the pivot lengths. Raise the external pivot length for fewer, more significant blocks. Enabling internal-structure order blocks will substantially increase the count.

**Drawings look stale after changing inputs**
Changing an input triggers a full recalculation, which clears and rebuilds all drawings. If something looks wrong, **Chart → Recalculate** forces a clean rebuild.

---

## Disclaimer

This study is an analysis and charting tool. It does not generate trade recommendations, and nothing here is financial advice. Smart Money Concepts are a discretionary framework — the same price action can be labeled several defensible ways, and the definitions implemented here are one reasonable interpretation among many. Test on your own instruments and timeframes before relying on it, and understand that past chart behavior does not predict future results.

## License

Released under the MIT License. See [`LICENSE`](LICENSE).
