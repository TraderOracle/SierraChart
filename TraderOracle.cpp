#include "sierrachart.h" 

SCDLLName("Trader Oracle DLL") 

struct s_CandleStickPatternsFinderSettings
{
	int PriceRangeNumberOfBars;
	double	PriceRangeMultiplier;
	int UseTrendDetection;

	s_CandleStickPatternsFinderSettings()
	{
		// default values
		PriceRangeNumberOfBars	= 100;
		PriceRangeMultiplier		= 0.01;
		UseTrendDetection = true;
	}
};

inline double CandleLength(SCBaseDataRef InData, int index);
inline double BodyLength(SCBaseDataRef InData, int index);
inline double UpperWickLength(SCBaseDataRef InData, int index);
inline double LowerWickLength(SCBaseDataRef InData, int index);
inline bool IsRed(SCBaseDataRef InData, int index);
inline bool IsGreen(SCBaseDataRef InData, int index);
inline double PercentOfCandleLength(SCBaseDataRef InData, int index, double percent);
inline double PercentOfBodyLength(SCBaseDataRef InData, int index, double percent);
inline bool IsUpperWickSmall(SCBaseDataRef InData, int index, double percent);
inline bool IsLowerWickSmall(SCBaseDataRef InData, int index, double percent);
inline bool IsNearEqual(double value1, double value2, SCBaseDataRef InData, int index, double percent);

bool IsThreeInsideUp(SCStudyInterfaceRef sc, const s_CandleStickPatternsFinderSettings& settings, int index); 
bool IsThreeOutsideUp(SCStudyInterfaceRef sc, const s_CandleStickPatternsFinderSettings& settings, int index); 
bool IsThreeInsideDown(SCStudyInterfaceRef sc, const s_CandleStickPatternsFinderSettings& settings, int index); 
bool IsThreeOutsideDown(SCStudyInterfaceRef sc, const s_CandleStickPatternsFinderSettings& settings, int index);

inline bool IsGreen(SCBaseDataRef InData, int index)
{ return InData[SC_LAST][index]>InData[SC_OPEN][index]; }

inline bool IsRed(SCBaseDataRef InData, int index)
{ return InData[SC_LAST][index]<InData[SC_OPEN][index]; }

inline bool IsNearEqual(double value1, double value2, SCBaseDataRef InData, int index, double percent)
{ return abs(value1 - value2) < PercentOfCandleLength(InData, index, percent); }

inline bool IsUpperWickSmall(SCBaseDataRef InData, int index, double percent)
{ return UpperWickLength(InData, index) < PercentOfCandleLength(InData, index, percent); }

inline bool IsLowerWickSmall(SCBaseDataRef InData, int index, double percent)
{ return LowerWickLength(InData, index) < PercentOfCandleLength(InData, index, percent); }

inline double CandleLength(SCBaseDataRef InData, int index)
{ return InData[SC_HIGH][index]-InData[SC_LOW][index]; }

inline double BodyLength(SCBaseDataRef InData, int index)
{ return fabs(InData[SC_OPEN][index] - InData[SC_LAST][index]); }

inline double PercentOfCandleLength(SCBaseDataRef InData, int index, double percent)
{ return CandleLength(InData, index) * (percent / 100.0); }

inline double PercentOfBodyLength(SCBaseDataRef InData, int index, double percent)
{ return BodyLength(InData, index) * percent / 100.0; }

inline double UpperWickLength(SCBaseDataRef InData, int index)
{
	double upperBoundary = max(InData[SC_LAST][index], InData[SC_OPEN][index]);
	double upperWickLength = InData[SC_HIGH][index] - upperBoundary;
	return upperWickLength;
}

inline double LowerWickLength(SCBaseDataRef InData, int index)
{
	double lowerBoundary = min(InData[SC_LAST][index], InData[SC_OPEN][index]);
	double lowerWickLength = lowerBoundary - InData[SC_LOW][index];
	return lowerWickLength;
}

