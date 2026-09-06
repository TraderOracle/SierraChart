// =============================================================================
//  Fair Value Gap (FVG) + Inverse Fair Value Gap (IFVG)
//  ACSIL study for Sierra Chart
// -----------------------------------------------------------------------------
//  WHAT IT DRAWS
//
//  Bullish FVG   : Low[i] > High[i-2]   -> unfilled gap between those two levels
//                  Acts as a SUPPORT zone.  Box = [ High[i-2] , Low[i] ]
//
//  Bearish FVG   : High[i] < Low[i-2]   -> unfilled gap between those two levels
//                  Acts as a RESISTANCE zone. Box = [ High[i] , Low[i-2] ]
//
//  IFVG          : an FVG that price has CLOSED completely through.
//                  It flips polarity:
//                     bullish FVG closed below  -> becomes RESISTANCE (bearish IFVG)
//                     bearish FVG closed above  -> becomes SUPPORT    (bullish IFVG)
//                  The IFVG stays alive until price closes back through it again.
//
//  LIFE CYCLE OF ONE ZONE
//      ACTIVE  --touched per mitigation rule-->  MITIGATED (box freezes)
//      ACTIVE/MITIGATED --closed fully through-->  INVERTED (IFVG, keeps extending)
//      INVERTED --closed back through-->           DEAD (box freezes)
//
//  NOTE ON REPAINTING
//      Everything is evaluated on CLOSED bars only, so a zone never appears and
//      then disappears while the current bar is still forming.
// =============================================================================

#include "sierrachart.h"
#include <vector>

SCDLLName("ICT Fair Value Gaps and Inverse")

// -----------------------------------------------------------------------------
//  Data model
// -----------------------------------------------------------------------------
namespace FVG
{
    enum GapState
    {
        GAP_ACTIVE    = 0,   // valid, untested
        GAP_MITIGATED = 1,   // tested per the chosen rule, box frozen
        GAP_INVERTED  = 2,   // closed through -> now an IFVG
        GAP_DEAD      = 3    // IFVG failed, box frozen
    };

    struct s_Gap
    {
        int   StartIndex;    // first bar of the 3-bar pattern
        int   EndIndex;      // right edge of the box
        float Top;
        float Bottom;
        bool  Bullish;       // true = created as a bullish (support) gap
        int   State;
        int   InvertIndex;   // bar where it flipped to an IFVG, -1 if never
        bool  WickFullFired; // full wick sweep already reported for this zone
        bool  WickHalfFired; // 50% wick tap already reported for this zone

        // drawing bookkeeping
        int   BoxLine;
        int   MidLine;
        int   TextLine;
        int   DrawnEnd;      // last EndIndex we actually drew
        int   DrawnState;    // last State we actually drew
        bool  Visible;
    };

    typedef std::vector<s_Gap> GapList;

    inline float MidPrice(const s_Gap& G) { return (G.Top + G.Bottom) * 0.5f; }
}

// Input index constants -- keeps the code readable further down.
enum e_Inputs
{
    IN_SHOW_BULL = 0,
    IN_SHOW_BEAR,
    IN_SHOW_IFVG,
    IN_MIN_TICKS,
    IN_MIN_BODY_PCT,
    IN_MITIGATION_RULE,
    IN_FILLED_STYLE,
    IN_EXTEND_BARS,
    IN_DRAW_MIDLINE,
    IN_SHOW_LABELS,
    IN_FONT_SIZE,
    IN_TRANSPARENCY,
    IN_MAX_GAPS,
    IN_ALERT_NEW,
    IN_ALERT_INVERT,
    IN_ALERT_SOUND,
    IN_DRAW_OUTLINE,
    IN_ALERT_WICK_FULL,
    IN_ALERT_WICK_FULL_SOUND,
    IN_ALERT_WICK_HALF,
    IN_ALERT_WICK_HALF_SOUND
};

// Subgraph index constants (these exist purely so the user can pick colors
// from the normal study Settings > Subgraphs tab).
enum e_Subgraphs
{
    SG_BULL_FVG = 0,
    SG_BEAR_FVG,
    SG_BULL_IFVG,
    SG_BEAR_IFVG,
    SG_SPENT_DARK,
    SG_SPENT_MEDIUM,
    SG_MIDLINE,
    SG_LABEL
};

