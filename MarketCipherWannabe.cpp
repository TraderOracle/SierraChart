//============================================================================
//  VuManChu Cipher B Divergences  -  Sierra Chart ACSIL port
//
//  Ported from the Pine Script v4 study "VuManChu B Divergences"
//  (VMC Cipher_B_Divergences).
//
//  Implemented:
//    - WaveTrend (WT1 / WT2 / Fast WT "VWAP")
//    - RSI + colouring
//    - RSI/MFI money-flow area and money-flow bar
//    - Stochastic RSI (K/D, optional log source, optional K/D average)
//    - Schaff Trend Cycle
//    - Fractal-based regular & hidden divergences on WT2, RSI and Stoch K
//    - Buy / Sell / Divergence / Gold circles
//
//  Not implemented (require higher-timeframe security() calls):
//    - Sommi flags, Sommi diamonds, MACD-based WT colouring
//    See the notes at the bottom of this file.
//
//  Build: place in <SierraChart>/ACS_Source, then
//         Analysis >> Build Custom Studies DLL >> Build
//============================================================================

#include "sierrachart.h"

SCDLLName("Market Cipher Wannabe")

//============================================================================
// Helpers
//============================================================================
namespace VMC
{
    inline float SafeDiv(float Numerator, float Denominator)
    {
        if (Denominator == 0.0f)
            return 0.0f;
        return Numerator / Denominator;
    }

    inline float HighestValue(SCFloatArrayRef In, int Index, int Length)
    {
        int Start = Index - Length + 1;
        if (Start < 0)
            Start = 0;

        float Result = In[Start];
        for (int i = Start + 1; i <= Index; ++i)
        {
            if (In[i] > Result)
                Result = In[i];
        }
        return Result;
    }

    inline float LowestValue(SCFloatArrayRef In, int Index, int Length)
    {
        int Start = Index - Length + 1;
        if (Start < 0)
            Start = 0;

        float Result = In[Start];
        for (int i = Start + 1; i <= Index; ++i)
        {
            if (In[i] < Result)
                Result = In[i];
        }
        return Result;
    }

    // Result of one f_findDivs() evaluation
    struct DivResult
    {
        bool  FractalTop;
        bool  FractalBot;
        bool  TopValid;
        bool  BotValid;
        float HighPrev;
        float LowPrev;
        bool  BearSignal;
        bool  BullSignal;
        bool  BearHidden;
        bool  BullHidden;

        DivResult()
            : FractalTop(false), FractalBot(false)
            , TopValid(false), BotValid(false)
            , HighPrev(0.0f), LowPrev(0.0f)
            , BearSignal(false), BullSignal(false)
            , BearHidden(false), BullHidden(false)
        {}
    };

    //------------------------------------------------------------------------
    //  Direct port of:
    //
    //  f_top_fractal(src) => src[4]<src[2] and src[3]<src[2] and
    //                        src[2]>src[1] and src[2]>src[0]
    //  f_findDivs(src, topLimit, botLimit, useLimits)
    //
    //  The Pine code uses valuewhen(fractal, x, 0)[2].  Because two fractals
    //  can never occur less than 3 bars apart, reading the "most recent
    //  fractal" state as it stood 2 bars ago is exactly the PREVIOUS fractal.
    //  That is reproduced here with carry-forward state arrays.
    //
    //  State subgraph array usage:
    //     Arrays[0] : last fractal-top src value      (highPrev source)
    //     Arrays[1] : last fractal-top high price     (highPrice source)
    //     Arrays[2] : last fractal-bot src value      (lowPrev source)
    //     Arrays[3] : last fractal-bot low price      (lowPrice source)
    //     Arrays[4] : 1 once a top fractal has occurred
    //     Arrays[5] : 1 once a bottom fractal has occurred
    //     Arrays[6] : free (used by the caller for the Gold-buy lastRsi)
    //------------------------------------------------------------------------
    void FindDivergences(SCStudyInterfaceRef sc,
                         int Index,
                         SCFloatArrayRef Src,
                         SCSubgraphRef State,
                         float TopLimit,
                         float BotLimit,
                         bool  UseLimits,
                         DivResult& R)
    {
        R = DivResult();

        SCFloatArrayRef LastTopSrc  = State.Arrays[0];
        SCFloatArrayRef LastTopHigh = State.Arrays[1];
        SCFloatArrayRef LastBotSrc  = State.Arrays[2];
        SCFloatArrayRef LastBotLow  = State.Arrays[3];
        SCFloatArrayRef TopFound    = State.Arrays[4];
        SCFloatArrayRef BotFound    = State.Arrays[5];

        // Carry the valuewhen() state forward one bar
        if (Index > 0)
        {
            LastTopSrc[Index]  = LastTopSrc[Index - 1];
            LastTopHigh[Index] = LastTopHigh[Index - 1];
            LastBotSrc[Index]  = LastBotSrc[Index - 1];
            LastBotLow[Index]  = LastBotLow[Index - 1];
            TopFound[Index]    = TopFound[Index - 1];
            BotFound[Index]    = BotFound[Index - 1];
        }
        else
        {
            LastTopSrc[0]  = 0.0f;
            LastTopHigh[0] = 0.0f;
            LastBotSrc[0]  = 0.0f;
            LastBotLow[0]  = 0.0f;
            TopFound[0]    = 0.0f;
            BotFound[0]    = 0.0f;
        }

        if (Index < 4)
            return;

        const float s0 = Src[Index];
        const float s1 = Src[Index - 1];
        const float s2 = Src[Index - 2];
        const float s3 = Src[Index - 3];
        const float s4 = Src[Index - 4];

        const bool IsTopFractal = (s4 < s2) && (s3 < s2) && (s2 > s1) && (s2 > s0);
        const bool IsBotFractal = (s4 > s2) && (s3 > s2) && (s2 < s1) && (s2 < s0);

        R.FractalTop = IsTopFractal && (!UseLimits || s2 >= TopLimit);
        R.FractalBot = IsBotFractal && (!UseLimits || s2 <= BotLimit);

        // Read the PREVIOUS fractal (state as of 2 bars ago) before updating
        if (Index >= 6)
        {
            R.HighPrev = LastTopSrc[Index - 2];
            R.LowPrev  = LastBotSrc[Index - 2];
            R.TopValid = (TopFound[Index - 2] != 0.0f);
            R.BotValid = (BotFound[Index - 2] != 0.0f);

            const float HighPrice = LastTopHigh[Index - 2];
            const float LowPrice  = LastBotLow[Index - 2];
            const float H2 = sc.High[Index - 2];
            const float L2 = sc.Low[Index - 2];

            if (R.FractalTop && R.TopValid)
            {
                R.BearSignal = (H2 > HighPrice) && (s2 < R.HighPrev);
                R.BearHidden = (H2 < HighPrice) && (s2 > R.HighPrev);
            }

            if (R.FractalBot && R.BotValid)
            {
                R.BullSignal = (L2 < LowPrice) && (s2 > R.LowPrev);
                R.BullHidden = (L2 > LowPrice) && (s2 < R.LowPrev);
            }
        }

        // Now update the valuewhen() state for this bar
        if (R.FractalTop)
        {
            LastTopSrc[Index]  = s2;
            LastTopHigh[Index] = sc.High[Index - 2];
            TopFound[Index]    = 1.0f;
        }

        if (R.FractalBot)
        {
            LastBotSrc[Index] = s2;
            LastBotLow[Index] = sc.Low[Index - 2];
            BotFound[Index]   = 1.0f;
        }
    }

} // namespace VMC

