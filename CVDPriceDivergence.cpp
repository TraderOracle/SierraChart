//============================================================================
//  CVD / Price Divergence  -  Sierra Chart ACSIL study
//
//  Finds swing pivots on price, compares them against Cumulative Volume Delta
//  at the same pivots, and draws a connecting line on the price region (and
//  optionally on the CVD region) whenever price and CVD disagree.
//
//  Regular bearish : price makes a higher high, CVD makes a lower high
//  Regular bullish : price makes a lower low,  CVD makes a higher low
//  Hidden  bearish : price makes a lower high, CVD makes a higher high
//  Hidden  bullish : price makes a higher low, CVD makes a lower low
//
//  Build:  copy to  <SierraChart>\ACS_Source\  then
//          Analysis >> Build Custom Studies DLL >> Build
//============================================================================

#include "sierrachart.h"

SCDLLName("CVD Divergence")

//----------------------------------------------------------------------------
enum DivergenceTypeEnum
{
	DIV_REGULAR_BEAR = 0,
	DIV_REGULAR_BULL = 1,
	DIV_HIDDEN_BEAR  = 2,
	DIV_HIDDEN_BULL  = 3
};

//----------------------------------------------------------------------------
static void DrawDivergenceLine(SCStudyInterfaceRef sc,
                               int Region,
                               int LineNumber,
                               int BeginIndex,
                               float BeginValue,
                               int EndIndex,
                               float EndValue,
                               COLORREF Color,
                               int LineWidth,
                               SubgraphLineStyles LineStyle)
{
	s_UseTool Tool;
	Tool.Clear();

	Tool.ChartNumber   = sc.ChartNumber;
	Tool.Region        = Region;
	Tool.DrawingType   = DRAWING_LINE;
	Tool.LineNumber    = LineNumber;
	Tool.BeginIndex    = BeginIndex;
	Tool.EndIndex      = EndIndex;
	Tool.BeginValue    = BeginValue;
	Tool.EndValue      = EndValue;
	Tool.Color         = Color;
	Tool.LineWidth     = LineWidth;
	Tool.LineStyle     = LineStyle;
	Tool.AddMethod     = UTAM_ADD_OR_ADJUST;
	Tool.AddAsUserDrawnDrawing = 0;

	sc.UseTool(Tool);
}

//----------------------------------------------------------------------------
static void DrawDivergenceLabel(SCStudyInterfaceRef sc,
                                int Region,
                                int LineNumber,
                                int BarIndex,
                                float Value,
                                const SCString& Text,
                                COLORREF Color,
                                int FontSize)
{
	s_UseTool Tool;
	Tool.Clear();

	Tool.ChartNumber   = sc.ChartNumber;
	Tool.Region        = Region;
	Tool.DrawingType   = DRAWING_TEXT;
	Tool.LineNumber    = LineNumber;
	Tool.BeginIndex    = BarIndex;
	Tool.BeginValue    = Value;
	Tool.Color         = Color;
	Tool.Text          = Text;
	Tool.FontSize      = FontSize;
	Tool.FontBold      = 0;
	Tool.TextAlignment = DT_CENTER | DT_VCENTER;
	Tool.AddMethod     = UTAM_ADD_OR_ADJUST;
	Tool.AddAsUserDrawnDrawing = 0;

	sc.UseTool(Tool);
}

