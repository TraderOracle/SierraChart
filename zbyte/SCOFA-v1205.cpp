// ============================================================
// SCOFA-v1205.cpp
// SCOF Absorption Detector [zbytedev]
// Version 1.2.05
//
// Detects four orderflow patterns from Volume at Price
// (footprint) data:
//   1. ABSORPTION  — aggressive flow absorbed at a bar extreme
//                    (v1204 refinements — see below)
//   2. SWEEP       — liquidity-pool sweep / stop-run trap:
//                    confirmed swing pools (incl. relative equal
//                    highs/lows), penetration + reclaim gate that
//                    separates sweeps from liquidity runs
//   3. SLINGSHOT   — sweep-trap sequence: Sweep -> displacement
//                    candle leaving a Fair Value Gap -> retrace tap
//                    of the FVG, optionally inside configured
//                    time windows
//   4. RUN         — liquidity run: pool penetrated with NO reclaim
//                    inside the window — breakout/continuation
//                    marker in the breakout direction (v1205,
//                    In:67, default OFF)
//
// Plus: statistical edge reporting (MFE/MAE, win rate) to the
// Message Log, per pattern and direction.
//
// v1205 sweep/slingshot refinements + double-quantity migration
// (docs/scofdoc-10-v1205-refinements.md):
//   Fix S1 — pool/sweep/slingshot state mutates on CLOSED bars only,
//            even with In:30=No (intrabar mode previously consumed
//            pools and advanced setups on partial bars: phantom /
//            missed signals, live-vs-recalc divergence)
//   Fix S2 — pool confirmation catches up over skipped bars
//            (session filter, lookback warm-up)
//   Fix S3 — two-bar sweep exhaustion bonus scored on the bar that
//            holds the sweep extreme (was: always the reclaim bar)
//   Fix S4 — raid scan falls back to deeper penetrated pools when
//            the nearest fails its conditions (spec "first match")
//   Fix S5 — a newer sweep supersedes only WAIT_DISPLACEMENT
//            setups; confirmed FVGs awaiting their tap survive
//   Fix S6 — pool capacity evicts the least-recently-touched pool
//            (was: oldest-created, which killed reinforced clusters)
//   Fix S7 — In:44=0 no longer accepts zero-height FVGs (1-tick floor)
//   Fix S8 — pools penetrated by a signaling reclaim bar are
//            consumed (no stale "fresh raid" on already-run stops)
//   R6  — trapped-delta graded into sweep strength at In:38=0
//         (share of pen-zone volume; the bonus was dead by default)
//   R8  — burst ratio baseline excludes the penetration zone's own
//         levels (no self-dilution)
//   R11 — slingshot delta-flip denominator uses the RAID bar's delta
//   In:66 — Sweep: Max Reclaim Bars (1-3, default 1 = v1204 exact)
//   In:67/68 — liquidity RUN signals (SG9-11, "RN"), default OFF
//   DBL — all VAP quantity handling migrated to double (fractional
//         quantity instruments; kills MSVC C4244 warnings)
//
// v1204 absorption refinements (docs/scofdoc-09-v1204-refinements.md):
//   Fix A1 — per-(bar, pattern, direction) output dedup; intrabar
//            re-detections no longer duplicate zones, edge
//            registrations or alerts; retracted mid-bar absorption
//            signals are withdrawn (drawings deleted)
//   Fix A2 — In:11 default 1 (was -1, which tripped its own
//            legacy-migration warning on fresh installs)
//   Fix A3 — prior-candle scan continues past the requirement so
//            the >=3 context bonus is reachable
//   Fix A4 — VAP streak walk enforces true 1-tick price adjacency
//            via PriceInTicks (array adjacency != tick adjacency
//            on sparse bars); a price gap breaks the streak
//   Fix A5 — diagonal imbalance requires a populated partner level
//            (no more auto-pass against untraded levels)
//   B7  — absorbed-volume intensity ratio feeds the strength score
//   B8  — tapering judged on MEDIAN per-level growth (outlier-robust)
//   B9  — ratio-based exhaustion option (In:62; 0 = strict v1105)
//   B10 — zone height selectable: fixed ticks or measured cluster (In:61)
//   B11 — optional relative-volume context gate (In:63/64)
//   B12 — absorption confluence: overlapping same-direction zones
//         merge and reinforce (+8 strength) instead of stacking
//   B13 — new-extreme tick tolerance (In:65; 0 = strict v1105)
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

#define SCOFA_VERSION_STR  "1.2.05"
#define SCOFA_VERSION_NUM  "1205"

SCDLLName("SCOFA-v" SCOFA_VERSION_NUM)

static const int   STUDY_VERSION      = 1205;   // must equal SCOFA_VERSION_NUM

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
// v1205: 4 patterns x 3 elements x 2 directions -> max slot index
// 3*6 + 2*2 + 1 = 23, so 24 slots per bar are required.
// Overflow-safe to ~85M bars.
static const int LINE_NUMBER_BASE     = 100000000; // clear of SC auto-assigned tool numbers
static const int LINE_SLOTS_PER_BAR   = 24;

// ============================================================
// ENUMS
// ============================================================

// PATTERN_COUNT sizes the per-pattern state arrays; keep it last.
enum PatternType
{
    PATTERN_ABSORPTION = 0,
    PATTERN_SWEEP      = 1,
    PATTERN_SLINGSHOT  = 2,
    PATTERN_RUN        = 3,     // liquidity run (v1205)
    PATTERN_COUNT      = 4
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

// All 69 inputs, cached in StudyState and re-read once per update
// cycle by LoadSettings().
struct StudySettings
{
    // Sweep/Run refinements (In:66-68, v1205)
    int  sweepMaxReclaimBars; bool enableRunSignals;  SCString markerTextRun;
    // Absorption core + filters (In:0-11)
    int  minOppLevels;       int  priorCandlesReq;    bool requireExhaustion;
    bool requireDeltaDiv;    bool requireCandle;      int  minVolAtExtreme;
    int  newExtremeN;        bool enableTapering;     int  minGrowthPct;
    bool enableDiag;         int  diagRatioPct;       int  counterDeltaMag;   // abs(), Fix 6
    // Absorption refinements (In:61-65, v1204)
    int  zoneHeightMode;     int  exhaustMaxOppPct;   int  minRelVolPct;
    int  relVolPeriod;       int  newExtremeTolTicks;
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
// v1205: volume quantities are double (fractional-quantity headers).
struct DetectionResult
{
    bool  Detected;          PatternType Pattern;    bool  IsBullish;
    int   Strength;          float MarkerPrice;
    float ZoneTop;           float ZoneBottom;       bool  WantZone;
    // absorption metrics
    int   ConsecLevels;      float MedGrowthPct;     float MaxDiagRatio;
    bool  ExhaustionPresent; bool  DeltaDivPresent;  int   PriorCandles;
    double VolAtExtreme;     float AbsorptionRatioPct;  // streak per-level vol vs bar avg (v1204)
    // sweep / run metrics. For PATTERN_RUN, CloseBackTicks holds the
    // distance CLOSED BEYOND the pool at window end (run conviction).
    int   PenetrationTicks;  float BurstRatioPct;    float WickPct;
    double ZoneDelta;        int   CloseBackTicks;   int   PoolTouchCount;
    double ZonePenVolume;    int   RaidBarIndex;     // pen-zone volume + raid bar (v1205)
    // slingshot metrics
    int   FvgTicks;          float BodyPct;          float DeltaFlipPct;
    int   TapDepthTicks;     int   SourceSweepStrength;
};

// Output of the absorption VAP streak walk (v1204 refactor of the
// former 8-out-param CountConsecutiveOppositeDelta signature).
struct StreakMetrics
{
    int    ConsecLevels;     // counter-delta levels, tick-contiguous from the extreme
    float  MedGrowthPct;     // MEDIAN per-level aggressive-volume growth (B8)
    float  MaxDiagRatio;     // best diagonal imbalance over valid pairs
    bool   TapOk;            // tapering verdict (honors Fix 2)
    bool   DiagOk;           // diagonal verdict
    int    GrowthPairs;      // adjacent growth pairs measured
    double AbsorbedVolume;   // total aggressive volume across the streak
    float  AbsorptionRatioPct; // (AbsorbedVolume/ConsecLevels) vs bar avg level volume x100 (B7)
};

// Replaces v1105 AbsorptionZone — adds the pattern tag.
// LastReinforceBar (B12, v1204): the bar of the latest confluence
// merge; like the signal bar itself, it does not freeze the zone.
struct SignalZone
{
    int   SignalBarIndex;    float ZoneTop;          float ZoneBottom;
    int   RectLineNumber;    PatternType Pattern;    bool  IsBullish;
    bool  IsExtending;       int   ExtendedBars;     int   LastReinforceBar;
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

// Reclaim deferral (spec 5.2.2 condition 2, generalized by In:66):
// the penetration bar closed beyond the pool; the next
// sweepMaxReclaimBars bars decide sweep (reclaim) vs liquidity run
// (still beyond at window end). v1205: tracks the extreme across
// the whole window and the bar holding it (Fix S3).
struct PendingSweep
{
    bool  Active;
    float PoolLevel;         int   PoolTouchCount;
    int   PenBar;            float PenExtreme;       // deepest Low (bull) / High (bear) so far
    int   ExtremeBar;        // bar holding PenExtreme
    bool  CondsPassed;       // wick/burst/counter-delta verdict from the pen bar
    int   PenetrationTicks;  float BurstRatioPct;    float WickPct;
    double ZoneDelta;        double ZonePenVolume;
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
    EdgeBucket   Edge[PATTERN_COUNT][2];     // [PatternType][0=bull, 1=bear]
    int   LastTrackedBarIndex;               // edge dedup guard (intrabar ticks)
    int   LastPoolUpdateBar;                 // pool-confirmation dedup guard
    int   LastSwingExamined;                 // Fix S2: last swing candidate s0 examined
    int   ResolvedSinceSummary;
    bool  ZoneCapacityWarned;                // Fix 4
    bool  LegacyIn11Notified;                // Fix 6
    int   LastSignalBar[PATTERN_COUNT][2];   // Fix A1: [pattern][0=bull,1=bear] output dedup
    bool  VapMissingWarned;                  // one-time no-VAP notice
    StudySettings Settings;                  // cached inputs (re-read per update cycle)
    bool  SettingsLoaded;

