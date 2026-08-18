# Trend Architect v9.3 — Release Notes (2026-07-03)

One build containing two change sets: the **v9.2 visibility fixes** and the
**v9.3 signal enhancements**. The study announces itself as
**"Trend Architect v9.3"** in the Studies list — if your chart shows v9.1/v9.2,
the new DLL is not loaded.

---

## Install

1. Copy the **entire `source/` tree** into `ACS_Source` (two module files are
   NEW: `modules/mod_RangeRegimeGate.h`, `modules/mod_SignalLogger.h` —
   copying only changed files breaks the compile). Build `TrendArchitect.cpp`
   as usual. (Alternative: build the single-file
   `source/build/TrendArchitect_Remote.cpp`.)
2. **Remove and re-add the study** (or press Set Defaults). Required: subgraph
   indices shifted by 2 in v9.2 for the bar-mask layer. The chart renders
   correctly even before this (runtime enforcement), but Subgraphs-tab
   names/colors stay stale until you do it.
3. For real bid/ask delta on existing charts: Data/Trade Service Settings →
   **Intraday Data Storage Time Unit = 1 Tick**, then
   `Edit >> Reload and Recalculate` once per chart. Without 1-tick storage
   everything falls back to the old candle-geometry delta automatically.

## v9.2 — visibility fixes (recap)

- Cloud/channel/ribbon fills use REAL transparency — native candles and other
  studies (e.g. a separate SuperTrend) show through; nothing is blanketed.
- New bottom-layer **bar mask**: when Indicator Candles (or Candle Coloring)
  are ON, the chart's native bars are blanked behind the synthetic candles
  automatically; when OFF, the study drops underneath the price graph. The
  manual "Hide Main Price Graph" step is gone in both modes.
- Keep the chart background pure black (dark themes) or white (light themes),
  matching the study's "Chart Background" input.

## v9.3 — signal enhancements (all opt-in inputs, indices 106-133)

Defaults marked **(parity)** leave the TradingView baseline byte-identical.

**On by default**

| Input | What it does |
|---|---|
| Delta Source: Real Bid/Ask Volume | True aggressor volume replaces the candle-shape CVD estimate feeding the Quality Gate, elevation c4, and the delta highlight. Per-bar auto-fallback where data is absent. |
| OF Gate: Delta Agreement (window 2) | A full arrow needs net signal-side delta over the window, else it demotes to a marginal dot. |
| PRISM Adaptive: Estimate Bar Seconds | **Fixes Adaptive on tick/volume/range charts** — `sc.SecondsPerBar` reads 0 there, so Adaptive scaling was silently dead on all tick charts. Median bar duration is measured instead. |
| Log Signals to CSV | Outcome logger (below). Passive — no signal changes. |

**Off by default — recommended first enables**

| Input | Recommended | What it does |
|---|---|---|
| PRISM Signal Confirmation | **Bar Close** | Arrows/alerts (1-7) only once the signal bar closes — eliminates intrabar ghost arrows. Costs one bar of entry latency (seconds on tick charts). |
| PRISM: Opposite-Signal Cooldown | **6** | An opposite full arrow within N bars prints as a dot — kills the alternating whipsaw loser train. |
| Range Regime Gate | Yes (after baseline watch) | Choppiness(14)>61.8 / ADX(14)<20 / ATR-percentile<25, 2-of-3 vote suppresses full arrows in ranges. |
| Regime Gate: Calibrated Hurst/Accel | Yes (after baseline watch) | Parity Hurst threshold 0.50 fires on ~every bar (verified by simulation), degenerating the 2-of-3 vote; 0.72 restores it. Adds an accel deadband. |

**Off by default — enable after log analysis:** Strict Gate Mode, CVD
Divergence Veto, Absorption Veto, Adaptive ER Percentile, AO Stop-Out ATR
(suggest 1.0).

Everything demotes full arrows to marginal dots — filtered signals stay
visible, nothing is hidden.

## The measurement loop

Confirmed signals log one CSV row each (once resolved over K bars) to
`<Data Files Folder>\TA_signals_<symbol>_ch<chart>.csv`: direction,
full-vs-marginal, entry, MFE/MAE in ATR, resolution tier, intrabar flicker
count, and the full fire-time gate context (ER, KAMA slope, bar quality,
delta rank/agreement, regime votes, chop metrics, AO length).

After 1-3 weeks of live sessions:

```
python execution/analyze_signals.py "D:/APPS/SierraChart/Data/TA_signals_*.csv" --out report.md
```

The report shows per-gate win rates (marginal dots are the control group) and
threshold sweeps — which cutoff changes would have removed the most losers per
winner lost, each mapped to a study input. Change ONE input per iteration.

## Documentation map

| Where | What |
|---|---|
| `directives/trendarchitect.md` | Full change log with rationale, ACSIL gotchas, recommended settings (§v9.1 fills, §v9.2 mask, §v9.3 enhancements) |
| `docs/v93-enhancement-review.md` | Complete review matrix: all 18 proposals, mechanisms, risks, verification verdicts |
| `source/include/TA_Inputs.h` §9 | New input index map with defaults |
| `source/include/TA_Subgraphs.h` | Subgraph z-order + hidden Arrays slot allocation |
| `execution/analyze_signals.py` | Analyzer usage (docstring) |

## Known limitations

- Tick-chart bar-seconds estimate is frozen per full recalculation (doesn't
  track intraday pace shifts); estimate errs conservative.
- Strong-trend cloud fills cap at 20% opacity (one fill-transparency knob per
  study vs Pine's 45% ceiling); TC glow lines have no transparency support.
- Logger rows are live-session only by design (no historical backfill) —
  expect 1-3 weeks per symbol before threshold sweeps are trustworthy.
- Alerts 8-13 (ribbon/OB-OS/regime crossings) keep live semantics in both
  confirmation modes.