//============================================================================
SCSFExport scsf_CVDPriceDivergence(SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_CVD      = sc.Subgraph[0];
	SCSubgraphRef Subgraph_BullSig  = sc.Subgraph[1];
	SCSubgraphRef Subgraph_BearSig  = sc.Subgraph[2];

	// Persistent working arrays attached to Subgraph_CVD:
	//   Arrays[0][i] = 1 when bar i is a confirmed pivot high
	//   Arrays[1][i] = 1 when bar i is a confirmed pivot low
	SCFloatArrayRef PivotHighFlag = Subgraph_CVD.Arrays[0];
	SCFloatArrayRef PivotLowFlag  = Subgraph_CVD.Arrays[1];

	SCInputRef Input_CVDSource     = sc.Input[0];
	SCInputRef Input_CVDStudy      = sc.Input[1];
	SCInputRef Input_ResetDaily    = sc.Input[2];
	SCInputRef Input_LeftStrength  = sc.Input[3];
	SCInputRef Input_RightStrength = sc.Input[4];
	SCInputRef Input_MinBars       = sc.Input[5];
	SCInputRef Input_MaxBars       = sc.Input[6];
	SCInputRef Input_MinPriceTicks = sc.Input[7];
	SCInputRef Input_MinCVDDiff    = sc.Input[8];
	SCInputRef Input_RegularBear   = sc.Input[9];
	SCInputRef Input_RegularBull   = sc.Input[10];
	SCInputRef Input_HiddenBear    = sc.Input[11];
	SCInputRef Input_HiddenBull    = sc.Input[12];
	SCInputRef Input_CVDRegion     = sc.Input[13];
	SCInputRef Input_LineWidth     = sc.Input[14];
	SCInputRef Input_BearColor     = sc.Input[15];
	SCInputRef Input_BullColor     = sc.Input[16];
	SCInputRef Input_DashHidden    = sc.Input[17];
	SCInputRef Input_ClosedBarOnly = sc.Input[18];
	SCInputRef Input_ArrowOffset   = sc.Input[19];
	SCInputRef Input_ShowLabels    = sc.Input[20];
	SCInputRef Input_EnableAlerts  = sc.Input[21];

	//------------------------------------------------------------------------
	if (sc.SetDefaults)
	{
		sc.GraphName            = "CVD / Price Divergence Lines";
		sc.GraphRegion          = 0;          // must be on the price region
		sc.AutoLoop             = 0;          // manual looping
		sc.ValueFormat          = VALUEFORMAT_INHERITED;
		sc.CalculationPrecedence = LOW_PREC_LEVEL;
		sc.DrawZeros            = 0;
		sc.FreeDLL              = 0;

		Subgraph_CVD.Name         = "CVD (working)";
		Subgraph_CVD.DrawStyle    = DRAWSTYLE_IGNORE;
		Subgraph_CVD.PrimaryColor = RGB(128, 128, 128);
		Subgraph_CVD.DrawZeros    = 0;

		Subgraph_BullSig.Name         = "Bullish Divergence";
		Subgraph_BullSig.DrawStyle    = DRAWSTYLE_ARROW_UP;
		Subgraph_BullSig.PrimaryColor = RGB(0, 255, 0);
		Subgraph_BullSig.LineWidth    = 2;
		Subgraph_BullSig.DrawZeros    = 0;

		Subgraph_BearSig.Name         = "Bearish Divergence";
		Subgraph_BearSig.DrawStyle    = DRAWSTYLE_ARROW_DOWN;
		Subgraph_BearSig.PrimaryColor = RGB(255, 0, 0);
		Subgraph_BearSig.LineWidth    = 2;
		Subgraph_BearSig.DrawZeros    = 0;

		Input_CVDSource.Name = "CVD Source";
		Input_CVDSource.SetCustomInputStrings("Calculate Internally (AskVol - BidVol);Study Subgraph Reference");
		Input_CVDSource.SetCustomInputIndex(0);

		Input_CVDStudy.Name = "  CVD Study and Subgraph";
		Input_CVDStudy.SetStudySubgraphValues(0, 0);

		Input_ResetDaily.Name = "  Reset Internal CVD Each Trading Day";
		Input_ResetDaily.SetYesNo(0);

		Input_LeftStrength.Name = "Pivot Left Strength (bars)";
		Input_LeftStrength.SetInt(5);
		Input_LeftStrength.SetIntLimits(1, 200);

		Input_RightStrength.Name = "Pivot Right Strength (bars)";
		Input_RightStrength.SetInt(5);
		Input_RightStrength.SetIntLimits(1, 200);

		Input_MinBars.Name = "Minimum Bars Between Pivots";
		Input_MinBars.SetInt(5);
		Input_MinBars.SetIntLimits(1, 1000);

		Input_MaxBars.Name = "Maximum Bars Between Pivots";
		Input_MaxBars.SetInt(120);
		Input_MaxBars.SetIntLimits(2, 100000);

		Input_MinPriceTicks.Name = "Minimum Price Difference (ticks)";
		Input_MinPriceTicks.SetFloat(1.0f);

		Input_MinCVDDiff.Name = "Minimum CVD Difference (contracts)";
		Input_MinCVDDiff.SetFloat(1.0f);

		Input_RegularBear.Name = "Detect Regular Bearish (HH price / LH CVD)";
		Input_RegularBear.SetYesNo(1);

		Input_RegularBull.Name = "Detect Regular Bullish (LL price / HL CVD)";
		Input_RegularBull.SetYesNo(1);

		Input_HiddenBear.Name = "Detect Hidden Bearish (LH price / HH CVD)";
		Input_HiddenBear.SetYesNo(0);

		Input_HiddenBull.Name = "Detect Hidden Bullish (HL price / LL CVD)";
		Input_HiddenBull.SetYesNo(0);

		Input_CVDRegion.Name = "Also Draw Line In Region Number (0 based, -1 = off)";
		Input_CVDRegion.SetInt(-1);
		Input_CVDRegion.SetIntLimits(-1, 15);

		Input_LineWidth.Name = "Line Width";
		Input_LineWidth.SetInt(1);
		Input_LineWidth.SetIntLimits(1, 10);

		Input_BearColor.Name = "Bearish Line Color";
		Input_BearColor.SetColor(255, 0, 0);

		Input_BullColor.Name = "Bullish Line Color";
		Input_BullColor.SetColor(0, 255, 0);

		Input_DashHidden.Name = "Use Dashed Lines For Hidden Divergences";
		Input_DashHidden.SetYesNo(1);

		Input_ClosedBarOnly.Name = "Confirm Pivots On Closed Bars Only";
		Input_ClosedBarOnly.SetYesNo(1);

		Input_ArrowOffset.Name = "Arrow Offset From Pivot (ticks)";
		Input_ArrowOffset.SetInt(4);
		Input_ArrowOffset.SetIntLimits(0, 500);

		Input_ShowLabels.Name = "Show Text Labels";
		Input_ShowLabels.SetYesNo(0);

		Input_EnableAlerts.Name = "Enable Alerts";
		Input_EnableAlerts.SetYesNo(0);

		return;
	}

	//------------------------------------------------------------------------
	const int   LeftStrength  = Input_LeftStrength.GetInt();
	const int   RightStrength = Input_RightStrength.GetInt();
	const int   MinBars       = Input_MinBars.GetInt();
	const int   MaxBars       = Input_MaxBars.GetInt();
	const float MinPriceDiff  = Input_MinPriceTicks.GetFloat() * (float)sc.TickSize;
	const float MinCVDDiff    = Input_MinCVDDiff.GetFloat();
	const int   CVDRegion     = Input_CVDRegion.GetInt();
	const int   LineWidth     = Input_LineWidth.GetInt();
	const COLORREF BearColor  = Input_BearColor.GetColor();
	const COLORREF BullColor  = Input_BullColor.GetColor();
	const SubgraphLineStyles HiddenStyle =
		Input_DashHidden.GetYesNo() ? LINESTYLE_DASH : LINESTYLE_SOLID;
	const float ArrowOffset   = Input_ArrowOffset.GetInt() * (float)sc.TickSize;

	//--- Full recalculation: wipe state and remove drawings we own ----------
	if (sc.UpdateStartIndex == 0)
	{
		sc.SetPersistentInt(1, -1);   // highest pivot candidate already evaluated

		for (int i = 0; i < sc.ArraySize; ++i)
		{
			PivotHighFlag[i]   = 0;
			PivotLowFlag[i]    = 0;
			Subgraph_BullSig[i] = 0;
			Subgraph_BearSig[i] = 0;
		}

		// Note: on newer Sierra Chart versions this function is named
		// sc.DeleteACSILChartDrawing() with identical parameters.
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_ALL, 0);
	}

	//--- Build / fetch the CVD series --------------------------------------
	const int CVDSource = Input_CVDSource.GetIndex();

	SCFloatArray ExternalCVD;
	if (CVDSource == 1)
	{
		sc.GetStudyArrayUsingID(Input_CVDStudy.GetStudyID(),
		                        Input_CVDStudy.GetSubgraphIndex(),
		                        ExternalCVD);

		if (ExternalCVD.GetArraySize() == 0)
		{
			sc.AddMessageToLog("CVD Divergence: the referenced CVD study/subgraph is empty. "
			                   "Check the 'CVD Study and Subgraph' input.", 1);
			return;
		}
	}

	const int ResetDaily = Input_ResetDaily.GetYesNo();

	for (int i = sc.UpdateStartIndex; i < sc.ArraySize; ++i)
	{
		if (CVDSource == 1)
		{
			Subgraph_CVD[i] = (i < ExternalCVD.GetArraySize()) ? ExternalCVD[i] : 0.0f;
		}
		else
		{
			const float BarDelta = sc.AskVolume[i] - sc.BidVolume[i];

			if (i == 0 || (ResetDaily && sc.IsNewTradingDay(i)))
				Subgraph_CVD[i] = BarDelta;
			else
				Subgraph_CVD[i] = Subgraph_CVD[i - 1] + BarDelta;
		}
	}

	//--- Pivot detection ----------------------------------------------------
	// A bar P is a confirmed pivot once LeftStrength bars to its left and
	// RightStrength bars to its right exist and fail to exceed it.
	const int ConfirmOffset = Input_ClosedBarOnly.GetYesNo() ? 1 : 0;
	const int LastEvaluated = sc.GetPersistentInt(1);
	const int MaxCandidate  = sc.ArraySize - 1 - RightStrength - ConfirmOffset;

	int StartCandidate = LastEvaluated + 1;
	if (StartCandidate < LeftStrength)
		StartCandidate = LeftStrength;

	for (int P = StartCandidate; P <= MaxCandidate; ++P)
	{
		//------------------------------------------------ pivot high
		bool IsPivotHigh = true;
		for (int k = 1; k <= LeftStrength && IsPivotHigh; ++k)
			if (sc.High[P - k] > sc.High[P])
				IsPivotHigh = false;
		for (int k = 1; k <= RightStrength && IsPivotHigh; ++k)
			if (sc.High[P + k] >= sc.High[P])
				IsPivotHigh = false;

		//------------------------------------------------ pivot low
		bool IsPivotLow = true;
		for (int k = 1; k <= LeftStrength && IsPivotLow; ++k)
			if (sc.Low[P - k] < sc.Low[P])
				IsPivotLow = false;
		for (int k = 1; k <= RightStrength && IsPivotLow; ++k)
			if (sc.Low[P + k] <= sc.Low[P])
				IsPivotLow = false;

		//====================================================================
		//  PIVOT HIGH -> look back for the previous pivot high
		//====================================================================
		if (IsPivotHigh)
		{
			PivotHighFlag[P] = 1;

			int ScanStart = P - MinBars;
			int ScanStop  = P - MaxBars;
			if (ScanStop < 0)
				ScanStop = 0;

			int Prev = -1;
			for (int q = ScanStart; q >= ScanStop; --q)
			{
				if (PivotHighFlag[q] != 0)
				{
					Prev = q;
					break;
				}
			}

			if (Prev >= 0)
			{
				const float Price1 = sc.High[Prev];
				const float Price2 = sc.High[P];
				const float Cvd1   = Subgraph_CVD[Prev];
				const float Cvd2   = Subgraph_CVD[P];

				const float PriceDelta = Price2 - Price1;
				const float CvdDelta   = Cvd2 - Cvd1;

				const bool PriceUp   =  PriceDelta >=  MinPriceDiff;
				const bool PriceDown = -PriceDelta >=  MinPriceDiff;
				const bool CvdUp     =  CvdDelta   >=  MinCVDDiff;
				const bool CvdDown   = -CvdDelta   >=  MinCVDDiff;

				int Type = -1;
				if (Input_RegularBear.GetYesNo() && PriceUp && CvdDown)
					Type = DIV_REGULAR_BEAR;
				else if (Input_HiddenBear.GetYesNo() && PriceDown && CvdUp)
					Type = DIV_HIDDEN_BEAR;

				if (Type >= 0)
				{
					const int  LineBase = P * 16;
					const SubgraphLineStyles Style =
						(Type == DIV_HIDDEN_BEAR) ? HiddenStyle : LINESTYLE_SOLID;

					DrawDivergenceLine(sc, sc.GraphRegion, LineBase + Type,
					                   Prev, Price1, P, Price2,
					                   BearColor, LineWidth, Style);

					if (CVDRegion >= 0)
						DrawDivergenceLine(sc, CVDRegion, LineBase + 4 + Type,
						                   Prev, Cvd1, P, Cvd2,
						                   BearColor, LineWidth, Style);

					if (Input_ShowLabels.GetYesNo())
					{
						SCString Label;
						Label.Format("%s %.0f",
						             (Type == DIV_REGULAR_BEAR) ? "Bear" : "H-Bear",
						             CvdDelta);
						DrawDivergenceLabel(sc, sc.GraphRegion, LineBase + 8 + Type,
						                    P, Price2 + ArrowOffset * 2.0f,
						                    Label, BearColor, 8);
					}

					Subgraph_BearSig[P] = Price2 + ArrowOffset;

					if (Input_EnableAlerts.GetYesNo() && !sc.IsFullRecalculation)
					{
						SCString Msg;
						Msg.Format("%s: bearish CVD divergence (price %s, CVD %+.0f)",
						           sc.Symbol.GetChars(),
						           sc.FormatGraphValue(PriceDelta, sc.BaseGraphValueFormat).GetChars(),
						           CvdDelta);
						sc.AddAlertLine(Msg, 1);
						sc.SetAlert(1, Msg);
					}
				}
			}
		}

		//====================================================================
		//  PIVOT LOW -> look back for the previous pivot low
		//====================================================================
		if (IsPivotLow)
		{
			PivotLowFlag[P] = 1;

			int ScanStart = P - MinBars;
			int ScanStop  = P - MaxBars;
			if (ScanStop < 0)
				ScanStop = 0;

			int Prev = -1;
			for (int q = ScanStart; q >= ScanStop; --q)
			{
				if (PivotLowFlag[q] != 0)
				{
					Prev = q;
					break;
				}
			}

			if (Prev >= 0)
			{
				const float Price1 = sc.Low[Prev];
				const float Price2 = sc.Low[P];
				const float Cvd1   = Subgraph_CVD[Prev];
				const float Cvd2   = Subgraph_CVD[P];

				const float PriceDelta = Price2 - Price1;
				const float CvdDelta   = Cvd2 - Cvd1;

				const bool PriceUp   =  PriceDelta >=  MinPriceDiff;
				const bool PriceDown = -PriceDelta >=  MinPriceDiff;
				const bool CvdUp     =  CvdDelta   >=  MinCVDDiff;
				const bool CvdDown   = -CvdDelta   >=  MinCVDDiff;

				int Type = -1;
				if (Input_RegularBull.GetYesNo() && PriceDown && CvdUp)
					Type = DIV_REGULAR_BULL;
				else if (Input_HiddenBull.GetYesNo() && PriceUp && CvdDown)
					Type = DIV_HIDDEN_BULL;

				if (Type >= 0)
				{
					const int LineBase = P * 16;
					const SubgraphLineStyles Style =
						(Type == DIV_HIDDEN_BULL) ? HiddenStyle : LINESTYLE_SOLID;

					DrawDivergenceLine(sc, sc.GraphRegion, LineBase + Type,
					                   Prev, Price1, P, Price2,
					                   BullColor, LineWidth, Style);

					if (CVDRegion >= 0)
						DrawDivergenceLine(sc, CVDRegion, LineBase + 4 + Type,
						                   Prev, Cvd1, P, Cvd2,
						                   BullColor, LineWidth, Style);

					if (Input_ShowLabels.GetYesNo())
					{
						SCString Label;
						Label.Format("%s %+.0f",
						             (Type == DIV_REGULAR_BULL) ? "Bull" : "H-Bull",
						             CvdDelta);
						DrawDivergenceLabel(sc, sc.GraphRegion, LineBase + 8 + Type,
						                    P, Price2 - ArrowOffset * 2.0f,
						                    Label, BullColor, 8);
					}

					Subgraph_BullSig[P] = Price2 - ArrowOffset;

					if (Input_EnableAlerts.GetYesNo() && !sc.IsFullRecalculation)
					{
						SCString Msg;
						Msg.Format("%s: bullish CVD divergence (price %s, CVD %+.0f)",
						           sc.Symbol.GetChars(),
						           sc.FormatGraphValue(PriceDelta, sc.BaseGraphValueFormat).GetChars(),
						           CvdDelta);
						sc.AddAlertLine(Msg, 1);
						sc.SetAlert(1, Msg);
					}
				}
			}
		}
	}

	if (MaxCandidate > LastEvaluated)
		sc.SetPersistentInt(1, MaxCandidate);
}
