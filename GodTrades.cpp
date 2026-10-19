#include "sierrachart.h"

SCDLLName("God Trades DLL")

/*
    God Trades

    ------------------------------------------------------------------------
    MAIN FIX: alerts replaying on chart refresh / recalculation.

    Three separate things caused it:

      1. Nothing stopped alert code from running during a full recalculation.
         Sierra Chart re-runs the study over every bar in the chart when you
         refresh, reload, change a setting, press INS, or when it back-fills
         history.  Every one of those passes hit the alert calls again.

      2. The `if (!bIsCurrentBar) return;` guard never actually ran.  The two
         `if (IsVolImb...)` statements above it had their bodies commented out,
         so the compiler attached the *next* statement as the body.  The code
         really parsed as:

             if (IsVolImbGreen(...))
                 if (IsVolImbRed(...))
                     if (!bIsCurrentBar)
                         return;

         Green and red can never both be true, so the return was dead and the
         whole Volume Imbalance block below it - alerts included - ran for
         every bar of the chart on every recalculation.

      3. There was no per-bar de-duplication, so a bar could alert more than
         once inside a single session.

    All alerts now go through FireAlert(), which refuses to fire during a full
    recalculation or a history download, and remembers the DateTime of the last
    bar it alerted on (in a persistent variable) so a given bar can only fire
    a given alert once.
    ------------------------------------------------------------------------
*/

#pragma region COMMON FUNCTIONS

double CandleLength(SCBaseDataRef InData, int index) {
    return InData[SC_HIGH][index] - InData[SC_LOW][index];
}

double BodyLength(SCBaseDataRef InData, int index) {
    return fabs(InData[SC_OPEN][index] - InData[SC_LAST][index]);
}

double PercentOfCandleLength(SCBaseDataRef InData, int index, double percent) {
    return CandleLength(InData, index) * (percent / 100.0);
}

double PercentOfBodyLength(SCBaseDataRef InData, int index, double percent) {
    return BodyLength(InData, index) * (percent / 100.0);
}

double UpperWickLength(SCBaseDataRef InData, int index) {
    const double upperBoundary = max(InData[SC_LAST][index], InData[SC_OPEN][index]);
    return InData[SC_HIGH][index] - upperBoundary;
}

double LowerWickLength(SCBaseDataRef InData, int index) {
    const double lowerBoundary = min(InData[SC_LAST][index], InData[SC_OPEN][index]);
    return lowerBoundary - InData[SC_LOW][index];
}

bool IsGreen(SCBaseDataRef InData, int index) {
    return InData[SC_LAST][index] > InData[SC_OPEN][index];
}

bool IsRed(SCBaseDataRef InData, int index) {
    return InData[SC_LAST][index] < InData[SC_OPEN][index];
}

// FIXED: was taking InData/index it never used, and used abs() on doubles.
// Tolerance is now an explicit price distance (callers pass 3 * sc.TickSize,
// which is what the old code worked out to).
bool IsNearEqual(double value1, double value2, double tolerance) {
    return fabs(value1 - value2) < tolerance;
}

bool IsUpperWickSmall(SCBaseDataRef InData, int index, double percent) {
    return UpperWickLength(InData, index) < PercentOfCandleLength(InData, index, percent);
}

bool IsLowerWickSmall(SCBaseDataRef InData, int index, double percent) {
    return LowerWickLength(InData, index) < PercentOfCandleLength(InData, index, percent);
}

bool IsBullishEngulfing(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;

    if (index < 1)
        return false;

    if (InData[SC_LAST][index - 1] < InData[SC_OPEN][index - 1] && InData[SC_LAST][index] > InData[SC_OPEN][index]) {
        if ((InData[SC_HIGH][index] > InData[SC_HIGH][index - 1]) &&
            (InData[SC_LOW][index] < InData[SC_LOW][index - 1]) &&
            (InData[SC_LAST][index] > InData[SC_OPEN][index - 1]) &&
            (InData[SC_OPEN][index] < InData[SC_LAST][index - 1]))
            return true;
    }

    return false;
}

bool IsBearishEngulfing(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;

    if (index < 1)
        return false;

    if (InData[SC_LAST][index - 1] > InData[SC_OPEN][index - 1] && InData[SC_LAST][index] < InData[SC_OPEN][index]) {
        if ((InData[SC_HIGH][index] > InData[SC_HIGH][index - 1]) &&
            (InData[SC_LOW][index] < InData[SC_LOW][index - 1]) &&
            (InData[SC_OPEN][index] > InData[SC_LAST][index - 1]) &&
            (InData[SC_LAST][index] < InData[SC_OPEN][index - 1]))
            return true;
    }

    return false;
}

bool IsThreeOutsideUp(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;

    if (index < 3)
        return false;

    if (IsGreen(InData, index) && (InData[SC_LAST][index] > InData[SC_LAST][index - 1]))
        return IsBullishEngulfing(sc, index - 1);

    return false;
}

bool IsThreeOutsideDown(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;

    if (index < 3)
        return false;

    if (IsRed(InData, index) && (InData[SC_LAST][index] < InData[SC_LAST][index - 1]))
        return IsBearishEngulfing(sc, index - 1);

    return false;
}

const int k_Body_NUM_OF_CANDLES = 5; // number of previous candles to calculate body strength

// FIXED: `auto mov_aver{0}` made this an int, so every body length was
// truncated to a whole number of points - on ES/NQ that is almost always 0.
inline bool IsBodyStrong(SCBaseDataRef InData, int index) {
    if (index < k_Body_NUM_OF_CANDLES)
        return false;

    double mov_aver = 0.0;
    for (int i = 1; i <= k_Body_NUM_OF_CANDLES; i++)
        mov_aver += BodyLength(InData, index - i);
    mov_aver /= k_Body_NUM_OF_CANDLES;

    return BodyLength(InData, index) > mov_aver;
}

// FIXED: was comparing Open[i-1] against Close[i-2], which on continuous
// futures data is near-identical on almost every bar, so this fired constantly.
// A tweezer top / "Equal High" needs the two HIGHS to match.
bool IsTweezerTop(SCStudyInterfaceRef sc, int index, float UpperBand, double Tolerance) {
    SCBaseDataRef InData = sc.BaseData;

    if (index < 3)
        return false;

    if (IsNearEqual(InData[SC_HIGH][index - 1], InData[SC_HIGH][index - 2], Tolerance)
        && InData[SC_LOW][index] < InData[SC_LOW][index - 1] // current bar lower than previous
        && IsRed(InData, index) && IsRed(InData, index - 1)
        && IsGreen(InData, index - 2) && IsGreen(InData, index - 3)
        && (InData[SC_HIGH][index - 1] > UpperBand || InData[SC_HIGH][index - 2] > UpperBand))
        return true;

    return false;
}

// FIXED: same problem as IsTweezerTop - now compares the two LOWS.
bool IsTweezerBottom(SCStudyInterfaceRef sc, int index, float LowerBand, double Tolerance) {
    SCBaseDataRef InData = sc.BaseData;

    if (index < 3)
        return false;

    if (IsNearEqual(InData[SC_LOW][index - 1], InData[SC_LOW][index - 2], Tolerance)
        && InData[SC_HIGH][index] > InData[SC_HIGH][index - 1]
        && IsGreen(InData, index) && IsGreen(InData, index - 1)
        && IsRed(InData, index - 2) && IsRed(InData, index - 3)
        && (InData[SC_LOW][index - 1] < LowerBand || InData[SC_LOW][index - 2] < LowerBand))
        return true;

    return false;
}

