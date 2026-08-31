#include "sierrachart.h"

SCDLLName("Inside Bar Scanner")

//============================================================================
// INSIDE BAR SCANNER
//
// Scans up to 20 DAILY charts in the same Chartbook and reports which symbols
// printed an inside bar on the last COMPLETED daily bar.
//
// Why this handles "yesterday, or Friday if today is Monday" with no weekday
// math: a Daily chart only contains trading days. The bar before the current
// one IS Friday when today is Monday, and it is the day before a holiday when
// yesterday was a holiday. So all we do is drop today's in-progress bar (if
// present) and test the newest completed bar against the one before it.
//
// SETUP
//   1. Open one Daily chart per symbol you want to scan, in this Chartbook.
//      They can be hidden (Window >> Hide Window) to save resources.
//   2. Apply this study to any chart.
//   3. In Study Settings, set "Chart 1".."Chart 20" to those chart numbers.
//      Leave unused ones at 0.
//============================================================================

// If sc.GetChartName() does not exist in your Sierra Chart version, change
// this to 0 and the display will fall back to "Chart #N" labels.
#define HAVE_GET_CHART_NAME 1

namespace {

	const int MAX_SYMBOLS   = 20;
	const int MAX_LINES     = MAX_SYMBOLS + 2;   // header + one line per hit
	const int LINE_NUM_BASE = 91300;             // unique drawing IDs

	enum InputIndex
	{
		IN_CHART_FIRST = 0,                      // IN_CHART_FIRST .. +19
		IN_STRICT = MAX_SYMBOLS,                 // 20
		IN_SKIP_TODAY,
		IN_SHOW_NON_HITS,
		IN_HORIZ_POS,
		IN_VERT_POS,
		IN_LINE_SPACING,
		IN_FONT_SIZE,
		IN_BOLD,
		IN_TRANSPARENT_BG,
		IN_HEADER_COLOR,
		IN_HIT_COLOR,
		IN_MISS_COLOR
	};

	//------------------------------------------------------------------------
	// Text line drawing. Fixed LineNumber + UTAM_ADD_OR_ADJUST means each line
	// is REPLACED in place, so text can never overlap or be left behind.
	// Passing an empty string erases the line.
	//------------------------------------------------------------------------
	void SetTextLine(SCStudyInterfaceRef sc, int LineIndex, const SCString& Text, COLORREF TextColor)
	{
		if (LineIndex < 0 || LineIndex >= MAX_LINES)
			return;

		// Cache: only touch the drawing when the text actually changed.
		SCString& LastText = sc.GetPersistentSCString(LineIndex);
		if (strcmp(LastText.GetChars(), Text.GetChars()) == 0)
			return;

		LastText = Text;

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
		for (int i = 0; i < MAX_LINES; ++i)
		{
			sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LINE_NUM_BASE + i);
			sc.GetPersistentSCString(i) = "";
		}
	}

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

	//------------------------------------------------------------------------
	// Result of scanning one chart.
	//------------------------------------------------------------------------
	struct s_ScanResult
	{
		int   Valid;         // 0 = chart empty / not enough bars
		int   IsInsideBar;
		int   Month;
		int   Day;
		float Range;         // target bar range, used for the display only
	};

	s_ScanResult ScanChart(SCStudyInterfaceRef sc, int ChartNumber, int Strict, int SkipTodaysBar)
	{
		s_ScanResult Result;
		Result.Valid = 0;
		Result.IsInsideBar = 0;
		Result.Month = 0;
		Result.Day = 0;
		Result.Range = 0.0f;

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

		// Drop the bar that is still forming today, so we always evaluate a
		// bar that has closed.
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

		Result.Range = TargetHigh - TargetLow;
		Result.Valid = 1;
		return Result;
	}
}

//============================================================================

