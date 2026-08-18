#include "sierrachart.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

SCDLLName("TLADe_GEX_Levels")

// ============================================================================
// TLADe GEX Levels + Breakout Structure + Session AVWAP  (single study)
//
// ACSIL port of the TradingView Pine scripts gexlevel-nq-gamma.pine and
// gexlevel-es-gamma.pine. The two scripts are line-for-line identical except
// for ticker scaling constants and the baked-in weekly data string, so they
// are merged into ONE study: the "Display Ticker" input selects the family
// (NQ/NDX/QQQ vs ES/SPX/SPY) and the data is pasted per chart.
//
// Usage:
//  - Paste the GEX data string (S:spread|L:levels|P:profile) into
//    Study Settings >> the multiline "GEX Data" text input at the bottom.
//    No data is baked into the DLL; each chart keeps its own pasted string.
//  - Set "Display Ticker" to the instrument of THIS chart. Strikes arrive in
//    futures units and are rescaled for cash/ETF charts:
//      NDX = NQ - spread     QQQ = (NQ - spread) / 40
//      SPX = ES - spread     SPY = (ES - spread) / 10
//    The spread is read from the S: prefix of the data string. If the S:
//    prefix is missing the conversion falls back to identity (levels shown
//    in futures units unchanged).
//  - Sessions are defined in New York time (Asia 18:00-03:00, EU 03:00-08:00,
//    Pre 08:00-09:30, US RTH 09:30-16:00/17:00). If the chart time zone is
//    not New York, set "Hours To Add To Chart Time For New York Time"
//    (e.g. chart in US Central -> 1, UTC in summer -> -4).
//  - The chart should include the full 24h session; on an RTH-only chart the
//    overnight AVWAP anchors never fire (same limitation as the TV original).
//  - Wall-flip granularity is max(chart bar period, 5 minutes): on charts
//    above 5 minutes each closed bar counts as one "5m close".
//  - TV label hover tooltips (hold/break %, GEX detail) have no Sierra Chart
//    equivalent and are not rendered. Flipped walls are marked with a
//    " [FLIP]" label suffix and a thinner line, like the "(recycle)" suffix
//    in the TV version.
//  - Multi-timeframe behavior: BOS periods are derived from New York time on
//    the chart's own bars (futures day rolls at 18:00 ET; H4 buckets anchor
//    to 18:00 ET). BOS levels are confirmed on the close of the breakout
//    period, which matches how the TV script renders historical bars.
// ============================================================================

// ---------------------------------------------------------------------------
// Line number allocation (unique within this study instance)
// ---------------------------------------------------------------------------
static const int PK_STATE = 1; // persistent pointer key for StudyState

static const int LN_LEVEL_BASE   = 10000; // + 2*i line, + 2*i + 1 label
static const int LN_BOS_BASE     = 30000; // + 2*counter line, +1 label
static const int LN_AVWAP_EXT    = 41000; // .. 41003
static const int LN_AVWAP_LBL    = 42000; // .. 42003
static const int LN_SESSBOX_BASE = 43000; // + running counter
static const int LN_PROFILE_BASE = 50000; // + i
static const int LN_CONF_BASE    = 60000; // + 2*k box, + 2*k + 1 label
static const int LN_WATERMARK    = 61000;

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------
struct GEXLevel
{
	double strike = 0;      // futures (base) units
	std::string type;       // CW PW ZG MP EH EL VH VL PDH PDL PWH PWL ...
	std::string label;
	double mag = 0;         // |Tot GEX| in millions (0 for system levels)
};

struct ProfileEntry
{
	double strike = 0;
	double forza = 0;       // signed: calls positive, puts negative
	double sign = 0;        // >= 0 call, < 0 put
};

struct WallTrack
{
	double strike = 0;      // futures units
	bool isCall = false;
	int counter = 0;        // consecutive out-side 5m closes
	bool flipped = false;
};

struct BOSLevel
{
	double price = 0;       // chart units
	bool bullish = false;
	int tf = 0;             // 0=M 1=W 2=D 3=H4 4=H1
	int lineNo = 0;         // line drawing number; label is lineNo + 1
};

struct TFTrack
{
	bool hasPrev = false;
	bool hasCur = false;
	double prevH = 0, prevL = 0;
	double curH = 0, curL = 0, curC = 0;
	long long curId = -1;
};

struct SessTrack
{
	bool active = false;
	double hi = 0, lo = 0;
	int startIndex = 0;
	int lineNo = 0;
};

// Session AVWAP accumulators. Mirrors the Pine `var` state exactly; advanced
// once per CLOSED bar, and copied for a transient advance on the developing
// bar (Pine gets the same effect from its rollback of `var`s on every tick).
struct AvwapCore
{
	double aPV = 0, aV = 0;     // Asia
	double ePV = 0, eV = 0;     // EU
	double uPV = 0, uV = 0;     // US
	double pdPV = 0, pdV = 0;   // US previous day
	bool asiaActive = false, euActive = false, usActive = false;
	bool inCurrentDay = false;
	bool prevInAsia = false, prevInEU = false, prevInUS = false;
	bool havePrev = false;
};

struct StudyState
{
	// parsed data (cache keyed by dataHash)
	unsigned int dataHash = 0;
	double spread = 0;
	std::vector<GEXLevel> levels;
	std::vector<ProfileEntry> profile;

	// wall flip tracking
	std::vector<WallTrack> walls;
	long long last5mBucket = -1;

	// AVWAP
	AvwapCore avwap;

	// BOS
	TFTrack tf[5];
	std::vector<BOSLevel> bos;
	int bosCounter = 0;

	// session boxes: 0 Asia, 1 EU, 2 Pre, 3 US
	SessTrack sess[4];
	int sessBoxCounter = 0;

	// per-closed-bar commit bookkeeping
	int lastCommitted = -1;