    StudyState()
    {
        // Session-scope log guards: initialized here, NOT in Reset(),
        // so each notice appears once per session — the Fix A7 reset
        // now actually runs on every full recalculation and would
        // otherwise re-arm them each time.
        ZoneCapacityWarned = false;
        LegacyIn11Notified = false;
        VapMissingWarned   = false;
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
        LastSwingExamined    = -1;
        ResolvedSinceSummary = 0;
        for (int p = 0; p < PATTERN_COUNT; ++p)
            for (int d = 0; d < 2; ++d)
                LastSignalBar[p][d] = -1;
        SettingsLoaded = false;              // forces a reload after any reset
    }
};

// ============================================================
// Fix 1: deterministic drawing line numbers.
// 4 patterns x 3 elements x 2 directions packed into 24
// slots per bar (max slot index 23). Overflow-safe to ~85M bars.
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
    int consecLevels, float medGrowthPct, float maxDiagRatio,
    bool exhaustionPresent, bool deltaDivPresent, int priorCandles,
    int minOppLevels, int minGrowthPct, int diagRatioPct,
    float absorptionRatioPct);

static bool CheckExhaustionAtExtreme(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    int maxOppPct, double& out_volAtExtreme);

static bool IsNewExtreme(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    int lookbackN, int tolTicks);

static void ScanAbsorptionStreak(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    const StudySettings& s, int maxLevels, StreakMetrics& out);

static void RetractAbsorptionIntrabar(
    SCStudyInterfaceRef sc, StudyState& state,
    const DetectionResult& r, bool isBullish);

static void UpdateLiquidityPools(
    SCStudyInterfaceRef sc, const StudySettings& s, StudyState& state);

static DetectionResult EvaluateSweep(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    const StudySettings& s, StudyState& state, DetectionResult& out_run);

static bool EvaluateSweepRaidBar(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish, float poolLevel,
    const StudySettings& s,
    int& out_penTicks, float& out_burstPct, float& out_wickPct,
    double& out_zoneDelta, double& out_penZoneVol);

static int CalculateSweepStrength(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    const DetectionResult& r, const StudySettings& s, int extremeBarIndex);

static int CalculateRunStrength(
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
    SCStudyInterfaceRef sc, DetectionResult r,
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
    SCSubgraphRef SG_BullRun       = sc.Subgraph[9];    // v1205
    SCSubgraphRef SG_BearRun       = sc.Subgraph[10];
    SCSubgraphRef SG_RunStrength   = sc.Subgraph[11];

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
    // New: absorption refinements (In:61-65, v1204)
    SCInputRef In_ZoneHeightMode     = sc.Input[61];
    SCInputRef In_ExhaustMaxOppPct   = sc.Input[62];
    SCInputRef In_MinRelVolPct       = sc.Input[63];
    SCInputRef In_RelVolPeriod       = sc.Input[64];
    SCInputRef In_NewExtremeTol      = sc.Input[65];
    // New: sweep/run refinements (In:66-68, v1205)
    SCInputRef In_MaxReclaimBars     = sc.Input[66];
    SCInputRef In_EnableRunSignals   = sc.Input[67];
    SCInputRef In_MarkerTextRun      = sc.Input[68];

    // ----------------------------------------------------------
    // SET DEFAULTS
    // ----------------------------------------------------------
    if (sc.SetDefaults)
    {
        sc.GraphName        = "SCOF Absorption Detector [zbytedev]";
        sc.StudyDescription =
            "Detects orderflow Absorption, liquidity-pool Sweeps, "
            "Slingshot (sweep -> displacement FVG -> tap) and optional "
            "liquidity Run (no-reclaim breakout) patterns using "
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

        // v1205: liquidity runs (In:67, default OFF)
        SG_BullRun.Name          = "Bullish Run";
        SG_BullRun.DrawStyle     = DRAWSTYLE_POINT;
        SG_BullRun.PrimaryColor  = RGB(255, 215, 0);
        SG_BullRun.LineWidth     = 3;
        SG_BullRun.DrawZeros     = 0;

        SG_BearRun.Name          = "Bearish Run";
        SG_BearRun.DrawStyle     = DRAWSTYLE_POINT;
        SG_BearRun.PrimaryColor  = RGB(178, 34, 34);
        SG_BearRun.LineWidth     = 3;
        SG_BearRun.DrawZeros     = 0;

        SG_RunStrength.Name         = "Run Strength (0-100)";
        SG_RunStrength.DrawStyle    = DRAWSTYLE_IGNORE;
        SG_RunStrength.PrimaryColor = RGB(128, 128, 128);
        SG_RunStrength.DrawZeros    = 0;

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
        // Fix A2 (v1204): default 1 (was -1, which tripped the legacy
        // warning on every fresh install; effective value unchanged).
        In_CounterDeltaMag.Name = "Min Counter-Delta Magnitude (0=Any)";
        In_CounterDeltaMag.SetInt(1);
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

        // Window hours are in the CHART's timezone (v1205 ruling —
        // §15.2 item 1 resolved: no auto-conversion); defaults assume
        // US Eastern: windows 03-04 / 10-11 / 14-15. Adjust the hours
        // per chart if your chart uses a different timezone.
        In_SlingUseWindows.Name = "Slingshot: Restrict to Time Windows";
        In_SlingUseWindows.SetYesNo(0);

        In_SlingWin1Hour.Name = "Slingshot: Window 1 Start Hour (Chart TZ)";
        In_SlingWin1Hour.SetInt(3);
        In_SlingWin1Hour.SetIntLimits(0, 23);

        In_SlingWin2Hour.Name = "Slingshot: Window 2 Start Hour (Chart TZ)";
        In_SlingWin2Hour.SetInt(10);
        In_SlingWin2Hour.SetIntLimits(0, 23);

        In_SlingWin3Hour.Name = "Slingshot: Window 3 Start Hour (Chart TZ)";
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

        // ---- Group 10: Absorption Refinements (In:61-65, v1204) ----
        // Every default reproduces the v1203 behavior exactly.
        In_ZoneHeightMode.Name = "Absorption Zone Height";
        In_ZoneHeightMode.SetCustomInputStrings("Fixed Ticks;Absorption Cluster");
        In_ZoneHeightMode.SetCustomInputIndex(0);

        In_ExhaustMaxOppPct.Name = "Exhaustion: Max Opposing Volume at Extreme (%) (0=Strict)";
        In_ExhaustMaxOppPct.SetInt(0);
        In_ExhaustMaxOppPct.SetIntLimits(0, 100);

        In_MinRelVolPct.Name = "Absorption: Min Bar Volume vs Average (%) (0=Off)";
        In_MinRelVolPct.SetInt(0);
        In_MinRelVolPct.SetIntLimits(0, 1000);

        In_RelVolPeriod.Name = "Absorption: Volume Average Period (Bars)";
        In_RelVolPeriod.SetInt(20);
        In_RelVolPeriod.SetIntLimits(5, 500);

        // t=0: strict v1105 rule (an equal prior extreme disqualifies).
        // t=1: equal extremes tolerated (double-bottom/top retests count).
        // t=n: prior extremes up to n-1 ticks beyond the bar extreme tolerated.
        In_NewExtremeTol.Name = "New Extreme Tolerance (Ticks, 0=Strict)";
        In_NewExtremeTol.SetInt(0);
        In_NewExtremeTol.SetIntLimits(0, 10);

        // ---- Group 11: Sweep/Run Refinements (In:66-68, v1205) ----
        // Defaults reproduce v1204 behavior exactly.
        In_MaxReclaimBars.Name = "Sweep: Max Reclaim Bars (1=Classic)";
        In_MaxReclaimBars.SetInt(1);
        In_MaxReclaimBars.SetIntLimits(1, 3);

        In_EnableRunSignals.Name = "Enable Liquidity Run Signals";
        In_EnableRunSignals.SetYesNo(0);

        In_MarkerTextRun.Name = "Marker Text (Run)";
        In_MarkerTextRun.SetString("RN");

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

    // v1204: a silent return here previously left users guessing why
    // the study produced nothing on a non-footprint chart.
    if (sc.VolumeAtPriceForBars == nullptr)
    {
        if (!state.VapMissingWarned)
        {
            sc.AddMessageToLog(
                "SCOFA v" SCOFA_VERSION_STR ": Volume at Price data is not "
                "available on this chart. Apply the study to a Numbers Bars / "
                "footprint chart with VAP data enabled. No signals will be "
                "produced.", 1);
            state.VapMissingWarned = true;
        }
        return;
    }

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
    // FULL RECALCULATION RESET — once, at the first processed bar.
    // Sierra Chart auto-deletes ACS drawings on full recalc;
    // we only reset our internal tracking.
    // v1204: bar 1, not bar 0 — the sc.Index < 1 guard above meant
    // the v1203 bar-0 reset was unreachable, so zones/edge state
    // silently survived every full recalculation.
    // ----------------------------------------------------------
    if (sc.IsFullRecalculation && sc.Index == 1)
        state.Reset();

    // ----------------------------------------------------------
    // LOOKBACK GUARD
    // ----------------------------------------------------------
    const int lookback = In_Lookback.GetInt();
    if (lookback > 0 && (sc.ArraySize - sc.Index) > lookback)
        return;

    // ----------------------------------------------------------
    // SETTINGS — cached in StudyState (v1204). Reloaded on every
    // bar of a full recalculation (input changes always trigger
    // one, and bar 0 never reaches this line, so the cache would
    // otherwise serve pre-change values for the whole recalc) and
    // once per live update cycle, where the caching pays off.
    // ----------------------------------------------------------
    if (!state.SettingsLoaded || sc.IsFullRecalculation
        || sc.Index == sc.UpdateStartIndex)
    {
        LoadSettings(sc, state.Settings, state);
        state.SettingsLoaded = true;
    }
    const StudySettings& s = state.Settings;

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
    // Fix S1 (v1205): pool/sweep/slingshot state mutates on CLOSED
    // bars only, even with In:30=No. Those detectors consume pools
    // and advance state machines irreversibly; evaluating them on a
    // partial bar produced phantom sweeps (non-retractable), missed
    // signals (pools consumed by half-formed bars) and a two-bar
    // reclaim gate that self-destructed intrabar. Absorption keeps
    // its intrabar detect+retract behavior. With In:30=Yes this gate
    // is always true here (the global bar-close gate above returned
    // already), so closed-bar behavior is unchanged.
    // ----------------------------------------------------------
    const bool barClosed =
        (sc.GetBarHasClosedStatus(sc.Index) == BHCS_BAR_HAS_CLOSED);

    // ----------------------------------------------------------
    // LIQUIDITY POOLS — confirm swings through Index - swingStrength
    // ----------------------------------------------------------
    if (s.enableSweep && barClosed)
        UpdateLiquidityPools(sc, s, state);

    // ----------------------------------------------------------
    // DETECTION
    // ----------------------------------------------------------
    DetectionResult bullAbs = EvaluateAbsorption(sc, sc.Index, true,  s);
    DetectionResult bearAbs = EvaluateAbsorption(sc, sc.Index, false, s);

    // Fix A1 (v1204): a mid-bar absorption whose conditions no longer
    // hold on a later tick of the same bar is withdrawn. Only
    // absorption is retractable — its evaluator is stateless; sweep
    // and slingshot detection consume pools/setups irreversibly, so
    // those signals are emit-once (deduped in OutputSignal).
    RetractAbsorptionIntrabar(sc, state, bullAbs, true);
    RetractAbsorptionIntrabar(sc, state, bearAbs, false);

    // A run fires in the direction OPPOSITE the sweep path that
    // detects it: a run through a low pool is a bearish breakout,
    // so the bull-sweep path yields the BEARISH run and vice versa.
    DetectionResult bullSweep; memset(&bullSweep, 0, sizeof(bullSweep));
    DetectionResult bearSweep; memset(&bearSweep, 0, sizeof(bearSweep));
    DetectionResult bearRun;   memset(&bearRun,   0, sizeof(bearRun));
    DetectionResult bullRun;   memset(&bullRun,   0, sizeof(bullRun));
    if (s.enableSweep && barClosed)
    {
        bullSweep = EvaluateSweep(sc, sc.Index, true,  s, state, bearRun);
        bearSweep = EvaluateSweep(sc, sc.Index, false, s, state, bullRun);
    }

    DetectionResult bullSling; memset(&bullSling, 0, sizeof(bullSling));
    DetectionResult bearSling; memset(&bearSling, 0, sizeof(bearSling));
    if (s.enableSling && barClosed)
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
    if (s.enableRunSignals)
    {
        if (bullRun.Detected) OutputSignal(sc, bullRun, s, state);
        if (bearRun.Detected) OutputSignal(sc, bearRun, s, state);
    }

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
// Single point reading all 69 inputs (extensibility refactor).
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

    // Absorption refinements (In:61-65, v1204)
    s.zoneHeightMode     = sc.Input[61].GetIndex();
    s.exhaustMaxOppPct   = sc.Input[62].GetInt();
    s.minRelVolPct       = sc.Input[63].GetInt();
    s.relVolPeriod       = sc.Input[64].GetInt();
    s.newExtremeTolTicks = sc.Input[65].GetInt();

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

    // Sweep/Run refinements (In:66-68, v1205)
    s.sweepMaxReclaimBars = sc.Input[66].GetInt();
    s.enableRunSignals    = (sc.Input[67].GetYesNo() != 0);
    s.markerTextRun = sc.Input[68].GetString();
    if (s.markerTextRun.GetLength() == 0)     s.markerTextRun = "RN";
    else if (s.markerTextRun.GetLength() > 4) s.markerTextRun = s.markerTextRun.Left(4);

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
// halves the maintenance surface). v1204: relative-volume gate
// (B11), new-extreme tolerance (B13), ratio exhaustion (B9),
// deeper prior-candle scan (Fix A3), tick-adjacent streak walk
// (Fix A4/A5, B7, B8) and cluster zone height (B10).
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

    // 2b. Relative-volume context gate (B11, In:63/64; 0 = off).
    // Absorption on a dead bar is rarely tradeable.
    if (s.minRelVolPct > 0)
    {
        const int n = min(s.relVolPeriod, barIndex);
        if (n > 0)
        {
            double sum = 0.0;
            for (int b = barIndex - n; b < barIndex; ++b)
                sum += sc.Volume[b];
            const double avg = sum / static_cast<double>(n);
            if (avg > 0.0 &&
                static_cast<double>(sc.Volume[barIndex])
                    < avg * (static_cast<double>(s.minRelVolPct) / 100.0))
                return result;
        }
    }

    // 3. New extreme filter (B13: optional tick tolerance)
    if (!IsNewExtreme(sc, barIndex, isBullish, s.newExtremeN, s.newExtremeTolTicks))
        return result;

    // 4. Prior counter-direction candles (consecutive, immediately prior).
    // Fix A3: the scan continues past the requirement (up to 10 bars, or
    // the requirement itself if larger) so the >=3 strength bonus is
    // reachable and reflects actual context depth even when In:20 is low
    // or off.
    int priorCandlesFound = 0;
    {
        const int maxScan = max(s.priorCandlesReq, 10);
        for (int i = 1; i <= maxScan && (barIndex - i) >= 0; ++i)
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
    }
    if (s.priorCandlesReq > 0 && priorCandlesFound < s.priorCandlesReq)
        return result;

    // 5. Exhaustion at extreme (Finished Business; B9 ratio option)
    double volAtExtreme = 0.0;
    bool exhaustionPresent = CheckExhaustionAtExtreme(
        sc, barIndex, isBullish, s.exhaustMaxOppPct, volAtExtreme);
    if (s.requireExhaustion && !exhaustionPresent)
        return result;

    // 6. Minimum volume at extreme
    if (s.minVolAtExtreme > 0 && volAtExtreme < static_cast<double>(s.minVolAtExtreme))
        return result;

    // 7. Tick-contiguous opposite-delta streak + tapering + diagonal
    StreakMetrics m;
    ScanAbsorptionStreak(sc, barIndex, isBullish, s,
        s.minOppLevels + 10,   // scan a bit deeper than required
        m);

    if (m.ConsecLevels < s.minOppLevels)
        return result;
    if (s.enableTapering && !m.TapOk)
        return result;
    if (s.enableDiag && !m.DiagOk)
        return result;

    // All filters passed — signal detected
    result.Detected            = true;
    result.ConsecLevels        = m.ConsecLevels;
    result.MedGrowthPct        = m.MedGrowthPct;
    result.MaxDiagRatio        = m.MaxDiagRatio;
    result.ExhaustionPresent   = exhaustionPresent;
    result.DeltaDivPresent     = deltaDivPresent;
    result.PriorCandles        = priorCandlesFound;
    result.VolAtExtreme        = volAtExtreme;
    result.AbsorptionRatioPct  = m.AbsorptionRatioPct;
    result.WantZone            = true;

    // Marker at the extreme. Zone height (B10, In:61):
    //   Fixed Ticks        — zoneWidthTicks band (v1105 baseline)
    //   Absorption Cluster — the measured streak extent, so the drawn
    //                        zone covers exactly the absorbed levels
    const float zoneW = (s.zoneHeightMode == 1)
        ? static_cast<float>(m.ConsecLevels) * sc.TickSize
        : static_cast<float>(s.zoneWidthTicks) * sc.TickSize;
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
        m.ConsecLevels, m.MedGrowthPct, m.MaxDiagRatio,
        exhaustionPresent, deltaDivPresent, priorCandlesFound,
        s.minOppLevels, s.minGrowthPct, s.diagRatioPct,
        m.AbsorptionRatioPct);

    return result;
}

// ============================================================
// HELPER: CheckExhaustionAtExtreme
// Bullish: opposing AskVol at the lowest price level within
// maxOppPct % of the dominant BidVol. Bearish is the mirror.
// B9 (v1204): maxOppPct = 0 reproduces the strict v1105 rule
// (exactly zero opposing volume); a small percentage stops one
// stray contract at the extreme from killing the check.
// v1205 (DBL): quantities are double — VAP volumes support
// fractional quantities on newer Sierra Chart headers.
// ============================================================

static bool CheckExhaustionAtExtreme(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    int maxOppPct, double& out_volAtExtreme)
{
    out_volAtExtreme = 0.0;
    if (sc.VolumeAtPriceForBars == nullptr)
        return false;

    int numLevels = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(barIndex);
    if (numLevels == 0)
        return false;

    const s_VolumeAtPriceV2* p_VAP = nullptr;

    double dominant, opposing;
    if (isBullish)
    {
        if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(barIndex, 0, &p_VAP))
            return false;
        dominant = p_VAP->BidVolume;
        opposing = p_VAP->AskVolume;
    }
    else
    {
        if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(barIndex, numLevels - 1, &p_VAP))
            return false;
        dominant = p_VAP->AskVolume;
        opposing = p_VAP->BidVolume;
    }

