# Ehlers Dominant Cycle Stochastic RSI — User Guide & Reference

*Sierra Chart custom study (ACSIL). Ported from the TradingView Pine Script
original, with six approved enhancements. Study source:
`EhlersDominantStoch.cpp`. Conversion details: `CONVERSION.md`.*

---

## 1. What it is

A Stochastic RSI in which **nothing has a fixed period**. Classic oscillators
use one lookback (e.g., RSI-14) regardless of market conditions; this study
continuously measures the market's **dominant cycle length** and scales the
RSI and stochastic lookbacks to it. When the market cycles fast, the oscillator
speeds up; when cycles stretch out, it slows down — keeping overbought/oversold
readings aligned with the rhythm the market is actually trading.

Three independent estimators measure the cycle, cross-check each other, and
produce a confidence score, so you can also see *how trustworthy* the current
cycle estimate is before acting on a signal.

## 2. Quick start

1. Copy `EhlersDominantStoch.cpp` into `SierraChart/ACS_Source/`.
2. In Sierra Chart: **Analysis → Build Custom Studies DLL → Remote Build**
   (no local compiler needed) — or **Build → Local Build** with Visual C++.
3. **Analysis → Studies → Add Custom Study →** "Ehlers Dominant Cycle
   Stochastic RSI". It opens in its own region as a 0–100 oscillator.
4. Defaults are sensible. The three highest-leverage settings to try first:
   - **RSI / Stoch Period Multiplier** — 1.0 = smooth full-cycle, 0.5 = fast
     half-cycle.
   - **Appearance** — Modern (one thick direction-colored line) vs Classic
     (separate K and D lines).
   - **Fade/Transparency Blend Color** — set to your chart background color if
     it isn't black, so fills and fades render correctly.

## 3. How it works

Pipeline, per bar:

```
Source ──► [Bandpass pre-filter (optional "spectral dilation")]
        ├─► Hilbert Transform homodyne ────────┐
        └─► Highpass(MaxPeriod) → SuperSmoother(10)
              ├─► Autocorrelation periodogram ─┤──► Confirmation & weighting
              └─► Goertzel DFT power scan ─────┘        │
                                              Final cycle period (smoothed,
                                              clamped to Min..Max Period)
                                                        │
        Adaptive RSI (period = cycle × RSI mult) ◄──────┘
              │
        Stochastic of RSI (lookback = cycle × Stoch mult)
              │
        K smoothing (selectable MA) → D smoothing (selectable MA)
```

**The three cycle estimators**

- **Hilbert Transform** (homodyne discriminator): recursive, updates every
  bar, responds quickly, can be noisy.
- **Autocorrelation periodogram**: finds the lag whose Pearson correlation
  with the recent filtered series peaks (minimum r = 0.2). Robust in
  well-cycling markets.
- **Goertzel DFT**: measures spectral power at each candidate period over a
  sample window (Max Period × DFT Sample Window bars) and picks the strongest.

**Confirmation**: each estimate is compared to the three-method average. A
method within the Confirmation Tolerance "agrees". The confidence score is
agreement ÷ 3 (0%, 33%, 67%, 100% — displayed thresholds: ≥66 high, ≥33
moderate). The final period is a weighted blend — agreeing methods get full
weight, disagreeing ones half, and the autocorrelation method is additionally
weighted by its own correlation strength — then smoothed heavily (15% per bar)
and clamped to [Min Period, Max Period].

**Adaptive RSI**: gain/loss smoothing uses alpha = 2/(P+1) (EMA form, the
original design) or alpha = 1/P (Wilder, like a standard RSI) with
P = round(cycle × RSI multiplier), never below 2.

**Stochastic**: raw %K = position of the RSI within its highest/lowest range
over round(cycle × Stoch multiplier) bars (never below 5). K then D are
smoothed with your chosen MA types.

## 4. Reading the indicator

- **K line** (Modern): thick line, up-color while rising, down-color while
  falling. **D line**: faint gray reference. Classic mode shows K in the
  up-color and D in the down-color, both width 2.
- **Zones**: above 80 = overbought, below 20 = oversold, 50 = midline. Zone
  fills use dimmed theme colors.
- **Histogram (K − D)**: momentum of the oscillator itself. It is normalized
  over the last N bars (input 23) and scaled so recent extremes fill the 20–80
  band, centered on 50. Solid color = momentum growing; faded = momentum
  fading. It appears only after N bars of history exist.
