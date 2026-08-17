#include "sierrachart.h"

SCDLLName("God Trades DLL")

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
    return BodyLength(InData, index) * percent / 100.0;
}

double UpperWickLength(SCBaseDataRef InData, int index) {
    double upperBoundary = max(InData[SC_LAST][index], InData[SC_OPEN][index]);
    double upperWickLength = InData[SC_HIGH][index] - upperBoundary;
    return upperWickLength;
}

double LowerWickLength(SCBaseDataRef InData, int index) {
    double lowerBoundary = min(InData[SC_LAST][index], InData[SC_OPEN][index]);
    double lowerWickLength = lowerBoundary - InData[SC_LOW][index];
    return lowerWickLength;
}

bool IsGreen(SCBaseDataRef InData, int index) {
    return InData[SC_LAST][index] > InData[SC_OPEN][index];
}

bool IsRed(SCBaseDataRef InData, int index) {
    return InData[SC_LAST][index] < InData[SC_OPEN][index];
}

bool IsNearEqual(double value1, double value2, SCBaseDataRef InData, int index, double percent) {
    return abs(value1 - value2) < (3 * percent); // PercentOfCandleLength(InData, index, percent);
}

bool IsUpperWickSmall(SCBaseDataRef InData, int index, double percent) {
    return UpperWickLength(InData, index) < PercentOfCandleLength(InData, index, percent);
}

bool IsLowerWickSmall(SCBaseDataRef InData, int index, double percent) {
    return LowerWickLength(InData, index) < PercentOfCandleLength(InData, index, percent);
}

bool IsBullishEngulfing(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    if (InData[SC_LAST][index - 1] < InData[SC_OPEN][index - 1] && InData[SC_LAST][index] > InData[SC_OPEN][index]) {
        if ((InData[SC_HIGH][index] > InData[SC_HIGH][index - 1]) &&
            (InData[SC_LOW][index] < InData[SC_LOW][index - 1]) &&
            (InData[SC_LAST][index] > InData[SC_OPEN][index - 1]) &&
            (InData[SC_OPEN][index] < InData[SC_LAST][index - 1]))
            ret_flag = true;
    }
    return ret_flag;
}

bool IsBearishEngulfing(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    if (InData[SC_LAST][index - 1] > InData[SC_OPEN][index - 1] && InData[SC_LAST][index] < InData[SC_OPEN][index]) {
        if ((InData[SC_HIGH][index] > InData[SC_HIGH][index - 1]) &&
            (InData[SC_LOW][index] < InData[SC_LOW][index - 1]) &&
            (InData[SC_OPEN][index] > InData[SC_LAST][index - 1]) &&
            (InData[SC_LAST][index] < InData[SC_OPEN][index - 1]))
            ret_flag = true;
    }

    return ret_flag;
}

bool IsThreeOutsideUp(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    if (IsGreen(InData, index) && (InData[SC_LAST][index] > InData[SC_LAST][index - 1])) {
        if (IsBullishEngulfing(sc, index - 1))
            ret_flag = true;
    }

    return ret_flag;
}

bool IsThreeOutsideDown(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    if (IsRed(InData, index) && (InData[SC_LAST][index] < InData[SC_LAST][index - 1])) {
        if (IsBearishEngulfing(sc, index - 1))
            ret_flag = true;
    }

    return ret_flag;
}

const int k_Body_NUM_OF_CANDLES = 5; // number of previous candles to calculate body strength

inline bool IsBodyStrong(SCBaseDataRef InData, int index) {
    bool ret_flag = false;
    auto mov_aver{0};
    for (int i = 1; i < k_Body_NUM_OF_CANDLES + 1; i++)
        mov_aver += static_cast<float>(BodyLength(InData, index - i));
    mov_aver /= k_Body_NUM_OF_CANDLES;

    if (BodyLength(InData, index) > mov_aver)
        ret_flag = true;
    return ret_flag;
}

bool IsTweezerTop(SCStudyInterfaceRef sc, int index, float UpperBand) {
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;
    if (IsNearEqual(InData[SC_OPEN][index - 1], InData[SC_LAST][index - 2], InData, index, sc.TickSize) && InData[
            SC_LOW][index] < InData[SC_LOW][index - 1] // current bar lower than previous
        && IsRed(InData, index) && IsRed(InData, index - 1) && IsGreen(InData, index - 2) && IsGreen(InData, index - 3)
        && (InData[SC_HIGH][index - 1] > UpperBand || InData[SC_HIGH][index - 2] > UpperBand))
        ret_flag = true;

    return ret_flag;
}

bool IsTweezerBottom(SCStudyInterfaceRef sc, int index, float LowerBand) {
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    if (IsNearEqual(InData[SC_OPEN][index - 1], InData[SC_LAST][index - 2], InData, index, sc.TickSize) && InData[
            SC_HIGH][index] > InData[SC_HIGH][index - 1] && IsGreen(InData, index) && IsGreen(InData, index - 1) &&
        IsRed(InData, index - 2) && IsRed(InData, index - 3) && (
            InData[SC_LOW][index - 1] < LowerBand || InData[SC_LOW][index - 2] < LowerBand))
        ret_flag = true;

    return ret_flag;
}

