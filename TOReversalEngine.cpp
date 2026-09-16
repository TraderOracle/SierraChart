// ============================================================================
//  TO Reversal Engine  -  Numbers Bars / Footprint order flow study
//  Sierra Chart ACSIL
//
//  1. ABSORPTION BUBBLES
//     Detects price levels inside a bar where aggressive order flow was
//     absorbed by passive liquidity, and draws a filled bubble at that level.
//       Green = bullish absorption (aggressive SELLING absorbed near the low)
//       Red   = bearish absorption (aggressive BUYING absorbed near the high)
//     Bubble size and opacity scale with the absorption score.
//
//  2. BAR COLORING
//     Price bars can be colored by any one of the order flow metrics
//     (net imbalance count by default), shaded from neutral toward green or
//     red as the value grows. The observed min / max of the selected metric
//     across the calculated history is written back into the input names in
//     Study Settings so thresholds can be set against real numbers.
//
//  3. REVERSAL MARKERS
//     Two standard subgraphs, "Reversal Up Marker" and "Reversal Down Marker",
//     plot below the low / above the high when the metrics line up as a
//     reversal setup. Draw style, color and size come from the Subgraphs tab,
//     so they can be switched to Square, Circle, Arrow Up/Down, Triangle, etc.
//     without recompiling. This is a confluence score over what already
//     happened in the bar, not a forecast - validate it on your own data.
//
//  Metrics computed per bar and exposed as subgraphs:
//     Bar Delta, Net Trapped Volume, Net Imbalance Count, Net Absorbed Volume,
//     Reversal Score.
//
//  Requirements:
//    - Chart must have Volume at Price data (Numbers Bars / footprint data).
//    - Add this study to the same chart region as the price bars (Region 0).
//
//  Build: Analysis >> Build Custom Studies DLL >> Build (put this file in
//         SierraChart\ACS_Source\)
// ============================================================================

#include "sierrachart.h"

SCDLLName("TO Reversal Engine")

// ---------------------------------------------------------------------------
// Internal constants
// ---------------------------------------------------------------------------
namespace
{
    const int MAX_CANDIDATES_PER_BAR = 64;   // scratch buffer size
    const int MAX_BUBBLE_SLOTS       = 6;    // hard cap of bubbles per bar
    const int LINES_PER_SLOT         = 2;    // 1 = ellipse, 2 = volume text
    const int BACKGROUND_LINE_OFFSET = MAX_BUBBLE_SLOTS * LINES_PER_SLOT;
    const int LINES_PER_BAR          = BACKGROUND_LINE_OFFSET + 1;

    struct s_Candidate
    {
        int   PriceInTicks;
        int   AggressiveVolume;   // volume that hit the passive side
        int   PassiveVolume;      // volume on the opposite side of the level
        int   TotalVolume;
        int   Direction;          // +1 = bullish absorption, -1 = bearish
        float Score;
    };

    // Descending sort by Score. Small N, so a simple selection sort is fine
    // and avoids pulling in <algorithm> ordering guarantees.
    void SortCandidatesDescending(s_Candidate* Candidates, int Count)
    {
        for (int i = 0; i < Count - 1; ++i)
        {
            int BestIndex = i;
            for (int j = i + 1; j < Count; ++j)
            {
                if (Candidates[j].Score > Candidates[BestIndex].Score)
                    BestIndex = j;
            }
            if (BestIndex != i)
            {
                s_Candidate Temp      = Candidates[i];
                Candidates[i]         = Candidates[BestIndex];
                Candidates[BestIndex] = Temp;
            }
        }
    }

    inline float Clampf(float Value, float Low, float High)
    {
        if (Value < Low)  return Low;
        if (Value > High) return High;
        return Value;
    }

    // 1234 -> "1.2k", 1450000 -> "1.45m". Used for the history ranges shown
    // in the input names.
    SCString FormatVolumeCompact(int Value, int UseCompact)
    {
        SCString Result;

        int AbsoluteValue = Value;
        if (AbsoluteValue < 0)
            AbsoluteValue = -AbsoluteValue;

        if (!UseCompact || AbsoluteValue < 1000)
            Result.Format("%d", Value);
        else if (AbsoluteValue < 1000000)
            Result.Format("%.1fk", Value / 1000.0);
        else
            Result.Format("%.2fm", Value / 1000000.0);

        return Result;
    }

    // Removes every drawing this study owns across a range of bars.
    // Deliberately built on TOOL_DELETE_CHARTDRAWING only, since the
    // "delete all" enum name differs between Sierra Chart versions.
    // Deleting a line number that holds no drawing is a harmless no-op.
    void DeleteDrawingsForBarRange(SCStudyInterfaceRef sc, int FirstBar, int LastBar)
    {
        if (FirstBar < 0)
            FirstBar = 0;

        for (int BarIndex = FirstBar; BarIndex <= LastBar; ++BarIndex)
        {
            const int BarLineBase = BarIndex * LINES_PER_BAR + 1;
            for (int Line = 0; Line < LINES_PER_BAR; ++Line)
                sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, BarLineBase + Line);
        }
    }

    // Linear blend between two colors. The COLORREF bytes are pulled out by
    // hand rather than through GetRValue/GetGValue/GetBValue so this stays
    // portable across Sierra Chart's build headers.
    COLORREF BlendColors(COLORREF FromColor, COLORREF ToColor, float Fraction)
    {
        if (Fraction < 0.0f) Fraction = 0.0f;
        if (Fraction > 1.0f) Fraction = 1.0f;

        const int FromR = (int)( FromColor        & 0xFF);
        const int FromG = (int)((FromColor >> 8)  & 0xFF);
        const int FromB = (int)((FromColor >> 16) & 0xFF);

        const int ToR = (int)( ToColor        & 0xFF);
        const int ToG = (int)((ToColor >> 8)  & 0xFF);
        const int ToB = (int)((ToColor >> 16) & 0xFF);

        const int R = FromR + (int)((ToR - FromR) * Fraction);
        const int G = FromG + (int)((ToG - FromG) * Fraction);
        const int B = FromB + (int)((ToB - FromB) * Fraction);

        return RGB(R, G, B);
    }
}

