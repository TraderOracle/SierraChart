// ============================================================
// SCOFA-v1203.cpp
// SCOF Absorption Detector [zbytedev]
// Version 1.2.03
//
// Detects three orderflow patterns from Volume at Price
// (footprint) data:
//   1. ABSORPTION  — aggressive flow absorbed at a bar extreme
//                    (unchanged detection logic from v1.1.05)
//   2. SWEEP       — liquidity-pool sweep / stop-run trap:
//                    confirmed swing pools (incl. relative equal
//                    highs/lows), penetration + reclaim gate that
//                    separates sweeps from liquidity runs
//   3. SLINGSHOT   — sweep-trap sequence: Sweep -> displacement
//                    candle leaving a Fair Value Gap -> retrace tap
//                    of the FVG, optionally inside configured
//                    time windows
//
// Plus: statistical edge reporting (MFE/MAE, win rate) to the
// Message Log, per pattern and direction.
//
// Specification: docs/scofdoc-08-v1200-specification.md
// Architected for Sierra Chart Remote Build (ACSIL).
// Apply to a footprint/Numbers Bars chart with VAP data enabled.
// ============================================================

#include "sierrachart.h"

// ============================================================
// VERSION — single source of truth (Fix 5).
// The digits below must not appear in any other definition.
// ============================================================

#define SCOFA_VERSION_STR  "1.2.03"
#define SCOFA_VERSION_NUM  "1203"

SCDLLName("SCOFA-v" SCOFA_VERSION_NUM)

static const int   STUDY_VERSION      = 1203;   // must equal SCOFA_VERSION_NUM

// ============================================================
// CONSTANTS
// ============================================================

static const int   MAX_ZONES          = 500;    // max simultaneous extending rectangles
static const int   MAX_POOLS_PER_SIDE = 64;     // liquidity pools kept per side
static const float STRENGTH_MIN       = 0.0f;
static const float STRENGTH_MAX       = 100.0f;

// Fix 3: tolerance for float ratio/threshold comparisons.
// Convention:  value + FP_EPSILON >= threshold   for ">="
//              value < threshold - FP_EPSILON    for strict "<"
static const float FP_EPSILON         = 1e-4f;

// Persistent storage (Fix 1: exactly two slots, forever)
static const int PERSIST_STATE_PTR    = 0;      // sc.GetPersistentPointer -> StudyState*
static const int PERSIST_HIDE_STATE   = 1;      // previous sc.HideStudy value

// Fix 1: deterministic drawing line numbers — a pure function of
// (bar, pattern, direction, element). Nothing is stored per bar.
// 3 patterns x 3 elements x 2 directions -> max slot index
// 2*6 + 2*2 + 1 = 17, so 18 slots per bar are required.
static const int LINE_NUMBER_BASE     = 100000000; // clear of SC auto-assigned tool numbers
static const int LINE_SLOTS_PER_BAR   = 18;

// ============================================================
// ENUMS
// ============================================================

enum PatternType
{
    PATTERN_ABSORPTION = 0,
    PATTERN_SWEEP      = 1,
    PATTERN_SLINGSHOT  = 2
};

enum DrawElement
{
    ELEM_MARKER    = 0,
    ELEM_HIGHLIGHT = 1,
    ELEM_RECT      = 2
};

// Slingshot state machine stages
enum SlingState
{
    SLING_WAIT_DISPLACEMENT = 0,
    SLING_WAIT_TAP          = 1
};

// ============================================================
// DATA STRUCTURES
// ============================================================

// All 61 inputs, read once per scsf call by LoadSettings().
struct StudySettings
{
    // Absorption core + filters (In:0-11)
    int  minOppLevels;       int  priorCandlesReq;    bool requireExhaustion;
    bool requireDeltaDiv;    bool requireCandle;      int  minVolAtExtreme;
    int  newExtremeN;        bool enableTapering;     int  minGrowthPct;
    bool enableDiag;         int  diagRatioPct;       int  counterDeltaMag;   // abs(), Fix 6
    // Sweep (In:31-39)
    bool enableSweep;        int  swingStrength;      int  poolLookbackBars;
    int  equalLevelTolTicks; int  sweepMinPenTicks;   int  sweepCloseBackTicks;
    int  sweepBurstPct;      int  sweepMinZoneDelta;  int  sweepMinWickPct;
    // Slingshot (In:40-51)
    bool enableSling;        int  slingDispWindow;    int  slingTapWindow;
    int  slingMinBodyPct;    int  slingMinFvgTicks;   bool slingRequireFlip;
    int  slingFlipPct;       bool slingUseWindows;    int  slingWin1Hour;
    int  slingWin2Hour;      int  slingWin3Hour;      int  slingWinMinutes;
    // Edge reporting (In:52-58)
    bool edgeEnabled;        int  edgeHorizon;        int  edgeTargetTicks;
    int  edgeStopTicks;      int  edgeSummaryEveryN;  bool edgePerSignalLog;
    int  edgeMinStrength;
    // Visual & output (In:12-21, 59-60)
    int  markerOffsetTicks;  SCString markerTextAbs;  SCString markerTextSweep;
    SCString markerTextSling; int markerFontSize;     bool enableRects;
    int  maxExtBars;         int  zoneWidthTicks;     int  zoneTransparency;
    COLORREF bullZoneColor;  COLORREF bearZoneColor;  bool enableHighlight;
};

// Generic across patterns; metrics not relevant to a pattern stay zero.
struct DetectionResult
{
    bool  Detected;          PatternType Pattern;    bool  IsBullish;
    int   Strength;          float MarkerPrice;
    float ZoneTop;           float ZoneBottom;       bool  WantZone;
    // absorption metrics
    int   ConsecLevels;      float AvgGrowthPct;     float MaxDiagRatio;
    bool  ExhaustionPresent; bool  DeltaDivPresent;  int   PriorCandles;
    unsigned int VolAtExtreme;
    // sweep metrics
    int   PenetrationTicks;  float BurstRatioPct;    float WickPct;
    int   ZoneDelta;         int   CloseBackTicks;   int   PoolTouchCount;
    // slingshot metrics
    int   FvgTicks;          float BodyPct;          float DeltaFlipPct;
    int   TapDepthTicks;     int   SourceSweepStrength;
};

// Replaces v1105 AbsorptionZone — adds the pattern tag.
struct SignalZone
{
    int   SignalBarIndex;    float ZoneTop;          float ZoneBottom;
    int   RectLineNumber;    PatternType Pattern;    bool  IsBullish;
    bool  IsExtending;       int   ExtendedBars;
};

// A confirmed swing level holding resting stops (spec 5.2.1).
struct LiquidityPool
{
    float Level;             // min of cluster for low pools, max for high pools
    int   LastTouchBar;      // most recent swing bar in the cluster
    int   TouchCount;        // >= 2 -> relative equal highs/lows (stronger)
    bool  IsLowPool;         // true = sell-side liquidity below price
    bool  Consumed;          // swept or run-through
};

// Two-bar reclaim deferral (spec 5.2.2 condition 2): the
// penetration bar closed beyond the pool; the next bar decides
// sweep (reclaim) vs liquidity run (another close beyond).
struct PendingSweep
{
    bool  Active;
    float PoolLevel;         int   PoolTouchCount;
    int   PenBar;            float PenExtreme;       // Low (bull) / High (bear) of pen bar
    bool  CondsPassed;       // wick/burst/counter-delta verdict from the pen bar
    int   PenetrationTicks;  float BurstRatioPct;    float WickPct;
    int   ZoneDelta;
};

// Slingshot 3-stage state machine instance (spec 5.3).
struct SlingshotSetup
{
    bool  IsBullish;         int   State;            // SlingState
    int   SweepBar;          float SweepExtreme;     float PoolLevel;
    int   SweepStrength;
    int   DisplacementBar;   float FvgTop;           float FvgBottom;
    float DeltaFlipPct;      double SweepBarDelta;   // bar delta of the sweep bar
    int   ExpiryBar;         bool  Done;
};

// Edge reporting (spec 11).
struct TrackedSignal
{
    PatternType Pattern;     bool  IsBullish;        int   SignalBarIndex;
    float EntryPrice;        int   Strength;         float MFETicks;
    float MAETicks;
};

struct EdgeBucket
{
    int    Count;            int    Wins;            int    Losses;
    int    Expired;          double SumMFE;          double SumMAE;
    double SumStrength;      double SumBarsToResolve;
};

// THE single heap object behind persistent pointer 0.
struct StudyState
{
    std::deque<SignalZone>     Zones;
    std::deque<LiquidityPool>  Pools;        // both sides, tagged by IsLowPool
    std::deque<SlingshotSetup> Setups;
    std::deque<TrackedSignal>  OpenSignals;
    PendingSweep PendBull;
    PendingSweep PendBear;
    EdgeBucket   Edge[3][2];                 // [PatternType][0=bull, 1=bear]
    int   LastTrackedBarIndex;               // edge dedup guard (intrabar ticks)
    int   LastPoolUpdateBar;                 // pool-confirmation dedup guard
    int   ResolvedSinceSummary;
    bool  ZoneCapacityWarned;                // Fix 4
    bool  LegacyIn11Notified;                // Fix 6

    StudyState()
    {
        Reset();
    }

    void Reset()
    {
        Zones.clear(); Pools.clear(); Setups.clear(); OpenSignals.clear();
        memset(&PendBull, 0, sizeof(PendBull));
        memset(&PendBear, 0, sizeof(PendBear));
        memset(Edge, 0, sizeof(Edge));
        LastTrackedBarIndex  = -1;
        LastPoolUpdateBar    = -1;
        ResolvedSinceSummary = 0;
        ZoneCapacityWarned   = false;
        LegacyIn11Notified   = false;
    }
};

// ============================================================
// Fix 1: deterministic drawing line numbers.
// 3 patterns x 3 elements x 2 directions packed into 18
// slots per bar (max slot index 17). Overflow-safe to ~113M bars.
// Drawings are per-study-instance, so two instances on one
// chart cannot collide.
// ============================================================

inline int MakeLineNumber(int barIndex, PatternType pattern, bool isBullish, int element)
{
    return LINE_NUMBER_BASE + barIndex * LINE_SLOTS_PER_BAR
         + static_cast<int>(pattern) * 6 + element * 2 + (isBullish ? 0 : 1);
}

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

static void LoadSettings(SCStudyInterfaceRef sc, StudySettings& s, StudyState& state);

static DetectionResult EvaluateAbsorption(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish, const StudySettings& s);

static int CalculateAbsorptionStrength(
    int consecLevels, float avgGrowthPct, float maxDiagRatio,
    bool exhaustionPresent, bool deltaDivPresent, int priorCandles,
    int minOppLevels, int minGrowthPct, int diagRatioPct,
    unsigned int volAtExtreme, int minVolAtExtreme);

static bool CheckExhaustionAtExtreme(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    unsigned int& out_volAtExtreme);

static bool IsNewExtreme(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish, int lookbackN);

static int CountConsecutiveOppositeDelta(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    int counterDeltaMag, int maxLevels,
    float& out_avgGrowthPct, float& out_maxDiagRatio,
    bool enableTapering, int minGrowthPct,
    bool enableDiag, int diagRatioPct,
    bool& out_tapOk, bool& out_diagOk, int& out_growthCount);

static void UpdateLiquidityPools(
    SCStudyInterfaceRef sc, const StudySettings& s, StudyState& state);

static DetectionResult EvaluateSweep(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    const StudySettings& s, StudyState& state);

static bool EvaluateSweepRaidBar(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish, float poolLevel,
    const StudySettings& s,
    int& out_penTicks, float& out_burstPct, float& out_wickPct, int& out_zoneDelta);

static int CalculateSweepStrength(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    const DetectionResult& r, const StudySettings& s);

static void UpdateSlingshots(
    SCStudyInterfaceRef sc, const StudySettings& s, StudyState& state,
    const DetectionResult& sweepBull, const DetectionResult& sweepBear,
    DetectionResult& out_slingBull, DetectionResult& out_slingBear);

static bool IsWithinSlingshotWindow(
    SCStudyInterfaceRef sc, int barIndex, const StudySettings& s);

static int CalculateSlingshotStrength(
    const DetectionResult& r, const StudySettings& s, bool tapInWindow);

static void OutputSignal(
    SCStudyInterfaceRef sc, const DetectionResult& r,
    const StudySettings& s, StudyState& state);

static void UpdateEdgeTracking(
    SCStudyInterfaceRef sc, const StudySettings& s, StudyState& state);

static void LogSignalOutcome(
    SCStudyInterfaceRef sc, const TrackedSignal& sig, int resolveBar,
    const char* outcome);

static void LogEdgeSummary(
    SCStudyInterfaceRef sc, StudyState& state, const StudySettings& s,
    const char* reason);

static bool IsWithinTimeFilter(
    SCStudyInterfaceRef sc, int barIndex,
    int startH, int startM, int endH, int endM);