- **Cross arrows** (enhancement): up arrow = K crossed above D while K was
  below the Bullish Cross Threshold (default 30 — bullish reversal from
  oversold); down arrow = K crossed below D while K was above the Bearish
  Cross Threshold (default 70). Both thresholds are inputs (38/39), and an
  optional Minimum Signal Confidence (input 40) suppresses arrows when the
  cycle detectors disagree. These are the classic entry signals of the study.
- **Confidence background** (input 32, off by default): green = all three
  cycle methods agree (trust adaptive behavior), yellow = two agree, red =
  disagreement (cycle estimate unreliable — treat signals with more caution,
  or fall back to price structure).
- **Info table** (input 31, off by default): live per-method periods at the
  top-right of the region; `+` marks methods currently within tolerance, plus
  the confirmed period and confidence.

**A practical filter**: take cross-arrow signals only when confidence ≥ 66%
(background green / High Confidence alert active). Disagreeing estimators
usually mean a trending or chaotic market where mean-reversion signals from
any stochastic are weakest. Set **Minimum Signal Confidence % (input 40) to
67** to have the study enforce this automatically for both arrows and alerts.

## 5. Input reference

### Cycle Detection

| Input | Default | What it does / how to tune |
|---|---|---|
| Source (0) | Last (close) | Price series analyzed. |
| Min Period (1) | 8 | Floor for every detected cycle. Raise to ignore very fast noise cycles. |
| Max Period (2) | 50 | Ceiling for detected cycles; also sets the highpass cutoff, the autocorrelation buffer, and the DFT window base. Larger = slower, smoother adaptation. |
| Use Spectral Dilation (3) | Yes | Bandpass pre-filter centered mid-range before cycle analysis. Reduces the tendency of longer periods to dominate the spectrum. Turn off to analyze raw price. |
| Bandpass Bandwidth (4) | 0.3 | Width of that pre-filter. Lower = more selective (can lock onto one cycle), higher = more tolerant. |
| Confirmation Tolerance (5) | 0.2 | How close (fraction of the average) a method must be to "agree". Tighter (0.1) = confidence is rarer but means more; looser (0.4) = more bars count as confirmed. |

### RSI

| Input | Default | Notes |
|---|---|---|
| RSI Period Multiplier (6) | 1.0 | 1.0 = full detected cycle (smoother), 0.5 = half cycle (faster, classic Ehlers advice for RSI). |
| RSI Calculation (7) | EMA (Original) | EMA = the Pine original, alpha 2/(P+1), more responsive. Wilder = alpha 1/P, matches standard RSI readings (an RSI-14 comparison only makes sense with Wilder). |

### Stochastic

| Input | Default | Notes |
|---|---|---|
| Stoch Period Multiplier (8) | 1.0 | Lookback of the stochastic window as a fraction of the cycle. 0.5 = faster signals, more of them; 1.0 = fewer, cleaner. |
| Cap Stoch Lookback (TV Behavior) (9) | No | The original Pine code silently capped the window at Max Period bars, so multiplier 2.0 wasn't fully honored near long cycles. No = honor the true window. Yes = replicate TradingView exactly. |
| K Smoothing Length (10) / Type (11) | 10 / EMA | Smoothing of raw %K. Length dominates feel: shorter = jumpier. HMA/ALMA/DEMA/TEMA trade smoothness against lag differently — HMA and ALMA are smooth *and* fast; TEMA is fastest but can overshoot. |
| D Smoothing Length (12) / Type (13) | 4 / EMA | Signal line smoothing of K. |
| ALMA Offset (14) / Sigma (15) | 0.85 / 6.0 | Only used when a smoothing type is ALMA. Offset→1 = more responsive; higher sigma = smoother. |

### Visual Style

| Input | Default | Notes |
|---|---|---|
| Appearance (16) | Modern | Modern = single thick direction-colored K + faint D. Classic = two plain lines. |
| Color Theme (17) | Modern (Red/Cyan) | Six built-in pairs or Custom (uses inputs 18/19). |
| Custom Up / Down Color (18/19) | cyan / red | Only with theme = Custom. |
| Modern Line Width (20) | 4 | Modern K thickness (Classic is fixed at 2). |
| Show Signal (D) Line (21) | Yes | Hide to declutter Modern mode. |
| Show Histogram (22) | Yes | The K−D momentum columns. |
| Histogram Normalization Length (23) | 50 | Bars used to scale the histogram into the 20–80 band. Shorter = livelier columns. |
| Histogram Transparency (24) | 30 | 0 solid … 100 invisible; fading bars get +30 on top. |
| Highlight OB/OS Zones (25) | Yes | Fills above 80 / below 20. |
| Show Cross Signal Arrows (26) | Yes | The bull/bear cross markers. Signal *values* stay available to automation even when hidden. |
| Fade/Transparency Blend Color (27) | black | All "transparency" is simulated by blending toward this color. **Set it to your chart background color** (Chart → Graphics Settings) if you use a non-black background. |
| Hide Warm-up Bars (28) | Yes | Suppresses the first Max Period bars, where the adaptive filters are still settling. No = draw everything, like TradingView. |

