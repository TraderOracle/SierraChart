/*============================================================================
  Order Flow + Market Profile Model  (Location -> Refinement -> Trigger)
  Sierra Chart ACSIL study.

  MODEL
  -----
  1) LOCATION      Volume profile of a reference balance area (prior session,
                   prior N sessions, or rolling N bars). From it:
                     - POC / VAH / VAL
                     - HVN (high volume nodes)  -> magnets / targets
                     - LVN (low volume nodes)   -> reaction / rejection levels
  2) REFINEMENT    Volume spread analysis on each bar:
                     - session cumulative volume delta (CVD) and its slope
                     - bar delta / volume ratio (one-sided aggression)
                     - bar volume vs average volume
                     - average trade size (proxy for "big" participants), and
                       optionally real-time large prints from Time & Sales
  3) TRIGGER       No anticipation. A signal requires, in order:
                     a) a CLOSE beyond a level by N ticks, with aggression
                     b) a RETEST of that level that holds
                     c) aggression again on the retest, CVD agreeing
                   Target defaults to the nearest HVN / POC in the direction
                   of the trade (seeking balance); stop sits beyond the level.

  REQUIREMENTS
  ------------
  - Intraday chart with bid/ask volume data (tick-by-tick download recommended:
    Chart Settings -> "Intraday Data Storage Time Unit" = 1 tick for best
    profile and delta accuracy).
  - This is an INDICATOR: it draws levels, arrows, labels and fires alerts.
    It places no orders.

  BUILD
  -----
  Put this file in Sierra Chart's ACS_Source folder, then
  Analysis -> Build Custom Studies DLL -> select this file.
  Add "Order Flow + Market Profile Model" to the price graph.

  Not financial advice. Test on sim before using it for anything real.
============================================================================*/

#include <map>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "sierrachart.h"

SCDLLName("OrderFlow MarketProfile Model")

/*----------------------------------------------------------------------------
  sierrachart.h defines min() and max() as function-like macros. Any call
  written as std::max(a, b) is therefore mangled by the preprocessor and will
  not compile. These local helpers avoid the macro entirely.
----------------------------------------------------------------------------*/
static inline int    MaxInt   (const int A, const int B)       { return (A > B) ? A : B; }
static inline int    MinInt   (const int A, const int B)       { return (A < B) ? A : B; }
static inline int    AbsInt   (const int A)                    { return (A < 0) ? -A : A; }
static inline double MaxDouble(const double A, const double B) { return (A > B) ? A : B; }

/*----------------------------------------------------------------------------
  Subgraph / Input / Persistent indices
----------------------------------------------------------------------------*/
enum SubgraphIndexes
{
    SG_POC = 0,
    SG_VAH,
    SG_VAL,
    SG_HVN1,
    SG_HVN2,
    SG_HVN3,
    SG_LVN1,
    SG_LVN2,
    SG_LVN3,
    SG_LONG_TRIGGER,
    SG_SHORT_TRIGGER,
    SG_CVD,
    SG_BAR_DELTA,
    SG_DELTA_RATIO,
    SG_AVG_TRADE_SIZE,
    SG_AVG_VOLUME,
    SG_LARGE_BUY,
    SG_LARGE_SELL,
    SG_LONG_STATE,
    SG_SHORT_STATE,
    SG_ENTRY,
    SG_STOP,
    SG_TARGET,
    NUM_SUBGRAPHS
};

enum InputIndexes
{
    IN_PROFILE_MODE = 0,
    IN_SESSIONS_BACK,
    IN_ROLLING_BARS,
    IN_VALUE_AREA_PCT,
    IN_HVN_PCT,
    IN_LVN_PCT,
    IN_NODE_WINDOW_TICKS,
    IN_SMOOTH_TICKS,
    IN_MIN_NODE_SEP_TICKS,
    IN_MAX_NODES,
    IN_USE_PRIOR_HL,

    IN_CVD_RESET_SESSION,
    IN_CVD_LOOKBACK,
    IN_MIN_DELTA_RATIO,
    IN_VOL_MULTIPLE,
    IN_AVG_VOL_LEN,
    IN_MIN_AVG_TRADE_SIZE,
    IN_USE_TIME_AND_SALES,
    IN_LARGE_PRINT_SIZE,
    IN_REQUIRE_LARGE_PRINTS,

    IN_BREAKOUT_TICKS,
    IN_RETEST_TOL_TICKS,
    IN_MAX_RETEST_BARS,
    IN_INVALIDATION_TICKS,
    IN_STOP_TICKS,
    IN_MIN_TARGET_TICKS,
    IN_ON_BAR_CLOSE,

    IN_DRAW_LABELS,
    IN_ARROW_OFFSET_TICKS,
    IN_ENABLE_ALERTS,
    NUM_INPUTS
};

enum PersistIntIndexes
{
    PI_CURR_SESS_START = 1,
    PI_PREV_SESS_START,
    PI_PREV_SESS_END,
    PI_LAST_ROLLING_BAR,
    PI_HVN_COUNT,
    PI_LVN_COUNT,
    PI_PROFILE_VALID,
    PI_LONG_STATE,
    PI_LONG_BO_BAR,
    PI_SHORT_STATE,
    PI_SHORT_BO_BAR,
    PI_TNS_SEQUENCE,
    PI_LAST_ALERT_BAR
};

enum PersistFloatIndexes
{
    PF_POC = 1,
    PF_VAH,
    PF_VAL,
    PF_HVN1,
    PF_HVN2,
    PF_HVN3,
    PF_LVN1,
    PF_LVN2,
    PF_LVN3,
    PF_PROFILE_HIGH,
    PF_PROFILE_LOW,
    PF_LONG_LEVEL,
    PF_SHORT_LEVEL
};

/*----------------------------------------------------------------------------
  Profile construction
----------------------------------------------------------------------------*/
struct s_ProfileResult
{
    bool  Valid;
    float POC;
    float VAH;
    float VAL;
    float ProfileHigh;
    float ProfileLow;
    std::vector<float> HVN;
    std::vector<float> LVN;

    s_ProfileResult()
        : Valid(false), POC(0.0f), VAH(0.0f), VAL(0.0f)
        , ProfileHigh(0.0f), ProfileLow(0.0f)
    {}
};

