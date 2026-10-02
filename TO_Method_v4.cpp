// ============================================================================================
//  REVERSAL GRAIL - Sierra Chart ACSIL custom study
// ============================================================================================
//
//  One study, three modules. Each module has its own section of inputs in Study Settings,
//  headed by a separator input with the module's name, and its own block of fixed settings in
//  this file:
//
//    REVERSAL GRAIL            the reversal signal (BUY / SELL tags), stop / target tracking,
//                              alerts 1-10, TraderOracle patterns, Resistance Cloud.
//                              Fixed settings: "HOLY GRAIL REVERSALS - SETTINGS".
//    LITTLE RIZZY              measured-move target levels from two swing pivots, touch /
//                              break / expiry styling, touch-and-reverse arrows.
//                              Fixed settings: "LITTLE RIZZY - SETTINGS".
//    REVERSAL VOLUME PATTERNS  the four volume / candle reversal patterns (blue arrows).
//                              Fixed settings: "VOLUME REVERSALS - SETTINGS".
//
//  All three modules' alerts go out as one combined Alert Manager entry per update, so Sierra
//  Chart's one-alert-per-update rule never drops one.
//
//  ---------------------------------------------------------------- the REVERSAL GRAIL signal
//
//  One reversal signal built from the components that tested best across 10 public reversal
//  indicators (LuxAlgo TD/Reversal Signals, VDUB %B divergence, Reversal Volume Patterns,
//  BB re-entry, AlgoAlpha, Candlestick Reversal System, 3-Bar Reversal, Island Reversal,
//  Pivot HL, Reversal Probability Zone), backtested on ~5M bars of index / commodity / FX /
//  crypto / stock data with a development set and a separate holdout set.
//
//  Values in <angle brackets> are constants in the HOLY GRAIL REVERSALS settings block below.
//
//  BULLISH signal at the close of bar i (bearish is the exact mirror image):
//
//    1. LOCATION   Low[i] is the lowest low of the last <Extreme Lookback> bars.
//
//    2. VETOES     ("don't catch the knife" - each of these made reversals WORSE in testing)
//                  - any close in the last 3 bars more than <Band Veto> std devs below its
//                    20-bar mean (a band blow-through is momentum, not exhaustion)
//                  - the signal bar's range is more than <Wide-Range Veto> x ATR
//                  - any of the last 3 bars has volume more than <Climax Veto> x average
//
//    3. SCORE      one point each, 0..4
//                  + Persistence : a TD-style count of consecutive closes below the close
//                                  4 bars earlier reached <Persistence Count> in the last 5 bars
//                  + Divergence  : the new low undercuts the prior swing low (bars i-30..i-6)
//                                  while the Bollinger %B z-score is HIGHER than it was there
//                  + Quiet volume: relative volume at the new low is below <Quiet Threshold>
//                                  (selling drying up beat climax volume in every test set)
//                  + Trend side  : close is above the 200 EMA (buying a dip in an uptrend)
//
//    4. FIRE       score >= <Minimum Score> and no buy signal in the previous <Cooldown> bars.
//                  Score 4 is plotted with the larger "A+" arrow.
//
//  The signal fires on the extreme bar itself. Waiting for a breakout above the signal bar
//  (the "confirmation" used by several of the source indicators) consistently gave back the
//  edge in testing, so there is deliberately no confirmation delay.
//
//  Stop  = signal-bar low - <Stop Buffer> x ATR  (risk floored at <Min Risk> x ATR)
//  Target= close + <Target R> x risk
//  Each signal is tracked for up to <Max Bars To Track> bars until its stop or target is hit.
//
//  ALERTS (each one: Off / Buys only / Sells only / Buys and sells, plus its own sound number)
//    1. Signal arrow           - an arrow appears.
//    2. Target hit             - price reaches a signal's target before its stop. Fires the
//                                moment the target is touched. If one bar contains both the
//                                stop and the target, bar data can't tell which came first:
//                                live, the target counts only if it was touched while the stop
//                                was still untouched; after a chart reload such a bar counts as
//                                a stop-out.
//    3. Open = prior close     - an arrow appears AND the signal bar opened at the previous
//                                bar's close (within <Tolerance> ticks).
//    4. Open = prior close + band break
//                              - as 3, AND the signal bar or the bar before it broke the outer
//                                band on the signal's side (lower band for buys, upper band for
//                                sells). "Broke" = wick beyond the band by default, or close
//                                beyond it. The band is the alert Bollinger band by default, or
//                                the inner edge of the Resistance Cloud ("Alert 4 Band").
//    5. Trampoline             - a TraderOracle trampoline completes (see below).
//    6. Engulfing at band      - a TraderOracle engulfing candle at the Bollinger band.
//    7. Squeeze dot            - a TraderOracle squeeze relaxer buy or sell dot.
//    8. Large wick in cloud    - TraderOracle Method "Large Wick in Cloud": a red bar whose lower
//                                wick is longer than its body, with its low inside the lower
//                                cloud (buy); a green bar with a long upper wick into the upper
//                                cloud (sell).
//    9. Squeeze dot in cloud   - a squeeze dot on a bar that reached the cloud on its side.
//   10. Trampoline in cloud    - a trampoline whose three bars reached the cloud on its side
//                                (the TraderOracle Method script pairs buys with the UPPER cloud,
//                                which almost never happens; here buys pair with the lower cloud).
//    Events found in the same update are combined into one Alert Manager entry; its sound is
//    the most specific alert that matched (4 > 3 > 1 > 2 > 10 > 9 > 8 > 5 > 6 > 7). Sound
//    number 0 = log entry only. Alerts 5-10 are off by default. Alerts never fire while the
//    chart is loading or recalculating.
//
//  RESISTANCE CLOUD (the outer bands of the TraderOracle Method script)
//    The Nadaraya-Watson envelope (non-repainting, jdehorty KernelFunctions v2) exactly as the
//    script computes it: a rational-quadratic kernel average of close / high / low, an ATR
//    (Wilder, <Cloud ATR Length>) of those smoothed series, inner edge = average +/- <Near ATR
//    Factor> x that ATR, outer edge = halfway between the near and far (<Far ATR Factor>)
//    bounds. Note: the kernel averages <Start Regression At Bar> + 2 bars (32 by default) - that
//    is how the library's loop is written - and the 256 lookback only sets how evenly those bars
//    are weighted. In testing the cloud did not improve the signals (signals with and
//    without a cloud touch performed the same), so it is context, not part of the score; the
//    large-wick-in-cloud setup had a small, consistent edge on its own (the same wick outside
//    the cloud had none).
//
//  TRADERORACLE PATTERNS (separate markers; they do not change the signal score)
//    Ported from the TraderOracle module and computed with the same Sierra Chart functions
//    (sc.RSI simple 14, sc.BollingerBands 20/2 simple, the squeeze relaxer EMA/linear
//    regression sequence), so they line up with its Olympus / Trampoline studies. In testing
//    none of the three had an edge on its own, and none reliably improved the signals.
//    - Trampoline: bearish = green bar at the upper band, then two red bars with a lower close,
//      RSI > 80 on one of the three bars; bullish is the mirror (RSI < 20, lower band).
//      "As in TraderOracle" reproduces the module exactly: a bug there makes the band check
//      almost always pass. "Require band touch" applies the band check as intended (1 tick).
//      Marked on the bar that completes the pattern (TraderOracle labels the bar before it).
//    - Engulfing at band (Olympus "Engulfing Green/Red BB"): a green (red) bar with a bigger
//      body than the previous bar, where both bars' lows (highs) are below (above) the
//      current lower (upper) band; doji bars skipped. Shown as a background highlight.
//      Optional: TraderOracle's classic engulfing test, at the band or anywhere.
//    - Squeeze dot (Olympus "Squeeze Relaxer"): the squeeze momentum histogram turns up while
//      at or below zero (buy) or down while at or above zero (sell); dots alternate buy/sell.
//
//  MARKER STYLE
//    Labels (default): tags drawn with Sierra Chart chart drawings. Regular signals are bold
//      coloured text with no background - "BUY" in bright cyan below the bar, "SELL" in bright
//      pink above it. A+ (4/4) signals get a filled tag, "BUY+" / "SELL+", with a soft glow
//      behind the bar. <Signal Tag Text> can say LONG / SHORT instead. <Pin Stem> hangs the tag
//      on a thin stem below / above the bar. "TR" tags for trampolines; a "TP" tag where a
//      target line ends; squeeze dots (small amber dots, off by default - "Show Squeeze Dots").
//      One tag per side per bar. Colours come from the matching subgraph colours (Buy, Sell,
//      Buy A+, Sell A+, Trampoline Buy, Target Hit, ...); text on filled tags is picked
//      automatically for contrast.
//    Classic shapes (USE_LABEL_TAGS = false): the original subgraph arrows, triangles, stars and squares.
//    The "Signal" subgraph (+score for buys, -score for sells) is filled in both styles for
//    use in alert formulas, the Spreadsheet study or automated trading.
//
//  Non-repainting: with "Evaluate On Bar Close Only" = Yes (default) a signal is only ever
//  set on a completed bar and never changes afterwards.
//
//  Subgraphs "Bull Score" / "Bear Score" carry the 0-4 score on qualifying extreme bars and
//  can be referenced from other studies, the Spreadsheet study, or automated trading.
// ============================================================================================

#include "sierrachart.h"
#include <math.h>
#include <vector>
#include <chrono>

SCDLLName("TO Method v4")

// =============================================================================================
// =============================================================================================
//
//      HOLY GRAIL REVERSALS  -  SETTINGS
//
//      These used to be study inputs. They are fixed here so the signal is the one that was
//      tested. To change one, edit the value, rebuild the study (Analysis >> Build Custom Studies
//      DLL) and reload the chart. Float values need the trailing "f".
//
// =============================================================================================
// =============================================================================================
namespace HolyGrail
{
    // ---------------------------------------------------------------- the reversal signal
    const int   EXTREME_LOOKBACK_BARS     = 20;    // signal bar must be the lowest low (buy) / highest high (sell) of this many bars
    const int   MIN_SCORE_TO_SIGNAL       = 3;     // 1-4. 3 = default, 2 = ~2.5x more signals with a smaller edge, 4 = A+ only
    const int   PERSISTENCE_COUNT         = 6;     // closes beyond the close 4 bars back (TD-style count) needed for the persistence point
    const int   DIVERGENCE_WINDOW_START   = 30;    // prior-swing window for the %B divergence point: from this many bars back...
    const int   DIVERGENCE_WINDOW_END     = 6;     // ...to this many bars back
    const int   ZSCORE_BOLLINGER_LENGTH   = 20;    // Bollinger length for the %B z-score (divergence point and blow-through veto)
    const bool  USE_VOLUME                = true;  // quiet-volume point and climax-volume veto
    const int   VOLUME_AVERAGE_LENGTH     = 20;
    const float QUIET_VOLUME_THRESHOLD    = 1.0f;  // x average volume: below this at the new extreme = quiet-volume point
    const float CLIMAX_VOLUME_VETO        = 2.5f;  // x average volume: any of the last 3 bars above this = no signal
    const int   TREND_EMA_LENGTH          = 200;   // trend-side point: buys above / sells below this EMA
    const float BAND_BLOWTHROUGH_VETO     = 3.0f;  // std devs: a close this far outside its 20-bar mean in the last 3 bars = no signal
    const float WIDE_RANGE_BAR_VETO       = 1.6f;  // x ATR: a signal bar wider than this = no signal
    const int   ATR_LENGTH                = 14;
    const int   COOLDOWN_BARS             = 5;     // bars between same-side signals
    const float STOP_BUFFER_ATR           = 0.1f;  // stop = signal-bar extreme -/+ this x ATR
    const float MIN_RISK_ATR              = 0.3f;  // risk is at least this x ATR
    const float TARGET_R                  = 1.5f;  // target = entry +/- this x risk
    const bool  SHOW_STOP_TARGET_LINES    = false;  // dashes at the latest signal's stop and target until one is hit
    const float TAG_OFFSET_ATR            = 0.3f;  // distance of the BUY / SELL tag from the bar (x ATR)
    const bool  EVALUATE_ON_BAR_CLOSE     = false;  // true = non-repainting: signals only on completed bars

    // ---------------------------------------------------------------- alert bands (alerts 3 and 4)
    const int   OPEN_EQ_TOLERANCE_TICKS   = 0;     // "open = prior close" within this many ticks (0 = exact)
    const int   ALERT_BB_LENGTH           = 20;
    const float ALERT_BB_STDDEV           = 2.0f;
    const bool  BAND_BREAK_ON_CLOSE       = false; // false = a wick beyond the band counts, true = needs a close beyond it
    const bool  SHOW_ALERT_BANDS          = true;
    const bool  MARK_OPEN_EQ_SIGNALS      = true;  // diamond at the open of open = prior close signals

    // ---------------------------------------------------------------- TraderOracle patterns
    const bool  SHOW_TRAMPOLINE           = true;
    const int   TRAMPOLINE_BAND_CHECK     = 0;     // 0 = as in TraderOracle (band check almost always passes), 1 = require band touch
    const int   TRAMPOLINE_RSI_OVERBOUGHT = 80;
    const int   TRAMPOLINE_RSI_OVERSOLD   = 20;
    const bool  SHOW_ENGULFING            = true;
    const int   ENGULFING_RULE            = 0;     // 0 = Olympus Engulfing BB, 1 = classic engulfing beyond band, 2 = classic anywhere
    const bool  ENGULFING_SKIP_DOJI       = true;
    const int   SQUEEZE_LENGTH            = 20;

    // ---------------------------------------------------------------- chart tags
    const bool  USE_LABEL_TAGS            = true;  // true = text tags (BUY / SELL / BUY+ ...), false = classic subgraph arrows
    const int   LABEL_FONT_SIZE           = 8;
    const char* const LABEL_FONT          = "Segoe UI";
    const bool  GLOW_BEHIND_A_PLUS        = true;

    // ---------------------------------------------------------------- Resistance Cloud (TraderOracle Method outer bands)
    const int   CLOUD_KERNEL_LOOKBACK     = 256;   // script "Lookback Window"
    const float CLOUD_RELATIVE_WEIGHT     = 3.5f;  // script "Relative Weighting"
    const int   CLOUD_START_AT_BAR        = 30;    // script "Start Regression at Bar" (the kernel averages this + 2 bars)
    const int   CLOUD_ATR_LENGTH          = 66;
    const float CLOUD_NEAR_ATR_FACTOR     = 1.95f; // inner edge of the cloud
    const float CLOUD_FAR_ATR_FACTOR      = 7.0f;  // outer edge = halfway between the near and far bounds
}
// =============================================================================================

namespace
{
    inline double UrsMax(double a, double b) { return a > b ? a : b; }
    inline int    UrsMaxI(int a, int b)      { return a > b ? a : b; }
    inline int    UrsMinI(int a, int b)      { return a < b ? a : b; }

    // Bollinger z-score of the close at bar k (population std dev, like Pine ta.stdev).
    bool ZScoreAt(SCStudyInterfaceRef sc, int k, int length, double& z)
    {
        if (k < length - 1)
            return false;
        double sum = 0.0;
        for (int j = k - length + 1; j <= k; ++j)
            sum += sc.Close[j];
        const double mean = sum / length;
        double ss = 0.0;
        for (int j = k - length + 1; j <= k; ++j)
        {
            const double d = sc.Close[j] - mean;
            ss += d * d;
        }
        const double sd = sqrt(ss / length);
        if (sd <= 0.0)
            return false;
        z = (sc.Close[k] - mean) / sd;
        return true;
    }

    // Volume at bar k relative to its simple average over `length` bars.
    bool RelVolumeAt(SCStudyInterfaceRef sc, int k, int length, double& rvol)
    {
        if (k < length - 1)
            return false;
        double sum = 0.0;
        for (int j = k - length + 1; j <= k; ++j)
            sum += sc.Volume[j];
        if (sum <= 0.0)
            return false;
        rvol = sc.Volume[k] / (sum / length);
        return true;
    }

    double TrueRange(SCStudyInterfaceRef sc, int j)
    {
        const double hl = sc.High[j] - sc.Low[j];
        if (j == 0)
            return hl;
        const double hc = fabs(sc.High[j] - sc.Close[j - 1]);
        const double lc = fabs(sc.Low[j] - sc.Close[j - 1]);
        return UrsMax(hl, UrsMax(hc, lc));
    }

    // Alert kinds, ordered by specificity: when several match in one update, the highest
    // one supplies the sound.
    enum
    {
        URS_ALERT_SQUEEZE = 0,        // alert 7
        URS_ALERT_ENGULF = 1,         // alert 6
        URS_ALERT_TRAMP = 2,          // alert 5
        URS_ALERT_WICK_CLOUD = 3,     // alert 8
        URS_ALERT_SQUEEZE_CLOUD = 4,  // alert 9
        URS_ALERT_TRAMP_CLOUD = 5,    // alert 10
        URS_ALERT_TARGET = 6,         // alert 2
        URS_ALERT_SIGNAL = 7,         // alert 1
        URS_ALERT_OPEN_EQ = 8,        // alert 3
        URS_ALERT_OPEN_EQ_BAND = 9,   // alert 4
        URS_ALERT_KINDS = 10
    };

    // Direction dropdown: 0 = Off, 1 = Buys only, 2 = Sells only, 3 = Buys and sells
    inline bool DirectionOn(int setting, bool isBuy)
    {
        return setting == 3 || (setting == 1 && isBuy) || (setting == 2 && !isBuy);
    }

    const int URS_MAX_EVENTS = 64;

    struct AlertEvent
    {
        bool IsTarget;      // target-hit event (else a signal event)
        int  Dir;           // 0 = buy, 1 = sell
        int  SignalBar;
        int  KindMask;      // alert kinds being reported
        int  Priority;
        int  Index;         // bar index the alert is attached to
        SCString Text;
    };

    // Persistent-int keys
    const int PKEY_LAST_ALERTED_BASE = 100;  // 100..119: last bar alerted, per kind and direction
    const int PKEY_ALERTED_LAST_CALL = 20;
    const int PKEY_LAST_ALERT_INDEX  = 21;
    const int PKEY_PENDING_FROM      = 22;   // earliest bar of events deferred to the next update (-1 = none)
    const int PKEY_EXT_SOUND         = 23;   // sound of other modules' alerts carried to the next update
    const int PKEY_EXT_TEXT          = 1;    // (persistent string) their text
}


// =============================================================================================
//  Alerts raised by the Little Rizzy module during an update. They are sent together with the
//  signal module's alerts as one Alert Manager entry at the end of the update.
// =============================================================================================
struct RG_ExternalAlerts
{
    enum { MAX = 16 };
    int      Count = 0;
    int      Sound[MAX];
    SCString Text[MAX];
    void Add(int sound, const char* text)
    {
        if (Count >= MAX)
            return;
        Sound[Count] = sound;
        Text[Count] = text;
        ++Count;
    }
    void Add(int sound, const SCString& text) { Add(sound, text.GetChars()); }
};

// Subgraph and input numbers of each module (inputs are numbered without gaps).
const int RG_SG_LITTLE_RIZZY   = 34;
const int RG_SG_VOLUME_PATTERN = 51;
const int RG_IN_SEP_GRAIL      = 0;
const int RG_IN_GRAIL          = 1;
const int RG_IN_SEP_RIZZY      = 31;
const int RG_IN_RIZZY          = 32;
const int RG_IN_SEP_VOLUME     = 45;
const int RG_IN_VOLUME         = 46;