// Options for the "Filled Zone Display" input
enum e_FilledStyle
{
    FILLED_HIDE   = 0,
    FILLED_DARK   = 1,
    FILLED_MEDIUM = 2,
    FILLED_NORMAL = 3
};

// -----------------------------------------------------------------------------
//  Remove a zone's drawings from the chart
// -----------------------------------------------------------------------------
static void EraseGap(SCStudyInterfaceRef sc, FVG::s_Gap& Gap)
{
    if (!Gap.Visible)
        return;

    sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, Gap.BoxLine);
    sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, Gap.MidLine);
    sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, Gap.TextLine);

    Gap.Visible    = false;
    Gap.DrawnEnd   = -1;
    Gap.DrawnState = -1;
}

// -----------------------------------------------------------------------------
//  Draw (or update) one zone: shaded box + optional 50% line + optional label
// -----------------------------------------------------------------------------
static void DrawGap(SCStudyInterfaceRef sc, FVG::s_Gap& Gap)
{
    using namespace FVG;

    const int  Transparency = sc.Input[IN_TRANSPARENCY].GetInt();
    const bool DrawMid      = sc.Input[IN_DRAW_MIDLINE].GetYesNo() != 0;
    const bool ShowLabels   = sc.Input[IN_SHOW_LABELS].GetYesNo() != 0;
    const int  FontSize     = sc.Input[IN_FONT_SIZE].GetInt();

    // ---- pick the color and the caption for the current state ---------------
    // Direction is already obvious from the color, so the label only says
    // which kind of zone it is.
    COLORREF ZoneColor;
    SCString Label;

    if (Gap.State == GAP_INVERTED)
    {
        // A bullish gap that failed is now resistance, and vice versa.
        ZoneColor = Gap.Bullish ? sc.Subgraph[SG_BEAR_IFVG].PrimaryColor
                                : sc.Subgraph[SG_BULL_IFVG].PrimaryColor;
        Label     = "IFVG";
    }
    else
    {
        ZoneColor = Gap.Bullish ? sc.Subgraph[SG_BULL_FVG].PrimaryColor
                                : sc.Subgraph[SG_BEAR_FVG].PrimaryColor;
        Label     = "FVG";
    }

    // A filled (mitigated) or dead zone can optionally be greyed out so the
    // live zones stand out.
    const int FilledStyle = sc.Input[IN_FILLED_STYLE].GetIndex();

    if (Gap.State == GAP_MITIGATED || Gap.State == GAP_DEAD)
    {
        if      (FilledStyle == FILLED_DARK)   ZoneColor = sc.Subgraph[SG_SPENT_DARK].PrimaryColor;
        else if (FilledStyle == FILLED_MEDIUM) ZoneColor = sc.Subgraph[SG_SPENT_MEDIUM].PrimaryColor;
        // FILLED_NORMAL leaves the original color untouched.
    }

    const int UseTransparency = Transparency;

    // Once a zone has inverted, the box starts at the flip so it reads as a
    // fresh IFVG rather than a box stretching back to the original gap.
    const int BoxStart = (Gap.InvertIndex >= 0) ? Gap.InvertIndex : Gap.StartIndex;

    // ---- the shaded box -----------------------------------------------------
    s_UseTool Tool;
    Tool.Clear();
    Tool.ChartNumber           = sc.ChartNumber;
    Tool.DrawingType           = DRAWING_RECTANGLEHIGHLIGHT;
    Tool.LineNumber            = Gap.BoxLine;
    Tool.BeginIndex            = BoxStart;
    Tool.EndIndex              = Gap.EndIndex;
    Tool.BeginValue            = Gap.Bottom;
    Tool.EndValue              = Gap.Top;
    Tool.Color                 = ZoneColor;        // border
    Tool.SecondaryColor        = ZoneColor;        // fill
    Tool.TransparencyLevel     = UseTransparency;
    // Width 0 removes the border entirely. The border color is kept identical
    // to the fill so nothing shows through even if a hairline is drawn.
    Tool.LineWidth             = sc.Input[IN_DRAW_OUTLINE].GetYesNo() ? 1 : 0;
    Tool.AddMethod             = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);

    // ---- 50% line (consequent encroachment) ---------------------------------
    if (DrawMid)
    {
        Tool.Clear();
        Tool.ChartNumber           = sc.ChartNumber;
        Tool.DrawingType           = DRAWING_LINE;
        Tool.LineNumber            = Gap.MidLine;
        Tool.BeginIndex            = BoxStart;
        Tool.EndIndex              = Gap.EndIndex;
        Tool.BeginValue            = MidPrice(Gap);
        Tool.EndValue              = MidPrice(Gap);
        Tool.Color                 = sc.Subgraph[SG_MIDLINE].PrimaryColor;
        Tool.LineStyle             = LINESTYLE_DASH;
        Tool.LineWidth             = 1;
        Tool.TransparencyLevel     = UseTransparency;
        Tool.AddMethod             = UTAM_ADD_OR_ADJUST;
        Tool.AddAsUserDrawnDrawing = 0;
        sc.UseTool(Tool);
    }

    // ---- label, pinned to the right edge so it never covers the candles -----
    if (ShowLabels)
    {
        Tool.Clear();
        Tool.ChartNumber           = sc.ChartNumber;
        Tool.DrawingType           = DRAWING_TEXT;
        Tool.LineNumber            = Gap.TextLine;
        Tool.BeginIndex            = Gap.EndIndex;
        Tool.BeginValue            = MidPrice(Gap);
        Tool.Text                  = Label;
        Tool.Color                 = sc.Subgraph[SG_LABEL].PrimaryColor;
        Tool.FontSize              = FontSize;
        Tool.FontBold              = 0;
        Tool.TextAlignment         = DT_RIGHT | DT_VCENTER;
        Tool.AddMethod             = UTAM_ADD_OR_ADJUST;
        Tool.AddAsUserDrawnDrawing = 0;
        sc.UseTool(Tool);
    }

    Gap.Visible    = true;
    Gap.DrawnEnd   = Gap.EndIndex;
    Gap.DrawnState = Gap.State;
}