bool IsTrampoline(SCStudyInterfaceRef sc, int index, float rsi, float prsi, float pprsi, float BBBand, int iTickSize) {
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    if (IsRed(InData, index) && IsRed(InData, index - 1) && IsGreen(InData, index - 2) && InData[SC_LAST][index] <
        InData[SC_LAST][index - 1] && (rsi > 80 || prsi > 80 || pprsi > 80) && InData[SC_HIGH][index - 2] >= (
            BBBand - (1 * iTickSize)))
        ret_flag = true;

    if (IsGreen(InData, index) && IsGreen(InData, index - 1) && IsRed(InData, index - 2) && InData[SC_LAST][index] >
        InData[SC_LAST][index - 1] && (rsi < 20 || prsi < 20 || pprsi < 20) && InData[SC_LOW][index - 2] <= (
            BBBand + (1 * iTickSize)))
        ret_flag = true;

    return ret_flag;
}

bool IsStairs(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    return ret_flag;
}

bool IsVolImbGreen(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    if (IsGreen(InData, index) && IsGreen(InData, index - 1) && InData[SC_OPEN][index] > InData[SC_LAST][index - 1])
        ret_flag = true;

    return ret_flag;
}

bool IsVolImbRed(SCStudyInterfaceRef sc, int index) {
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    if (IsRed(InData, index) && IsRed(InData, index - 1) && InData[SC_OPEN][index] < InData[SC_LAST][index - 1])
        ret_flag = true;

    return ret_flag;
}

bool IsDoji(SCBaseDataRef InData, int index) {
    bool ret_flag = false;

    if (IsRed(InData, index)) {
        if (abs(InData[SC_HIGH][index] - InData[SC_OPEN][index]) > abs(InData[SC_OPEN][index] - InData[SC_LAST][index])
            &&
            abs(InData[SC_LAST][index] - InData[SC_LOW][index]) > abs(InData[SC_OPEN][index] - InData[SC_LAST][index]))
            ret_flag = true;
    } else if (IsGreen(InData, index)) {
        if (abs(InData[SC_HIGH][index] - InData[SC_LAST][index]) > abs(InData[SC_OPEN][index] - InData[SC_LAST][index])
            &&
            abs(InData[SC_OPEN][index] - InData[SC_LOW][index]) > abs(InData[SC_OPEN][index] - InData[SC_LAST][index]))
            ret_flag = true;
    }

    return ret_flag;
}

void DrawText(SCStudyInterfaceRef sc, SCSubgraphRef screffy, SCString txt, int iAboveCandle, int iBuffer) {
    s_UseTool Tool;
    int i = sc.Index;

    if (txt == "Eq Lo" || txt == "Eq Hi" || txt == "TR")
        i = sc.Index - 1;

    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_TEXT;
    Tool.BeginIndex = sc.CurrentIndex;
    Tool.Region = sc.GraphRegion;

    if (iAboveCandle == 0) // automatic detection
    {
        if (sc.Close[i] > sc.Open[i]) // green candle
            iAboveCandle = 1;
        else
            iAboveCandle = -1;
    }

    if (iAboveCandle == 1) {
        Tool.BeginValue = sc.High[i] + ((sc.High[i] - sc.Low[i]) * iBuffer * 0.01f);
        Tool.TextAlignment = DT_CENTER | DT_BOTTOM;
    } else if (iAboveCandle == -1) {
        Tool.BeginValue = sc.Low[i] - ((sc.High[i] - sc.Low[i]) * iBuffer * 0.01f);
        Tool.TextAlignment = DT_CENTER | DT_TOP;
    }

    Tool.Color = screffy.PrimaryColor;
    Tool.FontBackColor = screffy.SecondaryColor;
    if (txt == "TR")
        Tool.FontBackColor = COLOR_RED;
    Tool.FontSize = screffy.LineWidth;
    Tool.FontBold = TRUE;
    Tool.Text.Format("%s", txt.GetChars());
    Tool.AddMethod = UTAM_ADD_ALWAYS;

    sc.UseTool(Tool);
}

#pragma endregion

