# Candle Gap Lines

A Sierra Chart ACSIL study that draws a right-extending line from every candle gap
between two same-coloured candles, tracks each line until price returns to touch it,
and reports the direction and distance of the nearest unfilled line.

**Version 1.5.0**

---

## What it does

A gap is two candles of the same colour where the second one leaves untraded space
behind it — two greens where the second opens above the first one's close, or two
reds where the second opens below it. The study drops a white horizontal line at
that level and extends it to the right, bar by bar, for as long as price stays away.

When a later candle touches the level, the line stops at exactly that bar and an
alert fires. Gaps that get filled almost immediately are deleted again and never
alert, so what stays on the chart is the set of levels that actually held.

The idea being tested is that price travels from one unfilled gap to the next. The
on-screen readout supports that: it shows which unfilled line is closest, how far
away it is, and a proximity-weighted split between the nearest line above and the
nearest below.

## Why not `DrawLineUntilFutureIntersection`

Sierra's built-in "line until future intersection" tool does most of this, but it
does not tell your study when a line has been terminated, so you cannot alert on a
fill or keep an accurate list of what is still open.

This study manages its own registry instead. Every line lives in a persistent
`std::vector` holding its origin bar, level, drawing number, and state. Drawing is
done with `s_UseTool` / `DRAWING_LINE`, extending `EndIndex` as new bars form and
freezing it at the touching bar on a fill. Because the study owns the bookkeeping,
it always knows exactly which levels are open and where they are.

## Install

1. Copy `CandleGapLines.cpp` into `<SierraChart>\ACS_Source`
2. In Sierra Chart: **Analysis → Build Custom Studies DLL**, pick the file
3. **Analysis → Studies → Add Custom Study**, choose *Candle Gap Lines v1.5.0*

The version number in the study name and in the message log on chart load tells you
which build is actually running.

## Inputs

### Gap detection

| Input | Default | Notes |
|---|---|---|
| Bars Between The Two Gap Candles | 0 | 0 = adjacent candles. Set to 1 for a classic three-candle imbalance, higher for wider structures. |
| Gap Measured Between | Bodies | Bodies uses open/close, Wicks uses high/low. On continuous intraday data adjacent *wick* gaps are almost nonexistent, which is why bodies is the default. |
| Minimum Gap Size (Ticks) | 1 | Filters small gaps. |
| Require Both Candles Same Color | Yes | Also requires direction to match colour: a green pair may only gap up, a red pair only down. |
| Bars In Between May Not Cross The Gap | No | Only relevant when *Bars Between* > 0. Leave off for imbalances — the middle candle always crosses the gap by definition. Turn on for true untraded gaps. |

### Lines

