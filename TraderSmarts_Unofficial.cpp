#include "sierrachart.h"

#pragma region UTILITIES

static bool StartsWithSC(const SCString &Str, const SCString &Prefix) {
    const int PrefixLen = Prefix.GetLength();
    if (PrefixLen > Str.GetLength())
        return false;
    return strncmp(Str.GetChars(), Prefix.GetChars(), PrefixLen) == 0;
}

static bool EndsWithSC(const SCString &Str, const SCString &Suffix) {
    const int StrLen = Str.GetLength();
    const int SuffixLen = Suffix.GetLength();
    if (SuffixLen > StrLen)
        return false;
    return strcmp(Str.GetChars() + (StrLen - SuffixLen), Suffix.GetChars()) == 0;
}

static SCString UpperCaseSC(const SCString &Str) {
    std::string Buffer = Str.GetChars();

    for (char &c: Buffer)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    return SCString(Buffer.c_str());
}

static SCString LowerCaseSC(const SCString &Str) {
    std::string Buffer = Str.GetChars();

    for (char &c: Buffer)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    return SCString(Buffer.c_str());
}

static SCString CapitalizeFirstSC(const SCString &Str) {
    std::string Buffer = Str.GetChars();

    if (!Buffer.empty())
        Buffer[0] = static_cast<char>(toupper(static_cast<unsigned char>(Buffer[0])));

    for (size_t i = 1; i < Buffer.size(); ++i)
        Buffer[i] = static_cast<char>(tolower(static_cast<unsigned char>(Buffer[i])));

    return SCString(Buffer.c_str());
}

static SCString TrimSC(const SCString &Str) {
    const char *Chars = Str.GetChars();
    const int Len = Str.GetLength();

    int Start = 0;
    while (Start < Len && isspace(static_cast<unsigned char>(Chars[Start])))
        ++Start;

    int End = Len;
    while (End > Start && isspace(static_cast<unsigned char>(Chars[End - 1])))
        --End;

    return Str.GetSubString(End - Start, Start);
}

std::vector<SCString> SplitStringSC(const SCString &Input, char Delimiter) {
    std::vector<SCString> Result;
    std::string Work = Input.GetChars();
    std::stringstream Stream(Work);
    std::string Token;

    while (std::getline(Stream, Token, Delimiter)) {
        Result.push_back(Token.c_str()); // std::string -> SCString
    }

    return Result;
}

static int FindStringSC(const SCString &Str, const SCString &SubStr, int StartPos = 0) {
    const int StrLen = Str.GetLength();
    const int SubLen = SubStr.GetLength();

    if (SubLen == 0 || StartPos < 0 || StartPos > StrLen - SubLen)
        return -1;

    const char *Found = strstr(Str.GetChars() + StartPos, SubStr.GetChars());
    if (Found == nullptr)
        return -1;

    return static_cast<int>(Found - Str.GetChars());
}

static bool ContainsStringSC(const SCString &Str, const SCString &SubStr) {
    return FindStringSC(Str, SubStr) != -1;
}

static void ReplaceAllStr(std::string &Str, const std::string &From, const std::string &To) {
    if (From.empty())
        return;

    size_t Pos = 0;
    while ((Pos = Str.find(From, Pos)) != std::string::npos) {
        Str.replace(Pos, From.length(), To);
        Pos += To.length();
    }
}

static SCString ReplaceAllSC(const SCString &Str, const SCString &From, const SCString &To) {
    const int FromLen = From.GetLength();
    if (FromLen == 0)
        return Str;

    const char *StrChars = Str.GetChars();
    const char *FromChars = From.GetChars();

    SCString Result;
    int Pos = 0;
    const char *Found;

    while ((Found = strstr(StrChars + Pos, FromChars)) != nullptr) {
        int FoundPos = static_cast<int>(Found - StrChars);
        Result += Str.GetSubString(FoundPos - Pos, Pos); // text before the match
        Result += To; // the replacement
        Pos = FoundPos + FromLen; // advance past the match
    }
    Result += Str.GetSubString(Str.GetLength() - Pos, Pos); // trailing remainder

    return Result;
}

static SCString PadLeftSC(const SCString &Str, int Width, char PadChar = ' ') {
    const int Len = Str.GetLength();
    if (Len >= Width)
        return Str;
    return SCString(std::string(Width - Len, PadChar).c_str()) + Str;
}

static SCString ToStringSC(double Value, int Decimals = 2) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(Decimals) << Value;
    return SCString(oss.str().c_str());
}

static double ToDoubleSC(const SCString &Str) {
    return atof(Str.GetChars());
}

