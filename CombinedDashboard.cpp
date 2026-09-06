#include "sierrachart.h"

#include <fstream>
#include <string>
#include <map>

SCDLLName("Combined Chart Dashboard")

//============================================================================
// COMBINED CHART DASHBOARD
//
// One study replacing nine. Nine lines of text pinned to the upper-left of
// the chart, one per original study, all updating live:
//
//   0  QUOTE          symbol, last, bid/ask, bar count      (FourLineChartText)
//   1  MIDNIGHT       midnight close, distance, relation    (MidnightLevel)
//   2  SUNDAY OPEN    5pm Sunday open, distance, relation   (SundayOpenLevel)
//   3  PREV CLOSE     previous 5pm close, distance, trend   (PrevSessionCloseTracker)
//   4  1ST HOUR       first-hour direction, green/red day   (FirstHourTrend)
//   5  OPENING RANGE  which extreme formed first            (OneHourOR_HighLow_First)
//   6  DAY BIAS       historical green/red % for the day    (DayOfWeekBias)
//   7  TIME SLOT      value for the current slot from CSV   (TimeSlotValue)
//   8  INSIDE BAR     inside-bar scan across Daily charts   (InsideBarScanner)
//
// Each line can be switched off individually; a hidden line collapses so the
// remaining ones close up rather than leaving a gap.
//
// SETUP
//   Apply to an INTRADAY chart with the timezone set to Central, so 08:30 is
//   the NY open and 17:00 is the 5pm settlement / evening reopen.
//   Load at least 7 days of history so the Sunday open is reachable on Friday.
//   For the inside-bar line, open one Daily chart per symbol in this
//   Chartbook (they can be hidden) and set the "IB Chart 1..12" inputs.
//
//----------------------------------------------------------------------------
// WHAT WAS SHARED
//
// The nine originals each carried their own copy of the same plumbing. Here
// there is exactly one of each:
//
//   SetTextLine()        one text renderer for all nine lines, with the color
//                        folded into the cache key so a red/green flip on
//                        identical text still redraws
//   DrawLevel()          one horizontal-line + right-hand-label renderer,
//                        used by both the midnight and Sunday levels
//   FindBoundaryIndex()  one backwards session-boundary search replacing the
//                        four near-identical finders. Mode and an optional
//                        required weekday cover every case:
//                          midnight   -> window at 00:00, date change
//                          Sunday     -> window at 17:00, date change, Sunday
//                          RTH open   -> window at 08:30, window entry
//                          prev close -> window at 17:00, window entry, -1 bar
//   HHMMToMinutes / MinutesOfDay / AbsValue / DayOfWeekFromDate /
//   TicksBetween / FormatSigned   one copy each
//============================================================================

// If sc.GetChartName() does not exist in your Sierra Chart version, change
// this to 0 and the inside-bar line falls back to "Chart #N" labels.
#define HAVE_GET_CHART_NAME 1

namespace {

	//========================================================================
	// LAYOUT CONSTANTS
	//========================================================================

	const int NUM_LINES   = 9;    // one status line per original study
	const int MAX_SYMBOLS = 12;   // inside-bar scanner chart slots

	const int LINE_NUM_BASE = 99000;   // drawing IDs: base + 0 .. base + 8

	const int MID_LEVEL_LINE_NUM = LINE_NUM_BASE + 50;
	const int MID_LABEL_LINE_NUM = LINE_NUM_BASE + 51;
	const int SUN_LEVEL_LINE_NUM = LINE_NUM_BASE + 52;
	const int SUN_LABEL_LINE_NUM = LINE_NUM_BASE + 53;

	// Persistent SCString slots: 0..8 are the text-line caches.
	const int MID_LEVEL_CACHE_IX = NUM_LINES + 0;   //  9
	const int MID_LABEL_CACHE_IX = NUM_LINES + 1;   // 10
	const int SUN_LEVEL_CACHE_IX = NUM_LINES + 2;   // 11
	const int SUN_LABEL_CACHE_IX = NUM_LINES + 3;   // 12
	const int IB_TEXT_CACHE_IX   = NUM_LINES + 4;   // 13 - last scan result
	const int NUM_CACHE_SLOTS    = NUM_LINES + 5;

	// Persistent ints
	const int PI_IB_SCAN_BARCOUNT = 0;
	const int PI_IB_HIT_COUNT     = 1;

	//========================================================================
	// INPUTS
	//========================================================================

	enum InputIndex
	{
		// ---- shared display ----
		IN_HORIZ_POS = 0,
		IN_VERT_POS,
		IN_LINE_SPACING,
		IN_FONT_SIZE,
		IN_BOLD,
		IN_TRANSPARENT_BG,
		IN_GREEN_COLOR,
		IN_RED_COLOR,
		IN_FLAT_COLOR,
		IN_INFO_COLOR,

		// ---- shared session times ----
		IN_RTH_OPEN_HHMM,
		IN_RTH_CLOSE_HHMM,
		IN_EVENING_OPEN_HHMM,
		IN_DETECT_WINDOW_MINUTES,

		// ---- line visibility ----
		IN_SHOW_QUOTE,
		IN_SHOW_MIDNIGHT,
		IN_SHOW_SUNDAY,
		IN_SHOW_PREV_CLOSE,
		IN_SHOW_FIRST_HOUR,
		IN_SHOW_OR_SEQUENCE,
		IN_SHOW_DAY_BIAS,
		IN_SHOW_TIME_SLOT,
		IN_SHOW_INSIDE_BAR,

		// ---- level lines (midnight + Sunday) ----
		IN_DRAW_LEVEL_LINES,
		IN_LEVEL_LINE_WIDTH,
		IN_LEVEL_DASHED,
		IN_MIDNIGHT_LABEL,
		IN_SUNDAY_LABEL,
		IN_REQUIRE_SUNDAY,
		IN_USE_COMPLETED_BAR,
		IN_INVERT_LEVEL_COLORS,

		// ---- previous session close ----
		IN_PREV_CLOSE_ONLY_RTH,
		IN_TREND_LOOKBACK,

		// ---- first hour / opening range ----
		IN_WINDOW_MINUTES,
		IN_MIN_TICKS,
		IN_RESOLVE_SAME_BAR,

		// ---- day of week bias ----
		IN_MON_GREEN,
		IN_TUE_GREEN,
		IN_WED_GREEN,
		IN_THU_GREEN,
		IN_FRI_GREEN,
		IN_DOW_ROLL_EVENING,
		IN_DOW_USE_SYSTEM_DATE,

		// ---- time slot value ----
		IN_SLOT_FILE_PATH,
		IN_SLOT_SUBTRACT_MINUTES,
		IN_SLOT_MINUTES,
		IN_SLOT_USE_SYSTEM_CLOCK,
		IN_SLOT_SHOW_NEXT,
		IN_SLOT_RELOAD,
		IN_SLOT_RED_THRESHOLD,
		IN_SLOT_GREY_THRESHOLD,
		IN_SLOT_GREY_COLOR,

		// ---- inside bar scanner ----
		IN_IB_STRICT,
		IN_IB_SKIP_TODAY,
		IN_IB_MAX_NAMES,
		IN_IB_CHART_FIRST                       // .. IN_IB_CHART_FIRST + MAX_SYMBOLS - 1
	};

	//========================================================================
	// SHARED SMALL HELPERS  (one copy of what used to live in nine files)
	//========================================================================

	// HHMM integer (830 = 08:30) -> minutes since midnight
	int HHMMToMinutes(int HHMM)
	{
		return (HHMM / 100) * 60 + (HHMM % 100);
	}

	int MinutesOfDay(const SCDateTime& DT)
	{
		return DT.GetHour() * 60 + DT.GetMinute();
	}

	double AbsValue(double Value)
	{
		return (Value < 0.0) ? -Value : Value;
	}

	// Sakamoto's method. Returns 0 = Sunday .. 6 = Saturday.
	int DayOfWeekFromDate(int Year, int Month, int Day)
	{
		static const int MonthTable[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };

		if (Month < 1 || Month > 12)
			return -1;

		if (Month < 3)
			Year -= 1;

		return (Year + Year / 4 - Year / 100 + Year / 400 + MonthTable[Month - 1] + Day) % 7;
	}

	int TicksBetween(SCStudyInterfaceRef sc, double Distance)
	{
		if (sc.TickSize <= 0.0)
			return 0;

		return (int)(AbsValue(Distance) / sc.TickSize + 0.5);
	}

	SCString FormatSigned(SCStudyInterfaceRef sc, double Value, int ValueFormat)
	{
		SCString Out;
		Out.Format("%s%s",
			(Value >= 0.0) ? "+" : "-",
			sc.FormatGraphValue(AbsValue(Value), ValueFormat).GetChars());
		return Out;
	}

