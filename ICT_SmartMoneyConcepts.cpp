//==============================================================================
//  ICT Smart Money Concepts  -  Sierra Chart / ACSIL custom study
//------------------------------------------------------------------------------
//  DLL Name    : ICT_SmartMoneyConcepts
//  Study Name  : ICT Smart Money Concepts
//------------------------------------------------------------------------------
//  Feature set
//    1. Market Structure Mapping
//         - Break of Structure (BOS)            external + internal
//         - Change of Character (CHoCH / MSS)   external + internal
//         - Internal vs. External swing tracking
//         - Strong / Weak highs and lows
//    2. Institutional Supply & Demand Arrays (PD Arrays)
//         - Order Blocks (OB)
//         - Breaker Blocks (failed OB flips polarity)
//         - Mitigation Blocks (failure swing / no HH or LL before MSS)
//         - Rejection Blocks (long wick rejection zones)
//    3. Price Imbalances & Inefficiencies
//         - Fair Value Gaps (3 candle imbalance)
//         - Opening / Volume Gaps (prior close -> next open)
//         - Liquidity Voids (fast one directional legs)
//    4. Liquidity Pools & Sweeps
//         - Equal Highs / Equal Lows (EQH / EQL)
//         - Liquidity sweeps / stop hunts (pierce + reject)
//         - Previous Day / Week / Month High & Low (with raid alerts)
//    5. Premium / Discount / Equilibrium + OTE band
//
//  Every major event has its own individually switchable alert.
//------------------------------------------------------------------------------
//  Build
//    1. Copy this file to  <Sierra Chart>\ACS_Source\
//    2. Analysis >> Build Custom Studies DLL >> select this file (or Build All)
//    3. Analysis >> Studies >> Add Custom Study >> "ICT Smart Money Concepts"
//
//  Notes
//    * All structure / PD array logic is evaluated on CLOSED bars only, so the
//      drawings never repaint. Optional intrabar alerts can be enabled.
//    * Colors are taken from the Subgraphs tab, so every element is user
//      configurable without editing code.
//    * To draw the zones behind the price bars enable
//      "Draw Study Underneath Main Price Graph" in the study settings.
//==============================================================================

#include "sierrachart.h"
#include <vector>
#include <cmath>
#include <cfloat>

SCDLLName("ICT_SmartMoneyConcepts")

//==============================================================================
//  small helpers (avoid min/max macro collisions)
//==============================================================================
static inline float SMC_MaxF(float a, float b) { return (a > b) ? a : b; }
static inline float SMC_MinF(float a, float b) { return (a < b) ? a : b; }
static inline int   SMC_MaxI(int a, int b)     { return (a > b) ? a : b; }
static inline int   SMC_MinI(int a, int b)     { return (a < b) ? a : b; }

//==============================================================================
//  Subgraph indexes (plot data + user editable color holders)
//==============================================================================
enum SMCSubgraphs
{
    SG_SWEEP_BULL = 0,     // marker: sell-side liquidity swept (bullish)
    SG_SWEEP_BEAR,         // marker: buy-side liquidity swept (bearish)

    SG_BULL_STRUCT,        // external bullish BOS / CHoCH color
    SG_BEAR_STRUCT,        // external bearish BOS / CHoCH color
    SG_BULL_INT,           // internal bullish structure color
    SG_BEAR_INT,           // internal bearish structure color

    // --- zone colors : MUST stay in the same order as SMCZoneType ---
    SG_BULL_OB,
    SG_BEAR_OB,
    SG_BULL_BRK,
    SG_BEAR_BRK,
    SG_BULL_MB,
    SG_BEAR_MB,
    SG_BULL_RB,
    SG_BEAR_RB,
    SG_BULL_FVG,
    SG_BEAR_FVG,
    SG_BULL_GAP,
    SG_BEAR_GAP,
    SG_BULL_VOID,
    SG_BEAR_VOID,
    // ----------------------------------------------------------------

    SG_EQH,
    SG_EQL,
    SG_PREMIUM,
    SG_DISCOUNT,
    SG_EQUILIBRIUM,
    SG_OTE,
    SG_PDHL,
    SG_PWHL,
    SG_PMHL,
    SG_STRONG,
    SG_WEAK,

    SG_ATR,                // internal working array (hidden)
    SG_COUNT
};

//==============================================================================
//  Input indexes
//==============================================================================
enum SMCInputs
{
    IN_SWING_LEN = 0,
    IN_INT_LEN,
    IN_CONFIRM,
    IN_SHOW_EXT_STRUCT,
    IN_SHOW_INT_STRUCT,
    IN_SHOW_STRONG_WEAK,
    IN_MAX_STRUCT_LABELS,

    IN_SHOW_OB,
    IN_OB_SOURCE,
    IN_ZONE_DEF,
    IN_INVALIDATION,
    IN_SHOW_BRK,
    IN_SHOW_MB,
    IN_SHOW_RB,
    IN_RB_WICK_RATIO,
    IN_RB_MIN_WICK,
    IN_INTERNAL_OB,

    IN_SHOW_FVG,
    IN_FVG_MIN,
    IN_SHOW_GAP,
    IN_GAP_MIN,
    IN_SHOW_VOID,
    IN_VOID_MIN_BARS,
    IN_VOID_MIN_SIZE,
    IN_FILL_METHOD,

    IN_SHOW_EQ,
    IN_EQ_LEN,
    IN_EQ_TOL,
    IN_EQ_MAXBARS,
    IN_SHOW_SWEEP,
    IN_SWEEP_PEN,
    IN_MAX_LIQ,

    IN_SHOW_PD,
    IN_SHOW_OTE,
    IN_SHOW_PDAY,
    IN_SHOW_PWEEK,
    IN_SHOW_PMONTH,

    IN_ATR_LEN,
    IN_ZONE_EXTEND,
    IN_TRANSPARENCY,
    IN_MAX_ZONES,
    IN_FONT_SIZE,
    IN_SHOW_LABELS,
    IN_DELETE_INVALID,

    IN_ALERTS_ON,
    IN_ALERT_SOUND,
    IN_ALERT_INTRABAR,

    IN_AL_EXT_BOS,
    IN_AL_EXT_CHOCH,
    IN_AL_INT_BOS,
    IN_AL_INT_CHOCH,
    IN_AL_NEW_OB,
    IN_AL_OB_TAP,
    IN_AL_OB_INVALID,
    IN_AL_BRK,
    IN_AL_MB,
    IN_AL_RB,
    IN_AL_FVG_NEW,
    IN_AL_FVG_FILL,
    IN_AL_GAP,
    IN_AL_VOID,
    IN_AL_EQ,
    IN_AL_SWEEP,
    IN_AL_PD,
    IN_AL_HTF,

    IN_COUNT
};

//==============================================================================
//  Zone types (bullish types are EVEN, bearish types are ODD)
//==============================================================================
enum SMCZoneType
{
    ZT_BULL_OB = 0,
    ZT_BEAR_OB,
    ZT_BULL_BRK,
    ZT_BEAR_BRK,
    ZT_BULL_MB,
    ZT_BEAR_MB,
    ZT_BULL_RB,
    ZT_BEAR_RB,
    ZT_BULL_FVG,
    ZT_BEAR_FVG,
    ZT_BULL_GAP,
    ZT_BEAR_GAP,
    ZT_BULL_VOID,
    ZT_BEAR_VOID,
    ZT_COUNT
};

enum SMCZoneFamily
{
    FAM_OB = 0, FAM_BRK, FAM_MB, FAM_RB, FAM_FVG, FAM_GAP, FAM_VOID, FAM_COUNT
};

static const char* const ZoneLabel[ZT_COUNT] =
{
    "Bull OB",  "Bear OB",
    "Bull BB",  "Bear BB",
    "Bull MB",  "Bear MB",
    "Bull RB",  "Bear RB",
    "FVG",      "FVG",
    "Gap",      "Gap",
    "Void",     "Void"
};

static inline bool ZoneIsBull(int Type)   { return (Type % 2) == 0; }
static inline int  ZoneFamily(int Type)   { return Type / 2; }

//==============================================================================
//  Reserved (fixed) drawing line numbers.  Dynamic drawings start at 1000.
//==============================================================================
enum SMCFixedLines
{
    LN_STRONG_HIGH = 10, LN_STRONG_LOW, LN_WEAK_HIGH, LN_WEAK_LOW,
    LN_PREM_BOX = 20, LN_DISC_BOX, LN_EQ_LINE, LN_OTE_BOX,
    LN_PREM_TXT, LN_DISC_TXT, LN_EQ_TXT, LN_OTE_TXT,
    LN_PDH = 40, LN_PDL, LN_PWH, LN_PWL, LN_PMH, LN_PML,
    LN_PDH_T = 50, LN_PDL_T, LN_PWH_T, LN_PWL_T, LN_PMH_T, LN_PML_T,
    LN_DYNAMIC_BASE = 1000
};

//==============================================================================
//  Persistent data structures
//==============================================================================
struct s_SMCZone
{
    int   Type;
    int   StartIndex;      // bar the zone is anchored to
    int   Created;         // bar the zone was created on
    int   EndIndex;        // frozen right edge once invalidated
    float Top;
    float Bottom;
    int   Active;
    int   Tapped;
    int   Internal;
    int   WasBreaker;
    int   BoxLine;
    int   TextLine;
};

struct s_SMCLiq
{
    int   IsHigh;          // 1 = EQH / buy side , 0 = EQL / sell side
    int   StartIndex;
    int   EndIndex;
    float Price;
    int   Swept;
    int   Line;
    int   TextLine;
};

struct s_SMCPivot
{
    int   Index;
    float Price;
    int   Crossed;
    int   Swept;
    int   Valid;
};

struct s_SMCState
{
    std::vector<s_SMCZone> Zones;
    std::vector<s_SMCLiq>  Liq;
    std::vector<int>       StructLines;

    s_SMCPivot SwHigh, SwLow, PrevSwHigh, PrevSwLow;   // external
    s_SMCPivot InHigh, InLow;                          // internal
    s_SMCPivot EqHigh, EqLow;                          // equal high/low pivots

    int   SwTrend;          //  1 bullish , -1 bearish , 0 undefined
    int   InTrend;

    float RangeHigh, RangeLow;
    int   RangeHighIdx, RangeLowIdx;

    int   PDZone;           //  1 premium , -1 discount , 0 equilibrium

    // previous day / week / month
    float CurDayH, CurDayL, PrevDayH, PrevDayL;
    float CurWkH,  CurWkL,  PrevWkH,  PrevWkL;
    float CurMoH,  CurMoL,  PrevMoH,  PrevMoL;
    int   DayKey,  WkKey,   MoKey;
    int   DayStartIdx, WkStartIdx, MoStartIdx;
    int   PDHTaken, PDLTaken, PWHTaken, PWLTaken, PMHTaken, PMLTaken;

    int   LastProcessed;
    int   LastArraySize;
    int   NextLine;
    int   NeedRefresh;
    int   IntrabarUpIdx, IntrabarDnIdx, IntrabarSweepIdx;
};

static void SMC_ResetPivot(s_SMCPivot& P)
{
    P.Index = -1; P.Price = 0.0f; P.Crossed = 0; P.Swept = 0; P.Valid = 0;
}

static void SMC_ResetState(s_SMCState& S)
{
    S.Zones.clear();
    S.Liq.clear();
    S.StructLines.clear();

    SMC_ResetPivot(S.SwHigh);   SMC_ResetPivot(S.SwLow);
    SMC_ResetPivot(S.PrevSwHigh); SMC_ResetPivot(S.PrevSwLow);
    SMC_ResetPivot(S.InHigh);   SMC_ResetPivot(S.InLow);
    SMC_ResetPivot(S.EqHigh);   SMC_ResetPivot(S.EqLow);

    S.SwTrend = 0;  S.InTrend = 0;
    S.RangeHigh = 0.0f; S.RangeLow = 0.0f;
    S.RangeHighIdx = -1; S.RangeLowIdx = -1;
    S.PDZone = 0;

    S.CurDayH = S.CurWkH = S.CurMoH = -FLT_MAX;
    S.CurDayL = S.CurWkL = S.CurMoL =  FLT_MAX;
    S.PrevDayH = S.PrevDayL = 0.0f;
    S.PrevWkH  = S.PrevWkL  = 0.0f;
    S.PrevMoH  = S.PrevMoL  = 0.0f;
    S.DayKey = S.WkKey = S.MoKey = -1;
    S.DayStartIdx = S.WkStartIdx = S.MoStartIdx = 0;
    S.PDHTaken = S.PDLTaken = S.PWHTaken = S.PWLTaken = S.PMHTaken = S.PMLTaken = 0;

    S.LastProcessed  = -1;
    S.LastArraySize  = 0;
    S.NextLine       = LN_DYNAMIC_BASE;
    S.NeedRefresh    = 1;
    S.IntrabarUpIdx  = -1;
    S.IntrabarDnIdx  = -1;
    S.IntrabarSweepIdx = -1;
}