//============================================================================
//  Study
//============================================================================
SCSFExport scsf_MarketCipherWannabe(SCStudyInterfaceRef sc)
{
    //------------------------------------------------------------------ plots
    SCSubgraphRef SG_WT1        = sc.Subgraph[0];
    SCSubgraphRef SG_WT2        = sc.Subgraph[1];
    SCSubgraphRef SG_WTVwap     = sc.Subgraph[2];
    SCSubgraphRef SG_MFI        = sc.Subgraph[3];
    SCSubgraphRef SG_MFIBar     = sc.Subgraph[4];
    SCSubgraphRef SG_RSI        = sc.Subgraph[5];
    SCSubgraphRef SG_StochK     = sc.Subgraph[6];
    SCSubgraphRef SG_StochD     = sc.Subgraph[7];
    SCSubgraphRef SG_STC        = sc.Subgraph[8];
    SCSubgraphRef SG_Zero       = sc.Subgraph[9];
    SCSubgraphRef SG_OB2        = sc.Subgraph[10];
    SCSubgraphRef SG_OS2        = sc.Subgraph[11];
    SCSubgraphRef SG_OB3        = sc.Subgraph[12];
    SCSubgraphRef SG_CrossDot   = sc.Subgraph[13];
    SCSubgraphRef SG_BuyCircle  = sc.Subgraph[14];
    SCSubgraphRef SG_SellCircle = sc.Subgraph[15];
    SCSubgraphRef SG_DivBuy     = sc.Subgraph[16];
    SCSubgraphRef SG_DivSell    = sc.Subgraph[17];
    SCSubgraphRef SG_GoldBuy    = sc.Subgraph[18];
    SCSubgraphRef SG_WTBearDiv  = sc.Subgraph[19];
    SCSubgraphRef SG_WTBullDiv  = sc.Subgraph[20];
    SCSubgraphRef SG_WTBearDiv2 = sc.Subgraph[21];
    SCSubgraphRef SG_WTBullDiv2 = sc.Subgraph[22];
    SCSubgraphRef SG_RSIBearDiv = sc.Subgraph[23];
    SCSubgraphRef SG_RSIBullDiv = sc.Subgraph[24];
    SCSubgraphRef SG_StoBearDiv = sc.Subgraph[25];
    SCSubgraphRef SG_StoBullDiv = sc.Subgraph[26];

    //-------------------------------------------------------------- internals
    SCSubgraphRef SG_StochRSI   = sc.Subgraph[27];  // RSI used by Stoch RSI
    SCSubgraphRef SG_STCCalc    = sc.Subgraph[28];  // Schaff work arrays

    SCSubgraphRef SG_DivWT      = sc.Subgraph[29];  // WT divergence state
    SCSubgraphRef SG_DivWTAdd   = sc.Subgraph[30];  // WT 2nd-range state
    SCSubgraphRef SG_DivWTnl    = sc.Subgraph[31];  // WT no-limit state
    SCSubgraphRef SG_DivRSI     = sc.Subgraph[32];  // RSI divergence state
    SCSubgraphRef SG_DivRSInl   = sc.Subgraph[33];  // RSI no-limit state
    SCSubgraphRef SG_DivStoch   = sc.Subgraph[34];  // Stoch divergence state

    //--------------------------------------------------------- alert flags
    //  Exposed as (hidden) subgraphs so they can also be referenced from
    //  Chart Alerts, spreadsheets or other studies.
    //     +1 = bullish event, -1 = bearish event, 0 = nothing
    SCSubgraphRef SG_AlSmallDot = sc.Subgraph[35];
    SCSubgraphRef SG_AlBigDot   = sc.Subgraph[36];
    SCSubgraphRef SG_AlDivDot   = sc.Subgraph[37];
    SCSubgraphRef SG_AlGoldDot  = sc.Subgraph[38];
    SCSubgraphRef SG_AlMFIFlip  = sc.Subgraph[39];

    //------------------------------------------------------------------ input
    SCInputRef In_WTShow        = sc.Input[0];
    SCInputRef In_WTVwapShow    = sc.Input[1];
    SCInputRef In_WTChannelLen  = sc.Input[2];
    SCInputRef In_WTAverageLen  = sc.Input[3];
    SCInputRef In_WTMALen       = sc.Input[4];
    SCInputRef In_WTMASource    = sc.Input[5];

    SCInputRef In_ObLevel       = sc.Input[6];
    SCInputRef In_ObLevel2      = sc.Input[7];
    SCInputRef In_ObLevel3      = sc.Input[8];
    SCInputRef In_OsLevel       = sc.Input[9];
    SCInputRef In_OsLevel2      = sc.Input[10];
    SCInputRef In_OsLevel3      = sc.Input[11];

    SCInputRef In_BuyShow       = sc.Input[12];
    SCInputRef In_GoldShow      = sc.Input[13];
    SCInputRef In_SellShow      = sc.Input[14];
    SCInputRef In_DivShow       = sc.Input[15];

    SCInputRef In_WTShowDiv     = sc.Input[16];
    SCInputRef In_WTShowHidDiv  = sc.Input[17];
    SCInputRef In_HidDivNoLimit = sc.Input[18];
    SCInputRef In_WTDivOBLevel  = sc.Input[19];
    SCInputRef In_WTDivOSLevel  = sc.Input[20];
    SCInputRef In_WTDiv2Show    = sc.Input[21];
    SCInputRef In_WTDivOBAdd    = sc.Input[22];
    SCInputRef In_WTDivOSAdd    = sc.Input[23];

    SCInputRef In_MFIShow       = sc.Input[24];
    SCInputRef In_MFIPeriod     = sc.Input[25];
    SCInputRef In_MFIMultiplier = sc.Input[26];
    SCInputRef In_MFIPosY       = sc.Input[27];
    SCInputRef In_MFIBarShow    = sc.Input[28];

    SCInputRef In_RSIShow       = sc.Input[29];
    SCInputRef In_RSISource     = sc.Input[30];
    SCInputRef In_RSILen        = sc.Input[31];
    SCInputRef In_RSIOversold   = sc.Input[32];
    SCInputRef In_RSIOverbought = sc.Input[33];
    SCInputRef In_RSIShowDiv    = sc.Input[34];
    SCInputRef In_RSIShowHidDiv = sc.Input[35];
    SCInputRef In_RSIDivOBLevel = sc.Input[36];
    SCInputRef In_RSIDivOSLevel = sc.Input[37];

    SCInputRef In_StochShow     = sc.Input[38];
    SCInputRef In_StochUseLog   = sc.Input[39];
    SCInputRef In_StochAvg      = sc.Input[40];
    SCInputRef In_StochSource   = sc.Input[41];
    SCInputRef In_StochLen      = sc.Input[42];
    SCInputRef In_StochRSILen   = sc.Input[43];
    SCInputRef In_StochKSmooth  = sc.Input[44];
    SCInputRef In_StochDSmooth  = sc.Input[45];
    SCInputRef In_StochShowDiv  = sc.Input[46];
    SCInputRef In_StochShowHid  = sc.Input[47];

    SCInputRef In_TCLine        = sc.Input[48];
    SCInputRef In_TCSource      = sc.Input[49];
    SCInputRef In_TCLength      = sc.Input[50];
    SCInputRef In_TCFastLength  = sc.Input[51];
    SCInputRef In_TCSlowLength  = sc.Input[52];
    SCInputRef In_TCFactor      = sc.Input[53];

    SCInputRef In_AlertsEnable  = sc.Input[54];
    SCInputRef In_AlertOnClose  = sc.Input[55];
    SCInputRef In_AlSmallOn     = sc.Input[56];
    SCInputRef In_AlSmallSound  = sc.Input[57];
    SCInputRef In_AlBigOn       = sc.Input[58];
    SCInputRef In_AlBigSound    = sc.Input[59];
    SCInputRef In_AlDivOn       = sc.Input[60];
    SCInputRef In_AlDivSound    = sc.Input[61];
    SCInputRef In_AlGoldOn      = sc.Input[62];
    SCInputRef In_AlGoldSound   = sc.Input[63];
    SCInputRef In_AlMFIOn       = sc.Input[64];
    SCInputRef In_AlMFISound    = sc.Input[65];

    //========================================================================
    if (sc.SetDefaults)
    {
        sc.GraphName        = "Market Cipher Wannabe";
        sc.GraphRegion      = 1;
        sc.AutoLoop         = 1;
        sc.ValueFormat      = 2;
        sc.FreeDLL          = 0;
        sc.CalculationPrecedence = LOW_PREC_LEVEL;

        // ---- WaveTrend -----------------------------------------------------
        SG_WT1.Name             = "WT Wave 1";
        SG_WT1.DrawStyle        = DRAWSTYLE_LINE;
        SG_WT1.PrimaryColor     = RGB(73, 148, 236);
        SG_WT1.LineWidth        = 2;
        SG_WT1.DrawZeros        = 0;

        SG_WT2.Name             = "WT Wave 2";
        SG_WT2.DrawStyle        = DRAWSTYLE_LINE;
        SG_WT2.PrimaryColor     = RGB(120, 100, 220);
        SG_WT2.LineWidth        = 2;
        SG_WT2.DrawZeros        = 0;

        SG_WTVwap.Name          = "Fast WT (VWAP)";
        SG_WTVwap.DrawStyle     = DRAWSTYLE_BAR;
        SG_WTVwap.PrimaryColor  = RGB(200, 200, 200);
        SG_WTVwap.LineWidth     = 2;
        SG_WTVwap.DrawZeros     = 0;

        // ---- Money flow ----------------------------------------------------
        SG_MFI.Name             = "RSI+MFI Area";
        SG_MFI.DrawStyle        = DRAWSTYLE_BAR;
        SG_MFI.PrimaryColor     = RGB(62, 225, 69);
        SG_MFI.SecondaryColor   = RGB(255, 61, 46);
        SG_MFI.SecondaryColorUsed = 1;
        SG_MFI.LineWidth        = 2;
        SG_MFI.DrawZeros        = 0;

        SG_MFIBar.Name          = "MFI Bar";
        SG_MFIBar.DrawStyle     = DRAWSTYLE_POINT;
        SG_MFIBar.PrimaryColor  = RGB(62, 225, 69);
        SG_MFIBar.LineWidth     = 5;
        SG_MFIBar.DrawZeros     = 0;

        // ---- RSI -----------------------------------------------------------
        SG_RSI.Name             = "RSI";
        SG_RSI.DrawStyle        = DRAWSTYLE_LINE;
        SG_RSI.PrimaryColor     = RGB(195, 62, 225);
        SG_RSI.LineWidth        = 2;
        SG_RSI.DrawZeros        = 0;

        // ---- Stoch RSI -----------------------------------------------------
        SG_StochK.Name          = "Stoch K";
        SG_StochK.DrawStyle     = DRAWSTYLE_LINE;
        SG_StochK.PrimaryColor  = RGB(33, 186, 243);
        SG_StochK.LineWidth     = 2;
        SG_StochK.DrawZeros     = 0;

        SG_StochD.Name          = "Stoch D";
        SG_StochD.DrawStyle     = DRAWSTYLE_LINE;
        SG_StochD.PrimaryColor  = RGB(103, 58, 183);
        SG_StochD.LineWidth     = 1;
        SG_StochD.DrawZeros     = 0;

        // ---- Schaff --------------------------------------------------------
        SG_STC.Name             = "Schaff Trend Cycle";
        SG_STC.DrawStyle        = DRAWSTYLE_LINE;
        SG_STC.PrimaryColor     = RGB(103, 58, 183);
        SG_STC.LineWidth        = 2;
        SG_STC.DrawZeros        = 0;

        // ---- Levels --------------------------------------------------------
        SG_Zero.Name            = "Zero Line";
        SG_Zero.DrawStyle       = DRAWSTYLE_LINE;
        SG_Zero.PrimaryColor    = RGB(128, 128, 128);
        SG_Zero.LineWidth       = 1;
        SG_Zero.DrawZeros       = 1;

        SG_OB2.Name             = "Over Bought Level 2";
        SG_OB2.DrawStyle        = DRAWSTYLE_DASH;
        SG_OB2.PrimaryColor     = RGB(180, 180, 180);
        SG_OB2.DrawZeros        = 0;

        SG_OS2.Name             = "Over Sold Level 2";
        SG_OS2.DrawStyle        = DRAWSTYLE_DASH;
        SG_OS2.PrimaryColor     = RGB(180, 180, 180);
        SG_OS2.DrawZeros        = 0;

        SG_OB3.Name             = "Over Bought Level 3";
        SG_OB3.DrawStyle        = DRAWSTYLE_DASH;
        SG_OB3.PrimaryColor     = RGB(110, 110, 110);
        SG_OB3.DrawZeros        = 0;

        // ---- Signals -------------------------------------------------------
        SG_CrossDot.Name        = "WT Cross Dot";
        SG_CrossDot.DrawStyle   = DRAWSTYLE_POINT;
        SG_CrossDot.PrimaryColor= RGB(0, 230, 118);
        SG_CrossDot.LineWidth   = 4;
        SG_CrossDot.DrawZeros   = 0;

        SG_BuyCircle.Name       = "Buy Circle";
        SG_BuyCircle.DrawStyle  = DRAWSTYLE_POINT;
        SG_BuyCircle.PrimaryColor = RGB(63, 255, 0);
        SG_BuyCircle.LineWidth  = 6;
        SG_BuyCircle.DrawZeros  = 0;

        SG_SellCircle.Name      = "Sell Circle";
        SG_SellCircle.DrawStyle = DRAWSTYLE_POINT;
        SG_SellCircle.PrimaryColor = RGB(255, 0, 0);
        SG_SellCircle.LineWidth = 6;
        SG_SellCircle.DrawZeros = 0;

        SG_DivBuy.Name          = "Divergence Buy Circle";
        SG_DivBuy.DrawStyle     = DRAWSTYLE_POINT;
        SG_DivBuy.PrimaryColor  = RGB(63, 255, 0);
        SG_DivBuy.LineWidth     = 7;
        SG_DivBuy.DrawZeros     = 0;

        SG_DivSell.Name         = "Divergence Sell Circle";
        SG_DivSell.DrawStyle    = DRAWSTYLE_POINT;
        SG_DivSell.PrimaryColor = RGB(255, 0, 0);
        SG_DivSell.LineWidth    = 7;
        SG_DivSell.DrawZeros    = 0;

        SG_GoldBuy.Name         = "Gold Buy Circle";
        SG_GoldBuy.DrawStyle    = DRAWSTYLE_POINT;
        SG_GoldBuy.PrimaryColor = RGB(226, 164, 0);
        SG_GoldBuy.LineWidth    = 9;
        SG_GoldBuy.DrawZeros    = 0;

        // ---- Divergence markers -------------------------------------------
        SG_WTBearDiv.Name       = "WT Bearish Divergence";
        SG_WTBearDiv.DrawStyle  = DRAWSTYLE_POINT;
        SG_WTBearDiv.PrimaryColor = RGB(230, 0, 0);
        SG_WTBearDiv.LineWidth  = 6;
        SG_WTBearDiv.DrawZeros  = 0;

        SG_WTBullDiv.Name       = "WT Bullish Divergence";
        SG_WTBullDiv.DrawStyle  = DRAWSTYLE_POINT;
        SG_WTBullDiv.PrimaryColor = RGB(0, 230, 118);
        SG_WTBullDiv.LineWidth  = 6;
        SG_WTBullDiv.DrawZeros  = 0;

        SG_WTBearDiv2.Name      = "WT 2nd Bearish Divergence";
        SG_WTBearDiv2.DrawStyle = DRAWSTYLE_POINT;
        SG_WTBearDiv2.PrimaryColor = RGB(140, 0, 0);
        SG_WTBearDiv2.LineWidth = 5;
        SG_WTBearDiv2.DrawZeros = 0;

        SG_WTBullDiv2.Name      = "WT 2nd Bullish Divergence";
        SG_WTBullDiv2.DrawStyle = DRAWSTYLE_POINT;
        SG_WTBullDiv2.PrimaryColor = RGB(0, 140, 70);
        SG_WTBullDiv2.LineWidth = 5;
        SG_WTBullDiv2.DrawZeros = 0;

        SG_RSIBearDiv.Name      = "RSI Bearish Divergence";
        SG_RSIBearDiv.DrawStyle = DRAWSTYLE_POINT;
        SG_RSIBearDiv.PrimaryColor = RGB(230, 0, 0);
        SG_RSIBearDiv.LineWidth = 4;
        SG_RSIBearDiv.DrawZeros = 0;

        SG_RSIBullDiv.Name      = "RSI Bullish Divergence";
        SG_RSIBullDiv.DrawStyle = DRAWSTYLE_POINT;
        SG_RSIBullDiv.PrimaryColor = RGB(56, 255, 66);
        SG_RSIBullDiv.LineWidth = 4;
        SG_RSIBullDiv.DrawZeros = 0;

        SG_StoBearDiv.Name      = "Stoch Bearish Divergence";
        SG_StoBearDiv.DrawStyle = DRAWSTYLE_POINT;
        SG_StoBearDiv.PrimaryColor = RGB(230, 0, 0);
        SG_StoBearDiv.LineWidth = 4;
        SG_StoBearDiv.DrawZeros = 0;

        SG_StoBullDiv.Name      = "Stoch Bullish Divergence";
        SG_StoBullDiv.DrawStyle = DRAWSTYLE_POINT;
        SG_StoBullDiv.PrimaryColor = RGB(56, 255, 66);
        SG_StoBullDiv.LineWidth = 4;
        SG_StoBullDiv.DrawZeros = 0;

        // ---- Hidden work subgraphs ----------------------------------------
        SG_StochRSI.Name        = "(internal) Stoch RSI";
        SG_StochRSI.DrawStyle   = DRAWSTYLE_IGNORE;
        SG_STCCalc.Name         = "(internal) Schaff";
        SG_STCCalc.DrawStyle    = DRAWSTYLE_IGNORE;
        SG_DivWT.Name           = "(internal) WT Div State";
        SG_DivWT.DrawStyle      = DRAWSTYLE_IGNORE;
        SG_DivWTAdd.Name        = "(internal) WT Div State 2";
        SG_DivWTAdd.DrawStyle   = DRAWSTYLE_IGNORE;
        SG_DivWTnl.Name         = "(internal) WT Div State NL";
        SG_DivWTnl.DrawStyle    = DRAWSTYLE_IGNORE;
        SG_DivRSI.Name          = "(internal) RSI Div State";
        SG_DivRSI.DrawStyle     = DRAWSTYLE_IGNORE;
        SG_DivRSInl.Name        = "(internal) RSI Div State NL";
        SG_DivRSInl.DrawStyle   = DRAWSTYLE_IGNORE;
        SG_DivStoch.Name        = "(internal) Stoch Div State";
        SG_DivStoch.DrawStyle   = DRAWSTYLE_IGNORE;

        SG_AlSmallDot.Name      = "Alert Flag: Small Dot (+1 up / -1 down)";
        SG_AlSmallDot.DrawStyle = DRAWSTYLE_IGNORE;
        SG_AlSmallDot.DrawZeros = 0;

        SG_AlBigDot.Name        = "Alert Flag: Big Dot (+1 buy / -1 sell)";
        SG_AlBigDot.DrawStyle   = DRAWSTYLE_IGNORE;
        SG_AlBigDot.DrawZeros   = 0;

        SG_AlDivDot.Name        = "Alert Flag: Divergence Dot (+1 bull / -1 bear)";
        SG_AlDivDot.DrawStyle   = DRAWSTYLE_IGNORE;
        SG_AlDivDot.DrawZeros   = 0;

        SG_AlGoldDot.Name       = "Alert Flag: Gold Dot (+1)";
        SG_AlGoldDot.DrawStyle  = DRAWSTYLE_IGNORE;
        SG_AlGoldDot.DrawZeros  = 0;

        SG_AlMFIFlip.Name       = "Alert Flag: Bottom Dots Flip (+1 to green / -1 to red)";
        SG_AlMFIFlip.DrawStyle  = DRAWSTYLE_IGNORE;
        SG_AlMFIFlip.DrawZeros  = 0;

        //-------------------------------------------------------------- inputs
        In_WTShow.Name = "WT: Show WaveTrend";
        In_WTShow.SetYesNo(1);

        In_WTVwapShow.Name = "WT: Show Fast WT";
        In_WTVwapShow.SetYesNo(1);

        In_WTChannelLen.Name = "WT: Channel Length";
        In_WTChannelLen.SetInt(9);
        In_WTChannelLen.SetIntLimits(1, 1000);

        In_WTAverageLen.Name = "WT: Average Length";
        In_WTAverageLen.SetInt(12);
        In_WTAverageLen.SetIntLimits(1, 1000);

        In_WTMALen.Name = "WT: MA Length";
        In_WTMALen.SetInt(3);
        In_WTMALen.SetIntLimits(1, 1000);

        In_WTMASource.Name = "WT: MA Source";
        In_WTMASource.SetInputDataIndex(SC_HLC_AVG);

        In_ObLevel.Name = "WT: Overbought Level 1";
        In_ObLevel.SetFloat(53.0f);

        In_ObLevel2.Name = "WT: Overbought Level 2";
        In_ObLevel2.SetFloat(60.0f);

        In_ObLevel3.Name = "WT: Overbought Level 3";
        In_ObLevel3.SetFloat(100.0f);

        In_OsLevel.Name = "WT: Oversold Level 1";
        In_OsLevel.SetFloat(-53.0f);

        In_OsLevel2.Name = "WT: Oversold Level 2";
        In_OsLevel2.SetFloat(-60.0f);

        In_OsLevel3.Name = "WT: Oversold Level 3";
        In_OsLevel3.SetFloat(-75.0f);

        In_BuyShow.Name = "WT: Show Buy dots";
        In_BuyShow.SetYesNo(1);

        In_GoldShow.Name = "WT: Show Gold dots";
        In_GoldShow.SetYesNo(1);

        In_SellShow.Name = "WT: Show Sell dots";
        In_SellShow.SetYesNo(1);

        In_DivShow.Name = "WT: Show Div. dots";
        In_DivShow.SetYesNo(1);

        In_WTShowDiv.Name = "WT: Show Regular Divergences";
        In_WTShowDiv.SetYesNo(1);

        In_WTShowHidDiv.Name = "WT: Show Hidden Divergences";
        In_WTShowHidDiv.SetYesNo(0);

        In_HidDivNoLimit.Name = "Do NOT apply OB/OS limits on Hidden Divergences";
        In_HidDivNoLimit.SetYesNo(1);

        In_WTDivOBLevel.Name = "WT: Bearish Divergence min";
        In_WTDivOBLevel.SetFloat(45.0f);

        In_WTDivOSLevel.Name = "WT: Bullish Divergence min";
        In_WTDivOSLevel.SetFloat(-65.0f);

        In_WTDiv2Show.Name = "WT: Show 2nd Regular Divergences";
        In_WTDiv2Show.SetYesNo(1);

        In_WTDivOBAdd.Name = "WT: 2nd Bearish Divergence";
        In_WTDivOBAdd.SetFloat(15.0f);

        In_WTDivOSAdd.Name = "WT: 2nd Bullish Divergence";
        In_WTDivOSAdd.SetFloat(-40.0f);

        In_MFIShow.Name = "MFI: Show MFI Area";
        In_MFIShow.SetYesNo(1);

        In_MFIPeriod.Name = "MFI: Period";
        In_MFIPeriod.SetInt(60);
        In_MFIPeriod.SetIntLimits(1, 1000);

        In_MFIMultiplier.Name = "MFI: Area multiplier";
        In_MFIMultiplier.SetFloat(150.0f);

        In_MFIPosY.Name = "MFI: Area Y Pos";
        In_MFIPosY.SetFloat(2.5f);

        In_MFIBarShow.Name = "MFI: Show MFI Bar";
        In_MFIBarShow.SetYesNo(1);

        In_RSIShow.Name = "RSI: Show RSI";
        In_RSIShow.SetYesNo(1);

        In_RSISource.Name = "RSI: Source";
        In_RSISource.SetInputDataIndex(SC_LAST);

        In_RSILen.Name = "RSI: Length";
        In_RSILen.SetInt(14);
        In_RSILen.SetIntLimits(1, 1000);

        In_RSIOversold.Name = "RSI: Oversold";
        In_RSIOversold.SetFloat(30.0f);

        In_RSIOverbought.Name = "RSI: Overbought";
        In_RSIOverbought.SetFloat(60.0f);

        In_RSIShowDiv.Name = "RSI: Show Regular Divergences";
        In_RSIShowDiv.SetYesNo(0);

        In_RSIShowHidDiv.Name = "RSI: Show Hidden Divergences";
        In_RSIShowHidDiv.SetYesNo(0);

        In_RSIDivOBLevel.Name = "RSI: Bearish Divergence min";
        In_RSIDivOBLevel.SetFloat(60.0f);

        In_RSIDivOSLevel.Name = "RSI: Bullish Divergence min";
        In_RSIDivOSLevel.SetFloat(30.0f);

        In_StochShow.Name = "Stoch: Show Stochastic RSI";
        In_StochShow.SetYesNo(1);

        In_StochUseLog.Name = "Stoch: Use Log Source";
        In_StochUseLog.SetYesNo(1);

        In_StochAvg.Name = "Stoch: Use Average of both K & D";
        In_StochAvg.SetYesNo(0);

        In_StochSource.Name = "Stoch: Source";
        In_StochSource.SetInputDataIndex(SC_LAST);

        In_StochLen.Name = "Stoch: Stochastic Length";
        In_StochLen.SetInt(14);
        In_StochLen.SetIntLimits(1, 1000);

        In_StochRSILen.Name = "Stoch: RSI Length";
        In_StochRSILen.SetInt(14);
        In_StochRSILen.SetIntLimits(1, 1000);

        In_StochKSmooth.Name = "Stoch: K Smooth";
        In_StochKSmooth.SetInt(3);
        In_StochKSmooth.SetIntLimits(1, 1000);

        In_StochDSmooth.Name = "Stoch: D Smooth";
        In_StochDSmooth.SetInt(3);
        In_StochDSmooth.SetIntLimits(1, 1000);

        In_StochShowDiv.Name = "Stoch: Show Regular Divergences";
        In_StochShowDiv.SetYesNo(0);

        In_StochShowHid.Name = "Stoch: Show Hidden Divergences";
        In_StochShowHid.SetYesNo(0);

        In_TCLine.Name = "Schaff: Show TC line";
        In_TCLine.SetYesNo(0);

        In_TCSource.Name = "Schaff: Source";
        In_TCSource.SetInputDataIndex(SC_LAST);

        In_TCLength.Name = "Schaff: TC Length";
        In_TCLength.SetInt(10);
        In_TCLength.SetIntLimits(1, 1000);

        In_TCFastLength.Name = "Schaff: TC Fast Length";
        In_TCFastLength.SetInt(23);
        In_TCFastLength.SetIntLimits(1, 1000);

        In_TCSlowLength.Name = "Schaff: TC Slow Length";
        In_TCSlowLength.SetInt(50);
        In_TCSlowLength.SetIntLimits(1, 1000);

        In_TCFactor.Name = "Schaff: TC Factor";
        In_TCFactor.SetFloat(0.5f);

        // ---- Alerts --------------------------------------------------------
        // Sound number 0 = use the alert configured on the study's Alerts tab.
        // 1..N = play that numbered alert sound (Global Settings >> Alert Sounds).
        In_AlertsEnable.Name = "Alerts: Enable Alerts";
        In_AlertsEnable.SetYesNo(0);

        In_AlertOnClose.Name = "Alerts: Only At Bar Close";
        In_AlertOnClose.SetYesNo(1);

        In_AlSmallOn.Name = "Alerts: Small Dots (every WT cross)";
        In_AlSmallOn.SetYesNo(1);

        In_AlSmallSound.Name = "Alerts: Small Dot Sound Number";
        In_AlSmallSound.SetInt(1);
        In_AlSmallSound.SetIntLimits(0, 100);

        In_AlBigOn.Name = "Alerts: Big Dots (buy / sell circles)";
        In_AlBigOn.SetYesNo(1);

        In_AlBigSound.Name = "Alerts: Big Dot Sound Number";
        In_AlBigSound.SetInt(2);
        In_AlBigSound.SetIntLimits(0, 100);

        In_AlDivOn.Name = "Alerts: Divergence Dots";
        In_AlDivOn.SetYesNo(1);

        In_AlDivSound.Name = "Alerts: Divergence Dot Sound Number";
        In_AlDivSound.SetInt(3);
        In_AlDivSound.SetIntLimits(0, 100);

        In_AlGoldOn.Name = "Alerts: Gold Dot";
        In_AlGoldOn.SetYesNo(1);

        In_AlGoldSound.Name = "Alerts: Gold Dot Sound Number";
        In_AlGoldSound.SetInt(4);
        In_AlGoldSound.SetIntLimits(0, 100);

        In_AlMFIOn.Name = "Alerts: Bottom Dots Colour Flip (money flow)";
        In_AlMFIOn.SetYesNo(1);

        In_AlMFISound.Name = "Alerts: Bottom Dots Flip Sound Number";
        In_AlMFISound.SetInt(5);
        In_AlMFISound.SetIntLimits(0, 100);

        return;
    }

    //========================================================================
    //  Runtime
    //========================================================================

    // Apply the "Show ..." switches once per full recalculation
    if (sc.UpdateStartIndex == 0)
    {
        SG_WT1.DrawStyle     = In_WTShow.GetYesNo()     ? DRAWSTYLE_LINE  : DRAWSTYLE_IGNORE;
        SG_WT2.DrawStyle     = In_WTShow.GetYesNo()     ? DRAWSTYLE_LINE  : DRAWSTYLE_IGNORE;
        SG_WTVwap.DrawStyle  = In_WTVwapShow.GetYesNo() ? DRAWSTYLE_BAR   : DRAWSTYLE_IGNORE;
        SG_MFI.DrawStyle     = In_MFIShow.GetYesNo()    ? DRAWSTYLE_BAR   : DRAWSTYLE_IGNORE;
        SG_MFIBar.DrawStyle  = In_MFIBarShow.GetYesNo() ? DRAWSTYLE_POINT : DRAWSTYLE_IGNORE;
        SG_RSI.DrawStyle     = In_RSIShow.GetYesNo()    ? DRAWSTYLE_LINE  : DRAWSTYLE_IGNORE;
        SG_StochK.DrawStyle  = In_StochShow.GetYesNo()  ? DRAWSTYLE_LINE  : DRAWSTYLE_IGNORE;
        SG_StochD.DrawStyle  = In_StochShow.GetYesNo()  ? DRAWSTYLE_LINE  : DRAWSTYLE_IGNORE;
        SG_STC.DrawStyle     = In_TCLine.GetYesNo()     ? DRAWSTYLE_LINE  : DRAWSTYLE_IGNORE;

        // Clear the "already alerted on this bar" trackers
        for (int Key = 1; Key <= 8; ++Key)
            sc.GetPersistentInt(Key) = -1;
    }

    const int Index = sc.Index;

    const float ObLevel   = In_ObLevel.GetFloat();
    const float ObLevel2  = In_ObLevel2.GetFloat();
    const float ObLevel3  = In_ObLevel3.GetFloat();
    const float OsLevel   = In_OsLevel.GetFloat();
    const float OsLevel2  = In_OsLevel2.GetFloat();
    const float OsLevel3  = In_OsLevel3.GetFloat();

    //---------------------------------------------------------------- levels
    SG_Zero[Index] = 0.0f;
    SG_OB2[Index]  = ObLevel2;
    SG_OS2[Index]  = OsLevel2;
    SG_OB3[Index]  = ObLevel3;

    // Reset the "marker" subgraphs for this bar.  Markers that belong to this
    // bar are written later, from the bar 2 positions ahead (Pine offset=-2).
    SG_CrossDot[Index]   = 0.0f;
    SG_BuyCircle[Index]  = 0.0f;
    SG_SellCircle[Index] = 0.0f;
    SG_DivBuy[Index]     = 0.0f;
    SG_DivSell[Index]    = 0.0f;
    SG_GoldBuy[Index]    = 0.0f;
    SG_WTBearDiv[Index]  = 0.0f;
    SG_WTBullDiv[Index]  = 0.0f;
    SG_WTBearDiv2[Index] = 0.0f;
    SG_WTBullDiv2[Index] = 0.0f;
    SG_RSIBearDiv[Index] = 0.0f;
    SG_RSIBullDiv[Index] = 0.0f;
    SG_StoBearDiv[Index] = 0.0f;
    SG_StoBullDiv[Index] = 0.0f;

    SG_AlSmallDot[Index] = 0.0f;
    SG_AlBigDot[Index]   = 0.0f;
    SG_AlDivDot[Index]   = 0.0f;
    SG_AlGoldDot[Index]  = 0.0f;
    SG_AlMFIFlip[Index]  = 0.0f;

    //====================================================================
    //  WaveTrend
    //
    //  esa = ema(src, chlen)
    //  de  = ema(abs(src - esa), chlen)
    //  ci  = (src - esa) / (0.015 * de)
    //  wt1 = ema(ci, avg)
    //  wt2 = sma(wt1, malen)
    //  wtVwap = wt1 - wt2
    //====================================================================
    SCFloatArrayRef WTSource = sc.BaseData[In_WTMASource.GetInputDataIndex()];
    SCFloatArrayRef ESA      = SG_WT1.Arrays[0];
    SCFloatArrayRef AbsDiff  = SG_WT1.Arrays[1];
    SCFloatArrayRef DE       = SG_WT1.Arrays[2];
    SCFloatArrayRef CI       = SG_WT1.Arrays[3];

    const int WTChannelLen = In_WTChannelLen.GetInt();
    const int WTAverageLen = In_WTAverageLen.GetInt();
    const int WTMALen      = In_WTMALen.GetInt();

    sc.ExponentialMovAvg(WTSource, ESA, Index, WTChannelLen);
    AbsDiff[Index] = fabsf(WTSource[Index] - ESA[Index]);
    sc.ExponentialMovAvg(AbsDiff, DE, Index, WTChannelLen);
    CI[Index] = VMC::SafeDiv(WTSource[Index] - ESA[Index], 0.015f * DE[Index]);

    sc.ExponentialMovAvg(CI, SG_WT1, Index, WTAverageLen);
    sc.SimpleMovAvg(SG_WT1, SG_WT2, Index, WTMALen);

    const float WT1 = SG_WT1[Index];
    const float WT2 = SG_WT2[Index];
    SG_WTVwap[Index] = WT1 - WT2;

    const bool WTOversold   = (WT2 <= OsLevel);
    const bool WTOverbought = (WT2 >= ObLevel);

    bool WTCross = false;
    if (Index > 0)
    {
        const float Diff     = WT1 - WT2;
        const float PrevDiff = SG_WT1[Index - 1] - SG_WT2[Index - 1];
        WTCross = (Diff > 0.0f && PrevDiff <= 0.0f) || (Diff < 0.0f && PrevDiff >= 0.0f);
    }
    const bool WTCrossUp   = (WT2 - WT1) <= 0.0f;
    const bool WTCrossDown = (WT2 - WT1) >= 0.0f;

    //====================================================================
    //  RSI + MFI area
    //  sma(((close-open)/(high-low)) * multiplier, period) - posY
    //====================================================================
    SCFloatArrayRef MFIRaw = SG_MFI.Arrays[0];
    MFIRaw[Index] = VMC::SafeDiv(sc.Close[Index] - sc.Open[Index],
                                 sc.High[Index] - sc.Low[Index])
                    * In_MFIMultiplier.GetFloat();

    sc.SimpleMovAvg(MFIRaw, SG_MFI, Index, In_MFIPeriod.GetInt());
    SG_MFI[Index] -= In_MFIPosY.GetFloat();

    const COLORREF MFIColorUp   = RGB(62, 225, 69);
    const COLORREF MFIColorDown = RGB(255, 61, 46);
    const COLORREF MFIColor     = (SG_MFI[Index] > 0.0f) ? MFIColorUp : MFIColorDown;
    SG_MFI.DataColor[Index] = MFIColor;

    SG_MFIBar[Index] = -97.0f;
    SG_MFIBar.DataColor[Index] = MFIColor;

    //====================================================================
    //  RSI
    //====================================================================
    sc.RSI(sc.BaseData[In_RSISource.GetInputDataIndex()], SG_RSI, Index,
           MOVAVGTYPE_WILDERS, In_RSILen.GetInt());

    const float RSIValue = SG_RSI[Index];
    if (RSIValue <= In_RSIOversold.GetFloat())
        SG_RSI.DataColor[Index] = RGB(62, 225, 69);
    else if (RSIValue >= In_RSIOverbought.GetFloat())
        SG_RSI.DataColor[Index] = RGB(225, 62, 62);
    else
        SG_RSI.DataColor[Index] = RGB(195, 62, 225);

    //====================================================================
    //  Stochastic RSI
    //
    //  src = log ? log(src) : src
    //  rsi = rsi(src, rsilen)
    //  kk  = sma(stoch(rsi, rsi, rsi, stochlen), smoothk)
    //  d1  = sma(kk, smoothd)
    //  k   = avg ? avg(kk, d1) : kk
    //====================================================================
    SCFloatArrayRef StochLogSrc = SG_StochRSI.Arrays[5];
    SCFloatArrayRef StochRaw    = SG_StochRSI.Arrays[6];
    SCFloatArrayRef StochKK     = SG_StochRSI.Arrays[7];

    {
        const float RawSrc = sc.BaseData[In_StochSource.GetInputDataIndex()][Index];
        StochLogSrc[Index] = (In_StochUseLog.GetYesNo() && RawSrc > 0.0f)
                             ? logf(RawSrc)
                             : RawSrc;
    }

    sc.RSI(StochLogSrc, SG_StochRSI, Index, MOVAVGTYPE_WILDERS, In_StochRSILen.GetInt());

    {
        const int StochLen = In_StochLen.GetInt();
        const float HH = VMC::HighestValue(SG_StochRSI, Index, StochLen);
        const float LL = VMC::LowestValue(SG_StochRSI, Index, StochLen);
        StochRaw[Index] = 100.0f * VMC::SafeDiv(SG_StochRSI[Index] - LL, HH - LL);
    }

    sc.SimpleMovAvg(StochRaw, StochKK, Index, In_StochKSmooth.GetInt());
    sc.SimpleMovAvg(StochKK, SG_StochD, Index, In_StochDSmooth.GetInt());

    SG_StochK[Index] = In_StochAvg.GetYesNo()
                       ? (StochKK[Index] + SG_StochD[Index]) * 0.5f
                       : StochKK[Index];

    //====================================================================
    //  Schaff Trend Cycle
    //====================================================================
    {
        SCFloatArrayRef Ema1    = SG_STCCalc.Arrays[0];
        SCFloatArrayRef Ema2    = SG_STCCalc.Arrays[1];
        SCFloatArrayRef MacdVal = SG_STCCalc.Arrays[2];
        SCFloatArrayRef Gamma   = SG_STCCalc.Arrays[3];
        SCFloatArrayRef Delta   = SG_STCCalc.Arrays[4];
        SCFloatArrayRef Eta     = SG_STCCalc.Arrays[5];

        SCFloatArrayRef TCSource = sc.BaseData[In_TCSource.GetInputDataIndex()];
        const int   TCLength = In_TCLength.GetInt();
        const float TCFactor = In_TCFactor.GetFloat();

        sc.ExponentialMovAvg(TCSource, Ema1, Index, In_TCFastLength.GetInt());
        sc.ExponentialMovAvg(TCSource, Ema2, Index, In_TCSlowLength.GetInt());
        MacdVal[Index] = Ema1[Index] - Ema2[Index];

        const float Alpha = VMC::LowestValue(MacdVal, Index, TCLength);
        const float Beta  = VMC::HighestValue(MacdVal, Index, TCLength) - Alpha;

        if (Beta > 0.0f)
            Gamma[Index] = (MacdVal[Index] - Alpha) / Beta * 100.0f;
        else
            Gamma[Index] = (Index > 0) ? Gamma[Index - 1] : 0.0f;

        if (Index == 0)
            Delta[0] = Gamma[0];
        else
            Delta[Index] = Delta[Index - 1] + TCFactor * (Gamma[Index] - Delta[Index - 1]);

        const float Epsilon = VMC::LowestValue(Delta, Index, TCLength);
        const float Zeta    = VMC::HighestValue(Delta, Index, TCLength) - Epsilon;

        if (Zeta > 0.0f)
            Eta[Index] = (Delta[Index] - Epsilon) / Zeta * 100.0f;
        else
            Eta[Index] = (Index > 0) ? Eta[Index - 1] : 0.0f;

        if (Index == 0)
            SG_STC[0] = Eta[0];
        else
            SG_STC[Index] = SG_STC[Index - 1] + TCFactor * (Eta[Index] - SG_STC[Index - 1]);
    }

    //====================================================================
    //  Divergences
    //====================================================================
    const bool ShowHidNoLimit = In_HidDivNoLimit.GetYesNo() != 0;
    const bool WTShowDiv      = In_WTShowDiv.GetYesNo() != 0;
    const bool WTShowHidDiv   = In_WTShowHidDiv.GetYesNo() != 0;
    const bool RSIShowDiv     = In_RSIShowDiv.GetYesNo() != 0;
    const bool RSIShowHidDiv  = In_RSIShowHidDiv.GetYesNo() != 0;
    const bool StochShowDiv   = In_StochShowDiv.GetYesNo() != 0;
    const bool StochShowHid   = In_StochShowHid.GetYesNo() != 0;
    const bool WTDiv2Show     = In_WTDiv2Show.GetYesNo() != 0;

    VMC::DivResult WT, WTAdd, WTnl, RSIDiv, RSIDivNL, StochDiv;

    VMC::FindDivergences(sc, Index, SG_WT2, SG_DivWT,
                         In_WTDivOBLevel.GetFloat(), In_WTDivOSLevel.GetFloat(), true, WT);

    VMC::FindDivergences(sc, Index, SG_WT2, SG_DivWTAdd,
                         In_WTDivOBAdd.GetFloat(), In_WTDivOSAdd.GetFloat(), true, WTAdd);

    VMC::FindDivergences(sc, Index, SG_WT2, SG_DivWTnl,
                         0.0f, 0.0f, false, WTnl);

    VMC::FindDivergences(sc, Index, SG_RSI, SG_DivRSI,
                         In_RSIDivOBLevel.GetFloat(), In_RSIDivOSLevel.GetFloat(), true, RSIDiv);

    VMC::FindDivergences(sc, Index, SG_RSI, SG_DivRSInl,
                         0.0f, 0.0f, false, RSIDivNL);

    VMC::FindDivergences(sc, Index, SG_StochK, SG_DivStoch,
                         0.0f, 0.0f, false, StochDiv);

    // Hidden divergence selection (showHiddenDiv_nl)
    const bool WTBearHidden  = ShowHidNoLimit ? WTnl.BearHidden     : WT.BearHidden;
    const bool WTBullHidden  = ShowHidNoLimit ? WTnl.BullHidden     : WT.BullHidden;
    const bool RSIBearHidden = ShowHidNoLimit ? RSIDivNL.BearHidden : RSIDiv.BearHidden;
    const bool RSIBullHidden = ShowHidNoLimit ? RSIDivNL.BullHidden : RSIDiv.BullHidden;

    // Gold-buy: valuewhen(wtFractalBot, rsi[2], 0)[2]
    float LastRsiAtBotFractal = 100.0f;
    {
        SCFloatArrayRef LastBotRsi = SG_DivWT.Arrays[6];
        if (Index > 0)
            LastBotRsi[Index] = LastBotRsi[Index - 1];
        else
            LastBotRsi[0] = 100.0f;

        if (Index >= 2)
            LastRsiAtBotFractal = LastBotRsi[Index - 2];

        if (WT.FractalBot && Index >= 2)
            LastBotRsi[Index] = SG_RSI[Index - 2];
    }

    //====================================================================
    //  Draw divergence markers (Pine offset = -2  ->  write to Index-2)
    //====================================================================
    if (Index >= 2)
    {
        const int MarkerIndex = Index - 2;
        const float WT2Prev   = SG_WT2[MarkerIndex];

        if (WT.FractalTop && ((WTShowDiv && WT.BearSignal) || (WTShowHidDiv && WTBearHidden)))
            SG_WTBearDiv[MarkerIndex] = WT2Prev;

        if (WT.FractalBot && ((WTShowDiv && WT.BullSignal) || (WTShowHidDiv && WTBullHidden)))
            SG_WTBullDiv[MarkerIndex] = WT2Prev;

        if (WTDiv2Show && WTAdd.FractalTop
            && ((WTShowDiv && WTAdd.BearSignal) || (WTShowHidDiv && WTAdd.BearHidden)))
            SG_WTBearDiv2[MarkerIndex] = WT2Prev;

        if (WTDiv2Show && WTAdd.FractalBot
            && ((WTShowDiv && WTAdd.BullSignal) || (WTShowHidDiv && WTAdd.BullHidden)))
            SG_WTBullDiv2[MarkerIndex] = WT2Prev;

        if (RSIDiv.FractalTop && ((RSIShowDiv && RSIDiv.BearSignal) || (RSIShowHidDiv && RSIBearHidden)))
            SG_RSIBearDiv[MarkerIndex] = SG_RSI[MarkerIndex];

        if (RSIDiv.FractalBot && ((RSIShowDiv && RSIDiv.BullSignal) || (RSIShowHidDiv && RSIBullHidden)))
            SG_RSIBullDiv[MarkerIndex] = SG_RSI[MarkerIndex];

        if (StochDiv.FractalTop && ((StochShowDiv && StochDiv.BearSignal) || (StochShowHid && StochDiv.BearHidden)))
            SG_StoBearDiv[MarkerIndex] = SG_StochK[MarkerIndex];

        if (StochDiv.FractalBot && ((StochShowDiv && StochDiv.BullSignal) || (StochShowHid && StochDiv.BullHidden)))
            SG_StoBullDiv[MarkerIndex] = SG_StochK[MarkerIndex];
    }

    //====================================================================
    //  Signals
    //====================================================================

    // Small circle at every WT cross
    if (WTCross)
    {
        SG_CrossDot[Index] = WT2;
        SG_CrossDot.DataColor[Index] = ((WT2 - WT1) > 0.0f)
                                       ? RGB(255, 82, 82)     // sell-ish
                                       : RGB(0, 230, 118);    // buy-ish
    }

    // Big green / red circles
    const bool BuySignal  = WTCross && WTCrossUp   && WTOversold;
    const bool SellSignal = WTCross && WTCrossDown && WTOverbought;

    if (In_BuyShow.GetYesNo() && BuySignal)
        SG_BuyCircle[Index] = -107.0f;

    if (In_SellShow.GetYesNo() && SellSignal)
        SG_SellCircle[Index] = 105.0f;

    // Divergence circles (offset -2)
    const bool BuySignalDiv  = (WTShowDiv    && WT.BullSignal)
                            || (WTShowDiv    && WTAdd.BullSignal)
                            || (StochShowDiv && StochDiv.BullSignal)
                            || (RSIShowDiv   && RSIDiv.BullSignal);

    const bool SellSignalDiv = (WTShowDiv    && WT.BearSignal)
                            || (WTShowDiv    && WTAdd.BearSignal)
                            || (StochShowDiv && StochDiv.BearSignal)
                            || (RSIShowDiv   && RSIDiv.BearSignal);

    if (In_DivShow.GetYesNo() && BuySignalDiv && Index >= 2)
    {
        SG_DivBuy[Index - 2] = -106.0f;
        SG_DivBuy.DataColor[Index - 2] = WT.BullSignal
                                         ? RGB(63, 255, 0)
                                         : (WTAdd.BullSignal ? RGB(30, 130, 0) : RGB(63, 255, 0));
    }

    if (In_DivShow.GetYesNo() && SellSignalDiv && Index >= 2)
    {
        SG_DivSell[Index - 2] = 106.0f;
        SG_DivSell.DataColor[Index - 2] = WT.BearSignal
                                          ? RGB(255, 0, 0)
                                          : (WTAdd.BearSignal ? RGB(130, 0, 0) : RGB(255, 0, 0));
    }

    // Gold buy
    const bool GoldBuy = ((WTShowDiv && WT.BullSignal) || (RSIShowDiv && RSIDiv.BullSignal))
                      && WT.BotValid
                      && (WT.LowPrev <= OsLevel3)
                      && (WT2 > OsLevel3)
                      && ((WT.LowPrev - WT2) <= -5.0f)
                      && (LastRsiAtBotFractal < 30.0f);

    if (In_GoldShow.GetYesNo() && GoldBuy && Index >= 2)
        SG_GoldBuy[Index - 2] = -106.0f;

    //====================================================================
    //  Bottom money-flow dots: green <-> red colour change
    //
    //  The bottom dot row is green while the money-flow value is above 0
    //  and red while it is below.  A "flip" is the bar where that sign
    //  changes.  Flat/zero is treated as red, matching the Pine ternary
    //  rsiMFI > 0 ? green : red.
    //====================================================================
    bool MFIFlipToGreen = false;
    bool MFIFlipToRed   = false;

    if (Index > 0)
    {
        const bool IsGreen     = (SG_MFI[Index]     > 0.0f);
        const bool WasGreen    = (SG_MFI[Index - 1] > 0.0f);

        MFIFlipToGreen = IsGreen && !WasGreen;
        MFIFlipToRed   = !IsGreen && WasGreen;
    }

    //====================================================================
    //  Alert flags (also usable from Chart Alerts / spreadsheets)
    //====================================================================
    const bool SmallDotUp   = WTCross && WTCrossUp;
    const bool SmallDotDown = WTCross && WTCrossDown;

    if (SmallDotUp)         SG_AlSmallDot[Index] =  1.0f;
    else if (SmallDotDown)  SG_AlSmallDot[Index] = -1.0f;

    if (BuySignal)          SG_AlBigDot[Index] =  1.0f;
    else if (SellSignal)    SG_AlBigDot[Index] = -1.0f;

    if (BuySignalDiv)       SG_AlDivDot[Index] =  1.0f;
    else if (SellSignalDiv) SG_AlDivDot[Index] = -1.0f;

    if (GoldBuy)            SG_AlGoldDot[Index] = 1.0f;

    if (MFIFlipToGreen)     SG_AlMFIFlip[Index] =  1.0f;
    else if (MFIFlipToRed)  SG_AlMFIFlip[Index] = -1.0f;

    //====================================================================
    //  Alerts
    //
    //  Each alert fires at most once per bar.  With "Only At Bar Close"
    //  set to Yes the alert is evaluated on the bar that has just closed;
    //  set it to No to be alerted on the forming bar (which can repaint if
    //  the condition stops being true before the bar closes).
    //====================================================================
    if (In_AlertsEnable.GetYesNo()
        && sc.IsFullRecalculation == 0
        && sc.DownloadingHistoricalData == 0)
    {
        bool EvaluateThisBar = false;

        if (In_AlertOnClose.GetYesNo())
            EvaluateThisBar = (sc.GetBarHasClosedStatus(Index) == BHCS_BAR_HAS_CLOSED);
        else
            EvaluateThisBar = (Index == sc.ArraySize - 1);

        if (EvaluateThisBar)
        {
            // Fire at most once per bar per alert type
            auto Trigger = [&](int PersistentKey, int SoundNumber, const char* Text)
            {
                if (sc.GetPersistentInt(PersistentKey) == Index)
                    return;

                sc.GetPersistentInt(PersistentKey) = Index;

                SCString Message;
                Message.Format("VMC Cipher B | %s | %s", sc.Symbol.GetChars(), Text);

                sc.SetAlert(SoundNumber, Message);
            };

            const int SmallSound = In_AlSmallSound.GetInt();
            const int BigSound   = In_AlBigSound.GetInt();
            const int DivSound   = In_AlDivSound.GetInt();
            const int GoldSound  = In_AlGoldSound.GetInt();
            const int MFISound   = In_AlMFISound.GetInt();

            // --- Gold dot (most significant, checked first) ---------------
            if (In_AlGoldOn.GetYesNo() && GoldBuy)
                Trigger(7, GoldSound, "GOLD dot - bullish (marks bar -2)");

            // --- Big dots -------------------------------------------------
            if (In_AlBigOn.GetYesNo())
            {
                if (BuySignal)
                    Trigger(3, BigSound, "BIG GREEN dot - WT cross up in oversold");

                if (SellSignal)
                    Trigger(4, BigSound, "BIG RED dot - WT cross down in overbought");
            }

            // --- Divergence dots -----------------------------------------
            if (In_AlDivOn.GetYesNo())
            {
                if (BuySignalDiv)
                    Trigger(5, DivSound, "DIVERGENCE dot - bullish (marks bar -2)");

                if (SellSignalDiv)
                    Trigger(6, DivSound, "DIVERGENCE dot - bearish (marks bar -2)");
            }

            // --- Small dots (every WT cross) ------------------------------
            if (In_AlSmallOn.GetYesNo())
            {
                if (SmallDotUp)
                    Trigger(1, SmallSound, "small GREEN dot - WT cross up");

                if (SmallDotDown)
                    Trigger(2, SmallSound, "small RED dot - WT cross down");
            }

            // --- Bottom money-flow dots changing colour -------------------
            if (In_AlMFIOn.GetYesNo())
            {
                if (MFIFlipToGreen)
                    Trigger(8, MFISound, "BOTTOM DOTS flipped RED -> GREEN (money flow turned positive)");

                if (MFIFlipToRed)
                    Trigger(8, MFISound, "BOTTOM DOTS flipped GREEN -> RED (money flow turned negative)");
            }
        }
    }
}