	//========================================================================
	// SHARED TEXT RENDERER
	//
	// Every visible line gets a FIXED LineNumber, and UTAM_ADD_OR_ADJUST
	// replaces that drawing in place, so text can never overlap or be left
	// behind. The color is part of the cache key, otherwise a red-to-green
	// flip with identical text would never be redrawn. "" erases the line.
	//========================================================================

	void SetTextLine(SCStudyInterfaceRef sc, int LineIndex, const SCString& Text, COLORREF TextColor)
	{
		if (LineIndex < 0 || LineIndex >= NUM_LINES)
			return;

		SCString CacheKey;
		CacheKey.Format("%u|%s", (unsigned int)TextColor, Text.GetChars());

		SCString& LastKey = sc.GetPersistentSCString(LineIndex);
		if (strcmp(LastKey.GetChars(), CacheKey.GetChars()) == 0)
			return;

		LastKey = CacheKey;

		const int LineNumber = LINE_NUM_BASE + LineIndex;

		if (Text.GetLength() == 0)
		{
			sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LineNumber);
			return;
		}

		s_UseTool Tool;
		Tool.Clear();
		Tool.ChartNumber  = sc.ChartNumber;
		Tool.Region       = sc.GraphRegion;
		Tool.DrawingType  = DRAWING_TEXT;
		Tool.LineNumber   = LineNumber;
		Tool.AddMethod    = UTAM_ADD_OR_ADJUST;
		Tool.AddAsUserDrawnDrawing = 0;

		Tool.UseRelativeVerticalValues = 1;   // horiz 0-150, vert 0-100 (100 = top)
		Tool.BeginDateTime = sc.Input[IN_HORIZ_POS].GetInt();
		Tool.BeginValue    = sc.Input[IN_VERT_POS].GetFloat()
		                     - LineIndex * sc.Input[IN_LINE_SPACING].GetFloat();

		Tool.Text          = Text;
		Tool.Color         = TextColor;
		Tool.FontSize      = sc.Input[IN_FONT_SIZE].GetInt();
		Tool.FontBold      = sc.Input[IN_BOLD].GetYesNo();
		Tool.TransparentLabelBackground = sc.Input[IN_TRANSPARENT_BG].GetYesNo();
		Tool.TextAlignment = DT_LEFT | DT_TOP;

