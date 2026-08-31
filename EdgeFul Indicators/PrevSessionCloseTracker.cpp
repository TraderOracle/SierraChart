#include "sierrachart.h"

SCDLLName("Prev Session Close Tracker")

//============================================================================
// PREVIOUS 5PM CLOSE TRACKER
//
// Apply to an INTRADAY chart whose Session Times include the evening session
// (a 23-hour futures session), and whose timezone is set to Central so that
// 17:00 on the chart really is 5pm CST.
//
// Reference price: the close of the last bar BEFORE the most recent 17:00
// boundary. Finding it by boundary crossing rather than by calendar date is
// what makes Monday work correctly: walking back from Monday morning, the
// bar before the Sunday 17:00 reopen is Friday's final bar, which is the
// close a trader actually means. On any other morning it lands on yesterday
// 16:59, i.e. yesterday's 5pm close.
//
// Display turns on at the RTH open (08:30 CST by default) and shows the
// signed distance from the current bar's close, whether price is closing on
// that level or leaving it, and a trend-colored readout.
//============================================================================

namespace {

	const int NUM_LINES     = 4;
	const int LINE_NUM_BASE = 92400;

	enum InputIndex
	{
		IN_REF_CLOSE_HHMM = 0,
		IN_RTH_START_HHMM,
		IN_RTH_END_HHMM,
		IN_ONLY_DURING_RTH,
		IN_TREND_LOOKBACK,
		IN_HORIZ_POS,
		IN_VERT_POS,
		IN_LINE_SPACING,
		IN_FONT_SIZE,
		IN_BOLD,
		IN_TRANSPARENT_BG,
		IN_UP_COLOR,
		IN_DOWN_COLOR,
		IN_FLAT_COLOR
	};

