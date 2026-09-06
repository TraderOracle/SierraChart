// ============================================================================
//  ICT Breaker Blocks  -  Sierra Chart ACSIL study
// ----------------------------------------------------------------------------
//  A breaker block is an order block that failed and then had its origin swing
//  violated in the opposite direction. It flips polarity and is expected to act
//  as support / resistance on the retest.
//
//  BULLISH BREAKER
//      swing low L1 -> swing high H1 -> lower low L2 (< L1)
//      then a bar closes ABOVE H1  ==>  the bearish order block that produced
//      the drop from H1 has failed. Its range becomes a bullish breaker
//      (support) and is projected forward.
//
//  BEARISH BREAKER
//      swing high H1 -> swing low L1 -> higher high H2 (> H1)
//      then a bar closes BELOW L1   ==>  the bullish order block that produced
//      the rally from L1 has failed. Its range becomes a bearish breaker
//      (resistance).
//
//  The order block candle is the last up-close candle at the swing high
//  (bullish breaker) or the last down-close candle at the swing low
//  (bearish breaker), searched back a configurable number of bars.
//
//  Detection runs on CLOSED bars only, so a zone appears on the bar after the
//  break confirms. Pivots additionally lag by "Swing Strength" bars, which is
//  inherent to any swing-based structure method.
// ============================================================================

#include "sierrachart.h"
#include <vector>

SCDLLName("ICT Breaker Blocks")

namespace ICTBreaker
{
	struct s_Swing
	{
		int   BarIndex;
		float Price;
		int   IsHigh;      // 1 = swing high, 0 = swing low
	};

	struct s_Breaker
	{
		int   IsBullish;
		int   OBBarIndex;        // left edge of the zone (order block candle)
		int   AnchorSwingBar;    // the swing pivot this breaker was built from
		int   BreakBarIndex;     // bar whose close confirmed the breaker
		float Top;
		float Bottom;
		int   Mitigated;
		int   MitigatedBarIndex;
		int   RectLineNumber;
		int   TextLineNumber;
	};

	// Keep the swing list strictly alternating high / low. If two swings of the
	// same type arrive in a row, keep only the more extreme one. This produces a
	// clean zig-zag that the structure rules can rely on.
	inline void AddSwing(std::vector<s_Swing>& Swings, const s_Swing& NewSwing)
	{
		if (!Swings.empty() && Swings.back().IsHigh == NewSwing.IsHigh)
		{
			s_Swing& Last = Swings.back();

			if (NewSwing.IsHigh)
			{
				if (NewSwing.Price >= Last.Price)
					Last = NewSwing;
			}
			else
			{
				if (NewSwing.Price <= Last.Price)
					Last = NewSwing;
			}
			return;
		}

		Swings.push_back(NewSwing);

		if (Swings.size() > 200)
			Swings.erase(Swings.begin());
	}
}

using namespace ICTBreaker;