SCSFExport scsf_InsideBarScanner(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName    = "Inside Bar Scanner (20 Symbols)";
		sc.StudyDescription
			= "Scans up to 20 Daily charts in this Chartbook and lists the symbols "
			  "whose last completed daily bar was an inside bar.";
		sc.GraphRegion  = 0;
		sc.AutoLoop     = 0;
		sc.UpdateAlways = 0;   // set to 1 if you want it refreshed on every chart update
		sc.FreeDLL      = 0;
		sc.DrawZeros    = 0;

		// Low precedence so the referenced charts are calculated first.
		sc.CalculationPrecedence = LOW_PREC_LEVEL;

		SCString InputName;
		for (int i = 0; i < MAX_SYMBOLS; ++i)
		{
			InputName.Format("Chart %d", i + 1);
			sc.Input[IN_CHART_FIRST + i].Name = InputName;
			sc.Input[IN_CHART_FIRST + i].SetChartNumber(0);
		}

		sc.Input[IN_STRICT].Name = "Strict Inside Bar (No = allow equal High/Low)";
		sc.Input[IN_STRICT].SetYesNo(1);

		sc.Input[IN_SKIP_TODAY].Name = "Ignore Today's Unfinished Bar";
		sc.Input[IN_SKIP_TODAY].SetYesNo(1);

		sc.Input[IN_SHOW_NON_HITS].Name = "List Symbols That Did NOT Qualify";
		sc.Input[IN_SHOW_NON_HITS].SetYesNo(0);

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

		sc.Input[IN_HEADER_COLOR].Name = "Header Color";
		sc.Input[IN_HEADER_COLOR].SetColor(255, 255, 255);

		sc.Input[IN_HIT_COLOR].Name = "Inside Bar Color";
		sc.Input[IN_HIT_COLOR].SetColor(0, 255, 0);

		sc.Input[IN_MISS_COLOR].Name = "Non-Qualifying Color";
		sc.Input[IN_MISS_COLOR].SetColor(140, 140, 140);

		return;
	}

	if (sc.LastCallToFunction)
	{
		ClearAllLines(sc);
		return;
	}

	if (sc.IsFullRecalculation)
	{
		for (int i = 0; i < MAX_LINES; ++i)
			sc.GetPersistentSCString(i) = "";
	}

	const int Strict        = sc.Input[IN_STRICT].GetYesNo();
	const int SkipToday     = sc.Input[IN_SKIP_TODAY].GetYesNo();
	const int ShowNonHits   = sc.Input[IN_SHOW_NON_HITS].GetYesNo();
	const COLORREF HeaderColor = sc.Input[IN_HEADER_COLOR].GetColor();
	const COLORREF HitColor    = sc.Input[IN_HIT_COLOR].GetColor();
	const COLORREF MissColor   = sc.Input[IN_MISS_COLOR].GetColor();

	int NextLine   = 1;   // line 0 is the header
	int Scanned    = 0;
	int HitCount   = 0;
	SCString Text;

	for (int i = 0; i < MAX_SYMBOLS; ++i)
	{
		const int ChartNumber = sc.Input[IN_CHART_FIRST + i].GetChartNumber();
		if (ChartNumber <= 0)
			continue;

		const s_ScanResult Result = ScanChart(sc, ChartNumber, Strict, SkipToday);
		if (!Result.Valid)
			continue;

		++Scanned;

		if (Result.IsInsideBar)
		{
			++HitCount;
			Text.Format("%s   IB %02d/%02d",
				GetChartLabel(sc, ChartNumber).GetChars(), Result.Month, Result.Day);
			SetTextLine(sc, NextLine++, Text, HitColor);
		}
		else if (ShowNonHits)
		{
			Text.Format("%s   -",
				GetChartLabel(sc, ChartNumber).GetChars());
			SetTextLine(sc, NextLine++, Text, MissColor);
		}
	}

	Text.Format("INSIDE BAR SCAN - prev completed daily - %d of %d", HitCount, Scanned);
	SetTextLine(sc, 0, Text, HeaderColor);

	if (Scanned > 0 && HitCount == 0 && !ShowNonHits)
	{
		SetTextLine(sc, NextLine++, "(none)", MissColor);
	}
	else if (Scanned == 0)
	{
		SetTextLine(sc, NextLine++, "Set the Chart 1-20 inputs to your Daily charts", MissColor);
	}

	// Erase any lines left over from a previous, longer result set.
	for (int i = NextLine; i < MAX_LINES; ++i)
		SetTextLine(sc, i, "", MissColor);
}
