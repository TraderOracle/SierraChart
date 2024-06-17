#include "sierrachart.h" 

SCDLLName("Trader Oracle DLL") 

#pragma region COMMON FUNCTIONS

double CandleLength(SCBaseDataRef InData, int index)
{
	return InData[SC_HIGH][index] - InData[SC_LOW][index];
}

double BodyLength(SCBaseDataRef InData, int index)
{
	return fabs(InData[SC_OPEN][index] - InData[SC_LAST][index]);
}

double PercentOfCandleLength(SCBaseDataRef InData, int index, double percent)
{
	return CandleLength(InData, index) * (percent / 100.0);
}

double PercentOfBodyLength(SCBaseDataRef InData, int index, double percent)
{
	return BodyLength(InData, index) * percent / 100.0;
}

double UpperWickLength(SCBaseDataRef InData, int index)
{
	double upperBoundary = max(InData[SC_LAST][index], InData[SC_OPEN][index]);
	double upperWickLength = InData[SC_HIGH][index] - upperBoundary;
	return upperWickLength;
}

double LowerWickLength(SCBaseDataRef InData, int index)
{
	double lowerBoundary = min(InData[SC_LAST][index], InData[SC_OPEN][index]);
	double lowerWickLength = lowerBoundary - InData[SC_LOW][index];
	return lowerWickLength;
}

bool IsGreen(SCBaseDataRef InData, int index)
{
	return InData[SC_LAST][index] > InData[SC_OPEN][index];
}

bool IsRed(SCBaseDataRef InData, int index)
{
	return InData[SC_LAST][index] < InData[SC_OPEN][index];
}

bool IsNearEqual(double value1, double value2, SCBaseDataRef InData, int index, double percent)
{
	return abs(value1 - value2) < (3 * percent); //PercentOfCandleLength(InData, index, percent);
}

bool IsUpperWickSmall(SCBaseDataRef InData, int index, double percent)
{
	return UpperWickLength(InData, index) < PercentOfCandleLength(InData, index, percent);
}

bool IsLowerWickSmall(SCBaseDataRef InData, int index, double percent)
{
	return LowerWickLength(InData, index) < PercentOfCandleLength(InData, index, percent);
}

bool IsBullishEngulfing(SCStudyInterfaceRef sc, int index)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;

	if (InData[SC_LAST][index - 1] < InData[SC_OPEN][index - 1] && InData[SC_LAST][index] > InData[SC_OPEN][index])
	{
		if ((InData[SC_HIGH][index] > InData[SC_HIGH][index - 1]) &&
			(InData[SC_LOW][index] < InData[SC_LOW][index - 1]) &&
			(InData[SC_LAST][index] > InData[SC_OPEN][index - 1]) &&
			(InData[SC_OPEN][index] < InData[SC_LAST][index - 1]))
			ret_flag = true;
	}
	return ret_flag;
}

bool IsBearishEngulfing(SCStudyInterfaceRef sc, int index)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;

	if (InData[SC_LAST][index - 1] > InData[SC_OPEN][index - 1] && InData[SC_LAST][index] < InData[SC_OPEN][index])
	{
		if ((InData[SC_HIGH][index] > InData[SC_HIGH][index - 1]) &&
			(InData[SC_LOW][index] < InData[SC_LOW][index - 1]) &&
			(InData[SC_OPEN][index] > InData[SC_LAST][index - 1]) &&
			(InData[SC_LAST][index] < InData[SC_OPEN][index - 1]))
			ret_flag = true;
	}

	return ret_flag;
}

bool IsThreeOutsideUp(SCStudyInterfaceRef sc, int index)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;

	if (IsGreen(InData, index) && (InData[SC_LAST][index] > InData[SC_LAST][index - 1]))
	{
		if (IsBullishEngulfing(sc, index - 1))
			ret_flag = true;
	}

	return ret_flag;
}

bool IsThreeOutsideDown(SCStudyInterfaceRef sc, int index)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;

	if (IsRed(InData, index) && (InData[SC_LAST][index] < InData[SC_LAST][index - 1]))
	{
		if (IsBearishEngulfing(sc, index - 1))
			ret_flag = true;
	}

	return ret_flag;
}

const int k_Body_NUM_OF_CANDLES = 5;				// number of previous candles to calculate body strength

inline bool IsBodyStrong(SCBaseDataRef InData, int index)
{
	bool ret_flag = false;
	float mov_aver = 0;
	for (int i = 1; i < k_Body_NUM_OF_CANDLES + 1; i++)
		mov_aver += static_cast<float>(BodyLength(InData, index - i));
	mov_aver /= k_Body_NUM_OF_CANDLES;

	if (BodyLength(InData, index) > mov_aver)
		ret_flag = true;
	return ret_flag;
}

bool IsTweezerTop(SCStudyInterfaceRef sc, int index, float UpperBand)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;
	if (IsNearEqual(InData[SC_OPEN][index - 1], InData[SC_LAST][index - 2], InData, index, sc.TickSize)
		&& InData[SC_LOW][index] < InData[SC_LOW][index - 1] // current bar lower than previous
		&& IsRed(InData, index)
		&& IsRed(InData, index - 1)
		&& IsGreen(InData, index - 2)
		&& IsGreen(InData, index - 3)
		&& (InData[SC_HIGH][index - 1] > UpperBand || InData[SC_HIGH][index - 2] > UpperBand)
		)
		ret_flag = true;

	return ret_flag;
}

bool IsTweezerBottom(SCStudyInterfaceRef sc, int index, float LowerBand)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;

	if (IsNearEqual(InData[SC_OPEN][index - 1], InData[SC_LAST][index - 2], InData, index, sc.TickSize)
		&& InData[SC_HIGH][index] > InData[SC_HIGH][index - 1]
		&& IsGreen(InData, index)
		&& IsGreen(InData, index - 1)
		&& IsRed(InData, index - 2)
		&& IsRed(InData, index - 3)
		&& (InData[SC_LOW][index - 1] < LowerBand || InData[SC_LOW][index - 2] < LowerBand)
		)
		ret_flag = true;

	return ret_flag;
}

bool IsTrampoline(SCStudyInterfaceRef sc, int index, float rsi, float prsi, float pprsi, float BBBand, int iTickSize)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;

	if (IsRed(InData, index)
		&& IsRed(InData, index - 1)
		&& IsGreen(InData, index - 2)
		&& InData[SC_LAST][index] < InData[SC_LAST][index - 1]
		&& (rsi > 80 || prsi > 80 || pprsi > 80)
		&& InData[SC_HIGH][index - 2] >= (BBBand - (1 * iTickSize))
		)
		ret_flag = true;

	if (IsGreen(InData, index)
		&& IsGreen(InData, index - 1)
		&& IsRed(InData, index - 2)
		&& InData[SC_LAST][index] > InData[SC_LAST][index - 1]
		&& (rsi < 20 || prsi < 20 || pprsi < 20)
		&& InData[SC_LOW][index - 2] <= (BBBand + (1 * iTickSize))
		)
		ret_flag = true;

	return ret_flag;
}

bool IsStairs(SCStudyInterfaceRef sc, int index)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;

	return ret_flag;
}

bool IsVolImbGreen(SCStudyInterfaceRef sc, int index)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;
	
	if (IsGreen(InData, index)
		&& IsGreen(InData, index - 1)
		&& InData[SC_OPEN][index] > InData[SC_LAST][index - 1]
		)
		ret_flag = true;

	return ret_flag;
}

bool IsVolImbRed(SCStudyInterfaceRef sc, int index)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;

	if (IsRed(InData, index)
		&& IsRed(InData, index - 1)
		&& InData[SC_OPEN][index] < InData[SC_LAST][index - 1]
		)
		ret_flag = true;

	return ret_flag;
}

bool IsDoji(SCBaseDataRef InData, int index)
{
	bool ret_flag = false;

	if (IsRed(InData, index))
	{
		if (abs(InData[SC_HIGH][index] - InData[SC_OPEN][index]) > abs(InData[SC_OPEN][index] - InData[SC_LAST][index]) &&
		  abs(InData[SC_LAST][index] - InData[SC_LOW][index]) > abs(InData[SC_OPEN][index] - InData[SC_LAST][index]))
			ret_flag = true;
	}
	else if (IsGreen(InData, index))
	{
		if (abs(InData[SC_HIGH][index] - InData[SC_LAST][index]) > abs(InData[SC_OPEN][index] - InData[SC_LAST][index]) &&
			abs(InData[SC_OPEN][index] - InData[SC_LOW][index]) > abs(InData[SC_OPEN][index] - InData[SC_LAST][index]))
			ret_flag = true;
	}

	return ret_flag;
}

void DrawText(SCStudyInterfaceRef sc, SCSubgraphRef screffy, SCString txt, int iAboveCandle, int iBuffer)
{
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

	if (iAboveCandle == 1)
	{
		Tool.BeginValue = sc.High[i] + ((sc.High[i] - sc.Low[i]) * iBuffer * 0.01f);
		Tool.TextAlignment = DT_CENTER | DT_BOTTOM;
	}
	else if(iAboveCandle == -1)
	{
		Tool.BeginValue = sc.Low[i] - ((sc.High[i] - sc.Low[i]) * iBuffer * 0.01f);
		Tool.TextAlignment = DT_CENTER | DT_TOP;
	}

	Tool.Color = screffy.PrimaryColor;
	Tool.FontBackColor = screffy.SecondaryColor;
	Tool.FontSize = screffy.LineWidth;
	Tool.FontBold = TRUE;
	Tool.Text.Format("%s", txt.GetChars());
	Tool.AddMethod = UTAM_ADD_ALWAYS;

	sc.UseTool(Tool);
}

#pragma endregion

#pragma region DELTA INTENSITY

