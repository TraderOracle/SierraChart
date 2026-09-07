/*=============================================================================
    RLC Framework  -  Regime / Location / Confirmation
    ---------------------------------------------------------------------------
    A Sierra Chart ACSIL study that encodes a four-stage discretionary
    order-flow process into a single indicator:

        1. ENVIRONMENT   Market structure (HH/HL vs LH/LL) + gamma (GEX) regime
        2. LOCATION      Session volume profile (POC / VAH / VAL) + deep
                         Fibonacci retracement "golden pocket" (.705/.786/.886)
                         that must sit OUTSIDE the value area
        3. CONFIRMATION  Bid/ask footprint order flow: absorption (aggressive
                         participation that fails) then a dominance shift
                         (delta flip + stacked 400% diagonal imbalances)
        4. MANAGEMENT    Entry marker, invalidation stop, 1.5R / 2R targets,
                         POC / swing reference, and a trailing stop

    This study DOES NOT place orders. It marks, measures and alerts.

    Requirements:
        - Intraday chart with Volume at Price (bid/ask) data
        - Data feed that supplies bid/ask volume (most futures feeds do)

    Build:  Analysis >> Build Custom Studies DLL  (or Remote Build)
    ---------------------------------------------------------------------------
    If your Sierra Chart build does not have
    AddAndManageSingleTextDrawingForStudy(), set RLC_ENABLE_DASHBOARD to 0.
    Everything else in the study is unaffected; the same information is always
    available in the Values Window.
=============================================================================*/

#include "sierrachart.h"
#include <vector>
#include <map>
#include <algorithm>

SCDLLName("Champion_Orderflow_Strat")

#define RLC_ENABLE_DASHBOARD 1

/*============================================================================*/
/*  Types and helpers                                                          */
/*============================================================================*/
namespace RLC
{
    const int STAGE_IDLE   = 0;   // nothing to do
    const int STAGE_ARMED  = 1;   // environment + location valid, price in zone
    const int STAGE_ABSORB = 2;   // failed aggressive push recorded
    const int STAGE_TRADE  = 3;   // triggered, managing

    const int STRUCT_DOWN  = -1;
    const int STRUCT_FLAT  =  0;
    const int STRUCT_UP    =  1;

    const int GAMMA_NEG    = -1;
    const int GAMMA_UNK    =  0;
    const int GAMMA_POS    =  1;

    // one price row of a footprint bar
    struct s_Lvl
    {
        int    Tick;
        double Bid;
        double Ask;
    };

    static bool LvlLess(const s_Lvl& A, const s_Lvl& B) { return A.Tick < B.Tick; }

    // order flow summary of a single bar
    struct s_OF
    {
        double Delta;
        double Volume;
        int    BuyImb;
        int    SellImb;
        int    StackBuy;
        int    StackSell;

        void Clear()
        {
            Delta = 0.0; Volume = 0.0;
            BuyImb = 0; SellImb = 0; StackBuy = 0; StackSell = 0;
        }
    };

    // everything the study has to remember between calls
    struct s_State
    {
        // ---- session volume profile (closed bars only) -------------------
        std::map<int, double> SessionVol;   // price in ticks -> volume
        int   TradingDayDate;
        int   LastProfileBar;

        float POC, VAH, VAL;
        int   ProfileValid;
        float PriorPOC, PriorVAH, PriorVAL;
        int   PriorValid;

        // ---- evaluation bookkeeping --------------------------------------
        int   LastEvalBar;

        // ---- environment (cached, computed on each closed bar) -----------
        int   Structure;
        int   Gamma;
        float LastSwingHigh;
        float LastSwingLow;

        // ---- active retracement legs / zones ------------------------------
        int   LongZoneValid;
        float LongLegLow, LongLegHigh;
        float LongFib1, LongFib2, LongFib3;   // .705 / .786 / .886 prices
        float LongZoneTop, LongZoneBot;

        int   ShortZoneValid;
        float ShortLegLow, ShortLegHigh;
        float ShortFib1, ShortFib2, ShortFib3;
        float ShortZoneTop, ShortZoneBot;

        // ---- setup state machines -----------------------------------------
        int   LongStage;
        int   LongAbsorbBar;
        float LongFailLow;
        float LongAbsorbHigh;

        int   ShortStage;
        int   ShortAbsorbBar;
        float ShortFailHigh;
        float ShortAbsorbLow;

        // ---- active (displayed) trade --------------------------------------
        int   TradeDir;          // +1 long, -1 short, 0 flat
        int   TradeEntryBar;
        float Entry, Stop, InitialStop, Tgt1, Tgt2, Trail;
        int   TrailActive;
        int   Tgt1Hit;
        float BestPrice;

        int   SetupsThisSession;

        // scratch buffer for footprint rows (per study instance, not static)
        std::vector<s_Lvl> Scratch;

        void ResetTrade()
        {
            TradeDir = 0; TradeEntryBar = -1;
            Entry = Stop = InitialStop = Tgt1 = Tgt2 = Trail = 0.0f;
            TrailActive = 0; Tgt1Hit = 0; BestPrice = 0.0f;
        }

        void ResetSetups()
        {
            LongStage = STAGE_IDLE;  LongAbsorbBar = -1;
            LongFailLow = 0.0f;      LongAbsorbHigh = 0.0f;
            ShortStage = STAGE_IDLE; ShortAbsorbBar = -1;
            ShortFailHigh = 0.0f;    ShortAbsorbLow = 0.0f;
        }

        void Reset()
        {
            SessionVol.clear();
            TradingDayDate = -1;
            LastProfileBar = -1;
            POC = VAH = VAL = 0.0f;  ProfileValid = 0;
            PriorPOC = PriorVAH = PriorVAL = 0.0f; PriorValid = 0;
            LastEvalBar = -1;
            Structure = STRUCT_FLAT;
            Gamma = GAMMA_UNK;
            LastSwingHigh = LastSwingLow = 0.0f;
            LongZoneValid = 0;  ShortZoneValid = 0;
            LongLegLow = LongLegHigh = 0.0f;
            LongFib1 = LongFib2 = LongFib3 = 0.0f;
            LongZoneTop = LongZoneBot = 0.0f;
            ShortLegLow = ShortLegHigh = 0.0f;
            ShortFib1 = ShortFib2 = ShortFib3 = 0.0f;
            ShortZoneTop = ShortZoneBot = 0.0f;
            ResetSetups();
            ResetTrade();
            SetupsThisSession = 0;
            Scratch.clear();
        }

