#include "sierrachart.h"

SCDLLName("Day Of Week Bias")

//============================================================================
// DAY OF WEEK BIAS
//
// Displays the historical green/red percentage for the current weekday and
// colors the readout by whichever side is larger.
//
// Defaults are the values from the supplied study:
//   Mon 65.38 / 34.62   Tue 46.15 / 53.85   Wed 53.85 / 46.15
//   Thu 53.85 / 46.15   Fri 52.00 / 48.00
// Red is always 100 - green, so only the green figure is an input.
//
// The weekday is computed from the calendar date with Sakamoto's algorithm
// rather than a date-library call, so it needs nothing beyond GetYear,
// GetMonth and GetDay.
//
// By default the evening session is rolled forward: a bar stamped Sunday
// 18:00 counts as Monday, which is what the underlying statistics assume.
//============================================================================

namespace {

	const int NUM_LINES     = 4;
	const int LINE_NUM_BASE = 95700;

	enum InputIndex
	{
		IN_MON_GREEN = 0,
		IN_TUE_GREEN,
		IN_WED_GREEN,
		IN_THU_GREEN,
		IN_FRI_GREEN,
		IN_USE_TRADE_DATE,
		IN_SESSION_START_HHMM,
		IN_USE_SYSTEM_DATE,
		IN_HORIZ_POS,
		IN_VERT_POS,
		IN_LINE_SPACING,
		IN_FONT_SIZE,
		IN_BOLD,
		IN_TRANSPARENT_BG,
		IN_GREEN_COLOR,
		IN_RED_COLOR,
		IN_FLAT_COLOR
	};

	int HHMMToMinutes(int HHMM)
	{
		return (HHMM / 100) * 60 + (HHMM % 100);
	}

	int MinutesOfDay(const SCDateTime& DT)
	{
		return DT.GetHour() * 60 + DT.GetMinute();
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

	//------------------------------------------------------------------------
	// Text line drawing. Fixed LineNumber + UTAM_ADD_OR_ADJUST replaces the
	// line in place. Color is part of the cache key so a red/green flip on
	// identical text still redraws.
	//------------------------------------------------------------------------
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

		Tool.UseRelativeVerticalValues = 1;
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

	void ClearAllLines(SCStudyInterfaceRef sc)
	{
		for (int i = 0; i < NUM_LINES; ++i)
		{
			sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LINE_NUM_BASE + i);
			sc.GetPersistentSCString(i) = "";
		}
	}
}

//============================================================================