SCSFExport scsf_Delta_Intensity(SCStudyInterfaceRef sc)
{
	int i = sc.Index;

	SCInputRef Input_Threshold = sc.Input[0];
	SCInputRef Input_InputDataLow = sc.Input[1];

	SCSubgraphRef Subgraph_GreenBar = sc.Subgraph[0];
	SCSubgraphRef Subgraph_IntenseGreenBar = sc.Subgraph[1];
	SCSubgraphRef Subgraph_RedBar = sc.Subgraph[2];
	SCSubgraphRef Subgraph_IntenseRedBar = sc.Subgraph[3];
	SCSubgraphRef Subgraph_Winner = sc.Subgraph[4];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Delta Intensity";
		sc.GraphRegion = 1;
		sc.AutoLoop = 1;

		Input_Threshold.Name = "Bright Color Threshold";
		Input_Threshold.SetInt(450);

		Subgraph_GreenBar.Name = "Green Bar";
		Subgraph_GreenBar.PrimaryColor = RGB(0, 140, 0);
		Subgraph_GreenBar.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_GreenBar.LineWidth = 17;

		Subgraph_RedBar.Name = "Red Bar";
		Subgraph_RedBar.PrimaryColor = RGB(140, 0, 0);
		Subgraph_RedBar.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_RedBar.LineWidth = 17;

		Subgraph_IntenseGreenBar.Name = "Intense Green Bar";
		Subgraph_IntenseGreenBar.PrimaryColor = RGB(0, 255, 0);
		Subgraph_IntenseGreenBar.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_IntenseGreenBar.LineWidth = 17;

		Subgraph_IntenseRedBar.Name = "Intense Red Bar";
		Subgraph_IntenseRedBar.PrimaryColor = RGB(255, 0, 0);
		Subgraph_IntenseRedBar.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_IntenseRedBar.LineWidth = 17;

		Subgraph_Winner.Name = "Green or Red Dominant";
		Subgraph_Winner.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
		Subgraph_Winner.PrimaryColor = RGB(255, 255, 255);
		Subgraph_Winner.SecondaryColorUsed = true;
		Subgraph_Winner.SecondaryColor = RGB(0, 167, 0);
		Subgraph_Winner.LineWidth = 10;
		Subgraph_Winner.DrawZeros = false;

		return;
	}

	double green = sc.BidVolume[i];
	double red = sc.AskVolume[i];

	float volsec = 1;
	float minDelta = sc.BaseData[SC_ASKBID_DIFF_LOW][sc.Index];
	float maxDelta = sc.BaseData[SC_ASKBID_DIFF_HIGH][sc.Index];

	//SCDateTime BarEndDateTime = sc.GetEndingDateTimeForBarIndex(sc.Index);
	//float bartime = static_cast<float>((BarEndDateTime - sc.BaseDateTimeIn[sc.Index] + SCDateTime::MICROSECONDS(1)).GetAsDouble());
	//if (bartime > 0)
	//	volsec = (sc.Volume[i] / bartime);

	float delta = sc.AskVolume[i] - sc.BidVolume[i];
	float deltaPer = delta > 0 ? (delta / maxDelta) : (delta / minDelta);
	float deltaIntense = abs((delta * deltaPer) * volsec);

	if (deltaIntense > Input_Threshold.GetInt())
	{
		if (delta > 0)
		{
			Subgraph_IntenseGreenBar[i] = deltaIntense;
			Subgraph_GreenBar[i] = 0;
		}
		else
		{
			Subgraph_IntenseRedBar[i] = deltaIntense;
			Subgraph_RedBar[i] = 0;
		}
	}
	else
	{
		if (delta > 0)
		{
			Subgraph_IntenseGreenBar[i] = 0;
			Subgraph_GreenBar[i] = deltaIntense;
		}
		else
		{
			Subgraph_IntenseRedBar[i] = 0;
			Subgraph_RedBar[i] = deltaIntense;
		}
	}

	SCString t = "";

	if (IsRed(sc.BaseData, i) && delta < 0)
	{
		Subgraph_Winner.SecondaryColor = RGB(0, 0, 0);
		Subgraph_Winner.PrimaryColor = RGB(0, 0, 0);
		sc.AddAndManageSingleTextUserDrawnDrawingForStudy(sc, true, 90, 90, Subgraph_Winner, false, t, true, 1);
	}
	if (IsGreen(sc.BaseData, i) && delta > 0)
	{
		Subgraph_Winner.SecondaryColor = RGB(0, 0, 0);
		Subgraph_Winner.PrimaryColor = RGB(0, 0, 0);
		sc.AddAndManageSingleTextUserDrawnDrawingForStudy(sc, true, 90, 90, Subgraph_Winner, false, t, true, 1);
	}

	if (IsRed(sc.BaseData, i) && delta > 0)
	{
		t = "Delta Divergence";
		Subgraph_Winner.SecondaryColor = RGB(167, 0, 0);
		Subgraph_Winner.PrimaryColor = RGB(255, 255, 255);
		sc.AddAndManageSingleTextUserDrawnDrawingForStudy(sc, true, 90, 90, Subgraph_Winner, false, t, true, 1);
	}
	if (IsGreen(sc.BaseData, i) && delta < 0)
	{
		t = "Delta Divergence";
		Subgraph_Winner.SecondaryColor = RGB(0, 167, 0);
		Subgraph_Winner.PrimaryColor = RGB(255, 255, 255);
		sc.AddAndManageSingleTextUserDrawnDrawingForStudy(sc, true, 90, 90, Subgraph_Winner, false, t, true, 1);
	}

}

#pragma endregion

#pragma region OLYMPUS

