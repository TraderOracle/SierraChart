# Squeeze Channel — User Manual

Sierra Chart custom study (`SqueezeChannel.cpp`). Ported from the TradingView
"Squeeze Channel" Pine Script with approved enhancements; conversion details
live in `CONVERSION.md`.

## 1. What the Study Does

Squeeze Channel is a volatility-compression breakout system:

1. **Squeeze detection** — when Bollinger Bands contract inside the Keltner
   Channel, volatility is compressed ("the squeeze"). The tighter the
   Bollinger Bands relative to the Keltner Channel, the stronger the squeeze.
2. **Swing channel** — the moment a squeeze starts, the study draws a price
   channel from the recent swing high/low, lets it widen for a few bars, then
   locks it. This channel frames the consolidation.
3. **Breakout signals** — once the channel is locked, a close above the
   channel high is a bullish breakout; a close below the channel low is a
   bearish breakout. The channel is retired after a breakout.
4. **Failed-breakout reversals** — after a breakout, the opposite channel edge
   is watched for a limited number of bars. If price closes back through it,
   the breakout failed and a reversal signal fires (often a strong
   counter-move).
5. **Extended S/R** — after every breakout, both channel edges are remembered
   as support/resistance for a configurable number of bars. They are drawn
   when price comes near them, and can optionally generate their own
   breakout/reclaim signals ("retest signals").

## 2. Installation