### Performance

| Input | Default | Notes |
|---|---|---|
| Cycle Recalc Interval (29) | 3 | Run the heavy autocorrelation + DFT scans every N bars (the Hilbert method still updates every bar, and results are heavily smoothed anyway). 1 = most accurate, 3–5 = virtually identical output, faster full recalculations. |
| DFT Sample Window (30) | 2.0 | DFT analysis window as a multiple of Max Period. 2.0 = best frequency resolution; 1.0 = fastest. |

### Display & Alerts

| Input | Default | Notes |
|---|---|---|
| Show Cycle Detection Info (31) | No | Top-right text panel with per-method periods, confirmed period, confidence. |
| Confidence Background (32) | No | Green/yellow/red background by method agreement. |
| Enable Alerts (33) | No | Master switch for the three alerts. |
| Alert Timing (34) | Bar Close | Bar Close = evaluate the last *closed* bar (TV "Once Per Bar Close"). Intrabar = evaluate the developing bar, at most once per bar — earlier but the condition can vanish by the close. |
| Bullish / Bearish / Confidence Alert Number (35/36/37) | 1 / 2 / 3 | Sierra alert (sound) number per condition; 0 disables that one condition. |

### Signal Filters

These shape **both** the cross arrows and the two cross alerts — the alerts
read the same signal series the arrows draw, so they can never disagree.

| Input | Default | Notes |
|---|---|---|
| Bullish Cross Threshold (38) | 30 | A bullish K/D cross only signals when K is below this at the cross bar. 30 = original hardcoded value; 20 = match the drawn OS zone; 100 = every bullish cross. |
| Bearish Cross Threshold (39) | 70 | A bearish cross only signals when K is above this. 70 = original; 80 = match the drawn OB zone; 0 = every bearish cross. |
| Minimum Signal Confidence % (40) | 0 (off) | Confidence steps are 0/33/67/100: set 34 to require at least two of the three cycle methods to agree, 67 to require all three. Does not affect the High Confidence alert. |

## 6. Subgraph reference & automation

ACSIL indexes subgraphs from 0; **Sierra Chart formulas (Alert Conditions,
Spreadsheet Studies) index from 1** — use the SG column below in formulas.

Subgraphs are ordered background-first so the lines and arrows always draw on
top (Sierra paints subgraphs in ascending order).

| ACSIL | Formula | Name | Notes |
|---|---|---|---|
| 0 | SG1 | Confidence background | Display plumbing. |
| 1–4 | SG2–SG5 | Zone fill bounds | Display plumbing. |
| 5/6 | SG6/SG7 | Histogram fill top/bottom | Display plumbing (50-centered band). |
| 7/8/9 | SG8/SG9/SG10 | OB 80 / Mid 50 / OS 20 | Constant reference lines. |
| 10 | SG11 | StochRSI K | The main line (0–100). |
| 11 | SG12 | D (Signal) | Smoothed K. |
| 12 | SG13 | Bull Cross Signal | Nonzero (arrow position) on the bar K crossed above D with K < 30; else 0. |
| 13 | SG14 | Bear Cross Signal | Nonzero on the bar K crossed below D with K > 70; else 0. |
| 14 | SG15 | Final Cycle Period | The confirmed, smoothed period — useful as an input to *other* adaptive studies via Study/Price Overlay or spreadsheets. |
| 15/16/17 | SG16/SG17/SG18 | Hilbert / Autocorr / DFT period | Per-method estimates. |
| 18 | SG19 | Cycle Confidence % | 0 / 33.3 / 66.7 / 100. |
| 19/20 | SG20/SG21 | RSI Period / Stoch Period | The adaptive lookbacks in use. |
| 21 | SG22 | Raw RSI | The adaptive RSI before the stochastic. |
| 22 | SG23 | Histogram Value | Raw K − D. |
| 23 | SG24 | AC Correlation % | Autocorrelation peak strength × 100. |

Example Alert Condition formulas (study ID 1):