// =============================================================================
//  Study entry point
// =============================================================================
SCSFExport scsf_FairValueGaps(SCStudyInterfaceRef sc)
{
    using namespace FVG;

    // -------------------------------------------------------------------------
    //  Defaults
    // -------------------------------------------------------------------------
    if (sc.SetDefaults)
    {
        sc.GraphName             = "ICT Fair Value Gaps and Inverse";
        sc.StudyDescription      = "Draws unfilled 3-bar imbalances (Fair Value Gaps) "
                                   "and flips them into Inverse FVGs once price closes "
                                   "through them.";
        sc.GraphRegion           = 0;      // main price graph
        sc.AutoLoop              = 0;      // manual looping
        sc.ValueFormat           = VALUEFORMAT_INHERITED;
        sc.DrawZeros             = 0;
        sc.FreeDLL               = 0;
        sc.CalculationPrecedence = LOW_PREC_LEVEL;

        // --- colors (these are the only reason the subgraphs exist) ----------
        sc.Subgraph[SG_BULL_FVG].Name         = "Bullish FVG (support)";
        sc.Subgraph[SG_BULL_FVG].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BULL_FVG].PrimaryColor = RGB(0, 190, 140);

        sc.Subgraph[SG_BEAR_FVG].Name         = "Bearish FVG (resistance)";
        sc.Subgraph[SG_BEAR_FVG].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BEAR_FVG].PrimaryColor = RGB(225, 70, 90);

        sc.Subgraph[SG_BULL_IFVG].Name         = "Bullish IFVG (support)";
        sc.Subgraph[SG_BULL_IFVG].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BULL_IFVG].PrimaryColor = RGB(90, 170, 255);

        sc.Subgraph[SG_BEAR_IFVG].Name         = "Bearish IFVG (resistance)";
        sc.Subgraph[SG_BEAR_IFVG].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BEAR_IFVG].PrimaryColor = RGB(255, 165, 60);

        sc.Subgraph[SG_SPENT_DARK].Name         = "Filled Zone - Dark Grey";
        sc.Subgraph[SG_SPENT_DARK].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_SPENT_DARK].PrimaryColor = RGB(70, 70, 70);

        sc.Subgraph[SG_SPENT_MEDIUM].Name         = "Filled Zone - Medium Grey";
        sc.Subgraph[SG_SPENT_MEDIUM].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_SPENT_MEDIUM].PrimaryColor = RGB(140, 140, 140);

        sc.Subgraph[SG_MIDLINE].Name         = "50% Line (CE)";
        sc.Subgraph[SG_MIDLINE].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_MIDLINE].PrimaryColor = RGB(160, 160, 160);

        sc.Subgraph[SG_LABEL].Name         = "Label Text";
        sc.Subgraph[SG_LABEL].DrawStyle    = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_LABEL].PrimaryColor = RGB(200, 200, 200);

        // --- inputs ----------------------------------------------------------
        sc.Input[IN_SHOW_BULL].Name = "Show Bullish FVGs";
        sc.Input[IN_SHOW_BULL].SetYesNo(1);

        sc.Input[IN_SHOW_BEAR].Name = "Show Bearish FVGs";
        sc.Input[IN_SHOW_BEAR].SetYesNo(1);

        sc.Input[IN_SHOW_IFVG].Name = "Show Inverse FVGs (IFVG)";
        sc.Input[IN_SHOW_IFVG].SetYesNo(1);

        sc.Input[IN_MIN_TICKS].Name = "Minimum Gap Size (Ticks)";
        sc.Input[IN_MIN_TICKS].SetInt(1);
        sc.Input[IN_MIN_TICKS].SetIntLimits(0, 10000);

        sc.Input[IN_MIN_BODY_PCT].Name = "Min. Middle-Bar Body % of Range (0 = off)";
        sc.Input[IN_MIN_BODY_PCT].SetInt(0);
        sc.Input[IN_MIN_BODY_PCT].SetIntLimits(0, 100);

        sc.Input[IN_MITIGATION_RULE].Name = "Mitigation Rule";
        sc.Input[IN_MITIGATION_RULE].SetCustomInputStrings("Touch Near Edge;Reach 50% (CE);Full Fill");
        sc.Input[IN_MITIGATION_RULE].SetCustomInputIndex(0);

        sc.Input[IN_FILLED_STYLE].Name = "Filled Zone Display";
        sc.Input[IN_FILLED_STYLE].SetCustomInputStrings("Hide;Dark Grey;Medium Grey;Normal Color");
        sc.Input[IN_FILLED_STYLE].SetCustomInputIndex(FILLED_DARK);

        sc.Input[IN_EXTEND_BARS].Name = "Extend Right Past Last Bar (Bars)";
        sc.Input[IN_EXTEND_BARS].SetInt(0);
        sc.Input[IN_EXTEND_BARS].SetIntLimits(0, 200);

        sc.Input[IN_DRAW_MIDLINE].Name = "Draw 50% Line (Consequent Encroachment)";
        sc.Input[IN_DRAW_MIDLINE].SetYesNo(0);

        sc.Input[IN_SHOW_LABELS].Name = "Show Labels";
        sc.Input[IN_SHOW_LABELS].SetYesNo(1);

        sc.Input[IN_FONT_SIZE].Name = "Label Font Size";
        sc.Input[IN_FONT_SIZE].SetInt(8);
        sc.Input[IN_FONT_SIZE].SetIntLimits(5, 40);

        sc.Input[IN_TRANSPARENCY].Name = "Fill Transparency (0-100)";
        sc.Input[IN_TRANSPARENCY].SetInt(72);
        sc.Input[IN_TRANSPARENCY].SetIntLimits(0, 100);

        sc.Input[IN_MAX_GAPS].Name = "Maximum Zones Kept";
        sc.Input[IN_MAX_GAPS].SetInt(60);
        sc.Input[IN_MAX_GAPS].SetIntLimits(1, 1000);

        sc.Input[IN_ALERT_NEW].Name = "Alert on New FVG";
        sc.Input[IN_ALERT_NEW].SetYesNo(0);

        sc.Input[IN_ALERT_INVERT].Name = "Alert on Inversion (IFVG)";
        sc.Input[IN_ALERT_INVERT].SetYesNo(0);

        sc.Input[IN_ALERT_SOUND].Name = "Alert Sound Number";
        sc.Input[IN_ALERT_SOUND].SetInt(1);
        sc.Input[IN_ALERT_SOUND].SetIntLimits(1, 500);

        sc.Input[IN_DRAW_OUTLINE].Name = "Draw Zone Outline";
        sc.Input[IN_DRAW_OUTLINE].SetYesNo(0);

        sc.Input[IN_ALERT_WICK_FULL].Name = "Alert: Full Wick Sweep (Body Held Outside)";
        sc.Input[IN_ALERT_WICK_FULL].SetYesNo(0);

        sc.Input[IN_ALERT_WICK_FULL_SOUND].Name = "   Full Wick Sweep - Alert Sound Number";
        sc.Input[IN_ALERT_WICK_FULL_SOUND].SetInt(2);
        sc.Input[IN_ALERT_WICK_FULL_SOUND].SetIntLimits(1, 500);

        sc.Input[IN_ALERT_WICK_HALF].Name = "Alert: 50% Wick Tap (Body Held Outside)";
        sc.Input[IN_ALERT_WICK_HALF].SetYesNo(0);

        sc.Input[IN_ALERT_WICK_HALF_SOUND].Name = "   50% Wick Tap - Alert Sound Number";
        sc.Input[IN_ALERT_WICK_HALF_SOUND].SetInt(3);
        sc.Input[IN_ALERT_WICK_HALF_SOUND].SetIntLimits(1, 500);

        return;
    }

    // -------------------------------------------------------------------------
    //  Persistent storage: the list of zones survives between calls
    // -------------------------------------------------------------------------
    if (sc.LastCallToFunction)
    {
        GapList* p_Dead = (GapList*)sc.GetPersistentPointer(1);
        if (p_Dead != NULL)
        {
            delete p_Dead;
            sc.SetPersistentPointer(1, NULL);
        }
        return;
    }

    GapList* p_Gaps = (GapList*)sc.GetPersistentPointer(1);
    if (p_Gaps == NULL)
    {
        p_Gaps = new GapList;
        sc.SetPersistentPointer(1, p_Gaps);
    }
    GapList& Gaps = *p_Gaps;

    int& r_NextLineNumber = sc.GetPersistentInt(1);
    int& r_LastProcessed  = sc.GetPersistentInt(2);

    // -------------------------------------------------------------------------
    //  Read inputs
    // -------------------------------------------------------------------------
    const bool  ShowBull      = sc.Input[IN_SHOW_BULL].GetYesNo() != 0;
    const bool  ShowBear      = sc.Input[IN_SHOW_BEAR].GetYesNo() != 0;
    const bool  ShowIFVG      = sc.Input[IN_SHOW_IFVG].GetYesNo() != 0;
    const float MinGapSize    = sc.Input[IN_MIN_TICKS].GetInt() * (float)sc.TickSize;
    const int   MinBodyPct    = sc.Input[IN_MIN_BODY_PCT].GetInt();
    const int   MitigRule     = sc.Input[IN_MITIGATION_RULE].GetIndex();  // 0/1/2
    const int   FilledStyle   = sc.Input[IN_FILLED_STYLE].GetIndex();
    const int   ExtendBars    = sc.Input[IN_EXTEND_BARS].GetInt();
    const int   MaxGaps       = sc.Input[IN_MAX_GAPS].GetInt();
    const bool  AlertNew      = sc.Input[IN_ALERT_NEW].GetYesNo() != 0;
    const bool  AlertInvert   = sc.Input[IN_ALERT_INVERT].GetYesNo() != 0;
    const int   AlertSound    = sc.Input[IN_ALERT_SOUND].GetInt();
    const bool  AlertWickFull = sc.Input[IN_ALERT_WICK_FULL].GetYesNo() != 0;
    const int   WickFullSound = sc.Input[IN_ALERT_WICK_FULL_SOUND].GetInt();
    const bool  AlertWickHalf = sc.Input[IN_ALERT_WICK_HALF].GetYesNo() != 0;
    const int   WickHalfSound = sc.Input[IN_ALERT_WICK_HALF_SOUND].GetInt();

    // -------------------------------------------------------------------------
    //  Full recalculation: wipe everything and start clean
    // -------------------------------------------------------------------------
    if (sc.UpdateStartIndex == 0)
    {
        Gaps.clear();
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_ALL, 0);
        r_NextLineNumber = 1;
        r_LastProcessed  = 1;
    }

    if (sc.ArraySize < 3)
        return;

    // Only closed bars are evaluated, so nothing ever repaints.
    const int LastClosedIndex = sc.ArraySize - 2;
    int       StartIndex      = r_LastProcessed + 1;
    if (StartIndex < 2)
        StartIndex = 2;

    // =========================================================================
    //  Bar-by-bar processing
    // =========================================================================
    for (int i = StartIndex; i <= LastClosedIndex; ++i)
    {
        const bool CanAlert = (sc.IsFullRecalculation == 0) && (i >= sc.ArraySize - 3);

        // ---------------------------------------------------------------------
        //  1. Update every zone that is still alive
        // ---------------------------------------------------------------------
        for (size_t n = 0; n < Gaps.size(); ++n)
        {
            s_Gap& G = Gaps[n];

            if (G.State == GAP_DEAD)
                continue;

            const float Mid = MidPrice(G);

            // A zone can invert whether or not it has already been tested.
            // Being touched (mitigated) almost always happens BEFORE price can
            // close through the far side, so mitigated zones must stay in the
            // running here or IFVGs would essentially never form.
            if (G.State == GAP_ACTIVE || G.State == GAP_MITIGATED)
            {
                // --- wick rejection alerts -----------------------------------
                // The wick reaches into the zone but the whole body (open AND
                // close) stays on the side price approached from, i.e. the zone
                // was probed and held. Each event fires once per zone.
                if (AlertWickFull || AlertWickHalf)
                {
                    const float BodyLow  = (sc.Open[i] < sc.Close[i]) ? sc.Open[i]  : sc.Close[i];
                    const float BodyHigh = (sc.Open[i] > sc.Close[i]) ? sc.Open[i]  : sc.Close[i];

                    const bool BodyHeldOutside = G.Bullish ? (BodyLow  >= G.Top)
                                                           : (BodyHigh <= G.Bottom);

                    if (BodyHeldOutside)
                    {
                        // Did the wick clear the far edge, or only reach 50%?
                        const bool WickSweptAll = G.Bullish ? (sc.Low[i]  <= G.Bottom)
                                                            : (sc.High[i] >= G.Top);
                        const bool WickHitMid   = G.Bullish ? (sc.Low[i]  <= Mid)
                                                            : (sc.High[i] >= Mid);

                        if (WickSweptAll && !G.WickFullFired)
                        {
                            G.WickFullFired = true;
                            G.WickHalfFired = true;   // a full sweep supersedes the 50% event

                            if (AlertWickFull && CanAlert)
                            {
                                SCString Msg;
                                Msg.Format("%s: %s FVG fully swept by wick, body held outside. Zone %s - %s",
                                           sc.Symbol.GetChars(),
                                           G.Bullish ? "bullish" : "bearish",
                                           sc.FormatGraphValue(G.Bottom, sc.GetValueFormat()).GetChars(),
                                           sc.FormatGraphValue(G.Top,    sc.GetValueFormat()).GetChars());
                                sc.SetAlert(WickFullSound, Msg);
                            }
                        }
                        else if (WickHitMid && !G.WickHalfFired && !G.WickFullFired)
                        {
                            G.WickHalfFired = true;

                            if (AlertWickHalf && CanAlert)
                            {
                                SCString Msg;
                                Msg.Format("%s: %s FVG wicked to 50%% and rejected, body held outside. Mid %s",
                                           sc.Symbol.GetChars(),
                                           G.Bullish ? "bullish" : "bearish",
                                           sc.FormatGraphValue(Mid, sc.GetValueFormat()).GetChars());
                                sc.SetAlert(WickHalfSound, Msg);
                            }
                        }
                    }
                }

                // --- has price CLOSED all the way through?  -> inversion -----
                const bool Inverted = G.Bullish ? (sc.Close[i] < G.Bottom)
                                                : (sc.Close[i] > G.Top);

                if (Inverted)
                {
                    G.State = ShowIFVG ? GAP_INVERTED : GAP_DEAD;
                    if (G.State == GAP_DEAD)
                        G.EndIndex = i;
                    else
                        G.InvertIndex = i;

                    if (AlertInvert && CanAlert && ShowIFVG)
                    {
                        SCString Msg;
                        Msg.Format("IFVG formed on %s: %s zone %s - %s",
                                   sc.Symbol.GetChars(),
                                   G.Bullish ? "resistance" : "support",
                                   sc.FormatGraphValue(G.Bottom, sc.GetValueFormat()).GetChars(),
                                   sc.FormatGraphValue(G.Top,    sc.GetValueFormat()).GetChars());
                        sc.SetAlert(AlertSound, Msg);
                    }
                    continue;
                }

                // --- otherwise, has it merely been tested? -------------------
                if (G.State != GAP_ACTIVE)
                    continue;

                bool Mitigated = false;
                if (G.Bullish)
                {
                    if      (MitigRule == 0) Mitigated = (sc.Low[i] <= G.Top);
                    else if (MitigRule == 1) Mitigated = (sc.Low[i] <= Mid);
                    else                     Mitigated = (sc.Low[i] <= G.Bottom);
                }
                else
                {
                    if      (MitigRule == 0) Mitigated = (sc.High[i] >= G.Bottom);
                    else if (MitigRule == 1) Mitigated = (sc.High[i] >= Mid);
                    else                     Mitigated = (sc.High[i] >= G.Top);
                }

                if (Mitigated)
                {
                    G.State    = GAP_MITIGATED;
                    G.EndIndex = i;
                }
            }
            else if (G.State == GAP_INVERTED)
            {
                // An IFVG dies when price closes back through it.
                const bool Reclaimed = G.Bullish ? (sc.Close[i] > G.Top)
                                                 : (sc.Close[i] < G.Bottom);
                if (Reclaimed)
                {
                    G.State    = GAP_DEAD;
                    G.EndIndex = i;
                }
            }
        }

        // ---------------------------------------------------------------------
        //  2. Look for a brand new gap formed by bars i-2, i-1, i
        // ---------------------------------------------------------------------
        bool  NewBull = (sc.Low[i]  > sc.High[i - 2]);
        bool  NewBear = (sc.High[i] < sc.Low[i - 2]);

        if (NewBull || NewBear)
        {
            const float Top    = NewBull ? sc.Low[i]      : sc.Low[i - 2];
            const float Bottom = NewBull ? sc.High[i - 2] : sc.High[i];

            bool Accept = ((Top - Bottom) >= MinGapSize);

            // Optional displacement filter on the middle bar.
            if (Accept && MinBodyPct > 0)
            {
                const float Range = sc.High[i - 1] - sc.Low[i - 1];
                const float Body  = fabsf(sc.Close[i - 1] - sc.Open[i - 1]);
                if (Range <= 0.0f || (Body / Range) * 100.0f < (float)MinBodyPct)
                    Accept = false;
            }

            if (Accept)
            {
                s_Gap G;
                G.StartIndex = i - 2;
                G.EndIndex   = i;
                G.Top        = Top;
                G.Bottom     = Bottom;
                G.Bullish     = NewBull;
                G.State       = GAP_ACTIVE;
                G.InvertIndex = -1;
                G.WickFullFired = false;
                G.WickHalfFired = false;
                G.BoxLine    = r_NextLineNumber++;
                G.MidLine    = r_NextLineNumber++;
                G.TextLine   = r_NextLineNumber++;
                G.DrawnEnd   = -1;
                G.DrawnState = -1;
                G.Visible    = false;

                Gaps.push_back(G);

                if (AlertNew && CanAlert)
                {
                    SCString Msg;
                    Msg.Format("New %s FVG on %s: %s - %s",
                               NewBull ? "bullish" : "bearish",
                               sc.Symbol.GetChars(),
                               sc.FormatGraphValue(Bottom, sc.GetValueFormat()).GetChars(),
                               sc.FormatGraphValue(Top,    sc.GetValueFormat()).GetChars());
                    sc.SetAlert(AlertSound, Msg);
                }
            }
        }

        r_LastProcessed = i;
    }

    // =========================================================================
    //  3. Trim the oldest zones so the chart and memory stay light
    // =========================================================================
    while ((int)Gaps.size() > MaxGaps)
    {
        EraseGap(sc, Gaps.front());
        Gaps.erase(Gaps.begin());
    }

    // =========================================================================
    //  4. Extend live zones to the hard right edge and redraw what changed
    // =========================================================================
    const int RightEdge = sc.ArraySize - 1 + ExtendBars;

    for (size_t n = 0; n < Gaps.size(); ++n)
    {
        s_Gap& G = Gaps[n];

        if (G.State == GAP_ACTIVE || G.State == GAP_INVERTED)
            G.EndIndex = RightEdge;

        // Is this zone supposed to be on screen right now?
        bool ShouldShow = true;

        if (G.State == GAP_INVERTED)
        {
            ShouldShow = ShowIFVG;
        }
        else
        {
            if (G.Bullish  && !ShowBull) ShouldShow = false;
            if (!G.Bullish && !ShowBear) ShouldShow = false;
            if (FilledStyle == FILLED_HIDE && (G.State == GAP_MITIGATED || G.State == GAP_DEAD))
                ShouldShow = false;
        }

        if (!ShouldShow)
        {
            EraseGap(sc, G);
            continue;
        }

        // Redraw only when the geometry or the state actually changed.
        if (G.EndIndex != G.DrawnEnd || G.State != G.DrawnState || !G.Visible)
            DrawGap(sc, G);
    }
}