	// last-bar drawing group (levels/profile/confluence/watermark)
	std::set<int> prevDrawn;
	int lastDrawnBar = -1;
	bool needLevelRedraw = true;
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static void TrimStr(std::string& s)
{
	const char* ws = " \t\r\n";
	size_t b = s.find_first_not_of(ws);
	if (b == std::string::npos)
	{
		s.clear();
		return;
	}
	size_t e = s.find_last_not_of(ws);
	s = s.substr(b, e - b + 1);
}

static void SplitStr(const std::string& s, char delim, std::vector<std::string>& out)
{
	out.clear();
	size_t pos = 0;
	while (pos <= s.size())
	{
		size_t next = s.find(delim, pos);
		if (next == std::string::npos)
			next = s.size();
		std::string item = s.substr(pos, next - pos);
		TrimStr(item);
		if (!item.empty())
			out.push_back(item);
		pos = next + 1;
	}
}

static unsigned int Fnv1a(const char* s, int len)
{
	unsigned int h = 2166136261u;
	for (int i = 0; i < len; ++i)
	{
		h ^= (unsigned char)s[i];
		h *= 16777619u;
	}
	return h;
}

// Theme accent colors (Pine colorPos / colorNeg per scheme).
// 0 = Wall Street Classic, 1 = Boreal, 2 = Lady Trader.
static COLORREF ThemePos(int theme)
{
	if (theme == 1) return RGB(34, 211, 238);
	if (theme == 2) return RGB(45, 212, 191);
	return RGB(34, 197, 94);
}

static COLORREF ThemeNeg(int theme)
{
	if (theme == 1) return RGB(244, 114, 182);
	if (theme == 2) return RGB(192, 132, 252);
	return RGB(239, 68, 68);
}

// ---------------------------------------------------------------------------
// New York session context for one bar
// ---------------------------------------------------------------------------
struct BarCtx
{
	long long etTotalMin = 0; // ET minutes since the SCDateTime epoch
	int etMins = 0;           // ET minutes into the day
	bool inAsia = false, inEU = false, inPre = false, inUS = false, inGap = false;
};

static BarCtx GetBarCtx(const SCDateTime& dt, int etOffsetHours, int usEndMins)
{
	BarCtx c;
	long long t = (long long)dt.GetDate() * 1440
		+ dt.GetTimeInSeconds() / 60
		+ (long long)etOffsetHours * 60;
	c.etTotalMin = t;
	c.etMins = (int)(((t % 1440) + 1440) % 1440);
	c.inAsia = c.etMins >= 1080 || c.etMins < 180;       // 18:00 - 03:00
	c.inEU   = c.etMins >= 180 && c.etMins < 480;        // 03:00 - 08:00
	c.inPre  = c.etMins >= 480 && c.etMins < 570;        // 08:00 - 09:30
	c.inUS   = c.etMins >= 570 && c.etMins < usEndMins;  // 09:30 - US end
	c.inGap  = c.etMins >= usEndMins && c.etMins < 1080; // US end - 18:00
	return c;
}

// Futures day rolls at 18:00 ET: shifting forward 6 hours moves the evening
// session onto the next calendar date (Sunday 18:00 ET -> Monday).
static long long FuturesDayId(const BarCtx& c) { return (c.etTotalMin + 360) / 1440; }

static long long MonthIdOf(const BarCtx& c)
{
	SCDateTime d((int)FuturesDayId(c), 0);
	int y = 0, m = 0, dd = 0;
	d.GetDateYMD(y, m, dd);
	return (long long)y * 12 + (m - 1);
}

// SCDateTime day serial 2 = Monday 1900-01-01, so this groups Mon..Sun weeks.
static long long WeekIdOf(const BarCtx& c) { return (FuturesDayId(c) - 2) / 7; }

// 4H buckets anchored to the 18:00 ET futures day open.
static long long H4IdOf(const BarCtx& c)
{
	long long shifted = c.etTotalMin + 360;
	return (shifted / 1440) * 6 + (shifted % 1440) / 240;
}

static long long H1IdOf(const BarCtx& c) { return c.etTotalMin / 60; }

// ---------------------------------------------------------------------------
// Session AVWAP advance for one bar. Direct port of the Pine block, including
// the ordering subtleties:
//  - the US sums are reset ONLY at US start, so at Asia start they still hold
//    yesterday's totals and can be promoted to the previous-day AVWAP;
//  - Asia never deactivates (it accumulates until the 16:00/17:00 gap via
//    inCurrentDay), EU/US deactivate at Asia start.
// ---------------------------------------------------------------------------
static void AdvanceAvwap(AvwapCore& a, const BarCtx& c, double src, double vol)
{
	const bool asiaStart = c.inAsia && a.havePrev && !a.prevInAsia;
	const bool euStart   = c.inEU && a.havePrev && !a.prevInEU;
	const bool usStart   = c.inUS && a.havePrev && !a.prevInUS;

	if (asiaStart)
		a.inCurrentDay = true;
	if (c.inGap)
		a.inCurrentDay = false;

	// Asia
	if (asiaStart)
	{
		a.aPV = src * vol;
		a.aV = vol;
		a.asiaActive = true;
	}
	else if (a.asiaActive && a.inCurrentDay)
	{
		a.aPV += src * vol;
		a.aV += vol;
	}

	// EU
	if (asiaStart)
		a.euActive = false;
	if (euStart)
	{
		a.ePV = src * vol;
		a.eV = vol;
		a.euActive = true;
	}
	else if (a.euActive && a.inCurrentDay)
	{
		a.ePV += src * vol;
		a.eV += vol;
	}

	// Previous-day promotion BEFORE any US reset (asiaStart and usStart can
	// never coincide, so the order relative to the US block below is safe).
	if (asiaStart)
		a.usActive = false;
	if (asiaStart && a.uV > 0)
	{
		a.pdPV = a.uPV;
		a.pdV = a.uV;
	}
	else if (a.pdV > 0 && a.inCurrentDay)
	{
		a.pdPV += src * vol;
		a.pdV += vol;
	}

	// US
	if (usStart)
	{
		a.uPV = src * vol;
		a.uV = vol;
		a.usActive = true;
	}
	else if (a.usActive && c.inUS)
	{
		a.uPV += src * vol;
		a.uV += vol;
	}

	a.prevInAsia = c.inAsia;
	a.prevInEU = c.inEU;
	a.prevInUS = c.inUS;
	a.havePrev = true;
}

// ---------------------------------------------------------------------------
// GEX data parsing: "S:spread|L:strike,type,label,tooltip,mag;...|P:strike,forza,sign;..."
// Faithful to the Pine parser, including its quirk: with no "|P:" separator
// the whole remainder is treated as PROFILE data (legacy format), and the
// levels section is only recognized behind an "L:" prefix.
// ---------------------------------------------------------------------------
static void ParseGEXData(const std::string& input, double& outSpread,
	std::vector<GEXLevel>& outLevels, std::vector<ProfileEntry>& outProfile)
{
	outSpread = 0.0;
	outLevels.clear();
	outProfile.clear();
	if (input.empty())
		return;

	std::string remaining = input;

	if (remaining.size() >= 2 && remaining.compare(0, 2, "S:") == 0)
	{
		size_t pipe = remaining.find('|');
		if (pipe != std::string::npos && pipe > 2)
		{
			outSpread = atof(remaining.substr(2, pipe - 2).c_str());
			if (outSpread < 0)
				outSpread = 0;
			remaining = remaining.substr(pipe + 1);
		}
	}

	std::string levelsPart;
	std::string profilePart;
	size_t pPos = remaining.find("|P:");
	if (pPos != std::string::npos)
	{
		std::string before = remaining.substr(0, pPos);
		profilePart = remaining.substr(pPos + 3);
		if (before.size() >= 2 && before.compare(0, 2, "L:") == 0)
			levelsPart = before.substr(2);
	}
	else
	{
		profilePart = remaining; // Pine quirk: no |P: -> everything is profile
	}

	std::vector<std::string> pairs;
	std::vector<std::string> fields;

	SplitStr(levelsPart, ';', pairs);
	for (size_t i = 0; i < pairs.size(); ++i)
	{
		SplitStr(pairs[i], ',', fields);
		if (fields.size() < 3)
			continue;
		GEXLevel lvl;
		lvl.strike = atof(fields[0].c_str());
		lvl.type = fields[1];
		lvl.label = fields[2];
		// fields[3] is the tooltip: parsed but not rendered (no SC tooltips)
		lvl.mag = fields.size() >= 5 ? atof(fields[4].c_str()) : 0.0;
		if (lvl.strike != 0.0)
			outLevels.push_back(lvl);
	}

	SplitStr(profilePart, ';', pairs);
	for (size_t i = 0; i < pairs.size(); ++i)
	{
		SplitStr(pairs[i], ',', fields);
		if (fields.size() != 3)
			continue;
		ProfileEntry pe;
		pe.strike = atof(fields[0].c_str());
		pe.forza = atof(fields[1].c_str());
		pe.sign = atof(fields[2].c_str());
		if (pe.strike != 0.0)
			outProfile.push_back(pe);
	}
}

// Wall list parse (Pine f_parseWalls): unlike the display parser above, this
// one reads the level section even when the "|P:" separator is absent.
static void ParseWalls(const std::string& input, std::vector<WallTrack>& outWalls)
{
	outWalls.clear();
	if (input.empty())
		return;

	std::string rest = input;
	if (rest.size() >= 2 && rest.compare(0, 2, "S:") == 0)
	{
		size_t pipe = rest.find('|');
		if (pipe != std::string::npos && pipe > 2)
			rest = rest.substr(pipe + 1);
	}
	size_t pPos = rest.find("|P:");
	std::string lvlSection = (pPos != std::string::npos) ? rest.substr(0, pPos) : rest;
	if (lvlSection.size() >= 2 && lvlSection.compare(0, 2, "L:") == 0)
		lvlSection = lvlSection.substr(2);

	std::vector<std::string> pairs;
	std::vector<std::string> fields;
	SplitStr(lvlSection, ';', pairs);
	for (size_t i = 0; i < pairs.size(); ++i)
	{
		SplitStr(pairs[i], ',', fields);
		if (fields.size() < 2)
			continue;
		double s = atof(fields[0].c_str());
		if (s == 0.0)
			continue;
		if (fields[1] == "CW" || fields[1] == "PW")
		{
			WallTrack w;
			w.strike = s;
			w.isCall = fields[1] == "CW";
			outWalls.push_back(w);
		}
	}
}

// ---------------------------------------------------------------------------
// Price conversion between futures (base) units and chart/display units.
// tickerIdx: 0 NQ, 1 NDX, 2 QQQ, 3 ES, 4 SPX, 5 SPY.
// Identity when the spread is missing (<= 0), matching the ES Pine guard.
// ---------------------------------------------------------------------------
static double ConvertPrice(double basePrice, int tickerIdx, double spread)
{
	if (spread <= 0.0)
		return basePrice;
	switch (tickerIdx)
	{
		case 1: return basePrice - spread;          // NDX
		case 2: return (basePrice - spread) / 40.0; // QQQ
		case 4: return basePrice - spread;          // SPX
		case 5: return (basePrice - spread) / 10.0; // SPY
		default: return basePrice;                  // NQ / ES
	}
}

static double InvertPrice(double chartPrice, int tickerIdx, double spread)
{
	if (spread <= 0.0)
		return chartPrice;
	switch (tickerIdx)
	{
		case 1: return chartPrice + spread;
		case 2: return chartPrice * 40.0 + spread;
		case 4: return chartPrice + spread;
		case 5: return chartPrice * 10.0 + spread;
		default: return chartPrice;
	}
}

static SCString FormatLevelPrice(double basePrice, int tickerIdx, double spread)
{
	double cv = ConvertPrice(basePrice, tickerIdx, spread);
	SCString out;
	if (tickerIdx == 2 || tickerIdx == 5)
		out.Format("%.2f", cv);
	else
		out.Format("%.0f", cv);
	return out;
}

// Period-close breakout check (Pine f_checkBOS): the completed period closed
// beyond the prior period's high/low, and the close-side body exceeds the
// same-side shadow.
static void CheckBOSPeriod(const TFTrack& t, bool& outBull, bool& outBear)
{
	outBull = false;
	outBear = false;
	if (!t.hasPrev)
		return;
	if (t.curC > t.prevH)
	{
		double body = t.curC - t.prevH;
		double shadow = t.curH - t.curC;
		if (body > shadow && body > 0)
			outBull = true;
	}
	if (t.curC < t.prevL)
	{
		double body = t.prevL - t.curC;
		double shadow = t.curC - t.curL;
		if (body > shadow && body > 0)
			outBear = true;
	}
}

// ===========================================================================
// The study
// ===========================================================================
SCSFExport scsf_TLADe_GEX_Levels(SCStudyInterfaceRef sc)
{
	SCInputRef In_Ticker         = sc.Input[0];
	SCInputRef In_ETOffset       = sc.Input[1];
	SCInputRef In_USEnd          = sc.Input[2];

	SCInputRef In_ShowWalls      = sc.Input[3];
	SCInputRef In_ShowSystem     = sc.Input[4];
	SCInputRef In_ShowStructure  = sc.Input[5];
	SCInputRef In_ShowProfile    = sc.Input[6];
	SCInputRef In_MaxLevels      = sc.Input[7];
	SCInputRef In_OnlyNear       = sc.Input[8];
	SCInputRef In_NearPct        = sc.Input[9];
	SCInputRef In_UseThreshold   = sc.Input[10];
	SCInputRef In_Threshold      = sc.Input[11];

	SCInputRef In_ProfileWidth   = sc.Input[12];
	SCInputRef In_ProfileHeight  = sc.Input[13];
	SCInputRef In_ProfileOffset  = sc.Input[14];

	SCInputRef In_BOS_M          = sc.Input[15];
	SCInputRef In_BOS_W          = sc.Input[16];
	SCInputRef In_BOS_D          = sc.Input[17];
	SCInputRef In_BOS_H4         = sc.Input[18];
	SCInputRef In_BOS_H1         = sc.Input[19];
	SCInputRef In_BOSWidth       = sc.Input[20];
	SCInputRef In_BOSBull        = sc.Input[21];
	SCInputRef In_BOSBear        = sc.Input[22];

	SCInputRef In_ShowSessions   = sc.Input[23];
	SCInputRef In_SessAsia       = sc.Input[24];
	SCInputRef In_SessEU         = sc.Input[25];
	SCInputRef In_SessPre        = sc.Input[26];
	SCInputRef In_SessUS         = sc.Input[27];
	SCInputRef In_ColAsia        = sc.Input[28];
	SCInputRef In_ColEU          = sc.Input[29];
	SCInputRef In_ColPre         = sc.Input[30];
	SCInputRef In_ColUS          = sc.Input[31];
	SCInputRef In_HistSessions   = sc.Input[32];

	SCInputRef In_ShowAVWAP      = sc.Input[33];
	SCInputRef In_AvAsia         = sc.Input[34];
	SCInputRef In_AvEU           = sc.Input[35];
	SCInputRef In_AvUS           = sc.Input[36];
	SCInputRef In_AvPD           = sc.Input[37];
	SCInputRef In_HistAVWAP      = sc.Input[38];
	SCInputRef In_AvLabels       = sc.Input[39];
	SCInputRef In_AvExtend       = sc.Input[40];

	SCInputRef In_Theme          = sc.Input[41];
	SCInputRef In_ColZG          = sc.Input[42];
	SCInputRef In_ColMP          = sc.Input[43];
	SCInputRef In_ColEM          = sc.Input[44];
	SCInputRef In_ColVol         = sc.Input[45];
	SCInputRef In_ColStructure   = sc.Input[46];
	SCInputRef In_LeftExt        = sc.Input[47];
	SCInputRef In_LineStyle      = sc.Input[48];
	SCInputRef In_LineWidth      = sc.Input[49];
	SCInputRef In_LabelSide      = sc.Input[50];
	SCInputRef In_BarColors      = sc.Input[51];

	SCInputRef In_ShowConf       = sc.Input[52];
	SCInputRef In_ConfMinSize    = sc.Input[53];
	SCInputRef In_ConfBandPts    = sc.Input[54];
	SCInputRef In_ConfCol3       = sc.Input[55];
	SCInputRef In_ConfCol4       = sc.Input[56];
	SCInputRef In_ConfCol5       = sc.Input[57];

	SCInputRef In_EnableAlerts   = sc.Input[58];
	SCInputRef In_AlertNumber    = sc.Input[59];

	if (sc.SetDefaults)
	{
		sc.GraphName = "TLADe GEX Levels + BOS + Session AVWAP";
		sc.StudyDescription
			= "Port of the TLADe GEX Levels TradingView indicator (NQ and ES merged)."
			  " Paste the GEX data string (S:..|L:..|P:..) into the GEX Data text"
			  " input and set Display Ticker to this chart's instrument."
			  " AVWAP colors and widths are set on the Subgraphs tab.";
		sc.GraphRegion = 0;
		sc.AutoLoop = 1;

		sc.TextInputName = "GEX Data (S:spread|L:levels|P:profile)";

		sc.Subgraph[0].Name = "AVWAP Asia";
		sc.Subgraph[0].DrawStyle = DRAWSTYLE_LINE;
		sc.Subgraph[0].LineWidth = 2;
		sc.Subgraph[0].PrimaryColor = RGB(245, 158, 11);
		sc.Subgraph[0].DrawZeros = 0;

		sc.Subgraph[1].Name = "AVWAP EU";
		sc.Subgraph[1].DrawStyle = DRAWSTYLE_LINE;
		sc.Subgraph[1].LineWidth = 2;
		sc.Subgraph[1].PrimaryColor = RGB(59, 130, 246);
		sc.Subgraph[1].DrawZeros = 0;

		sc.Subgraph[2].Name = "AVWAP US";
		sc.Subgraph[2].DrawStyle = DRAWSTYLE_LINE;
		sc.Subgraph[2].LineWidth = 2;
		sc.Subgraph[2].PrimaryColor = RGB(34, 197, 94);
		sc.Subgraph[2].DrawZeros = 0;

		sc.Subgraph[3].Name = "AVWAP US Prev Day";
		sc.Subgraph[3].DrawStyle = DRAWSTYLE_LINE;
		sc.Subgraph[3].LineWidth = 2;
		sc.Subgraph[3].PrimaryColor = RGB(20, 120, 60);
		sc.Subgraph[3].DrawZeros = 0;

		In_Ticker.Name = "Display Ticker (this chart's instrument)";
		In_Ticker.SetCustomInputStrings("NQ;NDX;QQQ;ES;SPX;SPY");
		In_Ticker.SetCustomInputIndex(0);

		In_ETOffset.Name = "Hours To Add To Chart Time For New York Time";
		In_ETOffset.SetInt(0);
		In_ETOffset.SetIntLimits(-23, 23);

		In_USEnd.Name = "US Session End";
		In_USEnd.SetCustomInputStrings("16:00 (TradingView parity);17:00");
		In_USEnd.SetCustomInputIndex(0);

		In_ShowWalls.Name = "Show Call/Put Walls";
		In_ShowWalls.SetYesNo(1);
		In_ShowSystem.Name = "Show System Levels (ZG/MP/EM/Vol)";
		In_ShowSystem.SetYesNo(1);
		In_ShowStructure.Name = "Show Structure Levels (PDH/PDL/PWH/PWL)";
		In_ShowStructure.SetYesNo(1);
		In_ShowProfile.Name = "Show GEX Profile Bars";
		In_ShowProfile.SetYesNo(1);

		In_MaxLevels.Name = "Max GEX Levels";
		In_MaxLevels.SetCustomInputStrings("5;10;15;20;All");
		In_MaxLevels.SetCustomInputIndex(1);

		In_OnlyNear.Name = "Show Only Levels Near Price";
		In_OnlyNear.SetYesNo(0);
		In_NearPct.Name = "Near Radius (%)";
		In_NearPct.SetFloat(3.0f);
		In_NearPct.SetFloatLimits(0.5f, 10.0f);

		In_UseThreshold.Name = "Enable Min GEX Threshold";
		In_UseThreshold.SetYesNo(0);
		In_Threshold.Name = "Min GEX Magnitude (M)";
		In_Threshold.SetFloat(50.0f);

		In_ProfileWidth.Name = "Profile Bar Width (bars)";
		In_ProfileWidth.SetInt(50);
		In_ProfileHeight.Name = "Profile Bar Height (ticks)";
		In_ProfileHeight.SetInt(8);
		In_ProfileOffset.Name = "Profile Offset (bars right of last)";
		In_ProfileOffset.SetInt(15);

		In_BOS_M.Name = "BOS Monthly";
		In_BOS_M.SetYesNo(1);
		In_BOS_W.Name = "BOS Weekly";
		In_BOS_W.SetYesNo(1);
		In_BOS_D.Name = "BOS Daily";
		In_BOS_D.SetYesNo(1);
		In_BOS_H4.Name = "BOS 4H";
		In_BOS_H4.SetYesNo(1);
		In_BOS_H1.Name = "BOS 1H";
		In_BOS_H1.SetYesNo(0);
		In_BOSWidth.Name = "BOS Line Width";
		In_BOSWidth.SetInt(2);
		In_BOSWidth.SetIntLimits(1, 4);
		In_BOSBull.Name = "BOS Bull Color";
		In_BOSBull.SetColor(RGB(34, 197, 94));
		In_BOSBear.Name = "BOS Bear Color";
		In_BOSBear.SetColor(RGB(239, 68, 68));

		In_ShowSessions.Name = "Show Session Boxes";
		In_ShowSessions.SetYesNo(1);
		In_SessAsia.Name = "Session Box: Asia";
		In_SessAsia.SetYesNo(1);
		In_SessEU.Name = "Session Box: Europe";
		In_SessEU.SetYesNo(1);
		In_SessPre.Name = "Session Box: Pre-Market";
		In_SessPre.SetYesNo(1);
		In_SessUS.Name = "Session Box: US RTH";
		In_SessUS.SetYesNo(1);
		In_ColAsia.Name = "Asia Box Color";
		In_ColAsia.SetColor(RGB(245, 158, 11));
		In_ColEU.Name = "EU Box Color";
		In_ColEU.SetColor(RGB(59, 130, 246));
		In_ColPre.Name = "Pre-Market Box Color";
		In_ColPre.SetColor(RGB(168, 85, 247));
		In_ColUS.Name = "US Box Color";
		In_ColUS.SetColor(RGB(34, 197, 94));
		In_HistSessions.Name = "Show Historical Session Boxes";
		In_HistSessions.SetYesNo(0);

		In_ShowAVWAP.Name = "Show Session AVWAPs";
		In_ShowAVWAP.SetYesNo(1);
		In_AvAsia.Name = "AVWAP: Asia";
		In_AvAsia.SetYesNo(1);
		In_AvEU.Name = "AVWAP: EU";
		In_AvEU.SetYesNo(1);
		In_AvUS.Name = "AVWAP: US";
		In_AvUS.SetYesNo(1);
		In_AvPD.Name = "AVWAP: US Previous Day";
		In_AvPD.SetYesNo(1);
		In_HistAVWAP.Name = "Show Historical AVWAP";
		In_HistAVWAP.SetYesNo(0);
		In_AvLabels.Name = "AVWAP Labels";
		In_AvLabels.SetYesNo(1);
		In_AvExtend.Name = "Extend AVWAP Lines Right";
		In_AvExtend.SetYesNo(1);

		In_Theme.Name = "Color Theme";
		In_Theme.SetCustomInputStrings("Wall Street Classic;Boreal;Lady Trader");
		In_Theme.SetCustomInputIndex(0);
		In_ColZG.Name = "Zero Gamma Color";
		In_ColZG.SetColor(RGB(156, 163, 175));
		In_ColMP.Name = "Max Pain Color";
		In_ColMP.SetColor(RGB(239, 68, 68));
		In_ColEM.Name = "Expected Move Color";
		In_ColEM.SetColor(RGB(59, 130, 246));
		In_ColVol.Name = "Vol Band Color";
		In_ColVol.SetColor(RGB(156, 163, 175));
		In_ColStructure.Name = "Structure Color";
		In_ColStructure.SetColor(RGB(156, 163, 175));
		In_LeftExt.Name = "Label/Zone Left Extension (bars)";
		In_LeftExt.SetInt(50);
		In_LeftExt.SetIntLimits(1, 5000);
		In_LineStyle.Name = "Default Line Style";
		In_LineStyle.SetCustomInputStrings("Solid;Dashed;Dotted");
		In_LineStyle.SetCustomInputIndex(2);
		In_LineWidth.Name = "Level Line Width";
		In_LineWidth.SetInt(2);
		In_LineWidth.SetIntLimits(1, 4);
		In_LabelSide.Name = "Label Side";
		In_LabelSide.SetCustomInputStrings("Left;Right");
		In_LabelSide.SetCustomInputIndex(0);
		In_BarColors.Name = "Profile Bar Colors";
		In_BarColors.SetCustomInputStrings("Theme Colors;Greyscale");
		In_BarColors.SetCustomInputIndex(0);

		In_ShowConf.Name = "Show Confluence Zones";
		In_ShowConf.SetYesNo(1);
		In_ConfMinSize.Name = "Confluence Min Cluster Size";
		In_ConfMinSize.SetInt(3);
		In_ConfMinSize.SetIntLimits(2, 5);
		In_ConfBandPts.Name = "Confluence Band Width (points)";
		In_ConfBandPts.SetFloat(5.0f);
		In_ConfBandPts.SetFloatLimits(1.0f, 50.0f);
		In_ConfCol3.Name = "Confluence 3-Level Color";
		In_ConfCol3.SetColor(RGB(250, 204, 21));
		In_ConfCol4.Name = "Confluence 4-Level Color";
		In_ConfCol4.SetColor(RGB(251, 146, 60));
		In_ConfCol5.Name = "Confluence 5+ (UBER) Color";
		In_ConfCol5.SetColor(RGB(239, 68, 68));

		In_EnableAlerts.Name = "Enable BOS Cross Alerts";
		In_EnableAlerts.SetYesNo(0);
		In_AlertNumber.Name = "Alert Sound Number";
		In_AlertNumber.SetInt(1);
		In_AlertNumber.SetIntLimits(1, 150);

		return;
	}

	// ---------------- state ----------------
	StudyState* st = (StudyState*)sc.GetPersistentPointer(PK_STATE);
	if (sc.LastCallToFunction)
	{
		delete st;
		sc.SetPersistentPointer(PK_STATE, NULL);
		return;
	}
	if (st == NULL)
	{
		st = new StudyState();
		sc.SetPersistentPointer(PK_STATE, st);
	}

	if (sc.ArraySize <= 0)
		return;

	// ---------------- config snapshot ----------------
	const int tickerIdx = (int)In_Ticker.GetIndex();
	const SCString tickerStr = In_Ticker.GetSelectedCustomString();
	const int etOffset = In_ETOffset.GetInt();
	const int usEndMins = (In_USEnd.GetIndex() == 1) ? 1020 : 960;
	const int theme = (int)In_Theme.GetIndex();
	const COLORREF colPos = ThemePos(theme); // put walls / support
	const COLORREF colNeg = ThemeNeg(theme); // call walls / resistance
	const int lastIndex = sc.ArraySize - 1;

	const bool bosEnabled[5] = {
		In_BOS_M.GetYesNo() != 0, In_BOS_W.GetYesNo() != 0, In_BOS_D.GetYesNo() != 0,
		In_BOS_H4.GetYesNo() != 0, In_BOS_H1.GetYesNo() != 0 };
	static const char* BOS_NAME[5] = { "M", "W", "D", "H4", "H1" };

	// 30-hour "current day only" window, measured from the newest bar.
	SCDateTime recentCutoff = sc.BaseDateTimeIn[lastIndex];
	recentCutoff.AddHours(-30);

	// ---------------- data parse (cached by hash) ----------------
	const SCString& rawData = sc.TextInput;
	unsigned int hash = Fnv1a(rawData.GetChars() ? rawData.GetChars() : "", rawData.GetLength());
	if (hash != st->dataHash || st->dataHash == 0)
	{
		st->dataHash = hash;
		std::string dataStr = rawData.GetChars() ? std::string(rawData.GetChars()) : std::string();
		ParseGEXData(dataStr, st->spread, st->levels, st->profile);
		ParseWalls(dataStr, st->walls); // resets counters/flip states, like Pine
		st->needLevelRedraw = true;
		if (sc.Index == 0)
		{
			SCString msg;
			msg.Format("TLADe GEX: parsed %d levels, %d profile bars, spread %.2f",
				(int)st->levels.size(), (int)st->profile.size(), st->spread);
			sc.AddMessageToLog(msg, 0);
		}
	}

	// ---------------- full recalc / rewind reset ----------------
	if (sc.Index == 0 || sc.Index <= st->lastCommitted)
	{
		// Index 0 = full recalculation (SC has deleted this study's drawings).
		// Index <= lastCommitted = data rewind without full recalc; incremental
		// state cannot be rolled back, so it is rebuilt from bar 0 by the
		// commit loop below (lastCommitted resets to -1).
		st->avwap = AvwapCore();
		for (int i = 0; i < 5; ++i)
			st->tf[i] = TFTrack();
		st->bos.clear();
		st->bosCounter = 0;
		for (int i = 0; i < 4; ++i)
			st->sess[i] = SessTrack();
		st->sessBoxCounter = 0;
		st->last5mBucket = -1;
		for (size_t i = 0; i < st->walls.size(); ++i)
		{
			st->walls[i].counter = 0;
			st->walls[i].flipped = false;
		}
		st->lastCommitted = -1;
		st->lastDrawnBar = -1;
		st->needLevelRedraw = true;
		if (sc.Index == 0)
			st->prevDrawn.clear();
	}

	// ---------------- helpers bound to this call ----------------
	auto BarSrc = [&](int i) -> double
	{
		return (sc.BaseDataIn[SC_HIGH][i] + sc.BaseDataIn[SC_LOW][i]
			+ sc.BaseDataIn[SC_LAST][i]) / 3.0;
	};
	auto BarVol = [&](int i) -> double
	{
		double v = sc.BaseDataIn[SC_VOLUME][i];
		return v > 0 ? v : 1.0; // zero/no-volume bars get weight 1 (Pine nz fallback)
	};
	auto Ctx = [&](int i) -> BarCtx
	{
		return GetBarCtx(sc.BaseDateTimeIn[i], etOffset, usEndMins);
	};
	auto BarIsRecent = [&](int i) -> bool
	{
		return sc.BaseDateTimeIn[i] >= recentCutoff;
	};
	// X coordinate for drawings, allowing positions right of the last bar.
	auto XTime = [&](int barsFromLast) -> SCDateTime
	{
		if (barsFromLast <= 0)
		{
			int idx = lastIndex + barsFromLast;
			if (idx < 0)
				idx = 0;
			return sc.BaseDateTimeIn[idx];
		}
		SCDateTime t = sc.BaseDateTimeIn[lastIndex];
		int spb = sc.SecondsPerBar > 0 ? sc.SecondsPerBar : 60;
		t.AddSeconds(spb * barsFromLast);
		return t;
	};
	auto DeleteDrawing = [&](int lineNo)
	{
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, lineNo);
	};
	auto WallFlipped = [&](double strike) -> bool
	{
		for (size_t i = 0; i < st->walls.size(); ++i)
			if (st->walls[i].strike == strike)
				return st->walls[i].flipped;
		return false;
	};

