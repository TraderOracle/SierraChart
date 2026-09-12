// ═══════════════════════════════════════════════════════════════════════════
//  Level -> FVG -> Fill -> Continuation                          ACSIL / C++
//
//  1. Price touches a reference level or zone
//  2. Within N bars a Fair Value Gap prints in the rejection direction
//  3. Price returns and fills that gap
//  4. Price closes back through the gap  -> signal
//
//  Port of the TradingView Pine v6 study.
//
//  NOTE: sierrachart.h defines min() and max() as preprocessor macros, so
//  std::min / std::max cannot be used anywhere in this file. Use the macros
//  or plain ternaries.
// ═══════════════════════════════════════════════════════════════════════════

#include "sierrachart.h"

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cmath>

SCDLLName("Level FVG Fill Continuation")

// ───────────────────────────── SUBGRAPHS ─────────────────────────────────
//  Running values live in Arrays[] so that Data[] can be blanked when a
//  level is switched off without breaking the bar-to-bar carry chain.
enum SubgraphIDs
{
    SG_LONG = 0,
    SG_SHORT,
    SG_IBH,      // Arrays[0] = IB high carry
    SG_IBL,      // Arrays[0] = IB low carry
    SG_PDH,      // Arrays[0]=day high  [1]=day low  [2]=PDH  [3]=PDL  [4]=PDC
    SG_PDL,
    SG_PDC,
    SG_ONH,      // Arrays[0] = overnight high carry
    SG_ONL,      // Arrays[0] = overnight low carry
    SG_VWAP,     // Arrays[0] = cum price*vol, Arrays[1] = cum vol
    SG_EMA,      // Arrays[0] = TR, Arrays[1] = ATR, Arrays[2] = raw EMA
    SG_COUNT
};

// ─────────────────────────────── INPUTS ──────────────────────────────────
enum InputIDs
{
    IN_USE_PASTED = 0,
    IN_SET_A, IN_SET_B, IN_SET_C, IN_SET_D,
    IN_FILE_PATH,
    IN_MATCH_SYMBOL,
    IN_DRAW_PASTED,
    IN_PASTED_BARS_BACK,
    IN_PASTED_COLOR,
    IN_PASTED_FILL,
    IN_PASTED_TRANSP,
    IN_PASTED_STYLE,
    IN_PASTED_SHOW_PRICE,

    IN_USE_IB, IN_IB_START, IN_IB_END,
    IN_USE_PD,
    IN_USE_ON, IN_ON_START, IN_ON_END,
    IN_USE_VWAP,
    IN_USE_EMA, IN_EMA_LEN,

    IN_TOL_MODE, IN_TOL_ATR, IN_TOL_TICKS, IN_COOLDOWN,

    IN_MIN_LAG, IN_MAX_LAG, IN_MIN_GAP, IN_MIN_DISP, IN_NEED_REJ,

    IN_FILL_MODE, IN_MAX_FILL, IN_CONF_MODE, IN_MAX_CONF,
    IN_INV_BUF, IN_ONE_A_DAY,

    IN_DRAW_FVG, IN_FVG_BULL, IN_FVG_BEAR, IN_FVG_TRANSP,
    IN_ARROW_OFFSET_ATR,
    IN_COUNT
};

// ─────────────────────────── DATA STRUCTURES ─────────────────────────────
struct SLevel
{
    std::string Name;
    float Top;
    float Bot;
    SLevel() : Top(0.0f), Bot(0.0f) {}
    SLevel(const std::string& n, float t, float b) : Name(n), Top(t), Bot(b) {}
};

struct STouch
{
    std::string Name;
    float Top;
    float Bot;
    int   Bar;
};

struct SSetup
{
    std::string Name;
    int   Dir;          // 1 = bullish, -1 = bearish
    float GTop;
    float GBot;
    float ConfP;
    int   FvgBar;
    int   FillBar;
    int   State;        // 0 = waiting for fill, 1 = filled, waiting for continuation
    int   BoxLine;      // drawing LineNumber, -1 if none
};

struct SStudyState
{
    std::vector<SLevel> Pasted;
    std::vector<STouch> Touches;
    std::vector<SSetup> Setups;
    std::map<std::string, int> LastTouchBar;
    std::map<std::string, int> LastSignalDay;   // value is a trading-day date int

    int  LastProcessed;
    int  NextLineNumber;
    int  LastDrawnArraySize;

    SStudyState() { Reset(); }

    void Reset()
    {
        Pasted.clear();
        Touches.clear();
        Setups.clear();
        LastTouchBar.clear();
        LastSignalDay.clear();
        LastProcessed      = -1;
        NextLineNumber     = 500000;
        LastDrawnArraySize = -1;
    }
};

// ══════════════════════════ TEXT PARSING HELPERS ═════════════════════════
static std::string TrimStr(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\v\f");
    if (b == std::string::npos)
        return std::string();
    size_t e = s.find_last_not_of(" \t\v\f");
    return s.substr(b, e - b + 1);
}

static void ReplaceAll(std::string& s, const std::string& from, const std::string& to)
{
    if (from.empty())
        return;
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos)
    {
        s.replace(p, from.length(), to);
        p += to.length();
    }
}

static std::vector<std::string> SplitStr(const std::string& s, char delim)
{
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == delim)
        {
            out.push_back(cur);
            cur.clear();
        }
        else
            cur += s[i];
    }
    out.push_back(cur);
    return out;
}

// Strict numeric parse: the whole token must be a number.
static bool ToNumber(const std::string& sIn, double& Out)
{
    std::string s = TrimStr(sIn);
    if (s.empty())
        return false;

    char* End = NULL;
    double v = strtod(s.c_str(), &End);
    if (End == s.c_str())
        return false;
    while (*End == ' ' || *End == '\t')
        ++End;
    if (*End != '\0')
        return false;

    Out = v;
    return true;
}

static void AddParsedLevel(std::vector<SLevel>& Out, const std::string& NameIn, double a, double b)
{
    std::string Base = TrimStr(NameIn);
    if (Base.empty())
        Base = "LVL";

    std::string Final = Base;
    int k = 2;
    bool Clash = true;
    while (Clash)
    {
        Clash = false;
        for (size_t i = 0; i < Out.size(); ++i)
        {
            if (Out[i].Name == Final)
            {
                Clash = true;
                break;
            }
        }
        if (Clash)
        {
            char Buf[32];
            sprintf(Buf, " (%d)", k++);
            Final = Base + Buf;
        }
    }

    SLevel L;
    L.Name = Final;
    L.Top  = (float)((a > b) ? a : b);     // std::max is unusable here, see header note
    L.Bot  = (float)((a < b) ? a : b);
    Out.push_back(L);
}

