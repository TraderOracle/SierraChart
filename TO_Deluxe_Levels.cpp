// ===========================================================================
//  Previous Day / Week / Month Levels  +  Session & Opening Ranges
//
//  Sierra Chart ACSIL custom study.
//
//  Draws horizontal lines with labels at the right edge for:
//      Previous Day    : High, Low, Close, VAH, VAL, POC
//      Previous Week   : High, Low, Close, VAH, VAL, POC
//      Previous Month  : High, Low, Close, VAH, VAL, POC
//      Initial Balance : High / Low of 09:30 - 09:45 (15 minutes)
//      Opening Range   : High / Low of 09:30 - 10:30 (1 hour)
//      Asian Session   : High / Low of 18:00 - 03:00 (wraps midnight)
//      London Session  : High / Low of 03:00 - 09:30
//
//  All times are chart times. Set the chart to New York (Eastern) time.
//
//  Period boundary for day / week / month defaults to 18:00:00 chart time,
//  which is 5:00 PM CST / 6:00 PM ET. The "Close" of each period is the last
//  trade printed before that boundary, and an evening session's trade date
//  rolls forward to the next calendar day (standard futures convention).
//
//  Weeks group Monday..Sunday by trade date, so the Sunday evening session
//  belongs to the following week. Months group by the trade date's month.
//
//  The four intraday ranges are computed per trade date from bars whose start
//  time falls inside the window, and are back-filled across the whole day so
//  the lines are flat from the session start. They build live as the window
//  progresses and freeze when the window closes. A window may wrap midnight
//  (start time later than end time); it is still owned by a single trade date
//  as long as it does not cross the day/week/month boundary time.
//
//  Volume profiles are built internally from the chart's Volume At Price
//  data. This study requests "Maintain Volume At Price Data" automatically;
//  reload the chart once so historical VAP data is present. Bars with no VAP
//  data fall back to spreading their volume evenly across the bar range.
//
//  Build: place in <SierraChart>\ACS_Source\ and use
//         Analysis >> Build Custom Studies DLL.
// ===========================================================================

#include "sierrachart.h"

#include <map>
#include <vector>
#include <cmath>
#include <cfloat>
#include <climits>

SCDLLName("TO Deluxe Levels")

// ---------------------------------------------------------------------------
namespace PrevPeriodLevels
{
    const int NUM_GROUPS       = 3;   // Day, Week, Month
    const int LEVELS_PER_GROUP = 6;   // High, Low, Close, VAH, VAL, POC
    const int NUM_PERIOD_SUBGRAPHS = NUM_GROUPS * LEVELS_PER_GROUP;   // 0..17

    // Intraday ranges. Each uses two subgraphs: high then low.
    const int RANGE_IB     = 0;
    const int RANGE_ORB    = 1;
    const int RANGE_ASIA   = 2;
    const int RANGE_LONDON = 3;
    const int NUM_RANGES   = 4;

    const int SG_RANGE_BASE = NUM_PERIOD_SUBGRAPHS;                   // 18
    const int NUM_SUBGRAPHS = SG_RANGE_BASE + NUM_RANGES * 2;         // 26

    // -----------------------------------------------------------------------
    struct s_Levels
    {
        double High;
        double Low;
        double Close;
        double VAH;
        double VAL;
        double POC;
        bool   Valid;

        s_Levels() { Clear(); }

        void Clear()
        {
            High = Low = Close = VAH = VAL = POC = 0.0;
            Valid = false;
        }
    };

    // -----------------------------------------------------------------------
    struct s_Range
    {
        double High;
        double Low;
        bool   HasData;

        s_Range() { Clear(); }

        void Clear()
        {
            High = 0.0;
            Low  = 0.0;
            HasData = false;
        }

        // Returns true when either extreme actually moved.
        bool Extend(double BarHigh, double BarLow)
        {
            if (!HasData)
            {
                High = BarHigh;
                Low  = BarLow;
                HasData = true;
                return true;
            }

            bool Changed = false;

            if (BarHigh > High)
            {
                High = BarHigh;
                Changed = true;
            }

            if (BarLow < Low)
            {
                Low = BarLow;
                Changed = true;
            }

            return Changed;
        }
    };

    inline bool RangesEqual(const s_Range& A, const s_Range& B)
    {
        return A.HasData == B.HasData && A.High == B.High && A.Low == B.Low;
    }

    // -----------------------------------------------------------------------
    struct s_RangeConfig
    {
        int  StartSeconds;
        int  EndSeconds;
        bool Show;

        s_RangeConfig() : StartSeconds(0), EndSeconds(0), Show(false) {}
    };

