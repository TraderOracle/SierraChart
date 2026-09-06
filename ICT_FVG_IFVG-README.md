# ICT Fair Value Gaps and Inverse

A Sierra Chart ACSIL study that plots Fair Value Gaps (FVG) and automatically flips them into Inverse Fair Value Gaps (IFVG) once price closes through them.

Zones are drawn as shaded rectangles that extend to the right edge of the chart, fade to grey once they've been filled, and change color when they invert. Everything is evaluated on closed bars only, so a zone never appears mid-bar and then vanishes.

## Contents

- [What it draws](#what-it-draws)
- [Zone life cycle](#zone-life-cycle)
- [Installation](#installation)
- [Inputs](#inputs)
- [Colors](#colors)
- [Alerts](#alerts)
- [Notes and limitations](#notes-and-limitations)

## What it draws

A Fair Value Gap is a three-bar imbalance — a range that price moved through so quickly that the wicks of the outer two bars never overlapped.

**Bullish FVG** — `Low[i] > High[i-2]`
The gap sits between those two levels and acts as a support zone.
Box spans `High[i-2]` to `Low[i]`.

**Bearish FVG** — `High[i] < Low[i-2]`
The gap acts as a resistance zone.
Box spans `High[i]` to `Low[i-2]`.

**Inverse FVG** — an FVG that price has *closed* completely through. It flips polarity:

| Original | Closed through | Becomes |
|---|---|---|
| Bullish FVG | Close below the bottom edge | Resistance (bearish IFVG) |
| Bearish FVG | Close above the top edge | Support (bullish IFVG) |

The IFVG box is drawn starting at the bar where the flip occurred, not back at the original gap, so it reads as a fresh zone. It stays alive until price closes back through it in the other direction.

## Zone life cycle

```
                  tested per mitigation rule
   ACTIVE  ──────────────────────────────────▶  MITIGATED
      │                                             │
      │        close fully through the zone         │
      └──────────────────┬──────────────────────────┘
                         ▼
                     INVERTED  (IFVG, keeps extending right)
                         │
                         │  close back through
                         ▼
                       DEAD  (box freezes)
```

A zone that has been tested is still eligible to invert later — being touched almost always happens before price can close through the far side, so mitigated zones stay in the running.

## Installation

1. Copy `FVG_IFVG_Indicator.cpp` into your Sierra Chart `ACS_Source` folder.
   Typically `C:\SierraChart\ACS_Source\`.
2. In Sierra Chart: **Analysis → Build Custom Studies DLL**, select the file, and build.
3. On a chart: **Analysis → Studies → Add Custom Study**, and pick **ICT Fair Value Gaps and Inverse**.

Requires a Sierra Chart version with a working custom studies build environment. No external dependencies beyond `sierrachart.h`.

## Inputs

### Display

| Input | Default | Notes |
|---|---|---|
| Show Bullish FVGs | Yes | |
| Show Bearish FVGs | Yes | |
| Show Inverse FVGs (IFVG) | Yes | Turning this off retires zones at inversion instead of flipping them |
| Filled Zone Display | Dark Grey | `Hide` / `Dark Grey` / `Medium Grey` / `Normal Color` |
| Draw Zone Outline | No | Border color always matches the fill |
| Fill Transparency (0-100) | 72 | Higher is more transparent |
| Draw 50% Line (CE) | No | Dashed line at consequent encroachment |
| Show Labels | Yes | `FVG` or `IFVG`, pinned to the right edge |
| Label Font Size | 8 | |
| Extend Right Past Last Bar (Bars) | 0 | Needs chart space to the right of the last bar |

### Detection

| Input | Default | Notes |
|---|---|---|
| Minimum Gap Size (Ticks) | 1 | Raise to filter noise — 4–8 is reasonable on fast index futures |
| Min. Middle-Bar Body % of Range | 0 (off) | Set 60–70 to require a real displacement candle |
| Mitigation Rule | Touch Near Edge | `Touch Near Edge` / `Reach 50% (CE)` / `Full Fill` |
| Maximum Zones Kept | 60 | Oldest zones are pruned past this count |

## Colors

All eight colors are editable under **Settings → Subgraphs**. The subgraphs use `DRAWSTYLE_IGNORE` — they exist only as color pickers and plot nothing themselves.

| Subgraph | Default RGB |
|---|---|
| Bullish FVG (support) | `0, 190, 140` |
| Bearish FVG (resistance) | `225, 70, 90` |
| Bullish IFVG (support) | `90, 170, 255` |
| Bearish IFVG (resistance) | `255, 165, 60` |
| Filled Zone – Dark Grey | `70, 70, 70` |
| Filled Zone – Medium Grey | `140, 140, 140` |
| 50% Line (CE) | `160, 160, 160` |
| Label Text | `200, 200, 200` |

## Alerts

All alerts are off by default and each group has its own sound number, so they're distinguishable by ear.

| Alert | Default sound | Fires when |
|---|---|---|
| New FVG | 1 | A gap is created |
| Inversion (IFVG) | 1 | A gap is closed through and flips |
| Full Wick Sweep | 2 | See below |
| 50% Wick Tap | 3 | See below |

### Wick rejection alerts

These two catch the case where a zone is probed and holds — the wick reaches in, but the entire candle body (both open and close) stays on the side price approached from.

For a bullish FVG on zone `[Bottom, Top]`:

**Full Wick Sweep** — `Low <= Bottom` and `min(Open, Close) >= Top`
The wick cleared the whole gap; the body never entered it.

**50% Wick Tap** — `Low <= Mid`, `Low > Bottom`, and `min(Open, Close) >= Top`
The wick reached consequent encroachment and reversed without filling the gap.

Bearish zones are the mirror image. The two are mutually exclusive — a full sweep suppresses the 50% alert on that zone, since a wick clearing the far edge necessarily crossed the midline first.

Each event fires **once per zone**. Without that, a zone wicked repeatedly would alert every time it's touched. The flags are set while processing historical bars too, so loading a chart doesn't replay old events.

Note that "body outside" here means outside on the origin side. A body that closes past the *far* edge is an inversion, which the IFVG alert already covers.

## Notes and limitations

**Closed bars only.** Detection, mitigation, inversion, and all alerts are evaluated on the last closed bar. Nothing repaints, but signals arrive one bar later than an intrabar implementation would produce them.

**Box start.** The FVG box spans all three bars of the pattern. To start it at the middle candle instead, change `G.StartIndex = i - 2;` to `i - 1`.

**Alert wording.** Alert messages say "bullish"/"bearish" and "support"/"resistance" even though the on-chart labels don't, since a sound or popup has no color to read.

**Input ordering.** Newer inputs were appended to the end of the list rather than inserted, to avoid shifting saved study settings on existing charts. Hence the alert settings aren't all grouped together in the UI.

**Renaming the DLL.** Sierra Chart uses `SCDLLName` as the DLL's identity. Changing it produces a new DLL file rather than replacing the old one; charts already running the study stay bound to the old DLL. Remove the study from your charts, rebuild, and delete the stale DLL from the Data folder.

## License

MIT.

## Disclaimer

This is a charting tool, not trading advice. Fair Value Gaps are a discretionary concept with no standardized definition — the specific rules here (what counts as mitigation, when a zone dies, how inversions are drawn) are choices, and other implementations make them differently. Test against your own instruments and timeframes before relying on it.
