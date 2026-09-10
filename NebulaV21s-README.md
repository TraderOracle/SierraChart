# Nebula v2.1s for Sierra Chart

A C++ (ACSIL) port of TraderOracle's **Nebula v2.1s** TradingView indicator.

Nebula bundles about a dozen well-known indicators into one overlay study. Instead of stacking ten separate panels under your chart, Nebula folds them into three things you actually look at: a **cloud** behind price that shows trend, **colored candles** that show momentum, and a handful of **markers** that flag reversals and entries.

> **This is not financial advice and it is not a trading system.** It is a chart-drawing tool. None of the signals below have been backtested by this project, and several of them were adapted from code with known quirks (documented at the bottom). Test on a simulated account before risking money.

---

## Requirements

- Sierra Chart (any recent version with ACSIL support)
- The bundled compiler that ships with Sierra Chart — no external toolchain needed

## Installing

1. Copy `NebulaV21s.cpp` into your Sierra Chart `Data/ACS_Source/` folder.
2. In Sierra Chart: **Analysis → Build Custom Studies DLL → Build**.
3. Open a chart, then **Analysis → Studies → Add Custom Study → Nebula v2.1s**.

If the build window shows errors, nothing gets installed and your chart is unaffected — fix and rebuild.

## Quick start

The defaults are a reasonable starting point. Two things are worth setting immediately:

