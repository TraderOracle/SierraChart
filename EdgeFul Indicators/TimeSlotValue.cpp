#include "sierrachart.h"

#include <fstream>
#include <string>
#include <map>

SCDLLName("Time Slot Value")

//============================================================================
// TIME SLOT VALUE DISPLAY
//
// Reads a table of per-slot numbers from a CSV file. Instead of printing the
// number, the second line is a solid rectangle whose color encodes the value:
//
//     value >= Red Threshold    (default 20)  -> red
//     value >= Grey Threshold   (default 15)  -> grey
//     below that                              -> green
//
// FILE FORMAT - one line per slot, times exactly as they appear in the source
// chart, blank lines and lines starting with # ignored:
//
//     09:30,22
//     09:35,12
//
// The "Subtract From File Times" input shifts those labels to your chart's
// clock. It defaults to 60, so a 09:30 row is matched and displayed as 08:30.
//============================================================================

namespace {

	const int NUM_LINES     = 4;
	const int LINE_NUM_BASE = 96800;
	const int RECT_LINE_NUM = LINE_NUM_BASE + 50;   // rectangle owns its own ID
	const int NUM_LINE_NUM  = LINE_NUM_BASE + 51;   // number drawn over it
	const int RECT_CACHE_IX = NUM_LINES;            // persistent string slot 4

	enum InputIndex
	{
		IN_FILE_PATH = 0,
		IN_SUBTRACT_MINUTES,
		IN_SLOT_MINUTES,
		IN_USE_SYSTEM_CLOCK,
		IN_SHOW_NEXT_SLOT,
		IN_RELOAD,
		IN_RED_THRESHOLD,
		IN_GREY_THRESHOLD,
		IN_RECT_WIDTH,
		IN_RECT_HEIGHT,
		IN_CHAR_WIDTH,
		IN_TRANSPARENCY,
		IN_HORIZ_POS,
		IN_VERT_POS,
		IN_LINE_SPACING,
		IN_FONT_SIZE,
		IN_BOLD,
		IN_TRANSPARENT_BG,
		IN_TEXT_COLOR,
		IN_MISSING_COLOR,
		IN_RED_COLOR,
		IN_GREY_COLOR,
		IN_GREEN_COLOR,
		IN_NUMBER_COLOR
	};

	std::map<int, int> g_SlotValues;
	std::string        g_LoadedPath;
	int                g_LoadedReloadFlag = -1;
	int                g_LoadFailed       = 0;

	int MinutesOfDay(const SCDateTime& DT)
	{
		return DT.GetHour() * 60 + DT.GetMinute();
	}

	int ParseLine(const std::string& Line, int& r_Minutes, int& r_Value)
	{
		if (Line.empty() || Line[0] == '#')
			return 0;

		const size_t CommaPos = Line.find(',');
		if (CommaPos == std::string::npos)
			return 0;

		std::string TimePart  = Line.substr(0, CommaPos);
		std::string ValuePart = Line.substr(CommaPos + 1);

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
			if (ParseLine(Line, Minutes, Value))
				g_SlotValues[Minutes] = Value;
		}

