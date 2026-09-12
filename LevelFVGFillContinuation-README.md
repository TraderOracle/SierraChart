# Level → FVG → Fill → Continuation

A Sierra Chart study (ACSIL / C++) that waits for one specific sequence and marks it when all four parts line up:

1. Price **touches** a reference level or zone
2. Within a few bars, a **Fair Value Gap** prints in the direction that rejects the level
3. Price comes back and **fills** that gap
4. Price **closes back through** the gap and continues

Steps 1–3 are common. The study only prints an arrow when step 4 confirms, so a gap that gets filled and then keeps going the wrong way never becomes a signal.

![Chart screenshot](docs/screenshot.png)

---

## Why this sequence

Any of these pieces alone is noise. Together they describe a specific thing happening in the order book.

A **touch** of a meaningful level tells you where participants are making a decision. A **fair value gap** immediately afterward tells you that decision was violent enough to leave a three-bar imbalance behind — somebody was willing to pay through the offer stack rather than wait. The **fill** is the test: price comes back into that imbalance to see whether the people who created it are still there. The **continuation close** is the answer. If they are, price rejects the gap and leaves; if they aren't, price keeps going and the setup is thrown away.

So the study isn't looking for a gap. It's looking for a gap that got tested and held, near a level that mattered.

---

## What you see on the chart

### Signals

| Element | Meaning |
|---|---|
| **Teal up arrow** below a bar | Long setup confirmed on that bar's close |
| **Red down arrow** above a bar | Short setup confirmed on that bar's close |
| **Text under/over the arrow** | Which level the setup came from (`PDL`, `VWAP`, `Range Long`, …) |

Arrows sit one ATR-fraction away from the bar so they don't sit on the wick. Adjust with *Signal Arrow Offset*.

### Fair value gap boxes

A shaded rectangle appears the moment a qualifying FVG prints after a touch, and stretches to the right as long as the setup is still alive.

- **Teal box** — bullish FVG, waiting for the fill or the continuation
- **Red box** — bearish FVG
- **Box becomes more opaque** — the setup confirmed; this is the one that produced an arrow
- **Box disappears** — the setup was invalidated or timed out. Nothing happened here; the study cleans up after itself so the chart doesn't fill with dead rectangles

### Reference levels

Dashed horizontal lines, each a separate subgraph you can recolor or hide in *Settings → Subgraphs*:

| Line | What it is |
|---|---|
| `IBH` / `IBL` | Initial Balance high and low — the range of the opening window, 09:30–10:30 by default |
| `PDH` / `PDL` / `PDC` | Previous trading day's high, low and close |
| `ONH` / `ONL` | Overnight session high and low (off by default) |
| `VWAP` | Session volume-weighted average price, resets each trading day |
| `EMA` | Exponential moving average, 200 by default (solid line) |

### Your own levels

Levels you paste in draw as grey dotted lines. Ranges draw as shaded rectangles. Each one is labelled on the right edge with its name and price.

These are the ones that usually matter most — the study treats a hand-entered `Highest Odds Long FTD` zone exactly the same as it treats VWAP.

---

## Installing

1. Copy `LevelFVGFillContinuation.cpp` into `SierraChart/ACS_Source/`
2. In Sierra Chart: **Analysis → Build Custom Studies DLL → Build**
3. Open a chart, **Analysis → Studies**, add **Level -> FVG -> Fill -> Continuation**

If the build fails on `DRAWSTYLE_ARROW_UP` / `DRAWSTYLE_ARROW_DOWN`, your Sierra version names these differently — change them to `DRAWSTYLE_TRIANGLE_UP` and `DRAWSTYLE_TRIANGLE_DOWN` near the top of the `sc.SetDefaults` block.

---

## Entering your own levels

Two formats. The study auto-detects which one each line uses, and you can mix them freely.

### Format A — name, price pairs

```
$NQ1!: vix r1, 29402, vix r2, 29417, VAH, 29488, range daily min, 28743
```

Everything before the `:` is treated as a ticker tag and ignored. After that it reads as name, price, name, price. Multi-word names are fine.

### Format B — price or range, then name

```
7714.00 - 7702.75 Extreme Short
7674.00 - 7664.75 Highest Odds Short FTU
7606.75 Line in the Sand
7603.75 - 7594.25 Range Long
```

A range becomes a **zone**. A single number becomes a **line**. The name is whatever follows the price.

A line like `ES Execution/Target Zones:` has nothing after the colon, so it's skipped as a header. Blank lines are ignored.

### Where to put them

**Short lists** → the four *Set A–D* text inputs. Sierra Chart's string inputs are single-line, so separate entries with a semicolon:

```
7606.75 Line in the Sand; 7603.75 - 7594.25 Range Long
```

**Longer lists** → save a plain `.txt` file, one entry per line, and put the full path in *Levels File*. This is the better option for daily level sheets — you overwrite the file each morning and the study picks it up on the next full recalculation (right-click the chart → **Recalculate**).

Both sources load together, so you can keep permanent levels in the text inputs and rotating ones in the file.

### Naming details

Duplicate names are auto-numbered: three lines called `MAJOR positive GEX cluster` become that plus `(2)` and `(3)`. This matters — the re-arm cooldown and the one-per-day limit are keyed by name, so without it a touch at one cluster would lock out the others.