	auto DrawSessionBox = [&](const SessTrack& t, int endIndex, COLORREF color, int transparency)
	{
		s_UseTool box;
		box.Clear();
		box.ChartNumber = sc.ChartNumber;
		box.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
		box.AddMethod = UTAM_ADD_OR_ADJUST;
		box.LineNumber = t.lineNo;
		box.BeginIndex = t.startIndex;
		box.EndIndex = endIndex;
		box.BeginValue = (float)t.hi;
		box.EndValue = (float)t.lo;
		box.Color = color;
		box.SecondaryColor = color;
		box.TransparencyLevel = transparency;
		box.LineWidth = 1;
		sc.UseTool(box);
	};

	// ---------------- BOS period advancement ----------------
	// A breakout is confirmed the moment the FIRST bar of a new period exists
	// (the completed period's data is final then), which matches the Pine
	// timing. Safe to call for the developing bar and again at its commit:
	// the period-id compare makes the rollover fire exactly once, and the
	// high/low merge is idempotent.
	auto AdvanceBOS = [&](int i)
	{
		const BarCtx c = Ctx(i);
		const double high = sc.BaseDataIn[SC_HIGH][i];
		const double low = sc.BaseDataIn[SC_LOW][i];
		const double close = sc.BaseDataIn[SC_LAST][i];
		const long long periodIds[5]
			= { MonthIdOf(c), WeekIdOf(c), FuturesDayId(c), H4IdOf(c), H1IdOf(c) };
		for (int tf = 0; tf < 5; ++tf)
		{
			TFTrack& t = st->tf[tf];
			if (t.curId != periodIds[tf])
			{
				if (t.hasCur)
				{
					bool bull = false, bear = false;
					CheckBOSPeriod(t, bull, bear);
					if (bosEnabled[tf])
					{
						struct { bool fire; bool isBull; } events[2]
							= { { bull, true }, { bear, false } };
						for (int e = 0; e < 2; ++e)
						{
							if (!events[e].fire)
								continue;
							BOSLevel b;
							b.price = events[e].isBull ? t.prevH : t.prevL;
							b.bullish = events[e].isBull;
							b.tf = tf;
							b.lineNo = LN_BOS_BASE + 2 * st->bosCounter++;
							st->bos.push_back(b);
							st->needLevelRedraw = true; // confluence includes BOS

							COLORREF clr = b.bullish ? In_BOSBull.GetColor() : In_BOSBear.GetColor();
							int w = In_BOSWidth.GetInt();
							if (tf == 0) w += 2;
							else if (tf == 1) w += 1;
							else if (tf >= 3) w = w - 1 > 1 ? w - 1 : 1;

							s_UseTool line;
							line.Clear();
							line.ChartNumber = sc.ChartNumber;
							line.DrawingType = DRAWING_HORIZONTAL_RAY;
							line.AddMethod = UTAM_ADD_OR_ADJUST;
							line.LineNumber = b.lineNo;
							line.BeginIndex = i;
							line.BeginValue = (float)b.price;
							line.Color = clr;
							line.LineWidth = w;
							line.LineStyle = tf <= 2 ? LINESTYLE_SOLID : LINESTYLE_DASH;
							sc.UseTool(line);

							s_UseTool lbl;
							lbl.Clear();
							lbl.ChartNumber = sc.ChartNumber;
							lbl.DrawingType = DRAWING_TEXT;
							lbl.AddMethod = UTAM_ADD_OR_ADJUST;
							lbl.LineNumber = b.lineNo + 1;
							lbl.BeginIndex = i;
							lbl.BeginValue = (float)b.price;
							lbl.Text.Format("%s%s", BOS_NAME[tf], b.bullish ? "+" : "-");
							lbl.Color = RGB(255, 255, 255);
							lbl.FontBackColor = clr;
							lbl.TransparentLabelBackground = 0;
							lbl.FontSize = 8;
							sc.UseTool(lbl);
						}
					}
					t.prevH = t.curH;
					t.prevL = t.curL;
					t.hasPrev = true;
				}
				t.curId = periodIds[tf];
				t.curH = high;
				t.curL = low;
				t.curC = close;
				t.hasCur = true;
			}
			else
			{
				if (high > t.curH) t.curH = high;
				if (low < t.curL) t.curL = low;
				t.curC = close;
			}
		}
	};