        s_State() { Reset(); }
    };

/*---------------------------------------------------------------------------*/
/*  Footprint order flow for one bar                                          */
/*---------------------------------------------------------------------------*/
    static void ComputeOrderFlow(SCStudyInterfaceRef sc,
                                 int BarIndex,
                                 double ImbalanceRatio,
                                 double MinImbVolume,
                                 s_OF& OF,
                                 std::vector<s_Lvl>& Scratch)
    {
        OF.Clear();

        if (sc.VolumeAtPriceForBars == NULL)
            return;
        if (BarIndex < 0 || BarIndex >= sc.ArraySize)
            return;

        const int NumLevels = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(BarIndex);
        if (NumLevels <= 0)
            return;

        Scratch.clear();
        Scratch.reserve(NumLevels);

        for (int e = 0; e < NumLevels; ++e)
        {
            const s_VolumeAtPriceV2* p_VAP = NULL;
            if (sc.VolumeAtPriceForBars->GetVAPElementAtIndex(BarIndex, e, &p_VAP) == 0)
                continue;
            if (p_VAP == NULL)
                continue;

            s_Lvl L;
            L.Tick = p_VAP->PriceInTicks;
            L.Bid  = (double)p_VAP->BidVolume;   // traded at the bid = selling
            L.Ask  = (double)p_VAP->AskVolume;   // traded at the ask = buying
            Scratch.push_back(L);

            OF.Delta  += L.Ask - L.Bid;
            OF.Volume += (double)p_VAP->Volume;
        }

        if (Scratch.empty())
            return;

        std::sort(Scratch.begin(), Scratch.end(), LvlLess);

        // Diagonal imbalance, the standard footprint definition:
        //   BUY  imbalance at price P : Ask(P)  vs Bid(P - 1 tick)
        //   SELL imbalance at price P : Bid(P)  vs Ask(P + 1 tick)
        int RunBuy = 0, RunSell = 0;
        const int N = (int)Scratch.size();

        for (int i = 0; i < N; ++i)
        {
            const double Ask = Scratch[i].Ask;
            const double Bid = Scratch[i].Bid;

            double BidBelow = 0.0;
            if (i > 0 && Scratch[i - 1].Tick == Scratch[i].Tick - 1)
                BidBelow = Scratch[i - 1].Bid;

            double AskAbove = 0.0;
            if (i + 1 < N && Scratch[i + 1].Tick == Scratch[i].Tick + 1)
                AskAbove = Scratch[i + 1].Ask;

            const bool BuyImb =
                (Ask >= MinImbVolume) &&
                (BidBelow <= 0.0 ? true : (Ask >= ImbalanceRatio * BidBelow));

            const bool SellImb =
                (Bid >= MinImbVolume) &&
                (AskAbove <= 0.0 ? true : (Bid >= ImbalanceRatio * AskAbove));

            if (BuyImb)
            {
                OF.BuyImb++;
                RunBuy++;
                if (RunBuy > OF.StackBuy) OF.StackBuy = RunBuy;
            }
            else
                RunBuy = 0;

            if (SellImb)
            {
                OF.SellImb++;
                RunSell++;
                if (RunSell > OF.StackSell) OF.StackSell = RunSell;
            }
            else
                RunSell = 0;
        }
    }

/*---------------------------------------------------------------------------*/
/*  Add one bar's traded volume into the session profile map                   */
/*---------------------------------------------------------------------------*/
    static void AddBarToProfile(SCStudyInterfaceRef sc, int BarIndex,
                                std::map<int, double>& Profile)
    {
        if (sc.VolumeAtPriceForBars == NULL)
            return;

        const int NumLevels = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(BarIndex);
        for (int e = 0; e < NumLevels; ++e)
        {
            const s_VolumeAtPriceV2* p_VAP = NULL;
            if (sc.VolumeAtPriceForBars->GetVAPElementAtIndex(BarIndex, e, &p_VAP) == 0)
                continue;
            if (p_VAP == NULL)
                continue;

            Profile[p_VAP->PriceInTicks] += (double)p_VAP->Volume;
        }
    }

/*---------------------------------------------------------------------------*/
/*  POC + value area from a price->volume map                                  */
/*---------------------------------------------------------------------------*/
    static bool ComputeValueArea(const std::map<int, double>& Profile,
                                 double ValueAreaPercent,
                                 float TickSize,
                                 float& OutPOC, float& OutVAH, float& OutVAL)
    {
        if (Profile.empty())
            return false;

        std::vector<int>    Ticks;
        std::vector<double> Vols;
        Ticks.reserve(Profile.size());
        Vols.reserve(Profile.size());

        double Total = 0.0;
        for (std::map<int, double>::const_iterator it = Profile.begin(); it != Profile.end(); ++it)
        {
            Ticks.push_back(it->first);
            Vols.push_back(it->second);
            Total += it->second;
        }

        if (Total <= 0.0)
            return false;

        const int N = (int)Ticks.size();

        int    PocIdx = 0;
        double PocVol = Vols[0];
        for (int i = 1; i < N; ++i)
        {
            if (Vols[i] > PocVol)
            {
                PocVol = Vols[i];
                PocIdx = i;
            }
        }

        const double Target = Total * (ValueAreaPercent / 100.0);
        double Acc = Vols[PocIdx];
        int Lo = PocIdx, Hi = PocIdx;

        while (Acc < Target && (Lo > 0 || Hi < N - 1))
        {
            const double VolAbove = (Hi < N - 1) ? Vols[Hi + 1] : -1.0;
            const double VolBelow = (Lo > 0)     ? Vols[Lo - 1] : -1.0;

            if (VolAbove < 0.0 && VolBelow < 0.0)
                break;

            if (VolAbove >= VolBelow)
            {
                Hi++;
                Acc += Vols[Hi];
            }
            else
            {
                Lo--;
                Acc += Vols[Lo];
            }
        }

        OutPOC = (float)(Ticks[PocIdx] * TickSize);
        OutVAL = (float)(Ticks[Lo]     * TickSize);
        OutVAH = (float)(Ticks[Hi]     * TickSize);
        return true;
    }

/*---------------------------------------------------------------------------*/
/*  Swing pivot search (used for structure and for the retracement leg)        */
/*---------------------------------------------------------------------------*/
    static bool IsPivotHigh(SCFloatArrayRef High, int K, int Strength, int MaxIndex)
    {
        if (K - Strength < 0 || K + Strength > MaxIndex)
            return false;

        const float V = High[K];
        for (int j = 1; j <= Strength; ++j)
        {
            if (High[K - j] > V)  return false;   // strictly higher on the left invalidates
            if (High[K + j] >= V) return false;   // ties on the right invalidate
        }
        return true;
    }

    static bool IsPivotLow(SCFloatArrayRef Low, int K, int Strength, int MaxIndex)
    {
        if (K - Strength < 0 || K + Strength > MaxIndex)
            return false;

        const float V = Low[K];
        for (int j = 1; j <= Strength; ++j)
        {
            if (Low[K - j] < V)  return false;
            if (Low[K + j] <= V) return false;
        }
        return true;
    }

    // Walks backwards from StartFrom looking for the most recent confirmed pivot.
    static bool FindLastPivot(SCFloatArrayRef High, SCFloatArrayRef Low,
                              bool WantHigh,
                              int StartFrom, int MaxIndex,
                              int Strength, int MaxLookback,
                              int& OutIndex, float& OutPrice)
    {
        int Stop = StartFrom - MaxLookback;
        if (Stop < Strength) Stop = Strength;

        for (int k = StartFrom; k >= Stop; --k)
        {
            if (WantHigh)
            {
                if (IsPivotHigh(High, k, Strength, MaxIndex))
                {
                    OutIndex = k; OutPrice = High[k];
                    return true;
                }
            }
            else
            {
                if (IsPivotLow(Low, k, Strength, MaxIndex))
                {
                    OutIndex = k; OutPrice = Low[k];
                    return true;
                }
            }
        }
        return false;
    }

    // Value-up / value-down / sideways from the last two swing highs and lows.
    static int DetectStructure(SCFloatArrayRef High, SCFloatArrayRef Low,
                               int EndIndex, int Strength, int MaxLookback,
                               float& OutSwingHigh, float& OutSwingLow)
    {
        OutSwingHigh = 0.0f;
        OutSwingLow  = 0.0f;

        float PH[2] = { 0.0f, 0.0f };
        float PL[2] = { 0.0f, 0.0f };
        int   FoundH = 0, FoundL = 0;

        int Start = EndIndex - Strength;
        int Stop  = Start - MaxLookback;
        if (Stop < Strength) Stop = Strength;

        for (int k = Start; k >= Stop && (FoundH < 2 || FoundL < 2); --k)
        {
            if (FoundH < 2 && IsPivotHigh(High, k, Strength, EndIndex))
                PH[FoundH++] = High[k];

            if (FoundL < 2 && IsPivotLow(Low, k, Strength, EndIndex))
                PL[FoundL++] = Low[k];
        }

        if (FoundH > 0) OutSwingHigh = PH[0];
        if (FoundL > 0) OutSwingLow  = PL[0];

        if (FoundH < 2 || FoundL < 2)
            return STRUCT_FLAT;

        if (PH[0] > PH[1] && PL[0] > PL[1]) return STRUCT_UP;    // HH + HL
        if (PH[0] < PH[1] && PL[0] < PL[1]) return STRUCT_DOWN;  // LH + LL
        return STRUCT_FLAT;
    }

    static const char* StructureText(int S)
    {
        if (S == STRUCT_UP)   return "VALUE-UP";
        if (S == STRUCT_DOWN) return "VALUE-DOWN";
        return "SIDEWAYS";
    }

    static const char* GammaText(int G)
    {
        if (G == GAMMA_POS) return "POSITIVE (dampening)";
        if (G == GAMMA_NEG) return "NEGATIVE (amplifying)";
        return "UNKNOWN";
    }

    static const char* StageText(int S)
    {
        if (S == STAGE_ARMED)  return "ARMED (in location)";
        if (S == STAGE_ABSORB) return "ABSORPTION SEEN";
        if (S == STAGE_TRADE)  return "IN TRADE";
        return "IDLE";
    }
} // namespace RLC