1. Copy `SqueezeChannel.cpp` into the `ACS_Source` folder inside your Sierra
   Chart installation folder (e.g. `C:\SierraChart\ACS_Source\`).
2. In Sierra Chart: **Analysis >> Build Custom Studies DLL**, select the file,
   then **Remote Build** (no compiler needed) or **Build** (local Visual
   Studio).
3. Open a chart: **Analysis >> Studies >> Add Custom Study**, expand
   **Squeeze Channel**, add **Squeeze Channel**.

The study draws on the main price region and needs at least
`max(Bollinger Length, Keltner Length)` bars before anything appears.

## 3. Reading the Chart

| Element | Appearance | Meaning |
|---|---|---|
| Channel High / Low | Dotted lines, colored | Active swing channel. Color = squeeze strength gradient while the squeeze holds; switches to the "Squeeze Ended" color (default fuchsia) when the squeeze releases while the channel is still waiting for a breakout. |
| Channel Fill | Translucent band between the channel lines | Same color logic; visual emphasis of the consolidation zone. Opacity comes from "Transparency Level for Fill Styles" on the Subgraphs tab — set ~85 for a subtle TradingView-like fill. |
| Channel Mid | Thin gray dots | Midpoint of the locked channel; appears only once the channel is locked. |
| Squeeze gradient | Yellow → orange → red | Weak → strong squeeze. Red = Bollinger Bands extremely tight vs. Keltner — the most energetic setups. |
| Triangle up (teal, below bar) | Bull Breakout | Close above the locked channel high. |
| Triangle down (red, above bar) | Bear Breakout | Close below the locked channel low. |
| Reversal Level | Dotted line after a breakout | The opposite channel edge being watched for a failed breakout. Colored with the *opposing* signal color (red-ish after a bull breakout). Disappears when the watch window ends or the reversal triggers. |
| Diamond (cyan, below bar) | Bull Reversal | A bear breakout failed: price closed back above the old channel high. |
| Diamond (magenta, above bar) | Bear Reversal | A bull breakout failed: price closed back below the old channel low. |
| Dashed red/teal segments | Extended resistance / support | Remembered channel edges from past breakouts; drawn only while price is within `ATR × proximity` of the level. |
| Dimmed triangles / diamonds | Ext Breakout / Ext Reversal signals | Optional retest signals on the extended levels (off by default). |

## 4. Signal Logic in Detail

**Squeeze on**: `BB lower > KC lower` **and** `BB upper < KC upper`
(Bollinger = SMA ± mult × stdev of closes; Keltner = SMA ± mult × Wilder ATR).

**Squeeze strength**: `1 − min(BBwidth / KCwidth, 1)`, bucketed at
0.2 / 0.4 / 0.6 / 0.8 into the 5 gradient colors.

**Channel lifecycle**:
- On squeeze start: channel seeded from the highest high / lowest low of the
  last *Swing Lookback Bars*.
- For the next *Swing Lookforward Bars*: the channel expands to include any
  new high/low.
- Then it **locks**. From the locking bar onward, closes outside it signal
  breakouts. A breakout retires the channel until the next squeeze.
- If the squeeze releases while the channel is active, the channel stays (in
  the "Squeeze Ended" color) and breakouts remain valid — the squeeze ending
  without a breakout is itself the setup.

**Reversal watch**: a breakout arms a watch on the opposite channel edge for
*Reversal Watch Bars* bars (starting the bar after the breakout). A close
through that edge fires the reversal signal — once per breakout at most.

**Extended S/R**: the bar after a breakout, that channel's high and low are
stored for *Extended Level Bars* bars. With **Enable S/R Retest Signals** on,
after *S/R Signal Delay Bars*: the first close above a stored resistance
fires *Ext Bull Breakout* (level marked "broken"); a later close back below it
fires *Ext Bear Reversal* and removes the level. Supports mirror this.

## 5. Settings Reference

### Squeeze Detection
| Input | Default | Notes |
|---|---|---|
| Bollinger Length / Multiplier | 12 / 2.0 | Shorter = more squeezes, noisier. |
| Keltner Length / ATR Multiplier | 20 / 2.0 | Larger multiplier = stricter squeeze definition (BB must be very tight to fit inside). |
| Use Unbiased (Sample) Std Dev | No | No = TradingView-identical (divide by N). Yes = divide by N−1: slightly wider bands, marginally fewer squeezes. |

### Swing Channel
| Input | Default | Notes |
|---|---|---|
| Swing Lookback Bars | 5 | Bars scanned backward to seed the channel at squeeze start. |
| Swing Lookforward Bars | 3 | Bars the channel may still expand before locking. Larger = wider, later channels. |
| Squeeze Re-Entry Resets Active Channel | Yes | Yes = TradingView behavior: a fresh squeeze while a channel is active re-seeds it. No = keep the existing channel until it resolves via breakout (gradient coloring resumes on it). |

### Reversal Detection
| Input | Default | Notes |
|---|---|---|
| Enable Failed Breakout Reversal | Yes | Master switch for the reversal watch and diamonds. |
| Reversal Watch Bars | 7 | How many bars after a breakout a close through the opposite edge counts as a reversal. |
| Legacy (Pine) Reversal Window Timing | No | No = exactly N bars after the breakout are checked. Yes = TradingView-identical: the breakout bar consumes one watch bar (N=1 then never fires). |

### Extended S/R
| Input | Default | Notes |
|---|---|---|
| Show Extended S/R Levels | Yes | Draw remembered levels near price. |
| Enable S/R Retest Signals | No | Turn on the Ext breakout/reversal signals. |
| Extended Level Bars | 75 | Lifetime of a stored level. |
| S/R Signal Delay Bars | 5 | Retest signals ignored until a level is this old (avoids signalling into the original breakout momentum). |
| ATR Proximity Threshold / ATR Length | 1.5 / 14 | A level is drawn only while `|close − level| ≤ ATR × threshold`. Also sets the ATR used for marker offsets. |
| Plot Nearest Extended Levels | Yes | Yes = the 5 slots per side show the levels *nearest to price*. No = TradingView-identical: the 5 oldest levels, newer ones hidden while more than 5 are alive. |

### Visuals & Alerts
| Input | Default | Notes |
|---|---|---|
| Show Swing Channel / Show Channel Fill | Yes / Yes | Hiding the channel also hides the fill (as on TradingView). |
| Channel Color (Squeeze Ended) | Fuchsia | Channel color after the squeeze releases. |
| Squeeze 1 (Weak) … Squeeze 5 (Strong) | Yellow → Red | The strength gradient. |
| Signal Marker Offset (ATR Multiple) | 0.25 | Distance of triangles/diamonds from the bar high/low. |
| Enable Alerts | No | Master alert switch (see below). |
| Alerts Only On Bar Close | Yes | Yes = one evaluation per closed bar (recommended; matches "Once Per Bar Close" on TradingView). No = fire as soon as the developing bar qualifies. |

Signal/marker and extended-level **colors** are edited on the study's
**Subgraphs** tab (Bull/Bear Breakout, Bull/Bear Reversal, Ext levels, etc.).
The Reversal Level line automatically reuses the Bull/Bear Breakout colors.

## 6. Alerts

Set **Enable Alerts = Yes**. The study raises these Sierra Chart alerts
(visible in the Alerts Log / Alert Manager; assign sounds per alert number
under **Global Settings >> Alert Sounds**):

| # | Fires when | Message |
|---|---|---|
| 1 | Bull breakout | Squeeze Channel: Bullish breakout |
| 2 | Bear breakout | Squeeze Channel: Bearish breakout |
| 3 | Bull reversal | Squeeze Channel: Failed bear breakout - bull reversal |
| 4 | Bear reversal | Squeeze Channel: Failed bull breakout - bear reversal |
| 5 | Ext resistance broken | Squeeze Channel: Extended resistance breakout |
| 6 | Ext support broken | Squeeze Channel: Extended support breakdown |
| 7 | Ext support reclaimed | Squeeze Channel: Extended support reclaim |
| 8 | Ext resistance rejected | Squeeze Channel: Extended resistance rejection |
| 9 | Squeeze starts | Squeeze Channel: Squeeze detected |

Alerts are suppressed during full recalculations and historical downloads, and
deduplicated to once per bar per condition.

## 7. Using the Study from Other Tools

All outputs are regular subgraphs, so Alert Manager formulas, Spreadsheet
Studies, and other studies can reference them (`ID` = this study's ID on the
chart):

| SG# | Subgraph | Values |
|---|---|---|
| 1 / 2 / 3 | Channel High / Low / Mid | Price, 0 when no channel |
| 4 / 5 | Channel Fill Top / Bottom | Internal fill pair |
| 6 | Reversal Level | Price while watching, else 0 |
| 7 / 8 | Bull / Bear Breakout | Marker price on signal bar, else 0 |
| 9 / 10 | Bull / Bear Reversal | Marker price on signal bar, else 0 |
| 11–14 | Ext Bull/Bear Breakout, Ext Bull/Bear Reversal | Marker price, else 0 |
| 15–19 | Ext Resistance 1–5 | Level price while near, else 0 |
| 20–24 | Ext Support 1–5 | Level price while near, else 0 |
| 25 | Squeeze On (hidden) | 1 while squeezing, else 0 |
| 26 | Squeeze Strength (hidden) | 0–1 while squeezing, else 0 |

Examples (chart/study alert formulas):
- New squeeze started: `AND(ID1.SG25 = 1, ID1.SG25[-1] = 0)`
- Any breakout on the last closed bar: `OR(ID1.SG7[-1] <> 0, ID1.SG8[-1] <> 0)`
- Strong squeeze in progress: `ID1.SG26 >= 0.6`

## 8. TradingView Compatibility

To replicate the original Pine script exactly, set:

| Input | TradingView-exact value |
|---|---|
| Legacy (Pine) Reversal Window Timing | **Yes** |
| Plot Nearest Extended Levels | **No** |
| Squeeze Re-Entry Resets Active Channel | Yes (default) |
| Use Unbiased (Sample) Std Dev | No (default) |

Remaining differences are cosmetic or platform-inherent: dashes instead of
circles for extended levels (change draw styles on the Subgraphs tab if
desired), solid dimmed colors where Pine used per-color transparency, marker
offsets in ATR units instead of pixels, and float vs. double rounding on rare
borderline bars. Signal logic is otherwise identical bar by bar.

## 9. Tips & Troubleshooting

- **Nothing plots** — the chart needs more bars than the longest length
  input; check that the study is on the price region (Region 0).
- **No signals for long stretches** — normal: breakouts require a full
  squeeze → channel → lock sequence. Shorten Bollinger/Keltner lengths or
  reduce the Keltner multiplier for more setups.
- **Markers overlap bars** — raise "Signal Marker Offset (ATR Multiple)".
- **Fill too strong/weak** — adjust "Transparency Level for Fill Styles" on
  the study's Subgraphs tab (~85 matches the original TradingView look), or
  disable "Show Channel Fill".
- **Too many extended levels** — lower "Extended Level Bars" or the ATR
  proximity threshold; levels also disappear on expiry or reversal removal.
- **Chart replay / data reloads** — fully supported; the study rebuilds its
  level state deterministically, and alerts resume with the next closed bar.
- **Intraday vs. daily** — all parameters are in bars, so behavior scales
  with the timeframe automatically; no session-specific settings needed.