// =============================================================================================
//  MODULE 1: REVERSAL GRAIL signal
// =============================================================================================
static void RG_SignalModule(SCStudyInterfaceRef sc, RG_ExternalAlerts& Ext)
{
    // ---------------------------------------------------------------- subgraphs
    SCSubgraphRef Subgraph_Buy            = sc.Subgraph[0];
    SCSubgraphRef Subgraph_Sell           = sc.Subgraph[1];
    SCSubgraphRef Subgraph_BuyAPlus       = sc.Subgraph[2];
    SCSubgraphRef Subgraph_SellAPlus      = sc.Subgraph[3];
    SCSubgraphRef Subgraph_Stop           = sc.Subgraph[4];
    SCSubgraphRef Subgraph_Target         = sc.Subgraph[5];
    SCSubgraphRef Subgraph_BullScore      = sc.Subgraph[6];
    SCSubgraphRef Subgraph_BearScore      = sc.Subgraph[7];
    SCSubgraphRef Subgraph_TargetHit      = sc.Subgraph[8];
    SCSubgraphRef Subgraph_OpenEqMark     = sc.Subgraph[9];
    SCSubgraphRef Subgraph_OpenEqBandMark = sc.Subgraph[10];
    SCSubgraphRef Subgraph_BandUpper      = sc.Subgraph[11];
    SCSubgraphRef Subgraph_BandLower      = sc.Subgraph[12];
    SCSubgraphRef Subgraph_TrampBuy       = sc.Subgraph[13];
    SCSubgraphRef Subgraph_TrampSell      = sc.Subgraph[14];
    SCSubgraphRef Subgraph_EngulfBull     = sc.Subgraph[15];
    SCSubgraphRef Subgraph_EngulfBear     = sc.Subgraph[16];
    SCSubgraphRef Subgraph_SqueezeBuy     = sc.Subgraph[17];
    SCSubgraphRef Subgraph_SqueezeSell    = sc.Subgraph[18];
    // internal (unnamed, not drawn): outputs of the Sierra Chart functions TraderOracle uses
    SCSubgraphRef Subgraph_PatRSI         = sc.Subgraph[19];
    SCSubgraphRef Subgraph_PatBB          = sc.Subgraph[20];
    SCSubgraphRef Subgraph_SqA            = sc.Subgraph[21];
    SCSubgraphRef Subgraph_SqD            = sc.Subgraph[22];
    SCSubgraphRef Subgraph_SqHist         = sc.Subgraph[23];
    SCFloatArrayRef Array_SqState         = Subgraph_SqHist.Arrays[0];   // squeeze dot state after each bar
    SCSubgraphRef Subgraph_Signal         = sc.Subgraph[24];
    // label bookkeeping (extra arrays of internal subgraphs)
    SCFloatArrayRef Array_PatFlags        = Subgraph_SqD.Arrays[0];   // 1 TR buy, 2 TR sell, 4 SQ buy, 8 SQ sell
    SCFloatArrayRef Array_LabelMask       = Subgraph_SqD.Arrays[1];   // labels currently drawn at each bar
    SCFloatArrayRef Array_TPFlags         = Subgraph_SqA.Arrays[0];   // 1 = a buy's target hit here, 2 = a sell's
    SCFloatArrayRef Array_TPBuyValue      = Subgraph_SqA.Arrays[1];
    SCFloatArrayRef Array_TPSellValue     = Subgraph_SqA.Arrays[2];

    // internal per-bar state (extra arrays of in-use subgraphs)
    SCFloatArrayRef Array_ATR        = Subgraph_Buy.Arrays[0];
    SCFloatArrayRef Array_EMA        = Subgraph_Buy.Arrays[1];
    SCFloatArrayRef Array_TDBull     = Subgraph_Buy.Arrays[2];
    SCFloatArrayRef Array_LastBuy    = Subgraph_Buy.Arrays[3];   // index of most recent buy at or before bar
    SCFloatArrayRef Array_BuyStop    = Subgraph_Buy.Arrays[4];
    SCFloatArrayRef Array_BuyTarget  = Subgraph_Buy.Arrays[5];
    SCFloatArrayRef Array_BuyFlags   = Subgraph_Buy.Arrays[6];   // 1 = open = prior close, 2 = band break
    SCFloatArrayRef Array_TDBear     = Subgraph_Sell.Arrays[0];
    SCFloatArrayRef Array_LastSell   = Subgraph_Sell.Arrays[1];
    SCFloatArrayRef Array_SellStop   = Subgraph_Sell.Arrays[2];
    SCFloatArrayRef Array_SellTarget = Subgraph_Sell.Arrays[3];
    SCFloatArrayRef Array_SellFlags  = Subgraph_Sell.Arrays[4];

    // trade tracking, indexed by the signal bar
    SCFloatArrayRef Array_BuyResBar     = Subgraph_BuyAPlus.Arrays[0];   // bar where the trade resolved
    SCFloatArrayRef Array_BuyResType    = Subgraph_BuyAPlus.Arrays[1];   // 0 open, 1 target, -1 stop, 2 expired
    SCFloatArrayRef Array_BuyTPAlerted  = Subgraph_BuyAPlus.Arrays[2];   // target event already handled
    SCFloatArrayRef Array_BuyTPLive     = Subgraph_BuyAPlus.Arrays[3];   // target touched live first: bar index + 1
    SCFloatArrayRef Array_SellResBar    = Subgraph_SellAPlus.Arrays[0];
    SCFloatArrayRef Array_SellResType   = Subgraph_SellAPlus.Arrays[1];
    SCFloatArrayRef Array_SellTPAlerted = Subgraph_SellAPlus.Arrays[2];
    SCFloatArrayRef Array_SellTPLive    = Subgraph_SellAPlus.Arrays[3];

    SCFloatArrayRef Array_BandUpper = Subgraph_BandUpper.Arrays[0];
    SCFloatArrayRef Array_BandLower = Subgraph_BandLower.Arrays[0];

    // Resistance Cloud: each Fill Top subgraph is immediately followed by its Fill Bottom.
    SCSubgraphRef Subgraph_CloudUpperOuter = sc.Subgraph[25];   // upper cloud, top edge (avg bound)
    SCSubgraphRef Subgraph_CloudUpperInner = sc.Subgraph[26];   // upper cloud, bottom edge (near bound)
    SCSubgraphRef Subgraph_CloudLowerInner = sc.Subgraph[27];   // lower cloud, top edge (near bound)
    SCSubgraphRef Subgraph_CloudLowerOuter = sc.Subgraph[28];   // lower cloud, bottom edge (avg bound)
    SCSubgraphRef Subgraph_WickInCloud     = sc.Subgraph[29];
    // Edge lines, so the cloud is visible even where a transparent fill is not drawn.
    SCSubgraphRef Subgraph_CloudUpperOuterLine = sc.Subgraph[30];
    SCSubgraphRef Subgraph_CloudUpperInnerLine = sc.Subgraph[31];
    SCSubgraphRef Subgraph_CloudLowerInnerLine = sc.Subgraph[32];
    SCSubgraphRef Subgraph_CloudLowerOuterLine = sc.Subgraph[33];
    SCFloatArrayRef Array_CloudYC     = Subgraph_CloudUpperOuter.Arrays[0];   // kernel average of close
    SCFloatArrayRef Array_CloudYH     = Subgraph_CloudUpperOuter.Arrays[1];   // ... of high
    SCFloatArrayRef Array_CloudYL     = Subgraph_CloudUpperOuter.Arrays[2];   // ... of low
    SCFloatArrayRef Array_CloudATR    = Subgraph_CloudUpperOuter.Arrays[3];   // Wilder ATR of the kernel series
    SCFloatArrayRef Array_UpperNear   = Subgraph_CloudUpperOuter.Arrays[4];
    SCFloatArrayRef Array_LowerNear   = Subgraph_CloudUpperOuter.Arrays[5];
    SCFloatArrayRef Array_UpperAvg    = Subgraph_CloudUpperOuter.Arrays[6];
    SCFloatArrayRef Array_LowerAvg    = Subgraph_CloudUpperOuter.Arrays[7];
    SCFloatArrayRef Array_CloudFlags  = Subgraph_CloudLowerOuter.Arrays[0];   // 1 = low in lower cloud, 2 = high in upper cloud

    // ---------------------------------------------------------------- inputs
    // Inputs are numbered without gaps: Sierra Chart stops listing inputs at the first unused number.
    SCInputRef Input_MaxTrackBars    = sc.Input[RG_IN_GRAIL + 0];

    SCInputRef Input_AlertSignal         = sc.Input[RG_IN_GRAIL + 1];
    SCInputRef Input_AlertSignalSound    = sc.Input[RG_IN_GRAIL + 2];
    SCInputRef Input_AlertTarget         = sc.Input[RG_IN_GRAIL + 3];
    SCInputRef Input_AlertTargetSound    = sc.Input[RG_IN_GRAIL + 4];
    SCInputRef Input_AlertOpenEq         = sc.Input[RG_IN_GRAIL + 5];
    SCInputRef Input_AlertOpenEqSound    = sc.Input[RG_IN_GRAIL + 6];
    SCInputRef Input_AlertOpenEqBand     = sc.Input[RG_IN_GRAIL + 7];
    SCInputRef Input_AlertOpenEqBandSound= sc.Input[RG_IN_GRAIL + 8];

    SCInputRef Input_ShowSqueeze         = sc.Input[RG_IN_GRAIL + 9];
    SCInputRef Input_SqueezeOffset       = sc.Input[RG_IN_GRAIL + 10];
    SCInputRef Input_TrampOffset         = sc.Input[RG_IN_GRAIL + 11];
    SCInputRef Input_AlertTramp          = sc.Input[RG_IN_GRAIL + 12];
    SCInputRef Input_AlertTrampSound     = sc.Input[RG_IN_GRAIL + 13];
    SCInputRef Input_AlertEngulf         = sc.Input[RG_IN_GRAIL + 14];
    SCInputRef Input_AlertEngulfSound    = sc.Input[RG_IN_GRAIL + 15];
    SCInputRef Input_AlertSqueeze        = sc.Input[RG_IN_GRAIL + 16];
    SCInputRef Input_AlertSqueezeSound   = sc.Input[RG_IN_GRAIL + 17];

    SCInputRef Input_ShowCloud           = sc.Input[RG_IN_GRAIL + 18];
    SCInputRef Input_CloudTransparency   = sc.Input[RG_IN_GRAIL + 19];
    SCInputRef Input_TagText             = sc.Input[RG_IN_GRAIL + 20];
    SCInputRef Input_PinStem             = sc.Input[RG_IN_GRAIL + 21];
    SCInputRef Input_PinStemLength       = sc.Input[RG_IN_GRAIL + 22];
    SCInputRef Input_AlertWickCloud      = sc.Input[RG_IN_GRAIL + 23];
    SCInputRef Input_AlertWickCloudSound = sc.Input[RG_IN_GRAIL + 24];
    SCInputRef Input_AlertSqCloud        = sc.Input[RG_IN_GRAIL + 25];
    SCInputRef Input_AlertSqCloudSound   = sc.Input[RG_IN_GRAIL + 26];
    SCInputRef Input_AlertTrCloud        = sc.Input[RG_IN_GRAIL + 27];
    SCInputRef Input_AlertTrCloudSound   = sc.Input[RG_IN_GRAIL + 28];
    SCInputRef Input_Alert4Band          = sc.Input[RG_IN_GRAIL + 29];

    if (sc.SetDefaults)
    {
        sc.GraphName = "TO Method v4";
        sc.StudyDescription =
            "Reversal signal at a fresh N-bar extreme, scored 0-4 on persistence (TD-style count), "
            "%B momentum divergence, quiet volume and 200-EMA trend side, with vetoes for band "
            "blow-throughs, wide-range bars and climax volume. Non-repainting on bar close. "
            "Alerts for new arrows, targets hit before the stop, signal bars that open at the prior "
            "close, and those that also broke the outer Bollinger band. Also shows the TraderOracle "
            "trampoline, engulfing-at-band and squeeze dot patterns and the TraderOracle Method "
            "Resistance Cloud (Nadaraya-Watson envelope), with their own alerts. Includes the Little "
            "Rizzy measured-move levels and the Reversal Volume Patterns.";
        sc.AutoLoop = 0;
        sc.GraphRegion = 0;
        sc.ValueFormat = VALUEFORMAT_INHERITED;
        sc.AlertOnlyOncePerBar = 0;
        sc.ResetAlertOnNewBar = 1;

        Subgraph_Buy.Name = "Buy";
        Subgraph_Buy.DrawStyle = DRAWSTYLE_ARROW_UP;
        Subgraph_Buy.PrimaryColor = RGB(0, 230, 255);
        Subgraph_Buy.LineWidth = 3;
        Subgraph_Buy.DrawZeros = false;

        Subgraph_Sell.Name = "Sell";
        Subgraph_Sell.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        Subgraph_Sell.PrimaryColor = RGB(255, 60, 170);
        Subgraph_Sell.LineWidth = 3;
        Subgraph_Sell.DrawZeros = false;

        Subgraph_BuyAPlus.Name = "Buy A+ (score 4)";
        Subgraph_BuyAPlus.DrawStyle = DRAWSTYLE_ARROW_UP;
        Subgraph_BuyAPlus.PrimaryColor = RGB(0, 230, 255);
        Subgraph_BuyAPlus.LineWidth = 6;
        Subgraph_BuyAPlus.DrawZeros = false;

        Subgraph_SellAPlus.Name = "Sell A+ (score 4)";
        Subgraph_SellAPlus.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        Subgraph_SellAPlus.PrimaryColor = RGB(255, 60, 170);
        Subgraph_SellAPlus.LineWidth = 6;
        Subgraph_SellAPlus.DrawZeros = false;

        Subgraph_Stop.Name = "Stop";
        Subgraph_Stop.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_Stop.PrimaryColor = RGB(200, 80, 80);
        Subgraph_Stop.LineWidth = 2;
        Subgraph_Stop.DrawZeros = false;

        Subgraph_Target.Name = "Target";
        Subgraph_Target.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_Target.PrimaryColor = RGB(80, 160, 255);
        Subgraph_Target.LineWidth = 2;
        Subgraph_Target.DrawZeros = false;

        Subgraph_BullScore.Name = "Bull Score";
        Subgraph_BullScore.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_BullScore.PrimaryColor = RGB(0, 200, 120);
        Subgraph_BullScore.DrawZeros = false;

        Subgraph_BearScore.Name = "Bear Score";
        Subgraph_BearScore.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_BearScore.PrimaryColor = RGB(230, 60, 60);
        Subgraph_BearScore.DrawZeros = false;

        Subgraph_TargetHit.Name = "Target Hit";
        Subgraph_TargetHit.DrawStyle = DRAWSTYLE_SQUARE;
        Subgraph_TargetHit.PrimaryColor = RGB(255, 200, 60);
        Subgraph_TargetHit.LineWidth = 4;
        Subgraph_TargetHit.DrawZeros = false;

        Subgraph_OpenEqMark.Name = "Open = Prior Close";
        Subgraph_OpenEqMark.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_OpenEqMark.PrimaryColor = RGB(120, 170, 255);
        Subgraph_OpenEqMark.LineWidth = 4;
        Subgraph_OpenEqMark.DrawZeros = false;

        Subgraph_OpenEqBandMark.Name = "Open = Prior Close + Band Break";
        Subgraph_OpenEqBandMark.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_OpenEqBandMark.PrimaryColor = RGB(255, 140, 0);
        Subgraph_OpenEqBandMark.LineWidth = 6;
        Subgraph_OpenEqBandMark.DrawZeros = false;

        Subgraph_BandUpper.Name = "Alert Band Upper";
        Subgraph_BandUpper.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_BandUpper.PrimaryColor = RGB(130, 130, 160);
        Subgraph_BandUpper.LineWidth = 1;
        Subgraph_BandUpper.DrawZeros = false;

        Subgraph_BandLower.Name = "Alert Band Lower";
        Subgraph_BandLower.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_BandLower.PrimaryColor = RGB(130, 130, 160);
        Subgraph_BandLower.LineWidth = 1;
        Subgraph_BandLower.DrawZeros = false;

        // ------------------------------------------------ signal inputs

        Input_MaxTrackBars.Name = "Max Bars To Track Stop/Target";
        Input_MaxTrackBars.SetInt(30);
        Input_MaxTrackBars.SetIntLimits(1, 2000);
        Input_MaxTrackBars.SetDescription("A signal's stop and target are watched for this many bars; after that the trade is treated as expired and no target alert is given.");

        // ------------------------------------------------ alert inputs
        const char* DIRECTIONS = "Off;Buys only;Sells only;Buys and sells";

        Input_AlertSignal.Name = "Alert 1: Signal Arrow Appears";
        Input_AlertSignal.SetCustomInputStrings(DIRECTIONS);
        Input_AlertSignal.SetCustomInputIndex(3);

        Input_AlertSignalSound.Name = "Alert 1: Sound Number (0 = log only)";
        Input_AlertSignalSound.SetInt(1);
        Input_AlertSignalSound.SetIntLimits(0, 150);

        Input_AlertTarget.Name = "Alert 2: Target Hit Before Stop";
        Input_AlertTarget.SetCustomInputStrings(DIRECTIONS);
        Input_AlertTarget.SetCustomInputIndex(3);

        Input_AlertTargetSound.Name = "Alert 2: Sound Number (0 = log only)";
        Input_AlertTargetSound.SetInt(2);
        Input_AlertTargetSound.SetIntLimits(0, 150);

        Input_AlertOpenEq.Name = "Alert 3: Signal Bar Open = Prior Close";
        Input_AlertOpenEq.SetCustomInputStrings(DIRECTIONS);
        Input_AlertOpenEq.SetCustomInputIndex(3);

        Input_AlertOpenEqSound.Name = "Alert 3: Sound Number (0 = log only)";
        Input_AlertOpenEqSound.SetInt(3);
        Input_AlertOpenEqSound.SetIntLimits(0, 150);

        Input_AlertOpenEqBand.Name = "Alert 4: Open = Prior Close + Band Break";
        Input_AlertOpenEqBand.SetCustomInputStrings(DIRECTIONS);
        Input_AlertOpenEqBand.SetCustomInputIndex(3);

        Input_AlertOpenEqBandSound.Name = "Alert 4: Sound Number (0 = log only)";
        Input_AlertOpenEqBandSound.SetInt(4);
        Input_AlertOpenEqBandSound.SetIntLimits(0, 150);

        // ------------------------------------------------ TraderOracle patterns
        Subgraph_TrampBuy.Name = "Trampoline Buy";
        Subgraph_TrampBuy.DrawStyle = DRAWSTYLE_TRIANGLE_UP;
        Subgraph_TrampBuy.PrimaryColor = RGB(124, 156, 255);
        Subgraph_TrampBuy.LineWidth = 3;
        Subgraph_TrampBuy.DrawZeros = false;

        Subgraph_TrampSell.Name = "Trampoline Sell";
        Subgraph_TrampSell.DrawStyle = DRAWSTYLE_TRIANGLE_DOWN;
        Subgraph_TrampSell.PrimaryColor = RGB(124, 156, 255);
        Subgraph_TrampSell.LineWidth = 3;
        Subgraph_TrampSell.DrawZeros = false;

        Subgraph_EngulfBull.Name = "Engulfing Green BB";
        Subgraph_EngulfBull.DrawStyle = DRAWSTYLE_BACKGROUND;
        Subgraph_EngulfBull.PrimaryColor = RGB(0, 64, 0);
        Subgraph_EngulfBull.LineWidth = 1;
        Subgraph_EngulfBull.DrawZeros = false;

        Subgraph_EngulfBear.Name = "Engulfing Red BB";
        Subgraph_EngulfBear.DrawStyle = DRAWSTYLE_BACKGROUND;
        Subgraph_EngulfBear.PrimaryColor = RGB(70, 0, 35);
        Subgraph_EngulfBear.LineWidth = 1;
        Subgraph_EngulfBear.DrawZeros = false;

        Subgraph_SqueezeBuy.Name = "Squeeze Buy Dot";
        Subgraph_SqueezeBuy.DrawStyle = DRAWSTYLE_STAR;
        Subgraph_SqueezeBuy.PrimaryColor = RGB(255, 196, 0);
        Subgraph_SqueezeBuy.LineWidth = 1;
        Subgraph_SqueezeBuy.DrawZeros = false;

        Subgraph_SqueezeSell.Name = "Squeeze Sell Dot";
        Subgraph_SqueezeSell.DrawStyle = DRAWSTYLE_STAR;
        Subgraph_SqueezeSell.PrimaryColor = RGB(255, 196, 0);
        Subgraph_SqueezeSell.LineWidth = 1;
        Subgraph_SqueezeSell.DrawZeros = false;

        Input_ShowSqueeze.Name = "Show Squeeze Dots";
        Input_ShowSqueeze.SetYesNo(0);

        Input_SqueezeOffset.Name = "Squeeze Dot Offset (ticks)";
        Input_SqueezeOffset.SetInt(2);
        Input_SqueezeOffset.SetIntLimits(0, 1000);

        Input_TrampOffset.Name = "Trampoline Marker Offset (ticks)";
        Input_TrampOffset.SetInt(6);
        Input_TrampOffset.SetIntLimits(0, 1000);

        Input_AlertTramp.Name = "Alert 5: Trampoline";
        Input_AlertTramp.SetCustomInputStrings(DIRECTIONS);
        Input_AlertTramp.SetCustomInputIndex(0);

        Input_AlertTrampSound.Name = "Alert 5: Sound Number (0 = log only)";
        Input_AlertTrampSound.SetInt(5);
        Input_AlertTrampSound.SetIntLimits(0, 150);

        Input_AlertEngulf.Name = "Alert 6: Engulfing At Band";
        Input_AlertEngulf.SetCustomInputStrings(DIRECTIONS);
        Input_AlertEngulf.SetCustomInputIndex(0);

        Input_AlertEngulfSound.Name = "Alert 6: Sound Number (0 = log only)";
        Input_AlertEngulfSound.SetInt(6);
        Input_AlertEngulfSound.SetIntLimits(0, 150);

        Input_AlertSqueeze.Name = "Alert 7: Squeeze Dot";
        Input_AlertSqueeze.SetCustomInputStrings(DIRECTIONS);
        Input_AlertSqueeze.SetCustomInputIndex(0);

        Input_AlertSqueezeSound.Name = "Alert 7: Sound Number (0 = log only)";
        Input_AlertSqueezeSound.SetInt(7);
        Input_AlertSqueezeSound.SetIntLimits(0, 150);

        // ------------------------------------------------ marker style
        Subgraph_PatRSI.Name = "(internal) Pattern RSI";
        Subgraph_PatRSI.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_PatBB.Name = "(internal) Pattern Bollinger";
        Subgraph_PatBB.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_SqA.Name = "(internal) Squeeze Average";
        Subgraph_SqA.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_SqD.Name = "(internal) Squeeze Delta";
        Subgraph_SqD.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_SqHist.Name = "(internal) Squeeze Histogram";
        Subgraph_SqHist.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_Signal.Name = "Signal";
        Subgraph_Signal.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_Signal.PrimaryColor = RGB(160, 160, 160);
        Subgraph_Signal.DrawZeros = false;

        Input_TagText.Name = "Signal Tag Text";
        Input_TagText.SetCustomInputStrings("BUY / SELL (A+ = BUY+ / SELL+);LONG / SHORT (A+ = LONG+ / SHORT+)");
        Input_TagText.SetCustomInputIndex(0);

        Input_PinStem.Name = "Pin Stem Under Signal Tags";
        Input_PinStem.SetYesNo(0);
        Input_PinStem.SetDescription("Hang each tag on a thin stem, further from the bar.");

        Input_PinStemLength.Name = "Pin Stem Length (x ATR)";
        Input_PinStemLength.SetFloat(1.0f);
        Input_PinStemLength.SetFloatLimits(0.1f, 20.0f);

        // ------------------------------------------------ Resistance Cloud
        Subgraph_CloudUpperOuter.Name = "Upper Cloud Outer Edge";
        Subgraph_CloudUpperOuter.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_TOP;
        Subgraph_CloudUpperOuter.PrimaryColor = RGB(224, 49, 73);
        Subgraph_CloudUpperOuter.DrawZeros = false;

        Subgraph_CloudUpperInner.Name = "Upper Cloud Inner Edge";
        Subgraph_CloudUpperInner.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_BOTTOM;
        Subgraph_CloudUpperInner.PrimaryColor = RGB(224, 49, 73);
        Subgraph_CloudUpperInner.DrawZeros = false;

        Subgraph_CloudLowerInner.Name = "Lower Cloud Inner Edge";
        Subgraph_CloudLowerInner.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_TOP;
        Subgraph_CloudLowerInner.PrimaryColor = RGB(0, 168, 107);
        Subgraph_CloudLowerInner.DrawZeros = false;

        Subgraph_CloudLowerOuter.Name = "Lower Cloud Outer Edge";
        Subgraph_CloudLowerOuter.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_BOTTOM;
        Subgraph_CloudLowerOuter.PrimaryColor = RGB(0, 168, 107);
        Subgraph_CloudLowerOuter.DrawZeros = false;

        Subgraph_CloudUpperOuterLine.Name = "Upper Cloud Outer Line";
        Subgraph_CloudUpperOuterLine.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_CloudUpperOuterLine.PrimaryColor = RGB(150, 40, 60);
        Subgraph_CloudUpperOuterLine.LineWidth = 1;
        Subgraph_CloudUpperOuterLine.DrawZeros = false;

        Subgraph_CloudUpperInnerLine.Name = "Upper Cloud Inner Line";
        Subgraph_CloudUpperInnerLine.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_CloudUpperInnerLine.PrimaryColor = RGB(224, 49, 73);
        Subgraph_CloudUpperInnerLine.LineWidth = 2;
        Subgraph_CloudUpperInnerLine.DrawZeros = false;

        Subgraph_CloudLowerInnerLine.Name = "Lower Cloud Inner Line";
        Subgraph_CloudLowerInnerLine.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_CloudLowerInnerLine.PrimaryColor = RGB(0, 168, 107);
        Subgraph_CloudLowerInnerLine.LineWidth = 2;
        Subgraph_CloudLowerInnerLine.DrawZeros = false;

        Subgraph_CloudLowerOuterLine.Name = "Lower Cloud Outer Line";
        Subgraph_CloudLowerOuterLine.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_CloudLowerOuterLine.PrimaryColor = RGB(0, 110, 72);
        Subgraph_CloudLowerOuterLine.LineWidth = 1;
        Subgraph_CloudLowerOuterLine.DrawZeros = false;

        Subgraph_WickInCloud.Name = "Large Wick In Cloud";
        Subgraph_WickInCloud.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_WickInCloud.PrimaryColor = RGB(255, 196, 0);
        Subgraph_WickInCloud.LineWidth = 3;
        Subgraph_WickInCloud.DrawZeros = false;

        Input_ShowCloud.Name = "Show Resistance Cloud";
        Input_ShowCloud.SetYesNo(1);

        Input_CloudTransparency.Name = "Cloud Transparency %";
        Input_CloudTransparency.SetInt(78);
        Input_CloudTransparency.SetIntLimits(0, 100);
        Input_CloudTransparency.SetDescription("Applied to the study's Transparency Level (used only by the cloud fill).");

        Input_AlertWickCloud.Name = "Alert 8: Large Wick In Cloud";
        Input_AlertWickCloud.SetCustomInputStrings(DIRECTIONS);
        Input_AlertWickCloud.SetCustomInputIndex(0);

        Input_AlertWickCloudSound.Name = "Alert 8: Sound Number (0 = log only)";
        Input_AlertWickCloudSound.SetInt(8);
        Input_AlertWickCloudSound.SetIntLimits(0, 150);

        Input_AlertSqCloud.Name = "Alert 9: Squeeze Dot In Cloud";
        Input_AlertSqCloud.SetCustomInputStrings(DIRECTIONS);
        Input_AlertSqCloud.SetCustomInputIndex(0);

        Input_AlertSqCloudSound.Name = "Alert 9: Sound Number (0 = log only)";
        Input_AlertSqCloudSound.SetInt(9);
        Input_AlertSqCloudSound.SetIntLimits(0, 150);

        Input_AlertTrCloud.Name = "Alert 10: Trampoline In Cloud";
        Input_AlertTrCloud.SetCustomInputStrings(DIRECTIONS);
        Input_AlertTrCloud.SetCustomInputIndex(0);

        Input_AlertTrCloudSound.Name = "Alert 10: Sound Number (0 = log only)";
        Input_AlertTrCloudSound.SetInt(10);
        Input_AlertTrCloudSound.SetIntLimits(0, 150);

        Input_Alert4Band.Name = "Alert 4 Band";
        Input_Alert4Band.SetCustomInputStrings("Alert Bollinger Bands;Resistance Cloud inner edge");
        Input_Alert4Band.SetCustomInputIndex(0);

        return;
    }

    // ---------------------------------------------------------------- settings
    const int    lookback    = HolyGrail::EXTREME_LOOKBACK_BARS;
    const int    minScore    = HolyGrail::MIN_SCORE_TO_SIGNAL;
    const int    tdMin       = HolyGrail::PERSISTENCE_COUNT;
    const int    divFar      = UrsMaxI(HolyGrail::DIVERGENCE_WINDOW_START, HolyGrail::DIVERGENCE_WINDOW_END + 1);
    const int    divNear     = HolyGrail::DIVERGENCE_WINDOW_END;
    const int    bbLen       = HolyGrail::ZSCORE_BOLLINGER_LENGTH;
    const bool   useVol      = HolyGrail::USE_VOLUME;
    const int    volLen      = HolyGrail::VOLUME_AVERAGE_LENGTH;
    const double quietRVol   = HolyGrail::QUIET_VOLUME_THRESHOLD;
    const double climaxVeto  = HolyGrail::CLIMAX_VOLUME_VETO;
    const int    emaLen      = HolyGrail::TREND_EMA_LENGTH;
    const double zVeto       = HolyGrail::BAND_BLOWTHROUGH_VETO;
    const double rangeVeto   = HolyGrail::WIDE_RANGE_BAR_VETO;
    const int    atrLen      = HolyGrail::ATR_LENGTH;
    const int    cooldown    = HolyGrail::COOLDOWN_BARS;
    const double stopBuf     = HolyGrail::STOP_BUFFER_ATR;
    const double minRisk     = HolyGrail::MIN_RISK_ATR;
    const double targetR     = HolyGrail::TARGET_R;
    const bool   showLevels  = HolyGrail::SHOW_STOP_TARGET_LINES;
    const double arrowOff    = HolyGrail::TAG_OFFSET_ATR;
    const bool   closeOnly   = HolyGrail::EVALUATE_ON_BAR_CLOSE;
    const int    maxTrack    = UrsMaxI(1, Input_MaxTrackBars.GetInt());

    int alertDir[URS_ALERT_KINDS];
    int alertSound[URS_ALERT_KINDS];
    alertDir[URS_ALERT_TARGET]       = Input_AlertTarget.GetIndex();
    alertDir[URS_ALERT_SIGNAL]       = Input_AlertSignal.GetIndex();
    alertDir[URS_ALERT_OPEN_EQ]      = Input_AlertOpenEq.GetIndex();
    alertDir[URS_ALERT_OPEN_EQ_BAND] = Input_AlertOpenEqBand.GetIndex();
    alertSound[URS_ALERT_TARGET]       = Input_AlertTargetSound.GetInt();
    alertSound[URS_ALERT_SIGNAL]       = Input_AlertSignalSound.GetInt();
    alertSound[URS_ALERT_OPEN_EQ]      = Input_AlertOpenEqSound.GetInt();
    alertSound[URS_ALERT_OPEN_EQ_BAND] = Input_AlertOpenEqBandSound.GetInt();
    alertDir[URS_ALERT_TRAMP]     = Input_AlertTramp.GetIndex();
    alertDir[URS_ALERT_ENGULF]    = Input_AlertEngulf.GetIndex();
    alertDir[URS_ALERT_SQUEEZE]   = Input_AlertSqueeze.GetIndex();
    alertSound[URS_ALERT_TRAMP]   = Input_AlertTrampSound.GetInt();
    alertSound[URS_ALERT_ENGULF]  = Input_AlertEngulfSound.GetInt();
    alertSound[URS_ALERT_SQUEEZE] = Input_AlertSqueezeSound.GetInt();
    alertDir[URS_ALERT_WICK_CLOUD]      = Input_AlertWickCloud.GetIndex();
    alertDir[URS_ALERT_SQUEEZE_CLOUD]   = Input_AlertSqCloud.GetIndex();
    alertDir[URS_ALERT_TRAMP_CLOUD]     = Input_AlertTrCloud.GetIndex();
    alertSound[URS_ALERT_WICK_CLOUD]    = Input_AlertWickCloudSound.GetInt();
    alertSound[URS_ALERT_SQUEEZE_CLOUD] = Input_AlertSqCloudSound.GetInt();
    alertSound[URS_ALERT_TRAMP_CLOUD]   = Input_AlertTrCloudSound.GetInt();

    const double tick        = (sc.TickSize > 0) ? sc.TickSize : 1e-8;
    const double openEqTol   = (HolyGrail::OPEN_EQ_TOLERANCE_TICKS + 0.5) * tick;
    const int    bandLen     = HolyGrail::ALERT_BB_LENGTH;
    const double bandMult    = HolyGrail::ALERT_BB_STDDEV;
    const bool   bandOnClose = HolyGrail::BAND_BREAK_ON_CLOSE;
    const bool   showBands   = HolyGrail::SHOW_ALERT_BANDS;
    const bool   showOpenEq  = HolyGrail::MARK_OPEN_EQ_SIGNALS;

    const bool   showTramp     = HolyGrail::SHOW_TRAMPOLINE;
    const int    trampMode     = HolyGrail::TRAMPOLINE_BAND_CHECK;
    const float  trampRSIHigh  = (float)HolyGrail::TRAMPOLINE_RSI_OVERBOUGHT;
    const float  trampRSILow   = (float)HolyGrail::TRAMPOLINE_RSI_OVERSOLD;
    const bool   showEngulf    = HolyGrail::SHOW_ENGULFING;
    const int    engulfRule    = HolyGrail::ENGULFING_RULE;
    const bool   engulfNoDoji  = HolyGrail::ENGULFING_SKIP_DOJI;
    const bool   showSqueeze   = Input_ShowSqueeze.GetYesNo() != 0;
    const int    sqLen         = HolyGrail::SQUEEZE_LENGTH;
    const double sqOffset      = Input_SqueezeOffset.GetInt() * (double)sc.TickSize;
    const double trampOffset   = Input_TrampOffset.GetInt() * (double)sc.TickSize;

    const bool   useLabels     = HolyGrail::USE_LABEL_TAGS;
    const bool   classic       = !useLabels;
    const int    labelSize     = HolyGrail::LABEL_FONT_SIZE;
    const bool   labelGlow     = HolyGrail::GLOW_BEHIND_A_PLUS;
    const int    tagText       = Input_TagText.GetIndex();
    const bool   pinStem       = Input_PinStem.GetYesNo() != 0;
    const double pinStemLen    = Input_PinStemLength.GetFloat();

    // Resistance Cloud (Nadaraya-Watson envelope) settings and kernel weights
    const bool   showCloud     = Input_ShowCloud.GetYesNo() != 0;
    const int    cloudLookback = UrsMaxI(1, HolyGrail::CLOUD_KERNEL_LOOKBACK);
    const double cloudRelW     = HolyGrail::CLOUD_RELATIVE_WEIGHT;
    const int    cloudWindow   = UrsMaxI(0, HolyGrail::CLOUD_START_AT_BAR) + 2;
    const int    cloudATRLen   = UrsMaxI(1, HolyGrail::CLOUD_ATR_LENGTH);
    const double cloudNear     = HolyGrail::CLOUD_NEAR_ATR_FACTOR;
    const double cloudFar      = HolyGrail::CLOUD_FAR_ATR_FACTOR;
    const int    cloudFirst    = (cloudWindow - 1) + (cloudATRLen - 1);   // first bar with a cloud
    const bool   alert4Cloud   = Input_Alert4Band.GetIndex() == 1;
    std::vector<double> cloudW(cloudWindow);
    double cloudWSum = 0.0;
    for (int k = 0; k < cloudWindow; ++k)
    {
        cloudW[k] = pow(1.0 + (double)k * k / ((double)cloudLookback * cloudLookback * 2.0 * cloudRelW), -cloudRelW);
        cloudWSum += cloudW[k];
    }
    // True range of the kernel series at bar k (the script's kernel_atr)
    auto cloudTR = [&](int k) -> double
    {
        const double yh = Array_CloudYH[k], yl = Array_CloudYL[k];
        if (k == cloudWindow - 1)
            return yh - yl;
        const double pc = Array_CloudYC[k - 1];
        return UrsMax(UrsMax(yh - yl, fabs(yh - pc)), fabs(yl - pc));
    };

    const double emaAlpha = 2.0 / (emaLen + 1.0);
    const int warmup = UrsMaxI(UrsMaxI(lookback, divFar), UrsMaxI(bbLen + 2, atrLen));
    const float NO_SIGNAL = -1.0e9f;

    const bool fullRecalc = sc.IsFullRecalculation != 0;
    const bool resetState = (sc.UpdateStartIndex == 0);
    int& pendingFrom = sc.GetPersistentInt(PKEY_PENDING_FROM);

    if (resetState)
    {
        for (int k = 0; k < URS_ALERT_KINDS; ++k)
            for (int d = 0; d < 2; ++d)
                sc.GetPersistentInt(PKEY_LAST_ALERTED_BASE + 2 * k + d) = -1;
        sc.GetPersistentInt(PKEY_ALERTED_LAST_CALL) = 0;
        sc.GetPersistentInt(PKEY_LAST_ALERT_INDEX) = -1;
        pendingFrom = -1;

        // The cloud fill uses the study's Transparency Level; keep it at the input's value.
        if (showCloud)
        {
            const int wantTransparency = Input_CloudTransparency.GetInt();
            if (sc.GetChartStudyTransparencyLevel(sc.ChartNumber, sc.StudyGraphInstanceID) != wantTransparency)
                sc.SetChartStudyTransparencyLevel(sc.ChartNumber, sc.StudyGraphInstanceID, wantTransparency);
        }
    }

    // Re-evaluate the previous bar as well: when a new bar opens, the bar before it has
    // just closed and is now eligible for a signal.
    int start = sc.UpdateStartIndex;
    if (start > 0)
        --start;
    // Bars at or after freshFrom changed in this update; events on earlier bars are history.
    int freshFrom = sc.UpdateStartIndex - 1;
    if (pendingFrom >= 0)
    {
        // Alerts deferred by the previous update: re-scan from their bars so they are found again.
        start = UrsMinI(start, pendingFrom);
        freshFrom = UrsMinI(freshFrom, pendingFrom);
    }

    AlertEvent events[URS_MAX_EVENTS];
    int eventCount = 0;

    // Signal arrow alerts (kinds 1, 3, 4) for a signal on bar i.
    auto queueSignalAlert = [&](int i, bool isBuy, int score, int flags, double stop, double target)
    {
        const int d = isBuy ? 0 : 1;
        bool matched[URS_ALERT_KINDS];
        for (int k = 0; k < URS_ALERT_KINDS; ++k)
            matched[k] = false;
        matched[URS_ALERT_SIGNAL]       = true;
        matched[URS_ALERT_OPEN_EQ]      = (flags & 1) != 0;
        matched[URS_ALERT_OPEN_EQ_BAND] = (flags & 3) == 3;

        int mask = 0, priority = -1;
        for (int k = URS_ALERT_SIGNAL; k < URS_ALERT_KINDS; ++k)
        {
            if (!matched[k] || !DirectionOn(alertDir[k], isBuy))
                continue;
            int& lastAlerted = sc.GetPersistentInt(PKEY_LAST_ALERTED_BASE + 2 * k + d);
            if (i <= lastAlerted)
                continue;
            if (fullRecalc || i < freshFrom)
            {
                lastAlerted = i;   // history: remember it, never alert it
                continue;
            }
            mask |= 1 << k;
            priority = k;
        }
        if (mask == 0 || eventCount >= URS_MAX_EVENTS)
            return;

        AlertEvent& e = events[eventCount++];
        e.IsTarget = false;
        e.Dir = d;
        e.SignalBar = i;
        e.KindMask = mask;
        e.Priority = priority;
        e.Index = i;
        e.Text.Format("%s signal, score %d", isBuy ? "BUY" : "SELL", score);
        if (flags & 1)
            e.Text.AppendFormat(", open = prior close");
        if (flags & 2)
            e.Text.AppendFormat(alert4Cloud ? ", cloud break" : ", band break");
        e.Text.AppendFormat(", stop %s, target %s",
            sc.FormatGraphValue(stop, sc.BaseGraphValueFormat).GetChars(),
            sc.FormatGraphValue(target, sc.BaseGraphValueFormat).GetChars());
    };

    // Pattern alerts (alerts 5-10) for a pattern completed on bar i. `kind` is the plain pattern
    // alert (-1 = none) and `cloudKind` its "in cloud" alert (-1 = the bar is not in the cloud);
    // both are reported as one event.
    auto queuePatternAlert = [&](int i, int kind, int cloudKind, bool isBuy, const char* what)
    {
        const int d = isBuy ? 0 : 1;
        const int kinds[2] = { kind, cloudKind };
        int mask = 0, priority = -1;
        for (int n = 0; n < 2; ++n)
        {
            const int k = kinds[n];
            if (k < 0 || !DirectionOn(alertDir[k], isBuy))
                continue;
            int& lastAlerted = sc.GetPersistentInt(PKEY_LAST_ALERTED_BASE + 2 * k + d);
            if (i <= lastAlerted)
                continue;
            if (fullRecalc || i < freshFrom)
            {
                lastAlerted = i;
                continue;
            }
            mask |= 1 << k;
            priority = UrsMaxI(priority, k);
        }
        if (mask == 0 || eventCount >= URS_MAX_EVENTS)
            return;
        AlertEvent& e = events[eventCount++];
        e.IsTarget = false;
        e.Dir = d;
        e.SignalBar = i;
        e.KindMask = mask;
        e.Priority = priority;
        e.Index = i;
        e.Text.Format("%s %s%s at %s", isBuy ? "BUY" : "SELL", what, cloudKind >= 0 ? " in cloud" : "",
            sc.FormatGraphValue(sc.Close[i], sc.BaseGraphValueFormat).GetChars());
    };

    // Did bar k break the outer band (alert Bollinger band or cloud inner edge) on the signal's side?
    auto bandBroken = [&](int k, bool isBuy) -> bool
    {
        if (k < 0 || k < (alert4Cloud ? cloudFirst : bandLen - 1))
            return false;
        const float lower = alert4Cloud ? Array_LowerNear[k] : Array_BandLower[k];
        const float upper = alert4Cloud ? Array_UpperNear[k] : Array_BandUpper[k];
        if (isBuy)
            return (bandOnClose ? sc.Close[k] : sc.Low[k]) < lower;
        return (bandOnClose ? sc.Close[k] : sc.High[k]) > upper;
    };

    // Did bar k reach the cloud on the buy (lower) or sell (upper) side?
    auto inCloud = [&](int k, bool isBuy) -> bool
    {
        if (k < cloudFirst)
            return false;
        return ((int)Array_CloudFlags[k] & (isBuy ? 1 : 2)) != 0;
    };
    // A trampoline at bar i is "in cloud" when any of its three bars reached the cloud.
    auto trampInCloud = [&](int i, bool isBuy) -> bool
    {
        return inCloud(i, isBuy) || inCloud(i - 1, isBuy) || inCloud(i - 2, isBuy);
    };

    // ================================================================ pass 1: signals
    for (int i = start; i < sc.ArraySize; ++i)
    {
        // ------------------------------------------------ clear outputs for this bar
        Subgraph_Buy[i] = 0;
        Subgraph_Sell[i] = 0;
        Subgraph_BuyAPlus[i] = 0;
        Subgraph_SellAPlus[i] = 0;
        Subgraph_Stop[i] = 0;
        Subgraph_Target[i] = 0;
        Subgraph_BullScore[i] = 0;
        Subgraph_BearScore[i] = 0;
        Subgraph_TargetHit[i] = 0;
        Subgraph_OpenEqMark[i] = 0;
        Subgraph_OpenEqBandMark[i] = 0;
        Subgraph_BandUpper[i] = 0;
        Subgraph_BandLower[i] = 0;
        Array_BuyStop[i] = 0;
        Array_BuyTarget[i] = 0;
        Array_SellStop[i] = 0;
        Array_SellTarget[i] = 0;
        Array_BuyFlags[i] = 0;
        Array_SellFlags[i] = 0;
        Subgraph_Signal[i] = 0;
        Array_PatFlags[i] = 0;
        Array_TPFlags[i] = 0;
        Array_TPBuyValue[i] = 0;
        Array_TPSellValue[i] = 0;
        if (resetState)
            Array_LabelMask[i] = 0;   // chart drawings are removed by Sierra Chart on a full recalculation
        Array_LastBuy[i]  = (i > 0) ? Array_LastBuy[i - 1]  : NO_SIGNAL;
        Array_LastSell[i] = (i > 0) ? Array_LastSell[i - 1] : NO_SIGNAL;
        if (resetState)
        {
            Array_BuyTPAlerted[i] = 0;
            Array_BuyTPLive[i] = 0;
            Array_SellTPAlerted[i] = 0;
            Array_SellTPLive[i] = 0;
        }

        // ------------------------------------------------ ATR (Wilder, SMA seed)
        if (i == atrLen - 1)
        {
            double sum = 0.0;
            for (int j = 0; j < atrLen; ++j)
                sum += TrueRange(sc, j);
            Array_ATR[i] = (float)(sum / atrLen);
        }
        else if (i >= atrLen)
            Array_ATR[i] = (float)((Array_ATR[i - 1] * (atrLen - 1.0) + TrueRange(sc, i)) / atrLen);
        else
            Array_ATR[i] = 0;

        // ------------------------------------------------ EMA (seeded with first close)
        Array_EMA[i] = (i == 0) ? sc.Close[i] : (float)(emaAlpha * sc.Close[i] + (1.0 - emaAlpha) * Array_EMA[i - 1]);

        // ------------------------------------------------ alert Bollinger Bands (SMA +/- population std dev)
        if (i >= bandLen - 1)
        {
            double sum = 0.0;
            for (int j = i - bandLen + 1; j <= i; ++j)
                sum += sc.Close[j];
            const double mean = sum / bandLen;
            double ss = 0.0;
            for (int j = i - bandLen + 1; j <= i; ++j)
            {
                const double d = sc.Close[j] - mean;
                ss += d * d;
            }
            const double sd = sqrt(ss / bandLen);
            Array_BandUpper[i] = (float)(mean + bandMult * sd);
            Array_BandLower[i] = (float)(mean - bandMult * sd);
            if (showBands)
            {
                Subgraph_BandUpper[i] = Array_BandUpper[i];
                Subgraph_BandLower[i] = Array_BandLower[i];
            }
        }
        else
        {
            Array_BandUpper[i] = 0;
            Array_BandLower[i] = 0;
        }

        // ------------------------------------------------ persistence counts
        if (i >= 4)
        {
            Array_TDBull[i] = (sc.Close[i] < sc.Close[i - 4]) ? Array_TDBull[i - 1] + 1 : 0;
            Array_TDBear[i] = (sc.Close[i] > sc.Close[i - 4]) ? Array_TDBear[i - 1] + 1 : 0;
        }
        else
        {
            Array_TDBull[i] = 0;
            Array_TDBear[i] = 0;
        }

        const bool barClosed = (i < sc.ArraySize - 1);

        // ------------------------------------------------ Resistance Cloud (Nadaraya-Watson envelope)
        Subgraph_CloudUpperOuter[i] = 0;
        Subgraph_CloudUpperInner[i] = 0;
        Subgraph_CloudLowerInner[i] = 0;
        Subgraph_CloudLowerOuter[i] = 0;
        Subgraph_WickInCloud[i] = 0;
        Subgraph_CloudUpperOuterLine[i] = 0;
        Subgraph_CloudUpperInnerLine[i] = 0;
        Subgraph_CloudLowerInnerLine[i] = 0;
        Subgraph_CloudLowerOuterLine[i] = 0;
        Array_CloudFlags[i] = 0;
        if (i >= cloudWindow - 1)
        {
            // rational quadratic kernel over the last cloudWindow bars (weights fixed per call)
            double sumC = 0.0, sumH = 0.0, sumL = 0.0;
            for (int k = 0; k < cloudWindow; ++k)
            {
                sumC += sc.Close[i - k] * cloudW[k];
                sumH += sc.High[i - k]  * cloudW[k];
                sumL += sc.Low[i - k]   * cloudW[k];
            }
            Array_CloudYC[i] = (float)(sumC / cloudWSum);
            Array_CloudYH[i] = (float)(sumH / cloudWSum);
            Array_CloudYL[i] = (float)(sumL / cloudWSum);

            // Wilder ATR (Pine ta.rma) of the kernel series, seeded with an SMA
            if (i == cloudFirst)
            {
                double sum = 0.0;
                for (int k = i - cloudATRLen + 1; k <= i; ++k)
                    sum += cloudTR(k);
                Array_CloudATR[i] = (float)(sum / cloudATRLen);
            }
            else if (i > cloudFirst)
            {
                const double a = 1.0 / cloudATRLen;
                Array_CloudATR[i] = (float)(a * cloudTR(i) + (1.0 - a) * Array_CloudATR[i - 1]);
            }
            else
                Array_CloudATR[i] = 0;
        }
        else
        {
            Array_CloudYC[i] = 0;
            Array_CloudYH[i] = 0;
            Array_CloudYL[i] = 0;
            Array_CloudATR[i] = 0;
        }
        if (i >= cloudFirst)
        {
            const double yc = Array_CloudYC[i], katr = Array_CloudATR[i];
            const double upperNear = yc + cloudNear * katr, upperFar = yc + cloudFar * katr;
            const double lowerNear = yc - cloudNear * katr, lowerFar = yc - cloudFar * katr;
            Array_UpperNear[i] = (float)upperNear;
            Array_LowerNear[i] = (float)lowerNear;
            Array_UpperAvg[i]  = (float)((upperNear + upperFar) / 2.0);
            Array_LowerAvg[i]  = (float)((lowerNear + lowerFar) / 2.0);
            if (showCloud)
            {
                Subgraph_CloudUpperOuter[i] = Array_UpperAvg[i];
                Subgraph_CloudUpperInner[i] = Array_UpperNear[i];
                Subgraph_CloudLowerInner[i] = Array_LowerNear[i];
                Subgraph_CloudLowerOuter[i] = Array_LowerAvg[i];
                Subgraph_CloudUpperOuterLine[i] = Array_UpperAvg[i];
                Subgraph_CloudUpperInnerLine[i] = Array_UpperNear[i];
                Subgraph_CloudLowerInnerLine[i] = Array_LowerNear[i];
                Subgraph_CloudLowerOuterLine[i] = Array_LowerAvg[i];
            }
            int cloudFlags = 0;
            if (sc.Low[i] < Array_LowerNear[i])  cloudFlags |= 1;   // script: bBrokeDown
            if (sc.High[i] > Array_UpperNear[i]) cloudFlags |= 2;   // script: bBrokeUp
            Array_CloudFlags[i] = (float)cloudFlags;

            // Large wick in cloud (script alert "Large Wick in Cloud")
            if (barClosed || !closeOnly)
            {
                const double o = sc.Open[i], h = sc.High[i], l = sc.Low[i], c = sc.Close[i];
                const bool wickBuy  = (cloudFlags & 1) && c < o && fabs(l - c) > fabs(o - c);
                const bool wickSell = (cloudFlags & 2) && c > o && fabs(h - c) > fabs(o - c);
                if (wickBuy)
                {
                    Subgraph_WickInCloud[i] = sc.Low[i];
                    queuePatternAlert(i, -1, URS_ALERT_WICK_CLOUD, true, "large wick");
                }
                if (wickSell)
                {
                    Subgraph_WickInCloud[i] = sc.High[i];
                    queuePatternAlert(i, -1, URS_ALERT_WICK_CLOUD, false, "large wick");
                }
            }
        }
        else
        {
            Array_UpperNear[i] = 0;
            Array_LowerNear[i] = 0;
            Array_UpperAvg[i] = 0;
            Array_LowerAvg[i] = 0;
        }

        // ------------------------------------------------ TraderOracle patterns
        // Same Sierra Chart calls, in the same order, as the TraderOracle module.
        Subgraph_TrampBuy[i] = 0;
        Subgraph_TrampSell[i] = 0;
        Subgraph_EngulfBull[i] = 0;
        Subgraph_EngulfBear[i] = 0;
        Subgraph_SqueezeBuy[i] = 0;
        Subgraph_SqueezeSell[i] = 0;

        sc.RSI(sc.Close, Subgraph_PatRSI, i, MOVAVGTYPE_SIMPLE, 14);
        sc.BollingerBands(sc.Close, Subgraph_PatBB, i, 20, 2.0f, MOVAVGTYPE_SIMPLE);

        // Squeeze relaxer histogram. As in the module, the EMA array is overwritten by a linear
        // regression average at the end of each bar, and the next bar's EMA builds on that.
        sc.ExponentialMovAvg(sc.Close, Subgraph_SqA, i, sqLen);
        sc.MovingAverage(sc.Close, Subgraph_SqA, MOVAVGTYPE_EXPONENTIAL, i, sqLen);
        {
            const float hlh = sc.GetHighest(sc.High, i, sqLen);
            const float lll = sc.GetLowest(sc.Low, i, sqLen);
            Subgraph_SqD[i] = sc.Open[i] - ((hlh + lll) / 2.0f + Subgraph_SqA[i]) / 2.0f;
        }
        sc.LinearRegressionIndicator(Subgraph_SqD, Subgraph_SqHist, i, sqLen);
        sc.MovingAverage(sc.Close, Subgraph_SqA, MOVAVGTYPE_LINEARREGRESSION, i, sqLen);

        int sqState = (i > 0) ? (int)Array_SqState[i - 1] : 0;
        if (i >= 2 && (barClosed || !closeOnly))
        {
            // squeeze dots, alternating buy / sell
            const float hist = Subgraph_SqHist[i];
            const float prevHist = Subgraph_SqHist[i - 1];
            if (hist <= 0 && hist > prevHist)
            {
                if (sqState == 0)
                {
                    sqState = 1;
                    Array_PatFlags[i] = (float)((int)Array_PatFlags[i] | 4);
                    if (showSqueeze && classic)
                        Subgraph_SqueezeBuy[i] = (float)(sc.Low[i] - sqOffset);
                    queuePatternAlert(i, URS_ALERT_SQUEEZE, inCloud(i, true) ? URS_ALERT_SQUEEZE_CLOUD : -1, true, "squeeze dot");
                }
            }
            else if (hist >= 0 && hist < prevHist)
            {
                if (sqState == 1)
                {
                    sqState = 0;
                    Array_PatFlags[i] = (float)((int)Array_PatFlags[i] | 8);
                    if (showSqueeze && classic)
                        Subgraph_SqueezeSell[i] = (float)(sc.High[i] + sqOffset);
                    queuePatternAlert(i, URS_ALERT_SQUEEZE, inCloud(i, false) ? URS_ALERT_SQUEEZE_CLOUD : -1, false, "squeeze dot");
                }
            }

            const float upperBand = Subgraph_PatBB.Arrays[0][i];
            const float lowerBand = Subgraph_PatBB.Arrays[1][i];

            // trampoline
            const float rsi0 = Subgraph_PatRSI[i], rsi1 = Subgraph_PatRSI[i - 1], rsi2 = Subgraph_PatRSI[i - 2];
            const bool rsiHigh = rsi0 > trampRSIHigh || rsi1 > trampRSIHigh || rsi2 > trampRSIHigh;
            const bool rsiLow  = rsi0 < trampRSILow  || rsi1 < trampRSILow  || rsi2 < trampRSILow;
            const bool trBearPattern = sc.Close[i] < sc.Open[i] && sc.Close[i - 1] < sc.Open[i - 1]
                && sc.Close[i - 2] > sc.Open[i - 2] && sc.Close[i] < sc.Close[i - 1] && rsiHigh;
            const bool trBullPattern = sc.Close[i] > sc.Open[i] && sc.Close[i - 1] > sc.Open[i - 1]
                && sc.Close[i - 2] < sc.Open[i - 2] && sc.Close[i] > sc.Close[i - 1] && rsiLow;
            bool trBull = false, trBear = false;
            if (trampMode == 0)
            {
                // Exactly as TraderOracle: IsTrampoline() gets the tick size as an int and is
                // called with the upper band, then with the lower band.
                const float iTick = (float)(int)sc.TickSize;
                const bool withUpper = (trBearPattern && sc.High[i - 2] >= upperBand - (1 * iTick))
                                    || (trBullPattern && sc.Low[i - 2] <= upperBand + (1 * iTick));
                const bool withLower = (trBearPattern && sc.High[i - 2] >= lowerBand - (1 * iTick))
                                    || (trBullPattern && sc.Low[i - 2] <= lowerBand + (1 * iTick));
                if (withUpper || withLower)
                {
                    trBull = trBullPattern;
                    trBear = trBearPattern;
                }
            }
            else
            {
                trBear = trBearPattern && sc.High[i - 2] >= upperBand - (double)sc.TickSize;
                trBull = trBullPattern && sc.Low[i - 2] <= lowerBand + (double)sc.TickSize;
            }
            if (trBull)
            {
                Array_PatFlags[i] = (float)((int)Array_PatFlags[i] | 1);
                if (showTramp && classic)
                    Subgraph_TrampBuy[i] = (float)(sc.Low[i] - trampOffset);
                queuePatternAlert(i, URS_ALERT_TRAMP, trampInCloud(i, true) ? URS_ALERT_TRAMP_CLOUD : -1, true, "trampoline");
            }
            if (trBear)
            {
                Array_PatFlags[i] = (float)((int)Array_PatFlags[i] | 2);
                if (showTramp && classic)
                    Subgraph_TrampSell[i] = (float)(sc.High[i] + trampOffset);
                queuePatternAlert(i, URS_ALERT_TRAMP, trampInCloud(i, false) ? URS_ALERT_TRAMP_CLOUD : -1, false, "trampoline");
            }

            // engulfing candle at the band
            const double open = sc.Open[i], close = sc.Close[i], high = sc.High[i], low = sc.Low[i];
            const bool green = open < close, red = open > close;
            const double body = fabs(open - close);
            const double prevBody = fabs((double)sc.Open[i - 1] - (double)sc.Close[i - 1]);
            const double upperWick = green ? fabs(high - close) : fabs(high - open);
            const double lowerWick = green ? fabs(open - low) : fabs(close - low);
            const bool doji = upperWick > body && lowerWick > body;
            if (!(engulfNoDoji && doji))
            {
                const bool bandBull = low < lowerBand && sc.Low[i - 1] < lowerBand;
                const bool bandBear = high > upperBand && sc.High[i - 1] > upperBand;
                bool enBull, enBear;
                if (engulfRule == 0)
                {
                    enBull = bandBull && green && body > prevBody;
                    enBear = bandBear && red && body > prevBody;
                }
                else
                {
                    // TraderOracle IsBullishEngulfing / IsBearishEngulfing
                    const bool classicBull = sc.Close[i - 1] < sc.Open[i - 1] && green
                        && sc.High[i] > sc.High[i - 1] && sc.Low[i] < sc.Low[i - 1]
                        && sc.Close[i] > sc.Open[i - 1] && sc.Open[i] < sc.Close[i - 1];
                    const bool classicBear = sc.Close[i - 1] > sc.Open[i - 1] && red
                        && sc.High[i] > sc.High[i - 1] && sc.Low[i] < sc.Low[i - 1]
                        && sc.Open[i] > sc.Close[i - 1] && sc.Close[i] < sc.Open[i - 1];
                    enBull = classicBull && (engulfRule == 2 || bandBull);
                    enBear = classicBear && (engulfRule == 2 || bandBear);
                }
                if (enBull)
                {
                    if (showEngulf)
                        Subgraph_EngulfBull[i] = 1;
                    queuePatternAlert(i, URS_ALERT_ENGULF, -1, true, "engulfing candle at band");
                }
                if (enBear)
                {
                    if (showEngulf)
                        Subgraph_EngulfBear[i] = 1;
                    queuePatternAlert(i, URS_ALERT_ENGULF, -1, false, "engulfing candle at band");
                }
            }
        }
        Array_SqState[i] = (float)sqState;

        const double atr = Array_ATR[i];
        const bool evaluate = (i >= warmup) && (atr > 0.0) && (barClosed || !closeOnly);
        if (!evaluate)
            continue;

        const double hi = sc.High[i];
        const double lo = sc.Low[i];
        const double cl = sc.Close[i];
        const double rangeATR = (hi - lo) / atr;
        const bool openEqPrior = fabs((double)sc.Open[i] - (double)sc.Close[i - 1]) <= openEqTol;

        bool vetoLowZ = false, vetoHighZ = false, vetoVol = false;
        for (int k = i - 2; k <= i; ++k)
        {
            double z;
            if (ZScoreAt(sc, k, bbLen, z))
            {
                if (z < -zVeto) vetoLowZ = true;
                if (z >  zVeto) vetoHighZ = true;
            }
            double rv;
            if (useVol && RelVolumeAt(sc, k, volLen, rv) && rv > climaxVeto)
                vetoVol = true;
        }
        const bool commonVeto = (rangeATR > rangeVeto) || vetoVol;

        double tdMaxBull = 0.0, tdMaxBear = 0.0;
        for (int k = i - 4; k <= i; ++k)
        {
            tdMaxBull = UrsMax(tdMaxBull, (double)Array_TDBull[k]);
            tdMaxBear = UrsMax(tdMaxBear, (double)Array_TDBear[k]);
        }

        // ============================================ BULLISH
        bool atLow = true;
        for (int j = i - lookback + 1; j < i; ++j)
            if (sc.Low[j] < sc.Low[i]) { atLow = false; break; }

        if (atLow && !commonVeto && !vetoLowZ)
        {
            int score = 0;
            if (tdMaxBull >= tdMin)
                ++score;

            int r = i;
            for (int j = i - 2; j <= i; ++j)
                if (sc.Low[j] < sc.Low[r]) r = j;
            int p = i - divFar;
            for (int j = i - divFar; j <= i - divNear; ++j)
                if (sc.Low[j] < sc.Low[p]) p = j;
            double z1, z0;
            if (sc.Low[r] < sc.Low[p] && ZScoreAt(sc, r, bbLen, z1) && ZScoreAt(sc, p, bbLen, z0) && z1 > z0)
                ++score;

            double rv;
            if (useVol && RelVolumeAt(sc, r, volLen, rv) && rv < quietRVol)
                ++score;

            if (cl > Array_EMA[i])
                ++score;

            Subgraph_BullScore[i] = (float)score;

            const int lastBuy = (i > 0) ? (int)Array_LastBuy[i - 1] : (int)NO_SIGNAL;
            if (score >= minScore && i - lastBuy > cooldown)
            {
                const double risk = UrsMax(cl - (lo - stopBuf * atr), minRisk * atr);
                Array_LastBuy[i] = (float)i;
                Array_BuyStop[i] = (float)(cl - risk);
                Array_BuyTarget[i] = (float)(cl + targetR * risk);
                const float arrowY = (float)(lo - arrowOff * atr);
                Subgraph_Signal[i] = (float)score;
                if (classic)
                {
                    if (score >= 4) Subgraph_BuyAPlus[i] = arrowY;
                    else            Subgraph_Buy[i] = arrowY;
                }

                int flags = 0;
                if (openEqPrior) flags |= 1;
                if (bandBroken(i, true) || bandBroken(i - 1, true)) flags |= 2;
                Array_BuyFlags[i] = (float)flags;
                if (showOpenEq && (flags & 1))
                {
                    if (flags & 2) Subgraph_OpenEqBandMark[i] = sc.Open[i];
                    else           Subgraph_OpenEqMark[i] = sc.Open[i];
                }
                queueSignalAlert(i, true, score, flags, Array_BuyStop[i], Array_BuyTarget[i]);
            }
        }

        // ============================================ BEARISH (mirror)
        bool atHigh = true;
        for (int j = i - lookback + 1; j < i; ++j)
            if (sc.High[j] > sc.High[i]) { atHigh = false; break; }

        if (atHigh && !commonVeto && !vetoHighZ)
        {
            int score = 0;
            if (tdMaxBear >= tdMin)
                ++score;

            int r = i;
            for (int j = i - 2; j <= i; ++j)
                if (sc.High[j] > sc.High[r]) r = j;
            int p = i - divFar;
            for (int j = i - divFar; j <= i - divNear; ++j)
                if (sc.High[j] > sc.High[p]) p = j;
            double z1, z0;
            if (sc.High[r] > sc.High[p] && ZScoreAt(sc, r, bbLen, z1) && ZScoreAt(sc, p, bbLen, z0) && z1 < z0)
                ++score;

            double rv;
            if (useVol && RelVolumeAt(sc, r, volLen, rv) && rv < quietRVol)
                ++score;

            if (cl < Array_EMA[i])
                ++score;

            Subgraph_BearScore[i] = (float)score;

            const int lastSell = (i > 0) ? (int)Array_LastSell[i - 1] : (int)NO_SIGNAL;
            if (score >= minScore && i - lastSell > cooldown)
            {
                const double risk = UrsMax((hi + stopBuf * atr) - cl, minRisk * atr);
                Array_LastSell[i] = (float)i;
                Array_SellStop[i] = (float)(cl + risk);
                Array_SellTarget[i] = (float)(cl - targetR * risk);
                const float arrowY = (float)(hi + arrowOff * atr);
                if (Subgraph_Signal[i] == 0)
                    Subgraph_Signal[i] = (float)-score;
                if (classic)
                {
                    if (score >= 4) Subgraph_SellAPlus[i] = arrowY;
                    else            Subgraph_Sell[i] = arrowY;
                }

                int flags = 0;
                if (openEqPrior) flags |= 1;
                if (bandBroken(i, false) || bandBroken(i - 1, false)) flags |= 2;
                Array_SellFlags[i] = (float)flags;
                if (showOpenEq && (flags & 1))
                {
                    if (flags & 2) Subgraph_OpenEqBandMark[i] = sc.Open[i];
                    else           Subgraph_OpenEqMark[i] = sc.Open[i];
                }
                queueSignalAlert(i, false, score, flags, Array_SellStop[i], Array_SellTarget[i]);
            }
        }
    }

    // ================================================================ pass 2: track stop / target
    // Any signal whose tracking window reaches into the updated bars is re-checked.
    const int trackFrom = UrsMaxI(0, start - maxTrack - 1);
    for (int s = trackFrom; s < sc.ArraySize; ++s)
    {
        for (int d = 0; d < 2; ++d)
        {
            const bool isBuy = (d == 0);
            SCFloatArrayRef lastSig   = isBuy ? Array_LastBuy      : Array_LastSell;
            SCFloatArrayRef stopArr   = isBuy ? Array_BuyStop      : Array_SellStop;
            SCFloatArrayRef tgtArr    = isBuy ? Array_BuyTarget    : Array_SellTarget;
            SCFloatArrayRef resBar    = isBuy ? Array_BuyResBar    : Array_SellResBar;
            SCFloatArrayRef resType   = isBuy ? Array_BuyResType   : Array_SellResType;
            SCFloatArrayRef tpAlerted = isBuy ? Array_BuyTPAlerted : Array_SellTPAlerted;
            SCFloatArrayRef tpLive    = isBuy ? Array_BuyTPLive    : Array_SellTPLive;

            resBar[s] = 0;
            resType[s] = 0;
            if ((int)lastSig[s] != s)
                continue;

            const double stop = stopArr[s];
            const double target = tgtArr[s];
            int rType = 0, rBar = 0;
            const int lastJ = UrsMinI(s + maxTrack, sc.ArraySize - 1);
            for (int j = s + 1; j <= lastJ; ++j)
            {
                if ((int)tpLive[s] == j + 1) { rType = 1; rBar = j; break; }   // target touched first, seen live
                const bool hitStop = isBuy ? (sc.Low[j] <= stop) : (sc.High[j] >= stop);
                if (hitStop) { rType = -1; rBar = j; break; }
                const bool hitTarget = isBuy ? (sc.High[j] >= target) : (sc.Low[j] <= target);
                if (hitTarget) { rType = 1; rBar = j; break; }
            }
            if (rType == 0 && s + maxTrack < sc.ArraySize - 1)
            {
                rType = 2;
                rBar = s + maxTrack;
            }
            resType[s] = (float)rType;
            resBar[s] = (float)rBar;
            if (rType != 1)
                continue;

            if (rBar >= start)
            {
                if (classic)
                    Subgraph_TargetHit[rBar] = (float)target;
                Array_TPFlags[rBar] = (float)((int)Array_TPFlags[rBar] | (isBuy ? 1 : 2));
                if (isBuy) Array_TPBuyValue[rBar] = (float)target;
                else       Array_TPSellValue[rBar] = (float)target;
            }
            // Remember that the target came first on the live bar, so a later stop touch in
            // the same bar does not flip the outcome.
            if (rBar == sc.ArraySize - 1 && !fullRecalc && tpLive[s] == 0)
                tpLive[s] = (float)(rBar + 1);

            if (tpAlerted[s] != 0)
                continue;
            if (fullRecalc || rBar < freshFrom || !DirectionOn(alertDir[URS_ALERT_TARGET], isBuy))
            {
                tpAlerted[s] = 1;   // history, or this alert is switched off
                continue;
            }
            if (eventCount >= URS_MAX_EVENTS)
                continue;
            AlertEvent& e = events[eventCount++];
            e.IsTarget = true;
            e.Dir = d;
            e.SignalBar = s;
            e.KindMask = 1 << URS_ALERT_TARGET;
            e.Priority = URS_ALERT_TARGET;
            e.Index = rBar;
            e.Text.Format("%s target hit at %s, stop not hit (signal %d bars ago)",
                isBuy ? "BUY" : "SELL",
                sc.FormatGraphValue(target, sc.BaseGraphValueFormat).GetChars(),
                rBar - s);
        }
    }

    // ================================================================ pass 3: stop / target lines
    for (int i = start; i < sc.ArraySize; ++i)
    {
        if (!showLevels)
            break;
        const int lb = (int)Array_LastBuy[i];
        const int ls = (int)Array_LastSell[i];
        const int s = UrsMaxI(lb, ls);
        if (s < 0 || lb == ls)
            continue;
        const bool isBuy = (s == lb);
        const int rType = (int)(isBuy ? Array_BuyResType[s] : Array_SellResType[s]);
        const int rBar  = (int)(isBuy ? Array_BuyResBar[s]  : Array_SellResBar[s]);
        const int endBar = (rType == 0) ? s + maxTrack : rBar;
        if (i > endBar)
            continue;
        Subgraph_Stop[i]   = isBuy ? Array_BuyStop[s]   : Array_SellStop[s];
        Subgraph_Target[i] = isBuy ? Array_BuyTarget[s] : Array_SellTarget[s];
    }

    // ================================================================ pass 4: labels
    // One tag per side per bar (signal and/or trampoline, optionally on a pin stem), squeeze
    // dots, TP tags and the A+ glow are Sierra Chart chart drawings with a fixed LineNumber per bar and slot,
    // so redrawing a bar adjusts its drawings instead of adding more. Drawings that are no
    // longer wanted (a signal that disappeared while its bar was still forming) are deleted.
    {
        const int lineBase = 1100000000 + (sc.StudyGraphInstanceID % 40) * 20000000;
        const SCString fontFace = HolyGrail::LABEL_FONT;
        enum { SLOT_BUY = 0, SLOT_SELL, SLOT_SQ_BUY, SLOT_SQ_SELL, SLOT_TP_BUY, SLOT_TP_SELL, SLOT_GLOW_BUY, SLOT_GLOW_SELL,
               SLOT_STEM_BUY, SLOT_STEM_SELL, SLOT_COUNT };

        auto lineNumber = [&](int i, int slot) { return lineBase + (i % 2000000) * SLOT_COUNT + slot; };
        auto contrastText = [](COLORREF c) -> COLORREF
        {
            const int r = (int)(c & 0xFF), g = (int)((c >> 8) & 0xFF), b = (int)((c >> 16) & 0xFF);
            return (0.299 * r + 0.587 * g + 0.114 * b > 140.0) ? RGB(20, 20, 20) : RGB(255, 255, 255);
        };
        auto drawText = [&](int i, int slot, double y, unsigned int align, const SCString& text,
                            COLORREF color, int fontSize, bool badge)
        {
            s_UseTool t;
            t.Clear();
            t.ChartNumber = sc.ChartNumber;
            t.DrawingType = DRAWING_TEXT;
            t.LineNumber = lineNumber(i, slot);
            t.AddMethod = UTAM_ADD_OR_ADJUST;
            t.BeginIndex = i;
            t.BeginValue = (float)y;
            t.Region = sc.GraphRegion;
            t.FontSize = fontSize;
            t.FontBold = 1;
            t.FontFace = fontFace;
            t.TextAlignment = align;
            t.Text = text;
            if (badge)
            {
                t.Color = contrastText(color);
                t.FontBackColor = color;
                t.TransparentLabelBackground = 0;
            }
            else
            {
                t.Color = color;
                t.TransparentLabelBackground = 1;
            }
            sc.UseTool(t);
        };
        auto drawGlow = [&](int i, int slot, double y, double halfHeight, COLORREF color)
        {
            const int b0 = UrsMaxI(0, i - 1), b1 = UrsMinI(i + 1, sc.ArraySize - 1);
            if (b1 <= b0)
                return;
            s_UseTool t;
            t.Clear();
            t.ChartNumber = sc.ChartNumber;
            t.DrawingType = DRAWING_ELLIPSEHIGHLIGHT;
            t.LineNumber = lineNumber(i, slot);
            t.AddMethod = UTAM_ADD_OR_ADJUST;
            t.BeginIndex = b0;
            t.EndIndex = b1;
            t.BeginValue = (float)(y - halfHeight);
            t.EndValue = (float)(y + halfHeight);
            t.Region = sc.GraphRegion;
            t.Color = color;
            t.SecondaryColor = color;
            t.TransparencyLevel = 72;
            t.LineWidth = 0;
            t.DrawUnderneathMainGraph = 1;
            sc.UseTool(t);
        };

        auto drawStem = [&](int i, int slot, double y0, double y1, COLORREF color)
        {
            s_UseTool t;
            t.Clear();
            t.ChartNumber = sc.ChartNumber;
            t.DrawingType = DRAWING_LINE;
            t.LineNumber = lineNumber(i, slot);
            t.AddMethod = UTAM_ADD_OR_ADJUST;
            t.BeginIndex = i;
            t.EndIndex = i;
            t.BeginValue = (float)y0;
            t.EndValue = (float)y1;
            t.Region = sc.GraphRegion;
            t.Color = color;
            t.LineWidth = 1;
            sc.UseTool(t);
        };

        const char* SEPARATOR = " \xC2\xB7 ";     // " U+00B7 "
        const char* DOT = "\xE2\x97\x8F";        // UTF-8 U+25CF (filled circle)

        for (int i = start; i < sc.ArraySize; ++i)
        {
            int want = 0;
            const int drawn = (int)Array_LabelMask[i];
            if (useLabels)
            {
                const double atrNow = Array_ATR[i];
                const double off = pinStem ? UrsMax(pinStemLen * atrNow, 4.0 * tick) : UrsMax(arrowOff * atrNow, 2.0 * tick);
                const double stemGap = UrsMax(0.1 * atrNow, tick);
                const int pat = (int)Array_PatFlags[i];
                const int tp = (int)Array_TPFlags[i];
                for (int side = 0; side < 2; ++side)
                {
                    const bool isBuy = (side == 0);
                    const bool hasSignal = isBuy ? ((int)Array_LastBuy[i] == i) : ((int)Array_LastSell[i] == i);
                    const int score = (int)(isBuy ? Subgraph_BullScore[i] : Subgraph_BearScore[i]);
                    const bool hasTramp = showTramp && (pat & (isBuy ? 1 : 2)) != 0;
                    const bool hasSqueeze = showSqueeze && (pat & (isBuy ? 4 : 8)) != 0;
                    const double y = isBuy ? sc.Low[i] - off : sc.High[i] + off;
                    const unsigned int align = DT_CENTER | (isBuy ? DT_TOP : DT_BOTTOM);
                    if (hasSignal || hasTramp)
                    {
                        SCString text;
                        COLORREF color;
                        int size = labelSize;
                        bool filled = true;   // filled tag; regular signals are coloured text only
                        if (hasSignal)
                        {
                            const char* word = (tagText == 1) ? (isBuy ? "LONG" : "SHORT") : (isBuy ? "BUY" : "SELL");
                            text.Format("%s%s", word, score >= 4 ? "+" : "");
                            color = score >= 4 ? (isBuy ? Subgraph_BuyAPlus.PrimaryColor : Subgraph_SellAPlus.PrimaryColor)
                                               : (isBuy ? Subgraph_Buy.PrimaryColor : Subgraph_Sell.PrimaryColor);
                            filled = score >= 4;
                            if (hasTramp)
                                text.AppendFormat("%sTR", SEPARATOR);
                        }
                        else
                        {
                            text = "TR";
                            color = isBuy ? Subgraph_TrampBuy.PrimaryColor : Subgraph_TrampSell.PrimaryColor;
                            size = UrsMaxI(6, labelSize - 1);
                        }
                        drawText(i, isBuy ? SLOT_BUY : SLOT_SELL, y, align, text, color, size, filled);
                        want |= 1 << (isBuy ? SLOT_BUY : SLOT_SELL);
                        if (pinStem && off > stemGap)
                        {
                            drawStem(i, isBuy ? SLOT_STEM_BUY : SLOT_STEM_SELL,
                                     isBuy ? sc.Low[i] - stemGap : sc.High[i] + stemGap, y, color);
                            want |= 1 << (isBuy ? SLOT_STEM_BUY : SLOT_STEM_SELL);
                        }
                    }
                    if (hasSqueeze)
                    {
                        const double yDot = isBuy ? sc.Low[i] - sqOffset : sc.High[i] + sqOffset;
                        drawText(i, isBuy ? SLOT_SQ_BUY : SLOT_SQ_SELL, yDot, align, DOT,
                                 isBuy ? Subgraph_SqueezeBuy.PrimaryColor : Subgraph_SqueezeSell.PrimaryColor,
                                 UrsMaxI(6, labelSize - 2), false);
                        want |= 1 << (isBuy ? SLOT_SQ_BUY : SLOT_SQ_SELL);
                    }
                    if (tp & (isBuy ? 1 : 2))
                    {
                        drawText(i, isBuy ? SLOT_TP_BUY : SLOT_TP_SELL,
                                 isBuy ? Array_TPBuyValue[i] : Array_TPSellValue[i],
                                 DT_LEFT | DT_VCENTER, " TP", Subgraph_TargetHit.PrimaryColor,
                                 UrsMaxI(6, labelSize - 2), true);
                        want |= 1 << (isBuy ? SLOT_TP_BUY : SLOT_TP_SELL);
                    }
                    if (labelGlow && hasSignal && score >= 4 && atrNow > 0)
                    {
                        drawGlow(i, isBuy ? SLOT_GLOW_BUY : SLOT_GLOW_SELL, isBuy ? sc.Low[i] : sc.High[i], 0.4 * atrNow,
                                 isBuy ? Subgraph_BuyAPlus.PrimaryColor : Subgraph_SellAPlus.PrimaryColor);
                        want |= 1 << (isBuy ? SLOT_GLOW_BUY : SLOT_GLOW_SELL);
                    }
                }
            }
            const int stale = drawn & ~want;
            for (int slot = 0; slot < SLOT_COUNT; ++slot)
                if (stale & (1 << slot))
                    sc.DeleteACSChartDrawing(0, TOOL_DELETE_CHARTDRAWING, lineNumber(i, slot));
            Array_LabelMask[i] = (float)want;
        }
    }

    // ================================================================ alerts
    // Sierra Chart generates only one alert per study call, and ignores an alert at the same
    // bar index as the previous call's alert. So all events found in this update are combined
    // into one alert; if that alert would be ignored, the events stay pending until next update.
    int& alertedLastCall = sc.GetPersistentInt(PKEY_ALERTED_LAST_CALL);
    int& lastAlertIndex  = sc.GetPersistentInt(PKEY_LAST_ALERT_INDEX);

    // The other modules' alerts from this update, plus any carried over from a deferred update.
    SCString& carriedText  = sc.GetPersistentSCString(PKEY_EXT_TEXT);
    int&      carriedSound = sc.GetPersistentInt(PKEY_EXT_SOUND);
    if (resetState)
    {
        carriedText = "";
        carriedSound = 0;
    }
    SCString extText = carriedText;
    int extSound = carriedSound;
    for (int n = 0; n < Ext.Count; ++n)
    {
        if (extText.GetLength() > 0)
            extText.AppendFormat(" || ");
        else
            extSound = Ext.Sound[n];
        extText.AppendFormat("%s", Ext.Text[n].GetChars());
    }
    carriedText = "";
    carriedSound = 0;
    const bool haveExt = extText.GetLength() > 0;

    if (eventCount == 0 && !haveExt)
    {
        alertedLastCall = 0;
        pendingFrom = -1;
        return;
    }

    int alertIndex = sc.ArraySize - 2, best = -1;
    for (int n = 0; n < eventCount; ++n)
    {
        alertIndex = UrsMaxI(alertIndex, events[n].Index);
        if (best < 0 || events[n].Priority > events[best].Priority)
            best = n;
    }

    if (alertedLastCall && alertIndex == lastAlertIndex)
    {
        // Defer: nothing is marked as alerted, and the next update re-scans from these bars;
        // the other modules' alerts are carried over as text.
        alertedLastCall = 0;
        pendingFrom = (eventCount > 0) ? sc.ArraySize : -1;
        for (int n = 0; n < eventCount; ++n)
            pendingFrom = UrsMinI(pendingFrom, events[n].IsTarget ? events[n].Index : events[n].SignalBar);
        carriedText = extText;
        carriedSound = extSound;
        return;
    }

    SCString message;
    message = "Reversal Grail: ";
    for (int n = 0; n < eventCount; ++n)
        message.AppendFormat("%s%s", n ? " || " : "", events[n].Text.GetChars());
    if (haveExt)
        message.AppendFormat("%s%s", eventCount ? " || " : "", extText.GetChars());
    sc.SetAlert(best >= 0 ? alertSound[events[best].Priority] : extSound, alertIndex, message);
    alertedLastCall = 1;
    lastAlertIndex = alertIndex;
    pendingFrom = -1;

    for (int n = 0; n < eventCount; ++n)
    {
        const AlertEvent& e = events[n];
        if (e.IsTarget)
        {
            if (e.Dir == 0) Array_BuyTPAlerted[e.SignalBar] = 1;
            else            Array_SellTPAlerted[e.SignalBar] = 1;
            continue;
        }
        for (int k = 0; k < URS_ALERT_KINDS; ++k)
            if (k != URS_ALERT_TARGET && (e.KindMask & (1 << k)))
                sc.GetPersistentInt(PKEY_LAST_ALERTED_BASE + 2 * k + e.Dir) = e.SignalBar;
    }
}

