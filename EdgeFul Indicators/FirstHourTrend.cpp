#include "sierrachart.h"

SCDLLName("First Hour Trend")

//============================================================================
// NY FIRST HOUR TREND
//
// Apply to an INTRADAY chart, timezone set to Central so 08:30 on the chart
// is the NY open.
//
// The session open bar is located by walking backwards for the most recent
// crossing of the RTH open time, so weekends and holidays never need special
// handling: before Monday's open the search skips straight past the Sunday
// evening reopen and lands on Friday's 08:30, because Sunday 17:00 is not an
// 08:30 crossing.
//
// Once the first hour completes, the open-to-close direction of that window
// is reported as a GREEN DAY or a RED DAY.
//============================================================================

namespace {

	const int NUM_LINES     = 4;
	const int LINE_NUM_BASE = 93500;

	enum InputIndex
	{
		IN_RTH_OPEN_HHMM = 0,
		IN_DURATION_MINUTES,
		IN_MIN_TICKS,
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

	double AbsValue(double Value)
	{
		return (Value < 0.0) ? -Value : Value;
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
	// Index of the most recent RTH opening bar.
	//
	// A bar qualifies when it sits at or just after the open time and the bar
	// before it is either earlier in the day or on a different date. The
	// "just after" window is what rejects the Sunday 17:00 reopen, whose
	// predecessor is Friday's last bar on a different date.
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

SCSFExport scsf_FirstHourTrend(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "NY First Hour Trend";
		sc.StudyDescription
			= "Measures the direction of the first hour after the NY open and labels "
			  "the session a green day or a red day.";
		sc.GraphRegion  = 0;
		sc.AutoLoop     = 0;
		sc.UpdateAlways = 1;
		sc.FreeDLL      = 0;
		sc.DrawZeros    = 0;

		sc.Input[IN_RTH_OPEN_HHMM].Name = "RTH Open (HHMM, chart timezone)";
		sc.Input[IN_RTH_OPEN_HHMM].SetInt(830);

		sc.Input[IN_DURATION_MINUTES].Name = "Measurement Window (minutes)";
		sc.Input[IN_DURATION_MINUTES].SetInt(60);
		sc.Input[IN_DURATION_MINUTES].SetIntLimits(1, 720);

		sc.Input[IN_MIN_TICKS].Name = "Minimum Ticks To Call The Day";
		sc.Input[IN_MIN_TICKS].SetInt(0);
		sc.Input[IN_MIN_TICKS].SetIntLimits(0, 10000);

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

		sc.Input[IN_GREEN_COLOR].Name = "Green Day Color";
		sc.Input[IN_GREEN_COLOR].SetColor(0, 220, 0);

		sc.Input[IN_RED_COLOR].Name = "Red Day Color";
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
	// Walk forward across the measurement window
	//------------------------------------------------------------------------
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

	int NetTicks = 0;
	if (sc.TickSize > 0.0)
		NetTicks = (int)(AbsValue(Net) / sc.TickSize + 0.5);

	const int MinTicks = sc.Input[IN_MIN_TICKS].GetInt();

	//------------------------------------------------------------------------
	// Verdict
	//------------------------------------------------------------------------
	COLORREF LineColor = FlatColor;
	SCString Verdict;

	if (Net > 0.0 && NetTicks >= MinTicks)
	{
		LineColor = sc.Input[IN_GREEN_COLOR].GetColor();
		Verdict = IsComplete ? "GREEN DAY" : "LEANING GREEN";
	}
	else if (Net < 0.0 && NetTicks >= MinTicks)
	{
		LineColor = sc.Input[IN_RED_COLOR].GetColor();
		Verdict = IsComplete ? "RED DAY" : "LEANING RED";
	}
	else
	{
		Verdict = IsComplete ? "NEUTRAL DAY" : "NEUTRAL SO FAR";
	}

	if (!IsComplete)
		Verdict += "  (first hour still running)";

	//------------------------------------------------------------------------
	// Display
	//------------------------------------------------------------------------
	const int ValueFormat = sc.GetValueFormat();
	const SCDateTime OpenDT = sc.BaseDateTimeIn[OpenIndex];

	SCString Text;

	Text.Format("NY FIRST HOUR  %02d/%02d  %02d:%02d - %02d:%02d",
		OpenDT.GetMonth(), OpenDT.GetDay(),
		OpenMinutes / 60, OpenMinutes % 60,
		EndMinutes / 60, EndMinutes % 60);
	SetTextLine(sc, 0, Text, LineColor);

	Text.Format("OPEN: %s   CLOSE: %s",
		sc.FormatGraphValue(WindowOpen, ValueFormat).GetChars(),
		sc.FormatGraphValue(WindowClose, ValueFormat).GetChars());
	SetTextLine(sc, 1, Text, LineColor);

	Text.Format("NET: %s%s (%d ticks)   RANGE: %s - %s",
		(Net >= 0.0) ? "+" : "-",
		sc.FormatGraphValue(AbsValue(Net), ValueFormat).GetChars(),
		NetTicks,
		sc.FormatGraphValue(WindowLow, ValueFormat).GetChars(),
		sc.FormatGraphValue(WindowHigh, ValueFormat).GetChars());
	SetTextLine(sc, 2, Text, LineColor);

	SetTextLine(sc, 3, Verdict, LineColor);
}