    // Handles windows that wrap past midnight (start later than end).
    static bool IsInWindow(int TimeSeconds, const s_RangeConfig& Config)
    {
        if (Config.StartSeconds == Config.EndSeconds)
            return false;

        if (Config.StartSeconds < Config.EndSeconds)
            return TimeSeconds >= Config.StartSeconds && TimeSeconds < Config.EndSeconds;

        return TimeSeconds >= Config.StartSeconds || TimeSeconds < Config.EndSeconds;
    }

    // -----------------------------------------------------------------------
    struct s_PeriodAccum
    {
        double High;
        double Low;
        double Close;
        double TotalVolume;
        bool   HasData;
        std::map<int, double> VolumeByTick;   // key = price in ticks

        s_PeriodAccum() { Clear(); }

        void Clear()
        {
            High        = -DBL_MAX;
            Low         =  DBL_MAX;
            Close       =  0.0;
            TotalVolume =  0.0;
            HasData     =  false;
            VolumeByTick.clear();
        }
    };

    // -----------------------------------------------------------------------
    struct s_State
    {
        int LastProcessedIndex;

        int DayKey;
        int WeekKey;
        int MonthKey;

        s_PeriodAccum Day;
        s_PeriodAccum Week;
        s_PeriodAccum Month;

        s_Levels PrevDay;
        s_Levels PrevWeek;
        s_Levels PrevMonth;

        // Current trade date's intraday ranges.
        int     DayStartIndex;
        s_Range Ranges[NUM_RANGES];

        // Last values written when filling the entire chart.
        s_Levels ShownDay;
        s_Levels ShownWeek;
        s_Levels ShownMonth;
        s_Range  ShownRanges[NUM_RANGES];
        bool     HaveShown;

        s_State() { Clear(); }

        void Clear()
        {
            LastProcessedIndex = -1;
            DayKey = WeekKey = MonthKey = INT_MIN;
            Day.Clear();
            Week.Clear();
            Month.Clear();
            PrevDay.Clear();
            PrevWeek.Clear();
            PrevMonth.Clear();
            DayStartIndex = 0;
            ShownDay.Clear();
            ShownWeek.Clear();
            ShownMonth.Clear();
            HaveShown = false;

            for (int i = 0; i < NUM_RANGES; ++i)
            {
                Ranges[i].Clear();
                ShownRanges[i].Clear();
            }
        }

        void ClearRanges()
        {
            for (int i = 0; i < NUM_RANGES; ++i)
                Ranges[i].Clear();
        }
    };

    // -----------------------------------------------------------------------
    inline int RoundToInt(double Value)
    {
        return (int)floor(Value + 0.5);
    }

    inline int GroupTick(int PriceInTicks, int TicksPerLevel)
    {
        if (TicksPerLevel <= 1)
            return PriceInTicks;

        const double Divided = (double)PriceInTicks / (double)TicksPerLevel;
        return (int)floor(Divided) * TicksPerLevel;
    }

    // -----------------------------------------------------------------------
    // Seconds-from-midnight of the START of a bar.
    // -----------------------------------------------------------------------
    static int GetBarStartTimeSeconds(SCStudyInterfaceRef sc,
                                      int BarIndex,
                                      bool TimestampsAreBarEnd)
    {
        int TimeSeconds = sc.BaseDateTimeIn[BarIndex].GetTimeInSeconds();

        if (TimestampsAreBarEnd && sc.SecondsPerBar > 0)
        {
            TimeSeconds -= sc.SecondsPerBar;
            while (TimeSeconds < 0)
                TimeSeconds += SECONDS_PER_DAY;
        }

        return TimeSeconds;
    }

    // -----------------------------------------------------------------------
    // Determine the day / week / month grouping keys for a bar.
    // SessionStartSeconds is seconds from midnight of the session boundary.
    // -----------------------------------------------------------------------
    static void GetPeriodKeys(SCStudyInterfaceRef sc,
                              int BarIndex,
                              int SessionStartSeconds,
                              int& r_DayKey,
                              int& r_WeekKey,
                              int& r_MonthKey)
    {
        const SCDateTime BarDateTime = sc.BaseDateTimeIn[BarIndex];

        // Shift the bar back so the session boundary becomes midnight.
        const SCDateTime Shifted = BarDateTime - SCDateTime::SECONDS(SessionStartSeconds);

        const int SessionStartDate = Shifted.GetDate();

        // Evening sessions carry the next calendar day as their trade date.
        int TradeDate = SessionStartDate;
        if (SessionStartSeconds >= 12 * SECONDS_PER_HOUR)
            TradeDate = SessionStartDate + 1;

        const SCDateTime TradeDateTime = SCDateTime::DAYS(TradeDate);

        // Monday = 0 ... Sunday = 6
        const int DayOfWeek = (TradeDateTime.GetDayOfWeek() + 6) % 7;

        r_DayKey   = TradeDate;
        r_WeekKey  = TradeDate - DayOfWeek;
        r_MonthKey = TradeDateTime.GetYear() * 12 + TradeDateTime.GetMonth();
    }