//############################################################################
//############################################################################
//##                                                                        ##
//##                   LITTLE RIZZY  -  SETTINGS                            ##
//##                                                                        ##
//##  Hardcoded settings of the Little Rizzy module. These used to be study ##
//##  inputs; the values below are what they were set to. To change one,    ##
//##  edit it here and rebuild (Analysis >> Build Custom Studies DLL).      ##
//##                                                                        ##
//##  Yes/No settings: 1 = Yes, 0 = No.                                     ##
//##                                                                        ##
//############################################################################
//############################################################################
namespace LittleRizzy
{
    // --- Pattern detection --------------------------------------------------
    const int   PIVOT_STRENGTH        = 3;      // bars each side of a swing pivot
    const int   MIN_ANCHOR_BARS       = 3;      // min bars between the two trend line anchors
    const int   DETECT_BEARISH        = 1;      // lower-high setups
    const int   DETECT_BULLISH        = 1;      // higher-low setups

    // --- Bollinger Bands ----------------------------------------------------
    const int   BB_LENGTH             = 20;
    const float BB_STD_DEVS           = 2.0f;
    const int   BB_MA_TYPE            = MOVAVGTYPE_SIMPLE;
    const int   BB_INPUT_DATA         = SC_LAST;
    const int   REQUIRE_BAND_PIVOT    = 0;      // confirming pivot must be outside the band
    const float BAND_PCTB_THRESHOLD   = 0.90f;  // %B (0-1) that counts as outside the band