*Only Use Lines Matching This Symbol* compares the first two characters of the ticker tag against the chart symbol, so `$ES1!`, `ESH6` and `ESU2026` all match an ES chart. Lines with no tag are always included. Off by default.

---

## Settings

### 1 · Pasted levels & zones

The four text inputs, the file path, and how your levels draw. *Pasted Level Length* controls how far back the lines and zones extend.

### 2 · Built-in levels

Toggle each reference level on or off, and set the IB and overnight windows. Times are in the chart's timezone. Turning a level off removes it from the chart *and* stops it generating setups.

### 3 · Touch

**Touch Tolerance** — how close counts as a touch. `ATR` mode scales with volatility (0.10 × ATR by default), `Ticks` is fixed, `None` requires the bar's range to actually reach the level.

**Bars Before Same Level Re-Arms** — a cooldown, 10 bars by default. Without it, price grinding along VWAP would arm a new setup on every single bar. It's per level, not global, so ES levels 20 points apart don't block each other.

### 4 · FVG

**Min / Max Bars Touch To FVG** — how soon after the touch the gap has to appear. Defaults are 1 and 4. Tightening the max makes the study stricter about the gap being a *reaction to* the level rather than something that happened nearby.

**Min Gap Size** — ignores hairline gaps. 0.10 × ATR.

**Min Displacement Body** — the middle candle of the three-bar pattern must have a body of at least 0.60 × ATR. This is the most important filter in the study. Without it you get a lot of gaps nobody will defend. **Tune this per instrument and timeframe** — 0.60 ATR is generous on 1-minute futures and quite strict on 15-minute.

**FVG Must Close Clear Of The Level** — requires the gap's final bar to close on the rejecting side of the level (above the zone for a long, below it for a short). On by default.

### 5 · Fill & continuation

**Fill Requirement** — how deep the retrace has to go:

| Option | Bullish gap between 7600 (bottom) and 7610 (top) |
|---|---|
| Touch (near edge) | Price must trade down to 7610 |
| 50% (CE) | Price must trade down to 7605 |
| Full (far edge) | Price must trade down to 7600 — **default** |

**Max Bars To Fill** — if the gap isn't filled within 30 bars, the setup expires. Price ran away without giving you the entry.

**Continuation Confirmation** — what counts as "and continues on":

- *Close back through gap* — a close above the gap's top (bullish). Earlier, more signals.
- *Close beyond FVG bar extreme* — a close above the high of the gap's third bar. Later, fewer, stronger.

**Max Bars To Confirm After Fill** — 15 bars to get the continuation close, then the setup dies.

**Invalidation Buffer Beyond Gap** — if price *closes* past the gap by this much (0.15 × ATR), the setup is killed immediately. This distinguishes a fill from a failure: a wick to the far edge is the fill you wanted, a close through it means the imbalance didn't hold.

**One Signal Per Level Per Day** — useful when you have a dense level sheet and only want the first reaction at each price.

### 6 · Visuals

FVG box colors and transparency, and how far the signal arrows sit from the bar.

---

## Alerts

Alert number **1** fires on longs, **2** on shorts, with a message naming the level:

```
LONG continuation off Highest Odds Long FTD  |  ESZ26
```

Wire them up in **Chart Settings → Alerts**. Alerts only fire on live bars, never during a historical recalculation.

---

## How it handles the live bar

The engine only evaluates **closed** bars. Nothing about a signal can change once it's printed — no repainting, no arrows appearing and vanishing as the current bar moves.

The practical consequence: a signal shows up on the first tick of the bar *after* the one that confirmed it, rather than at the exact moment of the close. Same instant in wall-clock terms, one bar later in appearance.

Reference levels are the exception — VWAP, the EMA and the developing IB update live, because you want to see where they are right now.

---

## Notes and limitations

- **ATR** is fixed at 14-period Wilders, used internally for every `× ATR` setting.
- **Zones extend to the last bar only.** Sierra's rectangle drawings don't project into the empty space to the right of the chart, so there's no "extend right" option.
- **Hyphens inside level names** get split by the Format B parser — `Line-in-Sand` displays as `Line - in - Sand`. Use spaces.
- **Commas inside level names** break Format A, since commas are the delimiter. Format B has no such problem.
- The level file is read on **full recalculation** only. Edit the file, then right-click the chart → **Recalculate**.
- State resets on recalculation and on historical backfill, so the study rebuilds its touch and setup history from scratch rather than double-counting.

---

## A worked example

Say `PDL` sits at 7594 and price sells off into it during the morning.

1. A bar's low reaches 7593.50. Within tolerance — **touch registered** on `PDL`.
2. Two bars later a strong green candle prints, and the bar after it opens above the high of the bar before the green one. Gap is big enough, body is big enough, close is above 7594. **Bullish FVG created**, teal box drawn.
3. Over the next eleven bars price drifts back down and wicks into the bottom of the box. **Filled.**
4. Three bars later a candle closes above the top of the box. **Teal arrow prints below the bar, labelled `PDL`.**

If instead price had *closed* below the bottom of the box at step 3, the box would have vanished and no arrow would ever print there.

---

## Related

A TradingView Pine Script v6 version of this study lives alongside it. Behaviour is identical apart from the platform differences noted above.
