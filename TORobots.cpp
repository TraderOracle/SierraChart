#include "sierrachart.h" 

SCDLLName("TO Robots DLL")

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

void DrawStatusText(SCStudyInterfaceRef sc)
{
	return;

	SCString sF;

	int& r_DrawingNumber = sc.GetPersistentInt(5);
	SCString& r_CurrentOnScreenMessage = sc.GetPersistentSCString(1);

	s_SCPositionData SCPositionData;
	if (sc.GetTradePosition(SCPositionData))
	{
		sF.Format("GoldBug Bot - Version 1.0 \nDaily PNL: %02f, Open PNL: %02f \n",
			SCPositionData.DailyProfitLoss, SCPositionData.OpenProfitLoss);
		sF.Append(r_CurrentOnScreenMessage);
	}

	//n_ACSIL::s_TradeAccountDataFields TradeAccountDataFields;
	//if (sc.GetTradeAccountData(TradeAccountDataFields, sc.SelectedTradeAccount))
	//{
	//	double AccountValue = TradeAccountDataFields.m_AccountValue;
	//	double dailyPnl = TradeAccountDataFields.m_DailyProfitLoss;
	//	double openPnl = TradeAccountDataFields.m_OpenPositionsProfitLoss;
	//	double cash = TradeAccountDataFields.m_CurrentCashBalance;

	//	sF.Format("GoldBug Bot - version 1.0 \nDaily PNL: %0f, Open PNL: %0f \nCash available: %0f \n",
	//		dailyPnl, openPnl, cash);
	//	sF.Append(r_CurrentOnScreenMessage);
	//}

	s_UseTool Tool;
	Tool.Clear();
	Tool.ChartNumber = sc.ChartNumber;
	Tool.DrawingType = DRAWING_TEXT;
	Tool.BeginDateTime = 5;
	Tool.Region = sc.GraphRegion;
	Tool.BeginValue = 95;
	Tool.UseRelativeVerticalValues = true;
	Tool.Color = RGB(255, 255, 255);
	Tool.FontBold = false;

	Tool.Text = sF;
	Tool.FontSize = 10;
	Tool.AddMethod = UTAM_ADD_OR_ADJUST;
	Tool.ReverseTextColor = false;

	if (r_DrawingNumber != 0)
		Tool.LineNumber = r_DrawingNumber;

	if (sc.UseTool(Tool))
		r_DrawingNumber = Tool.LineNumber;
}

void LogInfo(SCStudyInterfaceRef sc)
{
	SCString& r_CurrentOnScreenMessage = sc.GetPersistentSCString(1);

	DrawStatusText(sc);
	sc.AddMessageToLog(r_CurrentOnScreenMessage, 0);
}

#pragma endregion

#pragma region ORDER MANIPULATION

int OrderPizza(SCStudyInterfaceRef sc, int iDirection, int MaxPos)
{
	int& orderBar = sc.GetPersistentInt(2);
	if (sc.Index == orderBar)
		return -3;

	int iR;
	s_SCPositionData PositionData;

	sc.GetTradePosition(PositionData);

	//if (PositionData.PositionQuantity == 0)
	{
		s_SCNewOrder NewOrder;
		NewOrder.OrderQuantity = 1;
		NewOrder.OrderType = SCT_ORDERTYPE_MARKET;
		NewOrder.TimeInForce = SCT_TIF_GOOD_TILL_CANCELED;

		if (iDirection == 1)
			iR = static_cast<int>(sc.BuyEntry(NewOrder));
		else
			iR = static_cast<int>(sc.SellEntry(NewOrder));
		sc.AddMessageToLog("Submitted trade order", 0);
		return -2;
	}

	if (iR > 0)//order was accepted
	{
		
	}
	else//order error
	{
		if (sc.Index == sc.ArraySize - 1)
		{
			// log error
		}
	}

	return 0;
}

#pragma endregion

#pragma region GOLDBUG