| Input | Default | Notes |
|---|---|---|
| Line Level | Near edge | Near edge = first touch (equals the second candle's open in Bodies mode). Far edge = full fill. Both = two independently terminating lines per gap. |
| Line Color | White | |
| Line Width | 1 | |
| Extend Active Lines Past Last Bar (Bars) | 0 | Extra bars of extension into empty space to the right. |
| Delete Line If Filled Within This Many Bars | 1 | A line touched inside this window is removed from the chart and issues no alerts. 0 disables. |
| Keep Terminated Line Visible | Yes | Off removes the line entirely on fill instead of leaving the capped segment. |
| Maximum Lines To Track | 500 | Oldest terminated lines are pruned past this count. Active lines are never pruned. |

### Alerts

| Input | Default | Notes |
|---|---|---|
| Enable Alerts | Yes | |
| Alert Sound Number - New Gap | 1 | Sound numbers come from Global Settings → General Settings → Alerts. 0 = silent. |
| Alert Sound Number - Line Filled | 2 | |
| Minimum Bars Between Gap And Fill To Alert | 4 | Fills that happen sooner terminate the line silently. |

### Display

| Input | Default |
|---|---|
| Show Nearest Line Direction Display | Yes |
| Display Position - Bars Right Of Last Bar | 4 |
| Display Position - Ticks Above Last Price | 40 |
| Gap Between Arrow And Text (Ticks) | 10 |
| Arrow Font Size | 24 |
| Text Font Size | 12 |
| Display Color - Up | Green |
| Display Color - Down | Red |

### Diagnostics

| Input | Default | Notes |
|---|---|---|
| Diagnostics To Message Log | Yes | One summary line per bulk pass. |
| Write Events To Message Log | No | Logs individual gap and fill events. |

## Reading the display

```
                    ▲
UP  12 ticks to 5432.25  |  up 68%  dn 32%
```

Green arrow up, red arrow down, pointing at whichever unfilled line is nearest to
the last price.

The percentages are an inverse-distance split between the closest unfilled line
above and the closest below:

```
P(up) = DistanceBelow / (DistanceAbove + DistanceBelow)
```

A line 10 ticks above and one 30 ticks below gives up 75%. When only one side has an
open line it reads 100%.

**This is a restatement of proximity, not a measured hit rate.** It will always call
the nearer line more likely, by exactly the ratio of the distances, whether or not
that holds in your data. Treat it as a display of relative distance until it has
been validated against actual outcomes.

## Subgraphs

All are `DRAWSTYLE_IGNORE` — invisible on the chart, available to spreadsheet studies
and alert conditions.

| # | Name | Values |
|---|---|---|
| 0 | Active Line Count | Number of unfilled lines |
| 1 | New Gap Flag | 1 up gap, -1 down gap, at the gap bar |
| 2 | Line Filled Flag | 1 or -1 at the bar that filled a line |
| 3 | Next Line Direction | 1 up, -1 down, 0 none |
| 4 | Next Line Distance (Ticks) | Ticks to the nearest unfilled line |
| 5 | Up Probability (Percent) | 0–100, inverse-distance |

## Troubleshooting

**No lines appear.** Check the message log after a chart reload for the diagnostic
line:

```
Candle Gap Lines v1.5.0: scanned 4312 bars | raw gaps 87 | rejected: color 41,
size 0, crossed 0 | queued 46 | drawn 44 | discarded as quick fill 2 |
largest gap 6 ticks
```

- `raw gaps 0` — no gaps exist under the current definition. Try Bodies instead of
  Wicks, or raise *Bars Between* to 1.
- most rejected by `color` — the colour/direction rule is doing the filtering.
- most rejected by `size` — lower *Minimum Gap Size*; `largest gap` tells you what
  is actually available.
- most rejected by `crossed` — turn off *Bars In Between May Not Cross The Gap*.
- `queued` high but `drawn` low — the quick-fill window is removing them.

**Changed defaults don't take effect.** Sierra keeps the input values saved with the
study instance. New defaults only apply to newly added instances — either edit the
inputs by hand or remove and re-add the study.

## Notes on behaviour

Gaps are only detected on closed bars, since a still-forming bar can extend its
range and erase the gap. Fill detection *does* run on the forming bar, because a
touch there is permanent.

The gap alert waits for the line to survive the quick-fill window, so it arrives one
bar after the gap when that input is 1. This is unavoidable: you cannot know a line
survived the next bar until the next bar exists. Set the input to 0 for an immediate
alert, accepting that some alerted lines will later be deleted.

On a historical recalculation the outcome of every gap is already known, so
quick-filled gaps are never drawn at all. The draw-then-delete path only runs live,
which avoids thousands of pointless drawing calls on chart load.

## Version history

| Version | Changes |
|---|---|
| 1.5.0 | Nearest unfilled line display: arrow, text, distance, inverse-distance odds. Subgraphs 3–5. |
| 1.4.0 | Lines filled within N bars are deleted and issue no alerts. Gap alert waits for survival. Historical passes skip draw-then-delete. |
| 1.3.0 | Defaults aligned to place the line on the second candle's open. Colour test now requires direction to match colour. |
| 1.2.0 | Fill alerts suppressed when too few bars separate gap from fill. |
| 1.1.0 | Selectable wick/body gap source. Intrusion rule made optional and defaulted off. Diagnostic summary added. |
| 1.0.1 | Build fix: drawing removal uses `sc.DeleteACSChartDrawing`. |
| 1.0.0 | Initial version. |

## Ideas not yet built

- **Empirical hit rate.** Everything needed to test the theory is already tracked.
  Each time a line fills, the distance to the opposite side at the moment of
  creation is known. Tallying how often the nearer line was hit first, bucketed by
  distance ratio, would give a real probability to compare against the naive split.
- Discord webhook on gap and fill events.
- Drawing the gap as a filled zone rather than a single line.
