#include "sierrachart.h"

SCDLLName("Midnight Level")

//============================================================================
// MIDNIGHT LEVEL
//
// Apply to an INTRADAY chart, timezone set to Central so 00:00 on the chart
// is midnight CST.
//
// The midnight bar is the first bar of a new calendar date, found by walking
// backwards for the most recent date change. A tolerance window rejects the
// Sunday evening reopen, which is also a date change but happens at 17:00.
// Its close is the 00:00-00:01 closing price on a 1 minute chart.
//
// Draws a horizontal line at that price with a label on the right, plus a
// readout of how far the current price sits from it.
//
//   midnight BELOW current price -> red
//   midnight ABOVE current price -> green
//
// Everything recalculates when a new bar opens, not on every tick.
//============================================================================

namespace {

	const int NUM_LINES      = 3;
	const int LINE_NUM_BASE  = 97900;
	const int LEVEL_LINE_NUM = LINE_NUM_BASE + 50;
	const int LABEL_LINE_NUM = LINE_NUM_BASE + 51;
	const int LEVEL_CACHE_IX = NUM_LINES;       // persistent slot 3
	const int LABEL_CACHE_IX = NUM_LINES + 1;   // persistent slot 4

	enum InputIndex
	{
		IN_LABEL_TEXT = 0,
		IN_TOLERANCE_MINUTES,
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

	int MinutesOfDay(const SCDateTime& DT)
	{
		return DT.GetHour() * 60 + DT.GetMinute();
	}

	double AbsValue(double Value)
	{
		return (Value < 0.0) ? -Value : Value;
	}

	double LineTopValue(SCStudyInterfaceRef sc, int LineIndex)
	{
		return sc.Input[IN_VERT_POS].GetFloat()
		       - LineIndex * sc.Input[IN_LINE_SPACING].GetFloat();
	}

	//------------------------------------------------------------------------
	// First bar of the most recent calendar date.
	//------------------------------------------------------------------------
	int FindMidnightIndex(SCStudyInterfaceRef sc, int ToleranceMinutes)
	{
		const int LastIndex = sc.ArraySize - 1;

		for (int i = LastIndex; i >= 1; --i)
		{
			if (sc.BaseDateTimeIn[i].GetDate() == sc.BaseDateTimeIn[i - 1].GetDate())
				continue;

			// A date change late in the day is a session reopen, not midnight.
			if (MinutesOfDay(sc.BaseDateTimeIn[i]) >= ToleranceMinutes)
				continue;

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
	// The level line and its right hand label. Both use real chart
	// coordinates, so no relative positioning here.
	//------------------------------------------------------------------------
	void SetLevelLine(SCStudyInterfaceRef sc, int MidnightIndex, double Price, COLORREF LineColor)
	{
		SCString CacheKey;
		CacheKey.Format("L|%u|%.8f|%d", (unsigned int)LineColor, Price, MidnightIndex);

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

		Tool.BeginDateTime = sc.BaseDateTimeIn[MidnightIndex];
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

		// Anchored at the newest bar, drawn leftwards-aligned so the text runs
		// out into the empty space on the right of the chart.
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

SCSFExport scsf_MidnightLevel(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "Midnight Level";
		sc.StudyDescription
			= "Draws the midnight closing price and reports the current distance from it.";
		sc.GraphRegion  = 0;
		sc.AutoLoop     = 0;
		sc.UpdateAlways = 0;   // bar close driven, not tick driven
		sc.FreeDLL      = 0;
		sc.DrawZeros    = 0;

		// Written as explicit UTF-8 bytes so the moon survives whatever
		// encoding the source file is saved in. F0 9F 8C 99 is U+1F319.
		sc.Input[IN_LABEL_TEXT].Name = "Label Text";
		sc.Input[IN_LABEL_TEXT].SetString("\xF0\x9F\x8C\x99 midnight");

		sc.Input[IN_TOLERANCE_MINUTES].Name = "Midnight Detect Window (minutes)";
		sc.Input[IN_TOLERANCE_MINUTES].SetInt(60);
		sc.Input[IN_TOLERANCE_MINUTES].SetIntLimits(1, 720);

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

		sc.Input[IN_ABOVE_COLOR].Name = "Midnight Above Price Color";
		sc.Input[IN_ABOVE_COLOR].SetColor(0, 220, 0);

		sc.Input[IN_BELOW_COLOR].Name = "Midnight Below Price Color";
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
	// Midnight close
	//------------------------------------------------------------------------
	const int MidnightIndex =
		FindMidnightIndex(sc, sc.Input[IN_TOLERANCE_MINUTES].GetInt());

	if (MidnightIndex < 0)
	{
		SetTextLine(sc, 0, "Midnight bar not in loaded data", FlatColor);
		SetTextLine(sc, 1, "", FlatColor);
		SetTextLine(sc, 2, "", FlatColor);
		return;
	}

	const double MidnightClose = sc.Close[MidnightIndex];

	// The newest bar is still forming, so measure from the one before it.
	int PriceIndex = LastIndex;
	if (sc.Input[IN_USE_COMPLETED_BAR].GetYesNo() && LastIndex >= 1)
		PriceIndex = LastIndex - 1;

	const double CurrentPrice = sc.Close[PriceIndex];
	const double Distance     = CurrentPrice - MidnightClose;

	//------------------------------------------------------------------------
	// Color: midnight below price is red, midnight above price is green.
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
	SCString RelationText = "PRICE AT MIDNIGHT LEVEL";

	if (Distance > 0.0)
	{
		LineColor = BelowColor;              // midnight is below current price
		RelationText = "MIDNIGHT BELOW PRICE";
	}
	else if (Distance < 0.0)
	{
		LineColor = AboveColor;              // midnight is above current price
		RelationText = "MIDNIGHT ABOVE PRICE";
	}

	//------------------------------------------------------------------------
	// Draw
	//------------------------------------------------------------------------
	SetLevelLine(sc, MidnightIndex, MidnightClose, LineColor);

	SCString LabelText = sc.Input[IN_LABEL_TEXT].GetString();
	SetLevelLabel(sc, MidnightClose, LineColor, LabelText);

	const int ValueFormat = sc.GetValueFormat();
	const SCDateTime MidnightDT = sc.BaseDateTimeIn[MidnightIndex];

	int Ticks = 0;
	if (sc.TickSize > 0.0)
		Ticks = (int)(AbsValue(Distance) / sc.TickSize + 0.5);

	SCString Text;

	Text.Format("MIDNIGHT %02d/%02d %02d:%02d  %s",
		MidnightDT.GetMonth(), MidnightDT.GetDay(),
		MidnightDT.GetHour(), MidnightDT.GetMinute(),
		sc.FormatGraphValue(MidnightClose, ValueFormat).GetChars());
	SetTextLine(sc, 0, Text, LineColor);

	Text.Format("DIST: %s%s (%d ticks)",
		(Distance >= 0.0) ? "+" : "-",
		sc.FormatGraphValue(AbsValue(Distance), ValueFormat).GetChars(),
		Ticks);
	SetTextLine(sc, 1, Text, LineColor);

	SetTextLine(sc, 2, RelationText, LineColor);
}
