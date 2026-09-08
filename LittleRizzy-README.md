# Little Rizzy — Sierra Chart Study

An ACSIL custom study that automates the "Little Rizzy" measured-move pattern: it finds the trend line, measures the pattern, projects the target, and tracks the setup until it either reaches that target or is invalidated by a close beyond the line.

Bollinger Bands are drawn alongside as a context gauge, and can optionally gate which setups the study will accept.

https://www.youtube.com/watch?v=AVVM-FyewLg

---

## Table of contents

- [What the study does](#what-the-study-does)
- [Installation](#installation)
- [Anatomy of the pattern](#anatomy-of-the-pattern)
- [Reading the chart](#reading-the-chart)
- [Signals and their exact meaning](#signals-and-their-exact-meaning)
- [A worked example](#a-worked-example)
- [Settings reference](#settings-reference)
- [Tuning guide](#tuning-guide)
- [Timing, lag, and repainting](#timing-lag-and-repainting)
- [Subgraph reference](#subgraph-reference)
- [Alerts](#alerts)
- [Known limitations](#known-limitations)
- [Disclaimer](#disclaimer)

---

## What the study does

The pattern has five parts. The study handles all five:

| Step | What happens |
|---|---|
| **1. Trend identification** | Two consecutive confirmed swing pivots define a sloping trend line. A higher high followed by a **lower high** is bearish; a lower low followed by a **higher low** is bullish. |
| **2. Measurement** | The study finds the pattern extreme — the lowest low (bearish) or highest high (bullish) sitting *between* the two anchors — and measures the vertical distance from that extreme to the trend line directly above/below it. Call this distance **H**. |
| **3. Target projection** | **H** is projected beyond the extreme. Bearish target = `extreme low − H`. Bullish target = `extreme high + H`. |
| **4. Bollinger context** | 20-period, 2 standard deviation bands are plotted. Price near the middle band is "in reality"; price out at a band is stretched. Optionally used as a filter on the confirming pivot. |
| **5. Invalidation** | Once a setup is live, a bar **closing** beyond the trend line kills it. The setup is also closed out when the target is reached. |

There is no entry logic. The study marks the structure and the target; the trigger is left to you. See [Known limitations](#known-limitations).

---

## Installation

1. Copy `LittleRizzy.cpp` into your Sierra Chart `ACS_Source` folder.
   Typically `C:\SierraChart\ACS_Source\`.
2. In Sierra Chart: **Analysis → Build Custom Studies DLL → Build**.
3. Watch the build output window. On success you'll see `LittleRizzy.dll` produced with no errors.
4. Open a chart, then **Analysis → Studies → Add Custom Study**, and choose **Little Rizzy Pattern**.

To update after editing the source, just rebuild — Sierra Chart releases and reloads the DLL automatically. If it complains the DLL is locked, use **Analysis → Release All DLLs and Deny Load** first, then rebuild and re-allow.

**Requirements:** any reasonably current Sierra Chart version with the bundled Visual C++ compiler installed (the default install includes it).

---

## Anatomy of the pattern

A bearish example. Bullish is the exact mirror.

```
  price
    │
    │      ●  A1          first anchor: a swing high
    │       ╲
    │        ╲            ← trend line: a RAY through A1 and A2,
    │         ╲             extended right indefinitely
    │          ╲   ●  A2  second anchor: a LOWER swing high
    │           ╲ ╱ ╲
    │            ╳    ╲
    │           ╱ ╲     ╲ ····· a CLOSE above the ray here = INVALIDATED
    │          │    ╲     ╲
    │          │      ╲     ╲
    │          │  H     ╲     ╲
    │          │          ╲     ╲
    │          ●  E        ╲      ╲   pattern extreme: the lowest low
    │          ┊             ╲       ╲  between A1 and A2
    │          ┊  H (projected)
    │          ┊
    │ ═════════╧══════════════════════  TARGET = E − H
    │
    └──────────────────────────────────────── time
```

- **A1 → A2** is the trend line. It must slope *with* the trend: A2 lower than A1 for bearish, A2 higher than A1 for bullish.
- **E** is the pattern extreme. It is the deepest low **between the two anchors** — the drop that preceded the bounce.
- **H** is measured vertically from E straight up to the trend line at that same bar index, not to A1 or A2.
- **Target** is H carried the same distance past E.

---

## Reading the chart

Once installed, six things appear.

| On the chart | Appearance | Meaning |
|---|---|---|
| **Swing dots** | Small red dots above highs, green dots below lows | Confirmed pivots. Red = swing high, green = swing low. These are the raw structural points the study builds trend lines from. |
| **Trend line** | Solid ray, red (bearish) or green (bullish), extending right forever | The active setup's trend line. This is the invalidation boundary. |
| **Measurement leg** | Solid vertical yellow segment, labelled `H 15.75` | The measured distance from the pattern extreme to the trend line. |
| **Projection leg** | Dashed vertical cyan segment below (or above) the extreme | The same distance carried past the extreme. Its far end is the target. |
| **Target line** | Dashed horizontal cyan line, extending right as the setup ages, labelled `Short TGT 4964.25` | The projected target price. Grows rightward each bar while the setup is live. |
| **Outcome dot** | Large cyan dot = target reached. Large orange dot = invalidated. | Marks the bar where the setup resolved. |

Bollinger Bands are drawn as three grey lines: top, bottom, and a dashed midline named **BB Mid (Reality)**.

Historical setups stay on the chart. Every setup gets its own block of drawing line numbers, so you can scroll back and audit how the study behaved. They're cleared only on a full recalculation (changing a setting, reloading data, or restarting).

---

## Signals and their exact meaning

### A new setup appeared

Both anchors are confirmed pivots, the trend line slopes with the trend, the extreme sits between them, and H is positive. On the bar this happens, you'll see the trend line, both legs, and the target line all render at once.

Note the setup is **not** created if, during the confirmation lag, price had already closed beyond the trend line or already reached the target. Those are discarded silently rather than drawn as instant winners or instant losers.

### Target reached — large cyan dot

For a bearish setup, `Low <= Target` on a closed bar. For bullish, `High >= Target`.

This uses the bar's **low/high**, not the close — the measured move counts as achieved if price traded there at all. The setup then deactivates, and the study will look for a fresh pattern.

### Invalidated — large orange dot

A closed bar's **close** is beyond the trend line: above it for bearish, below it for bullish. Wicks through the line do nothing; only the close matters, which is what the original method specifies.

The orange dot is plotted at the closing price of the offending bar. You can widen the tolerance with *Invalidation Buffer (Ticks)* if you're getting killed by single-tick pokes.

### Nothing on the chart

Either no valid two-pivot structure exists yet, or every candidate was filtered out. Most commonly this is *Pivot Strength* set too high for the timeframe. See [Tuning guide](#tuning-guide).

### Only one setup at a time

By design, the study tracks a single active setup. When a new qualifying pattern forms while one is already live, the default behaviour (*New Pattern Replaces Active Setup* = Yes) is to abandon the old one and adopt the new structure — the newer trend line is the more relevant one. Set it to **No** if you'd rather let a setup run to resolution without being superseded.

---

## A worked example

Bearish, on a hypothetical ES chart with *Pivot Strength* = 3.

| | Bar | Price |
|---|---|---|
| A1 — swing high | 100 | 5000.00 |
| E — lowest low between anchors | 108 | 4980.00 |
| A2 — lower swing high | 115 | 4992.00 |

The study computes:

```
slope        = (4992.00 − 5000.00) / (115 − 100)   = −0.5333 per bar
line at E    = 5000.00 + (−0.5333 × (108 − 100))   = 4995.73
H            = 4995.73 − 4980.00                   = 15.73
target       = 4980.00 − 15.73                     = 4964.27
```

A2 is a pivot at bar 115, so it confirms at **bar 118** (115 + strength of 3). Assuming nothing closed above the line and price never reached 4964.27 during bars 116–118, the setup renders at bar 118 with the target line labelled `Short TGT 4964.25`.

Invalidation is a moving level, because the line slopes. At bar 130 the trend line sits at `5000.00 − 0.5333 × 30 = 4984.00`. A close above 4984.00 on that bar kills the setup. Ten bars later the threshold has dropped to 4978.67 — the line tightens on price as time passes, which is the point of using a sloping line rather than a flat stop.

---

## Settings reference

### Pattern detection

| Setting | Default | Notes |
|---|---|---|
| **Pivot Strength (Bars Each Side)** | 3 | How many bars on each side a pivot must dominate. The single most important setting. Higher = fewer, more structural pivots, and more confirmation lag. |
| **Minimum Bars Between Trend Line Anchors** | 3 | Rejects trend lines drawn between two pivots that are too close together to be meaningful. |
| **Detect Bearish (Lower High) Setups** | Yes | |
| **Detect Bullish (Higher Low) Setups** | Yes | Turn one off to trade with a higher-timeframe bias. |
| **New Pattern Replaces Active Setup** | Yes | See [Only one setup at a time](#only-one-setup-at-a-time). |

### Bollinger Bands

| Setting | Default | Notes |
|---|---|---|
| **Bollinger Bands Length** | 20 | |
| **Bollinger Bands Standard Deviations** | 2.0 | |
| **Bollinger Bands Moving Average Type** | Simple | |
| **Bollinger Bands Input Data** | Last | |
| **Require Confirming Pivot Outside Band** | No | When Yes, a setup is only accepted if the second anchor was stretched to the band. |
| **Outside Band %B Threshold (0-1)** | 0.90 | With the filter on: bearish needs the A2 close in the top 10% of the band, bullish needs the A2 close in the bottom 10%. |

### Measurement and invalidation

| Setting | Default | Notes |
|---|---|---|
| **Re-measure If New Extreme Forms** | No | Off = the target is locked when the pattern confirms. On = a new low (or high) re-measures H and pushes the target further out. |
| **Invalidate On Close Beyond Trend Line** | Yes | Turn off to let setups run to target or forever. |
| **Invalidation Buffer (Ticks)** | 0 | Tolerance added past the line before a close counts as a break. |

### Display

| Setting | Default | Notes |
|---|---|---|
| **Draw Pattern On Chart** | Yes | Master switch for all drawings. Subgraph values still calculate when off. |
| **Draw Trend Line** | Yes | Hides just the ray. Measurement, projection, target and labels still draw, and invalidation logic is unaffected. |
| **Trend Line Opacity (%)** | 100 | 100 = solid, 0 = invisible. |
| **Show Text Labels** | Yes | The `H` and `TGT` text. |
| **Drawing Line Width** | 2 | |
| **Bearish / Bullish / Measurement / Target Color** | — | |
| **Enable Alerts** | No | See [Alerts](#alerts). |

---

## Tuning guide

**Start with Pivot Strength.** This is the setting that determines whether the study draws the lines you would have drawn by hand.

| Value | Behaviour |
|---|---|
| 1–2 | Every small wiggle is a pivot. Lots of setups, most of them noise. |
| **3–5** | Reasonable starting range on intraday charts. |
| 6–10 | Only meaningful structural swings. Fewer setups, longer confirmation lag, cleaner lines. |
| 10+ | Swing/position timeframes. |

The honest way to set it: scroll back over a few hundred bars and compare the study's trend lines against the ones you'd draw yourself. Raise strength until the noise clears; stop before it starts skipping structure you consider real.

**If you get no setups at all**, the usual culprits are Pivot Strength too high for the bar count on screen, or *Minimum Bars Between Trend Line Anchors* set higher than your typical swing spacing.

**If setups die instantly**, add a few ticks of *Invalidation Buffer*. On tick or volume bars especially, a one-tick poke above a trend line closes plenty of bars beyond it without meaning anything.

**The Bollinger filter is off by default** because it's restrictive. Turn it on when you want only setups that started from a stretched, exhausted-looking bounce. Expect the setup count to drop sharply. Lowering the threshold from 0.90 to around 0.75 loosens it considerably.

---

## Timing, lag, and repainting

**Nothing repaints.** Two design decisions guarantee this:

1. A pivot at bar `P` is not confirmed until bar `P + PivotStrength` has closed. The study never marks a pivot it couldn't have known about at the time.
2. All state transitions run on **closed bars only**, via `GetBarHasClosedStatus`. A mid-bar spike through the trend line will not invalidate a setup and then un-invalidate it when price pulls back before the close.

The trade-off is lag. A setup cannot appear until `PivotStrength` bars after its second anchor. With strength 5 on a 5-minute chart, that's 25 minutes after the bounce high printed. This is inherent to any non-repainting pivot method, and it's why the study discards setups that already resolved during the confirmation window rather than reporting them as immediate hits.

Bollinger Bands are the exception — they update on the forming bar, since they're context rather than signal.

---

## Subgraph reference

Useful if you're feeding this into a Spreadsheet Study, an alert condition, or another study's input.

| # | Name | Value | Drawn by default |
|---|---|---|---|
| SG1 | BB Top | Upper band price | Yes |
| SG2 | BB Bottom | Lower band price | Yes |
| SG3 | BB Mid (Reality) | Middle band price | Yes |
| SG4 | Swing High | Pivot high price at that bar, else 0 | Yes (dot) |
| SG5 | Swing Low | Pivot low price at that bar, else 0 | Yes (dot) |
| SG6 | Trend Line Value | Active trend line's value at this bar, else 0 | Hidden |
| SG7 | Target | Active target price, else 0 | Hidden |
| SG8 | Target Hit | Target price on the bar it was reached, else 0 | Yes (dot) |
| SG9 | Invalidated | Close price on the bar it broke, else 0 | Yes (dot) |
| SG10 | Percent B | Position within the bands, 0–1 | Hidden |
| SG11 | Setup State | `+1` bullish active, `−1` bearish active, `0` none | Hidden |

`Percent B` and `Setup State` are hidden because plotting a 0–1 value on a price scale would wreck the chart. To see them, add a second copy of the study in its own chart region and set only those subgraphs to a visible draw style.

**Setup State** is the useful one for automation. A transition from `0` to `−1` is a new bearish setup; `−1` back to `0` is a resolution, and you check SG8 and SG9 on that bar to see which kind.

---

## Alerts

Set **Enable Alerts** to Yes, then configure alert sounds under the study's **Alerts** tab.

| Alert # | Fires when |
|---|---|
| 1 | A new setup is created. Message includes direction and target price. |
| 2 | A setup is invalidated by a close beyond the trend line. |
| 3 | A setup reaches its measured-move target. |

Alerts fire only on live bars during real-time updating. Loading historical data or recalculating the study will not spam you with hundreds of past alerts.

---

## Known limitations

Worth understanding before you rely on this.

- **No entry logic.** The source method never defines an entry trigger, so none was invented. The study tells you the structure is present and where the target sits. Whether you enter on the break of the extreme, on a retest of the trend line, or on something else entirely is yours to decide.
- **No stop placement.** Invalidation is a close beyond the trend line, which is a signal, not an order. It also moves every bar. Position sizing off a moving level needs thought.
- **One setup at a time.** Overlapping structures on different scales are common in real price action and this study will only track one. Add a second instance with a different Pivot Strength if you want a multi-timeframe view.
- **The "already resolved" filter is strict.** Setups that reached target or broke the line during the confirmation lag are discarded entirely. On fast timeframes this suppresses a real number of them. If you'd rather see them marked, that check is a single loop in the source and is straightforward to remove.
- **Pivot ties.** When adjacent bars share an identical high, the earlier bar wins. Rarely matters, but it can shift a trend line anchor by a bar on flat, low-volatility data.
- **Transparency depends on the renderer.** The opacity setting may have no visible effect on the GDI renderer. Check **Global Settings → Graphics Settings** if it seems inert; the Draw Trend Line toggle works regardless.
- **Interpretation, not gospel.** The rules here were derived from a spoken description of a discretionary method. Several details — what exactly counts as a trend line anchor, which low is "the low of the pattern" — required judgement calls. They're documented above and in the source comments so you can disagree and change them.

---

## Disclaimer

This is a charting tool, not trading advice. It identifies a geometric pattern and projects a target; it makes no claim about whether that target will be reached. Measured-move projections are not predictions. Test anything you plan to trade on replay and on your own data before risking money on it.

Provided as-is, with no warranty.