SCSFExport scsf_Olympus(SCStudyInterfaceRef sc)
{

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

	if (sc.SetDefaults)
	{
		sc.GraphName = "Olympus";
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

		Subgraph_Intersection.Name = "Vol Imb Intersections";
		Subgraph_Intersection.DrawStyle = DRAWSTYLE_IGNORE;

		return;
	}

#pragma endregion

	int i = sc.Index;
	int& r_SqueezeUp = sc.GetPersistentInt(0);
	int cl = sc.GetBarHasClosedStatus(i); // BHCS_BAR_HAS_NOT_CLOSED
	SCBaseDataRef in = sc.BaseData;
	double close = sc.Close[i];
	double open = sc.Open[i];
	double high = sc.High[i];
	double low = sc.Low[i];
	double pclose = sc.Close[i - 1];
	double popen = sc.Open[i - 1];
	double phigh = sc.High[i - 1];
	double plow = sc.Low[i - 1];
	double body = abs(open - close);
	double pbody = abs(popen - pclose);
	bool red = open > close;
	bool green = open < close;
	bool pdoji = false;
	double upperwick = 0;
	double lowerwick = 0;
	if (green)
	{
		upperwick = abs(high - close);
		lowerwick = abs(open - low);
		pdoji = abs(phigh - pclose) > pbody && abs(popen - plow) > body;
	}
	else // red
	{
		upperwick = abs(high - open);
		lowerwick = abs(close - low);
		pdoji = abs(phigh - popen) > pbody && abs(pclose - plow) > body;
	}
	bool doji = upperwick > body && lowerwick > body;
	SCFloatArrayRef Price = sc.BaseData[SC_HL_AVG];
	SCFloatArrayRef Array_Value = Subgraph_Calc.Arrays[0];
	//int X = sc.BarIndexToXPixelCoordinate(i);
	//int Y = sc.BarIndexToRelativeHorizontalCoordinate(i, false);

#pragma region INDICATORS

	//	for (int i = sc.UpdateStartIndex; i < sc.ArraySize; i++)
	{
		bool BarCloseStatus = false;
		bool sqRelaxUp;
		bool bSuperUp;

		if (i < sc.ArraySize - 1)
			BarCloseStatus = true;

		// SUPER TREND
		int ATRMultiplier = 2;
		int ATRPeriod = 11;

		sc.TrueRange(sc.BaseDataIn, Array_TrueRange);
		sc.HullMovingAverage(Array_TrueRange, Subgraph_HullATR, ATRPeriod);
		Array_AvgTrueRange[sc.Index] = Subgraph_HullATR[sc.Index];

		Array_UpperBandBasic[sc.Index] = sc.BaseDataIn[SC_HL][sc.Index] + ATRMultiplier * Array_AvgTrueRange[sc.Index];
		Array_LowerBandBasic[sc.Index] = sc.BaseDataIn[SC_HL][sc.Index] - ATRMultiplier * Array_AvgTrueRange[sc.Index];

		// Calculate Upper and Lower Bands
		if (Array_UpperBandBasic[sc.Index] < Array_UpperBand[sc.Index - 1] || sc.Close[sc.Index - 1] > Array_UpperBand[sc.Index - 1])
			Array_UpperBand[sc.Index] = Array_UpperBandBasic[sc.Index];
		else
			Array_UpperBand[sc.Index] = Array_UpperBand[sc.Index - 1];

		if (Array_LowerBandBasic[sc.Index] > Array_LowerBand[sc.Index - 1] || sc.Close[sc.Index - 1] < Array_LowerBand[sc.Index - 1])
			Array_LowerBand[sc.Index] = Array_LowerBandBasic[sc.Index - 1];
		else
			Array_LowerBand[sc.Index] = Array_LowerBand[sc.Index - 1];

		if (sc.Index == 0)
			Subgraph_SuperTrend[sc.Index] = Array_UpperBand[sc.Index];

		if (Subgraph_SuperTrend[sc.Index - 1] == Array_UpperBand[sc.Index - 1] && sc.Close[sc.Index] < Array_UpperBand[sc.Index])
			Subgraph_SuperTrend[sc.Index] = Array_UpperBand[sc.Index];
		else if (Subgraph_SuperTrend[sc.Index - 1] == Array_UpperBand[sc.Index - 1] && sc.Close[sc.Index] > Array_UpperBand[sc.Index])
			Subgraph_SuperTrend[sc.Index] = Array_LowerBand[sc.Index];
		else if (Subgraph_SuperTrend[sc.Index - 1] == Array_LowerBand[sc.Index - 1] && sc.Close[sc.Index] > Array_LowerBand[sc.Index])
			Subgraph_SuperTrend[sc.Index] = Array_LowerBand[sc.Index];
		else if (Subgraph_SuperTrend[sc.Index - 1] == Array_LowerBand[sc.Index - 1] && sc.Close[sc.Index] < Array_LowerBand[sc.Index])
			Subgraph_SuperTrend[sc.Index] = Array_UpperBand[sc.Index];
		else
			Subgraph_SuperTrend[sc.Index] = Subgraph_SuperTrend[sc.Index - 1];

		if (Subgraph_SuperTrend[sc.Index] == Array_UpperBand[sc.Index])
			bSuperUp = false;
		else
			bSuperUp = true;


		// SQUEEZE RELAXER
		sc.DataStartIndex = 20;
		sc.ExponentialMovAvg(sc.Close, Subgraph_MomentumHistUpColors, 20);  // Note: EMA returns close when index is < HistogramLenSecondData.GetInt()
		sc.MovingAverage(sc.Close, Subgraph_MomentumHistUpColors, MOVAVGTYPE_EXPONENTIAL, 20);

		float hlh = sc.GetHighest(sc.High, 20);
		float lll = sc.GetLowest(sc.Low, 20);

		Subgraph_MomentumHistDownColors[sc.Index] = sc.Open[sc.Index] - ((hlh + lll) / 2.0f + Subgraph_MomentumHistUpColors[sc.Index]) / 2.0f;
		sc.LinearRegressionIndicator(Subgraph_MomentumHistDownColors, Subgraph_MomentumHist, 20);
		sc.MovingAverage(sc.Close, Subgraph_MomentumHistUpColors, MOVAVGTYPE_LINEARREGRESSION, 20);

		if ((Subgraph_MomentumHist[i] <= 0) && (Subgraph_MomentumHist[i] > Subgraph_MomentumHist[i - 1]))
		{
			if (r_SqueezeUp == 0) {
				Subgraph_SqueezeUp[i] = sc.Low[i] - ((Input_UpOffset.GetInt()) * sc.TickSize);
				Subgraph_SqueezeDown[i] = 0;
				r_SqueezeUp = 1;
			}
		}
		else if ((Subgraph_MomentumHist[i] >= 0) && (Subgraph_MomentumHist[i] < Subgraph_MomentumHist[i - 1]))
		{
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

		//int data_type_indx = (int)Input_InputData.GetInputDataIndex();
		//SCFloatArrayRef in = sc.BaseDataIn[data_type_indx];
		//SCDateTimeArrayRef ti = sc.BaseDateTimeIn[data_type_indx];

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
		if (SubgraphArray.GetArraySize() == 0)
		{
			return; // The SubgraphArray may not exist or is empty. Either way we can not do anything with it.
		}

		double dTickie = Input_ShavedBuffer.GetInt() * sc.TickSize;
		if (Input_IgnoreDoji.GetYesNo() == SC_YES && doji)
			return;

		if (green && high == close)
		{
			Subgraph_ShavedGreen[i] = 1;
			Subgraph_ColorBar.DataColor[i] = RGB(170, 221, 255);
		}
			
		if (red && close == low)
			Subgraph_ShavedRed[i] = 1;

		if (low < LowerBand && green && high > phigh && close > pclose && pclose < popen)
			Subgraph_BBGreen[i] = 1;

		if (high > UpperBand && red && low < plow && close < pclose && pclose < popen)
			Subgraph_BBRed[i] = 1;

#pragma region BUY SELL PLOTS

		if (BarCloseStatus)
		{
			int iEndings = 0;
			const int32_t numLines = sc.GetNumLinesUntilFutureIntersection(sc.ChartNumber, sc.StudyGraphInstanceID) - 1;
			if (numLines != 0)
			{
				for (int32_t lineIndex = numLines; lineIndex >= 0; --lineIndex)
				{
					int32_t lineID{ 0 };
					int32_t startIndex{ 0 };
					int32_t endIndex{ 0 };
					float lineValue{ 0.0f };

					if (sc.GetStudyLineUntilFutureIntersectionByIndex(sc.ChartNumber, sc.StudyGraphInstanceID, lineIndex, lineID, startIndex, lineValue, endIndex))
					{
						if (endIndex > 0)
						{
							iEndings++;
							//DrawText(sc, Subgraph_3oU, "shit", 0, 5);
							//sc.DeleteLineUntilFutureIntersection(startIndex, lineID);
							//Subgraph_Intersection[i] = endIndex;
						}
					}
				}
			}
			Subgraph_Intersection[i] = iEndings;
		}

		if (Subgraph_Intersection[i] > Subgraph_Intersection[i - 1] &&
			!IsVolImbGreen(sc, i-1) && !IsVolImbGreen(sc, i - 2) &&
			!IsVolImbRed(sc, i - 1) && !IsVolImbRed(sc, i - 2))
		{
			//DrawText(sc, Subgraph_3oU, "FILL", 0, 5);
		}

		if (IsThreeOutsideUp(sc, i) && BarCloseStatus)
		{
			DrawText(sc, Subgraph_3oU, "3oU", 0, 5);
			Subgraph_3oU[i] = sc.Low[i];
		}

		if (IsThreeOutsideDown(sc, i) && BarCloseStatus)
		{
			DrawText(sc, Subgraph_3oD, "3oD", 0, 5);
			Subgraph_3oD[i] = sc.Low[i];
		}

		if (IsTweezerTop(sc, i, UpperBand))
		{
			DrawText(sc, Subgraph_EqualH, "Eq Hi", 1, 5);
			Subgraph_EqualH[i] = sc.Low[i];
		}

		if (IsTweezerBottom(sc, i, LowerBand))
		{
			DrawText(sc, Subgraph_EqualL, "Eq Lo", -1, 5);
			Subgraph_EqualL[i] = sc.Low[i];
		}

		if (IsTrampoline(sc, i, rsi, prsi, pprsi, UpperBand, sc.TickSize))
		{
			DrawText(sc, Subgraph_Tramp, "TR", -1, 3);
			Subgraph_Tramp[i] = sc.Low[i];
		}
		else if (IsTrampoline(sc, i, rsi, prsi, pprsi, LowerBand, sc.TickSize))
		{
			DrawText(sc, Subgraph_Tramp, "TR", 1, 3);
			Subgraph_Tramp[i] = sc.Low[i];
		}

		if (Input_DojiCity.GetYesNo() == SC_YES && doji && pdoji)
		{
			DrawText(sc, Subgraph_Tramp, "AAAAA", 1, 3);
			Subgraph_Tramp[i] = sc.Low[i];

			DrawText(sc, Subgraph_DojiCity, "Doji City", 1, 3);
			Subgraph_DojiCity[i] = sc.Low[i];
		}

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
		}
		else if (Input_BarColor.GetIndex() == 2) // linda macd
		{
			iRedGreenColor = min(255, abs(t1) + Input_BarColorLinda.GetInt());
			if (linda > 0)
				Subgraph_ColorBar.DataColor[i] = RGB(0, iRedGreenColor, 0);
			else
				Subgraph_ColorBar.DataColor[i] = RGB(iRedGreenColor, 0, 0);
		}
		else if (Input_BarColor.GetIndex() == 3) // super trend
		{
			if (bSuperUp)
				Subgraph_ColorBar.DataColor[i] = RGB(0, 255, 0);
			else
				Subgraph_ColorBar.DataColor[i] = RGB(255, 0, 0);
		}

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
			(Input_UseSuperTrend.GetYesNo() == SC_YES && !bSuperUp)
			)
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
			(Input_UseSuperTrend.GetYesNo() == SC_YES && bSuperUp)
			)
			bShowDown = false;

		if (BarCloseStatus && bShowUp)
		{
			Subgraph_DotUp[i] = sc.Low[i] - ((Input_UpOffset.GetInt()) * sc.TickSize);
			txt.Format("Olympus BUY Signal at %.2d", close);
			//sc.AddMessageToLog(txt, 0);
			if (i >= sc.ArraySize - 1)
				sc.AlertWithMessage(199, "Olympus BUY Signal");
		}

		if (BarCloseStatus && bShowDown)
		{
			Subgraph_DotDown[i] = sc.High[i] + ((Input_DownOffset.GetInt()) * sc.TickSize);
			txt.Format("Olympus SELL Signal at %.2d", close);
			if (sc.IsNewBar(i))
				sc.AlertWithMessage(200, "Olympus SELL Signal");
		}

		Subgraph_VolImbUp[i] = 0;
		Subgraph_VolImbDown[i] = 0;

		if (BarCloseStatus && IsVolImbGreen(sc, sc.CurrentIndex))
		{
			sc.AddLineUntilFutureIntersection(i, i, open, RGB(106, 149, 247), 4, LINESTYLE_SOLID, false, false, "");
			Subgraph_VolImbUp[i] = low - ((Input_UpOffset.GetInt()) * sc.TickSize);
			txt.Format("Volume Imbalance BUY at %.2d", close);
			if (sc.IsNewBar(i))
				sc.AlertWithMessage(197, "Volume Imbalance BUY");
		}

		if (BarCloseStatus && IsVolImbRed(sc, sc.CurrentIndex))
		{
			sc.AddLineUntilFutureIntersection(i, i, open, RGB(106, 149, 247), 4, LINESTYLE_SOLID, false, false, "");
			Subgraph_VolImbDown[i] = high + ((Input_UpOffset.GetInt()) * sc.TickSize);
			txt.Format("Volume Imbalance SELL at %.2d", close);
			//sc.AddMessageToLog(txt, 0);
			if (sc.IsNewBar(i))
				sc.AlertWithMessage(198, "Volume Imbalance SELL");
		}


	}

#pragma endregion

	//STARTUPINFO si;
	//PROCESS_INFORMATION pi;

	//CreateProcess("notepad.exe",   // No module name (use command line)
	//	NULL,        // Command line
	//	NULL,           // Process handle not inheritable
	//	NULL,           // Thread handle not inheritable
	//	FALSE,          // Set handle inheritance to FALSE
	//	0,              // No creation flags
	//	NULL,           // Use parent's environment block
	//	NULL,           // Use parent's starting directory 
	//	&si,            // Pointer to STARTUPINFO structure
	//	&pi);

}

#pragma endregion

#pragma region OLYMPUSOLD