    out_volAtExtreme = dominant;
    if (dominant <= 0.0)
        return false;

    return (opposing * 100.0 <= static_cast<double>(maxOppPct) * dominant);
}

// ============================================================
// HELPER: IsNewExtreme
// B13 (v1204): tolTicks generalizes the strict v1105 rule.
//   t=0: any prior low <= bar low disqualifies (v1105 exact)
//   t=1: equal prior lows tolerated (double-bottom retests count)
//   t=n: prior lows up to n-1 ticks below the bar low tolerated
// Bearish is the mirror. Prices are tick-quantized, so the
// half-tick guard cannot produce a false verdict.
// ============================================================

static bool IsNewExtreme(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    int lookbackN, int tolTicks)
{
    if (lookbackN <= 0)
        return true; // disabled

    int startIdx = max(0, barIndex - lookbackN);

    const float tolF     = static_cast<float>(tolTicks) * sc.TickSize;
    const float halfTick = sc.TickSize * 0.5f;

    if (isBullish)
    {
        // Disqualify when a prior low sits at or below (barLow - tol).
        float limit = sc.Low[barIndex] - tolF + halfTick;
        for (int i = startIdx; i < barIndex; ++i)
            if (sc.Low[i] < limit)
                return false;
        return true;
    }
    else
    {
        float limit = sc.High[barIndex] + tolF - halfTick;
        for (int i = startIdx; i < barIndex; ++i)
            if (sc.High[i] > limit)
                return false;
        return true;
    }
}