/*=============================================================================*/
/*  The study                                                                   */
/*=============================================================================*/
SCSFExport scsf_RLCFramework(SCStudyInterfaceRef sc)
{
    /*------------------------------------------------------------------ plots */
    SCSubgraphRef Sub_VAH        = sc.Subgraph[0];
    SCSubgraphRef Sub_VAL        = sc.Subgraph[1];
    SCSubgraphRef Sub_POC        = sc.Subgraph[2];
    SCSubgraphRef Sub_PriorVAH   = sc.Subgraph[3];
    SCSubgraphRef Sub_PriorVAL   = sc.Subgraph[4];
    SCSubgraphRef Sub_PriorPOC   = sc.Subgraph[5];
    SCSubgraphRef Sub_Fib705     = sc.Subgraph[6];
    SCSubgraphRef Sub_Fib786     = sc.Subgraph[7];
    SCSubgraphRef Sub_Fib886     = sc.Subgraph[8];
    SCSubgraphRef Sub_Absorption = sc.Subgraph[9];
    SCSubgraphRef Sub_LongSig    = sc.Subgraph[10];
    SCSubgraphRef Sub_ShortSig   = sc.Subgraph[11];
    SCSubgraphRef Sub_EntryLine  = sc.Subgraph[12];
    SCSubgraphRef Sub_StopLine   = sc.Subgraph[13];
    SCSubgraphRef Sub_Target1    = sc.Subgraph[14];
    SCSubgraphRef Sub_Target2    = sc.Subgraph[15];
    SCSubgraphRef Sub_TrailLine  = sc.Subgraph[16];
    SCSubgraphRef Sub_Delta      = sc.Subgraph[17];
    SCSubgraphRef Sub_BuyImb     = sc.Subgraph[18];
    SCSubgraphRef Sub_SellImb    = sc.Subgraph[19];
    SCSubgraphRef Sub_StackBuy   = sc.Subgraph[20];
    SCSubgraphRef Sub_StackSell  = sc.Subgraph[21];
    SCSubgraphRef Sub_Structure  = sc.Subgraph[22];
    SCSubgraphRef Sub_GammaState = sc.Subgraph[23];
    SCSubgraphRef Sub_StageOut   = sc.Subgraph[24];
    SCSubgraphRef Sub_Dashboard  = sc.Subgraph[25];

    /*----------------------------------------------------------------- inputs */
    SCInputRef In_EnableLong     = sc.Input[0];
    SCInputRef In_EnableShort    = sc.Input[1];
    SCInputRef In_StructChart    = sc.Input[2];
    SCInputRef In_StructStrength = sc.Input[3];
    SCInputRef In_StructLookback = sc.Input[4];
    SCInputRef In_StructFilter   = sc.Input[5];
    SCInputRef In_GammaMode      = sc.Input[6];
    SCInputRef In_GammaFlip      = sc.Input[7];
    SCInputRef In_GammaRequired  = sc.Input[8];
    SCInputRef In_VAPercent      = sc.Input[9];
    SCInputRef In_LocSource      = sc.Input[10];
    SCInputRef In_ZoneOutsideVA  = sc.Input[11];
    SCInputRef In_VATolTicks     = sc.Input[12];
    SCInputRef In_LegStrength    = sc.Input[13];
    SCInputRef In_Fib1           = sc.Input[14];
    SCInputRef In_Fib2           = sc.Input[15];
    SCInputRef In_Fib3           = sc.Input[16];
    SCInputRef In_MinLegTicks    = sc.Input[17];
    SCInputRef In_LegLookback    = sc.Input[18];
    SCInputRef In_ImbRatioPct    = sc.Input[19];
    SCInputRef In_ImbMinVol      = sc.Input[20];
    SCInputRef In_MinStacked     = sc.Input[21];
    SCInputRef In_AbsMinDelta    = sc.Input[22];
    SCInputRef In_AbsAutoMult    = sc.Input[23];
    SCInputRef In_AbsClosePos    = sc.Input[24];
    SCInputRef In_AbsNeedImb     = sc.Input[25];
    SCInputRef In_TrigMaxBars    = sc.Input[26];
    SCInputRef In_TrigNeedBreak  = sc.Input[27];
    SCInputRef In_StopBuffer     = sc.Input[28];
    SCInputRef In_R1             = sc.Input[29];
    SCInputRef In_R2             = sc.Input[30];
    SCInputRef In_TrailActivateR = sc.Input[31];
    SCInputRef In_TrailLookback  = sc.Input[32];
    SCInputRef In_TrailBuffer    = sc.Input[33];
    SCInputRef In_UseSession     = sc.Input[34];
    SCInputRef In_SessionStart   = sc.Input[35];
    SCInputRef In_SessionEnd     = sc.Input[36];
    SCInputRef In_MaxSetups      = sc.Input[37];
    SCInputRef In_Alerts         = sc.Input[38];
    SCInputRef In_DrawZone       = sc.Input[39];
    SCInputRef In_ShowPrior      = sc.Input[40];
    SCInputRef In_ShowDash       = sc.Input[41];
    SCInputRef In_DashX          = sc.Input[42];
    SCInputRef In_DashY          = sc.Input[43];

    /*======================================================================*/
    /*  Defaults                                                             */
    /*======================================================================*/
    if (sc.SetDefaults)
    {
        sc.GraphName            = "Champion OrderFlow Strat (Regime / Location / Confirmation)";
        sc.StudyDescription     = "Environment, Location, Confirmation and trade management "
                                  "framework using session volume profile, deep Fibonacci "
                                  "retracement and bid/ask footprint order flow.";
        sc.GraphRegion          = 0;
        sc.AutoLoop             = 1;
        sc.MaintainVolumeAtPriceData = 1;
        sc.ValueFormat          = VALUEFORMAT_INHERITED;
        sc.DrawZeros            = 0;
        sc.AlertOnlyOncePerBar  = 1;

        // ---- profile lines
        Sub_VAH.Name = "Value Area High";
        Sub_VAH.DrawStyle = DRAWSTYLE_DASH;
        Sub_VAH.PrimaryColor = RGB(120, 170, 255);
        Sub_VAH.LineWidth = 1;
        Sub_VAH.DrawZeros = 0;

        Sub_VAL.Name = "Value Area Low";
        Sub_VAL.DrawStyle = DRAWSTYLE_DASH;
        Sub_VAL.PrimaryColor = RGB(120, 170, 255);
        Sub_VAL.LineWidth = 1;
        Sub_VAL.DrawZeros = 0;

        Sub_POC.Name = "Point of Control";
        Sub_POC.DrawStyle = DRAWSTYLE_LINE;
        Sub_POC.PrimaryColor = RGB(255, 200, 0);
        Sub_POC.LineWidth = 2;
        Sub_POC.DrawZeros = 0;

        Sub_PriorVAH.Name = "Prior Session VAH";
        Sub_PriorVAH.DrawStyle = DRAWSTYLE_DASH;
        Sub_PriorVAH.PrimaryColor = RGB(90, 110, 150);
        Sub_PriorVAH.LineWidth = 1;
        Sub_PriorVAH.DrawZeros = 0;

        Sub_PriorVAL.Name = "Prior Session VAL";
        Sub_PriorVAL.DrawStyle = DRAWSTYLE_DASH;
        Sub_PriorVAL.PrimaryColor = RGB(90, 110, 150);
        Sub_PriorVAL.LineWidth = 1;
        Sub_PriorVAL.DrawZeros = 0;

        Sub_PriorPOC.Name = "Prior Session POC";
        Sub_PriorPOC.DrawStyle = DRAWSTYLE_DASH;
        Sub_PriorPOC.PrimaryColor = RGB(160, 130, 40);
        Sub_PriorPOC.LineWidth = 1;
        Sub_PriorPOC.DrawZeros = 0;

        // ---- fib zone
        Sub_Fib705.Name = "Fib 0.705 (zone edge)";
        Sub_Fib705.DrawStyle = DRAWSTYLE_LINE;
        Sub_Fib705.PrimaryColor = RGB(0, 200, 120);
        Sub_Fib705.LineWidth = 1;
        Sub_Fib705.DrawZeros = 0;

        Sub_Fib786.Name = "Fib 0.786";
        Sub_Fib786.DrawStyle = DRAWSTYLE_LINE;
        Sub_Fib786.PrimaryColor = RGB(0, 160, 200);
        Sub_Fib786.LineWidth = 1;
        Sub_Fib786.DrawZeros = 0;

        Sub_Fib886.Name = "Fib 0.886 (zone edge)";
        Sub_Fib886.DrawStyle = DRAWSTYLE_LINE;
        Sub_Fib886.PrimaryColor = RGB(0, 200, 120);
        Sub_Fib886.LineWidth = 1;
        Sub_Fib886.DrawZeros = 0;

        // ---- markers
        Sub_Absorption.Name = "Absorption Bar";
        Sub_Absorption.DrawStyle = DRAWSTYLE_POINT;
        Sub_Absorption.PrimaryColor = RGB(255, 140, 0);
        Sub_Absorption.LineWidth = 5;
        Sub_Absorption.DrawZeros = 0;

        Sub_LongSig.Name = "Long Trigger";
        Sub_LongSig.DrawStyle = DRAWSTYLE_ARROW_UP;
        Sub_LongSig.PrimaryColor = RGB(0, 255, 90);
        Sub_LongSig.LineWidth = 3;
        Sub_LongSig.DrawZeros = 0;

        Sub_ShortSig.Name = "Short Trigger";
        Sub_ShortSig.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        Sub_ShortSig.PrimaryColor = RGB(255, 60, 60);
        Sub_ShortSig.LineWidth = 3;
        Sub_ShortSig.DrawZeros = 0;

        // ---- trade management lines
        Sub_EntryLine.Name = "Entry";
        Sub_EntryLine.DrawStyle = DRAWSTYLE_LINE;
        Sub_EntryLine.PrimaryColor = RGB(255, 255, 255);
        Sub_EntryLine.LineWidth = 1;
        Sub_EntryLine.DrawZeros = 0;

        Sub_StopLine.Name = "Stop (invalidation)";
        Sub_StopLine.DrawStyle = DRAWSTYLE_LINE;
        Sub_StopLine.PrimaryColor = RGB(255, 60, 60);
        Sub_StopLine.LineWidth = 2;
        Sub_StopLine.DrawZeros = 0;

        Sub_Target1.Name = "Target 1 (R multiple)";
        Sub_Target1.DrawStyle = DRAWSTYLE_LINE;
        Sub_Target1.PrimaryColor = RGB(0, 210, 255);
        Sub_Target1.LineWidth = 1;
        Sub_Target1.DrawZeros = 0;

        Sub_Target2.Name = "Target 2 (R multiple)";
        Sub_Target2.DrawStyle = DRAWSTYLE_LINE;
        Sub_Target2.PrimaryColor = RGB(0, 255, 200);
        Sub_Target2.LineWidth = 1;
        Sub_Target2.DrawZeros = 0;

        Sub_TrailLine.Name = "Trailing Stop";
        Sub_TrailLine.DrawStyle = DRAWSTYLE_DASH;
        Sub_TrailLine.PrimaryColor = RGB(255, 170, 60);
        Sub_TrailLine.LineWidth = 2;
        Sub_TrailLine.DrawZeros = 0;

        // ---- data only (Values Window)
        Sub_Delta.Name      = "Bar Delta";              Sub_Delta.DrawStyle      = DRAWSTYLE_IGNORE;
        Sub_BuyImb.Name     = "Buy Imbalances";         Sub_BuyImb.DrawStyle     = DRAWSTYLE_IGNORE;
        Sub_SellImb.Name    = "Sell Imbalances";        Sub_SellImb.DrawStyle    = DRAWSTYLE_IGNORE;
        Sub_StackBuy.Name   = "Stacked Buy Imb";        Sub_StackBuy.DrawStyle   = DRAWSTYLE_IGNORE;
        Sub_StackSell.Name  = "Stacked Sell Imb";       Sub_StackSell.DrawStyle  = DRAWSTYLE_IGNORE;
        Sub_Structure.Name  = "Structure (1/0/-1)";     Sub_Structure.DrawStyle  = DRAWSTYLE_IGNORE;
        Sub_GammaState.Name = "Gamma Regime (1/0/-1)";  Sub_GammaState.DrawStyle = DRAWSTYLE_IGNORE;
        Sub_StageOut.Name   = "Setup Stage (0-3)";      Sub_StageOut.DrawStyle   = DRAWSTYLE_IGNORE;

        // The text drawing function requires DRAWSTYLE_CUSTOM_TEXT on this
        // subgraph. PrimaryColor = text color, SecondaryColor = background,
        // LineWidth = font size.
        Sub_Dashboard.Name = "Dashboard Text";
        Sub_Dashboard.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
        Sub_Dashboard.PrimaryColor = RGB(230, 230, 230);
        Sub_Dashboard.SecondaryColor = RGB(20, 20, 20);
        Sub_Dashboard.LineWidth = 9;   // font size for the text drawing
        Sub_Dashboard.DrawZeros = 0;

        /*------------------------------------------------------------ inputs */
        In_EnableLong.Name = "1. Enable Long Setups";
        In_EnableLong.SetYesNo(1);

        In_EnableShort.Name = "1. Enable Short Setups";
        In_EnableShort.SetYesNo(1);

        In_StructChart.Name = "2. Environment: Structure Source Chart Number (0 = this chart)";
        In_StructChart.SetInt(0);
        In_StructChart.SetIntLimits(0, 10000);

        In_StructStrength.Name = "2. Environment: Structure Pivot Strength (bars each side)";
        In_StructStrength.SetInt(5);
        In_StructStrength.SetIntLimits(1, 100);

        In_StructLookback.Name = "2. Environment: Structure Lookback (bars)";
        In_StructLookback.SetInt(300);
        In_StructLookback.SetIntLimits(20, 5000);

        In_StructFilter.Name = "2. Environment: Structure Filter";
        In_StructFilter.SetCustomInputStrings("Off;Require Aligned Structure;Aligned or Sideways");
        In_StructFilter.SetCustomInputIndex(1);

        In_GammaMode.Name = "2. Environment: Gamma (GEX) Mode";
        In_GammaMode.SetCustomInputStrings("Off;Force Positive;Force Negative;Auto from Gamma Flip Level");
        In_GammaMode.SetCustomInputIndex(0);

        In_GammaFlip.Name = "2. Environment: Gamma Flip Level (price)";
        In_GammaFlip.SetFloat(0.0f);

        In_GammaRequired.Name = "2. Environment: Required Gamma Regime";
        In_GammaRequired.SetCustomInputStrings("Any;Positive Only;Negative Only");
        In_GammaRequired.SetCustomInputIndex(0);

        In_VAPercent.Name = "3. Location: Value Area Percent";
        In_VAPercent.SetFloat(70.0f);
        In_VAPercent.SetFloatLimits(30.0f, 95.0f);

        In_LocSource.Name = "3. Location: Profile Used for Discount/Premium";
        In_LocSource.SetCustomInputStrings("Developing Session;Prior Session;Either");
        In_LocSource.SetCustomInputIndex(0);

        In_ZoneOutsideVA.Name = "3. Location: Require Fib Zone Outside Value Area";
        In_ZoneOutsideVA.SetYesNo(1);

        In_VATolTicks.Name = "3. Location: Value Area Tolerance (ticks)";
        In_VATolTicks.SetInt(2);
        In_VATolTicks.SetIntLimits(0, 200);

        In_LegStrength.Name = "3. Location: Retracement Leg Pivot Strength";
        In_LegStrength.SetInt(3);
        In_LegStrength.SetIntLimits(1, 50);

        In_Fib1.Name = "3. Location: Fib Level 1 (zone edge)";
        In_Fib1.SetFloat(0.705f);
        In_Fib1.SetFloatLimits(0.1f, 0.99f);

        In_Fib2.Name = "3. Location: Fib Level 2 (mid)";
        In_Fib2.SetFloat(0.786f);
        In_Fib2.SetFloatLimits(0.1f, 0.99f);

        In_Fib3.Name = "3. Location: Fib Level 3 (zone edge)";
        In_Fib3.SetFloat(0.886f);
        In_Fib3.SetFloatLimits(0.1f, 0.99f);

        In_MinLegTicks.Name = "3. Location: Minimum Leg Size (ticks)";
        In_MinLegTicks.SetInt(8);
        In_MinLegTicks.SetIntLimits(1, 100000);

        In_LegLookback.Name = "3. Location: Leg Search Lookback (bars)";
        In_LegLookback.SetInt(150);
        In_LegLookback.SetIntLimits(10, 5000);

        In_ImbRatioPct.Name = "4. Confirmation: Diagonal Imbalance Ratio (percent)";
        In_ImbRatioPct.SetInt(400);
        In_ImbRatioPct.SetIntLimits(110, 5000);

        In_ImbMinVol.Name = "4. Confirmation: Imbalance Minimum Volume per Level";
        In_ImbMinVol.SetInt(10);
        In_ImbMinVol.SetIntLimits(0, 100000);

        In_MinStacked.Name = "4. Confirmation: Minimum Stacked Imbalances to Trigger";
        In_MinStacked.SetInt(2);
        In_MinStacked.SetIntLimits(1, 50);

        In_AbsMinDelta.Name = "4. Confirmation: Absorption Minimum |Delta| (0 = auto)";
        In_AbsMinDelta.SetInt(0);
        In_AbsMinDelta.SetIntLimits(0, 10000000);

        In_AbsAutoMult.Name = "4. Confirmation: Auto Delta Threshold (x 20-bar average)";
        In_AbsAutoMult.SetFloat(1.0f);
        In_AbsAutoMult.SetFloatLimits(0.1f, 20.0f);

        In_AbsClosePos.Name = "4. Confirmation: Absorption Close Position in Bar Range (0-1)";
        In_AbsClosePos.SetFloat(0.5f);
        In_AbsClosePos.SetFloatLimits(0.0f, 1.0f);

        In_AbsNeedImb.Name = "4. Confirmation: Absorption Requires Opposing Imbalance";
        In_AbsNeedImb.SetYesNo(1);

        In_TrigMaxBars.Name = "4. Confirmation: Max Bars from Absorption to Trigger";
        In_TrigMaxBars.SetInt(6);
        In_TrigMaxBars.SetIntLimits(1, 200);

        In_TrigNeedBreak.Name = "4. Confirmation: Trigger Must Close Beyond Absorption Bar";
        In_TrigNeedBreak.SetYesNo(0);

        In_StopBuffer.Name = "5. Management: Stop Buffer (ticks)";
        In_StopBuffer.SetInt(2);
        In_StopBuffer.SetIntLimits(0, 1000);

        In_R1.Name = "5. Management: Target 1 (R multiple)";
        In_R1.SetFloat(1.5f);
        In_R1.SetFloatLimits(0.1f, 100.0f);

        In_R2.Name = "5. Management: Target 2 (R multiple)";
        In_R2.SetFloat(2.0f);
        In_R2.SetFloatLimits(0.1f, 100.0f);

        In_TrailActivateR.Name = "5. Management: Activate Trailing Stop at (R multiple)";
        In_TrailActivateR.SetFloat(1.0f);
        In_TrailActivateR.SetFloatLimits(0.1f, 100.0f);

        In_TrailLookback.Name = "5. Management: Trailing Stop Swing Lookback (bars)";
        In_TrailLookback.SetInt(3);
        In_TrailLookback.SetIntLimits(1, 200);

        In_TrailBuffer.Name = "5. Management: Trailing Stop Buffer (ticks)";
        In_TrailBuffer.SetInt(2);
        In_TrailBuffer.SetIntLimits(0, 1000);

        In_UseSession.Name = "6. Filters: Use Session Time Filter";
        In_UseSession.SetYesNo(1);

        In_SessionStart.Name = "6. Filters: Session Start Time";
        In_SessionStart.SetTime(HMS_TIME(9, 30, 0));

        In_SessionEnd.Name = "6. Filters: Session End Time";
        In_SessionEnd.SetTime(HMS_TIME(15, 45, 0));

        In_MaxSetups.Name = "6. Filters: Max Setups Per Session (0 = unlimited)";
        In_MaxSetups.SetInt(0);
        In_MaxSetups.SetIntLimits(0, 100);

        In_Alerts.Name = "7. Display: Enable Alerts";
        In_Alerts.SetYesNo(1);

        In_DrawZone.Name = "7. Display: Draw Golden Pocket Zone";
        In_DrawZone.SetYesNo(1);

        In_ShowPrior.Name = "7. Display: Show Prior Session Levels";
        In_ShowPrior.SetYesNo(1);

        In_ShowDash.Name = "7. Display: Show Status Dashboard";
        In_ShowDash.SetYesNo(1);

        In_DashX.Name = "7. Display: Dashboard Horizontal Position (0-150)";
        In_DashX.SetInt(20);
        In_DashX.SetIntLimits(0, 150);

        In_DashY.Name = "7. Display: Dashboard Vertical Position (0-100)";
        In_DashY.SetInt(92);
        In_DashY.SetIntLimits(0, 100);

        return;
    }

    /*======================================================================*/
    /*  Persistent state                                                     */
    /*======================================================================*/
    RLC::s_State* p = (RLC::s_State*)sc.GetPersistentPointer(1);

    if (sc.LastCallToFunction)
    {
        if (p != NULL)
        {
            delete p;
            sc.SetPersistentPointer(1, NULL);
        }
        return;
    }

    if (p == NULL)
    {
        p = new RLC::s_State();
        sc.SetPersistentPointer(1, p);
    }

    if (sc.Index == 0)
        p->Reset();                       // full recalculation restart

    if (sc.VolumeAtPriceForBars == NULL)
    {
        if (sc.Index == 0)
            sc.AddMessageToLog("RLC Framework: no Volume at Price data on this chart. "
                               "Use an intraday chart with bid/ask volume.", 1);
        return;
    }

    const float  TickSize      = sc.TickSize;
    const double ImbRatio      = In_ImbRatioPct.GetInt() / 100.0;
    const double ImbMinVol     = (double)In_ImbMinVol.GetInt();
    const int    LineBase      = 5000000 + (sc.StudyGraphInstanceID * 10);

    /*----------------------------------------------------------------------*/
    /*  Live order flow for the current (forming) bar                        */
    /*----------------------------------------------------------------------*/
    RLC::s_OF OFNow;
    RLC::ComputeOrderFlow(sc, sc.Index, ImbRatio, ImbMinVol, OFNow, p->Scratch);

    Sub_Delta[sc.Index]     = (float)OFNow.Delta;
    Sub_BuyImb[sc.Index]    = (float)OFNow.BuyImb;
    Sub_SellImb[sc.Index]   = (float)OFNow.SellImb;
    Sub_StackBuy[sc.Index]  = (float)OFNow.StackBuy;
    Sub_StackSell[sc.Index] = (float)OFNow.StackSell;

    /*======================================================================*/
    /*  Once-per-closed-bar evaluation                                       */
    /*  Everything that creates a signal is computed on a CLOSED bar so the  */
    /*  study never repaints.                                                */
    /*======================================================================*/
    if (sc.Index >= 1 && sc.Index > p->LastEvalBar)
    {
        const int E = sc.Index - 1;               // the bar being evaluated
        p->LastEvalBar = sc.Index;

        /*---------------------------------------------- 1. session profile */
        for (int b = p->LastProfileBar + 1; b <= E; ++b)
        {
            if (b < 0)
                continue;

            const int DayDate = sc.GetTradingDayStartDateTimeOfBar(sc.BaseDateTimeIn[b]).GetDate();

            if (DayDate != p->TradingDayDate)
            {
                if (p->ProfileValid)
                {
                    p->PriorPOC = p->POC;
                    p->PriorVAH = p->VAH;
                    p->PriorVAL = p->VAL;
                    p->PriorValid = 1;
                }
                p->SessionVol.clear();
                p->ProfileValid = 0;
                p->TradingDayDate = DayDate;
                p->SetupsThisSession = 0;
            }

            RLC::AddBarToProfile(sc, b, p->SessionVol);
            p->LastProfileBar = b;
        }

        if (RLC::ComputeValueArea(p->SessionVol, In_VAPercent.GetFloat(), TickSize,
                                  p->POC, p->VAH, p->VAL))
            p->ProfileValid = 1;

        /*--------------------------------------------------- 2. structure */
        if (E >= 2 * In_StructStrength.GetInt() + 2)
        {
            const int StructChart = In_StructChart.GetInt();

            if (StructChart <= 0 || StructChart == sc.ChartNumber)
            {
                p->Structure = RLC::DetectStructure(sc.High, sc.Low, E,
                                                    In_StructStrength.GetInt(),
                                                    In_StructLookback.GetInt(),
                                                    p->LastSwingHigh, p->LastSwingLow);
            }
            else
            {
                SCGraphData HTFData;
                sc.GetChartBaseData(StructChart, HTFData);

                SCFloatArrayRef HTFHigh = HTFData[SC_HIGH];
                SCFloatArrayRef HTFLow  = HTFData[SC_LOW];

                const int HTFSize = HTFHigh.GetArraySize();
                if (HTFSize > 0)
                {
                    // This function maps a bar index on THIS chart to the
                    // containing bar index on the referenced chart.
                    int HTFIndex = sc.GetContainingIndexForDateTimeIndex(StructChart, E);
                    if (HTFIndex >= HTFSize) HTFIndex = HTFSize - 1;

                    if (HTFIndex >= 2 * In_StructStrength.GetInt() + 2)
                    {
                        p->Structure = RLC::DetectStructure(HTFHigh, HTFLow, HTFIndex,
                                                            In_StructStrength.GetInt(),
                                                            In_StructLookback.GetInt(),
                                                            p->LastSwingHigh, p->LastSwingLow);
                    }
                }
                else
                {
                    p->Structure = RLC::DetectStructure(sc.High, sc.Low, E,
                                                        In_StructStrength.GetInt(),
                                                        In_StructLookback.GetInt(),
                                                        p->LastSwingHigh, p->LastSwingLow);
                }
            }
        }

        /*------------------------------------------------- 3. gamma regime */
        const int GammaMode = In_GammaMode.GetIndex();
        if (GammaMode == 0)
            p->Gamma = RLC::GAMMA_UNK;
        else if (GammaMode == 1)
            p->Gamma = RLC::GAMMA_POS;
        else if (GammaMode == 2)
            p->Gamma = RLC::GAMMA_NEG;
        else
        {
            const float Flip = In_GammaFlip.GetFloat();
            if (Flip <= 0.0f)
                p->Gamma = RLC::GAMMA_UNK;
            else
                p->Gamma = (sc.Close[E] >= Flip) ? RLC::GAMMA_POS : RLC::GAMMA_NEG;
        }

        /*----------------------------------- 4. retracement legs and zones */
        p->LongZoneValid  = 0;
        p->ShortZoneValid = 0;

        float FibA = In_Fib1.GetFloat();
        float FibB = In_Fib2.GetFloat();
        float FibC = In_Fib3.GetFloat();
        if (FibA > FibC) { const float T = FibA; FibA = FibC; FibC = T; }

        const int   LegStrength = In_LegStrength.GetInt();
        const int   LegLookback = In_LegLookback.GetInt();
        const float MinLeg      = In_MinLegTicks.GetInt() * TickSize;

        // ---- LONG leg: swing low -> swing high, retrace down
        {
            int   HiIdx = 0; float HiPx = 0.0f;
            if (RLC::FindLastPivot(sc.High, sc.Low, true, E - LegStrength, E,
                                   LegStrength, LegLookback, HiIdx, HiPx))
            {
                int   LoIdx = 0; float LoPx = 0.0f;
                if (RLC::FindLastPivot(sc.High, sc.Low, false, HiIdx - 1, E,
                                       LegStrength, LegLookback, LoIdx, LoPx))
                {
                    // true leg low = lowest low between the pivot low and the pivot high
                    float LegLow = LoPx;
                    for (int b = LoIdx; b <= HiIdx; ++b)
                        if (sc.Low[b] < LegLow) LegLow = sc.Low[b];

                    const float Range = HiPx - LegLow;

                    if (Range >= MinLeg && sc.Low[E] >= LegLow)
                    {
                        p->LongLegLow   = LegLow;
                        p->LongLegHigh  = HiPx;
                        p->LongFib1     = HiPx - Range * FibA;
                        p->LongFib2     = HiPx - Range * FibB;
                        p->LongFib3     = HiPx - Range * FibC;
                        p->LongZoneTop  = p->LongFib1;
                        p->LongZoneBot  = p->LongFib3;
                        p->LongZoneValid = 1;
                    }
                }
            }
        }

        // ---- SHORT leg: swing high -> swing low, retrace up
        {
            int   LoIdx = 0; float LoPx = 0.0f;
            if (RLC::FindLastPivot(sc.High, sc.Low, false, E - LegStrength, E,
                                   LegStrength, LegLookback, LoIdx, LoPx))
            {
                int   HiIdx = 0; float HiPx = 0.0f;
                if (RLC::FindLastPivot(sc.High, sc.Low, true, LoIdx - 1, E,
                                       LegStrength, LegLookback, HiIdx, HiPx))
                {
                    float LegHigh = HiPx;
                    for (int b = HiIdx; b <= LoIdx; ++b)
                        if (sc.High[b] > LegHigh) LegHigh = sc.High[b];

                    const float Range = LegHigh - LoPx;

                    if (Range >= MinLeg && sc.High[E] <= LegHigh)
                    {
                        p->ShortLegLow   = LoPx;
                        p->ShortLegHigh  = LegHigh;
                        p->ShortFib1     = LoPx + Range * FibA;
                        p->ShortFib2     = LoPx + Range * FibB;
                        p->ShortFib3     = LoPx + Range * FibC;
                        p->ShortZoneBot  = p->ShortFib1;
                        p->ShortZoneTop  = p->ShortFib3;
                        p->ShortZoneValid = 1;
                    }
                }
            }
        }

        /*------------------------------------------------ 5. gate checks  */
        // session time
        bool SessionOK = true;
        if (In_UseSession.GetYesNo())
        {
            const int T     = sc.BaseDateTimeIn[E].GetTimeInSeconds();
            const int Start = In_SessionStart.GetTime();
            const int End   = In_SessionEnd.GetTime();
            SessionOK = (Start <= End) ? (T >= Start && T <= End)
                                       : (T >= Start || T <= End);
        }

        // structure
        const int StructFilter = In_StructFilter.GetIndex();
        bool StructOKLong  = true;
        bool StructOKShort = true;
        if (StructFilter == 1)
        {
            StructOKLong  = (p->Structure == RLC::STRUCT_UP);
            StructOKShort = (p->Structure == RLC::STRUCT_DOWN);
        }
        else if (StructFilter == 2)
        {
            StructOKLong  = (p->Structure != RLC::STRUCT_DOWN);
            StructOKShort = (p->Structure != RLC::STRUCT_UP);
        }

        // gamma
        const int GammaReq = In_GammaRequired.GetIndex();
        bool GammaOK = true;
        if (GammaReq == 1) GammaOK = (p->Gamma == RLC::GAMMA_POS);
        if (GammaReq == 2) GammaOK = (p->Gamma == RLC::GAMMA_NEG);

        // location: discount / premium against the chosen profile
        const int   LocSource = In_LocSource.GetIndex();
        const float VATol     = In_VATolTicks.GetInt() * TickSize;

        bool  HaveVAL = false, HaveVAH = false;
        float UseVAL = 0.0f, UseVAH = 0.0f;

        if (LocSource == 0 && p->ProfileValid)
        {
            UseVAL = p->VAL; UseVAH = p->VAH; HaveVAL = HaveVAH = true;
        }
        else if (LocSource == 1 && p->PriorValid)
        {
            UseVAL = p->PriorVAL; UseVAH = p->PriorVAH; HaveVAL = HaveVAH = true;
        }
        else if (LocSource == 2)
        {
            if (p->ProfileValid && p->PriorValid)
            {
                UseVAL = (p->VAL < p->PriorVAL) ? p->VAL : p->PriorVAL;   // widest discount edge
                UseVAH = (p->VAH > p->PriorVAH) ? p->VAH : p->PriorVAH;
                HaveVAL = HaveVAH = true;
            }
            else if (p->ProfileValid)
            {
                UseVAL = p->VAL; UseVAH = p->VAH; HaveVAL = HaveVAH = true;
            }
            else if (p->PriorValid)
            {
                UseVAL = p->PriorVAL; UseVAH = p->PriorVAH; HaveVAL = HaveVAH = true;
            }
        }

        bool LocationOKLong  = true;
        bool LocationOKShort = true;
        if (In_ZoneOutsideVA.GetYesNo())
        {
            LocationOKLong  = HaveVAL && p->LongZoneValid  && (p->LongZoneTop  <= UseVAL + VATol);
            LocationOKShort = HaveVAH && p->ShortZoneValid && (p->ShortZoneBot >= UseVAH - VATol);
        }

        const bool SetupBudgetOK = (In_MaxSetups.GetInt() == 0) ||
                                   (p->SetupsThisSession < In_MaxSetups.GetInt());

        const bool EnvOKLong  = In_EnableLong.GetYesNo()  && SessionOK && StructOKLong  &&
                                GammaOK && LocationOKLong  && p->LongZoneValid  && SetupBudgetOK;
        const bool EnvOKShort = In_EnableShort.GetYesNo() && SessionOK && StructOKShort &&
                                GammaOK && LocationOKShort && p->ShortZoneValid && SetupBudgetOK;

        /*----------------------------------- 6. order flow of evaluated bar */
        const double Delta    = Sub_Delta[E];
        const int    StackBuy = (int)Sub_StackBuy[E];
        const int    StackSel = (int)Sub_StackSell[E];
        const int    BuyImb   = (int)Sub_BuyImb[E];
        const int    SellImb  = (int)Sub_SellImb[E];

        // auto delta threshold = multiple of the average |delta| of the last 20 bars
        double DeltaThreshold = (double)In_AbsMinDelta.GetInt();
        if (DeltaThreshold <= 0.0)
        {
            double Sum = 0.0; int Count = 0;
            for (int b = E; b > E - 20 && b >= 0; --b)
            {
                const double D = (double)Sub_Delta[b];
                Sum += (D < 0.0) ? -D : D;
                Count++;
            }
            const double Avg = (Count > 0) ? (Sum / Count) : 0.0;
            DeltaThreshold = Avg * In_AbsAutoMult.GetFloat();
        }

        const float BarRange = sc.High[E] - sc.Low[E];
        const float ClosePos = (BarRange > 0.0f)
                             ? ((sc.Close[E] - sc.Low[E]) / BarRange)
                             : 0.5f;

        const int   StopBufTicks = In_StopBuffer.GetInt();
        const float StopBuffer   = StopBufTicks * TickSize;

        /*=================================================================*/
        /*  TRADE MANAGEMENT (runs first so a closed trade can re-arm)      */
        /*=================================================================*/
        if (p->TradeDir != 0)
        {
            const float R = (p->TradeDir > 0) ? (p->Entry - p->InitialStop)
                                              : (p->InitialStop - p->Entry);

            if (p->TradeDir > 0)
            {
                if (sc.High[E] > p->BestPrice) p->BestPrice = sc.High[E];

                if (!p->Tgt1Hit && sc.High[E] >= p->Tgt1) p->Tgt1Hit = 1;

                if (!p->TrailActive && R > 0.0f &&
                    (p->BestPrice - p->Entry) >= In_TrailActivateR.GetFloat() * R)
                    p->TrailActive = 1;

                if (p->TrailActive)
                {
                    float SwingLow = sc.Low[E];
                    for (int b = E; b > E - In_TrailLookback.GetInt() && b >= 0; --b)
                        if (sc.Low[b] < SwingLow) SwingLow = sc.Low[b];

                    const float Candidate = SwingLow - In_TrailBuffer.GetInt() * TickSize;
                    if (Candidate > p->Trail) p->Trail = Candidate;
                    if (p->Trail > p->Stop)   p->Stop  = p->Trail;
                }

                if (sc.Low[E] <= p->Stop)
                {
                    if (In_Alerts.GetYesNo() && !sc.IsFullRecalculation)
                    {
                        SCString M;
                        M.Format("RLC: LONG closed on stop/trail at %s",
                                 sc.FormatGraphValue(p->Stop, sc.BaseGraphValueFormat).GetChars());
                        sc.SetAlert(1, M);
                    }
                    p->ResetTrade();
                    p->ResetSetups();
                }
                else if (sc.High[E] >= p->Tgt2)
                {
                    if (In_Alerts.GetYesNo() && !sc.IsFullRecalculation)
                    {
                        SCString M;
                        M.Format("RLC: LONG reached final target %s",
                                 sc.FormatGraphValue(p->Tgt2, sc.BaseGraphValueFormat).GetChars());
                        sc.SetAlert(1, M);
                    }
                    p->ResetTrade();
                    p->ResetSetups();
                }
            }
            else
            {
                if (p->BestPrice == 0.0f || sc.Low[E] < p->BestPrice) p->BestPrice = sc.Low[E];

                if (!p->Tgt1Hit && sc.Low[E] <= p->Tgt1) p->Tgt1Hit = 1;

                if (!p->TrailActive && R > 0.0f &&
                    (p->Entry - p->BestPrice) >= In_TrailActivateR.GetFloat() * R)
                    p->TrailActive = 1;

                if (p->TrailActive)
                {
                    float SwingHigh = sc.High[E];
                    for (int b = E; b > E - In_TrailLookback.GetInt() && b >= 0; --b)
                        if (sc.High[b] > SwingHigh) SwingHigh = sc.High[b];

                    const float Candidate = SwingHigh + In_TrailBuffer.GetInt() * TickSize;
                    if (p->Trail == 0.0f || Candidate < p->Trail) p->Trail = Candidate;
                    if (p->Trail < p->Stop) p->Stop = p->Trail;
                }

                if (sc.High[E] >= p->Stop)
                {
                    if (In_Alerts.GetYesNo() && !sc.IsFullRecalculation)
                    {
                        SCString M;
                        M.Format("RLC: SHORT closed on stop/trail at %s",
                                 sc.FormatGraphValue(p->Stop, sc.BaseGraphValueFormat).GetChars());
                        sc.SetAlert(1, M);
                    }
                    p->ResetTrade();
                    p->ResetSetups();
                }
                else if (sc.Low[E] <= p->Tgt2)
                {
                    if (In_Alerts.GetYesNo() && !sc.IsFullRecalculation)
                    {
                        SCString M;
                        M.Format("RLC: SHORT reached final target %s",
                                 sc.FormatGraphValue(p->Tgt2, sc.BaseGraphValueFormat).GetChars());
                        sc.SetAlert(1, M);
                    }
                    p->ResetTrade();
                    p->ResetSetups();
                }
            }
        }

        /*=================================================================*/
        /*  LONG STATE MACHINE                                              */
        /*=================================================================*/
        if (p->TradeDir == 0)
        {
            if (!EnvOKLong)
            {
                p->LongStage = RLC::STAGE_IDLE;
            }
            else
            {
                // -------- IDLE -> ARMED : price trades into the golden pocket
                if (p->LongStage == RLC::STAGE_IDLE)
                {
                    if (sc.Low[E] <= p->LongZoneTop && sc.Low[E] >= p->LongLegLow)
                        p->LongStage = RLC::STAGE_ARMED;
                }

                // -------- ARMED -> ABSORPTION : sellers press and fail
                if (p->LongStage == RLC::STAGE_ARMED)
                {
                    const bool TradedInZone   = (sc.Low[E] <= p->LongZoneTop);
                    const bool AggressiveSell = (Delta <= -DeltaThreshold) ||
                                                (In_AbsNeedImb.GetYesNo() && StackSel >= 1 && Delta < 0.0);
                    const bool FailedToBreak  = (ClosePos >= In_AbsClosePos.GetFloat());
                    const bool ImbOK          = (!In_AbsNeedImb.GetYesNo()) || (SellImb >= 1);

                    if (TradedInZone && AggressiveSell && FailedToBreak && ImbOK &&
                        sc.Low[E] >= p->LongLegLow)
                    {
                        p->LongStage      = RLC::STAGE_ABSORB;
                        p->LongAbsorbBar  = E;
                        p->LongFailLow    = sc.Low[E];
                        p->LongAbsorbHigh = sc.High[E];
                        Sub_Absorption[E] = sc.Low[E] - 2 * TickSize;

                        if (In_Alerts.GetYesNo() && !sc.IsFullRecalculation)
                        {
                            SCString M;
                            M.Format("RLC: LONG absorption in discount zone, low %s, delta %.0f",
                                     sc.FormatGraphValue(sc.Low[E], sc.BaseGraphValueFormat).GetChars(),
                                     Delta);
                            sc.SetAlert(1, M);
                        }
                    }
                }
                // -------- ABSORPTION -> TRIGGER or invalidation
                else if (p->LongStage == RLC::STAGE_ABSORB)
                {
                    const bool Invalidated = (sc.Close[E] < p->LongFailLow - StopBuffer) ||
                                             (E - p->LongAbsorbBar > In_TrigMaxBars.GetInt());

                    if (Invalidated)
                    {
                        p->LongStage = RLC::STAGE_IDLE;
                    }
                    else if (E > p->LongAbsorbBar)
                    {
                        const bool BullishBar  = (sc.Close[E] > sc.Open[E]) &&
                                                 (sc.Close[E] > sc.Close[E - 1]);
                        const bool DeltaFlip   = (Delta > 0.0);
                        const bool Dominance   = (StackBuy >= In_MinStacked.GetInt()) ||
                                                 (BuyImb >= In_MinStacked.GetInt());
                        const bool BreakOK     = (!In_TrigNeedBreak.GetYesNo()) ||
                                                 (sc.Close[E] > p->LongAbsorbHigh);

                        if (BullishBar && DeltaFlip && Dominance && BreakOK)
                        {
                            p->TradeDir      = 1;
                            p->TradeEntryBar = E;
                            p->Entry         = sc.Close[E];
                            p->InitialStop   = p->LongFailLow - StopBuffer;
                            p->Stop          = p->InitialStop;
                            p->BestPrice     = sc.High[E];
                            p->TrailActive   = 0;
                            p->Tgt1Hit       = 0;
                            p->Trail         = 0.0f;

                            const float R = p->Entry - p->InitialStop;
                            p->Tgt1 = p->Entry + In_R1.GetFloat() * R;
                            p->Tgt2 = p->Entry + In_R2.GetFloat() * R;

                            p->LongStage = RLC::STAGE_TRADE;
                            p->SetupsThisSession++;

                            Sub_LongSig[E] = sc.Low[E] - 4 * TickSize;

                            if (In_Alerts.GetYesNo() && !sc.IsFullRecalculation)
                            {
                                SCString M;
                                M.Format("RLC LONG TRIGGER  entry %s  stop %s  T1 %s  T2 %s  (R=%.2f)",
                                    sc.FormatGraphValue(p->Entry, sc.BaseGraphValueFormat).GetChars(),
                                    sc.FormatGraphValue(p->Stop,  sc.BaseGraphValueFormat).GetChars(),
                                    sc.FormatGraphValue(p->Tgt1,  sc.BaseGraphValueFormat).GetChars(),
                                    sc.FormatGraphValue(p->Tgt2,  sc.BaseGraphValueFormat).GetChars(),
                                    R);
                                sc.SetAlert(1, M);
                            }
                        }
                    }
                }
            }
        }

        /*=================================================================*/
        /*  SHORT STATE MACHINE                                             */
        /*=================================================================*/
        if (p->TradeDir == 0)
        {
            if (!EnvOKShort)
            {
                p->ShortStage = RLC::STAGE_IDLE;
            }
            else
            {
                if (p->ShortStage == RLC::STAGE_IDLE)
                {
                    if (sc.High[E] >= p->ShortZoneBot && sc.High[E] <= p->ShortLegHigh)
                        p->ShortStage = RLC::STAGE_ARMED;
                }

                if (p->ShortStage == RLC::STAGE_ARMED)
                {
                    const bool TradedInZone  = (sc.High[E] >= p->ShortZoneBot);
                    const bool AggressiveBuy = (Delta >= DeltaThreshold) ||
                                               (In_AbsNeedImb.GetYesNo() && StackBuy >= 1 && Delta > 0.0);
                    const bool FailedToBreak = (ClosePos <= 1.0f - In_AbsClosePos.GetFloat());
                    const bool ImbOK         = (!In_AbsNeedImb.GetYesNo()) || (BuyImb >= 1);

                    if (TradedInZone && AggressiveBuy && FailedToBreak && ImbOK &&
                        sc.High[E] <= p->ShortLegHigh)
                    {
                        p->ShortStage     = RLC::STAGE_ABSORB;
                        p->ShortAbsorbBar = E;
                        p->ShortFailHigh  = sc.High[E];
                        p->ShortAbsorbLow = sc.Low[E];
                        Sub_Absorption[E] = sc.High[E] + 2 * TickSize;

                        if (In_Alerts.GetYesNo() && !sc.IsFullRecalculation)
                        {
                            SCString M;
                            M.Format("RLC: SHORT absorption in premium zone, high %s, delta %.0f",
                                     sc.FormatGraphValue(sc.High[E], sc.BaseGraphValueFormat).GetChars(),
                                     Delta);
                            sc.SetAlert(1, M);
                        }
                    }
                }
                else if (p->ShortStage == RLC::STAGE_ABSORB)
                {
                    const bool Invalidated = (sc.Close[E] > p->ShortFailHigh + StopBuffer) ||
                                             (E - p->ShortAbsorbBar > In_TrigMaxBars.GetInt());

                    if (Invalidated)
                    {
                        p->ShortStage = RLC::STAGE_IDLE;
                    }
                    else if (E > p->ShortAbsorbBar)
                    {
                        const bool BearishBar = (sc.Close[E] < sc.Open[E]) &&
                                                (sc.Close[E] < sc.Close[E - 1]);
                        const bool DeltaFlip  = (Delta < 0.0);
                        const bool Dominance  = (StackSel >= In_MinStacked.GetInt()) ||
                                                (SellImb >= In_MinStacked.GetInt());
                        const bool BreakOK    = (!In_TrigNeedBreak.GetYesNo()) ||
                                                (sc.Close[E] < p->ShortAbsorbLow);

                        if (BearishBar && DeltaFlip && Dominance && BreakOK)
                        {
                            p->TradeDir      = -1;
                            p->TradeEntryBar = E;
                            p->Entry         = sc.Close[E];
                            p->InitialStop   = p->ShortFailHigh + StopBuffer;
                            p->Stop          = p->InitialStop;
                            p->BestPrice     = sc.Low[E];
                            p->TrailActive   = 0;
                            p->Tgt1Hit       = 0;
                            p->Trail         = 0.0f;

                            const float R = p->InitialStop - p->Entry;
                            p->Tgt1 = p->Entry - In_R1.GetFloat() * R;
                            p->Tgt2 = p->Entry - In_R2.GetFloat() * R;

                            p->ShortStage = RLC::STAGE_TRADE;
                            p->SetupsThisSession++;

                            Sub_ShortSig[E] = sc.High[E] + 4 * TickSize;

                            if (In_Alerts.GetYesNo() && !sc.IsFullRecalculation)
                            {
                                SCString M;
                                M.Format("RLC SHORT TRIGGER  entry %s  stop %s  T1 %s  T2 %s  (R=%.2f)",
                                    sc.FormatGraphValue(p->Entry, sc.BaseGraphValueFormat).GetChars(),
                                    sc.FormatGraphValue(p->Stop,  sc.BaseGraphValueFormat).GetChars(),
                                    sc.FormatGraphValue(p->Tgt1,  sc.BaseGraphValueFormat).GetChars(),
                                    sc.FormatGraphValue(p->Tgt2,  sc.BaseGraphValueFormat).GetChars(),
                                    R);
                                sc.SetAlert(1, M);
                            }
                        }
                    }
                }
            }
        }
    } // end once-per-bar evaluation

    /*======================================================================*/
    /*  Plot the current state on every call so lines extend to the         */
    /*  right-hand edge of the chart                                        */
    /*======================================================================*/
    if (p->ProfileValid)
    {
        Sub_VAH[sc.Index] = p->VAH;
        Sub_VAL[sc.Index] = p->VAL;
        Sub_POC[sc.Index] = p->POC;
    }

    if (In_ShowPrior.GetYesNo() && p->PriorValid)
    {
        Sub_PriorVAH[sc.Index] = p->PriorVAH;
        Sub_PriorVAL[sc.Index] = p->PriorVAL;
        Sub_PriorPOC[sc.Index] = p->PriorPOC;
    }

    const bool ShowLongZone  = (p->LongZoneValid  && p->TradeDir >= 0 && In_EnableLong.GetYesNo());
    const bool ShowShortZone = (p->ShortZoneValid && p->TradeDir <= 0 && In_EnableShort.GetYesNo()
                                && !ShowLongZone);

    if (ShowLongZone)
    {
        Sub_Fib705[sc.Index] = p->LongFib1;
        Sub_Fib786[sc.Index] = p->LongFib2;
        Sub_Fib886[sc.Index] = p->LongFib3;
    }
    else if (ShowShortZone)
    {
        Sub_Fib705[sc.Index] = p->ShortFib1;
        Sub_Fib786[sc.Index] = p->ShortFib2;
        Sub_Fib886[sc.Index] = p->ShortFib3;
    }

    if (p->TradeDir != 0)
    {
        Sub_EntryLine[sc.Index] = p->Entry;
        Sub_StopLine[sc.Index]  = p->Stop;
        Sub_Target1[sc.Index]   = p->Tgt1;
        Sub_Target2[sc.Index]   = p->Tgt2;
        if (p->TrailActive)
            Sub_TrailLine[sc.Index] = p->Trail;
    }

    Sub_Structure[sc.Index]  = (float)p->Structure;
    Sub_GammaState[sc.Index] = (float)p->Gamma;
    Sub_StageOut[sc.Index]   = (float)((p->TradeDir != 0) ? RLC::STAGE_TRADE
                                     : ((p->LongStage > p->ShortStage) ? p->LongStage : p->ShortStage));

    /*======================================================================*/
    /*  Golden pocket rectangle                                              */
    /*======================================================================*/
    if (sc.Index == sc.ArraySize - 1)
    {
        if (In_DrawZone.GetYesNo() && (ShowLongZone || ShowShortZone))
        {
            s_UseTool Tool;
            Tool.Clear();
            Tool.ChartNumber       = sc.ChartNumber;
            Tool.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
            Tool.LineNumber        = LineBase + 1;
            Tool.AddMethod         = UTAM_ADD_OR_ADJUST;
            Tool.BeginIndex        = (sc.Index > 60) ? (sc.Index - 60) : 0;
            Tool.EndIndex          = sc.Index;
            Tool.BeginValue        = ShowLongZone ? p->LongZoneBot : p->ShortZoneBot;
            Tool.EndValue          = ShowLongZone ? p->LongZoneTop : p->ShortZoneTop;
            Tool.Color             = ShowLongZone ? RGB(0, 190, 110) : RGB(220, 80, 80);
            Tool.SecondaryColor    = Tool.Color;
            Tool.TransparencyLevel = 82;
            sc.UseTool(Tool);
        }
        else
        {
            sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LineBase + 1);
        }
    }

    /*======================================================================*/
    /*  Status dashboard                                                     */
    /*======================================================================*/