static float ToFloatSC(const SCString &Str) {
    return static_cast<float>(atof(Str.GetChars()));
}

static int ToIntSC(const SCString &Str) {
    return atoi(Str.GetChars());
}

static bool ToBoolSC(const SCString &Str) {
    SCString Trimmed = TrimSC(Str);
    SCString Lower = LowerCaseSC(Trimmed);
    const char *Chars = Lower.GetChars();

    return strcmp(Chars, "true") == 0
           || strcmp(Chars, "1") == 0
           || strcmp(Chars, "yes") == 0
           || strcmp(Chars, "y") == 0;
}

static bool EqualsIgnoreCaseSC(const SCString &A, const SCString &B) {
    if (A.GetLength() != B.GetLength())
        return false;
    return strcasecmp(A.GetChars(), B.GetChars()) == 0; // _stricmp on MSVC
}

static SCString JoinSC(const std::vector<SCString> &Parts, const SCString &Delimiter) {
    SCString Result;
    for (size_t i = 0; i < Parts.size(); ++i) {
        if (i > 0)
            Result += Delimiter;
        Result += Parts[i];
    }
    return Result;
}

static std::vector<SCString> SplitSC(const SCString &Str, char Delimiter) {
    std::vector<SCString> Result;
    std::string Buffer = Str.GetChars();
    std::stringstream ss(Buffer);
    std::string Token;

    while (std::getline(ss, Token, Delimiter))
        Result.push_back(SCString(Token.c_str()));

    return Result;
}

#pragma endregion

SCDLLName("TraderSmarts Unofficial DLL")

static SCString ReadTextFile(SCStudyInterfaceRef sc, const SCString &FileLocation) {
    int FileHandle = 0;
    if (!sc.OpenFile(FileLocation.GetChars(),
                     n_ACSIL::FILE_MODE_OPEN_EXISTING_FOR_SEQUENTIAL_READING,
                     FileHandle))
        return SCString();

    std::string Contents;
    char Buffer[4096];
    unsigned int BytesRead = 0;

    while (sc.ReadFile(FileHandle, Buffer, sizeof(Buffer), &BytesRead) && BytesRead > 0) {
        Contents.append(Buffer, BytesRead);
        BytesRead = 0;
    }

    sc.CloseFile(FileHandle);
    return SCString(Contents.c_str());
}