// ============================================================
// HELPER: ScanAbsorptionStreak
// v1204 rewrite of CountConsecutiveOppositeDelta. Retains
// Fix 2 (tapering fails when unverifiable), Fix 3 (FP_EPSILON)
// and Fix 6 (counterDeltaMag magnitude semantics: bullish counts
// levels with delta <= -mag, bearish delta >= +mag). New:
//   Fix A4 — the per-bar VAP array only contains traded prices,
//            so array adjacency != tick adjacency on sparse bars.
//            The walk verifies 1-tick contiguity via PriceInTicks;
//            a price gap (or untraded level) breaks the streak —
//            an absorption wall with holes in it is not a wall.
//   Fix A5 — a diagonal pair only counts when the partner level
//            is exactly 1 tick beyond AND populated; no more
//            auto-pass against untraded levels.
//   B7     — accumulates AbsorbedVolume across the streak and the
//            intensity ratio vs the bar's average level volume.
//   B8     — tapering judged on the MEDIAN per-level growth; a
//            single outlier pair no longer carries the filter.
// ============================================================

static void ScanAbsorptionStreak(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    const StudySettings& s, int maxLevels, StreakMetrics& out)
{
    out.ConsecLevels       = 0;
    out.MedGrowthPct       = 0.0f;
    out.MaxDiagRatio       = 0.0f;
    out.TapOk              = !s.enableTapering;  // passes by default when disabled
    out.DiagOk             = !s.enableDiag;
    out.GrowthPairs        = 0;
    out.AbsorbedVolume     = 0.0;
    out.AbsorptionRatioPct = 0.0f;

    if (sc.VolumeAtPriceForBars == nullptr)
        return;

    int numLevels = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(barIndex);
    if (numLevels == 0)
        return;

    const s_VolumeAtPriceV2* p_VAP     = nullptr;
    const s_VolumeAtPriceV2* p_VAPNext = nullptr;

    // Whole-bar per-level context for the B7 intensity ratio
    double barVolume       = 0.0;
    int    populatedLevels = 0;
    for (int lv = 0; lv < numLevels; ++lv)
    {
        if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(barIndex, lv, &p_VAP))
            continue;
        double lvVol = static_cast<double>(p_VAP->BidVolume)
                     + static_cast<double>(p_VAP->AskVolume);
        if (lvVol > 0.0)
        {
            populatedLevels++;
            barVolume += lvVol;
        }
    }

    int scanDepth = min(numLevels, maxLevels);

    // Growth samples for the median; scanDepth is bounded by
    // minOppLevels(<=20)+10, so 64 slots can never overflow.
    float growths[64];
    int   nGrowth = 0;

    int   consecCount    = 0;
    float prevAggVol     = 0.0f;
    bool  diagMet        = false;
    int   prevPriceTicks = 0;
    bool  havePrev       = false;

    for (int i = 0; i < scanDepth; ++i)
    {
        int levelIdx = isBullish ? i : (numLevels - 1 - i);

        if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(barIndex, levelIdx, &p_VAP))
            break;   // unreadable element mid-walk — data unreliable

        const double bidVol = p_VAP->BidVolume;
        const double askVol = p_VAP->AskVolume;

        // Fix A4: an untraded price inside the wall is a hole
        if (bidVol == 0.0 && askVol == 0.0)
            break;

        // Fix A4: the next counted level must sit exactly 1 tick
        // beyond the previous one on the price grid
        const int priceTicks = p_VAP->PriceInTicks;
        if (havePrev)
        {
            const int expected = isBullish ? (prevPriceTicks + 1)
                                           : (prevPriceTicks - 1);
            if (priceTicks != expected)
                break;
        }

        // Bullish: aggressive side = BidVolume (sellers hitting bids)
        // Bearish: aggressive side = AskVolume (buyers lifting offers)
        const double aggVol = isBullish ? bidVol : askVol;

        const double levelDelta = askVol - bidVol;

        bool isCounterDelta = isBullish
            ? (levelDelta <= -static_cast<double>(s.counterDeltaMag))
            : (levelDelta >=  static_cast<double>(s.counterDeltaMag));

        if (!isCounterDelta)
            break;   // streak broken

        consecCount++;
        out.AbsorbedVolume += aggVol;

        // Progressive volume tapering: pairs are tick-adjacent by
        // construction (the A4 contiguity rule above)
        if (s.enableTapering && prevAggVol > 0.0f && aggVol > 0.0 && nGrowth < 64)
        {
            growths[nGrowth++] =
                ((static_cast<float>(aggVol) - prevAggVol) / prevAggVol) * 100.0f;
        }
        if (aggVol > 0.0)
            prevAggVol = static_cast<float>(aggVol);

        // Diagonal imbalance (Fix A5):
        // Bullish: BidVol[level] vs AskVol at price+1 tick
        // Bearish: AskVol[level] vs BidVol at price-1 tick
        // The partner must be the adjacent VAP element, exactly 1 tick
        // beyond, and populated on the passive side.
        if (s.enableDiag)
        {
            int nextLevelIdx = isBullish ? (levelIdx + 1) : (levelIdx - 1);
            if (nextLevelIdx >= 0 && nextLevelIdx < numLevels &&
                sc.VolumeAtPriceForBars->GetVAPElementAtIndex(barIndex, nextLevelIdx, &p_VAPNext))
            {
                const int expectedNext = isBullish ? (priceTicks + 1)
                                                   : (priceTicks - 1);
                const double nextPassVol = isBullish ? p_VAPNext->AskVolume
                                                     : p_VAPNext->BidVolume;
                if (p_VAPNext->PriceInTicks == expectedNext && nextPassVol > 0.0)
                {
                    float diagRatio = (static_cast<float>(aggVol)
                                     / static_cast<float>(nextPassVol)) * 100.0f;

                    if (diagRatio > out.MaxDiagRatio)
                        out.MaxDiagRatio = diagRatio;

                    if (diagRatio + FP_EPSILON >= static_cast<float>(s.diagRatioPct))  // Fix 3
                        diagMet = true;
                }
            }
        }

        prevPriceTicks = priceTicks;
        havePrev       = true;
    }

    out.ConsecLevels = consecCount;
    out.GrowthPairs  = nGrowth;

    // B8: median per-level growth (insertion sort — n <= 64)
    if (nGrowth > 0)
    {
        for (int a = 1; a < nGrowth; ++a)
        {
            float v = growths[a];
            int   b = a - 1;
            while (b >= 0 && growths[b] > v)
            {
                growths[b + 1] = growths[b];
                --b;
            }
            growths[b + 1] = v;
        }
        out.MedGrowthPct = (nGrowth % 2 == 1)
            ? growths[nGrowth / 2]
            : 0.5f * (growths[nGrowth / 2 - 1] + growths[nGrowth / 2]);
    }

    // Fix 2: an enabled tapering filter is UNVERIFIABLE (and fails)
    // when no adjacent growth pair existed (single populated level).
    if (s.enableTapering)
        out.TapOk = (nGrowth > 0)
                 && (out.MedGrowthPct + FP_EPSILON >= static_cast<float>(s.minGrowthPct));

    if (s.enableDiag)
        out.DiagOk = diagMet;

    // B7: absorbed-volume intensity — the streak's average per-level
    // aggressive volume vs the bar's average per-level total volume.
    // 100% = ordinary participation; 200%+ = a genuine wall.
    if (consecCount > 0 && populatedLevels > 0 && barVolume > 0.0)
    {
        double avgLevelVol  = barVolume / static_cast<double>(populatedLevels);
        double avgStreakVol = out.AbsorbedVolume / static_cast<double>(consecCount);
        out.AbsorptionRatioPct = static_cast<float>((avgStreakVol / avgLevelVol) * 100.0);
    }
}

