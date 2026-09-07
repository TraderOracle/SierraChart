# Sierra Chart Custom Studies

A collection of custom studies (ACSIL / C++) for [Sierra Chart](https://www.sierrachart.com/), built as compiled DLLs (Advanced Custom Study Interface & Language).

## 📁 Contents

| File | Description |
|------|--------------|
| `VolImbRenko.cpp` | Volume Imbalance detector for Renko-style bars. Flags consecutive same-color bars where the current bar's open gaps beyond the prior bar's close, marks them with a colored dot + horizontal line, and fires a Sierra Chart alert. Includes a helper for posting alerts to a Discord webhook. |

> Update this table as you add more files — one row per study/file, with a short note on what it does.
### `VolImbRenko.cpp`
- Study function: `scsf_VolImbRenko`
- Detects "volume imbalance" candles: two consecutive bars of the same color where the current bar's open is beyond the prior bar's close (a price gap between bar bodies).
- Draws a marker (`DRAWSTYLE_POINT`) above/below the imbalance bar and an `AddLineUntilFutureIntersection` line at the open price.
- Fires `sc.AlertWithMessage` on new bars where an imbalance is detected.
- Includes `SendDiscordWebhook()` for posting alert messages to a Discord webhook via `sc.MakeHTTPPOSTRequest` (not currently called from the study body — wire it in where you want notifications sent).