SCSFExport scsf_GodTrades(SCStudyInterfaceRef sc) {
#pragma region INPUTS

    SCString txt;

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
    SCSubgraphRef Subgraph_MomentumHistUpColors = sc.Subgraph[29];
    SCSubgraphRef Subgraph_MomentumHistDownColors = sc.Subgraph[30];
    SCSubgraphRef Subgraph_SuperTrend = sc.Subgraph[31];
    SCSubgraphRef Subgraph_Intersection = sc.Subgraph[32];
    SCSubgraphRef Subgraph_BBGreen = sc.Subgraph[33];
    SCSubgraphRef Subgraph_BBRed = sc.Subgraph[34];
    SCSubgraphRef Subgraph_ShavedGreen = sc.Subgraph[35];
    SCSubgraphRef Subgraph_ShavedRed = sc.Subgraph[36];
    SCSubgraphRef Subgraph_DojiCity = sc.Subgraph[37];
    SCSubgraphRef Subgraph_HullATR = sc.Subgraph[38];
    // Volume Imbalance tracking
    SCSubgraphRef Subgraph_VolImbOriginCandle = sc.Subgraph[43];
    SCSubgraphRef Subgraph_VolImbDirection = sc.Subgraph[41];
    SCSubgraphRef Subgraph_VolImbPrice = sc.Subgraph[42];

    SCFloatArrayRef Array_TrueRange = Subgraph_SuperTrend.Arrays[0];
    SCFloatArrayRef Array_AvgTrueRange = Subgraph_SuperTrend.Arrays[1];
    SCFloatArrayRef Array_UpperBandBasic = Subgraph_SuperTrend.Arrays[2];
    SCFloatArrayRef Array_LowerBandBasic = Subgraph_SuperTrend.Arrays[3];
    SCFloatArrayRef Array_UpperBand = Subgraph_SuperTrend.Arrays[4];
    SCFloatArrayRef Array_LowerBand = Subgraph_SuperTrend.Arrays[5];

    COLORREF UpColor = Subgraph_ColorUp.PrimaryColor;
    COLORREF DownColor = Subgraph_ColorDown.PrimaryColor;

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

        Input_ShavedBuffer.Name = "Shaved candle buffer";
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
        Input_IgnoreDoji.SetDescription("Ignore when wicks are larger than candle body");

        Input_DojiCity.Name = "Show Doji Cities";
        Input_DojiCity.SetYesNo(1);

        // =======================================================================

        Subgraph_ColorBar.Name = "Bar Color";
        Subgraph_ColorBar.DrawStyle = DRAWSTYLE_COLOR_BAR;
        Subgraph_ColorBar.LineWidth = 1;

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

        Subgraph_3oU.Name = "Three Outside Up";
        Subgraph_3oU.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
        Subgraph_3oU.PrimaryColor = RGB(255, 255, 29);
        Subgraph_3oU.SecondaryColor = RGB(1, 97, 3);
        Subgraph_3oU.LineWidth = 8;

        Subgraph_3oD.Name = "Three Outside Down";
        Subgraph_3oD.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
        Subgraph_3oD.PrimaryColor = RGB(255, 255, 29);
        Subgraph_3oD.SecondaryColor = RGB(97, 1, 1);
        Subgraph_3oD.LineWidth = 8;

        Subgraph_EqualH.Name = "Equal High";
        Subgraph_EqualH.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
        Subgraph_EqualH.PrimaryColor = RGB(255, 255, 29);
        Subgraph_EqualH.SecondaryColor = RGB(97, 1, 1);
        Subgraph_EqualH.LineWidth = 8;

        Subgraph_EqualL.Name = "Equal Low";
        Subgraph_EqualL.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
        Subgraph_EqualL.PrimaryColor = RGB(255, 255, 29);
        Subgraph_EqualL.SecondaryColor = RGB(1, 97, 3);
        Subgraph_EqualL.LineWidth = 8;

        Subgraph_Tramp.Name = "Trampoline";
        Subgraph_Tramp.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
        Subgraph_Tramp.PrimaryColor = RGB(0, 0, 0);
        Subgraph_Tramp.SecondaryColor = RGB(169, 205, 252);
        Subgraph_Tramp.LineWidth = 8;

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

        Subgraph_Calc.Name = "RSI";
        Subgraph_Calc.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_MomentumHist.Name = "Squeeze Relaxer 1";
        Subgraph_MomentumHist.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_MomentumHistUpColors.Name = "Squeeze Relaxer 2";
        Subgraph_MomentumHistUpColors.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_MomentumHistDownColors.Name = "Squeeze Relaxer 3";
        Subgraph_MomentumHistDownColors.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_BB.Name = "Bollinger Bands";
        Subgraph_BB.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_AO.Name = "Awesome Oscillator";
        Subgraph_AO.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_Fisher.Name = "Awesome Oscillator";
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
        Subgraph_VolImbPrice.Name = "VolImb Price";
        Subgraph_VolImbPrice.DrawStyle = DRAWSTYLE_IGNORE;

        return;
    }

#pragma endregion