// Aggregate volume-at-price over a bar range and extract POC / VA / HVN / LVN.
static void BuildProfile
( SCStudyInterfaceRef sc
, const int StartIndex
, const int EndIndex
, const float ValueAreaPercent
, const float HVNPercentOfMax
, const float LVNPercentOfMax
, const int   NodeWindowTicks
, const int   SmoothTicks
, const int   MinNodeSeparationTicks
, const int   MaxNodesPerType
, s_ProfileResult& Result
)
{
    Result = s_ProfileResult();

    if (sc.VolumeAtPriceForBars == NULL)
        return;

    if (StartIndex < 0 || EndIndex < StartIndex || EndIndex >= sc.ArraySize)
        return;

    if ((int)sc.VolumeAtPriceForBars->GetNumberOfBars() <= EndIndex)
        return;

    std::map<int, double> ProfileMap;
    double TotalVolume = 0.0;

    for (int BarIndex = StartIndex; BarIndex <= EndIndex; ++BarIndex)
    {
        const int VAPSize = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(BarIndex);

        for (int VAPIndex = 0; VAPIndex < VAPSize; ++VAPIndex)
        {
            const s_VolumeAtPriceV2* p_VAP = NULL;

            if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(BarIndex, VAPIndex, &p_VAP))
                break;

            if (p_VAP == NULL)
                continue;

            ProfileMap[p_VAP->PriceInTicks] += (double)p_VAP->Volume;
            TotalVolume                     += (double)p_VAP->Volume;
        }
    }

    if (ProfileMap.empty() || TotalVolume <= 0.0)
        return;

    const int MinTick = ProfileMap.begin()->first;
    const int MaxTick = ProfileMap.rbegin()->first;
    const int Rows    = MaxTick - MinTick + 1;

    // Sanity guard against absurd ranges (bad tick size / corrupt data).
    if (Rows <= 2 || Rows > 100000)
        return;

    std::vector<double> Volumes((size_t)Rows, 0.0);

    for (std::map<int, double>::const_iterator It = ProfileMap.begin(); It != ProfileMap.end(); ++It)
        Volumes[(size_t)(It->first - MinTick)] = It->second;

    // ---- POC (raw, unsmoothed) --------------------------------------------
    int    POCRow = 0;
    double MaxRowVolume = -1.0;

    for (int Row = 0; Row < Rows; ++Row)
    {
        if (Volumes[(size_t)Row] > MaxRowVolume)
        {
            MaxRowVolume = Volumes[(size_t)Row];
            POCRow       = Row;
        }
    }

    // ---- Value area: expand from POC toward the heavier adjacent row -------
    const double TargetVolume = TotalVolume * (double)ValueAreaPercent / 100.0;

    int    UpperRow = POCRow;
    int    LowerRow = POCRow;
    double VAVolume = Volumes[(size_t)POCRow];

    while (VAVolume < TargetVolume && (LowerRow > 0 || UpperRow < Rows - 1))
    {
        const double VolAbove = (UpperRow < Rows - 1) ? Volumes[(size_t)(UpperRow + 1)] : -1.0;
        const double VolBelow = (LowerRow > 0)        ? Volumes[(size_t)(LowerRow - 1)] : -1.0;

        if (VolAbove < 0.0 && VolBelow < 0.0)
            break;

        if (VolAbove >= VolBelow)
        {
            ++UpperRow;
            VAVolume += Volumes[(size_t)UpperRow];
        }
        else
        {
            --LowerRow;
            VAVolume += Volumes[(size_t)LowerRow];
        }
    }

    // ---- Smoothed copy used only for node detection ------------------------
    std::vector<double> Smoothed(Volumes);

    if (SmoothTicks > 0)
    {
        for (int Row = 0; Row < Rows; ++Row)
        {
            double Sum   = 0.0;
            int    Count = 0;

            for (int K = Row - SmoothTicks; K <= Row + SmoothTicks; ++K)
            {
                if (K < 0 || K >= Rows)
                    continue;

                Sum += Volumes[(size_t)K];
                ++Count;
            }

            Smoothed[(size_t)Row] = (Count > 0) ? Sum / (double)Count : 0.0;
        }
    }

    double SmoothedMax = 0.0;
    for (int Row = 0; Row < Rows; ++Row)
        SmoothedMax = MaxDouble(SmoothedMax, Smoothed[(size_t)Row]);

    if (SmoothedMax <= 0.0)
        return;

    // ---- Local extremes ----------------------------------------------------
    const int Window = MaxInt(1, NodeWindowTicks);

    std::vector< std::pair<double, int> > HVNCandidates;   // (volume, row)
    std::vector< std::pair<double, int> > LVNCandidates;

    for (int Row = Window; Row < Rows - Window; ++Row)
    {
        bool IsLocalMax = true;
        bool IsLocalMin = true;

        for (int K = Row - Window; K <= Row + Window; ++K)
        {
            if (K == Row)
                continue;

            if (Smoothed[(size_t)K] > Smoothed[(size_t)Row])
                IsLocalMax = false;

            if (Smoothed[(size_t)K] < Smoothed[(size_t)Row])
                IsLocalMin = false;
        }

        if (IsLocalMax && Smoothed[(size_t)Row] >= SmoothedMax * (double)HVNPercentOfMax / 100.0)
            HVNCandidates.push_back(std::make_pair(Smoothed[(size_t)Row], Row));

        if (IsLocalMin && Smoothed[(size_t)Row] <= SmoothedMax * (double)LVNPercentOfMax / 100.0)
            LVNCandidates.push_back(std::make_pair(Smoothed[(size_t)Row], Row));
    }

    // Strongest HVNs first, thinnest LVNs first.
    std::sort(HVNCandidates.begin(), HVNCandidates.end());
    std::reverse(HVNCandidates.begin(), HVNCandidates.end());
    std::sort(LVNCandidates.begin(), LVNCandidates.end());

    const int MinSeparation = MaxInt(1, MinNodeSeparationTicks);

    std::vector<int> AcceptedHVNRows;
    for (size_t N = 0; N < HVNCandidates.size() && (int)AcceptedHVNRows.size() < MaxNodesPerType; ++N)
    {
        const int Row = HVNCandidates[N].second;

        bool TooClose = (AbsInt(Row - POCRow) < MinSeparation);

        for (size_t A = 0; A < AcceptedHVNRows.size() && !TooClose; ++A)
        {
            if (AbsInt(Row - AcceptedHVNRows[A]) < MinSeparation)
                TooClose = true;
        }

        if (!TooClose)
            AcceptedHVNRows.push_back(Row);
    }

    std::vector<int> AcceptedLVNRows;
    for (size_t N = 0; N < LVNCandidates.size() && (int)AcceptedLVNRows.size() < MaxNodesPerType; ++N)
    {
        const int Row = LVNCandidates[N].second;

        bool TooClose = false;

        for (size_t A = 0; A < AcceptedLVNRows.size() && !TooClose; ++A)
        {
            if (AbsInt(Row - AcceptedLVNRows[A]) < MinSeparation)
                TooClose = true;
        }

        if (!TooClose)
            AcceptedLVNRows.push_back(Row);
    }

    // ---- Convert rows to prices -------------------------------------------
    Result.POC         = (float)((MinTick + POCRow)   * sc.TickSize);
    Result.VAH         = (float)((MinTick + UpperRow) * sc.TickSize);
    Result.VAL         = (float)((MinTick + LowerRow) * sc.TickSize);
    Result.ProfileHigh = (float)(MaxTick * sc.TickSize);
    Result.ProfileLow  = (float)(MinTick * sc.TickSize);

    for (size_t N = 0; N < AcceptedHVNRows.size(); ++N)
        Result.HVN.push_back((float)((MinTick + AcceptedHVNRows[N]) * sc.TickSize));

    for (size_t N = 0; N < AcceptedLVNRows.size(); ++N)
        Result.LVN.push_back((float)((MinTick + AcceptedLVNRows[N]) * sc.TickSize));

    Result.Valid = true;
}