// Handles both formats, auto-detected per line:
//   A)  tag: name, price, name, price ...
//   B)  price [- price] name
static void ParseLevelBlock(const std::string& SrcIn, bool MatchTicker,
                            const std::string& Symbol, std::vector<SLevel>& Out)
{
    if (TrimStr(SrcIn).empty())
        return;

    std::string Src = SrcIn;
    ReplaceAll(Src, "\r", "\n");
    ReplaceAll(Src, ";", "\n");
    ReplaceAll(Src, "\xE2\x80\x93", "-");   // en dash
    ReplaceAll(Src, "\xE2\x80\x94", "-");   // em dash

    std::vector<std::string> Lines = SplitStr(Src, '\n');

    for (size_t li = 0; li < Lines.size(); ++li)
    {
        std::string Line = TrimStr(Lines[li]);
        if (Line.empty())
            continue;

        std::string Payload = Line;
        bool Use = true;

        size_t ci = Line.find(':');
        if (ci != std::string::npos)
        {
            std::string Pre = TrimStr(Line.substr(0, ci));
            Payload = TrimStr(Line.substr(ci + 1));

            if (MatchTicker && !Payload.empty())
            {
                std::string Tag;
                for (size_t i = 0; i < Pre.size(); ++i)
                    if (Pre[i] != '$' && Pre[i] != '!')
                        Tag += (char)toupper((unsigned char)Pre[i]);

                std::string Sym;
                for (size_t i = 0; i < Symbol.size(); ++i)
                    Sym += (char)toupper((unsigned char)Symbol[i]);

                Use = (Tag.size() >= 2 && Sym.size() >= 2 &&
                       Tag.substr(0, 2) == Sym.substr(0, 2));
            }
        }

        if (!Use || Payload.empty())
            continue;

        // ── format A: comma separated name / price pairs ──
        if (Payload.find(',') != std::string::npos)
        {
            std::vector<std::string> Toks = SplitStr(Payload, ',');
            std::string Buf;

            for (size_t ti = 0; ti < Toks.size(); ++ti)
            {
                std::string Tk = TrimStr(Toks[ti]);
                if (Tk.empty())
                    continue;

                double Num = 0.0;
                if (ToNumber(Tk, Num))
                {
                    AddParsedLevel(Out, Buf, Num, Num);
                    Buf.clear();
                    continue;
                }

                // a bare range inside one comma field, e.g. "7714 - 7702"
                size_t dp = Tk.find('-');
                if (dp != std::string::npos && dp > 0)
                {
                    double a = 0.0, b = 0.0;
                    if (ToNumber(Tk.substr(0, dp), a) && ToNumber(Tk.substr(dp + 1), b))
                    {
                        AddParsedLevel(Out, Buf, a, b);
                        Buf.clear();
                        continue;
                    }
                }

                Buf = Buf.empty() ? Tk : Buf + " " + Tk;
            }
        }
        // ── format B: "price [- price] name" ──
        else
        {
            std::string P = Payload;
            ReplaceAll(P, "-", " - ");

            std::vector<std::string> Toks = SplitStr(P, ' ');

            double n1 = 0.0, n2 = 0.0;
            bool   Have1 = false, Have2 = false, Dash = false;
            size_t NameStart = Toks.size();

            for (size_t ti = 0; ti < Toks.size(); ++ti)
            {
                std::string Tk = TrimStr(Toks[ti]);
                if (Tk.empty())
                    continue;

                if (Tk == "-")
                {
                    if (!Have1 || Have2)
                    {
                        NameStart = ti;
                        break;
                    }
                    Dash = true;
                    continue;
                }

                double v = 0.0;
                bool IsNum = ToNumber(Tk, v);

                // stop as soon as the price section ends
                if (!IsNum || (Have1 && !Dash) || Have2)
                {
                    NameStart = ti;
                    break;
                }

                if (!Have1) { n1 = v; Have1 = true; }
                else        { n2 = v; Have2 = true; }
            }

            std::string Name;
            for (size_t ti = NameStart; ti < Toks.size(); ++ti)
            {
                std::string Tk = TrimStr(Toks[ti]);
                if (Tk.empty())
                    continue;
                Name = Name.empty() ? Tk : Name + " " + Tk;
            }

            if (Have1)
                AddParsedLevel(Out, Name, n1, Have2 ? n2 : n1);
        }
    }
}

static void LoadLevelsFromFile(const std::string& Path, bool MatchTicker,
                               const std::string& Symbol, std::vector<SLevel>& Out,
                               SCStudyInterfaceRef sc)
{
    if (TrimStr(Path).empty())
        return;

    std::ifstream File(Path.c_str());
    if (!File.is_open())
    {
        SCString Msg;
        Msg.Format("LFFC: could not open levels file: %s", Path.c_str());
        sc.AddMessageToLog(Msg, 1);
        return;
    }

    std::string All, Line;
    while (std::getline(File, Line))
        All += Line + "\n";
    File.close();

    ParseLevelBlock(All, MatchTicker, Symbol, Out);
}

static void AddActiveLevel(std::vector<SLevel>& V, bool Use, const char* Name, float Val)
{
    if (Use && Val != 0.0f)
        V.push_back(SLevel(Name, Val, Val));
}

