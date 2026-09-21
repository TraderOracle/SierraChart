# ADITRADYMEGATRON

A single Sierra Chart study that combines a multi-filter confluence engine, a
gap-trading engine (volume imbalance → continuation / Bollinger Gap / outside
bar reversal), a candle-pattern scanner, SMI divergence, a "Wave" trend state,
a volume-confirmed "Vodka Shot" entry signal, and a small on-chart dashboard —
all in one indicator, no other studies required.

This README is split into three parts, in this order:

1. **[What you're looking at](#1-what-youre-looking-at)** — every line, marker,
   label, color and dashboard row explained.
2. **[How the strategy works](#2-how-the-strategy-works)** — what each signal
   actually means and how they're meant to be used together.
3. **[Every setting, explained](#3-every-setting-explained)** — the full
   options reference, grouped exactly as they appear in the Study Settings
   dialog. It's long on purpose — it's at the *end* so it doesn't get in the
   way of understanding the indicator first.

---

## 1. What you're looking at

### Overlay lines

| Symbol | Meaning |
|---|---|
| Gold/brown line hugging price | **KAMA** (Kaufman Adaptive Moving Average) — speeds up in trends, flattens in chop. |
| Blue upper/lower lines | **Bollinger Bands** (default 20-period, 2 std-dev). |
| Amber/gold middle line | **Bollinger Middle** (the basis / moving average of the bands). |

### Bar coloring (off by default)

Turn on **08 - Bar Color Mode** to have candles themselves painted:

- **Waddah** — candle brightness/color tracks the strength of the "Waddah
  Explosion" (rate of change of the 20/40 EMA spread): brighter = stronger
  momentum, colored by your Waddah Up/Down colors.
- **LindaMACD** — candles colored green/red by the sign of the Linda MACD
  histogram, brightness scaled by its size.
- **Supertrend** — candles simply colored by the SuperTrend direction
  (green = up, red = down).

### Background shading

- A translucent green/red band drawn *behind* a candle marks a **Bollinger
  Engulf**: price closed outside a band, then the next bar has a bigger body
  than the last — a hint of expansion, not a signal by itself.
- Pale blue/pink **candle bodies** (not background — the candle itself) mark
  a **Shaved Candle**: the candle closed at (or almost at) its high or low,
  i.e. no wick on that side — one-sided conviction.

### Signal markers

| Glyph | Name | Meaning |
|---|---|---|
| ▲ / ▼ (green/red, at the bar's low/high) | **Buy / Sell** | The core confluence signal fired — see [Section 2](#the-core-confluence-signal). |
| ⬆ / ⬇ (bigger, further from price) | **Big Arrow** | A stronger, rarer MACD-vs-Bollinger-width + PSAR signal — see below. |
| ✦ (yellow star) | **Squeeze Star** | The Squeeze Momentum histogram just turned (relaxed up/down). |
| △ / ▽ (small, hollow) | **Volume Imbalance** | A same-direction two-bar gap was just detected (the start of the gap engine's life cycle). |
| ▲ / ▼ + **"FC"** label | **FC Continuation** | A valid gap was touched and price then continued through it in the gap's direction. |
| ▲ / ▼ + **"BG"** label | **Bollinger Gap** | A volume-imbalance gap formed while price was hugging the outer Bollinger Band. |
| ◆ (diamond) or ▲/▼ + **"OBR"** label | **Outside Bar Reversal** | An engulfing bar formed right at a Bollinger Band. |
| ✚ (heavy plus, green/red) | **Vodka Shot** | A volume + trend + "Wave" confirmed momentum entry. |
| ■ (square) | **Reversal Square** | Volume-weighted order-flow reversal at a band. |

### Candle-pattern text labels

Small text tags with a colored background, printed at the bar they fired on:

| Label | Pattern |
|---|---|
| **3oU** / **3oD** | Three Outside Up / Down (bullish or bearish engulfing + follow-through close). |
| **Eq Hi** / **Eq Lo** | Tweezer top / bottom at a Bollinger Band. |
| **Wick** | A rejection wick at a band, larger than the opposite wick. |
| **Stairs** | Four bars of shrinking bodies, closes walking one direction. |
| **KB** | KAMA Bounce — price pierced the KAMA line intrabar and closed back on the same side. |
| **TR** | Trampoline — RSI extreme at a band followed by a two-bar reversal. |

### Gap engine visuals

- **Solid line** — an active gap's reference line (price level to watch).
  Color = bullish/bearish gap color, or gray/red once touched/invalidated,
  depending on your settings. It projects forward (dashed-looking, thin)
  until price touches it or it ages out.
- **Translucent colored box** (off by default) — the **gap zone**, the actual
  price range of the imbalance, not just its reference line.
- **Small gold dot** (off by default) — a **touch marker**, placed exactly
  where price touched a gap line.

### Other on-chart text

- **"SPIDERWEB WARNING — N valid gaps within X ticks"** (orange, appears
  above the current bar) — too many untouched, *valid* gaps are stacked up
  close to price. Historically a sign of a "spiderweb" of overhead/underfoot
  supply — a caution flag, not a trade signal.
- **Evil Times session labels** (off by default) — text banners like
  *"Market Pivot (9-10am CST)"* during specific intraday windows, purely
  informational session markers.
- **SMI divergence lines** — a line connecting two price pivots when the SMI
  oscillator disagreed with price (see [Section 2](#smi-divergence)).

### The dashboard (small HUD panel)

A stack of compact, colored rows anchored near recent price action (top or
bottom, left- or right-justified — see **22 - Dashboard**):

| Row | Shows |
|---|---|
| **Trend** | SuperTrend direction (UP/DOWN) + current ADX reading. |
| **Wave** | Current Wave state (UP/DOWN) — see [Section 2](#wave-state). |
| **SMI** | Current SMI value and whether it's above/below its own average (bull/bear/flat). |
| **Squeeze** | Whether the squeeze histogram last relaxed up or down. |
| **Gaps** | Count of active gaps / how many are past their "valid" age. Turns orange if a spiderweb warning is active. |
| **VolRatio** | Current volatility ratio (ATR ÷ its own average) — only actually *used* to scale thresholds if Adaptive Thresholds is on, but always shown. |

Every row updates every bar; it isn't a log of past signals, it's a live
snapshot of current market state.

### Data Box / Values window outputs (not drawn, but available)

The indicator also exposes numeric "subgraph" values you can read in Sierra's
Values window or reference from another study: suggested Entry/Stop/Target
price, a signed Signal Direction/Code, Master Long/Short flags, active/valid
gap counts, SMI value/average/state, Wave state, and the volatility ratio.

---

## 2. How the strategy works

### Philosophy

There is no single "the" signal here — this is a toolbox of several
independent detectors that can be turned on or off and combined. Out of the
box, the two headline signals are the **confluence Buy/Sell** (trend-following,
always on) and the **gap engine** (FC / BG / OBR — mean-reversion-to-continuation
style). Everything else (patterns, Vodka Shot, SMI divergence) is context or
an independent secondary trigger you can layer on top.

### The core confluence signal

Every closed bar, up to 13 independent filters are checked (Waddah,
Linda MACD, PSAR, SuperTrend, Awesome Oscillator, HMA, T3, Fisher Transform,
Squeeze Momentum, ADXVMA, "Lizard" SuperTrend, Multi TSI, SMI). Only the ones
you enable in **01 - Confluence Filters** actually vote. A **Buy** fires when
every *enabled* filter agrees bullish and ADX clears your minimum threshold;
**Sell** is the mirror. This is the same idea as a standard "everything lines
up" confluence indicator — more filters enabled = fewer, higher-conviction
signals; fewer filters = more, noisier signals.

**Filter Rules** (Sierra vs. NinjaLegacy) changes *how* several of those
filters are evaluated (sign-based vs. slope-based for SAR/Fisher/HMA/AO) —
it's a rule-set choice, not an on/off switch. **Sierra Exact Mode** goes
further and reproduces a handful of specific quirks from the God Trades
(Sierra Chart) version of this system (SMA-averaged RSI, a different squeeze
basis, both triangles allowed to print together, etc.) — leave it off unless
you specifically want that exact historical behavior.

The **Big Arrow** is a separate, stricter check on top of the same Waddah/PSAR
data: it only fires when the Waddah value's *magnitude* exceeds the current
Bollinger Band width while PSAR flips — a rarer, higher-conviction version of
the same momentum idea, meant to mark genuinely aggressive moves.

### The gap engine (Volume Imbalance → FC / BG / OBR)

This is the second major system, and it runs independently of the confluence
filters:

1. **Volume Imbalance** — two same-direction bars in a row, where the second
   opens beyond the first bar's close by at least *N* ticks. This is the
   "gap" — the theory being that a sudden, unfilled move can act like an
   invisible support/resistance line the market tends to revisit.
2. The gap sits **active** and grows an aging counter. If price touches it
   before it's old enough (**Min Bars Before Valid**), it's flagged
   **invalid** (drawn red/dimmed) — it was tested too soon to mean anything.
3. Once a gap is old enough and gets touched, that's a **valid touch**. This
   is where two different outcomes can happen:
   - **FC (Continuation)** — if price approaches from the correct side and
     then, within a few bars, confirms by closing back beyond the gap
     (or the gap's line, or just touching it, depending on your Confirm
     Mode) — that's a same-direction continuation entry.
   - **BG (Bollinger Gap)** — if the imbalance itself formed while price was
     already hugging (or piercing) the outer Bollinger Band, it's tagged as
     a Bollinger Gap immediately, in place of a plain Volume Imbalance arrow
     — a continuation-at-the-extreme entry.
4. **OBR (Outside Bar Reversal)** is a related but separate check: an
   engulfing bar (bigger body, opposite direction of the prior bar) that
   also happens right at a Bollinger Band. Unlike FC/BG this is a
   **reversal** idea, not continuation.
5. **Spiderweb Warning** is purely a risk flag: if too many *valid, untouched*
   gaps have piled up within a tick distance of price, that's historically a
   sign of a chopped-up, indecisive zone — a reason to reduce size or stand
   aside, not a directional signal.

Every time FC, BG or OBR fires, the indicator also computes a **suggested
Entry / Stop / Target** (visible in the Values window): entry = the close of
the signal bar, stop = beyond the signal bar's high/low by your offset, and
target = either the opposite Bollinger Band or a fixed tick count, per
**15 - Target Mode**.

### Vodka Shot

A separate, purely momentum-and-volume based signal: it requires the "lazy"
triple-smoothed WMA trend line to be rising (falling), a fast/slow EMA
"MACD"-style energy reading to agree, the bar itself to close in that
direction, *and* volume to be meaningfully above its own recent statistical
average (mean + a multiple of its standard deviation). It also enforces a
cooldown so it can't repeatedly re-fire on the same move. By default it also
requires the **Wave** state (below) to agree with its direction.

### Wave state

A simple, persistent directional flag (UP or DOWN) that flips only when a
"bright" candle breaks convincingly past a recent candle of the opposite
color. It's not a signal by itself — it's a slow-moving bias filter you can
optionally require FC, BG, OBR or Vodka Shot to agree with (see **17 - Wave**).

### SMI divergence

Independent of everything else: the Stochastic Momentum Index is checked for
fractal pivots, both on price and on the oscillator itself. When price makes
a new extreme but the SMI doesn't confirm it (or vice versa, for hidden
divergence), a line is drawn between the two pivots — an early warning that
the current move may be running out of steam, well before any of the other
systems would flag a reversal.

### Candle patterns

The nine pattern detectors (Three Outside, Tweezers, Wick, Stairs, Reversal
Square, Shaved Candles, BB Engulf shading, KAMA Bounce, Trampoline) are best
treated as **context and confirmation**, not standalone entries — they mark
recurring "the crowd just did something" shapes, most of them specifically
tied to a Bollinger Band touch, and read naturally alongside a nearby FC/BG/OBR
or confluence signal rather than in isolation.

### Time-of-day filtering & adaptive thresholds

- **15 - Use Signal Time Filter** restricts FC/BG/OBR entries to a session
  window (e.g. skip the open or the close).
- **18 - Volatility Adaptive Thresholds**, when enabled, scales the ADX floor,
  gap-size filters, band-distance tolerances and the spiderweb distance up or
  down based on how the *current* ATR compares to its own recent average —
  so the same settings behave more consistently across a calm vs. volatile
  session, or across instruments, without you manually re-tuning tick counts.

### Reading the dashboard as a whole

At a glance: **Trend** + **Wave** agreeing tells you the higher-timeframe bias;
**SMI** and **Squeeze** tell you if momentum is confirming or already fading;
**Gaps** (and whether it's flagged orange) tells you if you're trading into a
clean zone or a congested one. None of this replaces watching the actual
markers fire — it's a fast sanity check.

---

## 3. Every setting, explained

Settings are numbered exactly as they're grouped in Sierra's Study Settings
dialog (the number prefix, e.g. "01 -", "02 -", only exists to keep related
settings sorted together in that flat list).

### 01 — Confluence Filters

| Setting | Default | What it does |
|---|---|---|
| Filter Rules | Sierra | Sierra = God Trades-style sign/close-based rules for SAR/Fisher/HMA/AO. NinjaLegacy = slope/low-based rules (OptimusNinja style). |
| Sierra Exact Mode | Off | Reproduces several specific God Trades (Sierra Chart) quirks together (see [Section 2](#the-core-confluence-signal)). |
| Filter: Waddah Explosion | On | Include the Waddah Explosion sign in the Buy/Sell vote. |
| Filter: Linda MACD | On | Include the Linda MACD histogram sign. |
| Filter: Parabolic SAR | On | Include PSAR position relative to price. |
| Filter: SuperTrend | Off | Include the main SuperTrend direction. |
| Filter: Awesome Oscillator | Off | Include the Awesome Oscillator. |
| Filter: Hull Moving Avg | On | Include HMA position/slope. |
| Filter: T3 | Off | Include the T3 moving average. |
| Filter: Fisher Transform | On | Include the Fisher Transform. |
| Filter: Squeeze Momentum | Off | Include the squeeze histogram sign. |
| Minimum ADX | 11 | Confluence signals require ADX at or above this (unless Adaptive Thresholds scales it). |
| Filter: ADXVMA Trend | Off | Include the built-in ADXVMA trend state. |
| Filter: Median SuperTrend (Lizard) | Off | Include the secondary "Lizard" SuperTrend. |
| Filter: Multi TSI | Off | Include the True Strength Index vs. its signal line. |
| Filter: SMI | Off | Include SMI vs. its own average (and slope, if required below). |
| Require Candle Direction | Off | On: Buy also requires a green bar, Sell a red bar. |
| Ignore Dojis | On | Skip signals/patterns/coloring on doji bars (the gap engine still runs). |

### 02 — Filter Parameters

Periods/constants for every filter above and for KAMA's building blocks:
Waddah Intensity (150), Linda MACD Fast/Slow/Signal (3/9/16), PSAR
Acceleration/Max/Step (0.02/0.2/0.02), ADX Period (14), HMA Period (10), T3
Period/Stage Count/V Factor (10/3/0.84), SuperTrend Period/Multiplier/ATR
Type (11/2.0/Hull), Squeeze Period (20) + Uses Open (On), ADXVMA Period (8),
TSI Smooth 1/2 + Signal Period (20/9/5), Lizard SuperTrend ATR
Period/Multiplier (15/2.5), and the full SMI parameter set: Period K (8),
Smooth 1/2 (3/5), Signal Period (7), Overbought/Oversold/Midline
(50/-50/0), Require Slope (On), Divergence Lookback (10).

### 03 — Signals / Patterns Display

On/off switches (plus a couple of tolerances) for everything drawn on the
chart: Buy/Sell Triangles, Big Arrow, Squeeze Stars, Volume Imbalance Arrows,
Trampoline (+ RSI High/Low thresholds 80/20 + tolerance), Three Outside,
Tweezers (+ tolerance), Wick Pattern, Stairs (default **off**), Reversal
Square (default **off**), Shaved Candles (+ tolerance), BB Engulf Shading.

### 04 — SMI Divergence

Enable SMI Divergence (off by default), Include Hidden Divergence (on),
Show SMI Divergence Lines (on).

### 05 — KAMA

Show KAMA (on), Fast/Period/Slow (2/9/109), Show KAMA Bounce Markers (off),
Line Width (2).

### 06 — Evil Times

Show Evil Times Session Labels (off), Chart Offset Hours (0 — shift the
built-in CST session windows if your chart isn't already on CST/CDT).

### 07 — Signal Layout

Stack Overlapping Signals (on — keeps labels from piling on top of each
other), Stack Step Ticks (8), Label Text Size (9), Show Signal Labels
(on — the "FC"/"BG"/"OBR" text next to those markers).

### 08 — Bar Color

Bar Color Mode (None/Waddah/LindaMACD/Supertrend, default **None**), plus
per-mode brightness controls: Waddah Bar Offset/Brightness Mode/Gain/Auto
Length/Auto Full Multiple, and the equivalent Linda Brightness Mode/
Intensity/Auto Length/Auto Full Multiple/Bar Offset. "Fixed" brightness uses
a flat gain; "AutoScale" scales brightness relative to the instrument's own
recent average, so it looks consistent across symbols/timeframes.

### 09 — Bollinger

Period (20), StdDev (2.0), Show Bollinger Bands (on), Band Line Width (1),
Show Bollinger Middle (on), Middle Width (1).

### 10 — Gap Engine

| Setting | Default | What it does |
|---|---|---|
| Min Gap Size Ticks | 1 | Minimum imbalance size to count as a gap. |
| Min Bars Before Valid | 3 | Age (in bars) before a touch counts as a real, tradeable touch. |
| Min Body Ticks | 0 | Optional minimum candle body size (both bars) to qualify. |
| Max Gap Bar Range Ticks | 0 (off) | Optional cap on how big the two gap-forming bars can be. |
| Max Active Gaps | 300 | Oldest gaps are dropped past this count (performance/clutter control). |
| Early Touch Handling | Stop Line Immediately | Whether an *invalid* (too-early) touch still stops the gap, or is ignored until the gap is valid. |
| Valid Touch Behavior | Stop + Mark Continuation | Whether a valid touch just closes the gap, or also queues an FC continuation check. |
| Gap Line Price Mode | Current Open Edge | Where the reference line sits: midpoint, the previous close edge, or the current-bar open edge. |
| Show Gap Line | On | Draw the reference line. |
| Show Gap Zone | Off | Draw the filled zone box. |
| Show Touch Marker | Off | Draw a dot exactly where price touched. |
| Use Touched Line Color | Off | Recolor the line gray once touched (before knowing valid/invalid). |
| Gap Line Width | 2 | Base line width (if not scaling by size). |
| Gap Width Scales With Size | On | Wider gaps draw thicker lines. |
| Gap Min/Max Width | 1 / 6 | Line width range when scaling by size. |
| Gap Max Width At Ticks | 20 | Gap size (in ticks) that reaches the max width. |
| Gap Zone Opacity | 12 | Fill opacity (0–100) for the zone box. |
| Gap Line Extension | Projected | Projected = extends into the future until touched; ToCurrentBar = only draws back to the current bar each update. |
| Gap Projection Bars | 300 | How far forward a projected line extends. |

### 11 — FC Continuation

Enable FC Continuation (on), Use Bollinger Midpoint Filter (on), FC Location
Source (WickExtreme — what price point is tested against the midpoint
filter: Close/WickExtreme/HLC3/BodyMidpoint), FC Long Below Mid Pct / FC
Short Above Mid Pct (50%/50% — how far past the midline price must be),
FC Confirmation Mode (RequireCloseBeyondFullZone — TouchOnly /
RequireCloseBeyondLine / RequireCloseBeyondFullZone), FC Confirm Bars After
Touch (2 — the window to confirm in), Require Signal Candle Direction (on),
Require Correct Approach (on — price must have approached from the expected
side), Show FC Markers (on).

### 12 — OBR (Outside Bar Reversal)

Enable (on), Paint OBR Bars (on), Marker Style (Diamond/Arrow/None), Use
Bollinger Midpoint Filter (on), Location Mode (NearBand —
within-tolerance-of-the-band vs. PierceOrPreviousBar), Allow OBR Outside
Band (on), Bear/Bull Tolerance Ticks (4/4), Pierce Ticks (2, used in
Pierce mode), Require Larger Body (off), Min Bar Size Ticks (0), Show Entry
Line (off) + Entry Line Bars (2), Show Zones (off) + Zone Bars (10) + Zone
Opacity (25).

### 13 — Bollinger Gap

Enable (on), Location Mode (Proximity — within-tolerance vs. SierraPierce —
must actually pierce the band), Proximity Ticks (8), Show BG Markers (on).

### 14 — Spiderweb

Enable Spiderweb Warning (on), Show Spiderweb Warning Text (on), Distance
Ticks (100 — how close a gap must be to price to count), Line Count (5 —
how many nearby valid gaps trigger the warning), Font Size (15).

### 15 — Time Filter / Targets

Use Signal Time Filter (off), Start/End Time as HHMMSS (default
10:15:00–15:00:00, wraps past midnight if Start > End), Suggested Stop
Offset Ticks (0), Target Mode (OppositeBollingerBand — None /
OppositeBollingerBand / FixedTicks), Fixed Target Ticks (40).

### 16 — Vodka Shot

Enable (on), Requires Wave Match (on), Volume Scale (1.0 — higher = stricter
volume requirement), Volume Length (70 — bars used for the volume average/
std-dev), Cooldown Bars (4), Trend Length (21 — feeds the triple-WMA "lazy"
line), MACD Fast/Slow/Signal (12/26/9), Glyph Size (18).

### 17 — Wave

Lookback (200 — how far back it searches for the opposite "bright" candle),
Ignore Dojis (off) + Doji Max Ticks (1), Wave Filters FC / BG / OBR (all off
by default — turn on to require each of those signals to agree with the
current Wave direction).

### 18 — Volatility Adaptive Thresholds

Enable (off), ATR Length / Average Length (50/50 — the ratio is *this bar's*
ATR over its own N-bar average), Min/Max Ratio (0.5/2.5 — the ratio is
clamped to this range), and four scope switches (all on by default, only
matter once the master switch above is on): Scale ADX Floor, Scale Gap
Filters, Scale Band Distances, Scale Spiderweb Distance.

### 19 — Alerts

Master switches: Enable Alerts (off), Play Alert Sounds (on). Then one
on/off checkbox per signal type (Buy, Sell, Big Buy/Sell, Squeeze Up/Down,
Volume Imbalance Buy/Sell, Gap Touch Bull/Bear, FC Long/Short, BG Long/Short,
OBR Long/Short, Trampoline, Three Outside, Tweezer, Wick Pattern, Stairs,
Reversal Square, KAMA Bounce, Vodka Buy/Sell, SMI Bull/Bear Divergence,
Spiderweb Warning) — most directional ones default **on**, most pattern/
cosmetic ones default **off**. Alerts write to Sierra's message log and
(if enabled) play one of three sound files:

- Bullish Alert Sound (`Alert2.wav`)
- Bearish Alert Sound (`Alert3.wav`)
- Neutral Alert Sound (`Alert1.wav`)

> These three replace the original's ~28 individual per-signal sound file
> pickers — every signal still routes to "bullish", "bearish" or "neutral" as
> appropriate, you just configure the sound once per category instead of
> once per signal.

### 20 — Offsets (ticks)

How far each marker/label sits from the bar's high/low: Buy/Sell Triangle,
Big Arrow Up/Down, Squeeze Star Up/Down, Volume Imbalance Up/Down, FC/BG/OBR
Marker, OBR Arrow, Signal Label, Pattern Label, Trampoline, Reversal Square,
Evil Times, Vodka Up/Down. All purely cosmetic spacing — raise these if
markers feel cramped against candles at your zoom level.

### 21 — Colors

One color picker per visual element (Waddah Up/Down, Buy, Sell, Squeeze
Star, Volume Imbalance, Gap Bull/Bear Line, Gap Touched, Gap Invalid, Gap
Bull/Bear Zone, Touch Marker, BG Long/Short, FC Long/Short, OBR Bull/Bear,
OBR Light/Shadow Zone, Spiderweb Warning, Pattern Text, Pattern Bull/Bear
Background, Trampoline Text/Background, BB Engulf Green/Red, Shaved
Green/Red, KAMA, BB Upper/Lower (shared), BB Middle, SMI Bull/Bear
Divergence, SMI Hidden Bull/Bear Divergence, Evil Times, Vodka Up/Down).
Defaults are a dark-chart-friendly palette (bright green/red for
bull/bear, gold accents) — change anything that doesn't suit your chart
background.

### 22 — Dashboard

Show Dashboard (on), Dashboard Corner (TopRight/TopLeft/BottomRight/
BottomLeft — controls vertical placement and text justification; the panel
is anchored to recent price, not a literal screen pixel, so it will drift
with price/zoom like any other chart drawing), Dashboard Font Size (11),
Dashboard Panel Opacity (72 — the per-row background chip opacity, 0–100).