- **Chart Background Color** — set this to match your actual chart background (black by default). Candle colors are shaded by blending toward this color, so if it's wrong the candles will look muddy. See [Why there's a background color setting](#why-theres-a-background-color-setting).
- **Cloud: max bars to render** — defaults to 2000. Set it higher only if you need cloud on older bars; it costs performance.

Set your chart to **Candlestick** bars so the body/outline coloring has something to color.

---

## What you're looking at

### 1. The cloud

The shaded band behind price. It's the space between two moving averages:

- **Fantail VMA** — a volatility-adjusted moving average that speeds up in trends and flattens in chop
- **McGinley Dynamic** — a moving average that adjusts its own speed to price

**Green cloud** = Fantail VMA is above McGinley → uptrend.
**Red cloud** = below → downtrend.

The thickness of the cloud is just the gap between the two lines. A fat cloud means the two averages disagree strongly (fast move); a pinched cloud means they've converged (consolidation, or a turn coming).

**Cloud Type** changes what the *shading intensity* means:

| Cloud Type | Brightness driven by |
|---|---|
| `None` | No cloud drawn |
| `Simple` | Nothing — one flat color at fixed opacity (**default**) |
| `Relative Strength` | RSI(14) — brighter as RSI moves toward the extremes |
| `Money Flow` | MFI(14) |
| `Commodity Channel` | CCI(20) |

Start with `Simple`. The others add information but also visual noise.

### 2. The candles

Nebula recolors your price bars. Pick one mode under **Candle Coloring**:

| Mode | What the color means |
|---|---|
| `Waddah` | Waddah Attar Explosion — bright = strong momentum with expanding volatility, dim = momentum without expansion (**default**) |
| `Squeeze` | TTM Squeeze momentum — bright = momentum accelerating, dim = fading |
| `Vector` | PVSRA volume analysis — green/red = climax volume, blue/violet = above-average volume, dark = ordinary bar |
| `Volume Delta` | Cumulative buying vs selling volume estimated from candle shape |
| `None` | Leave your candles alone |

The general read across all four: **bright, saturated candles mean conviction. Dim, washed-out candles mean the move is running out of fuel.**

Body fill and outline/wick are colored separately (`Color Candle Body Fill` / `Color Candle Outline / Wick`). Turn either off if you prefer.

### 3. The markers

TradingView can print text characters on bars; Sierra Chart can't, so every symbol from the original became a shape. Here's the translation:

| Shape | Color | Where | Original | What it means |
|---|---|---|---|---|
| **Dot** | Green | Below bar | `.` | **Buy signal.** A Tidal Wave reversal — price stopped overlapping in the down direction and started a new up leg. |
| **Dot** | Red | Above bar | `•` | **Sell signal.** Same idea, downward. |
| **Arrow ▲** | Green | Below bar | `➊` | **Strong buy.** Same as the dot, but the Ultimate Buy/Sell engine also fired within the last 4 bars. Two systems agreeing. |
| **Arrow ▼** | Red | Above bar | `➊` | **Strong sell.** |
| **Triangle ▲** | Green | Below bar | `+` | **Add to a long.** A volume imbalance (gap) formed in the direction of the existing uptrend. |
| **Triangle ▼** | Red | Above bar | `+` | **Add to a short.** |
| **Star ✦** | Green | Below bar | `✚` | **"Vodka Shot"** — a stronger add signal, needing trend, volume expansion, and directional-energy agreement all at once. Rare. |
| **Star ✦** | Red | Above bar | `✚` | Same, short side. |
| **Square** | Red | With trend | `✔` | **Take profit (higher threshold).** Fires at 7+ accumulated signal points. |
| **Square** | Purple | With trend | `✓` | **Take profit (lower threshold).** Fires at 5+ points. |
| **Triangle** | Yellow | Either | — | **9/21 EMA cross.** Off by default. |

Take-profit squares appear *above* the bar in an uptrend and *below* in a downtrend — the side you'd be exiting toward.

Markers sit a few ticks off the bar; adjust with **Marker Offset (ticks)**.

Three more marker sets are ported but **hidden by default**, because the original had them commented out. Turn them on if you want to see the components working:

- **Ultimate Buy/Sell** (cyan/orange triangles) — the RSI + Bollinger + ATR engine that upgrades dots to arrows
- **Trampoline** (green/red squares) — oversold/overbought thrust reversals
- **Squeeze** (yellow diamonds) — TTM squeeze momentum flips

### Lines

- **Fantail VMA** and **McGinley Dynamic** — the cloud edges, thin gray
- **HEMA** — a smoothed trend line, magenta, off by default
- **Rational Quadratic Kernel** — a smooth regression curve, off by default

---

## How the take-profit counter works

This is the one piece of Nebula that isn't obvious from looking at the chart.

Nebula runs seven small reversal detectors in the background. Each one that fires on the current bar (or the bar before it) adds points to a running total for that bar:

| Component | Points | What it detects |
|---|---|---|
| Trampoline | 4 | RSI + Bollinger exhaustion followed by a thrust the other way |
| Squeeze Relaxer | 4 | TTM squeeze momentum losing steam, with ADX confirmation |
| LuxAlgo Reversal | 3 | A 9-bar TD-style counting sequence completing |
| Dead Simple Reversal | 2 | An engulfing-style bar at a 50-bar extreme |
| Total Recall | 2 | Climax volume landing exactly on a broken market-structure level |
| The Shark | 2 | RSI outside its own Bollinger Bands |
| John Wick | 2 | A long wick rejecting a 2.5σ Bollinger Band |

Maximum is 19. When the total hits **5**, the purple square prints. At **7**, the red square prints.

The logic: one reversal detector firing is noise. Four firing on the same bar means a lot of independent methods think the move is exhausted — a reasonable moment to reduce size. The counter resets every bar; it doesn't accumulate over time.

Thresholds are adjustable (`Minimum signals for take partial profit` / `take ALL profit`).

> **Naming quirk, preserved from the original:** the input labeled "take partial profit" (5) drives the marker the original code titled "Take Full Profit," and vice versa. The numbers work; the two labels are swapped. Left as-is so behavior matches TradingView.

---

## Settings reference

Settings appear in the study's Inputs tab roughly in this order.

**Display**
| Setting | Default | Notes |
|---|---|---|
| Cloud Type | Simple | See cloud table above |
| Candle Coloring | Waddah | See candle table above |
| Color Theme | Standard | Four palettes, matching the original |
| Chart Background Color | Black | Must match your chart — see below |
| Color Candle Body Fill / Outline | Yes | Turn off to keep your own candle colors |
| Evaluate Signals Only On Bar Close | Yes | With this on, signals won't flicker mid-bar. Recommended. |
| Marker Offset (ticks) | 4 | Distance markers sit from the bar |

**Cloud**
| Setting | Default | Notes |
|---|---|---|
| Draw cloud fill | Yes | Turn off to use Sierra's built-in fill study instead |
| Cloud outline width | 0 | Leave at 0 — anything higher outlines every bar |
| Cloud: max bars to render | 2000 | 0 = whole chart, but slow on long histories |
| Simple Cloud Opacity | 80 | 0 = solid, 100 = invisible |
| Cloud Opacity Lower / Upper Limit | 80 / 50 | Opacity range for the RSI/MFI/CCI modes |

**Signal visibility** — `Show HEMA line`, `Show plus sign to add`, `Show bigger plus sign (Vodka Shot)`, `Show take profit suggestions`, `Show 9/21 EMA cross`, `Show volume imbalances`, plus the three hidden marker sets.

**Component tuning** — every component keeps its own group of inputs (WAE, Trampoline, Squeeze, Fantail VMA, Ultimate Buy Sell, VADER, Shark, ADX, HEMA, RQK). Defaults match the TradingView original. If you don't know what one does, leave it.

Two Trampoline notes from the original author: the defaults are tuned for **30-minute candles**, and `Bollinger Lower Threshold` should be `0.003` for daily bars, `0.0015` for 30-minute.

### Why there's a background color setting

TradingView colors have transparency built in. Sierra Chart subgraph colors don't — they're solid RGB. To reproduce a "70% transparent green" candle, this port blends green 70% of the way toward your background color. If the setting doesn't match your actual background, dim candles will look wrong.

The cloud is exempt: it's drawn with real transparency, so it looks correct regardless.

---

## Alerts

Ten alert conditions are registered and fire on the last bar. Configure them under the study's **Alerts** tab / Sierra's alert manager:

Ultimate Buy/Sell · Buy Signal Basic · Sell Signal Basic · Buy Signal Super · Sell Signal Super · Volume Imbalance · Vodka Shot · Take Partial Profit · Take FULL Profit · 9/21 EMA Cross

---

## Differences from the TradingView original

Honest accounting of where this port doesn't match, and why.

**Vector candles are a reimplementation.** The original calls `TradersReality/Traders_Reality_Lib`, an external TradingView library that can't be fetched or linked from Sierra Chart. The PVSRA rules here were rebuilt from the original's own tooltip: climax at ≥200% of the prior 10 bars' average volume or a new 10-bar high in volume×spread, rising at ≥150%. **Verify against TradingView before trusting it**, since Vector candles also feed the Total Recall detector and therefore the take-profit counter.

**The cloud is drawn as one rectangle per bar.** ACSIL has no equivalent of Pine's `fill()`. One rectangle per bar is the only way to get per-bar color and opacity, and it means the cloud is technically a staircase. At normal zoom it reads as a cloud; zoomed way in you'll see steps. For a genuinely smooth fill, set **Draw cloud fill** to No and add Sierra's built-in **Fill Space Between Two Study Subgraph Lines** study pointed at the *Fantail VMA* and *McGinley Dynamic* subgraphs — the tradeoff is a single flat color, which is exactly what `Simple` mode is anyway.

**Outline and wick are colored together.** `plotcandle` colors them separately; Sierra's bar coloring doesn't. Where the two differ, the border color wins.

**Early bars differ slightly.** Recursive averages (EMA, Wilder's RMA, Fantail VMA, McGinley) are seeded differently than Pine seeds them. Expect small differences over roughly the first few hundred bars of a chart, converging to identical after that.

**Dead code was dropped.** The MACD block (never plotted, and "MACD" isn't a candle option), `bTouchedLine` and its four EMAs plus VWAP, `corr`/`momOsc`/`vbcbColor`, and a few unused inputs. Removing them changes no output.

**Three original bugs preserved deliberately**, all commented in the source:
1. `pSellVodka` checks `downwards[1]` and then `upwards[2..4]` — almost certainly a typo, but changing it would change signals.
2. The `upwards` condition ANDs in a raw float (an RMA of RSI gains) that's non-zero on essentially every bar, so it filters nothing.
3. The take-profit label swap described above.

**One bug fixed.** The four "Signal:" toggles under Ultimate Buy Sell (`RSI crossing Basis`, `crossing under 75`, `crossing over 25`, `crossing a MA`) exist in the original but are never actually applied — unchecking them does nothing there. Here they work. All default to Yes, so out-of-the-box behavior is unchanged.

---

## Troubleshooting

**The cloud looks like a row of outlined boxes.** Set **Cloud outline width** to 0.

**Candles look muddy or washed out.** **Chart Background Color** doesn't match your chart.

**Nothing is colored.** Your chart is probably on OHLC bars rather than candlesticks, or `Color Candle Body Fill` / `Color Candle Outline` got turned off.

**Chart is sluggish.** Lower **Cloud: max bars to render**, or turn **Draw cloud fill** off.

**Signals flicker while a bar is forming.** That's expected with **Evaluate Signals Only On Bar Close** set to No. Set it to Yes.

**Almost no signals appear.** Check the ADX threshold and whether you're on a timeframe the defaults weren't tuned for (they target 30-minute bars).

---

## Credits

Nebula was created by **TraderOracle** (DaveTrade55 on TradingView) — [YouTube](https://www.youtube.com/@traderoracle).

Component authors credited in the original source: **LazyBear** (Waddah Attar Explosion), **Aaron D** (Tidal Wave / Wave theory), **LuxAlgo** (Reversal Signals, Market Structure), **Ankit_1618** (Cumulative Volume Delta), **RedK** (VADER, RSS_WMA), **DGT** (Neglected Volume), **Bixord** (Fantail VMA), **TradersReality** (PVSRA), **Serious Backtester** (Trampoline concept), **@davidclarke6612** (Rational Quadratic Kernel suggestion), and **Daviddtech**, whose video inspired the original combination.

## License

Mozilla Public License 2.0, matching the original Pine source — <https://mozilla.org/MPL/2.0/>
