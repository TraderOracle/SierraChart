#include "sierrachart.h"

SCDLLName("Sunday Open Level")

//============================================================================
// SUNDAY OPEN LEVEL
//
// Apply to an INTRADAY chart, timezone set to Central so 17:00 on the chart
// is the 5pm Sunday reopen.
//
// The reopen bar is pinned by three conditions checked while walking
// backwards: the bar sits at or just after 17:00, the bar before it is on a
// different date (so it is the first bar of a session, not a mid-evening
// bar), and its weekday is Sunday. The weekday comes from Sakamoto's
// algorithm on the calendar date, so it needs nothing beyond GetYear,
// GetMonth and GetDay.
//
// The level is that bar's OPEN, i.e. the first print of the trading week.
//
// Drawing and colors match the Midnight Level study:
//   level BELOW current price -> red
//   level ABOVE current price -> green
//
// Everything recalculates when a new bar opens, not on every tick.
//
// NOTE: the chart must load enough history to reach back to Sunday evening.
// By Friday that is five days, so set Days to Load to 7 or more.
//============================================================================

namespace {

	const int NUM_LINES      = 3;
	const int LINE_NUM_BASE  = 98100;
	const int LEVEL_LINE_NUM = LINE_NUM_BASE + 50;
	const int LABEL_LINE_NUM = LINE_NUM_BASE + 51;
	const int LEVEL_CACHE_IX = NUM_LINES;       // persistent slot 3
	const int LABEL_CACHE_IX = NUM_LINES + 1;   // persistent slot 4

	enum InputIndex
	{
		IN_LABEL_TEXT = 0,
		IN_SESSION_START_HHMM,
		IN_TOLERANCE_MINUTES,
		IN_REQUIRE_SUNDAY,
		IN_USE_COMPLETED_BAR,
		IN_INVERT_COLORS,
		IN_LINE_WIDTH,
		IN_DASHED_LINE,
		IN_HORIZ_POS,
		IN_VERT_POS,
		IN_LINE_SPACING,
		IN_FONT_SIZE,
		IN_BOLD,
		IN_TRANSPARENT_BG,
		IN_ABOVE_COLOR,
		IN_BELOW_COLOR,
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

	double LineTopValue(SCStudyInterfaceRef sc, int LineIndex)
	{
		return sc.Input[IN_VERT_POS].GetFloat()
		       - LineIndex * sc.Input[IN_LINE_SPACING].GetFloat();
	}

	//------------------------------------------------------------------------
	// First bar of the most recent Sunday evening session.
	//------------------------------------------------------------------------
	int FindSundayOpenIndex(SCStudyInterfaceRef sc, int StartMinutes, int ToleranceMinutes, int RequireSunday)
	{
		const int LastIndex = sc.ArraySize - 1;

		for (int i = LastIndex; i >= 1; --i)
		{
			const int CurrentMinutes = MinutesOfDay(sc.BaseDateTimeIn[i]);

			if (CurrentMinutes < StartMinutes || CurrentMinutes >= StartMinutes + ToleranceMinutes)
				continue;

			// Must be the first bar of a session, not a bar later that evening.
			if (sc.BaseDateTimeIn[i].GetDate() == sc.BaseDateTimeIn[i - 1].GetDate())
				continue;

			if (RequireSunday)
			{
				const SCDateTime& DT = sc.BaseDateTimeIn[i];
				if (DayOfWeekFromDate(DT.GetYear(), DT.GetMonth(), DT.GetDay()) != 0)
					continue;
			}

			return i;
		}

		return -1;
	}

	//------------------------------------------------------------------------
	// Readout text, positioned in relative screen units.
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
		Tool.BeginValue    = LineTopValue(sc, LineIndex);

		Tool.Text          = Text;
		Tool.Color         = TextColor;
		Tool.FontSize      = sc.Input[IN_FONT_SIZE].GetInt();
		Tool.FontBold      = sc.Input[IN_BOLD].GetYesNo();
		Tool.TransparentLabelBackground = sc.Input[IN_TRANSPARENT_BG].GetYesNo();
		Tool.TextAlignment = DT_LEFT | DT_TOP;