    // --- Setup management ---------------------------------------------------
    const int   REMEASURE_NEW_EXTREME = 0;      // re-measure if a new extreme forms
    const int   INVALIDATE_ON_CLOSE   = 1;      // a close beyond the trend line kills the setup
    const int   INVALIDATION_TICKS    = 0;      // buffer for that close
    const int   NEW_REPLACES_ACTIVE   = 1;      // a new pattern replaces the active setup

    // --- Drawing ------------------------------------------------------------
    const int      DRAW_PATTERN       = 1;
    const int      SHOW_TEXT_LABELS   = 0;      // riz / TGT / H labels
    const int      LINE_WIDTH         = 2;      // trend line, and untouched target lines
    const COLORREF BEAR_TREND_COLOR   = RGB(220, 80, 80);
    const COLORREF BULL_TREND_COLOR   = RGB(80, 190, 110);
    const COLORREF MEASURE_COLOR      = RGB(0, 0, 0);       // "H" label
    const COLORREF TARGET_COLOR       = RGB(252, 236, 188);
    const int      DRAW_TREND_LINE    = 0;
    const int      TREND_LINE_OPACITY = 0;      // %, 100 = solid, 0 = invisible

    // --- Alerts -------------------------------------------------------------
    const int   ENABLE_ALERTS         = 0;      // new setup / invalidated / target hit / reversal

