//=============================================================================
//  AlBrooksPriceActionSuite.cpp
//
//  A single Sierra Chart ACSIL study that implements the price action
//  methodology taught by Al Brooks (Trading Price Action: Trends / Trading
//  Ranges / Reversals, and the Brooks Trading Course).
//
//  Tuned by default for the E-mini S&P 500 (ES) on a 5 minute chart, which is
//  the chart Brooks uses for his own day trading and for nearly all of his
//  teaching examples. It will run on any instrument or bar period; the
//  defaults are simply sized for ES 5 minute (0.25 tick, ~16 tick max risk).
//
//  DESIGN NOTES
//  ------------
//  * Everything is causal. No pattern uses a bar that had not yet closed when
//    the pattern is reported. Swing points are confirmed with an explicit
//    N-bar delay rather than being read off the right edge of the chart, so
//    nothing repaints.
//  * All running state lives in per-bar arrays (sc.Subgraph[].Arrays[]) rather
//    than in persistent variables, so a full recalculation reproduces exactly
//    the same output as an incremental update.
//  * Brooks separates the SIGNAL BAR from the ENTRY BAR. This study does the
//    same. A setup is published on the close of the signal bar; the trade is
//    reported as triggered only once a later bar trades one tick beyond the
//    signal bar's extreme. Alerts default to firing on the trigger.
//
//  BUILD
//  -----
//  Drop this file in <SierraChart>/ACS_Source and use
//  Analysis >> Build Custom Studies DLL >> Remote Build.
//
//  This is an analysis and alerting tool. It does not place orders and it is
//  not trading advice.
//=============================================================================

#include "sierrachart.h"

SCDLLName("Al Brooks Price Action Suite")

//=============================================================================
//  Subgraph indices
//=============================================================================
namespace
{
    const int SG_EMA          = 0;   // 20 bar EMA (Brooks' only indicator)
    const int SG_HTF_EMA      = 1;   // higher timeframe EMA (60 min 20 EMA on a 5 min chart)
    const int SG_BUY          = 2;   // triggered long
    const int SG_SELL         = 3;   // triggered short
    const int SG_SETUP_BUY    = 4;   // long signal bar (not yet triggered)
    const int SG_SETUP_SELL   = 5;   // short signal bar (not yet triggered)
    const int SG_SWING_HIGH   = 6;
    const int SG_SWING_LOW    = 7;
    const int SG_ALWAYSIN     = 8;   // +1 always in long, -1 always in short
    const int SG_REGIME       = 9;   // 1 spike, 2 channel, 3 trading range, 4 tight range
    const int SG_HLCOUNT      = 10;  // +n = H count, -n = L count
    const int SG_SETUPCODE    = 11;  // signed setup code, see SETUP_* below
    const int SG_STRENGTH     = 12;  // 0..100 signal quality
    const int SG_ENTRY        = 13;
    const int SG_STOP         = 14;
    const int SG_T1           = 15;  // scalp target
    const int SG_T2           = 16;  // swing / measured move target
    const int SG_BARCOLOR     = 17;  // optional always-in bar colouring
    const int SG_ATR          = 18;  // ATR line (also holds TR in Arrays[0])

    // Pure working storage. Each subgraph carries 10 extra float arrays.
    const int SG_W1           = 19;  // pending setup published on the signal bar
    const int SG_W2           = 20;  // always-in state, H/L counting, micro channels
    const int SG_W3           = 21;  // confirmed swing highs and lows
    const int SG_W4           = 22;  // session state, trend line breaks, breakouts
    const int SG_W5           = 23;  // consolidation, MA gap bars, last signal

    //-- SG_W1: setup published on the SIGNAL bar ----------------------------
    const int W1_DIR      = 0;   // +1 long setup, -1 short setup, 0 none
    const int W1_CODE     = 1;
    const int W1_SCORE    = 2;
    const int W1_ENTRY    = 3;
    const int W1_STOP     = 4;
    const int W1_T1       = 5;
    const int W1_T2       = 6;
    const int W1_COUNT    = 7;   // H/L count carried by the setup
    const int W1_SECOND   = 8;   // 1 if this is a second entry
    const int W1_TRIGGER  = 9;   // reserved (triggering is derived from price, not stored)

    //-- SG_W2: trend state --------------------------------------------------
    const int W2_AI          = 0;
    const int W2_AIBARS      = 1;
    const int W2_HCOUNT      = 2;
    const int W2_LCOUNT      = 3;
    const int W2_HELIGIBLE   = 4;
    const int W2_LELIGIBLE   = 5;
    const int W2_LEGHIGH     = 6;   // running high of the current up leg
    const int W2_LEGLOW      = 7;   // running low of the current down leg
    const int W2_MCBULL      = 8;   // bull micro channel bar count
    const int W2_MCBEAR      = 9;

    //-- SG_W3 / SG_W4: confirmed swings ------------------------------------
    const int W3_SH0P = 0, W3_SH1P = 1, W3_SH2P = 2;
    const int W3_SH0I = 3, W3_SH1I = 4, W3_SH2I = 5;
    const int W3_SL0P = 6, W3_SL1P = 7, W3_SL2P = 8;
    const int W3_SL0I = 9;
    const int W4_SL1I = 0, W4_SL2I = 1;

    //-- SG_W4: session and structure ---------------------------------------
    const int W4_SESSOPEN   = 2;
    const int W4_SESSBARS   = 3;
    const int W4_SESSHIGH   = 4;
    const int W4_SESSLOW    = 5;
    const int W4_BULLTLBRK  = 6;   // bars since the bull trend line was broken (0 = not broken)
    const int W4_BEARTLBRK  = 7;
    const int W4_BULLBOLVL  = 8;   // level of the most recent upside breakout
    const int W4_BULLBOBARS = 9;

    //-- SG_W5 ---------------------------------------------------------------
    const int W5_BEARBOLVL  = 0;
    const int W5_BEARBOBARS = 1;
    const int W5_CONSOLIDX  = 2;
    const int W5_CONSOLHI   = 3;
    const int W5_CONSOLLO   = 4;
    const int W5_CONSOLBARS = 5;
    const int W5_MAGAPUP    = 6;   // consecutive bars whose low stayed above the EMA
    const int W5_MAGAPDN    = 7;
    const int W5_LASTSIGDIR = 8;
    const int W5_LASTSIGBAR = 9;   // bars since the last triggered signal

    //-- Input indices -------------------------------------------------------
    enum
    {
        IN_EMALEN = 0, IN_HTFEMALEN, IN_ATRLEN, IN_SWINGSTR,
        IN_TRENDBODY, IN_DOJIBODY, IN_STRONGCLOSE,
        IN_TRLOOKBACK, IN_TRATR, IN_TIGHTBARS, IN_MICROCHAN,
        IN_MAGAPBARS, IN_DTTOL, IN_WEDGEGAP,
        IN_MAXSTOPTICKS, IN_MINSTOPTICKS, IN_T1R, IN_T2R, IN_USEMM,
        IN_MINSTRENGTH, IN_DIRFILTER, IN_REQEMA, IN_SIGNALMODE,

        IN_P_H1L1, IN_P_H2L2, IN_P_H3H4, IN_P_BOPB, IN_P_FAILBO,
        IN_P_DBLTB, IN_P_WEDGE, IN_P_MTR, IN_P_FINALFLAG, IN_P_CLIMAX,
        IN_P_MAGAP, IN_P_TRFADE, IN_P_TWOBAR, IN_P_OPENREV, IN_P_INSIDEBO,

        IN_USESESSION, IN_SESSSTART, IN_SESSEND,
        IN_SKIPMIDDAY, IN_MIDSTART, IN_MIDEND, IN_LASTENTRY, IN_SKIPOPENBARS,

        IN_SHOWLABELS, IN_LABELBARS, IN_FONTSIZE, IN_SHOWHL, IN_SHOWSWINGS,
        IN_SHOWLEVELS, IN_COLORBARS, IN_ARROWOFF, IN_SHOWDASH,

        IN_ALERTS, IN_ALERTSOUND, IN_ALERTSETUP, IN_ALERTLOG, IN_SYMLABEL,
        IN_EXPECTSECONDS, IN_WARNCONFIG,
        IN_COUNT
    };

    //-- Setup codes ---------------------------------------------------------
    enum
    {
        SETUP_NONE = 0,
        SETUP_H1, SETUP_H2, SETUP_H3, SETUP_H4,
        SETUP_L1, SETUP_L2, SETUP_L3, SETUP_L4,
        SETUP_BOPB,          // breakout pullback
        SETUP_FAILEDBO,      // failed breakout
        SETUP_DOUBLEBOT, SETUP_DOUBLETOP,
        SETUP_WEDGEBOT, SETUP_WEDGETOP,
        SETUP_MTRBULL, SETUP_MTRBEAR,
        SETUP_FINALFLAG,
        SETUP_CLIMAX,
        SETUP_MAGAP,
        SETUP_TRFADE,
        SETUP_TWOBAR,
        SETUP_OPENREV,
        SETUP_INSIDEBO
    };

    //-- Regimes -------------------------------------------------------------
    const int REG_SPIKE = 1, REG_CHANNEL = 2, REG_RANGE = 3, REG_TIGHT = 4;

    //-- Chart drawing line number bases -------------------------------------
    const int LN_HL      = 6110000;
    const int LN_SIG     = 6210000;
    const int LN_DASH    = 6310000;
    const int LN_LEVEL   = 6310010;