- Bullish cross signal fired on the last closed bar: `ID1.SG13[-1] > 0`
- Bullish cross **and** high confidence: `AND(ID1.SG13[-1] > 0, ID1.SG19[-1] >= 66)`
- K left the oversold zone: `AND(ID1.SG11[-1] > 20, ID1.SG11[-2] <= 20)`

The signal subgraphs are also directly usable by **Trading System Based on
Alert Condition** for automation. Hidden subgraphs (SG13–SG22) don't draw but
their values appear in the Chart Values window and are available to formulas.

## 7. Alerts

1. Set **Enable Alerts** (33) to Yes.
2. Pick the timing (34) and, if desired, different alert numbers per condition
   (35–37); set a number to 0 to silence just that condition.
3. Map sounds to those alert numbers under **Global Settings → General
   Settings → Alerts** (each alert number has its own sound/notification
   config).

Conditions:

| # | Condition | Semantics |
|---|---|---|
| Bullish Cross | K crossed above D while K < Bullish Cross Threshold (default 30) | Fires once per bar; honors the Minimum Signal Confidence filter. |
| Bearish Cross | K crossed below D while K > Bearish Cross Threshold (default 70) | Fires once per bar; honors the Minimum Signal Confidence filter. |
| High Confidence | Confidence rose to ≥ 66% | Rising-edge only (won't repeat while it stays high); not affected by the signal filters. |

The two cross alerts evaluate the Bull/Bear Cross Signal subgraphs directly —
whatever draws (or would draw) an arrow is exactly what alerts.

## 8. TradingView parity

Out of the box the port already matches the Pine study's math. For **exact**
TradingView behavior, set:

| Input | Value |
|---|---|
| RSI Calculation (7) | EMA (Original) *(default)* |
| Cap Stoch Lookback (9) | **Yes** |
| Show Cross Signal Arrows (26) | No *(TV had no arrows)* |
| Hide Warm-up Bars (28) | **No** |

Remaining inherent differences (see `CONVERSION.md` for full detail): the
first ~Max Period bars differ slightly where Pine emits `na` while warm-up
seeds settle; transparency is simulated by color blending; the info table is
monochrome text with `+`/`-` agreement marks.

## 9. Performance notes

The autocorrelation and DFT scans are the heavy part: roughly
(MaxPeriod−MinPeriod) × window multiply-accumulates per scanned bar. With the
defaults (interval 3, window 2×) a full recalculation of even very large
charts takes well under a second in native C++; realtime per-tick cost is a
single bar's work. If you push Max Period to 100 with interval 1 and window
2.0 on a tick chart with hundreds of thousands of bars and notice slow reloads,
raise the Cycle Recalc Interval — the period is smoothed so heavily that
interval 3–5 is visually indistinguishable from 1.

## 10. Troubleshooting

- **Build errors** on Remote Build: make sure only this study's `.cpp` changed
  in `ACS_Source`; paste the build log output to whoever maintains the study.
- **Nothing draws on the left of the chart**: that's Hide Warm-up Bars (28)
  doing its job; set to No to draw from bar 1.
- **Fills/faded colors look wrong on a light chart**: set input 27 to your
  chart background color.
- **No alerts firing**: input 33 must be Yes; the per-condition number must be
  > 0; with Bar Close timing the condition must be true on the *closed* bar
  (an intrabar touch that retraces before the close never fires).
- **Arrows seem rare**: they require a cross *inside* the extreme zones
  (below/above the cross thresholds, default 30/70). Heavy K/D smoothing keeps
  K away from the extremes; shorten K Smoothing Length, use multiplier 0.5 for
  more excursions, or widen the thresholds (inputs 38/39). If a Minimum Signal
  Confidence is set, arrows are also suppressed while the detectors disagree.
- **Histogram missing at the start of the chart**: it needs Histogram
  Normalization Length (23) bars of history before it can scale itself.

## 11. Version notes

- **1.1** — Rendering fix: subgraphs reordered so the K/D lines and arrows
  draw above the histogram, bands, and background (Sierra paints subgraphs in
  ascending order). New signal filters: configurable bullish/bearish cross
  thresholds (defaults 30/70 = original behavior) and an optional minimum
  cycle-confidence gate; the cross alerts now evaluate the signal subgraphs
  directly, so arrows and alerts always agree.
- **1.0** — Full port of `EhlersDominantStoch.pine` (Pine v6) to ACSIL with
  bar-for-bar state semantics, plus six approved enhancements: full stoch
  lookback (with TV-compat toggle), Wilder RSI option, cross signal arrow
  subgraphs, per-alert enable/timing/sound numbers, configurable fade blend
  color, warm-up bar suppression.