    // -----------------------------------------------------------------------
    // Add one bar's high / low / close and volume-at-price into an accumulator.
    // -----------------------------------------------------------------------
    static void AccumulateBar(SCStudyInterfaceRef sc,
                              int BarIndex,
                              s_PeriodAccum& Accum,
                              bool UseVAPData,
                              int TicksPerLevel,
                              double TickSize)
    {
        const double BarHigh  = sc.High[BarIndex];
        const double BarLow   = sc.Low[BarIndex];
        const double BarClose = sc.Close[BarIndex];

        if (!Accum.HasData)
        {
            Accum.High    = BarHigh;
            Accum.Low     = BarLow;
            Accum.HasData = true;
        }
        else
        {
            if (BarHigh > Accum.High)
                Accum.High = BarHigh;
            if (BarLow < Accum.Low)
                Accum.Low = BarLow;
        }

        Accum.Close = BarClose;

        bool AddedVAP = false;

        if (UseVAPData && sc.VolumeAtPriceForBars != NULL)
        {
            const int VAPSizeAtBar = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(BarIndex);

            for (int VAPIndex = 0; VAPIndex < VAPSizeAtBar; ++VAPIndex)
            {
                s_VolumeAtPriceV2* p_VAP = NULL;

                if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(BarIndex, VAPIndex, &p_VAP))
                    break;

                if (p_VAP == NULL)
                    continue;

                const double Volume = (double)p_VAP->Volume;
                if (Volume <= 0.0)
                    continue;

                const int Tick = GroupTick(p_VAP->PriceInTicks, TicksPerLevel);

                Accum.VolumeByTick[Tick] += Volume;
                Accum.TotalVolume        += Volume;
                AddedVAP = true;
            }
        }