#pragma region LOCAL VARIABLES

    int ALERT_EMA21_WICK = 12;
    int ALERT_KAMA = 13;
    int ALERT_KAMA_WICK = 13;

    int ALERT_BUY = 5;
    int ALERT_SELL = 6;
    int ALERT_BBGAP_GREEN = 21;
    int ALERT_BBGAP_RED = 22;
    int ALERT_VOLIMB_GREEN = 17;
    int ALERT_VOLIMB_RED = 17;
    int ALERT_VOLIMB_FILL = 19;
    int ALERT_TRAMP_GREEN = 23;
    int ALERT_TRAMP_RED = 24;
    if (strstr(sc.Symbol.GetChars(), "NQ") != NULL) {
        ALERT_BUY = 7;
        ALERT_SELL = 8;
        ALERT_BBGAP_GREEN = 21;
        ALERT_BBGAP_RED = 22;
        ALERT_VOLIMB_GREEN = 18;
        ALERT_VOLIMB_RED = 18;
        ALERT_VOLIMB_FILL = 20;
        ALERT_TRAMP_GREEN = 23;
        ALERT_TRAMP_RED = 24;
    } else if (strstr(sc.Symbol.GetChars(), "ES") != NULL) {
    } else {
    }

    const int i = sc.Index;
    auto &r_SqueezeUp{sc.GetPersistentInt(0)};
    auto cl{sc.GetBarHasClosedStatus(i)}; // BHCS_BAR_HAS_NOT_CLOSED
    SCBaseDataRef in = sc.BaseData;
    auto close{sc.Close[i]};
    auto open{sc.Open[i]};
    auto high{sc.High[i]};
    auto low{sc.Low[i]};
    auto pclose{sc.Close[i - 1]};
    auto popen{sc.Open[i - 1]};
    auto phigh{sc.High[i - 1]};
    auto plow{sc.Low[i - 1]};
    auto body{abs(open - close)};
    auto pbody{abs(popen - pclose)};
    bool red = open > close;
    bool green = open < close;
    bool pdoji = false;
    auto upperwick{0};
    auto lowerwick{0};
    if (green) {
        upperwick = abs(high - close);
        lowerwick = abs(open - low);
        pdoji = abs(phigh - pclose) > pbody && abs(popen - plow) > body;
    } else // red
    {
        upperwick = abs(high - open);
        lowerwick = abs(close - low);
        pdoji = abs(phigh - popen) > pbody && abs(pclose - plow) > body;
    }
    bool doji = upperwick > body && lowerwick > body;
    SCFloatArrayRef Price = sc.BaseData[SC_HL_AVG];
    SCFloatArrayRef Array_Value = Subgraph_Calc.Arrays[0];
    auto &PreviousLineCount = sc.GetPersistentInt(1);
    auto CurrentLineCount = sc.GetNumLinesUntilFutureIntersection(sc.ChartNumber, sc.StudyGraphInstanceID);
    static const int NotYetInitialized = -1;
    auto &IsInitialized = sc.GetPersistentInt(2);
    auto AlreadyAlertedBar{0};
    // int X = sc.BarIndexToXPixelCoordinate(i);
    // int Y = sc.BarIndexToRelativeHorizontalCoordinate(i, false);
    bool bIsCurrentBar = false;
    bool sqRelaxUp;
    bool bSuperUp;

    if (i == sc.ArraySize - 2)
        bIsCurrentBar = true;

#pragma endregion

