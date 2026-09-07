#include "sierrachart.h"

SCDLLName("VWAP Pullback Trend Continuation")

/*==============================================================================
    VWAP PULLBACK TREND CONTINUATION SYSTEM  (built for NQ, 5-minute chart)

    Logic implemented
    -----------------
    VWAP        : volume weighted average price anchored to the RTH open
                  (09:30 by default) and reset every session.

    Trend (long): Close > VWAP
                  AND VWAP is higher than it was 15 minutes ago
                  AND Close is at least +0.10% vs. 60 minutes ago
                  AND (optional) higher-time-frame trend filter agrees.

    Trend (short): mirror image.

    Trigger     : the FIRST red bar that pulls back into the VWAP zone while the
                  up-trend is intact (first green bar into VWAP for shorts).

    Entry       : market on the open of the next bar, or a stop order through
                  the trigger bar's extreme (selectable).

    Exit        : attached OCO bracket, default 80 point stop / 45 point target.

    Guard rails : no entries before 10:30, none after 15:30, flat by 15:55,
                  max 4 trades/day, max 2 losing trades/day.

    IMPORTANT
    ---------
    All time inputs are read in the CHART's time zone. Set the chart time zone
    to US Eastern (Chart >> Chart Settings >> Advanced Settings >> Time Zone)
    or adjust the time inputs accordingly.
==============================================================================*/


/*------------------------------------------------------------------------------
    Convert a number of minutes into a number of bars on the current chart.
------------------------------------------------------------------------------*/
static int BarsForMinutes(SCStudyInterfaceRef sc, int Minutes)
{
    if (sc.SecondsPerBar <= 0)
        return max(1, Minutes);                 // non time-based chart: treat as bars

    const int Bars = static_cast<int>((Minutes * 60.0) / sc.SecondsPerBar + 0.5);
    return max(1, Bars);
}

/*------------------------------------------------------------------------------
    Long trigger bar: a down (red) bar that pulled back into the VWAP zone
    from above without collapsing through it.
------------------------------------------------------------------------------*/
static bool IsLongPullbackBar(SCStudyInterfaceRef sc, SCFloatArrayRef VWAP, int Index,
                              float ProximityPoints, float MaxPenetrationPoints)
{
    if (Index < 1 || Index >= sc.ArraySize)
        return false;

    if (VWAP[Index] == 0.0f)                                        // outside anchored session
        return false;

    if (sc.BaseData[SC_LAST][Index] >= sc.BaseData[SC_OPEN][Index]) // must be a red bar
        return false;

    const float LowDistance = sc.BaseData[SC_LOW][Index] - VWAP[Index];

    if (LowDistance > ProximityPoints)                              // never reached the VWAP zone
        return false;

    if (LowDistance < -MaxPenetrationPoints)                        // sliced too deep through VWAP
        return false;

    if (sc.BaseData[SC_LAST][Index] < VWAP[Index] - MaxPenetrationPoints) // closed too far below
        return false;

    return true;
}

/*------------------------------------------------------------------------------
    Short trigger bar: an up (green) bar that rallied back into the VWAP zone
    from below without breaking out above it.
------------------------------------------------------------------------------*/
static bool IsShortPullbackBar(SCStudyInterfaceRef sc, SCFloatArrayRef VWAP, int Index,
                               float ProximityPoints, float MaxPenetrationPoints)
{
    if (Index < 1 || Index >= sc.ArraySize)
        return false;

    if (VWAP[Index] == 0.0f)
        return false;

    if (sc.BaseData[SC_LAST][Index] <= sc.BaseData[SC_OPEN][Index]) // must be a green bar
        return false;

    const float HighDistance = VWAP[Index] - sc.BaseData[SC_HIGH][Index];

    if (HighDistance > ProximityPoints)
        return false;

    if (HighDistance < -MaxPenetrationPoints)
        return false;

    if (sc.BaseData[SC_LAST][Index] > VWAP[Index] + MaxPenetrationPoints)
        return false;

    return true;
}


