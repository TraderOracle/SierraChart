// ============================================================================
//  Nebula v2.1s  -  Sierra Chart ACSIL port
//  Original Pine Script v5 by TraderOracle (DaveTrade55 on TradingView)
//  Mozilla Public License 2.0  -  https://mozilla.org/MPL/2.0/
//
//  Ported components:
//    Cloud        : Bixord Fantail VMA vs McGinley Dynamic, RSI/MFI/CCI shading
//    Candles      : Waddah Attar Explosion, TTM Squeeze, Cumulative Volume
//                   Delta, PVSRA "Vector" candles
//    Signals      : Tidal Wave (Aaron D), Ultimate Buy/Sell, Trampoline,
//                   Squeeze Relaxer, Dead Simple Reversal, LuxAlgo Reversal,
//                   Total Recall, The Shark, John Wick bands, Vodka Shot
//    Overlays     : HEMA, Rational Quadratic Kernel, 9/21 cross,
//                   volume-imbalance lines, take-profit markers
//
//  Build: place in Data/ACS_Source and run
//         Analysis >> Build Custom Studies DLL.
// ============================================================================

#include "sierrachart.h"

SCDLLName("Nebula v2.1s")

namespace NB
{
    enum MaType    { MA_SMA = 0, MA_EMA, MA_WMA, MA_HMA, MA_VWMA, MA_RMA };
    enum WaveDir   { WAVE_NONE = 0, WAVE_UP = 1, WAVE_DOWN = 2 };
    enum CloudKind { CLOUD_NONE = 0, CLOUD_SIMPLE, CLOUD_RSI, CLOUD_MFI, CLOUD_CCI };
    enum CandleKind{ CANDLE_NONE = 0, CANDLE_VECTOR, CANDLE_WADDAH,
                     CANDLE_SQUEEZE, CANDLE_CVD };

    inline int Clamp255(int V) { return V < 0 ? 0 : (V > 255 ? 255 : V); }

    inline int CR_R(COLORREF C) { return (int)( C        & 0xFF); }
    inline int CR_G(COLORREF C) { return (int)((C >>  8) & 0xFF); }
    inline int CR_B(COLORREF C) { return (int)((C >> 16) & 0xFF); }

    inline COLORREF MakeCol(int R, int G, int B)
    { return (COLORREF)(Clamp255(R) | (Clamp255(G) << 8) | (Clamp255(B) << 16)); }

    // Sierra treats DataColor == 0 as "no override", so never emit pure black.
    inline COLORREF SafeCol(COLORREF C) { return C == 0 ? MakeCol(1,1,1) : C; }

    inline COLORREF Blend(COLORREF A, COLORREF B, float T)
    {
        if (T < 0.0f) T = 0.0f;
        if (T > 1.0f) T = 1.0f;
        return MakeCol((int)((1-T)*CR_R(A) + T*CR_R(B) + 0.5f),
                       (int)((1-T)*CR_G(A) + T*CR_G(B) + 0.5f),
                       (int)((1-T)*CR_B(A) + T*CR_B(B) + 0.5f));
    }

    // Pine color.new(Base, Transparency).  Subgraph colors have no alpha in
    // Sierra, so transparency is emulated by blending toward the background.
    inline COLORREF Transp(COLORREF Base, COLORREF Bg, float Pct)
    { return Blend(Base, Bg, Pct / 100.0f); }

    // Pine color.from_gradient()
    inline COLORREF Gradient(float V, float Bot, float Top, COLORREF CBot, COLORREF CTop)
    {
        const float T = (Top == Bot) ? 0.0f : (V - Bot) / (Top - Bot);
        return Blend(CBot, CTop, T);
    }

    inline float GradientT(float V, float Bot, float Top)
    {
        if (Top == Bot) return 0.0f;
        float T = (V - Bot) / (Top - Bot);
        return T < 0.0f ? 0.0f : (T > 1.0f ? 1.0f : T);
    }

    inline int Fit(int Index, int Length)
    { return (Index < Length - 1) ? Index + 1 : Length; }

    inline float SMA(SCFloatArrayRef In, int Index, int Length)
    {
        const int N = Fit(Index, Length);
        if (N <= 0) return 0.0f;
        float S = 0.0f;
        for (int k = 0; k < N; ++k) S += In[Index - k];
        return S / (float)N;
    }

    inline float Sum(SCFloatArrayRef In, int Index, int Length)
    {
        const int N = Fit(Index, Length);
        float S = 0.0f;
        for (int k = 0; k < N; ++k) S += In[Index - k];
        return S;
    }

    inline float WMA(SCFloatArrayRef In, int Index, int Length)
    {
        const int N = Fit(Index, Length);
        if (N <= 0) return 0.0f;
        float Num = 0.0f, Den = 0.0f;
        for (int k = 0; k < N; ++k)
        {
            const float W = (float)(N - k);
            Num += In[Index - k] * W;
            Den += W;
        }
        return Den == 0.0f ? 0.0f : Num / Den;
    }

    inline float VWMA(SCFloatArrayRef In, SCFloatArrayRef Vol, int Index, int Length)
    {
        const int N = Fit(Index, Length);
        float Num = 0.0f, Den = 0.0f;
        for (int k = 0; k < N; ++k)
        {
            Num += In[Index - k] * Vol[Index - k];
            Den += Vol[Index - k];
        }
        return Den == 0.0f ? In[Index] : Num / Den;
    }

    inline float Highest(SCFloatArrayRef In, int Index, int Length)
    {
        const int N = Fit(Index, Length);
        float M = In[Index];
        for (int k = 1; k < N; ++k) if (In[Index-k] > M) M = In[Index-k];
        return M;
    }

    inline float Lowest(SCFloatArrayRef In, int Index, int Length)
    {
        const int N = Fit(Index, Length);
        float M = In[Index];
        for (int k = 1; k < N; ++k) if (In[Index-k] < M) M = In[Index-k];
        return M;
    }

    // Population stdev - matches Pine ta.stdev() default (biased).
    inline float StdevPop(SCFloatArrayRef In, int Index, int Length)
    {
        const int N = Fit(Index, Length);
        if (N <= 1) return 0.0f;
        const float M = SMA(In, Index, N);
        double S = 0.0;
        for (int k = 0; k < N; ++k)
        {
            const double D = (double)In[Index-k] - (double)M;
            S += D * D;
        }
        return (float)sqrt(S / (double)N);
    }

    // Pine ta.dev() - mean absolute deviation about the SMA.
    inline float MeanAbsDev(SCFloatArrayRef In, int Index, int Length)
    {
        const int N = Fit(Index, Length);
        if (N <= 0) return 0.0f;
        const float M = SMA(In, Index, N);
        float S = 0.0f;
        for (int k = 0; k < N; ++k) S += fabsf(In[Index-k] - M);
        return S / (float)N;
    }

    // Value of the least-squares line at the newest bar == ta.linreg(src,len,0)
    inline float LinRegEnd(SCFloatArrayRef In, int Index, int Length)
    {
        const int N = Fit(Index, Length);
        if (N <= 1) return In[Index];
        double SumX = 0, SumY = 0, SumXY = 0, SumX2 = 0;
        for (int k = 0; k < N; ++k)
        {
            const double X = (double)k;
            const double Y = (double)In[Index - (N-1) + k];
            SumX += X; SumY += Y; SumXY += X*Y; SumX2 += X*X;
        }
        const double Nd  = (double)N;
        const double Den = Nd*SumX2 - SumX*SumX;
        if (Den == 0.0) return In[Index];
        const double Slope = (Nd*SumXY - SumX*SumY) / Den;
        const double Icept = (SumY - Slope*SumX) / Nd;
        return (float)(Icept + Slope*(Nd - 1.0));
    }

    // Recursive averages written out so they match Pine exactly and don't
    // depend on Sierra's seeding conventions.
    inline void EmaStep(SCFloatArrayRef In, SCFloatArrayRef Out, int Index, int Length)
    {
        if (Length < 1) Length = 1;
        if (Index == 0) { Out[0] = In[0]; return; }
        const float A = 2.0f / (float)(Length + 1);
        Out[Index] = A * In[Index] + (1.0f - A) * Out[Index-1];
    }

    // Wilder / Pine ta.rma
    inline void RmaStep(SCFloatArrayRef In, SCFloatArrayRef Out, int Index, int Length)
    {
        if (Length < 1) Length = 1;
        if (Index < Length) { Out[Index] = SMA(In, Index, Index + 1); return; }
        Out[Index] = (Out[Index-1] * (float)(Length - 1) + In[Index]) / (float)Length;
    }

    inline float RsiFrom(float UpRma, float DownRma)
    {
        if (DownRma == 0.0f) return 100.0f;
        if (UpRma   == 0.0f) return 0.0f;
        return 100.0f - (100.0f / (1.0f + UpRma / DownRma));
    }

    inline bool CrossOver (float A, float PrevA, float B, float PrevB)
    { return A > B && PrevA <= PrevB; }
    inline bool CrossUnder(float A, float PrevA, float B, float PrevB)
    { return A < B && PrevA >= PrevB; }
}