static void DrawMarkerText(
    SCStudyInterfaceRef sc, int barIndex, float price,
    COLORREF color, const SCString& text, int fontSize, int lineNumber);

static void CreateOrUpdateZoneRectangle(
    SCStudyInterfaceRef sc, SignalZone& zone,
    COLORREF fillColor, int transparency, int currentIndex);

static void UpdateExtendingRectangles(
    SCStudyInterfaceRef sc, std::deque<SignalZone>& zones,
    int currentIndex, int maxExtension,
    COLORREF bullFill, COLORREF bearFill, int transparency);

static void HighlightBar(
    SCStudyInterfaceRef sc, int barIndex, COLORREF color,
    int transparency, int lineNumber);

static const char* PatternName(PatternType p);
static const char* PatternCode(PatternType p);

// ============================================================
// MAIN STUDY FUNCTION
// ============================================================

SCSFExport scsf_SCOFAbsorptionDetector(SCStudyInterfaceRef sc)
{
    // ----------------------------------------------------------
    // SUBGRAPH REFERENCES (spec section 9)
    // ----------------------------------------------------------
    SCSubgraphRef SG_Bullish       = sc.Subgraph[0];
    SCSubgraphRef SG_Bearish       = sc.Subgraph[1];
    SCSubgraphRef SG_Strength      = sc.Subgraph[2];   // absorption only (downstream compat)
    SCSubgraphRef SG_BullSweep     = sc.Subgraph[3];
    SCSubgraphRef SG_BearSweep     = sc.Subgraph[4];
    SCSubgraphRef SG_BullSling     = sc.Subgraph[5];
    SCSubgraphRef SG_BearSling     = sc.Subgraph[6];
    SCSubgraphRef SG_SweepStrength = sc.Subgraph[7];
    SCSubgraphRef SG_SlingStrength = sc.Subgraph[8];

    // ----------------------------------------------------------
    // INPUT REFERENCES
    // ----------------------------------------------------------
    SCInputRef In_MinOppLevels       = sc.Input[19];
    SCInputRef In_PriorCandles       = sc.Input[20];
    SCInputRef In_RequireExhaustion  = sc.Input[2];
    SCInputRef In_RequireDeltaDiv    = sc.Input[3];
    SCInputRef In_RequireCandle      = sc.Input[4];
    SCInputRef In_MinVolAtExtreme    = sc.Input[5];
    SCInputRef In_NewExtremeN        = sc.Input[6];
    SCInputRef In_EnableTapering     = sc.Input[7];
    SCInputRef In_MinGrowthPct       = sc.Input[8];
    SCInputRef In_EnableDiag         = sc.Input[9];
    SCInputRef In_DiagRatioPct       = sc.Input[10];
    SCInputRef In_CounterDeltaMag    = sc.Input[11];   // renamed, Fix 6
    SCInputRef In_MarkerOffsetTicks  = sc.Input[12];
    SCInputRef In_MarkerChar         = sc.Input[13];
    SCInputRef In_MarkerFontSize     = sc.Input[14];
    SCInputRef In_EnableRects        = sc.Input[15];
    SCInputRef In_MaxExtBars         = sc.Input[16];
    SCInputRef In_ZoneWidthTicks     = sc.Input[17];
    SCInputRef In_ZoneTransparency   = sc.Input[18];
    SCInputRef In_BullishZoneColor   = sc.Input[0];
    SCInputRef In_BearishZoneColor   = sc.Input[1];
    SCInputRef In_EnableBarHighlight = sc.Input[21];
    SCInputRef In_AlertBullish       = sc.Input[22];
    SCInputRef In_AlertBearish       = sc.Input[23];
    SCInputRef In_EnableTimeFilter   = sc.Input[24];
    SCInputRef In_StartHour          = sc.Input[25];
    SCInputRef In_StartMinute        = sc.Input[26];
    SCInputRef In_EndHour            = sc.Input[27];
    SCInputRef In_EndMinute          = sc.Input[28];
    SCInputRef In_Lookback           = sc.Input[29];
    SCInputRef In_BarCloseOnly       = sc.Input[30];
    // New: Sweep (In:31-39)
    SCInputRef In_EnableSweep        = sc.Input[31];
    SCInputRef In_SwingStrength      = sc.Input[32];
    SCInputRef In_PoolLookback       = sc.Input[33];
    SCInputRef In_EqualLevelTol      = sc.Input[34];
    SCInputRef In_SweepMinPen        = sc.Input[35];
    SCInputRef In_SweepCloseBack     = sc.Input[36];
    SCInputRef In_SweepBurstPct      = sc.Input[37];
    SCInputRef In_SweepMinZoneDelta  = sc.Input[38];
    SCInputRef In_SweepMinWickPct    = sc.Input[39];
    // New: Slingshot (In:40-51)
    SCInputRef In_EnableSling        = sc.Input[40];
    SCInputRef In_SlingDispWindow    = sc.Input[41];
    SCInputRef In_SlingTapWindow     = sc.Input[42];
    SCInputRef In_SlingMinBodyPct    = sc.Input[43];
    SCInputRef In_SlingMinFvgTicks   = sc.Input[44];
    SCInputRef In_SlingRequireFlip   = sc.Input[45];
    SCInputRef In_SlingFlipPct       = sc.Input[46];
    SCInputRef In_SlingUseWindows    = sc.Input[47];
    SCInputRef In_SlingWin1Hour      = sc.Input[48];
    SCInputRef In_SlingWin2Hour      = sc.Input[49];
    SCInputRef In_SlingWin3Hour      = sc.Input[50];
    SCInputRef In_SlingWinMinutes    = sc.Input[51];
    // New: Edge reporting (In:52-58)
    SCInputRef In_EdgeEnabled        = sc.Input[52];
    SCInputRef In_EdgeHorizon        = sc.Input[53];
    SCInputRef In_EdgeTarget         = sc.Input[54];
    SCInputRef In_EdgeStop           = sc.Input[55];
    SCInputRef In_EdgeSummaryEveryN  = sc.Input[56];
    SCInputRef In_EdgePerSignalLog   = sc.Input[57];
    SCInputRef In_EdgeMinStrength    = sc.Input[58];
    // New: pattern marker text (In:59-60)
    SCInputRef In_MarkerTextSweep    = sc.Input[59];
    SCInputRef In_MarkerTextSling    = sc.Input[60];

    // ----------------------------------------------------------
    // SET DEFAULTS
    // ----------------------------------------------------------
    if (sc.SetDefaults)
    {
        sc.GraphName        = "SCOF Absorption Detector [zbytedev]";
        sc.StudyDescription =
            "Detects orderflow Absorption, liquidity-pool Sweeps and "
            "Slingshot (sweep -> displacement FVG -> tap) patterns using "
            "Volume at Price footprint data. Absorption filters: Exhaustion "
            "at Extreme (Finished Business), Progressive Volume Tapering, "
            "Diagonal Imbalance, Consecutive Opposite-Delta Levels, Delta "
            "Divergence, Candle Direction. Includes per-pattern Signal "
            "Strength scoring, customizable markers, extending zone "
            "rectangles, bar highlighting and statistical edge reporting "
            "to the Message Log. "
            "Apply to a Numbers Bars / footprint chart with live or "
            "historical VAP. Recommended for NQ, ES, CL, 6E and other "
            "liquid futures. "
            "v" SCOFA_VERSION_STR " | zbytedev";

        sc.GraphRegion = 0;     // draw on the main price chart
        sc.AutoLoop    = 1;     // Sierra Chart handles bar iteration
        sc.MaintainVolumeAtPriceData         = 1;  // CRITICAL: per-bar VAP
        sc.MaintainAdditionalChartDataArrays = 1;  // BidVolume[] / AskVolume[]
        sc.DrawZeros   = 0;

        // ---- Subgraphs (spec section 9) ----
        SG_Bullish.Name          = "Bullish Absorption";
        SG_Bullish.DrawStyle     = DRAWSTYLE_POINT;
        SG_Bullish.PrimaryColor  = RGB(0, 220, 0);
        SG_Bullish.LineWidth     = 3;
        SG_Bullish.DrawZeros     = 0;

        SG_Bearish.Name          = "Bearish Absorption";
        SG_Bearish.DrawStyle     = DRAWSTYLE_POINT;
        SG_Bearish.PrimaryColor  = RGB(220, 0, 0);
        SG_Bearish.LineWidth     = 3;
        SG_Bearish.DrawZeros     = 0;

        SG_Strength.Name         = "Signal Strength (0-100)";
        SG_Strength.DrawStyle    = DRAWSTYLE_IGNORE;
        SG_Strength.PrimaryColor = RGB(128, 128, 128);
        SG_Strength.DrawZeros    = 0;

        SG_BullSweep.Name          = "Bullish Sweep";
        SG_BullSweep.DrawStyle     = DRAWSTYLE_POINT;
        SG_BullSweep.PrimaryColor  = RGB(0, 180, 255);
        SG_BullSweep.LineWidth     = 3;
        SG_BullSweep.DrawZeros     = 0;

        SG_BearSweep.Name          = "Bearish Sweep";
        SG_BearSweep.DrawStyle     = DRAWSTYLE_POINT;
        SG_BearSweep.PrimaryColor  = RGB(255, 140, 0);
        SG_BearSweep.LineWidth     = 3;
        SG_BearSweep.DrawZeros     = 0;

        SG_BullSling.Name          = "Bullish Slingshot";
        SG_BullSling.DrawStyle     = DRAWSTYLE_POINT;
        SG_BullSling.PrimaryColor  = RGB(128, 255, 0);
        SG_BullSling.LineWidth     = 3;
        SG_BullSling.DrawZeros     = 0;

        SG_BearSling.Name          = "Bearish Slingshot";
        SG_BearSling.DrawStyle     = DRAWSTYLE_POINT;
        SG_BearSling.PrimaryColor  = RGB(255, 0, 255);
        SG_BearSling.LineWidth     = 3;
        SG_BearSling.DrawZeros     = 0;

        SG_SweepStrength.Name         = "Sweep Strength (0-100)";
        SG_SweepStrength.DrawStyle    = DRAWSTYLE_IGNORE;
        SG_SweepStrength.PrimaryColor = RGB(128, 128, 128);
        SG_SweepStrength.DrawZeros    = 0;

        SG_SlingStrength.Name         = "Slingshot Strength (0-100)";
        SG_SlingStrength.DrawStyle    = DRAWSTYLE_IGNORE;
        SG_SlingStrength.PrimaryColor = RGB(128, 128, 128);
        SG_SlingStrength.DrawZeros    = 0;

        // ---- Group 1: Absorption Core ----
        In_MinOppLevels.Name = "Min Opposite Delta Levels at Extreme";
        In_MinOppLevels.SetInt(3);
        In_MinOppLevels.SetIntLimits(1, 20);

        In_PriorCandles.Name = "Min Prior Counter-Direction Candles (0=Off)";
        In_PriorCandles.SetInt(2);
        In_PriorCandles.SetIntLimits(0, 20);

        In_RequireExhaustion.Name = "Require Exhaustion at Extreme";
        In_RequireExhaustion.SetYesNo(1);

        In_RequireDeltaDiv.Name = "Require Delta Divergence";
        In_RequireDeltaDiv.SetYesNo(1);

        In_RequireCandle.Name = "Require Matching Candle Body";
        In_RequireCandle.SetYesNo(1);

        In_MinVolAtExtreme.Name = "Min Volume at Extreme Level (0=Off)";
        In_MinVolAtExtreme.SetInt(0);
        In_MinVolAtExtreme.SetIntLimits(0, 100000);

        In_NewExtremeN.Name = "New Extreme of Last N Bars (0=Off)";
        In_NewExtremeN.SetInt(0);
        In_NewExtremeN.SetIntLimits(0, 500);

        // ---- Group 2: Absorption Filters (In:7-11) ----
        In_EnableTapering.Name = "Enable Progressive Volume Filter";
        In_EnableTapering.SetYesNo(1);

        In_MinGrowthPct.Name = "Min Volume Growth Per Level (%)";
        In_MinGrowthPct.SetInt(8);
        In_MinGrowthPct.SetIntLimits(0, 500);

        In_EnableDiag.Name = "Enable Diagonal Imbalance Filter";
        In_EnableDiag.SetYesNo(1);

        In_DiagRatioPct.Name = "Diagonal Imbalance Ratio (%)";
        In_DiagRatioPct.SetInt(300);
        In_DiagRatioPct.SetIntLimits(100, 2000);

        // Fix 6: magnitude semantics. Legacy negative chartbook values
        // are abs()'d in LoadSettings with a one-time log notice.
        In_CounterDeltaMag.Name = "Min Counter-Delta Magnitude (0=Any)";
        In_CounterDeltaMag.SetInt(-1);
        In_CounterDeltaMag.SetIntLimits(-10000, 10000); // lower bound kept for legacy values

        // ---- Group 3: Visual — Markers & Zones ----
        In_MarkerOffsetTicks.Name = "Signal Marker Offset (Ticks)";
        In_MarkerOffsetTicks.SetInt(10);
        In_MarkerOffsetTicks.SetIntLimits(0, 50);

        In_MarkerChar.Name = "Marker Character (Absorption)";
        In_MarkerChar.SetString("A");

        In_MarkerFontSize.Name = "Marker Font Size";
        In_MarkerFontSize.SetInt(18);
        In_MarkerFontSize.SetIntLimits(6, 48);

        In_EnableRects.Name = "Enable Zone Rectangles";
        In_EnableRects.SetYesNo(1);

        In_MaxExtBars.Name = "Max Zone Extension Bars";
        In_MaxExtBars.SetInt(500);
        In_MaxExtBars.SetIntLimits(1, 5000);

        In_ZoneWidthTicks.Name = "Zone Width (Ticks from Extreme)";
        In_ZoneWidthTicks.SetInt(16);
        In_ZoneWidthTicks.SetIntLimits(1, 20);

        In_ZoneTransparency.Name = "Zone Transparency (%)";
        In_ZoneTransparency.SetInt(85);
        In_ZoneTransparency.SetIntLimits(0, 100);

        In_BullishZoneColor.Name = "Bullish Zone Color";
        In_BullishZoneColor.SetColor(RGB(128, 255, 128));

        In_BearishZoneColor.Name = "Bearish Zone Color";
        In_BearishZoneColor.SetColor(RGB(255, 128, 128));

        In_EnableBarHighlight.Name = "Enable Bar Highlight on Signal";
        In_EnableBarHighlight.SetYesNo(1);

        // ---- Group 4: Alerts (In:22-23) — shared by all patterns ----
        In_AlertBullish.Name = "Bullish Signal Alert Sound";
        In_AlertBullish.SetAlertSoundNumber(0);

        In_AlertBearish.Name = "Bearish Signal Alert Sound";
        In_AlertBearish.SetAlertSoundNumber(0);

        // ---- Group 5: Time & Performance (In:24-30) ----
        In_EnableTimeFilter.Name = "Enable Session Time Filter";
        In_EnableTimeFilter.SetYesNo(0);

        In_StartHour.Name = "Session Start Hour (0-23)";
        In_StartHour.SetInt(9);
        In_StartHour.SetIntLimits(0, 23);

        In_StartMinute.Name = "Session Start Minute (0-59)";
        In_StartMinute.SetInt(30);
        In_StartMinute.SetIntLimits(0, 59);

        In_EndHour.Name = "Session End Hour (0-23)";
        In_EndHour.SetInt(16);
        In_EndHour.SetIntLimits(0, 23);

        In_EndMinute.Name = "Session End Minute (0-59)";
        In_EndMinute.SetInt(0);
        In_EndMinute.SetIntLimits(0, 59);

        In_Lookback.Name = "Historical Lookback Bars (0=All)";
        In_Lookback.SetInt(750);
        In_Lookback.SetIntLimits(0, 50000);

        In_BarCloseOnly.Name = "Detect Only on Bar Close";
        In_BarCloseOnly.SetYesNo(1);

        // ---- Group 6: Sweep Detection (In:31-39) ----
        In_EnableSweep.Name = "Enable Sweep Detection";
        In_EnableSweep.SetYesNo(1);

        In_SwingStrength.Name = "Sweep: Swing Strength (Bars Each Side)";
        In_SwingStrength.SetInt(3);
        In_SwingStrength.SetIntLimits(1, 10);

        In_PoolLookback.Name = "Sweep: Pool Lookback (Bars)";
        In_PoolLookback.SetInt(100);
        In_PoolLookback.SetIntLimits(10, 2000);

        In_EqualLevelTol.Name = "Sweep: Equal-Level Tolerance (Ticks)";
        In_EqualLevelTol.SetInt(2);
        In_EqualLevelTol.SetIntLimits(0, 20);

        In_SweepMinPen.Name = "Sweep: Min Penetration Beyond Pool (Ticks)";
        In_SweepMinPen.SetInt(2);
        In_SweepMinPen.SetIntLimits(1, 50);

        In_SweepCloseBack.Name = "Sweep: Min Close-Back Inside (Ticks)";
        In_SweepCloseBack.SetInt(1);
        In_SweepCloseBack.SetIntLimits(0, 50);

        In_SweepBurstPct.Name = "Sweep: Volume Burst Ratio (%)";
        In_SweepBurstPct.SetInt(200);
        In_SweepBurstPct.SetIntLimits(100, 2000);

        In_SweepMinZoneDelta.Name = "Sweep: Min Counter-Delta in Sweep Zone (0=Off)";
        In_SweepMinZoneDelta.SetInt(0);
        In_SweepMinZoneDelta.SetIntLimits(0, 100000);

        In_SweepMinWickPct.Name = "Sweep: Min Rejection Wick (% of Bar Range)";
        In_SweepMinWickPct.SetInt(30);
        In_SweepMinWickPct.SetIntLimits(0, 90);

        // ---- Group 7: Slingshot Detection (In:40-51) ----
        In_EnableSling.Name = "Enable Slingshot Detection";
        In_EnableSling.SetYesNo(1);

        In_SlingDispWindow.Name = "Slingshot: Displacement Window After Sweep (Bars)";
        In_SlingDispWindow.SetInt(5);
        In_SlingDispWindow.SetIntLimits(1, 50);

        In_SlingTapWindow.Name = "Slingshot: Tap Window After FVG (Bars)";
        In_SlingTapWindow.SetInt(10);
        In_SlingTapWindow.SetIntLimits(1, 100);

        In_SlingMinBodyPct.Name = "Slingshot: Min Displacement Body (% of Range)";
        In_SlingMinBodyPct.SetInt(60);
        In_SlingMinBodyPct.SetIntLimits(10, 100);

        In_SlingMinFvgTicks.Name = "Slingshot: Min FVG Size (Ticks)";
        In_SlingMinFvgTicks.SetInt(1);
        In_SlingMinFvgTicks.SetIntLimits(0, 50);

        In_SlingRequireFlip.Name = "Slingshot: Require Delta Flip on Displacement";
        In_SlingRequireFlip.SetYesNo(1);

        In_SlingFlipPct.Name = "Slingshot: Min Delta Flip Ratio (%)";
        In_SlingFlipPct.SetInt(150);
        In_SlingFlipPct.SetIntLimits(100, 2000);

        // Window hours are in the CHART's timezone; defaults assume
        // US Eastern defaults: windows 03-04 / 10-11 / 14-15.
        In_SlingUseWindows.Name = "Slingshot: Restrict to Time Windows";
        In_SlingUseWindows.SetYesNo(0);

        In_SlingWin1Hour.Name = "Slingshot: Window 1 Start Hour";
        In_SlingWin1Hour.SetInt(3);
        In_SlingWin1Hour.SetIntLimits(0, 23);

        In_SlingWin2Hour.Name = "Slingshot: Window 2 Start Hour";
        In_SlingWin2Hour.SetInt(10);
        In_SlingWin2Hour.SetIntLimits(0, 23);

        In_SlingWin3Hour.Name = "Slingshot: Window 3 Start Hour";
        In_SlingWin3Hour.SetInt(14);
        In_SlingWin3Hour.SetIntLimits(0, 23);

        In_SlingWinMinutes.Name = "Slingshot: Window Duration (Minutes)";
        In_SlingWinMinutes.SetInt(60);
        In_SlingWinMinutes.SetIntLimits(15, 240);

        // ---- Group 8: Edge Reporting (In:52-58) ----
        In_EdgeEnabled.Name = "Enable Edge Reporting (Message Log)";
        In_EdgeEnabled.SetYesNo(0);

        In_EdgeHorizon.Name = "Edge: Evaluation Horizon (Bars)";
        In_EdgeHorizon.SetInt(20);
        In_EdgeHorizon.SetIntLimits(1, 500);

        In_EdgeTarget.Name = "Edge: Target (Ticks)";
        In_EdgeTarget.SetInt(8);
        In_EdgeTarget.SetIntLimits(1, 1000);

        In_EdgeStop.Name = "Edge: Stop (Ticks)";
        In_EdgeStop.SetInt(8);
        In_EdgeStop.SetIntLimits(1, 1000);

        In_EdgeSummaryEveryN.Name = "Edge: Summary Every N Resolved Signals (0=End Only)";
        In_EdgeSummaryEveryN.SetInt(20);
        In_EdgeSummaryEveryN.SetIntLimits(0, 1000);

        In_EdgePerSignalLog.Name = "Edge: Log Each Signal Outcome";
        In_EdgePerSignalLog.SetYesNo(0);

        In_EdgeMinStrength.Name = "Edge: Min Strength To Track (0=All)";
        In_EdgeMinStrength.SetInt(0);
        In_EdgeMinStrength.SetIntLimits(0, 100);

        // ---- Group 9: New Pattern Visuals (In:59-60) ----
        In_MarkerTextSweep.Name = "Marker Text (Sweep)";
        In_MarkerTextSweep.SetString("SW");

        In_MarkerTextSling.Name = "Marker Text (Slingshot)";
        In_MarkerTextSling.SetString("SS");

        return;
    }

    // ----------------------------------------------------------
    // CLEANUP ON STUDY REMOVAL
    // ----------------------------------------------------------
    if (sc.LastCallToFunction)
    {
        StudyState* p_State =
            static_cast<StudyState*>(sc.GetPersistentPointer(PERSIST_STATE_PTR));
        if (p_State != nullptr)
        {
            // Final edge summary, if reporting was on and anything resolved
            if (In_EdgeEnabled.GetYesNo())
            {
                StudySettings s;
                LoadSettings(sc, s, *p_State);
                LogEdgeSummary(sc, *p_State, s, "final");
            }
            delete p_State;
            sc.SetPersistentPointer(PERSIST_STATE_PTR, nullptr);
        }
        return;
    }

    // ----------------------------------------------------------
    // SAFETY GUARDS
    // ----------------------------------------------------------
    if (sc.VolumeAtPriceForBars == nullptr)
        return;
    if (sc.ArraySize < 2 || sc.Index < 1)
        return;

    // ----------------------------------------------------------
    // STATE: allocate on first call (persistent pointer 0)
    // ----------------------------------------------------------
    StudyState* p_State =
        static_cast<StudyState*>(sc.GetPersistentPointer(PERSIST_STATE_PTR));
    if (p_State == nullptr)
    {
        p_State = new StudyState();
        sc.SetPersistentPointer(PERSIST_STATE_PTR, p_State);
    }
    StudyState& state = *p_State;

    // ----------------------------------------------------------
    // HIDE STATE MANAGEMENT
    // ----------------------------------------------------------
    int& r_PrevHideState = sc.GetPersistentInt(PERSIST_HIDE_STATE);
    if (sc.HideStudy != 0 && r_PrevHideState == 0)
        state.Reset();          // study just became hidden — drop tracking state
    r_PrevHideState = sc.HideStudy;

    if (sc.HideStudy != 0)
        return;

    // ----------------------------------------------------------
    // FULL RECALCULATION RESET — once, at bar 0 only.
    // Sierra Chart auto-deletes ACS drawings on full recalc;
    // we only reset our internal tracking.
    // ----------------------------------------------------------
    if (sc.IsFullRecalculation && sc.Index == 0)
        state.Reset();

    // ----------------------------------------------------------
    // LOOKBACK GUARD
    // ----------------------------------------------------------
    const int lookback = In_Lookback.GetInt();
    if (lookback > 0 && (sc.ArraySize - sc.Index) > lookback)
        return;

    // ----------------------------------------------------------
    // SETTINGS — read all inputs once (extensibility refactor)
    // ----------------------------------------------------------
    StudySettings s;
    LoadSettings(sc, s, state);

    // ----------------------------------------------------------
    // TIME FILTER — outside the session window we still extend
    // active zone rectangles (v1105 behavior preserved)
    // ----------------------------------------------------------
    if (In_EnableTimeFilter.GetYesNo())
    {
        if (!IsWithinTimeFilter(sc, sc.Index,
            In_StartHour.GetInt(), In_StartMinute.GetInt(),
            In_EndHour.GetInt(),   In_EndMinute.GetInt()))
        {
            if (s.enableRects && !state.Zones.empty())
                UpdateExtendingRectangles(sc, state.Zones, sc.Index,
                    s.maxExtBars, s.bullZoneColor, s.bearZoneColor,
                    s.zoneTransparency);
            return;
        }
    }

    // ----------------------------------------------------------
    // BAR CLOSE ONLY — mid-bar ticks only extend rectangles
    // ----------------------------------------------------------
    if (In_BarCloseOnly.GetYesNo())
    {
        if (sc.GetBarHasClosedStatus(sc.Index) != BHCS_BAR_HAS_CLOSED)
        {
            if (s.enableRects && !state.Zones.empty())
                UpdateExtendingRectangles(sc, state.Zones, sc.Index,
                    s.maxExtBars, s.bullZoneColor, s.bearZoneColor,
                    s.zoneTransparency);
            return;
        }
    }

    // ----------------------------------------------------------
    // EDGE TRACKING — must run BEFORE new detections so open
    // signals resolve against the just-closed bar first
    // ----------------------------------------------------------
    if (s.edgeEnabled)
        UpdateEdgeTracking(sc, s, state);

    // One backfill summary at the final bar of a full recalculation
    if (s.edgeEnabled && sc.IsFullRecalculation && sc.Index == sc.ArraySize - 1)
        LogEdgeSummary(sc, state, s, "historical backfill");

    // ----------------------------------------------------------
    // LIQUIDITY POOLS — confirm swings at Index - swingStrength
    // ----------------------------------------------------------
    if (s.enableSweep)
        UpdateLiquidityPools(sc, s, state);

    // ----------------------------------------------------------
    // DETECTION
    // ----------------------------------------------------------
    DetectionResult bullAbs = EvaluateAbsorption(sc, sc.Index, true,  s);
    DetectionResult bearAbs = EvaluateAbsorption(sc, sc.Index, false, s);

    DetectionResult bullSweep; memset(&bullSweep, 0, sizeof(bullSweep));
    DetectionResult bearSweep; memset(&bearSweep, 0, sizeof(bearSweep));
    if (s.enableSweep)
    {
        bullSweep = EvaluateSweep(sc, sc.Index, true,  s, state);
        bearSweep = EvaluateSweep(sc, sc.Index, false, s, state);
    }

    DetectionResult bullSling; memset(&bullSling, 0, sizeof(bullSling));
    DetectionResult bearSling; memset(&bearSling, 0, sizeof(bearSling));
    if (s.enableSling)
        UpdateSlingshots(sc, s, state, bullSweep, bearSweep, bullSling, bearSling);

    // ----------------------------------------------------------
    // OUTPUT — single path for every pattern. Bullish first so
    // the bull-precedence rule on shared strength subgraphs holds.
    // ----------------------------------------------------------
    if (bullAbs.Detected)   OutputSignal(sc, bullAbs,   s, state);
    if (bearAbs.Detected)   OutputSignal(sc, bearAbs,   s, state);
    if (bullSweep.Detected) OutputSignal(sc, bullSweep, s, state);
    if (bearSweep.Detected) OutputSignal(sc, bearSweep, s, state);
    if (bullSling.Detected) OutputSignal(sc, bullSling, s, state);
    if (bearSling.Detected) OutputSignal(sc, bearSling, s, state);

    // ----------------------------------------------------------
    // RECTANGLE EXTENSION MANAGEMENT (every processed bar)
    // ----------------------------------------------------------
    if (s.enableRects && !state.Zones.empty())
        UpdateExtendingRectangles(sc, state.Zones, sc.Index,
            s.maxExtBars, s.bullZoneColor, s.bearZoneColor,
            s.zoneTransparency);
}