	// ---------------- per-closed-bar commit ----------------
	// All stateful features advance exactly once per closed bar. The developing
	// bar is never committed; its AVWAP display value comes from a transient
	// copy below. This reproduces Pine's per-bar-close state semantics in
	// Sierra's repeated-call realtime model.
	auto CommitBar = [&](int i)
	{
		const BarCtx c = Ctx(i);
		const double high = sc.BaseDataIn[SC_HIGH][i];
		const double low = sc.BaseDataIn[SC_LOW][i];
		const double close = sc.BaseDataIn[SC_LAST][i];
		const double open = sc.BaseDataIn[SC_OPEN][i];

		// --- AVWAP ---
		AdvanceAvwap(st->avwap, c, BarSrc(i), BarVol(i));

		// --- Wall flips on completed 5-minute buckets ---
		long long bucket = c.etTotalMin / 5;
		if (st->last5mBucket >= 0 && bucket != st->last5mBucket
			&& !st->walls.empty() && i > 0)
		{
			// the bar before this one carried the close of the finished bucket
			double close5Base = InvertPrice(sc.BaseDataIn[SC_LAST][i - 1], tickerIdx, st->spread);
			for (size_t w = 0; w < st->walls.size(); ++w)
			{
				WallTrack& wall = st->walls[w];
				bool beyond;
				if (wall.isCall)
					beyond = wall.flipped ? close5Base < wall.strike : close5Base > wall.strike;
				else
					beyond = wall.flipped ? close5Base > wall.strike : close5Base < wall.strike;
				if (beyond)
				{
					if (++wall.counter >= 2)
					{
						wall.flipped = !wall.flipped;
						wall.counter = 0;
						st->needLevelRedraw = true;
					}
				}
				else
					wall.counter = 0;
			}
		}
		st->last5mBucket = bucket;

		// --- Breakout structure ---
		AdvanceBOS(i);

		// --- BOS invalidation: a close back through the level removes it ---
		for (int b = (int)st->bos.size() - 1; b >= 0; --b)
		{
			const BOSLevel& lvl = st->bos[b];
			bool broken = lvl.bullish ? close < lvl.price : close > lvl.price;
			if (broken)
			{
				DeleteDrawing(lvl.lineNo);
				DeleteDrawing(lvl.lineNo + 1);
				st->bos.erase(st->bos.begin() + b);
				st->needLevelRedraw = true;
			}
		}

		// --- BOS cross alerts (surviving levels only, like the Pine order) ---
		if (In_EnableAlerts.GetYesNo() && !st->bos.empty()
			&& !sc.IsFullRecalculation && !sc.DownloadingHistoricalData
			&& i >= sc.ArraySize - 2)
		{
			for (size_t b = 0; b < st->bos.size(); ++b)
			{
				double price = st->bos[b].price;
				bool touched = high >= price && low <= price;
				bool crossUp = touched && close >= price && (open < price || low < price);
				bool crossDown = touched && close <= price && (open > price || high > price);
				if (crossUp || crossDown)
				{
					SCString msg;
					msg.Format("TLADe BOS %s %s %.2f", BOS_NAME[st->bos[b].tf],
						crossUp ? "UP" : "DOWN", price);
					sc.SetAlert(In_AlertNumber.GetInt(), msg);
				}
			}
		}

		// --- Session boxes ---
		if (In_ShowSessions.GetYesNo())
		{
			const bool inSess[4] = { c.inAsia, c.inEU, c.inPre, c.inUS };
			const bool showSess[4] = {
				In_SessAsia.GetYesNo() != 0, In_SessEU.GetYesNo() != 0,
				In_SessPre.GetYesNo() != 0, In_SessUS.GetYesNo() != 0 };
			const COLORREF sessColor[4] = {
				In_ColAsia.GetColor(), In_ColEU.GetColor(),
				In_ColPre.GetColor(), In_ColUS.GetColor() };
			static const int SESS_TRANSPARENCY[4] = { 90, 90, 92, 92 };
			const bool recent = In_HistSessions.GetYesNo() || BarIsRecent(i);

			for (int s = 0; s < 4; ++s)
			{
				SessTrack& t = st->sess[s];
				if (!showSess[s] || !recent)
					continue;
				if (inSess[s])
				{
					if (!t.active)
					{
						t.active = true;
						t.hi = high;
						t.lo = low;
						t.startIndex = i;
						t.lineNo = LN_SESSBOX_BASE + st->sessBoxCounter++;
					}
					else
					{
						if (high > t.hi) t.hi = high;
						if (low < t.lo) t.lo = low;
					}
					DrawSessionBox(t, i, sessColor[s], SESS_TRANSPARENCY[s]);
				}
				else
					t.active = false;
			}
		}
	};

