# ICT Breaker Blocks — Sierra Chart Study

An ACSIL (C++) custom study that detects ICT breaker blocks from swing structure and draws the failed order block ranges as zones on the price graph.

A breaker block is an order block that failed and then had its origin swing violated in the opposite direction. It flips polarity and is expected to act as support or resistance when price returns to it.

---

## Detection logic

Pivots are confirmed with a symmetric left/right strength test, then forced into a strictly alternating high–low sequence — when two pivots of the same type occur in a row, only the more extreme one is kept. That produces a clean zig-zag the structure rules read off the tail of.

**Bullish breaker**

```
swing low (L1)  →  swing high (H1)  →  lower low (L2 < L1)  →  close above H1
```

The bearish order block that produced the drop from H1 has failed. Its range becomes a bullish breaker and is projected forward as support.

**Bearish breaker**

```
swing high (H1)  →  swing low (L1)  →  higher high (H2 > H1)  →  close below L1
```

The bullish order block that produced the rally from L1 has failed. Its range becomes a bearish breaker and is projected forward as resistance.

**Zone boundaries**

The zone is anchored on the order block candle, not the swing candle: the last up-close candle at or before the swing high for a bullish breaker, the last down-close candle at or before the swing low for a bearish breaker. The search window is set by `Order Block Search Bars`. If no matching candle is found the swing candle itself is used.

**Mitigation**

A bullish zone is mitigated when price trades below its bottom; a bearish zone when price trades above its top. Mitigated zones either disappear or render dashed and faded, and stop extending at the bar that mitigated them.

---

## Installation

1. Copy `ICT_BreakerBlocks.cpp` into `C:\SierraChart\ACS_Source\`
2. In Sierra Chart: **Analysis → Build Custom Studies DLL**, select the file
3. **Analysis → Studies → Add Custom Study**, choose *ICT Breaker Blocks*

Rebuilding while the study is on a chart is fine — Sierra unloads and reloads the DLL automatically.

---

## Inputs

| Input | Default | Notes |
|---|---|---|
| Swing Strength (Bars Each Side) | 5 | Main sensitivity dial. 3–5 for intraday structure, 10+ for higher-timeframe zones only. |
| Max Breakers Per Direction | 5 | Older zones are deleted beyond this count. |
| Order Block Search Bars | 10 | How far back from the swing pivot to look for the order block candle. |
| Use Candle Body Only For Zone | No | Yes restricts the zone to open–close instead of high–low. |
| Extend Zone Past Last Bar (Bars) | 10 | Requires chart space to the right of the last bar to be visible. |
| Structure Break Requires Bar Close | Yes | No uses wicks, which triggers earlier but less reliably. |
| Mitigation Requires Bar Close | Yes | No mitigates on any wick through the zone. |
| Hide Mitigated Breakers | Yes | No keeps them as dashed, faded rectangles. |
| Bullish Breaker Color | 0, 160, 90 | |
| Bearish Breaker Color | 200, 60, 60 | |
| Zone Fill Transparency (0–100) | 80 | Higher is more transparent. |
| Show Text Labels | Yes | Draws "Bullish Breaker" / "Bearish Breaker" at the left edge. |
| Enable Alerts On New Breaker | No | Uses alert numbers 1 (bullish) and 2 (bearish). |
| Export Zone Levels To Subgraphs | No | See below. |

---

## Subgraphs

Four subgraphs expose the nearest active zone boundaries per bar, for use in spreadsheet studies, alert conditions, or as inputs to other studies:

| Index | Name |
|---|---|
| 0 | Bull Breaker Top |
| 1 | Bull Breaker Bottom |
| 2 | Bear Breaker Top |
| 3 | Bear Breaker Bottom |

They hold zero unless `Export Zone Levels To Subgraphs` is enabled, and are set to `DRAWSTYLE_IGNORE` so nothing plots.

**If you see dots on the chart:** `sc.SetDefaults` only runs when a study is first added, so a draw style already saved in your chartbook overrides what the code sets. Open Chart Settings → Studies → ICT Breaker Blocks → Settings and Inputs → Subgraphs, and set all four to *Ignore*. Removing and re-adding the study also clears it.

---

## Behavior notes

- **Detection runs on closed bars only.** A zone appears on the bar after the break confirms. Pivots add a further lag of `Swing Strength` bars, which is inherent to any swing-based structure method.
- **Zones do not repaint once drawn**, but zones that have not yet formed can appear as new pivots confirm.
- Each swing pivot can anchor at most one breaker, and anchors must advance forward in time.
- Chart drawings are managed with `sc.UseTool` under this study instance and are cleared on full recalculation.
- State is held in persistent vectors, freed on `sc.LastCallToFunction`.

---

## Known limitations

- ICT sources disagree on whether the bullish breaker should be drawn from the order block candle or the full swing-high candle. This uses the order block convention. To switch, set the new zone's `Top` to `S2.Price` in the creation block.
- No higher-timeframe aggregation — the study reads only the chart it is applied to.
- No displacement or fair value gap filter on the structure break, so ranging markets produce more zones than a discretionary reading would.
- `Extend Zone Past Last Bar` relies on Sierra having bars-to-the-right configured; otherwise zones stop at the last bar.

---

## License

MIT. Trading involves risk; this is an analysis tool, not advice, and carries no warranty.