    // --- Touch-and-reverse arrows -------------------------------------------
    const int   MARK_REVERSALS        = 1;      // arrows at watched target levels
    const int   REVERSAL_WINDOW       = 2;      // bars after the touch, 0 = touch bar only
    const int   REVERSAL_BREAK_TICKS  = 0;      // close buffer for reversals and breaks
    const int   REQUIRE_OPPOSITE_BAR  = 0;      // reversal bar must close the other color

    // --- Watched target levels ----------------------------------------------
    const int   TOUCH_BACKGROUND      = 0;      // 0 = off, 1 = current target hit only,
                                                // 2 = any watched target level
    const int   PREV_LEVELS_TO_WATCH  = 0;      // previous setups' targets, 0-10
    const int   EXTEND_PREV_LINES     = 1;      // previous target lines keep running right

    // --- Target line styling ------------------------------------------------
    const int      TOUCHED_WIDTH      = 11;      // solid, once touched
    const int      APPROACH_MAX_WIDTH = 11;      // dashed, grows as price closes in
    const int      APPROACH_ZONE_PCT  = 50;     // % of the setup's measured move (H)
    const int      FAR_FADE_PCT       = 50;     // 0-90, how far a distant line fades
    const COLORREF BLINK_COLOR        = RGB(255, 255, 255);
    const int      BLINK_MS           = 250;    // length of each flash / off step
}
//############################################################################
//##                     END LITTLE RIZZY - SETTINGS                        ##
//############################################################################

//----------------------------------------------------------------------------
// Persistent variable keys
//----------------------------------------------------------------------------
namespace LR
{
    // All of this module's persistent variables are offset by KEY_BASE, so they never share a
    // key with the other modules of the study.
    const int KEY_BASE = 1000;

    enum PInt
    {
        PI_LastEvalIndex = 0,   // last closed bar index already processed
        PI_SetupActive,         // 0 = none, 1 = active
        PI_SetupDir,            // +1 bullish, -1 bearish
        PI_A1Index,             // first (older) trend line anchor
        PI_A2Index,             // second (newer) trend line anchor
        PI_ExtIndex,            // bar index of the pattern extreme
        PI_SetupID,             // incrementing id, used for drawing line numbers
        PI_LastHighIdx,
        PI_PrevHighIdx,
        PI_LastLowIdx,
        PI_PrevLowIdx
    };

    enum PFloat
    {
        PF_A1Value = 0,
        PF_A2Value,
        PF_ExtValue,
        PF_Distance,
        PF_Target,
        PF_Slope,
        PF_LastHighVal,
        PF_PrevHighVal,
        PF_LastLowVal,
        PF_PrevLowVal
    };

    // Previous setups' target levels, kept as a shift list in persistent
    // storage: slot 1 = the setup before the current one, slot 2 = before
    // that, and so on. Keys sit well clear of the enums above.
    const int MAX_PREV_LEVELS  = 10;
    const int PF_PrevLevelBase = 100;   // float keys 101..110: level price
    const int PF_PrevDistBase  = 120;   // float keys 121..130: measured move (H)
    const int PI_PrevExtBase   = 100;   // int keys   101..110: extreme bar index
    const int PI_PrevDirBase   = 120;   // int keys   121..130: setup direction

    // Per-slot line state, slot 0 = current setup, 1..MAX = previous ones.
    const int PI_TouchedBase   = 140;   // int keys    140..150: price has touched it
    const int PI_BlinkBase     = 160;   // int keys    160..170: blink steps left
    const int PI_TouchSideBase = 180;   // int keys    180..190: +1 first touch came from above, -1 from below
    const int PI_BrokenBase    = 200;   // int keys    200..210: bar that closed through it, -1 = not broken
    const int PI_FrozenEndBase = 220;   // int keys    220..230: fixed line end (lines not extended), -1 = extend
    const int PI_TouchBarBase  = 240;   // int keys    240..250: bar where price first touched it, -1 = never
    const int PD_BlinkTimeBase = 10;    // double keys  10..20 : time of last blink step (ms)

    // 3 blinks = flash, off, flash, off, flash, off.
    const int BLINK_STEPS = 6;

    // Waddah Attar Explosion, standard (LazyBear) parameters.
    const int   WAE_FAST        = 20;
    const int   WAE_SLOW        = 40;
    const float WAE_SENSITIVITY = 150.0f;
    const int   WAE_BB_LENGTH   = 20;
    const float WAE_BB_MULT     = 2.0f;
    const int   WAE_DZ_LENGTH   = 100;
    const float WAE_DZ_MULT     = 3.7f;

    // Value of the trend line at an arbitrary bar index (extrapolates freely).
    inline float LineValueAt(float A1Value, int A1Index, float Slope, int BarIndex)
    {
        return A1Value + Slope * static_cast<float>(BarIndex - A1Index);
    }
}

//----------------------------------------------------------------------------
// Did bar t touch level L, and from which side?
//   +1 = came down from above (prior close above L, bar traded down to L)
//   -1 = came up from below   (prior close below L, bar traded up to L)
//    0 = no touch
//----------------------------------------------------------------------------
static int LR_TouchSide(SCStudyInterfaceRef sc, int t, float L)
{
    if (t < 1)
        return 0;

    const float PrevClose = sc.Close[t - 1];
    if (PrevClose > L && sc.Low[t]  <= L) return +1;
    if (PrevClose < L && sc.High[t] >= L) return -1;
    return 0;
}

//----------------------------------------------------------------------------
// Is bar i the reversal after a touch of level L somewhere in [i - Window, i]?
// Reversal = the FIRST close back on the side price came from, so one touch
// gives at most one marker.
//   +1 = touched from above, closed back above (reversed up)
//   -1 = touched from below, closed back below (reversed down)
//    0 = no reversal on this bar
//----------------------------------------------------------------------------
static int LR_ReversalAt(SCStudyInterfaceRef sc, int i, float L,
                         int Window, float Buffer, int RequireColor)
{
    for (int t = i; t >= i - Window && t >= 1; --t)
    {
        const int Side = LR_TouchSide(sc, t, L);
        if (Side == 0)
            continue;

        // Already closed back on an earlier bar? Then this touch is used up.
        bool ClosedBackEarlier = false;
        for (int k = t; k < i; ++k)
        {
            if ((Side > 0 && sc.Close[k] > L + Buffer) ||
                (Side < 0 && sc.Close[k] < L - Buffer))
            {
                ClosedBackEarlier = true;
                break;
            }
        }
        if (ClosedBackEarlier)
            continue;

        if (Side > 0 && sc.Close[i] > L + Buffer &&
            (!RequireColor || sc.Close[i] > sc.Open[i]))
            return +1;

        if (Side < 0 && sc.Close[i] < L - Buffer &&
            (!RequireColor || sc.Close[i] < sc.Open[i]))
            return -1;
    }
    return 0;
}

//----------------------------------------------------------------------------
// The watched target levels: slot 0 = the current (latest) setup's target,
// slots 1..NumPrev = the previous setups' targets, newest first.
//----------------------------------------------------------------------------
struct LR_Level
{
    float Value;
    float Distance;     // that setup's measured move (H), scales the approach zone
    int   ExtIndex;
    int   Dir;
    int   SetupID;
    int   Slot;         // 0 = current, k = k-th previous setup
};

static int LR_CollectLevels(SCStudyInterfaceRef sc, int SetupID, int NumPrev,
                            float CurValue, float CurDistance,
                            int CurExtIndex, int CurDir,
                            LR_Level* Out)
{
    if (SetupID < 1)
        return 0;

    int n = 0;
    Out[n].Value    = CurValue;
    Out[n].Distance = CurDistance;
    Out[n].ExtIndex = CurExtIndex;
    Out[n].Dir      = CurDir;
    Out[n].SetupID  = SetupID;
    Out[n].Slot     = 0;
    ++n;

    for (int k = 1; k <= NumPrev && SetupID - k >= 1; ++k)
    {
        Out[n].Value    = sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_PrevLevelBase + k);
        Out[n].Distance = sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_PrevDistBase + k);
        Out[n].ExtIndex = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_PrevExtBase + k);
        Out[n].Dir      = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_PrevDirBase + k);
        Out[n].SetupID  = SetupID - k;
        Out[n].Slot     = k;
        ++n;
    }
    return n;
}

//----------------------------------------------------------------------------
// Bar t reached level L: its range covers L, or it gapped through L.
//----------------------------------------------------------------------------
static bool LR_BarTouches(SCStudyInterfaceRef sc, int t, float L)
{
    if (sc.Low[t] <= L && sc.High[t] >= L)
        return true;
    return LR_TouchSide(sc, t, L) != 0;
}

// Wall-clock milliseconds, for blink timing.
static double LR_NowMs()
{
    using namespace std::chrono;
    return static_cast<double>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

//----------------------------------------------------------------------------
// Target line appearance.
//----------------------------------------------------------------------------
struct LR_LineLook
{
    COLORREF           Color;
    COLORREF           LabelColor;   // the "TGT" label at the right end
    int                ShowTgtLabel; // off once broken / expired (their own tags say it)
    int                Width;
    SubgraphLineStyles Style;
    int                Hide;
};

struct LR_LookInputs
{
    COLORREF TargetColor;
    COLORREF BlinkColor;
    COLORREF BrokenColor;
    COLORREF ExpiredColor;
    COLORREF Background;
    int      BaseWidth;      // untouched, price far away
    int      ApproachWidth;  // untouched, price right at the level
    int      TouchedWidth;   // after the touch
    int      BrokenWidth;    // after a close went through it
    float    ZonePct;        // approach zone, % of the setup's measured move
    int      FarDimPct;      // how far toward the background a distant line fades

    // Sierra Chart lines and text can't be truly transparent, so these fade
    // the color toward the chart background instead (0 = off, 0.9 = faint).
    float    LineFade;
    float    TextFade;

    int      RizFontSize;    // "riz" at the left end of each target line
    int      LabelFontSize;  // "Short/Long TGT" and "H" labels
    int      TagFontSize;    // "Broken ..." and "expired" tags
};

// Where a target line is in its life:
//   untouched -> touched (price reached it) -> broken (a close went through)
//   untouched -> expired (dropped off the watch list without ever being hit)
enum LR_LineState
{
    LR_UNTOUCHED = 0,
    LR_TOUCHED,
    LR_BROKEN,
    LR_EXPIRED
};

static LR_LineState LR_StateOf(SCStudyInterfaceRef sc, int Slot, int Expiring)
{
    if (sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BrokenBase + Slot) >= 0)
        return LR_BROKEN;
    if (sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchedBase + Slot) != 0)
        return LR_TOUCHED;
    return Expiring ? LR_EXPIRED : LR_UNTOUCHED;
}

// 0 = price is outside the approach zone, 1 = price is at the level.
static float LR_Proximity(float Price, const LR_Level& Lv, float ZonePct)
{
    const float Zone = Lv.Distance * ZonePct / 100.0f;
    if (Zone <= 0.0f)
        return 0.0f;

    float p = 1.0f - static_cast<float>(fabs(Price - Lv.Value)) / Zone;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

// Mix two colors: t = 0 gives A, t = 1 gives B.
static COLORREF LR_Mix(COLORREF A, COLORREF B, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const int ar =  A        & 0xFF, ag = (A >> 8) & 0xFF, ab = (A >> 16) & 0xFF;
    const int br =  B        & 0xFF, bg = (B >> 8) & 0xFF, bb = (B >> 16) & 0xFF;
    return RGB(static_cast<int>(ar + (br - ar) * t + 0.5f),
               static_cast<int>(ag + (bg - ag) * t + 0.5f),
               static_cast<int>(ab + (bb - ab) * t + 0.5f));
}

// White on dark charts, black on light ones: used to keep label text readable.
static COLORREF LR_ContrastWith(COLORREF Background)
{
    const int r = Background & 0xFF, g = (Background >> 8) & 0xFF, b = (Background >> 16) & 0xFF;
    return (r * 299 + g * 587 + b * 114) / 1000 < 128 ? RGB(255, 255, 255) : RGB(0, 0, 0);
}

static LR_LineLook LR_LookForRaw(LR_LineState State, int BlinkLeft, float Prox,
                                 const LR_LookInputs& In)
{
    LR_LineLook L;
    L.Hide         = 0;
    L.LabelColor   = In.TargetColor;
    L.ShowTgtLabel = 1;

    if (BlinkLeft > 0)
    {
        // Steps 6,4,2 flash in the blink color; 5,3,1 are hidden.
        L.Color = In.BlinkColor;
        L.Width = In.TouchedWidth;
        L.Style = LINESTYLE_SOLID;
        L.Hide  = (BlinkLeft % 2 == 1) ? 1 : 0;
        return L;
    }

    switch (State)
    {
        case LR_BROKEN:
            // Solid grey, only slightly muted so it reads as "done" while
            // staying clearly visible.
            L.Color      = LR_Mix(In.BrokenColor, In.Background, 0.10f);
            L.LabelColor = In.BrokenColor;
            L.ShowTgtLabel = 0;
            L.Width      = In.BrokenWidth;
            L.Style      = LINESTYLE_SOLID;
            return L;

        case LR_EXPIRED:
            L.Color      = In.ExpiredColor;
            L.LabelColor = In.ExpiredColor;
            L.ShowTgtLabel = 0;
            L.Width      = In.BaseWidth;
            L.Style      = LINESTYLE_DASH;
            return L;

        case LR_TOUCHED:
            L.Color = In.TargetColor;
            L.Width = In.TouchedWidth;
            L.Style = LINESTYLE_SOLID;
            return L;

        case LR_UNTOUCHED:
        default:
            break;
    }

    L.Style = LINESTYLE_DASH;
    L.Width = In.BaseWidth + static_cast<int>(
                  Prox * (In.ApproachWidth - In.BaseWidth) + 0.5f);
    if (L.Width < 1)
        L.Width = 1;
    L.Color = LR_Mix(In.TargetColor, In.Background,
                     (In.FarDimPct / 100.0f) * (1.0f - Prox));
    return L;
}

// The look for a line's state, with the line / text transparency applied.
static LR_LineLook LR_LookFor(LR_LineState State, int BlinkLeft, float Prox,
                              const LR_LookInputs& In)
{
    LR_LineLook L = LR_LookForRaw(State, BlinkLeft, Prox, In);
    L.Color      = LR_Mix(L.Color,      In.Background, In.LineFade);
    L.LabelColor = LR_Mix(L.LabelColor, In.Background, In.TextFade);
    return L;
}

//----------------------------------------------------------------------------
// Which bars a target line covers, by state:
//   untouched / blinking : full length, from where it spawned (the pattern
//                          extreme) to FullEnd
//   touched / broken     : collapsed to CollapsePct (BrokenPct once broken)
//                          of the length it had when touched, centered on the
//                          touch bar (stretched right if needed so a later
//                          break X still sits on it)
//   expired              : collapsed to CollapsePct of its full length,
//                          left-aligned on where it spawned
// A collapsed line around a fresh touch can end past the newest bar; that
// part is drawn into the space to the right of the last bar.
//----------------------------------------------------------------------------
static void LR_LineSpan(const LR_Level& Lv, LR_LineState State, int Blinking,
                        int TouchBar, int BrokenAt, int FullEnd,
                        int CollapsePct, int BrokenPct, int& Begin, int& End)
{
    Begin = Lv.ExtIndex;
    End   = FullEnd;

    if (!Blinking && (State == LR_TOUCHED || State == LR_BROKEN) && TouchBar >= 0)
    {
        const int Len = TouchBar - Lv.ExtIndex;
        const int Pct = (State == LR_BROKEN) ? BrokenPct : CollapsePct;
        int Half = static_cast<int>(Len * Pct / 200.0f + 0.5f);
        if (Half < 1)
            Half = 1;

        Begin = TouchBar - Half;
        End   = TouchBar + Half;
        if (BrokenAt > End)
            End = BrokenAt;
    }
    else if (State == LR_EXPIRED)
    {
        const int Len = FullEnd - Lv.ExtIndex;
        int Seg = static_cast<int>(Len * CollapsePct / 100.0f + 0.5f);
        if (Seg < 2)
            Seg = 2;
        End = Lv.ExtIndex + Seg;
        if (End > FullEnd)
            End = FullEnd;
    }

    if (End <= Begin)
        End = Begin + 1;
}

//----------------------------------------------------------------------------
// Horizontal target line over [BeginIndex, EndIndex], plus its
// "Short/Long TGT" label at the right end. Shares the setup's line numbers,
// so calling it again just moves / restyles the existing line.
//
// Line numbers per setup (Base = 71000 + SetupID * 8):
//   +0 riz label   +1 trend ray     +2 expired tag   +3 break X   +4 target line
//   +5 H label     +6 TGT label     +7 break tag
//----------------------------------------------------------------------------
static void LR_DrawTargetLevel(
    SCStudyInterfaceRef sc,
    const LR_Level& Lv,
    int   BeginIndex,
    int   EndIndex,
    const LR_LineLook& Look,
    const LR_LookInputs& In,
    int   ShowLabels)
{
    const int Base = 71000 + (Lv.SetupID % 4000) * 8;

    int TargetEnd = EndIndex;
    if (TargetEnd <= BeginIndex)
        TargetEnd = BeginIndex + 1;

    s_UseTool Tool;

    Tool.Clear();
    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_LINE;
    Tool.LineNumber  = Base + 4;
    Tool.BeginIndex  = BeginIndex;
    // Past the newest bar: position by projected date-time instead.
    const int Future = (TargetEnd > sc.ArraySize - 1);
    if (Future)
        Tool.EndDateTime = sc.BaseDateTimeIn[TargetEnd];
    else
        Tool.EndIndex = TargetEnd;
    Tool.BeginValue  = Lv.Value;
    Tool.EndValue    = Lv.Value;
    Tool.Color       = Look.Color;
    Tool.LineWidth   = Look.Width;
    Tool.LineStyle   = Look.Style;
    Tool.HideDrawing = Look.Hide;
    Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);

    if (ShowLabels == 0)
        return;

    // "riz" just left of where the line starts (follows it when collapsed,
    // and blinks with it).
    Tool.Clear();
    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_TEXT;
    Tool.LineNumber  = Base + 0;
    Tool.BeginIndex  = BeginIndex;
    Tool.BeginValue  = Lv.Value;
    Tool.Color       = Look.LabelColor;
    Tool.FontSize    = In.RizFontSize;
    Tool.FontBold    = 0;
    Tool.TransparentLabelBackground = 1;
    Tool.TextAlignment = DT_RIGHT | DT_VCENTER;
    Tool.Text        = "riz ";
    Tool.HideDrawing = Look.Hide;
    Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);

    if (!Look.ShowTgtLabel)
    {
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, Base + 6);
        return;
    }

    Tool.Clear();
    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_TEXT;
    Tool.LineNumber  = Base + 6;
    if (Future)
        Tool.BeginDateTime = sc.BaseDateTimeIn[TargetEnd];
    else
        Tool.BeginIndex = TargetEnd;
    Tool.BeginValue  = Lv.Value;
    Tool.Color       = Look.LabelColor;
    Tool.FontSize    = In.LabelFontSize;
    Tool.FontBold    = 1;
    Tool.TransparentLabelBackground = 1;
    Tool.TextAlignment = DT_LEFT | DT_VCENTER;
    Tool.Text.Format("%s TGT %s",
        Lv.Dir < 0 ? "Short" : "Long",
        sc.FormatGraphValue(Lv.Value, sc.GetValueFormat()).GetChars());
    Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);
}

