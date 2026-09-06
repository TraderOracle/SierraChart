# Al Brooks Price Action Suite — Sierra Chart (ACSIL)

A single study implementing Al Brooks' price action method: always-in state,
market cycle, H/L pullback counting, with-trend continuation setups, the major
reversal patterns, quality scoring and alerts.

Defaults are sized for **ES on a 5 minute chart** (0.25 tick, 16-tick max risk,
20-bar EMA, RTH 09:30–16:00 with the midday period skipped) — the chart Brooks
uses in his own trading and in nearly all of his teaching examples.

## Install

1. Copy `AlBrooksPriceActionSuite.cpp` into `<SierraChart>/ACS_Source/`.
2. **Analysis >> Build Custom Studies DLL >> Remote Build**.
3. On an ES 5 minute chart: **Analysis >> Studies >> Add Custom Study >>
   Al Brooks Price Action Suite**.

The study checks the chart's bar period against the `Expected Bar Period`
input and writes a note to the Message Log if they disagree. It does not change
your chart symbol or period — set those yourself.

## How to read it

- **Dashboard** (top of chart): always-in direction and how long it has held,
  market cycle phase, current H and L counts, micro channel warnings.
- **H1..H4 / L1..L4** printed at bars: Brooks' pullback counts, running
  continuously and independently on both sides.
- **Small dot** on a bar = a **signal bar**. A buy stop goes one tick above it
  (or a sell stop one tick below).
- **Arrow** = the setup **triggered**: a later bar traded one tick beyond the
  signal bar. Setups stay live for 3 bars, then expire.
- **Label** next to the arrow = pattern name and quality score out of 100.
- **Lines** = entry, stop, and the two targets for the most recent signal.

Nothing repaints. Swing points are confirmed with an explicit N-bar delay
rather than read off the right edge, and every pattern uses only bars that had
already closed when it was reported.

## Signal bar vs entry bar

Brooks separates these and so does the study. A setup is published on the close
of the signal bar; the trade is reported only once price trades through. The
`Signal Mode` input chooses which one drives alerts. Default is the trigger.

Stops are one tick beyond the opposite extreme of the signal bar. Target 1 is
the scalp (1R by default), Target 2 the swing (2R, or the measured move
projection of the current leg when that is further).

## Patterns detected

| Continuation | Reversal |
|---|---|
| H1 / H2 / H3 / H4 | Double top / double bottom |
| L1 / L2 / L3 / L4 | Wedge / three pushes |
| Breakout pullback | Major trend reversal (trend line break, then failed test) |
| Moving average gap bar | Final flag |
| ii / ioi / iii breakout | Climax / trend channel line overshoot |
| Trading range fade | Failed breakout |
| | Two-bar reversal, micro double top/bottom |
| | Opening reversal |

Each is individually toggleable. H1/L1 is **off** by default — it is the first
entry and Brooks treats it as materially lower probability than the second.

## Scoring

Every candidate is scored 0–100 from signal bar quality (body, close position,
tails, size against ATR), context (with or against always-in, market cycle
phase, micro channels), location (distance to the 20 EMA, higher timeframe EMA
agreement), and whether it is a second entry. Reversal setups are penalised
when no trend line break has happened first — fading a trend without one is the
mistake Brooks warns about most. The `Minimum Signal Strength` input (default
55) filters the rest.

When several patterns fire on the same bar, the highest scoring one wins.

## Inputs worth tuning first

| Input | Why |
|---|---|
| `Minimum Signal Strength` | The main volume control. Raise to 65–70 for fewer, better setups. |
| `Swing Strength` | Larger = more robust swings but more confirmation lag. |
| `Maximum Stop Size (ticks)` | Skips setups whose signal bar is too big to risk. |
| `Direction Filter` | "With Always-In Only" for trend-following only. |
| `Skip Midday Doldrums` | On by default; the 11:30–13:30 chop Brooks avoids. |

## Exposed values

Subgraphs 8–16 (Always In, Regime code, H/L count, Setup code, Signal strength,
Entry, Stop, T1, T2) are set to `Ignore` draw style but carry real values, so
you can reference them from a Spreadsheet study, an alert condition formula, or
another ACSIL study.

## Scope

This is an analysis and alerting tool. It does not place orders. Brooks' method
is discretionary — the counts and patterns are meant to speed up your own
reading of the chart, not replace it. Treat the score as a prompt to look, not
a signal to trade.