	// HHMM integer (830 = 08:30) -> minutes since midnight
	int HHMMToMinutes(int HHMM)
	{
		const int Hours   = HHMM / 100;
		const int Minutes = HHMM % 100;
		return Hours * 60 + Minutes;
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
	// line in place, so nothing overlaps. Empty string erases the line.
	//------------------------------------------------------------------------
	void SetTextLine(SCStudyInterfaceRef sc, int LineIndex, const SCString& Text, COLORREF TextColor)
	{
		if (LineIndex < 0 || LineIndex >= NUM_LINES)
			return;

		// The color is part of the cache key, otherwise a red-to-green flip
		// with identical text would never be redrawn.
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

	void ClearAllLines(SCStudyInterfaceRef sc)
	{
		for (int i = 0; i < NUM_LINES; ++i)
		{
			sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LINE_NUM_BASE + i);
			sc.GetPersistentSCString(i) = "";
		}
	}

	//------------------------------------------------------------------------
	// Index of the bar whose close is the previous session close.
	// Returns -1 if it cannot be located in the loaded data.
	//------------------------------------------------------------------------
	int FindPrevSessionCloseIndex(SCStudyInterfaceRef sc, int RefMinutes)
	{
		const int LastIndex = sc.ArraySize - 1;
		if (LastIndex < 1)
			return -1;

		// Most recent crossing of the reference time going backwards.
		for (int i = LastIndex; i >= 1; --i)
		{
			const int CurrentMinutes  = MinutesOfDay(sc.BaseDateTimeIn[i]);
			const int PreviousMinutes = MinutesOfDay(sc.BaseDateTimeIn[i - 1]);

			if (CurrentMinutes >= RefMinutes && PreviousMinutes < RefMinutes)
				return i - 1;
		}

		// Fallback for charts that carry no evening session: last bar of the
		// previous calendar day.
		const int TodayDate = sc.BaseDateTimeIn[LastIndex].GetDate();
		for (int i = LastIndex; i >= 0; --i)
		{
			if (sc.BaseDateTimeIn[i].GetDate() < TodayDate)
				return i;
		}

		return -1;
	}
}

//============================================================================

SCSFExport scsf_PrevSessionCloseTracker(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "Prev Session Close Tracker";
		sc.StudyDescription
			= "Shows distance from the current close to the previous 5pm session close, "
			  "starting at the RTH open. Green when price is trending up, red when down.";
		sc.GraphRegion  = 0;
		sc.AutoLoop     = 0;
		sc.UpdateAlways = 1;
		sc.FreeDLL      = 0;
		sc.DrawZeros    = 0;

		sc.Input[IN_REF_CLOSE_HHMM].Name = "Session Close Time (HHMM, chart timezone)";
		sc.Input[IN_REF_CLOSE_HHMM].SetInt(1700);

		sc.Input[IN_RTH_START_HHMM].Name = "RTH Open (HHMM)";
		sc.Input[IN_RTH_START_HHMM].SetInt(830);

		sc.Input[IN_RTH_END_HHMM].Name = "RTH Close (HHMM)";
		sc.Input[IN_RTH_END_HHMM].SetInt(1500);

		sc.Input[IN_ONLY_DURING_RTH].Name = "Only Display During RTH Window";
		sc.Input[IN_ONLY_DURING_RTH].SetYesNo(1);

		sc.Input[IN_TREND_LOOKBACK].Name = "Trend Lookback (bars)";
		sc.Input[IN_TREND_LOOKBACK].SetInt(5);
		sc.Input[IN_TREND_LOOKBACK].SetIntLimits(1, 500);

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

		sc.Input[IN_UP_COLOR].Name = "Trending Up Color";
		sc.Input[IN_UP_COLOR].SetColor(0, 220, 0);

		sc.Input[IN_DOWN_COLOR].Name = "Trending Down Color";
		sc.Input[IN_DOWN_COLOR].SetColor(255, 60, 60);

		sc.Input[IN_FLAT_COLOR].Name = "Flat Color";
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

	//------------------------------------------------------------------------
	// Are we inside the display window?
	//------------------------------------------------------------------------
	const int NowMinutes   = MinutesOfDay(sc.BaseDateTimeIn[LastIndex]);
	const int StartMinutes = HHMMToMinutes(sc.Input[IN_RTH_START_HHMM].GetInt());
	const int EndMinutes   = HHMMToMinutes(sc.Input[IN_RTH_END_HHMM].GetInt());

	int InWindow = 1;
	if (sc.Input[IN_ONLY_DURING_RTH].GetYesNo())
	{
		if (StartMinutes <= EndMinutes)
			InWindow = (NowMinutes >= StartMinutes && NowMinutes <= EndMinutes) ? 1 : 0;
		else   // window wraps past midnight
			InWindow = (NowMinutes >= StartMinutes || NowMinutes <= EndMinutes) ? 1 : 0;
	}

	if (!InWindow)
	{
		for (int i = 0; i < NUM_LINES; ++i)
			SetTextLine(sc, i, "", FlatColor);
		return;
	}

	//------------------------------------------------------------------------
	// Reference close
	//------------------------------------------------------------------------
	const int RefMinutes = HHMMToMinutes(sc.Input[IN_REF_CLOSE_HHMM].GetInt());
	const int RefIndex   = FindPrevSessionCloseIndex(sc, RefMinutes);

	if (RefIndex < 0)
	{
		SetTextLine(sc, 0, "Previous session close not in loaded data", FlatColor);
		for (int i = 1; i < NUM_LINES; ++i)
			SetTextLine(sc, i, "", FlatColor);
		return;
	}

	const double RefClose     = sc.Close[RefIndex];
	const double CurrentClose = sc.Close[LastIndex];
	const double Distance     = CurrentClose - RefClose;

	//------------------------------------------------------------------------
	// Trend and approach
	//------------------------------------------------------------------------
	int Lookback = sc.Input[IN_TREND_LOOKBACK].GetInt();
	if (Lookback > LastIndex)
		Lookback = LastIndex;

	const double PastClose    = sc.Close[LastIndex - Lookback];
	const double TrendChange  = CurrentClose - PastClose;
	const double PastDistance = AbsValue(PastClose - RefClose);
	const double NowDistance  = AbsValue(Distance);

	COLORREF LineColor = FlatColor;
	SCString TrendText = "TREND: FLAT";

	if (TrendChange > 0.0)
	{
		LineColor = sc.Input[IN_UP_COLOR].GetColor();
		TrendText = "TREND: UP";
	}
	else if (TrendChange < 0.0)
	{
		LineColor = sc.Input[IN_DOWN_COLOR].GetColor();
		TrendText = "TREND: DOWN";
	}

	const char* ApproachText = "HOLDING";
	if (NowDistance < PastDistance)
		ApproachText = "MOVING TOWARD PREV CLOSE";
	else if (NowDistance > PastDistance)
		ApproachText = "MOVING AWAY FROM PREV CLOSE";

	//------------------------------------------------------------------------
	// Display
	//------------------------------------------------------------------------
	const int ValueFormat = sc.GetValueFormat();
	const SCDateTime RefDT = sc.BaseDateTimeIn[RefIndex];

	int Ticks = 0;
	if (sc.TickSize > 0.0)
		Ticks = (int)(NowDistance / sc.TickSize + 0.5);

	SCString Text;

	Text.Format("PREV CLOSE %02d:%02d (%02d/%02d): %s",
		RefDT.GetHour(), RefDT.GetMinute(), RefDT.GetMonth(), RefDT.GetDay(),
		sc.FormatGraphValue(RefClose, ValueFormat).GetChars());
	SetTextLine(sc, 0, Text, LineColor);

	Text.Format("LAST: %s", sc.FormatGraphValue(CurrentClose, ValueFormat).GetChars());
	SetTextLine(sc, 1, Text, LineColor);

	Text.Format("DIST: %s%s  (%d ticks)",
		(Distance >= 0.0) ? "+" : "-",
		sc.FormatGraphValue(NowDistance, ValueFormat).GetChars(),
		Ticks);
	SetTextLine(sc, 2, Text, LineColor);

	Text.Format("%s   |   %s", ApproachText, TrendText.GetChars());
	SetTextLine(sc, 3, Text, LineColor);
}