SCSFExport scsf_OlympusOLD(SCStudyInterfaceRef sc)
{

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

	SCSubgraphRef Subgraph_HullATR = sc.Subgraph[37];

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

	if (sc.SetDefaults)
	{
		sc.GraphName = "Olympus OLD";
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
		Subgraph_ShavedGreen.DrawStyle = DRAWSTYLE_COLOR_BAR_CANDLE_FILL;
		Subgraph_ShavedGreen.LineWidth = 1;
		Subgraph_ShavedGreen.DrawZeros = false;

		Subgraph_ShavedRed.Name = "Shaved Red Candle";
		Subgraph_ShavedRed.PrimaryColor = RGB(255, 128, 192);
		Subgraph_ShavedRed.DrawStyle = DRAWSTYLE_COLOR_BAR_CANDLE_FILL;
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

		Subgraph_Intersection.Name = "Vol Imb Intersections";
		Subgraph_Intersection.DrawStyle = DRAWSTYLE_IGNORE;

		return;
	}

	int i = sc.Index;
	int& r_SqueezeUp = sc.GetPersistentInt(0);
	int cl = sc.GetBarHasClosedStatus(i); // BHCS_BAR_HAS_NOT_CLOSED
	SCBaseDataRef in = sc.BaseData;
	double close = in[SC_LAST][i];
	SCFloatArrayRef Price = sc.BaseData[SC_HL_AVG];
	SCFloatArrayRef Array_Value = Subgraph_Calc.Arrays[0];
	//int X = sc.BarIndexToXPixelCoordinate(i);
	//int Y = sc.BarIndexToRelativeHorizontalCoordinate(i, false);

#pragma endregion

#pragma region INDICATORS

	//	for (int i = sc.UpdateStartIndex; i < sc.ArraySize; i++)
	{
		bool BarCloseStatus = false;
		bool sqRelaxUp;
		bool bSuperUp;

		if (i < sc.ArraySize - 1)
			BarCloseStatus = true;

		// SUPER TREND
		int ATRMultiplier = 2;
		int ATRPeriod = 11;

		sc.TrueRange(sc.BaseDataIn, Array_TrueRange);
		sc.HullMovingAverage(Array_TrueRange, Subgraph_HullATR, ATRPeriod);
		Array_AvgTrueRange[sc.Index] = Subgraph_HullATR[sc.Index];

		Array_UpperBandBasic[sc.Index] = sc.BaseDataIn[SC_HL][sc.Index] + ATRMultiplier * Array_AvgTrueRange[sc.Index];
		Array_LowerBandBasic[sc.Index] = sc.BaseDataIn[SC_HL][sc.Index] - ATRMultiplier * Array_AvgTrueRange[sc.Index];

		// Calculate Upper and Lower Bands
		if (Array_UpperBandBasic[sc.Index] < Array_UpperBand[sc.Index - 1] || sc.Close[sc.Index - 1] > Array_UpperBand[sc.Index - 1])
			Array_UpperBand[sc.Index] = Array_UpperBandBasic[sc.Index];
		else
			Array_UpperBand[sc.Index] = Array_UpperBand[sc.Index - 1];

		if (Array_LowerBandBasic[sc.Index] > Array_LowerBand[sc.Index - 1] || sc.Close[sc.Index - 1] < Array_LowerBand[sc.Index - 1])
			Array_LowerBand[sc.Index] = Array_LowerBandBasic[sc.Index - 1];
		else
			Array_LowerBand[sc.Index] = Array_LowerBand[sc.Index - 1];

		if (sc.Index == 0)
			Subgraph_SuperTrend[sc.Index] = Array_UpperBand[sc.Index];

		if (Subgraph_SuperTrend[sc.Index - 1] == Array_UpperBand[sc.Index - 1] && sc.Close[sc.Index] < Array_UpperBand[sc.Index])
			Subgraph_SuperTrend[sc.Index] = Array_UpperBand[sc.Index];
		else if (Subgraph_SuperTrend[sc.Index - 1] == Array_UpperBand[sc.Index - 1] && sc.Close[sc.Index] > Array_UpperBand[sc.Index])
			Subgraph_SuperTrend[sc.Index] = Array_LowerBand[sc.Index];
		else if (Subgraph_SuperTrend[sc.Index - 1] == Array_LowerBand[sc.Index - 1] && sc.Close[sc.Index] > Array_LowerBand[sc.Index])
			Subgraph_SuperTrend[sc.Index] = Array_LowerBand[sc.Index];
		else if (Subgraph_SuperTrend[sc.Index - 1] == Array_LowerBand[sc.Index - 1] && sc.Close[sc.Index] < Array_LowerBand[sc.Index])
			Subgraph_SuperTrend[sc.Index] = Array_UpperBand[sc.Index];
		else
			Subgraph_SuperTrend[sc.Index] = Subgraph_SuperTrend[sc.Index - 1];

		if (Subgraph_SuperTrend[sc.Index] == Array_UpperBand[sc.Index])
			bSuperUp = false;
		else
			bSuperUp = true;


		// SQUEEZE RELAXER
		sc.DataStartIndex = 20;
		sc.ExponentialMovAvg(sc.Close, Subgraph_MomentumHistUpColors, 20);  // Note: EMA returns close when index is < HistogramLenSecondData.GetInt()
		sc.MovingAverage(sc.Close, Subgraph_MomentumHistUpColors, MOVAVGTYPE_EXPONENTIAL, 20);

		float hlh = sc.GetHighest(sc.High, 20);
		float lll = sc.GetLowest(sc.Low, 20);

		Subgraph_MomentumHistDownColors[sc.Index] = sc.Open[sc.Index] - ((hlh + lll) / 2.0f + Subgraph_MomentumHistUpColors[sc.Index]) / 2.0f;
		sc.LinearRegressionIndicator(Subgraph_MomentumHistDownColors, Subgraph_MomentumHist, 20);
		sc.MovingAverage(sc.Close, Subgraph_MomentumHistUpColors, MOVAVGTYPE_LINEARREGRESSION, 20);

		if ((Subgraph_MomentumHist[i] <= 0) && (Subgraph_MomentumHist[i] > Subgraph_MomentumHist[i - 1]))
		{
			if (r_SqueezeUp == 0) {
				Subgraph_SqueezeUp[i] = sc.Low[i] - ((Input_UpOffset.GetInt()) * sc.TickSize);
				Subgraph_SqueezeDown[i] = 0;
				r_SqueezeUp = 1;
			}
		}
		else if ((Subgraph_MomentumHist[i] >= 0) && (Subgraph_MomentumHist[i] < Subgraph_MomentumHist[i - 1]))
		{
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

		//int data_type_indx = (int)Input_InputData.GetInputDataIndex();
		//SCFloatArrayRef in = sc.BaseDataIn[data_type_indx];
		//SCDateTimeArrayRef ti = sc.BaseDateTimeIn[data_type_indx];

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
		if (SubgraphArray.GetArraySize() == 0)
		{
			return; // The SubgraphArray may not exist or is empty. Either way we can not do anything with it.
		}

		double dTickie = Input_ShavedBuffer.GetInt() * sc.TickSize;
		if (Input_IgnoreDoji.GetYesNo() == SC_YES &&
			BodyLength(sc.BaseData, i) < LowerWickLength(sc.BaseData, i) &&
			BodyLength(sc.BaseData, i) < UpperWickLength(sc.BaseData, i))
			return;

		if (IsGreen(sc.BaseData, i) && abs(in[SC_HIGH][i] - in[SC_LAST][i]) < dTickie)
			Subgraph_ShavedGreen[i] = 1;

		if (IsRed(sc.BaseData, i) && abs(in[SC_LOW][i] - in[SC_LAST][i]) < dTickie)
			Subgraph_ShavedRed[i] = 1;

		if (in[SC_LOW][i] < LowerBand &&
			IsGreen(sc.BaseData, i) &&
			in[SC_HIGH][i] > in[SC_HIGH][i - 1] &&
			in[SC_LAST][i] > in[SC_LAST][i - 1] &&
			in[SC_LAST][i - 1] < in[SC_OPEN][i - 1])
		{
			Subgraph_BBGreen[i] = 1;
		}

		if (in[SC_HIGH][i] > UpperBand &&
			IsRed(sc.BaseData, i) &&
			in[SC_LOW][i] < in[SC_LOW][i - 1] &&
			in[SC_LAST][i] < in[SC_LAST][i - 1] &&
			in[SC_LAST][i - 1] < in[SC_OPEN][i - 1])
		{
			Subgraph_BBRed[i] = 1;
		}

#pragma region BUY SELL PLOTS

		if (BarCloseStatus)
		{
			int iEndings = 0;
			const int32_t numLines = sc.GetNumLinesUntilFutureIntersection(sc.ChartNumber, sc.StudyGraphInstanceID) - 1;
			if (numLines != 0)
			{
				for (int32_t lineIndex = numLines; lineIndex >= 0; --lineIndex)
				{
					int32_t lineID{ 0 };
					int32_t startIndex{ 0 };
					int32_t endIndex{ 0 };
					float lineValue{ 0.0f };

					if (sc.GetStudyLineUntilFutureIntersectionByIndex(sc.ChartNumber, sc.StudyGraphInstanceID, lineIndex, lineID, startIndex, lineValue, endIndex))
					{
						if (endIndex > 0)
						{
							iEndings++;
							//DrawText(sc, Subgraph_3oU, "shit", 0, 5);
							//sc.DeleteLineUntilFutureIntersection(startIndex, lineID);
							//Subgraph_Intersection[i] = endIndex;
						}
					}
				}
			}
			Subgraph_Intersection[i] = iEndings;
		}

		if (Subgraph_Intersection[i] > Subgraph_Intersection[i - 1] &&
			!IsVolImbGreen(sc, i - 1) && !IsVolImbGreen(sc, i - 2) &&
			!IsVolImbRed(sc, i - 1) && !IsVolImbRed(sc, i - 2))
		{
			//DrawText(sc, Subgraph_3oU, "FILL", 0, 5);
		}

		if (IsThreeOutsideUp(sc, sc.CurrentIndex) && BarCloseStatus)
		{
			DrawText(sc, Subgraph_3oU, "3oU", 0, 5);
			Subgraph_3oU[i] = sc.Low[i];
		}

		if (IsThreeOutsideDown(sc, sc.CurrentIndex) && BarCloseStatus)
		{
			DrawText(sc, Subgraph_3oD, "3oD", 0, 5);
			Subgraph_3oD[i] = sc.Low[i];
		}

		if (IsTweezerTop(sc, sc.CurrentIndex, UpperBand))
		{
			DrawText(sc, Subgraph_EqualH, "Eq Hi", 1, 5);
			Subgraph_EqualH[i] = sc.Low[i];
		}

		if (IsTweezerBottom(sc, sc.CurrentIndex, LowerBand))
		{
			DrawText(sc, Subgraph_EqualL, "Eq Lo", -1, 5);
			Subgraph_EqualL[i] = sc.Low[i];
		}

		if (IsTrampoline(sc, sc.CurrentIndex, rsi, prsi, pprsi, UpperBand, sc.TickSize))
		{
			DrawText(sc, Subgraph_Tramp, "TR", -1, 3);
			Subgraph_Tramp[i] = sc.Low[i];
		}
		else if (IsTrampoline(sc, sc.CurrentIndex, rsi, prsi, pprsi, LowerBand, sc.TickSize))
		{
			DrawText(sc, Subgraph_Tramp, "TR", 1, 3);
			Subgraph_Tramp[i] = sc.Low[i];
		}

		if (Input_BarColor.GetIndex() != 0)
			sc.RSI(sc.BaseDataIn[SC_LAST], Subgraph_ColorBar, MOVAVGTYPE_SIMPLE, 14);

		int iRedGreenColor = 255;
		if (Input_BarColor.GetIndex() == 1) // waddah explosion
		{
			iRedGreenColor = min(255, abs(t1) + Input_BarColorWaddah.GetInt());
			if (t1 > 0)
				Subgraph_ColorBar.DataColor[sc.Index] = RGB(0, iRedGreenColor, 0);
			else
				Subgraph_ColorBar.DataColor[sc.Index] = RGB(iRedGreenColor, 0, 0);
		}
		else if (Input_BarColor.GetIndex() == 2) // linda macd
		{
			iRedGreenColor = min(255, abs(t1) + Input_BarColorLinda.GetInt());
			if (linda > 0)
				Subgraph_ColorBar.DataColor[sc.Index] = RGB(0, iRedGreenColor, 0);
			else
				Subgraph_ColorBar.DataColor[sc.Index] = RGB(iRedGreenColor, 0, 0);
		}
		else if (Input_BarColor.GetIndex() == 3) // super trend
		{
			if (bSuperUp)
				Subgraph_ColorBar.DataColor[sc.Index] = RGB(0, 255, 0);
			else
				Subgraph_ColorBar.DataColor[sc.Index] = RGB(255, 0, 0);
		}

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
			(Input_UseSuperTrend.GetYesNo() == SC_YES && !bSuperUp)
			)
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
			(Input_UseSuperTrend.GetYesNo() == SC_YES && bSuperUp)
			)
			bShowDown = false;

		if (BarCloseStatus && bShowUp)
		{
			Subgraph_DotUp[i] = sc.Low[i] - ((Input_UpOffset.GetInt()) * sc.TickSize);
			txt.Format("Olympus BUY Signal at %.2d", close);
			//sc.AddMessageToLog(txt, 0);
			if (i >= sc.ArraySize - 1)
				sc.AlertWithMessage(199, "Olympus BUY Signal");
		}

		if (BarCloseStatus && bShowDown)
		{
			Subgraph_DotDown[i] = sc.High[i] + ((Input_DownOffset.GetInt()) * sc.TickSize);
			txt.Format("Olympus SELL Signal at %.2d", close);
			if (sc.IsNewBar(i))
				sc.AlertWithMessage(200, "Olympus SELL Signal");
		}

		Subgraph_VolImbUp[i] = 0;
		Subgraph_VolImbDown[i] = 0;

		if (BarCloseStatus && IsVolImbGreen(sc, sc.CurrentIndex))
		{
			sc.AddLineUntilFutureIntersection(sc.Index, sc.Index, sc.Open[sc.Index], RGB(106, 149, 247), 4, LINESTYLE_SOLID, false, false, "");
			Subgraph_VolImbUp[i] = sc.Low[i] - ((Input_UpOffset.GetInt()) * sc.TickSize);
			txt.Format("Volume Imbalance BUY at %.2d", close);
			if (sc.IsNewBar(i))
				sc.AlertWithMessage(197, "Volume Imbalance BUY");
		}

		if (BarCloseStatus && IsVolImbRed(sc, sc.CurrentIndex))
		{
			sc.AddLineUntilFutureIntersection(sc.Index, sc.Index, sc.Open[sc.Index], RGB(106, 149, 247), 4, LINESTYLE_SOLID, false, false, "");
			Subgraph_VolImbDown[i] = sc.High[i] + ((Input_UpOffset.GetInt()) * sc.TickSize);
			txt.Format("Volume Imbalance SELL at %.2d", close);
			//sc.AddMessageToLog(txt, 0);
			if (sc.IsNewBar(i))
				sc.AlertWithMessage(198, "Volume Imbalance SELL");
		}


	}