		File.close();
	}

	// Vertical position of the top of a given text line, in relative units.
	double LineTopValue(SCStudyInterfaceRef sc, int LineIndex)
	{
		return sc.Input[IN_VERT_POS].GetFloat()
		       - LineIndex * sc.Input[IN_LINE_SPACING].GetFloat();
	}

	//------------------------------------------------------------------------
	// Text line drawing. Fixed LineNumber + UTAM_ADD_OR_ADJUST replaces the
	// line in place. Color is part of the cache key.
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
	// Filled rectangle occupying the same slot a text line would, with the
	// value centered on it.
	//
	// Both drawings are rebuilt together whenever anything changes, and the
	// number is issued after the rectangle, so it always ends up on top.
	//------------------------------------------------------------------------
	void SetValueBlock(SCStudyInterfaceRef sc, int LineIndex, double Width, double Height,
		COLORREF FillColor, int Value)
	{
		const COLORREF NumberColor = sc.Input[IN_NUMBER_COLOR].GetColor();

		SCString CacheKey;
		CacheKey.Format("R|%u|%u|%.3f|%.3f|%d|%d",
			(unsigned int)FillColor, (unsigned int)NumberColor, Width, Height, LineIndex, Value);

		SCString& LastKey = sc.GetPersistentSCString(RECT_CACHE_IX);
		if (strcmp(LastKey.GetChars(), CacheKey.GetChars()) == 0)
			return;

		LastKey = CacheKey;

		const double Left = sc.Input[IN_HORIZ_POS].GetInt();
		const double Top  = LineTopValue(sc, LineIndex);

		s_UseTool Rect;
		Rect.Clear();
		Rect.ChartNumber  = sc.ChartNumber;
		Rect.Region       = sc.GraphRegion;
		Rect.DrawingType  = DRAWING_RECTANGLEHIGHLIGHT;
		Rect.LineNumber   = RECT_LINE_NUM;
		Rect.AddMethod    = UTAM_ADD_OR_ADJUST;
		Rect.AddAsUserDrawnDrawing = 0;

		Rect.UseRelativeVerticalValues = 1;
		Rect.BeginDateTime = Left;
		Rect.EndDateTime   = Left + Width;
		Rect.BeginValue    = Top;
		Rect.EndValue      = Top - Height;

		Rect.Color            = FillColor;   // border
		Rect.SecondaryColor   = FillColor;   // fill
		Rect.TransparencyLevel = sc.Input[IN_TRANSPARENCY].GetInt();
		Rect.LineWidth        = 1;

		sc.UseTool(Rect);

		SCString ValueText;
		ValueText.Format("%d", Value);

		s_UseTool Number;
		Number.Clear();
		Number.ChartNumber  = sc.ChartNumber;
		Number.Region       = sc.GraphRegion;
		Number.DrawingType  = DRAWING_TEXT;
		Number.LineNumber   = NUM_LINE_NUM;
		Number.AddMethod    = UTAM_ADD_OR_ADJUST;
		Number.AddAsUserDrawnDrawing = 0;

		Number.UseRelativeVerticalValues = 1;
		Number.BeginDateTime = Left + Width * 0.5;
		Number.BeginValue    = Top - Height * 0.5;

		Number.Text          = ValueText;
		Number.Color         = NumberColor;
		Number.FontSize      = sc.Input[IN_FONT_SIZE].GetInt();
		Number.FontBold      = sc.Input[IN_BOLD].GetYesNo();
		Number.TransparentLabelBackground = 1;   // let the fill show through
		Number.TextAlignment = DT_CENTER | DT_VCENTER;

		sc.UseTool(Number);
	}

	void ClearRectangle(SCStudyInterfaceRef sc)
	{
		SCString& LastKey = sc.GetPersistentSCString(RECT_CACHE_IX);
		if (LastKey.GetLength() == 0)
			return;

		LastKey = "";
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, RECT_LINE_NUM);
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, NUM_LINE_NUM);
	}

	void ClearAll(SCStudyInterfaceRef sc)
	{
		for (int i = 0; i < NUM_LINES; ++i)
		{
			sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LINE_NUM_BASE + i);
			sc.GetPersistentSCString(i) = "";
		}

		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, RECT_LINE_NUM);
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, NUM_LINE_NUM);
		sc.GetPersistentSCString(RECT_CACHE_IX) = "";
	}
}

//============================================================================

