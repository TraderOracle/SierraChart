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

SCDLLName("Stream Sounds DLL")

SCSFExport scsf_StreamSounds(SCStudyInterfaceRef sc) {

#pragma region LOCALS
    int i = sc.Index;
    int &Sound1Played = sc.GetPersistentInt(1);
    int &Sound2Played = sc.GetPersistentInt(2);
    int &Sound3Played = sc.GetPersistentInt(3);
    int &Sound4Played = sc.GetPersistentInt(4);
    int &Sound5Played = sc.GetPersistentInt(5);
    int &Sound6Played = sc.GetPersistentInt(6);
    int &Sound7Played = sc.GetPersistentInt(7);
    int &Sound8Played = sc.GetPersistentInt(8);
    SCString &prevTime = sc.GetPersistentSCString(5);
    SCString txt = "";
    SCString sLine = "";
    SCString w = "";

    sc.SetPersistentInt(1, 0);
    sc.SetPersistentInt(2, 0);
    sc.SetPersistentInt(3, 0);
    sc.SetPersistentInt(4, 0);
    sc.SetPersistentInt(5, 0);
    sc.SetPersistentInt(6, 0);
    sc.SetPersistentInt(7, 0);
    sc.SetPersistentInt(8, 0);

    SCInputRef Input_Version = sc.Input[99];
    SCInputRef Input_Time1 = sc.Input[0];
    SCInputRef Input_Sound1 = sc.Input[1];
    SCInputRef Input_Time2 = sc.Input[2];
    SCInputRef Input_Sound2 = sc.Input[3];
    SCInputRef Input_Time3 = sc.Input[4];
    SCInputRef Input_Sound3 = sc.Input[5];
    SCInputRef Input_Time4 = sc.Input[6];
    SCInputRef Input_Sound4 = sc.Input[7];

    SCInputRef Input_Time5 = sc.Input[8];
    SCInputRef Input_Sound5 = sc.Input[9];
    SCInputRef Input_Time6 = sc.Input[10];
    SCInputRef Input_Sound6 = sc.Input[11];
    SCInputRef Input_Time7 = sc.Input[12];
    SCInputRef Input_Sound7 = sc.Input[13];
    SCInputRef Input_Time8 = sc.Input[14];
    SCInputRef Input_Sound8 = sc.Input[15];

    SCInputRef Input_Trigger1 = sc.Input[16];
    SCInputRef Input_TrigSound1 = sc.Input[17];
    SCInputRef Input_Trigger2 = sc.Input[18];
    SCInputRef Input_TrigSound2 = sc.Input[19];
    SCInputRef Input_Trigger3 = sc.Input[20];
    SCInputRef Input_TrigSound3 = sc.Input[21];

    SCSubgraphRef Subgraph_Text = sc.Subgraph[0];

    const int NO = 0;
    const int YES = 1;
#pragma endregion

#pragma region DEFAULTS
    if (sc.SetDefaults) {
        sc.GraphName = "Stream Sounds";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;
        sc.UpdateAlways = 1;

        Input_Version.Name = "Version";
        Input_Version.SetFloat(2.7);

        Input_Time1.Name = "New York Open";
        Input_Time1.SetString("08:30");
        Input_Sound1.Name = "Sound 1";
        Input_Sound1.SetString("c:\\SierraChart\\AlertSounds\\NYopen.wav");

        Input_Time2.Name = "Market Pivot";
        Input_Time2.SetString("09:00");
        Input_Sound2.Name = "Sound 2";
        Input_Sound2.SetString("c:\\SierraChart\\AlertSounds\\MarketPivot.wav");

        Input_Time3.Name = "Inverse NY moves";
        Input_Time3.SetString("10:30");
        Input_Sound3.Name = "Sound 3";
        Input_Sound3.SetString("c:\\SierraChart\\AlertSounds\\InverseNY.wav");

        Input_Time4.Name = "Euro Move";
        Input_Time4.SetString("10:00");
        Input_Sound4.Name = "Sound 4";
        Input_Sound4.SetString("c:\\SierraChart\\AlertSounds\\EuroMove.wav");

        Input_Time5.Name = "Bond Auctions";
        Input_Time5.SetString("12:00");
        Input_Sound5.Name = "Sound 5";
        Input_Sound5.SetString("c:\\SierraChart\\AlertSounds\\BondAuctions.wav");

        Input_Time6.Name = "Capital Injection";
        Input_Time6.SetString("13:30");
        Input_Sound6.Name = "Sound 6";
        Input_Sound6.SetString("c:\\SierraChart\\AlertSounds\\CapInject.wav");

        Input_Time7.Name = "Rug Pull";
        Input_Time7.SetString("14:45");
        Input_Sound7.Name = "Sound 7";
        Input_Sound7.SetString("c:\\SierraChart\\AlertSounds\\RugPull.wav");

        Input_Time8.Name = "Time 8";
        Input_Time8.SetString("05:30");
        Input_Sound8.Name = "Sound 8";
        Input_Sound8.SetString("c:\\SierraChart\\AlertSounds\\ES.wav");

        Input_Trigger1.Name = "News in 2 minutes";
        Input_Trigger1.SetString("c:\\SierraChart\\AlertSounds\\trigger1.txt");
        Input_TrigSound1.Name = "News sound";
        Input_TrigSound1.SetString("c:\\SierraChart\\AlertSounds\\ES.wav");

        Input_Trigger2.Name = "Trigger File 2";
        Input_Trigger2.SetString("c:\\SierraChart\\AlertSounds\\trigger2.txt");
        Input_TrigSound2.Name = "Sound";
        Input_TrigSound2.SetString("c:\\SierraChart\\AlertSounds\\ES.wav");

        Input_Trigger3.Name = "Trigger File 3";
        Input_Trigger3.SetString("c:\\SierraChart\\AlertSounds\\trigger3.txt");
        Input_TrigSound3.Name = "Sound";
        Input_TrigSound3.SetString("c:\\SierraChart\\AlertSounds\\ES.wav");

        Subgraph_Text.Name = "Text";
        Subgraph_Text.DrawStyle = DRAWSTYLE_CUSTOM_TEXT;
        Subgraph_Text.PrimaryColor = RGB(151, 190, 252);
        Subgraph_Text.LineWidth = 12; // font size in points
        Subgraph_Text.DisplayNameValueInWindowsFlags = 0;

        return;
    }
#pragma endregion

    SCDateTime CurrentTime = sc.CurrentSystemDateTime;
    txt.Format("%02d:%02d", CurrentTime.GetHour(), CurrentTime.GetMinute());
    //sc.AddMessageToLog(sLine.Format("|%s| vs |%s|", txt.GetChars(), prevTime.GetChars()), 1);
    if (prevTime != txt) {
        prevTime = txt;

        //=-=-=-=-=-=-=-=-=-=-=  NEW MINUTE starts here  =-=-=-=-=-=-=-=-=-=-=

#pragma region TIME SOUNDS
        SCString text = Input_Time1.GetString();
        int played = sc.GetPersistentInt(1);
        if (text == txt && sc.GetPersistentInt(1) == 0) {
            SCString sound = Input_Sound1.GetString();
            sc.PlaySound(sound);
            sc.SetPersistentInt(1, YES);
            sc.AddMessageToLog(sLine.Format("Time1 matched %s at %s", text.GetChars(), txt.GetChars()), 1);
            sc.AddAndManageSingleTextDrawingForStudy(sc, false, 10, 10,Subgraph_Text
             , true, w.Format("%s",Input_Time1.GetString()), true, true);
        }

        text = Input_Time2.GetString();
        played = sc.GetPersistentInt(2);
        if (text == txt && sc.GetPersistentInt(2) == 0) {
            SCString sound = Input_Sound2.GetString();
            sc.PlaySound(sound);
            sc.SetPersistentInt(2, YES);
            sc.AddMessageToLog(sLine.Format("Time2 matched %s at %s", text.GetChars(), txt.GetChars()), 1);
            sc.AddAndManageSingleTextDrawingForStudy(sc, false, 10, 10,Subgraph_Text
 , true, w.Format("%s",Input_Time2.GetString()), true, true);
        }

        text = Input_Time3.GetString();
        played = sc.GetPersistentInt(3);
        if (text == txt && sc.GetPersistentInt(3) == 0) {
            SCString sound = Input_Sound3.GetString();
            sc.PlaySound(sound);
            sc.SetPersistentInt(3, YES);
            sc.AddMessageToLog(sLine.Format("Time3 matched %s at %s", text.GetChars(), txt.GetChars()), 1);
            sc.AddAndManageSingleTextDrawingForStudy(sc, false, 10, 10,Subgraph_Text
 , true, w.Format("%s",Input_Time3.GetString()), true, true);
        }

        text = Input_Time4.GetString();
        if (text == txt && sc.GetPersistentInt(4) == 0) {
            SCString sound = Input_Sound4.GetString();
            sc.PlaySound(sound);
            sc.SetPersistentInt(4, YES);
            sc.AddMessageToLog(sLine.Format("Time4 matched %s at %s", text.GetChars(), txt.GetChars()), 1);
            sc.AddAndManageSingleTextDrawingForStudy(sc, false, 10, 10,Subgraph_Text
 , true, w.Format("%s",Input_Time4.GetString()), true, true);
        }

        text = Input_Time5.GetString();
        if (text == txt && sc.GetPersistentInt(5) == 0) {
            SCString sound = Input_Sound5.GetString();
            sc.PlaySound(sound);
            sc.SetPersistentInt(5, YES);
            sc.AddMessageToLog(sLine.Format("Time5 matched %s at %s", text.GetChars(), txt.GetChars()), 1);
            sc.AddAndManageSingleTextDrawingForStudy(sc, false, 10, 10,Subgraph_Text
 , true, w.Format("%s",Input_Time5.GetString()), true, true);
        }
        text = Input_Time6.GetString();
        if (text == txt && sc.GetPersistentInt(6) == 0) {
            SCString sound = Input_Sound6.GetString();
            sc.PlaySound(sound);
            sc.SetPersistentInt(6, YES);
            sc.AddMessageToLog(sLine.Format("Time6 matched %s at %s", text.GetChars(), txt.GetChars()), 1);
            sc.AddAndManageSingleTextDrawingForStudy(sc, false, 10, 10,Subgraph_Text
 , true, w.Format("%s",Input_Time6.GetString()), true, true);
        }
        text = Input_Time7.GetString();
        if (text == txt && sc.GetPersistentInt(7) == 0) {
            SCString sound = Input_Sound7.GetString();
            sc.PlaySound(sound);
            sc.SetPersistentInt(7, YES);
            sc.AddMessageToLog(sLine.Format("Time7 matched %s at %s", text.GetChars(), txt.GetChars()), 1);
            sc.AddAndManageSingleTextDrawingForStudy(sc, false, 10, 10,Subgraph_Text
 , true, w.Format("%s",Input_Time7.GetString()), true, true);
        }
        text = Input_Time8.GetString();
        if (text == txt && sc.GetPersistentInt(8) == 0) {
            SCString sound = Input_Sound8.GetString();
            sc.PlaySound(sound);
            sc.SetPersistentInt(8, YES);
            sc.AddMessageToLog(sLine.Format("Time8 matched %s at %s", text.GetChars(), txt.GetChars()), 1);
        }
#pragma endregion

#pragma region TRIGGER SOUNDS
        int trigHandle1 = 0;
        SCString trig1 = Input_Trigger1.GetString();
        int OpenResult1 = sc.OpenFile(trig1.GetChars(), n_ACSIL::FILE_MODE_OPEN_EXISTING_FOR_SEQUENTIAL_READING,
                                      trigHandle1);
        if (OpenResult1 != 0) {
            SCString trig1File = Input_TrigSound1.GetString();
            sc.PlaySound(trig1File);
            sc.AddMessageToLog(SCString().Format("Trigger file found: %s", trig1.GetChars()), 0);
            sc.CloseFile(trigHandle1);
            int result1 = remove(trig1.GetChars());
        }

        int trigHandle2 = 0;
        SCString trig2 = Input_Trigger1.GetString();
        int OpenResult2 = sc.OpenFile(trig2.GetChars(), n_ACSIL::FILE_MODE_OPEN_EXISTING_FOR_SEQUENTIAL_READING,
                                      trigHandle2);
        if (OpenResult2 != 0) {
            SCString trig2File = Input_TrigSound2.GetString();
            sc.PlaySound(trig2File);
            sc.AddMessageToLog(SCString().Format("Trigger file found: %s", trig2.GetChars()), 0);
            sc.CloseFile(trigHandle2);
            int result2 = remove(trig2.GetChars());
        }

        int trigHandle3 = 0;
        SCString trig3 = Input_Trigger1.GetString();
        int OpenResult3 = sc.OpenFile(trig3.GetChars(), n_ACSIL::FILE_MODE_OPEN_EXISTING_FOR_SEQUENTIAL_READING,
                                      trigHandle3);
        if (OpenResult3 != 0) {
            SCString trig3File = Input_TrigSound3.GetString();
            sc.PlaySound(trig3File);
            sc.AddMessageToLog(SCString().Format("Trigger file found: %s", trig3.GetChars()), 0);
            sc.CloseFile(trigHandle3);
            int result3 = remove(trig3.GetChars());
        }
#pragma endregion

    }

    if (sc.IsFullRecalculation) {
        sc.AddAndManageSingleTextDrawingForStudy(sc, false, 10, 10,Subgraph_Text
         , true, w.Format("Like / Subscribe"), true, true);
    }
}