		sc.UseTool(Tool);
	}

	//========================================================================
	// SHARED LEVEL RENDERER
	//
	// A horizontal line in real chart coordinates plus a label anchored at
	// the newest bar, left-aligned so it runs out into the empty space on the
	// right. Used by the midnight level and the Sunday open level.
	//========================================================================

	struct s_LevelIds
	{
		int LineNumber;
		int LabelNumber;
		int LineCacheIndex;
		int LabelCacheIndex;
	};

	const s_LevelIds MIDNIGHT_LEVEL =
		{ MID_LEVEL_LINE_NUM, MID_LABEL_LINE_NUM, MID_LEVEL_CACHE_IX, MID_LABEL_CACHE_IX };

	const s_LevelIds SUNDAY_LEVEL =
		{ SUN_LEVEL_LINE_NUM, SUN_LABEL_LINE_NUM, SUN_LEVEL_CACHE_IX, SUN_LABEL_CACHE_IX };

	void DrawLevel(SCStudyInterfaceRef sc, const s_LevelIds& Ids, int AnchorIndex,
		double Price, COLORREF LevelColor, const SCString& LabelText)
	{
		const int LastIndex = sc.ArraySize - 1;
		if (LastIndex < 0 || AnchorIndex < 0)
			return;

		//---- the line ----
		SCString LineKey;
		LineKey.Format("L|%u|%.8f|%d", (unsigned int)LevelColor, Price, AnchorIndex);

		SCString& LastLineKey = sc.GetPersistentSCString(Ids.LineCacheIndex);
		if (strcmp(LastLineKey.GetChars(), LineKey.GetChars()) != 0)
		{
			LastLineKey = LineKey;

			s_UseTool Tool;
			Tool.Clear();
			Tool.ChartNumber  = sc.ChartNumber;
			Tool.Region       = sc.GraphRegion;
			Tool.DrawingType  = DRAWING_HORIZONTALLINE;
			Tool.LineNumber   = Ids.LineNumber;
			Tool.AddMethod    = UTAM_ADD_OR_ADJUST;
			Tool.AddAsUserDrawnDrawing = 0;

			Tool.BeginDateTime = sc.BaseDateTimeIn[AnchorIndex];
			Tool.BeginValue    = (float)Price;

			Tool.Color     = LevelColor;
			Tool.LineWidth = sc.Input[IN_LEVEL_LINE_WIDTH].GetInt();
			Tool.LineStyle = sc.Input[IN_LEVEL_DASHED].GetYesNo()
			                 ? LINESTYLE_DASH : LINESTYLE_SOLID;

			sc.UseTool(Tool);
		}

		//---- the right hand label ----
		SCString LabelKey;
		LabelKey.Format("T|%u|%.8f|%s",
			(unsigned int)LevelColor, Price, LabelText.GetChars());

		SCString& LastLabelKey = sc.GetPersistentSCString(Ids.LabelCacheIndex);
		if (strcmp(LastLabelKey.GetChars(), LabelKey.GetChars()) != 0)
		{
			LastLabelKey = LabelKey;

			s_UseTool Tool;
			Tool.Clear();
			Tool.ChartNumber  = sc.ChartNumber;
			Tool.Region       = sc.GraphRegion;
			Tool.DrawingType  = DRAWING_TEXT;
			Tool.LineNumber   = Ids.LabelNumber;
			Tool.AddMethod    = UTAM_ADD_OR_ADJUST;
			Tool.AddAsUserDrawnDrawing = 0;

			Tool.BeginDateTime = sc.BaseDateTimeIn[LastIndex];
			Tool.BeginValue    = (float)Price;

			Tool.Text          = LabelText;
			Tool.Color         = LevelColor;
			Tool.FontSize      = sc.Input[IN_FONT_SIZE].GetInt();
			Tool.FontBold      = sc.Input[IN_BOLD].GetYesNo();
			Tool.TransparentLabelBackground = 1;
			Tool.TextAlignment = DT_LEFT | DT_VCENTER;

			sc.UseTool(Tool);
		}
	}

	void EraseLevel(SCStudyInterfaceRef sc, const s_LevelIds& Ids)
	{
		SCString& LastLineKey  = sc.GetPersistentSCString(Ids.LineCacheIndex);
		SCString& LastLabelKey = sc.GetPersistentSCString(Ids.LabelCacheIndex);

		if (LastLineKey.GetLength() == 0 && LastLabelKey.GetLength() == 0)
			return;

		LastLineKey  = "";
		LastLabelKey = "";

		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, Ids.LineNumber);
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, Ids.LabelNumber);
	}

	void ClearAll(SCStudyInterfaceRef sc)
	{
		for (int i = 0; i < NUM_LINES; ++i)
			sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LINE_NUM_BASE + i);

		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, MID_LEVEL_LINE_NUM);
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, MID_LABEL_LINE_NUM);
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, SUN_LEVEL_LINE_NUM);
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, SUN_LABEL_LINE_NUM);

		for (int i = 0; i < NUM_CACHE_SLOTS; ++i)
			sc.GetPersistentSCString(i) = "";
	}

	//========================================================================
	// SHARED SESSION BOUNDARY SEARCH
	//
	// One backwards walk replacing the four separate finders. Returns the
	// index of the newest bar that opens the requested window, or -1.
	//
	//   BOUNDARY_WINDOW_ENTRY : the bar before it is earlier in the day or on
	//                           a different date. This is the "first bar at or
	//                           just after the RTH open" test.
	//   BOUNDARY_DATE_CHANGE  : the bar before it is on a different date, so
	//                           the bar is the first of a session.
	//
	// WindowMinutes is what rejects the wrong boundary: a 17:00 reopen is a
	// date change but is not inside a 00:00 window, and Friday's 08:30 is not
	// inside a 17:00 window.
	//
	// RequiredWeekday is -1 for "any", else 0 = Sunday .. 6 = Saturday.
	//========================================================================

	enum e_BoundaryMode
	{
		BOUNDARY_WINDOW_ENTRY = 0,
		BOUNDARY_DATE_CHANGE
	};

	int FindBoundaryIndex(SCStudyInterfaceRef sc, int StartMinutes, int WindowMinutes,
		int Mode, int RequiredWeekday)
	{
		const int LastIndex = sc.ArraySize - 1;
		if (LastIndex < 1 || WindowMinutes < 1)
			return -1;

		for (int i = LastIndex; i >= 1; --i)
		{
			const int CurrentMinutes = MinutesOfDay(sc.BaseDateTimeIn[i]);

			if (CurrentMinutes < StartMinutes || CurrentMinutes >= StartMinutes + WindowMinutes)
				continue;

			const int DateChanged =
				(sc.BaseDateTimeIn[i].GetDate() != sc.BaseDateTimeIn[i - 1].GetDate()) ? 1 : 0;

			if (Mode == BOUNDARY_DATE_CHANGE)
			{
				if (!DateChanged)
					continue;
			}
			else
			{
				const int PreviousMinutes = MinutesOfDay(sc.BaseDateTimeIn[i - 1]);
				if (PreviousMinutes >= StartMinutes && !DateChanged)
					continue;
			}

			if (RequiredWeekday >= 0)
			{
				const SCDateTime& DT = sc.BaseDateTimeIn[i];
				if (DayOfWeekFromDate(DT.GetYear(), DT.GetMonth(), DT.GetDay()) != RequiredWeekday)
					continue;
			}

			return i;
		}

		return -1;
	}

	//========================================================================
	// SHARED LEVEL RELATION
	//
	// Midnight and Sunday both color by "level below price = red, level above
	// price = green", with an invert option.
	//========================================================================

	COLORREF LevelRelation(SCStudyInterfaceRef sc, double Distance, const char* LevelName,
		SCString& r_RelationText)
	{
		COLORREF AboveColor = sc.Input[IN_GREEN_COLOR].GetColor();
		COLORREF BelowColor = sc.Input[IN_RED_COLOR].GetColor();

		if (sc.Input[IN_INVERT_LEVEL_COLORS].GetYesNo())
		{
			const COLORREF Swap = AboveColor;
			AboveColor = BelowColor;
			BelowColor = Swap;
		}

		if (Distance > 0.0)
		{
			r_RelationText.Format("%s BELOW PRICE", LevelName);
			return BelowColor;
		}

		if (Distance < 0.0)
		{
			r_RelationText.Format("%s ABOVE PRICE", LevelName);
			return AboveColor;
		}

		r_RelationText.Format("PRICE AT %s", LevelName);
		return sc.Input[IN_FLAT_COLOR].GetColor();
	}

	//========================================================================
	// TIME SLOT CSV TABLE  (from TimeSlotValue)
	//
	// FILE FORMAT - one line per slot, blank lines and # lines ignored:
	//     09:30,22
	//     09:35,12
	// "Subtract From File Times" shifts those labels to the chart clock.
	//========================================================================

	std::map<int, int> g_SlotValues;
	std::string        g_LoadedPath;
	int                g_LoadedReloadFlag = -1;
	int                g_LoadFailed       = 0;

	int ParseSlotLine(const std::string& Line, int& r_Minutes, int& r_Value)
	{
		if (Line.empty() || Line[0] == '#')
			return 0;

		const size_t CommaPos = Line.find(',');
		if (CommaPos == std::string::npos)
			return 0;

		const std::string TimePart  = Line.substr(0, CommaPos);
		const std::string ValuePart = Line.substr(CommaPos + 1);

		int Hours   = 0;
		int Minutes = 0;

		const size_t ColonPos = TimePart.find(':');
		if (ColonPos != std::string::npos)
		{
			Hours   = atoi(TimePart.substr(0, ColonPos).c_str());
			Minutes = atoi(TimePart.substr(ColonPos + 1).c_str());
		}
		else
		{
			const int HHMM = atoi(TimePart.c_str());
			Hours   = HHMM / 100;
			Minutes = HHMM % 100;
		}

		if (Hours < 0 || Hours > 23 || Minutes < 0 || Minutes > 59)
			return 0;

		r_Minutes = Hours * 60 + Minutes;
		r_Value   = atoi(ValuePart.c_str());
		return 1;
	}

	void LoadSlotFile(const std::string& Path)
	{
		g_SlotValues.clear();
		g_LoadFailed = 0;

		std::ifstream File(Path.c_str());
		if (!File.is_open())
		{
			g_LoadFailed = 1;
			return;
		}

		std::string Line;
		while (std::getline(File, Line))
		{
			while (!Line.empty()
				&& (Line[Line.size() - 1] == '\r' || Line[Line.size() - 1] == ' '
					|| Line[Line.size() - 1] == '\t'))
			{
				Line.erase(Line.size() - 1);
			}
			while (!Line.empty() && (Line[0] == ' ' || Line[0] == '\t'))
				Line.erase(0, 1);

			int Minutes = 0;
			int Value   = 0;
			if (ParseSlotLine(Line, Minutes, Value))
				g_SlotValues[Minutes] = Value;
		}

		File.close();
	}

	//========================================================================
	// INSIDE BAR SCAN  (from InsideBarScanner)
	//
	// A Daily chart only contains trading days, so "yesterday, or Friday if
	// today is Monday" needs no weekday math: drop today's unfinished bar and
	// compare the newest completed bar with the one before it.
	//========================================================================

	SCString GetChartLabel(SCStudyInterfaceRef sc, int ChartNumber)
	{
		SCString Label;
#if HAVE_GET_CHART_NAME
		Label = sc.GetChartName(ChartNumber);
		if (Label.GetLength() == 0)
			Label.Format("Chart #%d", ChartNumber);
#else
		Label.Format("Chart #%d", ChartNumber);
#endif
		return Label;
	}

	struct s_ScanResult
	{
		int Valid;
		int IsInsideBar;
		int Month;
		int Day;
	};

	s_ScanResult ScanChart(SCStudyInterfaceRef sc, int ChartNumber, int Strict, int SkipTodaysBar)
	{
		s_ScanResult Result;
		Result.Valid       = 0;
		Result.IsInsideBar = 0;
		Result.Month       = 0;
		Result.Day         = 0;

		SCGraphData BaseData;
		sc.GetChartBaseData(ChartNumber, BaseData);

		SCFloatArrayRef HighArray = BaseData[SC_HIGH];
		SCFloatArrayRef LowArray  = BaseData[SC_LOW];

		const int ArraySize = HighArray.GetArraySize();
		if (ArraySize < 2)
			return Result;

		SCDateTimeArray DateTimeArray;
		sc.GetChartDateTimeArray(ChartNumber, DateTimeArray);

		int TargetIndex = ArraySize - 1;

		if (SkipTodaysBar
			&& DateTimeArray.GetArraySize() == ArraySize
			&& DateTimeArray[TargetIndex].GetDate() >= sc.CurrentSystemDateTime.GetDate())
		{
			--TargetIndex;
		}

		const int PriorIndex = TargetIndex - 1;
		if (PriorIndex < 0)
			return Result;

		const float TargetHigh = HighArray[TargetIndex];
		const float TargetLow  = LowArray[TargetIndex];
		const float PriorHigh  = HighArray[PriorIndex];
		const float PriorLow   = LowArray[PriorIndex];

		if (Strict)
			Result.IsInsideBar = (TargetHigh <  PriorHigh && TargetLow >  PriorLow) ? 1 : 0;
		else
			Result.IsInsideBar = (TargetHigh <= PriorHigh && TargetLow >= PriorLow) ? 1 : 0;

		if (DateTimeArray.GetArraySize() == ArraySize)
		{
			Result.Month = DateTimeArray[TargetIndex].GetMonth();
			Result.Day   = DateTimeArray[TargetIndex].GetDay();
		}

		Result.Valid = 1;
		return Result;
	}

	//========================================================================
	// LINE BUILDERS - one per original study, each producing ONE line
	//========================================================================

	//------------------------------------------------------------------------
	// LINE 0 - QUOTE   (FourLineChartText)
	//------------------------------------------------------------------------
	void BuildQuoteLine(SCStudyInterfaceRef sc, SCString& r_Text, COLORREF& r_Color)
	{
		const int LastIndex   = sc.ArraySize - 1;
		const int ValueFormat = sc.GetValueFormat();

		r_Color = sc.Input[IN_INFO_COLOR].GetColor();

		if (LastIndex >= 1)
		{
			const double Change = sc.Close[LastIndex] - sc.Close[LastIndex - 1];
			if (Change > 0.0)
				r_Color = sc.Input[IN_GREEN_COLOR].GetColor();
			else if (Change < 0.0)
				r_Color = sc.Input[IN_RED_COLOR].GetColor();
		}

		r_Text.Format("%s  LAST %s   BID/ASK %s / %s   BARS %d",
			sc.Symbol.GetChars(),
			sc.FormatGraphValue(sc.Close[LastIndex], ValueFormat).GetChars(),
			sc.FormatGraphValue(sc.Bid, ValueFormat).GetChars(),
			sc.FormatGraphValue(sc.Ask, ValueFormat).GetChars(),
			sc.ArraySize);
	}

	//------------------------------------------------------------------------
	// LINE 1 - MIDNIGHT LEVEL   (MidnightLevel)
	//
	// The midnight bar is the first bar of a new calendar date. Its close is
	// the 00:00-00:01 closing price on a 1 minute chart.
	//------------------------------------------------------------------------
	void BuildMidnightLine(SCStudyInterfaceRef sc, int PriceIndex,
		SCString& r_Text, COLORREF& r_Color)
	{
		const int MidnightIndex = FindBoundaryIndex(
			sc, 0, sc.Input[IN_DETECT_WINDOW_MINUTES].GetInt(), BOUNDARY_DATE_CHANGE, -1);

		if (MidnightIndex < 0)
		{
			EraseLevel(sc, MIDNIGHT_LEVEL);
			r_Text  = "MIDNIGHT: bar not in loaded data";
			r_Color = sc.Input[IN_FLAT_COLOR].GetColor();
			return;
		}

		const double MidnightClose = sc.Close[MidnightIndex];
		const double Distance      = sc.Close[PriceIndex] - MidnightClose;

		SCString Relation;
		r_Color = LevelRelation(sc, Distance, "MIDNIGHT", Relation);

		if (sc.Input[IN_DRAW_LEVEL_LINES].GetYesNo())
		{
			SCString LabelText = sc.Input[IN_MIDNIGHT_LABEL].GetString();
			DrawLevel(sc, MIDNIGHT_LEVEL, MidnightIndex, MidnightClose, r_Color, LabelText);
		}
		else
		{
			EraseLevel(sc, MIDNIGHT_LEVEL);
		}

		const int ValueFormat = sc.GetValueFormat();
		const SCDateTime MidnightDT = sc.BaseDateTimeIn[MidnightIndex];

		r_Text.Format("MIDNIGHT %02d/%02d %02d:%02d %s   DIST %s (%d t)   %s",
			MidnightDT.GetMonth(), MidnightDT.GetDay(),
			MidnightDT.GetHour(), MidnightDT.GetMinute(),
			sc.FormatGraphValue(MidnightClose, ValueFormat).GetChars(),
			FormatSigned(sc, Distance, ValueFormat).GetChars(),
			TicksBetween(sc, Distance),
			Relation.GetChars());
	}

	//------------------------------------------------------------------------
	// LINE 2 - SUNDAY OPEN LEVEL   (SundayOpenLevel)
	//
	// The first bar of the most recent Sunday evening session. The level is
	// that bar's OPEN, i.e. the first print of the trading week.
	//------------------------------------------------------------------------
	void BuildSundayLine(SCStudyInterfaceRef sc, int PriceIndex,
		SCString& r_Text, COLORREF& r_Color)
	{
		const int OpenIndex = FindBoundaryIndex(
			sc,
			HHMMToMinutes(sc.Input[IN_EVENING_OPEN_HHMM].GetInt()),
			sc.Input[IN_DETECT_WINDOW_MINUTES].GetInt(),
			BOUNDARY_DATE_CHANGE,
			sc.Input[IN_REQUIRE_SUNDAY].GetYesNo() ? 0 : -1);

		if (OpenIndex < 0)
		{
			EraseLevel(sc, SUNDAY_LEVEL);
			r_Text  = "SUNDAY OPEN: not in loaded data - raise Days to Load to 7+";
			r_Color = sc.Input[IN_FLAT_COLOR].GetColor();
			return;
		}

		const double SundayOpen = sc.Open[OpenIndex];
		const double Distance   = sc.Close[PriceIndex] - SundayOpen;

		SCString Relation;
		r_Color = LevelRelation(sc, Distance, "SUNDAY OPEN", Relation);

		if (sc.Input[IN_DRAW_LEVEL_LINES].GetYesNo())
		{
			SCString LabelText = sc.Input[IN_SUNDAY_LABEL].GetString();
			DrawLevel(sc, SUNDAY_LEVEL, OpenIndex, SundayOpen, r_Color, LabelText);
		}
		else
		{
			EraseLevel(sc, SUNDAY_LEVEL);
		}

		const int ValueFormat = sc.GetValueFormat();
		const SCDateTime OpenDT = sc.BaseDateTimeIn[OpenIndex];

		r_Text.Format("SUNDAY OPEN %02d/%02d %02d:%02d %s   DIST %s (%d t)   %s",
			OpenDT.GetMonth(), OpenDT.GetDay(),
			OpenDT.GetHour(), OpenDT.GetMinute(),
			sc.FormatGraphValue(SundayOpen, ValueFormat).GetChars(),
			FormatSigned(sc, Distance, ValueFormat).GetChars(),
			TicksBetween(sc, Distance),
			Relation.GetChars());
	}

	//------------------------------------------------------------------------
	// LINE 3 - PREVIOUS SESSION CLOSE   (PrevSessionCloseTracker)
	//
	// The close of the last bar BEFORE the most recent 17:00 boundary.
	// Finding it by boundary crossing rather than by calendar date is what
	// makes Monday work: walking back from Monday morning, the bar before the
	// Sunday reopen is Friday's final bar.
	//------------------------------------------------------------------------
	void BuildPrevCloseLine(SCStudyInterfaceRef sc, SCString& r_Text, COLORREF& r_Color)
	{
		const int LastIndex  = sc.ArraySize - 1;
		const COLORREF Flat  = sc.Input[IN_FLAT_COLOR].GetColor();

		//---- display window ----
		const int NowMinutes   = MinutesOfDay(sc.BaseDateTimeIn[LastIndex]);
		const int StartMinutes = HHMMToMinutes(sc.Input[IN_RTH_OPEN_HHMM].GetInt());
		const int EndMinutes   = HHMMToMinutes(sc.Input[IN_RTH_CLOSE_HHMM].GetInt());

		if (sc.Input[IN_PREV_CLOSE_ONLY_RTH].GetYesNo())
		{
			int InWindow;
			if (StartMinutes <= EndMinutes)
				InWindow = (NowMinutes >= StartMinutes && NowMinutes <= EndMinutes) ? 1 : 0;
			else   // window wraps past midnight
				InWindow = (NowMinutes >= StartMinutes || NowMinutes <= EndMinutes) ? 1 : 0;

			if (!InWindow)
			{
				r_Text  = "PREV CLOSE: outside the RTH display window";
				r_Color = Flat;
				return;
			}
		}

		//---- reference close ----
		const int RefMinutes = HHMMToMinutes(sc.Input[IN_EVENING_OPEN_HHMM].GetInt());

		int RefIndex = FindBoundaryIndex(
			sc, RefMinutes, 24 * 60 - RefMinutes, BOUNDARY_WINDOW_ENTRY, -1);

		if (RefIndex > 0)
		{
			--RefIndex;   // the bar BEFORE the boundary is the closing bar
		}
		else
		{
			// Fallback for charts carrying no evening session: last bar of
			// the previous calendar day.
			RefIndex = -1;
			const int TodayDate = sc.BaseDateTimeIn[LastIndex].GetDate();
			for (int i = LastIndex; i >= 0; --i)
			{
				if (sc.BaseDateTimeIn[i].GetDate() < TodayDate)
				{
					RefIndex = i;
					break;
				}
			}
		}

		if (RefIndex < 0)
		{
			r_Text  = "PREV CLOSE: not in loaded data";
			r_Color = Flat;
			return;
		}

		const double RefClose     = sc.Close[RefIndex];
		const double CurrentClose = sc.Close[LastIndex];
		const double Distance     = CurrentClose - RefClose;

		//---- trend and approach ----
		int Lookback = sc.Input[IN_TREND_LOOKBACK].GetInt();
		if (Lookback > LastIndex)
			Lookback = LastIndex;

		const double PastClose    = sc.Close[LastIndex - Lookback];
		const double TrendChange  = CurrentClose - PastClose;
		const double PastDistance = AbsValue(PastClose - RefClose);
		const double NowDistance  = AbsValue(Distance);

		const char* TrendText = "TREND FLAT";
		r_Color = Flat;

		if (TrendChange > 0.0)
		{
			r_Color   = sc.Input[IN_GREEN_COLOR].GetColor();
			TrendText = "TREND UP";
		}
		else if (TrendChange < 0.0)
		{
			r_Color   = sc.Input[IN_RED_COLOR].GetColor();
			TrendText = "TREND DOWN";
		}

		const char* ApproachText = "HOLDING";
		if (NowDistance < PastDistance)
			ApproachText = "MOVING TOWARD";
		else if (NowDistance > PastDistance)
			ApproachText = "MOVING AWAY";

		const int ValueFormat  = sc.GetValueFormat();
		const SCDateTime RefDT = sc.BaseDateTimeIn[RefIndex];

		r_Text.Format("PREV CLOSE %02d/%02d %02d:%02d %s   DIST %s (%d t)   %s   %s",
			RefDT.GetMonth(), RefDT.GetDay(),
			RefDT.GetHour(), RefDT.GetMinute(),
			sc.FormatGraphValue(RefClose, ValueFormat).GetChars(),
			FormatSigned(sc, Distance, ValueFormat).GetChars(),
			TicksBetween(sc, Distance),
			ApproachText,
			TrendText);
	}

	//------------------------------------------------------------------------
	// LINE 4 - FIRST HOUR TREND   (FirstHourTrend)
	//
	// Open-to-close direction of the first hour after the RTH open.
	//------------------------------------------------------------------------
	void BuildFirstHourLine(SCStudyInterfaceRef sc, int OpenIndex,
		SCString& r_Text, COLORREF& r_Color)
	{
		const int LastIndex = sc.ArraySize - 1;
		r_Color = sc.Input[IN_FLAT_COLOR].GetColor();

		if (OpenIndex < 0)
		{
			r_Text = "1ST HOUR: RTH open not found in loaded data";
			return;
		}

		const int OpenMinutes = HHMMToMinutes(sc.Input[IN_RTH_OPEN_HHMM].GetInt());
		const int EndMinutes  = OpenMinutes + sc.Input[IN_WINDOW_MINUTES].GetInt();
		const int SessionDate = sc.BaseDateTimeIn[OpenIndex].GetDate();

		int WindowEndIndex = OpenIndex;
		double WindowHigh  = sc.High[OpenIndex];
		double WindowLow   = sc.Low[OpenIndex];

		for (int i = OpenIndex; i <= LastIndex; ++i)
		{
			if (sc.BaseDateTimeIn[i].GetDate() != SessionDate)
				break;
			if (MinutesOfDay(sc.BaseDateTimeIn[i]) >= EndMinutes)
				break;

			if (sc.High[i] > WindowHigh)
				WindowHigh = sc.High[i];
			if (sc.Low[i] < WindowLow)
				WindowLow = sc.Low[i];

			WindowEndIndex = i;
		}

		const int IsComplete = (WindowEndIndex < LastIndex) ? 1 : 0;

		const double WindowOpen  = sc.Open[OpenIndex];
		const double WindowClose = sc.Close[WindowEndIndex];
		const double Net         = WindowClose - WindowOpen;
		const int    NetTicks    = TicksBetween(sc, Net);
		const int    MinTicks    = sc.Input[IN_MIN_TICKS].GetInt();

		SCString Verdict;

		if (Net > 0.0 && NetTicks >= MinTicks)
		{
			r_Color = sc.Input[IN_GREEN_COLOR].GetColor();
			Verdict = IsComplete ? "GREEN DAY" : "LEANING GREEN";
		}
		else if (Net < 0.0 && NetTicks >= MinTicks)
		{
			r_Color = sc.Input[IN_RED_COLOR].GetColor();
			Verdict = IsComplete ? "RED DAY" : "LEANING RED";
		}
		else
		{
			Verdict = IsComplete ? "NEUTRAL DAY" : "NEUTRAL SO FAR";
		}

		if (!IsComplete)
			Verdict += " (still running)";

		const int ValueFormat = sc.GetValueFormat();

		r_Text.Format("1ST HOUR %02d:%02d-%02d:%02d   O %s C %s   NET %s (%d t)   %s",
			OpenMinutes / 60, OpenMinutes % 60,
			(EndMinutes / 60) % 24, EndMinutes % 60,
			sc.FormatGraphValue(WindowOpen, ValueFormat).GetChars(),
			sc.FormatGraphValue(WindowClose, ValueFormat).GetChars(),
			FormatSigned(sc, Net, ValueFormat).GetChars(),
			NetTicks,
			Verdict.GetChars());
	}

	//------------------------------------------------------------------------
	// LINE 5 - OPENING RANGE SEQUENCE   (OneHourOR_HighLow_First)
	//
	// Which of the opening range extremes was put in first. Strict > and <
	// are what make a later retest leave the recorded index alone.
	//------------------------------------------------------------------------
	void BuildOpeningRangeLine(SCStudyInterfaceRef sc, int OpenIndex,
		SCString& r_Text, COLORREF& r_Color)
	{
		const int LastIndex = sc.ArraySize - 1;
		r_Color = sc.Input[IN_FLAT_COLOR].GetColor();

		if (OpenIndex < 0)
		{
			r_Text = "OPENING RANGE: RTH open not found in loaded data";
			return;
		}

		const int OpenMinutes = HHMMToMinutes(sc.Input[IN_RTH_OPEN_HHMM].GetInt());
		const int EndMinutes  = OpenMinutes + sc.Input[IN_WINDOW_MINUTES].GetInt();
		const int SessionDate = sc.BaseDateTimeIn[OpenIndex].GetDate();

		int    WindowEndIndex = OpenIndex;
		int    HighIndex      = OpenIndex;
		int    LowIndex       = OpenIndex;
		double RangeHigh      = sc.High[OpenIndex];
		double RangeLow       = sc.Low[OpenIndex];

		for (int i = OpenIndex; i <= LastIndex; ++i)
		{
			if (sc.BaseDateTimeIn[i].GetDate() != SessionDate)
				break;
			if (MinutesOfDay(sc.BaseDateTimeIn[i]) >= EndMinutes)
				break;

			if (sc.High[i] > RangeHigh)
			{
				RangeHigh = sc.High[i];
				HighIndex = i;
			}

			if (sc.Low[i] < RangeLow)
			{
				RangeLow = sc.Low[i];
				LowIndex = i;
			}

			WindowEndIndex = i;
		}

		const int IsComplete = (WindowEndIndex < LastIndex) ? 1 : 0;

		SCString Verdict;

		if (LowIndex < HighIndex)
		{
			r_Color = sc.Input[IN_GREEN_COLOR].GetColor();
			Verdict = "LOW FIRST -> Breakout GREEN";
		}
		else if (HighIndex < LowIndex)
		{
			r_Color = sc.Input[IN_RED_COLOR].GetColor();
			Verdict = "HIGH FIRST -> Breakout RED";
		}
		else
		{
			// Both extremes belong to the same bar, so the bar data alone
			// cannot say which came first. Optionally infer it from that
			// bar's direction: a bar that closed up most likely made its low
			// before its high.
			Verdict = "SAME BAR -> Undetermined";

			if (sc.Input[IN_RESOLVE_SAME_BAR].GetYesNo())
			{
				const double BarOpen  = sc.Open[HighIndex];
				const double BarClose = sc.Close[HighIndex];

				if (BarClose > BarOpen)
				{
					r_Color = sc.Input[IN_GREEN_COLOR].GetColor();
					Verdict = "SAME UP BAR, LOW ASSUMED FIRST -> Breakout GREEN";
				}
				else if (BarClose < BarOpen)
				{
					r_Color = sc.Input[IN_RED_COLOR].GetColor();
					Verdict = "SAME DOWN BAR, HIGH ASSUMED FIRST -> Breakout RED";
				}
			}
		}

		if (!IsComplete)
			Verdict += " (forming)";

		const int ValueFormat   = sc.GetValueFormat();
		const SCDateTime HighDT = sc.BaseDateTimeIn[HighIndex];
		const SCDateTime LowDT  = sc.BaseDateTimeIn[LowIndex];

		r_Text.Format("OR %02d:%02d-%02d:%02d   H %s @%02d:%02d  L %s @%02d:%02d   %s",
			OpenMinutes / 60, OpenMinutes % 60,
			(EndMinutes / 60) % 24, EndMinutes % 60,
			sc.FormatGraphValue(RangeHigh, ValueFormat).GetChars(),
			HighDT.GetHour(), HighDT.GetMinute(),
			sc.FormatGraphValue(RangeLow, ValueFormat).GetChars(),
			LowDT.GetHour(), LowDT.GetMinute(),
			Verdict.GetChars());
	}

	//------------------------------------------------------------------------
	// LINE 6 - DAY OF WEEK BIAS   (DayOfWeekBias)
	//
	// Red is always 100 - green, so only the green figure is an input.
	// By default the evening session rolls forward: a bar stamped Sunday
	// 18:00 counts as Monday, which is what the statistics assume.
	//------------------------------------------------------------------------
	void BuildDayBiasLine(SCStudyInterfaceRef sc, SCString& r_Text, COLORREF& r_Color)
	{
		r_Color = sc.Input[IN_FLAT_COLOR].GetColor();

		SCDateTime ReferenceDT;
		if (sc.Input[IN_DOW_USE_SYSTEM_DATE].GetYesNo() || sc.ArraySize == 0)
			ReferenceDT = sc.CurrentSystemDateTime;
		else
			ReferenceDT = sc.BaseDateTimeIn[sc.ArraySize - 1];

		int DayOfWeek = DayOfWeekFromDate(
			ReferenceDT.GetYear(), ReferenceDT.GetMonth(), ReferenceDT.GetDay());

		if (DayOfWeek < 0)
		{
			r_Text = "DAY BIAS: invalid date";
			return;
		}

		// Roll the evening session onto the next weekday. Advancing the
		// weekday index sidesteps calendar arithmetic entirely.
		if (sc.Input[IN_DOW_ROLL_EVENING].GetYesNo()
			&& MinutesOfDay(ReferenceDT)
			   >= HHMMToMinutes(sc.Input[IN_EVENING_OPEN_HHMM].GetInt()))
		{
			DayOfWeek = (DayOfWeek + 1) % 7;
		}

		static const char* DayNames[7] =
			{ "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY" };

		if (DayOfWeek < 1 || DayOfWeek > 5)
		{
			r_Text.Format("%s - no weekday statistics", DayNames[DayOfWeek]);
			return;
		}

		// Monday is weekday 1, which is input index IN_MON_GREEN.
		const double GreenPercent = sc.Input[IN_MON_GREEN + (DayOfWeek - 1)].GetFloat();
		const double RedPercent   = 100.0 - GreenPercent;
		const double Edge         = GreenPercent - RedPercent;

		const char* BiasText = "NO EDGE - EVEN SPLIT";

		if (GreenPercent > RedPercent)
		{
			r_Color  = sc.Input[IN_GREEN_COLOR].GetColor();
			BiasText = "GREEN DAY BIAS";
		}
		else if (RedPercent > GreenPercent)
		{
			r_Color  = sc.Input[IN_RED_COLOR].GetColor();
			BiasText = "RED DAY BIAS";
		}

		r_Text.Format("%s %02d/%02d   GREEN %.2f%% / RED %.2f%%   SKEW %s%.2f%%   %s",
			DayNames[DayOfWeek], ReferenceDT.GetMonth(), ReferenceDT.GetDay(),
			GreenPercent, RedPercent,
			(Edge >= 0.0) ? "+" : "-", (Edge >= 0.0) ? Edge : -Edge,
			BiasText);
	}

	//------------------------------------------------------------------------
	// LINE 7 - TIME SLOT VALUE   (TimeSlotValue)
	//
	//   value >= Red Threshold   -> red
	//   value >= Grey Threshold  -> grey
	//   below that               -> green
	//------------------------------------------------------------------------
	void BuildTimeSlotLine(SCStudyInterfaceRef sc, int ForceReload,
		SCString& r_Text, COLORREF& r_Color)
	{
		const COLORREF Flat = sc.Input[IN_FLAT_COLOR].GetColor();
		r_Color = Flat;

		const std::string FilePath = sc.Input[IN_SLOT_FILE_PATH].GetString();
		const int ReloadFlag = sc.Input[IN_SLOT_RELOAD].GetYesNo();

		if (FilePath != g_LoadedPath
			|| ReloadFlag != g_LoadedReloadFlag
			|| ForceReload
			|| (g_SlotValues.empty() && !g_LoadFailed))
		{
			LoadSlotFile(FilePath);
			g_LoadedPath       = FilePath;
			g_LoadedReloadFlag = ReloadFlag;
		}

		if (g_LoadFailed)
		{
			r_Text.Format("TIME SLOT: cannot open %s", FilePath.c_str());
			return;
		}

		SCDateTime ReferenceDT;
		if (sc.Input[IN_SLOT_USE_SYSTEM_CLOCK].GetYesNo() || sc.ArraySize == 0)
			ReferenceDT = sc.CurrentSystemDateTime;
		else
			ReferenceDT = sc.BaseDateTimeIn[sc.ArraySize - 1];

		int SlotMinutes = sc.Input[IN_SLOT_MINUTES].GetInt();
		if (SlotMinutes < 1)
			SlotMinutes = 1;

		const int NowMinutes   = MinutesOfDay(ReferenceDT);
		const int LocalSlot    = (NowMinutes / SlotMinutes) * SlotMinutes;
		const int NextLocal    = LocalSlot + SlotMinutes;
		const int ShiftMinutes = sc.Input[IN_SLOT_SUBTRACT_MINUTES].GetInt();

		const int FileKey     = LocalSlot + ShiftMinutes;
		const int NextFileKey = FileKey + SlotMinutes;

		SCString NextText;
		if (sc.Input[IN_SLOT_SHOW_NEXT].GetYesNo())
		{
			std::map<int, int>::const_iterator NextFound = g_SlotValues.find(NextFileKey);
			if (NextFound != g_SlotValues.end())
			{
				NextText.Format("   next %02d:%02d -> %d",
					(NextLocal / 60) % 24, NextLocal % 60, NextFound->second);
			}
			else
			{
				NextText.Format("   next %02d:%02d -> --",
					(NextLocal / 60) % 24, NextLocal % 60);
			}
		}

		std::map<int, int>::const_iterator Found = g_SlotValues.find(FileKey);

		if (Found == g_SlotValues.end())
		{
			r_Text.Format("SLOT %02d:%02d-%02d:%02d   VALUE --%s   (%d slots loaded)",
				LocalSlot / 60, LocalSlot % 60,
				(NextLocal / 60) % 24, NextLocal % 60,
				NextText.GetChars(),
				(int)g_SlotValues.size());
			return;
		}

		const int Value = Found->second;
		const char* Band;

		if (Value >= sc.Input[IN_SLOT_RED_THRESHOLD].GetInt())
		{
			r_Color = sc.Input[IN_RED_COLOR].GetColor();
			Band    = "RED";
		}
		else if (Value >= sc.Input[IN_SLOT_GREY_THRESHOLD].GetInt())
		{
			r_Color = sc.Input[IN_SLOT_GREY_COLOR].GetColor();
			Band    = "GREY";
		}
		else
		{
			r_Color = sc.Input[IN_GREEN_COLOR].GetColor();
			Band    = "GREEN";
		}

		r_Text.Format("SLOT %02d:%02d-%02d:%02d   VALUE %d  %s%s",
			LocalSlot / 60, LocalSlot % 60,
			(NextLocal / 60) % 24, NextLocal % 60,
			Value, Band,
			NextText.GetChars());
	}

	//------------------------------------------------------------------------
	// LINE 8 - INSIDE BAR SCAN   (InsideBarScanner)
	//
	// Reading up to 12 other charts is the most expensive thing this study
	// does, so the result is cached and only recomputed when a new bar opens
	// or on a full recalculation. Daily inside-bar status cannot change more
	// often than that anyway.
	//------------------------------------------------------------------------
	void BuildInsideBarLine(SCStudyInterfaceRef sc, SCString& r_Text, COLORREF& r_Color)
	{
		SCString& CachedText  = sc.GetPersistentSCString(IB_TEXT_CACHE_IX);
		int&      ScanBarCount = sc.GetPersistentInt(PI_IB_SCAN_BARCOUNT);
		int&      HitCount     = sc.GetPersistentInt(PI_IB_HIT_COUNT);

		const int NeedScan = (sc.ArraySize != ScanBarCount)
			|| sc.IsFullRecalculation
			|| (CachedText.GetLength() == 0);

		if (NeedScan)
		{
			ScanBarCount = sc.ArraySize;

			const int Strict    = sc.Input[IN_IB_STRICT].GetYesNo();
			const int SkipToday = sc.Input[IN_IB_SKIP_TODAY].GetYesNo();
			int MaxNames        = sc.Input[IN_IB_MAX_NAMES].GetInt();
			if (MaxNames < 1)
				MaxNames = 1;

			int Scanned = 0;
			int Hits    = 0;
			int Listed  = 0;

			SCString Names;
			SCString Piece;

			for (int i = 0; i < MAX_SYMBOLS; ++i)
			{
				const int ChartNumber = sc.Input[IN_IB_CHART_FIRST + i].GetChartNumber();
				if (ChartNumber <= 0)
					continue;

				const s_ScanResult Result = ScanChart(sc, ChartNumber, Strict, SkipToday);
				if (!Result.Valid)
					continue;

				++Scanned;

				if (!Result.IsInsideBar)
					continue;

				++Hits;

				if (Listed < MaxNames)
				{
					Piece.Format("%s%s %02d/%02d",
						(Listed > 0) ? ", " : "",
						GetChartLabel(sc, ChartNumber).GetChars(),
						Result.Month, Result.Day);
					Names += Piece;
					++Listed;
				}
			}

			if (Hits > Listed)
			{
				Piece.Format(" +%d more", Hits - Listed);
				Names += Piece;
			}

			HitCount = Hits;

			if (Scanned == 0)
			{
				CachedText = "IB SCAN: set the IB Chart 1-12 inputs to your Daily charts";
			}
			else if (Hits == 0)
			{
				CachedText.Format("IB SCAN (prev completed daily) 0 of %d: (none)", Scanned);
			}
			else
			{
				CachedText.Format("IB SCAN (prev completed daily) %d of %d: %s",
					Hits, Scanned, Names.GetChars());
			}
		}

		r_Text  = CachedText;
		r_Color = (HitCount > 0)
			? sc.Input[IN_GREEN_COLOR].GetColor()
			: sc.Input[IN_INFO_COLOR].GetColor();
	}
}