//==============================================================================
//  Drawing helpers
//==============================================================================
static void SMC_DeleteDrawing(SCStudyInterfaceRef sc, int LineNumber)
{
    if (LineNumber > 0)
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LineNumber);
}

static void SMC_DrawBox(SCStudyInterfaceRef sc, int LineNumber,
                        int BeginIndex, int EndIndex,
                        float Top, float Bottom,
                        unsigned int Color, int Transparency,
                        SubgraphLineStyles LineStyle)
{
    if (LineNumber <= 0)
        return;

    s_UseTool Tool;
    Tool.Clear();
    Tool.ChartNumber       = sc.ChartNumber;
    Tool.Region            = sc.GraphRegion;
    Tool.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
    Tool.LineNumber        = LineNumber;
    Tool.BeginIndex        = BeginIndex;
    Tool.EndIndex          = EndIndex;
    Tool.BeginValue        = Top;
    Tool.EndValue          = Bottom;
    Tool.Color             = Color;
    Tool.SecondaryColor    = Color;
    Tool.TransparencyLevel = Transparency;
    Tool.LineWidth         = 1;
    Tool.LineStyle         = LineStyle;
    Tool.AddMethod         = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;

    sc.UseTool(Tool);
}

static void SMC_DrawLine(SCStudyInterfaceRef sc, int LineNumber,
                         int BeginIndex, int EndIndex,
                         float BeginValue, float EndValue,
                         unsigned int Color, int Width,
                         SubgraphLineStyles LineStyle)
{
    if (LineNumber <= 0)
        return;

    s_UseTool Tool;
    Tool.Clear();
    Tool.ChartNumber = sc.ChartNumber;
    Tool.Region      = sc.GraphRegion;
    Tool.DrawingType = DRAWING_LINE;
    Tool.LineNumber  = LineNumber;
    Tool.BeginIndex  = BeginIndex;
    Tool.EndIndex    = EndIndex;
    Tool.BeginValue  = BeginValue;
    Tool.EndValue    = EndValue;
    Tool.Color       = Color;
    Tool.LineWidth   = Width;
    Tool.LineStyle   = LineStyle;
    Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;

    sc.UseTool(Tool);
}

static void SMC_DrawText(SCStudyInterfaceRef sc, int LineNumber,
                         int Index, float Value, const char* Text,
                         unsigned int Color, int FontSize, int Alignment, int Bold)
{
    if (LineNumber <= 0 || Text == NULL || Text[0] == '\0')
        return;

    s_UseTool Tool;
    Tool.Clear();
    Tool.ChartNumber = sc.ChartNumber;
    Tool.Region      = sc.GraphRegion;
    Tool.DrawingType = DRAWING_TEXT;
    Tool.LineNumber  = LineNumber;
    Tool.BeginIndex  = Index;
    Tool.BeginValue  = Value;
    Tool.Color       = Color;
    Tool.FontSize    = FontSize;
    Tool.FontBold    = Bold;
    Tool.Text        = Text;
    Tool.TextAlignment = Alignment;
    Tool.TransparentLabelBackground = 1;
    Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;

    sc.UseTool(Tool);
}

//==============================================================================
//  Alert helper - never fires during a full recalculation of historical bars
//==============================================================================
static void SMC_Alert(SCStudyInterfaceRef sc, bool Enabled, int SoundNumber,
                      const SCString& Message)
{
    if (!Enabled)
        return;

    if (SoundNumber <= 0)
        sc.AddAlertLine(Message, 0);
    else
        sc.SetAlert(SoundNumber - 1, Message.GetChars());
}

//==============================================================================
//  Pivot detection.  Left side uses <= , right side uses < so that the most
//  recent bar of an equal-high cluster becomes the pivot.
//==============================================================================
static bool SMC_IsPivotHigh(SCStudyInterfaceRef sc, int P, int Len)
{
    if (P - Len < 0 || P + Len >= sc.ArraySize)
        return false;

    const float H = sc.High[P];
    for (int k = P - Len; k < P; ++k)
        if (sc.High[k] > H) return false;
    for (int k = P + 1; k <= P + Len; ++k)
        if (sc.High[k] >= H) return false;

    return true;
}

static bool SMC_IsPivotLow(SCStudyInterfaceRef sc, int P, int Len)
{
    if (P - Len < 0 || P + Len >= sc.ArraySize)
        return false;

    const float L = sc.Low[P];
    for (int k = P - Len; k < P; ++k)
        if (sc.Low[k] < L) return false;
    for (int k = P + 1; k <= P + Len; ++k)
        if (sc.Low[k] <= L) return false;

    return true;
}

//==============================================================================
//  Zone management
//==============================================================================
static void SMC_PruneZones(SCStudyInterfaceRef sc, s_SMCState& S, int Family, int MaxCount)
{
    for (;;)
    {
        int Count = 0;
        int Oldest = -1;

        for (int k = 0; k < (int)S.Zones.size(); ++k)
        {
            if (ZoneFamily(S.Zones[k].Type) != Family)
                continue;
            ++Count;
            if (Oldest < 0)
                Oldest = k;
        }

        if (Count <= MaxCount || Oldest < 0)
            break;

        SMC_DeleteDrawing(sc, S.Zones[Oldest].BoxLine);
        SMC_DeleteDrawing(sc, S.Zones[Oldest].TextLine);
        S.Zones.erase(S.Zones.begin() + Oldest);
    }
}

static void SMC_AddZone(SCStudyInterfaceRef sc, s_SMCState& S,
                        int Type, int StartIndex, int CreatedIndex,
                        float Top, float Bottom, int Internal, int MaxPerFamily)
{
    if (Top < Bottom)
    {
        const float T = Top; Top = Bottom; Bottom = T;
    }
    if (Top - Bottom < sc.TickSize * 0.5f)
    {
        Top    += sc.TickSize * 0.5f;
        Bottom -= sc.TickSize * 0.5f;
    }

    s_SMCZone Z;
    Z.Type       = Type;
    Z.StartIndex = StartIndex;
    Z.Created    = CreatedIndex;
    Z.EndIndex   = -1;
    Z.Top        = Top;
    Z.Bottom     = Bottom;
    Z.Active     = 1;
    Z.Tapped     = 0;
    Z.Internal   = Internal;
    Z.WasBreaker = 0;
    Z.BoxLine    = S.NextLine++;
    Z.TextLine   = S.NextLine++;

    S.Zones.push_back(Z);
    SMC_PruneZones(sc, S, ZoneFamily(Type), MaxPerFamily);
}

static void SMC_AddStructureDrawing(SCStudyInterfaceRef sc, s_SMCState& S,
                                    int PivotIndex, int BreakIndex, float Price,
                                    const char* Label, unsigned int Color,
                                    SubgraphLineStyles LineStyle, int FontSize,
                                    int ShowLabel, int MaxLabels)
{
    const int LineNo = S.NextLine++;
    const int TextNo = S.NextLine++;

    SMC_DrawLine(sc, LineNo, PivotIndex, BreakIndex, Price, Price, Color, 1, LineStyle);

    if (ShowLabel)
    {
        const int MidIndex = PivotIndex + (BreakIndex - PivotIndex) / 2;
        SMC_DrawText(sc, TextNo, MidIndex, Price, Label, Color, FontSize,
                     DT_CENTER | DT_VCENTER, 0);
    }

    S.StructLines.push_back(LineNo);
    S.StructLines.push_back(TextNo);

    while ((int)S.StructLines.size() > MaxLabels * 2 && !S.StructLines.empty())
    {
        SMC_DeleteDrawing(sc, S.StructLines[0]);
        S.StructLines.erase(S.StructLines.begin());
    }
}