//----------------------------------------------------------------------------
// Signed indicator value for the break tag: +60, -245, or +0.35 for values
// under 1 so small-priced instruments don't all read +0.
//----------------------------------------------------------------------------
static SCString LR_FormatSigned(float V)
{
    SCString S;
    if (fabs(V) >= 1.0)
        S.Format("%+.0f", V);
    else
        S.Format("%+.2f", V);
    return S;
}

//----------------------------------------------------------------------------
// Break marker: an X where the close went through, plus a bold tag centered
// over the middle of the (collapsed) broken line, sitting just above it so
// the line and the X stay visible underneath, e.g.
//      Broken 9:01 / +60 / 2340        (time / Waddah / volume of break bar)
//   ===========X=========
//----------------------------------------------------------------------------
static void LR_DrawBreakTag(SCStudyInterfaceRef sc, const LR_Level& Lv,
                            int BarIndex, int TagIndex, const SCString& Text,
                            const LR_LookInputs& In)
{
    const int      Base     = 71000 + (Lv.SetupID % 4000) * 8;
    const COLORREF Contrast = LR_ContrastWith(In.Background);

    s_UseTool Tool;

    Tool.Clear();
    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_MARKER;
    Tool.LineNumber  = Base + 3;
    Tool.BeginIndex  = BarIndex;
    Tool.BeginValue  = Lv.Value;
    Tool.MarkerType  = MARKER_X;
    Tool.MarkerSize  = 5;
    Tool.LineWidth   = 2;
    Tool.Color       = LR_Mix(LR_Mix(In.BrokenColor, Contrast, 0.15f),
                              In.Background, In.LineFade);
    Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);

    Tool.Clear();
    Tool.ChartNumber   = sc.ChartNumber;
    Tool.DrawingType   = DRAWING_TEXT;
    Tool.LineNumber    = Base + 7;
    if (TagIndex > sc.ArraySize - 1)
        Tool.BeginDateTime = sc.BaseDateTimeIn[TagIndex];   // past the newest bar
    else
        Tool.BeginIndex = TagIndex;
    Tool.BeginValue    = Lv.Value;
    Tool.Color         = LR_Mix(LR_Mix(In.BrokenColor, Contrast, 0.55f),
                                In.Background, In.TextFade);
    Tool.TransparentLabelBackground = 1;
    Tool.FontSize      = In.TagFontSize;
    Tool.FontBold      = 1;
    Tool.TextAlignment = DT_CENTER | DT_BOTTOM;
    Tool.Text          = Text;
    Tool.AddMethod     = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);
}

//----------------------------------------------------------------------------
// "expired" centered over the middle of the (collapsed) line, sitting just
// above it so the short dark red line stays visible underneath.
//----------------------------------------------------------------------------
static void LR_DrawExpiredTag(SCStudyInterfaceRef sc, const LR_Level& Lv,
                              int BeginIndex, int EndIndex, const LR_LookInputs& In)
{
    const int Base = 71000 + (Lv.SetupID % 4000) * 8;

    int Mid = (BeginIndex + EndIndex) / 2;
    if (Mid < BeginIndex)
        Mid = BeginIndex;

    s_UseTool Tool;
    Tool.Clear();
    Tool.ChartNumber   = sc.ChartNumber;
    Tool.DrawingType   = DRAWING_TEXT;
    Tool.LineNumber    = Base + 2;
    Tool.BeginIndex    = Mid;
    Tool.BeginValue    = Lv.Value;
    Tool.Color         = LR_Mix(LR_Mix(In.ExpiredColor, LR_ContrastWith(In.Background), 0.20f),
                                In.Background, In.TextFade);
    Tool.TransparentLabelBackground = 1;
    Tool.FontSize      = In.TagFontSize;
    Tool.FontBold      = 1;
    Tool.TextAlignment = DT_CENTER | DT_BOTTOM;
    Tool.Text          = "expired";
    Tool.AddMethod     = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);
}

//----------------------------------------------------------------------------
// Price reached a watched level on bar t: mark it touched (permanent), note
// which side price came from, and when this happens live, blink the line and
// raise the alert.
//----------------------------------------------------------------------------
static void LR_OnTouch(SCStudyInterfaceRef sc, const LR_Level& Lv, int t,
                       int Live, int LineIsDrawn, int AlertNumber, RG_ExternalAlerts& Ext)
{
    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchedBase + Lv.Slot) = 1;

    int Side = LR_TouchSide(sc, t, Lv.Value);
    if (Side == 0)
        Side = (t >= 1 && sc.Close[t - 1] < Lv.Value) ? -1 : +1;
    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchSideBase + Lv.Slot) = Side;

    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchBarBase + Lv.Slot) = t;

    // A line that stopped extending still reaches out to where it was hit.
    int& FrozenEnd = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_FrozenEndBase + Lv.Slot);
    if (FrozenEnd >= 0 && FrozenEnd < t)
        FrozenEnd = t;

    if (!Live)
        return;

    if (LineIsDrawn)
    {
        sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BlinkBase + Lv.Slot)        = LR::BLINK_STEPS;
        sc.GetPersistentDouble(LR::KEY_BASE + LR::PD_BlinkTimeBase + Lv.Slot) = LR_NowMs();
    }

    if (AlertNumber > 0)
    {
        SCString Msg;
        Msg.Format("Little Rizzy: price touched %s TGT %s",
            Lv.Dir < 0 ? "Short" : "Long",
            sc.FormatGraphValue(Lv.Value, sc.GetValueFormat()).GetChars());
        Ext.Add(AlertNumber, Msg);
    }
}

//----------------------------------------------------------------------------
// Draw / refresh all chart drawings belonging to one setup.
// Each setup owns a block of 8 line numbers so historical setups persist.
//----------------------------------------------------------------------------
static void LR_DrawSetup(
    SCStudyInterfaceRef sc,
    int   SetupID,
    int   A1Index, float A1Value,
    int   A2Index, float A2Value,
    float Slope,
    int   ExtIndex, float ExtValue,
    float Distance,
    COLORREF TrendColor,
    COLORREF MeasureColor,
    int   LineWidth,
    int   ShowLabels,
    int   DrawTrendLine,
    int   TrendTransparency,
    const LR_LookInputs& In)
{
    const int Base = 71000 + (SetupID % 4000) * 8;

    s_UseTool Tool;

    // --- 1. Trend line: a ray through the two anchors, extends right forever.
    if (DrawTrendLine)
    {
        Tool.Clear();
        Tool.ChartNumber = sc.ChartNumber;
        Tool.DrawingType = DRAWING_RAY;
        Tool.LineNumber  = Base + 1;
        Tool.BeginIndex  = A1Index;
        Tool.EndIndex    = A2Index;
        Tool.BeginValue  = A1Value;
        Tool.EndValue    = A2Value;
        Tool.Color       = TrendColor;
        Tool.LineWidth   = LineWidth;
        Tool.LineStyle   = LINESTYLE_SOLID;
        Tool.TransparencyLevel = TrendTransparency;
        Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
        Tool.AddAsUserDrawnDrawing = 0;
        sc.UseTool(Tool);
    }
    else
    {
        // Trend line turned off: remove any ray we drew earlier.
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, Base + 1);
    }

    const float LineAtExtreme = LR::LineValueAt(A1Value, A1Index, Slope, ExtIndex);

    // // --- 2. Measurement leg: vertical, extreme -> trend line.
    // Tool.Clear();
    // Tool.ChartNumber = sc.ChartNumber;
    // Tool.DrawingType = DRAWING_LINE;
    // Tool.LineNumber  = Base + 2;
    // Tool.BeginIndex  = ExtIndex;
    // Tool.EndIndex    = ExtIndex;
    // Tool.BeginValue  = ExtValue;
    // Tool.EndValue    = LineAtExtreme;
    // Tool.Color       = MeasureColor;
    // Tool.LineWidth   = LineWidth;
    // Tool.LineStyle   = LINESTYLE_SOLID;
    // Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    // Tool.AddAsUserDrawnDrawing = 0;
    // sc.UseTool(Tool);

    // --- 3. (removed) Vertical projection leg, extreme -> target.
    // --- 4 + 6. The horizontal target line and its label are drawn by
    //            LR_DrawTargetLevel from the render pass, which owns their
    //            dashed / approach / blink / touched styling.

    if (ShowLabels == 0)
        return;

    // --- 5. Distance label next to the measurement leg.
    Tool.Clear();
    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_TEXT;
    Tool.LineNumber  = Base + 5;
    Tool.BeginIndex  = ExtIndex;
    Tool.BeginValue  = (ExtValue + LineAtExtreme) * 0.5f;
    Tool.Color       = LR_Mix(MeasureColor, In.Background, In.TextFade);
    Tool.FontSize    = In.LabelFontSize;
    Tool.FontBold    = 0;
    Tool.TransparentLabelBackground = 1;
    Tool.TextAlignment = DT_LEFT | DT_VCENTER;
    Tool.Text.Format("H %s", sc.FormatGraphValue(Distance, sc.GetValueFormat()).GetChars());
    Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);
}

