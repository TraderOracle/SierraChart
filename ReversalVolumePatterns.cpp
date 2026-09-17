#include "sierrachart.h"

// Reversal Volume Patterns  (+ FVG confirmation)
// Converted from the TradingView Pine Script v4 study of the same name, then
// extended with a Fair Value Gap (FVG) confirmation layer.
//
// Base signals (arrow below bar = buy, arrow above bar = sell):
//   Pattern #1 - small red bar, larger red bar, small green bar
//   Pattern #2 - small red, larger red, even larger red, small green
//   Pattern #3 - 4 same-color bars, then a larger opposite-color bar
//   Pattern #4 - candlestick reversal (pivot + long-wick signal)
//
// FVG confirmation:
//   A 3-bar Fair Value Gap is detected at bar i:
//     bullish FVG  ->  Low[i]  > High[i-2]   (up-side imbalance)
//     bearish FVG  ->  High[i] < Low[i-2]    (down-side imbalance)
//   After a reversal arrow, the study looks for a matching FVG forming within
//   "FVG Window" bars (a bullish FVG confirms a buy, a bearish FVG a sell).
//     Mode 0 "Show all arrows + mark FVG": raw arrow stays, a confirmation
//             dot is drawn on the FVG bar and the gap is boxed. Non-repainting.
//     Mode 1 "Only FVG-confirmed arrows": the arrow is drawn (back-dated onto
//             the signal bar) only once an FVG confirms it. This appears a few
//             bars late, i.e. it repaints within the window.
//
// Build: put in SierraChart\ACS_Source, then Analysis >> Build Custom Studies DLL.

SCDLLName("Reversal Volume Patterns")

namespace
{
    // Line-number base for the FVG rectangle drawings (one per confirming bar).
    const int FVG_LINE_BASE = 30000000;
}

