#include "sierrachart.h"

SCDLLName("TraderSmarts Unofficial DLL")

SCString ReadTextFile(SCStudyInterfaceRef sc, SCString FileLocation) {
    char TextBuffer[1000] = {};
    int FileHandle = 1;
    unsigned int *p_BytesRead = new unsigned int(0);
    sc.OpenFile(FileLocation.GetChars(), n_ACSIL::FILE_MODE_OPEN_EXISTING_FOR_SEQUENTIAL_READING, FileHandle);
    sc.ReadFile(FileHandle, TextBuffer, 1000, p_BytesRead);
    sc.CloseFile(FileHandle);
    return TextBuffer;
}

SCSFExport scsf_TraderSmarts(SCStudyInterfaceRef sc) {
    int i = sc.Index;

    SCInputRef Input_FileName = sc.Input[0];
    SCString txt = "";
    SCSubgraphRef Subgraph_Storage = sc.Subgraph[0];
    SCInputRef Input_DrawLabels = sc.Input[0];
    SCFloatArrayRef StorageArray = Subgraph_Storage.Arrays[0];

    if (sc.SetDefaults) {
        sc.GraphName = "TraderSmarts Unofficial";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;
        Subgraph_Storage.Name = "Storage";
        Subgraph_Storage.DrawStyle = DRAWSTYLE_IGNORE;

        Input_DrawLabels.Name = "Version";
        Input_DrawLabels.SetFloat(2.1);

        return;
    }

    const int ALERT_TRADERSMARTS = 26;
    const int ALERT_TRADERSMARTS_WICK = 27;
    int &RecordCount = sc.GetPersistentInt(1);
    auto close{sc.Close[i]};
    auto open{sc.Open[i]};
    auto high{sc.High[i]};
    auto low{sc.Low[i]};
    auto pclose{sc.Close[i - 1]};
    auto popen{sc.Open[i - 1]};
    auto phigh{sc.High[i - 1]};
    auto plow{sc.Low[i - 1]};
    bool bIsCurrentBar = false;
    bool sqRelaxUp;
    bool bSuperUp;

    if (i == sc.ArraySize - 2)
        bIsCurrentBar = true;

    if (bIsCurrentBar && (sc.GetBarHasClosedStatus() == BHCS_BAR_HAS_CLOSED)) {
        //sc.AddMessageToLog(txt.Format("TraderSmarts new bar"), 1);
        for (int iF = 0; iF < RecordCount; iF++) {
            // FOREACH price in the array, check candle interception status
            auto val { StorageArray[iF] };
            if (StorageArray[iF] != 0) {
                if (high > val && close < val && open < val) {
                    sc.AddMessageToLog(txt.Format("TraderSmarts WICK Green"), 1);
                    sc.AlertWithMessage(ALERT_TRADERSMARTS_WICK, "TraderSmarts WICK");
                    break;
                }
                if (low < val && close > val && open > val) {
                    sc.AddMessageToLog(txt.Format("TraderSmarts WICK Red"), 1);
                    sc.AlertWithMessage(ALERT_TRADERSMARTS_WICK, "TraderSmarts WICK");
                    break;
                }
                //sc.AddMessageToLog(txt.Format("StorageArray[%d] = %f", iF, StorageArray[iF]), 1);
            }
        }
    }

    // FULL REFRESH (INSERT key)
    if (sc.UpdateStartIndex == 0) {
        SCString PathAndFileName = "";

        // Reset the record count and zero out previously used slots
        for (int iG = 0; iG < RecordCount; iG++)
            StorageArray[iG] = 0.0f;
        RecordCount = 0;

        if (strstr(sc.Symbol.GetChars(), "ES") != NULL)
            PathAndFileName = "C:\\temp\\TraderSmartsES.txt";
        else if (strstr(sc.Symbol.GetChars(), "NQ") != NULL)
            PathAndFileName = "C:\\temp\\TraderSmartsNQ.txt";
        else if (strstr(sc.Symbol.GetChars(), "YM") != NULL)
            PathAndFileName = "C:\\temp\\TraderSmartsYM.txt";
        else if (strstr(sc.Symbol.GetChars(), "CL") != NULL)
            PathAndFileName = "C:\\temp\\TraderSmartsCL.txt";
        SCString MyInputString("");
        SCString w("");

        float &fStart = sc.GetPersistentFloat(0);
        float &fEnd = sc.GetPersistentFloat(1);
        SCString &desc = sc.GetPersistentSCString(2);

        //sc.AddMessageToLog("starting bro", 0);
        if (MyInputString == "")
            MyInputString.Format(ReadTextFile(sc, PathAndFileName));
        std::vector<SCString> sLines;
        MyInputString.ParseLines(sLines);
        int idx = 1;
        for (const SCString &ss: sLines) {
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