SCSFExport scsf_DayOfWeekBias(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "Day Of Week Bias";
		sc.StudyDescription
			= "Shows the historical green/red percentage for the current weekday.";
		sc.GraphRegion  = 0;
		sc.AutoLoop     = 0;
		sc.UpdateAlways = 1;
		sc.FreeDLL      = 0;
		sc.DrawZeros    = 0;

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

		sc.Input[IN_USE_TRADE_DATE].Name = "Evening Session Counts As Next Day";
		sc.Input[IN_USE_TRADE_DATE].SetYesNo(1);

		sc.Input[IN_SESSION_START_HHMM].Name = "Session Start (HHMM, chart timezone)";
		sc.Input[IN_SESSION_START_HHMM].SetInt(1700);

		sc.Input[IN_USE_SYSTEM_DATE].Name = "Use System Clock Instead Of Last Bar";
		sc.Input[IN_USE_SYSTEM_DATE].SetYesNo(0);

		sc.Input[IN_HORIZ_POS].Name = "Horizontal Position (0-150)";
		sc.Input[IN_HORIZ_POS].SetInt(2);

		sc.Input[IN_VERT_POS].Name = "Vertical Position (0-100, 100 = top)";
		sc.Input[IN_VERT_POS].SetFloat(98.0f);

		sc.Input[IN_LINE_SPACING].Name = "Line Spacing (percent of height)";
		sc.Input[IN_LINE_SPACING].SetFloat(3.0f);

		sc.Input[IN_FONT_SIZE].Name = "Font Size";
		sc.Input[IN_FONT_SIZE].SetInt(12);

		sc.Input[IN_BOLD].Name = "Bold Text";
		sc.Input[IN_BOLD].SetYesNo(1);

		sc.Input[IN_TRANSPARENT_BG].Name = "Transparent Background";
		sc.Input[IN_TRANSPARENT_BG].SetYesNo(1);

		sc.Input[IN_GREEN_COLOR].Name = "Green Bias Color";
		sc.Input[IN_GREEN_COLOR].SetColor(0, 220, 0);

		sc.Input[IN_RED_COLOR].Name = "Red Bias Color";
		sc.Input[IN_RED_COLOR].SetColor(255, 60, 60);

		sc.Input[IN_FLAT_COLOR].Name = "Neutral Color";
		sc.Input[IN_FLAT_COLOR].SetColor(190, 190, 190);

		return;
	}

	if (sc.LastCallToFunction)
	{
		ClearAllLines(sc);
		return;
	}

	if (sc.IsFullRecalculation)
	{
		for (int i = 0; i < NUM_LINES; ++i)
			sc.GetPersistentSCString(i) = "";
	}

	const COLORREF FlatColor = sc.Input[IN_FLAT_COLOR].GetColor();

	//------------------------------------------------------------------------
	// Which weekday are we on
	//------------------------------------------------------------------------
	SCDateTime ReferenceDT;

	if (sc.Input[IN_USE_SYSTEM_DATE].GetYesNo() || sc.ArraySize == 0)
	{
		ReferenceDT = sc.CurrentSystemDateTime;
	}
	else
	{
		ReferenceDT = sc.BaseDateTimeIn[sc.ArraySize - 1];
	}

	int DayOfWeek = DayOfWeekFromDate(
		ReferenceDT.GetYear(), ReferenceDT.GetMonth(), ReferenceDT.GetDay());

	if (DayOfWeek < 0)
		return;

	// Roll the evening session onto the next weekday. Advancing the weekday
	// index sidesteps calendar arithmetic entirely.
	if (sc.Input[IN_USE_TRADE_DATE].GetYesNo()
		&& MinutesOfDay(ReferenceDT) >= HHMMToMinutes(sc.Input[IN_SESSION_START_HHMM].GetInt()))
	{
		DayOfWeek = (DayOfWeek + 1) % 7;
	}

	static const char* DayNames[7] =
		{ "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY" };

	if (DayOfWeek < 1 || DayOfWeek > 5)
	{
		SCString WeekendText;
		WeekendText.Format("%s - no weekday statistics", DayNames[DayOfWeek]);
		SetTextLine(sc, 0, WeekendText, FlatColor);
		for (int i = 1; i < NUM_LINES; ++i)
			SetTextLine(sc, i, "", FlatColor);
		return;
	}

	//------------------------------------------------------------------------
	// Look up the figures. Monday is weekday 1, which is input index 0.
	//------------------------------------------------------------------------
	const double GreenPercent = sc.Input[IN_MON_GREEN + (DayOfWeek - 1)].GetFloat();
	const double RedPercent   = 100.0 - GreenPercent;
	const double Edge         = GreenPercent - RedPercent;

	COLORREF LineColor = FlatColor;
	SCString BiasText = "NO EDGE - EVEN SPLIT";

	if (GreenPercent > RedPercent)
	{
		LineColor = sc.Input[IN_GREEN_COLOR].GetColor();
		BiasText = "GREEN DAY BIAS";
	}
	else if (RedPercent > GreenPercent)
	{
		LineColor = sc.Input[IN_RED_COLOR].GetColor();
		BiasText = "RED DAY BIAS";
	}

	//------------------------------------------------------------------------
	// Display
	//------------------------------------------------------------------------
	SCString Text;

	Text.Format("%s  %02d/%02d",
		DayNames[DayOfWeek], ReferenceDT.GetMonth(), ReferenceDT.GetDay());
	SetTextLine(sc, 0, Text, LineColor);

	Text.Format("GREEN %.2f%%   RED %.2f%%", GreenPercent, RedPercent);
	SetTextLine(sc, 1, Text, LineColor);

	Text.Format("SKEW: %s%.2f%%", (Edge >= 0.0) ? "+" : "-",
		(Edge >= 0.0) ? Edge : -Edge);
	SetTextLine(sc, 2, Text, LineColor);

	SetTextLine(sc, 3, BiasText, LineColor);
}
