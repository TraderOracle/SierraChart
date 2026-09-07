# VWAP Pullback Trend Continuation System

A Sierra Chart study for NASDAQ 100 futures (NQ) that draws a VWAP anchored to the 9:30 AM market open, identifies when the market is trending, and marks the first pullback into VWAP as an entry. It can run purely as a visual indicator, or place bracketed orders automatically.

Written in ACSIL (Sierra Chart's C++ study language). Single file, no dependencies.

---

## Contents

- [The idea in one paragraph](#the-idea-in-one-paragraph)
- [What you see on screen](#what-you-see-on-screen)
- [The rules it follows](#the-rules-it-follows)
- [Installation](#installation)
- [Chart setup](#chart-setup)
- [Settings reference](#settings-reference)
- [Running it as an indicator vs. a trading system](#running-it-as-an-indicator-vs-a-trading-system)
- [Backtesting](#backtesting)
- [Troubleshooting](#troubleshooting)
- [Known limitations](#known-limitations)
- [Risk notice](#risk-notice)

---

## The idea in one paragraph

VWAP is the average price everyone paid today, weighted by how much traded at each price. Large institutional orders are frequently benchmarked against it, so when a trending market drifts back toward VWAP, resting institutional interest tends to sit there. The premise of this study is that in a market already trending, a pullback into VWAP is where that interest shows up and pushes price back in the trend direction. The study waits for a trend to be established, waits for the first pullback candle into the VWAP zone, and marks it.

The premise is a premise, not a fact. Test it before you trust it.

---

## What you see on screen

Once the study is added to a 5-minute NQ chart, four things appear.

### 1. The anchored VWAP line

A yellow line running through the price bars. It starts fresh at 9:30 AM every session and ends at 4:00 PM. Outside those hours the line is not drawn at all, which is intentional: overnight volume would distort the average, and the strategy only trades the regular session.

This is not the same as Sierra Chart's built-in VWAP, which typically anchors to the futures session start at 6:00 PM the previous evening. If you have both on the chart they will not overlap, and that is expected.

### 2. Green up arrows

A green arrow appears **below** a candle when that candle is a valid long trigger. In plain terms: the market is trending up, price pulled back to the VWAP line, and that candle was the first red one to reach it.

The arrow appears the moment the candle closes. If the trading system is enabled, the entry goes in on the next candle.

### 3. Red down arrows

A red arrow appears **above** a candle for the mirror-image short setup: market trending down, price rallied back to VWAP, and that candle was the first green one to reach it.

### 4. Messages in the Message Log

Every entry and every losing trade writes a line to **Window >> Message Log**, for example:

```
VWAP Pullback: LONG entry submitted. VWAP 20431.25, trigger bar close 20438.50. Trade 2 of 4 today, 1 loss(es) so far.
VWAP Pullback: losing trade closed (P/L -1600.00). Losses today: 2 of 2.
```

This is how you confirm the guard rails are working. When the log shows 4 trades or 2 losses, the study stops taking setups for the rest of the day even if arrows keep printing.

### What you will *not* see

Arrows only print where every condition lined up at once. On a quiet, range-bound day you may see none at all. That is the filter doing its job, not the study failing. Days with two or three arrows are typical; the guard rails cap it at four trades regardless.

---

## The rules it follows

### Trend must be established first

For a **long** setup, all of these must be true on the candle that closes:

| Condition | Default |
|---|---|
| Price is above the VWAP | required |
| VWAP is higher than it was 15 minutes ago | required |
| Price is up at least 0.10% versus one hour ago | 0.10% |
| Higher time frame trend agrees | on by default |

For a **short** setup, every condition is inverted: price below VWAP, VWAP falling over 15 minutes, price down at least 0.10% over the hour.

The higher time frame check exists because the original strategy calls for a clear trend on the 15-minute chart. The study offers three ways to satisfy that, covered in the settings table below. By default it uses an internal 60-bar moving average of the 5-minute chart, which is roughly a 20-period average on a 15-minute chart, and requires price to be on the correct side of it with the average sloping the right way.

### Then the pullback

With the trend confirmed, the study watches for a candle that comes back to the VWAP line:

- **Long:** a red candle whose low comes within 10 points of VWAP, does not drop more than 5 points below it, and closes back above.
- **Short:** a green candle whose high comes within 10 points of VWAP, does not push more than 5 points above it, and closes back below.

By default only the **first** such candle counts. If price hovers around VWAP for four candles you get one signal, not four. Turning off `Only Take First Pullback Bar` will let it signal on every qualifying candle.

### Entry, stop, and target

The default entry is a market order at the open of the candle after the trigger. The alternative is a stop order placed just beyond the trigger candle's high (long) or low (short), which only fills if price actually moves in your direction, and cancels itself after two candles if it does not.

Both stop loss and profit target are attached to the entry as an OCO bracket, so they sit at the exchange from the moment you are filled. Defaults are 80 points of risk and 45 points of target on NQ.

### Guard rails

| Rule | Default |
|---|---|
| No entries before | 10:30 AM |
| No new entries after | 3:30 PM |
| Position flattened and orders cancelled at | 3:55 PM |
| Maximum trades per day | 4 |
| Maximum losing trades per day | 2 |

Counters reset automatically at the start of each new date. A stop entry order that expires without filling gives its trade slot back.

---

## Installation

1. Download `VWAPPullbackTrendSystem.cpp`.
2. In Sierra Chart, go to **Analysis >> Studies >> Build Custom Studies DLL**.
3. Click **Add Source File**, select the `.cpp` file, and let it copy into your `ACS_Source` folder.
4. Click **Build**. The build window should end with a success message and no errors.
5. Open the chart you want, go to **Analysis >> Studies**, find **VWAP Pullback Trend Continuation System** in the list, and add it.

If the build fails, the compiler message names the exact line. Sierra Chart constant names occasionally differ between versions; see [Troubleshooting](#troubleshooting).

---

## Chart setup

**This matters more than any setting.** Every time in the study is read in the chart's own time zone, not in Eastern time.

- Set **Chart >> Chart Settings >> Advanced Settings >> Time Zone** to `US Eastern`.
- Or leave your chart in local time and shift all five time inputs to match.

Recommended chart:

| Setting | Value |
|---|---|
| Symbol | NQ (front month) |
| Bar period | 5 minutes |
| Session times | include the full 9:30 AM – 4:00 PM regular session |
| Time zone | US Eastern |

The study reads `sc.SecondsPerBar` and converts the 15-minute and 60-minute lookbacks into the right number of bars, so it also works correctly on a 3-minute or 10-minute chart without changing any settings. It is not designed for tick, volume, or range bars.

---

## Settings reference

Open **Analysis >> Studies**, select the study, click **Settings**.

### VWAP and session

| Setting | Default | What it does |
|---|---|---|
| VWAP Price Input | HLC Avg | Which price each bar contributes to the average. HLC/3 is the standard choice. |
| VWAP Anchor Time | 09:30:00 | When VWAP resets each day. |
| Session End Time | 16:00:00 | When VWAP stops updating and the line ends. |

### Trend filter

| Setting | Default | What it does |
|---|---|---|
| VWAP Slope Lookback (Minutes) | 15 | How far back to measure whether VWAP is rising or falling. |
| Momentum Lookback (Minutes) | 60 | The window for the percentage move requirement. |
| Minimum Momentum Move (%) | 0.10 | How far price must have moved over that window. Raise it for fewer, stronger setups. |
| Higher Time Frame Trend Filter | Internal Moving Average | `None` uses only VWAP slope and momentum. `Internal Moving Average` uses the average below. `External Chart Study` reads a study from a separate chart. |
| Internal Trend Average Length | 60 | Length in chart bars. 60 bars on a 5-minute chart is about a 20-period average on a 15-minute chart. |
| External 15-Minute Trend Study | none | If you selected `External Chart Study`, point this at the chart number, study, and subgraph of a moving average on your 15-minute chart. |

### Pullback definition

| Setting | Default | What it does |
|---|---|---|
| Pullback Proximity To VWAP (Points) | 10 | How close the candle must get to VWAP to count. Larger means more signals, further from the line. |
| Maximum VWAP Penetration (Points) | 5 | How far through VWAP price may poke and still count. Larger tolerates deeper pullbacks. |
| Only Take First Pullback Bar | Yes | Signal once per pullback rather than on every qualifying candle. |

### Orders

| Setting | Default | What it does |
|---|---|---|
| Entry Method | Market At Next Bar Open | Or `Stop Through Trigger Bar Extreme`, which waits for confirmation. |
| Stop Entry Offset (Ticks) | 1 | How far beyond the trigger candle the stop entry sits. |
| Stop Entry Order Valid For (Bars) | 2 | Unfilled stop entries cancel after this many bars. |
| Order Quantity | 1 | Contracts per trade. |
| Stop Loss (Points) | 80 | Bracket stop distance from fill. |
| Profit Target (Points) | 45 | Bracket target distance from fill. |

### Guard rails and display

| Setting | Default | What it does |
|---|---|---|
| Earliest Entry Time | 10:30:00 | Skips the opening hour. |
| Latest Entry Time | 15:30:00 | No new positions after this. |
| Flatten And Cancel Time | 15:55:00 | Closes anything still open. |
| Maximum Trades Per Day | 4 | Counted on entry submission. |
| Maximum Losing Trades Per Day | 2 | Counted when a position closes at a loss. |
| Signal Arrow Offset (Ticks) | 8 | How far arrows sit from the candle, cosmetic only. |

---

## Running it as an indicator vs. a trading system

**As an indicator (default).** Add the study and it draws the VWAP and arrows. It will not place live orders: `sc.SendOrdersToTradeService` is set to false in the code, so anything it does goes to the simulated trading engine only.

**As a trading system.** In **Chart Settings >> Trading**, enable trading for the chart and set the chart to Trade Simulation Mode first. Watch it for several sessions and compare the Message Log against what you see on the chart. Only after that should you consider connecting it to a live account, and even then, start at one contract.

To trade it manually, ignore the order settings entirely and treat the arrows as alerts. Everything you need is visible: the arrow tells you the direction, the VWAP line tells you where the reference is, and you place your own bracket.

---

## Backtesting

The study uses manual looping (`sc.AutoLoop = 0`), which means order logic evaluates on the most recent bar. To test it over history, use **Chart Replay**:

1. Right-click the chart, choose **Chart Replay**.
2. Set the start date and choose a replay mode that recalculates the chart bar by bar.
3. Enable Trade Simulation for the chart before starting.
4. Review results in **Trade >> Trade Activity Log** and the trade statistics windows.

Historical arrows drawn during a normal chart load are calculated from closed bars only, so they are not repainted or forward-looking, but they also do not generate simulated fills without replay.

Worth checking before anything else: the default 80-point stop against a 45-point target needs roughly a 64% win rate to break even before commissions. That ratio, not the entry filter, is where the strategy lives or dies. Both numbers are inputs, so vary them and see what the data says.

---

## Troubleshooting

**No arrows anywhere.** Most often the chart time zone is not Eastern, so the 10:30–15:30 window lands on the wrong bars. Check that first. Second most common: the trend filter is never satisfied because the market has been range-bound. Set `Higher Time Frame Trend Filter` to `None` and `Minimum Momentum Move` to `0` temporarily; if arrows appear, the filters were working as designed.

**VWAP line is missing.** It only draws between the anchor time and session end. If your chart is displaying the overnight session, the line will correctly be absent there.

**VWAP does not match Sierra's built-in VWAP.** Expected. This one anchors at 9:30 AM; the built-in one usually anchors to the futures session open the previous evening.

**Build error naming a `DRAWSTYLE_` constant.** Replace `DRAWSTYLE_ARROW_UP` and `DRAWSTYLE_ARROW_DOWN` with `DRAWSTYLE_POINT` in the defaults section.

**Build error naming an `SCT_OSC_` constant.** Order status constant names vary slightly across Sierra Chart versions. The compiler message suggests the correct spelling; use it.

**Trades stop happening mid-afternoon.** Check the Message Log. You have probably hit 4 trades or 2 losses, which is the guard rail working.

---

## Known limitations

- Built for time-based bars. Tick, volume, and range bars will produce nonsense lookback windows.
- The 15-minute trend filter is approximated from the 5-minute chart by default. If you want a true 15-minute reading, use the external chart study mode and point it at a real 15-minute chart.
- Loss counting reads the last closed trade's profit and loss from the chart's position data. If you trade the same account manually on the same chart, your manual trades will be counted too.
- One position at a time. No scaling in, no partial exits, no trailing stop.
- Daily counters reset on calendar date change, which suits a 9:30–16:00 strategy but would need changing for an overnight-session variant.

---

## Risk notice

This is trading software published as-is, with no warranty. It can lose money, it can contain bugs, and a strategy that tested well can stop working without notice. Nothing here is financial advice. Run it in simulation until you understand exactly what it does, and never risk money you cannot afford to lose.

---

## License

MIT. Do what you like with it, at your own risk.