#pragma endregion

	//STARTUPINFO si;
	//PROCESS_INFORMATION pi;

	//CreateProcess("notepad.exe",   // No module name (use command line)
	//	NULL,        // Command line
	//	NULL,           // Process handle not inheritable
	//	NULL,           // Thread handle not inheritable
	//	FALSE,          // Set handle inheritance to FALSE
	//	0,              // No creation flags
	//	NULL,           // Use parent's environment block
	//	NULL,           // Use parent's starting directory 
	//	&si,            // Pointer to STARTUPINFO structure
	//	&pi);

}

#pragma endregion

#pragma region TESTING

SCSFExport scsf_SierraSqueeze(SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_MomentumHist = sc.Subgraph[0];
	SCSubgraphRef Subgraph_SqueezeDots = sc.Subgraph[1];
	SCSubgraphRef Subgraph_MomentumHistUpColors = sc.Subgraph[2];
	SCSubgraphRef Subgraph_MomentumHistDownColors = sc.Subgraph[3];
	SCSubgraphRef Subgraph_Temp4 = sc.Subgraph[4];
	//SCSubgraphRef Temp5 = sc.Subgraph[5];
	SCSubgraphRef Subgraph_SignalValues = sc.Subgraph[6];
	SCSubgraphRef Subgraph_Temp8 = sc.Subgraph[8];

	SCInputRef Input_InputData = sc.Input[0];
	SCInputRef Input_HistogramLenFirstData = sc.Input[1];
	SCInputRef Input_SqueezeLength = sc.Input[2];
	SCInputRef Input_NK = sc.Input[3];
	SCInputRef Input_NB = sc.Input[4];
	SCInputRef Input_FirstMovAvgType = sc.Input[5];
	SCInputRef Input_HistogramLenSecondData = sc.Input[6];
	SCInputRef Input_SecondMovAvgType = sc.Input[7];
	SCInputRef Input_VersionUpdate = sc.Input[8];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Squeeze Indicator 2";
		sc.StudyDescription	= "Developed by the user Tony C.";

		Subgraph_MomentumHist.Name = "Momentum HISTOGRAM";
		Subgraph_MomentumHist.DrawStyle = DRAWSTYLE_BAR;
		Subgraph_MomentumHist.PrimaryColor = RGB(0,255,0);
		Subgraph_MomentumHist.LineWidth = 12;
		Subgraph_MomentumHist.DrawZeros = true;

		Subgraph_SqueezeDots.Name = "Squeeze Dots";
		Subgraph_SqueezeDots.DrawStyle = DRAWSTYLE_POINT;
		Subgraph_SqueezeDots.PrimaryColor = RGB(255,0,255);
		Subgraph_SqueezeDots.LineWidth = 4;
		Subgraph_SqueezeDots.DrawZeros = true;

		Subgraph_MomentumHistUpColors.Name = "Momentum HISTOGRAM Up Colors";
		Subgraph_MomentumHistUpColors.DrawStyle = DRAWSTYLE_IGNORE;
		Subgraph_MomentumHistUpColors.SecondaryColorUsed = 1;
		Subgraph_MomentumHistUpColors.PrimaryColor = RGB(0, 255, 0);
		Subgraph_MomentumHistUpColors.SecondaryColor = RGB(0, 130, 130);
		Subgraph_MomentumHistUpColors.DrawZeros = true;
		Subgraph_MomentumHistUpColors.LineWidth = 12;

		Subgraph_MomentumHistDownColors.Name	= "Momentum HISTOGRAM Down Colors";
		Subgraph_MomentumHistDownColors.DrawStyle = DRAWSTYLE_IGNORE;
		Subgraph_MomentumHistDownColors.SecondaryColorUsed = 1;
		Subgraph_MomentumHistDownColors.PrimaryColor = RGB(255, 0, 0);
		Subgraph_MomentumHistDownColors.SecondaryColor = RGB(130, 0, 0);
		Subgraph_MomentumHistDownColors.DrawZeros = true;
		Subgraph_MomentumHistDownColors.LineWidth = 12;

		Subgraph_SignalValues.Name = "Signal Values";
		Subgraph_SignalValues.DrawStyle = DRAWSTYLE_IGNORE;
		Subgraph_SignalValues.PrimaryColor = RGB(127,0,255);
		Subgraph_SignalValues.DrawZeros = true;

		Input_InputData.Name = "Input Data";
		Input_InputData.SetInputDataIndex(SC_LAST); // default data field		

		Input_HistogramLenFirstData.Name = "Histogram Length First Data";
		Input_HistogramLenFirstData.SetInt(20);
		Input_HistogramLenFirstData.SetIntLimits(1,MAX_STUDY_LENGTH);

		Input_SqueezeLength.Name = "Squeeze Length";
		Input_SqueezeLength.SetFloat(20);

		Input_NK.Name = "NK.GetFloat()";
		Input_NK.SetFloat(1.5);

		Input_NB.Name = "NB.GetFloat()";
		Input_NB.SetFloat(2);

		Input_FirstMovAvgType.Name = "First MA Type";
		Input_FirstMovAvgType.SetMovAvgType(MOVAVGTYPE_EXPONENTIAL);

		Input_HistogramLenSecondData.Name = "Histogram Length Second Data";
		Input_HistogramLenSecondData.SetInt(20);
		Input_HistogramLenSecondData.SetIntLimits(1,MAX_STUDY_LENGTH);

		Input_SecondMovAvgType.Name = "Second MA Type";
		Input_SecondMovAvgType.SetMovAvgType(MOVAVGTYPE_LINEARREGRESSION);

		// hidden input for old versions support
		Input_VersionUpdate.SetInt(1);

		sc.AutoLoop = 1;
		

		return;
	}

	// upgrading the default settings
	if (Input_VersionUpdate.GetInt() != 1)
	{
		Subgraph_MomentumHistUpColors.Name = "Momentum Histogram Up Colors";
		Subgraph_MomentumHistUpColors.DrawStyle = DRAWSTYLE_IGNORE;
		Subgraph_MomentumHistUpColors.SecondaryColorUsed	= 1;
		Subgraph_MomentumHistUpColors.PrimaryColor = RGB(0, 0, 255);
		Subgraph_MomentumHistUpColors.SecondaryColor = RGB(0, 0, 130);

		Subgraph_MomentumHistDownColors.Name = "Momentum Histogram Down Colors";
		Subgraph_MomentumHistDownColors.DrawStyle = DRAWSTYLE_IGNORE;
		Subgraph_MomentumHistDownColors.SecondaryColorUsed = 1;
		Subgraph_MomentumHistDownColors.PrimaryColor = RGB(255, 0, 0);
		Subgraph_MomentumHistDownColors.SecondaryColor = RGB(130, 0, 0);

		Subgraph_SignalValues.Name = "Signal Values";
		Subgraph_SignalValues.DrawStyle = DRAWSTYLE_IGNORE;

		Input_VersionUpdate.SetInt(1);
	}

	// Inputs
	int i = sc.Index;
	const DWORD inside		= RGB(255, 0, 0);	
	const DWORD outside		= RGB(0, 255, 0);	

	Subgraph_MomentumHistUpColors[i] = 0;
	Subgraph_MomentumHistDownColors[i] = 0;

	// First output elements are not valid
	sc.DataStartIndex = Input_HistogramLenSecondData.GetInt();

	SCFloatArrayRef close =  sc.Close;
	sc.ExponentialMovAvg(close, Subgraph_MomentumHistUpColors, Input_HistogramLenSecondData.GetInt());  // Note: EMA returns close when index is < HistogramLenSecondData.GetInt()
	sc.MovingAverage(close,  Subgraph_MomentumHistUpColors, Input_FirstMovAvgType.GetMovAvgType(), Input_HistogramLenFirstData.GetInt());

	float hlh = sc.GetHighest(sc.High, Input_HistogramLenSecondData.GetInt());
	float lll = sc.GetLowest(sc.Low, Input_HistogramLenSecondData.GetInt());

	SCFloatArrayRef price = sc.Open;

	Subgraph_MomentumHistDownColors[sc.Index] = price[sc.Index] - ((hlh + lll)/2.0f + Subgraph_MomentumHistUpColors[sc.Index])/2.0f;
	sc.LinearRegressionIndicator(Subgraph_MomentumHistDownColors, Subgraph_MomentumHist, Input_HistogramLenSecondData.GetInt());
	sc.MovingAverage(close,  Subgraph_MomentumHistUpColors, Input_SecondMovAvgType.GetMovAvgType(), Input_HistogramLenSecondData.GetInt());


	if(
		(Subgraph_MomentumHist[i]<0) 
		&&(Subgraph_MomentumHist[i] < Subgraph_MomentumHist[i-1])
		)

	{
		Subgraph_MomentumHist.DataColor[sc.Index] = Subgraph_MomentumHistDownColors.PrimaryColor;		
	}
	else if(
		(Subgraph_MomentumHist[i]<=0) 
		&&(Subgraph_MomentumHist[i] > Subgraph_MomentumHist[i-1])
		)
	{
		Subgraph_MomentumHist.DataColor[sc.Index] = Subgraph_MomentumHistDownColors.SecondaryColor;		
	}
	else if(
		(Subgraph_MomentumHist[i]>0) 
		&&(Subgraph_MomentumHist[i] > Subgraph_MomentumHist[i-1])
		)
	{
		Subgraph_MomentumHist.DataColor[sc.Index] = Subgraph_MomentumHistUpColors.PrimaryColor;		
	}
	else if(
		(Subgraph_MomentumHist[i]>=0) 
		&&(Subgraph_MomentumHist[i] < Subgraph_MomentumHist[i-1])
		)
	{
		Subgraph_MomentumHist.DataColor[sc.Index] = Subgraph_MomentumHistUpColors.SecondaryColor;		
	}


	//Squeeze
	sc.Keltner(
		sc.BaseDataIn,
		sc.Close,
		Subgraph_Temp8,
		Input_SqueezeLength.GetInt(),
		MOVAVGTYPE_SMOOTHED,
		Input_SqueezeLength.GetInt(),
		MOVAVGTYPE_SMOOTHED,
		Input_NK.GetFloat(),
		Input_NK.GetFloat()
		);

	float TopBandOut = Subgraph_Temp8.Arrays[0][sc.Index];
	float BottomBandOut = Subgraph_Temp8.Arrays[1][sc.Index];

	sc.BollingerBands(sc.Close, Subgraph_Temp4, Input_SqueezeLength.GetInt(), Input_NB.GetFloat(), MOVAVGTYPE_SMOOTHED);

	float BU =Subgraph_Temp4.Arrays[0][sc.Index];
	float BL =Subgraph_Temp4.Arrays[1][sc.Index];

	if (
		(BU < TopBandOut)
		|| (BL > BottomBandOut)
		)
	{
		Subgraph_SqueezeDots[sc.Index] = 0.0;
		Subgraph_SqueezeDots.DataColor[sc.Index] = inside;		
		Subgraph_SignalValues[sc.Index] = 0.0;
		Subgraph_SignalValues.DataColor[sc.Index] = inside;
	}
	else
	{
		Subgraph_SqueezeDots[sc.Index] = 0.0;
		Subgraph_SqueezeDots.DataColor[sc.Index] = outside;		
		Subgraph_SignalValues[sc.Index] = 1.0;
		Subgraph_SignalValues.DataColor[sc.Index] = outside;
	}
}