/*
    FIXED: IsTrampoline() used to contain both the bullish and the bearish test
    but only accepted ONE band, and it was called twice - once with UpperBand,
    once with LowerBand.  That meant the bullish test was being run against the
    upper band (Low[i-2] <= UpperBand is true nearly always) and the bearish
    test against the lower band (High[i-2] >= LowerBand is true nearly always),
    so both calls returned true far too often.

    It also declared the tick size parameter as `int`, so passing sc.TickSize
    (0.25 on ES/NQ) truncated it to 0 and the tick buffer did nothing.

    Split into two functions, each with the band it actually needs.
*/
bool IsTrampolineDown(SCStudyInterfaceRef sc, int index, float rsi, float prsi, float pprsi,
                      float UpperBand, float TickSize) {
    SCBaseDataRef InData = sc.BaseData;

    if (index < 2)
        return false;

    return IsRed(InData, index) && IsRed(InData, index - 1) && IsGreen(InData, index - 2)
           && InData[SC_LAST][index] < InData[SC_LAST][index - 1]
           && (rsi > 80 || prsi > 80 || pprsi > 80)
           && InData[SC_HIGH][index - 2] >= (UpperBand - TickSize);
}

bool IsTrampolineUp(SCStudyInterfaceRef sc, int index, float rsi, float prsi, float pprsi,
                    float LowerBand, float TickSize) {
    SCBaseDataRef InData = sc.BaseData;

    if (index < 2)
        return false;

    return IsGreen(InData, index) && IsGreen(InData, index - 1) && IsRed(InData, index - 2)
           && InData[SC_LAST][index] > InData[SC_LAST][index - 1]
           && (rsi < 20 || prsi < 20 || pprsi < 20)
           && InData[SC_LOW][index - 2] <= (LowerBand + TickSize);
}

// Placeholder - never implemented, kept so existing references still compile.
bool IsStairs(SCStudyInterfaceRef sc, int index) {
    return false;
}

bool IsVolImbGreen(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;

    if (index < 1)
        return false;

    return IsGreen(InData, index) && IsGreen(InData, index - 1)
           && InData[SC_OPEN][index] > InData[SC_LAST][index - 1];
}

bool IsVolImbRed(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;

    if (index < 1)
        return false;

    return IsRed(InData, index) && IsRed(InData, index - 1)
           && InData[SC_OPEN][index] < InData[SC_LAST][index - 1];
}

// FIXED: abs() on floats can bind to the integer abs() and silently truncate.
// Using fabs() throughout.
bool IsDoji(SCBaseDataRef InData, int index) {
    const double body = fabs(InData[SC_OPEN][index] - InData[SC_LAST][index]);

    if (IsRed(InData, index)) {
        return fabs(InData[SC_HIGH][index] - InData[SC_OPEN][index]) > body
               && fabs(InData[SC_LAST][index] - InData[SC_LOW][index]) > body;
    }

    if (IsGreen(InData, index)) {
        return fabs(InData[SC_HIGH][index] - InData[SC_LAST][index]) > body
               && fabs(InData[SC_OPEN][index] - InData[SC_LOW][index]) > body;
    }

    return false;
}

/*
    FIXED: this used UTAM_ADD_ALWAYS with no LineNumber, so every single pass of
    the study stacked another copy of the text on top of the old one.  On a
    refresh that is one new drawing per matching bar, forever, until the chart
    is closed.  Now every drawing gets a stable LineNumber derived from the bar
    it belongs to, and UTAM_ADD_OR_ADJUST updates it in place instead.

    Also: the old version positioned the text using one bar index but anchored
    it with Tool.BeginIndex = sc.CurrentIndex, so "Eq Hi" / "Eq Lo" / "TR" were
    drawn on the wrong bar.  The bar index is now an explicit parameter.
*/
void DrawTextAtBar(SCStudyInterfaceRef sc, SCSubgraphRef Subgraph, const SCString &Text,
                   int BarIndex, int AboveCandle, int BufferPercent, int DrawingSlot) {
    if (BarIndex < 0 || BarIndex >= sc.ArraySize)
        return;

    s_UseTool Tool;
    Tool.Clear();

    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_TEXT;
    Tool.Region = sc.GraphRegion;
    Tool.BeginIndex = BarIndex;

    // Stable, unique ID per (bar, pattern) so the drawing is replaced, not duplicated.
    Tool.LineNumber = (BarIndex * 8) + DrawingSlot;
    Tool.AddMethod = UTAM_ADD_OR_ADJUST;

    if (AboveCandle == 0) // automatic detection
    {
        if (sc.Close[BarIndex] > sc.Open[BarIndex]) // green candle
            AboveCandle = 1;
        else
            AboveCandle = -1;
    }

    const float CandleRange = sc.High[BarIndex] - sc.Low[BarIndex];

    if (AboveCandle == 1) {
        Tool.BeginValue = sc.High[BarIndex] + (CandleRange * BufferPercent * 0.01f);
        Tool.TextAlignment = DT_CENTER | DT_BOTTOM;
    } else {
        Tool.BeginValue = sc.Low[BarIndex] - (CandleRange * BufferPercent * 0.01f);
        Tool.TextAlignment = DT_CENTER | DT_TOP;
    }

    Tool.Color = Subgraph.PrimaryColor;
    Tool.FontBackColor = Subgraph.SecondaryColor;
    if (Text == "TR")
        Tool.FontBackColor = COLOR_RED;
    Tool.FontSize = Subgraph.LineWidth;
    Tool.FontBold = TRUE;
    Tool.Text = Text;

    sc.UseTool(Tool);
}

#pragma endregion

#pragma region PERSISTENT STORAGE SLOTS

// Persistent ints
const int PI_SQUEEZE_STATE = 0;

// Persistent doubles - each holds the DateTime of the last bar that fired that
// alert, so the same bar can never fire the same alert twice.
const int PD_ALERT_BUY = 1;
const int PD_ALERT_SELL = 2;
const int PD_ALERT_KAMA_WICK = 3;
const int PD_ALERT_TRAMP_UP = 4;
const int PD_ALERT_TRAMP_DOWN = 5;
const int PD_ALERT_BBGAP_GREEN = 6;
const int PD_ALERT_BBGAP_RED = 7;
const int PD_ALERT_VOLIMB_GREEN = 8;
const int PD_ALERT_VOLIMB_RED = 9;
const int PD_ALERT_VOLIMB_FILL = 10;

// Drawing slots (must stay below 8 - see DrawTextAtBar LineNumber maths)
const int DRAWSLOT_3OU = 1;
const int DRAWSLOT_3OD = 2;
const int DRAWSLOT_EQHI = 3;
const int DRAWSLOT_EQLO = 4;
const int DRAWSLOT_TRAMP = 5;

#pragma endregion