#pragma region INDICATORS

    //	for (int i = sc.UpdateStartIndex; i < sc.ArraySize; i++)
    {
        // SUPER TREND
        int ATRMultiplier = 2;
        int ATRPeriod = 11;

        sc.TrueRange(sc.BaseDataIn, Array_TrueRange);
        sc.HullMovingAverage(Array_TrueRange, Subgraph_HullATR, ATRPeriod);
        Array_AvgTrueRange[i] = Subgraph_HullATR[i];

        Array_UpperBandBasic[i] = sc.BaseDataIn[SC_HL][i] + ATRMultiplier * Array_AvgTrueRange[i];
        Array_LowerBandBasic[i] = sc.BaseDataIn[SC_HL][i] - ATRMultiplier * Array_AvgTrueRange[i];

        // Calculate Upper and Lower Bands
        if (Array_UpperBandBasic[i] < Array_UpperBand[i - 1] || sc.Close[i - 1] > Array_UpperBand[i - 1])
            Array_UpperBand[i] = Array_UpperBandBasic[i];
        else
            Array_UpperBand[i] = Array_UpperBand[i - 1];

        if (Array_LowerBandBasic[i] > Array_LowerBand[i - 1] || sc.Close[i - 1] < Array_LowerBand[i - 1])
            Array_LowerBand[i] = Array_LowerBandBasic[i - 1];
        else
            Array_LowerBand[i] = Array_LowerBand[i - 1];

        if (i == 0)
            Subgraph_SuperTrend[i] = Array_UpperBand[i];

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

        if (Subgraph_SuperTrend[i] == Array_UpperBand[i])
            bSuperUp = false;
        else
            bSuperUp = true;

        // SQUEEZE RELAXER
        sc.DataStartIndex = 20;
        sc.ExponentialMovAvg(sc.Close, Subgraph_MomentumHistUpColors, 20);
        // Note: EMA returns close when index is < HistogramLenSecondData.GetInt()
        sc.MovingAverage(sc.Close, Subgraph_MomentumHistUpColors, MOVAVGTYPE_EXPONENTIAL, 20);

        float hlh = sc.GetHighest(sc.High, 20);
        float lll = sc.GetLowest(sc.Low, 20);

        Subgraph_MomentumHistDownColors[sc.Index] =
                sc.Open[sc.Index] - ((hlh + lll) / 2.0f + Subgraph_MomentumHistUpColors[sc.Index]) / 2.0f;
        sc.LinearRegressionIndicator(Subgraph_MomentumHistDownColors, Subgraph_MomentumHist, 20);
        sc.MovingAverage(sc.Close, Subgraph_MomentumHistUpColors, MOVAVGTYPE_LINEARREGRESSION, 20);

        if ((Subgraph_MomentumHist[i] <= 0) && (Subgraph_MomentumHist[i] > Subgraph_MomentumHist[i - 1])) {
            if (r_SqueezeUp == 0) {
                Subgraph_SqueezeUp[i] = sc.Low[i] - ((Input_UpOffset.GetInt()) * sc.TickSize);
                Subgraph_SqueezeDown[i] = 0;
                r_SqueezeUp = 1;
            }
        } else if ((Subgraph_MomentumHist[i] >= 0) && (Subgraph_MomentumHist[i] < Subgraph_MomentumHist[i - 1])) {
            if (r_SqueezeUp == 1) {
                Subgraph_SqueezeDown[i] = sc.High[i] + ((Input_DownOffset.GetInt()) * sc.TickSize);
                Subgraph_SqueezeUp[i] = 0;
                r_SqueezeUp = 0;
            }
        }

        // FISHER TRANSFORM
        float Highest = sc.GetHighest(Price, 10);
        float Lowest = sc.GetLowest(Price, 10);
        float Range = Highest - Lowest;

        if (Range == 0)
            Array_Value[i] = 0;
        else
            Array_Value[i] = .66f * ((Price[i] - Lowest) / Range - 0.5f) + 0.67f * Array_Value[i - 1];

        float TruncValue = Array_Value[i];

        if (TruncValue > .99f)
            TruncValue = .999f;
        else if (TruncValue < -.99f)
            TruncValue = -.999f;

        float fish = .5f * (log((1 + TruncValue) / (1 - TruncValue)) + Subgraph_Fisher[i - 1]);

        // int data_type_indx = (int)Input_InputData.GetInputDataIndex();
        // SCFloatArrayRef in = sc.BaseDataIn[data_type_indx];
        // SCDateTimeArrayRef ti = sc.BaseDateTimeIn[data_type_indx];

        sc.AdaptiveMovAvg(sc.BaseDataIn[SC_LAST], Subgraph_KAMA, 9, 2, 109);
        float kama = Subgraph_KAMA[i];

        sc.T3MovingAverage(sc.BaseDataIn[SC_LAST], Subgraph_T3, 0.84, 10);
        float t3 = Subgraph_T3[i];

        sc.ADX(sc.BaseDataIn, Subgraph_ADX, i, 14, 14);
        float adx = Subgraph_ADX[i];

        sc.AwesomeOscillator(sc.BaseDataIn[SC_LAST], Subgraph_AO, 0, 0);
        float ao = Subgraph_AO[i];

        sc.HullMovingAverage(sc.BaseDataIn[SC_LAST], Subgraph_HMA, 10);
        float hma = Subgraph_HMA[i];
        float phma = Subgraph_HMA[i - 1];

        sc.MovingAverage(sc.BaseDataIn[SC_LAST], Subgraph_Fast, MOVAVGTYPE_EXPONENTIAL, 20);
        float a1 = Subgraph_Fast[i];
        float b1 = Subgraph_Fast[i - 1];

        sc.MovingAverage(sc.BaseDataIn[SC_LAST], Subgraph_Slow, MOVAVGTYPE_EXPONENTIAL, 40);
        float a2 = Subgraph_Slow[i];
        float b2 = Subgraph_Slow[i - 1];

        sc.BollingerBands(sc.BaseDataIn[SC_LAST], Subgraph_BB, 20, 2, MOVAVGTYPE_SIMPLE);
        float UpperBand = Subgraph_BB.Arrays[0][i];
        float LowerBand = Subgraph_BB.Arrays[1][i];
        float e1 = UpperBand - LowerBand;

        float t1 = ((a1 - a2) - (b1 - b2)) * Input_WaddahIntensity.GetInt();

        sc.MACD(sc.BaseDataIn[SC_LAST], Subgraph_LindaMACD, 3, 9, 16, MOVAVGTYPE_SIMPLE);
        float linda = Subgraph_LindaMACD.Arrays[3][i];

        sc.Parabolic(sc.BaseDataIn, sc.BaseDateTimeIn, Subgraph_Parabolic, i, 0.02f, 0.02f, 0.2f, 0, SC_HIGH, SC_LOW);
        float SAR = Subgraph_Parabolic[i];

        sc.RSI(sc.BaseDataIn[SC_LAST], Subgraph_Calc, MOVAVGTYPE_SIMPLE, 14);
        float rsi = Subgraph_Calc[i];
        float prsi = Subgraph_Calc[i - 1];
        float pprsi = Subgraph_Calc[i - 2];

#pragma endregion

        SCFloatArray SubgraphArray;
        sc.GetStudyArray(11, 3, SubgraphArray);
        //if (SubgraphArray.GetArraySize() == 0)

        if (Input_IgnoreDoji.GetYesNo() == SC_YES && doji)
            return;

        if (red && close == low)
            Subgraph_ShavedRed[i] = 1;
        if (low < LowerBand && plow < LowerBand && green && body > pbody)
            Subgraph_BBGreen[i] = 1;
        if (high > UpperBand && phigh > UpperBand && red && body > pbody)
            Subgraph_BBRed[i] = 1;

        static bool dataLoaded = false;
        bool bBarClosed = (sc.GetBarHasClosedStatus() == BHCS_BAR_HAS_CLOSED);

#pragma region EXTRA ALERTS
        /*
                if (false) {
                    // Detect volimb line close
                    auto iEndings{0};
                    const int32_t numLines = sc.GetNumLinesUntilFutureIntersection(sc.ChartNumber, sc.StudyGraphInstanceID) - 1;
                    if (numLines != 0) {
                        for (int32_t lineIndex = numLines; lineIndex >= 0; --lineIndex) {
                            int32_t lineID{0};
                            int32_t startIndex{0};
                            int32_t endIndex{0};
                            float lineValue{0.0f};

                            if (sc.GetStudyLineUntilFutureIntersectionByIndex(sc.ChartNumber, sc.StudyGraphInstanceID, lineIndex, lineID, startIndex, lineValue, endIndex))
                            {
                                if (endIndex > 0) {
                                    iEndings++;
                                    DrawText(sc, Subgraph_3oU, "shit", 0, 5);
                                    //sc.DeleteLineUntilFutureIntersection(startIndex, lineID);
                                    Subgraph_Intersection[i] = endIndex;
                                }
                            }
                        }
                    }
                    Subgraph_Intersection[i] = iEndings;

                    if (bBarClosed) {
                        std::stringstream message;
                        message << "New bar closed - "
                                << "Symbol: " << sc.Symbol.GetChars() << ", "
                                << "Close: " << sc.Close[sc.Index] << ", "
                                << "Volume: " << sc.Volume[sc.Index];
                    }
                }
        */
        if (Subgraph_Intersection[i] > Subgraph_Intersection[i - 1] &&
            !IsVolImbGreen(sc, i - 1) && !IsVolImbGreen(sc, i - 2) &&
            !IsVolImbRed(sc, i - 1) && !IsVolImbRed(sc, i - 2)) {
            DrawText(sc, Subgraph_3oU, "FILL", 0, 5); // D2
        }

        if (IsThreeOutsideUp(sc, i) && bIsCurrentBar) {
            DrawText(sc, Subgraph_3oU, "3oU", 0, 5);
            Subgraph_3oU[i] = sc.Low[i];
        }

        if (IsThreeOutsideDown(sc, i) && bIsCurrentBar) {
            DrawText(sc, Subgraph_3oD, "3oD", 0, 5);
            Subgraph_3oD[i] = sc.Low[i];
        }

        if (IsTweezerTop(sc, i, UpperBand)) {
            DrawText(sc, Subgraph_EqualH, "Eq Hi", 1, 5);
            Subgraph_EqualH[i] = sc.Low[i];
        }

        if (IsTweezerBottom(sc, i, LowerBand)) {
            DrawText(sc, Subgraph_EqualL, "Eq Lo", -1, 5);
            Subgraph_EqualL[i] = sc.Low[i];
        }

        if (IsTrampoline(sc, i, rsi, prsi, pprsi, UpperBand, sc.TickSize)) {
            DrawText(sc, Subgraph_Tramp, "TR", -1, 3);
            Subgraph_Tramp[i] = sc.Low[i];
            if (bIsCurrentBar) {
                sc.AddMessageToLog(txt.Format("ALERT_TRAMP_GREEN"), 1);
                //sc.AlertWithMessage(ALERT_TRAMP_GREEN, "");
            }
        } else if (IsTrampoline(sc, i, rsi, prsi, pprsi, LowerBand, sc.TickSize)) {
            DrawText(sc, Subgraph_Tramp, "TR", 1, 3);
            Subgraph_Tramp[i] = sc.Low[i];
            if (bIsCurrentBar) {
                sc.AddMessageToLog(txt.Format("ALERT_TRAMP_RED"), 1);
                //sc.AlertWithMessage(ALERT_TRAMP_RED, "");
            }
        }
        // KAMA BOUNCE
        if (bIsCurrentBar && bBarClosed && strstr(sc.Symbol.GetChars(), "NQ") != NULL) {
            if (high > kama && open < kama && close < kama) {
                sc.AddMessageToLog(txt.Format("KAMA wick"), 1);
                sc.AlertWithMessage(ALERT_KAMA_WICK, "KAMA BOUNCE");
            }
            if (low < kama && open > kama && close > kama) {
                sc.AddMessageToLog(txt.Format("KAMA wick"), 1);
                sc.AlertWithMessage(ALERT_KAMA_WICK, "KAMA BOUNCE");
            }
        }
#pragma endregion

#pragma region BAR COLORING
        if (Input_BarColor.GetIndex() != 0)
            sc.RSI(sc.BaseDataIn[SC_LAST], Subgraph_ColorBar, MOVAVGTYPE_SIMPLE, 14);

        int iRedGreenColor = 255;
        if (Input_BarColor.GetIndex() == 1) // waddah explosion
        {
            iRedGreenColor = min(255, abs(t1) + Input_BarColorWaddah.GetInt());
            if (t1 > 0)
                Subgraph_ColorBar.DataColor[i] = RGB(0, iRedGreenColor, 0);
            else
                Subgraph_ColorBar.DataColor[i] = RGB(iRedGreenColor, 0, 0);
        } else if (Input_BarColor.GetIndex() == 2) // linda macd
        {
            iRedGreenColor = min(255, abs(t1) + Input_BarColorLinda.GetInt());
            if (linda > 0)
                Subgraph_ColorBar.DataColor[i] = RGB(0, iRedGreenColor, 0);
            else
                Subgraph_ColorBar.DataColor[i] = RGB(iRedGreenColor, 0, 0);
        } else if (Input_BarColor.GetIndex() == 3) // super trend
        {
            if (bSuperUp)
                Subgraph_ColorBar.DataColor[i] = RGB(0, 255, 0);
            else
                Subgraph_ColorBar.DataColor[i] = RGB(255, 0, 0);
        }
#pragma endregion

#pragma region BUY SELL PLOTS
        bool bShowUp = true;
        bool bShowDown = true;

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
            Subgraph_DotUp[i] = sc.Low[i] - ((Input_UpOffset.GetInt()) * sc.TickSize);
            if (bIsCurrentBar) {
                sc.AlertWithMessage(ALERT_BUY, "BUY Signal");
                sc.AddMessageToLog(txt.Format("BUY Signal"), 1);
            }
        }

        if (bShowDown) {
            Subgraph_DotDown[i] = sc.High[i] + ((Input_DownOffset.GetInt()) * sc.TickSize);
            if (bIsCurrentBar) {
                sc.AlertWithMessage(ALERT_SELL, "SELL Signal");
                sc.AddMessageToLog(txt.Format("SELL Signal"), 1);
            }
        }

