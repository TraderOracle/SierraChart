//============================================================================
// LittleRizzy.cpp
//
// Sierra Chart ACSIL study implementing the "Little Rizzy" pattern:
//
//   1. Trend identification : two consecutive confirmed swing pivots define a
//                             sloping trend line (lower highs = bearish,
//                             higher lows = bullish).
//   2. Measurement          : vertical distance from the pattern extreme
//                             (lowest low / highest high between the two
//                             anchors) up/down to the trend line at that bar.
//   3. Target projection    : that same distance projected beyond the extreme.
//   4. Bollinger context    : 2 SD bands used as an "in reality" gauge; can
//                             optionally gate setup creation via %B.
//   5. Exit / invalidation  : a bar CLOSING beyond the trend line kills the
//                             setup.
//
// Build: copy to  <SierraChart>\ACS_Source\  then
//        Analysis >> Build Custom Studies DLL >> Build
//
// Notes:
//  - Manual looping (sc.AutoLoop = 0). State transitions are evaluated only on
//    CLOSED bars, so nothing repaints intrabar and there is no look-ahead:
//    a pivot at bar P is not known until bar P + PivotStrength has closed.
//  - Bollinger Bands are recalculated on every bar including the forming one.
//============================================================================

#include "sierrachart.h"

SCDLLName("Little Rizzy")

//----------------------------------------------------------------------------
// Persistent variable keys
//----------------------------------------------------------------------------
namespace LR
{
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

    // Value of the trend line at an arbitrary bar index (extrapolates freely).
    inline float LineValueAt(float A1Value, int A1Index, float Slope, int BarIndex)
    {
        return A1Value + Slope * static_cast<float>(BarIndex - A1Index);
    }
}

//----------------------------------------------------------------------------
// Draw / refresh all chart drawings belonging to one setup.
// Each setup owns a block of 8 line numbers so historical setups persist.
//----------------------------------------------------------------------------
static void LR_DrawSetup(
    SCStudyInterfaceRef sc,
    int   SetupID,
    int   Dir,
    int   A1Index, float A1Value,
    int   A2Index, float A2Value,
    float Slope,
    int   ExtIndex, float ExtValue,
    float Distance, float Target,
    int   CurrentIndex,
    COLORREF TrendColor,
    COLORREF MeasureColor,
    COLORREF TargetColor,
    int   LineWidth,
    int   ShowLabels,
    int   DrawTrendLine,
    int   TrendTransparency)
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
        // Input was toggled off mid-session: remove any ray we drew earlier.
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, Base + 1);
    }

    const float LineAtExtreme = LR::LineValueAt(A1Value, A1Index, Slope, ExtIndex);

    // --- 2. Measurement leg: vertical, extreme -> trend line.
    Tool.Clear();
    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_LINE;
    Tool.LineNumber  = Base + 2;
    Tool.BeginIndex  = ExtIndex;
    Tool.EndIndex    = ExtIndex;
    Tool.BeginValue  = ExtValue;
    Tool.EndValue    = LineAtExtreme;
    Tool.Color       = MeasureColor;
    Tool.LineWidth   = LineWidth;
    Tool.LineStyle   = LINESTYLE_SOLID;
    Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);

    // --- 3. Projection leg: same distance carried beyond the extreme.
    Tool.Clear();
    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_LINE;
    Tool.LineNumber  = Base + 3;
    Tool.BeginIndex  = ExtIndex;
    Tool.EndIndex    = ExtIndex;
    Tool.BeginValue  = ExtValue;
    Tool.EndValue    = Target;
    Tool.Color       = TargetColor;
    Tool.LineWidth   = LineWidth;
    Tool.LineStyle   = LINESTYLE_DASH;
    Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);

    // --- 4. Target level, drawn forward to the current bar.
    int TargetEnd = CurrentIndex;
    if (TargetEnd <= ExtIndex)
        TargetEnd = ExtIndex + 1;

    Tool.Clear();
    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_LINE;
    Tool.LineNumber  = Base + 4;
    Tool.BeginIndex  = ExtIndex;
    Tool.EndIndex    = TargetEnd;
    Tool.BeginValue  = Target;
    Tool.EndValue    = Target;
    Tool.Color       = TargetColor;
    Tool.LineWidth   = LineWidth;
    Tool.LineStyle   = LINESTYLE_DASH;
    Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);

    if (ShowLabels == 0)
        return;

    // --- 5. Distance label next to the measurement leg.
    Tool.Clear();
    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_TEXT;
    Tool.LineNumber  = Base + 5;
    Tool.BeginIndex  = ExtIndex;
    Tool.BeginValue  = (ExtValue + LineAtExtreme) * 0.5f;
    Tool.Color       = MeasureColor;
    Tool.FontSize    = 9;
    Tool.FontBold    = 0;
    Tool.TextAlignment = DT_LEFT | DT_VCENTER;
    Tool.Text.Format("H %s", sc.FormatGraphValue(Distance, sc.GetValueFormat()).GetChars());
    Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);

    // --- 6. Target label.
    Tool.Clear();
    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_TEXT;
    Tool.LineNumber  = Base + 6;
    Tool.BeginIndex  = TargetEnd;
    Tool.BeginValue  = Target;
    Tool.Color       = TargetColor;
    Tool.FontSize    = 9;
    Tool.FontBold    = 1;
    Tool.TextAlignment = DT_LEFT | DT_VCENTER;
    Tool.Text.Format("%s TGT %s",
        Dir < 0 ? "Short" : "Long",
        sc.FormatGraphValue(Target, sc.GetValueFormat()).GetChars());
    Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
    Tool.AddAsUserDrawnDrawing = 0;
    sc.UseTool(Tool);
}