// ============================================================
// HELPER: LoadSettings
// Single point reading all 61 inputs (extensibility refactor).
// Also applies the Fix 6 legacy-value migration for In:11.
// ============================================================

static void LoadSettings(SCStudyInterfaceRef sc, StudySettings& s, StudyState& state)
{
    // Absorption
    s.minOppLevels      = sc.Input[19].GetInt();
    s.priorCandlesReq   = sc.Input[20].GetInt();
    s.requireExhaustion = (sc.Input[2].GetYesNo() != 0);
    s.requireDeltaDiv   = (sc.Input[3].GetYesNo() != 0);
    s.requireCandle     = (sc.Input[4].GetYesNo() != 0);
    s.minVolAtExtreme   = sc.Input[5].GetInt();
    s.newExtremeN       = sc.Input[6].GetInt();
    s.enableTapering    = (sc.Input[7].GetYesNo() != 0);
    s.minGrowthPct      = sc.Input[8].GetInt();
    s.enableDiag        = (sc.Input[9].GetYesNo() != 0);
    s.diagRatioPct      = sc.Input[10].GetInt();

    // Fix 6: magnitude semantics with legacy negative-value migration.
    {
        int v = sc.Input[11].GetInt();
        if (v < 0 && !state.LegacyIn11Notified)
        {
            SCString msg;
            msg.Format("SCOFA v" SCOFA_VERSION_STR ": Input 'Min Counter-Delta "
                "Magnitude' had legacy negative value %d; interpreted as %d.", v, -v);
            sc.AddMessageToLog(msg, 0);
            state.LegacyIn11Notified = true;
        }
        s.counterDeltaMag = (v < 0) ? -v : v;
    }

    // Sweep (In:31-39)
    s.enableSweep        = (sc.Input[31].GetYesNo() != 0);
    s.swingStrength      = sc.Input[32].GetInt();
    s.poolLookbackBars   = sc.Input[33].GetInt();
    s.equalLevelTolTicks = sc.Input[34].GetInt();
    s.sweepMinPenTicks   = sc.Input[35].GetInt();
    s.sweepCloseBackTicks = sc.Input[36].GetInt();
    s.sweepBurstPct      = sc.Input[37].GetInt();
    s.sweepMinZoneDelta  = sc.Input[38].GetInt();
    s.sweepMinWickPct    = sc.Input[39].GetInt();

    // Slingshot (In:40-51)
    s.enableSling        = (sc.Input[40].GetYesNo() != 0) && s.enableSweep; // sweep-only fuel
    s.slingDispWindow    = sc.Input[41].GetInt();
    s.slingTapWindow     = sc.Input[42].GetInt();
    s.slingMinBodyPct    = sc.Input[43].GetInt();
    s.slingMinFvgTicks   = sc.Input[44].GetInt();
    s.slingRequireFlip   = (sc.Input[45].GetYesNo() != 0);
    s.slingFlipPct       = sc.Input[46].GetInt();
    s.slingUseWindows    = (sc.Input[47].GetYesNo() != 0);
    s.slingWin1Hour      = sc.Input[48].GetInt();
    s.slingWin2Hour      = sc.Input[49].GetInt();
    s.slingWin3Hour      = sc.Input[50].GetInt();
    s.slingWinMinutes    = sc.Input[51].GetInt();

    // Edge reporting (In:52-58)
    s.edgeEnabled        = (sc.Input[52].GetYesNo() != 0);
    s.edgeHorizon        = sc.Input[53].GetInt();
    s.edgeTargetTicks    = sc.Input[54].GetInt();
    s.edgeStopTicks      = sc.Input[55].GetInt();
    s.edgeSummaryEveryN  = sc.Input[56].GetInt();
    s.edgePerSignalLog   = (sc.Input[57].GetYesNo() != 0);
    s.edgeMinStrength    = sc.Input[58].GetInt();

    // Visual & output
    s.markerOffsetTicks  = sc.Input[12].GetInt();

    s.markerTextAbs = sc.Input[13].GetString();
    if (s.markerTextAbs.GetLength() == 0)      s.markerTextAbs = "A";
    else if (s.markerTextAbs.GetLength() > 4)  s.markerTextAbs = s.markerTextAbs.Left(4);

    s.markerTextSweep = sc.Input[59].GetString();
    if (s.markerTextSweep.GetLength() == 0)     s.markerTextSweep = "SW";
    else if (s.markerTextSweep.GetLength() > 4) s.markerTextSweep = s.markerTextSweep.Left(4);

    s.markerTextSling = sc.Input[60].GetString();
    if (s.markerTextSling.GetLength() == 0)     s.markerTextSling = "SS";
    else if (s.markerTextSling.GetLength() > 4) s.markerTextSling = s.markerTextSling.Left(4);

    s.markerFontSize   = sc.Input[14].GetInt();
    s.enableRects      = (sc.Input[15].GetYesNo() != 0);
    s.maxExtBars       = sc.Input[16].GetInt();
    s.zoneWidthTicks   = sc.Input[17].GetInt();
    s.zoneTransparency = sc.Input[18].GetInt();
    s.bullZoneColor    = sc.Input[0].GetColor();
    s.bearZoneColor    = sc.Input[1].GetColor();
    s.enableHighlight  = (sc.Input[21].GetYesNo() != 0);
}

