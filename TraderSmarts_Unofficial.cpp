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

// Split a line into whitespace-separated tokens.
static std::vector<SCString> TokenizeWhitespace(const SCString &Line) {
    std::vector<SCString> Tokens;
    std::istringstream Stream(Line.GetChars());
    std::string Token;

    while (Stream >> Token)
        Tokens.push_back(SCString(Token.c_str()));

    return Tokens;
}

SCSFExport scsf_TraderSmarts(SCStudyInterfaceRef sc) {

#pragma region DEFAULTS

    SCSubgraphRef Subgraph_Storage = sc.Subgraph[0];
    SCInputRef Input_Version = sc.Input[0];
    SCInputRef Input_LicenseKey = sc.Input[1];
    SCInputRef Input_APIKey = sc.Input[2];
    SCInputRef Input_Directory = sc.Input[3];
    SCInputRef Input_PollSeconds = sc.Input[4];

    if (sc.SetDefaults) {
        sc.GraphName = "TraderSmarts Unofficial";
        sc.GraphRegion = 0;

        // Manual looping. This study does not compute per-bar values, it only
        // draws chart drawings, so there is no reason to run it once per bar.
        sc.AutoLoop = 0;

        // Needed so the study is called on a timer even when no new ticks
        // arrive, otherwise the text file would never be re-checked on a
        // quiet market. The time gate below keeps the cost near zero.
        sc.UpdateAlways = 1;

        sc.DrawZeros = 0;

        Subgraph_Storage.Name = "Storage";
        Subgraph_Storage.DrawStyle = DRAWSTYLE_IGNORE;

        Input_Version.Name = "Version";
        Input_Version.SetFloat(2.5f);

        Input_LicenseKey.Name = "License Key";
        Input_LicenseKey.SetString("");

        Input_APIKey.Name = "API Key";
        Input_APIKey.SetString("");

        Input_Directory.Name = "Text file directory";
        Input_Directory.SetString("c:\\SierraChart\\data");

        Input_PollSeconds.Name = "File re-check interval (seconds)";
        Input_PollSeconds.SetInt(15);
        Input_PollSeconds.SetIntLimits(1, 3600);

        return;
    }

#pragma endregion

#pragma region PERSISTENT STATE

    // Unique, non-zero base for drawing line numbers. LineNumber 0 is treated
    // by Sierra Chart as "unassigned", which makes UTAM_ADD_OR_ADJUST add a
    // brand new drawing every pass instead of adjusting the existing one.
    const int LINE_NUMBER_BASE = 71000;

    double &NextFileCheck = sc.GetPersistentDouble(0); // SCDateTime as double
    int &LastDrawnCount = sc.GetPersistentInt(0); // how many tools we drew last pass
    int &LastArraySize = sc.GetPersistentInt(1); // bar count at last draw
    SCString &LastFileContent = sc.GetPersistentSCString(0);

    // Clean up when the study is removed or the chart closes.
    if (sc.LastCallToFunction) {
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_ALL, 0);
        return;
    }

    // THE FIX FOR THE FLICKER.
    //
    // On a full recalculation (chart refresh, settings change, new data load)
    // Sierra Chart deletes every drawing this study added. The old code gated
    // redrawing on "has the wall-clock minute changed", and that gate variable
    // was persistent, so after a refresh it still matched the current minute
    // and the study refused to redraw. The drawings stayed gone until the
    // minute rolled over, then came back - which is the on/off flicker.
    //
    // Here we simply clear all the "already drawn" state so the block below
    // re-draws immediately on the same pass the drawings were wiped.
    if (sc.IsFullRecalculation) {
        NextFileCheck = 0.0;
        LastDrawnCount = 0;
        LastArraySize = 0;
        LastFileContent = "";
    }

#pragma endregion

    if (sc.ArraySize < 2)
        return;

    // Time gate: only touch the disk every N seconds.
    const SCDateTime Now = sc.CurrentSystemDateTime;
    if (Now.GetAsDouble() < NextFileCheck)
        return;

    NextFileCheck = (Now + SCDateTime::SECONDS(max(1, Input_PollSeconds.GetInt()))).GetAsDouble();

#pragma region RESOLVE SYMBOL AND READ FILE

    SCString Symbol;
    const char *SymbolChars = sc.Symbol.GetChars();

    if (strstr(SymbolChars, "NQ") != NULL) Symbol = "NQ";
    else if (strstr(SymbolChars, "ES") != NULL) Symbol = "ES";
    else if (strstr(SymbolChars, "YM") != NULL) Symbol = "YM";
    else if (strstr(SymbolChars, "CL") != NULL) Symbol = "CL";
    else if (strstr(SymbolChars, "6E") != NULL) Symbol = "6E";
    else if (strstr(SymbolChars, "RTY") != NULL) Symbol = "RTY";

    if (Symbol.GetLength() == 0)
        return; // unsupported symbol - leave whatever is on the chart alone

    SCString FilePath;
    FilePath.Format("%s\\%s.txt", Input_Directory.GetString(), Symbol.GetChars());

    const SCString FileContent = ReadTextFile(sc, FilePath);

    // Missing or empty file: keep the existing drawings rather than blanking
    // the chart. Blanking on a transient read failure is another flicker source.
    if (FileContent.GetLength() == 0)
        return;

    // Nothing changed and no new bars: leave the drawings exactly as they are.
    if (FileContent == LastFileContent && sc.ArraySize == LastArraySize)
        return;

    LastFileContent = FileContent;
    LastArraySize = sc.ArraySize;