	for (int i = st->lastCommitted + 1; i < sc.Index; ++i)
	{
		CommitBar(i);
		st->lastCommitted = i;
	}

	// ---------------- transient values for the developing bar ----------------
	const BarCtx curCtx = Ctx(sc.Index);
	AvwapCore tv = st->avwap;
	AdvanceAvwap(tv, curCtx, BarSrc(sc.Index), BarVol(sc.Index));

	const bool showAV = In_ShowAVWAP.GetYesNo() != 0;
	const bool avRecent = In_HistAVWAP.GetYesNo() || BarIsRecent(sc.Index);
	float pAsia = (showAV && In_AvAsia.GetYesNo() && tv.asiaActive && tv.inCurrentDay
		&& avRecent && tv.aV > 0) ? (float)(tv.aPV / tv.aV) : 0.0f;
	float pEU = (showAV && In_AvEU.GetYesNo() && tv.euActive && tv.inCurrentDay
		&& avRecent && tv.eV > 0) ? (float)(tv.ePV / tv.eV) : 0.0f;
	float pUS = (showAV && In_AvUS.GetYesNo() && tv.usActive && curCtx.inUS
		&& avRecent && tv.uV > 0) ? (float)(tv.uPV / tv.uV) : 0.0f;
	float pPD = (showAV && In_AvPD.GetYesNo() && tv.pdV > 0 && tv.inCurrentDay
		&& avRecent) ? (float)(tv.pdPV / tv.pdV) : 0.0f;