// =============================================================================================
//  MODULE 2: LITTLE RIZZY
// =============================================================================================
static void RG_LittleRizzyModule(SCStudyInterfaceRef sc, RG_ExternalAlerts& Ext)
{
    //------------------------------------------------------------------------
    // Subgraphs
    //------------------------------------------------------------------------
    SCSubgraphRef Sg_BBTop      = sc.Subgraph[RG_SG_LITTLE_RIZZY + 0];
    SCSubgraphRef Sg_BBBottom   = sc.Subgraph[RG_SG_LITTLE_RIZZY + 1];
    SCSubgraphRef Sg_BBMid      = sc.Subgraph[RG_SG_LITTLE_RIZZY + 2];
    SCSubgraphRef Sg_SwingHigh  = sc.Subgraph[RG_SG_LITTLE_RIZZY + 3];
    SCSubgraphRef Sg_SwingLow   = sc.Subgraph[RG_SG_LITTLE_RIZZY + 4];
    SCSubgraphRef Sg_TrendLine  = sc.Subgraph[RG_SG_LITTLE_RIZZY + 5];
    SCSubgraphRef Sg_Target     = sc.Subgraph[RG_SG_LITTLE_RIZZY + 6];
    SCSubgraphRef Sg_TargetHit  = sc.Subgraph[RG_SG_LITTLE_RIZZY + 7];
    SCSubgraphRef Sg_Invalid    = sc.Subgraph[RG_SG_LITTLE_RIZZY + 8];
    SCSubgraphRef Sg_PercentB   = sc.Subgraph[RG_SG_LITTLE_RIZZY + 9];   // hidden by default
    SCSubgraphRef Sg_State      = sc.Subgraph[RG_SG_LITTLE_RIZZY + 10];  // hidden by default
    SCSubgraphRef Sg_RevUp      = sc.Subgraph[RG_SG_LITTLE_RIZZY + 11];  // level touched from above, reversed up
    SCSubgraphRef Sg_RevDown    = sc.Subgraph[RG_SG_LITTLE_RIZZY + 12];  // level touched from below, reversed down
    SCSubgraphRef Sg_TouchBG    = sc.Subgraph[RG_SG_LITTLE_RIZZY + 13];  // background on target-level touch
    SCSubgraphRef Sg_WAETrend   = sc.Subgraph[RG_SG_LITTLE_RIZZY + 14];  // Waddah trend bar (signed); Arrays[0..2] = work
    SCSubgraphRef Sg_WAEExplode = sc.Subgraph[RG_SG_LITTLE_RIZZY + 15];  // Waddah explosion line
    SCSubgraphRef Sg_WAEDead    = sc.Subgraph[RG_SG_LITTLE_RIZZY + 16];  // Waddah dead zone

    //------------------------------------------------------------------------
    // Inputs
    //------------------------------------------------------------------------
    // Most settings are the LITTLE RIZZY - SETTINGS constants above. The
    // inputs below follow this module's separator input, numbered without gaps.
    SCInputRef In_RevOffset     = sc.Input[RG_IN_RIZZY + 0];
    SCInputRef In_TouchAlert    = sc.Input[RG_IN_RIZZY + 1];
    SCInputRef In_WAEStudy      = sc.Input[RG_IN_RIZZY + 2];
    SCInputRef In_BrokenColor   = sc.Input[RG_IN_RIZZY + 3];
    SCInputRef In_ExpiredColor  = sc.Input[RG_IN_RIZZY + 4];
    SCInputRef In_CollapsePct   = sc.Input[RG_IN_RIZZY + 5];
    SCInputRef In_BrokenWidth   = sc.Input[RG_IN_RIZZY + 6];
    SCInputRef In_BrokenPct     = sc.Input[RG_IN_RIZZY + 7];
    SCInputRef In_RizFont       = sc.Input[RG_IN_RIZZY + 8];
    SCInputRef In_LabelFont     = sc.Input[RG_IN_RIZZY + 9];
    SCInputRef In_TagFont       = sc.Input[RG_IN_RIZZY + 10];
    SCInputRef In_LineTransp    = sc.Input[RG_IN_RIZZY + 11];
    SCInputRef In_TextTransp    = sc.Input[RG_IN_RIZZY + 12];

    //------------------------------------------------------------------------
    // Defaults
    //------------------------------------------------------------------------
    if (sc.SetDefaults)
    {
        // (study-wide settings - name, region, manual looping - are set by the signal module)
        sc.CalculationPrecedence = LOW_PREC_LEVEL;   // lets the optional Waddah source be read

        Sg_BBTop.Name = "Rizzy: BB Top";
        Sg_BBTop.DrawStyle = DRAWSTYLE_IGNORE;    // same 20 / 2 band as the Alert Bollinger Bands
        Sg_BBTop.PrimaryColor = RGB(128, 128, 160);
        Sg_BBTop.DrawZeros = false;

        Sg_BBBottom.Name = "Rizzy: BB Bottom";
        Sg_BBBottom.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_BBBottom.PrimaryColor = RGB(128, 128, 160);
        Sg_BBBottom.DrawZeros = false;

        Sg_BBMid.Name = "Rizzy: BB Mid (Reality)";
        Sg_BBMid.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_BBMid.PrimaryColor = RGB(90, 90, 120);
        Sg_BBMid.DrawZeros = false;

        Sg_SwingHigh.Name = "Rizzy: Swing High";
        Sg_SwingHigh.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_SwingHigh.LineWidth = 4;
        Sg_SwingHigh.PrimaryColor = RGB(220, 80, 80);
        Sg_SwingHigh.DrawZeros = false;

        Sg_SwingLow.Name = "Rizzy: Swing Low";
        Sg_SwingLow.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_SwingLow.LineWidth = 4;
        Sg_SwingLow.PrimaryColor = RGB(80, 190, 110);
        Sg_SwingLow.DrawZeros = false;

        Sg_TrendLine.Name = "Rizzy: Trend Line Value";
        Sg_TrendLine.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_TrendLine.PrimaryColor = RGB(255, 200, 0);
        Sg_TrendLine.DrawZeros = false;

        Sg_Target.Name = "Rizzy: Target";
        Sg_Target.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_Target.PrimaryColor = RGB(0, 200, 255);
        Sg_Target.DrawZeros = false;

        Sg_TargetHit.Name = "Rizzy: Target Hit";
        Sg_TargetHit.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_TargetHit.LineWidth = 6;
        Sg_TargetHit.PrimaryColor = RGB(0, 220, 255);
        Sg_TargetHit.DrawZeros = false;

        Sg_Invalid.Name = "Rizzy: Invalidated";
        Sg_Invalid.DrawStyle = DRAWSTYLE_POINT;
        Sg_Invalid.LineWidth = 6;
        Sg_Invalid.PrimaryColor = RGB(255, 140, 0);
        Sg_Invalid.DrawZeros = false;

        Sg_PercentB.Name = "Rizzy: Percent B";
        Sg_PercentB.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_PercentB.PrimaryColor = RGB(160, 160, 160);
        Sg_PercentB.DrawZeros = false;

        Sg_State.Name = "Rizzy: Setup State (+1/-1)";
        Sg_State.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_State.PrimaryColor = RGB(160, 160, 160);
        Sg_State.DrawZeros = false;

        Sg_RevUp.Name = "Rizzy: Target Level Reversal Up";
        Sg_RevUp.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_RevUp.LineWidth = 3;
        Sg_RevUp.PrimaryColor = RGB(255, 60, 220);
        Sg_RevUp.DrawZeros = false;

        Sg_RevDown.Name = "Rizzy: Target Level Reversal Down";
        Sg_RevDown.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_RevDown.LineWidth = 3;
        Sg_RevDown.PrimaryColor = RGB(255, 60, 220);
        Sg_RevDown.DrawZeros = false;

        // Value is the touched level price (keeps it inside the price scale).
        // How see-through it is comes from the study's Transparency Level
        // (Study Settings), which current Sierra Chart versions no longer
        // expose as an sc.* member.
        Sg_TouchBG.Name = "Rizzy: Target Touch Background";
        Sg_TouchBG.DrawStyle = DRAWSTYLE_BACKGROUND_TRANSPARENT;
        Sg_TouchBG.PrimaryColor = RGB(0, 255, 255);
        Sg_TouchBG.DrawZeros = false;

        // Waddah Attar Explosion values, shown in the Chart Values window and
        // used for the break tag. Not plotted on the price chart.
        Sg_WAETrend.Name = "Rizzy: WAE Trend (+Up / -Down)";
        Sg_WAETrend.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_WAETrend.PrimaryColor = RGB(0, 200, 120);
        Sg_WAETrend.DrawZeros = false;

        Sg_WAEExplode.Name = "Rizzy: WAE Explosion Line";
        Sg_WAEExplode.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_WAEExplode.PrimaryColor = RGB(160, 120, 40);
        Sg_WAEExplode.DrawZeros = false;

        Sg_WAEDead.Name = "Rizzy: WAE Dead Zone";
        Sg_WAEDead.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_WAEDead.PrimaryColor = RGB(60, 110, 200);
        Sg_WAEDead.DrawZeros = false;

        // Most settings are hardcoded in the LITTLE RIZZY - SETTINGS block.
        // Only the inputs below remain in Study Settings.

        // --- Target level touch / touch-and-reverse --------------------------
        In_RevOffset.Name = "Reversal Marker Offset (Ticks)";
        In_RevOffset.SetInt(3);
        In_RevOffset.SetIntLimits(0, 1000);

        // Sierra Chart alert number (sound set in Global Settings >> Alerts).
        In_TouchAlert.Name = "Target Touch Alert Number (0 = Off)";
        In_TouchAlert.SetInt(5);
        In_TouchAlert.SetIntLimits(0, 150);

        // --- Broken / expired target lines -----------------------------------
        // Broken  = after a touch, a bar CLOSES through the level (beyond
        //           REVERSAL_BREAK_TICKS). Line turns grey + dim, tagged with
        //           the break time, Waddah intensity and volume.
        // Expired = the level drops off the watch list never having been
        //           touched. Line turns dark red with "expired" in the middle.

        // Leave at None to use the built-in Waddah Attar Explosion
        // (20/40 EMA, sensitivity 150, BB 20/2, dead zone ATR 100 x 3.7).
        // Or pick the subgraph of your own Waddah study on this chart to
        // show its value in the break tag instead.
        In_WAEStudy.Name = "Waddah Source For Break Tag (None = Built-In)";
        In_WAEStudy.SetStudySubgraphValues(0, 0);

        In_BrokenColor.Name = "Broken Target Line Color";
        In_BrokenColor.SetColor(140, 140, 150);

        In_ExpiredColor.Name = "Expired Target Line Color";
        In_ExpiredColor.SetColor(176, 73, 14);

        // Touched lines shrink to this % of their length, centered on the
        // touch. Expired (never touched) lines shrink to it, left-aligned on
        // where they spawned. 100 = keep full length.
        In_CollapsePct.Name = "Touched / Expired Line Length (% Of Original)";
        In_CollapsePct.SetInt(20);
        In_CollapsePct.SetIntLimits(5, 100);

        In_BrokenWidth.Name = "Broken Line Thickness";
        In_BrokenWidth.SetInt(3);
        In_BrokenWidth.SetIntLimits(1, 10);

        // Broken lines get their own (longer) collapsed length, centered on
        // the touch like touched lines.
        In_BrokenPct.Name = "Broken Line Length (% Of Original)";
        In_BrokenPct.SetInt(50);
        In_BrokenPct.SetIntLimits(5, 100);

        // --- Text and transparency ------------------------------------------
        In_RizFont.Name = "riz Label Font Size";
        In_RizFont.SetInt(8);
        In_RizFont.SetIntLimits(4, 40);

        In_LabelFont.Name = "TGT / H Label Font Size";
        In_LabelFont.SetInt(9);
        In_LabelFont.SetIntLimits(4, 40);

        In_TagFont.Name = "Broken / Expired Tag Font Size";
        In_TagFont.SetInt(8);
        In_TagFont.SetIntLimits(4, 40);

        // Sierra Chart can't draw lines or text truly see-through, so these
        // fade them toward the chart background: 0 = solid, 90 = faint.
        In_LineTransp.Name = "Target Line Transparency (%)";
        In_LineTransp.SetInt(0);
        In_LineTransp.SetIntLimits(0, 90);

        In_TextTransp.Name = "Text Transparency (%)";
        In_TextTransp.SetInt(0);
        In_TextTransp.SetIntLimits(0, 90);

        return;
    }

    //------------------------------------------------------------------------
    // Settings: LITTLE RIZZY - SETTINGS constants + the remaining
    // study inputs
    //------------------------------------------------------------------------
    const int   Strength      = LittleRizzy::PIVOT_STRENGTH;
    const int   MinBars       = LittleRizzy::MIN_ANCHOR_BARS;
    const int   DoBear        = LittleRizzy::DETECT_BEARISH;
    const int   DoBull        = LittleRizzy::DETECT_BULLISH;
    const int   BBLength      = LittleRizzy::BB_LENGTH;
    const float BBStdDev      = LittleRizzy::BB_STD_DEVS;
    const int   BBMAType      = LittleRizzy::BB_MA_TYPE;
    const int   BBInputIndex  = LittleRizzy::BB_INPUT_DATA;
    const int   RequireBand   = LittleRizzy::REQUIRE_BAND_PIVOT;
    const float BandThreshold = LittleRizzy::BAND_PCTB_THRESHOLD;
    const int   TrackExtreme  = LittleRizzy::REMEASURE_NEW_EXTREME;
    const int   DoInvalidate  = LittleRizzy::INVALIDATE_ON_CLOSE;
    const float Buffer        = LittleRizzy::INVALIDATION_TICKS * sc.TickSize;
    const int   ReplaceSetup  = LittleRizzy::NEW_REPLACES_ACTIVE;
    const int   DrawPattern   = LittleRizzy::DRAW_PATTERN;
    const int   ShowLabels    = LittleRizzy::SHOW_TEXT_LABELS;
    const int   LineWidth     = LittleRizzy::LINE_WIDTH;
    const int   EnableAlerts  = LittleRizzy::ENABLE_ALERTS;
    const int   DrawTrendLine = LittleRizzy::DRAW_TREND_LINE;
    const int   MarkReversal  = LittleRizzy::MARK_REVERSALS;
    const int   RevWindow     = LittleRizzy::REVERSAL_WINDOW;
    const float RevBuffer     = LittleRizzy::REVERSAL_BREAK_TICKS * sc.TickSize;
    const int   RevCandle     = LittleRizzy::REQUIRE_OPPOSITE_BAR;
    const float RevOffset     = In_RevOffset.GetInt() * sc.TickSize;
    const int   BGMode        = LittleRizzy::TOUCH_BACKGROUND;   // 0 off, 1 current hit, 2 any watched level
    int         NumPrev       = LittleRizzy::PREV_LEVELS_TO_WATCH;
    if (NumPrev < 0)                   NumPrev = 0;
    if (NumPrev > LR::MAX_PREV_LEVELS) NumPrev = LR::MAX_PREV_LEVELS;
    const int   ExtendLines   = LittleRizzy::EXTEND_PREV_LINES;
    const int   TouchAlertNum = In_TouchAlert.GetInt();

    int BrokenPct = In_BrokenPct.GetInt();
    if (BrokenPct < 5 || BrokenPct > 100)
        BrokenPct = 50;

    int CollapsePct = In_CollapsePct.GetInt();
    if (CollapsePct < 5 || CollapsePct > 100)
        CollapsePct = 20;

    int BlinkMs = LittleRizzy::BLINK_MS;
    if (BlinkMs < 50)
        BlinkMs = 250;

    // Look settings for the target lines (clamped, so odd values can't
    // produce zero-width or shrinking lines).
    LR_LookInputs Look;
    Look.TargetColor   = LittleRizzy::TARGET_COLOR;
    Look.BlinkColor    = LittleRizzy::BLINK_COLOR;
    Look.BrokenColor   = In_BrokenColor.GetColor();
    Look.ExpiredColor  = In_ExpiredColor.GetColor();
    Look.Background    = sc.ChartBackgroundColor;
    Look.BaseWidth     = LineWidth < 1 ? 1 : LineWidth;
    Look.ApproachWidth = LittleRizzy::APPROACH_MAX_WIDTH;
    if (Look.ApproachWidth < Look.BaseWidth)
        Look.ApproachWidth = Look.BaseWidth;
    Look.TouchedWidth  = LittleRizzy::TOUCHED_WIDTH < 1 ? 1 : LittleRizzy::TOUCHED_WIDTH;
    Look.BrokenWidth   = In_BrokenWidth.GetInt()  < 1 ? 3 : In_BrokenWidth.GetInt();
    Look.ZonePct       = static_cast<float>(LittleRizzy::APPROACH_ZONE_PCT);
    Look.FarDimPct     = LittleRizzy::FAR_FADE_PCT;
    if (Look.FarDimPct < 0)  Look.FarDimPct = 0;
    if (Look.FarDimPct > 90) Look.FarDimPct = 90;

    auto Clamp = [](int v, int lo, int hi, int dflt) { return (v < lo || v > hi) ? dflt : v; };
    Look.RizFontSize   = Clamp(In_RizFont.GetInt(),   4, 40, 8);
    Look.LabelFontSize = Clamp(In_LabelFont.GetInt(), 4, 40, 9);
    Look.TagFontSize   = Clamp(In_TagFont.GetInt(),   4, 40, 8);
    Look.LineFade      = Clamp(In_LineTransp.GetInt(), 0, 90, 0) / 100.0f;
    Look.TextFade      = Clamp(In_TextTransp.GetInt(), 0, 90, 0) / 100.0f;


    // Sierra Chart wants transparency (0 = solid), the setting is opacity.
    int TrendTransparency = 100 - LittleRizzy::TREND_LINE_OPACITY;
    if (TrendTransparency < 0)   TrendTransparency = 0;
    if (TrendTransparency > 100) TrendTransparency = 100;

    //------------------------------------------------------------------------
    // Persistent state
    //------------------------------------------------------------------------
    int&   r_LastEval     = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_LastEvalIndex);
    int&   r_Active       = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_SetupActive);
    int&   r_Dir          = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_SetupDir);
    int&   r_A1Index      = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_A1Index);
    int&   r_A2Index      = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_A2Index);
    int&   r_ExtIndex     = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_ExtIndex);
    int&   r_SetupID      = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_SetupID);
    int&   r_LastHighIdx  = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_LastHighIdx);
    int&   r_PrevHighIdx  = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_PrevHighIdx);
    int&   r_LastLowIdx   = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_LastLowIdx);
    int&   r_PrevLowIdx   = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_PrevLowIdx);

    float& r_A1Value      = sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_A1Value);
    float& r_A2Value      = sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_A2Value);
    float& r_ExtValue     = sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_ExtValue);
    float& r_Distance     = sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_Distance);
    float& r_Target       = sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_Target);
    float& r_Slope        = sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_Slope);
    float& r_LastHighVal  = sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_LastHighVal);
    float& r_PrevHighVal  = sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_PrevHighVal);
    float& r_LastLowVal   = sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_LastLowVal);
    float& r_PrevLowVal   = sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_PrevLowVal);

    // Full recalculation: wipe state and our own chart drawings.
    if (sc.UpdateStartIndex == 0)
    {
        r_LastEval    = -1;
        r_Active      = 0;
        r_Dir         = 0;
        r_A1Index     = -1;
        r_A2Index     = -1;
        r_ExtIndex    = -1;
        r_SetupID     = 0;
        r_LastHighIdx = -1;
        r_PrevHighIdx = -1;
        r_LastLowIdx  = -1;
        r_PrevLowIdx  = -1;
        r_A1Value = r_A2Value = r_ExtValue = 0.0f;
        r_Distance = r_Target = r_Slope = 0.0f;
        r_LastHighVal = r_PrevHighVal = r_LastLowVal = r_PrevLowVal = 0.0f;
        for (int k = 1; k <= LR::MAX_PREV_LEVELS; ++k)
        {
            sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_PrevLevelBase + k) = 0.0f;
            sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_PrevDistBase + k)  = 0.0f;
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_PrevExtBase + k)     = -1;
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_PrevDirBase + k)     = 0;
        }
        for (int k = 0; k <= LR::MAX_PREV_LEVELS; ++k)
        {
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchedBase + k)        = 0;
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BlinkBase + k)          = 0;
            sc.GetPersistentDouble(LR::KEY_BASE + LR::PD_BlinkTimeBase + k)   = 0.0;
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchSideBase + k)      = 0;
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BrokenBase + k)         = -1;
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_FrozenEndBase + k)      = -1;
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchBarBase + k)       = -1;
        }
        sc.UpdateAlways = 0;

        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_ALL, 0);
    }

    if (sc.ArraySize < 2)
        return;

    //------------------------------------------------------------------------
    // Bollinger Bands + %B (recalculated through the forming bar)
    //------------------------------------------------------------------------
    for (int i = sc.UpdateStartIndex; i < sc.ArraySize; ++i)
    {
        if (i < BBLength - 1)
        {
            Sg_BBTop[i] = Sg_BBBottom[i] = Sg_BBMid[i] = 0.0f;
            Sg_PercentB[i] = 0.0f;
            continue;
        }

        sc.MovingAverage(sc.BaseDataIn[BBInputIndex], Sg_BBMid, BBMAType, i, BBLength);

        double SumSq = 0.0;
        const double Mean = Sg_BBMid[i];
        for (int k = i - BBLength + 1; k <= i; ++k)
        {
            const double d = sc.BaseDataIn[BBInputIndex][k] - Mean;
            SumSq += d * d;
        }
        const float SD = static_cast<float>(sqrt(SumSq / BBLength));

        Sg_BBTop[i]    = Sg_BBMid[i] + BBStdDev * SD;
        Sg_BBBottom[i] = Sg_BBMid[i] - BBStdDev * SD;

        const float BandRange = Sg_BBTop[i] - Sg_BBBottom[i];
        Sg_PercentB[i] = (BandRange > 0.0f)
            ? (sc.Close[i] - Sg_BBBottom[i]) / BandRange
            : 0.5f;
    }

    //------------------------------------------------------------------------
    // Waddah Attar Explosion, built in (recalculated through the forming bar)
    //   Trend     = (MACD(20,40)[i] - MACD(20,40)[i-1]) * 150   (+ up, - down)
    //   Explosion = Bollinger(20, 2) upper - lower
    //   Dead zone = Wilder average of True Range(100) * 3.7
    //------------------------------------------------------------------------
    {
        SCFloatArrayRef FastEMA = Sg_WAETrend.Arrays[0];
        SCFloatArrayRef SlowEMA = Sg_WAETrend.Arrays[1];
        SCFloatArrayRef TrAvg   = Sg_WAETrend.Arrays[2];

        const float AlphaFast = 2.0f / (LR::WAE_FAST + 1);
        const float AlphaSlow = 2.0f / (LR::WAE_SLOW + 1);

        for (int i = sc.UpdateStartIndex; i < sc.ArraySize; ++i)
        {
            const float C = sc.Close[i];

            if (i == 0)
            {
                FastEMA[i] = SlowEMA[i] = C;
                TrAvg[i]   = sc.High[i] - sc.Low[i];
                Sg_WAETrend[i] = Sg_WAEExplode[i] = Sg_WAEDead[i] = 0.0f;
                continue;
            }

            FastEMA[i] = FastEMA[i - 1] + AlphaFast * (C - FastEMA[i - 1]);
            SlowEMA[i] = SlowEMA[i - 1] + AlphaSlow * (C - SlowEMA[i - 1]);
            Sg_WAETrend[i] = ((FastEMA[i] - SlowEMA[i]) - (FastEMA[i - 1] - SlowEMA[i - 1]))
                             * LR::WAE_SENSITIVITY;

            const float PrevC = sc.Close[i - 1];
            float TR = sc.High[i] - sc.Low[i];
            const float UpGap   = static_cast<float>(fabs(sc.High[i] - PrevC));
            const float DownGap = static_cast<float>(fabs(sc.Low[i]  - PrevC));
            if (UpGap   > TR) TR = UpGap;
            if (DownGap > TR) TR = DownGap;
            TrAvg[i] = TrAvg[i - 1] + (TR - TrAvg[i - 1]) / LR::WAE_DZ_LENGTH;
            Sg_WAEDead[i] = TrAvg[i] * LR::WAE_DZ_MULT;

            if (i >= LR::WAE_BB_LENGTH - 1)
            {
                double Sum = 0.0;
                for (int k = i - LR::WAE_BB_LENGTH + 1; k <= i; ++k)
                    Sum += sc.Close[k];
                const double Mean = Sum / LR::WAE_BB_LENGTH;

                double SumSq = 0.0;
                for (int k = i - LR::WAE_BB_LENGTH + 1; k <= i; ++k)
                {
                    const double d = sc.Close[k] - Mean;
                    SumSq += d * d;
                }
                Sg_WAEExplode[i] = static_cast<float>(
                    2.0 * LR::WAE_BB_MULT * sqrt(SumSq / LR::WAE_BB_LENGTH));
            }
            else
            {
                Sg_WAEExplode[i] = 0.0f;
            }
        }
    }

    // Optional: the user's own Waddah study subgraph, for the break tag.
    SCFloatArray WAERef;
    if (In_WAEStudy.GetStudyID() != 0)
        sc.GetStudyArrayUsingID(In_WAEStudy.GetStudyID(),
                                In_WAEStudy.GetSubgraphIndex(), WAERef);

    // Break tag text for bar b:  Broken 9:01 / +60 / 2340
    //   time   = start time of the bar that closed through the line
    //   +60    = Waddah trend bar (+ up, - down), or your own Waddah
    //            study's value when one is selected
    //   2340   = that bar's volume
    auto BreakText = [&](int b) -> SCString
    {
        SCDateTime BarTime = sc.BaseDateTimeIn[b];

        const float Wae = (WAERef.GetArraySize() > b) ? WAERef[b] : Sg_WAETrend[b];

        SCString Out;
        Out.Format(" Broken %d:%02d / %s / %.0f ",
            BarTime.GetHour(), BarTime.GetMinute(),
            LR_FormatSigned(Wae).GetChars(), sc.Volume[b]);
        return Out;
    };

    //------------------------------------------------------------------------
    // Pattern state machine - CLOSED BARS ONLY
    //------------------------------------------------------------------------
    const int MinRequired = 2 * Strength;
    int StartIndex = r_LastEval + 1;
    if (StartIndex < MinRequired)
        StartIndex = MinRequired;

    const COLORREF BearColor    = LittleRizzy::BEAR_TREND_COLOR;
    const COLORREF BullColor    = LittleRizzy::BULL_TREND_COLOR;
    const COLORREF MeasureColor = LittleRizzy::MEASURE_COLOR;
    for (int i = StartIndex; i < sc.ArraySize; ++i)
    {
        if (sc.GetBarHasClosedStatus(i) != BHCS_BAR_HAS_CLOSED)
            break;

        Sg_SwingHigh[i] = 0.0f;
        Sg_SwingLow[i]  = 0.0f;
        Sg_TargetHit[i] = 0.0f;
        Sg_Invalid[i]   = 0.0f;
        Sg_RevUp[i]     = 0.0f;
        Sg_RevDown[i]   = 0.0f;
        Sg_TouchBG[i]   = 0.0f;

        const int IsLive = (sc.IsFullRecalculation == 0 && i >= sc.ArraySize - 2);

        //--------------------------------------------------------------------
        // A. Manage the active setup first (invalidation / target / re-measure)
        //--------------------------------------------------------------------
        if (r_Active)
        {
            const float LineHere = LR::LineValueAt(r_A1Value, r_A1Index, r_Slope, i);

            bool Killed = false;

            if (DoInvalidate)
            {
                if (r_Dir < 0 && sc.Close[i] > LineHere + Buffer)
                {
                    Sg_Invalid[i] = sc.Close[i];
                    Killed = true;
                }
                else if (r_Dir > 0 && sc.Close[i] < LineHere - Buffer)
                {
                    Sg_Invalid[i] = sc.Close[i];
                    Killed = true;
                }

                if (Killed)
                {
                    r_Active = 0;
                    if (EnableAlerts && IsLive)
                        Ext.Add(2, "Little Rizzy: setup invalidated (close beyond trend line)");
                }
            }

            if (r_Active)
            {
                // Target reached?
                if ((r_Dir < 0 && sc.Low[i]  <= r_Target) ||
                    (r_Dir > 0 && sc.High[i] >= r_Target))
                {
                    Sg_TargetHit[i] = r_Target;
                    r_Active = 0;
                    if (EnableAlerts && IsLive)
                        Ext.Add(3, "Little Rizzy: measured move target reached");
                }
                // Otherwise optionally re-measure from a new extreme.
                else if (TrackExtreme)
                {
                    bool NewExtreme = false;
                    if (r_Dir < 0 && sc.Low[i] < r_ExtValue)
                    {
                        r_ExtIndex = i;
                        r_ExtValue = sc.Low[i];
                        NewExtreme = true;
                    }
                    else if (r_Dir > 0 && sc.High[i] > r_ExtValue)
                    {
                        r_ExtIndex = i;
                        r_ExtValue = sc.High[i];
                        NewExtreme = true;
                    }

                    if (NewExtreme)
                    {
                        const float LineAtExt =
                            LR::LineValueAt(r_A1Value, r_A1Index, r_Slope, r_ExtIndex);
                        r_Distance = (r_Dir < 0)
                            ? (LineAtExt - r_ExtValue)
                            : (r_ExtValue - LineAtExt);
                        r_Target = (r_Dir < 0)
                            ? (r_ExtValue - r_Distance)
                            : (r_ExtValue + r_Distance);
                    }
                }
            }
        }

        //--------------------------------------------------------------------
        // A2. Target levels: background on touch, arrow on touch-and-reverse.
        //     Levels = current (latest) target + the previous NumPrev setups'
        //     targets, whether or not those setups reached them.
        //     A touch from above that closes back above -> arrow up.
        //     A touch from below that closes back below -> arrow down.
        //     The touch bar itself counts (wick through, close back), then up
        //     to RevWindow further bars. Closed bars only, so nothing repaints.
        //--------------------------------------------------------------------
        {
            LR_Level Levels[LR::MAX_PREV_LEVELS + 1];
            const int NumLevels = LR_CollectLevels(sc, r_SetupID, NumPrev,
                                                   r_Target, r_Distance,
                                                   r_ExtIndex, r_Dir,
                                                   Levels);

            for (int n = 0; n < NumLevels; ++n)
            {
                const LR_Level& Lv = Levels[n];

                // First touch: line goes solid. If the forming-bar check
                // below already caught it intrabar, it's marked already.
                if (sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchedBase + Lv.Slot) == 0 &&
                    LR_BarTouches(sc, i, Lv.Value))
                {
                    LR_OnTouch(sc, Lv, i, IsLive, DrawPattern, TouchAlertNum, Ext);
                }

                // Break: once touched, the first CLOSE through to the far
                // side (past the buffer). Can be the touch bar itself.
                int& BrokenAt = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BrokenBase + Lv.Slot);
                if (BrokenAt < 0 &&
                    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchedBase + Lv.Slot) != 0)
                {
                    const int Side = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchSideBase + Lv.Slot);
                    if ((Side > 0 && sc.Close[i] < Lv.Value - RevBuffer) ||
                        (Side < 0 && sc.Close[i] > Lv.Value + RevBuffer))
                    {
                        BrokenAt = i;

                        // Stopped line: run it out to the break so the X and
                        // the tag sit on it.
                        int& FrozenEnd = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_FrozenEndBase + Lv.Slot);
                        if (FrozenEnd >= 0 && FrozenEnd < i)
                            FrozenEnd = i;

                        if (DrawPattern)
                        {
                            // Where the broken line will sit, so the tag can
                            // be centered over its middle.
                            int SpanBegin, SpanEnd;
                            LR_LineSpan(Lv, LR_BROKEN, 0,
                                        sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchBarBase + Lv.Slot),
                                        i, i, CollapsePct, BrokenPct,
                                        SpanBegin, SpanEnd);
                            LR_DrawBreakTag(sc, Lv, i, (SpanBegin + SpanEnd) / 2,
                                            BreakText(i), Look);
                        }
                    }
                }
            }

            // Background highlight.
            if (BGMode == 1)
            {
                if (Sg_TargetHit[i] != 0.0f)
                    Sg_TouchBG[i] = Sg_TargetHit[i];
            }
            else if (BGMode == 2)
            {
                for (int n = 0; n < NumLevels; ++n)
                {
                    if (LR_TouchSide(sc, i, Levels[n].Value) != 0)
                    {
                        Sg_TouchBG[i] = Levels[n].Value;
                        break;
                    }
                }
            }

            // Touch-and-reverse arrows.
            if (MarkReversal)
            {
                bool RevUp   = false;
                bool RevDown = false;

                for (int n = 0; n < NumLevels; ++n)
                {
                    const int R = LR_ReversalAt(sc, i, Levels[n].Value,
                                                RevWindow, RevBuffer, RevCandle);
                    if (R > 0)      RevUp   = true;
                    else if (R < 0) RevDown = true;
                }

                if (RevUp)
                    Sg_RevUp[i] = sc.Low[i] - RevOffset;
                if (RevDown)
                    Sg_RevDown[i] = sc.High[i] + RevOffset;

                if ((RevUp || RevDown) && EnableAlerts && IsLive)
                {
                    Ext.Add(4, RevUp
                        ? "Little Rizzy: target level touched, price reversed up"
                        : "Little Rizzy: target level touched, price reversed down");
                }
            }
        }

        //--------------------------------------------------------------------
        // B. Confirm the pivot that sits Strength bars back
        //--------------------------------------------------------------------
        const int p = i - Strength;

        bool NewSwingHigh = false;
        bool NewSwingLow  = false;

        if (p - Strength >= 0)
        {
            const float PH = sc.High[p];
            const float PL = sc.Low[p];

            bool IsHigh = true;
            bool IsLow  = true;

            for (int k = p - Strength; k <= p + Strength; ++k)
            {
                if (k == p)
                    continue;
                if (sc.High[k] > PH) IsHigh = false;
                if (sc.Low[k]  < PL) IsLow  = false;
                if (!IsHigh && !IsLow)
                    break;
            }

            if (IsHigh && p != r_LastHighIdx)
            {
                r_PrevHighIdx = r_LastHighIdx;
                r_PrevHighVal = r_LastHighVal;
                r_LastHighIdx = p;
                r_LastHighVal = PH;
                Sg_SwingHigh[p] = PH;
                NewSwingHigh = true;
            }

            if (IsLow && p != r_LastLowIdx)
            {
                r_PrevLowIdx = r_LastLowIdx;
                r_PrevLowVal = r_LastLowVal;
                r_LastLowIdx = p;
                r_LastLowVal = PL;
                Sg_SwingLow[p] = PL;
                NewSwingLow = true;
            }
        }

        //--------------------------------------------------------------------
        // C. Try to build a new setup from the two most recent pivots
        //--------------------------------------------------------------------
        for (int Pass = 0; Pass < 2; ++Pass)
        {
            const int Dir = (Pass == 0) ? -1 : +1;   // bearish first, then bullish

            if (Dir < 0 && (!DoBear || !NewSwingHigh)) continue;
            if (Dir > 0 && (!DoBull || !NewSwingLow))  continue;
            if (r_Active && !ReplaceSetup)            continue;

            const int   A1Index = (Dir < 0) ? r_PrevHighIdx : r_PrevLowIdx;
            const int   A2Index = (Dir < 0) ? r_LastHighIdx : r_LastLowIdx;
            const float A1Value = (Dir < 0) ? r_PrevHighVal : r_PrevLowVal;
            const float A2Value = (Dir < 0) ? r_LastHighVal : r_LastLowVal;

            if (A1Index < 0 || A2Index <= A1Index)
                continue;
            if (A2Index - A1Index < MinBars)
                continue;

            // Trend line must actually slope with the trend.
            if (Dir < 0 && A2Value >= A1Value) continue;   // need a lower high
            if (Dir > 0 && A2Value <= A1Value) continue;   // need a higher low

            // Optional Bollinger context: confirming pivot must be at the band.
            if (RequireBand)
            {
                const float PB = Sg_PercentB[A2Index];
                if (Dir < 0 && PB < BandThreshold)          continue;
                if (Dir > 0 && PB > (1.0f - BandThreshold)) continue;
            }

            const float Slope =
                (A2Value - A1Value) / static_cast<float>(A2Index - A1Index);

            // Pattern extreme: the drop (or rally) between the two anchors.
            int   ExtIndex = A1Index;
            float ExtValue = (Dir < 0) ? sc.Low[A1Index] : sc.High[A1Index];
            for (int k = A1Index; k <= A2Index; ++k)
            {
                if (Dir < 0 && sc.Low[k] < ExtValue)
                {
                    ExtValue = sc.Low[k];
                    ExtIndex = k;
                }
                else if (Dir > 0 && sc.High[k] > ExtValue)
                {
                    ExtValue = sc.High[k];
                    ExtIndex = k;
                }
            }

            const float LineAtExt = LR::LineValueAt(A1Value, A1Index, Slope, ExtIndex);
            const float Distance  = (Dir < 0) ? (LineAtExt - ExtValue)
                                              : (ExtValue - LineAtExt);
            if (Distance <= 0.0f)
                continue;

            const float Target = (Dir < 0) ? (ExtValue - Distance)
                                           : (ExtValue + Distance);

            // Reject if the line is already broken, or the target already met,
            // on the bars between confirmation of A2 and now.
            bool AlreadyResolved = false;
            for (int k = A2Index + 1; k <= i; ++k)
            {
                const float LineK = LR::LineValueAt(A1Value, A1Index, Slope, k);
                if (DoInvalidate)
                {
                    if (Dir < 0 && sc.Close[k] > LineK + Buffer) { AlreadyResolved = true; break; }
                    if (Dir > 0 && sc.Close[k] < LineK - Buffer) { AlreadyResolved = true; break; }
                }
                if (Dir < 0 && sc.Low[k]  <= Target) { AlreadyResolved = true; break; }
                if (Dir > 0 && sc.High[k] >= Target) { AlreadyResolved = true; break; }
            }
            if (AlreadyResolved)
                continue;

            // Commit the setup. The outgoing target becomes previous level 1,
            // everything else (level data + line state) shifts back one slot.
            if (r_SetupID > 0)
            {
                // The level in the last watched slot falls off the list here.
                // Draw it one final time in its settled look: broken (grey),
                // touched (solid) or, if price never reached it, expired
                // (dark red with "expired" in the middle).
                if (DrawPattern)
                {
                    LR_Level Old[LR::MAX_PREV_LEVELS + 1];
                    const int NumOld = LR_CollectLevels(sc, r_SetupID, NumPrev,
                                                        r_Target, r_Distance,
                                                        r_ExtIndex, r_Dir, Old);
                    if (NumOld > NumPrev && Old[NumPrev].ExtIndex >= 0)
                    {
                        const LR_Level& Out = Old[NumPrev];
                        const int Frozen = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_FrozenEndBase + Out.Slot);
                        const int End    = (Frozen >= 0) ? Frozen : i;
                        const LR_LineState State = LR_StateOf(sc, Out.Slot, 1);

                        int SpanBegin, SpanEnd;
                        LR_LineSpan(Out, State, 0,
                                    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchBarBase + Out.Slot),
                                    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BrokenBase + Out.Slot),
                                    End, CollapsePct, BrokenPct, SpanBegin, SpanEnd);

                        LR_DrawTargetLevel(sc, Out, SpanBegin, SpanEnd,
                                           LR_LookFor(State, 0, 0.0f, Look),
                                           Look, ShowLabels);
                        if (State == LR_EXPIRED)
                            LR_DrawExpiredTag(sc, Out, SpanBegin, SpanEnd, Look);
                    }
                }

                for (int k = LR::MAX_PREV_LEVELS; k >= 2; --k)
                {
                    sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_PrevLevelBase + k) =
                        sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_PrevLevelBase + k - 1);
                    sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_PrevDistBase + k) =
                        sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_PrevDistBase + k - 1);
                    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_PrevExtBase + k) =
                        sc.GetPersistentInt(LR::KEY_BASE + LR::PI_PrevExtBase + k - 1);
                    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_PrevDirBase + k) =
                        sc.GetPersistentInt(LR::KEY_BASE + LR::PI_PrevDirBase + k - 1);
                }
                sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_PrevLevelBase + 1) = r_Target;
                sc.GetPersistentFloat(LR::KEY_BASE + LR::PF_PrevDistBase + 1)  = r_Distance;
                sc.GetPersistentInt(LR::KEY_BASE + LR::PI_PrevExtBase + 1)     = r_ExtIndex;
                sc.GetPersistentInt(LR::KEY_BASE + LR::PI_PrevDirBase + 1)     = r_Dir;

                for (int k = LR::MAX_PREV_LEVELS; k >= 1; --k)
                {
                    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchedBase + k) =
                        sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchedBase + k - 1);
                    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BlinkBase + k) =
                        sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BlinkBase + k - 1);
                    sc.GetPersistentDouble(LR::KEY_BASE + LR::PD_BlinkTimeBase + k) =
                        sc.GetPersistentDouble(LR::KEY_BASE + LR::PD_BlinkTimeBase + k - 1);
                    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchSideBase + k) =
                        sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchSideBase + k - 1);
                    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BrokenBase + k) =
                        sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BrokenBase + k - 1);
                    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_FrozenEndBase + k) =
                        sc.GetPersistentInt(LR::KEY_BASE + LR::PI_FrozenEndBase + k - 1);
                    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchBarBase + k) =
                        sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchBarBase + k - 1);
                }

                // Previous lines not extended: the outgoing current line
                // stops here, but keeps being restyled while still watched.
                if (!ExtendLines)
                    sc.GetPersistentInt(LR::KEY_BASE + LR::PI_FrozenEndBase + 1) = i;
            }
            // The new current level starts untouched.
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchedBase)      = 0;
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BlinkBase)        = 0;
            sc.GetPersistentDouble(LR::KEY_BASE + LR::PD_BlinkTimeBase) = 0.0;
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchSideBase)    = 0;
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BrokenBase)       = -1;
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_FrozenEndBase)    = -1;
            sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchBarBase)     = -1;

            ++r_SetupID;
            r_Active   = 1;
            r_Dir      = Dir;
            r_A1Index  = A1Index;
            r_A1Value  = A1Value;
            r_A2Index  = A2Index;
            r_A2Value  = A2Value;
            r_Slope    = Slope;
            r_ExtIndex = ExtIndex;
            r_ExtValue = ExtValue;
            r_Distance = Distance;
            r_Target   = Target;

            if (EnableAlerts && IsLive)
            {
                SCString Msg;
                Msg.Format("Little Rizzy: %s setup, target %s",
                    Dir < 0 ? "bearish" : "bullish",
                    sc.FormatGraphValue(Target, sc.GetValueFormat()).GetChars());
                Ext.Add(1, Msg);
            }

            break;  // one new setup per bar
        }

        //--------------------------------------------------------------------
        // D. Per-bar outputs + drawing refresh
        //--------------------------------------------------------------------
        if (r_Active)
        {
            Sg_TrendLine[i] = LR::LineValueAt(r_A1Value, r_A1Index, r_Slope, i);
            Sg_Target[i]    = r_Target;
            Sg_State[i]     = static_cast<float>(r_Dir);

            if (DrawPattern)
            {
                LR_DrawSetup(sc, r_SetupID,
                    r_A1Index, r_A1Value, r_A2Index, r_A2Value, r_Slope,
                    r_ExtIndex, r_ExtValue, r_Distance,
                    (r_Dir < 0) ? BearColor : BullColor,
                    MeasureColor, LineWidth, ShowLabels,
                    DrawTrendLine, TrendTransparency, Look);
            }
        }
        else
        {
            Sg_TrendLine[i] = 0.0f;
            Sg_Target[i]    = 0.0f;
            Sg_State[i]     = 0.0f;
        }

        r_LastEval = i;
    }

    const int Last = sc.ArraySize - 1;
    const int LastIsForming = (sc.GetBarHasClosedStatus(Last) != BHCS_BAR_HAS_CLOSED);

    //------------------------------------------------------------------------
    // Intrabar touch: the forming bar reaching a watched level fires the
    // blink + alert immediately rather than waiting for the bar to close.
    // A bar's range only grows, so the closed-bar pass will agree later.
    //------------------------------------------------------------------------
    if (LastIsForming && Last >= 1)
    {
        LR_Level Levels[LR::MAX_PREV_LEVELS + 1];
        const int NumLevels = LR_CollectLevels(sc, r_SetupID, NumPrev,
                                               r_Target, r_Distance,
                                               r_ExtIndex, r_Dir, Levels);
        const int LiveNow = (sc.IsFullRecalculation == 0);

        for (int n = 0; n < NumLevels; ++n)
        {
            if (sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchedBase + Levels[n].Slot) == 0 &&
                LR_BarTouches(sc, Last, Levels[n].Value))
            {
                LR_OnTouch(sc, Levels[n], Last, LiveNow, DrawPattern, TouchAlertNum, Ext);
            }
        }
    }

    //------------------------------------------------------------------------
    // Render pass, every update: redraw every watched target line, out to
    // the current bar (or to where it was frozen), styled by state:
    //   blinking  -> flash / hidden steps, BlinkMs apart
    //   broken    -> grey, thin, dotted (its tag was drawn at the break)
    //   touched   -> solid, touched width
    //   untouched -> dashed; faded when price is far, thicker and brighter
    //                as price moves into the approach zone
    // While any line is blinking, sc.UpdateAlways keeps the study being
    // called at the chart update interval even if no ticks arrive.
    //------------------------------------------------------------------------
    int AnyBlinking = 0;

    if (DrawPattern && r_SetupID > 0)
    {
        LR_Level Levels[LR::MAX_PREV_LEVELS + 1];
        const int NumLevels = LR_CollectLevels(sc, r_SetupID, NumPrev,
                                               r_Target, r_Distance,
                                               r_ExtIndex, r_Dir, Levels);
        const float  Price = sc.Close[Last];
        const double Now   = LR_NowMs();

        for (int n = 0; n < NumLevels; ++n)
        {
            const LR_Level& Lv = Levels[n];
            if (Lv.ExtIndex < 0)
                continue;

            int&    BlinkLeft = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BlinkBase + Lv.Slot);
            double& BlinkTime = sc.GetPersistentDouble(LR::KEY_BASE + LR::PD_BlinkTimeBase + Lv.Slot);

            if (BlinkLeft > 0 && Now - BlinkTime >= BlinkMs)
            {
                --BlinkLeft;
                BlinkTime = Now;
            }
            if (BlinkLeft > 0)
                AnyBlinking = 1;

            const int Frozen = sc.GetPersistentInt(LR::KEY_BASE + LR::PI_FrozenEndBase + Lv.Slot);
            const int End    = (Frozen >= 0) ? Frozen : Last;

            const LR_LineState State = LR_StateOf(sc, Lv.Slot, 0);

            int SpanBegin, SpanEnd;
            LR_LineSpan(Lv, State, BlinkLeft > 0,
                        sc.GetPersistentInt(LR::KEY_BASE + LR::PI_TouchBarBase + Lv.Slot),
                        sc.GetPersistentInt(LR::KEY_BASE + LR::PI_BrokenBase + Lv.Slot),
                        End, CollapsePct, BrokenPct, SpanBegin, SpanEnd);

            LR_DrawTargetLevel(sc, Lv, SpanBegin, SpanEnd,
                               LR_LookFor(State, BlinkLeft,
                                          LR_Proximity(Price, Lv, Look.ZonePct), Look),
                               Look, ShowLabels);
        }
    }

    sc.UpdateAlways = AnyBlinking;

    //------------------------------------------------------------------------
    // Carry values across the forming bar so plots/spreadsheets stay continuous
    //------------------------------------------------------------------------
    if (Last > 0 && LastIsForming)
    {
        Sg_SwingHigh[Last] = 0.0f;
        Sg_SwingLow[Last]  = 0.0f;
        Sg_TargetHit[Last] = 0.0f;
        Sg_Invalid[Last]   = 0.0f;
        Sg_RevUp[Last]     = 0.0f;
        Sg_RevDown[Last]   = 0.0f;
        Sg_TouchBG[Last]   = 0.0f;

        if (r_Active)
        {
            Sg_TrendLine[Last] = LR::LineValueAt(r_A1Value, r_A1Index, r_Slope, Last);
            Sg_Target[Last]    = r_Target;
            Sg_State[Last]     = static_cast<float>(r_Dir);
        }
        else
        {
            Sg_TrendLine[Last] = 0.0f;
            Sg_Target[Last]    = 0.0f;
            Sg_State[Last]     = 0.0f;
        }
    }
}