// Walk back from EndIndex to find the first bar index of the session that is
// SessionsBack trading days before the session containing EndIndex.
static int FindSessionStartIndex(SCStudyInterfaceRef sc, const int EndIndex, const int SessionsBack)
{
    if (EndIndex <= 0)
        return 0;

    int Boundaries = 0;
    int Index      = EndIndex;

    while (Index > 0)
    {
        const SCDateTime ThisDay = sc.GetTradingDayDate(sc.BaseDateTimeIn[Index]);
        const SCDateTime PrevDay = sc.GetTradingDayDate(sc.BaseDateTimeIn[Index - 1]);

        if (ThisDay != PrevDay)
        {
            ++Boundaries;

            if (Boundaries >= SessionsBack)
                return Index;
        }

        --Index;
    }

    return 0;
}

/*----------------------------------------------------------------------------
  Study
----------------------------------------------------------------------------*/
SCSFExport scsf_OrderFlowMarketProfileModel(SCStudyInterfaceRef sc)
{
    SCSubgraphRef Subgraph_POC           = sc.Subgraph[SG_POC];
    SCSubgraphRef Subgraph_VAH           = sc.Subgraph[SG_VAH];
    SCSubgraphRef Subgraph_VAL           = sc.Subgraph[SG_VAL];
    SCSubgraphRef Subgraph_LongTrigger   = sc.Subgraph[SG_LONG_TRIGGER];
    SCSubgraphRef Subgraph_ShortTrigger  = sc.Subgraph[SG_SHORT_TRIGGER];
    SCSubgraphRef Subgraph_CVD           = sc.Subgraph[SG_CVD];
    SCSubgraphRef Subgraph_AvgVolume     = sc.Subgraph[SG_AVG_VOLUME];

    if (sc.SetDefaults)
    {
        sc.GraphName            = "Order Flow + Market Profile Model";
        sc.StudyDescription     = "Location (volume profile HVN/LVN/POC) -> Refinement (CVD, "
                                  "delta ratio, trade size) -> Trigger (breakout + retest with aggression).";
        sc.GraphRegion          = 0;
        sc.AutoLoop             = 0;
        sc.ValueFormat          = VALUEFORMAT_INHERITED;
        sc.MaintainVolumeAtPriceData     = 1;
        sc.MaintainAdditionalChartDataArrays = 1;
        sc.AlertOnlyOncePerBar  = 1;
        sc.FreeDLL              = 0;

        // ---- Level subgraphs ------------------------------------------------
        Subgraph_POC.Name          = "Balance POC (target)";
        Subgraph_POC.DrawStyle     = DRAWSTYLE_LINE;
        Subgraph_POC.PrimaryColor  = RGB(255, 200,   0);
        Subgraph_POC.LineWidth     = 2;
        Subgraph_POC.DrawZeros     = 0;

        Subgraph_VAH.Name          = "VAH";
        Subgraph_VAH.DrawStyle     = DRAWSTYLE_DASH;
        Subgraph_VAH.PrimaryColor  = RGB(160, 160, 160);
        Subgraph_VAH.LineWidth     = 1;
        Subgraph_VAH.DrawZeros     = 0;

        Subgraph_VAL.Name          = "VAL";
        Subgraph_VAL.DrawStyle     = DRAWSTYLE_DASH;
        Subgraph_VAL.PrimaryColor  = RGB(160, 160, 160);
        Subgraph_VAL.LineWidth     = 1;
        Subgraph_VAL.DrawZeros     = 0;

        for (int N = 0; N < 3; ++N)
        {
            SCString Name;
            Name.Format("HVN %d (magnet)", N + 1);
            sc.Subgraph[SG_HVN1 + N].Name         = Name;
            sc.Subgraph[SG_HVN1 + N].DrawStyle    = DRAWSTYLE_LINE;
            sc.Subgraph[SG_HVN1 + N].PrimaryColor = RGB(0, 170, 255);
            sc.Subgraph[SG_HVN1 + N].LineWidth    = 1;
            sc.Subgraph[SG_HVN1 + N].DrawZeros    = 0;

            SCString LVNName;
            LVNName.Format("LVN %d (reaction)", N + 1);
            sc.Subgraph[SG_LVN1 + N].Name         = LVNName;
            sc.Subgraph[SG_LVN1 + N].DrawStyle    = DRAWSTYLE_LINE;
            sc.Subgraph[SG_LVN1 + N].PrimaryColor = RGB(255, 90, 200);
            sc.Subgraph[SG_LVN1 + N].LineWidth    = 1;
            sc.Subgraph[SG_LVN1 + N].LineStyle    = LINESTYLE_DOT;
            sc.Subgraph[SG_LVN1 + N].DrawZeros    = 0;
        }

        // ---- Signals ---------------------------------------------------------
        Subgraph_LongTrigger.Name         = "Long Trigger";
        Subgraph_LongTrigger.DrawStyle    = DRAWSTYLE_ARROW_UP;
        Subgraph_LongTrigger.PrimaryColor = RGB(0, 220, 100);
        Subgraph_LongTrigger.LineWidth    = 3;
        Subgraph_LongTrigger.DrawZeros    = 0;

        Subgraph_ShortTrigger.Name         = "Short Trigger";
        Subgraph_ShortTrigger.DrawStyle    = DRAWSTYLE_ARROW_DOWN;
        Subgraph_ShortTrigger.PrimaryColor = RGB(255, 60, 60);
        Subgraph_ShortTrigger.LineWidth    = 3;
        Subgraph_ShortTrigger.DrawZeros    = 0;

        // ---- Diagnostic / exportable arrays ---------------------------------
        const char* HiddenNames[] =
        {
            "CVD (session)", "Bar Delta", "Delta/Volume Ratio", "Avg Trade Size",
            "Avg Volume", "Large Buy Prints", "Large Sell Prints",
            "Long State", "Short State", "Entry", "Stop", "Target"
        };

        for (int N = 0; N < 12; ++N)
        {
            sc.Subgraph[SG_CVD + N].Name      = HiddenNames[N];
            sc.Subgraph[SG_CVD + N].DrawStyle = DRAWSTYLE_IGNORE;
            sc.Subgraph[SG_CVD + N].DrawZeros = 0;
        }

        // ---- Inputs: LOCATION ------------------------------------------------
        sc.Input[IN_PROFILE_MODE].Name = "1. Location: Profile Source";
        sc.Input[IN_PROFILE_MODE].SetCustomInputStrings("Prior Session;Prior N Sessions;Rolling N Bars");
        sc.Input[IN_PROFILE_MODE].SetCustomInputIndex(0);

        sc.Input[IN_SESSIONS_BACK].Name = "1. Location: Number of Prior Sessions";
        sc.Input[IN_SESSIONS_BACK].SetInt(1);
        sc.Input[IN_SESSIONS_BACK].SetIntLimits(1, 20);

        sc.Input[IN_ROLLING_BARS].Name = "1. Location: Rolling Lookback (bars)";
        sc.Input[IN_ROLLING_BARS].SetInt(240);
        sc.Input[IN_ROLLING_BARS].SetIntLimits(10, 20000);

        sc.Input[IN_VALUE_AREA_PCT].Name = "1. Location: Value Area Percent";
        sc.Input[IN_VALUE_AREA_PCT].SetFloat(70.0f);
        sc.Input[IN_VALUE_AREA_PCT].SetFloatLimits(30.0f, 95.0f);

        sc.Input[IN_HVN_PCT].Name = "1. Location: HVN Threshold (% of max row volume)";
        sc.Input[IN_HVN_PCT].SetFloat(70.0f);
        sc.Input[IN_HVN_PCT].SetFloatLimits(10.0f, 100.0f);

        sc.Input[IN_LVN_PCT].Name = "1. Location: LVN Threshold (% of max row volume)";
        sc.Input[IN_LVN_PCT].SetFloat(30.0f);
        sc.Input[IN_LVN_PCT].SetFloatLimits(1.0f, 90.0f);

        sc.Input[IN_NODE_WINDOW_TICKS].Name = "1. Location: Node Detection Window (ticks)";
        sc.Input[IN_NODE_WINDOW_TICKS].SetInt(4);
        sc.Input[IN_NODE_WINDOW_TICKS].SetIntLimits(1, 200);

        sc.Input[IN_SMOOTH_TICKS].Name = "1. Location: Profile Smoothing (ticks each side)";
        sc.Input[IN_SMOOTH_TICKS].SetInt(2);
        sc.Input[IN_SMOOTH_TICKS].SetIntLimits(0, 100);

        sc.Input[IN_MIN_NODE_SEP_TICKS].Name = "1. Location: Minimum Node Separation (ticks)";
        sc.Input[IN_MIN_NODE_SEP_TICKS].SetInt(8);
        sc.Input[IN_MIN_NODE_SEP_TICKS].SetIntLimits(1, 500);

        sc.Input[IN_MAX_NODES].Name = "1. Location: Max Nodes Per Type (1-3)";
        sc.Input[IN_MAX_NODES].SetInt(3);
        sc.Input[IN_MAX_NODES].SetIntLimits(1, 3);

        sc.Input[IN_USE_PRIOR_HL].Name = "1. Location: Include Profile High/Low as Levels";
        sc.Input[IN_USE_PRIOR_HL].SetYesNo(1);

        // ---- Inputs: REFINEMENT ---------------------------------------------
        sc.Input[IN_CVD_RESET_SESSION].Name = "2. Refinement: Reset CVD Each Session";
        sc.Input[IN_CVD_RESET_SESSION].SetYesNo(1);

        sc.Input[IN_CVD_LOOKBACK].Name = "2. Refinement: CVD Slope Lookback (bars)";
        sc.Input[IN_CVD_LOOKBACK].SetInt(5);
        sc.Input[IN_CVD_LOOKBACK].SetIntLimits(1, 500);

        sc.Input[IN_MIN_DELTA_RATIO].Name = "2. Refinement: Min |Delta| / Volume Ratio";
        sc.Input[IN_MIN_DELTA_RATIO].SetFloat(0.15f);
        sc.Input[IN_MIN_DELTA_RATIO].SetFloatLimits(0.0f, 1.0f);

        sc.Input[IN_VOL_MULTIPLE].Name = "2. Refinement: Min Bar Volume vs Average";
        sc.Input[IN_VOL_MULTIPLE].SetFloat(1.2f);
        sc.Input[IN_VOL_MULTIPLE].SetFloatLimits(0.0f, 20.0f);

        sc.Input[IN_AVG_VOL_LEN].Name = "2. Refinement: Average Volume Length (bars)";
        sc.Input[IN_AVG_VOL_LEN].SetInt(20);
        sc.Input[IN_AVG_VOL_LEN].SetIntLimits(2, 1000);

        sc.Input[IN_MIN_AVG_TRADE_SIZE].Name = "2. Refinement: Min Average Trade Size (0 = off)";
        sc.Input[IN_MIN_AVG_TRADE_SIZE].SetFloat(0.0f);
        sc.Input[IN_MIN_AVG_TRADE_SIZE].SetFloatLimits(0.0f, 10000.0f);

        sc.Input[IN_USE_TIME_AND_SALES].Name = "2. Refinement: Track Large Prints via Time & Sales (real-time)";
        sc.Input[IN_USE_TIME_AND_SALES].SetYesNo(1);

        sc.Input[IN_LARGE_PRINT_SIZE].Name = "2. Refinement: Large Print Size (contracts)";
        sc.Input[IN_LARGE_PRINT_SIZE].SetInt(25);
        sc.Input[IN_LARGE_PRINT_SIZE].SetIntLimits(1, 100000);

        sc.Input[IN_REQUIRE_LARGE_PRINTS].Name = "2. Refinement: Require Large Print on Trigger Bar (real-time only)";
        sc.Input[IN_REQUIRE_LARGE_PRINTS].SetYesNo(0);

        // ---- Inputs: TRIGGER --------------------------------------------------
        sc.Input[IN_BREAKOUT_TICKS].Name = "3. Trigger: Breakout Confirmation Beyond Level (ticks)";
        sc.Input[IN_BREAKOUT_TICKS].SetInt(2);
        sc.Input[IN_BREAKOUT_TICKS].SetIntLimits(0, 1000);

        sc.Input[IN_RETEST_TOL_TICKS].Name = "3. Trigger: Retest Tolerance (ticks)";
        sc.Input[IN_RETEST_TOL_TICKS].SetInt(2);
        sc.Input[IN_RETEST_TOL_TICKS].SetIntLimits(0, 1000);

        sc.Input[IN_MAX_RETEST_BARS].Name = "3. Trigger: Max Bars to Wait for Retest";
        sc.Input[IN_MAX_RETEST_BARS].SetInt(12);
        sc.Input[IN_MAX_RETEST_BARS].SetIntLimits(1, 500);

        sc.Input[IN_INVALIDATION_TICKS].Name = "3. Trigger: Invalidation Beyond Level (ticks)";
        sc.Input[IN_INVALIDATION_TICKS].SetInt(4);
        sc.Input[IN_INVALIDATION_TICKS].SetIntLimits(1, 1000);

        sc.Input[IN_STOP_TICKS].Name = "3. Trigger: Stop Offset Beyond Level (ticks)";
        sc.Input[IN_STOP_TICKS].SetInt(6);
        sc.Input[IN_STOP_TICKS].SetIntLimits(1, 1000);

        sc.Input[IN_MIN_TARGET_TICKS].Name = "3. Trigger: Min Distance to Target (ticks)";
        sc.Input[IN_MIN_TARGET_TICKS].SetInt(8);
        sc.Input[IN_MIN_TARGET_TICKS].SetIntLimits(1, 5000);

        sc.Input[IN_ON_BAR_CLOSE].Name = "3. Trigger: Evaluate on Bar Close Only";
        sc.Input[IN_ON_BAR_CLOSE].SetYesNo(1);

        // ---- Inputs: display --------------------------------------------------
        sc.Input[IN_DRAW_LABELS].Name = "Display: Draw Entry/Stop/Target Labels";
        sc.Input[IN_DRAW_LABELS].SetYesNo(1);

        sc.Input[IN_ARROW_OFFSET_TICKS].Name = "Display: Signal Arrow Offset (ticks)";
        sc.Input[IN_ARROW_OFFSET_TICKS].SetInt(4);
        sc.Input[IN_ARROW_OFFSET_TICKS].SetIntLimits(0, 200);

        sc.Input[IN_ENABLE_ALERTS].Name = "Display: Enable Alerts";
        sc.Input[IN_ENABLE_ALERTS].SetYesNo(1);

        return;
    }

    /*------------------------------------------------------------------------
      Persistent state
    ------------------------------------------------------------------------*/
    int& CurrentSessionStart = sc.GetPersistentInt(PI_CURR_SESS_START);
    int& PriorSessionStart   = sc.GetPersistentInt(PI_PREV_SESS_START);
    int& PriorSessionEnd     = sc.GetPersistentInt(PI_PREV_SESS_END);
    int& LastRollingBar      = sc.GetPersistentInt(PI_LAST_ROLLING_BAR);
    int& HVNCount            = sc.GetPersistentInt(PI_HVN_COUNT);
    int& LVNCount            = sc.GetPersistentInt(PI_LVN_COUNT);
    int& ProfileValid        = sc.GetPersistentInt(PI_PROFILE_VALID);
    int& LongState           = sc.GetPersistentInt(PI_LONG_STATE);
    int& LongBreakoutBar     = sc.GetPersistentInt(PI_LONG_BO_BAR);
    int& ShortState          = sc.GetPersistentInt(PI_SHORT_STATE);
    int& ShortBreakoutBar    = sc.GetPersistentInt(PI_SHORT_BO_BAR);
    int& TnSSequence         = sc.GetPersistentInt(PI_TNS_SEQUENCE);

    float& LongLevel  = sc.GetPersistentFloat(PF_LONG_LEVEL);
    float& ShortLevel = sc.GetPersistentFloat(PF_SHORT_LEVEL);

    if (sc.UpdateStartIndex == 0)
    {
        CurrentSessionStart = 0;
        PriorSessionStart   = -1;
        PriorSessionEnd     = -1;
        LastRollingBar      = -1;
        HVNCount            = 0;
        LVNCount            = 0;
        ProfileValid        = 0;
        LongState           = 0;
        ShortState          = 0;
        LongBreakoutBar     = -1;
        ShortBreakoutBar    = -1;
        TnSSequence         = 0;
        LongLevel           = 0.0f;
        ShortLevel          = 0.0f;

        for (int N = PF_POC; N <= PF_PROFILE_LOW; ++N)
            sc.GetPersistentFloat(N) = 0.0f;

        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_ALL, 0);
    }

    if (sc.ArraySize < 2)
        return;

    // ---- Cached input values ----------------------------------------------
    const int   ProfileMode        = sc.Input[IN_PROFILE_MODE].GetIndex();
    const int   SessionsBack       = sc.Input[IN_SESSIONS_BACK].GetInt();
    const int   RollingBars        = sc.Input[IN_ROLLING_BARS].GetInt();
    const float ValueAreaPercent   = sc.Input[IN_VALUE_AREA_PCT].GetFloat();
    const float HVNPercent         = sc.Input[IN_HVN_PCT].GetFloat();
    const float LVNPercent         = sc.Input[IN_LVN_PCT].GetFloat();
    const int   NodeWindowTicks    = sc.Input[IN_NODE_WINDOW_TICKS].GetInt();
    const int   SmoothTicks        = sc.Input[IN_SMOOTH_TICKS].GetInt();
    const int   MinNodeSepTicks    = sc.Input[IN_MIN_NODE_SEP_TICKS].GetInt();
    const int   MaxNodes           = sc.Input[IN_MAX_NODES].GetInt();
    const bool  UseProfileHighLow  = sc.Input[IN_USE_PRIOR_HL].GetYesNo() != 0;

    const bool  ResetCVDEachSession = sc.Input[IN_CVD_RESET_SESSION].GetYesNo() != 0;
    const int   CVDLookback         = sc.Input[IN_CVD_LOOKBACK].GetInt();
    const float MinDeltaRatio       = sc.Input[IN_MIN_DELTA_RATIO].GetFloat();
    const float VolumeMultiple      = sc.Input[IN_VOL_MULTIPLE].GetFloat();
    const int   AvgVolumeLength     = sc.Input[IN_AVG_VOL_LEN].GetInt();
    const float MinAvgTradeSize     = sc.Input[IN_MIN_AVG_TRADE_SIZE].GetFloat();
    const bool  UseTimeAndSales     = sc.Input[IN_USE_TIME_AND_SALES].GetYesNo() != 0;
    const int   LargePrintSize      = sc.Input[IN_LARGE_PRINT_SIZE].GetInt();
    const bool  RequireLargePrints  = sc.Input[IN_REQUIRE_LARGE_PRINTS].GetYesNo() != 0;

    const float BreakoutOffset      = sc.Input[IN_BREAKOUT_TICKS].GetInt()      * (float)sc.TickSize;
    const float RetestTolerance     = sc.Input[IN_RETEST_TOL_TICKS].GetInt()    * (float)sc.TickSize;
    const int   MaxRetestBars       = sc.Input[IN_MAX_RETEST_BARS].GetInt();
    const float InvalidationOffset  = sc.Input[IN_INVALIDATION_TICKS].GetInt()  * (float)sc.TickSize;
    const float StopOffset          = sc.Input[IN_STOP_TICKS].GetInt()          * (float)sc.TickSize;
    const float MinTargetDistance   = sc.Input[IN_MIN_TARGET_TICKS].GetInt()    * (float)sc.TickSize;
    const bool  EvaluateOnBarClose  = sc.Input[IN_ON_BAR_CLOSE].GetYesNo() != 0;

    const bool  DrawLabels          = sc.Input[IN_DRAW_LABELS].GetYesNo() != 0;
    const float ArrowOffset         = sc.Input[IN_ARROW_OFFSET_TICKS].GetInt() * (float)sc.TickSize;
    const bool  EnableAlerts        = sc.Input[IN_ENABLE_ALERTS].GetYesNo() != 0;

    /*------------------------------------------------------------------------
      Real-time large prints from Time & Sales (current bar only).
      Historical bars fall back to average trade size as the "big player" proxy.
    ------------------------------------------------------------------------*/
    if (UseTimeAndSales && !sc.IsFullRecalculation && sc.ArraySize > 0)
    {
        c_SCTimeAndSalesArray TimeSales;
        sc.GetTimeAndSales(TimeSales);

        const int LastBar = sc.ArraySize - 1;

        for (int Index = 0; Index < TimeSales.Size(); ++Index)
        {
            const s_TimeAndSales& Record = TimeSales[Index];

            if ((int)Record.Sequence <= TnSSequence)
                continue;

            TnSSequence = (int)Record.Sequence;

            if ((int)Record.Volume < LargePrintSize)
                continue;

            if (Record.Type == SC_TS_ASK)
                sc.Subgraph[SG_LARGE_BUY].Data[LastBar]  += 1.0f;
            else if (Record.Type == SC_TS_BID)
                sc.Subgraph[SG_LARGE_SELL].Data[LastBar] += 1.0f;
        }
    }

    /*------------------------------------------------------------------------
      Main bar loop
    ------------------------------------------------------------------------*/
    for (int Index = sc.UpdateStartIndex; Index < sc.ArraySize; ++Index)
    {
        const bool IsLastBar = (Index == sc.ArraySize - 1);

        // ================= Session boundary ==================================
        bool NewSession = false;

        if (Index == 0)
        {
            NewSession = true;
        }
        else
        {
            const SCDateTime ThisDay = sc.GetTradingDayDate(sc.BaseDateTimeIn[Index]);
            const SCDateTime PrevDay = sc.GetTradingDayDate(sc.BaseDateTimeIn[Index - 1]);
            NewSession = (ThisDay != PrevDay);
        }

        // ================= STEP 1: LOCATION ==================================
        bool RecomputeProfile = false;
        int  ProfileStart     = -1;
        int  ProfileEnd       = -1;

        if (NewSession && Index > 0)
        {
            PriorSessionStart   = CurrentSessionStart;
            PriorSessionEnd     = Index - 1;
            CurrentSessionStart = Index;

            if (ProfileMode == 0)                     // Prior session
            {
                ProfileStart     = PriorSessionStart;
                ProfileEnd       = PriorSessionEnd;
                RecomputeProfile = true;
            }
            else if (ProfileMode == 1)                // Prior N sessions
            {
                ProfileStart     = FindSessionStartIndex(sc, PriorSessionEnd, SessionsBack);
                ProfileEnd       = PriorSessionEnd;
                RecomputeProfile = true;
            }
        }
        else if (Index == 0)
        {
            CurrentSessionStart = 0;
        }

        if (ProfileMode == 2 && Index != LastRollingBar) // Rolling N bars
        {
            ProfileEnd       = MaxInt(0, Index - 1);
            ProfileStart     = MaxInt(0, ProfileEnd - RollingBars + 1);
            RecomputeProfile = (ProfileEnd > ProfileStart);
            LastRollingBar   = Index;
        }

        if (RecomputeProfile && ProfileStart >= 0 && ProfileEnd >= ProfileStart)
        {
            s_ProfileResult Profile;

            BuildProfile(sc, ProfileStart, ProfileEnd, ValueAreaPercent, HVNPercent, LVNPercent,
                         NodeWindowTicks, SmoothTicks, MinNodeSepTicks, MaxNodes, Profile);

            if (Profile.Valid)
            {
                sc.GetPersistentFloat(PF_POC)           = Profile.POC;
                sc.GetPersistentFloat(PF_VAH)           = Profile.VAH;
                sc.GetPersistentFloat(PF_VAL)           = Profile.VAL;
                sc.GetPersistentFloat(PF_PROFILE_HIGH)  = Profile.ProfileHigh;
                sc.GetPersistentFloat(PF_PROFILE_LOW)   = Profile.ProfileLow;

                for (int N = 0; N < 3; ++N)
                {
                    sc.GetPersistentFloat(PF_HVN1 + N) =
                        (N < (int)Profile.HVN.size()) ? Profile.HVN[N] : 0.0f;

                    sc.GetPersistentFloat(PF_LVN1 + N) =
                        (N < (int)Profile.LVN.size()) ? Profile.LVN[N] : 0.0f;
                }

                HVNCount     = (int)Profile.HVN.size();
                LVNCount     = (int)Profile.LVN.size();
                ProfileValid = 1;

                // Levels changed: any pending setup is stale.
                LongState  = 0;
                ShortState = 0;
            }
        }

        // Plot levels (skip the session boundary bar so lines break cleanly).
        if (ProfileValid && !(NewSession && ProfileMode != 2))
        {
            Subgraph_POC.Data[Index] = sc.GetPersistentFloat(PF_POC);
            Subgraph_VAH.Data[Index] = sc.GetPersistentFloat(PF_VAH);
            Subgraph_VAL.Data[Index] = sc.GetPersistentFloat(PF_VAL);

            for (int N = 0; N < 3; ++N)
            {
                sc.Subgraph[SG_HVN1 + N].Data[Index] = sc.GetPersistentFloat(PF_HVN1 + N);
                sc.Subgraph[SG_LVN1 + N].Data[Index] = sc.GetPersistentFloat(PF_LVN1 + N);
            }
        }

        // ================= STEP 2: REFINEMENT ================================
        const float BarVolume = sc.Volume[Index];
        float BidVolume       = sc.BidVolume[Index];
        float AskVolume       = sc.AskVolume[Index];
        float BarDelta        = AskVolume - BidVolume;

        // Fallback when the data feed provides no bid/ask volume.
        if (BidVolume + AskVolume <= 0.0f && BarVolume > 0.0f)
        {
            if (sc.Close[Index] > sc.Open[Index])
                BarDelta = BarVolume;
            else if (sc.Close[Index] < sc.Open[Index])
                BarDelta = -BarVolume;
            else
                BarDelta = 0.0f;
        }

        sc.Subgraph[SG_BAR_DELTA].Data[Index] = BarDelta;

        // Session cumulative volume delta.
        if (Index == 0 || (NewSession && ResetCVDEachSession))
            Subgraph_CVD.Data[Index] = BarDelta;
        else
            Subgraph_CVD.Data[Index] = Subgraph_CVD.Data[Index - 1] + BarDelta;

        const float DeltaRatio = (BarVolume > 0.0f) ? (float)fabs(BarDelta) / BarVolume : 0.0f;
        sc.Subgraph[SG_DELTA_RATIO].Data[Index] = DeltaRatio;

        // Average trade size: prefer VAP trade counts, fall back to base data.
        float NumberOfTrades = sc.NumberOfTrades[Index];

        if (NumberOfTrades <= 0.0f && sc.VolumeAtPriceForBars != NULL
            && (int)sc.VolumeAtPriceForBars->GetNumberOfBars() > Index)
        {
            unsigned int TradeCount = 0;
            const int VAPSize = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(Index);

            for (int VAPIndex = 0; VAPIndex < VAPSize; ++VAPIndex)
            {
                const s_VolumeAtPriceV2* p_VAP = NULL;

                if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(Index, VAPIndex, &p_VAP))
                    break;

                if (p_VAP != NULL)
                    TradeCount += p_VAP->NumberOfTrades;
            }

            NumberOfTrades = (float)TradeCount;
        }

        const float AvgTradeSize = (NumberOfTrades > 0.0f) ? BarVolume / NumberOfTrades : 0.0f;
        sc.Subgraph[SG_AVG_TRADE_SIZE].Data[Index] = AvgTradeSize;

        sc.SimpleMovAvg(sc.Volume, Subgraph_AvgVolume.Data, Index, AvgVolumeLength);
        const float AverageVolume = Subgraph_AvgVolume.Data[Index];

        // CVD slope over the confirmation lookback.
        const int   CVDRefIndex = MaxInt(0, Index - CVDLookback);
        const float CVDSlope    = Subgraph_CVD.Data[Index] - Subgraph_CVD.Data[CVDRefIndex];

        const float LargeBuys  = sc.Subgraph[SG_LARGE_BUY].Data[Index];
        const float LargeSells = sc.Subgraph[SG_LARGE_SELL].Data[Index];

        const bool VolumeOK   = (AverageVolume <= 0.0f) || (BarVolume >= AverageVolume * VolumeMultiple);
        const bool RatioOK    = (DeltaRatio >= MinDeltaRatio);
        const bool TradeSizeOK = (MinAvgTradeSize <= 0.0f)
                              || (AvgTradeSize >= MinAvgTradeSize)
                              || (LargeBuys + LargeSells > 0.0f);

        const bool LargePrintBuyOK  = !RequireLargePrints || (LargeBuys  > 0.0f);
        const bool LargePrintSellOK = !RequireLargePrints || (LargeSells > 0.0f);

        const bool BuyAggression  = (BarDelta > 0.0f) && VolumeOK && RatioOK && TradeSizeOK;
        const bool SellAggression = (BarDelta < 0.0f) && VolumeOK && RatioOK && TradeSizeOK;

        // ================= STEP 3: TRIGGER ===================================
        sc.Subgraph[SG_LONG_STATE].Data[Index]  = (float)LongState;
        sc.Subgraph[SG_SHORT_STATE].Data[Index] = (float)ShortState;

        if (Index < 1 || !ProfileValid)
            continue;

        if (EvaluateOnBarClose && IsLastBar)
            continue;   // wait for the bar to close before changing state

        // Candidate levels: POC, VAH, VAL, HVNs, LVNs (+ profile extremes).
        std::vector<float> Levels;
        Levels.reserve(11);

        const float POCValue = sc.GetPersistentFloat(PF_POC);
        const float VAHValue = sc.GetPersistentFloat(PF_VAH);
        const float VALValue = sc.GetPersistentFloat(PF_VAL);

        if (POCValue > 0.0f) Levels.push_back(POCValue);
        if (VAHValue > 0.0f) Levels.push_back(VAHValue);
        if (VALValue > 0.0f) Levels.push_back(VALValue);

        for (int N = 0; N < 3; ++N)
        {
            const float HVNValue = sc.GetPersistentFloat(PF_HVN1 + N);
            const float LVNValue = sc.GetPersistentFloat(PF_LVN1 + N);

            if (HVNValue > 0.0f) Levels.push_back(HVNValue);
            if (LVNValue > 0.0f) Levels.push_back(LVNValue);
        }

        if (UseProfileHighLow)
        {
            const float ProfHigh = sc.GetPersistentFloat(PF_PROFILE_HIGH);
            const float ProfLow  = sc.GetPersistentFloat(PF_PROFILE_LOW);

            if (ProfHigh > 0.0f) Levels.push_back(ProfHigh);
            if (ProfLow  > 0.0f) Levels.push_back(ProfLow);
        }

        if (Levels.empty())
            continue;

        // ---------------- Long side -----------------------------------------
        if (LongState == 0)
        {
            // (a) Breakout: close above a level with buy aggression.
            float BestLevel = 0.0f;

            for (size_t N = 0; N < Levels.size(); ++N)
            {
                const float Level = Levels[N];

                const bool ClosedAbove = (sc.Close[Index]     >  Level + BreakoutOffset);
                const bool WasBelow    = (sc.Close[Index - 1] <= Level + BreakoutOffset);

                if (ClosedAbove && WasBelow && BuyAggression && LargePrintBuyOK)
                {
                    // Keep the highest level actually broken on this bar.
                    if (Level > BestLevel)
                        BestLevel = Level;
                }
            }

            if (BestLevel > 0.0f)
            {
                LongState       = 1;
                LongLevel       = BestLevel;
                LongBreakoutBar = Index;
            }
        }
        else if (LongState == 1)
        {
            const bool Invalidated = (sc.Close[Index] < LongLevel - InvalidationOffset);
            const bool TimedOut    = (Index - LongBreakoutBar > MaxRetestBars);

            if (Invalidated || TimedOut)
            {
                LongState = 0;
            }
            else if (Index > LongBreakoutBar)
            {
                // (b)+(c) Retest that holds, with aggression and CVD agreeing.
                const bool TouchedLevel = (sc.Low[Index]   <= LongLevel + RetestTolerance);
                const bool HeldAbove    = (sc.Close[Index] >  LongLevel);
                const bool CVDAgrees    = (CVDSlope >= 0.0f);

                if (TouchedLevel && HeldAbove && BuyAggression && LargePrintBuyOK && CVDAgrees)
                {
                    const float Entry = sc.Close[Index];
                    const float Stop  = LongLevel - StopOffset;

                    // Target: nearest HVN / POC above entry -> seek balance.
                    float Target = 0.0f;

                    for (size_t N = 0; N < Levels.size(); ++N)
                    {
                        const float Level = Levels[N];

                        if (Level > Entry + MinTargetDistance && (Target == 0.0f || Level < Target))
                            Target = Level;
                    }

                    if (Target == 0.0f)
                        Target = Entry + (Entry - Stop) * 2.0f;   // fallback 2R

                    Subgraph_LongTrigger.Data[Index]      = sc.Low[Index] - ArrowOffset;
                    sc.Subgraph[SG_ENTRY].Data[Index]     = Entry;
                    sc.Subgraph[SG_STOP].Data[Index]      = Stop;
                    sc.Subgraph[SG_TARGET].Data[Index]    = Target;

                    if (DrawLabels)
                    {
                        s_UseTool Tool;
                        Tool.Clear();
                        Tool.ChartNumber  = sc.ChartNumber;
                        Tool.DrawingType  = DRAWING_TEXT;
                        Tool.LineNumber   = 810000 + Index;
                        Tool.BeginIndex   = Index;
                        Tool.BeginValue   = sc.Low[Index] - ArrowOffset * 2.0f;
                        Tool.Color        = RGB(0, 220, 100);
                        Tool.FontSize     = 8;
                        Tool.AddMethod    = UTAM_ADD_OR_ADJUST;

                        SCString Label;
                        Label.Format("L %s | S %s | T %s",
                            sc.FormatGraphValue(Entry,  sc.BaseGraphValueFormat).GetChars(),
                            sc.FormatGraphValue(Stop,   sc.BaseGraphValueFormat).GetChars(),
                            sc.FormatGraphValue(Target, sc.BaseGraphValueFormat).GetChars());

                        Tool.Text = Label;
                        sc.UseTool(Tool);
                    }

                    if (EnableAlerts && !sc.IsFullRecalculation)
                    {
                        SCString AlertText;
                        AlertText.Format("LONG trigger: retest of %s held with buy aggression.",
                            sc.FormatGraphValue(LongLevel, sc.BaseGraphValueFormat).GetChars());
                        sc.SetAlert(1, AlertText);
                    }

                    LongState = 0;   // setup consumed
                }
            }
        }

        // ---------------- Short side ----------------------------------------
        if (ShortState == 0)
        {
            float BestLevel = 0.0f;

            for (size_t N = 0; N < Levels.size(); ++N)
            {
                const float Level = Levels[N];

                const bool ClosedBelow = (sc.Close[Index]     <  Level - BreakoutOffset);
                const bool WasAbove    = (sc.Close[Index - 1] >= Level - BreakoutOffset);

                if (ClosedBelow && WasAbove && SellAggression && LargePrintSellOK)
                {
                    if (BestLevel == 0.0f || Level < BestLevel)
                        BestLevel = Level;
                }
            }

            if (BestLevel > 0.0f)
            {
                ShortState       = 1;
                ShortLevel       = BestLevel;
                ShortBreakoutBar = Index;
            }
        }
        else if (ShortState == 1)
        {
            const bool Invalidated = (sc.Close[Index] > ShortLevel + InvalidationOffset);
            const bool TimedOut    = (Index - ShortBreakoutBar > MaxRetestBars);

            if (Invalidated || TimedOut)
            {
                ShortState = 0;
            }
            else if (Index > ShortBreakoutBar)
            {
                const bool TouchedLevel = (sc.High[Index]  >= ShortLevel - RetestTolerance);
                const bool HeldBelow    = (sc.Close[Index] <  ShortLevel);
                const bool CVDAgrees    = (CVDSlope <= 0.0f);

                if (TouchedLevel && HeldBelow && SellAggression && LargePrintSellOK && CVDAgrees)
                {
                    const float Entry = sc.Close[Index];
                    const float Stop  = ShortLevel + StopOffset;

                    float Target = 0.0f;

                    for (size_t N = 0; N < Levels.size(); ++N)
                    {
                        const float Level = Levels[N];

                        if (Level < Entry - MinTargetDistance && (Target == 0.0f || Level > Target))
                            Target = Level;
                    }

                    if (Target == 0.0f)
                        Target = Entry - (Stop - Entry) * 2.0f;

                    Subgraph_ShortTrigger.Data[Index]  = sc.High[Index] + ArrowOffset;
                    sc.Subgraph[SG_ENTRY].Data[Index]  = Entry;
                    sc.Subgraph[SG_STOP].Data[Index]   = Stop;
                    sc.Subgraph[SG_TARGET].Data[Index] = Target;

                    if (DrawLabels)
                    {
                        s_UseTool Tool;
                        Tool.Clear();
                        Tool.ChartNumber  = sc.ChartNumber;
                        Tool.DrawingType  = DRAWING_TEXT;
                        Tool.LineNumber   = 820000 + Index;
                        Tool.BeginIndex   = Index;
                        Tool.BeginValue   = sc.High[Index] + ArrowOffset * 2.0f;
                        Tool.Color        = RGB(255, 60, 60);
                        Tool.FontSize     = 8;
                        Tool.AddMethod    = UTAM_ADD_OR_ADJUST;

                        SCString Label;
                        Label.Format("S %s | S %s | T %s",
                            sc.FormatGraphValue(Entry,  sc.BaseGraphValueFormat).GetChars(),
                            sc.FormatGraphValue(Stop,   sc.BaseGraphValueFormat).GetChars(),
                            sc.FormatGraphValue(Target, sc.BaseGraphValueFormat).GetChars());

                        Tool.Text = Label;
                        sc.UseTool(Tool);
                    }

                    if (EnableAlerts && !sc.IsFullRecalculation)
                    {
                        SCString AlertText;
                        AlertText.Format("SHORT trigger: retest of %s rejected with sell aggression.",
                            sc.FormatGraphValue(ShortLevel, sc.BaseGraphValueFormat).GetChars());
                        sc.SetAlert(2, AlertText);
                    }

                    ShortState = 0;
                }
            }
        }

        sc.Subgraph[SG_LONG_STATE].Data[Index]  = (float)LongState;
        sc.Subgraph[SG_SHORT_STATE].Data[Index] = (float)ShortState;
    }
}