#if RLC_ENABLE_DASHBOARD
    if (In_ShowDash.GetYesNo() && sc.Index == sc.ArraySize - 1)
    {
        const int Stage = (p->TradeDir != 0) ? RLC::STAGE_TRADE
                        : ((p->LongStage > p->ShortStage) ? p->LongStage : p->ShortStage);

        SCString Zone;
        if (ShowLongZone)
            Zone.Format("%s - %s (discount)",
                sc.FormatGraphValue(p->LongZoneBot, sc.BaseGraphValueFormat).GetChars(),
                sc.FormatGraphValue(p->LongZoneTop, sc.BaseGraphValueFormat).GetChars());
        else if (ShowShortZone)
            Zone.Format("%s - %s (premium)",
                sc.FormatGraphValue(p->ShortZoneBot, sc.BaseGraphValueFormat).GetChars(),
                sc.FormatGraphValue(p->ShortZoneTop, sc.BaseGraphValueFormat).GetChars());
        else
            Zone = "no valid leg";

        SCString Trade;
        if (p->TradeDir != 0)
            Trade.Format("%s  entry %s  stop %s  T1 %s  T2 %s%s",
                (p->TradeDir > 0) ? "LONG" : "SHORT",
                sc.FormatGraphValue(p->Entry, sc.BaseGraphValueFormat).GetChars(),
                sc.FormatGraphValue(p->Stop,  sc.BaseGraphValueFormat).GetChars(),
                sc.FormatGraphValue(p->Tgt1,  sc.BaseGraphValueFormat).GetChars(),
                sc.FormatGraphValue(p->Tgt2,  sc.BaseGraphValueFormat).GetChars(),
                p->TrailActive ? "  [trailing]" : "");
        else
            Trade = "flat";

        SCString Text;
        Text.Format(
            "RLC FRAMEWORK\n"
            "1 ENVIRONMENT   structure: %s   gamma: %s\n"
            "2 LOCATION      POC %s   VAH %s   VAL %s\n"
            "                zone: %s\n"
            "3 CONFIRMATION  delta %.0f   imb B/S %d/%d   stacked B/S %d/%d\n"
            "4 STAGE         %s   |   %s",
            RLC::StructureText(p->Structure),
            RLC::GammaText(p->Gamma),
            p->ProfileValid ? sc.FormatGraphValue(p->POC, sc.BaseGraphValueFormat).GetChars() : "n/a",
            p->ProfileValid ? sc.FormatGraphValue(p->VAH, sc.BaseGraphValueFormat).GetChars() : "n/a",
            p->ProfileValid ? sc.FormatGraphValue(p->VAL, sc.BaseGraphValueFormat).GetChars() : "n/a",
            Zone.GetChars(),
            OFNow.Delta, OFNow.BuyImb, OFNow.SellImb, OFNow.StackBuy, OFNow.StackSell,
            RLC::StageText(Stage),
            Trade.GetChars());

        // Signature (per ACSIL documentation):
        //   sc.AddAndManageSingleTextDrawingForStudy(sc, DisplayInFillSpace,
        //       HorizontalPosition, VerticalPosition, Subgraph,
        //       TransparentLabelBackground, TextToDisplay,
        //       DrawAboveMainPriceGraph, BoldFont)
        sc.AddAndManageSingleTextDrawingForStudy(sc,
                                                 false,                 // DisplayInFillSpace
                                                 In_DashX.GetInt(),     // HorizontalPosition
                                                 In_DashY.GetInt(),     // VerticalPosition
                                                 Sub_Dashboard,         // controls color + size
                                                 0,                     // opaque background
                                                 Text,
                                                 1,                     // above the price graph
                                                 0);                    // bold font off
    }
#endif
}