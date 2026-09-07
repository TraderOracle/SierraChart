# Sierra Chart Custom Studies

A collection of custom studies (ACSIL / C++) for [Sierra Chart](https://www.sierrachart.com/), built as compiled DLLs (Advanced Custom Study Interface & Language).

## 📁 Contents

| File | Description |
|------|--------------|
| `VolImbRenko.cpp` | Volume Imbalance detector for Renko-style bars. Flags consecutive same-color bars where the current bar's open gaps beyond the prior bar's close, marks them with a colored dot + horizontal line, and fires a Sierra Chart alert. Includes a helper for posting alerts to a Discord webhook. |
| `SCStringUtils.cpp` | Standalone `SCString` helper library — string prefix/suffix checks, case conversion, trimming, find/replace, split/join, padding, and numeric/bool conversions for Sierra Chart's `SCString` type. |
| `TraderOracle.cpp` | Multi-study DLL (`SCDLLName("Trader Oracle DLL")`) bundling several indicators: Delta Intensity, Olympus (a composite multi-indicator signal study, plus an older `OlympusOLD` version), a Squeeze indicator, Linda Raschke's MACD/Anti-Setup studies, Waddah Explosion, and a DTS Scalper volume-split study. |

> Update this table as you add more files — one row per study/file, with a short note on what it does.

## 🗂️ How This Repo Is Organized

- Most `.cpp` files are standalone Sierra Chart custom studies (`SCSFExport scsf_*`), meant to be compiled into a DLL via Sierra Chart's Custom Study build process.
- Some files (e.g. `SCStringUtils.cpp`) are shared utility code — not studies themselves, but helpers meant to be `#include`d into a study source file.

## 🚀 Getting Started

These files are written against Sierra Chart's ACSIL API and require `sierrachart.h` (provided by a Sierra Chart installation) to compile.

1. Clone this repo, or copy the desired `.cpp` file, into your Sierra Chart `ACS_Source` folder.
2. Open Sierra Chart → **Analysis → Build Custom Studies DLL**, and build the file.
3. Add the resulting study to a chart via **Analysis → Studies**.

```bash
git clone https://github.com/your-username/your-repo.git
```

## 📄 File Notes

### `VolImbRenko.cpp`
- Study function: `scsf_VolImbRenko`
- Detects "volume imbalance" candles: two consecutive bars of the same color where the current bar's open is beyond the prior bar's close (a price gap between bar bodies).
- Draws a marker (`DRAWSTYLE_POINT`) above/below the imbalance bar and an `AddLineUntilFutureIntersection` line at the open price.
- Fires `sc.AlertWithMessage` on new bars where an imbalance is detected.
- Includes `SendDiscordWebhook()` for posting alert messages to a Discord webhook via `sc.MakeHTTPPOSTRequest` (not currently called from the study body — wire it in where you want notifications sent).
- ⚠️ Heads-up: in both the green and red imbalance branches, `hc2`, `lc2`, `hc3`, and `lc3` are declared twice in the same scope — this will fail to compile as-is. Remove the duplicate `double hc2 = ...` line in each block before building.

### `SCStringUtils.cpp`
A grab-bag of `SCString` helpers, since ACSIL's `SCString` doesn't ship with much built-in string manipulation:
- **Checks:** `StartsWithSC`, `EndsWithSC`, `ContainsStringSC`, `EqualsIgnoreCaseSC`
- **Case/whitespace:** `UpperCaseSC`, `LowerCaseSC`, `CapitalizeFirstSC`, `TrimSC`
- **Search/replace:** `FindStringSC`, `ReplaceAllSC` (SCString), `ReplaceAllStr` (`std::string`)
- **Split/join:** `SplitSC`, `SplitStringSC`, `JoinSC`
- **Formatting/parsing:** `PadLeftSC`, `ToStringSC`, `ToDoubleSC`, `ToFloatSC`, `ToIntSC`, `ToBoolSC`

Meant to be dropped into another study's source (or included as a shared header) rather than compiled as its own DLL — it has no `SCSFExport` entry point.

- ⚠️ Note: `SplitStringSC` and `SplitSC` do the same thing (split on a char delimiter via `std::getline`) — likely redundant; consider keeping just one.

### `TraderOracle.cpp`
A larger DLL containing multiple independent studies, each with its own `SCSFExport` entry point. Shared candle-pattern helpers (`IsGreen`, `IsBullishEngulfing`, `IsTweezerTop`, `IsTrampoline`, `IsVolImbGreen/Red`, etc.) and a custom `DrawText()` wrapper live at the top under `COMMON FUNCTIONS`.

| Study function | What it does |
|---|---|
| `scsf_Delta_Intensity` | Colors bars by bid/ask delta intensity relative to a threshold, and flags "Delta Divergence" (price direction vs. delta direction disagree) with on-chart text. |
| `scsf_Olympus` | The main composite signal study — combines Waddah, MACD, Parabolic SAR, SuperTrend, Awesome Oscillator, HMA, T3, Fisher Transform, and ADX (each toggleable) into buy/sell dots, plus separate markers for three-outside-up/down, tweezer tops/bottoms, "trampoline" reversals, doji clusters, shaved candles, and volume imbalances. |
| `scsf_OlympusOLD` | Earlier version of `scsf_Olympus`, kept for reference/comparison — same indicator stack, slightly different subgraph layout and pattern logic. |
| `scsf_SierraSqueeze` | Momentum histogram + Keltner/Bollinger "squeeze" detector (dots mark when Bollinger Bands move inside Keltner Channels). |
| `scsf_LindaMACD` | MACD histogram colored bright/dim based on agreement with a Parabolic SAR signal. |
| `scsf_WaddahExplosion` | Waddah Accelerator/Explosion style momentum histogram (dual EMA divergence vs. Bollinger Band width), with bar coloring. |
| `scsf_Linda_Anti_Setup` | Linda Raschke "Anti" setup — tracks MACD vs. its moving average, paints background on qualifying high/low crosses. |
| `scsf_DTS_Scalper` | Splits bar volume into buyer/seller portions (by close position within the bar range, adjusted by bid/ask volume) and displays a live buyer/seller percentage readout. |
| `DrawToChart` (helper) | Example custom-graphics callback (`sc.Graphics`) for drawing text directly onto the chart; not wired into any study's `SetDefaults` by default. |

- Large blocks of commented-out code (an alternate line-intersection cleanup routine, a `CreateProcess`/notepad launcher, a graphics-settings example, a marker-drawing example) are left in place as reference/scratch material rather than active logic.
- `scsf_Olympus` and `scsf_OlympusOLD` duplicate almost all of their indicator calculation code — a good candidate for refactoring into shared helper functions if you continue maintaining both.

## 🤝 Contributing

If others can contribute, briefly note how (e.g., "Open an issue or submit a pull request"). Otherwise, delete this section.

## 📜 License

Specify a license if applicable (e.g., MIT, Apache 2.0), or note that this repo is for personal/reference use only.

---

*Last updated: <!-- add date -->*