// ============================================================
// HELPER: EvaluateAbsorption
// Merged bullish/bearish evaluator (the v1105 pair was verified
// line-for-line mirror-symmetric; one parameterized function
// halves the maintenance surface). Detection logic is unchanged
// from v1.1.05 except Fix 2 (tapering) and Fix 3 (epsilon).
// ============================================================

static DetectionResult EvaluateAbsorption(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish, const StudySettings& s)
{
    DetectionResult result;
    memset(&result, 0, sizeof(result));
    result.Pattern   = PATTERN_ABSORPTION;
    result.IsBullish = isBullish;

    const float open  = sc.Open[barIndex];
    const float close = sc.Close[barIndex];

    // 1. Matching candle body
    if (s.requireCandle)
    {
        if (isBullish  && close <= open) return result;
        if (!isBullish && close >= open) return result;
    }

    // 2. Delta divergence (bar totals; MaintainAdditionalChartDataArrays=1)
    float barDelta = static_cast<float>(sc.AskVolume[barIndex])
                   - static_cast<float>(sc.BidVolume[barIndex]);
    bool deltaDivPresent = isBullish ? (barDelta < 0.0f) : (barDelta > 0.0f);
    if (s.requireDeltaDiv && !deltaDivPresent)
        return result;

    // 3. New extreme filter
    if (!IsNewExtreme(sc, barIndex, isBullish, s.newExtremeN))
        return result;

    // 4. Prior counter-direction candles (consecutive, immediately prior)
    int priorCandlesFound = 0;
    if (s.priorCandlesReq > 0)
    {
        for (int i = 1; i <= s.priorCandlesReq && (barIndex - i) >= 0; ++i)
        {
            int idx = barIndex - i;
            bool counterDir = isBullish
                ? (sc.Close[idx] < sc.Open[idx])    // bearish priors for bullish signal
                : (sc.Close[idx] > sc.Open[idx]);   // bullish priors for bearish signal
            if (counterDir)
                priorCandlesFound++;
            else
                break;
        }
        if (priorCandlesFound < s.priorCandlesReq)
            return result;
    }

    // 5. Exhaustion at extreme (Finished Business)
    unsigned int volAtExtreme = 0;
    bool exhaustionPresent = CheckExhaustionAtExtreme(sc, barIndex, isBullish, volAtExtreme);
    if (s.requireExhaustion && !exhaustionPresent)
        return result;

    // 6. Minimum volume at extreme
    if (s.minVolAtExtreme > 0 && volAtExtreme < static_cast<unsigned int>(s.minVolAtExtreme))
        return result;

    // 7. Consecutive opposite-delta levels + tapering + diagonal
    float avgGrowthPct = 0.0f;
    float maxDiagRatio = 0.0f;
    bool  tapOk        = false;
    bool  diagOk       = false;
    int   growthCount  = 0;

    int consecLevels = CountConsecutiveOppositeDelta(
        sc, barIndex, isBullish,
        s.counterDeltaMag, s.minOppLevels + 10,   // scan a bit deeper than required
        avgGrowthPct, maxDiagRatio,
        s.enableTapering, s.minGrowthPct,
        s.enableDiag, s.diagRatioPct,
        tapOk, diagOk, growthCount);

    if (consecLevels < s.minOppLevels)
        return result;
    if (s.enableTapering && !tapOk)
        return result;
    if (s.enableDiag && !diagOk)
        return result;

    // All filters passed — signal detected
    result.Detected          = true;
    result.ConsecLevels      = consecLevels;
    result.AvgGrowthPct      = avgGrowthPct;
    result.MaxDiagRatio      = maxDiagRatio;
    result.ExhaustionPresent = exhaustionPresent;
    result.DeltaDivPresent   = deltaDivPresent;
    result.PriorCandles      = priorCandlesFound;
    result.VolAtExtreme      = volAtExtreme;
    result.WantZone          = true;

    // Marker at the extreme; zone = zoneWidthTicks from the extreme
    // (v1105 baseline behavior — narrow band, not the candle body).
    const float zoneW = static_cast<float>(s.zoneWidthTicks) * sc.TickSize;
    if (isBullish)
    {
        result.MarkerPrice = sc.Low[barIndex];
        result.ZoneBottom  = sc.Low[barIndex];
        result.ZoneTop     = sc.Low[barIndex] + zoneW;
    }
    else
    {
        result.MarkerPrice = sc.High[barIndex];
        result.ZoneTop     = sc.High[barIndex];
        result.ZoneBottom  = sc.High[barIndex] - zoneW;
    }

    result.Strength = CalculateAbsorptionStrength(
        consecLevels, avgGrowthPct, maxDiagRatio,
        exhaustionPresent, deltaDivPresent, priorCandlesFound,
        s.minOppLevels, s.minGrowthPct, s.diagRatioPct,
        volAtExtreme, s.minVolAtExtreme);

    return result;
}