		sc.UseTool(Tool);
	}

	//------------------------------------------------------------------------
	// The level line and its right hand label, in real chart coordinates.
	//------------------------------------------------------------------------
	void SetLevelLine(SCStudyInterfaceRef sc, int AnchorIndex, double Price, COLORREF LineColor)
	{
		SCString CacheKey;
		CacheKey.Format("L|%u|%.8f|%d", (unsigned int)LineColor, Price, AnchorIndex);

		SCString& LastKey = sc.GetPersistentSCString(LEVEL_CACHE_IX);
		if (strcmp(LastKey.GetChars(), CacheKey.GetChars()) == 0)
			return;

		LastKey = CacheKey;

		s_UseTool Tool;
		Tool.Clear();
		Tool.ChartNumber  = sc.ChartNumber;
		Tool.Region       = sc.GraphRegion;
		Tool.DrawingType  = DRAWING_HORIZONTALLINE;
		Tool.LineNumber   = LEVEL_LINE_NUM;
		Tool.AddMethod    = UTAM_ADD_OR_ADJUST;
		Tool.AddAsUserDrawnDrawing = 0;

		Tool.BeginDateTime = sc.BaseDateTimeIn[AnchorIndex];
		Tool.BeginValue    = (float)Price;

		Tool.Color     = LineColor;
		Tool.LineWidth = sc.Input[IN_LINE_WIDTH].GetInt();
		Tool.LineStyle = sc.Input[IN_DASHED_LINE].GetYesNo() ? LINESTYLE_DASH : LINESTYLE_SOLID;

		sc.UseTool(Tool);
	}

	void SetLevelLabel(SCStudyInterfaceRef sc, double Price, COLORREF LabelColor, const SCString& LabelText)
	{
		const int LastIndex = sc.ArraySize - 1;

		SCString CacheKey;
		CacheKey.Format("T|%u|%.8f|%s", (unsigned int)LabelColor, Price, LabelText.GetChars());

		SCString& LastKey = sc.GetPersistentSCString(LABEL_CACHE_IX);
		if (strcmp(LastKey.GetChars(), CacheKey.GetChars()) == 0)
			return;

		LastKey = CacheKey;

		s_UseTool Tool;
		Tool.Clear();
		Tool.ChartNumber  = sc.ChartNumber;
		Tool.Region       = sc.GraphRegion;
		Tool.DrawingType  = DRAWING_TEXT;
		Tool.LineNumber   = LABEL_LINE_NUM;
		Tool.AddMethod    = UTAM_ADD_OR_ADJUST;
		Tool.AddAsUserDrawnDrawing = 0;

		// Anchored at the newest bar, left aligned so the text runs out into
		// the empty space to the right of the chart.
		Tool.BeginDateTime = sc.BaseDateTimeIn[LastIndex];
		Tool.BeginValue    = (float)Price;

		Tool.Text          = LabelText;
		Tool.Color         = LabelColor;
		Tool.FontSize      = sc.Input[IN_FONT_SIZE].GetInt();
		Tool.FontBold      = sc.Input[IN_BOLD].GetYesNo();
		Tool.TransparentLabelBackground = 1;
		Tool.TextAlignment = DT_LEFT | DT_VCENTER;

		sc.UseTool(Tool);
	}

	void ClearAll(SCStudyInterfaceRef sc)
	{
		for (int i = 0; i < NUM_LINES; ++i)
		{
			sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LINE_NUM_BASE + i);
			sc.GetPersistentSCString(i) = "";
		}

		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LEVEL_LINE_NUM);
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LABEL_LINE_NUM);
		sc.GetPersistentSCString(LEVEL_CACHE_IX) = "";
		sc.GetPersistentSCString(LABEL_CACHE_IX) = "";
	}
}

//============================================================================