// ============================================================
// HELPER: CalculateAbsorptionStrength
// v1204 rebalance (scofdoc-09 §4). The v1105 scale compressed:
// default-required filters auto-granted +20 and two bonuses were
// unreachable with default settings. New budget (max 100):
//   25 base | 15 levels | 15 absorbed-volume intensity |
//   10 tapering (median) | 10 diagonal | 10 exhaustion |
//   10 delta divergence | 5 prior candles
// (+8 confluence applied in OutputSignal, clamped at 100)
// ============================================================

static int CalculateAbsorptionStrength(
    int consecLevels, float medGrowthPct, float maxDiagRatio,
    bool exhaustionPresent, bool deltaDivPresent, int priorCandles,
    int minOppLevels, int minGrowthPct, int diagRatioPct,
    float absorptionRatioPct)
{
    float score = 25.0f; // base for passing all required filters

    float levelBonus = static_cast<float>(consecLevels - minOppLevels) * 3.0f;
    score += min(15.0f, max(0.0f, levelBonus));

    // B7: absorbed-volume intensity — full 15 points at 250% of the
    // bar's average level volume
    if (absorptionRatioPct > 0.0f)
        score += min(15.0f, max(0.0f, (absorptionRatioPct - 100.0f) / 10.0f));

    if (medGrowthPct > 0.0f)
    {
        float tapBonus = (medGrowthPct - static_cast<float>(minGrowthPct)) / 3.0f;
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
// Confirms swing points with a k-bar lag (no repaint — Fix S1
// guarantees only closed bars reach this function) and maintains
// the pool deque: equal-level clustering, expiry, per-side
// capacity.
// Fix S2 (v1205): every unexamined candidate up to i-k is
// scanned, not just i-k itself — bars skipped by the session
// filter or the lookback warm-up no longer permanently drop
// swing confirmations (the scan uses closed data only, so it is
// deterministic and identical on recalc).
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

    // ---- Confirm every unexamined swing candidate through i - k ----
    const int firstCand = max(k, state.LastSwingExamined + 1);
    const int lastCand  = i - k;

    for (int s0 = firstCand; s0 <= lastCand; ++s0)
    {
        // A pool born from this candidate would already be expired
        if ((i - s0) > s.poolLookbackBars)
            continue;

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
    }

    if (lastCand > state.LastSwingExamined)
        state.LastSwingExamined = lastCand;

    // ---- Per-side capacity (Fix S6): evict the least-recently-
    // touched pool of the heavy side (tie: fewest touches). The
    // v1204 rule dropped the oldest-CREATED pool, which deleted
    // recently-reinforced equal-level clusters — the strongest kind
    // — before younger single-touch pools.
    int lows = 0, highs = 0;
    for (const LiquidityPool& p : state.Pools)
        (p.IsLowPool ? lows : highs)++;

    while (lows > MAX_POOLS_PER_SIDE || highs > MAX_POOLS_PER_SIDE)
    {
        const bool dropLow = (lows > MAX_POOLS_PER_SIDE);
        std::deque<LiquidityPool>::iterator victim = state.Pools.end();
        for (std::deque<LiquidityPool>::iterator it = state.Pools.begin();
             it != state.Pools.end(); ++it)
        {
            if (it->IsLowPool != dropLow)
                continue;
            if (victim == state.Pools.end()
                || it->LastTouchBar < victim->LastTouchBar
                || (it->LastTouchBar == victim->LastTouchBar
                    && it->TouchCount < victim->TouchCount))
                victim = it;
        }
        if (victim == state.Pools.end())
            break;   // defensive: count said overflow but none found
        state.Pools.erase(victim);
        (dropLow ? lows : highs)--;
    }
}

// ============================================================
// HELPER: EvaluateSweepRaidBar (spec 5.2.2 conditions 3-6)
// Verdict on the penetration ("raid") bar: rejection wick,
// VAP volume burst into the penetration zone, trapped
// counter-delta. Returns true when all enabled checks pass.
// v1205: DBL quantity migration; R8 — the burst baseline
// excludes the penetration zone's own levels (they no longer
// dilute the average they are compared against); the pen-zone
// total volume is exported for the R6 trapped-share grading.
// ============================================================

static bool EvaluateSweepRaidBar(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish, float poolLevel,
    const StudySettings& s,
    int& out_penTicks, float& out_burstPct, float& out_wickPct,
    double& out_zoneDelta, double& out_penZoneVol)
{
    out_penTicks = 0; out_burstPct = 0.0f; out_wickPct = 0.0f;
    out_zoneDelta = 0.0; out_penZoneVol = 0.0;

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
    double  zoneDelta = 0.0;

    for (int lv = 0; lv < numLevels; ++lv)
    {
        if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(barIndex, lv, &p_VAP))
            continue;

        const double bidVol = p_VAP->BidVolume;
        const double askVol = p_VAP->AskVolume;
        if (bidVol == 0.0 && askVol == 0.0)
            continue;

        double lvVol = bidVol + askVol;
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
            zoneDelta += askVol - bidVol;
        }
    }
    out_zoneDelta   = zoneDelta;
    out_penZoneVol  = volPen;

    // Condition 4: burst — penetration-zone per-level volume vs the
    // per-level average of the REST of the bar (R8). Falls back to
    // the whole-bar average when the bar traded entirely beyond the
    // pool (no baseline levels remain).
    if (nPen < 1 || nAll < 1 || volAll <= 0.0)
        return false;
    double baseVol; int baseN;
    if (nAll > nPen) { baseVol = volAll - volPen; baseN = nAll - nPen; }
    else             { baseVol = volAll;          baseN = nAll;        }
    if (baseN < 1 || baseVol <= 0.0)
        return false;
    float avgLevelVol = static_cast<float>(baseVol / baseN);
    float avgPenVol   = static_cast<float>(volPen / nPen);
    out_burstPct = (avgPenVol / avgLevelVol) * 100.0f;
    if (out_burstPct + FP_EPSILON < static_cast<float>(s.sweepBurstPct))
        return false;

    // Condition 5: trapped counter-delta in the penetration zone (optional)
    if (s.sweepMinZoneDelta > 0)
    {
        if (isBullish  && !(zoneDelta <= -static_cast<double>(s.sweepMinZoneDelta))) return false;
        if (!isBullish && !(zoneDelta >=  static_cast<double>(s.sweepMinZoneDelta))) return false;
    }

    return true;
}