/*==========================================================================

SCSFExport scsf_GraphicsSettingsExample(SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_SimpleMA = sc.Subgraph[0];

	// Set configuration variables

	if (sc.SetDefaults)
	{
		// Set defaults

		sc.GraphName = "Chart Graphics Settings Example";

		sc.StudyDescription = "This example will change the color of the chart background, when the price is above a simple moving average. Add this study during a chart replay for a demonstration.";

		sc.GraphRegion = 0;

		Subgraph_SimpleMA.Name = "Simple Moving Average";
		Subgraph_SimpleMA.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_SimpleMA.PrimaryColor = RGB(0, 255, 0);

		sc.AutoLoop = 0;

		return;
	}


	// Do data processing
	if (sc.IsFullRecalculation)
	{
		//Specify to use the chart graphic settings.
		sc.SetUseGlobalGraphicsSettings(sc.ChartNumber, false);
	}

	uint32_t Color = 0;
	uint32_t LineWidth = 0;
	SubgraphLineStyles LineStyle = LINESTYLE_UNSET;

	for (int BarIndex = sc.UpdateStartIndex; BarIndex < sc.ArraySize; BarIndex++)
	{
		// Simple moving average in the first subgraph
		sc.SimpleMovAvg(sc.Close, Subgraph_SimpleMA, BarIndex, 10);

		sc.GetGraphicsSetting(sc.ChartNumber, n_ACSIL::GRAPHICS_SETTING_CHART_BACKGROUND, Color, LineWidth, LineStyle);

		if (Subgraph_SimpleMA.Data[BarIndex] < sc.Close[BarIndex])
		{
			if (Color != RGB(255, 0, 0))
			{
				Color = RGB(255, 0, 0);
				sc.SetGraphicsSetting(sc.ChartNumber, n_ACSIL::GRAPHICS_SETTING_CHART_BACKGROUND, Color);
			}

		}
		else
		{
			if (Color != RGB(0, 0, 0))
			{
				Color = RGB(0, 0, 0);
				sc.SetGraphicsSetting(sc.ChartNumber, n_ACSIL::GRAPHICS_SETTING_CHART_BACKGROUND, Color);
			}
		}
	}


}

SCSFExport scsf_test(SCStudyInterfaceRef sc)
{
// Marker example
s_UseTool Tool;
int UniqueLineNumber = 74191;//any random number.

Tool.Clear(); // Reset tool structure.  Good practice but unnecessary in this case.
Tool.ChartNumber = sc.ChartNumber;

Tool.DrawingType = DRAWING_MARKER;
Tool.LineNumber =  UniqueLineNumber +1;

int BarIndex = max(0, sc.ArraySize - 35);

Tool.BeginDateTime = sc.BaseDateTimeIn[BarIndex];
Tool.BeginValue = sc.High[BarIndex];

Tool.Color = RGB(0,200,200);
Tool.AddMethod = UTAM_ADD_OR_ADJUST;

Tool.MarkerType = MARKER_X;
Tool.MarkerSize = 8;

Tool.LineWidth = 5;

sc.UseTool(Tool);

}

*/

#pragma endregion

#pragma region LINDA MACD

SCSFExport scsf_LindaMACD(SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_MACDPos = sc.Subgraph[0];
	SCSubgraphRef Subgraph_MACDNeg = sc.Subgraph[1];
	SCSubgraphRef Subgraph_Calc = sc.Subgraph[2];
	SCSubgraphRef Subgraph_Parabolic = sc.Subgraph[3];

	SCSubgraphRef Subgraph_MACDPosBright = sc.Subgraph[4];
	SCSubgraphRef Subgraph_MACDNegBright = sc.Subgraph[5];

	SCInputRef Input_InputDataHigh = sc.Input[0];
	SCInputRef Input_InputDataLow = sc.Input[1];
	SCInputRef Input_StartAccelerationFactor = sc.Input[3];
	SCInputRef Input_AccelerationIncrement = sc.Input[4];
	SCInputRef Input_MaxAccelerationFactor = sc.Input[5];
	SCInputRef Input_AdjustForGap = sc.Input[6];
	SCInputRef Input_Version = sc.Input[7];

	if(sc.SetDefaults)
	{
		sc.GraphName = "Linda MACD";
		sc.AutoLoop	= 1;

		sc.GraphRegion = 1;
		sc.ValueFormat = 3;
		
		Subgraph_MACDPos.Name = "MACD Positive";
		Subgraph_MACDPos.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_MACDPos.DrawZeros = true;
		Subgraph_MACDPos.PrimaryColor = RGB(1, 110, 5);
		Subgraph_MACDPos.LineWidth = 12;

		Subgraph_MACDNeg.Name = "MACD Negative";
		Subgraph_MACDNeg.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_MACDNeg.DrawZeros = true;
		Subgraph_MACDNeg.PrimaryColor = RGB(171, 2, 2);
		Subgraph_MACDNeg.LineWidth = 12;

		Subgraph_MACDPosBright.Name = "MACD/PSAR Positive";
		Subgraph_MACDPosBright.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_MACDPosBright.DrawZeros = true;
		Subgraph_MACDPosBright.PrimaryColor = RGB(0, 255, 0);
		Subgraph_MACDPosBright.LineWidth = 12;

		Subgraph_MACDNegBright.Name = "MACD/PSAR Negative";
		Subgraph_MACDNegBright.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_MACDNegBright.DrawZeros = true;
		Subgraph_MACDNegBright.PrimaryColor = RGB(255, 0, 0);
		Subgraph_MACDNegBright.LineWidth = 12;

		Subgraph_Calc.Name = "Input Data";
		Subgraph_Calc.DrawStyle = DRAWSTYLE_IGNORE;

		Subgraph_Parabolic.Name = "Parabolic";
		Subgraph_Parabolic.DrawStyle = DRAWSTYLE_IGNORE;

		Input_InputDataHigh.Name = "Input Data High";
		Input_InputDataHigh.SetInputDataIndex(SC_HIGH);

		Input_InputDataLow.Name = "Input Data Low";
		Input_InputDataLow.SetInputDataIndex(SC_LOW);

		Input_Version.SetInt(1);

		Input_StartAccelerationFactor.Name = "Start Acceleration Factor";
		Input_StartAccelerationFactor.SetFloat(0.02f);
		Input_StartAccelerationFactor.SetFloatLimits(0.000001f, static_cast<float>(MAX_STUDY_LENGTH));
		
		Input_AccelerationIncrement.Name = "Acceleration Increment";
		Input_AccelerationIncrement.SetFloat(0.02f);
		Input_AccelerationIncrement.SetFloatLimits(0.00001f, 100.0f);
		
		Input_MaxAccelerationFactor.Name = "Max Acceleration Factor";
		Input_MaxAccelerationFactor.SetFloat(0.2f);
		Input_MaxAccelerationFactor.SetFloatLimits(0.000001f, static_cast<float>(MAX_STUDY_LENGTH));
		
		Input_AdjustForGap.Name = "Adjust for Gap";
		Input_AdjustForGap.SetYesNo(0);  // No

		return;
	}

	if (Input_Version.GetInt() == 0)
	{
		Input_InputDataHigh.SetInputDataIndex(SC_HIGH);
		Input_InputDataLow.SetInputDataIndex(SC_LOW);
	}

	sc.Parabolic(
		sc.BaseDataIn,
		sc.BaseDateTimeIn,
		Subgraph_Parabolic,
		sc.Index,
		Input_StartAccelerationFactor.GetFloat(),
		Input_AccelerationIncrement.GetFloat(),
		Input_MaxAccelerationFactor.GetFloat(),
		Input_AdjustForGap.GetYesNo(),
		Input_InputDataHigh.GetInputDataIndex(),
		Input_InputDataLow.GetInputDataIndex()
	);
	float SAR = Subgraph_Parabolic[sc.Index]; 

	sc.MACD(sc.BaseData[SC_LAST], Subgraph_Calc, sc.Index, 3, 9, 16, MOVAVGTYPE_SIMPLE); 
	float MACDHistogram = Subgraph_Calc.Arrays[3][sc.Index];

	if (MACDHistogram > 0) // Green MACD
	{
		if (SAR < sc.Low[sc.Index])  // If MACD/PSAR agreement, make it bright green
		{
			Subgraph_MACDPos[sc.Index] = 0;
			Subgraph_MACDPosBright[sc.Index] = MACDHistogram;
			Subgraph_MACDNeg[sc.Index] = 0;
			Subgraph_MACDNegBright[sc.Index] = 0;
		}
		else
		{
			Subgraph_MACDPos[sc.Index] = MACDHistogram;
			Subgraph_MACDPosBright[sc.Index] = 0;
			Subgraph_MACDNeg[sc.Index] = 0;
			Subgraph_MACDNegBright[sc.Index] = 0;
		}
	}
	else  // RED MACD
	{
		if (SAR > sc.High[sc.Index])  // If MACD/PSAR agreement, make it bright red
		{
			Subgraph_MACDPos[sc.Index] = 0;
			Subgraph_MACDPosBright[sc.Index] = 0;
			Subgraph_MACDNeg[sc.Index] = 0;
			Subgraph_MACDNegBright[sc.Index] = MACDHistogram * -1;
		}
		else
		{
			Subgraph_MACDPos[sc.Index] = 0;
			Subgraph_MACDPosBright[sc.Index] = 0;
			Subgraph_MACDNeg[sc.Index] = MACDHistogram * -1;
			Subgraph_MACDNegBright[sc.Index] = 0;
		}
	}
		
}