// ---------------------------------------------------------------------------
SCSFExport scsf_AbsorptionDetector(SCStudyInterfaceRef sc)
{
    // -- Subgraphs --
    SCSubgraphRef Subgraph_BullAbsorptionPrice = sc.Subgraph[0];
    SCSubgraphRef Subgraph_BearAbsorptionPrice = sc.Subgraph[1];
    SCSubgraphRef Subgraph_Score               = sc.Subgraph[2];
    SCSubgraphRef Subgraph_AbsorbedVolume      = sc.Subgraph[3];
    SCSubgraphRef Subgraph_BarDelta            = sc.Subgraph[4];
    SCSubgraphRef Subgraph_NetTrapped          = sc.Subgraph[5];
    SCSubgraphRef Subgraph_NetImbalanceCount   = sc.Subgraph[6];
    SCSubgraphRef Subgraph_NetAbsorption       = sc.Subgraph[7];
    SCSubgraphRef Subgraph_BarColor            = sc.Subgraph[8];
    SCSubgraphRef Subgraph_ReversalScore       = sc.Subgraph[9];
    SCSubgraphRef Subgraph_ReversalUp          = sc.Subgraph[10];
    SCSubgraphRef Subgraph_ReversalDown        = sc.Subgraph[11];

    // -- Absorption detection inputs --
    SCInputRef Input_EnableBullish        = sc.Input[0];
    SCInputRef Input_EnableBearish        = sc.Input[1];
    SCInputRef Input_MinAggressiveVolume  = sc.Input[2];
    SCInputRef Input_MinLevelVolPctOfBar  = sc.Input[3];
    SCInputRef Input_LevelVolMultiple     = sc.Input[4];
    SCInputRef Input_ImbalanceMode        = sc.Input[5];
    SCInputRef Input_MinImbalanceRatio    = sc.Input[6];
    SCInputRef Input_MaxTicksFromExtreme  = sc.Input[7];
    SCInputRef Input_MinRejectionTicks    = sc.Input[8];
    SCInputRef Input_RequireCloseThrough  = sc.Input[9];
    SCInputRef Input_ConfirmationBars     = sc.Input[10];
    SCInputRef Input_MaxContinuationTicks = sc.Input[11];
    SCInputRef Input_MaxBubblesPerBar     = sc.Input[12];
    SCInputRef Input_MinTickSeparation    = sc.Input[13];
    SCInputRef Input_ScoreForMinBubble    = sc.Input[14];
    SCInputRef Input_ScoreForMaxBubble    = sc.Input[15];
    SCInputRef Input_MinBubbleHeightTicks = sc.Input[16];
    SCInputRef Input_MaxBubbleHeightTicks = sc.Input[17];
    SCInputRef Input_BubbleWidthFraction  = sc.Input[18];
    SCInputRef Input_TransparencyWeak     = sc.Input[19];
    SCInputRef Input_TransparencyStrong   = sc.Input[20];
    SCInputRef Input_BullColor            = sc.Input[21];
    SCInputRef Input_BearColor            = sc.Input[22];
    SCInputRef Input_OutlineWidth         = sc.Input[23];
    SCInputRef Input_ShowVolumeText       = sc.Input[24];
    SCInputRef Input_TextFontSize         = sc.Input[25];
    SCInputRef Input_TextColor            = sc.Input[26];
    SCInputRef Input_DrawUnderPricebars   = sc.Input[27];
    SCInputRef Input_NumberOfBarsToCalc   = sc.Input[28];
    SCInputRef Input_EnableAlerts         = sc.Input[29];

    // -- Metric calculation inputs (indices kept stable; 30-40, 44, 45 and
    //    47-50 were the removed per-bar label settings) --
    SCInputRef Input_TrapBufferTicks      = sc.Input[41];
    SCInputRef Input_MinTrappedVolume     = sc.Input[42];
    SCInputRef Input_ImbalanceMinVolume   = sc.Input[43];
    SCInputRef Input_CompactNumbers       = sc.Input[46];

    // -- Bar coloring inputs --
    SCInputRef Input_ColorBars            = sc.Input[51];
    SCInputRef Input_ColorBarMetric       = sc.Input[52];
    SCInputRef Input_ColorBarFullScale    = sc.Input[53];
    SCInputRef Input_ColorBarDeadZone     = sc.Input[54];
    SCInputRef Input_ColorBarMode         = sc.Input[55];
    SCInputRef Input_BarColorPositive     = sc.Input[56];
    SCInputRef Input_BarColorNegative     = sc.Input[57];
    SCInputRef Input_BarColorNeutral      = sc.Input[58];

    // -- Reversal marker inputs --
    SCInputRef Input_EnableReversalMarker = sc.Input[59];
    SCInputRef Input_ReversalMinScore     = sc.Input[60];
    SCInputRef Input_ReversalLookback     = sc.Input[61];
    SCInputRef Input_RequireSwingExtreme  = sc.Input[62];
    SCInputRef Input_ExtremeToleranceTicks= sc.Input[63];
    SCInputRef Input_WeightAbsorption     = sc.Input[64];
    SCInputRef Input_WeightTrapped        = sc.Input[65];
    SCInputRef Input_WeightDivergence     = sc.Input[66];
    SCInputRef Input_WeightImbalance      = sc.Input[67];
    SCInputRef Input_UseVolumeWeighting   = sc.Input[68];
    SCInputRef Input_MarkerOffsetTicks    = sc.Input[71];
    SCInputRef Input_ReversalAlerts       = sc.Input[74];

    SCInputRef Input_ColorMethod          = sc.Input[75];
    SCInputRef Input_BackgroundTransp     = sc.Input[76];
    SCInputRef Input_BackgroundWidth      = sc.Input[77];
    SCInputRef Input_ReversalSensitivity  = sc.Input[78];

    // =======================================================================
    //  DEFAULTS
    // =======================================================================
    if (sc.SetDefaults)
    {
        sc.GraphName = "TO Reversal Engine";
        sc.StudyDescription =
            "Finds price levels within each bar where aggressive volume was "
            "absorbed by passive liquidity and price failed to continue. "
            "Colors bars by order flow metrics and marks reversal setups.";

        sc.GraphRegion            = 0;
        sc.AutoLoop               = 0;   // manual loop, we need forward lookups
        sc.MaintainVolumeAtPriceData = 1; // REQUIRED for footprint data
        sc.ValueFormat            = VALUEFORMAT_INHERITED;
        sc.DrawZeros              = 0;
        sc.CalculationPrecedence  = LOW_PREC_LEVEL;

        Subgraph_BullAbsorptionPrice.Name = "Bullish Absorption Price";
        Subgraph_BullAbsorptionPrice.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_BullAbsorptionPrice.PrimaryColor = RGB(0, 220, 0);

        Subgraph_BearAbsorptionPrice.Name = "Bearish Absorption Price";
        Subgraph_BearAbsorptionPrice.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_BearAbsorptionPrice.PrimaryColor = RGB(230, 0, 0);

        Subgraph_Score.Name = "Absorption Score";
        Subgraph_Score.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_AbsorbedVolume.Name = "Absorbed Volume";
        Subgraph_AbsorbedVolume.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_BarDelta.Name = "Bar Delta";
        Subgraph_BarDelta.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_NetTrapped.Name = "Net Trapped Volume (+ trapped shorts)";
        Subgraph_NetTrapped.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_NetImbalanceCount.Name = "Net Imbalance Count (+ buy)";
        Subgraph_NetImbalanceCount.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_NetAbsorption.Name = "Net Absorbed Volume (+ bullish)";
        Subgraph_NetAbsorption.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_ReversalScore.Name = "Reversal Score (-1 down, +1 up)";
        Subgraph_ReversalScore.DrawStyle = DRAWSTYLE_IGNORE;

        // Standard subgraph markers. Change Draw Style in the Subgraphs tab
        // to Square, Circle, Arrow Up / Down, Triangle, etc. as preferred -
        // no recompile needed. Size is the Line Width setting.
        Subgraph_ReversalUp.Name = "Reversal Up Marker";
        Subgraph_ReversalUp.DrawStyle = DRAWSTYLE_POINT;
        Subgraph_ReversalUp.PrimaryColor = RGB(255, 255, 255);
        Subgraph_ReversalUp.LineWidth = 6;
        Subgraph_ReversalUp.DrawZeros = 0;

        Subgraph_ReversalDown.Name = "Reversal Down Marker";
        Subgraph_ReversalDown.DrawStyle = DRAWSTYLE_POINT;
        Subgraph_ReversalDown.PrimaryColor = RGB(255, 255, 255);
        Subgraph_ReversalDown.LineWidth = 6;
        Subgraph_ReversalDown.DrawZeros = 0;

        Input_EnableBullish.Name = "Detect Bullish Absorption (sellers absorbed)";
        Input_EnableBullish.SetYesNo(1);

        Input_EnableBearish.Name = "Detect Bearish Absorption (buyers absorbed)";
        Input_EnableBearish.SetYesNo(1);

        Input_MinAggressiveVolume.Name = "Min Aggressive Volume At Level";
        Input_MinAggressiveVolume.SetInt(150);
        Input_MinAggressiveVolume.SetIntLimits(0, 10000000);

        Input_MinLevelVolPctOfBar.Name = "Min Level Volume As % Of Bar Volume";
        Input_MinLevelVolPctOfBar.SetFloat(8.0f);
        Input_MinLevelVolPctOfBar.SetFloatLimits(0.0f, 100.0f);

        Input_LevelVolMultiple.Name = "Min Level Volume vs Avg Level Volume (x)";
        Input_LevelVolMultiple.SetFloat(2.0f);
        Input_LevelVolMultiple.SetFloatLimits(0.0f, 100.0f);

        Input_ImbalanceMode.Name = "Imbalance Comparison";
        Input_ImbalanceMode.SetCustomInputStrings("Same Level (Ask vs Bid);Diagonal (footprint style)");
        Input_ImbalanceMode.SetCustomInputIndex(1);

        Input_MinImbalanceRatio.Name = "Min Imbalance Ratio";
        Input_MinImbalanceRatio.SetFloat(2.5f);
        Input_MinImbalanceRatio.SetFloatLimits(1.0f, 100.0f);

        Input_MaxTicksFromExtreme.Name = "Max Ticks From Bar High/Low";
        Input_MaxTicksFromExtreme.SetInt(3);
        Input_MaxTicksFromExtreme.SetIntLimits(0, 1000);

        Input_MinRejectionTicks.Name = "Min Rejection From Level At Close (Ticks)";
        Input_MinRejectionTicks.SetInt(2);
        Input_MinRejectionTicks.SetIntLimits(0, 1000);

        Input_RequireCloseThrough.Name = "Require Close On Absorbing Side Of Level";
        Input_RequireCloseThrough.SetYesNo(0);

        Input_ConfirmationBars.Name = "Confirmation Bars (0 = immediate)";
        Input_ConfirmationBars.SetInt(0);
        Input_ConfirmationBars.SetIntLimits(0, 50);

        Input_MaxContinuationTicks.Name = "Max Continuation Beyond Level (Ticks)";
        Input_MaxContinuationTicks.SetInt(2);
        Input_MaxContinuationTicks.SetIntLimits(0, 1000);

        Input_MaxBubblesPerBar.Name = "Max Bubbles Per Bar";
        Input_MaxBubblesPerBar.SetInt(2);
        Input_MaxBubblesPerBar.SetIntLimits(1, MAX_BUBBLE_SLOTS);

        Input_MinTickSeparation.Name = "Min Tick Separation Between Bubbles";
        Input_MinTickSeparation.SetInt(3);
        Input_MinTickSeparation.SetIntLimits(0, 1000);

        Input_ScoreForMinBubble.Name = "Score For Smallest Bubble";
        Input_ScoreForMinBubble.SetFloat(3.0f);
        Input_ScoreForMinBubble.SetFloatLimits(0.1f, 1000.0f);

        Input_ScoreForMaxBubble.Name = "Score For Largest Bubble";
        Input_ScoreForMaxBubble.SetFloat(15.0f);
        Input_ScoreForMaxBubble.SetFloatLimits(0.2f, 1000.0f);

        Input_MinBubbleHeightTicks.Name = "Smallest Bubble Height (Ticks)";
        Input_MinBubbleHeightTicks.SetFloat(2.0f);
        Input_MinBubbleHeightTicks.SetFloatLimits(0.5f, 500.0f);

        Input_MaxBubbleHeightTicks.Name = "Largest Bubble Height (Ticks)";
        Input_MaxBubbleHeightTicks.SetFloat(10.0f);
        Input_MaxBubbleHeightTicks.SetFloatLimits(0.5f, 500.0f);

        Input_BubbleWidthFraction.Name = "Largest Bubble Width (Fraction Of Bar)";
        Input_BubbleWidthFraction.SetFloat(0.9f);
        Input_BubbleWidthFraction.SetFloatLimits(0.05f, 5.0f);

        Input_TransparencyWeak.Name = "Transparency At Min Score (0-100)";
        Input_TransparencyWeak.SetInt(72);
        Input_TransparencyWeak.SetIntLimits(1, 99);

        Input_TransparencyStrong.Name = "Transparency At Max Score (0-100)";
        Input_TransparencyStrong.SetInt(18);
        Input_TransparencyStrong.SetIntLimits(1, 99);

        Input_BullColor.Name = "Bullish Absorption Color";
        Input_BullColor.SetColor(0, 225, 90);

        Input_BearColor.Name = "Bearish Absorption Color";
        Input_BearColor.SetColor(255, 45, 45);

        Input_OutlineWidth.Name = "Bubble Outline Width (0 = none)";
        Input_OutlineWidth.SetInt(1);
        Input_OutlineWidth.SetIntLimits(0, 10);

        Input_ShowVolumeText.Name = "Show Absorbed Volume Text In Bubble";
        Input_ShowVolumeText.SetYesNo(0);

        Input_TextFontSize.Name = "Bubble Text Font Size";
        Input_TextFontSize.SetInt(8);
        Input_TextFontSize.SetIntLimits(4, 40);

        Input_TextColor.Name = "Bubble Text Color";
        Input_TextColor.SetColor(255, 255, 255);

        Input_DrawUnderPricebars.Name = "Draw Bubbles Under Price Bars";
        Input_DrawUnderPricebars.SetYesNo(1);

        Input_NumberOfBarsToCalc.Name = "Number Of Bars To Calculate (0 = All)";
        Input_NumberOfBarsToCalc.SetInt(500);
        Input_NumberOfBarsToCalc.SetIntLimits(0, 1000000);

        Input_EnableAlerts.Name = "Enable Alert On New Absorption";
        Input_EnableAlerts.SetYesNo(0);

        // ---------------- Metric calculation ----------------
        Input_TrapBufferTicks.Name = "Trapped: Min Ticks Beyond Close";
        Input_TrapBufferTicks.SetInt(2);
        Input_TrapBufferTicks.SetIntLimits(0, 1000);

        Input_MinTrappedVolume.Name = "Trapped: Min Volume To Count";
        Input_MinTrappedVolume.SetInt(0);
        Input_MinTrappedVolume.SetIntLimits(0, 10000000);

        Input_ImbalanceMinVolume.Name = "Imbalance Count: Min Volume At Level";
        Input_ImbalanceMinVolume.SetInt(20);
        Input_ImbalanceMinVolume.SetIntLimits(0, 10000000);

        Input_CompactNumbers.Name = "Compact Number Format In Input Titles (k / m)";
        Input_CompactNumbers.SetYesNo(1);

        // ---------------- Bar coloring ----------------
        Subgraph_BarColor.Name = "Bar Color";
        Subgraph_BarColor.DrawStyle = DRAWSTYLE_COLOR_BAR;
        Subgraph_BarColor.PrimaryColor = RGB(128, 128, 128);

        Input_ColorBars.Name = "--- Color Price Bars By Metric ---";
        Input_ColorBars.SetYesNo(1);

        // These three names are rewritten at runtime to carry the observed
        // min / max of the selected metric across the calculated history.
        Input_ColorBarMetric.Name = "Color Bars By";
        Input_ColorBarMetric.SetCustomInputStrings(
            "Net Imbalance Count (I);Bar Delta (D);Absorbed Volume (A);Trapped Volume (T);Bar Volume (V)");
        Input_ColorBarMetric.SetCustomInputIndex(0);

        Input_ColorBarFullScale.Name = "Full Color At Value";
        Input_ColorBarFullScale.SetFloat(4.0f);
        Input_ColorBarFullScale.SetFloatLimits(0.01f, 100000000.0f);

        Input_ColorBarDeadZone.Name = "Leave Bar Uncolored Below Abs Value";
        Input_ColorBarDeadZone.SetFloat(1.0f);
        Input_ColorBarDeadZone.SetFloatLimits(0.0f, 100000000.0f);

        Input_ColorBarMode.Name = "Bar Color Mode";
        Input_ColorBarMode.SetCustomInputStrings("Gradient;Solid Once Past Dead Zone");
        Input_ColorBarMode.SetCustomInputIndex(0);

        Input_BarColorPositive.Name = "Bar Color: Positive Extreme";
        Input_BarColorPositive.SetColor(0, 235, 100);

        Input_BarColorNegative.Name = "Bar Color: Negative Extreme";
        Input_BarColorNegative.SetColor(255, 60, 60);

        Input_BarColorNeutral.Name = "Bar Color: Neutral / Gradient Start";
        Input_BarColorNeutral.SetColor(110, 110, 110);

        // Numbers Bars draws its own cells over the price bar, so the plain
        // price-bar coloring can be hidden. "Background Highlight" paints a
        // transparent rectangle over the bar's range instead, which always
        // shows. "Both" is the default so at least one of them lands.
        Input_ColorMethod.Name = "Bar Color Method";
        Input_ColorMethod.SetCustomInputStrings("Both;Price Bar Color Only;Background Highlight Only");
        Input_ColorMethod.SetCustomInputIndex(0);

        Input_BackgroundTransp.Name = "Background Highlight Transparency (0-100)";
        Input_BackgroundTransp.SetInt(78);
        Input_BackgroundTransp.SetIntLimits(1, 99);

        Input_BackgroundWidth.Name = "Background Highlight Width (Fraction Of Bar)";
        Input_BackgroundWidth.SetFloat(0.9f);
        Input_BackgroundWidth.SetFloatLimits(0.05f, 5.0f);

        // ---------------- Reversal markers ----------------
        Input_EnableReversalMarker.Name = "--- Show Reversal Markers ---";
        Input_EnableReversalMarker.SetYesNo(1);

        // Name is rewritten at runtime with the strongest score seen so far.
        Input_ReversalMinScore.Name = "Min Reversal Score To Mark (0-1)";
        Input_ReversalMinScore.SetFloat(0.30f);
        Input_ReversalMinScore.SetFloatLimits(0.01f, 1.0f);

        Input_ReversalLookback.Name = "Reversal Lookback (Bars)";
        Input_ReversalLookback.SetInt(10);
        Input_ReversalLookback.SetIntLimits(1, 500);

        Input_RequireSwingExtreme.Name = "Require Bar At Lookback Extreme";
        Input_RequireSwingExtreme.SetYesNo(1);

        Input_ExtremeToleranceTicks.Name = "Swing Extreme Tolerance (Ticks)";
        Input_ExtremeToleranceTicks.SetInt(1);
        Input_ExtremeToleranceTicks.SetIntLimits(0, 1000);

        Input_WeightAbsorption.Name = "Reversal Weight: Absorption";
        Input_WeightAbsorption.SetFloat(4.0f);
        Input_WeightAbsorption.SetFloatLimits(0.0f, 10.0f);

        Input_WeightTrapped.Name = "Reversal Weight: Trapped Traders";
        Input_WeightTrapped.SetFloat(1.0f);
        Input_WeightTrapped.SetFloatLimits(0.0f, 10.0f);

        Input_WeightDivergence.Name = "Reversal Weight: Delta Divergence";
        Input_WeightDivergence.SetFloat(1.0f);
        Input_WeightDivergence.SetFloatLimits(0.0f, 10.0f);

        Input_WeightImbalance.Name = "Reversal Weight: Failed Imbalances";
        Input_WeightImbalance.SetFloat(1.0f);
        Input_WeightImbalance.SetFloatLimits(0.0f, 10.0f);

        Input_UseVolumeWeighting.Name = "Scale Score By Relative Bar Volume";
        Input_UseVolumeWeighting.SetYesNo(1);

        Input_MarkerOffsetTicks.Name = "Reversal Marker Offset From Bar (Ticks)";
        Input_MarkerOffsetTicks.SetFloat(2.5f);
        Input_MarkerOffsetTicks.SetFloatLimits(0.0f, 500.0f);

        Input_ReversalAlerts.Name = "Enable Alert On Reversal Marker";
        Input_ReversalAlerts.SetYesNo(0);

        // The raw weighted components land in the 0.03 - 0.20 band on most
        // instruments, which is far below any sane threshold. This lifts them
        // into a usable 0 - 1 range before the threshold test.
        Input_ReversalSensitivity.Name = "Reversal Score Sensitivity (x)";
        Input_ReversalSensitivity.SetFloat(4.0f);
        Input_ReversalSensitivity.SetFloatLimits(0.1f, 50.0f);

        return;
    }

    // Persistent record of the bar range this study currently has drawings on,
    // so a full recalculation can clear them without a "delete all" call.
    int& LastAlertBarIndex  = sc.GetPersistentInt(1);
    int& DrawnFirstBarIndex = sc.GetPersistentInt(2);
    int& DrawnLastBarIndex  = sc.GetPersistentInt(3);

    // Observed range of the bar-coloring metric across calculated history.
    int&   LastColorMetricIndex = sc.GetPersistentInt(4);
    float& ObservedMetricMin    = sc.GetPersistentFloat(1);
    float& ObservedMetricMax    = sc.GetPersistentFloat(2);
    float& NameShownMin         = sc.GetPersistentFloat(3);
    float& NameShownMax         = sc.GetPersistentFloat(4);

    // Strongest absolute reversal score seen, also surfaced in an input name.
    float& ObservedReversalPeak = sc.GetPersistentFloat(5);
    float& NameShownReversal    = sc.GetPersistentFloat(6);

    // =======================================================================
    //  CLEANUP
    // =======================================================================
    if (sc.LastCallToFunction)
    {
        DeleteDrawingsForBarRange(sc, DrawnFirstBarIndex, DrawnLastBarIndex);
        return;
    }

    if (sc.VolumeAtPriceForBars == NULL)
    {
        sc.AddMessageToLog("TO Reversal Engine: no Volume at Price data available on this chart.", 1);
        return;
    }

    if (sc.TickSize <= 0.0f)
        return;

    const int ConfirmationBars    = Input_ConfirmationBars.GetInt();
    const int MaxBubblesPerBar    = min(Input_MaxBubblesPerBar.GetInt(), MAX_BUBBLE_SLOTS);
    const int MinTickSeparation   = Input_MinTickSeparation.GetInt();
    const float TickSize          = sc.TickSize;

    // -----------------------------------------------------------------------
    //  Determine the range of bars to evaluate
    // -----------------------------------------------------------------------
    int StartIndex = sc.UpdateStartIndex;

    if (sc.UpdateStartIndex == 0)
    {
        // Wipe whatever was drawn by the previous pass before rebuilding.
        DeleteDrawingsForBarRange(sc, DrawnFirstBarIndex, DrawnLastBarIndex);
        LastAlertBarIndex = -1;

        const int BarsToCalc = Input_NumberOfBarsToCalc.GetInt();
        if (BarsToCalc > 0 && sc.ArraySize > BarsToCalc)
            StartIndex = sc.ArraySize - BarsToCalc;

        DrawnFirstBarIndex = StartIndex;
        ObservedReversalPeak = 0.0f;
        NameShownReversal    = -1.0f;
    }

    DrawnLastBarIndex = sc.ArraySize - 1;

    // -----------------------------------------------------------------------
    //  Bar coloring setup
    // -----------------------------------------------------------------------
    const int ColorMetricIndex = Input_ColorBarMetric.GetIndex();
    const bool MetricIsOneSided = (ColorMetricIndex == 4);   // Bar Volume

    if (sc.UpdateStartIndex == 0 || LastColorMetricIndex != ColorMetricIndex)
    {
        LastColorMetricIndex = ColorMetricIndex;
        ObservedMetricMin =  1.0e30f;
        ObservedMetricMax = -1.0e30f;
        NameShownMin =  1.0e30f;
        NameShownMax = -1.0e30f;
    }

    // Bars near the right edge must be re-evaluated when confirmation is used,
    // because their status depends on bars that form later.
    StartIndex -= (ConfirmationBars + 1);
    if (StartIndex < 0)
        StartIndex = 0;

    // -----------------------------------------------------------------------
    //  Average bar duration -> used for drawing widths.
    //  Works for time bars and for tick/volume/range bars alike.
    // -----------------------------------------------------------------------
    double AverageBarSeconds = sc.SecondsPerBar > 0 ? (double)sc.SecondsPerBar : 0.0;

    if (AverageBarSeconds <= 0.0 && sc.ArraySize > 2)
    {
        const int SampleStart = max(1, sc.ArraySize - 500);
        double Sum = 0.0;
        int Count = 0;
        for (int i = SampleStart; i < sc.ArraySize; ++i)
        {
            const double Delta =
                (sc.BaseDateTimeIn[i] - sc.BaseDateTimeIn[i - 1]).GetAsDouble() * SECONDS_PER_DAY;
            if (Delta > 0.0 && Delta < 3600.0 * 6.0)   // ignore session gaps
            {
                Sum += Delta;
                ++Count;
            }
        }
        if (Count > 0)
            AverageBarSeconds = Sum / Count;
    }

    if (AverageBarSeconds <= 0.0)
        AverageBarSeconds = 60.0;

    // =======================================================================
    //  MAIN LOOP
    // =======================================================================
    for (int BarIndex = StartIndex; BarIndex < sc.ArraySize; ++BarIndex)
    {
        Subgraph_BullAbsorptionPrice[BarIndex] = 0.0f;
        Subgraph_BearAbsorptionPrice[BarIndex] = 0.0f;
        Subgraph_Score[BarIndex]               = 0.0f;
        Subgraph_AbsorbedVolume[BarIndex]      = 0.0f;
        Subgraph_BarDelta[BarIndex]            = 0.0f;
        Subgraph_NetTrapped[BarIndex]          = 0.0f;
        Subgraph_NetImbalanceCount[BarIndex]   = 0.0f;
        Subgraph_NetAbsorption[BarIndex]       = 0.0f;
        Subgraph_BarColor[BarIndex]            = 0.0f;
        Subgraph_BarColor.DataColor[BarIndex]  = 0;
        Subgraph_ReversalScore[BarIndex]       = 0.0f;
        Subgraph_ReversalUp[BarIndex]          = 0.0f;
        Subgraph_ReversalDown[BarIndex]        = 0.0f;

        // Base line number for every drawing this study owns on this bar.
        const int BarLineBase = BarIndex * LINES_PER_BAR + 1;

        const int VAPSize = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(BarIndex);

        if (VAPSize <= 0)
        {
            DeleteDrawingsForBarRange(sc, BarIndex, BarIndex);
            continue;
        }

        // Bubbles need forward bars to confirm.
        const bool HasForwardData =
            (ConfirmationBars == 0) || (BarIndex + ConfirmationBars <= sc.ArraySize - 1);

        // ---- Bar level statistics -----------------------------------------
        const int BarHighTicks  = (int)(sc.Round(sc.High[BarIndex]  / TickSize));
        const int BarLowTicks   = (int)(sc.Round(sc.Low[BarIndex]   / TickSize));
        const int BarCloseTicks = (int)(sc.Round(sc.Close[BarIndex] / TickSize));

        const bool UseDiagonal = (Input_ImbalanceMode.GetIndex() == 1);
        const float MinImbalance = Input_MinImbalanceRatio.GetFloat();
        const int MinAggVol = Input_MinAggressiveVolume.GetInt();
        const int MaxTicksFromExtreme = Input_MaxTicksFromExtreme.GetInt();
        const int MinRejectionTicks = Input_MinRejectionTicks.GetInt();
        const int MaxContinuationTicks = Input_MaxContinuationTicks.GetInt();
        const int TrapBufferTicks = Input_TrapBufferTicks.GetInt();
        const int ImbalanceMinVolume = Input_ImbalanceMinVolume.GetInt();

        // ---- Single pass over the footprint collecting every bar metric ----
        double BarVolume         = 0.0;
        int TotalAskVolume       = 0;   // aggressive buying
        int TotalBidVolume       = 0;   // aggressive selling
        int TrappedLongVolume    = 0;   // bought above where the bar ended up
        int TrappedShortVolume   = 0;   // sold below where the bar ended up
        int BuyImbalanceCount    = 0;
        int SellImbalanceCount   = 0;

        for (int VAPIndex = 0; VAPIndex < VAPSize; ++VAPIndex)
        {
            const s_VolumeAtPriceV2* p_VAP = NULL;
            if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(BarIndex, VAPIndex, &p_VAP) || p_VAP == NULL)
                continue;

            const int PriceTicks = p_VAP->PriceInTicks;
            const int AskVolume  = p_VAP->AskVolume;
            const int BidVolume  = p_VAP->BidVolume;

            BarVolume      += p_VAP->Volume;
            TotalAskVolume += AskVolume;
            TotalBidVolume += BidVolume;

            // Trapped traders: aggressive fills that ended up on the wrong
            // side of the close by at least the buffer distance.
            if (PriceTicks >= BarCloseTicks + TrapBufferTicks)
                TrappedLongVolume += AskVolume;

            if (PriceTicks <= BarCloseTicks - TrapBufferTicks)
                TrappedShortVolume += BidVolume;

            // Imbalance tally, using the same comparison mode as detection.
            if (AskVolume >= ImbalanceMinVolume && AskVolume > 0)
            {
                int Opposing = BidVolume;
                if (UseDiagonal)
                    Opposing = sc.VolumeAtPriceForBars->GetVAPElementAtPrice((unsigned int)BarIndex, PriceTicks - 1).BidVolume;

                if ((float)AskVolume / (float)max(1, Opposing) >= MinImbalance)
                    ++BuyImbalanceCount;
            }

            if (BidVolume >= ImbalanceMinVolume && BidVolume > 0)
            {
                int Opposing = AskVolume;
                if (UseDiagonal)
                    Opposing = sc.VolumeAtPriceForBars->GetVAPElementAtPrice((unsigned int)BarIndex, PriceTicks + 1).AskVolume;

                if ((float)BidVolume / (float)max(1, Opposing) >= MinImbalance)
                    ++SellImbalanceCount;
            }
        }

        if (BarVolume <= 0.0)
        {
            DeleteDrawingsForBarRange(sc, BarIndex, BarIndex);
            continue;
        }

        const int BarDelta = TotalAskVolume - TotalBidVolume;

        // Positive = trapped shorts (bullish), negative = trapped longs.
        int NetTrappedVolume = TrappedShortVolume - TrappedLongVolume;
        if (abs(NetTrappedVolume) < Input_MinTrappedVolume.GetInt())
            NetTrappedVolume = 0;

        const int NetImbalanceCount = BuyImbalanceCount - SellImbalanceCount;

        Subgraph_BarDelta[BarIndex]          = (float)BarDelta;
        Subgraph_NetTrapped[BarIndex]        = (float)NetTrappedVolume;
        Subgraph_NetImbalanceCount[BarIndex] = (float)NetImbalanceCount;

        const double AvgLevelVolume = BarVolume / (double)VAPSize;
        const double MinLevelVolFromPct =
            BarVolume * (Input_MinLevelVolPctOfBar.GetFloat() / 100.0);
        const double MinLevelVolFromMultiple =
            AvgLevelVolume * Input_LevelVolMultiple.GetFloat();

        // ---- Collect candidate levels --------------------------------------
        // Skipped entirely while a bar still lacks its confirmation window.
        s_Candidate Candidates[MAX_CANDIDATES_PER_BAR];
        int CandidateCount = 0;

        for (int VAPIndex = 0; HasForwardData && VAPIndex < VAPSize && CandidateCount < MAX_CANDIDATES_PER_BAR; ++VAPIndex)
        {
            const s_VolumeAtPriceV2* p_VAP = NULL;
            if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(BarIndex, VAPIndex, &p_VAP) || p_VAP == NULL)
                continue;

            const int PriceTicks = p_VAP->PriceInTicks;
            const int LevelVolume = p_VAP->Volume;
            const int AskVolume   = p_VAP->AskVolume;   // aggressive buying
            const int BidVolume   = p_VAP->BidVolume;   // aggressive selling

            if (LevelVolume <= 0)
                continue;

            // Level must be heavy relative to the rest of the bar.
            if (LevelVolume < MinLevelVolFromPct || LevelVolume < MinLevelVolFromMultiple)
                continue;

            // ---------------------------------------------------------------
            //  BEARISH ABSORPTION: aggressive buying (Ask volume) piled into
            //  a level near the high, and price did not go anywhere.
            // ---------------------------------------------------------------
            if (Input_EnableBearish.GetYesNo() && AskVolume >= MinAggVol)
            {
                bool Qualifies = true;

                // Location: at or just under the bar high
                if (BarHighTicks - PriceTicks > MaxTicksFromExtreme || PriceTicks > BarHighTicks)
                    Qualifies = false;

                // Imbalance
                int PassiveSideVolume = BidVolume;
                if (UseDiagonal)
                {
                    // Footprint convention: Ask at price P is compared with the
                    // Bid one tick below, since those two rest side by side on
                    // the book when the spread is one tick.
                    const s_VolumeAtPriceV2& DiagonalVAP =
                        sc.VolumeAtPriceForBars->GetVAPElementAtPrice((unsigned int)BarIndex, PriceTicks - 1);
                    PassiveSideVolume = DiagonalVAP.BidVolume;
                }

                const float Imbalance = (float)AskVolume / (float)max(1, PassiveSideVolume);
                if (Imbalance < MinImbalance)
                    Qualifies = false;

                // Failure: price rejected away from the level by the close
                const int RejectionTicks = PriceTicks - BarCloseTicks;
                if (RejectionTicks < MinRejectionTicks)
                    Qualifies = false;

                if (Input_RequireCloseThrough.GetYesNo() && BarCloseTicks >= PriceTicks)
                    Qualifies = false;

                // Optional forward confirmation: no meaningful continuation up
                if (Qualifies && ConfirmationBars > 0)
                {
                    const int LimitTicks = PriceTicks + MaxContinuationTicks;
                    for (int i = BarIndex + 1; i <= BarIndex + ConfirmationBars && i < sc.ArraySize; ++i)
                    {
                        if ((int)(sc.Round(sc.High[i] / TickSize)) > LimitTicks)
                        {
                            Qualifies = false;
                            break;
                        }
                    }
                }

                if (Qualifies)
                {
                    const float RelVol    = (float)(AskVolume / max(1.0, AvgLevelVolume));
                    const float ImbFactor = sqrtf(Clampf(Imbalance, 1.0f, 12.0f));
                    const float RejFactor = 1.0f + Clampf((float)RejectionTicks / 8.0f, 0.0f, 1.0f);

                    s_Candidate& C      = Candidates[CandidateCount++];
                    C.PriceInTicks      = PriceTicks;
                    C.AggressiveVolume  = AskVolume;
                    C.PassiveVolume     = PassiveSideVolume;
                    C.TotalVolume       = LevelVolume;
                    C.Direction         = -1;
                    C.Score             = RelVol * ImbFactor * RejFactor;
                }
            }

            // ---------------------------------------------------------------
            //  BULLISH ABSORPTION: aggressive selling (Bid volume) piled into
            //  a level near the low, and price did not break down.
            // ---------------------------------------------------------------
            if (Input_EnableBullish.GetYesNo() && BidVolume >= MinAggVol && CandidateCount < MAX_CANDIDATES_PER_BAR)
            {
                bool Qualifies = true;

                if (PriceTicks - BarLowTicks > MaxTicksFromExtreme || PriceTicks < BarLowTicks)
                    Qualifies = false;

                int PassiveSideVolume = AskVolume;
                if (UseDiagonal)
                {
                    // Bid at price P is compared with the Ask one tick above.
                    const s_VolumeAtPriceV2& DiagonalVAP =
                        sc.VolumeAtPriceForBars->GetVAPElementAtPrice((unsigned int)BarIndex, PriceTicks + 1);
                    PassiveSideVolume = DiagonalVAP.AskVolume;
                }

                const float Imbalance = (float)BidVolume / (float)max(1, PassiveSideVolume);
                if (Imbalance < MinImbalance)
                    Qualifies = false;

                const int RejectionTicks = BarCloseTicks - PriceTicks;
                if (RejectionTicks < MinRejectionTicks)
                    Qualifies = false;

                if (Input_RequireCloseThrough.GetYesNo() && BarCloseTicks <= PriceTicks)
                    Qualifies = false;

                if (Qualifies && ConfirmationBars > 0)
                {
                    const int LimitTicks = PriceTicks - MaxContinuationTicks;
                    for (int i = BarIndex + 1; i <= BarIndex + ConfirmationBars && i < sc.ArraySize; ++i)
                    {
                        if ((int)(sc.Round(sc.Low[i] / TickSize)) < LimitTicks)
                        {
                            Qualifies = false;
                            break;
                        }
                    }
                }

                if (Qualifies)
                {
                    const float RelVol    = (float)(BidVolume / max(1.0, AvgLevelVolume));
                    const float ImbFactor = sqrtf(Clampf(Imbalance, 1.0f, 12.0f));
                    const float RejFactor = 1.0f + Clampf((float)RejectionTicks / 8.0f, 0.0f, 1.0f);

                    s_Candidate& C      = Candidates[CandidateCount++];
                    C.PriceInTicks      = PriceTicks;
                    C.AggressiveVolume  = BidVolume;
                    C.PassiveVolume     = PassiveSideVolume;
                    C.TotalVolume       = LevelVolume;
                    C.Direction         = 1;
                    C.Score             = RelVol * ImbFactor * RejFactor;
                }
            }
        }

        // ---- Rank, thin out clustered levels, keep the best ones ----------
        SortCandidatesDescending(Candidates, CandidateCount);

        s_Candidate Selected[MAX_BUBBLE_SLOTS];
        int SelectedCount = 0;

        for (int i = 0; i < CandidateCount && SelectedCount < MaxBubblesPerBar; ++i)
        {
            bool TooClose = false;
            for (int j = 0; j < SelectedCount; ++j)
            {
                if (abs(Candidates[i].PriceInTicks - Selected[j].PriceInTicks) < MinTickSeparation
                    && Candidates[i].Direction == Selected[j].Direction)
                {
                    TooClose = true;
                    break;
                }
            }
            if (!TooClose)
                Selected[SelectedCount++] = Candidates[i];
        }

        // ---- Draw bubbles ---------------------------------------------------
        const float ScoreMin = Input_ScoreForMinBubble.GetFloat();
        const float ScoreMax = max(Input_ScoreForMaxBubble.GetFloat(), ScoreMin + 0.01f);
        const float HeightMin = Input_MinBubbleHeightTicks.GetFloat();
        const float HeightMax = max(Input_MaxBubbleHeightTicks.GetFloat(), HeightMin);
        const float MaxWidthFraction = Input_BubbleWidthFraction.GetFloat();
        const int TranspWeak = Input_TransparencyWeak.GetInt();
        const int TranspStrong = Input_TransparencyStrong.GetInt();

        for (int Slot = 0; Slot < MAX_BUBBLE_SLOTS; ++Slot)
        {
            const int EllipseLineNumber = BarLineBase + Slot * LINES_PER_SLOT;
            const int TextLineNumber    = EllipseLineNumber + 1;

            if (Slot >= SelectedCount)
            {
                sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, EllipseLineNumber);
                sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, TextLineNumber);
                continue;
            }

            const s_Candidate& C = Selected[Slot];

            // Normalized strength 0..1
            const float Normalized = Clampf((C.Score - ScoreMin) / (ScoreMax - ScoreMin), 0.0f, 1.0f);

            const float BubbleHeightTicks = HeightMin + Normalized * (HeightMax - HeightMin);
            const float HalfHeight = (BubbleHeightTicks * TickSize) * 0.5f;

            // Keep the bubble roughly circular: width scales with height.
            const float WidthFraction =
                MaxWidthFraction * Clampf(BubbleHeightTicks / HeightMax, 0.25f, 1.0f);
            int HalfWidthSeconds = (int)(AverageBarSeconds * WidthFraction * 0.5);
            if (HalfWidthSeconds < 1)
                HalfWidthSeconds = 1;

            const int Transparency =
                (int)(TranspWeak + Normalized * (float)(TranspStrong - TranspWeak));

            const COLORREF BubbleColor =
                (C.Direction > 0) ? Input_BullColor.GetColor() : Input_BearColor.GetColor();

            const float CenterPrice = C.PriceInTicks * TickSize;

            s_UseTool Tool;
            Tool.Clear();
            Tool.ChartNumber        = sc.ChartNumber;
            Tool.Region             = sc.GraphRegion;
            Tool.DrawingType        = DRAWING_ELLIPSEHIGHLIGHT;
            Tool.LineNumber         = EllipseLineNumber;
            Tool.AddMethod          = UTAM_ADD_OR_ADJUST;
            Tool.AddAsUserDrawnDrawing = 0;
            Tool.BeginDateTime      = sc.BaseDateTimeIn[BarIndex] - SCDateTime::SECONDS(HalfWidthSeconds);
            Tool.EndDateTime        = sc.BaseDateTimeIn[BarIndex] + SCDateTime::SECONDS(HalfWidthSeconds);
            Tool.BeginValue         = CenterPrice - HalfHeight;
            Tool.EndValue           = CenterPrice + HalfHeight;
            Tool.Color              = BubbleColor;           // outline
            Tool.SecondaryColor     = BubbleColor;           // fill
            Tool.TransparencyLevel  = Clampf((float)Transparency, 1.0f, 99.0f);
            Tool.LineWidth          = max(1, Input_OutlineWidth.GetInt());
            Tool.DrawUnderneathMainGraph = Input_DrawUnderPricebars.GetYesNo();
            sc.UseTool(Tool);

            if (Input_ShowVolumeText.GetYesNo())
            {
                SCString VolumeText;
                VolumeText.Format("%d", C.AggressiveVolume);

                s_UseTool TextTool;
                TextTool.Clear();
                TextTool.ChartNumber   = sc.ChartNumber;
                TextTool.Region        = sc.GraphRegion;
                TextTool.DrawingType   = DRAWING_TEXT;
                TextTool.LineNumber    = TextLineNumber;
                TextTool.AddMethod     = UTAM_ADD_OR_ADJUST;
                TextTool.AddAsUserDrawnDrawing = 0;
                TextTool.BeginDateTime = sc.BaseDateTimeIn[BarIndex];
                TextTool.BeginValue    = CenterPrice;
                TextTool.Color         = Input_TextColor.GetColor();
                TextTool.FontSize      = Input_TextFontSize.GetInt();
                TextTool.FontBold      = 1;
                TextTool.TextAlignment = DT_CENTER | DT_VCENTER;
                TextTool.Text          = VolumeText;
                sc.UseTool(TextTool);
            }
            else
            {
                sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, TextLineNumber);
            }

            // ---- Subgraph output (strongest signal of each direction) ------
            if (C.Direction > 0)
            {
                if (Subgraph_BullAbsorptionPrice[BarIndex] == 0.0f)
                    Subgraph_BullAbsorptionPrice[BarIndex] = CenterPrice;
            }
            else
            {
                if (Subgraph_BearAbsorptionPrice[BarIndex] == 0.0f)
                    Subgraph_BearAbsorptionPrice[BarIndex] = CenterPrice;
            }

            if (C.Score > Subgraph_Score[BarIndex])
            {
                Subgraph_Score[BarIndex]          = C.Score;
                Subgraph_AbsorbedVolume[BarIndex] = (float)C.AggressiveVolume;
            }
        }

        // Net absorbed volume: bullish absorption counts positive.
        int NetAbsorbedVolume = 0;
        for (int i = 0; i < SelectedCount; ++i)
            NetAbsorbedVolume += Selected[i].Direction * Selected[i].AggressiveVolume;

        Subgraph_NetAbsorption[BarIndex] = (float)NetAbsorbedVolume;

        // ===================================================================
        //  REVERSAL CONFLUENCE SCORE
        //
        //  Every component is an "effort vs result" reading: heavy one-sided
        //  activity that did NOT produce movement in its own direction. The
        //  score runs -1 (turn down expected) to +1 (turn up expected).
        // ===================================================================
        float ReversalScore = 0.0f;

        const float BarRange = sc.High[BarIndex] - sc.Low[BarIndex];
        const float ClosePosition = (BarRange > 0.0f)
            ? (sc.Close[BarIndex] - sc.Low[BarIndex]) / BarRange
            : 0.5f;

        // -1 = closed on the low, +1 = closed on the high.
        const float CloseFactor = (2.0f * ClosePosition) - 1.0f;

        {
            const float WeightAbsorption = Input_WeightAbsorption.GetFloat();
            const float WeightTrapped    = Input_WeightTrapped.GetFloat();
            const float WeightDivergence = Input_WeightDivergence.GetFloat();
            const float WeightImbalance  = Input_WeightImbalance.GetFloat();

            const float TotalWeight =
                WeightAbsorption + WeightTrapped + WeightDivergence + WeightImbalance;

            if (TotalWeight > 0.0f)
            {
                // 1. Absorption already carries its own direction.
                const float AbsorptionComponent =
                    Clampf((float)NetAbsorbedVolume / (float)BarVolume, -1.0f, 1.0f);

                // 2. Trapped traders: positive when shorts are offside.
                const float TrappedComponent =
                    Clampf((float)NetTrappedVolume / (float)BarVolume, -1.0f, 1.0f);

                // 3. Delta divergence: aggression that did not move price.
                const float DeltaRatio = Clampf((float)BarDelta / (float)BarVolume, -1.0f, 1.0f);
                float DeltaSign = 0.0f;
                if (DeltaRatio > 0.0f) DeltaSign =  1.0f;
                if (DeltaRatio < 0.0f) DeltaSign = -1.0f;

                const float DivergenceStrength = Clampf(-DeltaSign * CloseFactor, 0.0f, 1.0f);
                const float DivergenceComponent = -DeltaRatio * DivergenceStrength;

                // 4. Failed imbalances: same idea, on the imbalance tally.
                const float ImbalanceDenominator = (float)max(1, VAPSize);
                const float ImbalanceRatio =
                    Clampf((float)NetImbalanceCount / ImbalanceDenominator, -1.0f, 1.0f);
                float ImbalanceSign = 0.0f;
                if (ImbalanceRatio > 0.0f) ImbalanceSign =  1.0f;
                if (ImbalanceRatio < 0.0f) ImbalanceSign = -1.0f;

                const float ImbalanceStrength = Clampf(-ImbalanceSign * CloseFactor, 0.0f, 1.0f);
                const float ImbalanceComponent = -ImbalanceRatio * ImbalanceStrength;

                ReversalScore =
                    (WeightAbsorption * AbsorptionComponent
                   + WeightTrapped    * TrappedComponent
                   + WeightDivergence * DivergenceComponent
                   + WeightImbalance  * ImbalanceComponent) / TotalWeight;

                // Raw components are ratios of bar volume and rarely exceed
                // 0.2, so lift them into a usable range before thresholding.
                ReversalScore = Clampf(
                    ReversalScore * Input_ReversalSensitivity.GetFloat(), -1.0f, 1.0f);
            }

            // ---- Location and volume context ------------------------------
            const int Lookback = Input_ReversalLookback.GetInt();
            const int LookStart = max(0, BarIndex - Lookback);
            const float ExtremeTolerance = Input_ExtremeToleranceTicks.GetInt() * TickSize;

            bool AtHighExtreme = true;
            bool AtLowExtreme  = true;
            double PriorVolumeSum = 0.0;
            int PriorVolumeCount = 0;

            for (int i = LookStart; i < BarIndex; ++i)
            {
                if (sc.High[i] > sc.High[BarIndex] + ExtremeTolerance)
                    AtHighExtreme = false;
                if (sc.Low[i] < sc.Low[BarIndex] - ExtremeTolerance)
                    AtLowExtreme = false;

                PriorVolumeSum += sc.Volume[i];
                ++PriorVolumeCount;
            }

            // A turn down is only meaningful at a high, a turn up only at a low.
            if (Input_RequireSwingExtreme.GetYesNo())
            {
                if (ReversalScore < 0.0f && !AtHighExtreme)
                    ReversalScore = 0.0f;
                if (ReversalScore > 0.0f && !AtLowExtreme)
                    ReversalScore = 0.0f;
            }

            // Climax volume strengthens the read, thin volume weakens it.
            if (Input_UseVolumeWeighting.GetYesNo() && PriorVolumeCount > 0)
            {
                const double AveragePriorVolume = PriorVolumeSum / (double)PriorVolumeCount;
                if (AveragePriorVolume > 0.0)
                {
                    const float VolumeFactor =
                        Clampf((float)(BarVolume / AveragePriorVolume), 0.25f, 2.0f);
                    ReversalScore = Clampf(ReversalScore * VolumeFactor, -1.0f, 1.0f);
                }
            }
        }

        Subgraph_ReversalScore[BarIndex] = ReversalScore;

        float ReversalMagnitude = ReversalScore;
        if (ReversalMagnitude < 0.0f)
            ReversalMagnitude = -ReversalMagnitude;

        if (BarIndex < sc.ArraySize - 1 && ReversalMagnitude > ObservedReversalPeak)
            ObservedReversalPeak = ReversalMagnitude;

        // ---- Reversal markers ----------------------------------------------
        //  Plotted as standard subgraphs: the draw style, color and size are
        //  set from the Subgraphs tab of Study Settings.
        if (Input_EnableReversalMarker.GetYesNo()
            && ReversalScore != 0.0f
            && ReversalMagnitude >= Input_ReversalMinScore.GetFloat())
        {
            const float MarkerOffset = Input_MarkerOffsetTicks.GetFloat() * TickSize;

            if (ReversalScore > 0.0f)
                Subgraph_ReversalUp[BarIndex] = sc.Low[BarIndex] - MarkerOffset;
            else
                Subgraph_ReversalDown[BarIndex] = sc.High[BarIndex] + MarkerOffset;

            if (Input_ReversalAlerts.GetYesNo()
                && BarIndex == sc.ArraySize - 1
                && BarIndex != LastAlertBarIndex)
            {
                LastAlertBarIndex = BarIndex;

                SCString ReversalAlertText;
                ReversalAlertText.Format("Reversal setup %s  score %.2f",
                    ReversalScore > 0.0f ? "UP" : "DOWN", ReversalScore);

                sc.SetAlert(1, ReversalAlertText);
            }
        }

        // ===================================================================
        //  BAR COLORING
        // ===================================================================
        float ColorMetricValue = 0.0f;
        switch (ColorMetricIndex)
        {
            case 0:  ColorMetricValue = (float)NetImbalanceCount;  break;
            case 1:  ColorMetricValue = (float)BarDelta;           break;
            case 2:  ColorMetricValue = (float)NetAbsorbedVolume;  break;
            case 3:  ColorMetricValue = (float)NetTrappedVolume;   break;
            case 4:  ColorMetricValue = (float)BarVolume;          break;
            default: ColorMetricValue = (float)NetImbalanceCount;  break;
        }

        // Track the range over closed bars only, so a partially formed bar
        // cannot drag the reported min/max around.
        if (BarIndex < sc.ArraySize - 1)
        {
            if (ColorMetricValue < ObservedMetricMin)
                ObservedMetricMin = ColorMetricValue;
            if (ColorMetricValue > ObservedMetricMax)
                ObservedMetricMax = ColorMetricValue;
        }

        const int BackgroundLineNumber = BarLineBase + BACKGROUND_LINE_OFFSET;
        const int ColorMethod = Input_ColorMethod.GetIndex();  // 0 both, 1 bar, 2 background

        // 0 means "no color" - the bar keeps its normal chart appearance.
        COLORREF ResolvedBarColor = 0;

        if (Input_ColorBars.GetYesNo())
        {
            const float DeadZone = Input_ColorBarDeadZone.GetFloat();
            float FullScale = Input_ColorBarFullScale.GetFloat();
            if (FullScale <= DeadZone)
                FullScale = DeadZone + 0.01f;

            const bool SolidMode = (Input_ColorBarMode.GetIndex() == 1);

            const COLORREF NeutralColor  = Input_BarColorNeutral.GetColor();
            const COLORREF PositiveColor = Input_BarColorPositive.GetColor();
            const COLORREF NegativeColor = Input_BarColorNegative.GetColor();

            float Magnitude = ColorMetricValue;
            if (Magnitude < 0.0f)
                Magnitude = -Magnitude;

            if (Magnitude > DeadZone)
            {
                float Fraction = (Magnitude - DeadZone) / (FullScale - DeadZone);
                Fraction = Clampf(Fraction, 0.0f, 1.0f);
                if (SolidMode)
                    Fraction = 1.0f;

                const COLORREF TargetColor =
                    (MetricIsOneSided || ColorMetricValue > 0.0f) ? PositiveColor : NegativeColor;

                ResolvedBarColor = BlendColors(NeutralColor, TargetColor, Fraction);

                // A pure black result would be indistinguishable from "no
                // color", so nudge it off zero.
                if (ResolvedBarColor == 0)
                    ResolvedBarColor = RGB(1, 1, 1);
            }
        }

        // ---- Price bar coloring -------------------------------------------
        // DRAWSTYLE_COLOR_BAR skips bars whose subgraph VALUE is zero, so the
        // value has to be set to something non-zero as well as the color.
        if (ResolvedBarColor != 0 && (ColorMethod == 0 || ColorMethod == 1))
        {
            Subgraph_BarColor[BarIndex] = sc.Close[BarIndex];
            Subgraph_BarColor.DataColor[BarIndex] = ResolvedBarColor;
        }
        else
        {
            Subgraph_BarColor[BarIndex] = 0.0f;
            Subgraph_BarColor.DataColor[BarIndex] = 0;
        }

        // ---- Background highlight ------------------------------------------
        if (ResolvedBarColor != 0 && (ColorMethod == 0 || ColorMethod == 2))
        {
            int BackgroundHalfWidthSeconds =
                (int)(AverageBarSeconds * Input_BackgroundWidth.GetFloat() * 0.5);
            if (BackgroundHalfWidthSeconds < 1)
                BackgroundHalfWidthSeconds = 1;

            s_UseTool BackgroundTool;
            BackgroundTool.Clear();
            BackgroundTool.ChartNumber   = sc.ChartNumber;
            BackgroundTool.Region        = sc.GraphRegion;
            BackgroundTool.DrawingType   = DRAWING_RECTANGLEHIGHLIGHT;
            BackgroundTool.LineNumber    = BackgroundLineNumber;
            BackgroundTool.AddMethod     = UTAM_ADD_OR_ADJUST;
            BackgroundTool.AddAsUserDrawnDrawing = 0;
            BackgroundTool.BeginDateTime = sc.BaseDateTimeIn[BarIndex] - SCDateTime::SECONDS(BackgroundHalfWidthSeconds);
            BackgroundTool.EndDateTime   = sc.BaseDateTimeIn[BarIndex] + SCDateTime::SECONDS(BackgroundHalfWidthSeconds);
            BackgroundTool.BeginValue    = sc.Low[BarIndex];
            BackgroundTool.EndValue      = sc.High[BarIndex];
            BackgroundTool.Color         = ResolvedBarColor;
            BackgroundTool.SecondaryColor= ResolvedBarColor;
            BackgroundTool.TransparencyLevel = Input_BackgroundTransp.GetInt();
            BackgroundTool.LineWidth     = 1;
            BackgroundTool.DrawUnderneathMainGraph = 1;
            sc.UseTool(BackgroundTool);
        }
        else
        {
            sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, BackgroundLineNumber);
        }

        // ---- Absorption alerts ---------------------------------------------
        if (Input_EnableAlerts.GetYesNo()
            && SelectedCount > 0
            && BarIndex == sc.ArraySize - 1
            && BarIndex != LastAlertBarIndex)
        {
            LastAlertBarIndex = BarIndex;

            SCString AlertText;
            AlertText.Format("Absorption %s @ %s  vol %d  score %.1f",
                Selected[0].Direction > 0 ? "BULLISH" : "BEARISH",
                sc.FormatGraphValue(Selected[0].PriceInTicks * TickSize, sc.GetValueFormat()).GetChars(),
                Selected[0].AggressiveVolume,
                Selected[0].Score);

            sc.SetAlert(0, AlertText);
        }
    }

    // =======================================================================
    //  Feed the observed ranges back into the config titles
    // =======================================================================
    if (ObservedMetricMax >= ObservedMetricMin
        && (NameShownMin != ObservedMetricMin || NameShownMax != ObservedMetricMax))
    {
        NameShownMin = ObservedMetricMin;
        NameShownMax = ObservedMetricMax;

        const int UseCompact = Input_CompactNumbers.GetYesNo();

        float LargestMagnitude = ObservedMetricMax;
        if (-ObservedMetricMin > LargestMagnitude)
            LargestMagnitude = -ObservedMetricMin;
        if (LargestMagnitude < 0.0f)
            LargestMagnitude = 0.0f;

        SCString RangeText;
        RangeText.Format("[history %s to %s]",
            FormatVolumeCompact((int)ObservedMetricMin, UseCompact).GetChars(),
            FormatVolumeCompact((int)ObservedMetricMax, UseCompact).GetChars());

        SCString PeakText;
        PeakText.Format("[history peak %s]",
            FormatVolumeCompact((int)LargestMagnitude, UseCompact).GetChars());

        SCString NameText;

        NameText.Format("Color Bars By  %s", RangeText.GetChars());
        Input_ColorBarMetric.Name = NameText;

        NameText.Format("Full Color At Value  %s", PeakText.GetChars());
        Input_ColorBarFullScale.Name = NameText;

        NameText.Format("Leave Bar Uncolored Below Abs Value  %s", PeakText.GetChars());
        Input_ColorBarDeadZone.Name = NameText;
    }

    if (ObservedReversalPeak > 0.0f && NameShownReversal != ObservedReversalPeak)
    {
        NameShownReversal = ObservedReversalPeak;

        SCString NameText;
        NameText.Format("Min Reversal Score To Mark  [history peak %.2f]", ObservedReversalPeak);
        Input_ReversalMinScore.Name = NameText;
    }
}
