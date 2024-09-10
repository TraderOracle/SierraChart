#include "sierrachart.h" 

SCDLLName("Renko GOAT DLL")

#pragma region RABBIT

SCSFExport scsf_RabbitWatcher(SCStudyInterfaceRef sc)
{
	SCString txt;

	SCInputRef Input_MACDSlow = sc.Input[1];
	SCInputRef Input_MACDFast = sc.Input[2];
	SCInputRef Input_MACDLength = sc.Input[3];
	SCInputRef Input_EMA = sc.Input[4];
	SCInputRef Input_MovingAverageType = sc.Input[5];

	SCSubgraphRef Subgraph_HopUp = sc.Subgraph[0];
	SCSubgraphRef Subgraph_HopDown = sc.Subgraph[1];
	SCSubgraphRef Subgraph_EMA = sc.Subgraph[2];
	SCSubgraphRef Subgraph_Calc = sc.Subgraph[3];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Rabbit Watcher";
		sc.GraphRegion = 0;
		sc.AutoLoop = 1;

		Input_MACDSlow.Name = "MACD Fast";
		Input_MACDSlow.SetInt(12);

		Input_MACDFast.Name = "MACD Slow";
		Input_MACDFast.SetInt(26);

		Input_MACDLength.Name = "MACD Length";
		Input_MACDLength.SetInt(9);

		Input_EMA.Name = "Moving Average Length";
		Input_EMA.SetInt(100);

		Input_MovingAverageType.Name = "Moving Average Type";
		Input_MovingAverageType.SetMovAvgType(MOVAVGTYPE_EXPONENTIAL);

		// =======================================================================

		Subgraph_HopUp.Name = "Buy Hop";
		Subgraph_HopUp.PrimaryColor = RGB(0, 255, 0);
		Subgraph_HopUp.DrawStyle = DRAWSTYLE_COLOR_BAR;
		Subgraph_HopUp.LineWidth = 4;
		Subgraph_HopUp.DrawZeros = false;

		Subgraph_HopDown.Name = "Sell Hop";
		Subgraph_HopDown.PrimaryColor = RGB(255, 0, 0);
		Subgraph_HopDown.DrawStyle = DRAWSTYLE_COLOR_BAR;
		Subgraph_HopDown.LineWidth = 4;
		Subgraph_HopDown.DrawZeros = false;

		Subgraph_EMA.Name = "Moving Average";
		Subgraph_EMA.PrimaryColor = RGB(255, 255, 0);
		Subgraph_EMA.DrawStyle = DRAWSTYLE_IGNORE;
		Subgraph_EMA.LineWidth = 2;
		Subgraph_EMA.DrawZeros = false;

		Subgraph_Calc.Name = "Calcs";
		Subgraph_Calc.DrawStyle = DRAWSTYLE_IGNORE;

		return;
	}

	int i = sc.Index;
	sc.ExponentialMovAvg(sc.BaseDataIn[SC_LAST], Subgraph_EMA, i, Input_EMA.GetInt());
	float ema = Subgraph_EMA[i];

	sc.MACD(sc.BaseDataIn[SC_LAST], Subgraph_Calc, i, Input_MACDSlow.GetInt(), Input_MACDFast.GetInt(), Input_MACDLength.GetInt(), Input_MovingAverageType.GetMovAvgType());
	float macd = Subgraph_Calc.Arrays[3][i];
	float pmacd = Subgraph_Calc.Arrays[3][i - 1];

	if (macd > 0 && pmacd < 0 && sc.Close[i] > ema)
	{
		Subgraph_HopUp[i] = sc.Low[i] - (2 * sc.TickSize);
	}

	if (macd < 0 && pmacd > 0 && sc.Close[i] < ema)
	{
		Subgraph_HopDown[i] = sc.High[i] + (2 * sc.TickSize);
	}

}
#pragma endregion

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
	else if (iAboveCandle == -1)
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

#pragma region Renko GOAT

SCSFExport scsf_RenkoGOAT(SCStudyInterfaceRef sc)
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

	SCInputRef Input_BarColor = sc.Input[11];
	SCInputRef Input_BarColorWaddah = sc.Input[12];
	SCInputRef Input_BarColorLinda = sc.Input[13];

	SCInputRef Input_UpOffset = sc.Input[14];
	SCInputRef Input_DownOffset = sc.Input[15];

	SCSubgraphRef Subgraph_DotUp = sc.Subgraph[0];
	SCSubgraphRef Subgraph_DotDown = sc.Subgraph[1];
	SCSubgraphRef Subgraph_SqueezeUp = sc.Subgraph[4];
	SCSubgraphRef Subgraph_SqueezeDown = sc.Subgraph[5];
	SCSubgraphRef Subgraph_Tramp = sc.Subgraph[10];

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
		sc.GraphName = "Renko GOAT";
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

		Input_BarColor.Name = "Bar coloring";
		Input_BarColor.SetCustomInputStrings("None;Waddah;Linda MACD;Supertrend");
		Input_BarColor.SetCustomInputIndex(0);

		Input_BarColorWaddah.Name = "Bar color Waddah offset";
		Input_BarColorWaddah.SetInt(80);

		Input_BarColorLinda.Name = "Bar color LindaMACD offset";
		Input_BarColorLinda.SetInt(40);

		// =======================================================================

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

		Subgraph_Tramp.Name = "Trampoline";
		Subgraph_Tramp.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
		Subgraph_Tramp.PrimaryColor = RGB(0, 0, 0);
		Subgraph_Tramp.SecondaryColor = RGB(169, 205, 252);
		Subgraph_Tramp.LineWidth = 8;

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
			// The SubgraphArray may not exist or is empty. Either way we can not do anything with it.
		}

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

	}

#pragma endregion

}