	sc.Subgraph[0][sc.Index] = pAsia;
	sc.Subgraph[1][sc.Index] = pEU;
	sc.Subgraph[2][sc.Index] = pUS;
	sc.Subgraph[3][sc.Index] = pPD;

	// Everything below only runs on the newest bar (Pine barstate.islast).
	if (sc.Index != lastIndex)
		return;

	// Confirm pending period rollovers on the developing bar so a BOS appears
	// as soon as the new period opens (Pine timing), not one bar later.
	AdvanceBOS(sc.Index);

	// ---------------- session box for the developing bar ----------------
	if (In_ShowSessions.GetYesNo())
	{
		const bool inSess[4] = { curCtx.inAsia, curCtx.inEU, curCtx.inPre, curCtx.inUS };
		const bool showSess[4] = {
			In_SessAsia.GetYesNo() != 0, In_SessEU.GetYesNo() != 0,
			In_SessPre.GetYesNo() != 0, In_SessUS.GetYesNo() != 0 };
		const COLORREF sessColor[4] = {
			In_ColAsia.GetColor(), In_ColEU.GetColor(),
			In_ColPre.GetColor(), In_ColUS.GetColor() };
		static const int SESS_TRANSPARENCY[4] = { 90, 90, 92, 92 };
		const bool recent = In_HistSessions.GetYesNo() || BarIsRecent(sc.Index);
		const double high = sc.BaseDataIn[SC_HIGH][sc.Index];
		const double low = sc.BaseDataIn[SC_LOW][sc.Index];

		for (int s = 0; s < 4; ++s)
		{
			if (!showSess[s] || !recent || !inSess[s])
				continue;
			SessTrack t = st->sess[s]; // copy: never commit the developing bar
			if (!t.active)
			{
				t.hi = high;
				t.lo = low;
				t.startIndex = sc.Index;
				t.lineNo = LN_SESSBOX_BASE + st->sessBoxCounter; // peek, no increment
			}
			else
			{
				if (high > t.hi) t.hi = high;
				if (low < t.lo) t.lo = low;
			}
			DrawSessionBox(t, sc.Index, sessColor[s], SESS_TRANSPARENCY[s]);
		}
	}