SCSFExport scsf_SundayOpenLevel(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "Sunday Open Level";
		sc.StudyDescription
			= "Draws the Sunday 5pm session opening price and reports the current "
			  "distance from it.";
		sc.GraphRegion  = 0;
		sc.AutoLoop     = 0;
		sc.UpdateAlways = 0;   // bar close driven, not tick driven
		sc.FreeDLL      = 0;
		sc.DrawZeros    = 0;

		// Explicit UTF-8 bytes so the glyph survives the source file encoding.
		// F0 9F 8C 85 is U+1F305 sunrise.
		sc.Input[IN_LABEL_TEXT].Name = "Label Text";
		sc.Input[IN_LABEL_TEXT].SetString("\xF0\x9F\x8C\x85 sunday open");

		sc.Input[IN_SESSION_START_HHMM].Name = "Session Open (HHMM, chart timezone)";
		sc.Input[IN_SESSION_START_HHMM].SetInt(1700);

		sc.Input[IN_TOLERANCE_MINUTES].Name = "Open Detect Window (minutes)";
		sc.Input[IN_TOLERANCE_MINUTES].SetInt(60);
		sc.Input[IN_TOLERANCE_MINUTES].SetIntLimits(1, 720);

		sc.Input[IN_REQUIRE_SUNDAY].Name = "Require Sunday (No = any weekly reopen)";
		sc.Input[IN_REQUIRE_SUNDAY].SetYesNo(1);

		sc.Input[IN_USE_COMPLETED_BAR].Name = "Measure From Last Completed Bar";
		sc.Input[IN_USE_COMPLETED_BAR].SetYesNo(1);

		sc.Input[IN_INVERT_COLORS].Name = "Invert Colors";
		sc.Input[IN_INVERT_COLORS].SetYesNo(0);

		sc.Input[IN_LINE_WIDTH].Name = "Line Width";
		sc.Input[IN_LINE_WIDTH].SetInt(1);
		sc.Input[IN_LINE_WIDTH].SetIntLimits(1, 10);

		sc.Input[IN_DASHED_LINE].Name = "Dashed Line";
		sc.Input[IN_DASHED_LINE].SetYesNo(1);

		sc.Input[IN_HORIZ_POS].Name = "Text Horizontal Position (0-150)";
		sc.Input[IN_HORIZ_POS].SetInt(2);

		sc.Input[IN_VERT_POS].Name = "Text Vertical Position (0-100, 100 = top)";
		sc.Input[IN_VERT_POS].SetFloat(98.0f);

		sc.Input[IN_LINE_SPACING].Name = "Line Spacing (percent of height)";
		sc.Input[IN_LINE_SPACING].SetFloat(3.0f);

		sc.Input[IN_FONT_SIZE].Name = "Font Size";
		sc.Input[IN_FONT_SIZE].SetInt(12);

		sc.Input[IN_BOLD].Name = "Bold Text";
		sc.Input[IN_BOLD].SetYesNo(1);

		sc.Input[IN_TRANSPARENT_BG].Name = "Transparent Background";
		sc.Input[IN_TRANSPARENT_BG].SetYesNo(1);

		sc.Input[IN_ABOVE_COLOR].Name = "Level Above Price Color";
		sc.Input[IN_ABOVE_COLOR].SetColor(0, 220, 0);

		sc.Input[IN_BELOW_COLOR].Name = "Level Below Price Color";
		sc.Input[IN_BELOW_COLOR].SetColor(255, 60, 60);

		sc.Input[IN_FLAT_COLOR].Name = "At Level Color";
		sc.Input[IN_FLAT_COLOR].SetColor(190, 190, 190);

		return;
	}

	if (sc.LastCallToFunction)
	{
		ClearAll(sc);
		return;
	}

	if (sc.IsFullRecalculation)
	{
		for (int i = 0; i < NUM_LINES; ++i)
			sc.GetPersistentSCString(i) = "";
		sc.GetPersistentSCString(LEVEL_CACHE_IX) = "";
		sc.GetPersistentSCString(LABEL_CACHE_IX) = "";
	}

	const COLORREF FlatColor = sc.Input[IN_FLAT_COLOR].GetColor();

	if (sc.ChartDataType == DAILY_DATA)
	{
		SetTextLine(sc, 0, "Apply this study to an intraday chart", FlatColor);
		return;
	}

	const int LastIndex = sc.ArraySize - 1;
	if (LastIndex < 1)
		return;

	//------------------------------------------------------------------------
	// Only do the work when a new bar has opened.
	//------------------------------------------------------------------------
	int& LastBarCount = sc.GetPersistentInt(0);

	if (sc.ArraySize == LastBarCount && !sc.IsFullRecalculation)
		return;

	LastBarCount = sc.ArraySize;

	//------------------------------------------------------------------------
	// Sunday open
	//------------------------------------------------------------------------
	const int OpenIndex = FindSundayOpenIndex(
		sc,
		HHMMToMinutes(sc.Input[IN_SESSION_START_HHMM].GetInt()),
		sc.Input[IN_TOLERANCE_MINUTES].GetInt(),
		sc.Input[IN_REQUIRE_SUNDAY].GetYesNo());

	if (OpenIndex < 0)
	{
		SetTextLine(sc, 0, "Sunday open not in loaded data", FlatColor);
		SetTextLine(sc, 1, "Increase Days to Load in Chart Settings", FlatColor);
		SetTextLine(sc, 2, "", FlatColor);
		return;
	}

	const double SundayOpen = sc.Open[OpenIndex];

	// The newest bar is still forming, so measure from the one before it.
	int PriceIndex = LastIndex;
	if (sc.Input[IN_USE_COMPLETED_BAR].GetYesNo() && LastIndex >= 1)
		PriceIndex = LastIndex - 1;

	const double CurrentPrice = sc.Close[PriceIndex];
	const double Distance     = CurrentPrice - SundayOpen;

	//------------------------------------------------------------------------
	// Color: level below price is red, level above price is green.
	//------------------------------------------------------------------------
	COLORREF AboveColor = sc.Input[IN_ABOVE_COLOR].GetColor();
	COLORREF BelowColor = sc.Input[IN_BELOW_COLOR].GetColor();

	if (sc.Input[IN_INVERT_COLORS].GetYesNo())
	{
		const COLORREF Swap = AboveColor;
		AboveColor = BelowColor;
		BelowColor = Swap;
	}

	COLORREF LineColor = FlatColor;
	SCString RelationText = "PRICE AT SUNDAY OPEN";

	if (Distance > 0.0)
	{
		LineColor = BelowColor;             // level is below current price
		RelationText = "SUNDAY OPEN BELOW PRICE";
	}
	else if (Distance < 0.0)
	{
		LineColor = AboveColor;             // level is above current price
		RelationText = "SUNDAY OPEN ABOVE PRICE";
	}

	//------------------------------------------------------------------------
	// Draw
	//------------------------------------------------------------------------
	SetLevelLine(sc, OpenIndex, SundayOpen, LineColor);

	SCString LabelText = sc.Input[IN_LABEL_TEXT].GetString();
	SetLevelLabel(sc, SundayOpen, LineColor, LabelText);

	const int ValueFormat = sc.GetValueFormat();
	const SCDateTime OpenDT = sc.BaseDateTimeIn[OpenIndex];

	int Ticks = 0;
	if (sc.TickSize > 0.0)
		Ticks = (int)(AbsValue(Distance) / sc.TickSize + 0.5);

	SCString Text;

	Text.Format("Sunday Open Retrace (good on Monday): %02d/%02d %02d:%02d  %s",
		OpenDT.GetMonth(), OpenDT.GetDay(),
		OpenDT.GetHour(), OpenDT.GetMinute(),
		sc.FormatGraphValue(SundayOpen, ValueFormat).GetChars());
	SetTextLine(sc, 0, Text, LineColor);

	Text.Format("DIST: %s%s (%d ticks)",
		(Distance >= 0.0) ? "+" : "-",
		sc.FormatGraphValue(AbsValue(Distance), ValueFormat).GetChars(),
		Ticks);
	SetTextLine(sc, 1, Text, LineColor);

	SetTextLine(sc, 2, RelationText, LineColor);
}