#pragma endregion

#pragma region WADDAH EXPLOSION

SCSFExport scsf_WaddahExplosion(SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_WaddahPos = sc.Subgraph[0];
	SCSubgraphRef Subgraph_WaddahPosBright = sc.Subgraph[1];
	SCSubgraphRef Subgraph_WaddahNeg = sc.Subgraph[2];
	SCSubgraphRef Subgraph_WaddahNegBright = sc.Subgraph[3];

	SCSubgraphRef Subgraph_Explosion = sc.Subgraph[4];	
	SCSubgraphRef Subgraph_Slow = sc.Subgraph[5];
	SCSubgraphRef Subgraph_Fast = sc.Subgraph[6];
	SCSubgraphRef Subgraph_BB = sc.Subgraph[7];

	SCSubgraphRef Subgraph_ColorBar = sc.Subgraph[8];
	SCSubgraphRef Subgraph_ColorUp = sc.Subgraph[9];
	SCSubgraphRef Subgraph_ColorDown = sc.Subgraph[10];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Waddah Explosion";

        Subgraph_BB.Name = "BB";
		Subgraph_BB.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_Slow.Name = "Slow";
		Subgraph_Slow.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_Fast.Name = "Fast";
		Subgraph_Fast.DrawStyle = DRAWSTYLE_IGNORE;
		
        Subgraph_WaddahPos.Name = "Waddah Positive";
        Subgraph_WaddahPos.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_WaddahPos.LineWidth = 12;
		Subgraph_WaddahPos.PrimaryColor = RGB(1, 110, 5);;

        Subgraph_WaddahNeg.Name = "Waddah Negative";
        Subgraph_WaddahNeg.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_WaddahNeg.LineWidth = 12;
		Subgraph_WaddahNeg.PrimaryColor = RGB(171, 2, 2);;

		Subgraph_WaddahPosBright.Name = "Waddah Positive Bright";
        Subgraph_WaddahPosBright.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_WaddahPosBright.LineWidth = 12;
		Subgraph_WaddahPosBright.PrimaryColor = RGB(0, 255, 0);

        Subgraph_WaddahNegBright.Name = "Waddah Negative Bright";
        Subgraph_WaddahNegBright.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_WaddahNegBright.LineWidth = 12;
		Subgraph_WaddahNegBright.PrimaryColor = RGB(255, 0, 0);

        Subgraph_Explosion.Name = "Explosion Line";
        Subgraph_Explosion.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_Explosion.LineWidth = 2;
		Subgraph_Explosion.PrimaryColor = COLOR_WHITE;
		Subgraph_Explosion.SecondaryColor = COLOR_RED;

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

        sc.AutoLoop = 1;

        return;
    }

	sc.BollingerBands(sc.BaseData[SC_LAST], Subgraph_BB, 20, 2, MOVAVGTYPE_SIMPLE);
	float UpperBand = Subgraph_BB.Arrays[0][sc.Index];
	float LowerBand = Subgraph_BB.Arrays[1][sc.Index];
	float e1 = UpperBand - LowerBand;
	Subgraph_Explosion[sc.Index] = e1;

	sc.MovingAverage(sc.BaseDataIn[SC_LAST], Subgraph_Fast, MOVAVGTYPE_EXPONENTIAL, 20);
	float a1 = Subgraph_Fast[sc.Index];
	float b1 = Subgraph_Fast[sc.Index - 1];
	
	sc.MovingAverage(sc.BaseDataIn[SC_LAST], Subgraph_Slow, MOVAVGTYPE_EXPONENTIAL, 40);
	float a2 = Subgraph_Slow[sc.Index];
	float b2 = Subgraph_Slow[sc.Index - 1];

	float t1 = ((a1 - a2) - (b1 - b2)) * 150;

	COLORREF UpColor = Subgraph_ColorUp.PrimaryColor;
	COLORREF DownColor = Subgraph_ColorDown.PrimaryColor;

	int ix = min(t1, 255);
	UpColor = RGB(0, ix, 0);
	Subgraph_ColorUp[sc.Index] = UpColor;
	Subgraph_ColorBar.DataColor[sc.Index] = UpColor;

	if (t1 > 0)
	{
		if (t1 > e1)
		{
			Subgraph_WaddahPos[sc.Index] = 0;
			Subgraph_WaddahNeg[sc.Index] = 0;
			Subgraph_WaddahPosBright[sc.Index] = t1;
			Subgraph_WaddahNegBright[sc.Index] = 0;
		}
		else
		{
			Subgraph_WaddahPos[sc.Index] = t1;
			Subgraph_WaddahNeg[sc.Index] = 0;
			Subgraph_WaddahPosBright[sc.Index] = 0;
			Subgraph_WaddahNegBright[sc.Index] = 0;
		}
	}
	else
	{
		if (abs(t1) > e1)
		{
			Subgraph_WaddahPos[sc.Index] = 0;
			Subgraph_WaddahNeg[sc.Index] = 0;
			Subgraph_WaddahPosBright[sc.Index] = 0;
			Subgraph_WaddahNegBright[sc.Index] = t1 * -1;
		}
		else
		{
			Subgraph_WaddahPos[sc.Index] = 0;
			Subgraph_WaddahNeg[sc.Index] = t1 * -1;
			Subgraph_WaddahPosBright[sc.Index] = 0;
			Subgraph_WaddahNegBright[sc.Index] = 0;
		}
	}
	
    return;
}

#pragma endregion

