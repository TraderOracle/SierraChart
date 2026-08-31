#include "sierrachart.h"

SCDLLName("Opening Range Sequence")

//============================================================================
// 1H OPENING RANGE SEQUENCE
//
// Apply to an INTRADAY chart, timezone set to Central so 08:30 on the chart
// is the NY open.
//
// Walks the first hour after the open and records WHEN the range high and the
// range low were each put in. The extremes are captured with strict
// comparisons, so a level that is retested later keeps the timestamp of its
// first occurrence.
//
//   Low formed first  -> "Breakout GREEN"
//   High formed first -> "Breakout RED"
//
// The session open bar is found by walking backwards, so weekends and
// holidays need no special handling.
//============================================================================

namespace {

	const int NUM_LINES     = 4;
	const int LINE_NUM_BASE = 94600;

	enum InputIndex
	{
		IN_RTH_OPEN_HHMM = 0,
		IN_DURATION_MINUTES,
		IN_RESOLVE_SAME_BAR,
		IN_ONLY_TODAY,
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

	//------------------------------------------------------------------------
	// Index of the most recent RTH opening bar. A bar qualifies when it sits
	// at or just after the open time and the bar before it is either earlier
	// in the day or on a different date. The tolerance window rejects the
	// Sunday evening reopen, whose predecessor is also on a different date.
	//------------------------------------------------------------------------
	int FindSessionOpenIndex(SCStudyInterfaceRef sc, int OpenMinutes, int ToleranceMinutes)
	{
		const int LastIndex = sc.ArraySize - 1;

		for (int i = LastIndex; i >= 1; --i)
		{
			const int CurrentMinutes = MinutesOfDay(sc.BaseDateTimeIn[i]);

			if (CurrentMinutes < OpenMinutes || CurrentMinutes >= OpenMinutes + ToleranceMinutes)
				continue;

			const int PreviousMinutes = MinutesOfDay(sc.BaseDateTimeIn[i - 1]);
			const int DateChanged =
				(sc.BaseDateTimeIn[i].GetDate() != sc.BaseDateTimeIn[i - 1].GetDate()) ? 1 : 0;

			if (PreviousMinutes < OpenMinutes || DateChanged)
				return i;
		}

		return -1;
	}
}

//============================================================================

SCSFExport scsf_OpeningRangeSequence(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "1H Opening Range Sequence";
		sc.StudyDescription
			= "Reports whether the opening range low or high was formed first, and "
			  "labels the expected breakout direction.";
		sc.GraphRegion  = 0;
		sc.AutoLoop     = 0;
		sc.UpdateAlways = 1;
		sc.FreeDLL      = 0;
		sc.DrawZeros    = 0;

		sc.Input[IN_RTH_OPEN_HHMM].Name = "RTH Open (HHMM, chart timezone)";
		sc.Input[IN_RTH_OPEN_HHMM].SetInt(830);

		sc.Input[IN_DURATION_MINUTES].Name = "Opening Range (minutes)";
		sc.Input[IN_DURATION_MINUTES].SetInt(60);
		sc.Input[IN_DURATION_MINUTES].SetIntLimits(1, 720);

		sc.Input[IN_RESOLVE_SAME_BAR].Name = "Resolve Same-Bar Case By Bar Direction";
		sc.Input[IN_RESOLVE_SAME_BAR].SetYesNo(1);

		sc.Input[IN_ONLY_TODAY].Name = "Only Show Current Session";
		sc.Input[IN_ONLY_TODAY].SetYesNo(0);

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

		sc.Input[IN_GREEN_COLOR].Name = "Breakout GREEN Color";
		sc.Input[IN_GREEN_COLOR].SetColor(0, 220, 0);

		sc.Input[IN_RED_COLOR].Name = "Breakout RED Color";
		sc.Input[IN_RED_COLOR].SetColor(255, 60, 60);

		sc.Input[IN_FLAT_COLOR].Name = "Undetermined Color";
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

	if (sc.ChartDataType == DAILY_DATA)
	{
		SetTextLine(sc, 0, "Apply this study to an intraday chart", FlatColor);
		for (int i = 1; i < NUM_LINES; ++i)
			SetTextLine(sc, i, "", FlatColor);
		return;
	}

	const int LastIndex = sc.ArraySize - 1;
	if (LastIndex < 1)
		return;

	const int OpenMinutes     = HHMMToMinutes(sc.Input[IN_RTH_OPEN_HHMM].GetInt());
	const int DurationMinutes = sc.Input[IN_DURATION_MINUTES].GetInt();
	const int EndMinutes      = OpenMinutes + DurationMinutes;

	const int OpenIndex = FindSessionOpenIndex(sc, OpenMinutes, DurationMinutes);

	if (OpenIndex < 0)
	{
		SetTextLine(sc, 0, "RTH open not found in loaded data", FlatColor);
		for (int i = 1; i < NUM_LINES; ++i)
			SetTextLine(sc, i, "", FlatColor);
		return;
	}

	const int SessionDate = sc.BaseDateTimeIn[OpenIndex].GetDate();

	if (sc.Input[IN_ONLY_TODAY].GetYesNo()
		&& sc.BaseDateTimeIn[LastIndex].GetDate() != SessionDate)
	{
		for (int i = 0; i < NUM_LINES; ++i)
			SetTextLine(sc, i, "", FlatColor);
		return;
	}

	//------------------------------------------------------------------------
	// Walk the range, keeping the FIRST bar that made each extreme.
	// Strict > and < are what make a later retest leave the index alone.
	//------------------------------------------------------------------------
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

	//------------------------------------------------------------------------
	// Sequence and verdict
	//------------------------------------------------------------------------
	COLORREF LineColor = FlatColor;
	SCString SequenceText;
	SCString Verdict;

	if (LowIndex < HighIndex)
	{
		LineColor = sc.Input[IN_GREEN_COLOR].GetColor();
		SequenceText = "LOW FORMED FIRST";
		Verdict = "Breakout GREEN";
	}
	else if (HighIndex < LowIndex)
	{
		LineColor = sc.Input[IN_RED_COLOR].GetColor();
		SequenceText = "HIGH FORMED FIRST";
		Verdict = "Breakout RED";
	}
	else
	{
		// Both extremes belong to the same bar, so the bar data alone cannot
		// say which came first. Optionally infer it from that bar's direction:
		// a bar that closed up most likely made its low before its high.
		SequenceText = "HIGH AND LOW ON SAME BAR";
		Verdict = "Undetermined";

		if (sc.Input[IN_RESOLVE_SAME_BAR].GetYesNo())
		{
			const double BarOpen  = sc.Open[HighIndex];
			const double BarClose = sc.Close[HighIndex];

			if (BarClose > BarOpen)
			{
				LineColor = sc.Input[IN_GREEN_COLOR].GetColor();
				SequenceText = "SAME BAR - UP BAR, LOW ASSUMED FIRST";
				Verdict = "Breakout GREEN";
			}
			else if (BarClose < BarOpen)
			{
				LineColor = sc.Input[IN_RED_COLOR].GetColor();
				SequenceText = "SAME BAR - DOWN BAR, HIGH ASSUMED FIRST";
				Verdict = "Breakout RED";
			}
		}
	}

	if (!IsComplete)
		Verdict += "  (range still forming)";

	//------------------------------------------------------------------------
	// Display
	//------------------------------------------------------------------------
	const int ValueFormat = sc.GetValueFormat();
	const SCDateTime OpenDT = sc.BaseDateTimeIn[OpenIndex];
	const SCDateTime HighDT = sc.BaseDateTimeIn[HighIndex];
	const SCDateTime LowDT  = sc.BaseDateTimeIn[LowIndex];

	SCString Text;

	Text.Format("OPENING RANGE  %02d/%02d  %02d:%02d - %02d:%02d",
		OpenDT.GetMonth(), OpenDT.GetDay(),
		OpenMinutes / 60, OpenMinutes % 60,
		EndMinutes / 60, EndMinutes % 60);
	SetTextLine(sc, 0, Text, LineColor);

	Text.Format("HIGH %s @ %02d:%02d   LOW %s @ %02d:%02d",
		sc.FormatGraphValue(RangeHigh, ValueFormat).GetChars(),
		HighDT.GetHour(), HighDT.GetMinute(),
		sc.FormatGraphValue(RangeLow, ValueFormat).GetChars(),
		LowDT.GetHour(), LowDT.GetMinute());
	SetTextLine(sc, 1, Text, LineColor);

	SetTextLine(sc, 2, SequenceText, LineColor);
	SetTextLine(sc, 3, Verdict, LineColor);
}