//============================================================================
//  NOTES ON THE PORT
//----------------------------------------------------------------------------
//  1. Moving-average seeding
//     Pine's ema() seeds from the first bar; sc.ExponentialMovAvg() seeds with
//     In[0].  The two converge within a few multiples of the length, so early
//     bars on a chart may differ slightly from TradingView.  Load enough
//     history (a few hundred bars minimum) before comparing values.
//
//  2. Divergence "valuewhen(...)[2]"
//     Two Williams fractals can never be fewer than three bars apart, so the
//     Pine idiom valuewhen(fractal, x, 0)[2] always resolves to the PREVIOUS
//     fractal.  That is reproduced exactly by the carry-forward state arrays
//     in VMC::FindDivergences().
//
//  3. Pine's offset = -2
//     Rather than using a graph displacement, markers are written directly
//     into element (Index - 2) of the subgraph array, which is equivalent and
//     keeps the subgraph values aligned with the bars they describe.
//
//  4. Draw styles
//     Areas are drawn as DRAWSTYLE_BAR (histogram) and waves as lines, since
//     ACSIL has no direct equivalent of Pine's plot.style_area with
//     transparency.  Change any of these in Study Settings >> Subgraphs.
//
//  5. Alerts
//     Turn on "Alerts: Enable Alerts", then enable the individual categories
//     you want.  Each has its own sound number so you can tell them apart:
//     0 uses whatever is configured on the study's Alerts tab, 1..N play that
//     numbered sound from Global Settings >> General Settings >> Alert Sounds.
//
//     "Only At Bar Close" = Yes evaluates the bar that has just closed, so an
//     alert never fires for a condition that later disappears.  Set it to No
//     for intrabar alerts, which are faster but can fire on a cross that then
//     un-crosses before the bar completes.
//
//     Every alert is also mirrored into a hidden subgraph (Alert Flag: ...),
//     so the same events can drive a Chart Alert formula, a spreadsheet study
//     or an automated trading system.  Values are +1 bullish / -1 bearish.
//
//  6. Omitted features
//     Sommi flags, Sommi diamonds and the MACD-based WaveTrend colouring all
//     depend on Pine's security() calls to higher timeframes (720m, 60m, 240m
//     and Heikin-Ashi candles).  ACSIL has no direct equivalent; they require
//     either sc.GetStudyArrayFromChartUsingID() / sc.GetChartBaseData() against
//     separate higher-timeframe charts, or sc.GetTimeAndSalesArray-based
//     aggregation.  Both approaches need the user to configure chart numbers,
//     so they are left out here.
//============================================================================