//==============================================================================
//  MAIN STUDY
//==============================================================================
SCSFExport scsf_ICTSmartMoneyConcepts(SCStudyInterfaceRef sc)
{
    //--------------------------------------------------------------------------
    //  Defaults
    //--------------------------------------------------------------------------
    if (sc.SetDefaults)
    {
        sc.GraphName = "ICT Smart Money Concepts";
        sc.StudyDescription =
            "ICT / Smart Money Concepts: market structure (BOS, CHoCH/MSS, internal "
            "and external), PD arrays (order blocks, breaker blocks, mitigation "
            "blocks, rejection blocks), imbalances (fair value gaps, opening gaps, "
            "liquidity voids), liquidity pools (EQH/EQL, sweeps / stop hunts), "
            "previous day/week/month levels and premium/discount/OTE pricing. "
            "Includes an individually configurable alert for every major event.";

        sc.GraphRegion       = 0;
        sc.AutoLoop          = 0;          // manual looping
        sc.ValueFormat       = VALUEFORMAT_INHERITED;
        sc.FreeDLL           = 0;
        sc.DrawZeros         = 0;
        sc.CalculationPrecedence = LOW_PREC_LEVEL;

        // ---------------- subgraphs / colors ----------------
        sc.Subgraph[SG_SWEEP_BULL].Name         = "Sweep - Sell Side Taken";
        sc.Subgraph[SG_SWEEP_BULL].DrawStyle    = DRAWSTYLE_TRIANGLE_UP;
        sc.Subgraph[SG_SWEEP_BULL].PrimaryColor = RGB(  0, 210, 140);
        sc.Subgraph[SG_SWEEP_BULL].LineWidth    = 3;
        sc.Subgraph[SG_SWEEP_BULL].DrawZeros    = 0;

        sc.Subgraph[SG_SWEEP_BEAR].Name         = "Sweep - Buy Side Taken";
        sc.Subgraph[SG_SWEEP_BEAR].DrawStyle    = DRAWSTYLE_TRIANGLE_DOWN;
        sc.Subgraph[SG_SWEEP_BEAR].PrimaryColor = RGB(235,  70,  90);
        sc.Subgraph[SG_SWEEP_BEAR].LineWidth    = 3;
        sc.Subgraph[SG_SWEEP_BEAR].DrawZeros    = 0;

        sc.Subgraph[SG_BULL_STRUCT].Name         = "Color - Bullish Structure";
        sc.Subgraph[SG_BULL_STRUCT].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BULL_STRUCT].PrimaryColor = RGB(  0, 190, 255);

        sc.Subgraph[SG_BEAR_STRUCT].Name         = "Color - Bearish Structure";
        sc.Subgraph[SG_BEAR_STRUCT].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BEAR_STRUCT].PrimaryColor = RGB(255, 120,   0);

        sc.Subgraph[SG_BULL_INT].Name         = "Color - Internal Bullish";
        sc.Subgraph[SG_BULL_INT].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BULL_INT].PrimaryColor = RGB(120, 200, 230);

        sc.Subgraph[SG_BEAR_INT].Name         = "Color - Internal Bearish";
        sc.Subgraph[SG_BEAR_INT].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BEAR_INT].PrimaryColor = RGB(230, 170, 120);

        sc.Subgraph[SG_BULL_OB].Name         = "Zone - Bullish Order Block";
        sc.Subgraph[SG_BULL_OB].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BULL_OB].PrimaryColor = RGB( 20, 140, 110);

        sc.Subgraph[SG_BEAR_OB].Name         = "Zone - Bearish Order Block";
        sc.Subgraph[SG_BEAR_OB].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BEAR_OB].PrimaryColor = RGB(160,  40,  60);

        sc.Subgraph[SG_BULL_BRK].Name         = "Zone - Bullish Breaker";
        sc.Subgraph[SG_BULL_BRK].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BULL_BRK].PrimaryColor = RGB( 60, 190, 160);

        sc.Subgraph[SG_BEAR_BRK].Name         = "Zone - Bearish Breaker";
        sc.Subgraph[SG_BEAR_BRK].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BEAR_BRK].PrimaryColor = RGB(210,  80, 110);

        sc.Subgraph[SG_BULL_MB].Name         = "Zone - Bullish Mitigation";
        sc.Subgraph[SG_BULL_MB].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BULL_MB].PrimaryColor = RGB( 90, 160, 220);

        sc.Subgraph[SG_BEAR_MB].Name         = "Zone - Bearish Mitigation";
        sc.Subgraph[SG_BEAR_MB].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BEAR_MB].PrimaryColor = RGB(220, 140,  80);

        sc.Subgraph[SG_BULL_RB].Name         = "Zone - Bullish Rejection";
        sc.Subgraph[SG_BULL_RB].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BULL_RB].PrimaryColor = RGB(140, 200, 100);

        sc.Subgraph[SG_BEAR_RB].Name         = "Zone - Bearish Rejection";
        sc.Subgraph[SG_BEAR_RB].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BEAR_RB].PrimaryColor = RGB(200, 100, 140);

        sc.Subgraph[SG_BULL_FVG].Name         = "Zone - Bullish FVG";
        sc.Subgraph[SG_BULL_FVG].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BULL_FVG].PrimaryColor = RGB( 70, 170, 130);

        sc.Subgraph[SG_BEAR_FVG].Name         = "Zone - Bearish FVG";
        sc.Subgraph[SG_BEAR_FVG].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BEAR_FVG].PrimaryColor = RGB(190,  80,  90);

        sc.Subgraph[SG_BULL_GAP].Name         = "Zone - Bullish Opening Gap";
        sc.Subgraph[SG_BULL_GAP].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BULL_GAP].PrimaryColor = RGB(110, 190, 190);

        sc.Subgraph[SG_BEAR_GAP].Name         = "Zone - Bearish Opening Gap";
        sc.Subgraph[SG_BEAR_GAP].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BEAR_GAP].PrimaryColor = RGB(190, 150, 110);

        sc.Subgraph[SG_BULL_VOID].Name         = "Zone - Bullish Liquidity Void";
        sc.Subgraph[SG_BULL_VOID].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BULL_VOID].PrimaryColor = RGB( 90, 140, 200);

        sc.Subgraph[SG_BEAR_VOID].Name         = "Zone - Bearish Liquidity Void";
        sc.Subgraph[SG_BEAR_VOID].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BEAR_VOID].PrimaryColor = RGB(200, 120, 160);

        sc.Subgraph[SG_EQH].Name         = "Color - Equal Highs";
        sc.Subgraph[SG_EQH].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_EQH].PrimaryColor = RGB(230,  80,  80);

        sc.Subgraph[SG_EQL].Name         = "Color - Equal Lows";
        sc.Subgraph[SG_EQL].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_EQL].PrimaryColor = RGB( 80, 210, 130);

        sc.Subgraph[SG_PREMIUM].Name         = "Color - Premium Zone";
        sc.Subgraph[SG_PREMIUM].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_PREMIUM].PrimaryColor = RGB(180,  60,  60);

        sc.Subgraph[SG_DISCOUNT].Name         = "Color - Discount Zone";
        sc.Subgraph[SG_DISCOUNT].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_DISCOUNT].PrimaryColor = RGB( 60, 160, 100);

        sc.Subgraph[SG_EQUILIBRIUM].Name         = "Color - Equilibrium";
        sc.Subgraph[SG_EQUILIBRIUM].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_EQUILIBRIUM].PrimaryColor = RGB(160, 160, 160);

        sc.Subgraph[SG_OTE].Name         = "Color - OTE Zone";
        sc.Subgraph[SG_OTE].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_OTE].PrimaryColor = RGB(200, 180,  60);

        sc.Subgraph[SG_PDHL].Name         = "Color - Previous Day H/L";
        sc.Subgraph[SG_PDHL].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_PDHL].PrimaryColor = RGB(190, 190, 190);

        sc.Subgraph[SG_PWHL].Name         = "Color - Previous Week H/L";
        sc.Subgraph[SG_PWHL].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_PWHL].PrimaryColor = RGB(150, 150, 240);

        sc.Subgraph[SG_PMHL].Name         = "Color - Previous Month H/L";
        sc.Subgraph[SG_PMHL].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_PMHL].PrimaryColor = RGB(240, 150, 240);

        sc.Subgraph[SG_STRONG].Name         = "Color - Strong High/Low";
        sc.Subgraph[SG_STRONG].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_STRONG].PrimaryColor = RGB(255, 255, 255);

        sc.Subgraph[SG_WEAK].Name         = "Color - Weak High/Low";
        sc.Subgraph[SG_WEAK].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_WEAK].PrimaryColor = RGB(140, 140, 140);

        sc.Subgraph[SG_ATR].Name      = "Internal ATR (hidden)";
        sc.Subgraph[SG_ATR].DrawStyle = DRAWSTYLE_IGNORE;

        // ---------------- inputs ----------------
        sc.Input[IN_SWING_LEN].Name = "Structure: Swing (External) Pivot Length";
        sc.Input[IN_SWING_LEN].SetInt(25);
        sc.Input[IN_SWING_LEN].SetIntLimits(1, 500);

        sc.Input[IN_INT_LEN].Name = "Structure: Internal Pivot Length";
        sc.Input[IN_INT_LEN].SetInt(5);
        sc.Input[IN_INT_LEN].SetIntLimits(1, 500);

        sc.Input[IN_CONFIRM].Name = "Structure: Break Confirmation";
        sc.Input[IN_CONFIRM].SetCustomInputStrings("Candle Close;Wick");
        sc.Input[IN_CONFIRM].SetCustomInputIndex(0);

        sc.Input[IN_SHOW_EXT_STRUCT].Name = "Structure: Show External BOS / CHoCH";
        sc.Input[IN_SHOW_EXT_STRUCT].SetYesNo(1);

        sc.Input[IN_SHOW_INT_STRUCT].Name = "Structure: Show Internal BOS / CHoCH";
        sc.Input[IN_SHOW_INT_STRUCT].SetYesNo(1);

        sc.Input[IN_SHOW_STRONG_WEAK].Name = "Structure: Show Strong / Weak Highs & Lows";
        sc.Input[IN_SHOW_STRONG_WEAK].SetYesNo(1);

        sc.Input[IN_MAX_STRUCT_LABELS].Name = "Structure: Max Structure Labels Kept";
        sc.Input[IN_MAX_STRUCT_LABELS].SetInt(30);
        sc.Input[IN_MAX_STRUCT_LABELS].SetIntLimits(1, 500);

        sc.Input[IN_SHOW_OB].Name = "PD Arrays: Show Order Blocks";
        sc.Input[IN_SHOW_OB].SetYesNo(1);

        sc.Input[IN_OB_SOURCE].Name = "PD Arrays: Order Block Candle";
        sc.Input[IN_OB_SOURCE].SetCustomInputStrings("Last Opposite Candle;Extreme Candle");
        sc.Input[IN_OB_SOURCE].SetCustomInputIndex(0);

        sc.Input[IN_ZONE_DEF].Name = "PD Arrays: Zone Definition";
        sc.Input[IN_ZONE_DEF].SetCustomInputStrings("Full Candle Range;Body Only;Wick Only");
        sc.Input[IN_ZONE_DEF].SetCustomInputIndex(0);

        sc.Input[IN_INVALIDATION].Name = "PD Arrays: Zone Invalidation";
        sc.Input[IN_INVALIDATION].SetCustomInputStrings("Candle Close;Wick");
        sc.Input[IN_INVALIDATION].SetCustomInputIndex(0);

        sc.Input[IN_SHOW_BRK].Name = "PD Arrays: Show Breaker Blocks";
        sc.Input[IN_SHOW_BRK].SetYesNo(1);

        sc.Input[IN_SHOW_MB].Name = "PD Arrays: Show Mitigation Blocks";
        sc.Input[IN_SHOW_MB].SetYesNo(1);

        sc.Input[IN_SHOW_RB].Name = "PD Arrays: Show Rejection Blocks";
        sc.Input[IN_SHOW_RB].SetYesNo(1);

        sc.Input[IN_RB_WICK_RATIO].Name = "PD Arrays: Rejection Wick Ratio (%)";
        sc.Input[IN_RB_WICK_RATIO].SetFloat(60.0f);
        sc.Input[IN_RB_WICK_RATIO].SetFloatLimits(10.0f, 95.0f);

        sc.Input[IN_RB_MIN_WICK].Name = "PD Arrays: Rejection Min Wick (ATR x)";
        sc.Input[IN_RB_MIN_WICK].SetFloat(0.75f);

        sc.Input[IN_INTERNAL_OB].Name = "PD Arrays: Also Build Order Blocks From Internal Structure";
        sc.Input[IN_INTERNAL_OB].SetYesNo(0);

        sc.Input[IN_SHOW_FVG].Name = "Imbalance: Show Fair Value Gaps";
        sc.Input[IN_SHOW_FVG].SetYesNo(1);

        sc.Input[IN_FVG_MIN].Name = "Imbalance: FVG Min Size (ATR x)";
        sc.Input[IN_FVG_MIN].SetFloat(0.10f);

        sc.Input[IN_SHOW_GAP].Name = "Imbalance: Show Opening / Volume Gaps";
        sc.Input[IN_SHOW_GAP].SetYesNo(1);

        sc.Input[IN_GAP_MIN].Name = "Imbalance: Opening Gap Min Size (ATR x)";
        sc.Input[IN_GAP_MIN].SetFloat(0.05f);

        sc.Input[IN_SHOW_VOID].Name = "Imbalance: Show Liquidity Voids";
        sc.Input[IN_SHOW_VOID].SetYesNo(1);

        sc.Input[IN_VOID_MIN_BARS].Name = "Imbalance: Void Min Consecutive Bars";
        sc.Input[IN_VOID_MIN_BARS].SetInt(3);
        sc.Input[IN_VOID_MIN_BARS].SetIntLimits(2, 50);

        sc.Input[IN_VOID_MIN_SIZE].Name = "Imbalance: Void Min Leg Size (ATR x)";
        sc.Input[IN_VOID_MIN_SIZE].SetFloat(2.0f);

        sc.Input[IN_FILL_METHOD].Name = "Imbalance: Fill / Invalidate Method";
        sc.Input[IN_FILL_METHOD].SetCustomInputStrings("Wick Touch;Candle Close");
        sc.Input[IN_FILL_METHOD].SetCustomInputIndex(0);

        sc.Input[IN_SHOW_EQ].Name = "Liquidity: Show Equal Highs / Lows";
        sc.Input[IN_SHOW_EQ].SetYesNo(1);

        sc.Input[IN_EQ_LEN].Name = "Liquidity: EQH / EQL Pivot Length";
        sc.Input[IN_EQ_LEN].SetInt(3);
        sc.Input[IN_EQ_LEN].SetIntLimits(1, 100);

        sc.Input[IN_EQ_TOL].Name = "Liquidity: EQH / EQL Tolerance (ATR x)";
        sc.Input[IN_EQ_TOL].SetFloat(0.10f);

        sc.Input[IN_EQ_MAXBARS].Name = "Liquidity: EQH / EQL Max Bars Between";
        sc.Input[IN_EQ_MAXBARS].SetInt(100);
        sc.Input[IN_EQ_MAXBARS].SetIntLimits(2, 5000);

        sc.Input[IN_SHOW_SWEEP].Name = "Liquidity: Show Sweeps / Stop Hunts";
        sc.Input[IN_SHOW_SWEEP].SetYesNo(1);

        sc.Input[IN_SWEEP_PEN].Name = "Liquidity: Sweep Min Penetration (ATR x)";
        sc.Input[IN_SWEEP_PEN].SetFloat(0.0f);

        sc.Input[IN_MAX_LIQ].Name = "Liquidity: Max EQH / EQL Levels Kept";
        sc.Input[IN_MAX_LIQ].SetInt(20);
        sc.Input[IN_MAX_LIQ].SetIntLimits(1, 200);

        sc.Input[IN_SHOW_PD].Name = "Levels: Show Premium / Discount / Equilibrium";
        sc.Input[IN_SHOW_PD].SetYesNo(1);

        sc.Input[IN_SHOW_OTE].Name = "Levels: Show OTE Band (0.62 - 0.79)";
        sc.Input[IN_SHOW_OTE].SetYesNo(1);

        sc.Input[IN_SHOW_PDAY].Name = "Levels: Show Previous Day High / Low";
        sc.Input[IN_SHOW_PDAY].SetYesNo(1);

        sc.Input[IN_SHOW_PWEEK].Name = "Levels: Show Previous Week High / Low";
        sc.Input[IN_SHOW_PWEEK].SetYesNo(1);

        sc.Input[IN_SHOW_PMONTH].Name = "Levels: Show Previous Month High / Low";
        sc.Input[IN_SHOW_PMONTH].SetYesNo(0);

        sc.Input[IN_ATR_LEN].Name = "Display: ATR Length (sizing filters)";
        sc.Input[IN_ATR_LEN].SetInt(200);
        sc.Input[IN_ATR_LEN].SetIntLimits(2, 2000);

        sc.Input[IN_ZONE_EXTEND].Name = "Display: Zone Right Extension (bars)";
        sc.Input[IN_ZONE_EXTEND].SetInt(12);
        sc.Input[IN_ZONE_EXTEND].SetIntLimits(0, 500);

        sc.Input[IN_TRANSPARENCY].Name = "Display: Zone Transparency (0-100)";
        sc.Input[IN_TRANSPARENCY].SetInt(78);
        sc.Input[IN_TRANSPARENCY].SetIntLimits(0, 100);

        sc.Input[IN_MAX_ZONES].Name = "Display: Max Zones Per Type";
        sc.Input[IN_MAX_ZONES].SetInt(8);
        sc.Input[IN_MAX_ZONES].SetIntLimits(1, 100);

        sc.Input[IN_FONT_SIZE].Name = "Display: Label Font Size";
        sc.Input[IN_FONT_SIZE].SetInt(8);
        sc.Input[IN_FONT_SIZE].SetIntLimits(4, 40);

        sc.Input[IN_SHOW_LABELS].Name = "Display: Show Text Labels";
        sc.Input[IN_SHOW_LABELS].SetYesNo(1);

        sc.Input[IN_DELETE_INVALID].Name = "Display: Delete Invalidated Zones";
        sc.Input[IN_DELETE_INVALID].SetYesNo(1);

        sc.Input[IN_ALERTS_ON].Name = "Alerts: Master Enable";
        sc.Input[IN_ALERTS_ON].SetYesNo(1);

        sc.Input[IN_ALERT_SOUND].Name = "Alerts: Sound Number (0 = log only)";
        sc.Input[IN_ALERT_SOUND].SetInt(1);
        sc.Input[IN_ALERT_SOUND].SetIntLimits(0, 100);

        sc.Input[IN_ALERT_INTRABAR].Name = "Alerts: Allow Intrabar (Unconfirmed) Alerts";
        sc.Input[IN_ALERT_INTRABAR].SetYesNo(0);

        sc.Input[IN_AL_EXT_BOS].Name    = "Alert: External BOS";
        sc.Input[IN_AL_EXT_BOS].SetYesNo(1);
        sc.Input[IN_AL_EXT_CHOCH].Name  = "Alert: External CHoCH / MSS";
        sc.Input[IN_AL_EXT_CHOCH].SetYesNo(1);
        sc.Input[IN_AL_INT_BOS].Name    = "Alert: Internal BOS";
        sc.Input[IN_AL_INT_BOS].SetYesNo(0);
        sc.Input[IN_AL_INT_CHOCH].Name  = "Alert: Internal CHoCH";
        sc.Input[IN_AL_INT_CHOCH].SetYesNo(1);
        sc.Input[IN_AL_NEW_OB].Name     = "Alert: New Order Block";
        sc.Input[IN_AL_NEW_OB].SetYesNo(1);
        sc.Input[IN_AL_OB_TAP].Name     = "Alert: Zone Tap / Mitigation Entry";
        sc.Input[IN_AL_OB_TAP].SetYesNo(1);
        sc.Input[IN_AL_OB_INVALID].Name = "Alert: Zone Invalidated";
        sc.Input[IN_AL_OB_INVALID].SetYesNo(0);
        sc.Input[IN_AL_BRK].Name        = "Alert: Breaker Block Formed";
        sc.Input[IN_AL_BRK].SetYesNo(1);
        sc.Input[IN_AL_MB].Name         = "Alert: Mitigation Block Formed";
        sc.Input[IN_AL_MB].SetYesNo(1);
        sc.Input[IN_AL_RB].Name         = "Alert: Rejection Block Formed";
        sc.Input[IN_AL_RB].SetYesNo(0);
        sc.Input[IN_AL_FVG_NEW].Name    = "Alert: New Fair Value Gap";
        sc.Input[IN_AL_FVG_NEW].SetYesNo(0);
        sc.Input[IN_AL_FVG_FILL].Name   = "Alert: Fair Value Gap Filled";
        sc.Input[IN_AL_FVG_FILL].SetYesNo(0);
        sc.Input[IN_AL_GAP].Name        = "Alert: New Opening / Volume Gap";
        sc.Input[IN_AL_GAP].SetYesNo(0);
        sc.Input[IN_AL_VOID].Name       = "Alert: New Liquidity Void";
        sc.Input[IN_AL_VOID].SetYesNo(0);
        sc.Input[IN_AL_EQ].Name         = "Alert: Equal Highs / Lows Formed";
        sc.Input[IN_AL_EQ].SetYesNo(1);
        sc.Input[IN_AL_SWEEP].Name      = "Alert: Liquidity Sweep / Stop Hunt";
        sc.Input[IN_AL_SWEEP].SetYesNo(1);
        sc.Input[IN_AL_PD].Name         = "Alert: Premium / Discount Entry";
        sc.Input[IN_AL_PD].SetYesNo(0);
        sc.Input[IN_AL_HTF].Name        = "Alert: Previous Day/Week/Month Level Taken";
        sc.Input[IN_AL_HTF].SetYesNo(1);

        return;
    }

    //--------------------------------------------------------------------------
    //  Persistent state
    //--------------------------------------------------------------------------
    s_SMCState* S = (s_SMCState*)sc.GetPersistentPointer(0);

    if (sc.LastCallToFunction)
    {
        if (S != NULL)
        {
            delete S;
            sc.SetPersistentPointer(0, NULL);
        }
        return;
    }

    if (S == NULL)
    {
        S = new s_SMCState();
        if (S == NULL)
            return;
        sc.SetPersistentPointer(0, S);
        SMC_ResetState(*S);
    }

    if (sc.ArraySize < 10)
        return;

    //--------------------------------------------------------------------------
    //  Read inputs
    //--------------------------------------------------------------------------
    const int   SwingLen      = SMC_MaxI(1, sc.Input[IN_SWING_LEN].GetInt());
    const int   IntLen        = SMC_MaxI(1, sc.Input[IN_INT_LEN].GetInt());
    const bool  ConfirmWick   = (sc.Input[IN_CONFIRM].GetIndex() == 1);
    const bool  ShowExt       = sc.Input[IN_SHOW_EXT_STRUCT].GetYesNo() != 0;
    const bool  ShowInt       = sc.Input[IN_SHOW_INT_STRUCT].GetYesNo() != 0;
    const bool  ShowStrongWeak= sc.Input[IN_SHOW_STRONG_WEAK].GetYesNo() != 0;
    const int   MaxStructLbl  = SMC_MaxI(1, sc.Input[IN_MAX_STRUCT_LABELS].GetInt());

    const bool  ShowOB        = sc.Input[IN_SHOW_OB].GetYesNo() != 0;
    const int   OBSource      = sc.Input[IN_OB_SOURCE].GetIndex();
    const int   ZoneDef       = sc.Input[IN_ZONE_DEF].GetIndex();
    const bool  InvalidWick   = (sc.Input[IN_INVALIDATION].GetIndex() == 1);
    const bool  ShowBrk       = sc.Input[IN_SHOW_BRK].GetYesNo() != 0;
    const bool  ShowMB        = sc.Input[IN_SHOW_MB].GetYesNo() != 0;
    const bool  ShowRB        = sc.Input[IN_SHOW_RB].GetYesNo() != 0;
    const float RBWickRatio   = sc.Input[IN_RB_WICK_RATIO].GetFloat() / 100.0f;
    const float RBMinWick     = sc.Input[IN_RB_MIN_WICK].GetFloat();
    const bool  InternalOB    = sc.Input[IN_INTERNAL_OB].GetYesNo() != 0;

    const bool  ShowFVG       = sc.Input[IN_SHOW_FVG].GetYesNo() != 0;
    const float FVGMin        = sc.Input[IN_FVG_MIN].GetFloat();
    const bool  ShowGap       = sc.Input[IN_SHOW_GAP].GetYesNo() != 0;
    const float GapMin        = sc.Input[IN_GAP_MIN].GetFloat();
    const bool  ShowVoid      = sc.Input[IN_SHOW_VOID].GetYesNo() != 0;
    const int   VoidMinBars   = SMC_MaxI(2, sc.Input[IN_VOID_MIN_BARS].GetInt());
    const float VoidMinSize   = sc.Input[IN_VOID_MIN_SIZE].GetFloat();
    const bool  FillOnClose   = (sc.Input[IN_FILL_METHOD].GetIndex() == 1);

    const bool  ShowEQ        = sc.Input[IN_SHOW_EQ].GetYesNo() != 0;
    const int   EqLen         = SMC_MaxI(1, sc.Input[IN_EQ_LEN].GetInt());
    const float EqTol         = sc.Input[IN_EQ_TOL].GetFloat();
    const int   EqMaxBars     = SMC_MaxI(2, sc.Input[IN_EQ_MAXBARS].GetInt());
    const bool  ShowSweep     = sc.Input[IN_SHOW_SWEEP].GetYesNo() != 0;
    const float SweepPen      = sc.Input[IN_SWEEP_PEN].GetFloat();
    const int   MaxLiq        = SMC_MaxI(1, sc.Input[IN_MAX_LIQ].GetInt());

    const bool  ShowPD        = sc.Input[IN_SHOW_PD].GetYesNo() != 0;
    const bool  ShowOTE       = sc.Input[IN_SHOW_OTE].GetYesNo() != 0;
    const bool  ShowPDay      = sc.Input[IN_SHOW_PDAY].GetYesNo() != 0;
    const bool  ShowPWeek     = sc.Input[IN_SHOW_PWEEK].GetYesNo() != 0;
    const bool  ShowPMonth    = sc.Input[IN_SHOW_PMONTH].GetYesNo() != 0;

    const int   ATRLen        = SMC_MaxI(2, sc.Input[IN_ATR_LEN].GetInt());
    const int   ZoneExtend    = sc.Input[IN_ZONE_EXTEND].GetInt();
    const int   Transp        = sc.Input[IN_TRANSPARENCY].GetInt();
    const int   MaxZones      = SMC_MaxI(1, sc.Input[IN_MAX_ZONES].GetInt());
    const int   FontSize      = SMC_MaxI(4, sc.Input[IN_FONT_SIZE].GetInt());
    const bool  ShowLabels    = sc.Input[IN_SHOW_LABELS].GetYesNo() != 0;
    const bool  DeleteInvalid = sc.Input[IN_DELETE_INVALID].GetYesNo() != 0;

    const bool  AlertsOn      = sc.Input[IN_ALERTS_ON].GetYesNo() != 0;
    const int   AlertSound    = sc.Input[IN_ALERT_SOUND].GetInt();
    const bool  IntrabarAl    = sc.Input[IN_ALERT_INTRABAR].GetYesNo() != 0;

    const bool  AlExtBos      = sc.Input[IN_AL_EXT_BOS].GetYesNo()   != 0;
    const bool  AlExtChoch    = sc.Input[IN_AL_EXT_CHOCH].GetYesNo() != 0;
    const bool  AlIntBos      = sc.Input[IN_AL_INT_BOS].GetYesNo()   != 0;
    const bool  AlIntChoch    = sc.Input[IN_AL_INT_CHOCH].GetYesNo() != 0;
    const bool  AlNewOB       = sc.Input[IN_AL_NEW_OB].GetYesNo()    != 0;
    const bool  AlTap         = sc.Input[IN_AL_OB_TAP].GetYesNo()    != 0;
    const bool  AlInvalid     = sc.Input[IN_AL_OB_INVALID].GetYesNo()!= 0;
    const bool  AlBrk         = sc.Input[IN_AL_BRK].GetYesNo()       != 0;
    const bool  AlMB          = sc.Input[IN_AL_MB].GetYesNo()        != 0;
    const bool  AlRB          = sc.Input[IN_AL_RB].GetYesNo()        != 0;
    const bool  AlFVGNew      = sc.Input[IN_AL_FVG_NEW].GetYesNo()   != 0;
    const bool  AlFVGFill     = sc.Input[IN_AL_FVG_FILL].GetYesNo()  != 0;
    const bool  AlGap         = sc.Input[IN_AL_GAP].GetYesNo()       != 0;
    const bool  AlVoid        = sc.Input[IN_AL_VOID].GetYesNo()      != 0;
    const bool  AlEQ          = sc.Input[IN_AL_EQ].GetYesNo()        != 0;
    const bool  AlSweep       = sc.Input[IN_AL_SWEEP].GetYesNo()     != 0;
    const bool  AlPD          = sc.Input[IN_AL_PD].GetYesNo()        != 0;
    const bool  AlHTF         = sc.Input[IN_AL_HTF].GetYesNo()       != 0;

    const unsigned int ColBullStruct = sc.Subgraph[SG_BULL_STRUCT].PrimaryColor;
    const unsigned int ColBearStruct = sc.Subgraph[SG_BEAR_STRUCT].PrimaryColor;
    const unsigned int ColBullInt    = sc.Subgraph[SG_BULL_INT].PrimaryColor;
    const unsigned int ColBearInt    = sc.Subgraph[SG_BEAR_INT].PrimaryColor;

    const SCString Sym = sc.Symbol;

    //--------------------------------------------------------------------------
    //  Full recalculation -> wipe drawings and state
    //--------------------------------------------------------------------------
    if (sc.UpdateStartIndex == 0)
    {
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_ALL, 0);
        SMC_ResetState(*S);
    }

    //--------------------------------------------------------------------------
    //  ATR (used by every size filter)
    //--------------------------------------------------------------------------
    for (int i = sc.UpdateStartIndex; i < sc.ArraySize; ++i)
        sc.ATR(sc.BaseDataIn, sc.Subgraph[SG_ATR], i, ATRLen, MOVAVGTYPE_WILDERS);

    //--------------------------------------------------------------------------
    //  MAIN PASS - closed bars only (no repainting)
    //--------------------------------------------------------------------------
    const int LastClosed = sc.ArraySize - 2;
    int StartBar = S->LastProcessed + 1;
    if (StartBar < 1)
        StartBar = 1;

    for (int i = StartBar; i <= LastClosed; ++i)
    {
        S->LastProcessed = i;
        S->NeedRefresh   = 1;

        float ATRv = sc.Subgraph[SG_ATR][i];
        if (ATRv <= 0.0f)
            ATRv = sc.TickSize * 10.0f;

        const bool Live = AlertsOn && !sc.IsFullRecalculation && (i >= sc.ArraySize - 3);

        const float O = sc.Open[i];
        const float H = sc.High[i];
        const float L = sc.Low[i];
        const float C = sc.Close[i];

        SCString Msg;

        //======================================================================
        //  Previous Day / Week / Month tracking
        //======================================================================
        {
            const SCDateTime TDate = sc.GetTradingDayDate(sc.BaseDateTimeIn[i]);
            const int DayK = TDate.GetDate();
            const int WkK  = (DayK - 1) / 7;
            const int MoK  = TDate.GetYear() * 12 + TDate.GetMonth();

            if (S->DayKey != DayK)
            {
                if (S->DayKey != -1 && S->CurDayH > -FLT_MAX)
                {
                    S->PrevDayH = S->CurDayH;
                    S->PrevDayL = S->CurDayL;
                }
                S->DayKey      = DayK;
                S->DayStartIdx = i;
                S->CurDayH     = -FLT_MAX;
                S->CurDayL     =  FLT_MAX;
                S->PDHTaken    = 0;
                S->PDLTaken    = 0;
            }
            if (S->WkKey != WkK)
            {
                if (S->WkKey != -1 && S->CurWkH > -FLT_MAX)
                {
                    S->PrevWkH = S->CurWkH;
                    S->PrevWkL = S->CurWkL;
                }
                S->WkKey      = WkK;
                S->WkStartIdx = i;
                S->CurWkH     = -FLT_MAX;
                S->CurWkL     =  FLT_MAX;
                S->PWHTaken   = 0;
                S->PWLTaken   = 0;
            }
            if (S->MoKey != MoK)
            {
                if (S->MoKey != -1 && S->CurMoH > -FLT_MAX)
                {
                    S->PrevMoH = S->CurMoH;
                    S->PrevMoL = S->CurMoL;
                }
                S->MoKey      = MoK;
                S->MoStartIdx = i;
                S->CurMoH     = -FLT_MAX;
                S->CurMoL     =  FLT_MAX;
                S->PMHTaken   = 0;
                S->PMLTaken   = 0;
            }

            S->CurDayH = SMC_MaxF(S->CurDayH, H);  S->CurDayL = SMC_MinF(S->CurDayL, L);
            S->CurWkH  = SMC_MaxF(S->CurWkH,  H);  S->CurWkL  = SMC_MinF(S->CurWkL,  L);
            S->CurMoH  = SMC_MaxF(S->CurMoH,  H);  S->CurMoL  = SMC_MinF(S->CurMoL,  L);

            // ---- previous period levels raided ----
            if (AlHTF)
            {
                if (ShowPDay && S->PrevDayH > 0.0f && !S->PDHTaken && H > S->PrevDayH)
                {
                    S->PDHTaken = 1;
                    Msg.Format("ICT SMC [%s]: Previous Day HIGH taken (%s)", Sym.GetChars(),
                               sc.FormatGraphValue(S->PrevDayH, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }
                if (ShowPDay && S->PrevDayL > 0.0f && !S->PDLTaken && L < S->PrevDayL)
                {
                    S->PDLTaken = 1;
                    Msg.Format("ICT SMC [%s]: Previous Day LOW taken (%s)", Sym.GetChars(),
                               sc.FormatGraphValue(S->PrevDayL, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }
                if (ShowPWeek && S->PrevWkH > 0.0f && !S->PWHTaken && H > S->PrevWkH)
                {
                    S->PWHTaken = 1;
                    Msg.Format("ICT SMC [%s]: Previous Week HIGH taken (%s)", Sym.GetChars(),
                               sc.FormatGraphValue(S->PrevWkH, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }
                if (ShowPWeek && S->PrevWkL > 0.0f && !S->PWLTaken && L < S->PrevWkL)
                {
                    S->PWLTaken = 1;
                    Msg.Format("ICT SMC [%s]: Previous Week LOW taken (%s)", Sym.GetChars(),
                               sc.FormatGraphValue(S->PrevWkL, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }
                if (ShowPMonth && S->PrevMoH > 0.0f && !S->PMHTaken && H > S->PrevMoH)
                {
                    S->PMHTaken = 1;
                    Msg.Format("ICT SMC [%s]: Previous Month HIGH taken (%s)", Sym.GetChars(),
                               sc.FormatGraphValue(S->PrevMoH, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }
                if (ShowPMonth && S->PrevMoL > 0.0f && !S->PMLTaken && L < S->PrevMoL)
                {
                    S->PMLTaken = 1;
                    Msg.Format("ICT SMC [%s]: Previous Month LOW taken (%s)", Sym.GetChars(),
                               sc.FormatGraphValue(S->PrevMoL, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }
            }
        }

        //======================================================================
        //  3. IMBALANCES - Fair Value Gaps (3 candle)
        //======================================================================
        if (ShowFVG && i >= 2)
        {
            // bullish FVG : low of current bar above high of bar i-2
            if (sc.Low[i] > sc.High[i - 2])
            {
                const float GapTop = sc.Low[i];
                const float GapBot = sc.High[i - 2];
                if ((GapTop - GapBot) >= FVGMin * ATRv)
                {
                    SMC_AddZone(sc, *S, ZT_BULL_FVG, i - 1, i, GapTop, GapBot, 0, MaxZones);
                    if (AlFVGNew)
                    {
                        Msg.Format("ICT SMC [%s]: Bullish Fair Value Gap %s - %s", Sym.GetChars(),
                                   sc.FormatGraphValue(GapBot, sc.BaseGraphValueFormat).GetChars(),
                                   sc.FormatGraphValue(GapTop, sc.BaseGraphValueFormat).GetChars());
                        SMC_Alert(sc, Live, AlertSound, Msg);
                    }
                }
            }
            // bearish FVG : high of current bar below low of bar i-2
            else if (sc.High[i] < sc.Low[i - 2])
            {
                const float GapTop = sc.Low[i - 2];
                const float GapBot = sc.High[i];
                if ((GapTop - GapBot) >= FVGMin * ATRv)
                {
                    SMC_AddZone(sc, *S, ZT_BEAR_FVG, i - 1, i, GapTop, GapBot, 0, MaxZones);
                    if (AlFVGNew)
                    {
                        Msg.Format("ICT SMC [%s]: Bearish Fair Value Gap %s - %s", Sym.GetChars(),
                                   sc.FormatGraphValue(GapBot, sc.BaseGraphValueFormat).GetChars(),
                                   sc.FormatGraphValue(GapTop, sc.BaseGraphValueFormat).GetChars());
                        SMC_Alert(sc, Live, AlertSound, Msg);
                    }
                }
            }
        }

        //======================================================================
        //  3. IMBALANCES - Opening / Volume gaps (prior close -> current open)
        //======================================================================
        if (ShowGap && i >= 1)
        {
            const float PrevClose = sc.Close[i - 1];
            const float GapSize   = O - PrevClose;

            if (GapSize >= GapMin * ATRv && L > PrevClose)
            {
                SMC_AddZone(sc, *S, ZT_BULL_GAP, i - 1, i, O, PrevClose, 0, MaxZones);
                if (AlGap)
                {
                    Msg.Format("ICT SMC [%s]: Bullish opening/volume gap %s - %s", Sym.GetChars(),
                               sc.FormatGraphValue(PrevClose, sc.BaseGraphValueFormat).GetChars(),
                               sc.FormatGraphValue(O, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }
            }
            else if (-GapSize >= GapMin * ATRv && H < PrevClose)
            {
                SMC_AddZone(sc, *S, ZT_BEAR_GAP, i - 1, i, PrevClose, O, 0, MaxZones);
                if (AlGap)
                {
                    Msg.Format("ICT SMC [%s]: Bearish opening/volume gap %s - %s", Sym.GetChars(),
                               sc.FormatGraphValue(O, sc.BaseGraphValueFormat).GetChars(),
                               sc.FormatGraphValue(PrevClose, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }
            }
        }

        //======================================================================
        //  3. IMBALANCES - Liquidity Voids (fast one directional legs)
        //======================================================================
        if (ShowVoid && i >= VoidMinBars + 1)
        {
            const int DirNow  = (C > O) ? 1 : ((C < O) ? -1 : 0);
            const int DirPrev = (sc.Close[i - 1] > sc.Open[i - 1]) ? 1 :
                                ((sc.Close[i - 1] < sc.Open[i - 1]) ? -1 : 0);

            // the leg ended on bar i-1
            if (DirPrev != 0 && DirNow != DirPrev)
            {
                int Run = 0;
                for (int k = i - 1; k >= 0; --k)
                {
                    const int D = (sc.Close[k] > sc.Open[k]) ? 1 :
                                  ((sc.Close[k] < sc.Open[k]) ? -1 : 0);
                    if (D != DirPrev)
                        break;
                    ++Run;
                }

                if (Run >= VoidMinBars)
                {
                    const int StartIdx = i - Run;
                    const int EndIdx   = i - 1;

                    float LegHi = -FLT_MAX, LegLo = FLT_MAX;
                    for (int k = StartIdx; k <= EndIdx; ++k)
                    {
                        LegHi = SMC_MaxF(LegHi, sc.High[k]);
                        LegLo = SMC_MinF(LegLo, sc.Low[k]);
                    }

                    const float Displacement = std::fabs(sc.Close[EndIdx] - sc.Open[StartIdx]);
                    if (Displacement >= VoidMinSize * ATRv)
                    {
                        const int VType = (DirPrev > 0) ? ZT_BULL_VOID : ZT_BEAR_VOID;
                        SMC_AddZone(sc, *S, VType, StartIdx, i, LegHi, LegLo, 0, MaxZones);

                        if (AlVoid)
                        {
                            Msg.Format("ICT SMC [%s]: %s liquidity void %s - %s", Sym.GetChars(),
                                       (DirPrev > 0) ? "Bullish" : "Bearish",
                                       sc.FormatGraphValue(LegLo, sc.BaseGraphValueFormat).GetChars(),
                                       sc.FormatGraphValue(LegHi, sc.BaseGraphValueFormat).GetChars());
                            SMC_Alert(sc, Live, AlertSound, Msg);
                        }
                    }
                }
            }
        }

        //======================================================================
        //  2. PD ARRAYS - Rejection blocks (aggressive wick rejection)
        //======================================================================
        if (ShowRB)
        {
            const float Range     = H - L;
            const float BodyTop   = SMC_MaxF(O, C);
            const float BodyBot   = SMC_MinF(O, C);
            const float UpperWick = H - BodyTop;
            const float LowerWick = BodyBot - L;

            if (Range > 0.0f)
            {
                if (LowerWick / Range >= RBWickRatio && LowerWick >= RBMinWick * ATRv)
                {
                    SMC_AddZone(sc, *S, ZT_BULL_RB, i, i, BodyBot, L, 0, MaxZones);
                    if (AlRB)
                    {
                        Msg.Format("ICT SMC [%s]: Bullish rejection block %s - %s", Sym.GetChars(),
                                   sc.FormatGraphValue(L, sc.BaseGraphValueFormat).GetChars(),
                                   sc.FormatGraphValue(BodyBot, sc.BaseGraphValueFormat).GetChars());
                        SMC_Alert(sc, Live, AlertSound, Msg);
                    }
                }
                else if (UpperWick / Range >= RBWickRatio && UpperWick >= RBMinWick * ATRv)
                {
                    SMC_AddZone(sc, *S, ZT_BEAR_RB, i, i, H, BodyTop, 0, MaxZones);
                    if (AlRB)
                    {
                        Msg.Format("ICT SMC [%s]: Bearish rejection block %s - %s", Sym.GetChars(),
                                   sc.FormatGraphValue(BodyTop, sc.BaseGraphValueFormat).GetChars(),
                                   sc.FormatGraphValue(H, sc.BaseGraphValueFormat).GetChars());
                        SMC_Alert(sc, Live, AlertSound, Msg);
                    }
                }
            }
        }

        //======================================================================
        //  4. LIQUIDITY - Equal Highs / Equal Lows pivots
        //======================================================================
        if (ShowEQ)
        {
            const int P = i - EqLen;
            if (P > 0)
            {
                if (SMC_IsPivotHigh(sc, P, EqLen))
                {
                    if (S->EqHigh.Valid &&
                        (P - S->EqHigh.Index) <= EqMaxBars &&
                        std::fabs(sc.High[P] - S->EqHigh.Price) <= EqTol * ATRv)
                    {
                        s_SMCLiq Lq;
                        Lq.IsHigh     = 1;
                        Lq.StartIndex = S->EqHigh.Index;
                        Lq.EndIndex   = P;
                        Lq.Price      = SMC_MaxF(sc.High[P], S->EqHigh.Price);
                        Lq.Swept      = 0;
                        Lq.Line       = S->NextLine++;
                        Lq.TextLine   = S->NextLine++;
                        S->Liq.push_back(Lq);

                        if (AlEQ)
                        {
                            Msg.Format("ICT SMC [%s]: Equal Highs (buy-side liquidity) at %s",
                                       Sym.GetChars(),
                                       sc.FormatGraphValue(Lq.Price, sc.BaseGraphValueFormat).GetChars());
                            SMC_Alert(sc, Live, AlertSound, Msg);
                        }
                    }
                    S->EqHigh.Index = P;
                    S->EqHigh.Price = sc.High[P];
                    S->EqHigh.Valid = 1;
                }

                if (SMC_IsPivotLow(sc, P, EqLen))
                {
                    if (S->EqLow.Valid &&
                        (P - S->EqLow.Index) <= EqMaxBars &&
                        std::fabs(sc.Low[P] - S->EqLow.Price) <= EqTol * ATRv)
                    {
                        s_SMCLiq Lq;
                        Lq.IsHigh     = 0;
                        Lq.StartIndex = S->EqLow.Index;
                        Lq.EndIndex   = P;
                        Lq.Price      = SMC_MinF(sc.Low[P], S->EqLow.Price);
                        Lq.Swept      = 0;
                        Lq.Line       = S->NextLine++;
                        Lq.TextLine   = S->NextLine++;
                        S->Liq.push_back(Lq);

                        if (AlEQ)
                        {
                            Msg.Format("ICT SMC [%s]: Equal Lows (sell-side liquidity) at %s",
                                       Sym.GetChars(),
                                       sc.FormatGraphValue(Lq.Price, sc.BaseGraphValueFormat).GetChars());
                            SMC_Alert(sc, Live, AlertSound, Msg);
                        }
                    }
                    S->EqLow.Index = P;
                    S->EqLow.Price = sc.Low[P];
                    S->EqLow.Valid = 1;
                }

                while ((int)S->Liq.size() > MaxLiq)
                {
                    SMC_DeleteDrawing(sc, S->Liq[0].Line);
                    SMC_DeleteDrawing(sc, S->Liq[0].TextLine);
                    S->Liq.erase(S->Liq.begin());
                }
            }
        }

        //======================================================================
        //  1. STRUCTURE - internal pivots
        //======================================================================
        {
            const int P = i - IntLen;
            if (P > 0)
            {
                if (SMC_IsPivotHigh(sc, P, IntLen))
                {
                    S->InHigh.Index   = P;
                    S->InHigh.Price   = sc.High[P];
                    S->InHigh.Crossed = 0;
                    S->InHigh.Swept   = 0;
                    S->InHigh.Valid   = 1;
                }
                if (SMC_IsPivotLow(sc, P, IntLen))
                {
                    S->InLow.Index   = P;
                    S->InLow.Price   = sc.Low[P];
                    S->InLow.Crossed = 0;
                    S->InLow.Swept   = 0;
                    S->InLow.Valid   = 1;
                }
            }
        }

        //======================================================================
        //  1. STRUCTURE - external (swing) pivots
        //======================================================================
        {
            const int P = i - SwingLen;
            if (P > 0)
            {
                if (SMC_IsPivotHigh(sc, P, SwingLen))
                {
                    S->PrevSwHigh     = S->SwHigh;
                    S->SwHigh.Index   = P;
                    S->SwHigh.Price   = sc.High[P];
                    S->SwHigh.Crossed = 0;
                    S->SwHigh.Swept   = 0;
                    S->SwHigh.Valid   = 1;

                    S->RangeHigh    = sc.High[P];
                    S->RangeHighIdx = P;
                }
                if (SMC_IsPivotLow(sc, P, SwingLen))
                {
                    S->PrevSwLow     = S->SwLow;
                    S->SwLow.Index   = P;
                    S->SwLow.Price   = sc.Low[P];
                    S->SwLow.Crossed = 0;
                    S->SwLow.Swept   = 0;
                    S->SwLow.Valid   = 1;

                    S->RangeLow    = sc.Low[P];
                    S->RangeLowIdx = P;
                }
            }

            if (S->RangeHighIdx >= 0 && H > S->RangeHigh) { S->RangeHigh = H; S->RangeHighIdx = i; }
            if (S->RangeLowIdx  >= 0 && L < S->RangeLow ) { S->RangeLow  = L; S->RangeLowIdx  = i; }
        }

        //======================================================================
        //  4. LIQUIDITY - sweeps / stop hunts
        //======================================================================
        if (ShowSweep)
        {
            const float Pen = SweepPen * ATRv;

            // --- swing high swept but not closed above (buy side raid) ---
            if (S->SwHigh.Valid && !S->SwHigh.Crossed && !S->SwHigh.Swept &&
                H > S->SwHigh.Price + Pen && C < S->SwHigh.Price)
            {
                S->SwHigh.Swept = 1;
                sc.Subgraph[SG_SWEEP_BEAR][i] = H + ATRv * 0.35f;

                if (AlSweep)
                {
                    Msg.Format("ICT SMC [%s]: Buy-side liquidity SWEEP above swing high %s (stop hunt)",
                               Sym.GetChars(),
                               sc.FormatGraphValue(S->SwHigh.Price, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }
            }

            // --- swing low swept but not closed below (sell side raid) ---
            if (S->SwLow.Valid && !S->SwLow.Crossed && !S->SwLow.Swept &&
                L < S->SwLow.Price - Pen && C > S->SwLow.Price)
            {
                S->SwLow.Swept = 1;
                sc.Subgraph[SG_SWEEP_BULL][i] = L - ATRv * 0.35f;

                if (AlSweep)
                {
                    Msg.Format("ICT SMC [%s]: Sell-side liquidity SWEEP below swing low %s (stop hunt)",
                               Sym.GetChars(),
                               sc.FormatGraphValue(S->SwLow.Price, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }
            }

            // --- EQH / EQL pools ---
            for (int k = 0; k < (int)S->Liq.size(); ++k)
            {
                s_SMCLiq& Lq = S->Liq[k];
                if (Lq.Swept || Lq.EndIndex >= i)
                    continue;

                if (Lq.IsHigh && H > Lq.Price + Pen)
                {
                    Lq.Swept = 1;
                    if (C < Lq.Price)
                        sc.Subgraph[SG_SWEEP_BEAR][i] = H + ATRv * 0.35f;

                    if (AlSweep)
                    {
                        Msg.Format("ICT SMC [%s]: Equal Highs taken at %s (buy-side liquidity)",
                                   Sym.GetChars(),
                                   sc.FormatGraphValue(Lq.Price, sc.BaseGraphValueFormat).GetChars());
                        SMC_Alert(sc, Live, AlertSound, Msg);
                    }
                }
                else if (!Lq.IsHigh && L < Lq.Price - Pen)
                {
                    Lq.Swept = 1;
                    if (C > Lq.Price)
                        sc.Subgraph[SG_SWEEP_BULL][i] = L - ATRv * 0.35f;

                    if (AlSweep)
                    {
                        Msg.Format("ICT SMC [%s]: Equal Lows taken at %s (sell-side liquidity)",
                                   Sym.GetChars(),
                                   sc.FormatGraphValue(Lq.Price, sc.BaseGraphValueFormat).GetChars());
                        SMC_Alert(sc, Live, AlertSound, Msg);
                    }
                }
            }
        }

        //======================================================================
        //  1. STRUCTURE - INTERNAL break of structure / change of character
        //======================================================================
        {
            const float UpTest = ConfirmWick ? H : C;
            const float DnTest = ConfirmWick ? L : C;

            if (S->InHigh.Valid && !S->InHigh.Crossed && UpTest > S->InHigh.Price)
            {
                const bool IsChoch = (S->InTrend == -1);
                S->InHigh.Crossed  = 1;
                S->InTrend         = 1;

                if (ShowInt)
                    SMC_AddStructureDrawing(sc, *S, S->InHigh.Index, i, S->InHigh.Price,
                                            IsChoch ? "iCHoCH" : "iBOS", ColBullInt,
                                            LINESTYLE_DOT, SMC_MaxI(4, FontSize - 2),
                                            ShowLabels ? 1 : 0, MaxStructLbl);

                if ((IsChoch && AlIntChoch) || (!IsChoch && AlIntBos))
                {
                    Msg.Format("ICT SMC [%s]: Bullish internal %s above %s", Sym.GetChars(),
                               IsChoch ? "CHoCH" : "BOS",
                               sc.FormatGraphValue(S->InHigh.Price, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }

                if (InternalOB && ShowOB)
                {
                    const int RangeStart = SMC_MaxI(0, S->InHigh.Index);
                    int ObIdx = -1;
                    for (int j = i - 1; j >= RangeStart; --j)
                        if (sc.Close[j] < sc.Open[j]) { ObIdx = j; break; }
                    if (ObIdx >= 0)
                        SMC_AddZone(sc, *S, ZT_BULL_OB, ObIdx, i,
                                    sc.High[ObIdx], sc.Low[ObIdx], 1, MaxZones);
                }
            }

            if (S->InLow.Valid && !S->InLow.Crossed && DnTest < S->InLow.Price)
            {
                const bool IsChoch = (S->InTrend == 1);
                S->InLow.Crossed   = 1;
                S->InTrend         = -1;

                if (ShowInt)
                    SMC_AddStructureDrawing(sc, *S, S->InLow.Index, i, S->InLow.Price,
                                            IsChoch ? "iCHoCH" : "iBOS", ColBearInt,
                                            LINESTYLE_DOT, SMC_MaxI(4, FontSize - 2),
                                            ShowLabels ? 1 : 0, MaxStructLbl);

                if ((IsChoch && AlIntChoch) || (!IsChoch && AlIntBos))
                {
                    Msg.Format("ICT SMC [%s]: Bearish internal %s below %s", Sym.GetChars(),
                               IsChoch ? "CHoCH" : "BOS",
                               sc.FormatGraphValue(S->InLow.Price, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }

                if (InternalOB && ShowOB)
                {
                    const int RangeStart = SMC_MaxI(0, S->InLow.Index);
                    int ObIdx = -1;
                    for (int j = i - 1; j >= RangeStart; --j)
                        if (sc.Close[j] > sc.Open[j]) { ObIdx = j; break; }
                    if (ObIdx >= 0)
                        SMC_AddZone(sc, *S, ZT_BEAR_OB, ObIdx, i,
                                    sc.High[ObIdx], sc.Low[ObIdx], 1, MaxZones);
                }
            }
        }

        //======================================================================
        //  1. STRUCTURE - EXTERNAL break of structure / change of character
        //     plus order blocks and mitigation blocks
        //======================================================================
        {
            const float UpTest = ConfirmWick ? H : C;
            const float DnTest = ConfirmWick ? L : C;

            //------------------------------------------------------ bullish
            if (S->SwHigh.Valid && !S->SwHigh.Crossed && UpTest > S->SwHigh.Price)
            {
                const bool IsChoch = (S->SwTrend == -1);
                const int  PivotIdx = S->SwHigh.Index;
                const float PivotPx = S->SwHigh.Price;

                S->SwHigh.Crossed = 1;
                S->SwTrend        = 1;

                if (ShowExt)
                    SMC_AddStructureDrawing(sc, *S, PivotIdx, i, PivotPx,
                                            IsChoch ? "CHoCH" : "BOS", ColBullStruct,
                                            LINESTYLE_SOLID, FontSize,
                                            ShowLabels ? 1 : 0, MaxStructLbl);

                if ((IsChoch && AlExtChoch) || (!IsChoch && AlExtBos))
                {
                    Msg.Format("ICT SMC [%s]: BULLISH %s - close above swing high %s",
                               Sym.GetChars(), IsChoch ? "CHoCH / MSS" : "BOS",
                               sc.FormatGraphValue(PivotPx, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }

                // ---------------- bullish order block ----------------
                if (ShowOB)
                {
                    const int RangeStart = SMC_MaxI(0, SMC_MinI(PivotIdx, S->SwLow.Valid ? S->SwLow.Index : PivotIdx));
                    int ObIdx = -1;

                    if (OBSource == 0)
                    {
                        for (int j = i - 1; j >= RangeStart; --j)
                            if (sc.Close[j] < sc.Open[j]) { ObIdx = j; break; }
                    }

                    if (ObIdx < 0 || OBSource == 1)
                    {
                        float Lowest = FLT_MAX;
                        for (int j = i; j >= RangeStart; --j)
                            if (sc.Low[j] < Lowest) { Lowest = sc.Low[j]; ObIdx = j; }
                    }

                    if (ObIdx >= 0)
                    {
                        float Top, Bot;
                        const float BodyTop = SMC_MaxF(sc.Open[ObIdx], sc.Close[ObIdx]);
                        const float BodyBot = SMC_MinF(sc.Open[ObIdx], sc.Close[ObIdx]);

                        if (ZoneDef == 1)      { Top = BodyTop;         Bot = BodyBot; }
                        else if (ZoneDef == 2) { Top = BodyBot;         Bot = sc.Low[ObIdx]; }
                        else                   { Top = sc.High[ObIdx];  Bot = sc.Low[ObIdx]; }

                        SMC_AddZone(sc, *S, ZT_BULL_OB, ObIdx, i, Top, Bot, 0, MaxZones);

                        if (AlNewOB)
                        {
                            Msg.Format("ICT SMC [%s]: New BULLISH Order Block %s - %s", Sym.GetChars(),
                                       sc.FormatGraphValue(Bot, sc.BaseGraphValueFormat).GetChars(),
                                       sc.FormatGraphValue(Top, sc.BaseGraphValueFormat).GetChars());
                            SMC_Alert(sc, Live, AlertSound, Msg);
                        }
                    }
                }

                // ------- bullish mitigation block (failure swing: higher low) -------
                if (ShowMB && S->SwLow.Valid && S->PrevSwLow.Valid &&
                    S->SwLow.Price > S->PrevSwLow.Price)
                {
                    int MbIdx = S->SwLow.Index;
                    for (int j = S->SwLow.Index; j >= SMC_MaxI(0, S->SwLow.Index - 5); --j)
                        if (sc.Close[j] < sc.Open[j]) { MbIdx = j; break; }

                    SMC_AddZone(sc, *S, ZT_BULL_MB, MbIdx, i,
                                SMC_MaxF(sc.Open[MbIdx], sc.Close[MbIdx]), sc.Low[MbIdx], 0, MaxZones);

                    if (AlMB)
                    {
                        Msg.Format("ICT SMC [%s]: Bullish Mitigation Block formed at %s (failure swing)",
                                   Sym.GetChars(),
                                   sc.FormatGraphValue(sc.Low[MbIdx], sc.BaseGraphValueFormat).GetChars());
                        SMC_Alert(sc, Live, AlertSound, Msg);
                    }
                }
            }

            //------------------------------------------------------ bearish
            if (S->SwLow.Valid && !S->SwLow.Crossed && DnTest < S->SwLow.Price)
            {
                const bool IsChoch = (S->SwTrend == 1);
                const int  PivotIdx = S->SwLow.Index;
                const float PivotPx = S->SwLow.Price;

                S->SwLow.Crossed = 1;
                S->SwTrend       = -1;

                if (ShowExt)
                    SMC_AddStructureDrawing(sc, *S, PivotIdx, i, PivotPx,
                                            IsChoch ? "CHoCH" : "BOS", ColBearStruct,
                                            LINESTYLE_SOLID, FontSize,
                                            ShowLabels ? 1 : 0, MaxStructLbl);

                if ((IsChoch && AlExtChoch) || (!IsChoch && AlExtBos))
                {
                    Msg.Format("ICT SMC [%s]: BEARISH %s - close below swing low %s",
                               Sym.GetChars(), IsChoch ? "CHoCH / MSS" : "BOS",
                               sc.FormatGraphValue(PivotPx, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }

                // ---------------- bearish order block ----------------
                if (ShowOB)
                {
                    const int RangeStart = SMC_MaxI(0, SMC_MinI(PivotIdx, S->SwHigh.Valid ? S->SwHigh.Index : PivotIdx));
                    int ObIdx = -1;

                    if (OBSource == 0)
                    {
                        for (int j = i - 1; j >= RangeStart; --j)
                            if (sc.Close[j] > sc.Open[j]) { ObIdx = j; break; }
                    }

                    if (ObIdx < 0 || OBSource == 1)
                    {
                        float Highest = -FLT_MAX;
                        for (int j = i; j >= RangeStart; --j)
                            if (sc.High[j] > Highest) { Highest = sc.High[j]; ObIdx = j; }
                    }

                    if (ObIdx >= 0)
                    {
                        float Top, Bot;
                        const float BodyTop = SMC_MaxF(sc.Open[ObIdx], sc.Close[ObIdx]);
                        const float BodyBot = SMC_MinF(sc.Open[ObIdx], sc.Close[ObIdx]);

                        if (ZoneDef == 1)      { Top = BodyTop;        Bot = BodyBot; }
                        else if (ZoneDef == 2) { Top = sc.High[ObIdx]; Bot = BodyTop; }
                        else                   { Top = sc.High[ObIdx]; Bot = sc.Low[ObIdx]; }

                        SMC_AddZone(sc, *S, ZT_BEAR_OB, ObIdx, i, Top, Bot, 0, MaxZones);

                        if (AlNewOB)
                        {
                            Msg.Format("ICT SMC [%s]: New BEARISH Order Block %s - %s", Sym.GetChars(),
                                       sc.FormatGraphValue(Bot, sc.BaseGraphValueFormat).GetChars(),
                                       sc.FormatGraphValue(Top, sc.BaseGraphValueFormat).GetChars());
                            SMC_Alert(sc, Live, AlertSound, Msg);
                        }
                    }
                }

                // ------- bearish mitigation block (failure swing: lower high) -------
                if (ShowMB && S->SwHigh.Valid && S->PrevSwHigh.Valid &&
                    S->SwHigh.Price < S->PrevSwHigh.Price)
                {
                    int MbIdx = S->SwHigh.Index;
                    for (int j = S->SwHigh.Index; j >= SMC_MaxI(0, S->SwHigh.Index - 5); --j)
                        if (sc.Close[j] > sc.Open[j]) { MbIdx = j; break; }

                    SMC_AddZone(sc, *S, ZT_BEAR_MB, MbIdx, i,
                                sc.High[MbIdx], SMC_MinF(sc.Open[MbIdx], sc.Close[MbIdx]), 0, MaxZones);

                    if (AlMB)
                    {
                        Msg.Format("ICT SMC [%s]: Bearish Mitigation Block formed at %s (failure swing)",
                                   Sym.GetChars(),
                                   sc.FormatGraphValue(sc.High[MbIdx], sc.BaseGraphValueFormat).GetChars());
                        SMC_Alert(sc, Live, AlertSound, Msg);
                    }
                }
            }
        }

        //======================================================================
        //  ZONE MAINTENANCE - taps, invalidation, breaker conversion
        //======================================================================
        for (int k = 0; k < (int)S->Zones.size(); )
        {
            s_SMCZone& Z = S->Zones[k];

            if (!Z.Active || Z.Created >= i)
            {
                ++k;
                continue;
            }

            const bool Bull   = ZoneIsBull(Z.Type);
            const int  Family = ZoneFamily(Z.Type);
            const bool IsImbalance = (Family == FAM_FVG || Family == FAM_GAP || Family == FAM_VOID);

            // ---- first tap / mitigation entry ----
            if (!Z.Tapped && L <= Z.Top && H >= Z.Bottom)
            {
                Z.Tapped = 1;
                if (AlTap)
                {
                    Msg.Format("ICT SMC [%s]: Price tapped %s zone %s - %s", Sym.GetChars(),
                               ZoneLabel[Z.Type],
                               sc.FormatGraphValue(Z.Bottom, sc.BaseGraphValueFormat).GetChars(),
                               sc.FormatGraphValue(Z.Top, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }
            }

            // ---- invalidation / fill ----
            bool Invalid = false;
            if (IsImbalance)
            {
                const float TestV = FillOnClose ? C : (Bull ? L : H);
                Invalid = Bull ? (TestV <= Z.Bottom) : (TestV >= Z.Top);
            }
            else
            {
                const float TestV = InvalidWick ? (Bull ? L : H) : C;
                Invalid = Bull ? (TestV < Z.Bottom) : (TestV > Z.Top);
            }

            if (Invalid)
            {
                const bool CanBreak = ShowBrk && !Z.WasBreaker &&
                                      (Family == FAM_OB || Family == FAM_MB);

                if (CanBreak)
                {
                    // failed order block -> breaker block with flipped polarity
                    Z.Type       = Bull ? ZT_BEAR_BRK : ZT_BULL_BRK;
                    Z.WasBreaker = 1;
                    Z.Tapped     = 0;
                    Z.Created    = i;
                    Z.Active     = 1;

                    if (AlBrk)
                    {
                        Msg.Format("ICT SMC [%s]: %s Breaker Block formed %s - %s", Sym.GetChars(),
                                   Bull ? "Bearish" : "Bullish",
                                   sc.FormatGraphValue(Z.Bottom, sc.BaseGraphValueFormat).GetChars(),
                                   sc.FormatGraphValue(Z.Top, sc.BaseGraphValueFormat).GetChars());
                        SMC_Alert(sc, Live, AlertSound, Msg);
                    }

                    // NOTE: pruning is deferred until after this loop so the
                    // vector is not reordered while it is being iterated.
                    ++k;
                    continue;
                }

                if (IsImbalance && AlFVGFill)
                {
                    Msg.Format("ICT SMC [%s]: %s %s filled / rebalanced", Sym.GetChars(),
                               Bull ? "Bullish" : "Bearish", ZoneLabel[Z.Type]);
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }
                else if (!IsImbalance && AlInvalid)
                {
                    Msg.Format("ICT SMC [%s]: %s zone invalidated (%s - %s)", Sym.GetChars(),
                               ZoneLabel[Z.Type],
                               sc.FormatGraphValue(Z.Bottom, sc.BaseGraphValueFormat).GetChars(),
                               sc.FormatGraphValue(Z.Top, sc.BaseGraphValueFormat).GetChars());
                    SMC_Alert(sc, Live, AlertSound, Msg);
                }

                if (DeleteInvalid)
                {
                    SMC_DeleteDrawing(sc, Z.BoxLine);
                    SMC_DeleteDrawing(sc, Z.TextLine);
                    S->Zones.erase(S->Zones.begin() + k);
                    continue;
                }

                Z.Active   = 0;
                Z.EndIndex = i;
            }

            ++k;
        }

        // deferred prune for zones that were converted into breaker blocks
        SMC_PruneZones(sc, *S, FAM_BRK, MaxZones);

        //======================================================================
        //  5. Premium / Discount state change
        //======================================================================
        if (AlPD && S->RangeHighIdx >= 0 && S->RangeLowIdx >= 0 &&
            S->RangeHigh > S->RangeLow)
        {
            const float Eq = (S->RangeHigh + S->RangeLow) * 0.5f;
            const int NewZone = (C > Eq) ? 1 : ((C < Eq) ? -1 : 0);

            if (NewZone != 0 && NewZone != S->PDZone)
            {
                S->PDZone = NewZone;
                Msg.Format("ICT SMC [%s]: Price trading into %s (equilibrium %s)", Sym.GetChars(),
                           (NewZone > 0) ? "PREMIUM" : "DISCOUNT",
                           sc.FormatGraphValue(Eq, sc.BaseGraphValueFormat).GetChars());
                SMC_Alert(sc, Live, AlertSound, Msg);
            }
        }
    }

    //--------------------------------------------------------------------------
    //  Optional intrabar (unconfirmed) alerts on the forming bar
    //--------------------------------------------------------------------------
    if (IntrabarAl && AlertsOn && !sc.IsFullRecalculation && sc.ArraySize >= 2)
    {
        const int i = sc.ArraySize - 1;
        SCString Msg;

        float ATRv = sc.Subgraph[SG_ATR][i];
        if (ATRv <= 0.0f)
            ATRv = sc.TickSize * 10.0f;

        if (S->SwHigh.Valid && !S->SwHigh.Crossed && sc.Close[i] > S->SwHigh.Price &&
            S->IntrabarUpIdx != i)
        {
            S->IntrabarUpIdx = i;
            const bool IsChoch = (S->SwTrend == -1);
            if ((IsChoch && AlExtChoch) || (!IsChoch && AlExtBos))
            {
                Msg.Format("ICT SMC [%s]: (unconfirmed) bullish %s above %s", Sym.GetChars(),
                           IsChoch ? "CHoCH / MSS" : "BOS",
                           sc.FormatGraphValue(S->SwHigh.Price, sc.BaseGraphValueFormat).GetChars());
                SMC_Alert(sc, true, AlertSound, Msg);
            }
        }

        if (S->SwLow.Valid && !S->SwLow.Crossed && sc.Close[i] < S->SwLow.Price &&
            S->IntrabarDnIdx != i)
        {
            S->IntrabarDnIdx = i;
            const bool IsChoch = (S->SwTrend == 1);
            if ((IsChoch && AlExtChoch) || (!IsChoch && AlExtBos))
            {
                Msg.Format("ICT SMC [%s]: (unconfirmed) bearish %s below %s", Sym.GetChars(),
                           IsChoch ? "CHoCH / MSS" : "BOS",
                           sc.FormatGraphValue(S->SwLow.Price, sc.BaseGraphValueFormat).GetChars());
                SMC_Alert(sc, true, AlertSound, Msg);
            }
        }

        if (AlSweep && ShowSweep && S->IntrabarSweepIdx != i)
        {
            const float Pen = SweepPen * ATRv;

            if (S->SwHigh.Valid && !S->SwHigh.Crossed && !S->SwHigh.Swept &&
                sc.High[i] > S->SwHigh.Price + Pen && sc.Close[i] < S->SwHigh.Price)
            {
                S->IntrabarSweepIdx = i;
                Msg.Format("ICT SMC [%s]: (unconfirmed) buy-side sweep above %s", Sym.GetChars(),
                           sc.FormatGraphValue(S->SwHigh.Price, sc.BaseGraphValueFormat).GetChars());
                SMC_Alert(sc, true, AlertSound, Msg);
            }
            else if (S->SwLow.Valid && !S->SwLow.Crossed && !S->SwLow.Swept &&
                     sc.Low[i] < S->SwLow.Price - Pen && sc.Close[i] > S->SwLow.Price)
            {
                S->IntrabarSweepIdx = i;
                Msg.Format("ICT SMC [%s]: (unconfirmed) sell-side sweep below %s", Sym.GetChars(),
                           sc.FormatGraphValue(S->SwLow.Price, sc.BaseGraphValueFormat).GetChars());
                SMC_Alert(sc, true, AlertSound, Msg);
            }
        }
    }

    //--------------------------------------------------------------------------
    //  DRAWING REFRESH (only when the bar count changes or something changed)
    //--------------------------------------------------------------------------
    const bool NewBar = (sc.ArraySize != S->LastArraySize);
    if (!NewBar && !S->NeedRefresh)
        return;

    S->LastArraySize = sc.ArraySize;
    S->NeedRefresh   = 0;

    const int RightEdge = sc.ArraySize - 1 + ZoneExtend;

    const bool FamilyVisible[FAM_COUNT] =
    {
        ShowOB, ShowBrk, ShowMB, ShowRB, ShowFVG, ShowGap, ShowVoid
    };

    //--------------------------- zones ---------------------------
    for (int k = 0; k < (int)S->Zones.size(); ++k)
    {
        s_SMCZone& Z = S->Zones[k];
        const int Family = ZoneFamily(Z.Type);

        if (!FamilyVisible[Family])
        {
            SMC_DeleteDrawing(sc, Z.BoxLine);
            SMC_DeleteDrawing(sc, Z.TextLine);
            continue;
        }

        const unsigned int Color = sc.Subgraph[SG_BULL_OB + Z.Type].PrimaryColor;
        const int End = Z.Active ? RightEdge : Z.EndIndex;

        SMC_DrawBox(sc, Z.BoxLine, Z.StartIndex, End, Z.Top, Z.Bottom, Color, Transp,
                    Z.Active ? LINESTYLE_SOLID : LINESTYLE_DOT);

        const bool WantText = ShowLabels &&
                              (Family == FAM_OB || Family == FAM_BRK ||
                               Family == FAM_MB || Family == FAM_RB);

        if (WantText)
        {
            SCString Label = ZoneLabel[Z.Type];
            if (Z.Internal)
                Label += " (i)";

            SMC_DrawText(sc, Z.TextLine, End, (Z.Top + Z.Bottom) * 0.5f, Label.GetChars(),
                         Color, SMC_MaxI(4, FontSize - 1), DT_RIGHT | DT_VCENTER, 0);
        }
        else
        {
            SMC_DeleteDrawing(sc, Z.TextLine);
        }
    }

    //------------------------ EQH / EQL ------------------------
    for (int k = 0; k < (int)S->Liq.size(); ++k)
    {
        s_SMCLiq& Lq = S->Liq[k];

        if (!ShowEQ)
        {
            SMC_DeleteDrawing(sc, Lq.Line);
            SMC_DeleteDrawing(sc, Lq.TextLine);
            continue;
        }

        const unsigned int Color = Lq.IsHigh ? sc.Subgraph[SG_EQH].PrimaryColor
                                             : sc.Subgraph[SG_EQL].PrimaryColor;
        const int End = Lq.Swept ? Lq.EndIndex + 1 : RightEdge;

        SMC_DrawLine(sc, Lq.Line, Lq.StartIndex, End, Lq.Price, Lq.Price, Color, 1,
                     Lq.Swept ? LINESTYLE_DOT : LINESTYLE_DASH);

        if (ShowLabels)
        {
            SCString Txt = Lq.IsHigh ? "EQH" : "EQL";
            if (Lq.Swept)
                Txt += " x";
            SMC_DrawText(sc, Lq.TextLine, End, Lq.Price, Txt.GetChars(), Color,
                         SMC_MaxI(4, FontSize - 1),
                         DT_RIGHT | (Lq.IsHigh ? DT_BOTTOM : DT_TOP), 0);
        }
        else
        {
            SMC_DeleteDrawing(sc, Lq.TextLine);
        }
    }

    //--------------- strong / weak highs and lows ---------------
    if (ShowStrongWeak && S->SwHigh.Valid && S->SwLow.Valid)
    {
        const unsigned int ColStrong = sc.Subgraph[SG_STRONG].PrimaryColor;
        const unsigned int ColWeak   = sc.Subgraph[SG_WEAK].PrimaryColor;

        // In an uptrend the low that created the break is strong, the high is weak.
        const bool BullTrend = (S->SwTrend >= 0);

        SMC_DrawText(sc, LN_STRONG_HIGH, S->SwHigh.Index, S->SwHigh.Price,
                     BullTrend ? "Weak High" : "Strong High",
                     BullTrend ? ColWeak : ColStrong,
                     FontSize, DT_CENTER | DT_BOTTOM, 0);

        SMC_DrawText(sc, LN_STRONG_LOW, S->SwLow.Index, S->SwLow.Price,
                     BullTrend ? "Strong Low" : "Weak Low",
                     BullTrend ? ColStrong : ColWeak,
                     FontSize, DT_CENTER | DT_TOP, 0);
    }
    else
    {
        SMC_DeleteDrawing(sc, LN_STRONG_HIGH);
        SMC_DeleteDrawing(sc, LN_STRONG_LOW);
    }

    //--------------- premium / discount / equilibrium / OTE ---------------
    if (ShowPD && S->RangeHighIdx >= 0 && S->RangeLowIdx >= 0 &&
        S->RangeHigh > S->RangeLow)
    {
        const float Hi = S->RangeHigh;
        const float Lo = S->RangeLow;
        const float Eq = (Hi + Lo) * 0.5f;
        const float R  = Hi - Lo;
        const int   LeftIdx = SMC_MaxI(0, SMC_MinI(S->RangeHighIdx, S->RangeLowIdx));
        const int   PDTransp = SMC_MinI(95, Transp + 10);

        SMC_DrawBox(sc, LN_PREM_BOX, LeftIdx, RightEdge, Hi, Eq,
                    sc.Subgraph[SG_PREMIUM].PrimaryColor, PDTransp, LINESTYLE_SOLID);
        SMC_DrawBox(sc, LN_DISC_BOX, LeftIdx, RightEdge, Eq, Lo,
                    sc.Subgraph[SG_DISCOUNT].PrimaryColor, PDTransp, LINESTYLE_SOLID);
        SMC_DrawLine(sc, LN_EQ_LINE, LeftIdx, RightEdge, Eq, Eq,
                     sc.Subgraph[SG_EQUILIBRIUM].PrimaryColor, 1, LINESTYLE_DASH);

        if (ShowLabels)
        {
            SMC_DrawText(sc, LN_PREM_TXT, RightEdge, Hi, "Premium",
                         sc.Subgraph[SG_PREMIUM].PrimaryColor, FontSize, DT_RIGHT | DT_TOP, 0);
            SMC_DrawText(sc, LN_DISC_TXT, RightEdge, Lo, "Discount",
                         sc.Subgraph[SG_DISCOUNT].PrimaryColor, FontSize, DT_RIGHT | DT_BOTTOM, 0);
            SMC_DrawText(sc, LN_EQ_TXT, RightEdge, Eq, "Equilibrium (0.5)",
                         sc.Subgraph[SG_EQUILIBRIUM].PrimaryColor, FontSize, DT_RIGHT | DT_BOTTOM, 0);
        }
        else
        {
            SMC_DeleteDrawing(sc, LN_PREM_TXT);
            SMC_DeleteDrawing(sc, LN_DISC_TXT);
            SMC_DeleteDrawing(sc, LN_EQ_TXT);
        }

        if (ShowOTE)
        {
            float OteTop, OteBot;
            if (S->RangeLowIdx < S->RangeHighIdx)   // bullish leg -> retrace down
            {
                OteTop = Hi - R * 0.62f;
                OteBot = Hi - R * 0.79f;
            }
            else                                     // bearish leg -> retrace up
            {
                OteBot = Lo + R * 0.62f;
                OteTop = Lo + R * 0.79f;
            }

            SMC_DrawBox(sc, LN_OTE_BOX, LeftIdx, RightEdge, OteTop, OteBot,
                        sc.Subgraph[SG_OTE].PrimaryColor, SMC_MinI(95, Transp), LINESTYLE_SOLID);

            if (ShowLabels)
                SMC_DrawText(sc, LN_OTE_TXT, RightEdge, (OteTop + OteBot) * 0.5f, "OTE",
                             sc.Subgraph[SG_OTE].PrimaryColor, FontSize, DT_RIGHT | DT_VCENTER, 0);
            else
                SMC_DeleteDrawing(sc, LN_OTE_TXT);
        }
        else
        {
            SMC_DeleteDrawing(sc, LN_OTE_BOX);
            SMC_DeleteDrawing(sc, LN_OTE_TXT);
        }
    }
    else
    {
        SMC_DeleteDrawing(sc, LN_PREM_BOX);
        SMC_DeleteDrawing(sc, LN_DISC_BOX);
        SMC_DeleteDrawing(sc, LN_EQ_LINE);
        SMC_DeleteDrawing(sc, LN_OTE_BOX);
        SMC_DeleteDrawing(sc, LN_PREM_TXT);
        SMC_DeleteDrawing(sc, LN_DISC_TXT);
        SMC_DeleteDrawing(sc, LN_EQ_TXT);
        SMC_DeleteDrawing(sc, LN_OTE_TXT);
    }

    //--------------- previous day / week / month levels ---------------
    {
        struct s_HTF
        {
            bool  Show;
            float Price;
            int   StartIdx;
            int   Line;
            int   Text;
            int   ColorSG;
            const char* Label;
        };

        const s_HTF Levels[6] =
        {
            { ShowPDay,   S->PrevDayH, S->DayStartIdx, LN_PDH, LN_PDH_T, SG_PDHL, "PDH" },
            { ShowPDay,   S->PrevDayL, S->DayStartIdx, LN_PDL, LN_PDL_T, SG_PDHL, "PDL" },
            { ShowPWeek,  S->PrevWkH,  S->WkStartIdx,  LN_PWH, LN_PWH_T, SG_PWHL, "PWH" },
            { ShowPWeek,  S->PrevWkL,  S->WkStartIdx,  LN_PWL, LN_PWL_T, SG_PWHL, "PWL" },
            { ShowPMonth, S->PrevMoH,  S->MoStartIdx,  LN_PMH, LN_PMH_T, SG_PMHL, "PMH" },
            { ShowPMonth, S->PrevMoL,  S->MoStartIdx,  LN_PML, LN_PML_T, SG_PMHL, "PML" }
        };

        for (int k = 0; k < 6; ++k)
        {
            const s_HTF& Lv = Levels[k];

            if (!Lv.Show || Lv.Price <= 0.0f)
            {
                SMC_DeleteDrawing(sc, Lv.Line);
                SMC_DeleteDrawing(sc, Lv.Text);
                continue;
            }

            const unsigned int Color = sc.Subgraph[Lv.ColorSG].PrimaryColor;

            SMC_DrawLine(sc, Lv.Line, SMC_MaxI(0, Lv.StartIdx), RightEdge,
                         Lv.Price, Lv.Price, Color, 1, LINESTYLE_DASH);

            if (ShowLabels)
                SMC_DrawText(sc, Lv.Text, RightEdge, Lv.Price, Lv.Label, Color,
                             SMC_MaxI(4, FontSize - 1), DT_RIGHT | DT_VCENTER, 0);
            else
                SMC_DeleteDrawing(sc, Lv.Text);
        }
    }
}