// ============================================================
// HELPER: CheckExhaustionAtExtreme (unchanged from v1105)
// Bullish: AskVol == 0 at the lowest price level.
// Bearish: BidVol == 0 at the highest price level.
// ============================================================

static bool CheckExhaustionAtExtreme(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    unsigned int& out_volAtExtreme)
{
    out_volAtExtreme = 0;
    if (sc.VolumeAtPriceForBars == nullptr)
        return false;

    int numLevels = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(barIndex);
    if (numLevels == 0)
        return false;

    const s_VolumeAtPriceV2* p_VAP = nullptr;

    if (isBullish)
    {
        if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(barIndex, 0, &p_VAP))
            return false;
        out_volAtExtreme = p_VAP->BidVolume;
        return (p_VAP->AskVolume == 0 && p_VAP->BidVolume > 0);
    }
    else
    {
        if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(barIndex, numLevels - 1, &p_VAP))
            return false;
        out_volAtExtreme = p_VAP->AskVolume;
        return (p_VAP->BidVolume == 0 && p_VAP->AskVolume > 0);
    }
}

// ============================================================
// HELPER: IsNewExtreme (unchanged from v1105)
// ============================================================

static bool IsNewExtreme(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish, int lookbackN)
{
    if (lookbackN <= 0)
        return true; // disabled

    int startIdx = max(0, barIndex - lookbackN);

    if (isBullish)
    {
        float barLow = sc.Low[barIndex];
        for (int i = startIdx; i < barIndex; ++i)
            if (sc.Low[i] <= barLow)
                return false;
        return true;
    }
    else
    {
        float barHigh = sc.High[barIndex];
        for (int i = startIdx; i < barIndex; ++i)
            if (sc.High[i] >= barHigh)
                return false;
        return true;
    }
}

// ============================================================
// HELPER: CountConsecutiveOppositeDelta
// v1105 logic with:
//   Fix 2 — exposes growthCount; tapering FAILS when no adjacent
//           growth pair existed (previously passed vacuously)
//   Fix 3 — FP_EPSILON on the growth and diagonal comparisons
//   Fix 6 — counterDeltaMag (>= 0) magnitude semantics:
//           bullish counts levels with delta <= -mag,
//           bearish counts levels with delta >= +mag
// ============================================================

static int CountConsecutiveOppositeDelta(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    int counterDeltaMag, int maxLevels,
    float& out_avgGrowthPct, float& out_maxDiagRatio,
    bool enableTapering, int minGrowthPct,
    bool enableDiag, int diagRatioPct,
    bool& out_tapOk, bool& out_diagOk, int& out_growthCount)
{
    out_avgGrowthPct = 0.0f;
    out_maxDiagRatio = 0.0f;
    out_growthCount  = 0;
    out_tapOk  = !enableTapering;  // passes by default when disabled
    out_diagOk = !enableDiag;

    if (sc.VolumeAtPriceForBars == nullptr)
        return 0;

    int numLevels = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(barIndex);
    if (numLevels == 0)
        return 0;

    int scanDepth = min(numLevels, maxLevels);

    const s_VolumeAtPriceV2* p_VAP     = nullptr;
    const s_VolumeAtPriceV2* p_VAPNext = nullptr;

    int   consecCount    = 0;
    int   growthCount    = 0;
    float totalGrowthPct = 0.0f;
    float prevAggVol     = 0.0f;
    bool  diagMet        = false;

    for (int i = 0; i < scanDepth; ++i)
    {
        int levelIdx = isBullish ? i : (numLevels - 1 - i);

        if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(barIndex, levelIdx, &p_VAP))
            continue;

        unsigned int bidVol = p_VAP->BidVolume;
        unsigned int askVol = p_VAP->AskVolume;

        // Skip empty levels without breaking the streak
        if (bidVol == 0 && askVol == 0)
            continue;

        // Bullish: aggressive side = BidVolume (sellers hitting bids)
        // Bearish: aggressive side = AskVolume (buyers lifting offers)
        unsigned int aggVol = isBullish ? bidVol : askVol;

        int levelDelta = static_cast<int>(askVol) - static_cast<int>(bidVol);

        bool isCounterDelta = isBullish
            ? (levelDelta <= -counterDeltaMag)
            : (levelDelta >=  counterDeltaMag);

        if (!isCounterDelta)
            break;   // streak broken

        consecCount++;

        // Progressive volume tapering accumulation
        if (enableTapering && prevAggVol > 0.0f && aggVol > 0)
        {
            float growth = ((static_cast<float>(aggVol) - prevAggVol) / prevAggVol) * 100.0f;
            totalGrowthPct += growth;
            growthCount++;
        }
        if (aggVol > 0)
            prevAggVol = static_cast<float>(aggVol);

        // Diagonal imbalance:
        // Bullish: BidVol[level] vs AskVol[level+1] (one tick higher)
        // Bearish: AskVol[level] vs BidVol[level-1] (one tick lower)
        if (enableDiag && i + 1 < scanDepth)
        {
            int nextLevelIdx = isBullish ? (i + 1) : (numLevels - 2 - i);
            if (nextLevelIdx >= 0 && nextLevelIdx < numLevels)
            {
                if (sc.VolumeAtPriceForBars->GetVAPElementAtIndex(barIndex, nextLevelIdx, &p_VAPNext))
                {
                    unsigned int nextPassVol = isBullish ? p_VAPNext->AskVolume
                                                         : p_VAPNext->BidVolume;
                    float denom = static_cast<float>(max(nextPassVol, 1u));
                    float diagRatio = (static_cast<float>(aggVol) / denom) * 100.0f;

                    if (diagRatio > out_maxDiagRatio)
                        out_maxDiagRatio = diagRatio;

                    if (diagRatio + FP_EPSILON >= static_cast<float>(diagRatioPct))  // Fix 3
                        diagMet = true;
                }
            }
        }
    }

    if (growthCount > 0)
        out_avgGrowthPct = totalGrowthPct / static_cast<float>(growthCount);
    out_growthCount = growthCount;

    // Fix 2: an enabled tapering filter is UNVERIFIABLE (and fails)
    // when no adjacent growth pair existed (single populated level).
    if (enableTapering)
        out_tapOk = (growthCount > 0)
                 && (out_avgGrowthPct + FP_EPSILON >= static_cast<float>(minGrowthPct));

    if (enableDiag)
        out_diagOk = diagMet;

    return consecCount;
}

// ============================================================
// HELPER: CalculateAbsorptionStrength
// Identical to v1105 CalculateSignalStrength (baseline truth).
// ============================================================

static int CalculateAbsorptionStrength(
    int consecLevels, float avgGrowthPct, float maxDiagRatio,
    bool exhaustionPresent, bool deltaDivPresent, int priorCandles,
    int minOppLevels, int minGrowthPct, int diagRatioPct,
    unsigned int volAtExtreme, int minVolAtExtreme)
{
    float score = 30.0f; // base for passing all required filters

    float levelBonus = static_cast<float>(consecLevels - minOppLevels) * 3.0f;
    score += min(15.0f, max(0.0f, levelBonus));

    if (avgGrowthPct > 0.0f)
    {
        float tapBonus = (avgGrowthPct - static_cast<float>(minGrowthPct)) / 3.0f;
        score += min(10.0f, max(0.0f, tapBonus));
    }

    if (maxDiagRatio > 0.0f)
    {
        float diagBonus = (maxDiagRatio - static_cast<float>(diagRatioPct)) / 30.0f;
        score += min(10.0f, max(0.0f, diagBonus));
    }

    if (exhaustionPresent)
        score += 10.0f;
    if (deltaDivPresent)
        score += 10.0f;
    if (priorCandles >= 3)
        score += 10.0f;
    if (minVolAtExtreme > 0 && volAtExtreme >= static_cast<unsigned int>(minVolAtExtreme * 2))
        score += 5.0f;

    return static_cast<int>(min(STRENGTH_MAX, max(STRENGTH_MIN, score)));
}

// ============================================================
// HELPER: IsWithinTimeFilter (unchanged from v1105)
// ============================================================

static bool IsWithinTimeFilter(
    SCStudyInterfaceRef sc, int barIndex,
    int startH, int startM, int endH, int endM)
{
    SCDateTime barDT = sc.BaseDateTimeIn[barIndex];
    int barTimeInSeconds = barDT.GetTimeInSeconds();
    int startSeconds = startH * 3600 + startM * 60;
    int endSeconds   = endH   * 3600 + endM   * 60;

    if (startSeconds <= endSeconds)
        return (barTimeInSeconds >= startSeconds && barTimeInSeconds <= endSeconds);
    else // overnight window (e.g. 18:00 - 04:00)
        return (barTimeInSeconds >= startSeconds || barTimeInSeconds <= endSeconds);
}

// ============================================================
// HELPER: UpdateLiquidityPools (spec 5.2.1)
// Confirms swing points with a k-bar lag (no repaint) and
// maintains the pool deque: equal-level clustering, expiry,
// per-side capacity.
// ============================================================

static void UpdateLiquidityPools(
    SCStudyInterfaceRef sc, const StudySettings& s, StudyState& state)
{
    const int i = sc.Index;
    if (i <= state.LastPoolUpdateBar)
        return;                       // already processed this bar
    state.LastPoolUpdateBar = i;

    const int   k   = s.swingStrength;
    const float tol = static_cast<float>(s.equalLevelTolTicks) * sc.TickSize
                    + sc.TickSize * 0.01f;   // grid-rounding guard

    // ---- Expire stale pools (and drop consumed ones) ----
    for (std::deque<LiquidityPool>::iterator it = state.Pools.begin();
         it != state.Pools.end(); )
    {
        if (it->Consumed || (i - it->LastTouchBar) > s.poolLookbackBars)
            it = state.Pools.erase(it);
        else
            ++it;
    }

    // ---- Confirm the swing candidate at bar s0 = i - k ----
    const int s0 = i - k;
    if (s0 - k < 0)
        return;

    // Swing low: strictly below the k bars on each side
    bool isSwingLow = true;
    for (int m = 1; m <= k && isSwingLow; ++m)
    {
        if (!(sc.Low[s0] < sc.Low[s0 - m]) || !(sc.Low[s0] < sc.Low[s0 + m]))
            isSwingLow = false;
    }

    // Swing high: strictly above the k bars on each side
    bool isSwingHigh = true;
    for (int m = 1; m <= k && isSwingHigh; ++m)
    {
        if (!(sc.High[s0] > sc.High[s0 - m]) || !(sc.High[s0] > sc.High[s0 + m]))
            isSwingHigh = false;
    }

    if (isSwingLow)
    {
        const float lvl = sc.Low[s0];
        bool merged = false;
        for (LiquidityPool& p : state.Pools)
        {
            if (p.IsLowPool && !p.Consumed && fabs(p.Level - lvl) <= tol)
            {
                // Relative equal lows — cluster
                p.TouchCount++;
                p.Level        = min(p.Level, lvl);   // stops sit under the lower one
                p.LastTouchBar = s0;
                merged = true;
                break;
            }
        }
        if (!merged)
        {
            LiquidityPool p;
            p.Level = lvl; p.LastTouchBar = s0; p.TouchCount = 1;
            p.IsLowPool = true; p.Consumed = false;
            state.Pools.push_back(p);
        }
    }

    if (isSwingHigh)
    {
        const float lvl = sc.High[s0];
        bool merged = false;
        for (LiquidityPool& p : state.Pools)
        {
            if (!p.IsLowPool && !p.Consumed && fabs(p.Level - lvl) <= tol)
            {
                p.TouchCount++;
                p.Level        = max(p.Level, lvl);   // stops sit above the higher one
                p.LastTouchBar = s0;
                merged = true;
                break;
            }
        }
        if (!merged)
        {
            LiquidityPool p;
            p.Level = lvl; p.LastTouchBar = s0; p.TouchCount = 1;
            p.IsLowPool = false; p.Consumed = false;
            state.Pools.push_back(p);
        }
    }

    // ---- Per-side capacity: drop the oldest of the heavier side ----
    int lows = 0, highs = 0;
    for (const LiquidityPool& p : state.Pools)
        (p.IsLowPool ? lows : highs)++;

    while (lows > MAX_POOLS_PER_SIDE || highs > MAX_POOLS_PER_SIDE)
    {
        bool dropLow = (lows > MAX_POOLS_PER_SIDE);
        for (std::deque<LiquidityPool>::iterator it = state.Pools.begin();
             it != state.Pools.end(); ++it)
        {
            if (it->IsLowPool == dropLow)
            {
                state.Pools.erase(it);
                (dropLow ? lows : highs)--;
                break;
            }
        }
    }
}

// ============================================================
// HELPER: EvaluateSweepRaidBar (spec 5.2.2 conditions 3-6)
// Verdict on the penetration ("raid") bar: rejection wick,
// VAP volume burst into the penetration zone, trapped
// counter-delta. Returns true when all enabled checks pass.
// ============================================================