SCSFExport scsf_GoldBug(SCStudyInterfaceRef sc)
{

#pragma region INPUTS

	SCString txt;

	SCInputRef Input_Enabled = sc.Input[0];
	SCInputRef Input_Simulation = sc.Input[1];

	SCInputRef Input_TradeBuySell = sc.Input[2];
	SCInputRef Input_TradeImbalance = sc.Input[3];
	SCInputRef Input_TradeEngulfBB = sc.Input[4];
	SCInputRef Input_TradeFVG = sc.Input[5];

	SCInputRef Input_Aggressive = sc.Input[6];

	SCInputRef Input_MaxPositions = sc.Input[7];
	SCInputRef Input_MaxLoss = sc.Input[8];
	SCInputRef Input_MaxProfit = sc.Input[8];
	SCInputRef Input_IgnoreDoji = sc.Input[9];

	SCInputRef Input_UseWaddah = sc.Input[10];
	SCInputRef Input_UseMacd = sc.Input[11];
	SCInputRef Input_UseSar = sc.Input[12];
	SCInputRef Input_UseSuperTrend = sc.Input[13];
	SCInputRef Input_UseAO = sc.Input[14];
	SCInputRef Input_UseHMA = sc.Input[15];
	SCInputRef Input_UseT3 = sc.Input[16];
	SCInputRef Input_UseFisher = sc.Input[17];
	SCInputRef Input_WaddahExploding = sc.Input[18];

	SCInputRef Input_ADX = sc.Input[19];

	SCInputRef Input_WaddahIntensity = sc.Input[20];

	///////////////////////////////////////////////////////////////

	SCSubgraphRef Subgraph_WaddahPos = sc.Subgraph[0];
	SCSubgraphRef Subgraph_WaddahNeg = sc.Subgraph[1];
	SCSubgraphRef Subgraph_Slow = sc.Subgraph[2];
	SCSubgraphRef Subgraph_Fast = sc.Subgraph[3];
	SCSubgraphRef Subgraph_BB = sc.Subgraph[4];
	SCSubgraphRef Subgraph_ColorBar = sc.Subgraph[5];
	SCSubgraphRef Subgraph_ColorUp = sc.Subgraph[6];
	SCSubgraphRef Subgraph_ColorDown = sc.Subgraph[7];
	SCSubgraphRef Subgraph_LindaMACD = sc.Subgraph[8];
	SCSubgraphRef Subgraph_Parabolic = sc.Subgraph[9];
	SCSubgraphRef Subgraph_AO = sc.Subgraph[10];
	SCSubgraphRef Subgraph_Fisher = sc.Subgraph[11];
	SCSubgraphRef Subgraph_ADX = sc.Subgraph[12];
	SCSubgraphRef Subgraph_T3 = sc.Subgraph[13];
	SCSubgraphRef Subgraph_HMA = sc.Subgraph[14];
	SCSubgraphRef Subgraph_Calc = sc.Subgraph[15];
	SCSubgraphRef Subgraph_MomentumHist = sc.Subgraph[16];
	SCSubgraphRef Subgraph_MomentumHistUpColors = sc.Subgraph[17];
	SCSubgraphRef Subgraph_MomentumHistDownColors = sc.Subgraph[18];
	SCSubgraphRef Subgraph_SuperTrend = sc.Subgraph[19];
	SCSubgraphRef Subgraph_HullATR = sc.Subgraph[20];
	SCSubgraphRef Subgraph_KAMA = sc.Subgraph[21];

	SCSubgraphRef Subgraph_Txt = sc.Subgraph[22];

	SCFloatArrayRef Array_TrueRange = Subgraph_SuperTrend.Arrays[0];
	SCFloatArrayRef Array_AvgTrueRange = Subgraph_SuperTrend.Arrays[1];
	SCFloatArrayRef Array_UpperBandBasic = Subgraph_SuperTrend.Arrays[2];
	SCFloatArrayRef Array_LowerBandBasic = Subgraph_SuperTrend.Arrays[3];
	SCFloatArrayRef Array_UpperBand = Subgraph_SuperTrend.Arrays[4];
	SCFloatArrayRef Array_LowerBand = Subgraph_SuperTrend.Arrays[5];

#pragma endregion

#pragma region DEFAULTS

	if (sc.SetDefaults)
	{
		sc.GraphName = "GoldBug";
		sc.GraphRegion = 0;
		sc.AutoLoop = 1;

		Input_Enabled.Name = "Enabled";
		Input_Enabled.SetYesNo(1);
		Input_Enabled.SetDescription("This input enables the study and allows it to function. Otherwise, it does nothing.");

		Input_Simulation.Name = "Send Orders To Trade Service";
		Input_Simulation.SetYesNo(0);
		Input_Simulation.SetDescription("Send real orders to your trading service");

		Input_TradeBuySell.Name = "Standard Buy/Sell";
		Input_TradeBuySell.SetYesNo(1);
		Input_TradeBuySell.SetDescription("Trade standard buy/sell signals");

		Input_TradeImbalance.Name = "Trade Volume Imbalances";
		Input_TradeImbalance.SetYesNo(1);
		Input_TradeImbalance.SetDescription("Trade candle gaps (volume imbalances)");

		Input_TradeEngulfBB.Name = "Trade Engulfing Candle off Bollinger Bands";
		Input_TradeEngulfBB.SetYesNo(0);
		Input_TradeEngulfBB.SetDescription("Are we bouncing off a bollinger band, and have an engulfing candle?");

		Input_TradeFVG.Name = "Trade Fair Value Gaps";
		Input_TradeFVG.SetYesNo(1);
		Input_TradeFVG.SetDescription("Open trade when fair value gap is formed");

		Input_Aggressive.Name = "Aggressive Mode";
		Input_Aggressive.SetYesNo(0);
		Input_Aggressive.SetDescription("Trades on every same-colored-candle, then bails when opposite colored candle closes");

		Input_MaxPositions.Name = "Maximum Positions";
		Input_MaxPositions.SetInt(20);
		Input_MaxPositions.SetDescription("Maximum number of simultaneous open positions allowed");

		Input_MaxLoss.Name = "Max Loss";
		Input_MaxLoss.SetInt(5000);
		Input_MaxLoss.SetDescription("Maximum loss in dollars");

		Input_MaxProfit.Name = "Max Profit";
		Input_MaxProfit.SetInt(20000);
		Input_MaxProfit.SetDescription("Maximum profit in dollars");

		Input_IgnoreDoji.Name = "Ignore Dojis";
		Input_IgnoreDoji.SetYesNo(1);
		Input_IgnoreDoji.SetDescription("Don't trade when wicks are larger than candle body");

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

		Input_WaddahExploding.Name = "Only when Waddah Exploding";
		Input_WaddahExploding.SetYesNo(1);

		Input_UseT3.Name = "Use T3";
		Input_UseT3.SetYesNo(0);

		Input_ADX.Name = "Minimum ADX";
		Input_ADX.SetInt(0);

		Subgraph_KAMA.Name = "KAMA";
		Subgraph_LindaMACD.DrawStyle = DRAWSTYLE_IGNORE;

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

		Subgraph_Txt.Name = "Text Output 1";
		Subgraph_Txt.DrawStyle = DRAWSTYLE_TEXT;
		Subgraph_Txt.PrimaryColor = RGB(255, 255, 255);
		Subgraph_Txt.LineWidth = 10;
		Subgraph_Txt.DrawZeros = false;

		sc.SendOrdersToTradeService = false;
		sc.AllowMultipleEntriesInSameDirection = true;
		sc.MaximumPositionAllowed = 10000;
		sc.SupportReversals = true;
		sc.AllowOppositeEntryWithOpposingPositionOrOrders = true;
		sc.SupportAttachedOrdersForTrading = true;
		sc.UseGUIAttachedOrderSetting = false;
		sc.CancelAllOrdersOnEntries = false;
		sc.CancelAllOrdersOnReversals = true;
		sc.AllowEntryWithWorkingOrders = true;
		sc.CancelAllWorkingOrdersOnExit = true;
		sc.AllowOnlyOneTradePerBar = true;
		sc.MaintainTradeStatisticsAndTradesData = true;

		return;
	}

#pragma endregion

	sc.SendOrdersToTradeService = Input_Simulation.GetYesNo();
	sc.MaximumPositionAllowed = Input_MaxPositions.GetInt();

	if (!Input_Enabled.GetYesNo())
		return;

	if (sc.IsFullRecalculation)
		return;

	if (sc.LastCallToFunction)
		return;

	if (Input_IgnoreDoji.GetYesNo() == SC_YES && 
		BodyLength(sc.BaseData, sc.Index) < LowerWickLength(sc.BaseData, sc.Index) &&
		BodyLength(sc.BaseData, sc.Index) < UpperWickLength(sc.BaseData, sc.Index))
		return;

	SCString sF;
	SCString& r_Msg = sc.GetPersistentSCString(1);
	int& currBar = sc.GetPersistentInt(1);

	s_SCPositionData SCPositionData;
	if (sc.GetTradePosition(SCPositionData))
	{
		double totalPNL = abs(SCPositionData.DailyProfitLoss) + abs(SCPositionData.OpenProfitLoss);
		if (totalPNL > Input_MaxLoss.GetInt())
		{
			r_Msg = "You exceeded your max loss";
			return;
		}
		if (totalPNL > Input_MaxProfit.GetInt())
		{
			r_Msg = "You achieved your profit target";
			return;
		}
		sF.Format("GoldBug - Version 1.2 \nDaily PNL: %.02f, Open PNL: %.02f \n", SCPositionData.DailyProfitLoss, SCPositionData.OpenProfitLoss);
		sF.Append(r_Msg);
		sc.AddAndManageSingleTextUserDrawnDrawingForStudy(sc, true, 5, 90, Subgraph_Txt, true, sF, true, 1);
	}

	int i = sc.Index;
	currBar = sc.Index;
	int iPos = 0;
	int& r_SqueezeUp = sc.GetPersistentInt(0);
	int cl = sc.GetBarHasClosedStatus(i); // BHCS_BAR_HAS_NOT_CLOSED
	SCBaseDataRef in = sc.BaseData;
	double close = in[SC_OPEN][i];
	SCFloatArrayRef Price = sc.BaseData[SC_HL_AVG];
	SCFloatArrayRef Array_Value = Subgraph_Calc.Arrays[0];

#pragma region INDICATORS

		bool BarCloseStatus = false;
		bool sqRelaxUp;
		bool bSuperUp;

		if (i < sc.ArraySize - 1)
			BarCloseStatus = true;

		int FVGMinTickSize = 1;
		float L1 = sc.Low[i];
		float H1 = sc.High[i];
		float L3 = sc.Low[i - 2];
		float H3 = sc.High[i - 2];

		bool FVGUp = (H3 < L1) && (L1 - H3 >= FVGMinTickSize);
		bool FVGDn = (L3 > H1) && (L3 - H1 >= FVGMinTickSize);

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
				r_SqueezeUp = 1;
			}
		}
		else if ((Subgraph_MomentumHist[i] >= 0) && (Subgraph_MomentumHist[i] < Subgraph_MomentumHist[i - 1]))
		{
			if (r_SqueezeUp == 1) {
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

		if (!sc.ChartTradeModeEnabled)
		{
			r_Msg = "Chart Trade Mode is not Enabled";
			LogInfo(sc);
			return;
		}

		if (sc.TradingIsLocked)
		{
			r_Msg = "Trading is locked, fix it before continuing";
			LogInfo(sc);
			return;
		}

#pragma endregion

#pragma region BUY SELL

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
			(Input_UseSuperTrend.GetYesNo() == SC_YES && !bSuperUp) ||
			(Input_WaddahExploding.GetYesNo() == SC_YES && t1 < e1)
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
			(Input_UseSuperTrend.GetYesNo() == SC_YES && bSuperUp) ||
			(Input_WaddahExploding.GetYesNo() == SC_YES && t1 < e1)
			)
			bShowDown = false;

		if (BarCloseStatus && bShowUp)
		{
			if (Input_TradeBuySell.GetYesNo() == SC_YES)
				iPos = OrderPizza(sc, 1, Input_MaxPositions.GetInt());
			txt.Format("Standard BUY Signal at %.2f", sc.Low[sc.Index]);
			r_Msg = txt;
			LogInfo(sc);
			if (sc.IsNewBar(i))
				sc.AlertWithMessage(199, "Standard BUY Signal");
		}

		if (BarCloseStatus && bShowDown)
		{
			if (Input_TradeBuySell.GetYesNo() == SC_YES)
				iPos = OrderPizza(sc, -1, Input_MaxPositions.GetInt());
			txt.Format("Standard SELL Signal at %.2f", sc.Low[sc.Index]);
			r_Msg = txt;
			LogInfo(sc);
			if (sc.IsNewBar(i))
				sc.AlertWithMessage(200, "Standard SELL Signal");
		}

		if (BarCloseStatus && IsVolImbGreen(sc, sc.CurrentIndex))
		{
			if (Input_TradeImbalance.GetYesNo() == SC_YES)
				iPos = OrderPizza(sc, 1, Input_MaxPositions.GetInt());
			txt.Format("Volume Imbalance BUY at %.2f", sc.Low[sc.Index]);
			r_Msg = txt;
			LogInfo(sc);
			if (sc.IsNewBar(i))
				sc.AlertWithMessage(197, "Volume Imbalance BUY");
		}

		if (BarCloseStatus && IsVolImbRed(sc, sc.CurrentIndex))
		{
			if (Input_TradeImbalance.GetYesNo() == SC_YES)
				iPos = OrderPizza(sc, -1, Input_MaxPositions.GetInt());
			txt.Format("Volume Imbalance SELL at %.2f", sc.Low[sc.Index]);
			r_Msg = txt;
			LogInfo(sc);
			if (sc.IsNewBar(i))
				sc.AlertWithMessage(198, "Volume Imbalance SELL");
		}

		if (BarCloseStatus && FVGUp)
		{
			if (Input_TradeFVG.GetYesNo() == SC_YES)
				iPos = OrderPizza(sc, 1, Input_MaxPositions.GetInt());
			txt.Format("Fair Value Gap BUY at %.2f", sc.Low[sc.Index]);
			r_Msg = txt;
			LogInfo(sc);
			if (sc.IsNewBar(i))
				sc.AlertWithMessage(197, "Fair Value Gap BUY");
		}

		if (BarCloseStatus && FVGDn)
		{
			if (Input_TradeFVG.GetYesNo() == SC_YES)
				iPos = OrderPizza(sc, -1, Input_MaxPositions.GetInt());
			txt.Format("Fair Value Gap SELL at %.2f", sc.Low[sc.Index]);
			r_Msg = txt;
			LogInfo(sc);
			if (sc.IsNewBar(i))
				sc.AlertWithMessage(198, "Fair Value Gap SELL");
		}

#pragma endregion

}

#pragma endregion

