#include "sierrachart.h" 

SCDLLName("Trader Oracle DLL") 

/*==========================================================================*/

SCSFExport scsf_LindaMACD(SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_MACDPos = sc.Subgraph[0];
	SCSubgraphRef Subgraph_MACDNeg = sc.Subgraph[1];
	SCSubgraphRef Subgraph_Calc = sc.Subgraph[2];
	SCSubgraphRef Subgraph_Parabolic = sc.Subgraph[3];

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
	if (SAR < sc.Close[sc.Index])
		Subgraph_MACDPos.PrimaryColor = RGB(0, 255, 0);
	else
		Subgraph_MACDPos.PrimaryColor = RGB(1, 110, 5);

	sc.MACD(sc.BaseData[SC_LAST], Subgraph_Calc, sc.Index, 3, 9, 16, MOVAVGTYPE_SIMPLE); 
	float ix = Subgraph_Calc.Arrays[3][sc.Index];

	if (ix > 0)
		Subgraph_MACDPos[sc.Index] = ix;
	else
		Subgraph_MACDNeg[sc.Index] = ix * -1;
}

/*==========================================================================*/

SCSFExport scsf_WaddahExplosion(SCStudyInterfaceRef sc)
{
    if (sc.SetDefaults)
    {
        sc.GraphName = "Waddah Explosion";

        sc.Subgraph[0].Name = "Ignore";
		sc.Subgraph[0].DrawStyle = DRAWSTYLE_IGNORE;

        sc.Subgraph[1].Name = "Ignore";
		sc.Subgraph[1].DrawStyle = DRAWSTYLE_IGNORE;

        sc.Subgraph[5].Name = "Ignore";
		sc.Subgraph[5].DrawStyle = DRAWSTYLE_IGNORE;
		
        sc.Subgraph[2].Name = "Waddah Positive";
        sc.Subgraph[2].DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		sc.Subgraph[2].LineWidth = 12;
		sc.Subgraph[2].PrimaryColor = COLOR_GREEN;

        sc.Subgraph[3].Name = "Waddah Negative";
        sc.Subgraph[3].DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		sc.Subgraph[3].LineWidth = 12;
		sc.Subgraph[3].PrimaryColor = COLOR_RED;

        sc.Subgraph[4].Name = "Explosion Line";
        sc.Subgraph[4].DrawStyle = DRAWSTYLE_LINE;
		sc.Subgraph[4].LineWidth = 2;
		sc.Subgraph[4].PrimaryColor = COLOR_WHITE;
		sc.Subgraph[4].SecondaryColor = COLOR_RED;

        sc.AutoLoop = 1;

        return;
    }

	sc.BollingerBands(sc.BaseData[SC_LAST], sc.Subgraph[5], 20, 2, MOVAVGTYPE_SIMPLE);
	float UpperBand = sc.Subgraph[5].Arrays[0][sc.Index];
	float LowerBand = sc.Subgraph[5].Arrays[1][sc.Index];
	float e1 = UpperBand - LowerBand;
	sc.Subgraph[4][sc.Index] = e1;

	sc.MovingAverage(sc.BaseDataIn[SC_LAST], sc.Subgraph[0], MOVAVGTYPE_EXPONENTIAL, 20);
	float a1 = sc.Subgraph[0][sc.Index];
	float b1 = sc.Subgraph[0][sc.Index - 1];
	
	sc.MovingAverage(sc.BaseDataIn[SC_LAST], sc.Subgraph[0], MOVAVGTYPE_EXPONENTIAL, 40);
	float a2 = sc.Subgraph[1][sc.Index];
	float b2 = sc.Subgraph[1][sc.Index - 1];

	float t1 = ((a1 - a2) - (b1 - b2)) * 150;
	
	if (t1 > 0)
		sc.Subgraph[2][sc.Index] = t1;
	else
		sc.Subgraph[3][sc.Index] = t1 * -1;
	
    return;
}