        if (!AddedVAP)
        {
            // Fallback: spread the bar's volume evenly over its price range.
            const int LowTick  = RoundToInt(BarLow  / TickSize);
            const int HighTick = RoundToInt(BarHigh / TickSize);

            int LevelCount = HighTick - LowTick + 1;
            if (LevelCount < 1)
                LevelCount = 1;

            double BarVolume = sc.Volume[BarIndex];
            if (BarVolume <= 0.0)
                BarVolume = 1.0;   // treat as a TPO count when no volume exists

            const double VolumePerLevel = BarVolume / (double)LevelCount;

            for (int RawTick = LowTick; RawTick <= HighTick; ++RawTick)
            {
                const int Tick = GroupTick(RawTick, TicksPerLevel);
                Accum.VolumeByTick[Tick] += VolumePerLevel;
                Accum.TotalVolume        += VolumePerLevel;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Standard expanding value area calculation around the POC.
    // -----------------------------------------------------------------------
    static void ComputeProfile(const std::map<int, double>& VolumeByTick,
                               double TotalVolume,
                               double ValueAreaPercent,
                               double TickSize,
                               double& r_POC,
                               double& r_VAH,
                               double& r_VAL)
    {
        r_POC = r_VAH = r_VAL = 0.0;

        if (VolumeByTick.empty() || TotalVolume <= 0.0)
            return;

        std::vector< std::pair<int, double> > Rows(VolumeByTick.begin(), VolumeByTick.end());
        const int RowCount = (int)Rows.size();

        // POC = highest volume row. Ties resolve toward the middle of the range.
        double MaxVolume = -1.0;
        for (int i = 0; i < RowCount; ++i)
        {
            if (Rows[i].second > MaxVolume)
                MaxVolume = Rows[i].second;
        }

        const double MiddleIndex = (RowCount - 1) / 2.0;
        int POCIndex = 0;
        double BestDistance = DBL_MAX;

        for (int i = 0; i < RowCount; ++i)
        {
            if (Rows[i].second < MaxVolume)
                continue;

            const double Distance = fabs((double)i - MiddleIndex);
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                POCIndex = i;
            }
        }

        double TargetVolume = TotalVolume * (ValueAreaPercent / 100.0);
        if (TargetVolume <= 0.0)
            TargetVolume = TotalVolume;

        double RunningVolume = Rows[POCIndex].second;

        int UpIndex   = POCIndex + 1;
        int DownIndex = POCIndex - 1;
        int HighIndex = POCIndex;
        int LowIndex  = POCIndex;

        while (RunningVolume < TargetVolume && (UpIndex < RowCount || DownIndex >= 0))
        {
            double UpSum = 0.0;
            int UpCount = 0;
            for (int i = UpIndex; i < UpIndex + 2 && i < RowCount; ++i)
            {
                UpSum += Rows[i].second;
                ++UpCount;
            }

            double DownSum = 0.0;
            int DownCount = 0;
            for (int i = DownIndex; i > DownIndex - 2 && i >= 0; --i)
            {
                DownSum += Rows[i].second;
                ++DownCount;
            }

            if (UpCount == 0 && DownCount == 0)
                break;

            const bool TakeUp = (DownCount == 0) || (UpCount > 0 && UpSum >= DownSum);

            if (TakeUp)
            {
                RunningVolume += UpSum;
                HighIndex = UpIndex + UpCount - 1;
                UpIndex  += UpCount;
            }
            else
            {
                RunningVolume += DownSum;
                LowIndex  = DownIndex - DownCount + 1;
                DownIndex -= DownCount;
            }
        }

        r_POC = Rows[POCIndex].first  * TickSize;
        r_VAH = Rows[HighIndex].first * TickSize;
        r_VAL = Rows[LowIndex].first  * TickSize;
    }

    // -----------------------------------------------------------------------
    static s_Levels FinalizeLevels(const s_PeriodAccum& Accum,
                                   double ValueAreaPercent,
                                   double TickSize)
    {
        s_Levels Levels;

        if (!Accum.HasData)
            return Levels;

        Levels.High  = Accum.High;
        Levels.Low   = Accum.Low;
        Levels.Close = Accum.Close;

        ComputeProfile(Accum.VolumeByTick,
                       Accum.TotalVolume,
                       ValueAreaPercent,
                       TickSize,
                       Levels.POC,
                       Levels.VAH,
                       Levels.VAL);

        Levels.Valid = true;
        return Levels;
    }

    // -----------------------------------------------------------------------
    static bool LevelsEqual(const s_Levels& A, const s_Levels& B)
    {
        return A.Valid == B.Valid
            && A.High  == B.High
            && A.Low   == B.Low
            && A.Close == B.Close
            && A.VAH   == B.VAH
            && A.VAL   == B.VAL
            && A.POC   == B.POC;
    }

    // -----------------------------------------------------------------------
    // Write one bar's worth of previous-period values into subgraphs 0..17.
    // Hidden levels are written as zero and are not drawn (DrawZeros = false).
    // -----------------------------------------------------------------------
    static void SetPeriodValues(SCStudyInterfaceRef sc,
                                int BarIndex,
                                const s_Levels& DayLevels,
                                const s_Levels& WeekLevels,
                                const s_Levels& MonthLevels,
                                const bool* p_GroupEnabled,
                                bool ShowHighLowClose,
                                bool ShowValueArea)
    {
        const s_Levels* p_Groups[NUM_GROUPS] = { &DayLevels, &WeekLevels, &MonthLevels };

        for (int Group = 0; Group < NUM_GROUPS; ++Group)
        {
            const s_Levels& Levels = *p_Groups[Group];
            const int BaseSubgraph = Group * LEVELS_PER_GROUP;

            const double Values[LEVELS_PER_GROUP] =
            {
                Levels.High, Levels.Low, Levels.Close,
                Levels.VAH,  Levels.VAL, Levels.POC
            };

            for (int Slot = 0; Slot < LEVELS_PER_GROUP; ++Slot)
            {
                const bool SlotEnabled = (Slot < 3) ? ShowHighLowClose : ShowValueArea;

                double Value = 0.0;
                if (Levels.Valid && p_GroupEnabled[Group] && SlotEnabled)
                    Value = Values[Slot];

                sc.Subgraph[BaseSubgraph + Slot][BarIndex] = Value;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Write the intraday range values into subgraphs 18..25.
    // -----------------------------------------------------------------------
    static void SetRangeValues(SCStudyInterfaceRef sc,
                               int BarIndex,
                               const s_Range* p_Ranges,
                               const s_RangeConfig* p_Configs)
    {
        for (int RangeIndex = 0; RangeIndex < NUM_RANGES; ++RangeIndex)
        {
            const bool Draw = p_Configs[RangeIndex].Show && p_Ranges[RangeIndex].HasData;
            const int  Base = SG_RANGE_BASE + RangeIndex * 2;

            sc.Subgraph[Base][BarIndex]     = Draw ? p_Ranges[RangeIndex].High : 0.0;
            sc.Subgraph[Base + 1][BarIndex] = Draw ? p_Ranges[RangeIndex].Low  : 0.0;
        }
    }

    static void BackfillRangeValues(SCStudyInterfaceRef sc,
                                    int FirstIndex,
                                    int LastIndex,
                                    const s_Range* p_Ranges,
                                    const s_RangeConfig* p_Configs)
    {
        if (FirstIndex < 0)
            FirstIndex = 0;

        for (int BarIndex = FirstIndex; BarIndex <= LastIndex; ++BarIndex)
            SetRangeValues(sc, BarIndex, p_Ranges, p_Configs);
    }

} // namespace PrevPeriodLevels

// ===========================================================================
SCSFExport scsf_TODeluxeLevels(SCStudyInterfaceRef sc)
{
    using namespace PrevPeriodLevels;

    SCInputRef Input_SessionStartTime  = sc.Input[0];
    SCInputRef Input_ValueAreaPercent  = sc.Input[1];
    SCInputRef Input_ShowDay           = sc.Input[2];
    SCInputRef Input_ShowWeek          = sc.Input[3];
    SCInputRef Input_ShowMonth         = sc.Input[4];
    SCInputRef Input_ShowHighLowClose  = sc.Input[5];
    SCInputRef Input_ShowValueArea     = sc.Input[6];
    SCInputRef Input_UseVAPData        = sc.Input[7];
    SCInputRef Input_TicksPerLevel     = sc.Input[8];
    SCInputRef Input_LatestOnly        = sc.Input[9];
    SCInputRef Input_ShowIB            = sc.Input[10];
    SCInputRef Input_ShowORB           = sc.Input[11];
    SCInputRef Input_OpenTime          = sc.Input[12];
    SCInputRef Input_IBEndTime         = sc.Input[13];
    SCInputRef Input_ORBEndTime        = sc.Input[14];
    SCInputRef Input_BarEndTimestamps  = sc.Input[15];
    SCInputRef Input_ShowAsia          = sc.Input[16];
    SCInputRef Input_AsiaStartTime     = sc.Input[17];
    SCInputRef Input_AsiaEndTime       = sc.Input[18];
    SCInputRef Input_ShowLondon        = sc.Input[19];
    SCInputRef Input_LondonStartTime   = sc.Input[20];
    SCInputRef Input_LondonEndTime     = sc.Input[21];

    // -----------------------------------------------------------------------
    if (sc.SetDefaults)
    {
        sc.GraphName = "TO Deluxe Levels";
        sc.GraphRegion = 0;
        sc.AutoLoop = 0;                    // manual looping
        sc.ValueFormat = VALUEFORMAT_INHERITED;
        sc.MaintainVolumeAtPriceData = 1;   // required for the profiles
        sc.CalculationPrecedence = LOW_PREC_LEVEL;
        sc.DrawStudyUnderneathMainPriceGraph = 1;

        const char* SubgraphNames[NUM_SUBGRAPHS] =
        {
            "PDH",  "PDL",  "PDC",  "PD VAH", "PD VAL", "PD POC",
            "PWH",  "PWL",  "PWC",  "PW VAH", "PW VAL", "PW POC",
            "PMH",  "PML",  "PMC",  "PM VAH", "PM VAL", "PM POC",
            "15m High",  "15m Low",
            "1H High", "1H Low",
            "Asia High", "Asia Low",
            "London High",  "London Low"
        };

        const COLORREF SubgraphColors[NUM_SUBGRAPHS] =
        {
            RGB(  0, 200, 255), RGB(  0, 200, 255), RGB(120, 200, 255),   // day HLC
            RGB(  0, 160, 200), RGB(  0, 160, 200), RGB(  0, 230, 230),   // day VA
            RGB(255, 170,   0), RGB(255, 170,   0), RGB(255, 210, 120),   // week HLC
            RGB(210, 140,   0), RGB(210, 140,   0), RGB(255, 200,   0),   // week VA
            RGB(255, 100, 255), RGB(255, 100, 255), RGB(255, 170, 255),   // month HLC
            RGB(200,  80, 200), RGB(200,  80, 200), RGB(240,  60, 240),   // month VA
            RGB(120, 255, 120), RGB(120, 255, 120),                       // IB
            RGB(255, 255, 255), RGB(255, 255, 255),                       // ORB
            RGB(160, 160, 255), RGB(160, 160, 255),                       // Asia
            RGB(255, 140, 140), RGB(255, 140, 140)                        // London
        };

        for (int Index = 0; Index < NUM_SUBGRAPHS; ++Index)
        {
            sc.Subgraph[Index].Name = SubgraphNames[Index];
            sc.Subgraph[Index].PrimaryColor = SubgraphColors[Index];
            sc.Subgraph[Index].LineWidth = 1;
            sc.Subgraph[Index].DrawZeros = false;

            if (Index < NUM_PERIOD_SUBGRAPHS)
            {
                const int Slot = Index % LEVELS_PER_GROUP;
                sc.Subgraph[Index].DrawStyle = (Slot < 3) ? DRAWSTYLE_LINE : DRAWSTYLE_DASH;
            }
            else
            {
                sc.Subgraph[Index].DrawStyle = DRAWSTYLE_LINE;
            }

            sc.Subgraph[Index].LineLabel =
                  LL_DISPLAY_NAME
                | LL_NAME_ALIGN_FAR_RIGHT
                | LL_NAME_ALIGN_ABOVE
                | LL_DISPLAY_VALUE
                | LL_VALUE_ALIGN_VALUES_SCALE;
        }

        Input_SessionStartTime.Name = "Day/Week/Month Boundary Time (Chart Time)";
        Input_SessionStartTime.SetTime(HMS_TIME(18, 0, 0));

        Input_ValueAreaPercent.Name = "Value Area Percent";
        Input_ValueAreaPercent.SetFloat(70.0f);
        Input_ValueAreaPercent.SetFloatLimits(1.0f, 100.0f);

        Input_ShowDay.Name = "Show Previous Day Levels";
        Input_ShowDay.SetYesNo(true);

        Input_ShowWeek.Name = "Show Previous Week Levels";
        Input_ShowWeek.SetYesNo(true);

        Input_ShowMonth.Name = "Show Previous Month Levels";
        Input_ShowMonth.SetYesNo(true);

        Input_ShowHighLowClose.Name = "Show High / Low / Close";
        Input_ShowHighLowClose.SetYesNo(true);

        Input_ShowValueArea.Name = "Show VAH / VAL / POC";
        Input_ShowValueArea.SetYesNo(true);

        Input_UseVAPData.Name = "Use Volume At Price Data When Available";
        Input_UseVAPData.SetYesNo(true);

        Input_TicksPerLevel.Name = "Ticks Per Profile Price Level";
        Input_TicksPerLevel.SetInt(1);
        Input_TicksPerLevel.SetIntLimits(1, 1000);

        Input_LatestOnly.Name = "Draw Only Most Recent Values Across Entire Chart";
        Input_LatestOnly.SetYesNo(false);

        Input_ShowIB.Name = "Show 15 Minute Initial Balance (IBH / IBL)";
        Input_ShowIB.SetYesNo(true);

        Input_ShowORB.Name = "Show 1 Hour Opening Range (ORBH / ORBL)";
        Input_ShowORB.SetYesNo(true);

        Input_OpenTime.Name = "Opening Range Start Time (Chart Time)";
        Input_OpenTime.SetTime(HMS_TIME(9, 30, 0));

        Input_IBEndTime.Name = "Initial Balance End Time (Chart Time)";
        Input_IBEndTime.SetTime(HMS_TIME(9, 45, 0));

        Input_ORBEndTime.Name = "Opening Range End Time (Chart Time)";
        Input_ORBEndTime.SetTime(HMS_TIME(10, 30, 0));

        Input_BarEndTimestamps.Name = "Bars Are Timestamped At Bar End";
        Input_BarEndTimestamps.SetYesNo(false);

        Input_ShowAsia.Name = "Show Asian Session High / Low";
        Input_ShowAsia.SetYesNo(true);

        Input_AsiaStartTime.Name = "Asian Session Start Time (Chart Time)";
        Input_AsiaStartTime.SetTime(HMS_TIME(18, 0, 0));

        Input_AsiaEndTime.Name = "Asian Session End Time (Chart Time)";
        Input_AsiaEndTime.SetTime(HMS_TIME(3, 0, 0));

        Input_ShowLondon.Name = "Show London Session High / Low";
        Input_ShowLondon.SetYesNo(true);

        Input_LondonStartTime.Name = "London Session Start Time (Chart Time)";
        Input_LondonStartTime.SetTime(HMS_TIME(3, 0, 0));

        Input_LondonEndTime.Name = "London Session End Time (Chart Time)";
        Input_LondonEndTime.SetTime(HMS_TIME(9, 30, 0));

        return;
    }

    // -----------------------------------------------------------------------
    // Persistent state
    // -----------------------------------------------------------------------
    s_State* p_State = (s_State*)sc.GetPersistentPointer(1);

    if (sc.LastCallToFunction)
    {
        if (p_State != NULL)
        {
            delete p_State;
            sc.SetPersistentPointer(1, NULL);
        }
        return;
    }

    if (p_State == NULL)
    {
        p_State = new s_State();
        if (p_State == NULL)
            return;
        sc.SetPersistentPointer(1, p_State);
    }

    s_State& State = *p_State;

    if (sc.ArraySize < 1)
        return;

    // -----------------------------------------------------------------------
    const int    SessionStartSeconds = Input_SessionStartTime.GetTime();
    const double ValueAreaPercent    = Input_ValueAreaPercent.GetFloat();
    const bool   UseVAPData          = Input_UseVAPData.GetYesNo() != 0;
    const int    TicksPerLevel       = Input_TicksPerLevel.GetInt();
    const bool   ShowHighLowClose    = Input_ShowHighLowClose.GetYesNo() != 0;
    const bool   ShowValueArea       = Input_ShowValueArea.GetYesNo() != 0;
    const bool   LatestOnly          = Input_LatestOnly.GetYesNo() != 0;
    const bool   BarEndTimestamps    = Input_BarEndTimestamps.GetYesNo() != 0;
    const double TickSize            = (sc.TickSize > 0.0) ? sc.TickSize : 0.01;

    bool GroupEnabled[NUM_GROUPS];
    GroupEnabled[0] = Input_ShowDay.GetYesNo()   != 0;
    GroupEnabled[1] = Input_ShowWeek.GetYesNo()  != 0;
    GroupEnabled[2] = Input_ShowMonth.GetYesNo() != 0;

    s_RangeConfig RangeConfigs[NUM_RANGES];

    RangeConfigs[RANGE_IB].StartSeconds = Input_OpenTime.GetTime();
    RangeConfigs[RANGE_IB].EndSeconds   = Input_IBEndTime.GetTime();
    RangeConfigs[RANGE_IB].Show         = Input_ShowIB.GetYesNo() != 0;

    RangeConfigs[RANGE_ORB].StartSeconds = Input_OpenTime.GetTime();
    RangeConfigs[RANGE_ORB].EndSeconds   = Input_ORBEndTime.GetTime();
    RangeConfigs[RANGE_ORB].Show         = Input_ShowORB.GetYesNo() != 0;

    RangeConfigs[RANGE_ASIA].StartSeconds = Input_AsiaStartTime.GetTime();
    RangeConfigs[RANGE_ASIA].EndSeconds   = Input_AsiaEndTime.GetTime();
    RangeConfigs[RANGE_ASIA].Show         = Input_ShowAsia.GetYesNo() != 0;

    RangeConfigs[RANGE_LONDON].StartSeconds = Input_LondonStartTime.GetTime();
    RangeConfigs[RANGE_LONDON].EndSeconds   = Input_LondonEndTime.GetTime();
    RangeConfigs[RANGE_LONDON].Show         = Input_ShowLondon.GetYesNo() != 0;

    // Restart from scratch on a full recalculation or any backwards update.
    if (sc.UpdateStartIndex <= State.LastProcessedIndex)
        State.Clear();

    // -----------------------------------------------------------------------
    // Process every completed bar. The final bar is still forming, so it is
    // never committed; it is handled provisionally below.
    // -----------------------------------------------------------------------
    const int LastBarIndex    = sc.ArraySize - 1;
    const int ProcessEndIndex = LastBarIndex - 1;

    for (int BarIndex = State.LastProcessedIndex + 1; BarIndex <= ProcessEndIndex; ++BarIndex)
    {
        int DayKey = 0, WeekKey = 0, MonthKey = 0;
        GetPeriodKeys(sc, BarIndex, SessionStartSeconds, DayKey, WeekKey, MonthKey);

        // --- roll the completed periods ---
        if (DayKey != State.DayKey)
        {
            if (State.DayKey != INT_MIN && State.Day.HasData)
                State.PrevDay = FinalizeLevels(State.Day, ValueAreaPercent, TickSize);

            State.Day.Clear();
            State.DayKey = DayKey;

            // A new trade date restarts every intraday range.
            State.ClearRanges();
            State.DayStartIndex = BarIndex;
        }

        if (WeekKey != State.WeekKey)
        {
            if (State.WeekKey != INT_MIN && State.Week.HasData)
                State.PrevWeek = FinalizeLevels(State.Week, ValueAreaPercent, TickSize);

            State.Week.Clear();
            State.WeekKey = WeekKey;
        }

        if (MonthKey != State.MonthKey)
        {
            if (State.MonthKey != INT_MIN && State.Month.HasData)
                State.PrevMonth = FinalizeLevels(State.Month, ValueAreaPercent, TickSize);

            State.Month.Clear();
            State.MonthKey = MonthKey;
        }

        // --- previous-period values that apply to this bar ---
        SetPeriodValues(sc, BarIndex,
                        State.PrevDay, State.PrevWeek, State.PrevMonth,
                        GroupEnabled, ShowHighLowClose, ShowValueArea);

        // --- intraday ranges for this trade date ---
        const int BarTimeSeconds = GetBarStartTimeSeconds(sc, BarIndex, BarEndTimestamps);
        bool RangeChanged = false;

        for (int RangeIndex = 0; RangeIndex < NUM_RANGES; ++RangeIndex)
        {
            if (!IsInWindow(BarTimeSeconds, RangeConfigs[RangeIndex]))
                continue;

            if (State.Ranges[RangeIndex].Extend(sc.High[BarIndex], sc.Low[BarIndex]))
                RangeChanged = true;
        }

        SetRangeValues(sc, BarIndex, State.Ranges, RangeConfigs);

        // Keep the lines flat from the start of the trade date.
        if (RangeChanged && BarIndex > State.DayStartIndex)
        {
            BackfillRangeValues(sc, State.DayStartIndex, BarIndex - 1,
                                State.Ranges, RangeConfigs);
        }

        // --- accumulate this bar into the in-progress periods ---
        AccumulateBar(sc, BarIndex, State.Day,   UseVAPData, TicksPerLevel, TickSize);
        AccumulateBar(sc, BarIndex, State.Week,  UseVAPData, TicksPerLevel, TickSize);
        AccumulateBar(sc, BarIndex, State.Month, UseVAPData, TicksPerLevel, TickSize);

        State.LastProcessedIndex = BarIndex;
    }

    // -----------------------------------------------------------------------
    // The forming bar: detect a period roll and extend the intraday ranges
    // without committing any state, so everything is current on every tick.
    // -----------------------------------------------------------------------
    s_Levels DisplayDay   = State.PrevDay;
    s_Levels DisplayWeek  = State.PrevWeek;
    s_Levels DisplayMonth = State.PrevMonth;

    s_Range DisplayRanges[NUM_RANGES];
    for (int RangeIndex = 0; RangeIndex < NUM_RANGES; ++RangeIndex)
        DisplayRanges[RangeIndex] = State.Ranges[RangeIndex];

    int RangeStartIndex = State.DayStartIndex;

    {
        int DayKey = 0, WeekKey = 0, MonthKey = 0;
        GetPeriodKeys(sc, LastBarIndex, SessionStartSeconds, DayKey, WeekKey, MonthKey);

        if (DayKey != State.DayKey)
        {
            if (State.DayKey != INT_MIN && State.Day.HasData)
                DisplayDay = FinalizeLevels(State.Day, ValueAreaPercent, TickSize);

            for (int RangeIndex = 0; RangeIndex < NUM_RANGES; ++RangeIndex)
                DisplayRanges[RangeIndex].Clear();

            RangeStartIndex = LastBarIndex;
        }

        if (WeekKey != State.WeekKey && State.WeekKey != INT_MIN && State.Week.HasData)
            DisplayWeek = FinalizeLevels(State.Week, ValueAreaPercent, TickSize);

        if (MonthKey != State.MonthKey && State.MonthKey != INT_MIN && State.Month.HasData)
            DisplayMonth = FinalizeLevels(State.Month, ValueAreaPercent, TickSize);

        const int BarTimeSeconds = GetBarStartTimeSeconds(sc, LastBarIndex, BarEndTimestamps);

        for (int RangeIndex = 0; RangeIndex < NUM_RANGES; ++RangeIndex)
        {
            if (IsInWindow(BarTimeSeconds, RangeConfigs[RangeIndex]))
                DisplayRanges[RangeIndex].Extend(sc.High[LastBarIndex], sc.Low[LastBarIndex]);
        }
    }

    SetPeriodValues(sc, LastBarIndex,
                    DisplayDay, DisplayWeek, DisplayMonth,
                    GroupEnabled, ShowHighLowClose, ShowValueArea);

    SetRangeValues(sc, LastBarIndex, DisplayRanges, RangeConfigs);

    {
        bool RangeChanged = false;
        for (int RangeIndex = 0; RangeIndex < NUM_RANGES; ++RangeIndex)
        {
            if (!RangesEqual(DisplayRanges[RangeIndex], State.Ranges[RangeIndex]))
            {
                RangeChanged = true;
                break;
            }
        }

        if (RangeChanged && LastBarIndex > RangeStartIndex)
        {
            BackfillRangeValues(sc, RangeStartIndex, LastBarIndex - 1,
                                DisplayRanges, RangeConfigs);
        }
    }

    // -----------------------------------------------------------------------
    // Optional: one continuous line per level across the whole chart using
    // only the most recent values. Refilled only when something changes.
    // -----------------------------------------------------------------------
    if (LatestOnly)
    {
        bool Changed = !State.HaveShown
                    || !LevelsEqual(State.ShownDay,   DisplayDay)
                    || !LevelsEqual(State.ShownWeek,  DisplayWeek)
                    || !LevelsEqual(State.ShownMonth, DisplayMonth);

        if (!Changed)
        {
            for (int RangeIndex = 0; RangeIndex < NUM_RANGES; ++RangeIndex)
            {
                if (!RangesEqual(State.ShownRanges[RangeIndex], DisplayRanges[RangeIndex]))
                {
                    Changed = true;
                    break;
                }
            }
        }

        if (Changed)
        {
            for (int BarIndex = 0; BarIndex < sc.ArraySize; ++BarIndex)
            {
                SetPeriodValues(sc, BarIndex,
                                DisplayDay, DisplayWeek, DisplayMonth,
                                GroupEnabled, ShowHighLowClose, ShowValueArea);

                SetRangeValues(sc, BarIndex, DisplayRanges, RangeConfigs);
            }

            State.ShownDay   = DisplayDay;
            State.ShownWeek  = DisplayWeek;
            State.ShownMonth = DisplayMonth;

            for (int RangeIndex = 0; RangeIndex < NUM_RANGES; ++RangeIndex)
                State.ShownRanges[RangeIndex] = DisplayRanges[RangeIndex];

            State.HaveShown  = true;
        }
    }
    else if (State.HaveShown)
    {
        State.HaveShown = false;
    }
}