	// ---------------- AVWAP extension rays + labels ----------------
	// Rays and labels persist at their last drawn position when a value goes
	// inactive; the TV original behaves the same way (lines are moved, never
	// deleted).
	if (showAV && In_AvExtend.GetYesNo())
	{
		const float vals[4] = { pAsia, pEU, pUS, pPD };
		const bool on[4] = {
			In_AvAsia.GetYesNo() != 0, In_AvEU.GetYesNo() != 0,
			In_AvUS.GetYesNo() != 0, In_AvPD.GetYesNo() != 0 };
		for (int k = 0; k < 4; ++k)
		{
			if (!on[k] || vals[k] <= 0)
				continue;
			s_UseTool ray;
			ray.Clear();
			ray.ChartNumber = sc.ChartNumber;
			ray.DrawingType = DRAWING_HORIZONTAL_RAY;
			ray.AddMethod = UTAM_ADD_OR_ADJUST;
			ray.LineNumber = LN_AVWAP_EXT + k;
			ray.BeginIndex = sc.Index;
			ray.BeginValue = vals[k];
			ray.Color = sc.Subgraph[k].PrimaryColor;
			ray.LineWidth = sc.Subgraph[k].LineWidth;
			ray.LineStyle = k == 3 ? LINESTYLE_DASH : LINESTYLE_DOT;
			sc.UseTool(ray);
		}
	}

	if (showAV && In_AvLabels.GetYesNo())
	{
		const float vals[4] = { pAsia, pEU, pUS, pPD };
		const bool on[4] = {
			In_AvAsia.GetYesNo() != 0, In_AvEU.GetYesNo() != 0,
			In_AvUS.GetYesNo() != 0, In_AvPD.GetYesNo() != 0 };
		static const char* AV_NAME[4] = { "ASIA", "EU", "US", "US PD" };
		for (int k = 0; k < 4; ++k)
		{
			if (!on[k] || vals[k] <= 0)
				continue;
			s_UseTool lbl;
			lbl.Clear();
			lbl.ChartNumber = sc.ChartNumber;
			lbl.DrawingType = DRAWING_TEXT;
			lbl.AddMethod = UTAM_ADD_OR_ADJUST;
			lbl.LineNumber = LN_AVWAP_LBL + k;
			lbl.BeginDateTime = XTime(3);
			lbl.BeginValue = vals[k];
			lbl.Text.Format("%s %.2f", AV_NAME[k], vals[k]);
			lbl.Color = RGB(255, 255, 255);
			lbl.FontBackColor = sc.Subgraph[k].PrimaryColor;
			lbl.TransparentLabelBackground = 0;
			lbl.FontSize = 8;
			sc.UseTool(lbl);
		}
	}

	// ---------------- levels / profile / confluence / watermark ----------------
	// Redrawn when a new bar arrives, the data string changes, a wall flips,
	// or the BOS set changes (Pine: stateKey / bar_index redraw gate).
	if (st->lastDrawnBar == sc.Index && !st->needLevelRedraw)
		return;
	st->lastDrawnBar = sc.Index;
	st->needLevelRedraw = false;

	std::set<int> drawn;
	const double chartClose = sc.BaseDataIn[SC_LAST][sc.Index];

	auto UseAndTrack = [&](s_UseTool& tool)
	{
		sc.UseTool(tool);
		drawn.insert(tool.LineNumber);
	};

	if (!st->levels.empty() && (In_ShowWalls.GetYesNo() || In_ShowSystem.GetYesNo()
		|| In_ShowStructure.GetYesNo()))
	{
		// closest wall above/below spot is always shown ("protected")
		double closestAbove = 0, closestBelow = 0;
		double closestAboveDist = 1e18, closestBelowDist = 1e18;
		for (size_t i = 0; i < st->levels.size(); ++i)
		{
			const GEXLevel& lvl = st->levels[i];
			if (lvl.type != "CW" && lvl.type != "PW")
				continue;
			double cv = ConvertPrice(lvl.strike, tickerIdx, st->spread);
			double dist = fabs(cv - chartClose);
			if (cv > chartClose && dist < closestAboveDist)
			{
				closestAboveDist = dist;
				closestAbove = cv;
			}
			else if (cv <= chartClose && dist < closestBelowDist)
			{
				closestBelowDist = dist;
				closestBelow = cv;
			}
		}

		SCString maxSel = In_MaxLevels.GetSelectedCustomString();
		int maxVisible = maxSel.CompareNoCase("All") == 0 ? 999 : atoi(maxSel.GetChars());
		if (maxVisible <= 0)
			maxVisible = 10;
		const int halfMax = (maxVisible + 1) / 2;

		const bool onlyNear = In_OnlyNear.GetYesNo() != 0;
		const double nearPct = In_NearPct.GetFloat();
		const bool useThresh = In_UseThreshold.GetYesNo() != 0;
		const double thresh = In_Threshold.GetFloat();
		const bool labelRight = In_LabelSide.GetIndex() == 1;
		const int lineWidth = In_LineWidth.GetInt();
		const SubgraphLineStyles defStyle = In_LineStyle.GetIndex() == 0 ? LINESTYLE_SOLID
			: In_LineStyle.GetIndex() == 1 ? LINESTYLE_DASH : LINESTYLE_DOT;

		int aboveDrawn = 0;
		int belowDrawn = 0;

		for (size_t i = 0; i < st->levels.size(); ++i)
		{
			const GEXLevel& lvl = st->levels[i];
			const double cv = ConvertPrice(lvl.strike, tickerIdx, st->spread);

			if (onlyNear && fabs(chartClose - cv) / chartClose * 100.0 > nearPct)
				continue;

			const bool isGex = lvl.type == "CW" || lvl.type == "PW";
			const bool isSys = lvl.type == "ZG" || lvl.type == "MP" || lvl.type == "EH"
				|| lvl.type == "EL" || lvl.type == "VH" || lvl.type == "VL";
			const bool isStr = lvl.type == "PDH" || lvl.type == "PDL"
				|| lvl.type == "PWH" || lvl.type == "PWL";

			if (isGex && useThresh && !(lvl.mag == 0.0 || lvl.mag >= thresh))
				continue;

			const bool isAbove = cv > chartClose;
			const bool isProtected = fabs(cv - closestAbove) < 1e-9
				|| fabs(cv - closestBelow) < 1e-9;

			bool shouldShow = false;
			if (isGex && In_ShowWalls.GetYesNo())
			{
				if (isProtected)
					shouldShow = true;
				else if (isAbove ? aboveDrawn < halfMax : belowDrawn < halfMax)
				{
					shouldShow = true;
					if (isAbove)
						++aboveDrawn;
					else
						++belowDrawn;
				}
			}
			else if (isSys && In_ShowSystem.GetYesNo())
				shouldShow = true;
			else if (isStr && In_ShowStructure.GetYesNo())
				shouldShow = true;

			if (!shouldShow)
				continue;

			COLORREF lvlColor = RGB(128, 128, 128);
			int styleClass = 0; // 0 default, 1 dashed, 2 dotted
			bool flipped = false;
			if (lvl.type == "CW")
			{
				lvlColor = colNeg;
				flipped = WallFlipped(lvl.strike);
			}
			else if (lvl.type == "PW")
			{
				lvlColor = colPos;
				flipped = WallFlipped(lvl.strike);
			}
			else if (lvl.type == "ZG")
				lvlColor = In_ColZG.GetColor();
			else if (lvl.type == "MP")
			{
				lvlColor = In_ColMP.GetColor();
				styleClass = 2;
			}
			else if (lvl.type == "EH" || lvl.type == "EL")
			{
				lvlColor = In_ColEM.GetColor();
				styleClass = 1;
			}
			else if (lvl.type == "VH" || lvl.type == "VL")
			{
				lvlColor = In_ColVol.GetColor();
				styleClass = 2;
			}
			else if (isStr)
			{
				lvlColor = In_ColStructure.GetColor();
				styleClass = 2;
			}

			// full-width horizontal line (the TV lines use extend.both)
			s_UseTool line;
			line.Clear();
			line.ChartNumber = sc.ChartNumber;
			line.DrawingType = DRAWING_HORIZONTALLINE;
			line.AddMethod = UTAM_ADD_OR_ADJUST;
			line.LineNumber = LN_LEVEL_BASE + 2 * (int)i;
			line.BeginIndex = sc.Index;
			line.BeginValue = (float)cv;
			line.Color = lvlColor;
			line.LineWidth = flipped ? (lineWidth - 1 > 1 ? lineWidth - 1 : 1) : lineWidth;
			line.LineStyle = styleClass == 2 ? LINESTYLE_DOT
				: styleClass == 1 ? LINESTYLE_DASH : defStyle;
			line.DisplayHorizontalLineValue = 0;
			UseAndTrack(line);

			s_UseTool lbl;
			lbl.Clear();
			lbl.ChartNumber = sc.ChartNumber;
			lbl.DrawingType = DRAWING_TEXT;
			lbl.AddMethod = UTAM_ADD_OR_ADJUST;
			lbl.LineNumber = LN_LEVEL_BASE + 2 * (int)i + 1;
			lbl.BeginDateTime = labelRight
				? XTime(In_ProfileOffset.GetInt() + In_ProfileWidth.GetInt() + 5)
				: XTime(-In_LeftExt.GetInt());
			lbl.BeginValue = (float)cv;
			lbl.Text.Format("%s %s %s%s",
				FormatLevelPrice(lvl.strike, tickerIdx, st->spread).GetChars(),
				lvl.type.c_str(), lvl.label.c_str(), flipped ? " [FLIP]" : "");
			lbl.Color = RGB(255, 255, 255);
			lbl.FontBackColor = lvlColor;
			lbl.TransparentLabelBackground = 0;
			lbl.FontSize = 8;
			UseAndTrack(lbl);
		}
	}

