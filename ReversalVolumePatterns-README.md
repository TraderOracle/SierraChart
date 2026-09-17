# Reversal Volume Patterns — Sierra Chart Study

An overlay study for Sierra Chart (ACSIL) that flags potential **reversal bars**
from volume + candle structure, filters them with RSI, and — optionally —
waits for a **Fair Value Gap (FVG)** to form as confirmation.

It was ported from a TradingView Pine Script study of the same name and then
extended with the FVG layer.

---

## 1. What you'll see on the chart

| Symbol | Where | Meaning |
|---|---|---|
| 🔵 **Up arrow** | below a bar | **Buy** signal — a reversal pattern fired pointing up |
| 🔵 **Down arrow** | above a bar | **Sell** signal — a reversal pattern fired pointing down |
| 🟢 **Green dot** | below a bar | **FVG-confirmed buy** — a bullish gap formed shortly after a buy arrow |
| 🔴 **Red dot** | above a bar | **FVG-confirmed sell** — a bearish gap formed shortly after a sell arrow |
| 🟩 **Green box** | over 3+ bars | the **bullish gap** (Fair Value Gap) that did the confirming |
| 🟥 **Red box** | over 3+ bars | the **bearish gap** that did the confirming |

> An **arrow** is the raw signal. A **dot + box** means that signal was later
> confirmed by a gap. Everything is restylable — colors, shapes and sizes live
> in **Study Settings → Subgraphs**, so you can turn the dots into diamonds,
> recolor the arrows, etc., without recompiling.

---

## 2. The core idea

A reversal bar is one where the market shows exhaustion in one direction and a
turn in the other. This study looks for four such shapes, then optionally asks
for two forms of agreement before it trusts them:

1. **RSI** — only take buys when momentum is already stretched to the downside,
   and sells when it's stretched to the upside.
2. **FVG** — only (or additionally) mark the signal once price actually leaves a
   gap in the reversal's direction, which is a sign the turn had force behind it.

A **green candle** = close above open. **Anything else** (including a doji where
close = open) is treated as **red**.

---

## 3. The four patterns

You can switch each one on or off independently.

**Pattern #1 — small red, larger red, small green**
A pullback where selling volume expands and then a green bar appears on lighter
volume. (The mirror image gives the sell version: green, larger green, small red.)

**Pattern #2 — red, larger red, even larger red, small green**
The three-step version of Pattern #1: selling volume grows across three red bars,
then a small green reversal bar. (Mirror for sells.)

**Pattern #3 — four same-color bars, then a bigger opposite-color bar**
Four reds in a row, then a green bar whose volume is greater than all four
previous bars — a decisive flip. (Mirror for sells.)

**Pattern #4 — candlestick reversal**
A long-wick rejection candle (hammer / shooting-star style) that also sits at a
**5-bar pivot** — a local low for buys, a local high for sells. This is the only
pattern that uses wick geometry rather than volume.

If **Color Raw Arrows by Pattern** is on, arrows are tinted by which pattern
fired: **#1 fuchsia, #2 purple, #3 blue, #4 yellow** (if several fire at once,
the highest-numbered wins). Off by default (all arrows blue).

---

## 4. The RSI filter

When **Filter using RSI** is on:

- A **buy** arrow only shows if RSI is **≤ the Oversold value** (default 44).
- A **sell** arrow only shows if RSI is **≥ the Overbought value** (default 56).

So the study looks for reversals *into* an already-stretched market. RSI uses
Wilder's smoothing (the standard 14-length RSI). Raising Overbought / lowering
Oversold makes the filter stricter and produces fewer arrows.

There's also a built-in **de-duplication**: the study never prints two arrows of
the same direction on consecutive bars.

---

## 5. The FVG confirmation

A **Fair Value Gap** is a 3-candle imbalance — price moved so fast that the
middle candle left an untraded gap:

- **Bullish FVG** — the current bar's **low is above the high two bars back**
  (`Low[0] > High[-2]`). Up-side imbalance.
- **Bearish FVG** — the current bar's **high is below the low two bars back**
  (`High[0] < Low[-2]`). Down-side imbalance.

After an arrow prints, the study watches the next few bars (the **FVG Window**,
default 3) for a gap **in the same direction**: a bullish gap confirms a buy, a
bearish gap confirms a sell. Each arrow is confirmed **once**, by the nearest
qualifying gap.

### Two modes

**Mode 0 — "Show all arrows + mark FVG"** (default, recommended)
Arrows appear immediately as normal. When a confirming gap forms, a dot is added
on the gap bar and the gap is boxed. **Nothing moves after the fact** — what you
saw live is what stays.

**Mode 1 — "Only FVG-confirmed arrows"**
Raw arrows are held back. An arrow only appears — back-dated onto its original
reversal bar — once a gap confirms it.
⚠️ Because the study waits for the gap, the arrow shows up **1–3 bars late and
therefore repaints within the window.** This is expected for a confirmation
filter; use Mode 0 if repainting bothers you.

**Min FVG Gap (ticks)** filters out tiny gaps (0 = accept any gap).

---

## 6. Inputs reference

| Input | Default | What it does |
|---|---|---|
| Show Pattern #1 | On | Enable the 3-bar volume pattern |
| Show Pattern #2 | On | Enable the 4-bar volume pattern |
| Show Pattern #3 | On | Enable the "4 bars then bigger flip" pattern |
| Show Pattern #4 (Candlestick) | On | Enable the pivot + long-wick pattern |
| Filter using RSI | On | Require RSI agreement for arrows |
| RSI Overbought Value | 56 | Sells need RSI ≥ this |
| RSI Oversold Value | 44 | Buys need RSI ≤ this |
| RSI Length | 14 | RSI period (Wilder's) |
| Arrow Offset (ticks) | 4 | Distance of arrows from the bar |
| Color Raw Arrows by Pattern | Off | Tint arrows by which pattern fired |
| **Enable FVG Confirmation** | On | Turn the whole FVG layer on/off |
| **FVG Mode** | Show all + mark | Mode 0 (mark) vs Mode 1 (confirmed-only) |
| **FVG Window (bars after arrow)** | 3 | How many bars after an arrow to look for a gap |
| **Min FVG Gap (ticks)** | 0 | Ignore gaps smaller than this |
| **Draw FVG Box** | On | Draw the shaded gap zone |
| **FVG Box Extend (bars)** | 5 | How far right the box stretches |
| **FVG Box Transparency (0-100)** | 70 | Box fill transparency |
| **FVG Bullish Box Color** | green | Bullish gap fill |
| **FVG Bearish Box Color** | red | Bearish gap fill |

---

## 7. Install & build

1. Copy `ReversalVolumePatterns.cpp` into your `SierraChart\ACS_Source\` folder.
2. In Sierra Chart: **Analysis → Build Custom Studies DLL → Build**.
3. On your chart: **Studies → Add Custom Study → "Reversal Volume Patterns"**.
4. It draws in the **main price region** (it's an overlay).

Requires only standard OHLCV data — no footprint / Volume-at-Price data needed.

---

## 8. Good to know

- **Arrows print on bar close.** A signal on the currently-forming bar can change
  until that bar closes.
- **Reversal patterns describe what just happened; they don't predict.** Treat
  the arrows as a shortlist to review, not automatic entries. Validate on your
  own instrument and timeframe.
- **The pivot test uses strict comparison.** On very low-tick instruments where
  bars often share the same high/low, Pattern #4 may fire less often; that
  behavior can be loosened in the code if needed.
- **Mode 1 repaints by design** (see §5). Mode 0 does not.
- This is an analysis tool, not financial advice.