/*==============================================================================
                                   STUDY
==============================================================================*/
SCSFExport scsf_VWAPPullbackTrendSystem(SCStudyInterfaceRef sc)
{
    /*--- Subgraphs -----------------------------------------------------------*/
    SCSubgraphRef Subgraph_VWAP        = sc.Subgraph[0];
    SCSubgraphRef Subgraph_LongSignal  = sc.Subgraph[1];
    SCSubgraphRef Subgraph_ShortSignal = sc.Subgraph[2];
    SCSubgraphRef Subgraph_HTFAvg      = sc.Subgraph[3];

    /*--- Internal working arrays --------------------------------------------*/
    SCFloatArrayRef Array_CumPV        = Subgraph_VWAP.Arrays[0];  // sum((P - Anchor) * V)
    SCFloatArrayRef Array_CumVolume    = Subgraph_VWAP.Arrays[1];
    SCFloatArrayRef Array_AnchorPrice  = Subgraph_VWAP.Arrays[2];
    SCFloatArrayRef Array_SessionStart = Subgraph_VWAP.Arrays[3];  // bar index of the anchor bar
    SCFloatArrayRef Array_Signal       = Subgraph_VWAP.Arrays[4];  // +1 long, -1 short
    SCFloatArrayRef Array_Trend        = Subgraph_VWAP.Arrays[5];  // +1 up, -1 down

    /*--- Inputs --------------------------------------------------------------*/
    SCInputRef Input_PriceType            = sc.Input[0];
    SCInputRef Input_AnchorTime           = sc.Input[1];
    SCInputRef Input_SessionEndTime       = sc.Input[2];
    SCInputRef Input_FirstEntryTime       = sc.Input[3];
    SCInputRef Input_LastEntryTime        = sc.Input[4];
    SCInputRef Input_FlattenTime          = sc.Input[5];
    SCInputRef Input_SlopeMinutes         = sc.Input[6];
    SCInputRef Input_MomentumMinutes      = sc.Input[7];
    SCInputRef Input_MinMomentumPercent   = sc.Input[8];
    SCInputRef Input_PullbackProximity    = sc.Input[9];
    SCInputRef Input_MaxPenetration       = sc.Input[10];
    SCInputRef Input_FirstPullbackOnly    = sc.Input[11];
    SCInputRef Input_TrendFilterMode      = sc.Input[12];
    SCInputRef Input_HTFAvgLength         = sc.Input[13];
    SCInputRef Input_HTFStudySubgraph     = sc.Input[14];
    SCInputRef Input_EntryMethod          = sc.Input[15];
    SCInputRef Input_StopEntryOffsetTicks = sc.Input[16];
    SCInputRef Input_EntryOrderBarsValid  = sc.Input[17];
    SCInputRef Input_OrderQuantity        = sc.Input[18];
    SCInputRef Input_StopLossPoints       = sc.Input[19];
    SCInputRef Input_TargetPoints         = sc.Input[20];
    SCInputRef Input_MaxTradesPerDay      = sc.Input[21];
    SCInputRef Input_MaxLossesPerDay      = sc.Input[22];
    SCInputRef Input_ArrowOffsetTicks     = sc.Input[23];

    /*--- Persistent state ----------------------------------------------------*/
    int& PersistTradingDate       = sc.GetPersistentInt(0);
    int& TradesToday              = sc.GetPersistentInt(1);
    int& LossesToday              = sc.GetPersistentInt(2);
    int& LastProcessedSignalIndex = sc.GetPersistentInt(3);
    int& PreviousPositionQuantity = sc.GetPersistentInt(4);
    int& EntryOrderID             = sc.GetPersistentInt(5);
    int& EntryOrderBarIndex       = sc.GetPersistentInt(6);


    /*==========================================================================
                                  DEFAULTS
    ==========================================================================*/
    if (sc.SetDefaults)
    {
        sc.GraphName = "VWAP Pullback Trend Continuation System";
        sc.StudyDescription =
            "Anchored-VWAP pullback continuation system. Trades the first pullback "
            "bar into VWAP in the direction of an established trend, with a bracket "
            "exit and daily trade/loss guard rails. https://youtu.be/wm4A6qo0g3I";

        sc.GraphRegion   = 0;
        sc.AutoLoop      = 0;                       // manual looping
        sc.ValueFormat   = VALUEFORMAT_INHERITED;
        sc.CalculationPrecedence = LOW_PREC_LEVEL;

        /*--- Trading behaviour ---*/
        sc.SendOrdersToTradeService = false;        // simulated until you enable it
        sc.AllowMultipleEntriesInSameDirection = false;
        sc.MaximumPositionAllowed = 1000;
        sc.SupportReversals = false;
        sc.AllowOppositeEntryWithOpposingPositionOrOrders = false;
        sc.SupportAttachedOrdersForTrading = true;
        sc.UseGUIAttachedOrderSetting = false;
        sc.CancelAllWorkingOrdersOnExit = true;
        sc.AllowEntryWithWorkingOrders = false;
        sc.AllowOnlyOneTradePerBar = true;
        sc.MaintainTradeStatisticsAndTradesData = true;

        /*--- Subgraph appearance ---*/
        Subgraph_VWAP.Name = "Anchored VWAP";
        Subgraph_VWAP.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_VWAP.PrimaryColor = RGB(255, 200, 0);
        Subgraph_VWAP.LineWidth = 2;
        Subgraph_VWAP.DrawZeros = false;

        Subgraph_LongSignal.Name = "Long Trigger";
        Subgraph_LongSignal.DrawStyle = DRAWSTYLE_ARROW_UP;
        Subgraph_LongSignal.PrimaryColor = RGB(0, 220, 100);
        Subgraph_LongSignal.LineWidth = 11;
        Subgraph_LongSignal.DrawZeros = false;

        Subgraph_ShortSignal.Name = "Short Trigger";
        Subgraph_ShortSignal.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        Subgraph_ShortSignal.PrimaryColor = RGB(230, 60, 60);
        Subgraph_ShortSignal.LineWidth = 11;
        Subgraph_ShortSignal.DrawZeros = false;

        Subgraph_HTFAvg.Name = "HTF Trend Average";
        Subgraph_HTFAvg.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_HTFAvg.PrimaryColor = RGB(120, 160, 255);
        Subgraph_HTFAvg.DrawZeros = false;

        /*--- Input defaults ---*/
        Input_PriceType.Name = "VWAP Price Input";
        Input_PriceType.SetInputDataIndex(SC_HLC_AVG);

        Input_AnchorTime.Name = "VWAP Anchor Time (Chart Time Zone)";
        Input_AnchorTime.SetTime(HMS_TIME(9, 30, 0));

        Input_SessionEndTime.Name = "Session End Time";
        Input_SessionEndTime.SetTime(HMS_TIME(16, 0, 0));

        Input_FirstEntryTime.Name = "Earliest Entry Time";
        Input_FirstEntryTime.SetTime(HMS_TIME(10, 30, 0));

        Input_LastEntryTime.Name = "Latest Entry Time";
        Input_LastEntryTime.SetTime(HMS_TIME(15, 30, 0));

        Input_FlattenTime.Name = "Flatten And Cancel Time";
        Input_FlattenTime.SetTime(HMS_TIME(15, 55, 0));

        Input_SlopeMinutes.Name = "VWAP Slope Lookback (Minutes)";
        Input_SlopeMinutes.SetInt(15);
        Input_SlopeMinutes.SetIntLimits(1, 480);

        Input_MomentumMinutes.Name = "Momentum Lookback (Minutes)";
        Input_MomentumMinutes.SetInt(60);
        Input_MomentumMinutes.SetIntLimits(1, 480);

        Input_MinMomentumPercent.Name = "Minimum Momentum Move (%)";
        Input_MinMomentumPercent.SetFloat(0.10f);
        Input_MinMomentumPercent.SetFloatLimits(0.0f, 10.0f);

        Input_PullbackProximity.Name = "Pullback Proximity To VWAP (Points)";
        Input_PullbackProximity.SetFloat(10.0f);

        Input_MaxPenetration.Name = "Maximum VWAP Penetration (Points)";
        Input_MaxPenetration.SetFloat(5.0f);

        Input_FirstPullbackOnly.Name = "Only Take First Pullback Bar";
        Input_FirstPullbackOnly.SetYesNo(true);

        Input_TrendFilterMode.Name = "Higher Time Frame Trend Filter";
        Input_TrendFilterMode.SetCustomInputStrings(
            "None (VWAP slope + momentum only);Internal Moving Average;External Chart Study");
        Input_TrendFilterMode.SetCustomInputIndex(1);

        Input_HTFAvgLength.Name = "Internal Trend Average Length (Chart Bars)";
        Input_HTFAvgLength.SetInt(60);          // 60 x 5 min = 20 x 15-minute bars
        Input_HTFAvgLength.SetIntLimits(2, 1000);

        Input_HTFStudySubgraph.Name = "External 15-Minute Trend Study";
        Input_HTFStudySubgraph.SetChartStudySubgraphValues(0, 0, 0);

        Input_EntryMethod.Name = "Entry Method";
        Input_EntryMethod.SetCustomInputStrings(
            "Market At Next Bar Open;Stop Through Trigger Bar Extreme");
        Input_EntryMethod.SetCustomInputIndex(0);

        Input_StopEntryOffsetTicks.Name = "Stop Entry Offset (Ticks)";
        Input_StopEntryOffsetTicks.SetInt(1);
        Input_StopEntryOffsetTicks.SetIntLimits(0, 100);

        Input_EntryOrderBarsValid.Name = "Stop Entry Order Valid For (Bars)";
        Input_EntryOrderBarsValid.SetInt(2);
        Input_EntryOrderBarsValid.SetIntLimits(1, 50);

        Input_OrderQuantity.Name = "Order Quantity";
        Input_OrderQuantity.SetInt(1);
        Input_OrderQuantity.SetIntLimits(1, 1000);

        Input_StopLossPoints.Name = "Stop Loss (Points)";
        Input_StopLossPoints.SetFloat(80.0f);

        Input_TargetPoints.Name = "Profit Target (Points)";
        Input_TargetPoints.SetFloat(45.0f);

        Input_MaxTradesPerDay.Name = "Maximum Trades Per Day";
        Input_MaxTradesPerDay.SetInt(4);
        Input_MaxTradesPerDay.SetIntLimits(1, 100);

        Input_MaxLossesPerDay.Name = "Maximum Losing Trades Per Day";
        Input_MaxLossesPerDay.SetInt(2);
        Input_MaxLossesPerDay.SetIntLimits(1, 100);

        Input_ArrowOffsetTicks.Name = "Signal Arrow Offset (Ticks)";
        Input_ArrowOffsetTicks.SetInt(8);
        Input_ArrowOffsetTicks.SetIntLimits(0, 200);

        return;
    }


    /*==========================================================================
                              RESOLVE INPUTS
    ==========================================================================*/
    const int AnchorSeconds      = Input_AnchorTime.GetTime();
    const int SessionEndSeconds  = Input_SessionEndTime.GetTime();
    const int FirstEntrySeconds  = Input_FirstEntryTime.GetTime();
    const int LastEntrySeconds   = Input_LastEntryTime.GetTime();
    const int FlattenSeconds     = Input_FlattenTime.GetTime();

    const int   PriceDataIndex   = Input_PriceType.GetInputDataIndex();
    const int   SlopeBars        = BarsForMinutes(sc, Input_SlopeMinutes.GetInt());
    const int   MomentumBars     = BarsForMinutes(sc, Input_MomentumMinutes.GetInt());
    const float MomentumMinimum  = Input_MinMomentumPercent.GetFloat() / 100.0f;
    const float ProximityPoints  = Input_PullbackProximity.GetFloat();
    const float MaxPenetration   = Input_MaxPenetration.GetFloat();
    const bool  FirstPullbackOnly= Input_FirstPullbackOnly.GetYesNo() != 0;
    const int   TrendFilterMode  = Input_TrendFilterMode.GetIndex();
    const int   HTFAvgLength     = Input_HTFAvgLength.GetInt();
    const int   EntryMethod      = Input_EntryMethod.GetIndex();
    const int   BarSeconds       = max(1, sc.SecondsPerBar);

    /*--- External higher time frame study (only when that mode is selected) ---*/
    SCFloatArray HTFStudyArray;
    int HTFChartNumber = 0;

    if (TrendFilterMode == 2)
    {
        const s_ChartStudySubgraphValues ChartStudySubgraph =
            Input_HTFStudySubgraph.GetChartStudySubgraphValues();

        HTFChartNumber = ChartStudySubgraph.ChartNumber;
        sc.GetStudyArrayFromChartUsingID(ChartStudySubgraph, HTFStudyArray);
    }

    /*--- Reset persistent state on a full recalculation ---*/
    if (sc.UpdateStartIndex == 0)
    {
        PersistTradingDate       = 0;
        TradesToday              = 0;
        LossesToday              = 0;
        LastProcessedSignalIndex = -1;
        PreviousPositionQuantity = 0;
        EntryOrderID             = 0;
        EntryOrderBarIndex       = -1;
    }


    /*==========================================================================
                    PER-BAR CALCULATIONS: VWAP, TREND, TRIGGERS
    ==========================================================================*/
    for (int Index = sc.UpdateStartIndex; Index < sc.ArraySize; Index++)
    {
        // The internal trend average must be advanced on every bar so the
        // exponential recursion stays continuous.
        if (TrendFilterMode == 1)
            sc.ExponentialMovAvg(sc.BaseData[SC_LAST], Subgraph_HTFAvg, Index, HTFAvgLength);

        Subgraph_LongSignal[Index]  = 0.0f;
        Subgraph_ShortSignal[Index] = 0.0f;
        Array_Signal[Index]         = 0.0f;
        Array_Trend[Index]          = 0.0f;

        const SCDateTime BarDateTime = sc.BaseDateTimeIn[Index];
        const int BarTimeSeconds = BarDateTime.GetTimeInSeconds();
        const int BarDate        = BarDateTime.GetDate();

        /*--- Outside the anchored session: no VWAP, no signals ---*/
        if (BarTimeSeconds < AnchorSeconds || BarTimeSeconds >= SessionEndSeconds)
        {
            Array_CumPV[Index]        = 0.0f;
            Array_CumVolume[Index]    = 0.0f;
            Array_AnchorPrice[Index]  = 0.0f;
            Array_SessionStart[Index] = -1.0f;
            Subgraph_VWAP[Index]      = 0.0f;
            continue;
        }

        /*--- Detect the first bar of the anchored session ---*/
        bool IsNewAnchor = true;
        if (Index > 0)
        {
            const int PreviousTimeSeconds = sc.BaseDateTimeIn[Index - 1].GetTimeInSeconds();
            const int PreviousDate        = sc.BaseDateTimeIn[Index - 1].GetDate();

            if (PreviousDate == BarDate
                && PreviousTimeSeconds >= AnchorSeconds
                && PreviousTimeSeconds <  SessionEndSeconds)
            {
                IsNewAnchor = false;
            }
        }

        const float BarPrice  = sc.BaseData[PriceDataIndex][Index];
        const float BarVolume = sc.BaseData[SC_VOLUME][Index];

        /*--- Accumulate VWAP. Price deviations from the anchor price are summed
              rather than raw price*volume, which keeps float precision high. ---*/
        if (IsNewAnchor)
        {
            Array_AnchorPrice[Index]  = BarPrice;
            Array_SessionStart[Index] = static_cast<float>(Index);
            Array_CumPV[Index]        = 0.0f;
            Array_CumVolume[Index]    = BarVolume;
        }
        else
        {
            Array_AnchorPrice[Index]  = Array_AnchorPrice[Index - 1];
            Array_SessionStart[Index] = Array_SessionStart[Index - 1];
            Array_CumPV[Index]        = Array_CumPV[Index - 1]
                                      + (BarPrice - Array_AnchorPrice[Index]) * BarVolume;
            Array_CumVolume[Index]    = Array_CumVolume[Index - 1] + BarVolume;
        }

        if (Array_CumVolume[Index] > 0.0f)
            Subgraph_VWAP[Index] = Array_AnchorPrice[Index]
                                 + Array_CumPV[Index] / Array_CumVolume[Index];
        else
            Subgraph_VWAP[Index] = BarPrice;

        /*--- Need enough same-session history for the slope and momentum tests ---*/
        const int SessionStartIndex = static_cast<int>(Array_SessionStart[Index]);

        if (SessionStartIndex < 0
            || Index - SlopeBars    < SessionStartIndex
            || Index - MomentumBars < SessionStartIndex)
        {
            continue;
        }

        const float ClosePrice     = sc.BaseData[SC_LAST][Index];
        const float VWAPNow        = Subgraph_VWAP[Index];
        const float VWAPEarlier    = Subgraph_VWAP[Index - SlopeBars];
        const float CloseEarlier   = sc.BaseData[SC_LAST][Index - MomentumBars];

        const float VWAPSlope      = VWAPNow - VWAPEarlier;
        const float MomentumChange = (CloseEarlier != 0.0f)
                                   ? (ClosePrice - CloseEarlier) / CloseEarlier
                                   : 0.0f;

        /*--- Higher time frame trend filter ---*/
        bool HTFAllowsLong  = true;
        bool HTFAllowsShort = true;

        if (TrendFilterMode == 1)
        {
            const float AverageNow     = Subgraph_HTFAvg[Index];
            const float AverageEarlier = Subgraph_HTFAvg[max(0, Index - SlopeBars)];

            HTFAllowsLong  = (ClosePrice > AverageNow) && (AverageNow > AverageEarlier);
            HTFAllowsShort = (ClosePrice < AverageNow) && (AverageNow < AverageEarlier);
        }
        else if (TrendFilterMode == 2)
        {
            HTFAllowsLong  = false;
            HTFAllowsShort = false;

            if (HTFStudyArray.GetArraySize() > 1 && HTFChartNumber > 0)
            {
                const int RefIndex = sc.GetContainingIndexForDateTimeIndex(HTFChartNumber, Index);

                if (RefIndex > 0 && RefIndex < HTFStudyArray.GetArraySize())
                {
                    const float RefNow     = HTFStudyArray[RefIndex];
                    const float RefEarlier = HTFStudyArray[RefIndex - 1];

                    HTFAllowsLong  = (ClosePrice > RefNow) && (RefNow > RefEarlier);
                    HTFAllowsShort = (ClosePrice < RefNow) && (RefNow < RefEarlier);
                }
            }
        }

        const bool TrendIsUp = (ClosePrice > VWAPNow)
                            && (VWAPSlope > 0.0f)
                            && (MomentumChange >= MomentumMinimum)
                            && HTFAllowsLong;

        const bool TrendIsDown = (ClosePrice < VWAPNow)
                              && (VWAPSlope < 0.0f)
                              && (MomentumChange <= -MomentumMinimum)
                              && HTFAllowsShort;

        Array_Trend[Index] = TrendIsUp ? 1.0f : (TrendIsDown ? -1.0f : 0.0f);

        /*--- Entry on the following bar must fall inside the trading window ---*/
        const int ProjectedEntryTime = BarTimeSeconds + BarSeconds;

        if (ProjectedEntryTime < FirstEntrySeconds || ProjectedEntryTime > LastEntrySeconds)
            continue;

        /*--- Trigger bar ---*/
        if (TrendIsUp)
        {
            const bool ThisBarQualifies = IsLongPullbackBar(sc, Subgraph_VWAP, Index,
                                                            ProximityPoints, MaxPenetration);
            const bool PriorBarQualified = IsLongPullbackBar(sc, Subgraph_VWAP, Index - 1,
                                                             ProximityPoints, MaxPenetration);

            if (ThisBarQualifies && (!FirstPullbackOnly || !PriorBarQualified))
            {
                Array_Signal[Index] = 1.0f;
                Subgraph_LongSignal[Index] = sc.BaseData[SC_LOW][Index]
                                           - Input_ArrowOffsetTicks.GetInt() * sc.TickSize;
            }
        }
        else if (TrendIsDown)
        {
            const bool ThisBarQualifies = IsShortPullbackBar(sc, Subgraph_VWAP, Index,
                                                             ProximityPoints, MaxPenetration);
            const bool PriorBarQualified = IsShortPullbackBar(sc, Subgraph_VWAP, Index - 1,
                                                              ProximityPoints, MaxPenetration);

            if (ThisBarQualifies && (!FirstPullbackOnly || !PriorBarQualified))
            {
                Array_Signal[Index] = -1.0f;
                Subgraph_ShortSignal[Index] = sc.BaseData[SC_HIGH][Index]
                                            + Input_ArrowOffsetTicks.GetInt() * sc.TickSize;
            }
        }
    }


    /*==========================================================================
                          ORDER AND RISK MANAGEMENT
    ==========================================================================*/
    if (sc.ArraySize < 3)
        return;

    const int LastIndex   = sc.ArraySize - 1;   // forming bar
    const int SignalIndex = sc.ArraySize - 2;   // last fully closed bar

    s_SCPositionData PositionData;
    sc.GetTradePosition(PositionData);

    const int CurrentPositionQuantity = static_cast<int>(PositionData.PositionQuantity);
    const int CurrentBarDate          = sc.BaseDateTimeIn[LastIndex].GetDate();
    const int CurrentBarTimeSeconds   = sc.BaseDateTimeIn[LastIndex].GetTimeInSeconds();

    /*--- New day: reset the daily counters -----------------------------------*/
    if (PersistTradingDate != CurrentBarDate)
    {
        PersistTradingDate = CurrentBarDate;
        TradesToday        = 0;
        LossesToday        = 0;
        EntryOrderID       = 0;
        EntryOrderBarIndex = -1;
    }

    /*--- A trade just closed: count it if it was a loss -----------------------*/
    if (PreviousPositionQuantity != 0 && CurrentPositionQuantity == 0)
    {
        if (PositionData.LastTradeProfitLoss < 0.0)
        {
            LossesToday++;

            SCString LogMessage;
            LogMessage.Format("VWAP Pullback: losing trade closed (P/L %.2f). Losses today: %d of %d.",
                              PositionData.LastTradeProfitLoss, LossesToday,
                              Input_MaxLossesPerDay.GetInt());
            sc.AddMessageToLog(LogMessage, 0);
        }
    }
    PreviousPositionQuantity = CurrentPositionQuantity;

    /*--- End of day: flatten and cancel ---------------------------------------*/
    if (CurrentBarTimeSeconds >= FlattenSeconds)
    {
        if (CurrentPositionQuantity != 0
            || PositionData.PositionQuantityWithAllWorkingOrders != 0)
        {
            sc.FlattenAndCancelAllOrders();
        }
        return;
    }

    /*--- Manage an unfilled stop entry order ----------------------------------*/
    if (EntryOrderID != 0)
    {
        s_SCTradeOrder OrderDetails;

        if (sc.GetOrderByOrderID(EntryOrderID, OrderDetails) != 0)
        {
            const bool OrderIsWorking =
                   OrderDetails.OrderStatusCode == SCT_OSC_OPEN
                || OrderDetails.OrderStatusCode == SCT_OSC_ORDERSENT
                || OrderDetails.OrderStatusCode == SCT_OSC_PENDINGOPEN;

            if (OrderIsWorking)
            {
                if (LastIndex - EntryOrderBarIndex > Input_EntryOrderBarsValid.GetInt())
                {
                    sc.CancelOrder(EntryOrderID);
                    EntryOrderID = 0;

                    if (TradesToday > 0)        // it never filled, return the trade slot
                        TradesToday--;
                }
            }
            else
            {
                EntryOrderID = 0;               // filled, cancelled or rejected
            }
        }
        else
        {
            EntryOrderID = 0;
        }
    }

    /*--- Guard rails -----------------------------------------------------------*/
    if (CurrentPositionQuantity != 0)
        return;                                         // already in a trade

    if (PositionData.PositionQuantityWithAllWorkingOrders != 0)
        return;                                         // entry order still working

    if (TradesToday >= Input_MaxTradesPerDay.GetInt())
        return;

    if (LossesToday >= Input_MaxLossesPerDay.GetInt())
        return;

    if (CurrentBarTimeSeconds < FirstEntrySeconds || CurrentBarTimeSeconds > LastEntrySeconds)
        return;

    /*--- Evaluate each closed bar only once ------------------------------------*/
    if (LastProcessedSignalIndex == SignalIndex)
        return;

    LastProcessedSignalIndex = SignalIndex;

    const float Signal = Array_Signal[SignalIndex];

    if (Signal == 0.0f)
        return;

    /*--- Submit the entry with its attached bracket ----------------------------*/
    s_SCNewOrder NewOrder;
    NewOrder.OrderQuantity = Input_OrderQuantity.GetInt();
    NewOrder.TimeInForce   = SCT_TIF_DAY;
    NewOrder.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;
    NewOrder.AttachedOrderStop1Type   = SCT_ORDERTYPE_STOP;
    NewOrder.Target1Offset = sc.RoundToTickSize(Input_TargetPoints.GetFloat(),   sc.TickSize);
    NewOrder.Stop1Offset   = sc.RoundToTickSize(Input_StopLossPoints.GetFloat(), sc.TickSize);

    if (EntryMethod == 0)
    {
        NewOrder.OrderType = SCT_ORDERTYPE_MARKET;
    }
    else
    {
        const float TriggerOffset = Input_StopEntryOffsetTicks.GetInt() * sc.TickSize;

        NewOrder.OrderType = SCT_ORDERTYPE_STOP;
        NewOrder.Price1 = (Signal > 0.0f)
            ? sc.RoundToTickSize(sc.BaseData[SC_HIGH][SignalIndex] + TriggerOffset, sc.TickSize)
            : sc.RoundToTickSize(sc.BaseData[SC_LOW][SignalIndex]  - TriggerOffset, sc.TickSize);
    }

    const int Result = (Signal > 0.0f)
        ? static_cast<int>(sc.BuyEntry(NewOrder))
        : static_cast<int>(sc.SellEntry(NewOrder));

    if (Result > 0)
    {
        TradesToday++;
        EntryOrderID       = NewOrder.InternalOrderID;
        EntryOrderBarIndex = LastIndex;

        SCString LogMessage;
        LogMessage.Format("VWAP Pullback: %s entry submitted. VWAP %.2f, trigger bar close %.2f. "
                          "Trade %d of %d today, %d loss(es) so far.",
                          (Signal > 0.0f) ? "LONG" : "SHORT",
                          Subgraph_VWAP[SignalIndex],
                          sc.BaseData[SC_LAST][SignalIndex],
                          TradesToday, Input_MaxTradesPerDay.GetInt(), LossesToday);
        sc.AddMessageToLog(LogMessage, 0);
    }
    else if (Result < 0)
    {
        SCString LogMessage;
        LogMessage.Format("VWAP Pullback: entry rejected, error code %d.", Result);
        sc.AddMessageToLog(LogMessage, 1);
    }
}