// =============================================================================================
// =============================================================================================
//
//   VOLUME REVERSALS  -  SETTINGS
//
//   Fixed settings of the Reversal Volume Patterns module (converted from the TradingView
//   Pine Script v4 study of the same name). Change a value, then rebuild the DLL
//   (Analysis >> Build Custom Studies DLL) for it to take effect.
//
//   Still adjustable in Study Settings, under the REVERSAL VOLUME PATTERNS separator:
//     - Filter using RSI
//     - Arrow Offset (ticks)
//
//   Signals (arrow below bar = buy, arrow above bar = sell):
//     Pattern #1 - small red bar, larger red bar, small green bar
//     Pattern #2 - small red, larger red, even larger red, small green
//     Pattern #3 - 4 same-color bars, then a larger opposite-color bar
//     Pattern #4 - candlestick reversal (pivot + long-wick signal)
//   ("small" / "larger" refer to volume)
//
// =============================================================================================
// =============================================================================================
namespace VolumeReversals
{
    // ---- Which patterns produce arrows ----
    const bool SHOW_PATTERN_1 = true;    // small red, larger red, small green
    const bool SHOW_PATTERN_2 = true;    // small red, larger red, even larger red, small green
    const bool SHOW_PATTERN_3 = true;    // 4 same-color bars, then a larger opposite-color bar
    const bool SHOW_PATTERN_4 = true;    // candlestick reversal (pivot + long wick)

    // ---- RSI filter (only applied when "Filter using RSI" = Yes) ----
    const float RSI_OVERBOUGHT = 56.0f;  // sell arrows only when RSI >= this
    const float RSI_OVERSOLD   = 44.0f;  // buy arrows only when RSI <= this
    const int   RSI_LENGTH     = 14;

    // ---- Arrow coloring ----
    const bool COLOR_ARROWS_BY_PATTERN = false;  // true: #1 fuchsia, #2 purple, #3 blue, #4 yellow
}
// =============================================================================================
//   END VOLUME REVERSALS - SETTINGS
// =============================================================================================

// =============================================================================================
//  MODULE 3: REVERSAL VOLUME PATTERNS
//  Same logic as the stand-alone study, run as a manual loop over the updated bars.
// =============================================================================================
static void RG_VolumePatternsModule(SCStudyInterfaceRef sc)
{
    using namespace VolumeReversals;

    // ---- Plotted subgraphs ----
    SCSubgraphRef Subgraph_Up   = sc.Subgraph[RG_SG_VOLUME_PATTERN + 0];   // buy  (arrow below bar)
    SCSubgraphRef Subgraph_Down = sc.Subgraph[RG_SG_VOLUME_PATTERN + 1];   // sell (arrow above bar)
    SCSubgraphRef Subgraph_RSI  = sc.Subgraph[RG_SG_VOLUME_PATTERN + 2];   // internal

    // ---- Internal per-bar state (extra arrays) ----
    SCFloatArrayRef Array_HL      = Subgraph_Up.Arrays[0];     // High - Low
    SCFloatArrayRef Array_HLSMA   = Subgraph_Up.Arrays[1];     // SMA(High - Low, 50)
    SCFloatArrayRef Array_FinalUp = Subgraph_Up.Arrays[2];     // per-bar final up
    SCFloatArrayRef Array_FinalDn = Subgraph_Down.Arrays[0];   // per-bar final down

    // ---- Inputs ----
    SCInputRef Input_RSIFilter   = sc.Input[RG_IN_VOLUME + 0];
    SCInputRef Input_ArrowOffset = sc.Input[RG_IN_VOLUME + 1];

    if (sc.SetDefaults)
    {
        Subgraph_Up.Name         = "Vol Pattern: Buy Signal";
        Subgraph_Up.DrawStyle    = DRAWSTYLE_SQUARE;
        Subgraph_Up.PrimaryColor = RGB(0, 128, 255);   // blue (as in original)
        Subgraph_Up.LineWidth    = 2;
        Subgraph_Up.DrawZeros    = false;

        Subgraph_Down.Name         = "Vol Pattern: Sell Signal";
        Subgraph_Down.DrawStyle    = DRAWSTYLE_SQUARE;
        Subgraph_Down.PrimaryColor = RGB(0, 128, 255);
        Subgraph_Down.LineWidth    = 2;
        Subgraph_Down.DrawZeros    = false;

        Subgraph_RSI.Name      = "(internal) Vol Pattern RSI";
        Subgraph_RSI.DrawStyle = DRAWSTYLE_IGNORE;

        Input_RSIFilter.Name = "Filter using RSI";
        Input_RSIFilter.SetYesNo(1);

        Input_ArrowOffset.Name = "Arrow Offset (ticks)";
        Input_ArrowOffset.SetInt(4);
        Input_ArrowOffset.SetIntLimits(0, 10000);
        return;
    }

    for (int i = sc.UpdateStartIndex; i < sc.ArraySize; ++i)
    {
        // ------------------------------------------------------------------
        // Helper series computed on EVERY bar so their history stays valid.
        // ------------------------------------------------------------------
        sc.RSI(sc.Close, Subgraph_RSI, i, MOVAVGTYPE_WILDERS, RSI_LENGTH);

        Array_HL[i] = sc.High[i] - sc.Low[i];
        sc.SimpleMovAvg(Array_HL, Array_HLSMA, i, 50);

        // reset this bar's outputs / state
        Subgraph_Up[i]   = 0.0f;
        Subgraph_Down[i] = 0.0f;
        Array_FinalUp[i] = 0.0f;
        Array_FinalDn[i] = 0.0f;

        if (i < 5)  // need up to 5 bars of look-back (pivot uses [i-5])
            continue;

        // ------------------------------------------------------------------
        // Candle color flags. green = close > open; anything else = red
        // (matches Pine, where a doji counts as red).
        // ------------------------------------------------------------------
        bool is0Green = sc.Close[i]   > sc.Open[i];
        bool is1Green = sc.Close[i-1] > sc.Open[i-1];
        bool is2Green = sc.Close[i-2] > sc.Open[i-2];
        bool is3Green = sc.Close[i-3] > sc.Open[i-3];
        bool is4Green = sc.Close[i-4] > sc.Open[i-4];
        bool is0Red = !is0Green, is1Red = !is1Green, is2Red = !is2Green,
             is3Red = !is3Green, is4Red = !is4Green;

        float V0 = sc.Volume[i],   V1 = sc.Volume[i-1], V2 = sc.Volume[i-2],
              V3 = sc.Volume[i-3], V4 = sc.Volume[i-4];

        // Pattern #1
        bool up1   = (V1 > V2 && V0 < V1 && is0Green && is1Red && is2Red);
        bool down1 = (V1 > V2 && V0 < V1 && is0Red && is1Green && is2Green);

        // Pattern #2
        bool up2   = (V2 > V3 && V3 > V4 && V1 < V2 && V0 < V2 &&
                      is0Green && is1Red && is2Red && is3Red && is4Red);
        bool down2 = (V2 > V3 && V3 > V4 && V1 < V2 && V0 < V2 &&
                      is0Red && is1Green && is2Green && is3Green && is4Green);

        // Pattern #3
        bool up3   = (is1Red && is2Red && is3Red && is4Red && is0Green &&
                      V0 > V1 && V0 > V2 && V0 > V3 && V0 > V4);
        bool down3 = (is1Green && is2Green && is3Green && is4Green && is0Red &&
                      V0 > V1 && V0 > V2 && V0 > V3 && V0 > V4);

        // ------------------------------------------------------------------
        // Pattern #4 - candlestick reversal (from LonesomeTheDove snippet)
        // ------------------------------------------------------------------
        const float wick_multiplier = 10.0f;
        const float body_percentage = 1.0f;

        float O = sc.Open[i], C = sc.Close[i], H = sc.High[i], L = sc.Low[i];
        float HLSMA = Array_HLSMA[i];
        bool  hlsmaValid = (i >= 49);   // Pine sma(...,50) is na (=> false) until 50 bars

        bool Wlongsignal =
            ((C > O)  && (O - L) >= ((C - O) * wick_multiplier) && (H - C) <= ((H - L) * body_percentage)) ||
            ((C < O)  && (C - L) >= ((O - C) * wick_multiplier) && (H - C) <= ((H - L) * body_percentage)) ||
            ((C == O && C != H) && (H - L) >= ((H - C) * wick_multiplier) && (H - C) <= ((H - L) * body_percentage)) ||
            ((O == H && C == H) && hlsmaValid && (H - L) >= HLSMA);

        bool Wshortsignal =
            ((C < O)  && (H - O) >= ((O - C) * wick_multiplier) && (C - L) <= ((H - L) * body_percentage)) ||
            ((C > O)  && (H - C) >= ((C - O) * wick_multiplier) && (C - L) <= ((H - L) * body_percentage)) ||
            ((C == O && C != L) && (H - L) >= ((C - L) * wick_multiplier) && (C - L) <= ((H - L) * body_percentage)) ||
            ((O == L && C == L) && hlsmaValid && (H - L) >= HLSMA);

        // pivothigh/pivotlow with 5 left bars, 0 right bars (confirmed on current bar).
        // Strict '>' / '<'; switch to >= / <= if you want ties to still register.
        bool pivotHigh = (H > sc.High[i-1] && H > sc.High[i-2] && H > sc.High[i-3] &&
                          H > sc.High[i-4] && H > sc.High[i-5]);
        bool pivotLow  = (L < sc.Low[i-1]  && L < sc.Low[i-2]  && L < sc.Low[i-3]  &&
                          L < sc.Low[i-4]  && L < sc.Low[i-5]);

        bool up4   = pivotLow  && Wlongsignal;
        bool down4 = pivotHigh && Wshortsignal;

        // ---- "Show pattern" toggles ----
        if (!SHOW_PATTERN_1) { up1 = false; down1 = false; }
        if (!SHOW_PATTERN_2) { up2 = false; down2 = false; }
        if (!SHOW_PATTERN_3) { up3 = false; down3 = false; }
        if (!SHOW_PATTERN_4) { up4 = false; down4 = false; }

        bool up   = (up1 || up2 || up3 || up4);
        bool down = (down1 || down2 || down3 || down4);

        // ---- Eliminate consecutive duplicates (uses previous bar's FINAL value) ----
        if (Array_FinalUp[i-1] != 0.0f) up   = false;
        if (Array_FinalDn[i-1] != 0.0f) down = false;

        // ---- RSI directional filter ----
        if (Input_RSIFilter.GetYesNo())
        {
            float rsiVal = Subgraph_RSI[i];
            if (rsiVal < RSI_OVERBOUGHT) down = false; // sell only when >= overbought
            if (rsiVal > RSI_OVERSOLD)   up   = false; // buy  only when <= oversold
        }

        // ---- Store the final (post-filter) signal state for this bar ----
        Array_FinalUp[i] = up   ? 1.0f : 0.0f;
        Array_FinalDn[i] = down ? 1.0f : 0.0f;

        // ---- Draw the arrows ----
        const float offset = Input_ArrowOffset.GetInt() * sc.TickSize;

        if (up)   Subgraph_Up[i]   = sc.Low[i]  - offset;
        if (down) Subgraph_Down[i] = sc.High[i] + offset;

        if (COLOR_ARROWS_BY_PATTERN)
        {
            if (up)
            {
                COLORREF c = RGB(0,128,255);
                if (up1) c = RGB(255,0,255);   // fuchsia
                if (up2) c = RGB(128,0,128);   // purple
                if (up3) c = RGB(0,0,255);     // blue
                if (up4) c = RGB(255,255,0);   // yellow
                Subgraph_Up.DataColor[i] = c;
            }
            if (down)
            {
                COLORREF c = RGB(0,128,255);
                if (down1) c = RGB(255,0,255);
                if (down2) c = RGB(128,0,128);
                if (down3) c = RGB(0,0,255);
                if (down4) c = RGB(255,255,0);
                Subgraph_Down.DataColor[i] = c;
            }
        }
    }
}

// =============================================================================================
//  REVERSAL GRAIL - the study
// =============================================================================================
SCSFExport scsf_TOMethod4(SCStudyInterfaceRef sc)
{
    RG_ExternalAlerts Ext;

    if (sc.SetDefaults)
    {
        // Section separators in Study Settings >> Inputs: one titled input ahead of each
        // module's inputs. They have no effect.
        const char* LINE = "------------------------------";
        sc.Input[RG_IN_SEP_GRAIL].Name = "=========  REVERSAL GRAIL  =========";
        sc.Input[RG_IN_SEP_GRAIL].SetCustomInputStrings(LINE);
        sc.Input[RG_IN_SEP_GRAIL].SetCustomInputIndex(0);
        sc.Input[RG_IN_SEP_RIZZY].Name = "=========  LITTLE RIZZY  =========";
        sc.Input[RG_IN_SEP_RIZZY].SetCustomInputStrings(LINE);
        sc.Input[RG_IN_SEP_RIZZY].SetCustomInputIndex(0);
        sc.Input[RG_IN_SEP_VOLUME].Name = "=========  REVERSAL VOLUME PATTERNS  =========";
        sc.Input[RG_IN_SEP_VOLUME].SetCustomInputStrings(LINE);
        sc.Input[RG_IN_SEP_VOLUME].SetCustomInputIndex(0);
    }

    // Little Rizzy runs first: on a full recalculation it clears all of this study's chart
    // drawings before the other modules draw theirs. The signal module runs last because it
    // sends the one combined alert for all modules.
    RG_LittleRizzyModule(sc, Ext);
    RG_VolumePatternsModule(sc);
    RG_SignalModule(sc, Ext);
}