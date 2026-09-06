// ============================================================================
//  Draws a horizontal line extending to the right for every candle gap that
//  occurs between two same-colored candles (e.g. two greens in a row where
//  the second candle's Low is above the first candle's High).
//
//  - Lines are drawn with s_UseTool / DRAWING_LINE and are extended bar by bar.
//    DrawLineUntilFutureIntersection is deliberately NOT used, because it does
//    not report back when the line is terminated.
//  - Every line is tracked in a persistent std::vector, so the study always
//    knows where all the lines are, which are active, and which were filled.
//  - When a later candle touches a line's price level, the line is terminated
//    exactly at that bar and a "line filled" alert is fired.
//  - An alert is also fired when a new gap is detected.
// ============================================================================

#include "sierrachart.h"
#include <vector>

SCDLLName("Candle Gap Lines")

// ---------------------------------------------------------------------------
// Internal line registry
// ---------------------------------------------------------------------------
namespace
{
    struct s_GapLine
    {
        int         LineNumber;         // ACSIL drawing line number (0 while pending)
        int         BeginIndex;         // bar the gap completed on
        int         EndIndex;           // current right edge of the drawn line
        int         LastCheckedIndex;   // last bar checked for a touch
        int         FillIndex;          // bar that filled it (-1 while active)
        int         IsUpGap;            // 1 = gap up, 0 = gap down
        int         IsActive;           // 1 = still extending, 0 = terminated
        int         IsDrawn;            // 1 = a drawing currently exists on the chart
        int         IsConfirmed;        // 1 = survived the window, gap alert issued
        int         Discarded;          // 1 = filled too fast, remove without alerting
        int         GapSizeTicks;
        float       Level;              // price level of this line
        float       GapLow;
        float       GapHigh;
        SCDateTime  CreateDateTime;
    };

    typedef std::vector<s_GapLine> GapLineVector;

    void DrawGapLine(SCStudyInterfaceRef sc, const s_GapLine& Line, uint32_t LineColor, int LineWidth)
    {
        s_UseTool Tool;
        Tool.Clear();

        Tool.ChartNumber           = sc.ChartNumber;
        Tool.DrawingType           = DRAWING_LINE;
        Tool.LineNumber            = Line.LineNumber;
        Tool.BeginIndex            = Line.BeginIndex;
        Tool.EndIndex              = Line.EndIndex;
        Tool.BeginValue            = Line.Level;
        Tool.EndValue              = Line.Level;
        Tool.Color                 = LineColor;
        Tool.LineWidth             = LineWidth;
        Tool.LineStyle             = LINESTYLE_SOLID;
        Tool.AddMethod             = UTAM_ADD_OR_ADJUST;
        Tool.AddAsUserDrawnDrawing = 0;

        sc.UseTool(Tool);
    }

    // Note: s_UseTool::AddMethod has no delete member. Removing an ACSIL
    // drawing is done with sc.DeleteACSChartDrawing().
    void DeleteGapLine(SCStudyInterfaceRef sc, int LineNumber)
    {
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LineNumber);
    }

    // Deletes every line number this study has ever issued. Done as a loop over
    // the known line numbers rather than a bulk delete, so that only drawings
    // owned by this study instance are touched.
    void DeleteAllGapLines(SCStudyInterfaceRef sc, int HighestLineNumberUsed)
    {
        for (int LineNumber = 1; LineNumber <= HighestLineNumberUsed; ++LineNumber)
            sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LineNumber);
    }

    // 1 = up candle, -1 = down candle, 0 = doji
    inline int CandleColor(SCStudyInterfaceRef sc, int Index)
    {
        const float OpenPrice  = sc.BaseData[SC_OPEN][Index];
        const float ClosePrice = sc.BaseData[SC_LAST][Index];

        if (ClosePrice > OpenPrice)
            return 1;
        if (ClosePrice < OpenPrice)
            return -1;

        return 0;
    }
}

