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
	SCSubgraphRef Subgraph_WaddahPos = sc.Subgraph[0];
	SCSubgraphRef Subgraph_WaddahNeg = sc.Subgraph[1];
	SCSubgraphRef Subgraph_Explosion = sc.Subgraph[2];	
	SCSubgraphRef Subgraph_Slow = sc.Subgraph[3];
	SCSubgraphRef Subgraph_Fast = sc.Subgraph[4];
	SCSubgraphRef Subgraph_BB = sc.Subgraph[5];


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
		Subgraph_WaddahPos.PrimaryColor = COLOR_GREEN;

        Subgraph_WaddahNeg.Name = "Waddah Negative";
        Subgraph_WaddahNeg.DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		Subgraph_WaddahNeg.LineWidth = 12;
		Subgraph_WaddahNeg.PrimaryColor = COLOR_RED;

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
		Subgraph_WaddahPos[sc.Index] = t1;
		Subgraph_WaddahNeg[sc.Index] = 0;
	}
	else
	{
		Subgraph_WaddahNeg[sc.Index] = t1 * -1;
		Subgraph_WaddahPos[sc.Index] = 0;
	}
	
    return;
}