static bool EvaluateSweepRaidBar(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish, float poolLevel,
    const StudySettings& s,
    int& out_penTicks, float& out_burstPct, float& out_wickPct, int& out_zoneDelta)
{
    out_penTicks = 0; out_burstPct = 0.0f; out_wickPct = 0.0f; out_zoneDelta = 0;

    const float T        = sc.TickSize;
    const float halfTick = T * 0.5f;
    const float high     = sc.High[barIndex];
    const float low      = sc.Low[barIndex];
    const float range    = high - low;

    // Penetration depth (ticks beyond the pool level)
    float penF = isBullish ? (poolLevel - low) : (high - poolLevel);
    out_penTicks = static_cast<int>((penF + halfTick) / T);

    // Condition 3: rejection wick. Auto-fail on zero-range bars.
    if (range < halfTick)
        return false;
    float wick = isBullish ? (min(sc.Open[barIndex], sc.Close[barIndex]) - low)
                           : (high - max(sc.Open[barIndex], sc.Close[barIndex]));
    out_wickPct = (wick / range) * 100.0f;
    if (out_wickPct + FP_EPSILON < static_cast<float>(s.sweepMinWickPct))
        return false;

    // Conditions 4-6: VAP scan of the bar
    if (sc.VolumeAtPriceForBars == nullptr)
        return false;
    int numLevels = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(barIndex);
    if (numLevels <= 0)
        return false;

    const s_VolumeAtPriceV2* p_VAP = nullptr;
    int     nAll = 0, nPen = 0;
    double  volAll = 0.0, volPen = 0.0;
    int     zoneDelta = 0;

    for (int lv = 0; lv < numLevels; ++lv)
    {
        if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(barIndex, lv, &p_VAP))
            continue;

        unsigned int bidVol = p_VAP->BidVolume;
        unsigned int askVol = p_VAP->AskVolume;
        if (bidVol == 0 && askVol == 0)
            continue;

        double lvVol = static_cast<double>(bidVol) + static_cast<double>(askVol);
        nAll++;
        volAll += lvVol;

        // Level price from the tick-quantized VAP grid
        float lvPrice = p_VAP->PriceInTicks * T;

        bool inPenZone = isBullish ? (lvPrice < poolLevel - halfTick)
                                   : (lvPrice > poolLevel + halfTick);
        if (inPenZone)
        {
            nPen++;
            volPen   += lvVol;
            zoneDelta += static_cast<int>(askVol) - static_cast<int>(bidVol);
        }
    }
    out_zoneDelta = zoneDelta;

    // Condition 4: burst — penetration-zone per-level volume vs bar average
    if (nPen < 1 || nAll < 1 || volAll <= 0.0)
        return false;
    float avgLevelVol = static_cast<float>(volAll / nAll);
    float avgPenVol   = static_cast<float>(volPen / nPen);
    out_burstPct = (avgPenVol / avgLevelVol) * 100.0f;
    if (out_burstPct + FP_EPSILON < static_cast<float>(s.sweepBurstPct))
        return false;

    // Condition 5: trapped counter-delta in the penetration zone (optional)
    if (s.sweepMinZoneDelta > 0)
    {
        if (isBullish  && !(zoneDelta <= -s.sweepMinZoneDelta)) return false;
        if (!isBullish && !(zoneDelta >=  s.sweepMinZoneDelta)) return false;
    }

    return true;
}

// ============================================================
// HELPER: EvaluateSweep (spec 5.2.2)
// Resolves a pending two-bar reclaim first, then scans active
// pools for a fresh raid at this bar. Pools are consumed on
// penetration whether or not a signal results; continued
// closes beyond the pool classify as a liquidity RUN (no signal).
// ============================================================

static DetectionResult EvaluateSweep(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    const StudySettings& s, StudyState& state)
{
    DetectionResult result;
    memset(&result, 0, sizeof(result));
    result.Pattern   = PATTERN_SWEEP;
    result.IsBullish = isBullish;

    const float T        = sc.TickSize;
    const float halfTick = T * 0.5f;
    const float penReq   = static_cast<float>(s.sweepMinPenTicks)   * T;
    const float cbReq    = static_cast<float>(s.sweepCloseBackTicks) * T;
    const float close    = sc.Close[barIndex];

    PendingSweep& pend = isBullish ? state.PendBull : state.PendBear;

    // ---- 1. Resolve a pending two-bar reclaim from the prior bar ----
    if (pend.Active)
    {
        if (pend.PenBar == barIndex - 1)
        {
            bool reclaimed = isBullish
                ? (close + halfTick >= pend.PoolLevel + cbReq)
                : (close - halfTick <= pend.PoolLevel - cbReq);

            if (reclaimed && pend.CondsPassed)
            {
                // Sweep confirmed at THIS bar (the reclaim bar)
                result.Detected         = true;
                result.PoolTouchCount   = pend.PoolTouchCount;
                result.PenetrationTicks = pend.PenetrationTicks;
                result.BurstRatioPct    = pend.BurstRatioPct;
                result.WickPct          = pend.WickPct;
                result.ZoneDelta        = pend.ZoneDelta;

                float extreme = isBullish
                    ? min(pend.PenExtreme, sc.Low[barIndex])
                    : max(pend.PenExtreme, sc.High[barIndex]);

                float cbF = isBullish ? (close - pend.PoolLevel)
                                      : (pend.PoolLevel - close);
                result.CloseBackTicks = static_cast<int>((cbF + halfTick) / T);

                result.MarkerPrice = extreme;
                result.WantZone    = true;
                if (isBullish) { result.ZoneBottom = extreme; result.ZoneTop = pend.PoolLevel; }
                else           { result.ZoneTop = extreme; result.ZoneBottom = pend.PoolLevel; }

                result.Strength = CalculateSweepStrength(sc, barIndex, isBullish, result, s);
            }
            // Not reclaimed -> liquidity RUN (or failed conditions): no signal.
            pend.Active = false;
        }
        else
        {
            pend.Active = false;   // stale (recalc edge) — discard
        }
        if (result.Detected)
            return result;         // one sweep per bar per direction
    }

    // ---- 2. Scan pools for a fresh raid at this bar ----
    // Choose the level closest to the prior range among penetrated pools
    // (highest low pool / lowest high pool): that is where the stops sat.
    // All penetrated pools are consumed — they have been raided.
    LiquidityPool* best = nullptr;
    for (LiquidityPool& p : state.Pools)
    {
        if (p.Consumed || p.IsLowPool != isBullish)
            continue;
        // The pool must predate this bar's swing window entirely
        if (p.LastTouchBar >= barIndex)
            continue;

        bool penetrated = isBullish
            ? (sc.Low[barIndex]  <= p.Level - penReq + halfTick)
            : (sc.High[barIndex] >= p.Level + penReq - halfTick);
        if (!penetrated)
            continue;

        p.Consumed = true;
        if (best == nullptr ||
            ( isBullish && p.Level > best->Level) ||
            (!isBullish && p.Level < best->Level))
        {
            best = &p;
        }
    }

    if (best == nullptr)
        return result;

    // Raid-bar conditions (wick / burst / trapped delta)
    int   penTicks = 0, zoneDelta = 0;
    float burstPct = 0.0f, wickPct = 0.0f;
    bool condsPassed = EvaluateSweepRaidBar(
        sc, barIndex, isBullish, best->Level, s,
        penTicks, burstPct, wickPct, zoneDelta);

    bool reclaimedSameBar = isBullish
        ? (close + halfTick >= best->Level + cbReq)
        : (close - halfTick <= best->Level - cbReq);

    if (reclaimedSameBar)
    {
        if (!condsPassed)
            return result;

        result.Detected         = true;
        result.PoolTouchCount   = best->TouchCount;
        result.PenetrationTicks = penTicks;
        result.BurstRatioPct    = burstPct;
        result.WickPct          = wickPct;
        result.ZoneDelta        = zoneDelta;

        float extreme = isBullish ? sc.Low[barIndex] : sc.High[barIndex];
        float cbF = isBullish ? (close - best->Level) : (best->Level - close);
        result.CloseBackTicks = static_cast<int>((cbF + halfTick) / T);

        result.MarkerPrice = extreme;
        result.WantZone    = true;
        if (isBullish) { result.ZoneBottom = extreme; result.ZoneTop = best->Level; }
        else           { result.ZoneTop = extreme; result.ZoneBottom = best->Level; }

        result.Strength = CalculateSweepStrength(sc, barIndex, isBullish, result, s);
        return result;
    }

    bool closedBeyond = isBullish ? (close < best->Level - halfTick)
                                  : (close > best->Level + halfTick);
    if (closedBeyond)
    {
        // Two-bar deferral: next bar decides sweep (reclaim) vs run.
        pend.Active           = true;
        pend.PoolLevel        = best->Level;
        pend.PoolTouchCount   = best->TouchCount;
        pend.PenBar           = barIndex;
        pend.PenExtreme       = isBullish ? sc.Low[barIndex] : sc.High[barIndex];
        pend.CondsPassed      = condsPassed;
        pend.PenetrationTicks = penTicks;
        pend.BurstRatioPct    = burstPct;
        pend.WickPct          = wickPct;
        pend.ZoneDelta        = zoneDelta;
    }
    // Closed between pool level and pool+closeBack: neither a clean
    // reclaim nor a close beyond — treated as no sweep (pool consumed).

    return result;
}

// ============================================================
// HELPER: CalculateSweepStrength (spec 6.2)
// ============================================================

static int CalculateSweepStrength(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    const DetectionResult& r, const StudySettings& s)
{
    float score = 30.0f;

    if (r.PoolTouchCount >= 2)                       // relative equal highs/lows
        score += 10.0f;

    score += min(10.0f, max(0.0f,
        static_cast<float>(r.PenetrationTicks - s.sweepMinPenTicks) * 3.0f));

    score += min(15.0f, max(0.0f,
        (r.BurstRatioPct - static_cast<float>(s.sweepBurstPct)) / 20.0f));

    score += min(10.0f, max(0.0f,
        (r.WickPct - static_cast<float>(s.sweepMinWickPct)) / 4.0f));

    if (s.sweepMinZoneDelta > 0)
    {
        float beyond = static_cast<float>(
            (r.ZoneDelta < 0 ? -r.ZoneDelta : r.ZoneDelta) - s.sweepMinZoneDelta);
        score += min(10.0f, max(0.0f, beyond / 25.0f));
    }

    score += min(5.0f, max(0.0f,
        static_cast<float>(r.CloseBackTicks - s.sweepCloseBackTicks) * 2.0f));

    // Finished Business stacked on the sweep extreme
    unsigned int volAtExtreme = 0;
    if (CheckExhaustionAtExtreme(sc, barIndex, isBullish, volAtExtreme))
        score += 10.0f;

    return static_cast<int>(min(STRENGTH_MAX, max(STRENGTH_MIN, score)));
}

// ============================================================
// HELPER: IsWithinSlingshotWindow
// True when the bar's start time falls inside one of the three
// configured windows [startHour:00, startHour:00 + duration).
// Hours are in the CHART's timezone (defaults assume US Eastern).
// ============================================================

static bool IsWithinSlingshotWindow(
    SCStudyInterfaceRef sc, int barIndex, const StudySettings& s)
{
    int barSec = sc.BaseDateTimeIn[barIndex].GetTimeInSeconds();
    int durSec = s.slingWinMinutes * 60;

    const int starts[3] = {
        s.slingWin1Hour * 3600,
        s.slingWin2Hour * 3600,
        s.slingWin3Hour * 3600
    };

    for (int w = 0; w < 3; ++w)
    {
        int startSec = starts[w];
        int endSec   = startSec + durSec;
        if (endSec <= 86400)
        {
            if (barSec >= startSec && barSec < endSec)
                return true;
        }
        else // window wraps past midnight
        {
            if (barSec >= startSec || barSec < endSec - 86400)
                return true;
        }
    }
    return false;
}

// ============================================================
// HELPER: UpdateSlingshots (spec 5.3)
// Three-stage slingshot state machine:
//   Stage A  a Sweep signal arms a setup (sweep-only fuel)
//   Stage B  displacement candle (body %, close back over the
//            pool, optional delta flip) leaving an FVG, confirmed
//            at the close of the bar AFTER the displacement bar
//   Stage C  price retraces and taps the FVG -> signal (optionally
//            only inside configured time windows)
// Cancellation at any stage: price violates the sweep extreme,
// the FVG fully fills, or the stage window expires.
// ============================================================