#pragma endregion

#pragma region SPLIT INTO LINES

    std::vector<SCString> Lines;
    {
        const char *p = FileContent.GetChars();
        SCString CurrentLine;

        while (*p != '\0') {
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
                Lines.push_back(CurrentLine);
            }
        }
    }

#pragma endregion

#pragma region DRAW

    const SCDateTime ChartBegin = sc.BaseDateTimeIn[0];
    const SCDateTime ChartEnd = sc.BaseDateTimeIn[sc.ArraySize - 1];

    int DrawnCount = 0; // counts only tools actually drawn, so line numbers stay stable

    for (size_t LineIndex = 0; LineIndex < Lines.size(); ++LineIndex) {
        const SCString &Line = Lines[LineIndex];
        const char *LineChars = Line.GetChars();

        if (strstr(LineChars, "Sand") == NULL
            && strstr(LineChars, "Long") == NULL
            && strstr(LineChars, "Short") == NULL)
            continue;

        std::vector<SCString> Tokens = TokenizeWhitespace(Line);
        if (Tokens.size() < 2)
            continue;

        // "20294.25 - 20283.25 Range Short"  -> range
        // "20294.25 Long Entry"              -> single level
        const bool IsRange = (Tokens.size() >= 4 && strcmp(Tokens[1].GetChars(), "-") == 0);

        float StartValue = 0.0f;
        float EndValue = 0.0f;
        SCString Description;

        // atof, not std::stof: std::stof throws on a malformed line, and an
        // exception escaping the study function tears down the DLL, which also
        // makes the drawings vanish.
        if (IsRange) {
            StartValue = static_cast<float>(atof(Tokens[0].GetChars()));
            EndValue = static_cast<float>(atof(Tokens[2].GetChars()));

            for (size_t t = 3; t < Tokens.size(); ++t) {
                if (t > 3) Description += " ";
                Description += Tokens[t];
            }
        } else {
            StartValue = static_cast<float>(atof(Tokens[0].GetChars()));
            EndValue = StartValue;

            for (size_t t = 1; t < Tokens.size(); ++t) {
                if (t > 1) Description += " ";
                Description += Tokens[t];
            }
        }

        if (StartValue == 0.0f)
            continue; // unparseable price, skip the line

        COLORREF Color = COLOR_GAINSBORO;
        const char *DescChars = Description.GetChars();
        if (strstr(DescChars, "Sand") != NULL)
            Color = COLOR_GAINSBORO;
        else if (strstr(DescChars, "Short") != NULL)
            Color = COLOR_RED;
        else if (strstr(DescChars, "Long") != NULL)
            Color = COLOR_LIME;

        s_UseTool Tool;
        Tool.Clear();
        Tool.ChartNumber = sc.ChartNumber;
        Tool.AddMethod = UTAM_ADD_OR_ADJUST;
        Tool.LineNumber = LINE_NUMBER_BASE + DrawnCount; // stable and non-zero
        Tool.LineStyle = LINESTYLE_DASHDOTDOT;
        Tool.LineWidth = 1;
        Tool.TextAlignment = DT_RIGHT;
        Tool.ShowPrice = 0;
        Tool.FontSize = 8;
        Tool.FontBold = false;
        Tool.Color = Color;
        Tool.BeginValue = StartValue;
        Tool.EndValue = EndValue;
        Tool.BeginDateTime = ChartBegin;
        Tool.EndDateTime = ChartEnd;
        Tool.Text = Description;

        if (IsRange) {
            Tool.DrawingType = DRAWING_RECTANGLE_EXT_HIGHLIGHT;
            Tool.TransparencyLevel = 70;
            Tool.SecondaryColor = Color;
        } else {
            Tool.DrawingType = DRAWING_HORIZONTALLINE;
        }

        sc.UseTool(Tool);
        ++DrawnCount;
    }

    // Remove drawings left over from a previous, longer file. Because existing
    // drawings are adjusted in place rather than deleted and re-added, nothing
    // that should stay on screen is ever momentarily removed.
    for (int Stale = DrawnCount; Stale < LastDrawnCount; ++Stale)
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LINE_NUMBER_BASE + Stale);

    LastDrawnCount = DrawnCount;

#pragma endregion
}