//============================================================================
SCSFExport scsf_LittleRizzy(SCStudyInterfaceRef sc)
{
    //------------------------------------------------------------------------
    // Subgraphs
    //------------------------------------------------------------------------
    SCSubgraphRef Sg_BBTop      = sc.Subgraph[0];
    SCSubgraphRef Sg_BBBottom   = sc.Subgraph[1];
    SCSubgraphRef Sg_BBMid      = sc.Subgraph[2];
    SCSubgraphRef Sg_SwingHigh  = sc.Subgraph[3];
    SCSubgraphRef Sg_SwingLow   = sc.Subgraph[4];
    SCSubgraphRef Sg_TrendLine  = sc.Subgraph[5];
    SCSubgraphRef Sg_Target     = sc.Subgraph[6];
    SCSubgraphRef Sg_TargetHit  = sc.Subgraph[7];
    SCSubgraphRef Sg_Invalid    = sc.Subgraph[8];
    SCSubgraphRef Sg_PercentB   = sc.Subgraph[9];   // hidden by default
    SCSubgraphRef Sg_State      = sc.Subgraph[10];  // hidden by default

    //------------------------------------------------------------------------
    // Inputs
    //------------------------------------------------------------------------
    SCInputRef In_PivotStrength = sc.Input[0];
    SCInputRef In_MinBars       = sc.Input[1];
    SCInputRef In_DoBearish     = sc.Input[2];
    SCInputRef In_DoBullish     = sc.Input[3];
    SCInputRef In_BBLength      = sc.Input[4];
    SCInputRef In_BBStdDev      = sc.Input[5];
    SCInputRef In_BBMAType      = sc.Input[6];
    SCInputRef In_BBInputData   = sc.Input[7];
    SCInputRef In_RequireBand   = sc.Input[8];
    SCInputRef In_BandThreshold = sc.Input[9];
    SCInputRef In_TrackExtreme  = sc.Input[10];
    SCInputRef In_Invalidate    = sc.Input[11];
    SCInputRef In_BufferTicks   = sc.Input[12];
    SCInputRef In_ReplaceSetup  = sc.Input[13];
    SCInputRef In_DrawPattern   = sc.Input[14];
    SCInputRef In_ShowLabels    = sc.Input[15];
    SCInputRef In_LineWidth     = sc.Input[16];
    SCInputRef In_BearColor     = sc.Input[17];
    SCInputRef In_BullColor     = sc.Input[18];
    SCInputRef In_MeasureColor  = sc.Input[19];
    SCInputRef In_TargetColor   = sc.Input[20];
    SCInputRef In_EnableAlerts  = sc.Input[21];
    SCInputRef In_DrawTrendLine = sc.Input[22];
    SCInputRef In_TrendOpacity  = sc.Input[23];

    //------------------------------------------------------------------------
    // Defaults
    //------------------------------------------------------------------------
    if (sc.SetDefaults)
    {
        sc.GraphName        = "Little Rizzy Pattern";
        sc.GraphRegion      = 0;
        sc.AutoLoop         = 0;      // manual looping: we run a state machine
        sc.ValueFormat      = VALUEFORMAT_INHERITED;
        sc.FreeDLL          = 0;
        sc.DrawZeros        = 0;
        sc.CalculationPrecedence = LOW_PREC_LEVEL;

        Sg_BBTop.Name = "BB Top";
        Sg_BBTop.DrawStyle = DRAWSTYLE_LINE;
        Sg_BBTop.PrimaryColor = RGB(128, 128, 160);
        Sg_BBTop.DrawZeros = false;

        Sg_BBBottom.Name = "BB Bottom";
        Sg_BBBottom.DrawStyle = DRAWSTYLE_LINE;
        Sg_BBBottom.PrimaryColor = RGB(128, 128, 160);
        Sg_BBBottom.DrawZeros = false;

        Sg_BBMid.Name = "BB Mid (Reality)";
        Sg_BBMid.DrawStyle = DRAWSTYLE_DASH;
        Sg_BBMid.PrimaryColor = RGB(90, 90, 120);
        Sg_BBMid.DrawZeros = false;

        Sg_SwingHigh.Name = "Swing High";
        Sg_SwingHigh.DrawStyle = DRAWSTYLE_POINT;
        Sg_SwingHigh.LineWidth = 4;
        Sg_SwingHigh.PrimaryColor = RGB(220, 80, 80);
        Sg_SwingHigh.DrawZeros = false;

        Sg_SwingLow.Name = "Swing Low";
        Sg_SwingLow.DrawStyle = DRAWSTYLE_POINT;
        Sg_SwingLow.LineWidth = 4;
        Sg_SwingLow.PrimaryColor = RGB(80, 190, 110);
        Sg_SwingLow.DrawZeros = false;

        Sg_TrendLine.Name = "Trend Line Value";
        Sg_TrendLine.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_TrendLine.PrimaryColor = RGB(255, 200, 0);
        Sg_TrendLine.DrawZeros = false;

        Sg_Target.Name = "Target";
        Sg_Target.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_Target.PrimaryColor = RGB(0, 200, 255);
        Sg_Target.DrawZeros = false;

        Sg_TargetHit.Name = "Target Hit";
        Sg_TargetHit.DrawStyle = DRAWSTYLE_POINT;
        Sg_TargetHit.LineWidth = 6;
        Sg_TargetHit.PrimaryColor = RGB(0, 220, 255);
        Sg_TargetHit.DrawZeros = false;

        Sg_Invalid.Name = "Invalidated";
        Sg_Invalid.DrawStyle = DRAWSTYLE_POINT;
        Sg_Invalid.LineWidth = 6;
        Sg_Invalid.PrimaryColor = RGB(255, 140, 0);
        Sg_Invalid.DrawZeros = false;

        Sg_PercentB.Name = "Percent B";
        Sg_PercentB.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_PercentB.PrimaryColor = RGB(160, 160, 160);
        Sg_PercentB.DrawZeros = false;

        Sg_State.Name = "Setup State (+1/-1)";
        Sg_State.DrawStyle = DRAWSTYLE_IGNORE;
        Sg_State.PrimaryColor = RGB(160, 160, 160);
        Sg_State.DrawZeros = false;

        In_PivotStrength.Name = "Pivot Strength (Bars Each Side)";
        In_PivotStrength.SetInt(3);
        In_PivotStrength.SetIntLimits(1, 100);

        In_MinBars.Name = "Minimum Bars Between Trend Line Anchors";
        In_MinBars.SetInt(3);
        In_MinBars.SetIntLimits(1, 500);

        In_DoBearish.Name = "Detect Bearish (Lower High) Setups";
        In_DoBearish.SetYesNo(1);

        In_DoBullish.Name = "Detect Bullish (Higher Low) Setups";
        In_DoBullish.SetYesNo(1);

        In_BBLength.Name = "Bollinger Bands Length";
        In_BBLength.SetInt(20);
        In_BBLength.SetIntLimits(2, 5000);

        In_BBStdDev.Name = "Bollinger Bands Standard Deviations";
        In_BBStdDev.SetFloat(2.0f);

        In_BBMAType.Name = "Bollinger Bands Moving Average Type";
        In_BBMAType.SetMovAvgType(MOVAVGTYPE_SIMPLE);

        In_BBInputData.Name = "Bollinger Bands Input Data";
        In_BBInputData.SetInputDataIndex(SC_LAST);

        In_RequireBand.Name = "Require Confirming Pivot Outside Band";
        In_RequireBand.SetYesNo(0);

        In_BandThreshold.Name = "Outside Band %B Threshold (0-1)";
        In_BandThreshold.SetFloat(0.90f);

        In_TrackExtreme.Name = "Re-measure If New Extreme Forms";
        In_TrackExtreme.SetYesNo(0);

        In_Invalidate.Name = "Invalidate On Close Beyond Trend Line";
        In_Invalidate.SetYesNo(1);

        In_BufferTicks.Name = "Invalidation Buffer (Ticks)";
        In_BufferTicks.SetInt(0);
        In_BufferTicks.SetIntLimits(0, 1000);

        In_ReplaceSetup.Name = "New Pattern Replaces Active Setup";
        In_ReplaceSetup.SetYesNo(1);

        In_DrawPattern.Name = "Draw Pattern On Chart";
        In_DrawPattern.SetYesNo(1);

        In_ShowLabels.Name = "Show Text Labels";
        In_ShowLabels.SetYesNo(1);

        In_LineWidth.Name = "Drawing Line Width";
        In_LineWidth.SetInt(2);
        In_LineWidth.SetIntLimits(1, 10);

        In_BearColor.Name = "Bearish Trend Line Color";
        In_BearColor.SetColor(220, 80, 80);

        In_BullColor.Name = "Bullish Trend Line Color";
        In_BullColor.SetColor(80, 190, 110);

        In_MeasureColor.Name = "Measurement Leg Color";
        In_MeasureColor.SetColor(255, 210, 0);

        In_TargetColor.Name = "Target Color";
        In_TargetColor.SetColor(0, 200, 255);

        In_EnableAlerts.Name = "Enable Alerts";
        In_EnableAlerts.SetYesNo(0);

        In_DrawTrendLine.Name = "Draw Trend Line";
        In_DrawTrendLine.SetYesNo(0);

        // 100 = fully opaque, 0 = invisible. Converted to Sierra Chart's
        // TransparencyLevel (which runs the other way) at draw time.
        In_TrendOpacity.Name = "Trend Line Opacity (%)";
        In_TrendOpacity.SetInt(100);
        In_TrendOpacity.SetIntLimits(0, 100);

        return;
    }

    //------------------------------------------------------------------------
    // Cached input values
    //------------------------------------------------------------------------
    const int   Strength      = In_PivotStrength.GetInt();
    const int   MinBars       = In_MinBars.GetInt();
    const int   DoBear        = In_DoBearish.GetYesNo();
    const int   DoBull        = In_DoBullish.GetYesNo();
    const int   BBLength      = In_BBLength.GetInt();
    const float BBStdDev      = In_BBStdDev.GetFloat();
    const int   BBMAType      = In_BBMAType.GetMovAvgType();
    const int   BBInputIndex  = In_BBInputData.GetInputDataIndex();
    const int   RequireBand   = In_RequireBand.GetYesNo();
    const float BandThreshold = In_BandThreshold.GetFloat();
    const int   TrackExtreme  = In_TrackExtreme.GetYesNo();
    const int   DoInvalidate  = In_Invalidate.GetYesNo();
    const float Buffer        = In_BufferTicks.GetInt() * sc.TickSize;
    const int   ReplaceSetup  = In_ReplaceSetup.GetYesNo();
    const int   DrawPattern   = In_DrawPattern.GetYesNo();
    const int   ShowLabels    = In_ShowLabels.GetYesNo();
    const int   LineWidth     = In_LineWidth.GetInt();
    const int   EnableAlerts  = In_EnableAlerts.GetYesNo();
    const int   DrawTrendLine = In_DrawTrendLine.GetYesNo();

    // Sierra Chart wants transparency (0 = solid), the input is opacity.
    int TrendTransparency = 100 - In_TrendOpacity.GetInt();
    if (TrendTransparency < 0)   TrendTransparency = 0;
    if (TrendTransparency > 100) TrendTransparency = 100;

    //------------------------------------------------------------------------
    // Persistent state
    //------------------------------------------------------------------------
    int&   r_LastEval     = sc.GetPersistentInt(LR::PI_LastEvalIndex);
    int&   r_Active       = sc.GetPersistentInt(LR::PI_SetupActive);
    int&   r_Dir          = sc.GetPersistentInt(LR::PI_SetupDir);
    int&   r_A1Index      = sc.GetPersistentInt(LR::PI_A1Index);
    int&   r_A2Index      = sc.GetPersistentInt(LR::PI_A2Index);
    int&   r_ExtIndex     = sc.GetPersistentInt(LR::PI_ExtIndex);
    int&   r_SetupID      = sc.GetPersistentInt(LR::PI_SetupID);
    int&   r_LastHighIdx  = sc.GetPersistentInt(LR::PI_LastHighIdx);
    int&   r_PrevHighIdx  = sc.GetPersistentInt(LR::PI_PrevHighIdx);
    int&   r_LastLowIdx   = sc.GetPersistentInt(LR::PI_LastLowIdx);
    int&   r_PrevLowIdx   = sc.GetPersistentInt(LR::PI_PrevLowIdx);

    float& r_A1Value      = sc.GetPersistentFloat(LR::PF_A1Value);
    float& r_A2Value      = sc.GetPersistentFloat(LR::PF_A2Value);
    float& r_ExtValue     = sc.GetPersistentFloat(LR::PF_ExtValue);
    float& r_Distance     = sc.GetPersistentFloat(LR::PF_Distance);
    float& r_Target       = sc.GetPersistentFloat(LR::PF_Target);
    float& r_Slope        = sc.GetPersistentFloat(LR::PF_Slope);
    float& r_LastHighVal  = sc.GetPersistentFloat(LR::PF_LastHighVal);
    float& r_PrevHighVal  = sc.GetPersistentFloat(LR::PF_PrevHighVal);
    float& r_LastLowVal   = sc.GetPersistentFloat(LR::PF_LastLowVal);
    float& r_PrevLowVal   = sc.GetPersistentFloat(LR::PF_PrevLowVal);

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
    // Pattern state machine - CLOSED BARS ONLY
    //------------------------------------------------------------------------
    const int MinRequired = 2 * Strength;
    int StartIndex = r_LastEval + 1;
    if (StartIndex < MinRequired)
        StartIndex = MinRequired;

    const COLORREF BearColor    = In_BearColor.GetColor();
    const COLORREF BullColor    = In_BullColor.GetColor();
    const COLORREF MeasureColor = In_MeasureColor.GetColor();
    const COLORREF TargetColor  = In_TargetColor.GetColor();

    for (int i = StartIndex; i < sc.ArraySize; ++i)
    {
        if (sc.GetBarHasClosedStatus(i) != BHCS_BAR_HAS_CLOSED)
            break;

        Sg_SwingHigh[i] = 0.0f;
        Sg_SwingLow[i]  = 0.0f;
        Sg_TargetHit[i] = 0.0f;
        Sg_Invalid[i]   = 0.0f;

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
                        sc.SetAlert(2, "Little Rizzy: setup invalidated (close beyond trend line)");
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
                        sc.SetAlert(3, "Little Rizzy: measured move target reached");
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

            // Commit the setup.
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
                sc.SetAlert(1, Msg);
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
                LR_DrawSetup(sc, r_SetupID, r_Dir,
                    r_A1Index, r_A1Value, r_A2Index, r_A2Value, r_Slope,
                    r_ExtIndex, r_ExtValue, r_Distance, r_Target, i,
                    (r_Dir < 0) ? BearColor : BullColor,
                    MeasureColor, TargetColor, LineWidth, ShowLabels,
                    DrawTrendLine, TrendTransparency);
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

    //------------------------------------------------------------------------
    // Carry values across the forming bar so plots/spreadsheets stay continuous
    //------------------------------------------------------------------------
    const int Last = sc.ArraySize - 1;
    if (Last > 0 && sc.GetBarHasClosedStatus(Last) != BHCS_BAR_HAS_CLOSED)
    {
        Sg_SwingHigh[Last] = 0.0f;
        Sg_SwingLow[Last]  = 0.0f;
        Sg_TargetHit[Last] = 0.0f;
        Sg_Invalid[Last]   = 0.0f;

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