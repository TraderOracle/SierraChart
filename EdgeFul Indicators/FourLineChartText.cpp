#include "sierrachart.h"

SCDLLName("Four Line Chart Text")

//============================================================================
// 4 lines of text pinned to the upper-left of the chart.
//
// Key ideas:
//  * Each line owns a FIXED LineNumber. Re-drawing with the same LineNumber
//    and UTAM_ADD_OR_ADJUST *replaces* the drawing, so old text can never be
//    left behind or overlap. No manual erase needed.
//  * Position uses relative (percentage) coordinates, so the text stays glued
//    to the corner regardless of scrolling or zooming.
//  * The last string drawn per line is cached in a persistent SCString, so
//    sc.UseTool() is only called when the text actually changes.
//============================================================================

namespace {

	const int NUM_LINES     = 4;
	const int LINE_NUM_BASE = 90210;   // unique base ID for this study's drawings

	// Input indexes
	enum InputIndex
	{
		IN_HORIZ_POS = 0,
		IN_VERT_POS,
		IN_LINE_SPACING,
		IN_FONT_SIZE,
		IN_COLOR_1,        // IN_COLOR_1 + n  ->  color for line n
		IN_COLOR_2,
		IN_COLOR_3,
		IN_COLOR_4,
		IN_BOLD,
		IN_TRANSPARENT_BG
	};

	// Draw / update one line. Pass an empty string to erase that line.
	void SetTextLine(SCStudyInterfaceRef sc, int LineIndex, const SCString& Text)
	{
		if (LineIndex < 0 || LineIndex >= NUM_LINES)
			return;

		// Nothing changed -> do no work at all (keeps this cheap on every tick)
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
		Tool.LineNumber   = LineNumber;               // same number => replace, never duplicate
		Tool.AddMethod    = UTAM_ADD_OR_ADJUST;
		Tool.AddAsUserDrawnDrawing = 0;

		// Relative coordinates: horizontal 0-150, vertical 0-100 (100 = top)
		Tool.UseRelativeVerticalValues = 1;
		Tool.BeginDateTime = sc.Input[IN_HORIZ_POS].GetInt();
		Tool.BeginValue    = sc.Input[IN_VERT_POS].GetFloat()
		                     - LineIndex * sc.Input[IN_LINE_SPACING].GetFloat();

		Tool.Text          = Text;
		Tool.Color         = sc.Input[IN_COLOR_1 + LineIndex].GetColor();
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

SCSFExport scsf_FourLineChartText(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName          = "Four Line Chart Text";
		sc.GraphRegion        = 0;
		sc.AutoLoop           = 0;    // we don't need a per-bar loop
		sc.UpdateAlways       = 1;    // set to 0 if you only need updates on new data
		sc.FreeDLL            = 0;
		sc.DrawZeros          = 0;

		sc.Input[IN_HORIZ_POS].Name = "Horizontal Position (0-150)";
		sc.Input[IN_HORIZ_POS].SetInt(2);

		sc.Input[IN_VERT_POS].Name = "Vertical Position (0-100, 100 = top)";
		sc.Input[IN_VERT_POS].SetFloat(98.0f);

		sc.Input[IN_LINE_SPACING].Name = "Line Spacing (percent of height)";
		sc.Input[IN_LINE_SPACING].SetFloat(3.0f);

		sc.Input[IN_FONT_SIZE].Name = "Font Size";
		sc.Input[IN_FONT_SIZE].SetInt(12);

		sc.Input[IN_COLOR_1].Name = "Line 1 Color";
		sc.Input[IN_COLOR_1].SetColor(255, 255, 255);
		sc.Input[IN_COLOR_2].Name = "Line 2 Color";
		sc.Input[IN_COLOR_2].SetColor(0, 200, 255);
		sc.Input[IN_COLOR_3].Name = "Line 3 Color";
		sc.Input[IN_COLOR_3].SetColor(0, 255, 0);
		sc.Input[IN_COLOR_4].Name = "Line 4 Color";
		sc.Input[IN_COLOR_4].SetColor(255, 200, 0);

		sc.Input[IN_BOLD].Name = "Bold Text";
		sc.Input[IN_BOLD].SetYesNo(1);

		sc.Input[IN_TRANSPARENT_BG].Name = "Transparent Background";
		sc.Input[IN_TRANSPARENT_BG].SetYesNo(1);

		return;
	}

	// Study removed / chart closed: clean up
	if (sc.LastCallToFunction)
	{
		ClearAllLines(sc);
		return;
	}

	// On a full recalculation, drop the cache so every line is redrawn.
	if (sc.IsFullRecalculation)
	{
		for (int i = 0; i < NUM_LINES; ++i)
			sc.GetPersistentSCString(i) = "";
	}

	//------------------------------------------------------------------------
	// >>> YOUR CONTENT GOES HERE. Call SetTextLine() for any line, any time,
	//     in any order. Lines are fully independent. "" erases a line.
	//------------------------------------------------------------------------

	const int LastIndex = max(0, sc.ArraySize - 1);
	const int ValueFormat = sc.GetValueFormat();

	SCString Text;

	Text.Format("Symbol: %s", sc.Symbol.GetChars());
	SetTextLine(sc, 0, Text);

	Text.Format("Last: %s", sc.FormatGraphValue(sc.Close[LastIndex], ValueFormat).GetChars());
	SetTextLine(sc, 1, Text);

	Text.Format("Bid/Ask: %s / %s",
		sc.FormatGraphValue(sc.Bid, ValueFormat).GetChars(),
		sc.FormatGraphValue(sc.Ask, ValueFormat).GetChars());
	SetTextLine(sc, 2, Text);

	Text.Format("Bars: %d", sc.ArraySize);
	SetTextLine(sc, 3, Text);
}