    //=========================================================================
    //  Small helpers
    //=========================================================================
    inline float MaxF(float a, float b) { return a > b ? a : b; }
    inline float MinF(float a, float b) { return a < b ? a : b; }
    inline int   MaxI(int a, int b)     { return a > b ? a : b; }
    inline int   MinI(int a, int b)     { return a < b ? a : b; }
    inline float ClampF(float v, float lo, float hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    // Everything worth knowing about a single bar, in Brooks' vocabulary.
    struct BarInfo
    {
        float Open, High, Low, Close;
        float Range, Body, AbsBody;
        float UpperTail, LowerTail;
        float BodyRatio;    // body / range, 1.0 = marubozu
        float ClosePos;     // 0 = closed on the low, 1 = closed on the high
        bool  IsBull, IsBear, IsDoji;
        bool  IsBullTrendBar, IsBearTrendBar;
        bool  IsStrongBull, IsStrongBear;
        bool  IsInside, IsOutside;
        bool  IsBullRev, IsBearRev;
    };

    void ReadBar(SCStudyInterfaceRef sc, int Idx, float TrendBody, float DojiBody,
                 float StrongClose, BarInfo& B)
    {
        if (Idx < 0) Idx = 0;

        B.Open  = sc.Open[Idx];
        B.High  = sc.High[Idx];
        B.Low   = sc.Low[Idx];
        B.Close = sc.Close[Idx];

        B.Range = B.High - B.Low;
        if (B.Range <= 0.0f)
            B.Range = sc.TickSize > 0.0f ? sc.TickSize : 0.01f;

        B.Body      = B.Close - B.Open;
        B.AbsBody   = B.Body >= 0.0f ? B.Body : -B.Body;
        B.UpperTail = B.High - MaxF(B.Open, B.Close);
        B.LowerTail = MinF(B.Open, B.Close) - B.Low;
        B.BodyRatio = B.AbsBody / B.Range;
        B.ClosePos  = (B.Close - B.Low) / B.Range;

        B.IsBull = B.Close > B.Open;
        B.IsBear = B.Close < B.Open;
        B.IsDoji = B.BodyRatio <= DojiBody;

        B.IsBullTrendBar = B.IsBull && B.BodyRatio >= TrendBody;
        B.IsBearTrendBar = B.IsBear && B.BodyRatio >= TrendBody;
        B.IsStrongBull   = B.IsBullTrendBar && B.ClosePos >= StrongClose;
        B.IsStrongBear   = B.IsBearTrendBar && B.ClosePos <= (1.0f - StrongClose);

        if (Idx > 0)
        {
            B.IsInside  = (B.High <= sc.High[Idx - 1]) && (B.Low >= sc.Low[Idx - 1]);
            B.IsOutside = (B.High >  sc.High[Idx - 1]) && (B.Low <  sc.Low[Idx - 1]);
        }
        else
        {
            B.IsInside  = false;
            B.IsOutside = false;
        }

        // A good reversal bar has a prominent tail against the prior move and
        // closes back in the direction of the reversal.
        B.IsBullRev = (B.Close >= B.Open) && (B.LowerTail >= 0.30f * B.Range)
                      && (B.ClosePos >= 0.55f) && (B.LowerTail > B.UpperTail);
        B.IsBearRev = (B.Close <= B.Open) && (B.UpperTail >= 0.30f * B.Range)
                      && (B.ClosePos <= 0.45f) && (B.UpperTail > B.LowerTail);
    }

    float HighestHigh(SCStudyInterfaceRef sc, int FromIdx, int ToIdx)
    {
        if (FromIdx < 0) FromIdx = 0;
        float v = sc.High[FromIdx];
        for (int k = FromIdx + 1; k <= ToIdx; ++k)
            if (sc.High[k] > v) v = sc.High[k];
        return v;
    }

    float LowestLow(SCStudyInterfaceRef sc, int FromIdx, int ToIdx)
    {
        if (FromIdx < 0) FromIdx = 0;
        float v = sc.Low[FromIdx];
        for (int k = FromIdx + 1; k <= ToIdx; ++k)
            if (sc.Low[k] < v) v = sc.Low[k];
        return v;
    }

    // Value at BarIdx of the straight line through (i1,p1) and (i2,p2).
    float LineValueAt(float i1, float p1, float i2, float p2, float BarIdx)
    {
        const float dx = i2 - i1;
        if (dx == 0.0f) return p2;
        return p1 + (p2 - p1) * (BarIdx - i1) / dx;
    }

    void DrawText(SCStudyInterfaceRef sc, int BarIdx, float Value, const SCString& Text,
                  COLORREF Color, int FontSize, int LineNumber, bool Bold)
    {
        s_UseTool Tool;
        Tool.Clear();
        Tool.ChartNumber  = sc.ChartNumber;
        Tool.DrawingType  = DRAWING_TEXT;
        Tool.LineNumber   = LineNumber;
        Tool.BeginIndex   = BarIdx;
        Tool.BeginValue   = Value;
        Tool.Region       = sc.GraphRegion;
        Tool.Color        = Color;
        Tool.FontSize     = FontSize;
        Tool.FontBold     = Bold ? 1 : 0;
        Tool.Text         = Text;
        Tool.AddMethod    = UTAM_ADD_OR_ADJUST;
        sc.UseTool(Tool);
    }

    void DrawLevel(SCStudyInterfaceRef sc, int FromIdx, int ToIdx, float Value,
                   COLORREF Color, SubgraphLineStyles LineStyle, int LineNumber,
                   const SCString& Text)
    {
        s_UseTool Tool;
        Tool.Clear();
        Tool.ChartNumber = sc.ChartNumber;
        Tool.DrawingType = DRAWING_LINE;
        Tool.LineNumber  = LineNumber;
        Tool.BeginIndex  = FromIdx;
        Tool.EndIndex    = ToIdx;
        Tool.BeginValue  = Value;
        Tool.EndValue    = Value;
        Tool.Region      = sc.GraphRegion;
        Tool.Color       = Color;
        Tool.LineStyle   = LineStyle;
        Tool.LineWidth   = 1;
        Tool.Text        = Text;
        Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
        sc.UseTool(Tool);
    }

    SCString SetupName(int Code)
    {
        SCString s;
        switch (Code)
        {
        case SETUP_H1:        s = "H1";                 break;
        case SETUP_H2:        s = "H2";                 break;
        case SETUP_H3:        s = "H3";                 break;
        case SETUP_H4:        s = "H4";                 break;
        case SETUP_L1:        s = "L1";                 break;
        case SETUP_L2:        s = "L2";                 break;
        case SETUP_L3:        s = "L3";                 break;
        case SETUP_L4:        s = "L4";                 break;
        case SETUP_BOPB:      s = "BO Pullback";        break;
        case SETUP_FAILEDBO:  s = "Failed Breakout";    break;
        case SETUP_DOUBLEBOT: s = "Double Bottom";      break;
        case SETUP_DOUBLETOP: s = "Double Top";         break;
        case SETUP_WEDGEBOT:  s = "Wedge Bottom";       break;
        case SETUP_WEDGETOP:  s = "Wedge Top";          break;
        case SETUP_MTRBULL:   s = "Bull MTR";           break;
        case SETUP_MTRBEAR:   s = "Bear MTR";           break;
        case SETUP_FINALFLAG: s = "Final Flag";         break;
        case SETUP_CLIMAX:    s = "Climax Reversal";    break;
        case SETUP_MAGAP:     s = "MA Gap Bar";         break;
        case SETUP_TRFADE:    s = "Range Fade";         break;
        case SETUP_TWOBAR:    s = "Two Bar Reversal";   break;
        case SETUP_OPENREV:   s = "Opening Reversal";   break;
        case SETUP_INSIDEBO:  s = "Inside Bar BO";      break;
        default:              s = "Setup";              break;
        }
        return s;
    }

    SCString RegimeName(int Regime)
    {
        SCString s;
        switch (Regime)
        {
        case REG_SPIKE:   s = "Spike/Breakout"; break;
        case REG_CHANNEL: s = "Channel";        break;
        case REG_RANGE:   s = "Trading Range";  break;
        case REG_TIGHT:   s = "Tight Range";    break;
        default:          s = "-";              break;
        }
        return s;
    }

    // One candidate setup found on the current signal bar.
    struct Candidate
    {
        int   Dir;      // +1 long, -1 short
        int   Code;
        float Score;
        int   Count;    // H/L count if applicable
        int   Second;   // 1 if a second entry
    };

} // anonymous namespace


//=============================================================================
//  Study
//=============================================================================
SCSFExport scsf_AlBrooksPriceActionSuite(SCStudyInterfaceRef sc)
{
    SCSubgraphRef Sg_EMA        = sc.Subgraph[SG_EMA];
    SCSubgraphRef Sg_HtfEma     = sc.Subgraph[SG_HTF_EMA];
    SCSubgraphRef Sg_Buy        = sc.Subgraph[SG_BUY];
    SCSubgraphRef Sg_Sell       = sc.Subgraph[SG_SELL];
    SCSubgraphRef Sg_SetupBuy   = sc.Subgraph[SG_SETUP_BUY];
    SCSubgraphRef Sg_SetupSell  = sc.Subgraph[SG_SETUP_SELL];
    SCSubgraphRef Sg_SwingHigh  = sc.Subgraph[SG_SWING_HIGH];
    SCSubgraphRef Sg_SwingLow   = sc.Subgraph[SG_SWING_LOW];
    SCSubgraphRef Sg_AlwaysIn   = sc.Subgraph[SG_ALWAYSIN];
    SCSubgraphRef Sg_Regime     = sc.Subgraph[SG_REGIME];
    SCSubgraphRef Sg_HLCount    = sc.Subgraph[SG_HLCOUNT];
    SCSubgraphRef Sg_SetupCode  = sc.Subgraph[SG_SETUPCODE];
    SCSubgraphRef Sg_Strength   = sc.Subgraph[SG_STRENGTH];
    SCSubgraphRef Sg_Entry      = sc.Subgraph[SG_ENTRY];
    SCSubgraphRef Sg_Stop       = sc.Subgraph[SG_STOP];
    SCSubgraphRef Sg_T1         = sc.Subgraph[SG_T1];
    SCSubgraphRef Sg_T2         = sc.Subgraph[SG_T2];
    SCSubgraphRef Sg_BarColor   = sc.Subgraph[SG_BARCOLOR];
    SCSubgraphRef Sg_Atr        = sc.Subgraph[SG_ATR];
    SCSubgraphRef W1            = sc.Subgraph[SG_W1];
    SCSubgraphRef W2            = sc.Subgraph[SG_W2];
    SCSubgraphRef W3            = sc.Subgraph[SG_W3];
    SCSubgraphRef W4            = sc.Subgraph[SG_W4];
    SCSubgraphRef W5            = sc.Subgraph[SG_W5];

    //=========================================================================
    //  Defaults
    //=========================================================================
    if (sc.SetDefaults)
    {
        sc.GraphName = "Al Brooks Price Action Suite";
        sc.StudyDescription =
            "Al Brooks price action method: always-in state, market cycle, "
            "H/L pullback counting, with-trend continuation setups and the "
            "major reversal patterns, with signal-bar / entry-bar separation, "
            "quality scoring and alerts. Defaults tuned for ES 5 minute.";

        sc.GraphRegion         = 0;
        sc.AutoLoop            = 1;
        sc.ValueFormat         = VALUEFORMAT_INHERITED;
        sc.DrawZeros           = 0;
        sc.AlertOnlyOncePerBar = 1;
        sc.FreeDLL             = 0;

        //-- subgraphs -------------------------------------------------------
        Sg_EMA.Name         = "EMA (20)";
        Sg_EMA.DrawStyle    = DRAWSTYLE_LINE;
        Sg_EMA.PrimaryColor = RGB(255, 200, 0);
        Sg_EMA.LineWidth    = 2;
        Sg_EMA.DrawZeros    = 0;

        Sg_HtfEma.Name         = "Higher TF EMA";
        Sg_HtfEma.DrawStyle    = DRAWSTYLE_LINE;
        Sg_HtfEma.PrimaryColor = RGB(150, 120, 200);
        Sg_HtfEma.LineWidth    = 1;
        Sg_HtfEma.DrawZeros    = 0;

        Sg_Buy.Name         = "Buy (triggered)";
        Sg_Buy.DrawStyle    = DRAWSTYLE_ARROW_UP;
        Sg_Buy.PrimaryColor = RGB(0, 220, 80);
        Sg_Buy.LineWidth    = 3;
        Sg_Buy.DrawZeros    = 0;

        Sg_Sell.Name         = "Sell (triggered)";
        Sg_Sell.DrawStyle    = DRAWSTYLE_ARROW_DOWN;
        Sg_Sell.PrimaryColor = RGB(255, 60, 60);
        Sg_Sell.LineWidth    = 3;
        Sg_Sell.DrawZeros    = 0;

        Sg_SetupBuy.Name         = "Buy signal bar";
        Sg_SetupBuy.DrawStyle    = DRAWSTYLE_POINT;
        Sg_SetupBuy.PrimaryColor = RGB(0, 150, 60);
        Sg_SetupBuy.LineWidth    = 3;
        Sg_SetupBuy.DrawZeros    = 0;

        Sg_SetupSell.Name         = "Sell signal bar";
        Sg_SetupSell.DrawStyle    = DRAWSTYLE_POINT;
        Sg_SetupSell.PrimaryColor = RGB(180, 40, 40);
        Sg_SetupSell.LineWidth    = 3;
        Sg_SetupSell.DrawZeros    = 0;

        Sg_SwingHigh.Name         = "Swing high";
        Sg_SwingHigh.DrawStyle    = DRAWSTYLE_POINT;
        Sg_SwingHigh.PrimaryColor = RGB(200, 200, 200);
        Sg_SwingHigh.LineWidth    = 2;
        Sg_SwingHigh.DrawZeros    = 0;

        Sg_SwingLow.Name         = "Swing low";
        Sg_SwingLow.DrawStyle    = DRAWSTYLE_POINT;
        Sg_SwingLow.PrimaryColor = RGB(200, 200, 200);
        Sg_SwingLow.LineWidth    = 2;
        Sg_SwingLow.DrawZeros    = 0;

        Sg_AlwaysIn.Name      = "Always In (+1/-1)";
        Sg_AlwaysIn.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_AlwaysIn.DrawZeros = 1;

        Sg_Regime.Name      = "Regime code";
        Sg_Regime.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_Regime.DrawZeros = 1;

        Sg_HLCount.Name      = "H/L count (+H / -L)";
        Sg_HLCount.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_HLCount.DrawZeros = 1;

        Sg_SetupCode.Name      = "Setup code";
        Sg_SetupCode.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_SetupCode.DrawZeros = 1;

        Sg_Strength.Name      = "Signal strength";
        Sg_Strength.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_Strength.DrawZeros = 1;

        Sg_Entry.Name      = "Entry price";
        Sg_Entry.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_Entry.DrawZeros = 0;

        Sg_Stop.Name      = "Stop price";
        Sg_Stop.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_Stop.DrawZeros = 0;

        Sg_T1.Name      = "Target 1";
        Sg_T1.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_T1.DrawZeros = 0;

        Sg_T2.Name      = "Target 2";
        Sg_T2.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_T2.DrawZeros = 0;

        Sg_BarColor.Name      = "Always In bar colour";
        Sg_BarColor.DrawStyle = DRAWSTYLE_COLOR_BAR;
        Sg_BarColor.DrawZeros = 0;

        Sg_Atr.Name         = "ATR";
        Sg_Atr.DrawStyle    = DRAWSTYLE_IGNORE;
        Sg_Atr.PrimaryColor = RGB(120, 120, 120);
        Sg_Atr.DrawZeros    = 0;

        W1.Name = "int:setup";   W1.DrawStyle = DRAWSTYLE_IGNORE; W1.DrawZeros = 1;
        W2.Name = "int:trend";   W2.DrawStyle = DRAWSTYLE_IGNORE; W2.DrawZeros = 1;
        W3.Name = "int:swings";  W3.DrawStyle = DRAWSTYLE_IGNORE; W3.DrawZeros = 1;
        W4.Name = "int:session"; W4.DrawStyle = DRAWSTYLE_IGNORE; W4.DrawZeros = 1;
        W5.Name = "int:struct";  W5.DrawStyle = DRAWSTYLE_IGNORE; W5.DrawZeros = 1;

        //-- inputs: structure ----------------------------------------------
        sc.Input[IN_EMALEN].Name = "EMA Length (Brooks uses 20)";
        sc.Input[IN_EMALEN].SetInt(20);
        sc.Input[IN_EMALEN].SetIntLimits(2, 500);

        sc.Input[IN_HTFEMALEN].Name = "Higher TF EMA Length in chart bars (240 = 60min 20EMA on 5min)";
        sc.Input[IN_HTFEMALEN].SetInt(240);
        sc.Input[IN_HTFEMALEN].SetIntLimits(2, 5000);

        sc.Input[IN_ATRLEN].Name = "ATR Length";
        sc.Input[IN_ATRLEN].SetInt(20);
        sc.Input[IN_ATRLEN].SetIntLimits(2, 200);

        sc.Input[IN_SWINGSTR].Name = "Swing Strength (bars each side, adds this many bars of confirmation delay)";
        sc.Input[IN_SWINGSTR].SetInt(2);
        sc.Input[IN_SWINGSTR].SetIntLimits(1, 10);

        sc.Input[IN_TRENDBODY].Name = "Trend Bar Body/Range Ratio";
        sc.Input[IN_TRENDBODY].SetFloat(0.50f);
        sc.Input[IN_TRENDBODY].SetFloatLimits(0.10f, 0.95f);

        sc.Input[IN_DOJIBODY].Name = "Doji Body/Range Ratio";
        sc.Input[IN_DOJIBODY].SetFloat(0.25f);
        sc.Input[IN_DOJIBODY].SetFloatLimits(0.02f, 0.60f);

        sc.Input[IN_STRONGCLOSE].Name = "Strong Bar Close Position (0.65 = closes in top/bottom 35%)";
        sc.Input[IN_STRONGCLOSE].SetFloat(0.65f);
        sc.Input[IN_STRONGCLOSE].SetFloatLimits(0.50f, 0.99f);

        sc.Input[IN_TRLOOKBACK].Name = "Trading Range Lookback Bars";
        sc.Input[IN_TRLOOKBACK].SetInt(20);
        sc.Input[IN_TRLOOKBACK].SetIntLimits(5, 200);

        sc.Input[IN_TRATR].Name = "Trading Range Max Height (ATR multiple)";
        sc.Input[IN_TRATR].SetFloat(2.50f);
        sc.Input[IN_TRATR].SetFloatLimits(0.5f, 20.0f);

        sc.Input[IN_TIGHTBARS].Name = "Tight Range / Barb Wire Bars";
        sc.Input[IN_TIGHTBARS].SetInt(3);
        sc.Input[IN_TIGHTBARS].SetIntLimits(2, 20);

        sc.Input[IN_MICROCHAN].Name = "Micro Channel Minimum Bars";
        sc.Input[IN_MICROCHAN].SetInt(4);
        sc.Input[IN_MICROCHAN].SetIntLimits(3, 20);

        sc.Input[IN_MAGAPBARS].Name = "Moving Average Gap Bars Threshold";
        sc.Input[IN_MAGAPBARS].SetInt(20);
        sc.Input[IN_MAGAPBARS].SetIntLimits(5, 200);

        sc.Input[IN_DTTOL].Name = "Double Top/Bottom Tolerance (ATR multiple)";
        sc.Input[IN_DTTOL].SetFloat(0.35f);
        sc.Input[IN_DTTOL].SetFloatLimits(0.02f, 3.0f);

        sc.Input[IN_WEDGEGAP].Name = "Wedge: Minimum Bars Between Pushes";
        sc.Input[IN_WEDGEGAP].SetInt(3);
        sc.Input[IN_WEDGEGAP].SetIntLimits(1, 50);

        //-- inputs: risk ----------------------------------------------------
        sc.Input[IN_MAXSTOPTICKS].Name = "Maximum Stop Size (ticks, 0 = no cap)";
        sc.Input[IN_MAXSTOPTICKS].SetInt(16);
        sc.Input[IN_MAXSTOPTICKS].SetIntLimits(0, 10000);

        sc.Input[IN_MINSTOPTICKS].Name = "Minimum Stop Size (ticks)";
        sc.Input[IN_MINSTOPTICKS].SetInt(3);
        sc.Input[IN_MINSTOPTICKS].SetIntLimits(1, 1000);

        sc.Input[IN_T1R].Name = "Target 1 (R multiple, Brooks scalp)";
        sc.Input[IN_T1R].SetFloat(1.0f);
        sc.Input[IN_T1R].SetFloatLimits(0.1f, 20.0f);

        sc.Input[IN_T2R].Name = "Target 2 (R multiple, swing)";
        sc.Input[IN_T2R].SetFloat(2.0f);
        sc.Input[IN_T2R].SetFloatLimits(0.1f, 50.0f);

        sc.Input[IN_USEMM].Name = "Use Measured Move for Target 2 when available";
        sc.Input[IN_USEMM].SetYesNo(1);

        sc.Input[IN_MINSTRENGTH].Name = "Minimum Signal Strength (0-100)";
        sc.Input[IN_MINSTRENGTH].SetInt(55);
        sc.Input[IN_MINSTRENGTH].SetIntLimits(0, 100);

        sc.Input[IN_DIRFILTER].Name = "Direction Filter";
        sc.Input[IN_DIRFILTER].SetCustomInputStrings("Both;Long Only;Short Only;With Always-In Only");
        sc.Input[IN_DIRFILTER].SetCustomInputIndex(0);

        sc.Input[IN_REQEMA].Name = "Require Close Beyond EMA for With-Trend Entries";
        sc.Input[IN_REQEMA].SetYesNo(0);

        sc.Input[IN_SIGNALMODE].Name = "Signal Mode";
        sc.Input[IN_SIGNALMODE].SetCustomInputStrings("Confirmed on entry trigger;Setup on signal bar close");
        sc.Input[IN_SIGNALMODE].SetCustomInputIndex(0);

        //-- inputs: pattern toggles ----------------------------------------
        sc.Input[IN_P_H1L1].Name = "Enable H1 / L1 (first entry, lower probability)";
        sc.Input[IN_P_H1L1].SetYesNo(0);

        sc.Input[IN_P_H2L2].Name = "Enable H2 / L2 (second entry)";
        sc.Input[IN_P_H2L2].SetYesNo(1);

        sc.Input[IN_P_H3H4].Name = "Enable H3-H4 / L3-L4 (wedge pullbacks)";
        sc.Input[IN_P_H3H4].SetYesNo(1);

        sc.Input[IN_P_BOPB].Name = "Enable Breakout Pullback";
        sc.Input[IN_P_BOPB].SetYesNo(1);

        sc.Input[IN_P_FAILBO].Name = "Enable Failed Breakout";
        sc.Input[IN_P_FAILBO].SetYesNo(1);

        sc.Input[IN_P_DBLTB].Name = "Enable Double Top / Double Bottom";
        sc.Input[IN_P_DBLTB].SetYesNo(1);

        sc.Input[IN_P_WEDGE].Name = "Enable Wedge / Three Pushes";
        sc.Input[IN_P_WEDGE].SetYesNo(1);

        sc.Input[IN_P_MTR].Name = "Enable Major Trend Reversal";
        sc.Input[IN_P_MTR].SetYesNo(1);

        sc.Input[IN_P_FINALFLAG].Name = "Enable Final Flag";
        sc.Input[IN_P_FINALFLAG].SetYesNo(1);

        sc.Input[IN_P_CLIMAX].Name = "Enable Climax / Trend Channel Overshoot";
        sc.Input[IN_P_CLIMAX].SetYesNo(1);

        sc.Input[IN_P_MAGAP].Name = "Enable Moving Average Gap Bar";
        sc.Input[IN_P_MAGAP].SetYesNo(1);

        sc.Input[IN_P_TRFADE].Name = "Enable Trading Range Fade (buy low / sell high)";
        sc.Input[IN_P_TRFADE].SetYesNo(1);

        sc.Input[IN_P_TWOBAR].Name = "Enable Two Bar Reversal / Micro Double Top-Bottom";
        sc.Input[IN_P_TWOBAR].SetYesNo(1);

        sc.Input[IN_P_OPENREV].Name = "Enable Opening Reversal";
        sc.Input[IN_P_OPENREV].SetYesNo(1);

        sc.Input[IN_P_INSIDEBO].Name = "Enable ii / ioi / iii Breakout";
        sc.Input[IN_P_INSIDEBO].SetYesNo(1);

        //-- inputs: session -------------------------------------------------
        sc.Input[IN_USESESSION].Name = "Use Session Time Filter";
        sc.Input[IN_USESESSION].SetYesNo(1);

        sc.Input[IN_SESSSTART].Name = "Session Start Time";
        sc.Input[IN_SESSSTART].SetTime(HMS_TIME(9, 30, 0));

        sc.Input[IN_SESSEND].Name = "Session End Time";
        sc.Input[IN_SESSEND].SetTime(HMS_TIME(16, 0, 0));

        sc.Input[IN_SKIPMIDDAY].Name = "Skip Midday Doldrums";
        sc.Input[IN_SKIPMIDDAY].SetYesNo(1);

        sc.Input[IN_MIDSTART].Name = "Midday Start Time";
        sc.Input[IN_MIDSTART].SetTime(HMS_TIME(11, 30, 0));

        sc.Input[IN_MIDEND].Name = "Midday End Time";
        sc.Input[IN_MIDEND].SetTime(HMS_TIME(13, 30, 0));

        sc.Input[IN_LASTENTRY].Name = "No New Signals After";
        sc.Input[IN_LASTENTRY].SetTime(HMS_TIME(15, 45, 0));

        sc.Input[IN_SKIPOPENBARS].Name = "Bars to Skip at Session Open";
        sc.Input[IN_SKIPOPENBARS].SetInt(0);
        sc.Input[IN_SKIPOPENBARS].SetIntLimits(0, 100);

        //-- inputs: display -------------------------------------------------
        sc.Input[IN_SHOWLABELS].Name = "Show Setup Labels";
        sc.Input[IN_SHOWLABELS].SetYesNo(1);

        sc.Input[IN_LABELBARS].Name = "Maximum Bars to Label";
        sc.Input[IN_LABELBARS].SetInt(400);
        sc.Input[IN_LABELBARS].SetIntLimits(10, 5000);

        sc.Input[IN_FONTSIZE].Name = "Label Font Size";
        sc.Input[IN_FONTSIZE].SetInt(9);
        sc.Input[IN_FONTSIZE].SetIntLimits(5, 40);

        sc.Input[IN_SHOWHL].Name = "Show H / L Counts";
        sc.Input[IN_SHOWHL].SetYesNo(1);

        sc.Input[IN_SHOWSWINGS].Name = "Show Swing Points";
        sc.Input[IN_SHOWSWINGS].SetYesNo(1);

        sc.Input[IN_SHOWLEVELS].Name = "Show Entry / Stop / Target Lines for Latest Signal";
        sc.Input[IN_SHOWLEVELS].SetYesNo(1);

        sc.Input[IN_COLORBARS].Name = "Colour Bars by Always-In State";
        sc.Input[IN_COLORBARS].SetYesNo(0);

        sc.Input[IN_ARROWOFF].Name = "Arrow Offset (ATR multiple)";
        sc.Input[IN_ARROWOFF].SetFloat(0.40f);
        sc.Input[IN_ARROWOFF].SetFloatLimits(0.0f, 5.0f);

        sc.Input[IN_SHOWDASH].Name = "Show Context Dashboard";
        sc.Input[IN_SHOWDASH].SetYesNo(1);

        //-- inputs: alerts --------------------------------------------------
        sc.Input[IN_ALERTS].Name = "Enable Alerts";
        sc.Input[IN_ALERTS].SetYesNo(1);

        sc.Input[IN_ALERTSOUND].Name = "Alert Sound Number (0 = none)";
        sc.Input[IN_ALERTSOUND].SetInt(2);
        sc.Input[IN_ALERTSOUND].SetIntLimits(0, 100);

        sc.Input[IN_ALERTSETUP].Name = "Also Alert on Signal Bars (before trigger)";
        sc.Input[IN_ALERTSETUP].SetYesNo(0);

        sc.Input[IN_ALERTLOG].Name = "Write Alerts to Message Log";
        sc.Input[IN_ALERTLOG].SetYesNo(1);

        sc.Input[IN_SYMLABEL].Name = "Symbol Label for Alerts (blank = chart symbol)";
        sc.Input[IN_SYMLABEL].SetString("");

        sc.Input[IN_EXPECTSECONDS].Name = "Expected Bar Period (seconds, 300 = 5 minute)";
        sc.Input[IN_EXPECTSECONDS].SetInt(300);
        sc.Input[IN_EXPECTSECONDS].SetIntLimits(0, 86400);

        sc.Input[IN_WARNCONFIG].Name = "Warn in Message Log if Chart Is Not the Expected Period";
        sc.Input[IN_WARNCONFIG].SetYesNo(1);

        return;
    }

    //=========================================================================
    //  Cached inputs
    //=========================================================================
    const int   EmaLen        = sc.Input[IN_EMALEN].GetInt();
    const int   HtfEmaLen     = sc.Input[IN_HTFEMALEN].GetInt();
    const int   AtrLen        = sc.Input[IN_ATRLEN].GetInt();
    const int   SwingStr      = sc.Input[IN_SWINGSTR].GetInt();
    const float TrendBody     = sc.Input[IN_TRENDBODY].GetFloat();
    const float DojiBody      = sc.Input[IN_DOJIBODY].GetFloat();
    const float StrongClose   = sc.Input[IN_STRONGCLOSE].GetFloat();
    const int   TrLookback    = sc.Input[IN_TRLOOKBACK].GetInt();
    const float TrAtrMult     = sc.Input[IN_TRATR].GetFloat();
    const int   TightBars     = sc.Input[IN_TIGHTBARS].GetInt();
    const int   MicroChanMin  = sc.Input[IN_MICROCHAN].GetInt();
    const int   MaGapBars     = sc.Input[IN_MAGAPBARS].GetInt();
    const float DtTol         = sc.Input[IN_DTTOL].GetFloat();
    const int   WedgeGap      = sc.Input[IN_WEDGEGAP].GetInt();
    const int   MaxStopTicks  = sc.Input[IN_MAXSTOPTICKS].GetInt();
    const int   MinStopTicks  = sc.Input[IN_MINSTOPTICKS].GetInt();
    const float T1R           = sc.Input[IN_T1R].GetFloat();
    const float T2R           = sc.Input[IN_T2R].GetFloat();
    const int   UseMm         = sc.Input[IN_USEMM].GetYesNo();
    const int   MinStrength   = sc.Input[IN_MINSTRENGTH].GetInt();
    const int   DirFilter     = sc.Input[IN_DIRFILTER].GetIndex();
    const int   ReqEma        = sc.Input[IN_REQEMA].GetYesNo();
    const int   SignalMode    = sc.Input[IN_SIGNALMODE].GetIndex();

    const int   EnH1L1     = sc.Input[IN_P_H1L1].GetYesNo();
    const int   EnH2L2     = sc.Input[IN_P_H2L2].GetYesNo();
    const int   EnH3H4     = sc.Input[IN_P_H3H4].GetYesNo();
    const int   EnBopb     = sc.Input[IN_P_BOPB].GetYesNo();
    const int   EnFailBo   = sc.Input[IN_P_FAILBO].GetYesNo();
    const int   EnDblTb    = sc.Input[IN_P_DBLTB].GetYesNo();
    const int   EnWedge    = sc.Input[IN_P_WEDGE].GetYesNo();
    const int   EnMtr      = sc.Input[IN_P_MTR].GetYesNo();
    const int   EnFinal    = sc.Input[IN_P_FINALFLAG].GetYesNo();
    const int   EnClimax   = sc.Input[IN_P_CLIMAX].GetYesNo();
    const int   EnMaGap    = sc.Input[IN_P_MAGAP].GetYesNo();
    const int   EnTrFade   = sc.Input[IN_P_TRFADE].GetYesNo();
    const int   EnTwoBar   = sc.Input[IN_P_TWOBAR].GetYesNo();
    const int   EnOpenRev  = sc.Input[IN_P_OPENREV].GetYesNo();
    const int   EnInsideBo = sc.Input[IN_P_INSIDEBO].GetYesNo();

    const int   UseSession   = sc.Input[IN_USESESSION].GetYesNo();
    const int   SessStart    = sc.Input[IN_SESSSTART].GetTime();
    const int   SessEnd      = sc.Input[IN_SESSEND].GetTime();
    const int   SkipMidday   = sc.Input[IN_SKIPMIDDAY].GetYesNo();
    const int   MidStart     = sc.Input[IN_MIDSTART].GetTime();
    const int   MidEnd       = sc.Input[IN_MIDEND].GetTime();
    const int   LastEntry    = sc.Input[IN_LASTENTRY].GetTime();
    const int   SkipOpenBars = sc.Input[IN_SKIPOPENBARS].GetInt();

    const int   ShowLabels = sc.Input[IN_SHOWLABELS].GetYesNo();
    const int   LabelBars  = sc.Input[IN_LABELBARS].GetInt();
    const int   FontSize   = sc.Input[IN_FONTSIZE].GetInt();
    const int   ShowHl     = sc.Input[IN_SHOWHL].GetYesNo();
    const int   ShowSwings = sc.Input[IN_SHOWSWINGS].GetYesNo();
    const int   ShowLevels = sc.Input[IN_SHOWLEVELS].GetYesNo();
    const int   ColorBars  = sc.Input[IN_COLORBARS].GetYesNo();
    const float ArrowOff   = sc.Input[IN_ARROWOFF].GetFloat();
    const int   ShowDash   = sc.Input[IN_SHOWDASH].GetYesNo();

    const int   AlertsOn    = sc.Input[IN_ALERTS].GetYesNo();
    const int   AlertSound  = sc.Input[IN_ALERTSOUND].GetInt();
    const int   AlertSetup  = sc.Input[IN_ALERTSETUP].GetYesNo();
    const int   AlertLog    = sc.Input[IN_ALERTLOG].GetYesNo();

    const float Tick    = sc.TickSize > 0.0f ? sc.TickSize : 0.01f;
    const int   i       = sc.Index;
    const int   LastIdx = sc.ArraySize - 1;

    sc.DataStartIndex = MaxI(EmaLen, AtrLen) + SwingStr + 2;

    //=========================================================================
    //  Configuration warning (once, on the last bar of a full recalculation)
    //=========================================================================
    if (i == LastIdx && sc.Input[IN_WARNCONFIG].GetYesNo() != 0)
    {
        const int Expected = sc.Input[IN_EXPECTSECONDS].GetInt();
        if (Expected > 0 && sc.SecondsPerBar != Expected)
        {
            SCString Warn;
            Warn.Format("Al Brooks Price Action Suite: chart bar period is %d seconds, "
                        "expected %d. Brooks' method as configured here assumes a %d second "
                        "(%.0f minute) chart. Adjust the study inputs or the chart period.",
                        sc.SecondsPerBar, Expected, Expected, Expected / 60.0f);
            sc.AddMessageToLog(Warn, 0);
        }
    }

    //=========================================================================
    //  Bar 0: seed all state
    //=========================================================================
    if (i == 0)
    {
        Sg_EMA[0]    = sc.Close[0];
        Sg_HtfEma[0] = sc.Close[0];
        Sg_Atr[0]    = sc.High[0] - sc.Low[0];
        Sg_Atr.Arrays[0][0] = Sg_Atr[0];

        for (int a = 0; a < 10; ++a)
        {
            W1.Arrays[a][0] = 0.0f;
            W2.Arrays[a][0] = 0.0f;
            W3.Arrays[a][0] = 0.0f;
            W4.Arrays[a][0] = 0.0f;
            W5.Arrays[a][0] = 0.0f;
        }

        W2.Arrays[W2_LEGHIGH][0] = sc.High[0];
        W2.Arrays[W2_LEGLOW][0]  = sc.Low[0];
        W3.Arrays[W3_SH0P][0]    = sc.High[0];
        W3.Arrays[W3_SH1P][0]    = sc.High[0];
        W3.Arrays[W3_SH2P][0]    = sc.High[0];
        W3.Arrays[W3_SL0P][0]    = sc.Low[0];
        W3.Arrays[W3_SL1P][0]    = sc.Low[0];
        W3.Arrays[W3_SL2P][0]    = sc.Low[0];
        W4.Arrays[W4_SESSOPEN][0] = sc.Open[0];
        W4.Arrays[W4_SESSHIGH][0] = sc.High[0];
        W4.Arrays[W4_SESSLOW][0]  = sc.Low[0];
        W5.Arrays[W5_CONSOLHI][0] = sc.High[0];
        W5.Arrays[W5_CONSOLLO][0] = sc.Low[0];
        return;
    }

    //=========================================================================
    //  0. Clear this bar's outputs.
    //
    //  Sierra Chart re-runs the study function on the forming bar on every
    //  update. Without this reset a value written on an earlier tick would
    //  survive even after the condition that produced it stopped being true.
    //=========================================================================
    Sg_Buy[i]       = 0.0f;
    Sg_Sell[i]      = 0.0f;
    Sg_SetupBuy[i]  = 0.0f;
    Sg_SetupSell[i] = 0.0f;
    Sg_SwingHigh[i] = 0.0f;
    Sg_SwingLow[i]  = 0.0f;
    Sg_Entry[i]     = 0.0f;
    Sg_Stop[i]      = 0.0f;
    Sg_T1[i]        = 0.0f;
    Sg_T2[i]        = 0.0f;

    //=========================================================================
    //  1. EMAs and ATR
    //=========================================================================
    sc.ExponentialMovAvg(sc.BaseDataIn[SC_LAST], Sg_EMA, EmaLen);
    sc.ExponentialMovAvg(sc.BaseDataIn[SC_LAST], Sg_HtfEma, HtfEmaLen);

    // Wilder ATR, computed locally so it does not depend on any optional API.
    {
        const float PrevClose = sc.Close[i - 1];
        const float H = sc.High[i], L = sc.Low[i];
        float Tr = H - L;
        const float Th = (H - PrevClose) >= 0.0f ? (H - PrevClose) : (PrevClose - H);
        const float Tl = (L - PrevClose) >= 0.0f ? (L - PrevClose) : (PrevClose - L);
        Tr = MaxF(Tr, MaxF(Th, Tl));
        Sg_Atr.Arrays[0][i] = Tr;

        if (i < AtrLen)
        {
            float Sum = 0.0f;
            for (int k = 0; k <= i; ++k) Sum += Sg_Atr.Arrays[0][k];
            Sg_Atr[i] = Sum / (float)(i + 1);
        }
        else
        {
            Sg_Atr[i] = Sg_Atr[i - 1] + (Tr - Sg_Atr[i - 1]) / (float)AtrLen;
        }
    }

    const float Atr = Sg_Atr[i] > 0.0f ? Sg_Atr[i] : Tick;
    const float Ema = Sg_EMA[i];

    //=========================================================================
    //  2. Bar anatomy for the bars we care about
    //=========================================================================
    BarInfo B0, B1, B2;
    ReadBar(sc, i,     TrendBody, DojiBody, StrongClose, B0);
    ReadBar(sc, i - 1, TrendBody, DojiBody, StrongClose, B1);
    ReadBar(sc, i - 2 >= 0 ? i - 2 : 0, TrendBody, DojiBody, StrongClose, B2);

    //=========================================================================
    //  3. Session tracking
    //=========================================================================
    const int BarSec = sc.BaseDateTimeIn[i].GetTimeInSeconds();
    bool NewSession = false;
    {
        const SCDateTime DayNow  = sc.GetTradingDayDate(sc.BaseDateTimeIn[i]);
        const SCDateTime DayPrev = sc.GetTradingDayDate(sc.BaseDateTimeIn[i - 1]);
        const int PrevSec = sc.BaseDateTimeIn[i - 1].GetTimeInSeconds();

        if (DayNow != DayPrev)
            NewSession = true;
        else if (UseSession && PrevSec < SessStart && BarSec >= SessStart)
            NewSession = true;   // regular session open inside a 24h chart
    }

    if (NewSession)
    {
        W4.Arrays[W4_SESSOPEN][i] = sc.Open[i];
        W4.Arrays[W4_SESSBARS][i] = 1.0f;
        W4.Arrays[W4_SESSHIGH][i] = sc.High[i];
        W4.Arrays[W4_SESSLOW][i]  = sc.Low[i];
    }
    else
    {
        W4.Arrays[W4_SESSOPEN][i] = W4.Arrays[W4_SESSOPEN][i - 1];
        W4.Arrays[W4_SESSBARS][i] = W4.Arrays[W4_SESSBARS][i - 1] + 1.0f;
        W4.Arrays[W4_SESSHIGH][i] = MaxF(W4.Arrays[W4_SESSHIGH][i - 1], sc.High[i]);
        W4.Arrays[W4_SESSLOW][i]  = MinF(W4.Arrays[W4_SESSLOW][i - 1],  sc.Low[i]);
    }

    const int   SessBars  = (int)W4.Arrays[W4_SESSBARS][i];
    const float SessOpen  = W4.Arrays[W4_SESSOPEN][i];
    const float SessHigh  = W4.Arrays[W4_SESSHIGH][i];
    const float SessLow   = W4.Arrays[W4_SESSLOW][i];

    bool TimeOk = true;
    if (UseSession)
    {
        if (BarSec < SessStart || BarSec >= SessEnd)              TimeOk = false;
        if (BarSec >= LastEntry)                                  TimeOk = false;
        if (SkipMidday && BarSec >= MidStart && BarSec < MidEnd)  TimeOk = false;
        if (SessBars <= SkipOpenBars)                             TimeOk = false;
    }

    //=========================================================================
    //  4. Confirmed swing points (delayed by SwingStr bars, never repaints)
    //=========================================================================
    // Carry previous swing state forward first.
    W3.Arrays[W3_SH0P][i] = W3.Arrays[W3_SH0P][i - 1];
    W3.Arrays[W3_SH1P][i] = W3.Arrays[W3_SH1P][i - 1];
    W3.Arrays[W3_SH2P][i] = W3.Arrays[W3_SH2P][i - 1];
    W3.Arrays[W3_SH0I][i] = W3.Arrays[W3_SH0I][i - 1];
    W3.Arrays[W3_SH1I][i] = W3.Arrays[W3_SH1I][i - 1];
    W3.Arrays[W3_SH2I][i] = W3.Arrays[W3_SH2I][i - 1];
    W3.Arrays[W3_SL0P][i] = W3.Arrays[W3_SL0P][i - 1];
    W3.Arrays[W3_SL1P][i] = W3.Arrays[W3_SL1P][i - 1];
    W3.Arrays[W3_SL2P][i] = W3.Arrays[W3_SL2P][i - 1];
    W3.Arrays[W3_SL0I][i] = W3.Arrays[W3_SL0I][i - 1];
    W4.Arrays[W4_SL1I][i] = W4.Arrays[W4_SL1I][i - 1];
    W4.Arrays[W4_SL2I][i] = W4.Arrays[W4_SL2I][i - 1];

    const int Pivot = i - SwingStr;
    if (Pivot >= SwingStr)
    {
        bool IsSwingHigh = true, IsSwingLow = true;
        const float PH = sc.High[Pivot], PL = sc.Low[Pivot];

        for (int k = Pivot - SwingStr; k <= Pivot + SwingStr; ++k)
        {
            if (k == Pivot) continue;
            if (sc.High[k] >= PH) IsSwingHigh = false;
            if (sc.Low[k]  <= PL) IsSwingLow  = false;
        }

        if (IsSwingHigh)
        {
            W3.Arrays[W3_SH2P][i] = W3.Arrays[W3_SH1P][i];
            W3.Arrays[W3_SH2I][i] = W3.Arrays[W3_SH1I][i];
            W3.Arrays[W3_SH1P][i] = W3.Arrays[W3_SH0P][i];
            W3.Arrays[W3_SH1I][i] = W3.Arrays[W3_SH0I][i];
            W3.Arrays[W3_SH0P][i] = PH;
            W3.Arrays[W3_SH0I][i] = (float)Pivot;
            if (ShowSwings) Sg_SwingHigh[Pivot] = PH;
        }

        if (IsSwingLow)
        {
            W3.Arrays[W3_SL2P][i] = W3.Arrays[W3_SL1P][i];
            W4.Arrays[W4_SL2I][i] = W4.Arrays[W4_SL1I][i];
            W3.Arrays[W3_SL1P][i] = W3.Arrays[W3_SL0P][i];
            W4.Arrays[W4_SL1I][i] = W3.Arrays[W3_SL0I][i];
            W3.Arrays[W3_SL0P][i] = PL;
            W3.Arrays[W3_SL0I][i] = (float)Pivot;
            if (ShowSwings) Sg_SwingLow[Pivot] = PL;
        }
    }

    const float SH0 = W3.Arrays[W3_SH0P][i], SH1 = W3.Arrays[W3_SH1P][i], SH2 = W3.Arrays[W3_SH2P][i];
    const float SL0 = W3.Arrays[W3_SL0P][i], SL1 = W3.Arrays[W3_SL1P][i], SL2 = W3.Arrays[W3_SL2P][i];
    const float SH0i = W3.Arrays[W3_SH0I][i], SH1i = W3.Arrays[W3_SH1I][i];
    const float SH2i = W3.Arrays[W3_SH2I][i];
    const float SL0i = W3.Arrays[W3_SL0I][i], SL1i = W4.Arrays[W4_SL1I][i];
    const float SL2i = W4.Arrays[W4_SL2I][i];

    //=========================================================================
    //  5. Always-In state
    //
    //  Brooks: the always-in direction flips when a reasonable trader, looking
    //  at the right edge, would conclude the market has switched sides. That is
    //  usually a strong trend bar breaking out and trapping the other side.
    //=========================================================================
    int PrevAi = (int)W2.Arrays[W2_AI][i - 1];
    int Ai     = PrevAi;

    {
        const float Hi3 = HighestHigh(sc, MaxI(0, i - 3), i - 1);
        const float Lo3 = LowestLow(sc,  MaxI(0, i - 3), i - 1);

        const bool BullFlip =
            (B0.IsStrongBull && B0.Close > Hi3 && B0.Close > Ema) ||
            (B0.IsBullTrendBar && B0.Close > SH0 && B0.Close > Ema) ||
            (B0.IsBullTrendBar && B1.IsBullTrendBar && B0.Close > Ema && B0.Close > Hi3);

        const bool BearFlip =
            (B0.IsStrongBear && B0.Close < Lo3 && B0.Close < Ema) ||
            (B0.IsBearTrendBar && B0.Close < SL0 && B0.Close < Ema) ||
            (B0.IsBearTrendBar && B1.IsBearTrendBar && B0.Close < Ema && B0.Close < Lo3);

        if (BullFlip && Ai <= 0)      Ai =  1;
        else if (BearFlip && Ai >= 0) Ai = -1;
        else if (Ai == 0)             Ai = (B0.Close > Ema) ? 1 : -1;
    }

    W2.Arrays[W2_AI][i] = (float)Ai;
    W2.Arrays[W2_AIBARS][i] = (Ai != PrevAi) ? 1.0f : (W2.Arrays[W2_AIBARS][i - 1] + 1.0f);
    const int AiBars = (int)W2.Arrays[W2_AIBARS][i];
    Sg_AlwaysIn[i] = (float)Ai;

    if (ColorBars)
    {
        Sg_BarColor[i] = sc.Close[i];
        Sg_BarColor.DataColor[i] = (Ai > 0) ? RGB(0, 170, 90) : RGB(200, 50, 50);
    }
    else
    {
        Sg_BarColor[i] = 0.0f;
    }

    //=========================================================================
    //  6. Market cycle / regime
    //=========================================================================
    int Regime = REG_CHANNEL;
    float RangeHigh = sc.High[i], RangeLow = sc.Low[i];
    {
        const int From = MaxI(0, i - TrLookback + 1);
        RangeHigh = HighestHigh(sc, From, i);
        RangeLow  = LowestLow(sc,  From, i);
        const float RangeHeight = RangeHigh - RangeLow;

        const int SlopeBack = MinI(10, i);
        const float EmaSlope = (Sg_EMA[i] - Sg_EMA[i - SlopeBack]) / Atr;
        const bool  FlatEma  = (EmaSlope < 0.6f && EmaSlope > -0.6f);

        // Tight range / barb wire: a few small overlapping bars with a doji.
        const int TFrom = MaxI(0, i - TightBars + 1);
        const float TightHigh = HighestHigh(sc, TFrom, i);
        const float TightLow  = LowestLow(sc,  TFrom, i);
        int DojiCount = 0;
        for (int k = TFrom; k <= i; ++k)
        {
            BarInfo Bk;
            ReadBar(sc, k, TrendBody, DojiBody, StrongClose, Bk);
            if (Bk.IsDoji) ++DojiCount;
        }
        const bool BarbWire = ((TightHigh - TightLow) < 1.20f * Atr) && (DojiCount >= 1)
                              && ((i - TFrom + 1) >= TightBars);

        // Spike: consecutive strong trend bars covering real ground.
        const bool SpikeUp = (B0.IsBullTrendBar && B1.IsBullTrendBar)
                             && ((B0.Close - B2.Low) > 1.5f * Atr);
        const bool SpikeDn = (B0.IsBearTrendBar && B1.IsBearTrendBar)
                             && ((B2.High - B0.Close) > 1.5f * Atr);

        if (BarbWire)                                          Regime = REG_TIGHT;
        else if (SpikeUp || SpikeDn)                           Regime = REG_SPIKE;
        else if (RangeHeight < TrAtrMult * Atr && FlatEma)      Regime = REG_RANGE;
        else                                                   Regime = REG_CHANNEL;
    }
    Sg_Regime[i] = (float)Regime;

    //=========================================================================
    //  7. Micro channels
    //=========================================================================
    W2.Arrays[W2_MCBULL][i] = (sc.Low[i] > sc.Low[i - 1] && sc.High[i] > sc.High[i - 1])
                              ? W2.Arrays[W2_MCBULL][i - 1] + 1.0f : 0.0f;
    W2.Arrays[W2_MCBEAR][i] = (sc.High[i] < sc.High[i - 1] && sc.Low[i] < sc.Low[i - 1])
                              ? W2.Arrays[W2_MCBEAR][i - 1] + 1.0f : 0.0f;
    const int McBull = (int)W2.Arrays[W2_MCBULL][i];
    const int McBear = (int)W2.Arrays[W2_MCBEAR][i];

    //=========================================================================
    //  8. H / L pullback counting
    //
    //  An H1 is the first bar in a bull pullback whose high exceeds the prior
    //  bar's high. That bar is the ENTRY bar; the bar before it is the SIGNAL
    //  bar. After an H prints, price must push down again (a bar with a lower
    //  low) before the next H becomes available. The count resets when the
    //  pullback ends, i.e. when price exceeds the high of the leg it came from.
    //=========================================================================
    {
        float LegHigh    = W2.Arrays[W2_LEGHIGH][i - 1];
        float LegLow     = W2.Arrays[W2_LEGLOW][i - 1];
        int   HCount     = (int)W2.Arrays[W2_HCOUNT][i - 1];
        int   LCount     = (int)W2.Arrays[W2_LCOUNT][i - 1];
        int   HEligible  = (int)W2.Arrays[W2_HELIGIBLE][i - 1];
        int   LEligible  = (int)W2.Arrays[W2_LELIGIBLE][i - 1];

        //-- bull side ------------------------------------------------------
        if (sc.Low[i] < sc.Low[i - 1]) HEligible = 1;

        int HPrinted = 0;
        if (HEligible && sc.High[i] > sc.High[i - 1])
        {
            if (HCount < 9) ++HCount;
            HEligible = 0;
            HPrinted  = HCount;
        }

        if (sc.High[i] > LegHigh)
        {
            LegHigh   = sc.High[i];
            HCount    = 0;
            HEligible = 0;
        }

        //-- bear side ------------------------------------------------------
        if (sc.High[i] > sc.High[i - 1]) LEligible = 1;

        int LPrinted = 0;
        if (LEligible && sc.Low[i] < sc.Low[i - 1])
        {
            if (LCount < 9) ++LCount;
            LEligible = 0;
            LPrinted  = LCount;
        }

        if (sc.Low[i] < LegLow)
        {
            LegLow    = sc.Low[i];
            LCount    = 0;
            LEligible = 0;
        }

        // Reset counts when the always-in direction flips: the old pullback
        // structure no longer applies.
        if (Ai != PrevAi)
        {
            HCount = 0; LCount = 0;
            HEligible = 0; LEligible = 0;
            LegHigh = sc.High[i];
            LegLow  = sc.Low[i];
        }

        W2.Arrays[W2_LEGHIGH][i]   = LegHigh;
        W2.Arrays[W2_LEGLOW][i]    = LegLow;
        W2.Arrays[W2_HCOUNT][i]    = (float)HCount;
        W2.Arrays[W2_LCOUNT][i]    = (float)LCount;
        W2.Arrays[W2_HELIGIBLE][i] = (float)HEligible;
        W2.Arrays[W2_LELIGIBLE][i] = (float)LEligible;

        Sg_HLCount[i] = (HPrinted > 0) ? (float)HPrinted
                                       : ((LPrinted > 0) ? -(float)LPrinted : 0.0f);
    }

    const int HCountNow    = (int)W2.Arrays[W2_HCOUNT][i];
    const int LCountNow    = (int)W2.Arrays[W2_LCOUNT][i];
    const int HEligibleNow = (int)W2.Arrays[W2_HELIGIBLE][i];
    const int LEligibleNow = (int)W2.Arrays[W2_LELIGIBLE][i];

    //=========================================================================
    //  9. Trend lines, channel lines, breakouts, consolidations, MA gap bars
    //=========================================================================
    // Bull trend line through the last two confirmed swing lows; bear trend
    // line through the last two confirmed swing highs. The channel line is the
    // parallel through the intervening swing extreme.
    float BullTl = 0.0f, BearTl = 0.0f, BullChan = 0.0f, BearChan = 0.0f;
    bool  HaveBullTl = false, HaveBearTl = false;

    if (SL1i > 0.0f && SL0i > SL1i)
    {
        BullTl = LineValueAt(SL1i, SL1, SL0i, SL0, (float)i);
        HaveBullTl = true;
        if (SH0i > SL1i)
        {
            const float TlAtSh = LineValueAt(SL1i, SL1, SL0i, SL0, SH0i);
            BullChan = BullTl + (SH0 - TlAtSh);
        }
    }
    if (SH1i > 0.0f && SH0i > SH1i)
    {
        BearTl = LineValueAt(SH1i, SH1, SH0i, SH0, (float)i);
        HaveBearTl = true;
        if (SL0i > SH1i)
        {
            const float TlAtSl = LineValueAt(SH1i, SH1, SH0i, SH0, SL0i);
            BearChan = BearTl + (SL0 - TlAtSl);
        }
    }

    // Trend line break counters.
    {
        float BullBrk = W4.Arrays[W4_BULLTLBRK][i - 1];
        float BearBrk = W4.Arrays[W4_BEARTLBRK][i - 1];

        if (HaveBullTl && Ai > 0 && B0.Close < BullTl) BullBrk = 1.0f;
        else if (BullBrk > 0.0f)                       BullBrk += 1.0f;
        if (BullBrk > 60.0f) BullBrk = 0.0f;

        if (HaveBearTl && Ai < 0 && B0.Close > BearTl) BearBrk = 1.0f;
        else if (BearBrk > 0.0f)                       BearBrk += 1.0f;
        if (BearBrk > 60.0f) BearBrk = 0.0f;

        W4.Arrays[W4_BULLTLBRK][i] = BullBrk;
        W4.Arrays[W4_BEARTLBRK][i] = BearBrk;
    }
    const int BullTlBreakBars = (int)W4.Arrays[W4_BULLTLBRK][i];
    const int BearTlBreakBars = (int)W4.Arrays[W4_BEARTLBRK][i];

    // Swing breakouts.
    {
        float BullLvl  = W4.Arrays[W4_BULLBOLVL][i - 1];
        float BullBars = W4.Arrays[W4_BULLBOBARS][i - 1];
        float BearLvl  = W5.Arrays[W5_BEARBOLVL][i - 1];
        float BearBars = W5.Arrays[W5_BEARBOBARS][i - 1];

        if (SH0 > 0.0f && B0.Close > SH0 && sc.Close[i - 1] <= SH0 && B0.IsBullTrendBar)
        {
            BullLvl = SH0; BullBars = 1.0f;
        }
        else if (BullBars > 0.0f) BullBars += 1.0f;
        if (BullBars > 30.0f) BullBars = 0.0f;

        if (SL0 > 0.0f && B0.Close < SL0 && sc.Close[i - 1] >= SL0 && B0.IsBearTrendBar)
        {
            BearLvl = SL0; BearBars = 1.0f;
        }
        else if (BearBars > 0.0f) BearBars += 1.0f;
        if (BearBars > 30.0f) BearBars = 0.0f;

        W4.Arrays[W4_BULLBOLVL][i]  = BullLvl;
        W4.Arrays[W4_BULLBOBARS][i] = BullBars;
        W5.Arrays[W5_BEARBOLVL][i]  = BearLvl;
        W5.Arrays[W5_BEARBOBARS][i] = BearBars;
    }
    const float BullBoLevel = W4.Arrays[W4_BULLBOLVL][i];
    const int   BullBoBars  = (int)W4.Arrays[W4_BULLBOBARS][i];
    const float BearBoLevel = W5.Arrays[W5_BEARBOLVL][i];
    const int   BearBoBars  = (int)W5.Arrays[W5_BEARBOBARS][i];

    // Rolling consolidation (used for final flag detection).
    {
        float CHi   = W5.Arrays[W5_CONSOLHI][i - 1];
        float CLo   = W5.Arrays[W5_CONSOLLO][i - 1];
        float CBars = W5.Arrays[W5_CONSOLBARS][i - 1];
        float CIdx  = W5.Arrays[W5_CONSOLIDX][i - 1];

        const float NewHi = MaxF(CHi, sc.High[i]);
        const float NewLo = MinF(CLo, sc.Low[i]);

        if (CBars > 0.0f && (NewHi - NewLo) <= 1.60f * Atr)
        {
            CHi = NewHi; CLo = NewLo; CBars += 1.0f;
        }
        else
        {
            CHi = sc.High[i]; CLo = sc.Low[i]; CBars = 1.0f; CIdx = (float)i;
        }

        W5.Arrays[W5_CONSOLHI][i]   = CHi;
        W5.Arrays[W5_CONSOLLO][i]   = CLo;
        W5.Arrays[W5_CONSOLBARS][i] = CBars;
        W5.Arrays[W5_CONSOLIDX][i]  = CIdx;
    }

    // Moving average gap bars.
    W5.Arrays[W5_MAGAPUP][i] = (sc.Low[i]  > Ema) ? W5.Arrays[W5_MAGAPUP][i - 1] + 1.0f : 0.0f;
    W5.Arrays[W5_MAGAPDN][i] = (sc.High[i] < Ema) ? W5.Arrays[W5_MAGAPDN][i - 1] + 1.0f : 0.0f;
    const int MaGapUpPrev = (int)W5.Arrays[W5_MAGAPUP][i - 1];
    const int MaGapDnPrev = (int)W5.Arrays[W5_MAGAPDN][i - 1];

    // Last triggered signal, for second-entry recognition.
    W5.Arrays[W5_LASTSIGDIR][i] = W5.Arrays[W5_LASTSIGDIR][i - 1];
    W5.Arrays[W5_LASTSIGBAR][i] = W5.Arrays[W5_LASTSIGBAR][i - 1] > 0.0f
                                  ? W5.Arrays[W5_LASTSIGBAR][i - 1] + 1.0f : 0.0f;

    //=========================================================================
    //  10. Pattern scan
    //
    //  Everything below evaluates bar i as a potential SIGNAL bar. A long
    //  setup means: place a buy stop one tick above bar i's high.
    //=========================================================================
    Candidate Cands[24];
    int NumCands = 0;

    const bool BullCtx = (Ai > 0) || (B0.Close > Ema && Sg_EMA[i] > Sg_EMA[i - MinI(5, i)]);
    const bool BearCtx = (Ai < 0) || (B0.Close < Ema && Sg_EMA[i] < Sg_EMA[i - MinI(5, i)]);

    const bool GoodBullSignalBar = (B0.IsBull || B0.IsBullRev || B0.IsInside)
                                   && !(B0.IsStrongBear);
    const bool GoodBearSignalBar = (B0.IsBear || B0.IsBearRev || B0.IsInside)
                                   && !(B0.IsStrongBull);

    // Location helpers.
    const float DistToEmaLow  = sc.Low[i]  - Ema;   // < 0 means the bar reached the EMA
    const float DistToEmaHigh = sc.High[i] - Ema;

    //-- 10.1 H1 / H2 / H3 / H4 (with-trend long) ---------------------------
    if (HEligibleNow && BullCtx && GoodBullSignalBar)
    {
        const int NextCount = MinI(HCountNow + 1, 4);
        bool Enabled = false;
        if (NextCount == 1) Enabled = (EnH1L1 != 0);
        else if (NextCount == 2) Enabled = (EnH2L2 != 0);
        else Enabled = (EnH3H4 != 0);

        if (Enabled && NumCands < 24)
        {
            Candidate C;
            C.Dir    = 1;
            C.Code   = SETUP_H1 + (NextCount - 1);
            C.Count  = NextCount;
            C.Second = (NextCount == 2) ? 1 : 0;
            C.Score  = 0.0f;
            Cands[NumCands++] = C;
        }
    }

    //-- 10.2 L1 / L2 / L3 / L4 (with-trend short) --------------------------
    if (LEligibleNow && BearCtx && GoodBearSignalBar)
    {
        const int NextCount = MinI(LCountNow + 1, 4);
        bool Enabled = false;
        if (NextCount == 1) Enabled = (EnH1L1 != 0);
        else if (NextCount == 2) Enabled = (EnH2L2 != 0);
        else Enabled = (EnH3H4 != 0);

        if (Enabled && NumCands < 24)
        {
            Candidate C;
            C.Dir    = -1;
            C.Code   = SETUP_L1 + (NextCount - 1);
            C.Count  = NextCount;
            C.Second = (NextCount == 2) ? 1 : 0;
            C.Score  = 0.0f;
            Cands[NumCands++] = C;
        }
    }

    //-- 10.3 Breakout pullback ---------------------------------------------
    if (EnBopb && NumCands < 24)
    {
        if (BullBoBars >= 2 && BullBoBars <= 6 && Ai > 0
            && sc.Low[i] >= BullBoLevel - 0.60f * Atr && GoodBullSignalBar)
        {
            Candidate C; C.Dir = 1; C.Code = SETUP_BOPB; C.Count = HCountNow;
            C.Second = 0; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
        else if (BearBoBars >= 2 && BearBoBars <= 6 && Ai < 0
                 && sc.High[i] <= BearBoLevel + 0.60f * Atr && GoodBearSignalBar)
        {
            Candidate C; C.Dir = -1; C.Code = SETUP_BOPB; C.Count = LCountNow;
            C.Second = 0; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
    }

    //-- 10.4 Failed breakout ------------------------------------------------
    if (EnFailBo && NumCands < 24)
    {
        // Poked above a swing high but closed back below it.
        if (SH1 > 0.0f && B0.High > SH1 && B0.Close < SH1
            && (B0.High - SH1) < 1.0f * Atr && !B0.IsStrongBull)
        {
            Candidate C; C.Dir = -1; C.Code = SETUP_FAILEDBO; C.Count = 0;
            C.Second = 0; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
        else if (SL1 > 0.0f && B0.Low < SL1 && B0.Close > SL1
                 && (SL1 - B0.Low) < 1.0f * Atr && !B0.IsStrongBear)
        {
            Candidate C; C.Dir = 1; C.Code = SETUP_FAILEDBO; C.Count = 0;
            C.Second = 0; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
    }

    //-- 10.5 Double top / double bottom ------------------------------------
    if (EnDblTb && NumCands < 24)
    {
        const float Tol = DtTol * Atr;

        const float DiffH = (SH0 - SH1) >= 0.0f ? (SH0 - SH1) : (SH1 - SH0);
        if (SH0 > 0.0f && SH1 > 0.0f && DiffH <= Tol
            && (SH0i - SH1i) >= (float)WedgeGap
            && sc.High[i] >= SH0 - Tol && GoodBearSignalBar)
        {
            Candidate C; C.Dir = -1; C.Code = SETUP_DOUBLETOP; C.Count = 0;
            C.Second = 1; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }

        const float DiffL = (SL0 - SL1) >= 0.0f ? (SL0 - SL1) : (SL1 - SL0);
        if (NumCands < 24 && SL0 > 0.0f && SL1 > 0.0f && DiffL <= Tol
            && (SL0i - SL1i) >= (float)WedgeGap
            && sc.Low[i] <= SL0 + Tol && GoodBullSignalBar)
        {
            Candidate C; C.Dir = 1; C.Code = SETUP_DOUBLEBOT; C.Count = 0;
            C.Second = 1; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
    }

    //-- 10.6 Wedge / three pushes ------------------------------------------
    if (EnWedge && NumCands < 24)
    {
        // Three higher highs with the last push losing steam -> wedge top.
        if (SH2 > 0.0f && SH0 > SH1 && SH1 > SH2
            && (SH0i - SH1i) >= (float)WedgeGap && (SH1i - SH2i) >= (float)WedgeGap
            && (SH0 - SH1) <= (SH1 - SH2) * 1.15f
            && sc.High[i] >= SH0 - 0.75f * Atr
            && GoodBearSignalBar)
        {
            Candidate C; C.Dir = -1; C.Code = SETUP_WEDGETOP; C.Count = 0;
            C.Second = 1; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }

        if (NumCands < 24 && SL2 > 0.0f && SL0 < SL1 && SL1 < SL2
            && (SL0i - SL1i) >= (float)WedgeGap && (SL1i - SL2i) >= (float)WedgeGap
            && (SL1 - SL0) <= (SL2 - SL1) * 1.15f
            && sc.Low[i] <= SL0 + 0.75f * Atr
            && GoodBullSignalBar)
        {
            Candidate C; C.Dir = 1; C.Code = SETUP_WEDGEBOT; C.Count = 0;
            C.Second = 1; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
    }

    //-- 10.7 Major trend reversal ------------------------------------------
    //  Brooks: a trend line break, then a test of the old extreme that fails,
    //  then a reversal signal bar. Two separate events, in that order.
    if (EnMtr && NumCands < 24)
    {
        if (BearTlBreakBars > 0 && BearTlBreakBars <= 25 && SL0 > 0.0f
            && sc.Low[i] <= SL0 + 0.80f * Atr && B0.IsBullRev)
        {
            Candidate C; C.Dir = 1; C.Code = SETUP_MTRBULL; C.Count = 0;
            C.Second = 1; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
        else if (BullTlBreakBars > 0 && BullTlBreakBars <= 25 && SH0 > 0.0f
                 && sc.High[i] >= SH0 - 0.80f * Atr && B0.IsBearRev)
        {
            Candidate C; C.Dir = -1; C.Code = SETUP_MTRBEAR; C.Count = 0;
            C.Second = 1; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
    }

    //-- 10.8 Final flag ----------------------------------------------------
    //  A tight flag late in an extended trend, broken out of, that immediately
    //  fails back into the flag.
    if (EnFinal && NumCands < 24 && AiBars >= 12)
    {
        const int   ConsolIdx  = (int)W5.Arrays[W5_CONSOLIDX][i - 1];
        const float ConsolHi   = W5.Arrays[W5_CONSOLHI][i - 1];
        const float ConsolLo   = W5.Arrays[W5_CONSOLLO][i - 1];
        const int   ConsolBars = (int)W5.Arrays[W5_CONSOLBARS][i - 1];
        const int   BarsSince  = i - ConsolIdx;

        if (ConsolBars >= 3 && BarsSince <= 12)
        {
            if (Ai > 0 && HighestHigh(sc, MaxI(0, i - 5), i - 1) > ConsolHi
                && B0.Close < ConsolHi && GoodBearSignalBar)
            {
                Candidate C; C.Dir = -1; C.Code = SETUP_FINALFLAG; C.Count = 0;
                C.Second = 1; C.Score = 0.0f;
                Cands[NumCands++] = C;
            }
            else if (Ai < 0 && LowestLow(sc, MaxI(0, i - 5), i - 1) < ConsolLo
                     && B0.Close > ConsolLo && GoodBullSignalBar)
            {
                Candidate C; C.Dir = 1; C.Code = SETUP_FINALFLAG; C.Count = 0;
                C.Second = 1; C.Score = 0.0f;
                Cands[NumCands++] = C;
            }
        }
    }

    //-- 10.9 Climax / trend channel line overshoot -------------------------
    if (EnClimax && NumCands < 24)
    {
        const bool BigBars3 = (B0.Range > 1.3f * Atr) && (B1.Range > 1.2f * Atr);

        const bool BuyClimax = Ai > 0 && AiBars >= 8
                               && ((HaveBullTl && BullChan > 0.0f && B1.High > BullChan)
                                   || (BigBars3 && B1.IsStrongBull && B2.IsStrongBull))
                               && GoodBearSignalBar;

        const bool SellClimax = Ai < 0 && AiBars >= 8
                                && ((HaveBearTl && BearChan > 0.0f && B1.Low < BearChan)
                                    || (BigBars3 && B1.IsStrongBear && B2.IsStrongBear))
                                && GoodBullSignalBar;

        if (BuyClimax)
        {
            Candidate C; C.Dir = -1; C.Code = SETUP_CLIMAX; C.Count = 0;
            C.Second = 0; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
        else if (SellClimax)
        {
            Candidate C; C.Dir = 1; C.Code = SETUP_CLIMAX; C.Count = 0;
            C.Second = 0; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
    }

    //-- 10.10 Moving average gap bar ---------------------------------------
    if (EnMaGap && NumCands < 24)
    {
        if (Ai > 0 && MaGapUpPrev >= MaGapBars && DistToEmaLow <= 0.0f && GoodBullSignalBar)
        {
            Candidate C; C.Dir = 1; C.Code = SETUP_MAGAP; C.Count = HCountNow;
            C.Second = 0; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
        else if (Ai < 0 && MaGapDnPrev >= MaGapBars && DistToEmaHigh >= 0.0f && GoodBearSignalBar)
        {
            Candidate C; C.Dir = -1; C.Code = SETUP_MAGAP; C.Count = LCountNow;
            C.Second = 0; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
    }

    //-- 10.11 Trading range fade -------------------------------------------
    if (EnTrFade && NumCands < 24 && Regime == REG_RANGE)
    {
        const float Height = RangeHigh - RangeLow;
        if (Height > 0.0f)
        {
            const float Pos = (B0.Close - RangeLow) / Height;
            if (Pos >= 0.78f && GoodBearSignalBar && B0.IsBearRev)
            {
                Candidate C; C.Dir = -1; C.Code = SETUP_TRFADE; C.Count = 0;
                C.Second = 0; C.Score = 0.0f;
                Cands[NumCands++] = C;
            }
            else if (Pos <= 0.22f && GoodBullSignalBar && B0.IsBullRev)
            {
                Candidate C; C.Dir = 1; C.Code = SETUP_TRFADE; C.Count = 0;
                C.Second = 0; C.Score = 0.0f;
                Cands[NumCands++] = C;
            }
        }
    }

    //-- 10.12 Two bar reversal / micro double top and bottom ---------------
    if (EnTwoBar && NumCands < 24)
    {
        const float MicroTol = 1.5f * Tick;
        const float LowDiff  = (sc.Low[i] - sc.Low[i - 1]) >= 0.0f
                               ? (sc.Low[i] - sc.Low[i - 1]) : (sc.Low[i - 1] - sc.Low[i]);
        const float HighDiff = (sc.High[i] - sc.High[i - 1]) >= 0.0f
                               ? (sc.High[i] - sc.High[i - 1]) : (sc.High[i - 1] - sc.High[i]);

        const bool TwoBarBull = B1.IsBear && B0.IsBull && B0.Close > (B1.High + B1.Low) * 0.5f;
        const bool TwoBarBear = B1.IsBull && B0.IsBear && B0.Close < (B1.High + B1.Low) * 0.5f;
        const bool MicroDb    = LowDiff <= MicroTol && B0.IsBull;
        const bool MicroDt    = HighDiff <= MicroTol && B0.IsBear;

        const bool AtSupport    = SL0 > 0.0f && sc.Low[i]  <= SL0 + 0.80f * Atr;
        const bool AtResistance = SH0 > 0.0f && sc.High[i] >= SH0 - 0.80f * Atr;

        if ((TwoBarBull || MicroDb) && (AtSupport || DistToEmaLow <= 0.0f))
        {
            Candidate C; C.Dir = 1; C.Code = SETUP_TWOBAR; C.Count = 0;
            C.Second = 0; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
        else if ((TwoBarBear || MicroDt) && (AtResistance || DistToEmaHigh >= 0.0f))
        {
            Candidate C; C.Dir = -1; C.Code = SETUP_TWOBAR; C.Count = 0;
            C.Second = 0; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
    }

    //-- 10.13 Opening reversal ---------------------------------------------
    if (EnOpenRev && NumCands < 24 && SessBars >= 3 && SessBars <= 12)
    {
        if (sc.High[i] >= SessHigh - 0.25f * Atr && B0.IsBearRev && B0.Close < SessOpen)
        {
            Candidate C; C.Dir = -1; C.Code = SETUP_OPENREV; C.Count = 0;
            C.Second = 0; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
        else if (sc.Low[i] <= SessLow + 0.25f * Atr && B0.IsBullRev && B0.Close > SessOpen)
        {
            Candidate C; C.Dir = 1; C.Code = SETUP_OPENREV; C.Count = 0;
            C.Second = 0; C.Score = 0.0f;
            Cands[NumCands++] = C;
        }
    }

    //-- 10.14 ii / ioi / iii breakout --------------------------------------
    if (EnInsideBo && NumCands < 24 && B0.IsInside)
    {
        const bool Ii  = B1.IsInside;
        const bool Ioi = B1.IsOutside && (i >= 3);

        if (Ii || Ioi)
        {
            if (BullCtx && !B0.IsStrongBear)
            {
                Candidate C; C.Dir = 1; C.Code = SETUP_INSIDEBO; C.Count = 0;
                C.Second = 0; C.Score = 0.0f;
                Cands[NumCands++] = C;
            }
            else if (BearCtx && !B0.IsStrongBull)
            {
                Candidate C; C.Dir = -1; C.Code = SETUP_INSIDEBO; C.Count = 0;
                C.Second = 0; C.Score = 0.0f;
                Cands[NumCands++] = C;
            }
        }
    }

    //=========================================================================
    //  11. Score the candidates
    //=========================================================================
    int   BestIdx = -1;
    float BestScore = -1.0f;

    for (int c = 0; c < NumCands; ++c)
    {
        Candidate& C = Cands[c];
        float S = 40.0f;

        const bool Long = (C.Dir > 0);

        // Signal bar body and close position.
        S += 22.0f * ClampF(B0.BodyRatio, 0.0f, 1.0f);
        if (Long)
        {
            if (B0.ClosePos >= 0.70f) S += 12.0f;
            if (B0.IsBullRev)         S += 8.0f;
            if (B0.IsStrongBear)      S -= 22.0f;
            if (B0.Close > sc.High[i - 1]) S += 6.0f;
        }
        else
        {
            if (B0.ClosePos <= 0.30f) S += 12.0f;
            if (B0.IsBearRev)         S += 8.0f;
            if (B0.IsStrongBull)      S -= 22.0f;
            if (B0.Close < sc.Low[i - 1])  S += 6.0f;
        }

        // Bar size sanity: Brooks avoids both huge risk and meaningless dojis.
        const float SizeRatio = B0.Range / Atr;
        if (SizeRatio >= 0.5f && SizeRatio <= 1.6f) S += 8.0f;
        if (SizeRatio > 2.2f)                       S -= 10.0f;
        if (B0.IsDoji)                              S -= 14.0f;

        // Context: with the always-in direction, or against it.
        const bool WithTrend = (Long && Ai > 0) || (!Long && Ai < 0);
        const bool Reversal  = (C.Code == SETUP_DOUBLETOP || C.Code == SETUP_DOUBLEBOT
                                || C.Code == SETUP_WEDGETOP || C.Code == SETUP_WEDGEBOT
                                || C.Code == SETUP_MTRBULL || C.Code == SETUP_MTRBEAR
                                || C.Code == SETUP_FINALFLAG || C.Code == SETUP_CLIMAX
                                || C.Code == SETUP_FAILEDBO || C.Code == SETUP_OPENREV);

        if (WithTrend)                 S += 12.0f;
        else if (!Reversal)            S -= 16.0f;   // countertrend with no reversal case

        // Reversals need a trend line break first, otherwise they are just
        // fading a trend, which Brooks warns against.
        if (Reversal)
        {
            const bool HadBreak = Long ? (BearTlBreakBars > 0) : (BullTlBreakBars > 0);
            if (HadBreak) S += 10.0f; else S -= 12.0f;
        }

        // Second entries are the highest probability variant.
        if (C.Second) S += 10.0f;

        // Regime.
        if (Regime == REG_TIGHT)                       S -= 22.0f;  // barb wire
        if (Regime == REG_RANGE && !Reversal && C.Code != SETUP_TRFADE) S -= 10.0f;
        if (Regime == REG_SPIKE && WithTrend)          S += 8.0f;
        if (Regime == REG_SPIKE && !WithTrend)         S -= 14.0f;
        if (Regime == REG_CHANNEL && WithTrend)        S += 6.0f;

        // Micro channels: do not fade one.
        if (!Long && McBull >= MicroChanMin) S -= 15.0f;
        if (Long  && McBear >= MicroChanMin) S -= 15.0f;

        // Location relative to the 20 EMA.
        if (Long  && DistToEmaLow  <= 0.30f * Atr && DistToEmaLow  >= -1.2f * Atr) S += 8.0f;
        if (!Long && DistToEmaHigh >= -0.30f * Atr && DistToEmaHigh <= 1.2f * Atr) S += 8.0f;

        // Higher timeframe EMA agreement.
        if (Long  && B0.Close > Sg_HtfEma[i]) S += 5.0f;
        if (!Long && B0.Close < Sg_HtfEma[i]) S += 5.0f;

        // Extended pullback counts are weaker as continuation entries.
        if (C.Code == SETUP_H4 || C.Code == SETUP_L4) S -= 6.0f;

        C.Score = ClampF(S, 0.0f, 100.0f);

        if (C.Score > BestScore)
        {
            BestScore = C.Score;
            BestIdx = c;
        }
    }

    //=========================================================================
    //  12. Publish the setup on this signal bar
    //=========================================================================
    for (int a = 0; a < 10; ++a) W1.Arrays[a][i] = 0.0f;

    Sg_SetupCode[i] = 0.0f;
    Sg_Strength[i]  = 0.0f;

    if (BestIdx >= 0)
    {
        const Candidate& C = Cands[BestIdx];

        bool DirOk = true;
        if (DirFilter == 1 && C.Dir < 0) DirOk = false;
        if (DirFilter == 2 && C.Dir > 0) DirOk = false;
        if (DirFilter == 3 && ((C.Dir > 0 && Ai <= 0) || (C.Dir < 0 && Ai >= 0))) DirOk = false;

        if (ReqEma)
        {
            if (C.Dir > 0 && B0.Close <= Ema) DirOk = false;
            if (C.Dir < 0 && B0.Close >= Ema) DirOk = false;
        }

        float Entry, Stop;
        if (C.Dir > 0)
        {
            Entry = sc.High[i] + Tick;
            Stop  = sc.Low[i]  - Tick;
        }
        else
        {
            Entry = sc.Low[i]  - Tick;
            Stop  = sc.High[i] + Tick;
        }

        const float RiskPts   = (Entry - Stop) >= 0.0f ? (Entry - Stop) : (Stop - Entry);
        const int   RiskTicks = (int)((RiskPts / Tick) + 0.5f);

        bool RiskOk = true;
        if (MaxStopTicks > 0 && RiskTicks > MaxStopTicks) RiskOk = false;
        if (RiskTicks < MinStopTicks)                     RiskOk = false;

        if (DirOk && RiskOk && TimeOk && C.Score >= (float)MinStrength)
        {
            float T1, T2;
            if (C.Dir > 0)
            {
                T1 = Entry + T1R * RiskPts;
                T2 = Entry + T2R * RiskPts;
                if (UseMm && SH0 > 0.0f && SL0 > 0.0f && SH0 > SL0)
                {
                    const float Leg = SH0 - SL0;
                    const float Mm  = Entry + Leg;
                    if (Mm > T1) T2 = Mm;
                }
            }
            else
            {
                T1 = Entry - T1R * RiskPts;
                T2 = Entry - T2R * RiskPts;
                if (UseMm && SH0 > 0.0f && SL0 > 0.0f && SH0 > SL0)
                {
                    const float Leg = SH0 - SL0;
                    const float Mm  = Entry - Leg;
                    if (Mm < T1) T2 = Mm;
                }
            }

            W1.Arrays[W1_DIR][i]    = (float)C.Dir;
            W1.Arrays[W1_CODE][i]   = (float)C.Code;
            W1.Arrays[W1_SCORE][i]  = C.Score;
            W1.Arrays[W1_ENTRY][i]  = sc.RoundToTickSize(Entry, Tick);
            W1.Arrays[W1_STOP][i]   = sc.RoundToTickSize(Stop,  Tick);
            W1.Arrays[W1_T1][i]     = sc.RoundToTickSize(T1,    Tick);
            W1.Arrays[W1_T2][i]     = sc.RoundToTickSize(T2,    Tick);
            W1.Arrays[W1_COUNT][i]  = (float)C.Count;
            W1.Arrays[W1_SECOND][i] = (float)C.Second;

            Sg_SetupCode[i] = (float)(C.Dir > 0 ? C.Code : -C.Code);
            Sg_Strength[i]  = C.Score;

            const float Off = ArrowOff * Atr;
            if (C.Dir > 0) Sg_SetupBuy[i]  = sc.Low[i]  - Off * 0.6f;
            else           Sg_SetupSell[i] = sc.High[i] + Off * 0.6f;
        }
    }

    //=========================================================================
    //  13. Trigger check: did a later bar trade one tick beyond the signal bar?
    //
    //  A setup published on bar j stays live for up to 3 bars, which is how
    //  long a Brooks-style resting stop order would normally be left in place.
    //=========================================================================
    int   TrigDir = 0, TrigCode = 0, TrigSecond = 0;
    float TrigScore = 0.0f, TrigEntry = 0.0f, TrigStop = 0.0f, TrigT1 = 0.0f, TrigT2 = 0.0f;

    for (int Back = 1; Back <= 3 && (i - Back) >= 0; ++Back)
    {
        const int j = i - Back;
        const int Dir = (int)W1.Arrays[W1_DIR][j];
        if (Dir == 0) continue;

        const float EntryPx = W1.Arrays[W1_ENTRY][j];
        const float StopPx  = W1.Arrays[W1_STOP][j];

        // A setup is used up once an earlier bar either filled the entry stop
        // or took out the protective stop first. This is derived purely from
        // price rather than from a flag written on a previous pass, so the
        // result is identical however many times the bar is recalculated.
        bool Consumed = false;
        for (int k = j + 1; k < i; ++k)
        {
            if (Dir > 0 && (sc.High[k] >= EntryPx || sc.Low[k]  < StopPx)) { Consumed = true; break; }
            if (Dir < 0 && (sc.Low[k]  <= EntryPx || sc.High[k] > StopPx)) { Consumed = true; break; }
        }
        if (Consumed) continue;

        const bool Hit = (Dir > 0) ? (sc.High[i] >= EntryPx) : (sc.Low[i] <= EntryPx);
        if (!Hit) continue;

        TrigDir    = Dir;
        TrigCode   = (int)W1.Arrays[W1_CODE][j];
        TrigScore  = W1.Arrays[W1_SCORE][j];
        TrigSecond = (int)W1.Arrays[W1_SECOND][j];
        TrigEntry  = EntryPx;
        TrigStop   = StopPx;
        TrigT1     = W1.Arrays[W1_T1][j];
        TrigT2     = W1.Arrays[W1_T2][j];
        break;
    }

    if (TrigDir != 0)
    {
        const float Off = ArrowOff * Atr;
        if (TrigDir > 0) Sg_Buy[i]  = sc.Low[i]  - Off;
        else             Sg_Sell[i] = sc.High[i] + Off;

        Sg_Entry[i] = TrigEntry;
        Sg_Stop[i]  = TrigStop;
        Sg_T1[i]    = TrigT1;
        Sg_T2[i]    = TrigT2;

        W5.Arrays[W5_LASTSIGDIR][i] = (float)TrigDir;
        W5.Arrays[W5_LASTSIGBAR][i] = 1.0f;
    }

    //=========================================================================
    //  14. Labels
    //=========================================================================
    const bool InLabelWindow = (i >= sc.ArraySize - LabelBars);

    if (InLabelWindow && ShowHl && Sg_HLCount[i] != 0.0f)
    {
        const int Cnt = (int)Sg_HLCount[i];
        SCString Txt;
        if (Cnt > 0)
        {
            Txt.Format("H%d", Cnt);
            DrawText(sc, i, sc.Low[i] - 0.25f * Atr, Txt, RGB(120, 200, 255),
                     MaxI(6, FontSize - 1), LN_HL + i, false);
        }
        else
        {
            Txt.Format("L%d", -Cnt);
            DrawText(sc, i, sc.High[i] + 0.25f * Atr, Txt, RGB(255, 170, 120),
                     MaxI(6, FontSize - 1), LN_HL + i, false);
        }
    }

    if (InLabelWindow && ShowLabels && TrigDir != 0)
    {
        SCString Txt;
        Txt.Format("%s%s %d", SetupName(TrigCode).GetChars(),
                   TrigSecond ? " (2nd)" : "", (int)(TrigScore + 0.5f));

        if (TrigDir > 0)
            DrawText(sc, i, sc.Low[i] - 0.85f * Atr, Txt, RGB(0, 230, 120),
                     FontSize, LN_SIG + i, true);
        else
            DrawText(sc, i, sc.High[i] + 0.85f * Atr, Txt, RGB(255, 90, 90),
                     FontSize, LN_SIG + i, true);
    }

    //=========================================================================
    //  15. Entry / stop / target lines for the most recent triggered signal
    //=========================================================================
    if (i == LastIdx && ShowLevels)
    {
        int Found = -1;
        for (int k = LastIdx; k >= MaxI(0, LastIdx - 200); --k)
        {
            if (Sg_Entry[k] != 0.0f) { Found = k; break; }
        }

        if (Found >= 0)
        {
            const int To = MinI(LastIdx, Found + 40);
            DrawLevel(sc, Found, To, Sg_Entry[Found], RGB(220, 220, 220),
                      LINESTYLE_SOLID,  LN_LEVEL + 0, "Entry");
            DrawLevel(sc, Found, To, Sg_Stop[Found],  RGB(255, 80, 80),
                      LINESTYLE_DASH,   LN_LEVEL + 1, "Stop");
            DrawLevel(sc, Found, To, Sg_T1[Found],    RGB(80, 220, 140),
                      LINESTYLE_DOT,    LN_LEVEL + 2, "T1");
            DrawLevel(sc, Found, To, Sg_T2[Found],    RGB(60, 180, 220),
                      LINESTYLE_DOT,    LN_LEVEL + 3, "T2");
        }
    }

    //=========================================================================
    //  16. Context dashboard
    //=========================================================================
    if (i == LastIdx && ShowDash)
    {
        SCString Dash;
        Dash.Format("ALWAYS IN: %s (%d bars)   CYCLE: %s   H%d / L%d   %s%s",
                    Ai > 0 ? "LONG" : "SHORT",
                    AiBars,
                    RegimeName(Regime).GetChars(),
                    HCountNow, LCountNow,
                    McBull >= MicroChanMin ? "  [bull micro channel]" : "",
                    McBear >= MicroChanMin ? "  [bear micro channel]" : "");

        s_UseTool Tool;
        Tool.Clear();
        Tool.ChartNumber = sc.ChartNumber;
        Tool.DrawingType = DRAWING_TEXT;
        Tool.LineNumber  = LN_DASH;
        Tool.BeginIndex  = MaxI(0, LastIdx - 55);
        Tool.Region      = sc.GraphRegion;
        Tool.UseRelativeVerticalValues = 1;
        Tool.BeginValue  = 96.0f;
        Tool.Color       = Ai > 0 ? RGB(120, 255, 170) : RGB(255, 140, 140);
        Tool.FontSize    = FontSize + 1;
        Tool.FontBold    = 1;
        Tool.Text        = Dash;
        Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
        sc.UseTool(Tool);
    }

    //=========================================================================
    //  17. Alerts
    //
    //  Only on live updates (sc.UpdateStartIndex > 0 means this is not a full
    //  recalculation) and only on the newest bars, so loading history does not
    //  produce a storm of alerts.
    //=========================================================================
    if (!AlertsOn) return;

    const bool LiveUpdate = (sc.UpdateStartIndex > 0) && (i >= sc.ArraySize - 2);
    if (!LiveUpdate) return;

    SCString Symbol = sc.Input[IN_SYMLABEL].GetString();
    if (Symbol.GetLength() == 0)
        Symbol = sc.Symbol;

    SCString TimeStr = sc.DateTimeToString(sc.BaseDateTimeIn[i], FLAG_DT_COMPLETE_DATETIME);

    const bool WantTrigger = (SignalMode == 0);

    if (TrigDir != 0 && WantTrigger)
    {
        SCString Msg;
        Msg.Format("%s %s | %s %s%s | AlwaysIn %s | %s | Strength %d | Entry %s  Stop %s  T1 %s  T2 %s",
                   Symbol.GetChars(),
                   TimeStr.GetChars(),
                   TrigDir > 0 ? "BUY" : "SELL",
                   SetupName(TrigCode).GetChars(),
                   TrigSecond ? " (2nd entry)" : "",
                   Ai > 0 ? "Long" : "Short",
                   RegimeName(Regime).GetChars(),
                   (int)(TrigScore + 0.5f),
                   sc.FormatGraphValue(TrigEntry, sc.BaseGraphValueFormat).GetChars(),
                   sc.FormatGraphValue(TrigStop,  sc.BaseGraphValueFormat).GetChars(),
                   sc.FormatGraphValue(TrigT1,    sc.BaseGraphValueFormat).GetChars(),
                   sc.FormatGraphValue(TrigT2,    sc.BaseGraphValueFormat).GetChars());

        sc.SetAlert(AlertSound, Msg.GetChars());
        if (AlertLog) sc.AddMessageToLog(Msg, 0);
    }

    const int SetupDir = (int)W1.Arrays[W1_DIR][i];
    if (SetupDir != 0 && (AlertSetup || SignalMode == 1))
    {
        SCString Msg;
        Msg.Format("%s %s | SIGNAL BAR %s %s | AlwaysIn %s | %s | Strength %d | Buy stop %s  Stop %s",
                   Symbol.GetChars(),
                   TimeStr.GetChars(),
                   SetupDir > 0 ? "LONG" : "SHORT",
                   SetupName((int)W1.Arrays[W1_CODE][i]).GetChars(),
                   Ai > 0 ? "Long" : "Short",
                   RegimeName(Regime).GetChars(),
                   (int)(W1.Arrays[W1_SCORE][i] + 0.5f),
                   sc.FormatGraphValue(W1.Arrays[W1_ENTRY][i], sc.BaseGraphValueFormat).GetChars(),
                   sc.FormatGraphValue(W1.Arrays[W1_STOP][i],  sc.BaseGraphValueFormat).GetChars());

        sc.SetAlert(AlertSound, Msg.GetChars());
        if (AlertLog) sc.AddMessageToLog(Msg, 0);
    }
}
