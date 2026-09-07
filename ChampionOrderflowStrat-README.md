# RLC Framework — Regime · Location · Confirmation

A Sierra Chart custom study (ACSIL / C++) that encodes a four-stage discretionary order-flow
process into one indicator: define the **environment**, pick the **location**, wait for order-flow
**confirmation**, then mark the **entry, stop, targets and trailing stop**.

The study marks, measures and alerts. **It does not send orders.**

---

## Table of contents

1. [Quick start](#1-quick-start)
2. [What you see on the chart](#2-what-you-see-on-the-chart)
3. [The dashboard](#3-the-dashboard)
4. [Values Window fields](#4-values-window-fields)
5. [Strategy summary](#5-strategy-summary)
6. [Strategy detail — exactly what the code checks](#6-strategy-detail--exactly-what-the-code-checks)
7. [Input reference](#7-input-reference)
8. [Alerts](#8-alerts)
9. [Suggested workflow](#9-suggested-workflow)
10. [Limitations and honest caveats](#10-limitations-and-honest-caveats)
11. [Troubleshooting](#11-troubleshooting)

---

## 1. Quick start

**Requirements**

- Sierra Chart with an intraday chart (1-minute to 5-minute works well for the signal chart).
- A data feed that supplies **bid/ask volume** (Denali, Rithmic, CQG, etc.). The study reads the
  chart's Volume at Price records; without bid/ask volume there are no imbalances and no delta.
- Volume at Price data enabled. The study sets `sc.MaintainVolumeAtPriceData = 1` itself, but the
  chart still needs enough days of intraday data loaded.

**Install**

1. Copy `RLC_Framework.cpp` into your Sierra Chart `ACS_Source` folder
   (usually `C:\SierraChart\ACS_Source\`).
2. In Sierra Chart: **Analysis → Build Custom Studies DLL → Build**
   (or **Remote Build** if you have no local compiler).
3. Watch the Message Log for `Build succeeded`.
4. On your chart: **Analysis → Studies → Add Custom Study →
   "RLC Framework (Regime / Location / Confirmation)"**.

**First settings to touch**

| Setting | Why |
|---|---|
| `6. Filters: Session Start / End Time` | Defaults are 09:30–15:45 (US equity RTH). Change for your instrument and time zone. |
| `4. Confirmation: Imbalance Minimum Volume per Level` | Default 10. Too low on a thin instrument produces noise; too high on a thin instrument produces nothing. |
| `2. Environment: Structure Source Chart Number` | `0` uses the current chart. Point it at your 1-hour or 4-hour chart number to define structure on the higher time frame, as intended. |
| `2. Environment: Gamma (GEX) Mode` | Off by default. See §6.1 — this is the one input the study cannot compute for you. |

---

## 2. What you see on the chart

Everything below is drawn on the main price graph.

### Location — the volume profile

| Plot | Style | Meaning |
|---|---|---|
| **Point of Control** | thick yellow line | Highest-volume price of the developing session. The default "fair value" magnet and a common first target. |
| **Value Area High** | light blue dashes | Upper edge of the developing session value area (70% of volume by default). Above this is **premium**. |
| **Value Area Low** | light blue dashes | Lower edge. Below this is **discount**. |
| **Prior Session VAH / VAL / POC** | dim blue / dim gold dashes | Yesterday's finished profile, carried forward. Optional (`7. Display: Show Prior Session Levels`). |

These are **developing** values built from closed bars of the current trading day, so they extend and
shift during the session, exactly like a developing volume profile.

### Location — the golden pocket

| Plot | Style | Meaning |
|---|---|---|
| **Fib 0.705** | green line | Shallow edge of the deep-retracement zone. |
| **Fib 0.786** | cyan line | Mid reference. |
| **Fib 0.886** | green line | Deep edge of the zone. |
| **Zone rectangle** | translucent green (long) / red (short) box | The area between 0.705 and 0.886 — where the study will look for business. |

The zone is measured on the most recent completed swing leg. For a long it runs from the swing low up
to the swing high, and retraces downward; for a short it is mirrored. If no leg qualifies, the lines
and box disappear — that is normal and means "no location".

### Confirmation and management

| Plot | Style | Meaning |
|---|---|---|
| **Absorption Bar** | large orange dot below (long) or above (short) the bar | Aggressive participation that failed. The dot sits at the bar's extreme — that extreme becomes the invalidation point. |
| **Long Trigger** | green up arrow below the bar | Dominance shifted. All conditions met at the close of that bar. |
| **Short Trigger** | red down arrow above the bar | As above, mirrored. |
| **Entry** | thin white line | Close of the trigger bar. |
| **Stop (invalidation)** | thick red line | Beyond the failed attempt, plus a tick buffer. If price breaks it, the idea is wrong. |
| **Target 1** | light blue line | Default 1.5R. |
| **Target 2** | teal line | Default 2.0R. |
| **Trailing Stop** | orange dashes | Only appears once the trade has reached the activation threshold (default 1R). Once active it replaces the fixed stop when it is more protective. |

The management lines appear at the trigger bar and extend to the right until the trade is resolved
(stop, trail, or Target 2). Then they stop, and the study starts looking for the next setup.

---

## 3. The dashboard

A six-line status block, drawn at a fixed screen position (default upper-left, movable via the
`Dashboard Horizontal/Vertical Position` inputs):

```
RLC FRAMEWORK
1 ENVIRONMENT   structure: VALUE-UP   gamma: POSITIVE (dampening)
2 LOCATION      POC 5732.25   VAH 5738.50   VAL 5724.75
                zone: 5716.50 - 5721.25 (discount)
3 CONFIRMATION  delta -1240   imb B/S 0/3   stacked B/S 0/2
4 STAGE         ABSORPTION SEEN   |   flat
```

Reading it:

- **structure** — `VALUE-UP`, `VALUE-DOWN` or `SIDEWAYS`, from the higher-time-frame source you chose.
- **gamma** — `POSITIVE (dampening)`, `NEGATIVE (amplifying)` or `UNKNOWN`. Unknown simply means you
  have not told the study which regime you are in.
- **POC / VAH / VAL** — developing session values.
- **zone** — the current golden pocket, labelled `discount` or `premium`, or `no valid leg`.
- **delta / imb / stacked** — live order flow of the bar currently forming: net delta, count of
  buy/sell diagonal imbalances, and the longest consecutive run of each.
- **STAGE** — where the setup is in its life cycle:
  - `IDLE` — nothing qualifies.
  - `ARMED (in location)` — environment and location pass and price has traded into the zone.
  - `ABSORPTION SEEN` — a failed aggressive push has been recorded; the study is now waiting for the
    dominance shift.
  - `IN TRADE` — triggered, and the line after the `|` shows entry, stop, targets and whether the
    trail is engaged.

---

## 4. Values Window fields

Hidden data subgraphs. Open **Values Window** (`Ctrl+Alt+V`) and hover a bar:

| Field | Meaning |
|---|---|
| Bar Delta | Ask volume minus bid volume for that bar. |
| Buy Imbalances / Sell Imbalances | Total count of diagonally imbalanced price levels in that bar. |
| Stacked Buy Imb / Stacked Sell Imb | Longest run of *consecutive* imbalanced levels. This is the number the trigger rule uses. |
| Structure (1/0/-1) | 1 = value-up, 0 = sideways, −1 = value-down. |
| Gamma Regime (1/0/-1) | 1 = positive, 0 = unknown, −1 = negative. |
| Setup Stage (0-3) | 0 idle, 1 armed, 2 absorption, 3 in trade. |

These are also what you would reference from a Spreadsheet study or another custom study.

---

## 5. Strategy summary

The process is four sequential filters. A trade only exists when all four agree.

**1. Environment — should I be trading at all?**
Establish the higher-time-frame structure (value-up, value-down, sideways) and the gamma (GEX)
regime. Positive gamma implies dealers sell rips and buy dips — volatility is dampened and fading
extremes is the favoured behaviour. Negative gamma implies the opposite: moves get amplified, and
fading them is expensive. This step is about *not* trading in unfavourable conditions.

**2. Location — where do I want to do business?**
Not in the middle of the range. In a value-up structure the study looks only at **discount** —
below the Value Area Low. It then requires a deep Fibonacci retracement zone (the 0.705 / 0.786 /
0.886 "golden pocket") that sits *outside* the value area. Location is defined before any order flow
is considered.

**3. Confirmation — are participants behaving the way my idea requires?**
Inside that location, watch for **absorption**: aggressive sellers show up (large negative delta,
sell imbalances) and fail to push price lower. Then wait for the **dominance shift**: delta flips
positive, buying imbalances stack up (400% diagonal imbalances by default), and the bar closes
bullish. The failed attempt is what defines the risk.

**4. Entry, exit, management.**
Enter when the failed attempt is followed by the flip. Stop goes on the other side of the failure —
if price breaks past that point the idea is invalidated, not "just noise". Targets are the POC or a
swing reference, expressed as 1.5R and 2.0R. A trailing stop engages once the trade is 1R in profit
so gains are protected as it works toward target.

Shorts are the exact mirror: value-down structure, premium above the Value Area High, absorption of
aggressive buyers, dominance shift to sellers.

---

## 6. Strategy detail — exactly what the code checks

### 6.1 Environment

**Market structure.** The study finds confirmed swing pivots — a pivot high needs `Structure Pivot
Strength` bars on each side with no higher high, and a pivot low is the mirror. It compares the last
two pivot highs and last two pivot lows:

- higher high **and** higher low → `VALUE-UP`
- lower high **and** lower low → `VALUE-DOWN`
- anything else → `SIDEWAYS`

By default this runs on the chart the study is attached to. Set `Structure Source Chart Number` to
your 1-hour or 4-hour chart number and it will read that chart instead, mapping each signal-chart bar
to its containing higher-time-frame bar. That matches the intent of the original process: structure
is a higher-time-frame question.

The `Structure Filter` input controls how strictly this is enforced:

| Setting | Longs allowed when | Shorts allowed when |
|---|---|---|
| Off | always | always |
| Require Aligned Structure (default) | structure is VALUE-UP | structure is VALUE-DOWN |
| Aligned or Sideways | structure is not VALUE-DOWN | structure is not VALUE-UP |

**Gamma / GEX.** Sierra Chart has no options-chain gamma feed, so the study cannot compute "naive
GEX" for you. It gives you three honest ways to supply it:

- **Force Positive / Force Negative** — you looked at your GEX source before the open and told the
  study what regime you are in. This is the intended everyday use.
- **Auto from Gamma Flip Level** — you enter the gamma flip (zero-gamma) price. Above it the study
  treats the regime as positive; below it, negative. A crude but useful proxy that updates itself.
- **Off** — regime is `UNKNOWN`.

`Required Gamma Regime` then gates trades: `Any` (default), `Positive Only`, or `Negative Only`.
Note that `Positive Only` combined with `Off` blocks everything, because the regime is unknown — that
combination is intentional, not a bug.

**Session filter.** Bars outside `Session Start`–`Session End` cannot arm a setup or trigger. An
already-open trade is still managed to its conclusion.

### 6.2 Location

**Session profile.** The study builds its own volume profile from the chart's Volume at Price
records, one trading day at a time, using `sc.GetTradingDayStartDateTimeOfBar` for the session
boundary (so it respects your futures session settings). Only **closed** bars are included, which is
why the values never flicker mid-bar.

POC is the highest-volume price. The value area expands outward from the POC one row at a time,
always taking the heavier of the row above and the row below, until `Value Area Percent` (default 70)
of session volume is enclosed. The top and bottom rows of that set become VAH and VAL. When a new
session begins, the finished profile is stored as the prior-session levels.

**Discount / premium.** `Location: Profile Used for Discount/Premium` selects which profile defines
value: the developing session, the prior session, or `Either` (which takes the lower VAL and the
higher VAH — the more conservative definition of "outside value").

**The retracement leg.** For longs, the study finds the most recent confirmed pivot high, then the
most recent confirmed pivot low before it, then takes the true lowest low between them as the leg
low. Leg = low → high. The leg is discarded if it is shorter than `Minimum Leg Size (ticks)` or if
price has already traded below the leg low (the leg is dead — a fresh one will form).

**The zone.** Retracement prices are `high − range × ratio` for the three fib ratios. The zone is the
band between the 0.705 and 0.886 levels. Shorts mirror this from the leg high downward.

**The outside-value requirement.** With `Require Fib Zone Outside Value Area` set to Yes (default), a
long zone is only valid if its **top** (0.705) is at or below VAL, within `Value Area Tolerance`
ticks. A short zone needs its bottom at or above VAH. This is the rule that keeps the study from
taking trades in the middle of the range — it is the single most important filter in the location
stage, and turning it off changes the character of the strategy substantially.

### 6.3 Confirmation

All order flow is computed from the bar's footprint (Volume at Price rows with bid and ask volume).

**Delta** — ask volume minus bid volume, summed over all price rows of the bar.

**Diagonal imbalance** — the standard footprint definition:

- a **buy imbalance** at price *P* when `Ask(P) ≥ ratio × Bid(P − 1 tick)`
- a **sell imbalance** at price *P* when `Bid(P) ≥ ratio × Ask(P + 1 tick)`

`ratio` is `Imbalance Ratio (percent) / 100`, default 400% = 4.0. A level only counts if its volume
reaches `Imbalance Minimum Volume per Level` (default 10), which keeps single-lot noise out. If the
diagonal neighbour has zero volume, the level counts as imbalanced provided the minimum-volume test
passes. The study also tracks the longest run of consecutive imbalanced levels — **stacked**
imbalances — which is the number the trigger rule uses.

**Stage 1 — ARMED.** Environment and location pass, and the bar's low has traded into the long zone
(or the bar's high into the short zone).

**Stage 2 — ABSORPTION.** On a bar that traded in the zone, all of:

- Aggressive selling: bar delta ≤ −threshold, *or* at least one stacked sell imbalance with negative
  delta. The threshold is `Absorption Minimum |Delta|` if you set it, otherwise it is computed
  automatically as `Auto Delta Threshold × average |delta| of the last 20 bars` — so it adapts to the
  instrument and to the time of day.
- Failure: the close sits at or above `Absorption Close Position in Bar Range` of the bar's range
  (default 0.5 — the upper half). Sellers pressed and could not close it on the lows.
- If `Absorption Requires Opposing Imbalance` is Yes, at least one sell imbalance must be present —
  proof the selling was aggressive rather than passive.

The bar's low is stored as the **failure low**. An orange dot marks it. Shorts mirror everything.

**Stage 3 — TRIGGER.** On a *later* bar, within `Max Bars from Absorption to Trigger` (default 6),
all of:

- the bar closes above its open and above the previous close (it "flips back to bullish");
- delta is positive (dominance has shifted);
- stacked buy imbalances ≥ `Minimum Stacked Imbalances to Trigger`, or total buy imbalances meet the
  same count;
- optionally (`Trigger Must Close Beyond Absorption Bar`, default No) the close is above the
  absorption bar's high.

**Invalidation before trigger.** If a close prints below `failure low − stop buffer`, or the bar
budget expires, the setup resets to IDLE. The zone stays on the chart and can arm again.

### 6.4 Entry, stop, targets, trailing

| Item | Rule |
|---|---|
| **Entry** | Close of the trigger bar. |
| **Stop** | `failure low − Stop Buffer ticks` for longs, mirrored for shorts. This is the "other side of the failed attempt" — break it and the idea is gone. |
| **R** | `entry − stop`. Every target is expressed in this unit. |
| **Target 1** | `entry + 1.5R` by default. Marked and tracked; the study notes when it is reached. |
| **Target 2** | `entry + 2.0R` by default. Reaching it closes the tracked trade. |
| **Trailing stop** | Engages once the best price since entry reaches `Activate Trailing Stop at (R multiple)` (default 1.0R). It then trails to `lowest low of the last N bars − buffer` (N = `Trailing Stop Swing Lookback`, default 3), and only ever moves in the favourable direction. When it is more protective than the fixed stop, it becomes the stop. |
| **Trade end** | Stop or trail is touched, or Target 2 is reached. The study then resets and looks for the next setup. |

Use POC and the prior-session levels already on the chart as your discretionary target reference —
the original process targets swing highs or the POC, and both are plotted.

`Max Setups Per Session` (0 = unlimited) caps how many triggers the study will produce per trading
day, which is a useful discipline setting.

### 6.5 Why signals do not repaint

Everything that produces a marker or an alert is evaluated **once, on a closed bar**. The study
evaluates bar *N−1* when bar *N* opens. Live delta and imbalance counts for the currently forming bar
are still updated tick by tick and shown on the dashboard and in the Values Window, but they cannot
create or remove a signal. A green arrow that appears will not disappear.

The consequence: a trigger arrow appears when the trigger bar *closes*, not while it is forming.
That is deliberate — the alternative flickers.

---

## 7. Input reference

Inputs are grouped by number prefix so they read in order in the settings window.

**1. Enable**

| Input | Default | Notes |
|---|---|---|
| Enable Long Setups | Yes | |
| Enable Short Setups | Yes | |

**2. Environment**

| Input | Default | Notes |
|---|---|---|
| Structure Source Chart Number | 0 | 0 = this chart. Use your 1H/4H chart number. |
| Structure Pivot Strength | 5 | Bars each side of a swing. Larger = fewer, more significant swings. |
| Structure Lookback | 300 | How far back to search for the last two pivots. |
| Structure Filter | Require Aligned Structure | See §6.1. |
| Gamma (GEX) Mode | Off | Off / Force Positive / Force Negative / Auto from Flip Level. |
| Gamma Flip Level | 0 | Only used in Auto mode. |
| Required Gamma Regime | Any | Any / Positive Only / Negative Only. |

**3. Location**

| Input | Default | Notes |
|---|---|---|
| Value Area Percent | 70 | |
| Profile Used for Discount/Premium | Developing Session | Developing / Prior / Either. |
| Require Fib Zone Outside Value Area | Yes | The core location rule. |
| Value Area Tolerance (ticks) | 2 | Slack on the above test. |
| Retracement Leg Pivot Strength | 3 | Smaller than the structure strength — this is the trading leg. |
| Fib Level 1 / 2 / 3 | 0.705 / 0.786 / 0.886 | Levels 1 and 3 are the zone edges (order does not matter, they are sorted). |
| Minimum Leg Size (ticks) | 8 | Rejects legs too small to be worth trading. |
| Leg Search Lookback (bars) | 150 | |

**4. Confirmation**

| Input | Default | Notes |
|---|---|---|
| Diagonal Imbalance Ratio (percent) | 400 | |
| Imbalance Minimum Volume per Level | 10 | Raise on liquid futures, lower on thin ones. |
| Minimum Stacked Imbalances to Trigger | 2 | |
| Absorption Minimum \|Delta\| | 0 (auto) | Set a fixed contract count to override the adaptive threshold. |
| Auto Delta Threshold (× 20-bar average) | 1.0 | Raise to demand more extreme absorption. |
| Absorption Close Position in Bar Range | 0.5 | 0.5 = close in the upper half for a long. |
| Absorption Requires Opposing Imbalance | Yes | |
| Max Bars from Absorption to Trigger | 6 | |
| Trigger Must Close Beyond Absorption Bar | No | Yes = stricter, later entries, wider stops. |

**5. Management**

| Input | Default |
|---|---|
| Stop Buffer (ticks) | 2 |
| Target 1 (R multiple) | 1.5 |
| Target 2 (R multiple) | 2.0 |
| Activate Trailing Stop at (R multiple) | 1.0 |
| Trailing Stop Swing Lookback (bars) | 3 |
| Trailing Stop Buffer (ticks) | 2 |

**6. Filters**

| Input | Default |
|---|---|
| Use Session Time Filter | Yes |
| Session Start Time | 09:30:00 |
| Session End Time | 15:45:00 |
| Max Setups Per Session | 0 (unlimited) |

**7. Display**

| Input | Default |
|---|---|
| Enable Alerts | Yes |
| Draw Golden Pocket Zone | Yes |
| Show Prior Session Levels | Yes |
| Show Status Dashboard | Yes |
| Dashboard Horizontal Position | 20 |
| Dashboard Vertical Position | 92 |

---

## 8. Alerts

With `Enable Alerts` on, the study raises a Sierra Chart alert (alert number 1 — set the sound in
**Chart Settings → Alerts**) at four moments:

1. **Absorption detected** — includes the failure price and the bar's delta. This is your "get ready"
   alert.
2. **Trigger** — `RLC LONG TRIGGER entry … stop … T1 … T2 … (R=…)`. Everything you need to place the
   order is in the message.
3. **Stop or trail hit** — the tracked trade closed against you or the trail took you out.
4. **Final target reached** — Target 2.

Alerts fire once per bar and are suppressed during full chart recalculations, so reloading history
will not spam you.

---

## 9. Suggested workflow

**Before the open.** Set `Gamma (GEX) Mode` from whatever GEX source you use — Force Positive or
Force Negative, or enter today's gamma flip level and use Auto. Glance at the dashboard's structure
line and confirm it matches what you see on your own higher-time-frame chart. If structure is
`SIDEWAYS` and you are running the default filter, expect a quiet day from the study; that is the
filter doing its job.

**During the session.** Watch the STAGE line.

- `IDLE` → nothing to do.
- `ARMED` → price is in your location. Start paying attention to the footprint.
- `ABSORPTION SEEN` → the failed attempt is on the chart with an orange dot. Now you are waiting for
  one thing only: the flip. Your risk is already defined — it is the dot.
- Green or red arrow → the trigger. The alert carries entry, stop and both targets.

**Managing.** The stop line is your invalidation. When the orange trail line appears, the trade has
reached 1R and the study is now protecting it. Take partials at Target 1 if that suits you — the
study tracks it but does not assume it.

**Reviewing.** Scroll back through past sessions. Every absorption dot, arrow, stop and target is
drawn historically with the same rules, so you can audit how the framework behaved before you trade
it. Pay particular attention to absorption dots that never produced an arrow — those are the setups
the confirmation stage correctly refused.

---

## 10. Limitations and honest caveats

- **This is not a trading system and not financial advice.** It is a mechanical rendering of a
  discretionary framework. Discretion was doing real work in the original process — particularly in
  reading *how* participants behaved, which no threshold captures fully.
- **GEX is an input, not a computation.** Sierra Chart has no options-chain gamma feed. The study
  can only apply the regime you give it. The gamma-flip proxy is crude.
- **Order flow quality depends on your feed.** Bid/ask volume assignment differs between feeds, and
  imbalance counts are not comparable across data sources. Tune the thresholds on your own data.
- **One tracked trade at a time.** While a trade is open the study will not arm the opposite
  direction. This is a display and discipline choice, not a market claim.
- **Historical marks assume you took every signal exactly at the close** of the trigger bar with no
  slippage and no partial fills. Real results will differ.
- **The state machine is sequential.** On a full recalculation it rebuilds from the first bar of
  loaded data, so signals near the very start of the chart may be missing until the profile and
  pivots have enough history.
- **Structure detection is deliberately simple** — two swing highs and two swing lows. It will label
  some complex ranges as trending. Use it as a filter, not as a market opinion.

---

## 11. Troubleshooting

**"No Volume at Price data on this chart" in the Message Log.**
The chart has no VAP records. Use an intraday chart, make sure your feed supplies bid/ask volume, and
that intraday data is actually downloaded for the days shown.

**No lines appear at all.**
The profile needs at least one closed bar of the current session. If you have just added the study to
a chart at the session open, wait one bar.

**The fib lines and zone keep disappearing.**
That means no leg currently qualifies — usually because price broke the leg low (or high), or because
the leg is smaller than `Minimum Leg Size`. Lower the leg pivot strength or the minimum size if it
happens constantly on your time frame.

**Never any arrows.**
Work down the stages using the dashboard. If STAGE never leaves `IDLE`, the block is in environment
or location — try `Structure Filter = Off` and `Require Fib Zone Outside Value Area = No` temporarily
to confirm, then re-enable them one at a time. If STAGE reaches `ARMED` but never `ABSORPTION SEEN`,
lower `Auto Delta Threshold`. If it reaches `ABSORPTION SEEN` but never triggers, lower `Minimum
Stacked Imbalances` to 1 or raise `Max Bars from Absorption to Trigger`.

**Too many arrows.**
Raise `Imbalance Minimum Volume per Level`, raise `Minimum Stacked Imbalances` to 3, set `Trigger
Must Close Beyond Absorption Bar` to Yes, and set `Max Setups Per Session` to 2.

**Compile error on `DRAWSTYLE_ARROW_UP` / `DRAWSTYLE_ARROW_DOWN`.**
Older Sierra Chart builds spell these differently. Replace them with `DRAWSTYLE_POINT` (and raise
`LineWidth`) in the `sc.SetDefaults` block — nothing else changes.

**Compile error on `sc.AddAndManageSingleTextDrawingForStudy`.**
This is the dashboard. The study uses the documented nine-parameter member form, and the dashboard
subgraph is set to `DRAWSTYLE_CUSTOM_TEXT` as that function requires. If a much older build rejects
it, set `#define RLC_ENABLE_DASHBOARD 1` to `0` at the top of the file and rebuild. You lose only the
on-chart status block; every value it shows is still in the Values Window.

**Compile error on `sc.GetContainingIndexForDateTimeIndex`.**
Only used when `Structure Source Chart Number` is non-zero. Note this function takes a **bar index**
on the current chart, not an `SCDateTime` — the SCDateTime equivalent is
`sc.GetContainingIndexForSCDateTime`. Leave the input at 0 and, if it still will not build, delete
the `else` branch that reads the other chart.

**The study is slow on a very large chart.**
Reduce `Structure Lookback` and `Leg Search Lookback`, and load fewer days of intraday data on the
signal chart.