// ═════════════════════════════ MAIN STUDY ════════════════════════════════
SCSFExport scsf_LevelFVGFillContinuation(SCStudyInterfaceRef sc)
{
    if (sc.SetDefaults)
    {
        sc.GraphName          = "Level -> FVG -> Fill -> Continuation";
        sc.StudyDescription   = "Touch a level or zone, print an FVG in the rejection "
                                "direction, fill the gap, then close back through it.";
        sc.GraphRegion        = 0;
        sc.AutoLoop           = 0;      // manual loop: closed bars only
        sc.FreeDLL            = 0;
        sc.ValueFormat        = VALUEFORMAT_INHERITED;
        sc.DrawStudyUnderneathMainPriceGraph = 1;

        sc.Subgraph[SG_LONG].Name          = "Long Setup";
        sc.Subgraph[SG_LONG].DrawStyle     = DRAWSTYLE_ARROW_UP;
        sc.Subgraph[SG_LONG].PrimaryColor  = RGB(0, 190, 170);
        sc.Subgraph[SG_LONG].LineWidth     = 3;
        sc.Subgraph[SG_LONG].DrawZeros     = 0;

        sc.Subgraph[SG_SHORT].Name         = "Short Setup";
        sc.Subgraph[SG_SHORT].DrawStyle    = DRAWSTYLE_ARROW_DOWN;
        sc.Subgraph[SG_SHORT].PrimaryColor = RGB(235, 80, 80);
        sc.Subgraph[SG_SHORT].LineWidth    = 3;
        sc.Subgraph[SG_SHORT].DrawZeros    = 0;

        const char* LvNames[]  = { "IBH", "IBL", "PDH", "PDL", "PDC", "ONH", "ONL", "VWAP", "EMA" };
        COLORREF    LvColors[] = {
            RGB(255, 160, 60), RGB(255, 160, 60),
            RGB(90, 140, 255), RGB(90, 140, 255), RGB(60, 90, 170),
            RGB(180, 110, 220), RGB(180, 110, 220),
            RGB(230, 90, 230), RGB(230, 220, 90)
        };
        for (int k = 0; k < 9; ++k)
        {
            int sg = SG_IBH + k;
            sc.Subgraph[sg].Name         = LvNames[k];
            sc.Subgraph[sg].DrawStyle    = DRAWSTYLE_DASH;
            sc.Subgraph[sg].PrimaryColor = LvColors[k];
            sc.Subgraph[sg].LineWidth    = 1;
            sc.Subgraph[sg].DrawZeros    = 0;
        }
        sc.Subgraph[SG_EMA].DrawStyle = DRAWSTYLE_LINE;
        sc.Subgraph[SG_EMA].LineWidth = 2;

        // ── 1 · pasted levels ──
        sc.Input[IN_USE_PASTED].Name = "1 | Enable Pasted Levels";
        sc.Input[IN_USE_PASTED].SetYesNo(1);

        sc.Input[IN_SET_A].Name = "1 | Set A  (use ; between entries)";
        sc.Input[IN_SET_A].SetString("7606.75 Line in the Sand; "
                                     "7603.75 - 7594.25 Range Long");

        sc.Input[IN_SET_B].Name = "1 | Set B";
        sc.Input[IN_SET_B].SetString("");
        sc.Input[IN_SET_C].Name = "1 | Set C";
        sc.Input[IN_SET_C].SetString("");
        sc.Input[IN_SET_D].Name = "1 | Set D";
        sc.Input[IN_SET_D].SetString("");

        sc.Input[IN_FILE_PATH].Name = "1 | Levels File (optional, full path)";
        sc.Input[IN_FILE_PATH].SetString("");

        sc.Input[IN_MATCH_SYMBOL].Name = "1 | Only Use Lines Matching This Symbol";
        sc.Input[IN_MATCH_SYMBOL].SetYesNo(0);

        sc.Input[IN_DRAW_PASTED].Name = "1 | Draw Pasted Levels";
        sc.Input[IN_DRAW_PASTED].SetYesNo(1);

        sc.Input[IN_PASTED_BARS_BACK].Name = "1 | Pasted Level Length (bars)";
        sc.Input[IN_PASTED_BARS_BACK].SetInt(200);
        sc.Input[IN_PASTED_BARS_BACK].SetIntLimits(1, 100000);

        sc.Input[IN_PASTED_COLOR].Name = "1 | Pasted Line / Border Color";
        sc.Input[IN_PASTED_COLOR].SetColor(150, 150, 150);

        sc.Input[IN_PASTED_FILL].Name = "1 | Pasted Zone Fill Color";
        sc.Input[IN_PASTED_FILL].SetColor(150, 150, 150);

        sc.Input[IN_PASTED_TRANSP].Name = "1 | Pasted Zone Transparency (0-100)";
        sc.Input[IN_PASTED_TRANSP].SetInt(85);
        sc.Input[IN_PASTED_TRANSP].SetIntLimits(0, 100);

        sc.Input[IN_PASTED_STYLE].Name = "1 | Pasted Line Style";
        sc.Input[IN_PASTED_STYLE].SetCustomInputStrings("Solid;Dashed;Dotted");
        sc.Input[IN_PASTED_STYLE].SetCustomInputIndex(2);

        sc.Input[IN_PASTED_SHOW_PRICE].Name = "1 | Show Price In Pasted Label";
        sc.Input[IN_PASTED_SHOW_PRICE].SetYesNo(1);

        // ── 2 · built-in levels ──
        sc.Input[IN_USE_IB].Name = "2 | Use Initial Balance H/L";
        sc.Input[IN_USE_IB].SetYesNo(1);
        sc.Input[IN_IB_START].Name = "2 | IB Start Time";
        sc.Input[IN_IB_START].SetTime(HMS_TIME(9, 30, 0));
        sc.Input[IN_IB_END].Name = "2 | IB End Time";
        sc.Input[IN_IB_END].SetTime(HMS_TIME(10, 30, 0));

        sc.Input[IN_USE_PD].Name = "2 | Use Previous Day H/L/C";
        sc.Input[IN_USE_PD].SetYesNo(1);

        sc.Input[IN_USE_ON].Name = "2 | Use Overnight H/L";
        sc.Input[IN_USE_ON].SetYesNo(0);
        sc.Input[IN_ON_START].Name = "2 | Overnight Start Time";
        sc.Input[IN_ON_START].SetTime(HMS_TIME(18, 0, 0));
        sc.Input[IN_ON_END].Name = "2 | Overnight End Time";
        sc.Input[IN_ON_END].SetTime(HMS_TIME(9, 30, 0));

        sc.Input[IN_USE_VWAP].Name = "2 | Use Session VWAP";
        sc.Input[IN_USE_VWAP].SetYesNo(1);

        sc.Input[IN_USE_EMA].Name = "2 | Use EMA";
        sc.Input[IN_USE_EMA].SetYesNo(1);
        sc.Input[IN_EMA_LEN].Name = "2 | EMA Length";
        sc.Input[IN_EMA_LEN].SetInt(200);
        sc.Input[IN_EMA_LEN].SetIntLimits(1, 10000);

        // ── 3 · touch ──
        sc.Input[IN_TOL_MODE].Name = "3 | Touch Tolerance Mode";
        sc.Input[IN_TOL_MODE].SetCustomInputStrings("ATR;Ticks;None");
        sc.Input[IN_TOL_MODE].SetCustomInputIndex(0);
        sc.Input[IN_TOL_ATR].Name = "3 | Touch Tolerance (x ATR)";
        sc.Input[IN_TOL_ATR].SetFloat(0.10f);
        sc.Input[IN_TOL_TICKS].Name = "3 | Touch Tolerance (ticks)";
        sc.Input[IN_TOL_TICKS].SetInt(4);
        sc.Input[IN_COOLDOWN].Name = "3 | Bars Before Same Level Re-Arms";
        sc.Input[IN_COOLDOWN].SetInt(10);
        sc.Input[IN_COOLDOWN].SetIntLimits(1, 100000);

        // ── 4 · FVG ──
        sc.Input[IN_MIN_LAG].Name = "4 | Min Bars Touch To FVG";
        sc.Input[IN_MIN_LAG].SetInt(1);
        sc.Input[IN_MAX_LAG].Name = "4 | Max Bars Touch To FVG";
        sc.Input[IN_MAX_LAG].SetInt(4);
        sc.Input[IN_MIN_GAP].Name = "4 | Min Gap Size (x ATR)";
        sc.Input[IN_MIN_GAP].SetFloat(0.10f);
        sc.Input[IN_MIN_DISP].Name = "4 | Min Displacement Body (x ATR)";
        sc.Input[IN_MIN_DISP].SetFloat(0.60f);
        sc.Input[IN_NEED_REJ].Name = "4 | FVG Must Close Clear Of The Level";
        sc.Input[IN_NEED_REJ].SetYesNo(1);

        // ── 5 · fill & continuation ──
        sc.Input[IN_FILL_MODE].Name = "5 | Fill Requirement";
        sc.Input[IN_FILL_MODE].SetCustomInputStrings("Touch (near edge);50% (CE);Full (far edge)");
        sc.Input[IN_FILL_MODE].SetCustomInputIndex(2);
        sc.Input[IN_MAX_FILL].Name = "5 | Max Bars To Fill";
        sc.Input[IN_MAX_FILL].SetInt(30);
        sc.Input[IN_CONF_MODE].Name = "5 | Continuation Confirmation";
        sc.Input[IN_CONF_MODE].SetCustomInputStrings("Close back through gap;Close beyond FVG bar extreme");
        sc.Input[IN_CONF_MODE].SetCustomInputIndex(0);
        sc.Input[IN_MAX_CONF].Name = "5 | Max Bars To Confirm After Fill";
        sc.Input[IN_MAX_CONF].SetInt(15);
        sc.Input[IN_INV_BUF].Name = "5 | Invalidation Buffer Beyond Gap (x ATR)";
        sc.Input[IN_INV_BUF].SetFloat(0.15f);
        sc.Input[IN_ONE_A_DAY].Name = "5 | One Signal Per Level Per Day";
        sc.Input[IN_ONE_A_DAY].SetYesNo(0);

        // ── 6 · visuals ──
        sc.Input[IN_DRAW_FVG].Name = "6 | Draw FVG Boxes";
        sc.Input[IN_DRAW_FVG].SetYesNo(1);
        sc.Input[IN_FVG_BULL].Name = "6 | Bull FVG Color";
        sc.Input[IN_FVG_BULL].SetColor(0, 190, 170);
        sc.Input[IN_FVG_BEAR].Name = "6 | Bear FVG Color";
        sc.Input[IN_FVG_BEAR].SetColor(235, 80, 80);
        sc.Input[IN_FVG_TRANSP].Name = "6 | FVG Transparency (0-100)";
        sc.Input[IN_FVG_TRANSP].SetInt(75);
        sc.Input[IN_FVG_TRANSP].SetIntLimits(0, 100);
        sc.Input[IN_ARROW_OFFSET_ATR].Name = "6 | Signal Arrow Offset (x ATR)";
        sc.Input[IN_ARROW_OFFSET_ATR].SetFloat(0.5f);

        return;
    }

    // ─────────────────── persistent state lifecycle ──────────────────────
    SStudyState* p = (SStudyState*)sc.GetPersistentPointer(1);

    if (sc.LastCallToFunction)
    {
        if (p != NULL)
        {
            delete p;
            sc.SetPersistentPointer(1, NULL);
        }
        return;
    }

    if (p == NULL)
    {
        p = new SStudyState();
        sc.SetPersistentPointer(1, p);
    }
    SStudyState& St = *p;

    if (sc.ArraySize < 5)
        return;

    // A full recalculation, or any rewind of the update start, wipes state.
    bool FullReset = (sc.UpdateStartIndex == 0) || ((int)sc.UpdateStartIndex < St.LastProcessed);

    if (FullReset)
    {
        St.Reset();
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_ALL, 0);

        std::string Symbol = sc.Symbol.GetChars();
        bool MatchSym = sc.Input[IN_MATCH_SYMBOL].GetYesNo() != 0;

        if (sc.Input[IN_USE_PASTED].GetYesNo())
        {
            ParseLevelBlock(sc.Input[IN_SET_A].GetString(), MatchSym, Symbol, St.Pasted);
            ParseLevelBlock(sc.Input[IN_SET_B].GetString(), MatchSym, Symbol, St.Pasted);
            ParseLevelBlock(sc.Input[IN_SET_C].GetString(), MatchSym, Symbol, St.Pasted);
            ParseLevelBlock(sc.Input[IN_SET_D].GetString(), MatchSym, Symbol, St.Pasted);
            LoadLevelsFromFile(sc.Input[IN_FILE_PATH].GetString(), MatchSym, Symbol, St.Pasted, sc);
        }
    }

    // ───────────────────── cached input values ───────────────────────────
    const bool  UsePasted   = sc.Input[IN_USE_PASTED].GetYesNo() != 0;
    const bool  UseIB       = sc.Input[IN_USE_IB].GetYesNo() != 0;
    const bool  UsePD       = sc.Input[IN_USE_PD].GetYesNo() != 0;
    const bool  UseON       = sc.Input[IN_USE_ON].GetYesNo() != 0;
    const bool  UseVWAP     = sc.Input[IN_USE_VWAP].GetYesNo() != 0;
    const bool  UseEMA      = sc.Input[IN_USE_EMA].GetYesNo() != 0;
    const int   EMALen      = sc.Input[IN_EMA_LEN].GetInt();

    const int   IBStart     = sc.Input[IN_IB_START].GetTime();
    const int   IBEnd       = sc.Input[IN_IB_END].GetTime();
    const int   ONStart     = sc.Input[IN_ON_START].GetTime();
    const int   ONEnd       = sc.Input[IN_ON_END].GetTime();

    const int   TolMode     = sc.Input[IN_TOL_MODE].GetIndex();
    const float TolAtr      = sc.Input[IN_TOL_ATR].GetFloat();
    const int   TolTicks    = sc.Input[IN_TOL_TICKS].GetInt();
    const int   Cooldown    = sc.Input[IN_COOLDOWN].GetInt();

    const int   MinLag      = sc.Input[IN_MIN_LAG].GetInt();
    const int   MaxLag      = sc.Input[IN_MAX_LAG].GetInt();
    const float MinGap      = sc.Input[IN_MIN_GAP].GetFloat();
    const float MinDisp     = sc.Input[IN_MIN_DISP].GetFloat();
    const bool  NeedRej     = sc.Input[IN_NEED_REJ].GetYesNo() != 0;

    const int   FillMode    = sc.Input[IN_FILL_MODE].GetIndex();   // 0 near, 1 CE, 2 far
    const int   MaxFill     = sc.Input[IN_MAX_FILL].GetInt();
    const int   ConfMode    = sc.Input[IN_CONF_MODE].GetIndex();
    const int   MaxConf     = sc.Input[IN_MAX_CONF].GetInt();
    const float InvBuf      = sc.Input[IN_INV_BUF].GetFloat();
    const bool  OneADay     = sc.Input[IN_ONE_A_DAY].GetYesNo() != 0;

    const bool  DrawFVG     = sc.Input[IN_DRAW_FVG].GetYesNo() != 0;
    const int   FVGTransp   = sc.Input[IN_FVG_TRANSP].GetInt();
    const float ArrowOffset = sc.Input[IN_ARROW_OFFSET_ATR].GetFloat();

    // ══════════════ PASS 1: level values for every visible bar ═══════════
    // Running values are carried in Arrays[], so the forming bar can be
    // recomputed on every tick with no drift, and Data[] can be blanked for
    // disabled levels without breaking the chain.
    int DisplayStart = max(0, (int)sc.UpdateStartIndex - 1);

    for (int i = DisplayStart; i < sc.ArraySize; ++i)
    {
        if (i == 0)
        {
            sc.Subgraph[SG_PDH].Arrays[0][0]  = sc.High[0];
            sc.Subgraph[SG_PDH].Arrays[1][0]  = sc.Low[0];
            sc.Subgraph[SG_VWAP].Arrays[0][0] = (float)(((sc.High[0] + sc.Low[0] + sc.Close[0]) / 3.0) * sc.Volume[0]);
            sc.Subgraph[SG_VWAP].Arrays[1][0] = sc.Volume[0];
            continue;
        }

        // sc.GetTradingDayDate returns an int date, not an SCDateTime.
        int  Today  = sc.GetTradingDayDate(sc.BaseDateTimeIn[i]);
        int  Prev   = sc.GetTradingDayDate(sc.BaseDateTimeIn[i - 1]);
        bool NewDay = (Today != Prev);

        int TimeOfDay = sc.BaseDateTimeIn[i].GetTimeInSeconds();

        // ── previous-day values ──
        if (NewDay)
        {
            sc.Subgraph[SG_PDH].Arrays[2][i] = sc.Subgraph[SG_PDH].Arrays[0][i - 1];
            sc.Subgraph[SG_PDH].Arrays[3][i] = sc.Subgraph[SG_PDH].Arrays[1][i - 1];
            sc.Subgraph[SG_PDH].Arrays[4][i] = sc.Close[i - 1];
            sc.Subgraph[SG_PDH].Arrays[0][i] = sc.High[i];
            sc.Subgraph[SG_PDH].Arrays[1][i] = sc.Low[i];
        }
        else
        {
            sc.Subgraph[SG_PDH].Arrays[2][i] = sc.Subgraph[SG_PDH].Arrays[2][i - 1];
            sc.Subgraph[SG_PDH].Arrays[3][i] = sc.Subgraph[SG_PDH].Arrays[3][i - 1];
            sc.Subgraph[SG_PDH].Arrays[4][i] = sc.Subgraph[SG_PDH].Arrays[4][i - 1];
            sc.Subgraph[SG_PDH].Arrays[0][i] = max(sc.Subgraph[SG_PDH].Arrays[0][i - 1], sc.High[i]);
            sc.Subgraph[SG_PDH].Arrays[1][i] = min(sc.Subgraph[SG_PDH].Arrays[1][i - 1], sc.Low[i]);
        }
        sc.Subgraph[SG_PDH].Data[i] = UsePD ? sc.Subgraph[SG_PDH].Arrays[2][i] : 0.0f;
        sc.Subgraph[SG_PDL].Data[i] = UsePD ? sc.Subgraph[SG_PDH].Arrays[3][i] : 0.0f;
        sc.Subgraph[SG_PDC].Data[i] = UsePD ? sc.Subgraph[SG_PDH].Arrays[4][i] : 0.0f;

        // ── initial balance ──
        bool  InIB = (TimeOfDay >= IBStart && TimeOfDay < IBEnd);
        float ibh  = sc.Subgraph[SG_IBH].Arrays[0][i - 1];
        float ibl  = sc.Subgraph[SG_IBL].Arrays[0][i - 1];

        if (NewDay)
        {
            ibh = InIB ? sc.High[i] : 0.0f;
            ibl = InIB ? sc.Low[i]  : 0.0f;
        }
        else if (InIB)
        {
            ibh = (ibh == 0.0f) ? sc.High[i] : max(ibh, sc.High[i]);
            ibl = (ibl == 0.0f) ? sc.Low[i]  : min(ibl, sc.Low[i]);
        }
        sc.Subgraph[SG_IBH].Arrays[0][i] = ibh;
        sc.Subgraph[SG_IBL].Arrays[0][i] = ibl;
        sc.Subgraph[SG_IBH].Data[i] = UseIB ? ibh : 0.0f;
        sc.Subgraph[SG_IBL].Data[i] = UseIB ? ibl : 0.0f;

        // ── overnight range (may wrap midnight) ──
        int  PrevTOD = sc.BaseDateTimeIn[i - 1].GetTimeInSeconds();
        bool InON    = (ONStart <= ONEnd) ? (TimeOfDay >= ONStart && TimeOfDay < ONEnd)
                                          : (TimeOfDay >= ONStart || TimeOfDay < ONEnd);
        bool PrevOn  = (ONStart <= ONEnd) ? (PrevTOD   >= ONStart && PrevTOD   < ONEnd)
                                          : (PrevTOD   >= ONStart || PrevTOD   < ONEnd);

        float onh = sc.Subgraph[SG_ONH].Arrays[0][i - 1];
        float onl = sc.Subgraph[SG_ONL].Arrays[0][i - 1];

        if (InON && !PrevOn)
        {
            onh = sc.High[i];
            onl = sc.Low[i];
        }
        else if (InON)
        {
            onh = (onh == 0.0f) ? sc.High[i] : max(onh, sc.High[i]);
            onl = (onl == 0.0f) ? sc.Low[i]  : min(onl, sc.Low[i]);
        }
        sc.Subgraph[SG_ONH].Arrays[0][i] = onh;
        sc.Subgraph[SG_ONL].Arrays[0][i] = onl;
        sc.Subgraph[SG_ONH].Data[i] = UseON ? onh : 0.0f;
        sc.Subgraph[SG_ONL].Data[i] = UseON ? onl : 0.0f;

        // ── session VWAP ──
        double TP = (sc.High[i] + sc.Low[i] + sc.Close[i]) / 3.0;
        double PV = NewDay ? TP * sc.Volume[i]
                           : sc.Subgraph[SG_VWAP].Arrays[0][i - 1] + TP * sc.Volume[i];
        double VV = NewDay ? sc.Volume[i]
                           : sc.Subgraph[SG_VWAP].Arrays[1][i - 1] + sc.Volume[i];

        sc.Subgraph[SG_VWAP].Arrays[0][i] = (float)PV;
        sc.Subgraph[SG_VWAP].Arrays[1][i] = (float)VV;
        sc.Subgraph[SG_VWAP].Data[i] = UseVWAP ? (float)((VV > 0.0) ? (PV / VV) : TP) : 0.0f;

        // ── EMA (raw in Arrays[2] so the recursion survives being hidden) ──
        sc.ExponentialMovAvg(sc.BaseDataIn[SC_LAST], sc.Subgraph[SG_EMA].Arrays[2], i, EMALen);
        sc.Subgraph[SG_EMA].Data[i] = UseEMA ? sc.Subgraph[SG_EMA].Arrays[2][i] : 0.0f;

        // ── ATR: this overload needs a separate TR output array ──
        sc.ATR(sc.BaseDataIn,
               sc.Subgraph[SG_EMA].Arrays[0],   // TR out
               sc.Subgraph[SG_EMA].Arrays[1],   // ATR out
               i, 14, MOVAVGTYPE_WILDERS);
    }

    // ══════════ PASS 2: the engine, closed bars only ═════════════════════
    int EngineStart = FullReset ? 3 : max(3, St.LastProcessed + 1);
    int EngineEnd   = sc.ArraySize - 2;   // the last fully closed bar

    for (int i = EngineStart; i <= EngineEnd; ++i)
    {
        St.LastProcessed = i;

        float ATRv = sc.Subgraph[SG_EMA].Arrays[1][i];
        if (ATRv <= 0.0f)
            continue;

        int TradingDay = sc.GetTradingDayDate(sc.BaseDateTimeIn[i]);

        // ── assemble the active level list for this bar ──
        std::vector<SLevel> Lv;
        Lv.reserve(St.Pasted.size() + 9);

        AddActiveLevel(Lv, UseIB,   "IBH",  sc.Subgraph[SG_IBH].Data[i]);
        AddActiveLevel(Lv, UseIB,   "IBL",  sc.Subgraph[SG_IBL].Data[i]);
        AddActiveLevel(Lv, UsePD,   "PDH",  sc.Subgraph[SG_PDH].Data[i]);
        AddActiveLevel(Lv, UsePD,   "PDL",  sc.Subgraph[SG_PDL].Data[i]);
        AddActiveLevel(Lv, UsePD,   "PDC",  sc.Subgraph[SG_PDC].Data[i]);
        AddActiveLevel(Lv, UseON,   "ONH",  sc.Subgraph[SG_ONH].Data[i]);
        AddActiveLevel(Lv, UseON,   "ONL",  sc.Subgraph[SG_ONL].Data[i]);
        AddActiveLevel(Lv, UseVWAP, "VWAP", sc.Subgraph[SG_VWAP].Data[i]);
        AddActiveLevel(Lv, UseEMA,  "EMA",  sc.Subgraph[SG_EMA].Data[i]);

        if (UsePasted)
            for (size_t k = 0; k < St.Pasted.size(); ++k)
                Lv.push_back(St.Pasted[k]);

        // ── STEP 1: register touches ──
        float Tol = 0.0f;
        if (TolMode == 0)      Tol = ATRv * TolAtr;
        else if (TolMode == 1) Tol = (float)(sc.TickSize * TolTicks);

        for (size_t k = 0; k < Lv.size(); ++k)
        {
            if (sc.Low[i] - Tol <= Lv[k].Top && sc.High[i] + Tol >= Lv[k].Bot)
            {
                std::map<std::string, int>::iterator it = St.LastTouchBar.find(Lv[k].Name);
                int PrevBar = (it == St.LastTouchBar.end()) ? -999999 : it->second;

                if (i - PrevBar >= Cooldown)
                {
                    St.LastTouchBar[Lv[k].Name] = i;
                    STouch T;
                    T.Name = Lv[k].Name;
                    T.Top  = Lv[k].Top;
                    T.Bot  = Lv[k].Bot;
                    T.Bar  = i;
                    St.Touches.push_back(T);
                }
            }
        }

        // ── expire stale touches ──
        for (int k = (int)St.Touches.size() - 1; k >= 0; --k)
            if (i - St.Touches[k].Bar > MaxLag)
                St.Touches.erase(St.Touches.begin() + k);

        // ── STEP 2: FVG on the 3-bar window ending at i ──
        float Disp = (float)fabs(sc.Close[i - 1] - sc.Open[i - 1]);

        bool Bull = (sc.Low[i]  > sc.High[i - 2]) &&
                    ((sc.Low[i] - sc.High[i - 2]) >= ATRv * MinGap) &&
                    (Disp >= ATRv * MinDisp) && (sc.Close[i - 1] > sc.Open[i - 1]);

        bool Bear = (sc.High[i] < sc.Low[i - 2]) &&
                    ((sc.Low[i - 2] - sc.High[i]) >= ATRv * MinGap) &&
                    (Disp >= ATRv * MinDisp) && (sc.Close[i - 1] < sc.Open[i - 1]);

        if ((Bull || Bear) && !St.Touches.empty())
        {
            int   Dir  = Bull ? 1 : -1;
            float GBot = Bull ? sc.High[i - 2] : sc.High[i];
            float GTop = Bull ? sc.Low[i]      : sc.Low[i - 2];

            int Best = -1;
            for (int k = (int)St.Touches.size() - 1; k >= 0 && Best < 0; --k)
            {
                int  Lag   = i - St.Touches[k].Bar;
                bool OkLag = (Lag >= MinLag && Lag <= MaxLag);
                bool OkRej = !NeedRej ||
                             (Dir == 1 ? sc.Close[i] > St.Touches[k].Top
                                       : sc.Close[i] < St.Touches[k].Bot);
                if (OkLag && OkRej)
                    Best = k;
            }

            if (Best >= 0)
            {
                SSetup S;
                S.Name    = St.Touches[Best].Name;
                S.Dir     = Dir;
                S.GTop    = GTop;
                S.GBot    = GBot;
                S.FvgBar  = i;
                S.FillBar = 0;
                S.State   = 0;
                S.BoxLine = -1;

                if (ConfMode == 0)
                    S.ConfP = (Dir == 1) ? GTop : GBot;
                else
                    S.ConfP = (Dir == 1) ? sc.High[i] : sc.Low[i];

                if (DrawFVG)
                {
                    s_UseTool Tool;
                    Tool.Clear();
                    Tool.ChartNumber       = sc.ChartNumber;
                    Tool.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
                    Tool.LineNumber        = St.NextLineNumber++;
                    Tool.BeginIndex        = i - 2;
                    Tool.EndIndex          = i;
                    Tool.BeginValue        = GTop;
                    Tool.EndValue          = GBot;
                    Tool.Color             = (Dir == 1) ? sc.Input[IN_FVG_BULL].GetColor()
                                                        : sc.Input[IN_FVG_BEAR].GetColor();
                    Tool.SecondaryColor    = Tool.Color;
                    Tool.TransparencyLevel = FVGTransp;
                    Tool.AddMethod         = UTAM_ADD_OR_ADJUST;
                    Tool.AddAsUserDrawnDrawing = 0;
                    sc.UseTool(Tool);
                    S.BoxLine = Tool.LineNumber;
                }

                St.Setups.push_back(S);
                St.Touches.erase(St.Touches.begin() + Best);
            }
        }

        // ── STEPS 3 & 4: manage open setups ──
        float Buf = ATRv * InvBuf;

        for (int k = (int)St.Setups.size() - 1; k >= 0; --k)
        {
            SSetup& S = St.Setups[k];

            float Mid = (S.GTop + S.GBot) * 0.5f;
            float FillP;
            if (FillMode == 2)      FillP = (S.Dir == 1) ? S.GBot : S.GTop;
            else if (FillMode == 1) FillP = Mid;
            else                    FillP = (S.Dir == 1) ? S.GTop : S.GBot;

            bool Blown = (S.Dir == 1) ? (sc.Close[i] < S.GBot - Buf)
                                      : (sc.Close[i] > S.GTop + Buf);

            bool Dead  = false;
            bool Fired = false;

            if (S.State == 0)
            {
                bool Filled = (S.Dir == 1) ? (sc.Low[i] <= FillP) : (sc.High[i] >= FillP);
                if (Filled)
                {
                    S.State   = 1;
                    S.FillBar = i;
                }
                else if (Blown || (i - S.FvgBar > MaxFill))
                    Dead = true;
            }

            if (S.State == 1 && !Dead)
            {
                bool Conf = (S.Dir == 1) ? (sc.Close[i] > S.ConfP) : (sc.Close[i] < S.ConfP);

                std::map<std::string, int>::iterator it = St.LastSignalDay.find(S.Name);
                bool Ok = !OneADay || (it == St.LastSignalDay.end()) || (it->second != TradingDay);

                if (Conf && Ok)
                {
                    Fired = true;
                    Dead  = true;
                    St.LastSignalDay[S.Name] = TradingDay;

                    float Offset = ATRv * ArrowOffset;
                    if (S.Dir == 1)
                        sc.Subgraph[SG_LONG].Data[i] = sc.Low[i] - Offset;
                    else
                        sc.Subgraph[SG_SHORT].Data[i] = sc.High[i] + Offset;

                    s_UseTool Txt;
                    Txt.Clear();
                    Txt.ChartNumber   = sc.ChartNumber;
                    Txt.DrawingType   = DRAWING_TEXT;
                    Txt.LineNumber    = St.NextLineNumber++;
                    Txt.BeginIndex    = i;
                    Txt.BeginValue    = (S.Dir == 1) ? sc.Low[i] - Offset * 1.6f
                                                     : sc.High[i] + Offset * 1.6f;
                    Txt.Color         = (S.Dir == 1) ? sc.Input[IN_FVG_BULL].GetColor()
                                                     : sc.Input[IN_FVG_BEAR].GetColor();
                    Txt.FontSize      = 8;
                    Txt.Text          = S.Name.c_str();
                    Txt.TextAlignment = DT_CENTER;
                    Txt.AddMethod     = UTAM_ADD_OR_ADJUST;
                    Txt.AddAsUserDrawnDrawing = 0;
                    sc.UseTool(Txt);

                    if (i == sc.ArraySize - 2 && !sc.IsFullRecalculation)
                    {
                        SCString Msg;
                        Msg.Format("%s continuation off %s  |  %s",
                                   (S.Dir == 1) ? "LONG" : "SHORT",
                                   S.Name.c_str(),
                                   sc.Symbol.GetChars());
                        sc.SetAlert(S.Dir == 1 ? 1 : 2, Msg);
                    }
                }
                else if (Conf && !Ok)
                    Dead = true;
                else if (Blown || (i - S.FillBar > MaxConf))
                    Dead = true;
            }

            // keep the box growing while the setup is alive
            if (!Dead && S.BoxLine >= 0)
            {
                s_UseTool Tool;
                Tool.Clear();
                Tool.ChartNumber       = sc.ChartNumber;
                Tool.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
                Tool.LineNumber        = S.BoxLine;
                Tool.BeginIndex        = S.FvgBar - 2;
                Tool.EndIndex          = i;
                Tool.BeginValue        = S.GTop;
                Tool.EndValue          = S.GBot;
                Tool.Color             = (S.Dir == 1) ? sc.Input[IN_FVG_BULL].GetColor()
                                                      : sc.Input[IN_FVG_BEAR].GetColor();
                Tool.SecondaryColor    = Tool.Color;
                Tool.TransparencyLevel = FVGTransp;
                Tool.AddMethod         = UTAM_ADD_OR_ADJUST;
                Tool.AddAsUserDrawnDrawing = 0;
                sc.UseTool(Tool);
            }

            if (Dead)
            {
                if (S.BoxLine >= 0)
                {
                    if (Fired)
                    {
                        s_UseTool Tool;
                        Tool.Clear();
                        Tool.ChartNumber       = sc.ChartNumber;
                        Tool.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
                        Tool.LineNumber        = S.BoxLine;
                        Tool.BeginIndex        = S.FvgBar - 2;
                        Tool.EndIndex          = i;
                        Tool.BeginValue        = S.GTop;
                        Tool.EndValue          = S.GBot;
                        Tool.Color             = (S.Dir == 1) ? sc.Input[IN_FVG_BULL].GetColor()
                                                              : sc.Input[IN_FVG_BEAR].GetColor();
                        Tool.SecondaryColor    = Tool.Color;
                        Tool.TransparencyLevel = max(0, FVGTransp - 20);
                        Tool.AddMethod         = UTAM_ADD_OR_ADJUST;
                        Tool.AddAsUserDrawnDrawing = 0;
                        sc.UseTool(Tool);
                    }
                    else
                    {
                        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, S.BoxLine);
                    }
                }
                St.Setups.erase(St.Setups.begin() + k);
            }
        }
    }

    // ══════════════ PASS 3: draw the pasted levels and zones ═════════════
    if (UsePasted && sc.Input[IN_DRAW_PASTED].GetYesNo() && !St.Pasted.empty() &&
        St.LastDrawnArraySize != sc.ArraySize)
    {
        St.LastDrawnArraySize = sc.ArraySize;

        int Last = sc.ArraySize - 1;
        int Left = max(0, Last - sc.Input[IN_PASTED_BARS_BACK].GetInt());

        COLORREF LineCol = sc.Input[IN_PASTED_COLOR].GetColor();
        COLORREF FillCol = sc.Input[IN_PASTED_FILL].GetColor();
        int      Transp  = sc.Input[IN_PASTED_TRANSP].GetInt();
        int      StyleIx = sc.Input[IN_PASTED_STYLE].GetIndex();
        bool     ShowPx  = sc.Input[IN_PASTED_SHOW_PRICE].GetYesNo() != 0;

        // Tool.LineStyle is a SubgraphLineStyles enum, not an int.
        SubgraphLineStyles LineStyle = LINESTYLE_DOT;
        if (StyleIx == 0)      LineStyle = LINESTYLE_SOLID;
        else if (StyleIx == 1) LineStyle = LINESTYLE_DASH;

        for (size_t k = 0; k < St.Pasted.size(); ++k)
        {
            const SLevel& L = St.Pasted[k];
            int Base = 1000 + (int)k * 2;

            s_UseTool Tool;
            Tool.Clear();
            Tool.ChartNumber = sc.ChartNumber;
            Tool.LineNumber  = Base;
            Tool.BeginIndex  = Left;
            Tool.EndIndex    = Last;
            Tool.Color       = LineCol;
            Tool.LineWidth   = 1;
            Tool.LineStyle   = LineStyle;
            Tool.AddMethod   = UTAM_ADD_OR_ADJUST;
            Tool.AddAsUserDrawnDrawing = 0;

            if (L.Top == L.Bot)
            {
                Tool.DrawingType = DRAWING_LINE;
                Tool.BeginValue  = L.Top;
                Tool.EndValue    = L.Top;
            }
            else
            {
                Tool.DrawingType       = DRAWING_RECTANGLEHIGHLIGHT;
                Tool.BeginValue        = L.Top;
                Tool.EndValue          = L.Bot;
                Tool.SecondaryColor    = FillCol;
                Tool.TransparencyLevel = Transp;
            }
            sc.UseTool(Tool);

            SCString Label = L.Name.c_str();
            if (ShowPx)
            {
                if (L.Top == L.Bot)
                    Label.Format("%s  %s", L.Name.c_str(),
                                 sc.FormatGraphValue(L.Top, sc.BaseGraphValueFormat).GetChars());
                else
                    Label.Format("%s  %s - %s", L.Name.c_str(),
                                 sc.FormatGraphValue(L.Top, sc.BaseGraphValueFormat).GetChars(),
                                 sc.FormatGraphValue(L.Bot, sc.BaseGraphValueFormat).GetChars());
            }

            s_UseTool Txt;
            Txt.Clear();
            Txt.ChartNumber   = sc.ChartNumber;
            Txt.DrawingType   = DRAWING_TEXT;
            Txt.LineNumber    = Base + 1;
            Txt.BeginIndex    = Last;
            Txt.BeginValue    = L.Top;
            Txt.Color         = LineCol;
            Txt.FontSize      = 8;
            Txt.Text          = Label;
            Txt.TextAlignment = DT_RIGHT | DT_BOTTOM;
            Txt.AddMethod     = UTAM_ADD_OR_ADJUST;
            Txt.AddAsUserDrawnDrawing = 0;
            sc.UseTool(Txt);
        }
    }
}