static void UpdateSlingshots(
    SCStudyInterfaceRef sc, const StudySettings& s, StudyState& state,
    const DetectionResult& sweepBull, const DetectionResult& sweepBear,
    DetectionResult& out_slingBull, DetectionResult& out_slingBear)
{
    const int   i        = sc.Index;
    const float T        = sc.TickSize;
    const float halfTick = T * 0.5f;

    // ---- Cancellation + stage advancement for active setups ----
    for (SlingshotSetup& su : state.Setups)
    {
        if (su.Done)
            continue;

        // Trap-held rule: any violation of the sweep extreme kills the setup
        bool trapBroken = su.IsBullish
            ? (sc.Low[i]  <= su.SweepExtreme + halfTick)
            : (sc.High[i] >= su.SweepExtreme - halfTick);
        if (trapBroken && i > su.SweepBar)
        {
            su.Done = true;
            continue;
        }

        // Expiry
        if (i > su.ExpiryBar)
        {
            su.Done = true;
            continue;
        }

        if (su.State == SLING_WAIT_DISPLACEMENT)
        {
            // Bar i is the potential FVG-CONFIRM bar; the displacement
            // candidate is d = i - 1 (must lie strictly after the sweep
            // bar and inside the displacement window).
            const int d = i - 1;
            if (d <= su.SweepBar || d > su.SweepBar + s.slingDispWindow || d < 1)
                continue;

            const float dOpen  = sc.Open[d];
            const float dClose = sc.Close[d];
            const float dRange = sc.High[d] - sc.Low[d];
            if (dRange < halfTick)
                continue;

            // Directional displacement body
            bool rightDir = su.IsBullish ? (dClose > dOpen) : (dClose < dOpen);
            if (!rightDir)
                continue;
            float bodyPct = (fabs(dClose - dOpen) / dRange) * 100.0f;
            if (bodyPct + FP_EPSILON < static_cast<float>(s.slingMinBodyPct))
                continue;

            // Structure shift: displacement closes back beyond the swept pool
            bool overPool = su.IsBullish
                ? (dClose > su.PoolLevel + halfTick)
                : (dClose < su.PoolLevel - halfTick);
            if (!overPool)
                continue;

            // Optional delta flip vs the sweep bar (footprint enhancement)
            float flipPct = 0.0f;
            if (s.slingRequireFlip)
            {
                double dDelta = static_cast<double>(sc.AskVolume[d])
                              - static_cast<double>(sc.BidVolume[d]);
                bool flipped = su.IsBullish ? (dDelta > 0.0) : (dDelta < 0.0);
                if (!flipped)
                    continue;
                double sweepMag = fabs(su.SweepBarDelta);
                if (sweepMag < 1.0)
                    sweepMag = 1.0;
                flipPct = static_cast<float>((fabs(dDelta) / sweepMag) * 100.0);
                if (flipPct + FP_EPSILON < static_cast<float>(s.slingFlipPct))
                    continue;
            }

            // FVG confirmed at this bar's close:
            // bullish: Low[d+1] > High[d-1] + minFvg   (d+1 == i)
            // bearish: High[d+1] < Low[d-1] - minFvg
            const float fvgReq = static_cast<float>(s.slingMinFvgTicks) * T;
            if (su.IsBullish)
            {
                if (sc.Low[i] > sc.High[d - 1] + fvgReq - halfTick)
                {
                    su.FvgBottom = sc.High[d - 1];
                    su.FvgTop    = sc.Low[i];
                }
                else
                    continue;
            }
            else
            {
                if (sc.High[i] < sc.Low[d - 1] - fvgReq + halfTick)
                {
                    su.FvgTop    = sc.Low[d - 1];
                    su.FvgBottom = sc.High[i];
                }
                else
                    continue;
            }

            su.State           = SLING_WAIT_TAP;
            su.DisplacementBar = d;
            su.DeltaFlipPct    = flipPct;
            su.ExpiryBar       = i + s.slingTapWindow;
        }
        else if (su.State == SLING_WAIT_TAP)
        {
            // Tap bar must be strictly after the FVG-confirm bar (d+1)
            if (i <= su.DisplacementBar + 1)
                continue;

            bool tapped, gapFilled;
            float tapDepthF;
            if (su.IsBullish)
            {
                tapped    = (sc.Low[i] <= su.FvgTop + halfTick);
                gapFilled = (sc.Close[i] < su.FvgBottom - halfTick);
                tapDepthF = su.FvgTop - sc.Low[i];
            }
            else
            {
                tapped    = (sc.High[i] >= su.FvgBottom - halfTick);
                gapFilled = (sc.Close[i] > su.FvgTop + halfTick);
                tapDepthF = sc.High[i] - su.FvgBottom;
            }

            if (gapFilled)
            {
                su.Done = true;   // gap fully filled — setup invalid
                continue;
            }
            if (!tapped)
                continue;

            // Time-window gate: an out-of-window tap neither signals nor
            // cancels — the setup stays armed until expiry.
            bool inWindow = IsWithinSlingshotWindow(sc, i, s);
            if (s.slingUseWindows && !inWindow)
                continue;

            // SIGNAL
            DetectionResult& out = su.IsBullish ? out_slingBull : out_slingBear;
            memset(&out, 0, sizeof(out));
            out.Detected            = true;
            out.Pattern             = PATTERN_SLINGSHOT;
            out.IsBullish           = su.IsBullish;
            out.FvgTicks            = static_cast<int>(
                                         ((su.FvgTop - su.FvgBottom) + halfTick) / T);
            out.TapDepthTicks       = static_cast<int>((tapDepthF + halfTick) / T);
            out.DeltaFlipPct        = su.DeltaFlipPct;
            out.SourceSweepStrength = su.SweepStrength;

            const int d = su.DisplacementBar;
            float dRange = sc.High[d] - sc.Low[d];
            out.BodyPct = (dRange > halfTick)
                ? (fabs(sc.Close[d] - sc.Open[d]) / dRange) * 100.0f : 0.0f;

            out.MarkerPrice = su.IsBullish ? sc.Low[i] : sc.High[i];
            out.WantZone    = true;            // the FVG is the zone (ruling Q7)
            out.ZoneTop     = su.FvgTop;
            out.ZoneBottom  = su.FvgBottom;

            // Same-bar rejection beyond the gap = bonus context
            bool rejected = su.IsBullish
                ? (sc.Close[i] > su.FvgTop + halfTick)
                : (sc.Close[i] < su.FvgBottom - halfTick);

            out.Strength = CalculateSlingshotStrength(out, s, inWindow);
            if (rejected)
                out.Strength = static_cast<int>(
                    min(STRENGTH_MAX, static_cast<float>(out.Strength) + 10.0f));

            su.Done = true;   // one signal per setup
        }
    }

    // ---- Purge finished setups ----
    for (std::deque<SlingshotSetup>::iterator it = state.Setups.begin();
         it != state.Setups.end(); )
    {
        if (it->Done)
            it = state.Setups.erase(it);
        else
            ++it;
    }

    // ---- Stage A: arm new setups from this bar's sweep signals.
    // Added AFTER stage processing so a setup can never trigger on
    // its own sweep bar. A newer sweep supersedes an older active
    // setup of the same direction (latest fuel wins).
    const DetectionResult* fuels[2] = { &sweepBull, &sweepBear };
    for (int f = 0; f < 2; ++f)
    {
        const DetectionResult& fuel = *fuels[f];
        if (!fuel.Detected)
            continue;

        for (SlingshotSetup& su : state.Setups)
            if (su.IsBullish == fuel.IsBullish)
                su.Done = true;

        SlingshotSetup su;
        memset(&su, 0, sizeof(su));
        su.IsBullish     = fuel.IsBullish;
        su.State         = SLING_WAIT_DISPLACEMENT;
        su.SweepBar      = i;
        su.SweepExtreme  = fuel.MarkerPrice;     // sweep extreme price
        su.PoolLevel     = fuel.IsBullish ? fuel.ZoneTop : fuel.ZoneBottom;
        su.SweepStrength = fuel.Strength;
        su.SweepBarDelta = static_cast<double>(sc.AskVolume[i])
                         - static_cast<double>(sc.BidVolume[i]);
        // +1: the FVG confirms one bar after the last displacement candidate
        su.ExpiryBar     = i + s.slingDispWindow + 1;
        su.Done          = false;
        state.Setups.push_back(su);
    }
}

// ============================================================
// HELPER: CalculateSlingshotStrength (spec 6.3)
// The same-bar rejection bonus (+10) is applied by the caller.
// ============================================================

static int CalculateSlingshotStrength(
    const DetectionResult& r, const StudySettings& s, bool tapInWindow)
{
    float score = 30.0f;

    if (r.SourceSweepStrength >= 60)                 // high-grade fuel
        score += 10.0f;

    score += min(10.0f, max(0.0f,
        static_cast<float>(r.FvgTicks - s.slingMinFvgTicks) * 2.0f));

    score += min(10.0f, max(0.0f,
        (r.BodyPct - static_cast<float>(s.slingMinBodyPct)) / 4.0f));

    if (s.slingRequireFlip)
        score += min(15.0f, max(0.0f,
            (r.DeltaFlipPct - static_cast<float>(s.slingFlipPct)) / 30.0f));

    // Window-quality bonus only when windows are NOT enforced
    if (!s.slingUseWindows && tapInWindow)
        score += 5.0f;

    return static_cast<int>(min(STRENGTH_MAX, max(STRENGTH_MIN, score)));
}

// ============================================================
// HELPER: PatternName / PatternCode
// ============================================================

static const char* PatternName(PatternType p)
{
    switch (p)
    {
        case PATTERN_SWEEP:     return "SWEEP";
        case PATTERN_SLINGSHOT: return "SLINGSHOT";
        default:                return "ABSORPTION";
    }
}

static const char* PatternCode(PatternType p)
{
    switch (p)
    {
        case PATTERN_SWEEP:     return "SWP";
        case PATTERN_SLINGSHOT: return "SLG";
        default:                return "ABS";
    }
}

// ============================================================
// HELPER: OutputSignal (spec 4.5)
// The single output path for every pattern: subgraph write,
// marker, zone, highlight, alert, edge registration.
// ============================================================

static void OutputSignal(
    SCStudyInterfaceRef sc, const DetectionResult& r,
    const StudySettings& s, StudyState& state)
{
    const int i = sc.Index;

    // ---- Subgraph map (spec section 9) ----
    int priceSG, bullPriceSG, strengthSG;
    switch (r.Pattern)
    {
        case PATTERN_SWEEP:     bullPriceSG = 3; strengthSG = 7; break;
        case PATTERN_SLINGSHOT: bullPriceSG = 5; strengthSG = 8; break;
        default:                bullPriceSG = 0; strengthSG = 2; break;
    }
    priceSG = r.IsBullish ? bullPriceSG : bullPriceSG + 1;

    const float plotPrice = r.IsBullish
        ? r.MarkerPrice - (s.markerOffsetTicks * sc.TickSize)
        : r.MarkerPrice + (s.markerOffsetTicks * sc.TickSize);

    sc.Subgraph[priceSG][i] = plotPrice;

    // Bull precedence on the shared strength subgraph: a bearish
    // signal does not overwrite a bullish one on the same bar.
    if (r.IsBullish || sc.Subgraph[bullPriceSG][i] == 0.0f)
        sc.Subgraph[strengthSG][i] = static_cast<float>(r.Strength);

    // ---- Marker (deterministic line number, Fix 1) ----
    const SCString& markerText =
        (r.Pattern == PATTERN_SWEEP)     ? s.markerTextSweep :
        (r.Pattern == PATTERN_SLINGSHOT) ? s.markerTextSling :
                                           s.markerTextAbs;
    DrawMarkerText(sc, i, plotPrice,
        sc.Subgraph[priceSG].PrimaryColor, markerText, s.markerFontSize,
        MakeLineNumber(i, r.Pattern, r.IsBullish, ELEM_MARKER));

    // ---- Zone rectangle ----
    if (r.WantZone && s.enableRects)
    {
        if (static_cast<int>(state.Zones.size()) >= MAX_ZONES)
        {
            state.Zones.pop_front();
            if (!state.ZoneCapacityWarned)   // Fix 4: once per session
            {
                sc.AddMessageToLog(
                    "SCOFA v" SCOFA_VERSION_STR ": Zone capacity (500) reached - "
                    "oldest zones are being recycled. Consider reducing Max Zone "
                    "Extension Bars or Historical Lookback Bars.", 1);
                state.ZoneCapacityWarned = true;
            }
        }

        SignalZone zone;
        zone.SignalBarIndex = i;
        zone.ZoneTop        = r.ZoneTop;
        zone.ZoneBottom     = r.ZoneBottom;
        zone.RectLineNumber = MakeLineNumber(i, r.Pattern, r.IsBullish, ELEM_RECT);
        zone.Pattern        = r.Pattern;
        zone.IsBullish      = r.IsBullish;
        zone.IsExtending    = true;
        zone.ExtendedBars   = 0;

        CreateOrUpdateZoneRectangle(sc, zone,
            r.IsBullish ? s.bullZoneColor : s.bearZoneColor,
            s.zoneTransparency, i);
        state.Zones.push_back(zone);
    }

    // ---- Bar highlight ----
    if (s.enableHighlight)
        HighlightBar(sc, i,
            r.IsBullish ? s.bullZoneColor : s.bearZoneColor,
            s.zoneTransparency,
            MakeLineNumber(i, r.Pattern, r.IsBullish, ELEM_HIGHLIGHT));

    // ---- Alert (In:22/23 shared across patterns) ----
    const int alertNum = r.IsBullish
        ? sc.Input[22].GetAlertSoundNumber()
        : sc.Input[23].GetAlertSoundNumber();
    if (alertNum > 0)
    {
        SCString alertMsg;
        alertMsg.Format("SCOFA: %s %s @ %s | strength %d",
            r.IsBullish ? "Bullish" : "Bearish",
            PatternName(r.Pattern),
            sc.FormatGraphValue(r.MarkerPrice, sc.ValueFormat).GetChars(),
            r.Strength);
        sc.SetAlert(alertNum, alertMsg);
    }

    // ---- Edge registration (spec 11.1) ----
    if (s.edgeEnabled && r.Strength >= s.edgeMinStrength)
    {
        TrackedSignal sig;
        sig.Pattern        = r.Pattern;
        sig.IsBullish      = r.IsBullish;
        sig.SignalBarIndex = i;
        sig.EntryPrice     = sc.Close[i];
        sig.Strength       = r.Strength;
        sig.MFETicks       = 0.0f;
        sig.MAETicks       = 0.0f;
        state.OpenSignals.push_back(sig);
    }
}