SCSFExport scsf_Linda_Anti_Setup(SCStudyInterfaceRef sc)
{
#pragma region INPUTS

	int i = sc.Index;

	int& iHigh = sc.GetPersistentInt(1);
	int& iLow = sc.GetPersistentInt(2);
	int& iH = sc.GetPersistentInt(3);
	int& iL = sc.GetPersistentInt(4);
	int& iHPainted = sc.GetPersistentInt(5);
	int& iLPainted = sc.GetPersistentInt(6);

	SCSubgraphRef Subgraph_MACD = sc.Subgraph[0];
	SCSubgraphRef Subgraph_MA = sc.Subgraph[1];
	SCSubgraphRef Subgraph_Calc = sc.Subgraph[2];
	SCSubgraphRef Subgraph_Dot = sc.Subgraph[3];
	SCSubgraphRef Subgraph_BgGreen = sc.Subgraph[4];
	SCSubgraphRef Subgraph_BgRed = sc.Subgraph[5];
	SCSubgraphRef Subgraph_Txt = sc.Subgraph[6];

#pragma endregion

#pragma region DEFAULTS

	if (sc.SetDefaults)
	{
		sc.GraphName = "Linda Raschke Anti Setup";
		sc.GraphRegion = 1;
		sc.AutoLoop = 1;

		Subgraph_Calc.Name = "Calc";
		Subgraph_Calc.DrawStyle = DRAWSTYLE_IGNORE;

		Subgraph_MACD.Name = "MACD";
		Subgraph_MACD.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_MACD.LineWidth = 1;
		Subgraph_MACD.AutoColoring = AUTOCOLOR_SLOPE;
		Subgraph_MACD.PrimaryColor = RGB(0, 255, 0);
		Subgraph_MACD.SecondaryColor = RGB(255, 0, 0);
		Subgraph_MACD.DrawZeros = true;

		Subgraph_MA.Name = "MA";
		Subgraph_MA.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_MA.LineWidth = 2;
		Subgraph_MA.AutoColoring = AUTOCOLOR_SLOPE;
		Subgraph_MA.PrimaryColor = RGB(0, 255, 0);
		Subgraph_MA.SecondaryColor = RGB(255, 0, 0);
		Subgraph_MA.DrawZeros = true;

		Subgraph_BgGreen.Name = "Green Background";
		Subgraph_BgGreen.DrawStyle = DRAWSTYLE_BACKGROUND;
		Subgraph_BgGreen.LineWidth = 15;
		Subgraph_BgGreen.PrimaryColor = RGB(0, 120, 0);
		Subgraph_BgGreen.DrawZeros = true;

		Subgraph_BgRed.Name = "Red Background";
		Subgraph_BgRed.DrawStyle = DRAWSTYLE_BACKGROUND;
		Subgraph_BgRed.LineWidth = 15;
		Subgraph_BgRed.PrimaryColor = RGB(120, 0, 0);
		Subgraph_BgRed.DrawZeros = true;

		Subgraph_Dot.Name = "Dots";
		Subgraph_Dot.DrawStyle = DRAWSTYLE_HIDDEN;
		Subgraph_Dot.LineWidth = 5;
		Subgraph_Dot.PrimaryColor = RGB(255, 255, 255);
		Subgraph_Dot.DrawZeros = true;

		Subgraph_Txt.Name = "Text";
		Subgraph_Txt.DrawStyle = DRAWSTYLE_TEXT_WITH_BACKGROUND; //DRAWSTYLE_CUSTOM_VALUE_AT_Y;
		Subgraph_Txt.LineWidth = 9;
		Subgraph_Txt.PrimaryColor = RGB(255, 255, 255);
		Subgraph_Txt.DrawZeros = true;

		return;
	}

#pragma endregion

	if (sc.IsFullRecalculation && sc.Index == 0)
	{
		sc.GetPersistentInt(1) = 0;
		sc.GetPersistentInt(2) = 0;
		sc.GetPersistentInt(3) = 0;
		sc.GetPersistentInt(4) = 0;
		sc.GetPersistentInt(5) = 0;
		sc.GetPersistentInt(6) = 0;
	}

	sc.MACD(sc.BaseData[SC_LAST], Subgraph_Calc, i, 3, 10, 16, MOVAVGTYPE_SIMPLE);

	float MACD = Subgraph_Calc[i];
	float MACDMA = Subgraph_Calc.Arrays[2][i];
	float MACDDifference = Subgraph_Calc.Arrays[3][i];

	Subgraph_MACD[i] = MACD;
	Subgraph_MA[i] = MACDMA;

	// MACD above the MA
	if (Subgraph_MACD[i] > Subgraph_MA[i])
	{
		if (Subgraph_MACD[i] > iH)
			iH = Subgraph_MACD[i];

		if (Subgraph_MACD[i] > iHigh && iH != -999 && iHPainted == 0)
		{
			Subgraph_BgGreen[i] = MACD;
			iHPainted = 1;
		}
		else
			Subgraph_BgGreen[i] = 0;
	}

	// MACD below MA
	if (Subgraph_MACD[i] < Subgraph_MA[i])
	{
		if (Subgraph_MACD[i] < iL)
			iL = Subgraph_MACD[i];

		if (Subgraph_MACD[i] < iLow && iL != 999 && iLPainted == 0)
		{
			Subgraph_BgRed[i] = MACD;
			iLPainted = 1;
		}
	}

	if (sc.CrossOver(Subgraph_Calc, Subgraph_Calc.Arrays[2]) == CROSS_FROM_TOP)
	{
		iHigh = iH;
		iL = 999;
		iHPainted = 0;
	}

	if (sc.CrossOver(Subgraph_Calc, Subgraph_Calc.Arrays[2]) == CROSS_FROM_BOTTOM)
	{
		iH = -999;
		iLow = iL;
		iLPainted = 0;
	}

}

#pragma region DTS SCALPER

SCSFExport scsf_DTS_Scalper(SCStudyInterfaceRef sc)
{
	int i = sc.Index;

	SCSubgraphRef Subgraph_DotUp = sc.Subgraph[0];
	SCSubgraphRef Subgraph_DotDown = sc.Subgraph[1];
	SCSubgraphRef Subgraph_GreenLine = sc.Subgraph[2];
	SCSubgraphRef Subgraph_RedLine = sc.Subgraph[3];
	SCSubgraphRef Subgraph_Winner = sc.Subgraph[4];
	SCSubgraphRef Subgraph_VolCurr = sc.Subgraph[5];
	SCSubgraphRef Subgraph_VolPrev = sc.Subgraph[6];
	SCSubgraphRef Subgraph_Close = sc.Subgraph[7];

	if (sc.SetDefaults)
	{
		sc.GraphName = "DTS Scalper";
		sc.GraphRegion = 1;
		sc.AutoLoop = 1;

		return;
	}

	Subgraph_Close.Name = "Close";
	Subgraph_Close.DrawStyle = DRAWSTYLE_LINE;
	Subgraph_Close.PrimaryColor = RGB(128, 0, 0);
	Subgraph_Close.DrawZeros = true;

	Subgraph_GreenLine.Name = "Green Line";
	Subgraph_GreenLine.PrimaryColor = RGB(0, 180, 0);
	Subgraph_GreenLine.DrawStyle = DRAWSTYLE_LINE;
	Subgraph_GreenLine.LineWidth = 2;

	Subgraph_RedLine.Name = "Red Line";
	Subgraph_RedLine.PrimaryColor = RGB(180, 0, 0);
	Subgraph_RedLine.DrawStyle = DRAWSTYLE_LINE;
	Subgraph_RedLine.LineWidth = 2;

	Subgraph_DotUp.Name = "Standard Buy Dot";
	Subgraph_DotUp.PrimaryColor = RGB(0, 255, 0);
	Subgraph_DotUp.DrawStyle = DRAWSTYLE_POINT;
	Subgraph_DotUp.LineWidth = 4;
	Subgraph_DotUp.DrawZeros = false;

	Subgraph_DotDown.Name = "Standard Sell Dot";
	Subgraph_DotDown.PrimaryColor = RGB(255, 0, 0);
	Subgraph_DotDown.DrawStyle = DRAWSTYLE_POINT;
	Subgraph_DotDown.LineWidth = 4;
	Subgraph_DotDown.DrawZeros = false;

	Subgraph_Winner.Name = "Green or Red Dominant";
	Subgraph_Winner.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
	Subgraph_Winner.PrimaryColor = RGB(255, 255, 255);
	Subgraph_Winner.SecondaryColorUsed = true;
	Subgraph_Winner.SecondaryColor = RGB(0, 167, 0);
	Subgraph_Winner.LineWidth = 11;
	Subgraph_Winner.DrawZeros = false;

	Subgraph_VolCurr.Name = "Volume Current";
	Subgraph_VolCurr.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
	Subgraph_VolCurr.PrimaryColor = RGB(0, 0, 0);
	Subgraph_VolCurr.SecondaryColorUsed = true;
	Subgraph_VolCurr.SecondaryColor = RGB(255, 255, 255);
	Subgraph_VolCurr.LineWidth = 11;
	Subgraph_VolCurr.DrawZeros = false;

	Subgraph_VolPrev.Name = "Volume Previous";
	Subgraph_VolPrev.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
	Subgraph_VolPrev.PrimaryColor = RGB(0, 0, 0);
	Subgraph_VolPrev.SecondaryColorUsed = true;
	Subgraph_VolPrev.SecondaryColor = RGB(255, 255, 255);
	Subgraph_VolPrev.LineWidth = 11;
	Subgraph_VolPrev.DrawZeros = false;

	if ((sc.High[i] - sc.Low[i]) == 0)
		return;

	double green = sc.Volume[i] * (sc.Close[i] - sc.Low[i]) / (sc.High[i] - sc.Low[i]);
	double red = sc.Volume[i] * (sc.High[i] - sc.Close[i]) / (sc.High[i] - sc.Low[i]);
	green += sc.BidVolume[i];
	red += sc.AskVolume[i];
	double dTotalVolume = green + red;
	if (dTotalVolume == 0)
		return;

	Subgraph_DotUp[i] = green;
	Subgraph_DotDown[i] = red;

	Subgraph_GreenLine[i] = green;
	Subgraph_RedLine[i] = red;

	//sc.p_GDIFunction = DrawToChart;
	SCString t;

	if (green > red)
	{
		Subgraph_Winner.SecondaryColor = RGB(0, 140, 0);
		t.Format("Prev: %0.0f     Vol: %0.0f     Buyers %0.0f %%", sc.Volume[i - 1], dTotalVolume, (green / dTotalVolume) * 100);
		sc.AddAndManageSingleTextUserDrawnDrawingForStudy(sc, true, 90, 85, Subgraph_Winner, false, t, true, 1);
	}
	else
	{
		Subgraph_Winner.SecondaryColor = RGB(140, 0, 0);
		t.Format("Prev: %0.0f     Vol: %0.0f     Sellers %0.0f %%", sc.Volume[i - 1], dTotalVolume, (red / dTotalVolume) * 100);
		sc.AddAndManageSingleTextUserDrawnDrawingForStudy(sc, true, 90, 85, Subgraph_Winner, false, t, true, 1);
	}
}

#pragma endregion

void DrawToChart(HWND WindowHandle, HDC DeviceContext, SCStudyInterfaceRef sc)
{
	sc.Graphics.SetTextAlign(TA_NOUPDATECP);

	n_ACSIL::s_GraphicsFont GraphicsFont;
	GraphicsFont.m_Height = 16;
	GraphicsFont.m_Weight = FW_BOLD;
	sc.Graphics.SetTextFont(GraphicsFont);

	n_ACSIL::s_GraphicsColor GraphicsColor;

	GraphicsColor.SetRGB(64, 128, 0);
	sc.Graphics.SetBackgroundColor(GraphicsColor);
	sc.Graphics.DrawTextAt("Hello. Sierra Chart is the best.", 30, 30);

	GraphicsColor.SetRGB(255, 0, 0);
	sc.Graphics.SetBackgroundColor(GraphicsColor);
	sc.Graphics.DrawTextAt("Test.", 30, 50);

	GraphicsColor.SetRGB(255, 255, 0);
	sc.Graphics.SetBackgroundColor(GraphicsColor);
	sc.Graphics.DrawTextAt("Test.", 30, 70);

	return;
	/*
		s_UseTool Tool;
		Tool.ChartNumber = sc.ChartNumber;
		Tool.DrawingType = DRAWING_TEXT;
		Tool.BeginValue = 95; // from bottom
		Tool.BeginDateTime = iLeft; // from left side 90=start
		Tool.UseRelativeVerticalValues = true;
		Tool.Region = sc.GraphRegion;
		Tool.TextAlignment = DT_TOP | DT_TOP;
		Tool.Color = RGB(255, 255, 255);
		Tool.FontBackColor = c;
		Tool.FontSize = 12;
		Tool.FontBold = TRUE;
		Tool.Text = t;
		Tool.AddMethod = UTAM_ADD_ALWAYS;
		sc.UseTool(Tool);
	*/
}