// ---------------------------------------------------------------------------
// Study
// ---------------------------------------------------------------------------
SCSFExport scsf_CandleGapLines(SCStudyInterfaceRef sc)
{
    SCInputRef In_BarsBetween      = sc.Input[0];
    SCInputRef In_MinGapTicks      = sc.Input[1];
    SCInputRef In_RequireSameColor = sc.Input[2];
    SCInputRef In_LevelMode        = sc.Input[3];
    SCInputRef In_LineColor        = sc.Input[4];
    SCInputRef In_LineWidth        = sc.Input[5];
    SCInputRef In_ExtendBars       = sc.Input[6];
    SCInputRef In_EnableAlerts     = sc.Input[7];
    SCInputRef In_NewGapSound      = sc.Input[8];
    SCInputRef In_FillSound        = sc.Input[9];
    SCInputRef In_MaxLines         = sc.Input[10];
    SCInputRef In_KeepFilledLine   = sc.Input[11];
    SCInputRef In_LogEvents        = sc.Input[12];
    SCInputRef In_GapSource        = sc.Input[13];
    SCInputRef In_RequireCleanGap  = sc.Input[14];
    SCInputRef In_Diagnostics      = sc.Input[15];
    SCInputRef In_MinBarsForFill   = sc.Input[16];
    SCInputRef In_MinBarsToDraw    = sc.Input[17];

    SCSubgraphRef SG_ActiveCount   = sc.Subgraph[0];
    SCSubgraphRef SG_NewGapFlag    = sc.Subgraph[1];
    SCSubgraphRef SG_FilledFlag    = sc.Subgraph[2];

    // -----------------------------------------------------------------------
    if (sc.SetDefaults)
    {
        sc.GraphName        = "Candle Gap Lines";
        sc.StudyDescription = "Draws right-extending lines from candle gaps between same-colored "
                              "candles. Tracks every line internally, terminates a line at the bar "
                              "that touches it, and alerts on both gap creation and gap fill.";

        sc.GraphRegion            = 0;
        sc.AutoLoop               = 0;   // manual looping
        sc.FreeDLL                = 0;
        sc.CalculationPrecedence  = LOW_PREC_LEVEL;
        sc.ValueFormat            = VALUEFORMAT_INHERITED;
        sc.DrawZeros              = 0;

        In_BarsBetween.Name = "Bars Between The Two Gap Candles (0 = adjacent)";
        In_BarsBetween.SetInt(0);
        In_BarsBetween.SetIntLimits(0, 50);

        In_MinGapTicks.Name = "Minimum Gap Size (Ticks)";
        In_MinGapTicks.SetInt(1);
        In_MinGapTicks.SetIntLimits(1, 100000);

        In_RequireSameColor.Name = "Require Both Candles Same Color";
        In_RequireSameColor.SetYesNo(1);

        In_LevelMode.Name = "Line Level";
        In_LevelMode.SetCustomInputStrings("Far edge - full fill;Near edge - first touch;Both edges");
        In_LevelMode.SetCustomInputIndex(1);

        In_LineColor.Name = "Line Color";
        In_LineColor.SetColor(255, 255, 255);

        In_LineWidth.Name = "Line Width";
        In_LineWidth.SetInt(1);
        In_LineWidth.SetIntLimits(1, 10);

        In_ExtendBars.Name = "Extend Active Lines Past Last Bar (Bars)";
        In_ExtendBars.SetInt(0);
        In_ExtendBars.SetIntLimits(0, 500);

        In_EnableAlerts.Name = "Enable Alerts";
        In_EnableAlerts.SetYesNo(1);

        In_NewGapSound.Name = "Alert Sound Number - New Gap (0 = none)";
        In_NewGapSound.SetInt(1);
        In_NewGapSound.SetIntLimits(0, 100);

        In_FillSound.Name = "Alert Sound Number - Line Filled (0 = none)";
        In_FillSound.SetInt(2);
        In_FillSound.SetIntLimits(0, 100);

        In_MaxLines.Name = "Maximum Lines To Track";
        In_MaxLines.SetInt(500);
        In_MaxLines.SetIntLimits(10, 20000);

        In_KeepFilledLine.Name = "Keep Terminated Line Visible";
        In_KeepFilledLine.SetYesNo(1);

        In_LogEvents.Name = "Write Events To Message Log";
        In_LogEvents.SetYesNo(0);

        In_GapSource.Name = "Gap Measured Between";
        In_GapSource.SetCustomInputStrings("Wicks - High/Low;Bodies - Open/Close");
        In_GapSource.SetCustomInputIndex(1);

        In_RequireCleanGap.Name = "Bars In Between May Not Cross The Gap";
        In_RequireCleanGap.SetYesNo(0);

        In_Diagnostics.Name = "Diagnostics To Message Log";
        In_Diagnostics.SetYesNo(1);

        In_MinBarsForFill.Name = "Minimum Bars Between Gap And Fill To Alert";
        In_MinBarsForFill.SetInt(4);
        In_MinBarsForFill.SetIntLimits(0, 10000);

        In_MinBarsToDraw.Name = "Delete Line If Filled Within This Many Bars";
        In_MinBarsToDraw.SetInt(1);
        In_MinBarsToDraw.SetIntLimits(0, 1000);

        SG_ActiveCount.Name       = "Active Line Count";
        SG_ActiveCount.DrawStyle  = DRAWSTYLE_IGNORE;

        SG_NewGapFlag.Name        = "New Gap Flag";
        SG_NewGapFlag.DrawStyle   = DRAWSTYLE_IGNORE;

        SG_FilledFlag.Name        = "Line Filled Flag";
        SG_FilledFlag.DrawStyle   = DRAWSTYLE_IGNORE;

        return;
    }

    // -----------------------------------------------------------------------
    // Cleanup
    // -----------------------------------------------------------------------
    if (sc.LastCallToFunction)
    {
        GapLineVector* p_Existing = (GapLineVector*)sc.GetPersistentPointer(1);
        if (p_Existing != NULL)
        {
            delete p_Existing;
            sc.SetPersistentPointer(1, NULL);
        }
        return;
    }

    // -----------------------------------------------------------------------
    // Persistent state
    // -----------------------------------------------------------------------
    GapLineVector* p_Lines = (GapLineVector*)sc.GetPersistentPointer(1);
    if (p_Lines == NULL)
    {
        p_Lines = new GapLineVector;
        if (p_Lines == NULL)
            return;

        p_Lines->reserve(512);
        sc.SetPersistentPointer(1, p_Lines);
    }

    int& LastDetectionIndex     = sc.GetPersistentInt(1);
    int& NextLineNumber         = sc.GetPersistentInt(2);
    int& LastArraySize          = sc.GetPersistentInt(3);
    int& HighestLineNumberUsed  = sc.GetPersistentInt(4);

    // Full recalculation: throw away all state and all drawings, rebuild.
    if (sc.IsFullRecalculation || sc.UpdateStartIndex == 0)
    {
        p_Lines->clear();
        DeleteAllGapLines(sc, HighestLineNumberUsed);

        LastDetectionIndex = -1;
        NextLineNumber     = 1;
        LastArraySize      = 0;
    }

    if (sc.ArraySize < 3)
        return;

    const int   LastBarIndex   = sc.ArraySize - 1;
    const float TickSize       = sc.TickSize;
    const float Epsilon        = TickSize * 0.5f;
    const int   BarsBetween    = In_BarsBetween.GetInt();
    const int   MinGapTicks    = In_MinGapTicks.GetInt();
    const int   RequireColor   = In_RequireSameColor.GetYesNo();
    const int   LevelMode      = In_LevelMode.GetIndex();
    const uint32_t LineColor   = In_LineColor.GetColor();
    const int   LineWidth      = In_LineWidth.GetInt();
    const int   ExtendBars     = In_ExtendBars.GetInt();
    const int   AlertsEnabled  = In_EnableAlerts.GetYesNo();
    const int   LogEvents      = In_LogEvents.GetYesNo();
    const bool  UseBodies      = (In_GapSource.GetIndex() == 1);
    const int   RequireCleanGap= In_RequireCleanGap.GetYesNo();
    const int   Diagnostics    = In_Diagnostics.GetYesNo();
    const int   MinBarsForFill = In_MinBarsForFill.GetInt();
    const int   MinBarsToDraw  = In_MinBarsToDraw.GetInt();

    // Alerts must never fire while the whole chart is being rebuilt or while
    // historical data is being downloaded/backfilled.
    const bool AlertsAllowed = (AlertsEnabled != 0)
                            && (sc.IsFullRecalculation == 0)
                            && (sc.DownloadingHistoricalData == 0);

    // Reset the informational subgraphs over the range being recalculated.
    for (int BarIndex = sc.UpdateStartIndex; BarIndex <= LastBarIndex; ++BarIndex)
    {
        SG_NewGapFlag[BarIndex] = 0;
        SG_FilledFlag[BarIndex] = 0;
    }

    // -----------------------------------------------------------------------
    // 1. Gap detection - only on bars that have closed, because a still-forming
    //    bar can extend its High/Low and invalidate the gap.
    // -----------------------------------------------------------------------
    int LastClosedBarIndex = LastBarIndex;
    if (sc.GetBarHasClosedStatus(LastBarIndex) != BHCS_BAR_HAS_CLOSED)
        LastClosedBarIndex = LastBarIndex - 1;

    int FirstDetectIndex = LastDetectionIndex + 1;
    if (FirstDetectIndex < BarsBetween + 1)
        FirstDetectIndex = BarsBetween + 1;

    // Diagnostic counters for this pass.
    int BarsProcessed     = 0;
    int RawGapsFound      = 0;
    int RejectedByColor   = 0;
    int RejectedBySize    = 0;
    int RejectedByCross   = 0;
    int GapsQueued        = 0;
    int LinesConfirmed    = 0;
    int LinesDiscarded    = 0;
    int LargestGapTicks   = 0;

    for (int BarIndex = FirstDetectIndex; BarIndex <= LastClosedBarIndex; ++BarIndex)
    {
        LastDetectionIndex = BarIndex;
        ++BarsProcessed;

        const int PrevIndex = BarIndex - BarsBetween - 1;
        if (PrevIndex < 0)
            continue;

        // --- upper / lower edge of each candle, per the selected source ---
        float CurrentHigh, CurrentLow, PrevHigh, PrevLow;

        if (UseBodies)
        {
            const float CurOpen  = sc.BaseData[SC_OPEN][BarIndex];
            const float CurClose = sc.BaseData[SC_LAST][BarIndex];
            const float PrvOpen  = sc.BaseData[SC_OPEN][PrevIndex];
            const float PrvClose = sc.BaseData[SC_LAST][PrevIndex];

            CurrentHigh = max(CurOpen, CurClose);
            CurrentLow  = min(CurOpen, CurClose);
            PrevHigh    = max(PrvOpen, PrvClose);
            PrevLow     = min(PrvOpen, PrvClose);
        }
        else
        {
            CurrentHigh = sc.BaseData[SC_HIGH][BarIndex];
            CurrentLow  = sc.BaseData[SC_LOW][BarIndex];
            PrevHigh    = sc.BaseData[SC_HIGH][PrevIndex];
            PrevLow     = sc.BaseData[SC_LOW][PrevIndex];
        }

        int   IsUpGap = -1;
        float GapLow  = 0.0f;
        float GapHigh = 0.0f;

        // With "Bodies" selected these two tests are exactly the volume
        // imbalance test: CurrentLow is open[i] on a green candle and PrevHigh
        // is close[prev] on a green candle, so the first line reads
        // open[i] > close[prev]. The mirror applies to the red case.
        if (CurrentLow > PrevHigh + Epsilon)          // gap up
        {
            IsUpGap = 1;
            GapLow  = PrevHigh;
            GapHigh = CurrentLow;
        }
        else if (CurrentHigh < PrevLow - Epsilon)     // gap down
        {
            IsUpGap = 0;
            GapLow  = CurrentHigh;
            GapHigh = PrevLow;
        }
        else
        {
            continue;
        }

        ++RawGapsFound;

        const int GapSizeTicks = (int)((GapHigh - GapLow) / TickSize + 0.5f);
        if (GapSizeTicks > LargestGapTicks)
            LargestGapTicks = GapSizeTicks;

        // --- color requirement ---
        // Stricter than "both the same": a green pair may only produce an up
        // gap and a red pair only a down gap, matching the volume imbalance
        // definition in VolImbRenko.
        if (RequireColor)
        {
            const int ColorCurrent  = CandleColor(sc, BarIndex);
            const int ColorPrevious = CandleColor(sc, PrevIndex);

            const bool ColorMatchesDirection =
                   (IsUpGap == 1 && ColorCurrent ==  1 && ColorPrevious ==  1)
                || (IsUpGap == 0 && ColorCurrent == -1 && ColorPrevious == -1);

            if (!ColorMatchesDirection)
            {
                ++RejectedByColor;
                continue;
            }
        }

        // --- minimum gap size ---
        if (GapSizeTicks < MinGapTicks)
        {
            ++RejectedBySize;
            continue;
        }

        // --- optional: bars in between may not cross the gap ---
        // Off by default. In a normal 3-candle imbalance the middle candle is
        // the one that ran through this price range, so requiring it to stay
        // clear of the gap would reject every single imbalance.
        if (RequireCleanGap)
        {
            bool GapIsClean = true;

            for (int InnerIndex = PrevIndex + 1; InnerIndex < BarIndex; ++InnerIndex)
            {
                if (IsUpGap)
                {
                    if (sc.BaseData[SC_LOW][InnerIndex] < GapHigh - Epsilon)
                    {
                        GapIsClean = false;
                        break;
                    }
                }
                else
                {
                    if (sc.BaseData[SC_HIGH][InnerIndex] > GapLow + Epsilon)
                    {
                        GapIsClean = false;
                        break;
                    }
                }
            }

            if (!GapIsClean)
            {
                ++RejectedByCross;
                continue;
            }
        }

        // --- build the line level(s) ---
        const float FarEdge  = IsUpGap ? GapLow  : GapHigh;   // full fill level
        const float NearEdge = IsUpGap ? GapHigh : GapLow;    // first touch level

        float Levels[2];
        int   LevelCount = 0;

        if (LevelMode == 0)
        {
            Levels[LevelCount++] = FarEdge;
        }
        else if (LevelMode == 1)
        {
            Levels[LevelCount++] = NearEdge;
        }
        else
        {
            Levels[LevelCount++] = NearEdge;
            Levels[LevelCount++] = FarEdge;
        }

        for (int LevelIndex = 0; LevelIndex < LevelCount; ++LevelIndex)
        {
            s_GapLine NewLine;

            NewLine.LineNumber       = NextLineNumber++;
            NewLine.BeginIndex       = BarIndex;
            NewLine.EndIndex         = LastBarIndex + ExtendBars;
            NewLine.LastCheckedIndex = BarIndex;   // never test the origin bar
            NewLine.FillIndex        = -1;
            NewLine.IsUpGap          = IsUpGap;
            NewLine.IsActive         = 1;
            NewLine.IsDrawn          = 0;
            NewLine.IsConfirmed      = 0;
            NewLine.Discarded        = 0;
            NewLine.GapSizeTicks     = GapSizeTicks;
            NewLine.Level            = Levels[LevelIndex];
            NewLine.GapLow           = GapLow;
            NewLine.GapHigh          = GapHigh;
            NewLine.CreateDateTime   = sc.BaseDateTimeIn[BarIndex];

            if (NewLine.LineNumber > HighestLineNumberUsed)
                HighestLineNumberUsed = NewLine.LineNumber;

            // Draw straight away when the survival window has not played out
            // yet - that is the live case, and the line must be visible now.
            // On a historical pass the outcome is already known, so leave the
            // drawing to the confirmation step and skip the ones that get
            // discarded, rather than drawing and erasing thousands of lines.
            if (LastClosedBarIndex < BarIndex + MinBarsToDraw)
            {
                DrawGapLine(sc, NewLine, LineColor, LineWidth);
                NewLine.IsDrawn = 1;
            }

            p_Lines->push_back(NewLine);
            ++GapsQueued;
        }
    }

    // -----------------------------------------------------------------------
    // 2. Unconfirmed lines. The line is already on the chart. If it gets
    //    touched within MinBarsToDraw bars it is deleted again and no alert is
    //    issued. If it survives, the gap alert fires and it becomes a normal
    //    tracked line.
    // -----------------------------------------------------------------------
    const bool NewBarFormed = (sc.ArraySize != LastArraySize);

    for (size_t LineIdx = 0; LineIdx < p_Lines->size(); ++LineIdx)
    {
        s_GapLine& Line = (*p_Lines)[LineIdx];

        if (Line.IsConfirmed || Line.Discarded || !Line.IsActive)
            continue;

        const int ConfirmAtIndex = Line.BeginIndex + MinBarsToDraw;
        const int ScanEndIndex   = min(LastBarIndex, ConfirmAtIndex);

        bool TouchedEarly = false;

        for (int BarIndex = Line.BeginIndex + 1; BarIndex <= ScanEndIndex; ++BarIndex)
        {
            const float BarHigh = sc.BaseData[SC_HIGH][BarIndex];
            const float BarLow  = sc.BaseData[SC_LOW][BarIndex];

            if (BarHigh >= Line.Level - Epsilon && BarLow <= Line.Level + Epsilon)
            {
                TouchedEarly = true;
                break;
            }
        }

        if (TouchedEarly)
        {
            if (Line.IsDrawn)
            {
                DeleteGapLine(sc, Line.LineNumber);
                Line.IsDrawn = 0;
            }

            Line.Discarded = 1;
            Line.IsActive  = 0;
            ++LinesDiscarded;
            continue;
        }

        // Every bar in the survival window must be closed before we commit.
        if (LastClosedBarIndex < ConfirmAtIndex)
        {
            // Still undecided, but visible. Keep extending it meanwhile.
            if (Line.IsDrawn && NewBarFormed)
            {
                Line.EndIndex = LastBarIndex + ExtendBars;
                DrawGapLine(sc, Line, LineColor, LineWidth);
            }
            continue;
        }

        Line.IsConfirmed      = 1;
        Line.LastCheckedIndex = ConfirmAtIndex;
        Line.EndIndex         = LastBarIndex + ExtendBars;

        if (!Line.IsDrawn)
        {
            DrawGapLine(sc, Line, LineColor, LineWidth);
            Line.IsDrawn = 1;
        }

        ++LinesConfirmed;

        SG_NewGapFlag[Line.BeginIndex] = (float)(Line.IsUpGap ? 1 : -1);

        if (AlertsAllowed && ConfirmAtIndex >= LastBarIndex - 1)
        {
            SCString Message;
            Message.Format("Candle gap %s: %d ticks, line @ %s  [%s]",
                           Line.IsUpGap ? "UP" : "DOWN",
                           Line.GapSizeTicks,
                           sc.FormatGraphValue(Line.Level, sc.GetValueFormat()).GetChars(),
                           sc.DateTimeToString(Line.CreateDateTime, FLAG_DT_COMPLETE_DATETIME).GetChars());

            sc.SetAlert(In_NewGapSound.GetInt(), Message);

            if (LogEvents)
                sc.AddMessageToLog(Message, 0);
        }
    }

    // Drop the discarded records. Their drawings have been removed.
    for (size_t LineIdx = 0; LineIdx < p_Lines->size(); )
    {
        if ((*p_Lines)[LineIdx].Discarded)
            p_Lines->erase(p_Lines->begin() + LineIdx);
        else
            ++LineIdx;
    }

    // -----------------------------------------------------------------------
    // 3. Touch / fill detection for every confirmed, active line.
    //    The last bar is always re-tested, because it can still extend.
    // -----------------------------------------------------------------------
    for (size_t LineIdx = 0; LineIdx < p_Lines->size(); ++LineIdx)
    {
        s_GapLine& Line = (*p_Lines)[LineIdx];

        if (!Line.IsActive || !Line.IsConfirmed)
            continue;

        const int StartIndex = Line.LastCheckedIndex + 1;

        for (int BarIndex = StartIndex; BarIndex <= LastBarIndex; ++BarIndex)
        {
            const float BarHigh = sc.BaseData[SC_HIGH][BarIndex];
            const float BarLow  = sc.BaseData[SC_LOW][BarIndex];

            const bool Touched = (BarHigh >= Line.Level - Epsilon)
                              && (BarLow  <= Line.Level + Epsilon);

            if (!Touched)
                continue;

            // ---- terminate the line exactly at this bar ----
            Line.IsActive  = 0;
            Line.FillIndex = BarIndex;
            Line.EndIndex  = BarIndex;

            if (In_KeepFilledLine.GetYesNo())
                DrawGapLine(sc, Line, LineColor, LineWidth);
            else
                DeleteGapLine(sc, Line.LineNumber);

            SG_FilledFlag[BarIndex] = (float)(Line.IsUpGap ? 1 : -1);

            // Bars strictly in between the gap bar and the filling bar.
            // A fill on the very next bar gives 0.
            const int BarsInBetween = BarIndex - Line.BeginIndex - 1;
            const bool OldEnoughToAlert = (BarsInBetween >= MinBarsForFill);

            if (AlertsAllowed && BarIndex >= LastBarIndex - 1 && OldEnoughToAlert)
            {
                SCString Message;
                Message.Format("Gap line FILLED @ %s  (%s gap created %s, %d bars in between)",
                               sc.FormatGraphValue(Line.Level, sc.GetValueFormat()).GetChars(),
                               Line.IsUpGap ? "up" : "down",
                               sc.DateTimeToString(Line.CreateDateTime, FLAG_DT_COMPLETE_DATETIME).GetChars(),
                               BarsInBetween);

                sc.SetAlert(In_FillSound.GetInt(), Message);

                if (LogEvents)
                    sc.AddMessageToLog(Message, 0);
            }
            else if (LogEvents && AlertsAllowed && BarIndex >= LastBarIndex - 1)
            {
                SCString Message;
                Message.Format("Gap line filled @ %s after only %d bars - alert suppressed",
                               sc.FormatGraphValue(Line.Level, sc.GetValueFormat()).GetChars(),
                               BarsInBetween);
                sc.AddMessageToLog(Message, 0);
            }

            break;
        }

        if (Line.IsActive)
        {
            // Re-check the last bar next time round, it is not final yet.
            Line.LastCheckedIndex = LastBarIndex - 1;
            if (Line.LastCheckedIndex < Line.BeginIndex)
                Line.LastCheckedIndex = Line.BeginIndex;

            // Extend the drawing only when a new bar has appeared.
            if (NewBarFormed)
            {
                Line.EndIndex = LastBarIndex + ExtendBars;
                DrawGapLine(sc, Line, LineColor, LineWidth);
            }
        }
    }

    LastArraySize = sc.ArraySize;

    // Diagnostic summary. Only written for bulk passes (chart load, recalculate),
    // never once per bar in real time.
    if (Diagnostics && BarsProcessed > 10)
    {
        SCString Message;
        Message.Format("Candle Gap Lines: scanned %d bars | raw gaps %d | rejected: color %d, size %d, crossed %d | queued %d | drawn %d | discarded as quick fill %d | largest gap %d ticks",
                       BarsProcessed,
                       RawGapsFound,
                       RejectedByColor,
                       RejectedBySize,
                       RejectedByCross,
                       GapsQueued,
                       LinesConfirmed,
                       LinesDiscarded,
                       LargestGapTicks);

        sc.AddMessageToLog(Message, 0);
    }

    // -----------------------------------------------------------------------
    // 4. Prune old, already terminated lines.
    // -----------------------------------------------------------------------
    const int MaxLines = In_MaxLines.GetInt();
    if ((int)p_Lines->size() > MaxLines)
    {
        int RemoveCount = (int)p_Lines->size() - MaxLines;

        GapLineVector::iterator It = p_Lines->begin();
        while (It != p_Lines->end() && RemoveCount > 0)
        {
            if (It->IsActive == 0)
            {
                DeleteGapLine(sc, It->LineNumber);
                It = p_Lines->erase(It);
                --RemoveCount;
            }
            else
            {
                ++It;
            }
        }
    }

    // -----------------------------------------------------------------------
    // 5. Publish the active line count so other studies / spreadsheets can see it.
    // -----------------------------------------------------------------------
    int ActiveCount = 0;
    for (size_t LineIdx = 0; LineIdx < p_Lines->size(); ++LineIdx)
    {
        if ((*p_Lines)[LineIdx].IsActive)
            ++ActiveCount;
    }

    for (int BarIndex = sc.UpdateStartIndex; BarIndex <= LastBarIndex; ++BarIndex)
        SG_ActiveCount[BarIndex] = (float)ActiveCount;
}