	// --- GEX profile bars: calls extend right of the anchor, puts extend left
	//     (signed forza, like the TV boxes) ---
	if (In_ShowProfile.GetYesNo() && !st->profile.empty())
	{
		const double maxAbsForza = 10.0;
		const double boxH = In_ProfileHeight.GetInt() * sc.TickSize;
		const int profOffset = In_ProfileOffset.GetInt();
		const bool greyscale = In_BarColors.GetIndex() == 1;

		for (size_t i = 0; i < st->profile.size(); ++i)
		{
			const ProfileEntry& pe = st->profile[i];
			const double cv = ConvertPrice(pe.strike, tickerIdx, st->spread);
			const int barLen = (int)((pe.forza / maxAbsForza) * In_ProfileWidth.GetInt());
			if (barLen == 0)
				continue;

			double intensity = fabs(pe.forza) / maxAbsForza;
			int fillT = (int)(80 - intensity * 50);
			if (fillT < 0) fillT = 0;
			if (fillT > 100) fillT = 100;

			COLORREF baseC = pe.sign >= 0
				? (greyscale ? RGB(160, 160, 160) : colNeg)
				: (greyscale ? RGB(80, 80, 80) : colPos);

			s_UseTool box;
			box.Clear();
			box.ChartNumber = sc.ChartNumber;
			box.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
			box.AddMethod = UTAM_ADD_OR_ADJUST;
			box.LineNumber = LN_PROFILE_BASE + (int)i;
			box.BeginDateTime = XTime(profOffset + (barLen < 0 ? barLen : 0));
			box.EndDateTime = XTime(profOffset + (barLen > 0 ? barLen : 0));
			box.BeginValue = (float)(cv + boxH / 2.0);
			box.EndValue = (float)(cv - boxH / 2.0);
			box.Color = baseC;
			box.SecondaryColor = baseC;
			box.TransparencyLevel = fillT;
			box.LineWidth = 1;
			UseAndTrack(box);
		}
	}

	// --- Confluence zones: cluster ALL parsed levels + active BOS + active
	//     AVWAPs within the band (visibility toggles do not filter the pool,
	//     exactly like the TV script) ---
	if (In_ShowConf.GetYesNo())
	{
		std::vector<double> all;
		for (size_t i = 0; i < st->levels.size(); ++i)
		{
			const std::string& t = st->levels[i].type;
			bool tracked = t == "CW" || t == "PW" || t == "ZG" || t == "MP"
				|| t == "EH" || t == "EL" || t == "VH" || t == "VL"
				|| t == "PDH" || t == "PDL" || t == "PWH" || t == "PWL";
			if (tracked)
				all.push_back(ConvertPrice(st->levels[i].strike, tickerIdx, st->spread));
		}
		for (size_t b = 0; b < st->bos.size(); ++b)
			all.push_back(st->bos[b].price);
		if (pAsia > 0) all.push_back(pAsia);
		if (pEU > 0) all.push_back(pEU);
		if (pUS > 0) all.push_back(pUS);
		if (pPD > 0) all.push_back(pPD);

		std::sort(all.begin(), all.end());

		const double band = In_ConfBandPts.GetFloat() / 2.0;
		const int minSize = In_ConfMinSize.GetInt();
		int clusterNum = 0;
		size_t i = 0;
		while (i < all.size())
		{
			const double clusterMin = all[i];
			double clusterMax = clusterMin;
			size_t j = i + 1;
			int size = 1;
			while (j < all.size() && all[j] - clusterMin <= 2.0 * band)
			{
				clusterMax = all[j];
				++size;
				++j;
			}
			if (size >= minSize)
			{
				const int capped = size < 5 ? size : 5;
				const COLORREF boxColor = capped >= 5 ? In_ConfCol5.GetColor()
					: capped >= 4 ? In_ConfCol4.GetColor() : In_ConfCol3.GetColor();
				const int transparency = capped >= 5 ? 65 : capped >= 4 ? 70 : 75;

				s_UseTool box;
				box.Clear();
				box.ChartNumber = sc.ChartNumber;
				box.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
				box.AddMethod = UTAM_ADD_OR_ADJUST;
				box.LineNumber = LN_CONF_BASE + 2 * clusterNum;
				box.BeginDateTime = XTime(-In_LeftExt.GetInt());
				box.EndDateTime = XTime(In_ProfileOffset.GetInt());
				box.BeginValue = (float)clusterMax;
				box.EndValue = (float)clusterMin;
				box.Color = boxColor;
				box.SecondaryColor = boxColor;
				box.TransparencyLevel = transparency;
				box.LineWidth = 1;
				UseAndTrack(box);

				s_UseTool lbl;
				lbl.Clear();
				lbl.ChartNumber = sc.ChartNumber;
				lbl.DrawingType = DRAWING_TEXT;
				lbl.AddMethod = UTAM_ADD_OR_ADJUST;
				lbl.LineNumber = LN_CONF_BASE + 2 * clusterNum + 1;
				lbl.BeginDateTime = XTime(In_ProfileOffset.GetInt());
				lbl.BeginValue = (float)((clusterMax + clusterMin) / 2.0);
				if (capped >= 5)
					lbl.Text = "UBER";
				else
					lbl.Text.Format("%dx", capped);
				lbl.Color = RGB(255, 255, 255);
				lbl.FontBackColor = boxColor;
				lbl.TransparentLabelBackground = 0;
				lbl.FontSize = 8;
				UseAndTrack(lbl);

				++clusterNum;
			}
			i = j;
		}
	}

	// --- watermark ---
	{
		s_UseTool wm;
		wm.Clear();
		wm.ChartNumber = sc.ChartNumber;
		wm.DrawingType = DRAWING_TEXT;
		wm.AddMethod = UTAM_ADD_OR_ADJUST;
		wm.LineNumber = LN_WATERMARK;
		wm.BeginDateTime = XTime(In_ProfileOffset.GetInt());
		wm.BeginValue = (float)(sc.BaseDataIn[SC_HIGH][sc.Index] + sc.TickSize * 50.0);
		wm.Text.Format("TLADe %s", tickerStr.GetChars());
		wm.Color = RGB(128, 128, 128);
		wm.TransparentLabelBackground = 1;
		wm.FontSize = 8;
		UseAndTrack(wm);
	}

	// delete drawings from the previous pass that were not re-issued
	for (std::set<int>::const_iterator it = st->prevDrawn.begin();
		it != st->prevDrawn.end(); ++it)
	{
		if (drawn.find(*it) == drawn.end())
			DeleteDrawing(*it);
	}
	st->prevDrawn.swap(drawn);
}
