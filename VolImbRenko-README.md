# Sierra Chart Custom Studies

A collection of custom studies (ACSIL / C++) for [Sierra Chart](https://www.sierrachart.com/), built as compiled DLLs (Advanced Custom Study Interface & Language).

## 📁 Contents

| File | Description |
|------|--------------|
| `VolImbRenko.cpp` | Volume Imbalance detector for Renko-style bars. Flags consecutive same-color bars where the current bar's open gaps beyond the prior bar's close, marks them with a colored dot + horizontal line, and fires a Sierra Chart alert. Includes a helper for posting alerts to a Discord webhook. |

> Update this table as you add more files — one row per study/file, with a short note on what it does.

## 🗂️ How This Repo Is Organized

- Each `.cpp` file is a standalone Sierra Chart custom study (`SCSFExport scsf_*`), meant to be compiled into a DLL via Sierra Chart's Custom Study build process.
- One DLL/source file per strategy or indicator, named to match its `SCDLLName`.

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

## 🤝 Contributing

If others can contribute, briefly note how (e.g., "Open an issue or submit a pull request"). Otherwise, delete this section.

## 📜 License

Specify a license if applicable (e.g., MIT, Apache 2.0), or note that this repo is for personal/reference use only.

---

*Last updated: <!-- add date -->*
