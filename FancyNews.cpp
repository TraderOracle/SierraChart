#include "sierrachart.h"

SCDLLName("Fancy News DLL")

SCString ReadTextFile(SCStudyInterfaceRef sc, SCString FileLocation) {
    char TextBuffer[1000] = {};
    int FileHandle = 1;
    unsigned int *p_BytesRead = new unsigned int(0);
    sc.OpenFile(FileLocation.GetChars(), n_ACSIL::FILE_MODE_OPEN_EXISTING_FOR_SEQUENTIAL_READING, FileHandle);
    sc.ReadFile(FileHandle, TextBuffer, 1000, p_BytesRead);
    sc.CloseFile(FileHandle);
    return TextBuffer;
}

SCSFExport scsf_FancyNews(SCStudyInterfaceRef sc) {
    int i = sc.Index;
    SCInputRef Input_FileName = sc.Input[0];
    SCString txt = "";
    SCSubgraphRef Subgraph_Storage = sc.Subgraph[0];
    SCInputRef Input_DrawLabels = sc.Input[0];
    SCInputRef Input_URL = sc.Input[1];
    SCFloatArrayRef StorageArray = Subgraph_Storage.Arrays[0];

    if (sc.SetDefaults) {
        sc.GraphName = "Fancy News";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;
        Subgraph_Storage.Name = "Storage";
        Subgraph_Storage.DrawStyle = DRAWSTYLE_IGNORE;

        Input_DrawLabels.Name = "Version";
        Input_DrawLabels.SetFloat(1);

        Input_DrawLabels.Name = "News URL";
        Input_DrawLabels.SetString("https://tradingeconomics.com/calendar");

        return;
    }

    const int ALERT_TRADERSMARTS = 26;
    const int ALERT_TRADERSMARTS_WICK = 27;

    int& RequestState = sc.GetPersistentInt(1);
    int &RecordCount = sc.GetPersistentInt(2);
    int &LastProcessedIndex = sc.GetPersistentInt(3);
    SCString &LastDate = sc.GetPersistentSCString(4);
    bool bBarClosed = sc.GetBarHasClosedStatus() == BHCS_BAR_HAS_CLOSED;
    auto close{sc.Close[i]};
    auto open{sc.Open[i]};
    auto high{sc.High[i]};
    auto low{sc.Low[i]};
    auto pclose{sc.Close[i - 1]};
    auto popen{sc.Open[i - 1]};
    auto phigh{sc.High[i - 1]};
    auto plow{sc.Low[i - 1]};
    bool bIsCurrentBar = false;

    if (i == sc.ArraySize - 2)
        bIsCurrentBar = true;

    SCDateTime CurrentTime = sc.CurrentSystemDateTime;
    SCDateTime TimePlusTwoMinutes = CurrentTime + SCDateTime::MINUTES(2);
    SCString TimeString;
    SCString sAMPM = "AM";
    int iHr = TimePlusTwoMinutes.GetHour();
    if (iHr >= 12) {
        iHr -= 12;
        sAMPM = "PM";
    }
    TimeString.Format("%d:%02d %s", iHr, TimePlusTwoMinutes.GetMinute(), sAMPM.GetChars());
    if (TimeString != LastDate) {
        LastDate = TimeString;
        sc.AddMessageToLog(TimeString, 1);

        SCString PathAndFileName = "C:\\temp\\today.txt";
        SCString MyInputString("");
        if (MyInputString == "")
            MyInputString.Format(ReadTextFile(sc, PathAndFileName));
        std::vector<SCString> sLines;
        MyInputString.ParseLines(sLines);
        int idx = 1;
        for (const SCString &ss: sLines) {
            //sc.AddMessageToLog(ss.GetChars(), 1);
            if (strstr(ss, TimeString)) {
                sc.AddMessageToLog("Timestring found", 1);
                sc.AlertWithMessage(29, "TraderSmarts WICK");
            }
        }
    }

    if (bIsCurrentBar) {

    }

    // FULL REFRESH (INSERT key)
    if (sc.UpdateStartIndex == 0) {
        return;

        SCString PathAndFileName = "C:\\temp\\today.txt";
        SCString MyInputString("");
        SCString w("");

        float &fStart = sc.GetPersistentFloat(0);
        float &fEnd = sc.GetPersistentFloat(1);
        SCString &desc = sc.GetPersistentSCString(2);

        if (MyInputString == "")
            MyInputString.Format(ReadTextFile(sc, PathAndFileName));
        std::vector<SCString> sLines;
        MyInputString.ParseLines(sLines);
        int idx = 1;
        for (const SCString &ss: sLines) {
            if (strstr(ss, TimeString)) {
                sc.AddMessageToLog("Timestring found", 1);
            }
            /*
                        if (strstr(ss, "MTS Numbers:"))
                        {
                            int iX = ss.IndexOf(':');
                            SCString yy = ss.Right(ss.GetLength() - iX - 2);
                            sc.AddMessageToLog(w.Format("MTS = |%s|", yy.GetChars()), 1);
                            StorageArray[RecordCount] = fStart;
                            RecordCount++;

                            std::vector<char*> tokens;
                            yy.Tokenize(", ", tokens);
                            for (SCString s : tokens)
                            {
                                fStart = std::stof(s.GetChars());
                                sc.AddMessageToLog(w.Format("Token = %f", fStart), 1);
                                s_UseTool Tool;
                                Tool.LineStyle = LINESTYLE_DASHDOTDOT;
                                Tool.TextAlignment = DT_RIGHT;
                                Tool.DrawingType = DRAWING_HORIZONTALLINE;
                                Tool.BeginValue = fStart;
                                Tool.EndValue = fStart;
                                Tool.ChartNumber = sc.ChartNumber;
                                Tool.BeginDateTime = sc.BaseDateTimeIn[0];
                                Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize - 1];
                                Tool.AddMethod = UTAM_ADD_OR_ADJUST;
                                Tool.ShowPrice = 0;
                                Tool.Text.Format("MTS");
                                Tool.FontSize = 9;
                                Tool.LineWidth = 1;
                                Tool.LineNumber = idx;
                                Tool.Color = COLOR_GAINSBORO;
                                Tool.FontBold = true;
                                sc.UseTool(Tool);
                                idx++;
                            }
                        }
            */
            //sc.AddMessageToLog(ss, 0);
            if (strstr(ss, "Sand") || strstr(ss, "Long") || strstr(ss, "Short")) {
                int id = ss.IndexOf('-');
                if (id > 0) {
                    int iS = ss.IndexOf(' ');
                    if (iS > 0) {
                        // 20294.25 - 20283.25 Range Short
                        fStart = std::stof(ss.Left(iS).GetChars());
                        int ix = ss.IndexOf(' ', iS + 3);
                        SCString yy = ss.GetSubString(ix - iS - 3, iS + 3);
                        desc = ss.GetSubString(ss.GetLength() - ix - 2, ix + 1);
                        fEnd = std::stof(yy.GetChars());
                        //sc.AddMessageToLog(w.Format("Split = %f, %f", fStart, fEnd), 1);
                        //sc.AddMessageToLog(w.Format("Desc = %s, %f to %f", desc.GetChars(), fStart, fEnd), 1);

                        StorageArray[RecordCount] = fStart;
                        RecordCount++;
                        StorageArray[RecordCount] = fEnd;
                        RecordCount++;

                        s_UseTool Tool;
                        Tool.LineStyle = LINESTYLE_DASHDOTDOT;
                        Tool.LineWidth = 1;
                        Tool.TransparencyLevel = 70;
                        Tool.TextAlignment = DT_RIGHT;
                        Tool.DrawingType = DRAWING_RECTANGLE_EXT_HIGHLIGHT;
                        Tool.BeginValue = fStart;
                        Tool.EndValue = fEnd;
                        Tool.ChartNumber = sc.ChartNumber;
                        Tool.BeginDateTime = sc.BaseDateTimeIn[0];
                        Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize - 1];
                        Tool.AddMethod = UTAM_ADD_OR_ADJUST;
                        Tool.ShowPrice = 0;
                        Tool.Text.Format("%s", desc.GetChars());
                        Tool.FontSize = 9;
                        Tool.LineNumber = idx;
                        if (strstr(desc, "Sand"))
                            Tool.Color = COLOR_GAINSBORO;
                        else if (strstr(desc, "Short"))
                            Tool.Color = COLOR_RED;
                        else if (strstr(desc, "Long"))
                            Tool.Color = COLOR_LIME;
                        Tool.FontBold = true;
                        Tool.SecondaryColor = Tool.Color;
                        sc.UseTool(Tool);
                    }
                } else {
                    int iS = ss.IndexOf(' ');
                    if (iS > 0) {
                        fStart = std::stof(ss.Left(iS).GetChars());
                        desc = ss.GetSubString(ss.GetLength() - iS - 2, iS + 1);
                        //sc.AddMessageToLog(w.Format("%s = %f", desc.GetChars(), fStart), 1);
                        StorageArray[RecordCount] = fStart;
                        RecordCount++;

                        s_UseTool Tool;
                        Tool.LineStyle = LINESTYLE_DASHDOTDOT;
                        Tool.TextAlignment = DT_RIGHT;
                        Tool.DrawingType = DRAWING_HORIZONTALLINE;
                        Tool.BeginValue = fStart;
                        Tool.EndValue = fStart;
                        Tool.ChartNumber = sc.ChartNumber;
                        Tool.BeginDateTime = sc.BaseDateTimeIn[0];
                        Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize - 1];
                        Tool.AddMethod = UTAM_ADD_OR_ADJUST;
                        Tool.ShowPrice = 0;
                        Tool.Text.Format("%s", desc.GetChars());
                        Tool.FontSize = 9;
                        Tool.LineWidth = 1;
                        Tool.LineNumber = idx;
                        if (strstr(desc, "Sand"))
                            Tool.Color = COLOR_GAINSBORO;
                        else if (strstr(desc, "Short"))
                            Tool.Color = COLOR_RED;
                        else if (strstr(desc, "Long"))
                            Tool.Color = COLOR_LIME;
                        Tool.FontBold = true;
                        sc.UseTool(Tool);
                    }
                }
            }
            idx++;
        }
    }
}