SCSFExport scsf_TraderSmarts(SCStudyInterfaceRef sc) {

#pragma region LOCALS
    int i = sc.Index;
    float &fStart = sc.GetPersistentFloat(0);
    float &fEnd = sc.GetPersistentFloat(1);
    SCString &desc = sc.GetPersistentSCString(2);
    SCString txt = "";
    SCSubgraphRef Subgraph_Storage = sc.Subgraph[0];
    SCInputRef Input_Version = sc.Input[0];
    SCInputRef Input_LicenseKey = sc.Input[1];
    SCInputRef Input_APIKey = sc.Input[2];
    SCInputRef Input_Directory = sc.Input[3];
    const int REQUEST_IDLE = 0;
    const int REQUEST_SENT = 1;

    if (sc.SetDefaults) {
        sc.GraphName = "TraderSmarts Unofficial";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;
        Subgraph_Storage.Name = "Storage";
        Subgraph_Storage.DrawStyle = DRAWSTYLE_IGNORE;

        Input_Version.Name = "Version";
        Input_Version.SetFloat(2.4);

        Input_LicenseKey.Name = "License Key";
        Input_LicenseKey.SetString("");

        Input_APIKey.Name = "API Key";
        Input_APIKey.SetString("");

        Input_Directory.Name = "Text file directory";
        Input_Directory.SetString("c:\\SierraChart\\data");

        return;
    }

    const int ALERT_TRADERSMARTS = 26;
    const int ALERT_TRADERSMARTS_WICK = 27;
    int &RecordCount = sc.GetPersistentInt(3);
    auto &bFullyDrawn = sc.GetPersistentInt(4);
    SCString &prevTime = sc.GetPersistentSCString(5);
    SCString Cleaned;
    SCString w;

    // Persistent state
    int &RequestState = sc.GetPersistentInt(5);
    int &LastRequestedIndex = sc.GetPersistentInt(6); // last bar index we made a request for

    if (sc.IsFullRecalculation) {
        RequestState = REQUEST_IDLE;
        LastRequestedIndex = -1;
    }

#pragma endregion

    /*if (RequestState == REQUEST_SENT) {
        if (sc.HTTPResponse != "") {
            RequestState = REQUEST_IDLE;

#pragma region PROCESS HTML
            //const char *p = sc.HTTPResponse.GetChars();

            /*
            if (strstr(p, "Tradersmarts") == NULL) {
                sc.AddMessageToLog(txt.Format("Tradersmarts string - not found"), 1);
                //return;
            }

            SCString CurrentLine;
            while (*p != '\0') {
                CurrentLine = "";
                const char *LineStart = p;
                int LineLength = 0;
                while (*p != '\0' && *p != '\n' && *p != '\r') {
                    ++p;
                    ++LineLength;
                }
                while (*p == '\n' || *p == '\r')
                    ++p;
                if (LineLength > 0) {
                    CurrentLine.Format("%.*s", LineLength, LineStart);
                    sc.AddMessageToLog(txt.Format("Line = %d %s", LineLength, CurrentLine.GetChars()), 1);
                }

                const char *Src = sc.HTTPResponse.GetChars();
                //if (strstr(sc.HTTPResponse.GetChars(), "API key is invalid")) break;;
                //if (strstr(sc.HTTPResponse.GetChars(), "EMPTY_RESPONSE")) break;
                int InsideTag = 0;

                for (const char *c = Src; *c != '\0'; ++c) {
                    if (*c == '<') {
                        // Treat <br> and </p>, </tr>, </div> as line breaks
                        if (_strnicmp(c, "<br", 3) == 0 || _strnicmp(c, "</p", 3) == 0 ||
                            _strnicmp(c, "</tr", 4) == 0 || _strnicmp(c, "</div", 5) == 0) {
                            Cleaned.Append("\n");
                        }
                        InsideTag = 1;
                    } else if (*c == '>')
                        InsideTag = 0;
                    else if (!InsideTag) {
                        SCString OneChar;
                        OneChar.Format("%c", *c);
                        Cleaned.Append(OneChar);
                    }
                }

                std::string Work = Cleaned.GetChars();
                ReplaceAllStr(Work, "&nbsp;", "");
                ReplaceAllStr(Work, "&nbsp", "");
                while (Work.find("  ") != std::string::npos)
                    ReplaceAllStr(Work, "  ", " ");
                ReplaceAllStr(Work, "Extreme", " Extreme");
                ReplaceAllStr(Work, "Highest", " Highest");
                ReplaceAllStr(Work, "Line", " Line");
                ReplaceAllStr(Work, "Range", " Range");
                Cleaned = Work.c_str();

                sc.AddMessageToLog(txt.Format("Cleaned = %d %s", LineLength, Cleaned.GetChars()), 1);
#pragma endregion

                int idx = 1;
                //std::vector<SCString> sLines;
                Cleaned.ParseLines(sLines);

                for (const SCString &ss: sLines) {
                    sc.AddMessageToLog(w.Format("ss = %s", ss.GetChars()), 1);
                    if (strstr(ss, "Sand") || strstr(ss, "Long") || strstr(ss, "Short")) {
#pragma region TS RANGES
                        int id = ss.IndexOf('-');
                        if (id > 0) {
                            int iS = ss.IndexOf(' ');
                            if (iS > 0) {
                                // 20294.25 - 20283.25 Range Short
                                fStart = std::stof(ss.Left(iS).GetChars());
                                int ix = ss.IndexOf(' ', iS + 3);
                                SCString yy = ss.GetSubString(ix - iS - 3, iS + 3);
                                desc = ss.GetSubString(ss.GetLength() - ix - 1, ix + 1);
                                fEnd = std::stof(yy.GetChars());
                                sc.AddMessageToLog(w.Format("Split = %f, %f", fStart, fEnd), 1);
                                sc.AddMessageToLog(w.Format("Desc = %s, %f to %f", desc.GetChars(), fStart, fEnd), 1);

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
                                RecordCount++;
#pragma endregion
                            }
                        } else {
                            int iS = ss.IndexOf(' ');
                            if (iS > 0) {
#pragma region TS SINGLES
                                fStart = std::stof(ss.Left(iS).GetChars());
                                desc = ss.GetSubString(ss.GetLength() - iS - 1, iS + 1);
                                //sc.AddMessageToLog(w.Format("%s = %f", desc.GetChars(), fStart), 1);
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
#pragma endregion
                            }
                        }
                    }
                    idx++;
                }#1#

                if (strstr(sc.HTTPResponse.GetChars(), "TradePlan") && RecordCount > 2)
                    bFullyDrawn = 1;
            }
        } else {
            // Still waiting on the server. Do not send another request yet.
            return;
        }
    }
*/

    if (sc.ArraySize < 2)
        return;
    int LastClosedIndex = sc.ArraySize - 2; // sc.ArraySize - 1 is the still-forming bar
    if (LastClosedIndex <= LastRequestedIndex) return;
    if (LastClosedIndex < sc.ArraySize - 1 - 3) {
        LastRequestedIndex = LastClosedIndex; // Don't process candles older than 3 candles from the current bar.
        return;
    }

    SCDateTime CurrentTime = sc.CurrentSystemDateTime;
    txt.Format("%02d:%02d", CurrentTime.GetHour(), CurrentTime.GetMinute());

    if (!bFullyDrawn && prevTime != txt) {
        int Year = 0, Month = 0, Day = 0, idx = 0;
        sc.BaseDateTimeIn[LastClosedIndex].GetDateYMD(Year, Month, Day);
        SCString URL;
        SCString symbol;

        prevTime = txt;

        if (strstr(sc.Symbol.GetChars(), "NQ") != NULL) symbol = "NQ";
        else if (strstr(sc.Symbol.GetChars(), "ES") != NULL) symbol = "ES";
        else if (strstr(sc.Symbol.GetChars(), "YM") != NULL) symbol = "YM";
        else if (strstr(sc.Symbol.GetChars(), "CL") != NULL) symbol = "CL";
        else if (strstr(sc.Symbol.GetChars(), "6E") != NULL) symbol = "6E";
        else if (strstr(sc.Symbol.GetChars(), "RTY") != NULL) symbol = "RTY";

        std::vector<SCString> sLines;
        SCString sY = ReadTextFile(sc, txt.Format("%s\\%s.txt", Input_Directory.GetString(symbol.GetChars()).GetChars()));
        const char *p = sY.GetChars();
        SCString CurrentLine;
        while (*p != '\0') {
            CurrentLine = "";
            const char *LineStart = p;
            int LineLength = 0;
            while (*p != '\0' && *p != '\n' && *p != '\r') {
                ++p;
                ++LineLength;
            }
            while (*p == '\n' || *p == '\r')
                ++p;
            if (LineLength > 0) {
                CurrentLine.Format("%.*s", LineLength, LineStart);
                sc.AddMessageToLog(txt.Format("Line = %d %s", LineLength, CurrentLine.GetChars()), 1);
                sLines.push_back(CurrentLine);
            }
        }

        for (const SCString &ss: sLines) {
            sc.AddMessageToLog(w.Format("ss = %s", ss.GetChars()), 1);
            if (strstr(ss, "Sand") || strstr(ss, "Long") || strstr(ss, "Short")) {
#pragma region TS RANGES
                int id = ss.IndexOf('-');
                if (id > 0) {
                    int iS = ss.IndexOf(' ');
                    if (iS > 0) {
                        // 20294.25 - 20283.25 Range Short
                        fStart = std::stof(ss.Left(iS).GetChars());
                        int ix = ss.IndexOf(' ', iS + 3);
                        SCString yy = ss.GetSubString(ix - iS - 3, iS + 3);
                        desc = ss.GetSubString(ss.GetLength() - ix - 1, ix + 1);
                        fEnd = std::stof(yy.GetChars());
                        sc.AddMessageToLog(w.Format("Split = %f, %f", fStart, fEnd), 1);
                        sc.AddMessageToLog(w.Format("Desc = %s, %f to %f", desc.GetChars(), fStart, fEnd), 1);

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
                        RecordCount++;
#pragma endregion
                    }
                } else {
                    int iS = ss.IndexOf(' ');
                    if (iS > 0) {
#pragma region TS SINGLES
                        fStart = std::stof(ss.Left(iS).GetChars());
                        desc = ss.GetSubString(ss.GetLength() - iS - 1, iS + 1);
                        sc.AddMessageToLog(w.Format("%s = %f", desc.GetChars(), fStart), 1);
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
#pragma endregion
                    }
                }
            }
            idx++;
        }

        /*URL.Format(
            "https://tradersmarts.quantkey.com/api/v1/plan.php?lic=%s&root=%s&date=%04d%02d%02d&apikey=%s",
            Input_LicenseKey.GetString(),
            symbol.GetChars(),
            Year, Month, Day,
            Input_APIKey.GetString());

        //sc.AddMessageToLog(txt.Format("URL does eq %s", URL.GetChars()), 1);
        if (!sc.MakeHTTPRequest(URL)) {
            sc.AddMessageToLog("Error making HTTP request.", 1);
            return; // leave state idle; will retry on next update
        }*/

        RequestState = REQUEST_SENT;
        LastRequestedIndex = LastClosedIndex;
    }
}