#pragma endregion

        if (IsVolImbGreen(sc, sc.CurrentIndex))
            sc.AddLineUntilFutureIntersection(i, i, open, RGB(255, 255, 255), 2, LINESTYLE_SOLID, false, false, "");

        if (IsVolImbRed(sc, sc.CurrentIndex))
            sc.AddLineUntilFutureIntersection(i, i, open, RGB(255, 255, 255), 2, LINESTYLE_SOLID, false, false, "");

        if (!bIsCurrentBar)
            return;

#pragma region VOLUME IMBALANCE
        //sc.AddMessageToLog(txt.Format("NEW BAR"), 1);
        //sc.AddMessageToLog(txt.Format("Bar closed, i = %d Array-2 = %d", i, sc.ArraySize-2), 1);

        // CHECK FOR VOLIMB FINISHES
        if (bBarClosed) {
            //sc.AddMessageToLog(txt.Format("Checking NumLines status"), 1);
            auto LineIDForBar{0};
            auto StartIndex{0};
            auto LineValue{0.0f};
            auto endIndex{0};
            int NumLines = sc.GetNumLinesUntilFutureIntersection(sc.ChartNumber, sc.StudyGraphInstanceID);
            for (NumLines; NumLines >= 0; NumLines--) {
                sc.GetStudyLineUntilFutureIntersectionByIndex(
                    sc.ChartNumber,
                    sc.StudyGraphInstanceID,
                    NumLines,
                    LineIDForBar,
                    StartIndex,
                    LineValue,
                    endIndex);

                // GREEN volimb interception
                if (low < LineValue && Subgraph_VolImbDirection[StartIndex] == 1) {
                    Subgraph_VolImbDirection[StartIndex] = 0;
                    if (((i - StartIndex) > 2) && ((i - StartIndex) < 30)) {
                        txt.Format("gVB intercept: o %d c %d price %f low %f", StartIndex, i, LineValue, low);
                        sc.AddMessageToLog(txt, 1);
                        sc.AlertWithMessage(ALERT_VOLIMB_FILL, "");
                    }
                    break;
                }

                // RED volimb interception
                if (high > LineValue && Subgraph_VolImbDirection[StartIndex] == -1) {
                    Subgraph_VolImbDirection[StartIndex] = 0;
                    if (((i - StartIndex) > 2) && ((i - StartIndex) < 30)) {
                            txt.Format("rVB intercept: o %d c %d price %f high %f", StartIndex, i, LineValue, high);
                            sc.AddMessageToLog(txt, 1);
                            sc.AlertWithMessage(ALERT_VOLIMB_FILL, "");
                    }
                    break;
                }
            }
        }
        /*
                if (bBarClosed && !IsVolImbGreen(sc, sc.CurrentIndex) && !IsVolImbRed(sc, sc.CurrentIndex) && sc.IsNewBar(i))
                for (int iW = sc.ArraySize - 1; iW >= 0; iW--)
                {
                    if (Subgraph_VolImbDirection[iW] != 0 && Subgraph_VolImbPrice[iW] != 0) {
                        // GREEN volimb interception
                        if (low < Subgraph_VolImbPrice[iW] && Subgraph_VolImbDirection[iW] == 1) {
                            Subgraph_VolImbDirection[iW] = 0;
                            txt.Format("VB intercept: origin %d current %d price %f low %f", Subgraph_VolImbOriginCandle[iW], iW, Subgraph_VolImbPrice[iW], low);
                            sc.AddMessageToLog(txt, 0);
                            break;
                        }
                        // RED volimb interception
                        if (high > Subgraph_VolImbPrice[iW] && Subgraph_VolImbDirection[iW] == -1) {
                            Subgraph_VolImbDirection[iW] = 0;
                            txt.Format("VB intercept: origin %d current %d price %f low %f", Subgraph_VolImbOriginCandle[iW], iW, Subgraph_VolImbPrice[iW], high);
                            sc.AddMessageToLog(txt, 0);
                            break;
                        }
                    }
                }
        */
        Subgraph_VolImbUp[i] = 0;
        Subgraph_VolImbDown[i] = 0;
        Subgraph_VolImbOriginCandle[i] = 0;
        Subgraph_VolImbDirection[i] = 0;
        Subgraph_VolImbPrice[i] = 0;

        if (IsVolImbGreen(sc, sc.CurrentIndex)) {
            //sc.AddLineUntilFutureIntersection(i, i, open, RGB(255, 255, 255), 2, LINESTYLE_SOLID, false, false, "");
            Subgraph_VolImbUp[i] = low - ((Input_UpOffset.GetInt()) * sc.TickSize);
            Subgraph_VolImbOriginCandle[i] = sc.CurrentIndex;
            Subgraph_VolImbDirection[i] = 1;
            Subgraph_VolImbPrice[i] = open;

            // Send alerts
            //txt.Format("Volume Imbalance at %d, count %d on %s", sc.CurrentIndex, CurrentLineCount, sc.Symbol.GetChars());
            //sc.AddMessageToLog(txt, 0);
            if (bBarClosed) {
                // Is BB gap god trade
                if (plow < LowerBand || low < LowerBand) {
                    sc.AddMessageToLog(txt.Format("God trade GREEN at %d", sc.CurrentIndex), 1);
                    sc.AlertWithMessage(ALERT_BBGAP_GREEN, "Green Volume Imbalance");
                }

                //if (strstr(sc.Symbol.GetChars(), "NQ") != NULL)
                sc.AddMessageToLog(txt.Format("Green Volume Imbalance at %d", sc.CurrentIndex), 1);
                //sc.AlertWithMessage(ALERT_VOLIMB_GREEN, "Green Volume Imbalance");
                //else if (strstr(sc.Symbol.GetChars(), "ES") != NULL)
                //     sc.AlertWithMessage(ALERT_VOLIMB_GREEN, "Green Volume Imbalance ES");
            }
        }

        if (IsVolImbRed(sc, sc.CurrentIndex)) {
            //sc.AddLineUntilFutureIntersection(i, i, open, RGB(255, 255, 255), 2, LINESTYLE_SOLID, false, false, "");
            Subgraph_VolImbDown[i] = high + ((Input_UpOffset.GetInt()) * sc.TickSize);
            Subgraph_VolImbOriginCandle[i] = sc.CurrentIndex;
            Subgraph_VolImbDirection[i] = -1;
            Subgraph_VolImbPrice[i] = open;

            // Send alerts
            //txt.Format("Volume Imbalance at %d, count %d on %s", sc.CurrentIndex, CurrentLineCount, sc.Symbol.GetChars());
            //sc.AddMessageToLog(txt.Format(txt", sc.CurrentIndex, CurrentLineCount, sc.Symbol.GetChars(), 0);
            if (bBarClosed) {
                // Is BB gap god trade?
                if (phigh > UpperBand || high > UpperBand) {
                    sc.AddMessageToLog(txt.Format("BBGap God Trade RED at %d", sc.CurrentIndex), 1);
                    sc.AlertWithMessage(ALERT_BBGAP_RED, "BBGap God Trade");
                } else {
                    sc.AddMessageToLog(txt.Format("Red Volume Imbalance at %d", sc.CurrentIndex), 1);
                    //sc.AlertWithMessage(ALERT_VOLIMB_RED, "Red Volume Imbalance");
                }
                //if (strstr(sc.Symbol.GetChars(), "NQ") != NULL)
                //sc.AlertWithMessage(ALERT_VOLIMB_RED, "Red Volume Imbalance");
                //else if (strstr(sc.Symbol.GetChars(), "ES") != NULL)
                //    sc.AlertWithMessage(ALERT_VOLIMB_RED, "Red Volume Imbalance ES");
            }
        }

        /*
        // sc.StudyGraphInstanceID identifies this study instance
        auto LineIDForBar{0};
        auto StartIndex{0};
        auto LineValue{0.0f};
        auto endIndex{0};

        auto NumLines = sc.GetNumLinesUntilFutureIntersection(sc.ChartNumber, sc.StudyGraphInstanceID);
        for (NumLines; NumLines >= 0; NumLines--) {
            break;
            sc.GetStudyLineUntilFutureIntersectionByIndex(
                sc.ChartNumber,
                sc.StudyGraphInstanceID,
                NumLines,
                LineIDForBar,
                StartIndex,
                LineValue,
                endIndex);

            if (endIndex == 0) {
                if (AlreadyAlertedBar != i && sc.IsNewBar(i)) {
                    SCString Message;
                    Message.Format("Line ID %d (started at bar %d, value %.5f) intersected on current bar %d",LineIDForBar, StartIndex, LineValue, endIndex
                    );
                    sc.AddMessageToLog(Message, 0);
                    sc.AlertWithMessage(1, Message);
                    AlreadyAlertedBar = i;
                }
            } // endIndex
        } // Numlines loop
        */
    }

#pragma endregion
}