bool IsBullishEngulfing(SCStudyInterfaceRef sc, const s_CandleStickPatternsFinderSettings& settings, int index)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;

		if(InData[SC_LAST][index-1] < InData[SC_OPEN][index-1] && InData[SC_LAST][index] > InData[SC_OPEN][index])
		{
			if ((InData[SC_HIGH][index] > InData[SC_HIGH][index - 1]) &&
				(InData[SC_LOW][index] < InData[SC_LOW][index - 1]) && 
				(InData[SC_LAST][index] > InData[SC_OPEN][index - 1]) &&
				(InData[SC_OPEN][index] < InData[SC_LAST][index - 1]))
				ret_flag = true;	
		}
	return ret_flag;
}

bool IsBearishEngulfing(SCStudyInterfaceRef sc, const s_CandleStickPatternsFinderSettings& settings, int index)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;

		if(InData[SC_LAST][index-1] > InData[SC_OPEN][index-1] && InData[SC_LAST][index] < InData[SC_OPEN][index])
		{
			if ((InData[SC_HIGH][index] > InData[SC_HIGH][index - 1]) &&
				(InData[SC_LOW][index] < InData[SC_LOW][index - 1]) && 
				(InData[SC_OPEN][index] > InData[SC_LAST][index - 1]) &&
				(InData[SC_LAST][index] < InData[SC_OPEN][index - 1]))
				ret_flag = true;	
		}
	
	return ret_flag;
}

bool IsThreeOutsideUp(SCStudyInterfaceRef sc, const s_CandleStickPatternsFinderSettings& settings, int index)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;

		if (IsGreen(InData, index) &&	(InData[SC_LAST][index]>InData[SC_LAST][index-1])) 
		{
			if(IsBullishEngulfing(sc,settings,index-1))
				ret_flag = true;
		}

	return ret_flag;
}

bool IsThreeOutsideDown(SCStudyInterfaceRef sc, const s_CandleStickPatternsFinderSettings& settings, int index)
{
	SCBaseDataRef InData = sc.BaseData;
	bool ret_flag = false;

		if(IsRed(InData, index) && (InData[SC_LAST][index]<InData[SC_LAST][index-1]))
		{
			if(IsBearishEngulfing(sc,settings,index-1))
				ret_flag = true;
		}

	return ret_flag;
}

SCSFExport scsf_Olympus(SCStudyInterfaceRef sc)
{

}

/*==========================================================================*/

SCSFExport scsf_KaufmanEfficiencyRatio(SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_KER = sc.Subgraph[0];

	SCInputRef Input_InputData = sc.Input[0];
	SCInputRef   Input_Length = sc.Input[1];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Kaufman Efficiency Ratio";

		sc.AutoLoop = 1;

		Subgraph_KER.Name = "KER";
		Subgraph_KER.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_KER.PrimaryColor = RGB(0, 255, 0);
		Subgraph_KER.DrawZeros = true;

		Input_InputData.Name = "Input Data";
		Input_InputData.SetInputDataIndex(SC_LAST);

		Input_Length.Name = "Length";
		Input_Length.SetInt(20);
		Input_Length.SetIntLimits(1, MAX_STUDY_LENGTH);

		return;
	}

	// Compute Direction
	float Direction = fabs(sc.BaseDataIn[Input_InputData.GetInputDataIndex()][sc.Index] - sc.BaseDataIn[Input_InputData.GetInputDataIndex()][sc.Index - Input_Length.GetInt()]);

	// Compute Volatility
	float Volatility = 0.0f;

	for (int i = 0; i < Input_Length.GetInt(); i++)
		Volatility += fabs(sc.BaseDataIn[Input_InputData.GetInputDataIndex()][sc.Index - i] - sc.BaseDataIn[Input_InputData.GetInputDataIndex()][sc.Index - i - 1]);

	// Compute KER
	if (Volatility == 0.0f)
		Subgraph_KER[sc.Index] = 0.0f;
	else
		Subgraph_KER[sc.Index] = Direction / Volatility;
}

/*==========================================================================*/

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


/*==========================================================================*/

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

/*==========================================================================*/

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

/*==========================================================================*/

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