SCSFExport scsf_GodTrades(SCStudyInterfaceRef sc) {
#pragma region INPUTS

    SCInputRef Input_WaddahIntensity = sc.Input[0];

    SCInputRef Input_UseWaddah = sc.Input[1];
    SCInputRef Input_UseMacd = sc.Input[2];
    SCInputRef Input_UseSar = sc.Input[3];
    SCInputRef Input_UseSuperTrend = sc.Input[4];
    SCInputRef Input_UseAO = sc.Input[5];
    SCInputRef Input_UseHMA = sc.Input[6];
    SCInputRef Input_UseT3 = sc.Input[7];
    SCInputRef Input_UseFisher = sc.Input[8];
    SCInputRef Input_ADX = sc.Input[9];
    SCInputRef Input_IgnoreDoji = sc.Input[10];

    SCInputRef Input_BarColor = sc.Input[11];
    SCInputRef Input_BarColorWaddah = sc.Input[12];
    SCInputRef Input_BarColorLinda = sc.Input[13];

    SCInputRef Input_UpOffset = sc.Input[14];
    SCInputRef Input_DownOffset = sc.Input[15];
    SCInputRef Input_ShavedBuffer = sc.Input[16];
    SCInputRef Input_DojiCity = sc.Input[17];

    // Master on/off switch for every alert this study can raise.
    // Deliberately placed at the end so slots 0-17 keep their saved values.
    SCInputRef Input_EnableAlerts = sc.Input[18];

    SCSubgraphRef Subgraph_DotUp = sc.Subgraph[0];
    SCSubgraphRef Subgraph_DotDown = sc.Subgraph[1];
    SCSubgraphRef Subgraph_VolImbUp = sc.Subgraph[2];
    SCSubgraphRef Subgraph_VolImbDown = sc.Subgraph[3];
    SCSubgraphRef Subgraph_SqueezeUp = sc.Subgraph[4];
    SCSubgraphRef Subgraph_SqueezeDown = sc.Subgraph[5];
    SCSubgraphRef Subgraph_3oU = sc.Subgraph[6];
    SCSubgraphRef Subgraph_3oD = sc.Subgraph[7];
    SCSubgraphRef Subgraph_EqualH = sc.Subgraph[8];
    SCSubgraphRef Subgraph_EqualL = sc.Subgraph[9];
    SCSubgraphRef Subgraph_Tramp = sc.Subgraph[10];
    SCSubgraphRef Subgraph_KAMA = sc.Subgraph[11];

    SCSubgraphRef Subgraph_WaddahPos = sc.Subgraph[12];
    SCSubgraphRef Subgraph_WaddahNeg = sc.Subgraph[13];
    SCSubgraphRef Subgraph_Slow = sc.Subgraph[14];
    SCSubgraphRef Subgraph_Fast = sc.Subgraph[15];
    SCSubgraphRef Subgraph_BB = sc.Subgraph[16];
    SCSubgraphRef Subgraph_ColorBar = sc.Subgraph[17];
    SCSubgraphRef Subgraph_ColorUp = sc.Subgraph[18];
    SCSubgraphRef Subgraph_ColorDown = sc.Subgraph[19];
    SCSubgraphRef Subgraph_LindaMACD = sc.Subgraph[20];
    SCSubgraphRef Subgraph_Parabolic = sc.Subgraph[21];
    SCSubgraphRef Subgraph_AO = sc.Subgraph[22];
    SCSubgraphRef Subgraph_Fisher = sc.Subgraph[23];
    SCSubgraphRef Subgraph_ADX = sc.Subgraph[24];
    SCSubgraphRef Subgraph_T3 = sc.Subgraph[25];
    SCSubgraphRef Subgraph_HMA = sc.Subgraph[26];
    SCSubgraphRef Subgraph_Calc = sc.Subgraph[27];
    SCSubgraphRef Subgraph_MomentumHist = sc.Subgraph[28];
    SCSubgraphRef Subgraph_MomentumBasis = sc.Subgraph[29]; // was Subgraph_MomentumHistUpColors
    SCSubgraphRef Subgraph_MomentumSource = sc.Subgraph[30]; // was Subgraph_MomentumHistDownColors
    SCSubgraphRef Subgraph_SuperTrend = sc.Subgraph[31];
    SCSubgraphRef Subgraph_Intersection = sc.Subgraph[32];
    SCSubgraphRef Subgraph_BBGreen = sc.Subgraph[33];
    SCSubgraphRef Subgraph_BBRed = sc.Subgraph[34];
    SCSubgraphRef Subgraph_ShavedGreen = sc.Subgraph[35];
    SCSubgraphRef Subgraph_ShavedRed = sc.Subgraph[36];
    SCSubgraphRef Subgraph_DojiCity = sc.Subgraph[37];
    SCSubgraphRef Subgraph_HullATR = sc.Subgraph[38];
    // Volume Imbalance tracking
    SCSubgraphRef Subgraph_VolImbDirection = sc.Subgraph[41];
    SCSubgraphRef Subgraph_VolImbPrice = sc.Subgraph[42];
    SCSubgraphRef Subgraph_VolImbOriginCandle = sc.Subgraph[43];

    SCFloatArrayRef Array_TrueRange = Subgraph_SuperTrend.Arrays[0];
    SCFloatArrayRef Array_AvgTrueRange = Subgraph_SuperTrend.Arrays[1];
    SCFloatArrayRef Array_UpperBandBasic = Subgraph_SuperTrend.Arrays[2];
    SCFloatArrayRef Array_LowerBandBasic = Subgraph_SuperTrend.Arrays[3];
    SCFloatArrayRef Array_UpperBand = Subgraph_SuperTrend.Arrays[4];
    SCFloatArrayRef Array_LowerBand = Subgraph_SuperTrend.Arrays[5];

    // FIXED: the Fisher Transform intermediate used to live in
    // Subgraph_Calc.Arrays[0], but sc.RSI() also writes into Subgraph_Calc's
    // internal arrays.  The two were overwriting each other every bar.
    SCFloatArrayRef Array_FisherValue = Subgraph_Fisher.Arrays[0];

#pragma endregion

#pragma region DEFAULTS

    if (sc.SetDefaults) {
        sc.GraphName = "God Trades";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;

        Input_WaddahIntensity.Name = "Waddah Intensity";
        Input_WaddahIntensity.SetInt(150);

        Input_UseWaddah.Name = "Use Waddah";
        Input_UseWaddah.SetYesNo(1);

        Input_UseMacd.Name = "Use MACD";
        Input_UseMacd.SetYesNo(1);

        Input_UseSar.Name = "Use Parabolic Sar";
        Input_UseSar.SetYesNo(1);

        Input_UseSuperTrend.Name = "Use Supertrend";
        Input_UseSuperTrend.SetYesNo(0);

        Input_UseAO.Name = "Use Awesome Oscillator";
        Input_UseAO.SetYesNo(0);

        Input_UseFisher.Name = "Use Fisher Transform";
        Input_UseFisher.SetYesNo(1);

        Input_UseHMA.Name = "Use Hull Moving Average";
        Input_UseHMA.SetYesNo(1);

        Input_UseT3.Name = "Use T3";
        Input_UseT3.SetYesNo(0);

        Input_ADX.Name = "Minimum ADX";
        Input_ADX.SetInt(11);

        Input_UpOffset.Name = "Up Offset In Ticks";
        Input_UpOffset.SetInt(2);

        Input_DownOffset.Name = "Down Offset In Ticks";
        Input_DownOffset.SetInt(2);

        Input_ShavedBuffer.Name = "Shaved candle buffer (ticks)";
        Input_ShavedBuffer.SetInt(1);

        Input_BarColor.Name = "Bar coloring";
        Input_BarColor.SetCustomInputStrings("None;Waddah;Linda MACD;Supertrend");
        Input_BarColor.SetCustomInputIndex(0);

        Input_BarColorWaddah.Name = "Bar color Waddah offset";
        Input_BarColorWaddah.SetInt(80);

        Input_BarColorLinda.Name = "Bar color LindaMACD offset";
        Input_BarColorLinda.SetInt(40);

        Input_IgnoreDoji.Name = "Ignore Dojis";
        Input_IgnoreDoji.SetYesNo(1);
        Input_IgnoreDoji.SetDescription("Suppress buy/sell dots when wicks are larger than the candle body");

        Input_DojiCity.Name = "Show Doji Cities";
        Input_DojiCity.SetYesNo(1);

        Input_EnableAlerts.Name = "Enable Alerts";
        Input_EnableAlerts.SetYesNo(1);
        Input_EnableAlerts.SetDescription(
            "Master switch. Set to No to silence every alert and alert log message from this study. "
            "Dots, arrows, text and bar colouring are unaffected.");

        // =======================================================================

        Subgraph_ColorBar.Name = "Bar Color";
        Subgraph_ColorBar.DrawStyle = DRAWSTYLE_COLOR_BAR;
        Subgraph_ColorBar.LineWidth = 1;
        Subgraph_ColorBar.DrawZeros = false;

        Subgraph_ColorUp.Name = "Bar Color Up";
        Subgraph_ColorUp.PrimaryColor = RGB(0, 255, 0);
        Subgraph_ColorUp.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_ColorUp.LineWidth = 1;
        Subgraph_ColorUp.DrawZeros = false;

        Subgraph_ColorDown.Name = "Bar Color Down";
        Subgraph_ColorDown.PrimaryColor = RGB(255, 0, 0);
        Subgraph_ColorDown.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_ColorDown.LineWidth = 1;
        Subgraph_ColorDown.DrawZeros = false;

        Subgraph_DotUp.Name = "Standard Buy Dot";
        Subgraph_DotUp.PrimaryColor = RGB(0, 255, 0);
        Subgraph_DotUp.DrawStyle = DRAWSTYLE_TRIANGLE_UP;
        Subgraph_DotUp.LineWidth = 2;
        Subgraph_DotUp.DrawZeros = false;

        Subgraph_DotDown.Name = "Standard Sell Dot";
        Subgraph_DotDown.PrimaryColor = RGB(255, 0, 0);
        Subgraph_DotDown.DrawStyle = DRAWSTYLE_TRIANGLE_DOWN;
        Subgraph_DotDown.LineWidth = 2;
        Subgraph_DotDown.DrawZeros = false;

        Subgraph_SqueezeUp.Name = "Squeeze Buy Dot";
        Subgraph_SqueezeUp.PrimaryColor = RGB(255, 255, 0);
        Subgraph_SqueezeUp.DrawStyle = DRAWSTYLE_STAR;
        Subgraph_SqueezeUp.LineWidth = 1;
        Subgraph_SqueezeUp.DrawZeros = false;

        Subgraph_SqueezeDown.Name = "Squeeze Sell Dot";
        Subgraph_SqueezeDown.PrimaryColor = RGB(255, 255, 0);
        Subgraph_SqueezeDown.DrawStyle = DRAWSTYLE_STAR;
        Subgraph_SqueezeDown.LineWidth = 1;
        Subgraph_SqueezeDown.DrawZeros = false;

        Subgraph_VolImbUp.Name = "Volume Imbalance Up";
        Subgraph_VolImbUp.PrimaryColor = RGB(255, 255, 255);
        Subgraph_VolImbUp.DrawStyle = DRAWSTYLE_ARROW_UP;
        Subgraph_VolImbUp.LineWidth = 1;
        Subgraph_VolImbUp.DrawZeros = false;

        Subgraph_VolImbDown.Name = "Volume Imbalance Down";
        Subgraph_VolImbDown.PrimaryColor = RGB(255, 255, 255);
        Subgraph_VolImbDown.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        Subgraph_VolImbDown.LineWidth = 1;
        Subgraph_VolImbDown.DrawZeros = false;

        Subgraph_KAMA.Name = "KAMA";
        Subgraph_KAMA.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_KAMA.LineWidth = 2;
        Subgraph_KAMA.PrimaryColor = RGB(191, 140, 29);

        Subgraph_DojiCity.Name = "Doji City";
        Subgraph_DojiCity.DrawStyle = DRAWSTYLE_BACKGROUND;
        Subgraph_DojiCity.PrimaryColor = RGB(27, 25, 94);
        Subgraph_DojiCity.LineWidth = 1;
        Subgraph_DojiCity.DrawZeros = false;

        Subgraph_3oU.Name = "Three Outside Up";
        Subgraph_3oU.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
        Subgraph_3oU.PrimaryColor = RGB(255, 255, 29);
        Subgraph_3oU.SecondaryColor = RGB(1, 97, 3);
        Subgraph_3oU.LineWidth = 8;
        Subgraph_3oU.DrawZeros = false;

        Subgraph_3oD.Name = "Three Outside Down";
        Subgraph_3oD.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
        Subgraph_3oD.PrimaryColor = RGB(255, 255, 29);
        Subgraph_3oD.SecondaryColor = RGB(97, 1, 1);
        Subgraph_3oD.LineWidth = 8;
        Subgraph_3oD.DrawZeros = false;

        Subgraph_EqualH.Name = "Equal High";
        Subgraph_EqualH.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
        Subgraph_EqualH.PrimaryColor = RGB(255, 255, 29);
        Subgraph_EqualH.SecondaryColor = RGB(97, 1, 1);
        Subgraph_EqualH.LineWidth = 8;
        Subgraph_EqualH.DrawZeros = false;

        Subgraph_EqualL.Name = "Equal Low";
        Subgraph_EqualL.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
        Subgraph_EqualL.PrimaryColor = RGB(255, 255, 29);
        Subgraph_EqualL.SecondaryColor = RGB(1, 97, 3);
        Subgraph_EqualL.LineWidth = 8;
        Subgraph_EqualL.DrawZeros = false;

        Subgraph_Tramp.Name = "Trampoline";
        Subgraph_Tramp.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
        Subgraph_Tramp.PrimaryColor = RGB(0, 0, 0);
        Subgraph_Tramp.SecondaryColor = RGB(169, 205, 252);
        Subgraph_Tramp.LineWidth = 8;
        Subgraph_Tramp.DrawZeros = false;

        Subgraph_BBGreen.Name = "Engulfing Green BB";
        Subgraph_BBGreen.PrimaryColor = RGB(0, 64, 0);
        Subgraph_BBGreen.DrawStyle = DRAWSTYLE_BACKGROUND;
        Subgraph_BBGreen.LineWidth = 1;
        Subgraph_BBGreen.DrawZeros = false;

        Subgraph_BBRed.Name = "Engulfing Red BB";
        Subgraph_BBRed.PrimaryColor = RGB(70, 0, 35);
        Subgraph_BBRed.DrawStyle = DRAWSTYLE_BACKGROUND;
        Subgraph_BBRed.LineWidth = 1;
        Subgraph_BBRed.DrawZeros = false;

        Subgraph_ShavedGreen.Name = "Shaved Green Candle";
        Subgraph_ShavedGreen.PrimaryColor = RGB(170, 221, 255);
        Subgraph_ShavedGreen.DrawStyle = DRAWSTYLE_COLOR_BAR;
        Subgraph_ShavedGreen.LineWidth = 1;
        Subgraph_ShavedGreen.DrawZeros = false;

        Subgraph_ShavedRed.Name = "Shaved Red Candle";
        Subgraph_ShavedRed.PrimaryColor = RGB(255, 128, 192);
        Subgraph_ShavedRed.DrawStyle = DRAWSTYLE_COLOR_BAR;
        Subgraph_ShavedRed.LineWidth = 1;
        Subgraph_ShavedRed.DrawZeros = false;

        Subgraph_LindaMACD.Name = "Linda MACD";
        Subgraph_LindaMACD.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_Parabolic.Name = "Parabolic";
        Subgraph_Parabolic.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_HMA.Name = "Hull Moving Average";
        Subgraph_HMA.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_HullATR.Name = "Hull ATR (SuperTrend)";
        Subgraph_HullATR.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_Calc.Name = "RSI";
        Subgraph_Calc.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_MomentumHist.Name = "Squeeze Relaxer Histogram";
        Subgraph_MomentumHist.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_MomentumBasis.Name = "Squeeze Relaxer EMA";
        Subgraph_MomentumBasis.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_MomentumSource.Name = "Squeeze Relaxer Source";
        Subgraph_MomentumSource.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_BB.Name = "Bollinger Bands";
        Subgraph_BB.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_AO.Name = "Awesome Oscillator";
        Subgraph_AO.DrawStyle = DRAWSTYLE_IGNORE;

        // FIXED: was also named "Awesome Oscillator" (copy/paste)
        Subgraph_Fisher.Name = "Fisher Transform";
        Subgraph_Fisher.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_ADX.Name = "ADX";
        Subgraph_ADX.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_T3.Name = "T3";
        Subgraph_T3.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_Slow.Name = "SMA Slow";
        Subgraph_Slow.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_Fast.Name = "SMA Fast";
        Subgraph_Fast.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_WaddahPos.Name = "Waddah Positive";
        Subgraph_WaddahPos.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_WaddahNeg.Name = "Waddah Negative";
        Subgraph_WaddahNeg.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_SuperTrend.Name = "SuperTrend";
        Subgraph_SuperTrend.DrawStyle = DRAWSTYLE_IGNORE;

        // VOLUME IMBALANCE TRACKING - for detecting intersection
        Subgraph_Intersection.Name = "Vol Imb Intersections";
        Subgraph_Intersection.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_VolImbOriginCandle.Name = "VolImb Origin Candle";
        Subgraph_VolImbOriginCandle.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_VolImbOriginCandle.DrawZeros = false;
        Subgraph_VolImbDirection.Name = "VolImb Direction";
        Subgraph_VolImbDirection.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_VolImbDirection.DrawZeros = false;
        Subgraph_VolImbPrice.Name = "VolImb Price";
        Subgraph_VolImbPrice.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_VolImbPrice.DrawZeros = false;

        return;
    }

#pragma endregion

#pragma region ALERT NUMBERS

    int ALERT_EMERGENCY = 30; // currently unused
    int ALERT_EMA21_WICK = 12; // currently unused
    int ALERT_KAMA_WICK = 13;

    int ALERT_BUY = 5;
    int ALERT_SELL = 6;
    int ALERT_BBGAP_GREEN = 21;
    int ALERT_BBGAP_RED = 22;
    int ALERT_VOLIMB_GREEN = 17;
    int ALERT_VOLIMB_RED = 17;
    int ALERT_VOLIMB_FILL = 19;
    int ALERT_TRAMP_UP = 23;
    int ALERT_TRAMP_DOWN = 24;

    if (strstr(sc.Symbol.GetChars(), "NQ") != NULL) {
        ALERT_BUY = 7;
        ALERT_SELL = 8;
        ALERT_BBGAP_GREEN = 21;
        ALERT_BBGAP_RED = 22;
        ALERT_VOLIMB_GREEN = 18;
        ALERT_VOLIMB_RED = 18;
        ALERT_VOLIMB_FILL = 20;
        ALERT_TRAMP_UP = 23;
        ALERT_TRAMP_DOWN = 24;
    }

#pragma endregion

#pragma region LOCAL VARIABLES

    SCString txt;

    const int i = sc.Index;

    // Longest lookback in play: the 40 period EMA, plus a few bars of pattern
    // history.  Below this the indicators are still warming up.
    const int MinimumRequiredBars = 45;
    sc.DataStartIndex = MinimumRequiredBars;

    int &r_SqueezeUp = sc.GetPersistentInt(PI_SQUEEZE_STATE);

    // FIXED: persistent state used to carry over into a recalculation, so a
    // refresh produced different squeeze marks than the live run.  Reset it at
    // the start of every pass over the chart.
    if (i == 0)
        r_SqueezeUp = 0;

    const float close = sc.Close[i];
    const float open = sc.Open[i];
    const float high = sc.High[i];
    const float low = sc.Low[i];
    const float pclose = sc.Close[i - 1];
    const float popen = sc.Open[i - 1];
    const float phigh = sc.High[i - 1];
    const float plow = sc.Low[i - 1];

    // FIXED: these were all `auto x{0}`, i.e. INTEGERS.  Every wick and body
    // length was being truncated to whole points, which on ES/NQ (0.25 tick)
    // meant most of them rounded to 0 and the doji test was meaningless.
    const double body = fabs(open - close);
    const double pbody = fabs(popen - pclose);

    const bool red = open > close;
    const bool green = open < close;

    double upperwick = 0.0;
    double lowerwick = 0.0;
    bool pdoji = false;

    if (green) {
        upperwick = fabs(high - close);
        lowerwick = fabs(open - low);
        pdoji = fabs(phigh - pclose) > pbody && fabs(popen - plow) > pbody; // FIXED: was comparing against `body`
    } else // red
    {
        upperwick = fabs(high - open);
        lowerwick = fabs(close - low);
        pdoji = fabs(phigh - popen) > pbody && fabs(pclose - plow) > pbody; // FIXED: was comparing against `body`
    }

    const bool doji = upperwick > body && lowerwick > body;

    SCFloatArrayRef Price = sc.BaseData[SC_HL_AVG];

    bool bSuperUp = false;

    // The "current bar" for signalling purposes is the most recently CLOSED
    // bar, not the one still forming.
    const bool bIsLastClosedBar = (i == sc.ArraySize - 2);
    const bool bBarClosed = (sc.GetBarHasClosedStatus(i) == BHCS_BAR_HAS_CLOSED);

#pragma endregion

#pragma region ALERT GATE

    /*
        THIS IS THE FIX FOR THE REPEATED ALERTS.

        Input_EnableAlerts          -> master on/off switch in study settings
        sc.IsFullRecalculation      -> set while SC is reprocessing the whole
                                       chart (refresh, reload, INS key, changing
                                       a study setting, adding the study, ...)
        sc.DownloadingHistoricalData-> set while history is being back-filled
        sc.UpdateStartIndex == 0    -> belt and braces, also means "whole chart"

        None of those are live ticks, so none of them should make noise.
    */
    const bool bAlertsAllowed =
            Input_EnableAlerts.GetYesNo() == SC_YES
            && bIsLastClosedBar
            && sc.IsFullRecalculation == 0
            && sc.DownloadingHistoricalData == 0
            && sc.UpdateStartIndex != 0;

    const double BarDateTime = sc.BaseDateTimeIn[i].GetAsDouble();

    // Fires an alert at most once per bar, per alert channel.
    // Pass AlertNumber = 0 to write to the message log without playing a sound.
    // When "Enable Alerts" is set to No this does nothing at all - no sound and
    // no log entry. If you would rather keep the log entries when alerts are
    // switched off, move the Input_EnableAlerts test out of bAlertsAllowed and
    // put it around the sc.AlertWithMessage call below instead.
    auto FireAlert = [&](int PersistentSlot, int AlertNumber, const char *Message) {
        if (!bAlertsAllowed)
            return;

        double &r_LastAlertedBarTime = sc.GetPersistentDouble(PersistentSlot);
        if (r_LastAlertedBarTime == BarDateTime)
            return; // this bar already fired this alert

        r_LastAlertedBarTime = BarDateTime;

        sc.AddMessageToLog(Message, 1);

        if (AlertNumber > 0)
            sc.AlertWithMessage(AlertNumber, Message);
    };

#pragma endregion

#pragma region INDICATORS

    // SUPER TREND
    const int ATRMultiplier = 2;
    const int ATRPeriod = 11;

    sc.TrueRange(sc.BaseDataIn, Array_TrueRange);
    sc.HullMovingAverage(Array_TrueRange, Subgraph_HullATR, ATRPeriod);
    Array_AvgTrueRange[i] = Subgraph_HullATR[i];

    Array_UpperBandBasic[i] = sc.BaseDataIn[SC_HL][i] + ATRMultiplier * Array_AvgTrueRange[i];
    Array_LowerBandBasic[i] = sc.BaseDataIn[SC_HL][i] - ATRMultiplier * Array_AvgTrueRange[i];

    if (i == 0) {
        Array_UpperBand[i] = Array_UpperBandBasic[i];
        Array_LowerBand[i] = Array_LowerBandBasic[i];
        Subgraph_SuperTrend[i] = Array_UpperBand[i];
    } else {
        if (Array_UpperBandBasic[i] < Array_UpperBand[i - 1] || sc.Close[i - 1] > Array_UpperBand[i - 1])
            Array_UpperBand[i] = Array_UpperBandBasic[i];
        else
            Array_UpperBand[i] = Array_UpperBand[i - 1];

        // FIXED: this read Array_LowerBandBasic[i - 1] where the upper band
        // branch reads [i].  Off-by-one, the lower band lagged by a bar.
        if (Array_LowerBandBasic[i] > Array_LowerBand[i - 1] || sc.Close[i - 1] < Array_LowerBand[i - 1])
            Array_LowerBand[i] = Array_LowerBandBasic[i];
        else
            Array_LowerBand[i] = Array_LowerBand[i - 1];

        if (Subgraph_SuperTrend[i - 1] == Array_UpperBand[i - 1] && sc.Close[i] < Array_UpperBand[i])
            Subgraph_SuperTrend[i] = Array_UpperBand[i];
        else if (Subgraph_SuperTrend[i - 1] == Array_UpperBand[i - 1] && sc.Close[i] > Array_UpperBand[i])
            Subgraph_SuperTrend[i] = Array_LowerBand[i];
        else if (Subgraph_SuperTrend[i - 1] == Array_LowerBand[i - 1] && sc.Close[i] > Array_LowerBand[i])
            Subgraph_SuperTrend[i] = Array_LowerBand[i];
        else if (Subgraph_SuperTrend[i - 1] == Array_LowerBand[i - 1] && sc.Close[i] < Array_LowerBand[i])
            Subgraph_SuperTrend[i] = Array_UpperBand[i];
        else
            Subgraph_SuperTrend[i] = Subgraph_SuperTrend[i - 1];
    }

    bSuperUp = (Subgraph_SuperTrend[i] != Array_UpperBand[i]);

    /*
        SQUEEZE RELAXER

        FIXED: the old code called three different moving averages into the SAME
        output subgraph.  Because ACSIL moving averages read their own previous
        output value when they run inside AutoLoop, overwriting the array with a
        different MA corrupted the next bar's recursion.  It also ran the EMA
        twice (once via sc.ExponentialMovAvg, once via sc.MovingAverage) and
        computed a linear regression whose result was never read.
    */
    sc.ExponentialMovAvg(sc.Close, Subgraph_MomentumBasis, 20);

    const float hlh = sc.GetHighest(sc.High, 20);
    const float lll = sc.GetLowest(sc.Low, 20);

    Subgraph_MomentumSource[i] = sc.Open[i] - ((hlh + lll) / 2.0f + Subgraph_MomentumBasis[i]) / 2.0f;
    sc.LinearRegressionIndicator(Subgraph_MomentumSource, Subgraph_MomentumHist, 20);

    if ((Subgraph_MomentumHist[i] <= 0) && (Subgraph_MomentumHist[i] > Subgraph_MomentumHist[i - 1])) {
        if (r_SqueezeUp == 0) {
            Subgraph_SqueezeUp[i] = sc.Low[i] - (Input_UpOffset.GetInt() * sc.TickSize);
            Subgraph_SqueezeDown[i] = 0;
            r_SqueezeUp = 1;
        }
    } else if ((Subgraph_MomentumHist[i] >= 0) && (Subgraph_MomentumHist[i] < Subgraph_MomentumHist[i - 1])) {
        if (r_SqueezeUp == 1) {
            Subgraph_SqueezeDown[i] = sc.High[i] + (Input_DownOffset.GetInt() * sc.TickSize);
            Subgraph_SqueezeUp[i] = 0;
            r_SqueezeUp = 0;
        }
    }

    // FISHER TRANSFORM
    const float Highest = sc.GetHighest(Price, 10);
    const float Lowest = sc.GetLowest(Price, 10);
    const float Range = Highest - Lowest;

    if (Range == 0)
        Array_FisherValue[i] = 0;
    else
        Array_FisherValue[i] = .66f * ((Price[i] - Lowest) / Range - 0.5f) + 0.67f * Array_FisherValue[i - 1];

    float TruncValue = Array_FisherValue[i];

    if (TruncValue > .99f)
        TruncValue = .999f;
    else if (TruncValue < -.99f)
        TruncValue = -.999f;

    // FIXED: the result used to be assigned to a plain local, so
    // Subgraph_Fisher[i - 1] was always 0 and the recursive half of the Fisher
    // Transform never happened.  Store it back into the subgraph.
    Subgraph_Fisher[i] = .5f * (log((1 + TruncValue) / (1 - TruncValue)) + Subgraph_Fisher[i - 1]);
    const float fish = Subgraph_Fisher[i];

    sc.AdaptiveMovAvg(sc.BaseDataIn[SC_LAST], Subgraph_KAMA, 9, 2, 109);
    const float kama = Subgraph_KAMA[i];

    sc.T3MovingAverage(sc.BaseDataIn[SC_LAST], Subgraph_T3, 0.84f, 10);
    const float t3 = Subgraph_T3[i];

    sc.ADX(sc.BaseDataIn, Subgraph_ADX, i, 14, 14);
    const float adx = Subgraph_ADX[i];

    sc.AwesomeOscillator(sc.BaseDataIn[SC_LAST], Subgraph_AO, 0, 0);
    const float ao = Subgraph_AO[i];

    sc.HullMovingAverage(sc.BaseDataIn[SC_LAST], Subgraph_HMA, 10);
    const float hma = Subgraph_HMA[i];

    sc.MovingAverage(sc.BaseDataIn[SC_LAST], Subgraph_Fast, MOVAVGTYPE_EXPONENTIAL, 20);
    const float a1 = Subgraph_Fast[i];
    const float b1 = Subgraph_Fast[i - 1];

    sc.MovingAverage(sc.BaseDataIn[SC_LAST], Subgraph_Slow, MOVAVGTYPE_EXPONENTIAL, 40);
    const float a2 = Subgraph_Slow[i];
    const float b2 = Subgraph_Slow[i - 1];

    sc.BollingerBands(sc.BaseDataIn[SC_LAST], Subgraph_BB, 20, 2, MOVAVGTYPE_SIMPLE);
    const float UpperBand = Subgraph_BB.Arrays[0][i];
    const float LowerBand = Subgraph_BB.Arrays[1][i];
    const float pUpperBand = Subgraph_BB.Arrays[0][i - 1];
    const float pLowerBand = Subgraph_BB.Arrays[1][i - 1];

    const float t1 = ((a1 - a2) - (b1 - b2)) * Input_WaddahIntensity.GetInt();

    sc.MACD(sc.BaseDataIn[SC_LAST], Subgraph_LindaMACD, 3, 9, 16, MOVAVGTYPE_SIMPLE);
    const float linda = Subgraph_LindaMACD.Arrays[3][i];

    sc.Parabolic(sc.BaseDataIn, sc.BaseDateTimeIn, Subgraph_Parabolic, i, 0.02f, 0.02f, 0.2f, 0, SC_HIGH, SC_LOW);
    const float SAR = Subgraph_Parabolic[i];

    sc.RSI(sc.BaseDataIn[SC_LAST], Subgraph_Calc, MOVAVGTYPE_SIMPLE, 14);
    const float rsi = Subgraph_Calc[i];
    const float prsi = Subgraph_Calc[i - 1];
    const float pprsi = Subgraph_Calc[i - 2];

#pragma endregion

    // Everything above has to run on every bar so the recursive indicators build
    // correctly.  Everything below is signalling, and needs warmed-up data.
    if (i < MinimumRequiredBars)
        return;

#pragma region PER BAR RESETS

    // FIXED: none of these were being cleared.  On the bar that is still
    // forming, a dot or a background painted on an earlier tick stayed on the
    // chart even after price moved and the condition stopped being true.
    Subgraph_DotUp[i] = 0;
    Subgraph_DotDown[i] = 0;
    Subgraph_ShavedRed[i] = 0;
    Subgraph_ShavedGreen[i] = 0;
    Subgraph_BBGreen[i] = 0;
    Subgraph_BBRed[i] = 0;
    Subgraph_3oU[i] = 0;
    Subgraph_3oD[i] = 0;
    Subgraph_EqualH[i] = 0;
    Subgraph_EqualL[i] = 0;
    Subgraph_Tramp[i] = 0;
    Subgraph_VolImbUp[i] = 0;
    Subgraph_VolImbDown[i] = 0;
    Subgraph_VolImbOriginCandle[i] = 0;
    Subgraph_VolImbDirection[i] = 0;
    Subgraph_VolImbPrice[i] = 0;

#pragma endregion

#pragma region CANDLE MARKINGS

    // FIXED: Input_ShavedBuffer existed but was never used, and the green side
    // was never implemented at all even though the subgraph is configured.
    const float ShavedBuffer = Input_ShavedBuffer.GetInt() * sc.TickSize;

    if (red && close <= low + ShavedBuffer)
        Subgraph_ShavedRed[i] = 1;

    if (green && close >= high - ShavedBuffer)
        Subgraph_ShavedGreen[i] = 1;

    if (low < LowerBand && plow < pLowerBand && green && body > pbody)
        Subgraph_BBGreen[i] = 1;

    if (high > UpperBand && phigh > pUpperBand && red && body > pbody)
        Subgraph_BBRed[i] = 1;

#pragma endregion

#pragma region PATTERNS

    const double EqualTolerance = 3.0 * sc.TickSize;

    if (IsThreeOutsideUp(sc, i)) {
        DrawTextAtBar(sc, Subgraph_3oU, "3oU", i, 0, 5, DRAWSLOT_3OU);
        Subgraph_3oU[i] = sc.Low[i];
    }

    if (IsThreeOutsideDown(sc, i)) {
        DrawTextAtBar(sc, Subgraph_3oD, "3oD", i, 0, 5, DRAWSLOT_3OD);
        Subgraph_3oD[i] = sc.Low[i];
    }

    if (IsTweezerTop(sc, i, UpperBand, EqualTolerance)) {
        DrawTextAtBar(sc, Subgraph_EqualH, "Eq Hi", i - 1, 1, 5, DRAWSLOT_EQHI);
        Subgraph_EqualH[i] = sc.Low[i];
    }

    if (IsTweezerBottom(sc, i, LowerBand, EqualTolerance)) {
        DrawTextAtBar(sc, Subgraph_EqualL, "Eq Lo", i - 1, -1, 5, DRAWSLOT_EQLO);
        Subgraph_EqualL[i] = sc.Low[i];
    }

    // FIXED: the two branches used to have their labels the wrong way round -
    // the UpperBand call (a short setup) logged "TRAMP_GREEN".
    if (IsTrampolineDown(sc, i, rsi, prsi, pprsi, UpperBand, sc.TickSize)) {
        DrawTextAtBar(sc, Subgraph_Tramp, "TR", i - 1, 1, 3, DRAWSLOT_TRAMP);
        Subgraph_Tramp[i] = sc.Low[i];
        FireAlert(PD_ALERT_TRAMP_DOWN, 0 /* log only - set to ALERT_TRAMP_DOWN to enable sound */,
                  "Trampoline DOWN");
    } else if (IsTrampolineUp(sc, i, rsi, prsi, pprsi, LowerBand, sc.TickSize)) {
        DrawTextAtBar(sc, Subgraph_Tramp, "TR", i - 1, -1, 3, DRAWSLOT_TRAMP);
        Subgraph_Tramp[i] = sc.Low[i];
        FireAlert(PD_ALERT_TRAMP_UP, 0 /* log only - set to ALERT_TRAMP_UP to enable sound */,
                  "Trampoline UP");
    }

    // KAMA BOUNCE
    if (strstr(sc.Symbol.GetChars(), "NQ") != NULL) {
        const bool bKamaWickDown = (high > kama && open < kama && close < kama);
        const bool bKamaWickUp = (low < kama && open > kama && close > kama);

        if (bKamaWickDown || bKamaWickUp)
            FireAlert(PD_ALERT_KAMA_WICK, ALERT_KAMA_WICK, "KAMA BOUNCE");
    }

#pragma endregion

#pragma region BAR COLORING

    /*
        FIXED: this used to run a full RSI calculation into Subgraph_ColorBar
        purely to make the subgraph value non-zero (DRAWSTYLE_COLOR_BAR only
        paints when the value is non-zero).  Setting it to 1 does the same job
        without the wasted calculation, and without stuffing RSI values into a
        subgraph named "Bar Color".
    */
    Subgraph_ColorBar[i] = 0;

    const int BarColorMode = Input_BarColor.GetIndex();

    if (BarColorMode != 0) {
        Subgraph_ColorBar[i] = 1;

        if (BarColorMode == 1) // waddah explosion
        {
            int Intensity = static_cast<int>(fabs(t1)) + Input_BarColorWaddah.GetInt();
            if (Intensity > 255) Intensity = 255;
            if (Intensity < 0) Intensity = 0;

            if (t1 > 0)
                Subgraph_ColorBar.DataColor[i] = RGB(0, Intensity, 0);
            else
                Subgraph_ColorBar.DataColor[i] = RGB(Intensity, 0, 0);
        } else if (BarColorMode == 2) // linda macd
        {
            // NOTE: this branch scales the intensity from t1 (the Waddah value),
            // not from `linda`.  That looks like a copy/paste from the branch
            // above, but changing it alters how your bars look, so it has been
            // left as-is.  Swap fabs(t1) for fabs(linda) if that was the intent.
            int Intensity = static_cast<int>(fabs(t1)) + Input_BarColorLinda.GetInt();
            if (Intensity > 255) Intensity = 255;
            if (Intensity < 0) Intensity = 0;

            if (linda > 0)
                Subgraph_ColorBar.DataColor[i] = RGB(0, Intensity, 0);
            else
                Subgraph_ColorBar.DataColor[i] = RGB(Intensity, 0, 0);
        } else if (BarColorMode == 3) // super trend
        {
            if (bSuperUp)
                Subgraph_ColorBar.DataColor[i] = RGB(0, 255, 0);
            else
                Subgraph_ColorBar.DataColor[i] = RGB(255, 0, 0);
        }
    }

#pragma endregion

#pragma region BUY SELL PLOTS

    /*
        FIXED: the doji filter used to `return` out of the whole study function.
        In AutoLoop that skips every remaining calculation for that bar -
        Volume Imbalance, bar colouring, the lot.  It now only does what its
        description says: suppress the buy/sell dots.
    */
    const bool bSuppressForDoji = (Input_IgnoreDoji.GetYesNo() == SC_YES && doji);

    bool bShowUp = !bSuppressForDoji;
    bool bShowDown = !bSuppressForDoji;

    if (
        (Input_UseMacd.GetYesNo() == SC_YES && linda < 0) ||
        (Input_UseSar.GetYesNo() == SC_YES && SAR > close) ||
        (Input_UseFisher.GetYesNo() == SC_YES && fish < 0) ||
        (Input_UseT3.GetYesNo() == SC_YES && t3 > close) ||
        (Input_UseWaddah.GetYesNo() == SC_YES && t1 <= 0) ||
        (Input_UseAO.GetYesNo() == SC_YES && ao < 0) ||
        (adx < Input_ADX.GetInt()) ||
        (Input_UseHMA.GetYesNo() == SC_YES && hma > close) ||
        (Input_UseSuperTrend.GetYesNo() == SC_YES && !bSuperUp))
        bShowUp = false;

    if (
        (Input_UseMacd.GetYesNo() == SC_YES && linda > 0) ||
        (Input_UseSar.GetYesNo() == SC_YES && SAR < close) ||
        (Input_UseFisher.GetYesNo() == SC_YES && fish > 0) ||
        (Input_UseT3.GetYesNo() == SC_YES && t3 < close) ||
        (Input_UseWaddah.GetYesNo() == SC_YES && t1 > 0) ||
        (Input_UseAO.GetYesNo() == SC_YES && ao > 0) ||
        (adx < Input_ADX.GetInt()) ||
        (Input_UseHMA.GetYesNo() == SC_YES && hma < close) ||
        (Input_UseSuperTrend.GetYesNo() == SC_YES && bSuperUp))
        bShowDown = false;

    if (bShowUp) {
        Subgraph_DotUp[i] = sc.Low[i] - (Input_UpOffset.GetInt() * sc.TickSize);
        FireAlert(PD_ALERT_BUY, ALERT_BUY, "BUY Signal");
    }

    if (bShowDown) {
        Subgraph_DotDown[i] = sc.High[i] + (Input_DownOffset.GetInt() * sc.TickSize);
        FireAlert(PD_ALERT_SELL, ALERT_SELL, "SELL Signal");
    }

#pragma endregion

#pragma region VOLUME IMBALANCE

    /*
        The plotting below runs for every bar so the arrows survive a redraw or
        an INS-key recalculation - that was the reason for the original comment
        about keeping this above the current-bar check.  The alerts are handled
        by FireAlert(), which is what actually restricts them to the live bar,
        so there is no longer a `return` here that can accidentally swallow the
        rest of the function.
    */

    if (IsVolImbGreen(sc, i)) {
        Subgraph_VolImbUp[i] = low - (Input_UpOffset.GetInt() * sc.TickSize);
        Subgraph_VolImbOriginCandle[i] = static_cast<float>(i);
        Subgraph_VolImbDirection[i] = 1;
        Subgraph_VolImbPrice[i] = open;

        if (bBarClosed) {
            if (plow < pLowerBand || low < LowerBand)
                FireAlert(PD_ALERT_BBGAP_GREEN, ALERT_BBGAP_GREEN, "God trade GREEN");
            else
                FireAlert(PD_ALERT_VOLIMB_GREEN, 0 /* log only - use ALERT_VOLIMB_GREEN for sound */,
                          "Green Volume Imbalance");
        }
    }

    if (IsVolImbRed(sc, i)) {
        // FIXED: was using Input_UpOffset for the down arrow.
        Subgraph_VolImbDown[i] = high + (Input_DownOffset.GetInt() * sc.TickSize);
        Subgraph_VolImbOriginCandle[i] = static_cast<float>(i);
        Subgraph_VolImbDirection[i] = -1;
        Subgraph_VolImbPrice[i] = open;

        if (bBarClosed) {
            if (phigh > pUpperBand || high > UpperBand)
                FireAlert(PD_ALERT_BBGAP_RED, ALERT_BBGAP_RED, "BBGap God Trade RED");
            else
                FireAlert(PD_ALERT_VOLIMB_RED, 0 /* log only - use ALERT_VOLIMB_RED for sound */,
                          "Red Volume Imbalance");
        }
    }

#pragma endregion

#pragma region VOLUME IMBALANCE FILL DETECTION (work in progress - disabled)

    /*
        Still disabled, as in the original.  Two things were fixed in place so
        it behaves if you switch it on:

          - the loop started at NumLines rather than NumLines - 1, so the first
            iteration always asked for an index one past the end
          - the alerts now go through FireAlert() like everything else
    */
    if (false && bBarClosed) {
        int LineIDForBar = 0;
        int StartIndex = 0;
        int endIndex = 0;
        t_ChartArrayDataType price = 0;   // must match sc.GetStudyLineUntilFutureIntersectionByIndex (double in current builds)

        const int NumLines = sc.GetNumLinesUntilFutureIntersection(sc.ChartNumber, sc.StudyGraphInstanceID);

        for (int LineIndex = NumLines - 1; LineIndex >= 0; LineIndex--) {
            if (!sc.GetStudyLineUntilFutureIntersectionByIndex(
                sc.ChartNumber,
                sc.StudyGraphInstanceID,
                LineIndex,
                LineIDForBar,
                StartIndex,
                price,
                endIndex))
                continue;

            const int iUpDown = static_cast<int>(Subgraph_VolImbDirection[StartIndex]);

            // GREEN volimb interception
            if ((low < price || (high > price && open < price && close < price)) && iUpDown == 1) {
                if (((i - StartIndex) > 2) && ((i - StartIndex) < 30)) {
                    txt.Format("gVB intercept: o %d c %d price %f low %f", StartIndex, i, price, low);
                    FireAlert(PD_ALERT_VOLIMB_FILL, ALERT_VOLIMB_FILL, txt.GetChars());
                }
                break;
            }

            // RED volimb interception
            if ((high > price || (low < price && close > price && open > price)) && iUpDown == -1) {
                if (((i - StartIndex) > 2) && ((i - StartIndex) < 30)) {
                    txt.Format("rVB intercept: o %d c %d price %f high %f", StartIndex, i, price, high);
                    FireAlert(PD_ALERT_VOLIMB_FILL, ALERT_VOLIMB_FILL, txt.GetChars());
                }
                break;
            }
        }
    }

#pragma endregion
}