// ============================================================================
SCSFExport scsf_NebulaV21s(SCStudyInterfaceRef sc)
{
    using namespace NB;

    // ---------------- Visible subgraphs ----------------
    SCSubgraphRef SG_Body       = sc.Subgraph[0];
    SCSubgraphRef SG_Bar        = sc.Subgraph[1];
    SCSubgraphRef SG_BuyDot     = sc.Subgraph[2];
    SCSubgraphRef SG_SellDot    = sc.Subgraph[3];
    SCSubgraphRef SG_StrongBuy  = sc.Subgraph[4];
    SCSubgraphRef SG_StrongSell = sc.Subgraph[5];
    SCSubgraphRef SG_PlusUp     = sc.Subgraph[6];
    SCSubgraphRef SG_PlusDown   = sc.Subgraph[7];
    SCSubgraphRef SG_VodkaUp    = sc.Subgraph[8];
    SCSubgraphRef SG_VodkaDown  = sc.Subgraph[9];
    SCSubgraphRef SG_TpAll      = sc.Subgraph[10];
    SCSubgraphRef SG_TpPartial  = sc.Subgraph[11];
    SCSubgraphRef SG_CrossUp    = sc.Subgraph[12];
    SCSubgraphRef SG_CrossDown  = sc.Subgraph[13];
    SCSubgraphRef SG_Hema       = sc.Subgraph[14];
    SCSubgraphRef SG_FanVma     = sc.Subgraph[15];
    SCSubgraphRef SG_McG        = sc.Subgraph[16];
    SCSubgraphRef SG_Rqk        = sc.Subgraph[17];
    SCSubgraphRef SG_UltBuy     = sc.Subgraph[18];
    SCSubgraphRef SG_UltSell    = sc.Subgraph[19];
    SCSubgraphRef SG_TrampUp    = sc.Subgraph[20];
    SCSubgraphRef SG_TrampDown  = sc.Subgraph[21];
    SCSubgraphRef SG_SqzBuy     = sc.Subgraph[22];
    SCSubgraphRef SG_SqzSell    = sc.Subgraph[23];

    // ---------------- Hidden state / calculation arrays ----------------
    SCSubgraphRef H_TR      = sc.Subgraph[30];  // [0]=+DM [1]=-DM
    SCSubgraphRef H_RmaTR   = sc.Subgraph[31];
    SCSubgraphRef H_RmaPDM  = sc.Subgraph[32];
    SCSubgraphRef H_RmaMDM  = sc.Subgraph[33];
    SCSubgraphRef H_DX      = sc.Subgraph[34];  // [0]=+DI [1]=-DI
    SCSubgraphRef H_ADX     = sc.Subgraph[35];
    SCSubgraphRef H_Ema9    = sc.Subgraph[36];
    SCSubgraphRef H_Ema21   = sc.Subgraph[37];
    SCSubgraphRef H_EmaFast = sc.Subgraph[38];
    SCSubgraphRef H_EmaSlow = sc.Subgraph[39];
    SCSubgraphRef H_Macd    = sc.Subgraph[40];
    SCSubgraphRef H_Vol     = sc.Subgraph[41];  // [0]=buyVol [1]=sellVol
    SCSubgraphRef H_CumBuy  = sc.Subgraph[42];
    SCSubgraphRef H_CumSell = sc.Subgraph[43];
    SCSubgraphRef H_Fan     = sc.Subgraph[44];  // [0]=sPDI [1]=sMDI [2]=STR [3]=ADX [4]=VarMA
    SCSubgraphRef H_FanVma  = sc.Subgraph[45];
    SCSubgraphRef H_McG     = sc.Subgraph[46];
    SCSubgraphRef H_Hema    = sc.Subgraph[47];  // [0]=bH
    SCSubgraphRef H_Rsi14   = sc.Subgraph[48];  // [0]=upRma [1]=downRma
    SCSubgraphRef H_Rsi32   = sc.Subgraph[49];  // [0]=upRma [1]=downRma
    SCSubgraphRef H_RsiB    = sc.Subgraph[50];  // [0]=upper [1]=lower [2]=rsiMa
    SCSubgraphRef H_Atr     = sc.Subgraph[51];  // [0]=TR
    SCSubgraphRef H_AtrMa   = sc.Subgraph[52];
    SCSubgraphRef H_SqzSrc  = sc.Subgraph[53];
    SCSubgraphRef H_SqzVal  = sc.Subgraph[54];
    SCSubgraphRef H_SqzSt   = sc.Subgraph[55];  // [0]=cGreen [1]=cRed [2]=pos [3]=neg
    SCSubgraphRef H_Watch   = sc.Subgraph[56];  // [0]=buyW [1]=sellW [2]=lastClear
                                                // [3]=plotBuy [4]=plotSell [5]=lastSigBar
    SCSubgraphRef H_Wave    = sc.Subgraph[57];  // [0]=state [1]=brGreen [2]=brRed
                                                // [3]=gapGreen [4]=gapRed
    SCSubgraphRef H_Lux     = sc.Subgraph[58];  // [0]=bSC [1]=sSC
    SCSubgraphRef H_Fract   = sc.Subgraph[59];  // [0]=upFrac [1]=dnFrac [2]=upClose [3]=dnClose
    SCSubgraphRef H_Pvsra   = sc.Subgraph[60];  // [0]=volSpread [1]=green [2]=red
    SCSubgraphRef H_Vola    = sc.Subgraph[61];  // [0]=c [1]=c_plus [2]=c_minus
    SCSubgraphRef H_AvgVola = sc.Subgraph[62];
    SCSubgraphRef H_Dem     = sc.Subgraph[63];
    SCSubgraphRef H_Sup     = sc.Subgraph[64];
    SCSubgraphRef H_Adp     = sc.Subgraph[65];  // [0]=asp [1]=anp [2]=anp_s
    SCSubgraphRef H_LL1     = sc.Subgraph[66];
    SCSubgraphRef H_LL2     = sc.Subgraph[67];
    SCSubgraphRef H_LL3     = sc.Subgraph[68];
    SCSubgraphRef H_Vodka   = sc.Subgraph[69];  // [0]=upwards [1]=downwards
    SCSubgraphRef H_Tramp   = sc.Subgraph[70];  // [0]=weGoUp [1]=weGoDown
    SCSubgraphRef H_Flags   = sc.Subgraph[71];  // [0]=DSR [1]=Lux [2]=Tramp [3]=Sqz
                                                // [4]=Recall [5]=Shark [6]=Bands
    SCSubgraphRef H_Mfi     = sc.Subgraph[72];  // [0]=posMF [1]=negMF
    SCSubgraphRef H_Cci     = sc.Subgraph[73];
    SCSubgraphRef H_Hlc3    = sc.Subgraph[74];
    SCSubgraphRef H_Imb     = sc.Subgraph[75];  // [0]=level [1]=breakBar [2]=isUp
    SCSubgraphRef H_Tmp     = sc.Subgraph[76];

    // ---------------- Inputs ----------------
    SCInputRef In_CloudType   = sc.Input[0];
    SCInputRef In_CandleType  = sc.Input[1];
    SCInputRef In_Theme       = sc.Input[2];
    SCInputRef In_Background  = sc.Input[3];
    SCInputRef In_ColorBody   = sc.Input[4];
    SCInputRef In_ColorBar    = sc.Input[5];
    SCInputRef In_ConfirmOnly = sc.Input[6];
    SCInputRef In_MarkerTicks = sc.Input[7];

    SCInputRef In_ShowHEMA    = sc.Input[10];
    SCInputRef In_ShowPlus    = sc.Input[11];
    SCInputRef In_ShowBigPlus = sc.Input[12];
    SCInputRef In_ShowProfit  = sc.Input[13];
    SCInputRef In_Show921     = sc.Input[14];
    SCInputRef In_ShowQuad    = sc.Input[15];
    SCInputRef In_ShowRqkLine = sc.Input[16];
    SCInputRef In_ShowUlt     = sc.Input[17];
    SCInputRef In_ShowTramp   = sc.Input[18];
    SCInputRef In_ShowSqz     = sc.Input[19];

    SCInputRef In_IgnoreDoji  = sc.Input[20];
    SCInputRef In_MaxBody     = sc.Input[21];
    SCInputRef In_Profit      = sc.Input[22];
    SCInputRef In_MaxProfit   = sc.Input[23];

    SCInputRef In_CloudFill   = sc.Input[24];
    SCInputRef In_SimpleCloud = sc.Input[25];
    SCInputRef In_LowCloud    = sc.Input[26];
    SCInputRef In_HighCloud   = sc.Input[27];
    SCInputRef In_CloudBars   = sc.Input[28];
    SCInputRef In_TopBody     = sc.Input[29];
    SCInputRef In_TopBorder   = sc.Input[30];
    SCInputRef In_VolDeltaTop = sc.Input[31];
    SCInputRef In_CloudOutline= sc.Input[32];

    SCInputRef In_AdxLength   = sc.Input[33];
    SCInputRef In_Weighting   = sc.Input[34];
    SCInputRef In_MaLength    = sc.Input[35];

    SCInputRef In_AdxSmoothing= sc.Input[37];
    SCInputRef In_DiLength    = sc.Input[38];
    SCInputRef In_AdxSqueeze  = sc.Input[39];

    SCInputRef In_TrackBar    = sc.Input[41];
    SCInputRef In_BarExtend   = sc.Input[42];
    SCInputRef In_LineWidth   = sc.Input[43];
    SCInputRef In_LineStyle   = sc.Input[44];

    SCInputRef In_WaeSens     = sc.Input[46];
    SCInputRef In_WaeFast     = sc.Input[47];
    SCInputRef In_WaeSlow     = sc.Input[48];
    SCInputRef In_WaeChan     = sc.Input[49];
    SCInputRef In_WaeMult     = sc.Input[50];

    SCInputRef In_TrampBBThr  = sc.Input[52];
    SCInputRef In_TrampRsiLo  = sc.Input[53];
    SCInputRef In_TrampRsiHi  = sc.Input[54];
    SCInputRef In_TrampRsiLen = sc.Input[55];
    SCInputRef In_TrampBBLen  = sc.Input[56];
    SCInputRef In_TrampBBMult = sc.Input[57];

    SCInputRef In_SqzTol      = sc.Input[59];
    SCInputRef In_SqzLength   = sc.Input[60];
    SCInputRef In_SqzKcMult   = sc.Input[61];

    SCInputRef In_VecViolet   = sc.Input[63];
    SCInputRef In_VecBlue     = sc.Input[64];
    SCInputRef In_VecRegUp    = sc.Input[65];
    SCInputRef In_VecRegDown  = sc.Input[66];

    SCInputRef In_Apply2575   = sc.Input[68];

    SCInputRef In_WatchLook   = sc.Input[70];
    SCInputRef In_RequireWatch= sc.Input[71];
    SCInputRef In_UseSigWait  = sc.Input[72];
    SCInputRef In_SigWaitBars = sc.Input[73];
    SCInputRef In_RsiLength   = sc.Input[74];
    SCInputRef In_RsiBasisLen = sc.Input[75];
    SCInputRef In_RsiMult     = sc.Input[76];
    SCInputRef In_UseRsiWatch = sc.Input[77];
    SCInputRef In_PriceBasis  = sc.Input[78];
    SCInputRef In_PriceInner  = sc.Input[79];
    SCInputRef In_UsePriceW   = sc.Input[80];
    SCInputRef In_AtrPeriod   = sc.Input[81];
    SCInputRef In_AtrMaPeriod = sc.Input[82];
    SCInputRef In_AtrMult     = sc.Input[83];
    SCInputRef In_AtrMaType   = sc.Input[84];
    SCInputRef In_UseAtrWatch = sc.Input[85];
    SCInputRef In_RsiMaLength = sc.Input[86];
    SCInputRef In_RsiMaType   = sc.Input[87];
    SCInputRef In_UseRsiBasis = sc.Input[88];
    SCInputRef In_Use75       = sc.Input[89];
    SCInputRef In_Use25       = sc.Input[90];
    SCInputRef In_UseRsiMa    = sc.Input[91];

    SCInputRef In_VaderLen    = sc.Input[93];
    SCInputRef In_DerAvg      = sc.Input[94];
    SCInputRef In_DerMaType   = sc.Input[95];
    SCInputRef In_RSmooth     = sc.Input[96];
    SCInputRef In_VCalc       = sc.Input[97];
    SCInputRef In_VLookback   = sc.Input[98];
    SCInputRef In_LazyLen     = sc.Input[99];

    SCInputRef In_WickBBLen   = sc.Input[101];
    SCInputRef In_WickBBMult  = sc.Input[102];

    SCInputRef In_RqkH        = sc.Input[104];
    SCInputRef In_RqkR        = sc.Input[105];
    SCInputRef In_RqkX0       = sc.Input[106];

    SCInputRef In_AlphaLength = sc.Input[108];
    SCInputRef In_GammaLength = sc.Input[109];

    const int CLOUD_LINE_BASE = 1000000;
    const int IMB_LINE_BASE   = 3000000;

    // ========================================================================
    if (sc.SetDefaults)
    {
        sc.GraphName        = "Nebula v2.1s";
        sc.StudyDescription = "ACSIL port of TraderOracle's Nebula v2.1s.";
        sc.GraphRegion      = 0;
        sc.AutoLoop         = 1;
        sc.ValueFormat      = VALUEFORMAT_INHERITED;
        sc.FreeDLL          = 0;

        SG_Body.Name      = "Candle Body Fill";
        SG_Body.DrawStyle = DRAWSTYLE_COLOR_BAR_CANDLE_FILL;

        SG_Bar.Name       = "Candle Outline / Wick";
        SG_Bar.DrawStyle  = DRAWSTYLE_COLOR_BAR;

        SG_BuyDot.Name         = "Buy Signal";
        SG_BuyDot.DrawStyle    = DRAWSTYLE_POINT;
        SG_BuyDot.PrimaryColor = MakeCol(0,255,0);
        SG_BuyDot.LineWidth    = 4;   SG_BuyDot.DrawZeros = 0;

        SG_SellDot.Name         = "Sell Signal";
        SG_SellDot.DrawStyle    = DRAWSTYLE_POINT;
        SG_SellDot.PrimaryColor = MakeCol(255,0,0);
        SG_SellDot.LineWidth    = 4;   SG_SellDot.DrawZeros = 0;

        SG_StrongBuy.Name         = "Strong Buy Signal";
        SG_StrongBuy.DrawStyle    = DRAWSTYLE_ARROW_UP;
        SG_StrongBuy.PrimaryColor = MakeCol(0,255,0);
        SG_StrongBuy.LineWidth    = 3;  SG_StrongBuy.DrawZeros = 0;

        SG_StrongSell.Name         = "Strong Sell Signal";
        SG_StrongSell.DrawStyle    = DRAWSTYLE_ARROW_DOWN;
        SG_StrongSell.PrimaryColor = MakeCol(255,0,0);
        SG_StrongSell.LineWidth    = 3;  SG_StrongSell.DrawZeros = 0;

        SG_PlusUp.Name         = "Add Contract (Light)";
        SG_PlusUp.DrawStyle    = DRAWSTYLE_TRIANGLE_UP;
        SG_PlusUp.PrimaryColor = MakeCol(0,255,0);
        SG_PlusUp.LineWidth    = 2;  SG_PlusUp.DrawZeros = 0;

        SG_PlusDown.Name         = "Add Contract (Light) Down";
        SG_PlusDown.DrawStyle    = DRAWSTYLE_TRIANGLE_DOWN;
        SG_PlusDown.PrimaryColor = MakeCol(255,0,0);
        SG_PlusDown.LineWidth    = 2;  SG_PlusDown.DrawZeros = 0;

        SG_VodkaUp.Name         = "Add Contract (Vodka Shot)";
        SG_VodkaUp.DrawStyle    = DRAWSTYLE_STAR;
        SG_VodkaUp.PrimaryColor = MakeCol(0,255,0);
        SG_VodkaUp.LineWidth    = 4;  SG_VodkaUp.DrawZeros = 0;

        SG_VodkaDown.Name         = "Add Contract (Vodka Shot) Down";
        SG_VodkaDown.DrawStyle    = DRAWSTYLE_STAR;
        SG_VodkaDown.PrimaryColor = MakeCol(255,0,0);
        SG_VodkaDown.LineWidth    = 4;  SG_VodkaDown.DrawZeros = 0;

        SG_TpAll.Name         = "Take Profit (iMaxProfit)";
        SG_TpAll.DrawStyle    = DRAWSTYLE_SQUARE;
        SG_TpAll.PrimaryColor = MakeCol(255,0,0);
        SG_TpAll.LineWidth    = 4;  SG_TpAll.DrawZeros = 0;

        SG_TpPartial.Name         = "Take Profit (iProfit)";
        SG_TpPartial.DrawStyle    = DRAWSTYLE_SQUARE;
        SG_TpPartial.PrimaryColor = MakeCol(160,32,240);
        SG_TpPartial.LineWidth    = 4;  SG_TpPartial.DrawZeros = 0;

        SG_CrossUp.Name         = "9/21 Cross Up";
        SG_CrossUp.DrawStyle    = DRAWSTYLE_TRIANGLE_UP;
        SG_CrossUp.PrimaryColor = MakeCol(255,255,0);
        SG_CrossUp.LineWidth    = 3;  SG_CrossUp.DrawZeros = 0;

        SG_CrossDown.Name         = "9/21 Cross Down";
        SG_CrossDown.DrawStyle    = DRAWSTYLE_TRIANGLE_DOWN;
        SG_CrossDown.PrimaryColor = MakeCol(255,255,0);
        SG_CrossDown.LineWidth    = 3;  SG_CrossDown.DrawZeros = 0;

        SG_Hema.Name         = "HEMA";
        SG_Hema.DrawStyle    = DRAWSTYLE_LINE;
        SG_Hema.PrimaryColor = MakeCol(255,0,255);
        SG_Hema.LineWidth    = 1;  SG_Hema.DrawZeros = 0;

        SG_FanVma.Name         = "Fantail VMA (cloud edge)";
        SG_FanVma.DrawStyle    = DRAWSTYLE_LINE;
        SG_FanVma.PrimaryColor = MakeCol(90,90,90);
        SG_FanVma.LineWidth    = 1;  SG_FanVma.DrawZeros = 0;

        SG_McG.Name         = "McGinley Dynamic (cloud edge)";
        SG_McG.DrawStyle    = DRAWSTYLE_LINE;
        SG_McG.PrimaryColor = MakeCol(90,90,90);
        SG_McG.LineWidth    = 1;  SG_McG.DrawZeros = 0;

        SG_Rqk.Name         = "Rational Quadratic Kernel";
        SG_Rqk.DrawStyle    = DRAWSTYLE_LINE;
        SG_Rqk.PrimaryColor = MakeCol(255,255,255);
        SG_Rqk.LineWidth    = 2;  SG_Rqk.DrawZeros = 0;

        SG_UltBuy.Name    = "Ultimate Buy";
        SG_UltBuy.DrawStyle = DRAWSTYLE_TRIANGLE_UP;
        SG_UltBuy.PrimaryColor = MakeCol(0,200,255);
        SG_UltBuy.LineWidth = 3;  SG_UltBuy.DrawZeros = 0;

        SG_UltSell.Name    = "Ultimate Sell";
        SG_UltSell.DrawStyle = DRAWSTYLE_TRIANGLE_DOWN;
        SG_UltSell.PrimaryColor = MakeCol(255,140,0);
        SG_UltSell.LineWidth = 3;  SG_UltSell.DrawZeros = 0;

        SG_TrampUp.Name = "Trampoline Up";
        SG_TrampUp.DrawStyle = DRAWSTYLE_SQUARE;
        SG_TrampUp.PrimaryColor = MakeCol(0,255,0);
        SG_TrampUp.LineWidth = 3;  SG_TrampUp.DrawZeros = 0;

        SG_TrampDown.Name = "Trampoline Down";
        SG_TrampDown.DrawStyle = DRAWSTYLE_SQUARE;
        SG_TrampDown.PrimaryColor = MakeCol(255,0,0);
        SG_TrampDown.LineWidth = 3;  SG_TrampDown.DrawZeros = 0;

        SG_SqzBuy.Name = "Squeeze Buy";
        SG_SqzBuy.DrawStyle = DRAWSTYLE_DIAMOND;
        SG_SqzBuy.PrimaryColor = MakeCol(255,230,0);
        SG_SqzBuy.LineWidth = 3;  SG_SqzBuy.DrawZeros = 0;

        SG_SqzSell.Name = "Squeeze Sell";
        SG_SqzSell.DrawStyle = DRAWSTYLE_DIAMOND;
        SG_SqzSell.PrimaryColor = MakeCol(255,230,0);
        SG_SqzSell.LineWidth = 3;  SG_SqzSell.DrawZeros = 0;

        for (int k = 30; k <= 76; ++k)
        {
            sc.Subgraph[k].Name      = "";
            sc.Subgraph[k].DrawStyle = DRAWSTYLE_IGNORE;
        }

        In_CloudType.Name = "Cloud Type";
        In_CloudType.SetCustomInputStrings("None;Simple;Relative Strength;Money Flow;Commodity Channel");
        In_CloudType.SetCustomInputIndex(CLOUD_SIMPLE);

        In_CandleType.Name = "Candle Coloring";
        In_CandleType.SetCustomInputStrings("None;Vector;Waddah;Squeeze;Volume Delta");
        In_CandleType.SetCustomInputIndex(CANDLE_WADDAH);

        In_Theme.Name = "Color Theme";
        In_Theme.SetCustomInputStrings("Standard;Pinky and the Brain;Color Blind;Mellow Yellow");
        In_Theme.SetCustomInputIndex(0);

        In_Background.Name = "Chart Background Color (transparency emulation)";
        In_Background.SetColor(0,0,0);

        In_ColorBody.Name    = "Color Candle Body Fill";        In_ColorBody.SetYesNo(1);
        In_ColorBar.Name     = "Color Candle Outline / Wick";   In_ColorBar.SetYesNo(1);
        In_ConfirmOnly.Name  = "Evaluate Signals Only On Bar Close"; In_ConfirmOnly.SetYesNo(1);
        In_MarkerTicks.Name  = "Marker Offset (ticks)";         In_MarkerTicks.SetInt(4);

        In_ShowHEMA.Name    = "Show HEMA line";                  In_ShowHEMA.SetYesNo(0);
        In_ShowPlus.Name    = "Show plus sign to add";           In_ShowPlus.SetYesNo(1);
        In_ShowBigPlus.Name = "Show bigger plus sign (Vodka Shot)"; In_ShowBigPlus.SetYesNo(1);
        In_ShowProfit.Name  = "Show take profit suggestions";    In_ShowProfit.SetYesNo(1);
        In_Show921.Name     = "Show 9/21 EMA cross";             In_Show921.SetYesNo(0);
        In_ShowQuad.Name    = "Use quadratic equation for 9/21 cross"; In_ShowQuad.SetYesNo(0);
        In_ShowRqkLine.Name = "Plot the Rational Quadratic Kernel line"; In_ShowRqkLine.SetYesNo(0);
        In_ShowUlt.Name     = "Show Ultimate Buy/Sell markers";  In_ShowUlt.SetYesNo(0);
        In_ShowTramp.Name   = "Show Trampoline markers";         In_ShowTramp.SetYesNo(0);
        In_ShowSqz.Name     = "Show Squeeze markers";            In_ShowSqz.SetYesNo(0);

        In_IgnoreDoji.Name = "Ignore dojis (only use on NQ, 1 min)"; In_IgnoreDoji.SetYesNo(0);
        In_MaxBody.Name    = "Body size (ticks) to consider a doji"; In_MaxBody.SetInt(1);
        In_Profit.Name     = "Minimum signals for take partial profit"; In_Profit.SetInt(5);
        In_MaxProfit.Name  = "Minimum signals for take ALL profit";  In_MaxProfit.SetInt(7);

        In_CloudFill.Name = "Draw cloud fill (per-bar rectangles)";
        In_CloudFill.SetYesNo(1);
        In_SimpleCloud.Name = "Simple Cloud Opacity (0=brightest, 100=invisible)";
        In_SimpleCloud.SetInt(80);
        In_LowCloud.Name  = "Cloud Opacity Lower Limit";  In_LowCloud.SetInt(80);
        In_HighCloud.Name = "Cloud Opacity Upper Limit";  In_HighCloud.SetInt(50);
        In_CloudBars.Name = "Cloud: max bars to render (0 = all)"; In_CloudBars.SetInt(2000);
        In_CloudOutline.Name = "Cloud outline width (0 = none)";   In_CloudOutline.SetInt(0);
        In_TopBody.Name   = "Top WAE Body Value";   In_TopBody.SetInt(80);
        In_TopBorder.Name = "Top WAE Border Value"; In_TopBorder.SetInt(33);
        In_VolDeltaTop.Name = "Cumulative Volume Delta top end"; In_VolDeltaTop.SetInt(300);

        In_AdxLength.Name = "Fantail: ADX_Length"; In_AdxLength.SetInt(2);
        In_Weighting.Name = "Fantail: Weighting";  In_Weighting.SetFloat(10.0f);
        In_MaLength.Name  = "Fantail: MA_Length";  In_MaLength.SetInt(6);

        In_AdxSmoothing.Name = "ADX Smoothing"; In_AdxSmoothing.SetInt(14);
        In_DiLength.Name     = "DI Length";     In_DiLength.SetInt(14);
        In_AdxSqueeze.Name   = "ADX Threshold for Tidal Wave"; In_AdxSqueeze.SetInt(0);

        In_TrackBar.Name  = "Show volume imbalances"; In_TrackBar.SetYesNo(0);
        In_BarExtend.Name = "Number of bars to extend line"; In_BarExtend.SetInt(50);
        In_LineWidth.Name = "Imbalance Line Width"; In_LineWidth.SetInt(3);
        In_LineStyle.Name = "Imbalance Line Style";
        In_LineStyle.SetCustomInputStrings("Solid;Dotted;Dashed");
        In_LineStyle.SetCustomInputIndex(1);

        In_WaeSens.Name = "WAE Sensitivity";       In_WaeSens.SetInt(150);
        In_WaeFast.Name = "WAE FastEMA Length";    In_WaeFast.SetInt(20);
        In_WaeSlow.Name = "WAE SlowEMA Length";    In_WaeSlow.SetInt(40);
        In_WaeChan.Name = "WAE BB Channel Length"; In_WaeChan.SetInt(20);
        In_WaeMult.Name = "WAE BB Stdev Multiplier"; In_WaeMult.SetFloat(2.0f);

        In_TrampBBThr.Name  = "Trampoline: Bollinger Lower Threshold"; In_TrampBBThr.SetFloat(0.0015f);
        In_TrampRsiLo.Name  = "Trampoline: RSI Lower Threshold"; In_TrampRsiLo.SetInt(25);
        In_TrampRsiHi.Name  = "Trampoline: RSI Upper Threshold"; In_TrampRsiHi.SetInt(72);
        In_TrampRsiLen.Name = "Trampoline: RSI Length"; In_TrampRsiLen.SetInt(14);
        In_TrampBBLen.Name  = "Trampoline: BB Length";  In_TrampBBLen.SetInt(20);
        In_TrampBBMult.Name = "Trampoline: BB StdDev";  In_TrampBBMult.SetFloat(2.0f);

        In_SqzTol.Name     = "Squeeze Tolerance";      In_SqzTol.SetInt(2);
        In_SqzLength.Name  = "Squeeze / Keltner Length"; In_SqzLength.SetInt(20);
        In_SqzKcMult.Name  = "Squeeze Keltner Multiplier"; In_SqzKcMult.SetFloat(1.5f);

        In_VecViolet.Name  = "Vector: Violet";  In_VecViolet.SetColor(255,0,255);
        In_VecBlue.Name    = "Vector: Blue";    In_VecBlue.SetColor(83,144,249);
        In_VecRegUp.Name   = "Vector: Regular Up";   In_VecRegUp.SetColor(2,164,51);
        In_VecRegDown.Name = "Vector: Regular Down"; In_VecRegDown.SetColor(161,1,1);

        In_Apply2575.Name = "Shark: Apply 25/75 RSI rule"; In_Apply2575.SetYesNo(0);

        In_WatchLook.Name    = "# of bars back to use Watch Signals"; In_WatchLook.SetInt(35);
        In_RequireWatch.Name = "Require Watch Signals"; In_RequireWatch.SetYesNo(1);
        In_UseSigWait.Name   = "Use Signal Waiting";    In_UseSigWait.SetYesNo(0);
        In_SigWaitBars.Name  = "# of bars before signals are allowed"; In_SigWaitBars.SetInt(5);
        In_RsiLength.Name    = "RSI Length";       In_RsiLength.SetInt(32);
        In_RsiBasisLen.Name  = "RSI Basis Length"; In_RsiBasisLen.SetInt(32);
        In_RsiMult.Name      = "RSI Band Multiplier"; In_RsiMult.SetFloat(2.0f);
        In_UseRsiWatch.Name  = "RSI Watch Signals";   In_UseRsiWatch.SetYesNo(1);
        In_PriceBasis.Name   = "Price BBand Basis Length"; In_PriceBasis.SetInt(20);
        In_PriceInner.Name   = "Price Inner BB Multiplier"; In_PriceInner.SetFloat(2.0f);
        In_UsePriceW.Name    = "Bollinger Band Watch Signals"; In_UsePriceW.SetYesNo(1);
        In_AtrPeriod.Name    = "ATR Period";    In_AtrPeriod.SetInt(30);
        In_AtrMaPeriod.Name  = "ATR MA Period"; In_AtrMaPeriod.SetInt(10);
        In_AtrMult.Name      = "ATR Band multiplier"; In_AtrMult.SetFloat(1.5f);
        In_AtrMaType.Name    = "ATR Moving Average Type";
        In_AtrMaType.SetCustomInputStrings("SMA;EMA;WMA;HMA;VWMA;RMA");
        In_AtrMaType.SetCustomInputIndex(MA_WMA);
        In_UseAtrWatch.Name  = "ATR watch signals"; In_UseAtrWatch.SetYesNo(1);
        In_RsiMaLength.Name  = "Length of additional RSI MA"; In_RsiMaLength.SetInt(24);
        In_RsiMaType.Name    = "RSI MA Type";
        In_RsiMaType.SetCustomInputStrings("SMA;EMA;WMA;HMA;VWMA;RMA");
        In_RsiMaType.SetCustomInputIndex(MA_WMA);
        In_UseRsiBasis.Name  = "Signal: RSI crossing Basis";    In_UseRsiBasis.SetYesNo(1);
        In_Use75.Name        = "Signal: RSI crossing under 75"; In_Use75.SetYesNo(1);
        In_Use25.Name        = "Signal: RSI crossing over 25";  In_Use25.SetYesNo(1);
        In_UseRsiMa.Name     = "Signal: RSI crossing a MA";     In_UseRsiMa.SetYesNo(1);

        In_VaderLen.Name  = "VADER: Length";    In_VaderLen.SetInt(12);
        In_DerAvg.Name    = "VADER: Average";   In_DerAvg.SetInt(5);
        In_DerMaType.Name = "VADER: DER MA type";
        In_DerMaType.SetCustomInputStrings("WMA;EMA;SMA");
        In_DerMaType.SetCustomInputIndex(0);
        In_RSmooth.Name   = "VADER: Smooth";    In_RSmooth.SetInt(3);
        In_VCalc.Name     = "VADER: Volume Calculation";
        In_VCalc.SetCustomInputStrings("Relative;Full;None");
        In_VCalc.SetCustomInputIndex(0);
        In_VLookback.Name = "VADER: Lookback (for Relative)"; In_VLookback.SetInt(20);
        In_LazyLen.Name   = "Lazy Line Length"; In_LazyLen.SetInt(21);

        In_WickBBLen.Name  = "Wicking BB Length"; In_WickBBLen.SetInt(20);
        In_WickBBMult.Name = "Wicking BB StdDev"; In_WickBBMult.SetFloat(2.5f);

        In_RqkH.Name  = "RQK Lookback Window";    In_RqkH.SetFloat(21.0f);
        In_RqkR.Name  = "RQK Relative Weighting"; In_RqkR.SetFloat(8.0f);
        In_RqkX0.Name = "RQK Start Regression at Bar"; In_RqkX0.SetInt(15);

        In_AlphaLength.Name = "HEMA Alpha Length"; In_AlphaLength.SetInt(20);
        In_GammaLength.Name = "HEMA Gamma Length"; In_GammaLength.SetInt(20);

        return;
    }

    // ========================================================================
    //  Housekeeping
    // ========================================================================
    if (sc.UpdateStartIndex == 0)
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_ALL, 0);

    const int i = sc.Index;
    if (i < 1)
    {
        H_Wave.Arrays[0][0] = (float)WAVE_NONE;
        return;
    }

    const float O = sc.Open[i], H = sc.High[i], L = sc.Low[i], C = sc.Close[i];
    const float PrevC = sc.Close[i-1];

    const bool BarConfirmed = (In_ConfirmOnly.GetYesNo() == 0)
                            || (i < sc.ArraySize - 1)
                            || (sc.GetBarHasClosedStatus(i) == BHCS_BAR_HAS_CLOSED);

    COLORREF BigGreen, BigRed;
    switch (In_Theme.GetIndex())
    {
        case 1:  BigGreen = MakeCol(0x00,0xf5,0xf1); BigRed = MakeCol(0xfc,0x03,0xf8); break;
        case 2:  BigGreen = MakeCol(0x03,0xfc,0xf4); BigRed = MakeCol(0xfc,0xa9,0x03); break;
        case 3:  BigGreen = MakeCol(0x35,0xde,0xfc); BigRed = MakeCol(0xfc,0xf1,0x1c); break;
        default: BigGreen = MakeCol(0x00,0xff,0x00); BigRed = MakeCol(0xff,0x00,0x00); break;
    }
    const COLORREF Bg = In_Background.GetColor();

    const bool IsGreenBar = C > O;
    const bool IsRedBar   = C < O;

    H_Hlc3[i] = (H + L + C) / 3.0f;

    int TpCount = 0;   // Pine's iTPSignalCount, rebuilt every bar

    // ------------------------------------------------------------------
    //  ADX / DI  (Pine dirmov())
    // ------------------------------------------------------------------
    const int DiLen  = max(1, In_DiLength.GetInt());
    const int AdxLen = max(1, In_AdxSmoothing.GetInt());

    {
        const float Up5   =  sc.High[i] - sc.High[i-1];
        const float Down5 = -(sc.Low[i] - sc.Low[i-1]);
        H_TR.Arrays[0][i] = (Up5   > Down5 && Up5   > 0.0f) ? Up5   : 0.0f;
        H_TR.Arrays[1][i] = (Down5 > Up5   && Down5 > 0.0f) ? Down5 : 0.0f;
        H_TR[i] = max(H - L, max(fabsf(H - PrevC), fabsf(L - PrevC)));

        RmaStep(H_TR,            H_RmaTR,  i, DiLen);
        RmaStep(H_TR.Arrays[0],  H_RmaPDM, i, DiLen);
        RmaStep(H_TR.Arrays[1],  H_RmaMDM, i, DiLen);

        if (H_RmaTR[i] > 0.0f)
        {
            H_DX.Arrays[0][i] = 100.0f * H_RmaPDM[i] / H_RmaTR[i];
            H_DX.Arrays[1][i] = 100.0f * H_RmaMDM[i] / H_RmaTR[i];
        }
        else   // fixnan()
        {
            H_DX.Arrays[0][i] = H_DX.Arrays[0][i-1];
            H_DX.Arrays[1][i] = H_DX.Arrays[1][i-1];
        }
        const float S = H_DX.Arrays[0][i] + H_DX.Arrays[1][i];
        H_DX[i] = fabsf(H_DX.Arrays[0][i] - H_DX.Arrays[1][i]) / (S == 0.0f ? 1.0f : S);
        RmaStep(H_DX, H_ADX, i, AdxLen);
    }
    const float AdxValue    = 100.0f * H_ADX[i];
    const bool  SigAboveAdx = AdxValue > (float)In_AdxSqueeze.GetInt();

    // ------------------------------------------------------------------
    //  Cumulative Volume Delta candles  (Ankit_1618)
    // ------------------------------------------------------------------
    {
        const float UpperWick = IsGreenBar ? (H - C) : (H - O);
        const float LowerWick = IsGreenBar ? (O - L) : (C - L);
        const float Spread    = H - L;
        const float BodyLen   = Spread - (UpperWick + LowerWick);

        float PctU = 0, PctL = 0, PctB = 0;
        if (Spread > 0.0f)
        {
            PctU = UpperWick / Spread;
            PctL = LowerWick / Spread;
            PctB = BodyLen   / Spread;
        }
        const float Vol = sc.Volume[i];
        H_Vol.Arrays[0][i] = IsGreenBar ? (PctB + (PctU + PctL)*0.5f)*Vol : ((PctU + PctL)*0.5f)*Vol;
        H_Vol.Arrays[1][i] = IsRedBar   ? (PctB + (PctU + PctL)*0.5f)*Vol : ((PctU + PctL)*0.5f)*Vol;

        EmaStep(H_Vol.Arrays[0], H_CumBuy,  i, 14);
        EmaStep(H_Vol.Arrays[1], H_CumSell, i, 14);
    }
    const float Cvd = H_CumBuy[i] - H_CumSell[i];

    COLORREF CvdColor = MakeCol(255,255,255);
    {
        const float Top = (float)In_VolDeltaTop.GetInt();
        if (Cvd > 0.0f)
            CvdColor = Gradient(fabsf(Cvd), 0.0f, Top,
                                Transp(BigGreen, Bg, 70), Transp(BigGreen, Bg, 0));
        else if (Cvd < 0.0f)
            CvdColor = Gradient(fabsf(Cvd), 0.0f, Top,
                                Transp(BigRed, Bg, 70), Transp(BigRed, Bg, 0));
    }

    // ------------------------------------------------------------------
    //  Dead Simple Reversal            (+vDeadRev = 2)
    // ------------------------------------------------------------------
    bool BuyDsr = false, SellDsr = false;
    if (i >= 53)
    {
        const float Low3   = Lowest (sc.Low,  i, 3);
        const float High3  = Highest(sc.High, i, 3);
        const bool  c1 = (sc.Close[i-1] < sc.Open[i-1]) && IsGreenBar;
        const bool  c2 = C > sc.Open[i-1];
        const bool  c3 = Low3  < Lowest (sc.Low,  i-1, 50)
                      || Low3  < Lowest (sc.Low,  i-2, 50)
                      || Low3  < Lowest (sc.Low,  i-3, 50);
        const bool  c4 = (sc.Close[i-1] > sc.Open[i-1]) && IsRedBar;
        const bool  c5 = C < sc.Open[i-1];
        const bool  c6 = High3 > Highest(sc.High, i-1, 50)
                      || High3 > Highest(sc.High, i-2, 50)
                      || High3 > Highest(sc.High, i-3, 50);
        BuyDsr  = c1 && c2 && c3;
        SellDsr = c4 && c5 && c6;
    }
    H_Flags.Arrays[0][i] = (BuyDsr || SellDsr) ? 1.0f : 0.0f;
    if (H_Flags.Arrays[0][i] != 0.0f || H_Flags.Arrays[0][i-1] != 0.0f) TpCount += 2;

    // ------------------------------------------------------------------
    //  LuxAlgo Reversal Signals (TD-style 9 count)   (+vLuxRev = 3)
    // ------------------------------------------------------------------
    {
        int bSC = (int)H_Lux.Arrays[0][i-1];
        int sSC = (int)H_Lux.Arrays[1][i-1];
        const bool Con = (i >= 4) ? (C < sc.Close[i-4]) : false;
        if (Con) { bSC = (bSC == 9) ? 1 : bSC + 1; sSC = 0; }
        else     { sSC = (sSC == 9) ? 1 : sSC + 1; bSC = 0; }
        H_Lux.Arrays[0][i] = (float)bSC;
        H_Lux.Arrays[1][i] = (float)sSC;

        // The pbS / psS terms cancel out: (X and not P) or (X and P) == X
        const bool Uppies   = (bSC == 9 || ((int)H_Lux.Arrays[0][i-1] == 8 && sSC == 1)) && BarConfirmed;
        const bool Downies  = (sSC == 9 || ((int)H_Lux.Arrays[1][i-1] == 8 && bSC == 1)) && BarConfirmed;
        H_Flags.Arrays[1][i] = (Uppies || Downies) ? 1.0f : 0.0f;
    }
    if (H_Flags.Arrays[1][i] != 0.0f || H_Flags.Arrays[1][i-1] != 0.0f) TpCount += 3;

    // ------------------------------------------------------------------
    //  LuxAlgo Market Structure (fractals) - feeds Total Recall
    // ------------------------------------------------------------------
    {
        const int pT = 2;                       // int(length / 2), length = 5
        float UpFrac = H_Fract.Arrays[0][i-1];
        float DnFrac = H_Fract.Arrays[1][i-1];

        if (i >= 5)
        {
            float dhT = 0.0f, dlT = 0.0f, dhTp = 0.0f, dlTp = 0.0f;
            for (int k = 0; k < pT; ++k)
            {
                const int b = i - k;
                dhT += (sc.High[b] > sc.High[b-1]) ? 1.0f : ((sc.High[b] < sc.High[b-1]) ? -1.0f : 0.0f);
                dlT += (sc.Low [b] > sc.Low [b-1]) ? 1.0f : ((sc.Low [b] < sc.Low [b-1]) ? -1.0f : 0.0f);
                const int c = i - pT - k;
                if (c >= 1)
                {
                    dhTp += (sc.High[c] > sc.High[c-1]) ? 1.0f : ((sc.High[c] < sc.High[c-1]) ? -1.0f : 0.0f);
                    dlTp += (sc.Low [c] > sc.Low [c-1]) ? 1.0f : ((sc.Low [c] < sc.Low [c-1]) ? -1.0f : 0.0f);
                }
            }
            const bool BullF = (dhT == -(float)pT) && (dhTp == (float)pT)
                            && (sc.High[i-pT] == Highest(sc.High, i, 5));
            const bool BearF = (dlT ==  (float)pT) && (dlTp == -(float)pT)
                            && (sc.Low [i-pT] == Lowest (sc.Low,  i, 5));
            if (BullF) UpFrac = sc.High[i-pT];
            if (BearF) DnFrac = sc.Low [i-pT];
        }
        H_Fract.Arrays[0][i] = UpFrac;
        H_Fract.Arrays[1][i] = DnFrac;

        // upperT.iscrossed is reset but never set true, so it is always false.
        float UpClose = H_Fract.Arrays[2][i-1];
        float DnClose = H_Fract.Arrays[3][i-1];
        const float PrevUpFrac = H_Fract.Arrays[0][i-1];
        const float PrevDnFrac = H_Fract.Arrays[1][i-1];

        if (UpFrac != 0.0f && CrossOver (C, PrevC, UpFrac, PrevUpFrac)) UpClose = C;
        if (DnFrac != 0.0f && CrossUnder(C, PrevC, DnFrac, PrevDnFrac)) DnClose = C;
        H_Fract.Arrays[2][i] = UpClose;
        H_Fract.Arrays[3][i] = DnClose;
    }

    // ------------------------------------------------------------------
    //  Bixord Fantail VMA  +  McGinley Dynamic  ->  THE CLOUD
    // ------------------------------------------------------------------
    const float W = In_Weighting.GetFloat();
    {
        const float Hi = H,  Hi1 = sc.High[i-1];
        const float Lo = L,  Lo1 = sc.Low[i-1];

        const float Bulls1 = 0.5f * (fabsf(Hi - Hi1) + (Hi - Hi1));
        const float Bears1 = 0.5f * (fabsf(Lo1 - Lo) + (Lo1 - Lo));
        const float Bears  = (Bulls1 >  Bears1) ? 0.0f : ((Bulls1 == Bears1) ? 0.0f : Bears1);
        const float Bulls  = (Bulls1 <  Bears1) ? 0.0f : ((Bulls1 == Bears1) ? 0.0f : Bulls1);

        H_Fan.Arrays[0][i] = (W * H_Fan.Arrays[0][i-1] + Bulls) / (W + 1.0f);
        H_Fan.Arrays[1][i] = (W * H_Fan.Arrays[1][i-1] + Bears) / (W + 1.0f);

        const float TRf = max(Hi - Lo, Hi - PrevC);
        H_Fan.Arrays[2][i] = (W * H_Fan.Arrays[2][i-1] + TRf) / (W + 1.0f);

        const float STRv = H_Fan.Arrays[2][i];
        const float PDI  = (STRv > 0.0f) ? H_Fan.Arrays[0][i] / STRv : 0.0f;
        const float MDI  = (STRv > 0.0f) ? H_Fan.Arrays[1][i] / STRv : 0.0f;
        const float DXf  = ((PDI + MDI) > 0.0f) ? fabsf(PDI - MDI) / (PDI + MDI) : 0.0f;

        H_Fan.Arrays[3][i] = (W * H_Fan.Arrays[3][i-1] + DXf) / (W + 1.0f);

        const int   AdxL   = max(1, In_AdxLength.GetInt());
        const float AdxMin = Lowest (H_Fan.Arrays[3], i, AdxL);
        const float AdxMax = Highest(H_Fan.Arrays[3], i, AdxL);
        const float Diff   = AdxMax - AdxMin;
        const float Konst  = (Diff > 0.0f) ? (H_Fan.Arrays[3][i] - AdxMin) / Diff : 0.0f;

        H_Fan.Arrays[4][i] = ((2.0f - Konst) * H_Fan.Arrays[4][i-1] + Konst * C) * 0.5f;
        H_FanVma[i] = SMA(H_Fan.Arrays[4], i, max(1, In_MaLength.GetInt()));
    }
    {
        // McGinley Dynamic, seeded with EMA(close, 14) on the first bar
        if (i == 1)
        {
            EmaStep(sc.Close, H_Tmp, 0, 14);
            EmaStep(sc.Close, H_Tmp, 1, 14);
            H_McG[i-1] = H_Tmp[i-1];
        }
        const float Prev = H_McG[i-1];
        if (Prev == 0.0f) H_McG[i] = C;
        else
        {
            const double Ratio = (double)C / (double)Prev;
            const double Den   = 14.0 * pow(Ratio, 4.0);
            H_McG[i] = (Den == 0.0) ? Prev : (float)((double)Prev + ((double)C - (double)Prev) / Den);
        }
    }
    const float FanVma = H_FanVma[i];
    const float McG    = H_McG[i];

    // Cloud source indicators
    const int CloudType = In_CloudType.GetIndex();
    float ISource = 0.0f, MinCloud = 0.0f, MidCloud = 0.0f, MaxCloud = 0.0f;

    // RSI(close,14) is shared with the Trampoline / Shark sections
    {
        const int RLen = 14;
        const float Ch = C - PrevC;
        H_Tmp[i] = 0.0f;
        H_Rsi14.Arrays[0][i] = max(Ch, 0.0f);
        H_Rsi14.Arrays[1][i] = -min(Ch, 0.0f);
    }
    // NOTE: RMA needs its own output arrays; reuse H_Rsi14 data slot for value.
    static const int RSI14_LEN = 14;
    RmaStep(H_Rsi14.Arrays[0], H_Rsi14.Arrays[2], i, max(1, In_TrampRsiLen.GetInt()));
    RmaStep(H_Rsi14.Arrays[1], H_Rsi14.Arrays[3], i, max(1, In_TrampRsiLen.GetInt()));
    H_Rsi14[i] = RsiFrom(H_Rsi14.Arrays[2][i], H_Rsi14.Arrays[3][i]);
    const float RsiM = H_Rsi14[i];

    if (CloudType == CLOUD_RSI)
    {
        ISource = RsiM; MinCloud = 20; MidCloud = 50; MaxCloud = 80;
    }
    else if (CloudType == CLOUD_MFI)
    {
        const float Hlc3  = H_Hlc3[i];
        const float PHlc3 = H_Hlc3[i-1];
        H_Mfi.Arrays[0][i] = (Hlc3 > PHlc3) ? sc.Volume[i] * Hlc3 : 0.0f;
        H_Mfi.Arrays[1][i] = (Hlc3 < PHlc3) ? sc.Volume[i] * Hlc3 : 0.0f;
        const float Pos = Sum(H_Mfi.Arrays[0], i, 14);
        const float Neg = Sum(H_Mfi.Arrays[1], i, 14);
        H_Mfi[i] = (Neg == 0.0f) ? 100.0f : 100.0f - (100.0f / (1.0f + Pos / Neg));
        ISource = H_Mfi[i]; MinCloud = 20; MidCloud = 50; MaxCloud = 80;
    }
    else if (CloudType == CLOUD_CCI)
    {
        const float MaCci = SMA(H_Hlc3, i, 20);
        const float Dev   = MeanAbsDev(H_Hlc3, i, 20);
        H_Cci[i] = (Dev == 0.0f) ? 0.0f : (H_Hlc3[i] - MaCci) / (0.015f * Dev);
        ISource = H_Cci[i]; MinCloud = 20; MidCloud = -100; MaxCloud = 100;
    }

    const bool CloudGreen = FanVma > McG;

    // Cloud color + transparency.  Pine interpolates between two copies of the
    // same RGB with different alpha, so only the alpha actually varies.
    COLORREF CloudColor  = CloudGreen ? BigGreen : BigRed;
    float    CloudTransp = (float)In_SimpleCloud.GetInt();
    if (CloudType == CLOUD_RSI || CloudType == CLOUD_MFI || CloudType == CLOUD_CCI)
    {
        const float T = CloudGreen ? GradientT(ISource, MidCloud, MaxCloud)
                                   : GradientT(ISource, MinCloud, MidCloud);
        CloudTransp = (float)In_LowCloud.GetInt()
                    + T * ((float)In_HighCloud.GetInt() - (float)In_LowCloud.GetInt());
    }

    if (CloudType != CLOUD_NONE)
    {
        SG_FanVma[i] = FanVma;
        SG_McG[i]    = McG;

        const int MaxBars = In_CloudBars.GetInt();
        const bool InWindow = (MaxBars <= 0) || (i >= sc.ArraySize - MaxBars);
        if (In_CloudFill.GetYesNo() && InWindow)
        {
            // For DRAWING_RECTANGLEHIGHLIGHT, Color draws the border at full
            // opacity while SecondaryColor is the fill that TransparencyLevel
            // applies to. Drawing both in the same color produced a bright
            // outline around every bar, which is what made the cloud read as a
            // row of boxes. With an outline width of 0 the border is dropped;
            // the border color is also pre-blended to match the fill so the
            // band stays seamless even if Sierra still draws a 1px line.
            const int OutlineW = max(0, In_CloudOutline.GetInt());
            s_UseTool Tool;
            Tool.Clear();
            Tool.ChartNumber   = sc.ChartNumber;
            Tool.DrawingType   = DRAWING_RECTANGLEHIGHLIGHT;
            Tool.LineNumber    = CLOUD_LINE_BASE + i;
            Tool.BeginIndex    = i;
            Tool.EndIndex      = i + 1;
            Tool.BeginValue    = FanVma;
            Tool.EndValue      = McG;
            Tool.Color         = (OutlineW > 0) ? CloudColor
                                                : Transp(CloudColor, Bg, CloudTransp);
            Tool.SecondaryColor= CloudColor;
            Tool.LineWidth     = OutlineW;
            Tool.TransparencyLevel = (int)(CloudTransp + 0.5f);
            Tool.AddMethod     = UTAM_ADD_OR_ADJUST;
            Tool.AddAsUserDrawnDrawing = 0;
            sc.UseTool(Tool);
        }
    }

    // ------------------------------------------------------------------
    //  Waddah Attar Explosion candles
    // ------------------------------------------------------------------
    EmaStep(sc.Close, H_EmaFast, i, max(1, In_WaeFast.GetInt()));
    EmaStep(sc.Close, H_EmaSlow, i, max(1, In_WaeSlow.GetInt()));
    H_Macd[i] = H_EmaFast[i] - H_EmaSlow[i];

    const float T1 = (H_Macd[i] - H_Macd[i-1]) * (float)In_WaeSens.GetInt();
    const int   WaeLen = max(1, In_WaeChan.GetInt());
    const float E1 = 2.0f * In_WaeMult.GetFloat() * StdevPop(sc.Close, i, WaeLen);

    const float TrendUpWae   = (T1 >= 0.0f) ?  T1 : 0.0f;
    const float TrendDownWae = (T1 <  0.0f) ? -T1 : 0.0f;

    COLORREF WaeBody = MakeCol(255,255,255), WaeBorder = MakeCol(255,255,255),
             WaeWick = MakeCol(255,255,255);
    {
        const float TopB = (float)In_TopBody.GetInt();
        const float TopR = (float)In_TopBorder.GetInt();
        if (TrendUpWae > E1 && TrendUpWae > 0.0f)
        {
            WaeBody   = Gradient(fabsf(TrendUpWae - E1), 1, TopB, Transp(BigGreen,Bg,70), Transp(BigGreen,Bg,0));
            WaeBorder = Gradient(fabsf(TrendUpWae - E1), 1, TopR, Transp(BigGreen,Bg,70), Transp(BigGreen,Bg,0));
            WaeWick   = Transp(BigGreen, Bg, 0);
        }
        else if (TrendUpWae < E1 && TrendUpWae > 0.0f)
        {
            WaeBody   = Transp(BigGreen, Bg, 90);
            WaeBorder = Gradient(fabsf(E1 - TrendUpWae), 1, TopB, Transp(BigGreen,Bg,70), Transp(BigGreen,Bg,0));
            WaeWick   = Transp(BigGreen, Bg, 30);
        }
        else if (TrendDownWae > E1 && TrendDownWae > 0.0f)
        {
            WaeBody   = Gradient(fabsf(TrendDownWae - E1), 1, TopB, Transp(BigRed,Bg,50), Transp(BigRed,Bg,0));
            WaeBorder = Gradient(fabsf(TrendDownWae - E1), 1, TopR, Transp(BigRed,Bg,50), Transp(BigRed,Bg,0));
            WaeWick   = Transp(BigRed, Bg, 0);
        }
        else if (TrendDownWae < E1 && TrendDownWae > 0.0f)
        {
            WaeBody   = Transp(BigRed, Bg, 90);
            WaeBorder = Gradient(fabsf(E1 - TrendDownWae), 1, TopB, Transp(BigRed,Bg,50), Transp(BigRed,Bg,0));
            WaeWick   = Transp(BigRed, Bg, 30);
        }
    }

    // ------------------------------------------------------------------
    //  Trampoline                     (+vTramp = 4)
    // ------------------------------------------------------------------
    bool UpThrust = false, DownThrust = false;
    {
        const int   BBLen  = max(1, In_TrampBBLen.GetInt());
        const float BBMult = In_TrampBBMult.GetFloat();
        const float Thr    = In_TrampBBThr.GetFloat();
        const float RsiLo  = (float)In_TrampRsiLo.GetInt();
        const float RsiHi  = (float)In_TrampRsiHi.GetInt();

        bool AnyBack = false, AnyFor = false;
        for (int k = 1; k <= 5 && (i - k) >= 1; ++k)
        {
            const int b = i - k;
            const float Basis = SMA(sc.Close, b, BBLen);
            const float Dev   = BBMult * StdevPop(sc.Close, b, BBLen);
            const float Bbw   = (Basis == 0.0f) ? 0.0f : (2.0f * Dev) / Basis;
            const bool  KRed  = sc.Close[b] < sc.Open[b];
            const bool  KGrn  = sc.Close[b] > sc.Open[b];
            if (KRed && H_Rsi14[b] <= RsiLo && sc.Close[b] < (Basis - Dev) && Bbw > Thr) AnyBack = true;
            if (KGrn && H_Rsi14[b] >= RsiHi && sc.Close[b] > (Basis + Dev) && Bbw > Thr) AnyFor  = true;
        }

        const bool WeGoUp   = IsGreenBar && AnyBack && (H > sc.High[i-1]) && BarConfirmed;
        const bool WeGoDown = IsRedBar   && AnyFor  && (L < sc.Low[i-1])  && BarConfirmed;
        H_Tramp.Arrays[0][i] = WeGoUp   ? 1.0f : 0.0f;
        H_Tramp.Arrays[1][i] = WeGoDown ? 1.0f : 0.0f;

        UpThrust = WeGoUp;  DownThrust = WeGoDown;
        for (int k = 1; k <= 4 && (i-k) >= 0; ++k)
        {
            if (H_Tramp.Arrays[0][i-k] != 0.0f) UpThrust   = false;
            if (H_Tramp.Arrays[1][i-k] != 0.0f) DownThrust = false;
        }
    }
    H_Flags.Arrays[2][i] = (UpThrust || DownThrust) ? 1.0f : 0.0f;
    if (H_Flags.Arrays[2][i] != 0.0f || H_Flags.Arrays[2][i-1] != 0.0f) TpCount += 4;

    // ------------------------------------------------------------------
    //  Squeeze Relaxer                (+vSqueeze = 4)
    // ------------------------------------------------------------------
    bool SqzBuy = false, SqzSell = false;
    COLORREF SqBody = MakeCol(255,255,255), SqBorder = MakeCol(255,255,255),
             SqWick = MakeCol(255,255,255);
    {
        const int   SqLen  = max(1, In_SqzLength.GetInt());
        const float MultKc = In_SqzKcMult.GetFloat();

        // NOTE: the original uses multKC (1.5) for the BB deviation, not multQ.
        const float Basis   = SMA(sc.Close, i, SqLen);
        const float Dev     = MultKc * StdevPop(sc.Close, i, SqLen);
        float RangeMa = 0.0f;
        {
            const int N = Fit(i, SqLen);
            for (int k = 0; k < N; ++k) RangeMa += (sc.High[i-k] - sc.Low[i-k]);
            RangeMa /= (float)N;
        }
        const float UpperKc = Basis + RangeMa * MultKc;
        const float LowerKc = Basis - RangeMa * MultKc;
        const bool  SqzOn   = ((Basis - Dev) > LowerKc) && ((Basis + Dev) < UpperKc);

        const float Avg1 = (Highest(sc.High, i, SqLen) + Lowest(sc.Low, i, SqLen)) * 0.5f;
        const float Avg2 = (Avg1 + Basis) * 0.5f;
        H_SqzSrc[i] = C - Avg2;
        H_SqzVal[i] = LinRegEnd(H_SqzSrc, i, SqLen);

        const float Val = H_SqzVal[i], PVal = H_SqzVal[i-1];
        int  CGreen = (int)H_SqzSt.Arrays[0][i-1];
        int  CRed   = (int)H_SqzSt.Arrays[1][i-1];
        const bool PrevPos = H_SqzSt.Arrays[2][i-1] != 0.0f;
        const bool PrevNeg = H_SqzSt.Arrays[3][i-1] != 0.0f;
        bool Pos = false, Neg = false;
        const int Tol = In_SqzTol.GetInt();

        if (Val < PVal && Val < 5.0f && !SqzOn) CRed++;
        if (Val > PVal && Val > 5.0f && !SqzOn) CGreen++;
        if (Val > PVal && CRed   > Tol && Val < 5.0f && !PrevPos && SigAboveAdx) { CRed = 0;   Pos = true; }
        if (Val < PVal && CGreen > Tol && Val > 5.0f && !PrevNeg && SigAboveAdx) { CGreen = 0; Neg = true; }

        H_SqzSt.Arrays[0][i] = (float)CGreen;
        H_SqzSt.Arrays[1][i] = (float)CRed;
        H_SqzSt.Arrays[2][i] = Pos ? 1.0f : 0.0f;
        H_SqzSt.Arrays[3][i] = Neg ? 1.0f : 0.0f;

        SqzBuy  = Pos && BarConfirmed;
        SqzSell = Neg && BarConfirmed;

        if (Val > 0.0f)
        {
            if (Val > PVal)
            {
                SqBody   = Gradient(fabsf(Val), 0, 30, Transp(BigGreen,Bg,50), Transp(BigGreen,Bg,0));
                SqBorder = BigGreen;  SqWick = SqBody;
            }
            else if (Val < PVal)
            {
                SqBody = Transp(BigGreen, Bg, 70);  SqBorder = Bg;  SqWick = SqBody;
            }
        }
        else
        {
            if (Val < PVal)
            {
                SqBody   = Gradient(fabsf(Val), 0, 30, Transp(BigRed,Bg,50), Transp(BigRed,Bg,0));
                SqBorder = BigRed;  SqWick = SqBody;
            }
            else if (Val > PVal)
            {
                SqBody = Transp(BigRed, Bg, 50);  SqBorder = Bg;  SqWick = SqBody;
            }
        }
    }
    H_Flags.Arrays[3][i] = (SqzBuy || SqzSell) ? 1.0f : 0.0f;
    if (H_Flags.Arrays[3][i] != 0.0f || H_Flags.Arrays[3][i-1] != 0.0f) TpCount += 4;

    // ------------------------------------------------------------------
    //  PVSRA "Vector" candles
    //  Reimplementation of TradersReality/Traders_Reality_Lib calcPvsra():
    //    climax  : volume >= 2.0 * avg volume of the 10 PREVIOUS bars, OR
    //              volume*spread >= highest volume*spread of the 10 previous bars
    //    rising  : volume >= 1.5 * avg volume of the 10 previous bars
    // ------------------------------------------------------------------
    COLORREF PvsraColor;
    bool VectorGreen = false, VectorRed = false;
    {
        H_Pvsra.Arrays[0][i] = sc.Volume[i] * (H - L);
        const float AvgVol   = (i >= 1) ? SMA(sc.Volume, i-1, 10) : sc.Volume[i];
        const float HiVolSpr = (i >= 1) ? Highest(H_Pvsra.Arrays[0], i-1, 10) : 0.0f;

        const bool Climax = (sc.Volume[i] >= AvgVol * 2.0f)
                         || (H_Pvsra.Arrays[0][i] >= HiVolSpr);
        const bool Rising = (sc.Volume[i] >= AvgVol * 1.5f);

        if      (Climax && IsGreenBar) { PvsraColor = BigGreen;              VectorGreen = true; }
        else if (Climax && IsRedBar)   { PvsraColor = BigRed;                VectorRed   = true; }
        else if (Rising && IsGreenBar) { PvsraColor = In_VecBlue.GetColor(); }
        else if (Rising && IsRedBar)   { PvsraColor = In_VecViolet.GetColor(); }
        else if (IsGreenBar)           { PvsraColor = In_VecRegUp.GetColor(); }
        else                           { PvsraColor = In_VecRegDown.GetColor(); }

        H_Pvsra.Arrays[1][i] = VectorGreen ? 1.0f : 0.0f;
        H_Pvsra.Arrays[2][i] = VectorRed   ? 1.0f : 0.0f;
    }

    // ------------------------------------------------------------------
    //  Total Recall                   (+vEarlyRev = 2)
    // ------------------------------------------------------------------
    {
        bool Green = false, Red = false;
        const float UpClose = H_Fract.Arrays[2][i];
        const float DnClose = H_Fract.Arrays[3][i];
        for (int k = 0; k <= 3 && (i-k) >= 0; ++k)
        {
            if (VectorGreen && sc.Close[i-k] == H_Fract.Arrays[2][i-k]) Green = true;
            if (VectorRed   && sc.Close[i-k] == H_Fract.Arrays[3][i-k]) Red   = true;
        }
        H_Flags.Arrays[4][i] = (Green || Red) ? 1.0f : 0.0f;
    }
    if (H_Flags.Arrays[4][i] != 0.0f || H_Flags.Arrays[4][i-1] != 0.0f) TpCount += 2;

    // ------------------------------------------------------------------
    //  The Shark                      (+vShark = 2)
    // ------------------------------------------------------------------
    {
        const float Basis5 = SMA(H_Rsi14, i, 30);
        const float Dev5   = 2.0f * StdevPop(H_Rsi14, i, 30);
        bool Below25 = true, Above75 = true;
        if (In_Apply2575.GetYesNo()) { Below25 = RsiM < 26.0f; Above75 = RsiM > 74.0f; }

        const bool SharkUp   = (RsiM < (Basis5 - Dev5)) && Below25 && BarConfirmed;
        const bool SharkDown = (RsiM > (Basis5 + Dev5)) && Above75 && BarConfirmed;
        H_Flags.Arrays[5][i] = (SharkUp || SharkDown) ? 1.0f : 0.0f;
    }
    if (H_Flags.Arrays[5][i] != 0.0f || H_Flags.Arrays[5][i-1] != 0.0f) TpCount += 2;

    // ------------------------------------------------------------------
    //  John Wick / wicking Bollinger bands   (+vBands = 2)
    // ------------------------------------------------------------------
    {
        const int   WLen  = max(1, In_WickBBLen.GetInt());
        const float WBase = SMA(sc.Close, i, WLen);
        const float WDev  = In_WickBBMult.GetFloat() * StdevPop(sc.Close, i, WLen);
        const bool  BBUp   = (L <= (WBase - WDev)) && (C >= (WBase - WDev)) && IsRedBar;
        const bool  BBDown = (H >= (WBase + WDev)) && (C <  (WBase + WDev)) && IsGreenBar;
        H_Flags.Arrays[6][i] = (BBUp || BBDown) ? 1.0f : 0.0f;
    }
    if (H_Flags.Arrays[6][i] != 0.0f || H_Flags.Arrays[6][i-1] != 0.0f) TpCount += 2;

    // ------------------------------------------------------------------
    //  Ultimate Buy / Sell
    // ------------------------------------------------------------------
    bool PlotBuy = false, PlotSell = false;
    {
        const int RsiLen = max(1, In_RsiLength.GetInt());
        const float Ch = C - PrevC;
        H_Rsi32.Arrays[0][i] = max(Ch, 0.0f);
        H_Rsi32.Arrays[1][i] = -min(Ch, 0.0f);
        RmaStep(H_Rsi32.Arrays[0], H_Rsi32.Arrays[2], i, RsiLen);
        RmaStep(H_Rsi32.Arrays[1], H_Rsi32.Arrays[3], i, RsiLen);
        H_Rsi32[i] = RsiFrom(H_Rsi32.Arrays[2][i], H_Rsi32.Arrays[3][i]);

        const int   BasisLen = max(1, In_RsiBasisLen.GetInt());
        const float Basis    = WMA(H_Rsi32, i, BasisLen);      // Pine fixes this to WMA
        const float Dev      = StdevPop(H_Rsi32, i, BasisLen);
        H_RsiB[i]           = Basis;
        H_RsiB.Arrays[0][i] = Basis + In_RsiMult.GetFloat() * Dev;
        H_RsiB.Arrays[1][i] = Basis - In_RsiMult.GetFloat() * Dev;

        const int MaLen = max(1, In_RsiMaLength.GetInt());
        switch (In_RsiMaType.GetIndex())
        {
            case MA_SMA:  H_RsiB.Arrays[2][i] = SMA(H_Rsi32, i, MaLen); break;
            case MA_EMA:  EmaStep(H_Rsi32, H_RsiB.Arrays[2], i, MaLen); break;
            case MA_VWMA: H_RsiB.Arrays[2][i] = VWMA(H_Rsi32, sc.Volume, i, MaLen); break;
            case MA_RMA:  RmaStep(H_Rsi32, H_RsiB.Arrays[2], i, MaLen); break;
            default:      H_RsiB.Arrays[2][i] = WMA(H_Rsi32, i, MaLen); break;  // WMA / HMA
        }

        const int   PLen  = max(1, In_PriceBasis.GetInt());
        const float PB    = SMA(sc.Close, i, PLen);
        const float PS    = In_PriceInner.GetFloat() * StdevPop(sc.Close, i, PLen);
        const float PPB   = SMA(sc.Close, i-1, PLen);
        const float PPS   = In_PriceInner.GetFloat() * StdevPop(sc.Close, i-1, PLen);

        // ATR + ATR MA
        H_Atr.Arrays[0][i] = max(H - L, max(fabsf(H - PrevC), fabsf(L - PrevC)));
        RmaStep(H_Atr.Arrays[0], H_Atr, i, max(1, In_AtrPeriod.GetInt()));
        const int AtrMaLen = max(1, In_AtrMaPeriod.GetInt());
        switch (In_AtrMaType.GetIndex())
        {
            case MA_SMA:  H_AtrMa[i] = SMA(sc.Close, i, AtrMaLen); break;
            case MA_EMA:  EmaStep(sc.Close, H_AtrMa, i, AtrMaLen); break;
            case MA_VWMA: H_AtrMa[i] = VWMA(sc.Close, sc.Volume, i, AtrMaLen); break;
            case MA_RMA:  RmaStep(sc.Close, H_AtrMa, i, AtrMaLen); break;
            default:      H_AtrMa[i] = WMA(sc.Close, i, AtrMaLen); break;
        }
        const float AM = In_AtrMult.GetFloat();
        const float UpAtr  = H_AtrMa[i]   + H_Atr[i]   * AM;
        const float LoAtr  = H_AtrMa[i]   - H_Atr[i]   * AM;
        const float PUpAtr = H_AtrMa[i-1] + H_Atr[i-1] * AM;
        const float PLoAtr = H_AtrMa[i-1] - H_Atr[i-1] * AM;

        const float R  = H_Rsi32[i], PR = H_Rsi32[i-1];

        const bool PxOverInner  = CrossOver (C, PrevC, PB - PS, PPB - PPS);
        const bool PxUnderInner = CrossUnder(C, PrevC, PB + PS, PPB + PPS);
        const bool RsiOverLow   = CrossOver (R, PR, H_RsiB.Arrays[1][i], H_RsiB.Arrays[1][i-1]);
        const bool RsiUnderUp   = CrossUnder(R, PR, H_RsiB.Arrays[0][i], H_RsiB.Arrays[0][i-1]);
        const bool RsiOverBasis = CrossOver (R, PR, H_RsiB[i], H_RsiB[i-1]);
        const bool RsiUnderBasis= CrossUnder(R, PR, H_RsiB[i], H_RsiB[i-1]);
        const bool RsiOverMa    = CrossOver (R, PR, H_RsiB.Arrays[2][i], H_RsiB.Arrays[2][i-1]);
        const bool RsiUnderMa   = CrossUnder(R, PR, H_RsiB.Arrays[2][i], H_RsiB.Arrays[2][i-1]);
        const bool RsiUnder75   = CrossUnder(R, PR, 75.0f, 75.0f);
        const bool RsiOver25    = CrossOver (R, PR, 25.0f, 25.0f);
        const bool HighUnderAtr = CrossUnder(H, sc.High[i-1], LoAtr, PLoAtr);
        const bool LowOverAtr   = CrossOver (L, sc.Low[i-1],  UpAtr, PUpAtr);

        bool Blocked = false;
        if (In_UseSigWait.GetYesNo())
        {
            const int LastSig = (int)H_Watch.Arrays[5][i-1];
            if (LastSig > 0 && (i - LastSig) < In_SigWaitBars.GetInt()) Blocked = true;
        }

        const bool UPW = In_UsePriceW.GetYesNo()   != 0;
        const bool URW = In_UseRsiWatch.GetYesNo() != 0;
        const bool UAW = In_UseAtrWatch.GetYesNo() != 0;

        const bool BuyWatched  = ((UPW && PxOverInner)
                               || (URW && (RsiOverLow || RsiOver25))
                               || (UAW && HighUnderAtr)) && BarConfirmed && !Blocked;
        const bool SellWatched = ((UPW && PxUnderInner)
                               || (URW && (RsiUnderUp || RsiUnder75))
                               || (UAW && LowOverAtr))   && BarConfirmed && !Blocked;

        H_Watch.Arrays[0][i] = BuyWatched  ? 1.0f : 0.0f;
        H_Watch.Arrays[1][i] = SellWatched ? 1.0f : 0.0f;

        const int Look      = max(1, In_WatchLook.GetInt());
        const int LastClear = (int)H_Watch.Arrays[2][i-1];
        const int Start     = max(max(0, i - Look + 1), LastClear + 1);
        bool BuyMet = false, SellMet = false;
        for (int b = Start; b <= i; ++b)
        {
            if (H_Watch.Arrays[0][b] != 0.0f) BuyMet  = true;
            if (H_Watch.Arrays[1][b] != 0.0f) SellMet = true;
        }

        // NOTE: the original never applies these four toggles. They are honoured
        // here; all default to Yes so behaviour is unchanged.
        const bool CombBuy  = (In_UseRsiBasis.GetYesNo() && RsiOverBasis)
                           || (In_Use25.GetYesNo()       && RsiOver25)
                           || (In_UseRsiMa.GetYesNo()    && RsiOverMa);
        const bool CombSell = (In_UseRsiBasis.GetYesNo() && RsiUnderBasis)
                           || (In_Use75.GetYesNo()       && RsiUnder75)
                           || (In_UseRsiMa.GetYesNo()    && RsiUnderMa);

        const bool Req = In_RequireWatch.GetYesNo() != 0;
        const bool BuySig  = Req ? (BuyMet  && CombBuy)  : CombBuy;
        const bool SellSig = Req ? (SellMet && CombSell) : CombSell;

        int NewClear = LastClear;
        int NewSig   = (int)H_Watch.Arrays[5][i-1];

        if (BuySig && BarConfirmed && !BuyWatched && !Blocked)
        { PlotBuy = true;  NewSig = i;  NewClear = i; }
        else if (SellSig && BarConfirmed && !SellWatched && !Blocked)
        { PlotSell = true; NewSig = i;  NewClear = i; }

        H_Watch.Arrays[2][i] = (float)NewClear;
        H_Watch.Arrays[3][i] = PlotBuy  ? 1.0f : 0.0f;
        H_Watch.Arrays[4][i] = PlotSell ? 1.0f : 0.0f;
        H_Watch.Arrays[5][i] = (float)NewSig;
    }

    bool BigBuy = false, BigSell = false;
    for (int k = 0; k <= 3 && (i-k) >= 0; ++k)
    {
        if (H_Watch.Arrays[3][i-k] != 0.0f) BigBuy  = true;
        if (H_Watch.Arrays[4][i-k] != 0.0f) BigSell = true;
    }

    // ------------------------------------------------------------------
    //  Vodka Shot  =  Neglected Volume (DGT) + VADER (RedK) + Lazy Line
    // ------------------------------------------------------------------
    bool BuyVodka = false, SellVodka = false;
    {
        const float NzVol  = sc.Volume[i];
        const float VolAvgL= SMA(sc.Volume, i, 70);
        const float VolDev = (VolAvgL == 0.0f) ? 0.0f
                           : (VolAvgL + 1.618034f * StdevPop(sc.Volume, i, 70)) / VolAvgL * 11.0f / 100.0f;
        const float VolRel = (VolAvgL == 0.0f) ? 0.0f : NzVol / VolAvgL;

        // vola
        float Vola = 1.0f;
        const int VCalc = In_VCalc.GetIndex();       // 0 Relative, 1 Full, 2 None
        if (VCalc == 0)
        {
            const int   Lb = max(1, In_VLookback.GetInt());
            const float Hi = Highest(sc.Volume, i, Lb);
            const float Lo = Lowest (sc.Volume, i, Lb);
            Vola = (Hi == Lo) ? 0.0f : (NzVol - Lo) / (Hi - Lo);
        }
        else if (VCalc == 1) Vola = NzVol;
        H_Tmp[i] = Vola;

        const float R2 = (Highest(sc.High, i, 2) - Lowest(sc.Low, i, 2)) * 0.5f;
        float Cval;
        if (R2 == 0.0f) Cval = H_Vola[i-1];          // fixnan()
        else
        {
            float Sr = (C - PrevC) / R2;
            if (Sr >  1.0f) Sr =  1.0f;
            if (Sr < -1.0f) Sr = -1.0f;
            Cval = Sr * Vola;
        }
        H_Vola[i]           = Cval;
        H_Vola.Arrays[0][i] = max(Cval, 0.0f);
        H_Vola.Arrays[1][i] = -min(Cval, 0.0f);

        const int VLen  = max(1, In_VaderLen.GetInt());
        const int DerMa = In_DerMaType.GetIndex();   // 0 WMA, 1 EMA, 2 SMA
        if (DerMa == 1)
        {
            EmaStep(H_Tmp,            H_AvgVola, i, VLen);
            EmaStep(H_Vola.Arrays[0], H_Dem,     i, VLen);
            EmaStep(H_Vola.Arrays[1], H_Sup,     i, VLen);
        }
        else if (DerMa == 2)
        {
            H_AvgVola[i] = SMA(H_Tmp,            i, VLen);
            H_Dem[i]     = SMA(H_Vola.Arrays[0], i, VLen);
            H_Sup[i]     = SMA(H_Vola.Arrays[1], i, VLen);
        }
        else
        {
            H_AvgVola[i] = WMA(H_Tmp,            i, VLen);
            H_Dem[i]     = WMA(H_Vola.Arrays[0], i, VLen);
            H_Sup[i]     = WMA(H_Vola.Arrays[1], i, VLen);
        }
        const float Av = H_AvgVola[i];
        H_Dem[i] = (Av == 0.0f) ? 0.0f : H_Dem[i] / Av;
        H_Sup[i] = (Av == 0.0f) ? 0.0f : H_Sup[i] / Av;

        const int DerAvg = max(1, In_DerAvg.GetInt());
        H_Adp[i]           = 100.0f * WMA(H_Dem, i, DerAvg);
        H_Adp.Arrays[0][i] = 100.0f * WMA(H_Sup, i, DerAvg);
        H_Adp.Arrays[1][i] = H_Adp[i] - H_Adp.Arrays[0][i];
        H_Adp.Arrays[2][i] = WMA(H_Adp.Arrays[1], i, max(1, In_RSmooth.GetInt()));

        const float Bo = H_Adp.Arrays[0][i];         // asp
        const float Bc = H_Adp[i];                   // adp
        const bool  Rising  = (Bc - H_Adp[i-1]) > 0.0f;
        const bool  BarUp   = (Bc > Bo) &&  Rising;
        const bool  BarDown = (Bc < Bo) && !Rising;

        // Lazy Line (RedK RSS_WMA)
        {
            const int Len = max(1, In_LazyLen.GetInt());
            if (Len > 2)
            {
                const float Wf = (float)Len / 3.0f;
                const int   w2 = (int)(Wf + 0.5f);
                const int   w1 = (int)(((float)(Len - w2) / 2.0f) + 0.5f);
                const int   w3 = (int)((float)(Len - w2) / 2.0f);
                H_LL1[i] = WMA(sc.Close, i, max(1, w1));
                H_LL2[i] = WMA(H_LL1,    i, max(1, w2));
                H_LL3[i] = WMA(H_LL2,    i, max(1, w3));
            }
            else H_LL3[i] = C;
        }
        const bool LlUp = H_LL3[i] > H_LL3[i-1];
        const bool LlDn = H_LL3[i] < H_LL3[i-1];

        // NOTE: the original ANDs in the raw float `up` (an RMA of RSI gains),
        // which is non-zero on virtually every bar, so it is effectively a
        // no-op filter. Reproduced as "!= 0".
        const bool UpRmaNonZero = H_Rsi32.Arrays[2][i] != 0.0f;

        const bool Upwards   = LlUp && BarUp   && UpRmaNonZero
                            && (O < C) && (VolRel * 0.145898f > VolDev);
        const bool Downwards = LlDn && BarDown
                            && (O > C) && (VolRel * 0.145898f > VolDev);

        H_Vodka.Arrays[0][i] = Upwards   ? 1.0f : 0.0f;
        H_Vodka.Arrays[1][i] = Downwards ? 1.0f : 0.0f;

        BuyVodka = Upwards;
        for (int k = 1; k <= 4 && (i-k) >= 0; ++k)
            if (H_Vodka.Arrays[0][i-k] != 0.0f) BuyVodka = false;

        // Original bug preserved: pSellVodka checks downwards[1] then upwards[2..4]
        SellVodka = Downwards;
        if ((i-1) >= 0 && H_Vodka.Arrays[1][i-1] != 0.0f) SellVodka = false;
        for (int k = 2; k <= 4 && (i-k) >= 0; ++k)
            if (H_Vodka.Arrays[0][i-k] != 0.0f) SellVodka = false;
    }

    // ------------------------------------------------------------------
    //  Rational Quadratic Kernel  +  HEMA  +  9/21 cross
    // ------------------------------------------------------------------
    float Yhat = C;
    {
        // Pine's `size` evaluates to 1, so the loop runs i = 0 .. 1 + x_0
        const int   N  = 1 + In_RqkX0.GetInt();
        const float Hk = In_RqkH.GetFloat();
        const float Rk = In_RqkR.GetFloat();
        double CurW = 0.0, CumW = 0.0;
        for (int k = 0; k <= N && (i-k) >= 0; ++k)
        {
            const double Wt = pow(1.0 + ((double)k*k) / ((double)Hk*Hk*2.0*Rk), -(double)Rk);
            CurW += (double)sc.Close[i-k] * Wt;
            CumW += Wt;
        }
        Yhat = (CumW == 0.0) ? C : (float)(CurW / CumW);
    }

    {
        const float Alpha = 2.0f / (float)(max(1, In_AlphaLength.GetInt()) + 1);
        const float Gamma = 2.0f / (float)(max(1, In_GammaLength.GetInt()) + 1);
        const float PrevHema = H_Hema[i-1];
        const float PrevBH   = (i >= 2) ? H_Hema.Arrays[0][i-1] : C;
        H_Hema[i]           = (1.0f - Alpha) * (PrevHema + PrevBH) + Alpha * C;
        H_Hema.Arrays[0][i] = (1.0f - Gamma) * H_Hema.Arrays[0][i-1]
                            + Gamma * (H_Hema[i] - PrevHema);
    }

    EmaStep(sc.Close, H_Ema9,  i, 9);
    EmaStep(sc.Close, H_Ema21, i, 21);
    bool CrossUp = false, CrossDn = false;
    if (In_Show921.GetYesNo())
    {
        if (In_ShowQuad.GetYesNo())
        {
            CrossUp = CrossOver (H_Ema9[i], H_Ema9[i-1], Yhat, SG_Rqk[i-1]);
            CrossDn = CrossUnder(H_Ema9[i], H_Ema9[i-1], Yhat, SG_Rqk[i-1]);
        }
        else
        {
            CrossUp = CrossOver (H_Ema9[i], H_Ema9[i-1], H_Ema21[i], H_Ema21[i-1]);
            CrossDn = CrossUnder(H_Ema9[i], H_Ema9[i-1], H_Ema21[i], H_Ema21[i-1]);
        }
    }
    SG_Rqk[i] = Yhat;   // always stored so the quad cross has its previous value

    // ------------------------------------------------------------------
    //  Tidal Wave (Aaron D)
    // ------------------------------------------------------------------
    int  WaveState     = (int)H_Wave.Arrays[0][i-1];
    const int PrevWave = WaveState;
    float BrightGreen = 0.0f, BrightRed = 0.0f;
    bool  NoOverlapGreen = false, NoOverlapRed = false;
    bool  GapGreen = false, GapRed = false;

    {
        const float BodyWidth = fabsf(C - O);
        const bool  Doji = In_IgnoreDoji.GetYesNo()
                        && (BodyWidth <= (float)In_MaxBody.GetInt() * sc.TickSize);

        if (IsGreenBar && !Doji && BarConfirmed)
        {
            for (int k = 1; k <= 200 && (i-k) >= 0; ++k)
            {
                const int b = i - k;
                if (H_Wave.Arrays[2][b] != 0.0f) break;                       // brightRed
                else if (WaveState == WAVE_UP && sc.Close[b] < sc.Open[b]) break;
                else if (WaveState == WAVE_DOWN && O >= sc.Close[b] && sc.Close[b] > sc.Open[b])
                { NoOverlapGreen = true; BrightGreen = 1.0f; WaveState = WAVE_UP; break; }
            }
            if (O >= sc.Close[i-1] && sc.Close[i-1] > sc.Open[i-1])
            { WaveState = WAVE_UP; BrightGreen = 1.0f; GapGreen = true; }
        }

        if (IsRedBar && !Doji && BarConfirmed)
        {
            for (int k = 1; k <= 200 && (i-k) >= 0; ++k)
            {
                const int b = i - k;
                if (H_Wave.Arrays[1][b] != 0.0f) break;                       // brightGreen
                else if (WaveState == WAVE_DOWN && sc.Close[b] > sc.Open[b]) break;
                else if (WaveState == WAVE_UP && O <= sc.Close[b] && sc.Close[b] < sc.Open[b])
                { NoOverlapRed = true; BrightRed = 1.0f; WaveState = WAVE_DOWN; break; }
            }
            if (sc.Close[i-1] < sc.Open[i-1] && O < sc.Close[i-1])
            { WaveState = WAVE_DOWN; BrightRed = 1.0f; GapRed = true; }
        }
    }

    H_Wave.Arrays[0][i] = (float)WaveState;
    H_Wave.Arrays[1][i] = BrightGreen;
    H_Wave.Arrays[2][i] = BrightRed;
    H_Wave.Arrays[3][i] = GapGreen ? 1.0f : 0.0f;
    H_Wave.Arrays[4][i] = GapRed   ? 1.0f : 0.0f;

    const bool BuyChar  = NoOverlapGreen && (PrevWave == WAVE_DOWN);
    const bool SellChar = NoOverlapRed   && (PrevWave == WAVE_UP);

    // ------------------------------------------------------------------
    //  Volume imbalance lines
    // ------------------------------------------------------------------
    if (In_TrackBar.GetYesNo())
    {
        const int Extend = max(1, In_BarExtend.GetInt());
        SubgraphLineStyles Style = LINESTYLE_DOT;
        if (In_LineStyle.GetIndex() == 0) Style = LINESTYLE_SOLID;
        else if (In_LineStyle.GetIndex() == 2) Style = LINESTYLE_DASH;

        if (GapGreen || GapRed)
        {
            H_Imb.Arrays[0][i] = O;
            H_Imb.Arrays[1][i] = 0.0f;                 // not yet broken
            H_Imb.Arrays[2][i] = GapGreen ? 1.0f : 0.0f;
        }

        for (int j = i; j >= 0 && j > i - Extend; --j)
        {
            if (H_Imb.Arrays[0][j] == 0.0f) continue;
            const float Level = H_Imb.Arrays[0][j];
            if (H_Imb.Arrays[1][j] == 0.0f && j < i && H > Level && L < Level)
                H_Imb.Arrays[1][j] = (float)i;         // Pine deletes it here

            const int Broken = (int)H_Imb.Arrays[1][j];
            if (Broken != 0) continue;                 // deleted line: draw nothing

            s_UseTool Tool;
            Tool.Clear();
            Tool.ChartNumber = sc.ChartNumber;
            Tool.DrawingType = DRAWING_LINE;
            Tool.LineNumber  = IMB_LINE_BASE + j;
            Tool.BeginIndex  = j;
            Tool.EndIndex    = j + Extend;
            Tool.BeginValue  = Level;
            Tool.EndValue    = Level;
            Tool.Color       = Transp(H_Imb.Arrays[2][j] != 0.0f ? MakeCol(0,255,0)
                                                                 : MakeCol(255,0,0), Bg, 50);
            Tool.LineWidth   = max(1, In_LineWidth.GetInt());
            Tool.LineStyle   = Style;
            Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
            Tool.AddAsUserDrawnDrawing = 0;
            sc.UseTool(Tool);
        }
    }

    // ------------------------------------------------------------------
    //  Output
    // ------------------------------------------------------------------
    const float Off = sc.TickSize * (float)max(0, In_MarkerTicks.GetInt());

    const int CandleType = In_CandleType.GetIndex();
    if (CandleType != CANDLE_NONE)
    {
        COLORREF Body, Border, Wick;
        switch (CandleType)
        {
            case CANDLE_VECTOR:  Body = Border = Wick = PvsraColor; break;
            case CANDLE_CVD:     Body = Border = Wick = CvdColor;   break;
            case CANDLE_SQUEEZE: Body = SqBody;  Border = SqBorder;  Wick = SqWick;  break;
            default:             Body = WaeBody; Border = WaeBorder; Wick = WaeWick; break;
        }
        if (In_ColorBar.GetYesNo())
        {
            SG_Bar[i]           = 1.0f;
            SG_Bar.DataColor[i] = SafeCol((Border == Wick) ? Wick : Border);
        }
        if (In_ColorBody.GetYesNo())
        {
            SG_Body[i]           = 1.0f;
            SG_Body.DataColor[i] = SafeCol(Body);
        }
    }

    SG_BuyDot[i]     = (!BigBuy  && BuyChar)  ? (L - Off) : 0.0f;
    SG_SellDot[i]    = (!BigSell && SellChar) ? (H + Off) : 0.0f;
    SG_StrongBuy[i]  = ( BigBuy  && BuyChar)  ? (L - Off) : 0.0f;
    SG_StrongSell[i] = ( BigSell && SellChar) ? (H + Off) : 0.0f;

    if (In_ShowPlus.GetYesNo())
    {
        SG_PlusUp[i]   = (GapGreen && PrevWave == WAVE_UP)   ? (L - Off*2.0f) : 0.0f;
        SG_PlusDown[i] = (GapRed   && PrevWave == WAVE_DOWN) ? (H + Off*2.0f) : 0.0f;
    }
    if (In_ShowBigPlus.GetYesNo())
    {
        SG_VodkaUp[i]   = (BuyVodka  && PrevWave == WAVE_UP)   ? (L - Off*3.0f) : 0.0f;
        SG_VodkaDown[i] = (SellVodka && PrevWave == WAVE_DOWN) ? (H + Off*3.0f) : 0.0f;
    }

    // NOTE: in the original the plot titles are swapped relative to the input
    // names - iMaxProfit drives the plot titled "Take Partial Profit" and
    // iProfit drives "Take Full Profit". The behaviour is preserved as written.
    if (In_ShowProfit.GetYesNo())
    {
        if (TpCount >= In_MaxProfit.GetInt())
        {
            if (WaveState == WAVE_UP)        SG_TpAll[i] = H + Off*4.0f;
            else if (WaveState == WAVE_DOWN) SG_TpAll[i] = L - Off*4.0f;
        }
        if (TpCount >= In_Profit.GetInt())
        {
            if (WaveState == WAVE_UP)        SG_TpPartial[i] = H + Off*5.0f;
            else if (WaveState == WAVE_DOWN) SG_TpPartial[i] = L - Off*5.0f;
        }
    }

    SG_CrossUp[i]   = CrossUp ? (L - Off) : 0.0f;
    SG_CrossDown[i] = CrossDn ? (H + Off) : 0.0f;

    if (In_ShowHEMA.GetYesNo())
    {
        SG_Hema[i] = H_Hema[i];
        SG_Hema.DataColor[i] = SafeCol(H_Hema[i] > H_Hema[i-1]
                                       ? Transp(BigGreen, Bg, 40) : Transp(BigRed, Bg, 40));
    }
    if (!In_ShowRqkLine.GetYesNo()) SG_Rqk[i] = 0.0f;   // keep the value hidden

    if (In_ShowUlt.GetYesNo())
    {
        SG_UltBuy[i]  = PlotBuy  ? (L - Off*6.0f) : 0.0f;
        SG_UltSell[i] = PlotSell ? (H + Off*6.0f) : 0.0f;
    }
    if (In_ShowTramp.GetYesNo())
    {
        SG_TrampUp[i]   = UpThrust   ? ((H+L)*0.5f) : 0.0f;
        SG_TrampDown[i] = DownThrust ? ((H+L)*0.5f) : 0.0f;
    }
    if (In_ShowSqz.GetYesNo())
    {
        SG_SqzBuy[i]  = SqzBuy  ? (L - Off*7.0f) : 0.0f;
        SG_SqzSell[i] = SqzSell ? (H + Off*7.0f) : 0.0f;
    }

    // ------------------------------------------------------------------
    //  Alerts
    // ------------------------------------------------------------------
    if (i == sc.ArraySize - 1)
    {
        if (PlotBuy || PlotSell)          sc.SetAlert(0, "Nebula: Ultimate Buy/Sell Signal");
        if (!BigBuy  && BuyChar)          sc.SetAlert(1, "Nebula: Buy Signal Basic");
        if (!BigSell && SellChar)         sc.SetAlert(2, "Nebula: Sell Signal Basic");
        if ( BigBuy  && BuyChar)          sc.SetAlert(3, "Nebula: Buy Signal Super");
        if ( BigSell && SellChar)         sc.SetAlert(4, "Nebula: Sell Signal Super");
        if ((GapGreen && PrevWave == WAVE_UP) || (GapRed && PrevWave == WAVE_DOWN))
                                          sc.SetAlert(5, "Nebula: Volume Imbalance");
        if ((BuyVodka && PrevWave == WAVE_UP) || (SellVodka && PrevWave == WAVE_DOWN))
                                          sc.SetAlert(6, "Nebula: Vodka Shot");
        if (TpCount >= In_MaxProfit.GetInt()) sc.SetAlert(7, "Nebula: Take Partial Profit");
        if (TpCount >= In_Profit.GetInt())    sc.SetAlert(8, "Nebula: Take FULL Profit");
        if (CrossUp || CrossDn)               sc.SetAlert(9, "Nebula: 9/21 EMA Cross");
    }
}