//============================================================================

SCSFExport scsf_CombinedDashboard(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "Combined Chart Dashboard";
		sc.StudyDescription
			= "Nine live status lines: quote, midnight level, Sunday open, previous "
			  "session close, first hour trend, opening range sequence, day of week "
			  "bias, time slot value and inside bar scan.";
		sc.GraphRegion  = 0;
		sc.AutoLoop     = 0;
		sc.UpdateAlways = 1;
		sc.FreeDLL      = 0;
		sc.DrawZeros    = 0;

		// Low precedence so the referenced Daily charts calculate first.
		sc.CalculationPrecedence = LOW_PREC_LEVEL;

		//---- shared display ----
		sc.Input[IN_HORIZ_POS].Name = "Horizontal Position (0-150)";
		sc.Input[IN_HORIZ_POS].SetInt(2);

		sc.Input[IN_VERT_POS].Name = "Vertical Position (0-100, 100 = top)";
		sc.Input[IN_VERT_POS].SetFloat(98.0f);

		sc.Input[IN_LINE_SPACING].Name = "Line Spacing (percent of height)";
		sc.Input[IN_LINE_SPACING].SetFloat(3.0f);

		sc.Input[IN_FONT_SIZE].Name = "Font Size";
		sc.Input[IN_FONT_SIZE].SetInt(11);

		sc.Input[IN_BOLD].Name = "Bold Text";
		sc.Input[IN_BOLD].SetYesNo(1);

		sc.Input[IN_TRANSPARENT_BG].Name = "Transparent Background";
		sc.Input[IN_TRANSPARENT_BG].SetYesNo(1);

		sc.Input[IN_GREEN_COLOR].Name = "Green / Bullish Color";
		sc.Input[IN_GREEN_COLOR].SetColor(0, 220, 0);

		sc.Input[IN_RED_COLOR].Name = "Red / Bearish Color";
		sc.Input[IN_RED_COLOR].SetColor(255, 60, 60);

		sc.Input[IN_FLAT_COLOR].Name = "Neutral / Unavailable Color";
		sc.Input[IN_FLAT_COLOR].SetColor(190, 190, 190);

		sc.Input[IN_INFO_COLOR].Name = "Information Color";
		sc.Input[IN_INFO_COLOR].SetColor(0, 200, 255);

		//---- shared session times ----
		sc.Input[IN_RTH_OPEN_HHMM].Name = "RTH Open (HHMM, chart timezone)";
		sc.Input[IN_RTH_OPEN_HHMM].SetInt(830);

		sc.Input[IN_RTH_CLOSE_HHMM].Name = "RTH Close (HHMM)";
		sc.Input[IN_RTH_CLOSE_HHMM].SetInt(1500);

		sc.Input[IN_EVENING_OPEN_HHMM].Name = "Evening Session Open / Prev Close (HHMM)";
		sc.Input[IN_EVENING_OPEN_HHMM].SetInt(1700);

		sc.Input[IN_DETECT_WINDOW_MINUTES].Name = "Boundary Detect Window (minutes)";
		sc.Input[IN_DETECT_WINDOW_MINUTES].SetInt(60);
		sc.Input[IN_DETECT_WINDOW_MINUTES].SetIntLimits(1, 720);

		//---- line visibility ----
		sc.Input[IN_SHOW_QUOTE].Name = "Show Line: Quote";
		sc.Input[IN_SHOW_QUOTE].SetYesNo(1);

		sc.Input[IN_SHOW_MIDNIGHT].Name = "Show Line: Midnight Level";
		sc.Input[IN_SHOW_MIDNIGHT].SetYesNo(1);

		sc.Input[IN_SHOW_SUNDAY].Name = "Show Line: Sunday Open";
		sc.Input[IN_SHOW_SUNDAY].SetYesNo(1);

		sc.Input[IN_SHOW_PREV_CLOSE].Name = "Show Line: Prev Session Close";
		sc.Input[IN_SHOW_PREV_CLOSE].SetYesNo(1);

		sc.Input[IN_SHOW_FIRST_HOUR].Name = "Show Line: First Hour Trend";
		sc.Input[IN_SHOW_FIRST_HOUR].SetYesNo(1);

		sc.Input[IN_SHOW_OR_SEQUENCE].Name = "Show Line: Opening Range Sequence";
		sc.Input[IN_SHOW_OR_SEQUENCE].SetYesNo(1);

		sc.Input[IN_SHOW_DAY_BIAS].Name = "Show Line: Day Of Week Bias";
		sc.Input[IN_SHOW_DAY_BIAS].SetYesNo(1);

		sc.Input[IN_SHOW_TIME_SLOT].Name = "Show Line: Time Slot Value";
		sc.Input[IN_SHOW_TIME_SLOT].SetYesNo(1);

		sc.Input[IN_SHOW_INSIDE_BAR].Name = "Show Line: Inside Bar Scan";
		sc.Input[IN_SHOW_INSIDE_BAR].SetYesNo(1);

		//---- level lines ----
		sc.Input[IN_DRAW_LEVEL_LINES].Name = "Draw Midnight / Sunday Level Lines";
		sc.Input[IN_DRAW_LEVEL_LINES].SetYesNo(1);

		sc.Input[IN_LEVEL_LINE_WIDTH].Name = "Level Line Width";
		sc.Input[IN_LEVEL_LINE_WIDTH].SetInt(1);
		sc.Input[IN_LEVEL_LINE_WIDTH].SetIntLimits(1, 10);

		sc.Input[IN_LEVEL_DASHED].Name = "Level Lines Dashed";
		sc.Input[IN_LEVEL_DASHED].SetYesNo(1);

		// Explicit UTF-8 bytes so the glyphs survive the source file encoding.
		// F0 9F 8C 99 is U+1F319 moon, F0 9F 8C 85 is U+1F305 sunrise.
		sc.Input[IN_MIDNIGHT_LABEL].Name = "Midnight Label Text";
		sc.Input[IN_MIDNIGHT_LABEL].SetString("\xF0\x9F\x8C\x99 midnight");

		sc.Input[IN_SUNDAY_LABEL].Name = "Sunday Open Label Text";
		sc.Input[IN_SUNDAY_LABEL].SetString("\xF0\x9F\x8C\x85 sunday open");

		sc.Input[IN_REQUIRE_SUNDAY].Name = "Require Sunday (No = any weekly reopen)";
		sc.Input[IN_REQUIRE_SUNDAY].SetYesNo(1);

		sc.Input[IN_USE_COMPLETED_BAR].Name = "Levels: Measure From Last Completed Bar";
		sc.Input[IN_USE_COMPLETED_BAR].SetYesNo(1);

		sc.Input[IN_INVERT_LEVEL_COLORS].Name = "Levels: Invert Colors";
		sc.Input[IN_INVERT_LEVEL_COLORS].SetYesNo(0);

		//---- previous session close ----
		sc.Input[IN_PREV_CLOSE_ONLY_RTH].Name = "Prev Close: Only During RTH Window";
		sc.Input[IN_PREV_CLOSE_ONLY_RTH].SetYesNo(1);

		sc.Input[IN_TREND_LOOKBACK].Name = "Prev Close: Trend Lookback (bars)";
		sc.Input[IN_TREND_LOOKBACK].SetInt(5);
		sc.Input[IN_TREND_LOOKBACK].SetIntLimits(1, 500);

		//---- first hour / opening range ----
		sc.Input[IN_WINDOW_MINUTES].Name = "First Hour / Opening Range (minutes)";
		sc.Input[IN_WINDOW_MINUTES].SetInt(60);
		sc.Input[IN_WINDOW_MINUTES].SetIntLimits(1, 720);

		sc.Input[IN_MIN_TICKS].Name = "First Hour: Minimum Ticks To Call The Day";
		sc.Input[IN_MIN_TICKS].SetInt(0);
		sc.Input[IN_MIN_TICKS].SetIntLimits(0, 10000);

		sc.Input[IN_RESOLVE_SAME_BAR].Name = "OR: Resolve Same-Bar Case By Bar Direction";
		sc.Input[IN_RESOLVE_SAME_BAR].SetYesNo(1);

		//---- day of week bias ----
		sc.Input[IN_MON_GREEN].Name = "Monday Green %";
		sc.Input[IN_MON_GREEN].SetFloat(65.38f);
		sc.Input[IN_MON_GREEN].SetFloatLimits(0.0f, 100.0f);

		sc.Input[IN_TUE_GREEN].Name = "Tuesday Green %";
		sc.Input[IN_TUE_GREEN].SetFloat(46.15f);
		sc.Input[IN_TUE_GREEN].SetFloatLimits(0.0f, 100.0f);

		sc.Input[IN_WED_GREEN].Name = "Wednesday Green %";
		sc.Input[IN_WED_GREEN].SetFloat(53.85f);
		sc.Input[IN_WED_GREEN].SetFloatLimits(0.0f, 100.0f);

		sc.Input[IN_THU_GREEN].Name = "Thursday Green %";
		sc.Input[IN_THU_GREEN].SetFloat(53.85f);
		sc.Input[IN_THU_GREEN].SetFloatLimits(0.0f, 100.0f);

		sc.Input[IN_FRI_GREEN].Name = "Friday Green %";
		sc.Input[IN_FRI_GREEN].SetFloat(52.0f);
		sc.Input[IN_FRI_GREEN].SetFloatLimits(0.0f, 100.0f);

		sc.Input[IN_DOW_ROLL_EVENING].Name = "Day Bias: Evening Counts As Next Day";
		sc.Input[IN_DOW_ROLL_EVENING].SetYesNo(1);

		sc.Input[IN_DOW_USE_SYSTEM_DATE].Name = "Day Bias: Use System Clock Instead Of Last Bar";
		sc.Input[IN_DOW_USE_SYSTEM_DATE].SetYesNo(0);

		//---- time slot value ----
		sc.Input[IN_SLOT_FILE_PATH].Name = "Time Slot: Data File Path";
		sc.Input[IN_SLOT_FILE_PATH].SetString("C:\\SierraChart\\Data\\FiveMinStats.csv");

		sc.Input[IN_SLOT_SUBTRACT_MINUTES].Name = "Time Slot: Subtract From File Times (minutes)";
		sc.Input[IN_SLOT_SUBTRACT_MINUTES].SetInt(60);
		sc.Input[IN_SLOT_SUBTRACT_MINUTES].SetIntLimits(-720, 720);

		sc.Input[IN_SLOT_MINUTES].Name = "Time Slot: Slot Length (minutes)";
		sc.Input[IN_SLOT_MINUTES].SetInt(5);
		sc.Input[IN_SLOT_MINUTES].SetIntLimits(1, 240);

		sc.Input[IN_SLOT_USE_SYSTEM_CLOCK].Name = "Time Slot: Use System Clock (No = last bar)";
		sc.Input[IN_SLOT_USE_SYSTEM_CLOCK].SetYesNo(1);

		sc.Input[IN_SLOT_SHOW_NEXT].Name = "Time Slot: Also Show Next Slot";
		sc.Input[IN_SLOT_SHOW_NEXT].SetYesNo(1);

		sc.Input[IN_SLOT_RELOAD].Name = "Time Slot: Reload Data File (toggle to refresh)";
		sc.Input[IN_SLOT_RELOAD].SetYesNo(0);

		sc.Input[IN_SLOT_RED_THRESHOLD].Name = "Time Slot: Red At Or Above";
		sc.Input[IN_SLOT_RED_THRESHOLD].SetInt(20);

		sc.Input[IN_SLOT_GREY_THRESHOLD].Name = "Time Slot: Grey At Or Above";
		sc.Input[IN_SLOT_GREY_THRESHOLD].SetInt(15);

		sc.Input[IN_SLOT_GREY_COLOR].Name = "Time Slot: Grey Color";
		sc.Input[IN_SLOT_GREY_COLOR].SetColor(140, 140, 140);

		//---- inside bar scanner ----
		sc.Input[IN_IB_STRICT].Name = "IB: Strict (No = allow equal High/Low)";
		sc.Input[IN_IB_STRICT].SetYesNo(1);

		sc.Input[IN_IB_SKIP_TODAY].Name = "IB: Ignore Today's Unfinished Bar";
		sc.Input[IN_IB_SKIP_TODAY].SetYesNo(1);

		sc.Input[IN_IB_MAX_NAMES].Name = "IB: Max Symbols Named On The Line";
		sc.Input[IN_IB_MAX_NAMES].SetInt(6);
		sc.Input[IN_IB_MAX_NAMES].SetIntLimits(1, MAX_SYMBOLS);

		SCString InputName;
		for (int i = 0; i < MAX_SYMBOLS; ++i)
		{
			InputName.Format("IB Chart %d", i + 1);
			sc.Input[IN_IB_CHART_FIRST + i].Name = InputName;
			sc.Input[IN_IB_CHART_FIRST + i].SetChartNumber(0);
		}

		return;
	}

	//------------------------------------------------------------------------
	// Study removed / chart closed: clean up every drawing this study owns.
	//------------------------------------------------------------------------
	if (sc.LastCallToFunction)
	{
		ClearAll(sc);
		return;
	}

	// On a full recalculation, drop the caches so everything is redrawn.
	if (sc.IsFullRecalculation)
	{
		for (int i = 0; i < NUM_CACHE_SLOTS; ++i)
			sc.GetPersistentSCString(i) = "";
	}

	const COLORREF FlatColor = sc.Input[IN_FLAT_COLOR].GetColor();

	if (sc.ChartDataType == DAILY_DATA)
	{
		EraseLevel(sc, MIDNIGHT_LEVEL);
		EraseLevel(sc, SUNDAY_LEVEL);

		SetTextLine(sc, 0, "Apply this study to an intraday chart", FlatColor);
		for (int i = 1; i < NUM_LINES; ++i)
			SetTextLine(sc, i, "", FlatColor);
		return;
	}

	const int LastIndex = sc.ArraySize - 1;
	if (LastIndex < 1)
		return;

	//------------------------------------------------------------------------
	// Work shared by more than one line, computed once.
	//------------------------------------------------------------------------

	// The newest bar is still forming, so the levels measure from the one
	// before it unless told otherwise.
	int PriceIndex = LastIndex;
	if (sc.Input[IN_USE_COMPLETED_BAR].GetYesNo() && LastIndex >= 1)
		PriceIndex = LastIndex - 1;

	// One RTH open search feeding both the first hour and the opening range.
	const int NeedRthOpen = sc.Input[IN_SHOW_FIRST_HOUR].GetYesNo()
		|| sc.Input[IN_SHOW_OR_SEQUENCE].GetYesNo();

	int RthOpenIndex = -1;
	if (NeedRthOpen)
	{
		RthOpenIndex = FindBoundaryIndex(
			sc,
			HHMMToMinutes(sc.Input[IN_RTH_OPEN_HHMM].GetInt()),
			sc.Input[IN_WINDOW_MINUTES].GetInt(),
			BOUNDARY_WINDOW_ENTRY,
			-1);
	}

	//------------------------------------------------------------------------
	// Build the nine lines. A hidden line contributes nothing, and the ones
	// after it move up, so there is never a gap in the readout.
	//------------------------------------------------------------------------
	SCString LineText[NUM_LINES];
	COLORREF LineColor[NUM_LINES];

	int NextLine = 0;

	for (int Source = 0; Source < NUM_LINES; ++Source)
	{
		SCString Text;
		COLORREF Color = FlatColor;
		int      Show  = 0;

		switch (Source)
		{
		case 0:
			Show = sc.Input[IN_SHOW_QUOTE].GetYesNo();
			if (Show)
				BuildQuoteLine(sc, Text, Color);
			break;

		case 1:
			Show = sc.Input[IN_SHOW_MIDNIGHT].GetYesNo();
			if (Show)
				BuildMidnightLine(sc, PriceIndex, Text, Color);
			else
				EraseLevel(sc, MIDNIGHT_LEVEL);
			break;

		case 2:
			Show = sc.Input[IN_SHOW_SUNDAY].GetYesNo();
			if (Show)
				BuildSundayLine(sc, PriceIndex, Text, Color);
			else
				EraseLevel(sc, SUNDAY_LEVEL);
			break;

		case 3:
			Show = sc.Input[IN_SHOW_PREV_CLOSE].GetYesNo();
			if (Show)
				BuildPrevCloseLine(sc, Text, Color);
			break;

		case 4:
			Show = sc.Input[IN_SHOW_FIRST_HOUR].GetYesNo();
			if (Show)
				BuildFirstHourLine(sc, RthOpenIndex, Text, Color);
			break;

		case 5:
			Show = sc.Input[IN_SHOW_OR_SEQUENCE].GetYesNo();
			if (Show)
				BuildOpeningRangeLine(sc, RthOpenIndex, Text, Color);
			break;

		case 6:
			Show = sc.Input[IN_SHOW_DAY_BIAS].GetYesNo();
			if (Show)
				BuildDayBiasLine(sc, Text, Color);
			break;

		case 7:
			Show = sc.Input[IN_SHOW_TIME_SLOT].GetYesNo();
			if (Show)
				BuildTimeSlotLine(sc, sc.IsFullRecalculation, Text, Color);
			break;

		case 8:
			Show = sc.Input[IN_SHOW_INSIDE_BAR].GetYesNo();
			if (Show)
				BuildInsideBarLine(sc, Text, Color);
			break;
		}

		if (!Show || Text.GetLength() == 0)
			continue;

		LineText[NextLine]  = Text;
		LineColor[NextLine] = Color;
		++NextLine;
	}

	//------------------------------------------------------------------------
	// Draw, then erase anything left over from a previous, longer readout.
	//------------------------------------------------------------------------
	for (int i = 0; i < NextLine; ++i)
		SetTextLine(sc, i, LineText[i], LineColor[i]);

	for (int i = NextLine; i < NUM_LINES; ++i)
		SetTextLine(sc, i, "", FlatColor);
}