// ============================================================
// HELPER: EvaluateSweep (spec 5.2.2, v1205 rework)
// 1. Resolves/extends the pending reclaim window (In:66 bars):
//    reclaim inside the window -> SWEEP at that bar; still closed
//    beyond the pool at window end -> liquidity RUN (out_run, in
//    the breakout direction — opposite this evaluation path);
//    middle-ground close at window end -> silent consumption.
// 2. Scans pools for a fresh raid: every penetrated pool is
//    consumed; candidates are evaluated NEAREST-FIRST with
//    fallback to deeper pools when a nearer one fails its raid
//    conditions (Fix S4, spec "first match").
// Fix S8: pools penetrated by a bar that emits a pend-resolved
// sweep are still consumed (no stale raids later).
// Fix S3: the exhaustion bonus is scored on the bar holding the
// sweep extreme (pend.ExtremeBar), not blindly the reclaim bar.
// ============================================================

static DetectionResult EvaluateSweep(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    const StudySettings& s, StudyState& state, DetectionResult& out_run)
{
    DetectionResult result;
    memset(&result, 0, sizeof(result));
    result.Pattern   = PATTERN_SWEEP;
    result.IsBullish = isBullish;

    memset(&out_run, 0, sizeof(out_run));
    out_run.Pattern   = PATTERN_RUN;
    out_run.IsBullish = !isBullish;   // run continues AWAY from the raided pool

    const float T        = sc.TickSize;
    const float halfTick = T * 0.5f;
    const float penReq   = static_cast<float>(s.sweepMinPenTicks)   * T;
    const float cbReq    = static_cast<float>(s.sweepCloseBackTicks) * T;
    const float close    = sc.Close[barIndex];

    PendingSweep& pend = isBullish ? state.PendBull : state.PendBear;

    // ---- 1. Resolve or extend the pending reclaim window ----
    if (pend.Active)
    {
        const int barsSincePen = barIndex - pend.PenBar;
        if (barsSincePen < 1 || barsSincePen > s.sweepMaxReclaimBars)
        {
            // Outside the window (skipped bars / recalc edge) — the
            // pool stays consumed; a stale close cannot be trusted
            // for either verdict.
            pend.Active = false;
        }
        else
        {
            // Track the deepest extreme across the pending window
            if (isBullish && sc.Low[barIndex] < pend.PenExtreme)
            {
                pend.PenExtreme = sc.Low[barIndex];
                pend.ExtremeBar = barIndex;
            }
            if (!isBullish && sc.High[barIndex] > pend.PenExtreme)
            {
                pend.PenExtreme = sc.High[barIndex];
                pend.ExtremeBar = barIndex;
            }

            bool reclaimed = isBullish
                ? (close + halfTick >= pend.PoolLevel + cbReq)
                : (close - halfTick <= pend.PoolLevel - cbReq);

            if (reclaimed)
            {
                if (pend.CondsPassed)
                {
                    // Sweep confirmed at THIS bar (the reclaim bar)
                    result.Detected         = true;
                    result.PoolTouchCount   = pend.PoolTouchCount;
                    result.PenetrationTicks = pend.PenetrationTicks;
                    result.BurstRatioPct    = pend.BurstRatioPct;
                    result.WickPct          = pend.WickPct;
                    result.ZoneDelta        = pend.ZoneDelta;
                    result.ZonePenVolume    = pend.ZonePenVolume;
                    result.RaidBarIndex     = pend.PenBar;

                    const float extreme = pend.PenExtreme;

                    float cbF = isBullish ? (close - pend.PoolLevel)
                                          : (pend.PoolLevel - close);
                    result.CloseBackTicks = static_cast<int>((cbF + halfTick) / T);

                    result.MarkerPrice = extreme;
                    result.WantZone    = true;
                    if (isBullish) { result.ZoneBottom = extreme; result.ZoneTop = pend.PoolLevel; }
                    else           { result.ZoneTop = extreme; result.ZoneBottom = pend.PoolLevel; }

                    result.Strength = CalculateSweepStrength(
                        sc, barIndex, isBullish, result, s, pend.ExtremeBar);
                }
                pend.Active = false;
            }
            else if (barsSincePen == s.sweepMaxReclaimBars)
            {
                // Window exhausted with no reclaim. Still closed beyond
                // the pool -> liquidity RUN in the breakout direction.
                // Middle-ground close -> silent consumption (spec F14
                // ruling, scofdoc-10).
                bool stillBeyond = isBullish
                    ? (close < pend.PoolLevel - halfTick)
                    : (close > pend.PoolLevel + halfTick);
                if (stillBeyond)
                {
                    out_run.Detected         = true;
                    out_run.PoolTouchCount   = pend.PoolTouchCount;
                    out_run.PenetrationTicks = pend.PenetrationTicks;
                    out_run.BurstRatioPct    = pend.BurstRatioPct;
                    out_run.WickPct          = pend.WickPct;
                    out_run.ZoneDelta        = pend.ZoneDelta;
                    out_run.ZonePenVolume    = pend.ZonePenVolume;
                    out_run.RaidBarIndex     = pend.PenBar;

                    // For runs, CloseBackTicks = distance CLOSED BEYOND
                    // the pool at window end (breakout conviction)
                    float cbF = isBullish ? (pend.PoolLevel - close)
                                          : (close - pend.PoolLevel);
                    out_run.CloseBackTicks = static_cast<int>((cbF + halfTick) / T);

                    // Bearish run (bull path) plots above the bar;
                    // bullish run (bear path) plots below — matching
                    // the study-wide direction convention.
                    out_run.MarkerPrice = isBullish ? sc.High[barIndex]
                                                    : sc.Low[barIndex];
                    out_run.WantZone    = false;   // breakout, not an S/R pocket

                    out_run.Strength = CalculateRunStrength(out_run, s);
                }
                pend.Active = false;
            }
            // else: mid-window, not reclaimed — stay pending (In:66 > 1)
        }

        if (result.Detected || pend.Active)
        {
            // One sweep per bar per direction; while a window is open,
            // no fresh raids are scanned. Fix S8: pools penetrated by
            // this bar are consumed either way — those stops are gone.
            for (LiquidityPool& p : state.Pools)
            {
                if (p.Consumed || p.IsLowPool != isBullish)
                    continue;
                if (p.LastTouchBar >= barIndex)
                    continue;
                bool penetrated = isBullish
                    ? (sc.Low[barIndex]  <= p.Level - penReq + halfTick)
                    : (sc.High[barIndex] >= p.Level + penReq - halfTick);
                if (penetrated)
                    p.Consumed = true;
            }
            return result;
        }
    }

    // ---- 2. Fresh raid scan (Fix S4: nearest-first with fallback) ----
    // Every penetrated pool is consumed — those stops have been run.
    LiquidityPool* raided[MAX_POOLS_PER_SIDE];
    int nRaided = 0;
    for (LiquidityPool& p : state.Pools)
    {
        if (p.Consumed || p.IsLowPool != isBullish)
            continue;
        // The pool must predate this bar
        if (p.LastTouchBar >= barIndex)
            continue;

        bool penetrated = isBullish
            ? (sc.Low[barIndex]  <= p.Level - penReq + halfTick)
            : (sc.High[barIndex] >= p.Level + penReq - halfTick);
        if (!penetrated)
            continue;

        p.Consumed = true;
        if (nRaided < MAX_POOLS_PER_SIDE)
            raided[nRaided++] = &p;
    }

    if (nRaided == 0)
        return result;

    // Sort nearest-first: highest low pool / lowest high pool — that
    // is where the stops sat (insertion sort; n <= 64)
    for (int a = 1; a < nRaided; ++a)
    {
        LiquidityPool* v = raided[a];
        int b = a - 1;
        while (b >= 0 &&
               (( isBullish && raided[b]->Level < v->Level) ||
                (!isBullish && raided[b]->Level > v->Level)))
        {
            raided[b + 1] = raided[b];
            --b;
        }
        raided[b + 1] = v;
    }

    for (int c = 0; c < nRaided; ++c)
    {
        LiquidityPool& pool = *raided[c];

        // Raid-bar conditions (wick / burst / trapped delta)
        int    penTicks  = 0;
        float  burstPct  = 0.0f, wickPct = 0.0f;
        double zoneDelta = 0.0,  penVol  = 0.0;
        bool condsPassed = EvaluateSweepRaidBar(
            sc, barIndex, isBullish, pool.Level, s,
            penTicks, burstPct, wickPct, zoneDelta, penVol);

        bool reclaimedSameBar = isBullish
            ? (close + halfTick >= pool.Level + cbReq)
            : (close - halfTick <= pool.Level - cbReq);

        if (reclaimedSameBar)
        {
            if (!condsPassed)
                continue;   // Fix S4: fall back to the next-deeper pool

            result.Detected         = true;
            result.PoolTouchCount   = pool.TouchCount;
            result.PenetrationTicks = penTicks;
            result.BurstRatioPct    = burstPct;
            result.WickPct          = wickPct;
            result.ZoneDelta        = zoneDelta;
            result.ZonePenVolume    = penVol;
            result.RaidBarIndex     = barIndex;

            float extreme = isBullish ? sc.Low[barIndex] : sc.High[barIndex];
            float cbF = isBullish ? (close - pool.Level) : (pool.Level - close);
            result.CloseBackTicks = static_cast<int>((cbF + halfTick) / T);

            result.MarkerPrice = extreme;
            result.WantZone    = true;
            if (isBullish) { result.ZoneBottom = extreme; result.ZoneTop = pool.Level; }
            else           { result.ZoneTop = extreme; result.ZoneBottom = pool.Level; }

            result.Strength = CalculateSweepStrength(
                sc, barIndex, isBullish, result, s, barIndex);
            return result;
        }

        bool closedBeyond = isBullish ? (close < pool.Level - halfTick)
                                      : (close > pool.Level + halfTick);
        if (closedBeyond)
        {
            // Reclaim window opens on the NEAREST closed-beyond pool
            // (deeper pools are already consumed); the next In:66 bars
            // decide sweep (reclaim) vs run (still beyond).
            pend.Active           = true;
            pend.PoolLevel        = pool.Level;
            pend.PoolTouchCount   = pool.TouchCount;
            pend.PenBar           = barIndex;
            pend.PenExtreme       = isBullish ? sc.Low[barIndex] : sc.High[barIndex];
            pend.ExtremeBar       = barIndex;
            pend.CondsPassed      = condsPassed;
            pend.PenetrationTicks = penTicks;
            pend.BurstRatioPct    = burstPct;
            pend.WickPct          = wickPct;
            pend.ZoneDelta        = zoneDelta;
            pend.ZonePenVolume    = penVol;
            return result;
        }

        // Middle-ground close (between pool and pool+closeBack):
        // neither reclaim nor breakout for THIS pool — consumed
        // silently; a deeper pool may still qualify (Fix S4).
    }

    return result;
}

