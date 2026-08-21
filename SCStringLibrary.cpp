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

    for (char &c : Buffer)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    return SCString(Buffer.c_str());
}

static SCString LowerCaseSC(const SCString &Str) {
    std::string Buffer = Str.GetChars();

    for (char &c : Buffer)
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
        Result += Str.GetSubString(FoundPos - Pos, Pos);  // text before the match
        Result += To;                                      // the replacement
        Pos = FoundPos + FromLen;                           // advance past the match
    }
    Result += Str.GetSubString(Str.GetLength() - Pos, Pos);  // trailing remainder

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