/*==========================================================================*/
SCSFExport scsf_ICTBreakerBlocks(SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_BullTop    = sc.Subgraph[0];
	SCSubgraphRef Subgraph_BullBottom = sc.Subgraph[1];
	SCSubgraphRef Subgraph_BearTop    = sc.Subgraph[2];
	SCSubgraphRef Subgraph_BearBottom = sc.Subgraph[3];

	SCInputRef Input_SwingStrength   = sc.Input[0];
	SCInputRef Input_MaxPerSide      = sc.Input[1];
	SCInputRef Input_OBLookback      = sc.Input[2];
	SCInputRef Input_BodyOnly        = sc.Input[3];
	SCInputRef Input_ExtendBars      = sc.Input[4];
	SCInputRef Input_BreakOnClose    = sc.Input[5];
	SCInputRef Input_MitigateOnClose = sc.Input[6];
	SCInputRef Input_HideMitigated   = sc.Input[7];
	SCInputRef Input_BullColor       = sc.Input[8];
	SCInputRef Input_BearColor       = sc.Input[9];
	SCInputRef Input_Transparency    = sc.Input[10];
	SCInputRef Input_ShowLabels      = sc.Input[11];
	SCInputRef Input_EnableAlerts    = sc.Input[12];
	SCInputRef Input_ExportLevels    = sc.Input[13];

	// ---------------------------------------------------------------- defaults
	if (sc.SetDefaults)
	{
		sc.GraphName            = "ICT Breaker Blocks";
		sc.StudyDescription     = "Detects ICT bullish and bearish breaker blocks from swing structure and draws the failed order block ranges as zones.";
		sc.GraphRegion          = 0;
		sc.AutoLoop             = 0;
		sc.ValueFormat          = VALUEFORMAT_INHERITED;
		sc.DrawZeros            = 0;
		sc.FreeDLL              = 0;
		sc.MaintainVolumeAtPriceData = 0;

		Subgraph_BullTop.Name       = "Bull Breaker Top";
		Subgraph_BullTop.DrawStyle  = DRAWSTYLE_IGNORE;
		Subgraph_BullBottom.Name      = "Bull Breaker Bottom";
		Subgraph_BullBottom.DrawStyle = DRAWSTYLE_IGNORE;
		Subgraph_BearTop.Name       = "Bear Breaker Top";
		Subgraph_BearTop.DrawStyle  = DRAWSTYLE_IGNORE;
		Subgraph_BearBottom.Name      = "Bear Breaker Bottom";
		Subgraph_BearBottom.DrawStyle = DRAWSTYLE_IGNORE;

		Input_SwingStrength.Name = "Swing Strength (Bars Each Side)";
		Input_SwingStrength.SetInt(5);
		Input_SwingStrength.SetIntLimits(1, 100);

		Input_MaxPerSide.Name = "Max Breakers Per Direction";
		Input_MaxPerSide.SetInt(5);
		Input_MaxPerSide.SetIntLimits(1, 50);

		Input_OBLookback.Name = "Order Block Search Bars";
		Input_OBLookback.SetInt(10);
		Input_OBLookback.SetIntLimits(0, 100);

		Input_BodyOnly.Name = "Use Candle Body Only For Zone";
		Input_BodyOnly.SetYesNo(0);

		Input_ExtendBars.Name = "Extend Zone Past Last Bar (Bars)";
		Input_ExtendBars.SetInt(10);
		Input_ExtendBars.SetIntLimits(0, 500);

		Input_BreakOnClose.Name = "Structure Break Requires Bar Close";
		Input_BreakOnClose.SetYesNo(1);

		Input_MitigateOnClose.Name = "Mitigation Requires Bar Close";
		Input_MitigateOnClose.SetYesNo(1);

		Input_HideMitigated.Name = "Hide Mitigated Breakers";
		Input_HideMitigated.SetYesNo(1);

		Input_BullColor.Name = "Bullish Breaker Color";
		Input_BullColor.SetColor(0, 160, 90);

		Input_BearColor.Name = "Bearish Breaker Color";
		Input_BearColor.SetColor(200, 60, 60);

		Input_Transparency.Name = "Zone Fill Transparency (0-100)";
		Input_Transparency.SetInt(80);
		Input_Transparency.SetIntLimits(0, 100);

		Input_ShowLabels.Name = "Show Text Labels";
		Input_ShowLabels.SetYesNo(1);

		Input_EnableAlerts.Name = "Enable Alerts On New Breaker";
		Input_EnableAlerts.SetYesNo(0);

		// The four subgraphs are a data feed for spreadsheet studies / alert
		// conditions, not something to plot. They stay at zero unless this is
		// switched on.
		Input_ExportLevels.Name = "Export Zone Levels To Subgraphs";
		Input_ExportLevels.SetYesNo(0);

		return;
	}

	// -------------------------------------------------------- persistent state
	std::vector<s_Swing>*   p_Swings   = (std::vector<s_Swing>*)sc.GetPersistentPointer(1);
	std::vector<s_Breaker>* p_Breakers = (std::vector<s_Breaker>*)sc.GetPersistentPointer(2);

	if (sc.LastCallToFunction)
	{
		if (p_Swings != NULL)
		{
			delete p_Swings;
			sc.SetPersistentPointer(1, NULL);
		}
		if (p_Breakers != NULL)
		{
			delete p_Breakers;
			sc.SetPersistentPointer(2, NULL);
		}
		return;
	}

	if (p_Swings == NULL)
	{
		p_Swings = new std::vector<s_Swing>();
		sc.SetPersistentPointer(1, p_Swings);
	}
	if (p_Breakers == NULL)
	{
		p_Breakers = new std::vector<s_Breaker>();
		sc.SetPersistentPointer(2, p_Breakers);
	}

	std::vector<s_Swing>&   Swings   = *p_Swings;
	std::vector<s_Breaker>& Breakers = *p_Breakers;

	int& LineNumberCounter = sc.GetPersistentInt(1);
	int& LastProcessedBar  = sc.GetPersistentInt(2);
	int& LastBullSwingBar  = sc.GetPersistentInt(3);   // dedup guards
	int& LastBearSwingBar  = sc.GetPersistentInt(4);

	const int  SwingStrength   = Input_SwingStrength.GetInt();
	const int  MaxPerSide      = Input_MaxPerSide.GetInt();
	const int  OBLookback      = Input_OBLookback.GetInt();
	const bool BodyOnly        = Input_BodyOnly.GetYesNo() != 0;
	const int  ExtendBars      = Input_ExtendBars.GetInt();
	const bool BreakOnClose    = Input_BreakOnClose.GetYesNo() != 0;
	const bool MitigateOnClose = Input_MitigateOnClose.GetYesNo() != 0;
	const bool HideMitigated   = Input_HideMitigated.GetYesNo() != 0;
	const bool ShowLabels      = Input_ShowLabels.GetYesNo() != 0;

	const bool IsFullRecalculation = (sc.UpdateStartIndex == 0);

	if (IsFullRecalculation)
	{
		Swings.clear();
		Breakers.clear();
		LineNumberCounter = 70000;
		LastProcessedBar  = -1;
		LastBullSwingBar  = -1;
		LastBearSwingBar  = -1;
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_ALL, 0);
	}

	if (sc.ArraySize < (SwingStrength * 2 + 5))
		return;

	// ================================================================
	//  PASS 1 - process newly closed bars: pivots, breakers, mitigation
	// ================================================================
	const int LastClosedBar = sc.ArraySize - 2;

	for (int i = max(0, LastProcessedBar + 1); i <= LastClosedBar; i++)
	{
		// ---- confirm a pivot that sits SwingStrength bars back -------------
		const int p = i - SwingStrength;

		if (p - SwingStrength >= 0)
		{
			bool IsSwingHigh = true;
			bool IsSwingLow  = true;

			for (int j = p - SwingStrength; j <= p + SwingStrength; j++)
			{
				if (j == p)
					continue;

				if (j < p)
				{
					if (sc.High[j] >= sc.High[p]) IsSwingHigh = false;
					if (sc.Low[j]  <= sc.Low[p])  IsSwingLow  = false;
				}
				else
				{
					if (sc.High[j] > sc.High[p])  IsSwingHigh = false;
					if (sc.Low[j]  < sc.Low[p])   IsSwingLow  = false;
				}
			}

			if (IsSwingHigh)
			{
				s_Swing NewSwing;
				NewSwing.BarIndex = p;
				NewSwing.Price    = sc.High[p];
				NewSwing.IsHigh   = 1;
				AddSwing(Swings, NewSwing);
			}

			if (IsSwingLow)
			{
				s_Swing NewSwing;
				NewSwing.BarIndex = p;
				NewSwing.Price    = sc.Low[p];
				NewSwing.IsHigh   = 0;
				AddSwing(Swings, NewSwing);
			}
		}

		// ---- look for a new breaker forming on bar i -----------------------
		// The pattern is examined at the tail of the swing list. Offset 1 is
		// checked as well in case a fresh pivot was confirmed on this same bar.
		for (int Offset = 0; Offset <= 1; Offset++)
		{
			const int n = (int)Swings.size() - Offset;

			if (n < 3)
				break;

			const s_Swing& S3 = Swings[n - 1];   // most recent
			const s_Swing& S2 = Swings[n - 2];
			const s_Swing& S1 = Swings[n - 3];

			if (S3.BarIndex >= i)
				continue;

			// ---------------- bullish breaker: L1 -> H1 -> L2(lower) -> break up
			if (!S1.IsHigh && S2.IsHigh && !S3.IsHigh
				&& S3.Price < S1.Price
				&& S2.BarIndex > LastBullSwingBar)
			{
				const float TestValue = BreakOnClose ? sc.Close[i] : sc.High[i];

				if (TestValue > S2.Price)
				{
					// order block = last up-close candle at / before the swing high
					int OBIndex = S2.BarIndex;
					for (int j = S2.BarIndex; j >= max(0, S2.BarIndex - OBLookback); j--)
					{
						if (sc.Close[j] > sc.Open[j])
						{
							OBIndex = j;
							break;
						}
					}

					s_Breaker NewBreaker;
					NewBreaker.IsBullish         = 1;
					NewBreaker.OBBarIndex        = OBIndex;
					NewBreaker.AnchorSwingBar    = S2.BarIndex;
					NewBreaker.BreakBarIndex     = i;
					NewBreaker.Top               = BodyOnly ? max(sc.Open[OBIndex], sc.Close[OBIndex]) : sc.High[OBIndex];
					NewBreaker.Bottom            = BodyOnly ? min(sc.Open[OBIndex], sc.Close[OBIndex]) : sc.Low[OBIndex];
					NewBreaker.Mitigated         = 0;
					NewBreaker.MitigatedBarIndex = 0;
					NewBreaker.RectLineNumber    = LineNumberCounter++;
					NewBreaker.TextLineNumber    = LineNumberCounter++;

					Breakers.push_back(NewBreaker);
					LastBullSwingBar = S2.BarIndex;

					if (Input_EnableAlerts.GetYesNo() && !IsFullRecalculation && i == LastClosedBar)
						sc.SetAlert(1, "ICT: bullish breaker block formed");
				}
			}

			// ---------------- bearish breaker: H1 -> L1 -> H2(higher) -> break down
			if (S1.IsHigh && !S2.IsHigh && S3.IsHigh
				&& S3.Price > S1.Price
				&& S2.BarIndex > LastBearSwingBar)
			{
				const float TestValue = BreakOnClose ? sc.Close[i] : sc.Low[i];

				if (TestValue < S2.Price)
				{
					// order block = last down-close candle at / before the swing low
					int OBIndex = S2.BarIndex;
					for (int j = S2.BarIndex; j >= max(0, S2.BarIndex - OBLookback); j--)
					{
						if (sc.Close[j] < sc.Open[j])
						{
							OBIndex = j;
							break;
						}
					}

					s_Breaker NewBreaker;
					NewBreaker.IsBullish         = 0;
					NewBreaker.OBBarIndex        = OBIndex;
					NewBreaker.AnchorSwingBar    = S2.BarIndex;
					NewBreaker.BreakBarIndex     = i;
					NewBreaker.Top               = BodyOnly ? max(sc.Open[OBIndex], sc.Close[OBIndex]) : sc.High[OBIndex];
					NewBreaker.Bottom            = BodyOnly ? min(sc.Open[OBIndex], sc.Close[OBIndex]) : sc.Low[OBIndex];
					NewBreaker.Mitigated         = 0;
					NewBreaker.MitigatedBarIndex = 0;
					NewBreaker.RectLineNumber    = LineNumberCounter++;
					NewBreaker.TextLineNumber    = LineNumberCounter++;

					Breakers.push_back(NewBreaker);
					LastBearSwingBar = S2.BarIndex;

					if (Input_EnableAlerts.GetYesNo() && !IsFullRecalculation && i == LastClosedBar)
						sc.SetAlert(2, "ICT: bearish breaker block formed");
				}
			}
		}

		// ---- mitigation ----------------------------------------------------
		for (size_t k = 0; k < Breakers.size(); k++)
		{
			s_Breaker& B = Breakers[k];

			if (B.Mitigated || i <= B.BreakBarIndex)
				continue;

			if (B.IsBullish)
			{
				const float TestValue = MitigateOnClose ? sc.Close[i] : sc.Low[i];
				if (TestValue < B.Bottom)
				{
					B.Mitigated         = 1;
					B.MitigatedBarIndex = i;
				}
			}
			else
			{
				const float TestValue = MitigateOnClose ? sc.Close[i] : sc.High[i];
				if (TestValue > B.Top)
				{
					B.Mitigated         = 1;
					B.MitigatedBarIndex = i;
				}
			}
		}

		LastProcessedBar = i;
	}

	// ================================================================
	//  PASS 2 - trim to the configured maximum per direction
	// ================================================================
	for (int Direction = 0; Direction <= 1; Direction++)
	{
		int Count = 0;

		for (int k = (int)Breakers.size() - 1; k >= 0; k--)
		{
			if (Breakers[k].IsBullish != Direction)
				continue;

			Count++;

			if (Count > MaxPerSide)
			{
				sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, Breakers[k].RectLineNumber);
				sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, Breakers[k].TextLineNumber);
				Breakers.erase(Breakers.begin() + k);
			}
		}
	}

	// ================================================================
	//  PASS 3 - draw
	// ================================================================
	const int RightEdge = sc.ArraySize - 1 + ExtendBars;

	for (size_t k = 0; k < Breakers.size(); k++)
	{
		const s_Breaker& B = Breakers[k];

		if (B.Mitigated && HideMitigated)
		{
			sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, B.RectLineNumber);
			sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, B.TextLineNumber);
			continue;
		}

		const COLORREF ZoneColor = B.IsBullish ? Input_BullColor.GetColor() : Input_BearColor.GetColor();
		const int      EndIndex  = B.Mitigated ? B.MitigatedBarIndex : RightEdge;

		s_UseTool Rect;
		Rect.Clear();
		Rect.ChartNumber       = sc.ChartNumber;
		Rect.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
		Rect.LineNumber        = B.RectLineNumber;
		Rect.BeginIndex        = B.OBBarIndex;
		Rect.EndIndex          = EndIndex;
		Rect.BeginValue        = B.Bottom;
		Rect.EndValue          = B.Top;
		Rect.Color             = ZoneColor;
		Rect.SecondaryColor    = ZoneColor;
		Rect.LineWidth         = 1;
		Rect.LineStyle         = B.Mitigated ? LINESTYLE_DASH : LINESTYLE_SOLID;
		Rect.TransparencyLevel = B.Mitigated ? min(95, Input_Transparency.GetInt() + 10)
		                                     : Input_Transparency.GetInt();
		Rect.AddMethod         = UTAM_ADD_OR_ADJUST;
		sc.UseTool(Rect);

		if (ShowLabels)
		{
			s_UseTool Label;
			Label.Clear();
			Label.ChartNumber   = sc.ChartNumber;
			Label.DrawingType   = DRAWING_TEXT;
			Label.LineNumber    = B.TextLineNumber;
			Label.BeginIndex    = B.OBBarIndex;
			Label.BeginValue    = B.IsBullish ? B.Bottom : B.Top;
			Label.Color         = ZoneColor;
			Label.FontSize      = 8;
			Label.FontBold      = 0;
			Label.Text          = B.IsBullish ? "Bullish Breaker" : "Bearish Breaker";
			Label.TextAlignment = B.IsBullish ? (DT_LEFT | DT_TOP) : (DT_LEFT | DT_BOTTOM);
			Label.AddMethod     = UTAM_ADD_OR_ADJUST;
			sc.UseTool(Label);
		}
	}

	// ================================================================
	//  PASS 4 - subgraph values (nearest active breaker per bar)
	// ================================================================
	if (!Input_ExportLevels.GetYesNo())
		return;

	// Values written on earlier calls go stale as breakers are mitigated or
	// trimmed, so the whole affected span is rebuilt rather than just the tail.
	int SubgraphStart = sc.UpdateStartIndex;

	for (size_t k = 0; k < Breakers.size(); k++)
		SubgraphStart = min(SubgraphStart, Breakers[k].BreakBarIndex);

	SubgraphStart = max(0, SubgraphStart);

	for (int i = SubgraphStart; i < sc.ArraySize; i++)
	{
		Subgraph_BullTop[i]    = 0.0f;
		Subgraph_BullBottom[i] = 0.0f;
		Subgraph_BearTop[i]    = 0.0f;
		Subgraph_BearBottom[i] = 0.0f;

		for (int k = (int)Breakers.size() - 1; k >= 0; k--)
		{
			const s_Breaker& B = Breakers[k];

			if (B.BreakBarIndex > i)
				continue;

			if (B.Mitigated && B.MitigatedBarIndex <= i)
				continue;

			if (B.IsBullish && Subgraph_BullTop[i] == 0.0f)
			{
				Subgraph_BullTop[i]    = B.Top;
				Subgraph_BullBottom[i] = B.Bottom;
			}
			else if (!B.IsBullish && Subgraph_BearTop[i] == 0.0f)
			{
				Subgraph_BearTop[i]    = B.Top;
				Subgraph_BearBottom[i] = B.Bottom;
			}
		}
	}
}