// ============================================================
// HELPER: CalculateSweepStrength (spec 6.2, v1205 amendments)
// Fix S3: the exhaustion bonus inspects extremeBarIndex — the bar
// that actually holds the sweep extreme (for multi-bar sweeps the
// raid bar usually does; v1204 always checked the reclaim bar).
// R6: when In:38 = 0 the trapped-delta bonus is graded anyway as
// the counter-directional share of penetration-zone volume — the
// data was always computed; the bonus was dead at defaults.
// ============================================================

static int CalculateSweepStrength(
    SCStudyInterfaceRef sc, int barIndex, bool isBullish,
    const DetectionResult& r, const StudySettings& s, int extremeBarIndex)
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
        // Threshold mode (v1105 formula, on the double quantity)
        float beyond = static_cast<float>(
            fabs(r.ZoneDelta) - static_cast<double>(s.sweepMinZoneDelta));
        score += min(10.0f, max(0.0f, beyond / 25.0f));
    }
    else if (r.ZonePenVolume > 0.0)
    {
        // R6: trapped-flow share of pen-zone volume; the sign must
        // agree with the trap (bull sweep = sellers trapped below).
        // 50% counter-directional share = full 10.
        bool rightSign = isBullish ? (r.ZoneDelta <= 0.0)
                                   : (r.ZoneDelta >= 0.0);
        if (rightSign)
        {
            float sharePct = static_cast<float>(
                (fabs(r.ZoneDelta) / r.ZonePenVolume) * 100.0);
            score += min(10.0f, sharePct / 5.0f);
        }
    }

    score += min(5.0f, max(0.0f,
        static_cast<float>(r.CloseBackTicks - s.sweepCloseBackTicks) * 2.0f));

    // Finished Business stacked on the sweep extreme (Fix S3)
    double volAtExtreme = 0.0;
    if (CheckExhaustionAtExtreme(sc, extremeBarIndex, isBullish,
            s.exhaustMaxOppPct, volAtExtreme))
        score += 10.0f;

    (void)barIndex;   // retained for signature stability / future use

    return static_cast<int>(min(STRENGTH_MAX, max(STRENGTH_MIN, score)));
}

// ============================================================
// HELPER: CalculateRunStrength (v1205, scofdoc-10 §6)
// A run's quality = depth of the raid, conviction of the
// breakout close, pool importance, aligned aggression in the
// pen zone, and a LOW rejection wick on the raid bar (the
// mirror of the sweep's wick requirement).
// Budget: 30 base | 15 penetration | 15 close-beyond |
// 10 equal-level pool | 15 aligned delta share | 15 low wick.
// ============================================================

static int CalculateRunStrength(
    const DetectionResult& r, const StudySettings& s)
{
    float score = 30.0f;

    score += min(15.0f, max(0.0f,
        static_cast<float>(r.PenetrationTicks - s.sweepMinPenTicks) * 3.0f));

    score += min(15.0f, max(0.0f,
        static_cast<float>(r.CloseBackTicks) * 2.0f));

    if (r.PoolTouchCount >= 2)
        score += 10.0f;

    // Aligned aggression: for a run the pen-zone delta should point
    // WITH the breakout (bullish run = positive delta). 60% share of
    // pen-zone volume = full 15.
    if (r.ZonePenVolume > 0.0)
    {
        bool aligned = r.IsBullish ? (r.ZoneDelta >= 0.0)
                                   : (r.ZoneDelta <= 0.0);
        if (aligned)
        {
            float sharePct = static_cast<float>(
                (fabs(r.ZoneDelta) / r.ZonePenVolume) * 100.0);
            score += min(15.0f, sharePct / 4.0f);
        }
    }

    // Conviction: little rejection wick on the raid bar. 0% wick =
    // full 15; >= 50% = nothing (that bar looked like a sweep).
    score += min(15.0f, max(0.0f, (50.0f - r.WickPct) * 0.3f));

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
            // Fix S7 (v1205): 1-tick floor — In:44 = 0 previously
            // accepted zero-height "gaps" (Low == High[d-1]), arming
            // degenerate FVGs with FvgTop == FvgBottom.
            const float fvgReq =
                static_cast<float>(max(s.slingMinFvgTicks, 1)) * T;
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
    // its own sweep bar. A newer sweep supersedes an older same-
    // direction setup ONLY in WAIT_DISPLACEMENT (Fix S5, v1205):
    // a setup with a confirmed FVG awaiting its tap is a valid
    // armed pattern — the spec's cancellation list (trap break,
    // gap fill, expiry) governs it, not newer fuel.
    const DetectionResult* fuels[2] = { &sweepBull, &sweepBear };
    for (int f = 0; f < 2; ++f)
    {
        const DetectionResult& fuel = *fuels[f];
        if (!fuel.Detected)
            continue;

        for (SlingshotSetup& su : state.Setups)
            if (su.IsBullish == fuel.IsBullish
                && su.State == SLING_WAIT_DISPLACEMENT)
                su.Done = true;

        SlingshotSetup su;
        memset(&su, 0, sizeof(su));
        su.IsBullish     = fuel.IsBullish;
        su.State         = SLING_WAIT_DISPLACEMENT;
        su.SweepBar      = i;
        su.SweepExtreme  = fuel.MarkerPrice;     // sweep extreme price
        su.PoolLevel     = fuel.IsBullish ? fuel.ZoneTop : fuel.ZoneBottom;
        su.SweepStrength = fuel.Strength;
        // R11 (v1205): the flip denominator is the RAID bar's delta —
        // that is where the trapped flow sits. For same-bar sweeps
        // RaidBarIndex == i (identical to v1204); for multi-bar sweeps
        // it is the penetration bar, not the low-delta reclaim bar.
        {
            const int rb = (fuel.RaidBarIndex > 0) ? fuel.RaidBarIndex : i;
            su.SweepBarDelta = static_cast<double>(sc.AskVolume[rb])
                             - static_cast<double>(sc.BidVolume[rb]);
        }
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
        case PATTERN_RUN:       return "RUN";
        default:                return "ABSORPTION";
    }
}

static const char* PatternCode(PatternType p)
{
    switch (p)
    {
        case PATTERN_SWEEP:     return "SWP";
        case PATTERN_SLINGSHOT: return "SLG";
        case PATTERN_RUN:       return "RUN";
        default:                return "ABS";
    }
}