// ============================================================
// HELPER: UpdateEdgeTracking (spec 11.2)
// Runs once per newly closed bar, BEFORE new detections.
// Resolution order: stop first (same-bar tie -> Loss,
// conservative default ruling Q5), then target, then horizon.
// ============================================================

static void UpdateEdgeTracking(
    SCStudyInterfaceRef sc, const StudySettings& s, StudyState& state)
{
    const int i = sc.Index;
    if (i <= state.LastTrackedBarIndex)
        return;                          // intrabar tick — already processed
    state.LastTrackedBarIndex = i;

    const float T = sc.TickSize;

    for (std::deque<TrackedSignal>::iterator it = state.OpenSignals.begin();
         it != state.OpenSignals.end(); )
    {
        TrackedSignal& sig = *it;

        // A signal never resolves on its own bar
        if (sig.SignalBarIndex >= i)
        {
            ++it;
            continue;
        }

        // Update MFE/MAE from the just-closed bar
        float fav, adv;
        if (sig.IsBullish)
        {
            fav = (sc.High[i] - sig.EntryPrice) / T;
            adv = (sig.EntryPrice - sc.Low[i])  / T;
        }
        else
        {
            fav = (sig.EntryPrice - sc.Low[i])  / T;
            adv = (sc.High[i] - sig.EntryPrice) / T;
        }
        if (fav > sig.MFETicks) sig.MFETicks = fav;
        if (adv > sig.MAETicks) sig.MAETicks = adv;

        const int barsElapsed = i - sig.SignalBarIndex;
        const char* outcome = nullptr;

        if (sig.MAETicks + FP_EPSILON >= static_cast<float>(s.edgeStopTicks))
            outcome = "STOP";            // checked first: same-bar tie = Loss
        else if (sig.MFETicks + FP_EPSILON >= static_cast<float>(s.edgeTargetTicks))
            outcome = "TARGET";
        else if (barsElapsed >= s.edgeHorizon)
            outcome = "EXPIRED";

        if (outcome == nullptr)
        {
            ++it;
            continue;
        }

        // Fold into the bucket
        EdgeBucket& b = state.Edge[sig.Pattern][sig.IsBullish ? 0 : 1];
        b.Count++;
        if      (outcome[0] == 'S') b.Losses++;
        else if (outcome[0] == 'T') b.Wins++;
        else                        b.Expired++;
        b.SumMFE           += sig.MFETicks;
        b.SumMAE           += sig.MAETicks;
        b.SumStrength      += sig.Strength;
        b.SumBarsToResolve += barsElapsed;

        // Per-signal log line (suppressed during full recalculation)
        if (s.edgePerSignalLog && !sc.IsFullRecalculation)
            LogSignalOutcome(sc, sig, i, outcome);

        state.ResolvedSinceSummary++;
        if (s.edgeSummaryEveryN > 0
            && state.ResolvedSinceSummary >= s.edgeSummaryEveryN
            && !sc.IsFullRecalculation)
        {
            LogEdgeSummary(sc, state, s, "periodic");
        }

        it = state.OpenSignals.erase(it);
    }
}

// ============================================================
// HELPER: LogSignalOutcome (spec 11.4)
// ============================================================

static void LogSignalOutcome(
    SCStudyInterfaceRef sc, const TrackedSignal& sig, int resolveBar,
    const char* outcome)
{
    SCString when = sc.DateTimeToString(
        sc.BaseDateTimeIn[sig.SignalBarIndex],
        FLAG_DT_COMPLETE_DATETIME);

    SCString msg;
    msg.Format("SCOFA v" SCOFA_VERSION_STR " EDGE | %s-%s bar#%d %s | entry %s | "
        "%s in %d bars | MFE %.0ft MAE %.0ft | str %d",
        PatternCode(sig.Pattern),
        sig.IsBullish ? "BULL" : "BEAR",
        sig.SignalBarIndex,
        when.GetChars(),
        sc.FormatGraphValue(sig.EntryPrice, sc.ValueFormat).GetChars(),
        outcome,
        resolveBar - sig.SignalBarIndex,
        sig.MFETicks, sig.MAETicks,
        sig.Strength);
    sc.AddMessageToLog(msg, 0);
}

// ============================================================
// HELPER: LogEdgeSummary (spec 11.4)
// One line per non-empty (pattern, direction) bucket.
// ============================================================

static void LogEdgeSummary(
    SCStudyInterfaceRef sc, StudyState& state, const StudySettings& s,
    const char* reason)
{
    for (int p = 0; p < 3; ++p)
    {
        for (int d = 0; d < 2; ++d)
        {
            const EdgeBucket& b = state.Edge[p][d];
            if (b.Count == 0)
                continue;

            const double n = static_cast<double>(b.Count);
            SCString msg;
            msg.Format("SCOFA v" SCOFA_VERSION_STR " EDGE SUMMARY [%s] | %s-%s "
                "n=%d W %.0f%% L %.0f%% X %.0f%% | avgMFE %.1ft avgMAE %.1ft | "
                "avgBars %.1f | avgStr %.0f | tgt %dt stp %dt hz %d",
                reason,
                PatternCode(static_cast<PatternType>(p)),
                (d == 0) ? "BULL" : "BEAR",
                b.Count,
                100.0 * b.Wins    / n,
                100.0 * b.Losses  / n,
                100.0 * b.Expired / n,
                b.SumMFE / n, b.SumMAE / n,
                b.SumBarsToResolve / n,
                b.SumStrength / n,
                s.edgeTargetTicks, s.edgeStopTicks, s.edgeHorizon);
            sc.AddMessageToLog(msg, 0);
        }
    }
    state.ResolvedSinceSummary = 0;
}

// ============================================================
// HELPER: DrawMarkerText
// Fix 1: the line number is computed by the caller; with
// UTAM_ADD_OR_ADJUST Sierra Chart creates the drawing when the
// number is unknown and adjusts it otherwise — no capture-back.
// ============================================================

static void DrawMarkerText(
    SCStudyInterfaceRef sc, int barIndex, float price,
    COLORREF color, const SCString& text, int fontSize, int lineNumber)
{
    s_UseTool Tool;
    Tool.Clear();
    Tool.DrawingType                = DRAWING_TEXT;
    Tool.BeginDateTime              = sc.BaseDateTimeIn[barIndex];
    Tool.BeginValue                 = price;
    Tool.Text                       = text;
    Tool.FontSize                   = fontSize;
    Tool.FontBold                   = 1;
    Tool.Color                      = color;
    Tool.FontBackColor              = RGB(0, 0, 0);
    Tool.TransparentLabelBackground = 1;
    Tool.TextAlignment              = DT_CENTER | DT_VCENTER;
    Tool.AddMethod                  = UTAM_ADD_OR_ADJUST;
    Tool.LineNumber                 = lineNumber;

    sc.UseTool(Tool);
}

// ============================================================
// HELPER: CreateOrUpdateZoneRectangle
// DRAWING_RECTANGLEHIGHLIGHT respects explicit Begin/End
// datetimes (EXT_HIGHLIGHT would auto-extend to the right edge).
// ============================================================

static void CreateOrUpdateZoneRectangle(
    SCStudyInterfaceRef sc, SignalZone& zone,
    COLORREF fillColor, int transparency, int currentIndex)
{
    s_UseTool Tool;
    Tool.Clear();
    Tool.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
    Tool.BeginDateTime     = sc.BaseDateTimeIn[zone.SignalBarIndex];
    Tool.EndDateTime       = sc.BaseDateTimeIn[currentIndex];
    Tool.BeginValue        = zone.ZoneBottom;
    Tool.EndValue          = zone.ZoneTop;
    Tool.Color             = fillColor;
    Tool.SecondaryColor    = fillColor;
    Tool.TransparencyLevel = transparency;
    Tool.LineWidth         = 1;
    Tool.AddMethod         = UTAM_ADD_OR_ADJUST;
    Tool.LineNumber        = zone.RectLineNumber;

    sc.UseTool(Tool);
}

// ============================================================
// HELPER: UpdateExtendingRectangles
// Unchanged v1105 mechanics, generalized to SignalZone.
// ============================================================

static void UpdateExtendingRectangles(
    SCStudyInterfaceRef sc, std::deque<SignalZone>& zones,
    int currentIndex, int maxExtension,
    COLORREF bullFill, COLORREF bearFill, int transparency)
{
    for (SignalZone& zone : zones)
    {
        if (!zone.IsExtending)
            continue;
        if (zone.RectLineNumber == 0)
            continue;

        // Half-tick tolerance: price data is tick-quantized, so half a
        // tick can never produce a false positive (v1105 baseline).
        float barHigh   = sc.High[currentIndex];
        float barLow    = sc.Low[currentIndex];
        float tolerance = sc.TickSize * 0.5f;

        bool intersects = (barLow  <= zone.ZoneTop    + tolerance &&
                           barHigh >= zone.ZoneBottom - tolerance &&
                           currentIndex != zone.SignalBarIndex);

        bool hitLimit = (zone.ExtendedBars >= maxExtension);

        COLORREF fill = zone.IsBullish ? bullFill : bearFill;

        if (intersects || hitLimit)
        {
            // Freeze at this bar; IsExtending=false skips it from now on.
            zone.IsExtending = false;

            s_UseTool Tool;
            Tool.Clear();
            Tool.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
            Tool.BeginDateTime     = sc.BaseDateTimeIn[zone.SignalBarIndex];
            Tool.EndDateTime       = sc.BaseDateTimeIn[currentIndex];
            Tool.BeginValue        = zone.ZoneBottom;
            Tool.EndValue          = zone.ZoneTop;
            Tool.Color             = fill;
            Tool.SecondaryColor    = fill;
            Tool.TransparencyLevel = transparency;
            Tool.LineWidth         = 1;
            Tool.AddMethod         = UTAM_ADD_OR_ADJUST;
            Tool.LineNumber        = zone.RectLineNumber;
            sc.UseTool(Tool);
            continue;
        }

        // Still extending — advance EndDateTime to the current bar.
        zone.ExtendedBars++;

        s_UseTool Tool;
        Tool.Clear();
        Tool.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
        Tool.BeginDateTime     = sc.BaseDateTimeIn[zone.SignalBarIndex];
        Tool.EndDateTime       = sc.BaseDateTimeIn[currentIndex];
        Tool.BeginValue        = zone.ZoneBottom;
        Tool.EndValue          = zone.ZoneTop;
        Tool.Color             = fill;
        Tool.SecondaryColor    = fill;
        Tool.TransparencyLevel = transparency;
        Tool.LineWidth         = 1;
        Tool.AddMethod         = UTAM_ADD_OR_ADJUST;
        Tool.LineNumber        = zone.RectLineNumber;
        sc.UseTool(Tool);
    }
}

// ============================================================
// HELPER: HighlightBar
// ============================================================

static void HighlightBar(
    SCStudyInterfaceRef sc, int barIndex, COLORREF color,
    int transparency, int lineNumber)
{
    float top    = max(sc.Open[barIndex], sc.Close[barIndex]);
    float bottom = min(sc.Open[barIndex], sc.Close[barIndex]);

    s_UseTool Tool;
    Tool.Clear();
    Tool.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
    Tool.BeginDateTime     = sc.BaseDateTimeIn[barIndex];
    Tool.EndDateTime       = sc.BaseDateTimeIn[barIndex];
    Tool.BeginValue        = bottom;
    Tool.EndValue          = top;
    Tool.Color             = color;
    Tool.SecondaryColor    = color;
    Tool.TransparencyLevel = transparency;
    Tool.LineWidth         = 1;
    Tool.AddMethod         = UTAM_ADD_OR_ADJUST;
    Tool.LineNumber        = lineNumber;

    sc.UseTool(Tool);
}

// ============================================================
// END OF SCOFA-v1203.cpp
// ============================================================