/*==========================================================================*/
SCSFExport scsf_ReversalVolumePatterns(SCStudyInterfaceRef sc)
{
    // ---- Plotted subgraphs ----
    SCSubgraphRef Subgraph_Up          = sc.Subgraph[0];   // raw buy  (arrow below bar)
    SCSubgraphRef Subgraph_Down        = sc.Subgraph[1];   // raw sell (arrow above bar)
    SCSubgraphRef Subgraph_ConfirmUp   = sc.Subgraph[7];   // FVG-confirmed buy  (dot)
    SCSubgraphRef Subgraph_ConfirmDown = sc.Subgraph[8];   // FVG-confirmed sell (dot)

    // ---- Internal (hidden) subgraphs used for calculations / state ----
    SCSubgraphRef Subgraph_RSI         = sc.Subgraph[2];
    SCSubgraphRef Subgraph_HL          = sc.Subgraph[3];   // High - Low
    SCSubgraphRef Subgraph_HLSMA       = sc.Subgraph[4];   // SMA(High-Low, 50)
    SCSubgraphRef Subgraph_FinalUp     = sc.Subgraph[5];   // per-bar final raw up
    SCSubgraphRef Subgraph_FinalDn     = sc.Subgraph[6];   // per-bar final raw down
    SCSubgraphRef Subgraph_UpConfAt    = sc.Subgraph[9];   // bar index that confirmed this up (0 = none)
    SCSubgraphRef Subgraph_DnConfAt    = sc.Subgraph[10];  // bar index that confirmed this down (0 = none)

    // ---- Inputs ----
    SCInputRef Input_Show1        = sc.Input[0];
    SCInputRef Input_Show2        = sc.Input[1];
    SCInputRef Input_Show3        = sc.Input[2];
    SCInputRef Input_Show4        = sc.Input[3];
    SCInputRef Input_RSIFilter    = sc.Input[4];
    SCInputRef Input_RSIOver      = sc.Input[5];
    SCInputRef Input_RSIUnder     = sc.Input[6];
    SCInputRef Input_RSILength    = sc.Input[7];
    SCInputRef Input_ArrowOffset  = sc.Input[8];
    SCInputRef Input_ColorByPat   = sc.Input[9];

    SCInputRef Input_EnableFVG    = sc.Input[10];
    SCInputRef Input_FVGMode      = sc.Input[11];
    SCInputRef Input_FVGWindow    = sc.Input[12];
    SCInputRef Input_MinFVGTicks  = sc.Input[13];
    SCInputRef Input_DrawFVGBox   = sc.Input[14];
    SCInputRef Input_FVGBoxExtend = sc.Input[15];
    SCInputRef Input_FVGBoxTransp = sc.Input[16];
    SCInputRef Input_FVGBullColor = sc.Input[17];
    SCInputRef Input_FVGBearColor = sc.Input[18];

    if (sc.SetDefaults)
    {
        sc.GraphName      = "Reversal Volume Patterns";
        sc.GraphShortName = "Reversal Vol Patterns";
        sc.GraphRegion    = 0;      // overlay on the price chart (Pine overlay=true)
        sc.AutoLoop       = 1;      // per-bar processing, like Pine
        sc.ValueFormat    = VALUEFORMAT_INHERITED;
        sc.DrawZeros      = false;

        Subgraph_Up.Name         = "Buy Signal";
        Subgraph_Up.DrawStyle    = DRAWSTYLE_ARROW_UP;
        Subgraph_Up.PrimaryColor = RGB(0, 128, 255);   // blue (as in original)
        Subgraph_Up.LineWidth    = 2;
        Subgraph_Up.DrawZeros    = false;

        Subgraph_Down.Name         = "Sell Signal";
        Subgraph_Down.DrawStyle    = DRAWSTYLE_ARROW_DOWN;
        Subgraph_Down.PrimaryColor = RGB(0, 128, 255);
        Subgraph_Down.LineWidth    = 2;
        Subgraph_Down.DrawZeros    = false;

        Subgraph_ConfirmUp.Name         = "FVG-Confirmed Buy";
        Subgraph_ConfirmUp.DrawStyle    = DRAWSTYLE_POINT;   // change to Diamond/Triangle in Subgraphs tab if you like
        Subgraph_ConfirmUp.PrimaryColor = RGB(0, 210, 90);   // green
        Subgraph_ConfirmUp.LineWidth    = 4;
        Subgraph_ConfirmUp.DrawZeros    = false;

        Subgraph_ConfirmDown.Name         = "FVG-Confirmed Sell";
        Subgraph_ConfirmDown.DrawStyle    = DRAWSTYLE_POINT;
        Subgraph_ConfirmDown.PrimaryColor = RGB(255, 70, 70); // red
        Subgraph_ConfirmDown.LineWidth    = 4;
        Subgraph_ConfirmDown.DrawZeros    = false;

        Subgraph_RSI.Name     = "RSI (internal)";       Subgraph_RSI.DrawStyle     = DRAWSTYLE_IGNORE;
        Subgraph_HL.Name      = "H-L (internal)";       Subgraph_HL.DrawStyle      = DRAWSTYLE_IGNORE;
        Subgraph_HLSMA.Name   = "H-L SMA50 (internal)"; Subgraph_HLSMA.DrawStyle   = DRAWSTYLE_IGNORE;
        Subgraph_FinalUp.Name = "FinalUp (internal)";   Subgraph_FinalUp.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_FinalDn.Name = "FinalDown (internal)"; Subgraph_FinalDn.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_UpConfAt.Name= "UpConfirmedAt (int)";  Subgraph_UpConfAt.DrawStyle= DRAWSTYLE_IGNORE;
        Subgraph_DnConfAt.Name= "DnConfirmedAt (int)";  Subgraph_DnConfAt.DrawStyle= DRAWSTYLE_IGNORE;

        Input_Show1.Name = "Show Pattern #1";              Input_Show1.SetYesNo(1);
        Input_Show2.Name = "Show Pattern #2";              Input_Show2.SetYesNo(1);
        Input_Show3.Name = "Show Pattern #3";              Input_Show3.SetYesNo(1);
        Input_Show4.Name = "Show Pattern #4 (Candlestick)";Input_Show4.SetYesNo(1);

        Input_RSIFilter.Name = "Filter using RSI";         Input_RSIFilter.SetYesNo(1);

        Input_RSIOver.Name  = "RSI Overbought Value";      Input_RSIOver.SetFloat(56.0f);
        Input_RSIUnder.Name = "RSI Oversold Value";        Input_RSIUnder.SetFloat(44.0f);

        Input_RSILength.Name = "RSI Length";
        Input_RSILength.SetInt(14);
        Input_RSILength.SetIntLimits(1, 1000);

        Input_ArrowOffset.Name = "Arrow Offset (ticks)";
        Input_ArrowOffset.SetInt(4);
        Input_ArrowOffset.SetIntLimits(0, 10000);

        Input_ColorByPat.Name = "Color Raw Arrows by Pattern";
        Input_ColorByPat.SetYesNo(0);

        // ---- FVG confirmation ----
        Input_EnableFVG.Name = "Enable FVG Confirmation";
        Input_EnableFVG.SetYesNo(1);

        Input_FVGMode.Name = "FVG Mode";
        Input_FVGMode.SetCustomInputStrings("Show all arrows + mark FVG;Only FVG-confirmed arrows");
        Input_FVGMode.SetCustomInputIndex(0);

        Input_FVGWindow.Name = "FVG Window (bars after arrow)";
        Input_FVGWindow.SetInt(3);
        Input_FVGWindow.SetIntLimits(0, 50);

        Input_MinFVGTicks.Name = "Min FVG Gap (ticks)";
        Input_MinFVGTicks.SetInt(0);
        Input_MinFVGTicks.SetIntLimits(0, 100000);

        Input_DrawFVGBox.Name = "Draw FVG Box";
        Input_DrawFVGBox.SetYesNo(1);

        Input_FVGBoxExtend.Name = "FVG Box Extend (bars)";
        Input_FVGBoxExtend.SetInt(5);
        Input_FVGBoxExtend.SetIntLimits(0, 500);

        Input_FVGBoxTransp.Name = "FVG Box Transparency (0-100)";
        Input_FVGBoxTransp.SetInt(70);
        Input_FVGBoxTransp.SetIntLimits(0, 100);

        Input_FVGBullColor.Name = "FVG Bullish Box Color";
        Input_FVGBullColor.SetColor(0, 180, 80);

        Input_FVGBearColor.Name = "FVG Bearish Box Color";
        Input_FVGBearColor.SetColor(200, 40, 40);

        return;
    }

    const int i = sc.Index;

    // ------------------------------------------------------------------
    // Helper series computed on EVERY bar so their history stays valid.
    // ------------------------------------------------------------------
    sc.RSI(sc.Close, Subgraph_RSI, MOVAVGTYPE_WILDERS, Input_RSILength.GetInt());

    Subgraph_HL[i] = sc.High[i] - sc.Low[i];
    sc.SimpleMovAvg(Subgraph_HL, Subgraph_HLSMA, 50);

    // reset this bar's outputs / state (both code paths)
    Subgraph_Up[i]          = 0.0f;
    Subgraph_Down[i]        = 0.0f;
    Subgraph_ConfirmUp[i]   = 0.0f;
    Subgraph_ConfirmDown[i] = 0.0f;
    Subgraph_FinalUp[i]     = 0.0f;
    Subgraph_FinalDn[i]     = 0.0f;
    Subgraph_UpConfAt[i]    = 0.0f;
    Subgraph_DnConfAt[i]    = 0.0f;

    if (i < 5)  // need up to 5 bars of look-back (pivot uses [i-5])
    {
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FVG_LINE_BASE + i);
        return;
    }

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
    float HLSMA = Subgraph_HLSMA[i];
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
    if (!Input_Show1.GetYesNo()) { up1 = false; down1 = false; }
    if (!Input_Show2.GetYesNo()) { up2 = false; down2 = false; }
    if (!Input_Show3.GetYesNo()) { up3 = false; down3 = false; }
    if (!Input_Show4.GetYesNo()) { up4 = false; down4 = false; }

    bool up   = (up1 || up2 || up3 || up4);
    bool down = (down1 || down2 || down3 || down4);

    // ---- Eliminate consecutive duplicates (uses previous bar's FINAL value) ----
    if (Subgraph_FinalUp[i-1] != 0.0f) up   = false;
    if (Subgraph_FinalDn[i-1] != 0.0f) down = false;

    // ---- RSI directional filter ----
    if (Input_RSIFilter.GetYesNo())
    {
        float rsiVal = Subgraph_RSI[i];
        if (rsiVal < Input_RSIOver.GetFloat())  down = false; // sell only when >= overbought
        if (rsiVal > Input_RSIUnder.GetFloat())  up   = false; // buy  only when <= oversold
    }

    // ---- Store the raw (post-filter) signal state for this bar ----
    Subgraph_FinalUp[i] = up   ? 1.0f : 0.0f;
    Subgraph_FinalDn[i] = down ? 1.0f : 0.0f;

    const float tick   = sc.TickSize;
    const float offset = Input_ArrowOffset.GetInt() * tick;

    const bool  fvgEnabled    = Input_EnableFVG.GetYesNo() != 0;
    const bool  confirmedMode = fvgEnabled && (Input_FVGMode.GetIndex() == 1);

    // ---- Draw the raw arrow immediately UNLESS we are in confirmed-only mode ----
    if (!confirmedMode)
    {
        if (up)   Subgraph_Up[i]   = sc.Low[i]  - offset;
        if (down) Subgraph_Down[i] = sc.High[i] + offset;

        if (Input_ColorByPat.GetYesNo())
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

    // ==================================================================
    //  FVG DETECTION + CONFIRMATION
    // ==================================================================
    bool drewBox = false;

    if (fvgEnabled)
    {
        const float minGap = Input_MinFVGTicks.GetInt() * tick;
        const int   window = Input_FVGWindow.GetInt();

        // 3-bar Fair Value Gaps completing at bar i (mutually exclusive).
        float bullGapSize = sc.Low[i]   - sc.High[i-2];
        float bearGapSize = sc.Low[i-2] - sc.High[i];
        bool  bullFVG = (bullGapSize > 0.0f) && (bullGapSize >= minGap);
        bool  bearFVG = (bearGapSize > 0.0f) && (bearGapSize >= minGap);

        const float confOff = offset + 3.0f * tick;   // keep the dot clear of the arrow

        if (bullFVG)
        {
            // Nearest raw BUY arrow within the window that this FVG can confirm.
            for (int B = i; B >= i - window && B >= 0; --B)
            {
                bool hasArrow  = (Subgraph_FinalUp[B] != 0.0f);
                bool available = (Subgraph_UpConfAt[B] == 0.0f) || (Subgraph_UpConfAt[B] == (float)i);
                if (hasArrow && available)
                {
                    Subgraph_UpConfAt[B] = (float)i;   // record which bar confirmed it

                    if (confirmedMode)
                        Subgraph_Up[B] = sc.Low[B] - offset;         // back-date the arrow
                    else
                        Subgraph_ConfirmUp[i] = sc.Low[i] - confOff; // dot on the FVG bar

                    if (Input_DrawFVGBox.GetYesNo())
                    {
                        s_UseTool Box;
                        Box.Clear();
                        Box.ChartNumber       = sc.ChartNumber;
                        Box.Region            = sc.GraphRegion;
                        Box.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
                        Box.LineNumber        = FVG_LINE_BASE + i;
                        Box.AddMethod         = UTAM_ADD_OR_ADJUST;
                        Box.AddAsUserDrawnDrawing = 0;
                        Box.BeginIndex        = i - 2;
                        Box.EndIndex          = i + Input_FVGBoxExtend.GetInt();
                        Box.BeginValue        = sc.High[i-2];   // gap bottom
                        Box.EndValue          = sc.Low[i];      // gap top
                        Box.Color             = Input_FVGBullColor.GetColor();
                        Box.SecondaryColor    = Input_FVGBullColor.GetColor();
                        Box.TransparencyLevel = Input_FVGBoxTransp.GetInt();
                        Box.DrawUnderneathMainGraph = 1;
                        sc.UseTool(Box);
                        drewBox = true;
                    }
                    break;
                }
            }
        }
        else if (bearFVG)
        {
            for (int B = i; B >= i - window && B >= 0; --B)
            {
                bool hasArrow  = (Subgraph_FinalDn[B] != 0.0f);
                bool available = (Subgraph_DnConfAt[B] == 0.0f) || (Subgraph_DnConfAt[B] == (float)i);
                if (hasArrow && available)
                {
                    Subgraph_DnConfAt[B] = (float)i;

                    if (confirmedMode)
                        Subgraph_Down[B] = sc.High[B] + offset;         // back-date the arrow
                    else
                        Subgraph_ConfirmDown[i] = sc.High[i] + confOff; // dot on the FVG bar

                    if (Input_DrawFVGBox.GetYesNo())
                    {
                        s_UseTool Box;
                        Box.Clear();
                        Box.ChartNumber       = sc.ChartNumber;
                        Box.Region            = sc.GraphRegion;
                        Box.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
                        Box.LineNumber        = FVG_LINE_BASE + i;
                        Box.AddMethod         = UTAM_ADD_OR_ADJUST;
                        Box.AddAsUserDrawnDrawing = 0;
                        Box.BeginIndex        = i - 2;
                        Box.EndIndex          = i + Input_FVGBoxExtend.GetInt();
                        Box.BeginValue        = sc.High[i];     // gap bottom
                        Box.EndValue          = sc.Low[i-2];    // gap top
                        Box.Color             = Input_FVGBearColor.GetColor();
                        Box.SecondaryColor    = Input_FVGBearColor.GetColor();
                        Box.TransparencyLevel = Input_FVGBoxTransp.GetInt();
                        Box.DrawUnderneathMainGraph = 1;
                        sc.UseTool(Box);
                        drewBox = true;
                    }
                    break;
                }
            }
        }
    }

    // Remove any stale FVG box owned by this bar when it no longer draws one.
    if (!drewBox)
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FVG_LINE_BASE + i);
}