// ============================================================
// HELPER: OutputSignal (spec 4.5)
// The single output path for every pattern: subgraph write,
// marker, zone, highlight, alert, edge registration.
// v1204 (Fix A1): deduped per (bar, pattern, direction) — intrabar
// re-detections refresh subgraphs/drawings and update the open
// edge record, but never duplicate zones, registrations or alerts.
// v1204 (B12): an absorption overlapping a still-extending
// same-direction absorption zone reinforces that zone (bounds
// expand, +8 strength) instead of stacking a new rectangle.
// Takes r by value: the confluence bonus mutates the local copy.
// ============================================================

static void OutputSignal(
    SCStudyInterfaceRef sc, DetectionResult r,
    const StudySettings& s, StudyState& state)
{
    const int i   = sc.Index;
    const int dir = r.IsBullish ? 0 : 1;

    const bool firstEmitThisBar =
        (state.LastSignalBar[r.Pattern][dir] != i);

    const int      myRectLN = MakeLineNumber(i, r.Pattern, r.IsBullish, ELEM_RECT);
    const float    halfTick = sc.TickSize * 0.5f;
    const COLORREF fill     = r.IsBullish ? s.bullZoneColor : s.bearZoneColor;

    // Own zone from an earlier tick of this bar, if any — looked up
    // first so the confluence scan below cannot award a bonus on a
    // re-emit that would not actually merge (the own-zone path wins).
    SignalZone* p_Own = nullptr;
    if (r.WantZone && s.enableRects)
    {
        for (SignalZone& z : state.Zones)
        {
            if (z.RectLineNumber == myRectLN && z.SignalBarIndex == i)
            {
                p_Own = &z;
                break;
            }
        }
    }

    // ---- B12: absorption confluence scan ----
    SignalZone* p_MergeZone = nullptr;
    if (r.Pattern == PATTERN_ABSORPTION && r.WantZone && s.enableRects
        && p_Own == nullptr)
    {
        for (SignalZone& z : state.Zones)
        {
            if (z.Pattern != PATTERN_ABSORPTION || z.IsBullish != r.IsBullish)
                continue;
            if (!z.IsExtending || z.RectLineNumber == myRectLN)
                continue;
            if (z.ZoneBottom <= r.ZoneTop + halfTick &&
                z.ZoneTop    >= r.ZoneBottom - halfTick)
            {
                p_MergeZone = &z;
                break;
            }
        }
        if (p_MergeZone != nullptr)
            r.Strength = static_cast<int>(
                min(STRENGTH_MAX, static_cast<float>(r.Strength) + 8.0f));
    }

    // ---- Subgraph map (spec section 9; v1205 adds SG9-11) ----
    int priceSG, bullPriceSG, strengthSG;
    switch (r.Pattern)
    {
        case PATTERN_SWEEP:     bullPriceSG = 3; strengthSG = 7;  break;
        case PATTERN_SLINGSHOT: bullPriceSG = 5; strengthSG = 8;  break;
        case PATTERN_RUN:       bullPriceSG = 9; strengthSG = 11; break;
        default:                bullPriceSG = 0; strengthSG = 2;  break;
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
        (r.Pattern == PATTERN_RUN)       ? s.markerTextRun   :
                                           s.markerTextAbs;
    DrawMarkerText(sc, i, plotPrice,
        sc.Subgraph[priceSG].PrimaryColor, markerText, s.markerFontSize,
        MakeLineNumber(i, r.Pattern, r.IsBullish, ELEM_MARKER));

    // ---- Zone rectangle ----
    if (r.WantZone && s.enableRects)
    {
        // (a) own zone from an earlier tick of this bar — refresh it
        if (p_Own != nullptr)
        {
            p_Own->ZoneTop    = r.ZoneTop;
            p_Own->ZoneBottom = r.ZoneBottom;
            CreateOrUpdateZoneRectangle(sc, *p_Own, fill, s.zoneTransparency, i);
        }
        else if (p_MergeZone != nullptr)
        {
            // (b) B12: reinforce the overlapped zone (idempotent under
            // intrabar re-emits — expansion is min/max). The reinforce
            // bar must not freeze the zone, or the merge would end the
            // very extension it reinforces.
            p_MergeZone->ZoneTop          = max(p_MergeZone->ZoneTop, r.ZoneTop);
            p_MergeZone->ZoneBottom       = min(p_MergeZone->ZoneBottom, r.ZoneBottom);
            p_MergeZone->LastReinforceBar = i;
            CreateOrUpdateZoneRectangle(sc, *p_MergeZone, fill, s.zoneTransparency, i);
        }
        else
        {
            // (c) new zone
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
            zone.SignalBarIndex   = i;
            zone.ZoneTop          = r.ZoneTop;
            zone.ZoneBottom       = r.ZoneBottom;
            zone.RectLineNumber   = myRectLN;
            zone.Pattern          = r.Pattern;
            zone.IsBullish        = r.IsBullish;
            zone.IsExtending      = true;
            zone.ExtendedBars     = 0;
            zone.LastReinforceBar = i;

            CreateOrUpdateZoneRectangle(sc, zone, fill, s.zoneTransparency, i);
            state.Zones.push_back(zone);
        }
    }

    // ---- Bar highlight ----
    if (s.enableHighlight)
        HighlightBar(sc, i, fill, s.zoneTransparency,
            MakeLineNumber(i, r.Pattern, r.IsBullish, ELEM_HIGHLIGHT));

    // ---- Alert (In:22/23 shared across patterns; first emit only) ----
    if (firstEmitThisBar)
    {
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
    }

    // ---- Edge registration (spec 11.1) ----
    if (s.edgeEnabled)
    {
        // A re-emit updates the existing open record (entry price and
        // strength track the forming bar) instead of duplicating it.
        // The In:58 threshold gates only NEW registration — an already
        // registered record keeps tracking (and updating) even if a
        // later tick's recomputed strength dips below the threshold.
        TrackedSignal* p_Existing = nullptr;
        if (!firstEmitThisBar)
        {
            for (std::deque<TrackedSignal>::reverse_iterator it =
                     state.OpenSignals.rbegin();
                 it != state.OpenSignals.rend(); ++it)
            {
                if (it->Pattern == r.Pattern && it->IsBullish == r.IsBullish
                    && it->SignalBarIndex == i)
                {
                    p_Existing = &(*it);
                    break;
                }
            }
        }

        if (p_Existing != nullptr)
        {
            p_Existing->EntryPrice = sc.Close[i];
            p_Existing->Strength   = r.Strength;
        }
        else if (r.Strength >= s.edgeMinStrength)
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

    state.LastSignalBar[r.Pattern][dir] = i;
}

// ============================================================
// HELPER: RetractAbsorptionIntrabar (Fix A1, v1204)
// With "Detect Only on Bar Close" off, a forming bar can emit an
// absorption on one tick whose conditions fail on a later tick.
// This withdraws every artifact of such a signal: drawings,
// zone, open edge record, subgraph values. Only absorption is
// retractable (stateless evaluator). Limitation: a B12 confluence
// merge into a neighboring zone is not rolled back — the expanded
// bounds stand.
// ============================================================

static void RetractAbsorptionIntrabar(
    SCStudyInterfaceRef sc, StudyState& state,
    const DetectionResult& r, bool isBullish)
{
    const int i   = sc.Index;
    const int dir = isBullish ? 0 : 1;

    // Nothing emitted for this bar, or the signal still holds
    if (r.Detected || state.LastSignalBar[PATTERN_ABSORPTION][dir] != i)
        return;

    sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING,
        MakeLineNumber(i, PATTERN_ABSORPTION, isBullish, ELEM_MARKER));
    sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING,
        MakeLineNumber(i, PATTERN_ABSORPTION, isBullish, ELEM_HIGHLIGHT));

    const int rectLN = MakeLineNumber(i, PATTERN_ABSORPTION, isBullish, ELEM_RECT);
    for (std::deque<SignalZone>::iterator it = state.Zones.begin();
         it != state.Zones.end(); ++it)
    {
        if (it->RectLineNumber == rectLN && it->SignalBarIndex == i)
        {
            sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, rectLN);
            state.Zones.erase(it);
            break;
        }
    }

    for (std::deque<TrackedSignal>::iterator it = state.OpenSignals.begin();
         it != state.OpenSignals.end(); ++it)
    {
        if (it->Pattern == PATTERN_ABSORPTION && it->IsBullish == isBullish
            && it->SignalBarIndex == i)
        {
            state.OpenSignals.erase(it);
            break;
        }
    }

    sc.Subgraph[isBullish ? 0 : 1][i] = 0.0f;
    sc.Subgraph[2][i] = 0.0f;   // re-written this tick if the other direction survives

    state.LastSignalBar[PATTERN_ABSORPTION][dir] = -1;
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
    for (int p = 0; p < PATTERN_COUNT; ++p)
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
// v1105 mechanics, generalized to SignalZone. v1204 (B12): a
// confluence-reinforce bar, like the signal bar, does not freeze
// the zone it just reinforced.
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
                           currentIndex != zone.SignalBarIndex &&
                           currentIndex != zone.LastReinforceBar);  // B12, v1204

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
// END OF SCOFA-v1205.cpp
// ============================================================