SCSFExport scsf_TimeSlotValue(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "Time Slot Value";
		sc.StudyDescription
			= "Shows the current time slot's value from a CSV table as a color coded bar.";
		sc.GraphRegion  = 0;
		sc.AutoLoop     = 0;
		sc.UpdateAlways = 1;
		sc.FreeDLL      = 0;
		sc.DrawZeros    = 0;

		sc.Input[IN_FILE_PATH].Name = "Data File Path";
		sc.Input[IN_FILE_PATH].SetString("C:\\SierraChart\\Data\\FiveMinStats.csv");

		sc.Input[IN_SUBTRACT_MINUTES].Name = "Subtract From File Times (minutes)";
		sc.Input[IN_SUBTRACT_MINUTES].SetInt(60);
		sc.Input[IN_SUBTRACT_MINUTES].SetIntLimits(-720, 720);

		sc.Input[IN_SLOT_MINUTES].Name = "Slot Length (minutes)";
		sc.Input[IN_SLOT_MINUTES].SetInt(5);
		sc.Input[IN_SLOT_MINUTES].SetIntLimits(1, 240);

		sc.Input[IN_USE_SYSTEM_CLOCK].Name = "Use System Clock (No = last bar time)";
		sc.Input[IN_USE_SYSTEM_CLOCK].SetYesNo(1);

		sc.Input[IN_SHOW_NEXT_SLOT].Name = "Also Show Next Slot";
		sc.Input[IN_SHOW_NEXT_SLOT].SetYesNo(1);

		sc.Input[IN_RELOAD].Name = "Reload Data File (toggle to refresh)";
		sc.Input[IN_RELOAD].SetYesNo(0);

		sc.Input[IN_RED_THRESHOLD].Name = "Red At Or Above";
		sc.Input[IN_RED_THRESHOLD].SetInt(20);

		sc.Input[IN_GREY_THRESHOLD].Name = "Grey At Or Above";
		sc.Input[IN_GREY_THRESHOLD].SetInt(15);

		sc.Input[IN_RECT_WIDTH].Name = "Rectangle Width (0 = auto from text)";
		sc.Input[IN_RECT_WIDTH].SetFloat(0.0f);

		sc.Input[IN_RECT_HEIGHT].Name = "Rectangle Height (0 = auto from line spacing)";
		sc.Input[IN_RECT_HEIGHT].SetFloat(0.0f);

		sc.Input[IN_CHAR_WIDTH].Name = "Auto Width Per Character";
		sc.Input[IN_CHAR_WIDTH].SetFloat(0.62f);

		sc.Input[IN_TRANSPARENCY].Name = "Rectangle Transparency (0 = solid)";
		sc.Input[IN_TRANSPARENCY].SetInt(0);
		sc.Input[IN_TRANSPARENCY].SetIntLimits(0, 100);

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

		sc.Input[IN_TEXT_COLOR].Name = "Text Color";
		sc.Input[IN_TEXT_COLOR].SetColor(0, 170, 255);

		sc.Input[IN_MISSING_COLOR].Name = "No Data Color";
		sc.Input[IN_MISSING_COLOR].SetColor(190, 190, 190);

		sc.Input[IN_RED_COLOR].Name = "Red Color";
		sc.Input[IN_RED_COLOR].SetColor(255, 60, 60);

		sc.Input[IN_GREY_COLOR].Name = "Grey Color";
		sc.Input[IN_GREY_COLOR].SetColor(140, 140, 140);

		sc.Input[IN_GREEN_COLOR].Name = "Green Color";
		sc.Input[IN_GREEN_COLOR].SetColor(0, 220, 0);

		sc.Input[IN_NUMBER_COLOR].Name = "Number Color";
		sc.Input[IN_NUMBER_COLOR].SetColor(235, 235, 235);

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
		sc.GetPersistentSCString(RECT_CACHE_IX) = "";
	}

	const COLORREF TextColor    = sc.Input[IN_TEXT_COLOR].GetColor();
	const COLORREF MissingColor = sc.Input[IN_MISSING_COLOR].GetColor();

	//------------------------------------------------------------------------
	// Load the table when the path changes, reload is toggled, or on recalc.
	//------------------------------------------------------------------------
	const std::string FilePath = sc.Input[IN_FILE_PATH].GetString();
	const int ReloadFlag = sc.Input[IN_RELOAD].GetYesNo();

	if (FilePath != g_LoadedPath
		|| ReloadFlag != g_LoadedReloadFlag
		|| sc.IsFullRecalculation
		|| (g_SlotValues.empty() && !g_LoadFailed))
	{
		LoadSlotFile(FilePath);
		g_LoadedPath = FilePath;
		g_LoadedReloadFlag = ReloadFlag;
	}

	if (g_LoadFailed)
	{
		ClearRectangle(sc);
		SCString Message;
		Message.Format("Cannot open %s", FilePath.c_str());
		SetTextLine(sc, 0, Message, MissingColor);
		for (int i = 1; i < NUM_LINES; ++i)
			SetTextLine(sc, i, "", MissingColor);
		return;
	}

	//------------------------------------------------------------------------
	// Which slot are we in
	//------------------------------------------------------------------------
	SCDateTime ReferenceDT;
	if (sc.Input[IN_USE_SYSTEM_CLOCK].GetYesNo() || sc.ArraySize == 0)
		ReferenceDT = sc.CurrentSystemDateTime;
	else
		ReferenceDT = sc.BaseDateTimeIn[sc.ArraySize - 1];

	int SlotMinutes = sc.Input[IN_SLOT_MINUTES].GetInt();
	if (SlotMinutes < 1)
		SlotMinutes = 1;

	const int NowMinutes   = MinutesOfDay(ReferenceDT);
	const int LocalSlot    = (NowMinutes / SlotMinutes) * SlotMinutes;
	const int NextLocal    = LocalSlot + SlotMinutes;
	const int ShiftMinutes = sc.Input[IN_SUBTRACT_MINUTES].GetInt();

	const int FileKey     = LocalSlot + ShiftMinutes;
	const int NextFileKey = FileKey + SlotMinutes;

	//------------------------------------------------------------------------
	// Build the text lines first, so the rectangle can be sized from them.
	//------------------------------------------------------------------------
	SCString SlotText;
	SlotText.Format("SLOT %02d:%02d - %02d:%02d",
		LocalSlot / 60, LocalSlot % 60,
		(NextLocal / 60) % 24, NextLocal % 60);

	SCString NextText;
	if (sc.Input[IN_SHOW_NEXT_SLOT].GetYesNo())
	{
		std::map<int, int>::const_iterator NextFound = g_SlotValues.find(NextFileKey);
		if (NextFound != g_SlotValues.end())
		{
			NextText.Format("next %02d:%02d -> %d",
				(NextLocal / 60) % 24, NextLocal % 60, NextFound->second);
		}
		else
		{
			NextText.Format("next %02d:%02d -> --", (NextLocal / 60) % 24, NextLocal % 60);
		}
	}

	SCString CountText;
	CountText.Format("%d slots loaded", (int)g_SlotValues.size());

	SetTextLine(sc, 0, SlotText, TextColor);
	SetTextLine(sc, 2, NextText, TextColor);
	SetTextLine(sc, 3, CountText, MissingColor);

	//------------------------------------------------------------------------
	// Rectangle in place of line 1
	//------------------------------------------------------------------------
	std::map<int, int>::const_iterator Found = g_SlotValues.find(FileKey);

	if (Found == g_SlotValues.end())
	{
		ClearRectangle(sc);
		SetTextLine(sc, 1, "--", MissingColor);
		return;
	}

	SetTextLine(sc, 1, "", MissingColor);   // no number, the rectangle replaces it

	const int Value = Found->second;

	COLORREF FillColor;
	if (Value >= sc.Input[IN_RED_THRESHOLD].GetInt())
		FillColor = sc.Input[IN_RED_COLOR].GetColor();
	else if (Value >= sc.Input[IN_GREY_THRESHOLD].GetInt())
		FillColor = sc.Input[IN_GREY_COLOR].GetColor();
	else
		FillColor = sc.Input[IN_GREEN_COLOR].GetColor();

	// Auto width tracks the longest visible text line so the block lines up
	// with the rest of the readout.
	double Width = sc.Input[IN_RECT_WIDTH].GetFloat();
	if (Width <= 0.0)
	{
		int MaxChars = SlotText.GetLength();
		if (NextText.GetLength() > MaxChars)
			MaxChars = NextText.GetLength();
		if (CountText.GetLength() > MaxChars)
			MaxChars = CountText.GetLength();

		Width = MaxChars * sc.Input[IN_CHAR_WIDTH].GetFloat();
	}

	double Height = sc.Input[IN_RECT_HEIGHT].GetFloat();
	if (Height <= 0.0)
		Height = sc.Input[IN_LINE_SPACING].GetFloat() * 0.85;

	SetValueBlock(sc, 1, Width, Height, FillColor, Value);
}
