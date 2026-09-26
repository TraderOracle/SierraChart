#include "sierrachart.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <type_traits>

SCDLLName("Nebula v3")

namespace nebula
{
	struct Col
	{
		float r, g, b, t;
	};

	constexpr COLORREF Hex(uint32_t h)
	{
		return RGB((h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF);
	}

	inline Col C(COLORREF c, float t = 0.0f)
	{
		return Col{ (float)GetRValue(c), (float)GetGValue(c), (float)GetBValue(c), t };
	}

	inline Col T(Col c, float t)
	{
		c.t = t;
		return c;
	}

	inline float Clamp(float v, float lo, float hi)
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}

	inline Col Grad(float v, float lo, float hi, Col a, Col b)
	{
		if (!std::isfinite(v))
			return a;
		float f = hi == lo ? 1.0f : Clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
		return Col{ a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, a.t + (b.t - a.t) * f };
	}

	inline COLORREF Ref(Col c, COLORREF bg)
	{
		float a = 1.0f - Clamp(c.t, 0.0f, 100.0f) / 100.0f;
		int r = (int)std::lround(c.r * a + GetRValue(bg) * (1.0f - a));
		int g = (int)std::lround(c.g * a + GetGValue(bg) * (1.0f - a));
		int b = (int)std::lround(c.b * a + GetBValue(bg) * (1.0f - a));
		return RGB(r, g, b);
	}

	inline Col Darken(Col c, float k)
	{
		return Col{ c.r * k, c.g * k, c.b * k, c.t };
	}

	inline bool Ok(float v)
	{
		return std::isfinite(v) != 0;
	}

	inline float Nz(float v, float d = 0.0f)
	{
		return std::isfinite(v) ? v : d;
	}

	float Sma(SCFloatArrayRef a, int i, int n)
	{
		if (n > i + 1)
			n = i + 1;
		double s = 0;
		for (int k = 0; k < n; k++)
			s += a[i - k];
		return (float)(s / n);
	}

	float Stdev(SCFloatArrayRef a, int i, int n)
	{
		if (n > i + 1)
			n = i + 1;
		double m = 0;
		for (int k = 0; k < n; k++)
			m += a[i - k];
		m /= n;
		double v = 0;
		for (int k = 0; k < n; k++)
			v += (a[i - k] - m) * (a[i - k] - m);
		return (float)std::sqrt(v / n);
	}

	float Wma(SCFloatArrayRef a, int i, int n)
	{
		if (n > i + 1)
			n = i + 1;
		double s = 0, w = 0;
		for (int k = 0; k < n; k++)
		{
			double wk = n - k;
			s += a[i - k] * wk;
			w += wk;
		}
		return (float)(s / w);
	}

	float Highest(SCFloatArrayRef a, int i, int n)
	{
		float h = -FLT_MAX;
		for (int k = 0; k < n && i - k >= 0; k++)
			h = (std::max)(h, a[i - k]);
		return h;
	}

	float Lowest(SCFloatArrayRef a, int i, int n)
	{
		float l = FLT_MAX;
		for (int k = 0; k < n && i - k >= 0; k++)
			l = (std::min)(l, a[i - k]);
		return l;
	}

	float Linreg(SCFloatArrayRef a, int i, int n)
	{
		if (n > i + 1)
			n = i + 1;
		if (n < 2)
			return a[i];
		double sx = 0, sy = 0, sxy = 0, sxx = 0;
		for (int k = 0; k < n; k++)
		{
			double x = k;
			double y = a[i - n + 1 + k];
			sx += x;
			sy += y;
			sxy += x * y;
			sxx += x * x;
		}
		double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
		double icpt = (sy - slope * sx) / n;
		return (float)(icpt + slope * (n - 1));
	}

	void EmaStep(SCFloatArrayRef out, float v, int i, int n)
	{
		float al = 2.0f / (n + 1);
		if (i == 0 || !Ok(out[i - 1]))
			out[i] = v;
		else
			out[i] = al * v + (1.0f - al) * out[i - 1];
	}

	void RmaStep(SCFloatArrayRef out, float v, int i, int n)
	{
		float al = 1.0f / n;
		if (i == 0 || !Ok(out[i - 1]))
			out[i] = v;
		else
			out[i] = al * v + (1.0f - al) * out[i - 1];
	}

	inline bool CrossOver(float a0, float a1, float b0, float b1)
	{
		return a0 > b0 && a1 <= b1;
	}

	inline bool CrossUnder(float a0, float a1, float b0, float b1)
	{
		return a0 < b0 && a1 >= b1;
	}

	inline bool InSession(int t, int start, int end)
	{
		if (start < end)
			return t >= start && t < end;
		return t >= start || t < end;
	}

	SCString Bar(float pct, int blocks)
	{
		int full = (int)std::lround(Clamp(pct, 0.0f, 100.0f) / 100.0f * blocks);
		SCString s;
		for (int b = 1; b <= blocks; b++)
			s += b <= full ? "|" : ".";
		return s;
	}

	struct Imbalance
	{
		int Bar;
		float Level;
		float Fill;
		int Dir;
	};

	struct Tag
	{
		float Price;
		SCString Name;
		COLORREF Color;
	};

	const COLORREF THEME_BULL[] = {
		Hex(0x00ff00), Hex(0x00f5f1), Hex(0x03fcf4), Hex(0x35defc), Hex(0x39ff14), Hex(0x00f0ff), Hex(0x64ffda),
		Hex(0x4dd0e1), Hex(0xffc857), Hex(0xa3be8c), Hex(0x50fa7b), Hex(0x7aa2f7), Hex(0xffd700), Hex(0x00bfff),
		Hex(0x00e04b), Hex(0x1e88e5), Hex(0xffc400), Hex(0x76ff03), Hex(0x1de9b6) };
	const COLORREF THEME_BEAR[] = {
		Hex(0xff0000), Hex(0xfc03f8), Hex(0xfca903), Hex(0xfcf11c), Hex(0xff073a), Hex(0xff2a6d), Hex(0xb388ff),
		Hex(0xff8a65), Hex(0xe84855), Hex(0xbf616a), Hex(0xff79c6), Hex(0xf7768e), Hex(0x9d4edd), Hex(0xff4500),
		Hex(0xffd600), Hex(0xe53935), Hex(0xd50000), Hex(0xaa00ff), Hex(0xff9100) };
	const COLORREF CLOUD_UP[] = {
		0, Hex(0x00d4c8), Hex(0x2ecc71), Hex(0x4fc3f7), Hex(0xc6ff00), Hex(0xffd54f), Hex(0x1de9b6),
		Hex(0x69f0ae), Hex(0x2979ff), Hex(0x00a86b), Hex(0x40e0d0), Hex(0x8c9eff), Hex(0x80deea) };
	const COLORREF CLOUD_DN[] = {
		0, Hex(0xff6f61), Hex(0xe74c3c), Hex(0xba68c8), Hex(0xff9100), Hex(0x7c4dff), Hex(0xf50057),
		Hex(0xff80ab), Hex(0xffab00), Hex(0xe0115f), Hex(0xff6347), Hex(0xffab91), Hex(0xff3d00) };

	const COLORREF GRAY = Hex(0x787b86);
	const COLORREF PINE_RED = Hex(0xf23645);
	const COLORREF PINE_PURPLE = Hex(0x9c27b0);
	const COLORREF PINE_ORANGE = Hex(0xff9800);
	const COLORREF YELLOW = Hex(0xffeb3b);
	const COLORREF BLUE_VECTOR = RGB(83, 144, 249);
	const COLORREF VIOLET_VECTOR = Hex(0xe040fb);
	const COLORREF REG_UP = Hex(0x02a433);
	const COLORREF REG_DN = Hex(0xa10101);

	enum CandleMode
	{
		NCM_NONE, NCM_VECTOR, NCM_WADDAH, NCM_SQUEEZE, NCM_VOLUME_DELTA, NCM_TREND_AGREEMENT, NCM_TIDAL_WAVE,
		NCM_CONFLUENCE, NCM_RSI_HEAT, NCM_KERNEL_SLOPE, NCM_RELATIVE_VOLUME, NCM_ADX_STRENGTH, NCM_HEIKIN_ASHI,
		NCM_STRETCH, NCM_SUPERTREND
	};

	enum CloudType
	{
		NCT_NONE, NCT_SIMPLE, NCT_RSI, NCT_MFI, NCT_CCI
	};

	enum SubgraphIndex
	{
		SG_CANDLE_OUTLINE, SG_CANDLE_BODY, SG_CLOUD_TOP, SG_CLOUD_BOTTOM, SG_SQZ_TOP, SG_SQZ_BOTTOM,
		SG_SQZ_EDGE_TOP, SG_SQZ_EDGE_BOTTOM, SG_HEMA, SG_BUY, SG_SELL, SG_STRONG_BUY, SG_STRONG_SELL,
		SG_ADD_LONG, SG_ADD_SHORT, SG_VODKA_LONG, SG_VODKA_SHORT, SG_TP_ALL, SG_TP_PARTIAL, SG_DIV_BULL,
		SG_DIV_BEAR, SG_CROSS_UP, SG_CROSS_DOWN, SG_SQZ_REL_UP, SG_SQZ_REL_DOWN, SG_ORB_UP, SG_ORB_DOWN,
		SG_FANVMA, SG_MCGINLEY, SG_CALC0
	};

	enum InputIndex
	{
		IN_CANDLE_MODE, IN_THEME, IN_CLOUD_TYPE, IN_CLOUD_PALETTE, IN_SHOW_DASH, IN_SHOW_SQZ, IN_SHOW_DIV,
		IN_SHOW_LEVELS, IN_SHOW_HEMA, IN_SHOW_PLUS, IN_SHOW_VODKA, IN_SHOW_921, IN_SHOW_VI, IN_BG_COLOR,
		IN_TEXT_COLOR, IN_DASH_POS, IN_DASH_FONT, IN_DASH_LEVELS, IN_DASH_SPACING, IN_BAR_BLOCKS,
		IN_SQZ_COLOR, IN_SQZ_MIN, IN_SQZ_RELEASE, IN_DIV_LINES, IN_DIV_TRANSP, IN_DIV_RSI, IN_DIV_LBL, IN_DIV_LBR,
		IN_DIV_MIN, IN_DIV_MAX, IN_OR_ON, IN_OR_START, IN_OR_END, IN_LON_ON, IN_LON_START, IN_LON_END,
		IN_ASIA_ON, IN_ASIA_START, IN_ASIA_END, IN_YC_ON, IN_RTH_START, IN_RTH_END, IN_LVL_EXT, IN_LVL_FORMING,
		IN_LVL_BREAK, IN_LVL_MERGE, IN_LVL_OFFSET, IN_LVL_TRANSP, IN_VI_EXTEND, IN_VI_WIDTH, IN_VI_STYLE,
		IN_TP_PARTIAL, IN_TP_ALL, IN_QUAD_921, IN_CLOUD_SIMPLE_T, IN_CLOUD_WEAK_T, IN_CLOUD_STRONG_T,
		IN_WAE_SENS, IN_WAE_FAST, IN_WAE_SLOW, IN_WAE_CHANNEL, IN_WAE_MULT, IN_SQ_TOL, IN_SHARK_2575,
		IN_MARKER_SPACING, IN_ALERTS_ON, IN_ALERT_NUMBER
	};

	enum ArrayIndex
	{
		A_TR, A_ATR14, A_ATR30, A_ATR10, A_EMA9, A_EMA21, A_EMA14, A_PDM, A_MDM, A_DI_PLUS, A_DI_MINUS, A_ADX_IN, A_ADX,
		A_CVD_BUY, A_CVD_SELL, A_TD_B, A_TD_S, A_LUX, A_FR_UP, A_FR_DN, A_UP_CLOSE, A_DN_CLOSE, A_EARLY,
		A_RSI_UP, A_RSI_DN, A_RSI14, A_FV_SPDI, A_FV_SMDI, A_FV_STR, A_FV_ADX, A_FV_VAR, A_EMA_FAST, A_EMA_SLOW,
		A_MACD, A_T1, A_E1, A_T1S, A_E1S, A_TRAMP_DN, A_TRAMP_UP, A_GO_UP, A_GO_DN, A_TRAMP, A_SQ_ON, A_SQ_SRC,
		A_SQ_VAL, A_SQ_CR, A_SQ_CG, A_SQ_POS, A_SQ_NEG, A_SQZ, A_SQ_LEN, A_PV_CODE, A_SHARK, A_DEADREV, A_BANDS,
		A_RSIU_UP, A_RSIU_DN, A_RSIU, A_RSIU_BASIS, A_RSIU_UPPER, A_RSIU_LOWER, A_RSIU_MA, A_PI_UPPER, A_PI_LOWER,
		A_ATRB_UPPER, A_ATRB_LOWER, A_LAST_BUY_WATCH, A_LAST_SELL_WATCH, A_PLOT_BUY, A_PLOT_SELL, A_NZVOL,
		A_VOL_AVGL, A_VOLA, A_DER_C, A_DER_CP, A_DER_CM, A_DER_BO, A_DER_BC, A_WMA1, A_WMA2, A_LLW, A_UPW, A_DNW,
		A_KERNEL, A_RSID_UP, A_RSID_DN, A_RSID, A_PL_FOUND, A_PL_RSI, A_PL_LOW, A_PL_BAR, A_PH_FOUND, A_PH_RSI,
		A_PH_HIGH, A_PH_BAR, A_DIVB_X1, A_DIVB_Y1, A_DIVS_X1, A_DIVS_Y1, A_TP_COUNT, A_WAVE, A_BRIGHT_G, A_BRIGHT_R,
		A_SIG_WAVE, A_HEMA_B, A_RSI_HEAT, A_KSLOPE, A_RELVOL_S, A_ADX_S, A_HA_OPEN, A_ZSCORE, A_ST_UPPER,
		A_ST_LOWER, A_ST_DIR, A_ST_VAL, A_CVD, A_DER_SUPN, A_DER_DEMN, A_VOLA_RAW, A_CCI_TP,
		A_S0_HI, A_S0_LO, A_S0_START, A_S0_END, A_S0_STATE, A_S0_HIBRK, A_S0_LOBRK,
		A_S1_HI, A_S1_LO, A_S1_START, A_S1_END, A_S1_STATE, A_S1_HIBRK, A_S1_LOBRK,
		A_S2_HI, A_S2_LO, A_S2_START, A_S2_END, A_S2_STATE, A_S2_HIBRK, A_S2_LOBRK,
		A_YC_VAL, A_YC_X, A_ORB_DIR, A_ORB_BAR, A_ORB_POSTHI, A_ORB_POSTLO, A_OR_DONE_SIZE,
		A_COUNT
	};

	const int ARRAYS_PER_SUBGRAPH = 1 + MAX_SUBGRAPH_EXTRA_ARRAYS;
	const int CALC_SUBGRAPHS = (A_COUNT + ARRAYS_PER_SUBGRAPH - 1) / ARRAYS_PER_SUBGRAPH;

	enum SessionSlot
	{
		NSESS_OR = 0, NSESS_LON = 1, NSESS_ASIA = 2
	};

	enum SessionState
	{
		NSS_IDLE = 0, NSS_FORMING = 1, NSS_DONE = 2
	};

	const int LN_DASH = 781000;
	const int LN_LEVEL_LINE = 782000;
	const int LN_LEVEL_TAG = 783000;
	const int LN_FORMING = 784000;
	const int LN_IMBALANCE = 785000;
	const int LN_DIV_LINE = 786000;
	const int MAX_DASH_ROWS = 20;
	const int MAX_LEVEL_LINES = 20;
	const int MAX_FORMING = 3;
	const int MAX_IMBALANCES = 150;
	const int MAX_DIV_LINES = 40;
}

using namespace nebula;

template <typename TObj>
static auto TrySetTransparency(TObj& obj, int value, int) -> decltype(obj.TransparencyLevel = 0, void())
{
	obj.TransparencyLevel = static_cast<typename std::remove_reference<decltype(obj.TransparencyLevel)>::type>(value);
}

template <typename TObj>
static void TrySetTransparency(TObj&, int, long)
{
}

static SCFloatArrayRef Arr(SCStudyInterfaceRef sc, int n)
{
	int sg = SG_CALC0 + n / ARRAYS_PER_SUBGRAPH;
	int k = n % ARRAYS_PER_SUBGRAPH;
	if (k == 0)
		return sc.Subgraph[sg].Data;
	return sc.Subgraph[sg].Arrays[k - 1];
}

static void DeleteSlots(SCStudyInterfaceRef sc, int base, int from, int to)
{
	for (int k = from; k < to; k++)
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, base + k + sc.StudyGraphInstanceID * 10000);
}

static void UseSlot(SCStudyInterfaceRef sc, s_UseTool& tool, int lineNumber)
{
	tool.ChartNumber = sc.ChartNumber;
	tool.Region = sc.GraphRegion;
	tool.LineNumber = lineNumber + sc.StudyGraphInstanceID * 10000;
	tool.AddMethod = UTAM_ADD_OR_ADJUST;
	sc.UseTool(tool);
}

static SCString TimeframeLabel(int seconds)
{
	SCString s;
	if (seconds <= 0)
		s = "";
	else if (seconds < 60)
		s.Format("%ds", seconds);
	else if (seconds < 3600)
		s.Format("%dm", seconds / 60);
	else if (seconds < 86400)
		s.Format("%dh", seconds / 3600);
	else
		s.Format("%dD", seconds / 86400);
	return s;
}

static SCString Pad(const char* text, int width)
{
	SCString s = text;
	while ((int)s.GetLength() < width)
		s += " ";
	return s;
}

SCSFExport scsf_NebulaV3(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "Nebula v3";
		sc.StudyDescription = "Nebula v3 by TraderOracle: trend cloud, Tidal Wave signals, take-profit confluence, RSI divergences, squeeze zones, session levels and dashboard. Designed for 1-minute NQ.";
		sc.GraphRegion = 0;
		sc.AutoLoop = 0;
		sc.ValueFormat = VALUEFORMAT_INHERITED;
		sc.ScaleRangeType = SCALE_SAMEASREGION;
		sc.DrawStudyUnderneathMainPriceGraph = 0;
		TrySetTransparency(sc, 80, 0);
		sc.GlobalDisplayStudySubgraphsNameAndValue = 0;
		sc.DisplayStudyInputValues = 0;

		struct SgDef { int Index; const char* Name; int Style; COLORREF Color; int Width; };
		const SgDef defs[] = {
			{ SG_CANDLE_OUTLINE, "Candle Outline", DRAWSTYLE_COLOR_BAR, RGB(128, 128, 128), 1 },
			{ SG_CANDLE_BODY, "Candle Body", DRAWSTYLE_COLOR_BAR_CANDLE_FILL, RGB(128, 128, 128), 1 },
			{ SG_CLOUD_TOP, "Cloud Top", DRAWSTYLE_TRANSPARENT_FILL_TOP, RGB(0, 255, 0), 1 },
			{ SG_CLOUD_BOTTOM, "Cloud Bottom", DRAWSTYLE_TRANSPARENT_FILL_BOTTOM, RGB(0, 255, 0), 1 },
			{ SG_SQZ_TOP, "Squeeze Zone Top", DRAWSTYLE_TRANSPARENT_FILL_TOP, RGB(33, 150, 243), 1 },
			{ SG_SQZ_BOTTOM, "Squeeze Zone Bottom", DRAWSTYLE_TRANSPARENT_FILL_BOTTOM, RGB(33, 150, 243), 1 },
			{ SG_SQZ_EDGE_TOP, "Squeeze Edge Top", DRAWSTYLE_LINE_SKIP_ZEROS, RGB(33, 150, 243), 1 },
			{ SG_SQZ_EDGE_BOTTOM, "Squeeze Edge Bottom", DRAWSTYLE_LINE_SKIP_ZEROS, RGB(33, 150, 243), 1 },
			{ SG_HEMA, "HEMA", DRAWSTYLE_LINE_SKIP_ZEROS, Hex(0xe040fb), 1 },
			{ SG_BUY, "Buy", DRAWSTYLE_POINT, RGB(0, 255, 0), 5 },
			{ SG_SELL, "Sell", DRAWSTYLE_POINT, RGB(255, 0, 0), 5 },
			{ SG_STRONG_BUY, "Strong Buy", DRAWSTYLE_ARROW_UP, RGB(0, 255, 0), 3 },
			{ SG_STRONG_SELL, "Strong Sell", DRAWSTYLE_ARROW_DOWN, RGB(153, 0, 0), 3 },
			{ SG_ADD_LONG, "Add Long (light)", DRAWSTYLE_PLUS, RGB(0, 255, 0), 2 },
			{ SG_ADD_SHORT, "Add Short (light)", DRAWSTYLE_PLUS, RGB(255, 0, 0), 2 },
			{ SG_VODKA_LONG, "Add Long (Vodka Shot)", DRAWSTYLE_PLUS, RGB(0, 255, 0), 5 },
			{ SG_VODKA_SHORT, "Add Short (Vodka Shot)", DRAWSTYLE_PLUS, RGB(255, 0, 0), 5 },
			{ SG_TP_ALL, "Take All Profit", DRAWSTYLE_STAR, PINE_RED, 5 },
			{ SG_TP_PARTIAL, "Take Partial Profit", DRAWSTYLE_DIAMOND, PINE_PURPLE, 4 },
			{ SG_DIV_BULL, "Bullish Divergence", DRAWSTYLE_CUSTOM_TEXT, RGB(0, 255, 0), 8 },
			{ SG_DIV_BEAR, "Bearish Divergence", DRAWSTYLE_CUSTOM_TEXT, RGB(255, 0, 0), 8 },
			{ SG_CROSS_UP, "9/21 Cross Up", DRAWSTYLE_TRIANGLE_UP, YELLOW, 2 },
			{ SG_CROSS_DOWN, "9/21 Cross Down", DRAWSTYLE_TRIANGLE_DOWN, YELLOW, 2 },
			{ SG_SQZ_REL_UP, "Squeeze Release Up", DRAWSTYLE_ARROW_UP, RGB(0, 255, 0), 2 },
			{ SG_SQZ_REL_DOWN, "Squeeze Release Down", DRAWSTYLE_ARROW_DOWN, RGB(255, 0, 0), 2 },
			{ SG_ORB_UP, "OR Breakout Up", DRAWSTYLE_TRIANGLE_UP, RGB(0, 255, 0), 3 },
			{ SG_ORB_DOWN, "OR Breakout Down", DRAWSTYLE_TRIANGLE_DOWN, RGB(255, 0, 0), 3 },
			{ SG_FANVMA, "Fantail VMA", DRAWSTYLE_IGNORE, RGB(255, 255, 255), 1 },
			{ SG_MCGINLEY, "McGinley Dynamic", DRAWSTYLE_IGNORE, RGB(0, 0, 255), 1 },
		};
		for (const SgDef& d : defs)
		{
			SCSubgraphRef s = sc.Subgraph[d.Index];
			s.Name = d.Name;
			s.DrawStyle = d.Style;
			s.PrimaryColor = d.Color;
			s.LineWidth = d.Width;
			s.DrawZeros = 0;
			s.DisplayNameValueInWindowsFlags = 0;
		}
		sc.Subgraph[SG_DIV_BULL].TextDrawStyleText = "Div";
		sc.Subgraph[SG_DIV_BEAR].TextDrawStyleText = "Div";
		sc.Subgraph[SG_SQZ_EDGE_TOP].LineStyle = LINESTYLE_SOLID;
		for (int k = 0; k < CALC_SUBGRAPHS; k++)
		{
			SCString name;
			name.Format("Calc %d", k + 1);
			sc.Subgraph[SG_CALC0 + k].Name = name;
			sc.Subgraph[SG_CALC0 + k].DrawStyle = DRAWSTYLE_IGNORE;
			sc.Subgraph[SG_CALC0 + k].DrawZeros = 0;
			sc.Subgraph[SG_CALC0 + k].DisplayNameValueInWindowsFlags = 0;
		}

		sc.Input[IN_CANDLE_MODE].Name = "Candle Coloring";
		sc.Input[IN_CANDLE_MODE].SetCustomInputStrings("None;Vector;Waddah;Squeeze;Volume Delta;Trend Agreement;Tidal Wave;Signal Confluence;RSI Heat;Kernel Slope;Relative Volume;ADX Strength;Heikin Ashi Trend;Stretch from Mean;Supertrend");
		sc.Input[IN_CANDLE_MODE].SetCustomInputIndex(NCM_WADDAH);
		sc.Input[IN_THEME].Name = "Color Theme";
		sc.Input[IN_THEME].SetCustomInputStrings("Standard;Pinky and the Brain;Color Blind;Mellow Yellow;Neon;Cyberpunk;Aurora;Ocean;Sunset;Nord;Dracula;Tokyo Night;Royal;Ice and Fire;Green Lantern;Superman;Wonder Woman;Hulk;Aquaman");
		sc.Input[IN_THEME].SetCustomInputIndex(0);
		sc.Input[IN_CLOUD_TYPE].Name = "Cloud Type";
		sc.Input[IN_CLOUD_TYPE].SetCustomInputStrings("None;Simple;Relative Strength;Money Flow;Commodity Channel");
		sc.Input[IN_CLOUD_TYPE].SetCustomInputIndex(NCT_SIMPLE);
		sc.Input[IN_CLOUD_PALETTE].Name = "Cloud Colors";
		sc.Input[IN_CLOUD_PALETTE].SetCustomInputStrings("Theme;Aqua / Coral;Emerald / Crimson;Sky / Orchid;Lime / Tangerine;Gold / Violet;Teal / Magenta;Mint / Rose;Cobalt / Amber;Jade / Ruby;Turquoise / Tomato;Periwinkle / Peach;Ice / Lava");
		sc.Input[IN_CLOUD_PALETTE].SetCustomInputIndex(0);
		sc.Input[IN_SHOW_DASH].Name = "Show dashboard";
		sc.Input[IN_SHOW_DASH].SetYesNo(0);
		sc.Input[IN_SHOW_SQZ].Name = "Show squeeze zones";
		sc.Input[IN_SHOW_SQZ].SetYesNo(1);
		sc.Input[IN_SHOW_DIV].Name = "Show RSI divergences";
		sc.Input[IN_SHOW_DIV].SetYesNo(1);
		sc.Input[IN_SHOW_LEVELS].Name = "Show session levels";
		sc.Input[IN_SHOW_LEVELS].SetYesNo(1);
		sc.Input[IN_SHOW_HEMA].Name = "Show HEMA line";
		sc.Input[IN_SHOW_HEMA].SetYesNo(0);
		sc.Input[IN_SHOW_PLUS].Name = "Show plus sign to add";
		sc.Input[IN_SHOW_PLUS].SetYesNo(0);
		sc.Input[IN_SHOW_VODKA].Name = "Show bigger plus sign (Vodka Shot)";
		sc.Input[IN_SHOW_VODKA].SetYesNo(1);
		sc.Input[IN_SHOW_921].Name = "Show 9/21 EMA cross";
		sc.Input[IN_SHOW_921].SetYesNo(0);
		sc.Input[IN_SHOW_VI].Name = "Show volume imbalances";
		sc.Input[IN_SHOW_VI].SetYesNo(0);
		sc.Input[IN_BG_COLOR].Name = "Chart background color (for blending)";
		sc.Input[IN_BG_COLOR].SetColor(RGB(0, 0, 0));
		sc.Input[IN_TEXT_COLOR].Name = "Chart text color (neutral elements)";
		sc.Input[IN_TEXT_COLOR].SetColor(RGB(209, 212, 220));

		sc.Input[IN_DASH_POS].Name = "Dashboard: Position";
		sc.Input[IN_DASH_POS].SetCustomInputStrings("Top Right;Top Center;Top Left;Middle Right;Middle Left;Bottom Right;Bottom Center;Bottom Left");
		sc.Input[IN_DASH_POS].SetCustomInputIndex(0);
		sc.Input[IN_DASH_FONT].Name = "Dashboard: Font size";
		sc.Input[IN_DASH_FONT].SetInt(9);
		sc.Input[IN_DASH_FONT].SetIntLimits(6, 30);
		sc.Input[IN_DASH_LEVELS].Name = "Dashboard: Show session levels";
		sc.Input[IN_DASH_LEVELS].SetYesNo(1);
		sc.Input[IN_DASH_SPACING].Name = "Dashboard: Row spacing (% of chart height)";
		sc.Input[IN_DASH_SPACING].SetFloat(2.8f);
		sc.Input[IN_DASH_SPACING].SetFloatLimits(1.0f, 10.0f);
		sc.Input[IN_BAR_BLOCKS].Name = "Dashboard: Bar length (blocks)";
		sc.Input[IN_BAR_BLOCKS].SetInt(10);
		sc.Input[IN_BAR_BLOCKS].SetIntLimits(5, 20);

		sc.Input[IN_SQZ_COLOR].Name = "Squeeze zones: Color";
		sc.Input[IN_SQZ_COLOR].SetColor(RGB(33, 150, 243));
		sc.Input[IN_SQZ_MIN].Name = "Squeeze zones: Minimum length (bars)";
		sc.Input[IN_SQZ_MIN].SetInt(3);
		sc.Input[IN_SQZ_MIN].SetIntLimits(1, 500);
		sc.Input[IN_SQZ_RELEASE].Name = "Squeeze zones: Show release arrows";
		sc.Input[IN_SQZ_RELEASE].SetYesNo(0);

		sc.Input[IN_DIV_LINES].Name = "RSI divergence: Draw lines on price";
		sc.Input[IN_DIV_LINES].SetYesNo(0);
		sc.Input[IN_DIV_TRANSP].Name = "RSI divergence: Transparency";
		sc.Input[IN_DIV_TRANSP].SetInt(50);
		sc.Input[IN_DIV_TRANSP].SetIntLimits(0, 100);
		sc.Input[IN_DIV_RSI].Name = "RSI divergence: RSI length";
		sc.Input[IN_DIV_RSI].SetInt(14);
		sc.Input[IN_DIV_RSI].SetIntLimits(1, 500);
		sc.Input[IN_DIV_LBL].Name = "RSI divergence: Pivot lookback left";
		sc.Input[IN_DIV_LBL].SetInt(5);
		sc.Input[IN_DIV_LBL].SetIntLimits(1, 100);
		sc.Input[IN_DIV_LBR].Name = "RSI divergence: Pivot lookback right";
		sc.Input[IN_DIV_LBR].SetInt(5);
		sc.Input[IN_DIV_LBR].SetIntLimits(1, 100);
		sc.Input[IN_DIV_MIN].Name = "RSI divergence: Min bars between pivots";
		sc.Input[IN_DIV_MIN].SetInt(5);
		sc.Input[IN_DIV_MIN].SetIntLimits(1, 1000);
		sc.Input[IN_DIV_MAX].Name = "RSI divergence: Max bars between pivots";
		sc.Input[IN_DIV_MAX].SetInt(60);
		sc.Input[IN_DIV_MAX].SetIntLimits(2, 1000);

		sc.Input[IN_OR_ON].Name = "Session levels: Opening range";
		sc.Input[IN_OR_ON].SetYesNo(1);
		sc.Input[IN_OR_START].Name = "Session levels: Opening range start (New York)";
		sc.Input[IN_OR_START].SetTime(9 * 3600 + 30 * 60);
		sc.Input[IN_OR_END].Name = "Session levels: Opening range end (New York)";
		sc.Input[IN_OR_END].SetTime(10 * 3600 + 30 * 60);
		sc.Input[IN_LON_ON].Name = "Session levels: London";
		sc.Input[IN_LON_ON].SetYesNo(1);
		sc.Input[IN_LON_START].Name = "Session levels: London start (New York)";
		sc.Input[IN_LON_START].SetTime(3 * 3600);
		sc.Input[IN_LON_END].Name = "Session levels: London end (New York)";
		sc.Input[IN_LON_END].SetTime(8 * 3600);
		sc.Input[IN_ASIA_ON].Name = "Session levels: Asia";
		sc.Input[IN_ASIA_ON].SetYesNo(1);
		sc.Input[IN_ASIA_START].Name = "Session levels: Asia start (New York)";
		sc.Input[IN_ASIA_START].SetTime(18 * 3600);
		sc.Input[IN_ASIA_END].Name = "Session levels: Asia end (New York)";
		sc.Input[IN_ASIA_END].SetTime(3 * 3600);
		sc.Input[IN_YC_ON].Name = "Session levels: Yesterday's close";
		sc.Input[IN_YC_ON].SetYesNo(1);
		sc.Input[IN_RTH_START].Name = "Session levels: Regular session start (New York)";
		sc.Input[IN_RTH_START].SetTime(9 * 3600 + 30 * 60);
		sc.Input[IN_RTH_END].Name = "Session levels: Regular session end (New York)";
		sc.Input[IN_RTH_END].SetTime(16 * 3600);
		sc.Input[IN_LVL_EXT].Name = "Session levels: Opening range extensions (1.5x / 2x)";
		sc.Input[IN_LVL_EXT].SetYesNo(1);
		sc.Input[IN_LVL_FORMING].Name = "Session levels: Shade ranges while they form";
		sc.Input[IN_LVL_FORMING].SetYesNo(1);
		sc.Input[IN_LVL_BREAK].Name = "Session levels: Mark opening range breakout";
		sc.Input[IN_LVL_BREAK].SetYesNo(1);
		sc.Input[IN_LVL_MERGE].Name = "Session levels: Merge tags within (ticks)";
		sc.Input[IN_LVL_MERGE].SetInt(8);
		sc.Input[IN_LVL_MERGE].SetIntLimits(0, 1000);
		sc.Input[IN_LVL_OFFSET].Name = "Session levels: Tag distance from last bar";
		sc.Input[IN_LVL_OFFSET].SetInt(4);
		sc.Input[IN_LVL_OFFSET].SetIntLimits(1, 50);
		sc.Input[IN_LVL_TRANSP].Name = "Session levels: Level transparency";
		sc.Input[IN_LVL_TRANSP].SetInt(45);
		sc.Input[IN_LVL_TRANSP].SetIntLimits(0, 100);

		sc.Input[IN_VI_EXTEND].Name = "Volume imbalances: Bars to extend line";
		sc.Input[IN_VI_EXTEND].SetInt(50);
		sc.Input[IN_VI_EXTEND].SetIntLimits(10, 500);
		sc.Input[IN_VI_WIDTH].Name = "Volume imbalances: Line width";
		sc.Input[IN_VI_WIDTH].SetInt(3);
		sc.Input[IN_VI_WIDTH].SetIntLimits(1, 10);
		sc.Input[IN_VI_STYLE].Name = "Volume imbalances: Line style";
		sc.Input[IN_VI_STYLE].SetCustomInputStrings("Solid;Dotted;Dashed");
		sc.Input[IN_VI_STYLE].SetCustomInputIndex(1);

		sc.Input[IN_TP_PARTIAL].Name = "Advanced: Minimum signals for take partial profit";
		sc.Input[IN_TP_PARTIAL].SetInt(5);
		sc.Input[IN_TP_PARTIAL].SetIntLimits(2, 50);
		sc.Input[IN_TP_ALL].Name = "Advanced: Minimum signals for take ALL profit";
		sc.Input[IN_TP_ALL].SetInt(7);
		sc.Input[IN_TP_ALL].SetIntLimits(3, 50);
		sc.Input[IN_QUAD_921].Name = "Advanced: Use quadratic equation for 9/21 cross";
		sc.Input[IN_QUAD_921].SetYesNo(0);
		sc.Input[IN_CLOUD_SIMPLE_T].Name = "Advanced: Simple cloud transparency";
		sc.Input[IN_CLOUD_SIMPLE_T].SetInt(80);
		sc.Input[IN_CLOUD_SIMPLE_T].SetIntLimits(0, 100);
		sc.Input[IN_CLOUD_WEAK_T].Name = "Advanced: Weak trend transparency";
		sc.Input[IN_CLOUD_WEAK_T].SetInt(80);
		sc.Input[IN_CLOUD_WEAK_T].SetIntLimits(0, 100);
		sc.Input[IN_CLOUD_STRONG_T].Name = "Advanced: Strong trend transparency";
		sc.Input[IN_CLOUD_STRONG_T].SetInt(50);
		sc.Input[IN_CLOUD_STRONG_T].SetIntLimits(0, 100);

		sc.Input[IN_WAE_SENS].Name = "WAE: Sensitivity";
		sc.Input[IN_WAE_SENS].SetInt(150);
		sc.Input[IN_WAE_FAST].Name = "WAE: FastEMA length";
		sc.Input[IN_WAE_FAST].SetInt(20);
		sc.Input[IN_WAE_FAST].SetIntLimits(1, 500);
		sc.Input[IN_WAE_SLOW].Name = "WAE: SlowEMA length";
		sc.Input[IN_WAE_SLOW].SetInt(40);
		sc.Input[IN_WAE_SLOW].SetIntLimits(1, 500);
		sc.Input[IN_WAE_CHANNEL].Name = "WAE: BB channel length";
		sc.Input[IN_WAE_CHANNEL].SetInt(20);
		sc.Input[IN_WAE_CHANNEL].SetIntLimits(1, 500);
		sc.Input[IN_WAE_MULT].Name = "WAE: BB stdev multiplier";
		sc.Input[IN_WAE_MULT].SetFloat(2.0f);

		sc.Input[IN_SQ_TOL].Name = "Squeeze tolerance (lower = more sensitive)";
		sc.Input[IN_SQ_TOL].SetInt(2);
		sc.Input[IN_SQ_TOL].SetIntLimits(0, 100);
		sc.Input[IN_SHARK_2575].Name = "Shark: Apply 25/75 RSI rule";
		sc.Input[IN_SHARK_2575].SetYesNo(0);

		sc.Input[IN_MARKER_SPACING].Name = "Marker spacing from bar (x ATR 14)";
		sc.Input[IN_MARKER_SPACING].SetFloat(0.3f);
		sc.Input[IN_MARKER_SPACING].SetFloatLimits(0.0f, 5.0f);
		sc.Input[IN_ALERTS_ON].Name = "Alerts: Any Nebula signal";
		sc.Input[IN_ALERTS_ON].SetYesNo(1);
		sc.Input[IN_ALERT_NUMBER].Name = "Alerts: Alert sound number";
		sc.Input[IN_ALERT_NUMBER].SetInt(1);
		sc.Input[IN_ALERT_NUMBER].SetIntLimits(1, 150);
		return;
	}

	std::vector<Imbalance>* imbalances = (std::vector<Imbalance>*)sc.GetPersistentPointer(1);
	if (sc.LastCallToFunction)
	{
		if (imbalances != nullptr)
		{
			delete imbalances;
			sc.SetPersistentPointer(1, nullptr);
		}
		return;
	}
	if (imbalances == nullptr)
	{
		imbalances = new std::vector<Imbalance>();
		sc.SetPersistentPointer(1, imbalances);
	}
	int& lastImbalanceBar = sc.GetPersistentInt(1);
	int& prevLevelLines = sc.GetPersistentInt(2);
	int& prevLevelTags = sc.GetPersistentInt(3);
	int& prevForming = sc.GetPersistentInt(4);
	int& prevImbalanceLines = sc.GetPersistentInt(5);
	int& prevDivLines = sc.GetPersistentInt(6);
	int& prevDashRows = sc.GetPersistentInt(7);
	int& lastAlertBar = sc.GetPersistentInt(8);

	const int candleMode = sc.Input[IN_CANDLE_MODE].GetIndex();
	const int theme = (std::min)((int)sc.Input[IN_THEME].GetIndex(), 18);
	const int cloudType = sc.Input[IN_CLOUD_TYPE].GetIndex();
	const int palette = (std::min)((int)sc.Input[IN_CLOUD_PALETTE].GetIndex(), 12);
	const bool showDash = sc.Input[IN_SHOW_DASH].GetYesNo() != 0;
	const bool showSqz = sc.Input[IN_SHOW_SQZ].GetYesNo() != 0;
	const bool showDiv = sc.Input[IN_SHOW_DIV].GetYesNo() != 0;
	const bool showLevels = sc.Input[IN_SHOW_LEVELS].GetYesNo() != 0;
	const bool showHema = sc.Input[IN_SHOW_HEMA].GetYesNo() != 0;
	const bool showPlus = sc.Input[IN_SHOW_PLUS].GetYesNo() != 0;
	const bool showVodka = sc.Input[IN_SHOW_VODKA].GetYesNo() != 0;
	const bool show921 = sc.Input[IN_SHOW_921].GetYesNo() != 0;
	const bool showVI = sc.Input[IN_SHOW_VI].GetYesNo() != 0;
	const COLORREF bg = sc.Input[IN_BG_COLOR].GetColor();
	const COLORREF fg = sc.Input[IN_TEXT_COLOR].GetColor();
	const int sqzMin = sc.Input[IN_SQZ_MIN].GetInt();
	const bool sqzRelease = sc.Input[IN_SQZ_RELEASE].GetYesNo() != 0;
	const bool divLines = sc.Input[IN_DIV_LINES].GetYesNo() != 0;
	const int divTransp = sc.Input[IN_DIV_TRANSP].GetInt();
	const int divRsiLen = sc.Input[IN_DIV_RSI].GetInt();
	const int lbL = sc.Input[IN_DIV_LBL].GetInt();
	const int lbR = sc.Input[IN_DIV_LBR].GetInt();
	const int divMin = sc.Input[IN_DIV_MIN].GetInt();
	const int divMax = sc.Input[IN_DIV_MAX].GetInt();
	const int viExtend = sc.Input[IN_VI_EXTEND].GetInt();
	const int tpPartial = sc.Input[IN_TP_PARTIAL].GetInt();
	const int tpAll = sc.Input[IN_TP_ALL].GetInt();
	const bool quad921 = sc.Input[IN_QUAD_921].GetYesNo() != 0;
	const float waeSens = (float)sc.Input[IN_WAE_SENS].GetInt();
	const int waeFast = sc.Input[IN_WAE_FAST].GetInt();
	const int waeSlow = sc.Input[IN_WAE_SLOW].GetInt();
	const int waeChannel = sc.Input[IN_WAE_CHANNEL].GetInt();
	const float waeMult = sc.Input[IN_WAE_MULT].GetFloat();
	const int sqTol = sc.Input[IN_SQ_TOL].GetInt();
	const bool shark2575 = sc.Input[IN_SHARK_2575].GetYesNo() != 0;
	const float markerSpacing = sc.Input[IN_MARKER_SPACING].GetFloat();
	const bool alertsOn = sc.Input[IN_ALERTS_ON].GetYesNo() != 0;
	const int alertNumber = sc.Input[IN_ALERT_NUMBER].GetInt();

	const int sessStart[3] = { sc.Input[IN_OR_START].GetTime(), sc.Input[IN_LON_START].GetTime(), sc.Input[IN_ASIA_START].GetTime() };
	const int sessEnd[3] = { sc.Input[IN_OR_END].GetTime(), sc.Input[IN_LON_END].GetTime(), sc.Input[IN_ASIA_END].GetTime() };
	const int rthStart = sc.Input[IN_RTH_START].GetTime();
	const int rthEnd = sc.Input[IN_RTH_END].GetTime();

	const Col G = C(THEME_BULL[theme]);
	const Col R = C(THEME_BEAR[theme]);
	const Col cloudUp = palette == 0 ? G : C(CLOUD_UP[palette]);
	const Col cloudDn = palette == 0 ? R : C(CLOUD_DN[palette]);
	const Col grayC = C(GRAY);
	const Col strongSellRed = Darken(R, 0.6f);

	const int simpleT = sc.Input[IN_CLOUD_SIMPLE_T].GetInt();
	const int weakT = sc.Input[IN_CLOUD_WEAK_T].GetInt();
	const int strongT = sc.Input[IN_CLOUD_STRONG_T].GetInt();
	{
		const int fillT = cloudType == NCT_SIMPLE ? simpleT : strongT;
		TrySetTransparency(sc, fillT, 0);
		TrySetTransparency(sc.Subgraph[SG_CLOUD_TOP], fillT, 0);
		TrySetTransparency(sc.Subgraph[SG_CLOUD_BOTTOM], fillT, 0);
		TrySetTransparency(sc.Subgraph[SG_SQZ_TOP], fillT, 0);
		TrySetTransparency(sc.Subgraph[SG_SQZ_BOTTOM], fillT, 0);
	}

	const int ATR30 = 30;
	const float kernelH = 21.0f, kernelR = 8.0f;
	const int kernelX0 = 15;
	float kernelW[64];
	float kernelWSum = 0;
	for (int k = 0; k <= 1 + kernelX0; k++)
	{
		kernelW[k] = (float)std::pow(1.0 + (k * k) / (kernelH * kernelH * 2.0 * kernelR), -kernelR);
		kernelWSum += kernelW[k];
	}

	if (sc.UpdateStartIndex == 0)
	{
		imbalances->clear();
		lastImbalanceBar = -1;
		lastAlertBar = -1;
	}

	SCFloatArrayRef O = sc.Open;
	SCFloatArrayRef H = sc.High;
	SCFloatArrayRef L = sc.Low;
	SCFloatArrayRef Cl = sc.Close;
	SCFloatArrayRef V = sc.Volume;

	SCFloatArrayRef tr = Arr(sc, A_TR);
	SCFloatArrayRef atr14 = Arr(sc, A_ATR14);
	SCFloatArrayRef atr30 = Arr(sc, A_ATR30);
	SCFloatArrayRef atr10 = Arr(sc, A_ATR10);
	SCFloatArrayRef ema9 = Arr(sc, A_EMA9);
	SCFloatArrayRef ema21 = Arr(sc, A_EMA21);
	SCFloatArrayRef ema14 = Arr(sc, A_EMA14);
	SCFloatArrayRef pdm = Arr(sc, A_PDM);
	SCFloatArrayRef mdm = Arr(sc, A_MDM);
	SCFloatArrayRef diPlus = Arr(sc, A_DI_PLUS);
	SCFloatArrayRef diMinus = Arr(sc, A_DI_MINUS);
	SCFloatArrayRef adxIn = Arr(sc, A_ADX_IN);
	SCFloatArrayRef adx = Arr(sc, A_ADX);
	SCFloatArrayRef cvdBuy = Arr(sc, A_CVD_BUY);
	SCFloatArrayRef cvdSell = Arr(sc, A_CVD_SELL);
	SCFloatArrayRef cvd = Arr(sc, A_CVD);
	SCFloatArrayRef tdB = Arr(sc, A_TD_B);
	SCFloatArrayRef tdS = Arr(sc, A_TD_S);
	SCFloatArrayRef lux = Arr(sc, A_LUX);
	SCFloatArrayRef frUp = Arr(sc, A_FR_UP);
	SCFloatArrayRef frDn = Arr(sc, A_FR_DN);
	SCFloatArrayRef upClose = Arr(sc, A_UP_CLOSE);
	SCFloatArrayRef dnClose = Arr(sc, A_DN_CLOSE);
	SCFloatArrayRef early = Arr(sc, A_EARLY);
	SCFloatArrayRef rsiUp = Arr(sc, A_RSI_UP);
	SCFloatArrayRef rsiDn = Arr(sc, A_RSI_DN);
	SCFloatArrayRef rsi14 = Arr(sc, A_RSI14);
	SCFloatArrayRef fvSPDI = Arr(sc, A_FV_SPDI);
	SCFloatArrayRef fvSMDI = Arr(sc, A_FV_SMDI);
	SCFloatArrayRef fvSTR = Arr(sc, A_FV_STR);
	SCFloatArrayRef fvADX = Arr(sc, A_FV_ADX);
	SCFloatArrayRef fvVar = Arr(sc, A_FV_VAR);
	SCFloatArrayRef fanVMA = sc.Subgraph[SG_FANVMA].Data;
	SCFloatArrayRef mg = sc.Subgraph[SG_MCGINLEY].Data;
	SCFloatArrayRef emaFast = Arr(sc, A_EMA_FAST);
	SCFloatArrayRef emaSlow = Arr(sc, A_EMA_SLOW);
	SCFloatArrayRef macd = Arr(sc, A_MACD);
	SCFloatArrayRef t1 = Arr(sc, A_T1);
	SCFloatArrayRef e1 = Arr(sc, A_E1);
	SCFloatArrayRef t1s = Arr(sc, A_T1S);
	SCFloatArrayRef e1s = Arr(sc, A_E1S);
	SCFloatArrayRef trampDn = Arr(sc, A_TRAMP_DN);
	SCFloatArrayRef trampUp = Arr(sc, A_TRAMP_UP);
	SCFloatArrayRef goUp = Arr(sc, A_GO_UP);
	SCFloatArrayRef goDn = Arr(sc, A_GO_DN);
	SCFloatArrayRef tramp = Arr(sc, A_TRAMP);
	SCFloatArrayRef sqOn = Arr(sc, A_SQ_ON);
	SCFloatArrayRef sqSrc = Arr(sc, A_SQ_SRC);
	SCFloatArrayRef sqVal = Arr(sc, A_SQ_VAL);
	SCFloatArrayRef sqCR = Arr(sc, A_SQ_CR);
	SCFloatArrayRef sqCG = Arr(sc, A_SQ_CG);
	SCFloatArrayRef sqPos = Arr(sc, A_SQ_POS);
	SCFloatArrayRef sqNeg = Arr(sc, A_SQ_NEG);
	SCFloatArrayRef sqz = Arr(sc, A_SQZ);
	SCFloatArrayRef sqLen = Arr(sc, A_SQ_LEN);
	SCFloatArrayRef pvCode = Arr(sc, A_PV_CODE);
	SCFloatArrayRef shark = Arr(sc, A_SHARK);
	SCFloatArrayRef deadRev = Arr(sc, A_DEADREV);
	SCFloatArrayRef bands = Arr(sc, A_BANDS);
	SCFloatArrayRef rsiuUp = Arr(sc, A_RSIU_UP);
	SCFloatArrayRef rsiuDn = Arr(sc, A_RSIU_DN);
	SCFloatArrayRef rsiU = Arr(sc, A_RSIU);
	SCFloatArrayRef rsiuBasis = Arr(sc, A_RSIU_BASIS);
	SCFloatArrayRef rsiuUpper = Arr(sc, A_RSIU_UPPER);
	SCFloatArrayRef rsiuLower = Arr(sc, A_RSIU_LOWER);
	SCFloatArrayRef rsiuMa = Arr(sc, A_RSIU_MA);
	SCFloatArrayRef piUpper = Arr(sc, A_PI_UPPER);
	SCFloatArrayRef piLower = Arr(sc, A_PI_LOWER);
	SCFloatArrayRef atrbUpper = Arr(sc, A_ATRB_UPPER);
	SCFloatArrayRef atrbLower = Arr(sc, A_ATRB_LOWER);
	SCFloatArrayRef lastBuyWatch = Arr(sc, A_LAST_BUY_WATCH);
	SCFloatArrayRef lastSellWatch = Arr(sc, A_LAST_SELL_WATCH);
	SCFloatArrayRef plotBuy = Arr(sc, A_PLOT_BUY);
	SCFloatArrayRef plotSell = Arr(sc, A_PLOT_SELL);
	SCFloatArrayRef nzVol = Arr(sc, A_NZVOL);
	SCFloatArrayRef volAvgL = Arr(sc, A_VOL_AVGL);
	SCFloatArrayRef vola = Arr(sc, A_VOLA);
	SCFloatArrayRef derC = Arr(sc, A_DER_C);
	SCFloatArrayRef derCP = Arr(sc, A_DER_CP);
	SCFloatArrayRef derCM = Arr(sc, A_DER_CM);
	SCFloatArrayRef derSupN = Arr(sc, A_DER_SUPN);
	SCFloatArrayRef derDemN = Arr(sc, A_DER_DEMN);
	SCFloatArrayRef derBO = Arr(sc, A_DER_BO);
	SCFloatArrayRef derBC = Arr(sc, A_DER_BC);
	SCFloatArrayRef wma1 = Arr(sc, A_WMA1);
	SCFloatArrayRef wma2 = Arr(sc, A_WMA2);
	SCFloatArrayRef llw = Arr(sc, A_LLW);
	SCFloatArrayRef upw = Arr(sc, A_UPW);
	SCFloatArrayRef dnw = Arr(sc, A_DNW);
	SCFloatArrayRef kernel = Arr(sc, A_KERNEL);
	SCFloatArrayRef rsidUp = Arr(sc, A_RSID_UP);
	SCFloatArrayRef rsidDn = Arr(sc, A_RSID_DN);
	SCFloatArrayRef rsiD = Arr(sc, A_RSID);
	SCFloatArrayRef plFound = Arr(sc, A_PL_FOUND);
	SCFloatArrayRef plRsi = Arr(sc, A_PL_RSI);
	SCFloatArrayRef plLow = Arr(sc, A_PL_LOW);
	SCFloatArrayRef plBar = Arr(sc, A_PL_BAR);
	SCFloatArrayRef phFound = Arr(sc, A_PH_FOUND);
	SCFloatArrayRef phRsi = Arr(sc, A_PH_RSI);
	SCFloatArrayRef phHigh = Arr(sc, A_PH_HIGH);
	SCFloatArrayRef phBar = Arr(sc, A_PH_BAR);
	SCFloatArrayRef divbX1 = Arr(sc, A_DIVB_X1);
	SCFloatArrayRef divbY1 = Arr(sc, A_DIVB_Y1);
	SCFloatArrayRef divsX1 = Arr(sc, A_DIVS_X1);
	SCFloatArrayRef divsY1 = Arr(sc, A_DIVS_Y1);
	SCFloatArrayRef tpCount = Arr(sc, A_TP_COUNT);
	SCFloatArrayRef wave = Arr(sc, A_WAVE);
	SCFloatArrayRef brightG = Arr(sc, A_BRIGHT_G);
	SCFloatArrayRef brightR = Arr(sc, A_BRIGHT_R);
	SCFloatArrayRef sigWave = Arr(sc, A_SIG_WAVE);
	SCFloatArrayRef hemaB = Arr(sc, A_HEMA_B);
	SCFloatArrayRef hema = sc.Subgraph[SG_HEMA].Arrays[0];
	SCFloatArrayRef rsiHeat = Arr(sc, A_RSI_HEAT);
	SCFloatArrayRef kSlope = Arr(sc, A_KSLOPE);
	SCFloatArrayRef relVolS = Arr(sc, A_RELVOL_S);
	SCFloatArrayRef adxS = Arr(sc, A_ADX_S);
	SCFloatArrayRef haOpen = Arr(sc, A_HA_OPEN);
	SCFloatArrayRef zScore = Arr(sc, A_ZSCORE);
	SCFloatArrayRef stUpper = Arr(sc, A_ST_UPPER);
	SCFloatArrayRef stLower = Arr(sc, A_ST_LOWER);
	SCFloatArrayRef stDir = Arr(sc, A_ST_DIR);
	SCFloatArrayRef stVal = Arr(sc, A_ST_VAL);
	SCFloatArrayRef ccitp = Arr(sc, A_CCI_TP);
	SCFloatArrayRef ycVal = Arr(sc, A_YC_VAL);
	SCFloatArrayRef ycX = Arr(sc, A_YC_X);
	SCFloatArrayRef orbDir = Arr(sc, A_ORB_DIR);
	SCFloatArrayRef orbBar = Arr(sc, A_ORB_BAR);
	SCFloatArrayRef orbPostHi = Arr(sc, A_ORB_POSTHI);
	SCFloatArrayRef orbPostLo = Arr(sc, A_ORB_POSTLO);
	SCFloatArrayRef orDoneSize = Arr(sc, A_OR_DONE_SIZE);

	const int sBase[3] = { A_S0_HI, A_S1_HI, A_S2_HI };

	for (int i = sc.UpdateStartIndex; i < sc.ArraySize; i++)
	{
		const bool conf = sc.GetBarHasClosedStatus(i) == BHCS_BAR_HAS_CLOSED;
		const bool first = i == 0;
		const int p = first ? 0 : i - 1;
		const bool isGreen = Cl[i] > O[i];
		const bool isRed = Cl[i] < O[i];
		const bool bullBar = Cl[i] >= O[i];
		const float chg = first ? 0.0f : Cl[i] - Cl[p];

		tr[i] = first ? H[i] - L[i] : (std::max)(H[i] - L[i], (std::max)(std::fabs(H[i] - Cl[p]), std::fabs(L[i] - Cl[p])));
		RmaStep(atr14, tr[i], i, 14);
		RmaStep(atr30, tr[i], i, ATR30);
		RmaStep(atr10, tr[i], i, 10);
		EmaStep(ema9, Cl[i], i, 9);
		EmaStep(ema21, Cl[i], i, 21);
		EmaStep(ema14, Cl[i], i, 14);

		{
			float up = first ? 0.0f : H[i] - H[p];
			float dn = first ? 0.0f : L[p] - L[i];
			float plusDM = (up > dn && up > 0) ? up : 0.0f;
			float minusDM = (dn > up && dn > 0) ? dn : 0.0f;
			RmaStep(pdm, plusDM, i, 14);
			RmaStep(mdm, minusDM, i, 14);
			float trr = atr14[i];
			float pl = trr > 0 ? 100.0f * pdm[i] / trr : (first ? 0.0f : diPlus[p]);
			float mi = trr > 0 ? 100.0f * mdm[i] / trr : (first ? 0.0f : diMinus[p]);
			diPlus[i] = pl;
			diMinus[i] = mi;
			float sum = pl + mi;
			adxIn[i] = std::fabs(pl - mi) / (sum == 0 ? 1.0f : sum);
			RmaStep(adx, adxIn[i], i, 14);
		}
		const float adxVal = 100.0f * adx[i];
		const bool sigAbove = adxVal > 0.0f;

		{
			float upperWick = isGreen ? H[i] - Cl[i] : H[i] - O[i];
			float lowerWick = isGreen ? O[i] - L[i] : Cl[i] - L[i];
			float spread = H[i] - L[i];
			float pu = spread > 0 ? upperWick / spread : 0.0f;
			float pl = spread > 0 ? lowerWick / spread : 0.0f;
			float pb = spread > 0 ? (spread - (upperWick + lowerWick)) / spread : 0.0f;
			float ws = (pu + pl) / 2.0f;
			float buyV = isGreen ? (pb + ws) * V[i] : ws * V[i];
			float sellV = isRed ? (pb + ws) * V[i] : ws * V[i];
			EmaStep(cvdBuy, buyV, i, 14);
			EmaStep(cvdSell, sellV, i, 14);
			cvd[i] = cvdBuy[i] - cvdSell[i];
		}

		{
			float ll3 = Lowest(L, i, 3);
			float hh3 = Highest(H, i, 3);
			bool lowBreak = false, highBreak = false;
			for (int k = 1; k <= 3 && i - k >= 0; k++)
			{
				if (ll3 < Lowest(L, i - k, 50))
					lowBreak = true;
				if (hh3 > Highest(H, i - k, 50))
					highBreak = true;
			}
			bool buyDSR = !first && Cl[p] < O[p] && isGreen && Cl[i] > O[p] && lowBreak;
			bool sellDSR = !first && Cl[p] > O[p] && isRed && Cl[i] < O[p] && highBreak;
			deadRev[i] = (buyDSR || sellDSR) ? 1.0f : 0.0f;
		}

		{
			float b1 = first ? 0.0f : tdB[p];
			float s1 = first ? 0.0f : tdS[p];
			float b, s;
			if (i >= 4 && Cl[i] < Cl[i - 4])
			{
				b = b1 == 9 ? 1.0f : b1 + 1.0f;
				s = 0.0f;
			}
			else
			{
				s = s1 == 9 ? 1.0f : s1 + 1.0f;
				b = 0.0f;
			}
			tdB[i] = b;
			tdS[i] = s;
			lux[i] = ((b == 9 || (b1 == 8 && s == 1) || s == 9 || (s1 == 8 && b == 1)) && conf) ? 1.0f : 0.0f;
		}

		{
			frUp[i] = first ? 0.0f : frUp[p];
			frDn[i] = first ? 0.0f : frDn[p];
			upClose[i] = first ? 0.0f : upClose[p];
			dnClose[i] = first ? 0.0f : dnClose[p];
			if (i >= 5)
			{
				auto sgn = [](float x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); };
				int dh = sgn(H[i] - H[i - 1]) + sgn(H[i - 1] - H[i - 2]);
				int dh2 = sgn(H[i - 2] - H[i - 3]) + sgn(H[i - 3] - H[i - 4]);
				int dl = sgn(L[i] - L[i - 1]) + sgn(L[i - 1] - L[i - 2]);
				int dl2 = sgn(L[i - 2] - L[i - 3]) + sgn(L[i - 3] - L[i - 4]);
				if (dh == -2 && dh2 == 2 && H[i - 2] == Highest(H, i, 5))
					frUp[i] = H[i - 2];
				if (dl == 2 && dl2 == -2 && L[i - 2] == Lowest(L, i, 5))
					frDn[i] = L[i - 2];
			}
			if (!first && frUp[i] != 0 && CrossOver(Cl[i], Cl[p], frUp[i], frUp[p]))
				upClose[i] = Cl[i];
			if (!first && frDn[i] != 0 && CrossUnder(Cl[i], Cl[p], frDn[i], frDn[p]))
				dnClose[i] = Cl[i];
		}

		RmaStep(rsiUp, (std::max)(chg, 0.0f), i, 14);
		RmaStep(rsiDn, -(std::min)(chg, 0.0f), i, 14);
		rsi14[i] = rsiDn[i] == 0 ? 100.0f : (rsiUp[i] == 0 ? 0.0f : 100.0f - 100.0f / (1.0f + rsiUp[i] / rsiDn[i]));
		const float rsiM = rsi14[i];

		{
			const float W = 10.0f;
			float bulls1 = first ? 0.0f : 0.5f * (std::fabs(H[i] - H[p]) + (H[i] - H[p]));
			float bears1 = first ? 0.0f : 0.5f * (std::fabs(L[p] - L[i]) + (L[p] - L[i]));
			float bears = bulls1 >= bears1 ? 0.0f : bears1;
			float bulls = bulls1 <= bears1 ? 0.0f : bulls1;
			float trf = first ? H[i] - L[i] : (std::max)(H[i] - L[i], H[i] - Cl[p]);
			if (first)
			{
				fvSPDI[i] = 0;
				fvSMDI[i] = 0;
				fvSTR[i] = H[i] - L[i];
				fvADX[i] = 0;
				fvVar[i] = Cl[i];
			}
			else
			{
				fvSPDI[i] = (W * fvSPDI[p] + bulls) / (W + 1);
				fvSMDI[i] = (W * fvSMDI[p] + bears) / (W + 1);
				fvSTR[i] = (W * fvSTR[p] + trf) / (W + 1);
			}
			float pdi = fvSTR[i] > 0 ? fvSPDI[i] / fvSTR[i] : 0.0f;
			float mdi = fvSTR[i] > 0 ? fvSMDI[i] / fvSTR[i] : 0.0f;
			float dx = (pdi + mdi) > 0 ? std::fabs(pdi - mdi) / (pdi + mdi) : 0.0f;
			if (!first)
				fvADX[i] = (W * fvADX[p] + dx) / (W + 1);
			float adxMin = (std::min)(1000000.0f, Lowest(fvADX, i, 2));
			float adxMax = (std::max)(-1.0f, Highest(fvADX, i, 2));
			float diff = adxMax - adxMin;
			float cnst = diff > 0 ? (fvADX[i] - adxMin) / diff : 0.0f;
			if (!first)
				fvVar[i] = ((2 - cnst) * fvVar[p] + cnst * Cl[i]) / 2;
			fanVMA[i] = Sma(fvVar, i, 6);
			if (first || !Ok(mg[p]) || mg[p] == 0)
				mg[i] = ema14[i];
			else
				mg[i] = mg[p] + (Cl[i] - mg[p]) / (14.0f * (float)std::pow(Cl[i] / mg[p], 4));
		}
		const bool fanUp = fanVMA[i] > mg[i];

		EmaStep(emaFast, Cl[i], i, waeFast);
		EmaStep(emaSlow, Cl[i], i, waeSlow);
		macd[i] = emaFast[i] - emaSlow[i];
		t1[i] = first ? 0.0f : (macd[i] - macd[p]) * waeSens;
		e1[i] = 2.0f * waeMult * Stdev(Cl, i, waeChannel);
		EmaStep(t1s, t1[i], i, 3);
		EmaStep(e1s, e1[i], i, 3);

		const float basisBB = Sma(Cl, i, 20);
		const float sdBB = Stdev(Cl, i, 20);
		const float upperBB = basisBB + 2.0f * sdBB;
		const float lowerBB = basisBB - 2.0f * sdBB;
		const float bbw = basisBB != 0 ? (upperBB - lowerBB) / basisBB : 0.0f;

		trampDn[i] = (isRed && rsiM <= 25 && Cl[i] < lowerBB && bbw > 0.0015f) ? 1.0f : 0.0f;
		trampUp[i] = (isGreen && rsiM >= 72 && Cl[i] > upperBB && bbw > 0.0015f) ? 1.0f : 0.0f;
		{
			bool anyDn = false, anyUp = false;
			for (int k = 1; k <= 5 && i - k >= 0; k++)
			{
				anyDn = anyDn || trampDn[i - k] != 0;
				anyUp = anyUp || trampUp[i - k] != 0;
			}
			goUp[i] = (isGreen && anyDn && !first && H[i] > H[p] && conf) ? 1.0f : 0.0f;
			goDn[i] = (isRed && anyUp && !first && L[i] < L[p] && conf) ? 1.0f : 0.0f;
			bool upThrust = goUp[i] != 0, dnThrust = goDn[i] != 0;
			for (int k = 1; k <= 4 && i - k >= 0; k++)
			{
				if (goUp[i - k] != 0)
					upThrust = false;
				if (goDn[i - k] != 0)
					dnThrust = false;
			}
			tramp[i] = (upThrust || dnThrust) ? 1.0f : 0.0f;
		}

		const float sqBasis = basisBB;
		const float sqDev = 1.5f * sdBB;
		const float upperBBsq = sqBasis + sqDev;
		const float lowerBBsq = sqBasis - sqDev;
		{
			float rangeSum = 0;
			int n = (std::min)(20, i + 1);
			for (int k = 0; k < n; k++)
				rangeSum += H[i - k] - L[i - k];
			float rangema = rangeSum / n;
			float upperKC = sqBasis + rangema * 1.5f;
			float lowerKC = sqBasis - rangema * 1.5f;
			sqOn[i] = (lowerBBsq > lowerKC && upperBBsq < upperKC) ? 1.0f : 0.0f;
			float avg1 = (Highest(H, i, 20) + Lowest(L, i, 20)) / 2.0f;
			float avg2 = (avg1 + sqBasis) / 2.0f;
			sqSrc[i] = Cl[i] - avg2;
			sqVal[i] = Linreg(sqSrc, i, 20);
		}
		const float val = sqVal[i];
		const float valPrev = first ? 0.0f : sqVal[p];
		const bool squeezeOn = sqOn[i] != 0;
		{
			float cr = first ? 0.0f : sqCR[p];
			float cg = first ? 0.0f : sqCG[p];
			bool pos = false, neg = false;
			if (val < valPrev && val < 5 && !squeezeOn)
				cr += 1;
			if (val > valPrev && val > 5 && !squeezeOn)
				cg += 1;
			if (val > valPrev && cr > sqTol && val < 5 && !(first ? false : sqPos[p] != 0) && sigAbove)
			{
				cr = 0;
				pos = true;
			}
			if (val < valPrev && cg > sqTol && val > 5 && !(first ? false : sqNeg[p] != 0) && sigAbove)
			{
				cg = 0;
				neg = true;
			}
			sqCR[i] = cr;
			sqCG[i] = cg;
			sqPos[i] = pos ? 1.0f : 0.0f;
			sqNeg[i] = neg ? 1.0f : 0.0f;
			sqz[i] = ((pos || neg) && conf) ? 1.0f : 0.0f;
		}

		{
			double avgVol = 0;
			float highestVS = 0;
			int n = 0;
			for (int k = 1; k <= 10 && i - k >= 0; k++)
			{
				avgVol += V[i - k];
				highestVS = (std::max)(highestVS, V[i - k] * (H[i - k] - L[i - k]));
				n++;
			}
			avgVol = n > 0 ? avgVol / n : 0;
			float vs = V[i] * (H[i] - L[i]);
			int code = 0;
			if (n > 0 && (V[i] >= 2.0 * avgVol || vs >= highestVS))
				code = isGreen ? 1 : 2;
			else if (n > 0 && V[i] >= 1.5 * avgVol)
				code = isGreen ? 3 : 4;
			else
				code = isGreen ? 5 : 6;
			pvCode[i] = (float)code;
		}

		{
			bool gSig = false, rSig = false;
			for (int k = 0; k <= 3 && i - k >= 0; k++)
			{
				if (upClose[i - k] != 0 && Cl[i - k] == upClose[i - k])
					gSig = true;
				if (dnClose[i - k] != 0 && Cl[i - k] == dnClose[i - k])
					rSig = true;
			}
			early[i] = ((pvCode[i] == 1 && gSig) || (pvCode[i] == 2 && rSig)) ? 1.0f : 0.0f;
		}

		{
			float basis5 = Sma(rsi14, i, 30);
			float dev5 = 2.0f * Stdev(rsi14, i, 30);
			bool below = !shark2575 || rsiM < 26;
			bool above = !shark2575 || rsiM > 74;
			shark[i] = (((rsiM < basis5 - dev5 && below) || (rsiM > basis5 + dev5 && above)) && conf) ? 1.0f : 0.0f;
		}

		{
			float wb = basisBB;
			float wu = wb + 2.5f * sdBB;
			float wl = wb - 2.5f * sdBB;
			bands[i] = ((L[i] <= wl && Cl[i] >= wl && isRed) || (H[i] >= wu && Cl[i] < wu && isGreen)) ? 1.0f : 0.0f;
		}

		{
			float atrMa = Wma(Cl, i, 10);
			atrbUpper[i] = atrMa + atr30[i] * 1.5f;
			atrbLower[i] = atrMa - atr30[i] * 1.5f;
			RmaStep(rsiuUp, (std::max)(chg, 0.0f), i, 32);
			RmaStep(rsiuDn, -(std::min)(chg, 0.0f), i, 32);
			rsiU[i] = rsiuDn[i] == 0 ? 100.0f : (rsiuUp[i] == 0 ? 0.0f : 100.0f - 100.0f / (1.0f + rsiuUp[i] / rsiuDn[i]));
			rsiuBasis[i] = Wma(rsiU, i, 32);
			float rdev = Stdev(rsiU, i, 32);
			rsiuUpper[i] = rsiuBasis[i] + 2.0f * rdev;
			rsiuLower[i] = rsiuBasis[i] - 2.0f * rdev;
			rsiuMa[i] = Wma(rsiU, i, 24);
			piUpper[i] = basisBB + 2.0f * sdBB;
			piLower[i] = basisBB - 2.0f * sdBB;

			bool buyWatched = false, sellWatched = false, buySig = false, sellSig = false;
			if (!first)
			{
				bool pOver = CrossOver(Cl[i], Cl[p], piLower[i], piLower[p]);
				bool pUnder = CrossUnder(Cl[i], Cl[p], piUpper[i], piUpper[p]);
				bool rOverL = CrossOver(rsiU[i], rsiU[p], rsiuLower[i], rsiuLower[p]);
				bool rUnderU = CrossUnder(rsiU[i], rsiU[p], rsiuUpper[i], rsiuUpper[p]);
				bool rOverB = CrossOver(rsiU[i], rsiU[p], rsiuBasis[i], rsiuBasis[p]);
				bool rUnderB = CrossUnder(rsiU[i], rsiU[p], rsiuBasis[i], rsiuBasis[p]);
				bool rOverMa = CrossOver(rsiU[i], rsiU[p], rsiuMa[i], rsiuMa[p]);
				bool rUnderMa = CrossUnder(rsiU[i], rsiU[p], rsiuMa[i], rsiuMa[p]);
				bool rUnder75 = CrossUnder(rsiU[i], rsiU[p], 75.0f, 75.0f);
				bool rOver25 = CrossOver(rsiU[i], rsiU[p], 25.0f, 25.0f);
				bool hUnderAtr = CrossUnder(H[i], H[p], atrbLower[i], atrbLower[p]);
				bool lOverAtr = CrossOver(L[i], L[p], atrbUpper[i], atrbUpper[p]);
				buyWatched = conf && (pOver || rOverL || rOver25 || hUnderAtr);
				sellWatched = conf && (pUnder || rUnderU || rUnder75 || lOverAtr);
				float lb = lastBuyWatch[p], ls = lastSellWatch[p];
				bool buyValid = lb >= 0 && (i - lb) <= 34;
				bool sellValid = ls >= 0 && (i - ls) <= 34;
				buySig = conf && !buyWatched && buyValid && (rOverB || rOver25 || rOverMa);
				sellSig = !buySig && conf && !sellWatched && sellValid && (rUnderB || rUnder75 || rUnderMa);
				lastBuyWatch[i] = buyWatched ? (float)i : lb;
				lastSellWatch[i] = sellWatched ? (float)i : ls;
				if (buySig || sellSig)
				{
					lastBuyWatch[i] = -1;
					lastSellWatch[i] = -1;
				}
			}
			else
			{
				lastBuyWatch[i] = -1;
				lastSellWatch[i] = -1;
			}
			plotBuy[i] = buySig ? 1.0f : 0.0f;
			plotSell[i] = sellSig ? 1.0f : 0.0f;
		}

		{
			nzVol[i] = Nz(V[i]);
			volAvgL[i] = Sma(nzVol, i, 70);
			float vsd = Stdev(volAvgL, i, 70);
			float volDev = volAvgL[i] > 0 ? (volAvgL[i] + 1.618034f * vsd) / volAvgL[i] * 11.0f / 100.0f : 0.0f;
			float volRel = volAvgL[i] > 0 ? nzVol[i] / volAvgL[i] : 1.0f;
			bool volSpike = volRel * 0.145898f > volDev;

			float vHi = Highest(V, i, 20), vLo = Lowest(V, i, 20);
			vola[i] = vHi > vLo ? (V[i] - vLo) / (vHi - vLo) : 0.0f;
			float Rr = (Highest(H, i, 2) - Lowest(L, i, 2)) / 2.0f;
			float rsr = Rr > 0 ? Clamp(chg / Rr, -1.0f, 1.0f) : 0.0f;
			derC[i] = rsr * vola[i];
			derCP[i] = (std::max)(derC[i], 0.0f);
			derCM[i] = -(std::min)(derC[i], 0.0f);
			float avgVola = Wma(vola, i, 12);
			derDemN[i] = avgVola > 0 ? Wma(derCP, i, 12) / avgVola : 0.0f;
			derSupN[i] = avgVola > 0 ? Wma(derCM, i, 12) / avgVola : 0.0f;
			derBO[i] = 100.0f * Wma(derSupN, i, 5);
			derBC[i] = 100.0f * Wma(derDemN, i, 5);
			bool rising = !first && derBC[i] - derBC[p] > 0;
			bool vaderUp = derBC[i] > derBO[i] && rising;
			bool vaderDn = derBC[i] < derBO[i] && !rising;

			wma1[i] = Wma(Cl, i, 7);
			wma2[i] = Wma(wma1, i, 7);
			llw[i] = Wma(wma2, i, 7);
			bool upwards = !first && llw[i] > llw[p] && vaderUp && rsiuUp[i] > 0 && isGreen && volSpike;
			bool downwards = !first && llw[i] < llw[p] && vaderDn && isRed && volSpike;
			upw[i] = upwards ? 1.0f : 0.0f;
			dnw[i] = downwards ? 1.0f : 0.0f;
			EmaStep(relVolS, volRel, i, 3);
		}

		{
			float ks = 0;
			for (int k = 0; k <= 1 + kernelX0; k++)
				ks += Cl[(std::max)(0, i - k)] * kernelW[k];
			kernel[i] = ks / kernelWSum;
		}

		RmaStep(rsidUp, (std::max)(chg, 0.0f), i, divRsiLen);
		RmaStep(rsidDn, -(std::min)(chg, 0.0f), i, divRsiLen);
		rsiD[i] = rsidDn[i] == 0 ? 100.0f : (rsidUp[i] == 0 ? 0.0f : 100.0f - 100.0f / (1.0f + rsidUp[i] / rsidDn[i]));

		{
			plFound[i] = first ? -1.0f : plFound[p];
			plRsi[i] = first ? 0.0f : plRsi[p];
			plLow[i] = first ? 0.0f : plLow[p];
			plBar[i] = first ? -1.0f : plBar[p];
			phFound[i] = first ? -1.0f : phFound[p];
			phRsi[i] = first ? 0.0f : phRsi[p];
			phHigh[i] = first ? 0.0f : phHigh[p];
			phBar[i] = first ? -1.0f : phBar[p];
			int j = i - lbR;
			bool pl = false, ph = false;
			if (j - lbL >= 0)
			{
				pl = true;
				ph = true;
				for (int k = j - lbL; k <= i; k++)
				{
					if (k == j)
						continue;
					if (!(rsiD[j] < rsiD[k]))
						pl = false;
					if (!(rsiD[j] > rsiD[k]))
						ph = false;
				}
			}
			bool bullDiv = false, bearDiv = false;
			if (j >= 0)
			{
				sc.Subgraph[SG_DIV_BULL][j] = 0;
				sc.Subgraph[SG_DIV_BEAR][j] = 0;
				divbX1[j] = -1;
				divsX1[j] = -1;
			}
			if (pl)
			{
				float prevFound = first ? -1.0f : plFound[p];
				int since = prevFound >= 0 ? (i - 1) - (int)prevFound : -1;
				bool inRange = since >= divMin && since <= divMax;
				if (showDiv && inRange && rsiD[j] > plRsi[i] && L[j] < plLow[i])
				{
					bullDiv = true;
					divbX1[j] = plBar[i];
					divbY1[j] = plLow[i];
				}
				plFound[i] = (float)i;
				plRsi[i] = rsiD[j];
				plLow[i] = L[j];
				plBar[i] = (float)j;
			}
			if (ph)
			{
				float prevFound = first ? -1.0f : phFound[p];
				int since = prevFound >= 0 ? (i - 1) - (int)prevFound : -1;
				bool inRange = since >= divMin && since <= divMax;
				if (showDiv && inRange && rsiD[j] < phRsi[i] && H[j] > phHigh[i])
				{
					bearDiv = true;
					divsX1[j] = phBar[i];
					divsY1[j] = phHigh[i];
				}
				phFound[i] = (float)i;
				phRsi[i] = rsiD[j];
				phHigh[i] = H[j];
				phBar[i] = (float)j;
			}
			if (bullDiv)
			{
				float offj = markerSpacing * atr14[j];
				sc.Subgraph[SG_DIV_BULL][j] = L[j] - 2.0f * offj;
				sc.Subgraph[SG_DIV_BULL].DataColor[j] = Ref(T(G, (float)divTransp), bg);
			}
			if (bearDiv)
			{
				float offj = markerSpacing * atr14[j];
				sc.Subgraph[SG_DIV_BEAR][j] = H[j] + 2.0f * offj;
				sc.Subgraph[SG_DIV_BEAR].DataColor[j] = Ref(T(R, (float)divTransp), bg);
			}
		}

		{
			auto on = [&](SCFloatArrayRef a) { return a[i] != 0 || (!first && a[p] != 0); };
			int count = (on(deadRev) ? 2 : 0) + (on(lux) ? 3 : 0) + (on(tramp) ? 4 : 0) + (on(sqz) ? 4 : 0) + (on(early) ? 2 : 0) + (on(shark) ? 2 : 0) + (on(bands) ? 2 : 0);
			tpCount[i] = (float)count;
		}

		int ws = first ? 0 : (int)wave[p];
		bool noOverlapGreen = false, noOverlapRed = false, bG = false, bR = false, gapGreen = false, gapRed = false;
		if (isGreen && conf)
		{
			for (int k = 1; k <= 200 && i - k >= 0; k++)
			{
				int q = i - k;
				if (brightR[q] != 0)
					break;
				else if (ws == 1 && Cl[q] < O[q])
					break;
				else if (ws == 2 && O[i] >= Cl[q] && Cl[q] > O[q])
				{
					noOverlapGreen = true;
					bG = true;
					ws = 1;
					break;
				}
			}
			if (!first && O[i] >= Cl[p] && Cl[p] > O[p])
			{
				if (showVI && O[i] > Cl[p] && i > lastImbalanceBar)
				{
					imbalances->push_back(Imbalance{ i, O[i], Cl[p], 1 });
					lastImbalanceBar = i;
				}
				ws = 1;
				bG = true;
				gapGreen = true;
			}
		}
		if (isRed && conf)
		{
			for (int k = 1; k <= 200 && i - k >= 0; k++)
			{
				int q = i - k;
				if (brightG[q] != 0)
					break;
				else if (ws == 2 && Cl[q] > O[q])
					break;
				else if (ws == 1 && O[i] <= Cl[q] && Cl[q] < O[q])
				{
					noOverlapRed = true;
					bR = true;
					ws = 2;
					break;
				}
			}
			if (!first && Cl[p] < O[p] && O[i] < Cl[p])
			{
				if (showVI && i > lastImbalanceBar)
				{
					imbalances->push_back(Imbalance{ i, O[i], Cl[p], -1 });
					lastImbalanceBar = i;
				}
				ws = 2;
				bR = true;
				gapRed = true;
			}
		}
		wave[i] = (float)ws;
		brightG[i] = bG ? 1.0f : 0.0f;
		brightR[i] = bR ? 1.0f : 0.0f;

		if (!imbalances->empty())
		{
			for (int k = (int)imbalances->size() - 1; k >= 0; k--)
			{
				const Imbalance& im = (*imbalances)[k];
				if (im.Bar >= i)
					continue;
				bool filled = im.Dir == 1 ? L[i] <= im.Fill : H[i] >= im.Fill;
				if (filled)
					imbalances->erase(imbalances->begin() + k);
			}
			if ((int)imbalances->size() > MAX_IMBALANCES)
				imbalances->erase(imbalances->begin(), imbalances->begin() + ((int)imbalances->size() - MAX_IMBALANCES));
		}

		const int wsPrev = first ? 0 : (int)wave[p];
		bool bigBuy = false, bigSell = false;
		for (int k = 0; k <= 3 && i - k >= 0; k++)
		{
			bigBuy = bigBuy || plotBuy[i - k] != 0;
			bigSell = bigSell || plotSell[i - k] != 0;
		}
		const bool waveBuy = noOverlapGreen && wsPrev == 2;
		const bool waveSell = noOverlapRed && wsPrev == 1;
		const bool buyBasic = !bigBuy && waveBuy;
		const bool sellBasic = !bigSell && waveSell;
		const bool buySuper = bigBuy && waveBuy;
		const bool sellSuper = bigSell && waveSell;
		const bool plusBuy = gapGreen && wsPrev == 1;
		const bool plusSell = gapRed && wsPrev == 2;
		bool pBuyVodka = upw[i] != 0, pSellVodka = dnw[i] != 0;
		for (int k = 1; k <= 4 && i - k >= 0; k++)
		{
			if (upw[i - k] != 0)
			{
				pBuyVodka = false;
				if (k >= 2)
					pSellVodka = false;
			}
			if (k == 1 && dnw[i - k] != 0)
				pSellVodka = false;
		}
		const bool vodkaBuy = pBuyVodka && wsPrev == 1;
		const bool vodkaSell = pSellVodka && wsPrev == 2;
		const int tpc = (int)tpCount[i];
		const bool tpAllHit = tpc >= tpAll && ws != 0;
		const bool tpSomeHit = tpc >= tpPartial && !tpAllHit && ws != 0;
		sigWave[i] = (buyBasic || sellBasic || buySuper || sellSuper || plusBuy || plusSell || vodkaBuy || vodkaSell) ? 1.0f : 0.0f;

		{
			const float alpha = 2.0f / 21.0f, gamma = 2.0f / 21.0f;
			float hPrev = first ? 0.0f : hema[p];
			float bPrev = first ? Cl[i] : hemaB[p];
			hema[i] = (1 - alpha) * (hPrev + bPrev) + alpha * Cl[i];
			if (first)
				hema[i] = Cl[i];
			hemaB[i] = first ? 0.0f : (1 - gamma) * hemaB[p] + gamma * (hema[i] - hPrev);
			sc.Subgraph[SG_HEMA][i] = showHema ? hema[i] : 0.0f;
		}

		bool crossUp = false, crossDown = false;
		if (!first && show921)
		{
			float s0 = quad921 ? kernel[i] : ema21[i];
			float s1 = quad921 ? kernel[p] : ema21[p];
			crossUp = CrossOver(ema9[i], ema9[p], s0, s1);
			crossDown = CrossUnder(ema9[i], ema9[p], s0, s1);
		}

		{
			float src = cloudType == NCT_RSI ? rsiM : 0.0f;
			if (cloudType == NCT_MFI)
			{
				double posF = 0, negF = 0;
				for (int k = 0; k < 14 && i - k - 1 >= 0; k++)
				{
					int q = i - k;
					float tp0 = (H[q] + L[q] + Cl[q]) / 3.0f;
					float tp1 = (H[q - 1] + L[q - 1] + Cl[q - 1]) / 3.0f;
					if (tp0 > tp1)
						posF += tp0 * V[q];
					else if (tp0 < tp1)
						negF += tp0 * V[q];
				}
				src = negF == 0 ? 100.0f : (float)(100.0 - 100.0 / (1.0 + posF / negF));
			}
			else if (cloudType == NCT_CCI)
			{
				ccitp[i] = (H[i] + L[i] + Cl[i]) / 3.0f;
				float m = Sma(ccitp, i, 20);
				double md = 0;
				int n = (std::min)(20, i + 1);
				for (int k = 0; k < n; k++)
					md += std::fabs(ccitp[i - k] - m);
				md /= n;
				src = md > 0 ? (float)((ccitp[i] - m) / (0.015 * md)) : 0.0f;
			}
			bool osc = cloudType == NCT_RSI || cloudType == NCT_MFI || cloudType == NCT_CCI;
			bool cciT = cloudType == NCT_CCI;
			float minC = osc ? 20.0f : 0.0f;
			float midC = cciT ? -100.0f : (osc ? 50.0f : 0.0f);
			float maxC = cciT ? 100.0f : (osc ? 80.0f : 0.0f);
			if (cciT)
				minC = -300.0f;
			COLORREF cc;
			if (cloudType == NCT_SIMPLE)
				cc = Ref(fanUp ? cloudUp : cloudDn, bg);
			else
			{
				float extra = (float)(weakT - strongT) / (std::max)(1.0f, (float)(100 - strongT)) * 100.0f;
				Col c = fanUp ? Grad(src, midC, maxC, T(cloudUp, extra), T(cloudUp, 0)) : Grad(src, minC, midC, T(cloudDn, extra), T(cloudDn, 0));
				cc = Ref(c, bg);
			}
			if (cloudType != NCT_NONE)
			{
				sc.Subgraph[SG_CLOUD_TOP][i] = (std::max)(fanVMA[i], mg[i]);
				sc.Subgraph[SG_CLOUD_BOTTOM][i] = (std::min)(fanVMA[i], mg[i]);
				sc.Subgraph[SG_CLOUD_TOP].DataColor[i] = cc;
				sc.Subgraph[SG_CLOUD_BOTTOM].DataColor[i] = cc;
			}
			else
			{
				sc.Subgraph[SG_CLOUD_TOP][i] = 0;
				sc.Subgraph[SG_CLOUD_BOTTOM][i] = 0;
			}
		}

		{
			sqLen[i] = squeezeOn ? (first ? 1.0f : sqLen[p] + 1.0f) : 0.0f;
			const COLORREF zone = sc.Input[IN_SQZ_COLOR].GetColor();
			const COLORREF edge = Ref(T(C(zone), 45), bg);
			auto setZone = [&](int q, bool onz)
			{
				sc.Subgraph[SG_SQZ_TOP][q] = onz ? sc.Subgraph[SG_SQZ_TOP].Arrays[0][q] : 0.0f;
				sc.Subgraph[SG_SQZ_BOTTOM][q] = onz ? sc.Subgraph[SG_SQZ_BOTTOM].Arrays[0][q] : 0.0f;
				sc.Subgraph[SG_SQZ_EDGE_TOP][q] = onz ? sc.Subgraph[SG_SQZ_TOP].Arrays[0][q] : 0.0f;
				sc.Subgraph[SG_SQZ_EDGE_BOTTOM][q] = onz ? sc.Subgraph[SG_SQZ_BOTTOM].Arrays[0][q] : 0.0f;
				sc.Subgraph[SG_SQZ_TOP].DataColor[q] = zone;
				sc.Subgraph[SG_SQZ_BOTTOM].DataColor[q] = zone;
				sc.Subgraph[SG_SQZ_EDGE_TOP].DataColor[q] = edge;
				sc.Subgraph[SG_SQZ_EDGE_BOTTOM].DataColor[q] = edge;
			};
			sc.Subgraph[SG_SQZ_TOP].Arrays[0][i] = upperBBsq;
			sc.Subgraph[SG_SQZ_BOTTOM].Arrays[0][i] = lowerBBsq;
			int len = (int)sqLen[i];
			bool drawNow = showSqz && squeezeOn && len >= sqzMin;
			if (drawNow && len == sqzMin)
			{
				for (int k = 1; k < len && i - k >= 0; k++)
					setZone(i - k, true);
			}
			setZone(i, drawNow);
		}

		const float off = markerSpacing * atr14[i];
		float below = L[i] - off;
		float above = H[i] + off;
		auto clearSg = [&](int s) { sc.Subgraph[s][i] = 0; };
		clearSg(SG_BUY);
		clearSg(SG_SELL);
		clearSg(SG_STRONG_BUY);
		clearSg(SG_STRONG_SELL);
		clearSg(SG_ADD_LONG);
		clearSg(SG_ADD_SHORT);
		clearSg(SG_VODKA_LONG);
		clearSg(SG_VODKA_SHORT);
		clearSg(SG_TP_ALL);
		clearSg(SG_TP_PARTIAL);
		clearSg(SG_CROSS_UP);
		clearSg(SG_CROSS_DOWN);
		clearSg(SG_SQZ_REL_UP);
		clearSg(SG_SQZ_REL_DOWN);
		clearSg(SG_ORB_UP);
		clearSg(SG_ORB_DOWN);
		auto placeBelow = [&](int s, COLORREF c) { sc.Subgraph[s][i] = below; sc.Subgraph[s].DataColor[i] = c; below -= off; };
		auto placeAbove = [&](int s, COLORREF c) { sc.Subgraph[s][i] = above; sc.Subgraph[s].DataColor[i] = c; above += off; };

		if (buyBasic)
			placeBelow(SG_BUY, Ref(G, bg));
		if (sellBasic)
			placeAbove(SG_SELL, Ref(R, bg));
		if (buySuper)
			placeBelow(SG_STRONG_BUY, Ref(G, bg));
		if (sellSuper)
			placeAbove(SG_STRONG_SELL, Ref(strongSellRed, bg));
		if (showPlus && plusBuy)
			placeBelow(SG_ADD_LONG, Ref(G, bg));
		if (showPlus && plusSell)
			placeAbove(SG_ADD_SHORT, Ref(R, bg));
		if (showVodka && vodkaBuy)
			placeBelow(SG_VODKA_LONG, Ref(G, bg));
		if (showVodka && vodkaSell)
			placeAbove(SG_VODKA_SHORT, Ref(R, bg));
		if (tpAllHit)
		{
			if (ws == 1)
				placeAbove(SG_TP_ALL, PINE_RED);
			else
				placeBelow(SG_TP_ALL, PINE_RED);
		}
		if (tpSomeHit)
		{
			if (ws == 1)
				placeAbove(SG_TP_PARTIAL, PINE_PURPLE);
			else
				placeBelow(SG_TP_PARTIAL, PINE_PURPLE);
		}
		if (crossUp)
			placeBelow(SG_CROSS_UP, Ref(T(C(YELLOW), 20), bg));
		if (crossDown)
			placeAbove(SG_CROSS_DOWN, Ref(T(C(YELLOW), 20), bg));

		if (sqzRelease && showSqz && !first && !squeezeOn && sqOn[p] != 0 && (int)sqLen[p] >= sqzMin)
		{
			if (val > 0)
				placeAbove(SG_SQZ_REL_UP, Ref(G, bg));
			else
				placeBelow(SG_SQZ_REL_DOWN, Ref(R, bg));
		}

		{
			SCDateTime ny = sc.ConvertDateTimeFromChartTimeZone(sc.BaseDateTimeIn[i], TIMEZONE_NEW_YORK);
			SCDateTime nyPrev = first ? ny : sc.ConvertDateTimeFromChartTimeZone(sc.BaseDateTimeIn[p], TIMEZONE_NEW_YORK);
			int tNow = ny.GetTimeInSeconds();
			int tPrev = nyPrev.GetTimeInSeconds();
			SCDateTime key = ny;
			key += SCDateTime::HOURS(6);
			SCDateTime keyPrev = nyPrev;
			keyPrev += SCDateTime::HOURS(6);
			bool newDay = !first && key.GetDate() != keyPrev.GetDate();

			for (int s = 0; s < 3; s++)
			{
				SCFloatArrayRef sHi = Arr(sc, sBase[s] + 0);
				SCFloatArrayRef sLo = Arr(sc, sBase[s] + 1);
				SCFloatArrayRef sStart = Arr(sc, sBase[s] + 2);
				SCFloatArrayRef sEnd = Arr(sc, sBase[s] + 3);
				SCFloatArrayRef sState = Arr(sc, sBase[s] + 4);
				SCFloatArrayRef sHiBrk = Arr(sc, sBase[s] + 5);
				SCFloatArrayRef sLoBrk = Arr(sc, sBase[s] + 6);
				if (first)
				{
					sHi[i] = 0; sLo[i] = 0; sStart[i] = -1; sEnd[i] = -1; sState[i] = NSS_IDLE; sHiBrk[i] = 0; sLoBrk[i] = 0;
				}
				else
				{
					sHi[i] = sHi[p]; sLo[i] = sLo[p]; sStart[i] = sStart[p]; sEnd[i] = sEnd[p]; sState[i] = sState[p]; sHiBrk[i] = sHiBrk[p]; sLoBrk[i] = sLoBrk[p];
				}
				if (newDay && sState[i] != NSS_FORMING)
					sState[i] = NSS_IDLE;
				bool inNow = InSession(tNow, sessStart[s], sessEnd[s]);
				bool inBefore = !first && InSession(tPrev, sessStart[s], sessEnd[s]);
				if (inNow && !inBefore)
				{
					sHi[i] = H[i]; sLo[i] = L[i]; sStart[i] = (float)i; sEnd[i] = (float)i; sState[i] = NSS_FORMING; sHiBrk[i] = 0; sLoBrk[i] = 0;
				}
				else if (inNow && sState[i] == NSS_FORMING)
				{
					sHi[i] = (std::max)(sHi[i], H[i]); sLo[i] = (std::min)(sLo[i], L[i]); sEnd[i] = (float)i;
				}
				else if (!inNow && sState[i] == NSS_FORMING)
					sState[i] = NSS_DONE;
				if (sState[i] == NSS_DONE && conf)
				{
					if (Cl[i] > sHi[i])
						sHiBrk[i] = 1;
					if (Cl[i] < sLo[i])
						sLoBrk[i] = 1;
				}
			}

			bool inRTH = InSession(tNow, rthStart, rthEnd);
			bool inRTHPrev = !first && InSession(tPrev, rthStart, rthEnd);
			ycVal[i] = first ? 0.0f : ycVal[p];
			ycX[i] = first ? -1.0f : ycX[p];
			if (inRTHPrev && !inRTH)
			{
				ycVal[i] = Cl[p];
				ycX[i] = (float)i;
			}

			SCFloatArrayRef orHi = Arr(sc, A_S0_HI);
			SCFloatArrayRef orLo = Arr(sc, A_S0_LO);
			SCFloatArrayRef orState = Arr(sc, A_S0_STATE);
			bool inOR = InSession(tNow, sessStart[NSESS_OR], sessEnd[NSESS_OR]);
			bool inORPrev = !first && InSession(tPrev, sessStart[NSESS_OR], sessEnd[NSESS_OR]);
			orbDir[i] = first ? 0.0f : orbDir[p];
			orbBar[i] = first ? -1.0f : orbBar[p];
			orbPostHi[i] = first ? 0.0f : orbPostHi[p];
			orbPostLo[i] = first ? 0.0f : orbPostLo[p];
			orDoneSize[i] = 0;
			if (inOR && !inORPrev)
			{
				orbDir[i] = 0;
				orbBar[i] = -1;
				orbPostHi[i] = 0;
				orbPostLo[i] = 0;
			}
			if (inORPrev && !inOR)
				orDoneSize[i] = orHi[i] - orLo[i];
			if (orState[i] == NSS_DONE)
			{
				orbPostHi[i] = orbPostHi[i] == 0 ? H[i] : (std::max)(orbPostHi[i], H[i]);
				orbPostLo[i] = orbPostLo[i] == 0 ? L[i] : (std::min)(orbPostLo[i], L[i]);
				if (orbDir[i] == 0 && conf)
				{
					if (Cl[i] > orHi[i])
					{
						orbDir[i] = 1;
						orbBar[i] = (float)i;
					}
					else if (Cl[i] < orLo[i])
					{
						orbDir[i] = -1;
						orbBar[i] = (float)i;
					}
				}
			}
			bool orBreakNow = showLevels && sc.Input[IN_LVL_BREAK].GetYesNo() && sc.Input[IN_OR_ON].GetYesNo() && orbBar[i] == (float)i;
			if (orBreakNow && orbDir[i] == 1)
				placeBelow(SG_ORB_UP, Ref(G, bg));
			if (orBreakNow && orbDir[i] == -1)
				placeAbove(SG_ORB_DOWN, Ref(R, bg));
		}

		{
			auto dirGrad = [&](bool up, float s, float lo, float hi)
			{
				Col base = up ? G : R;
				return Grad(s, lo, hi, T(base, 85), base);
			};
			EmaStep(rsiHeat, rsiM, i, 3);
			float ksRaw = (!first && atr30[i] > 0) ? (kernel[i] - kernel[p]) / atr30[i] : 0.0f;
			EmaStep(kSlope, ksRaw, i, 3);
			EmaStep(adxS, adxVal, i, 3);
			float haClose = (O[i] + H[i] + L[i] + Cl[i]) / 4.0f;
			haOpen[i] = first ? (O[i] + Cl[i]) / 2.0f : (haOpen[p] + (O[p] + H[p] + L[p] + Cl[p]) / 4.0f) / 2.0f;
			float haBody = atr30[i] > 0 ? (haClose - haOpen[i]) / atr30[i] : 0.0f;
			float zRaw = sdBB > 0 ? (Cl[i] - basisBB) / sdBB : 0.0f;
			EmaStep(zScore, zRaw, i, 3);

			{
				float hl2 = (H[i] + L[i]) / 2.0f;
				float upB = hl2 - 3.0f * atr10[i];
				float dnB = hl2 + 3.0f * atr10[i];
				float prevLower = first ? upB : stLower[p];
				float prevUpper = first ? dnB : stUpper[p];
				float lowerBand = (upB > prevLower || (!first && Cl[p] < prevLower)) ? upB : prevLower;
				float upperBand = (dnB < prevUpper || (!first && Cl[p] > prevUpper)) ? dnB : prevUpper;
				float dir;
				if (first || i < 10)
					dir = 1;
				else if (stVal[p] == prevUpper)
					dir = Cl[i] > upperBand ? -1.0f : 1.0f;
				else
					dir = Cl[i] < lowerBand ? 1.0f : -1.0f;
				stLower[i] = lowerBand;
				stUpper[i] = upperBand;
				stDir[i] = dir;
				stVal[i] = dir < 0 ? lowerBand : upperBand;
			}

			Col body = G, wick = G, border = G;
			bool paint = true;
			switch (candleMode)
			{
			case NCM_NONE:
				paint = false;
				break;
			case NCM_VECTOR:
			{
				int code = (int)pvCode[i];
				Col c = code == 1 ? G : code == 2 ? R : code == 3 ? C(BLUE_VECTOR) : code == 4 ? C(VIOLET_VECTOR) : code == 5 ? T(C(REG_UP), 99) : T(C(REG_DN), 99);
				body = wick = border = c;
				break;
			}
			case NCM_WADDAH:
			{
				float ratio = e1s[i] > 0 ? t1s[i] / e1s[i] : 0.0f;
				float strength = std::fabs(ratio);
				Col base = ratio >= 0 ? G : R;
				body = Grad(strength, 0, 2, T(base, 90), base);
				border = Grad(strength, 0, 1, T(base, 70), base);
				wick = Grad(strength, 0, 1, T(base, 50), base);
				break;
			}
			case NCM_SQUEEZE:
			{
				Col sq = C(RGB(255, 255, 255));
				if (val > 0)
				{
					if (val > valPrev)
						sq = Grad(std::fabs(val), 0, 30, T(G, 50), G);
					else if (val < valPrev)
						sq = T(G, 70);
				}
				else
				{
					if (val < valPrev)
						sq = Grad(std::fabs(val), 0, 30, T(R, 50), R);
					else if (val > valPrev)
						sq = T(R, 50);
				}
				body = wick = border = sq;
				break;
			}
			case NCM_VOLUME_DELTA:
			{
				float v = cvd[i];
				Col c = v > 0 ? Grad(std::fabs(v), 0, 300, T(G, 70), G) : v < 0 ? Grad(std::fabs(v), 0, 300, T(R, 70), R) : C(RGB(255, 255, 255));
				body = wick = border = c;
				break;
			}
			case NCM_TREND_AGREEMENT:
			{
				Col base = fanUp ? G : R;
				Col c = fanUp == bullBar ? base : T(base, 75);
				body = wick = border = c;
				break;
			}
			case NCM_TIDAL_WAVE:
			{
				Col base = ws == 1 ? G : ws == 2 ? R : grayC;
				Col c = sigWave[i] != 0 ? base : T(base, 55);
				body = wick = border = c;
				break;
			}
			case NCM_CONFLUENCE:
				body = wick = border = dirGrad(bullBar, (float)tpc, 0, (float)tpAll);
				break;
			case NCM_RSI_HEAT:
			{
				float rh = rsiHeat[i];
				Col c = rh >= 50 ? Grad(rh, 50, 80, T(G, 85), G) : Grad(rh, 20, 50, R, T(R, 85));
				body = wick = border = c;
				break;
			}
			case NCM_KERNEL_SLOPE:
				body = wick = border = dirGrad(kSlope[i] >= 0, std::fabs(kSlope[i]), 0, 0.3f);
				break;
			case NCM_RELATIVE_VOLUME:
				body = wick = border = dirGrad(bullBar, relVolS[i], 0.5f, 2.5f);
				break;
			case NCM_ADX_STRENGTH:
				body = wick = border = Grad(adxS[i], 15, 40, T(grayC, 50), diPlus[i] >= diMinus[i] ? G : R);
				break;
			case NCM_HEIKIN_ASHI:
				body = wick = border = dirGrad(haBody >= 0, std::fabs(haBody), 0, 0.6f);
				break;
			case NCM_STRETCH:
				body = wick = border = dirGrad(zScore[i] >= 0, std::fabs(zScore[i]), 0, 2.5f);
				break;
			case NCM_SUPERTREND:
			{
				bool stUp = stDir[i] < 0;
				Col c = T(stUp ? G : R, stUp == bullBar ? 0.0f : 50.0f);
				body = wick = border = c;
				break;
			}
			default:
				paint = false;
				break;
			}
			if (paint)
			{
				sc.Subgraph[SG_CANDLE_BODY][i] = 1;
				sc.Subgraph[SG_CANDLE_BODY].DataColor[i] = Ref(body, bg);
				sc.Subgraph[SG_CANDLE_OUTLINE][i] = 1;
				sc.Subgraph[SG_CANDLE_OUTLINE].DataColor[i] = Ref(candleMode == NCM_WADDAH ? border : wick, bg);
			}
			else
			{
				sc.Subgraph[SG_CANDLE_BODY][i] = 0;
				sc.Subgraph[SG_CANDLE_OUTLINE][i] = 0;
			}
		}

		if (alertsOn && conf && i > lastAlertBar && !sc.IsFullRecalculation && i >= sc.ArraySize - 3)
		{
			SCString msg;
			auto add = [&](bool cond, const char* name)
			{
				if (!cond)
					return;
				if (msg.GetLength() > 0)
					msg += ", ";
				msg += name;
			};
			SCString strongB, strongS;
			strongB.Format("Strong Buy (confluence %d)", tpc);
			strongS.Format("Strong Sell (confluence %d)", tpc);
			add(buySuper, strongB.GetChars());
			add(sellSuper, strongS.GetChars());
			add(buyBasic, "Buy");
			add(sellBasic, "Sell");
			add(showVodka && vodkaBuy, "Add Long (Strong)");
			add(showVodka && vodkaSell, "Add Short (Strong)");
			add(showPlus && plusBuy, "Add Long");
			add(showPlus && plusSell, "Add Short");
			add(plotBuy[i] != 0, "Ultimate Buy");
			add(plotSell[i] != 0, "Ultimate Sell");
			add(i - lbR >= 0 && sc.Subgraph[SG_DIV_BULL][i - lbR] != 0, "Bullish Divergence");
			add(i - lbR >= 0 && sc.Subgraph[SG_DIV_BEAR][i - lbR] != 0, "Bearish Divergence");
			add(tpAllHit, "Take All Profit");
			add(tpSomeHit, "Take Partial Profit");
			add(crossUp, "9/21 Cross Up");
			add(crossDown, "9/21 Cross Down");
			if (msg.GetLength() > 0)
			{
				SCString full;
				full.Format("Nebula: %s %s | %s @ %s", sc.Symbol.GetChars(), TimeframeLabel(sc.SecondsPerBar).GetChars(), msg.GetChars(), sc.FormatGraphValue(Cl[i], sc.BaseGraphValueFormat).GetChars());
				sc.SetAlert(alertNumber, i, full);
				lastAlertBar = i;
			}
		}
	}

	const int last = sc.ArraySize - 1;
	if (last < 1)
		return;

	const int barSec = sc.SecondsPerBar > 0 ? sc.SecondsPerBar : 60;
	const int lvlOffset = sc.Input[IN_LVL_OFFSET].GetInt();
	SCDateTime rightEdge = sc.BaseDateTimeIn[last];
	rightEdge += SCDateTime::SECONDS(barSec * lvlOffset);
	const int fontSize = sc.Input[IN_DASH_FONT].GetInt();

	std::vector<Tag> tags;
	int levelLines = 0, levelTagsUsed = 0, formingUsed = 0;
	SCString nearUp = "-", nearDn = "-";
	if (showLevels)
	{
		const int lvlT = sc.Input[IN_LVL_TRANSP].GetInt();
		auto levelColor = [&](bool brk, bool isHigh) { return Ref(T(isHigh ? G : R, brk ? 10.0f : (float)lvlT), bg); };
		auto addLine = [&](float y, int x1, SCDateTime x2, COLORREF col, SubgraphLineStyles style, const char* name)
		{
			if (levelLines >= MAX_LEVEL_LINES || y == 0)
				return;
			s_UseTool t;
			t.DrawingType = DRAWING_LINE;
			t.BeginIndex = (std::max)(0, x1);
			t.BeginValue = y;
			t.EndDateTime = x2;
			t.EndValue = y;
			t.Color = col;
			t.LineWidth = 1;
			t.LineStyle = style;
			t.DrawUnderneathMainGraph = 1;
			UseSlot(sc, t, LN_LEVEL_LINE + levelLines);
			levelLines++;
			tags.push_back(Tag{ y, name, col });
		};
		const bool sessOn[3] = { sc.Input[IN_OR_ON].GetYesNo() != 0, sc.Input[IN_LON_ON].GetYesNo() != 0, sc.Input[IN_ASIA_ON].GetYesNo() != 0 };
		const char* sessName[3] = { "OR", "Lon", "Asia" };
		const SubgraphLineStyles sessStyle[3] = { LINESTYLE_SOLID, LINESTYLE_DASH, LINESTYLE_DOT };
		for (int s = 0; s < 3; s++)
		{
			if (!sessOn[s])
				continue;
			SCFloatArrayRef sHi = Arr(sc, sBase[s] + 0);
			SCFloatArrayRef sLo = Arr(sc, sBase[s] + 1);
			SCFloatArrayRef sStart = Arr(sc, sBase[s] + 2);
			SCFloatArrayRef sEnd = Arr(sc, sBase[s] + 3);
			SCFloatArrayRef sState = Arr(sc, sBase[s] + 4);
			SCFloatArrayRef sHiBrk = Arr(sc, sBase[s] + 5);
			SCFloatArrayRef sLoBrk = Arr(sc, sBase[s] + 6);
			int state = (int)sState[last];
			if (state == NSS_FORMING && sc.Input[IN_LVL_FORMING].GetYesNo() && formingUsed < MAX_FORMING)
			{
				s_UseTool t;
				t.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
				t.BeginIndex = (int)sStart[last];
				t.EndIndex = last;
				t.BeginValue = sHi[last];
				t.EndValue = sLo[last];
				t.Color = Ref(T(C(fg), 75), bg);
				t.SecondaryColor = Ref(T(C(fg), 85), bg);
				t.TransparencyLevel = 85;
				t.LineWidth = 1;
				t.LineStyle = LINESTYLE_DOT;
				t.DrawUnderneathMainGraph = 1;
				UseSlot(sc, t, LN_FORMING + formingUsed);
				formingUsed++;
			}
			else if (state == NSS_DONE)
			{
				SCString hn, ln;
				hn.Format("%sH", sessName[s]);
				ln.Format("%sL", sessName[s]);
				addLine(sHi[last], (int)sEnd[last], rightEdge, levelColor(sHiBrk[last] != 0, true), sessStyle[s], hn.GetChars());
				addLine(sLo[last], (int)sEnd[last], rightEdge, levelColor(sLoBrk[last] != 0, false), sessStyle[s], ln.GetChars());
			}
		}
		SCFloatArrayRef orHi = Arr(sc, A_S0_HI);
		SCFloatArrayRef orLo = Arr(sc, A_S0_LO);
		SCFloatArrayRef orState = Arr(sc, A_S0_STATE);
		int dir = (int)orbDir[last];
		if (sessOn[NSESS_OR] && orState[last] == NSS_DONE && sc.Input[IN_LVL_EXT].GetYesNo() && dir != 0)
		{
			float rng = orHi[last] - orLo[last];
			float ext1 = dir == 1 ? orHi[last] + 0.5f * rng : orLo[last] - 0.5f * rng;
			float ext2 = dir == 1 ? orHi[last] + rng : orLo[last] - rng;
			COLORREF ec = Ref(T(dir == 1 ? G : R, 50), bg);
			addLine(ext1, last - 6, rightEdge, ec, LINESTYLE_DOT, "OR 1.5x");
			bool hitExt1 = dir == 1 ? orbPostHi[last] >= ext1 : orbPostLo[last] <= ext1;
			if (hitExt1)
				addLine(ext2, last - 6, rightEdge, ec, LINESTYLE_DOT, "OR 2x");
		}
		if (sc.Input[IN_YC_ON].GetYesNo() && ycVal[last] != 0 && ycX[last] >= 0)
			addLine(ycVal[last], (int)ycX[last], rightEdge, Ref(T(C(fg), (float)lvlT), bg), LINESTYLE_DOT, "Close");

		std::sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) { return a.Price < b.Price; });
		const float tol = sc.Input[IN_LVL_MERGE].GetInt() * sc.TickSize;
		for (size_t a = 0; a < tags.size();)
		{
			SCString txt = tags[a].Name;
			size_t b = a + 1;
			while (b < tags.size() && tags[b].Price - tags[a].Price <= tol)
			{
				txt += " . ";
				txt += tags[b].Name;
				b++;
			}
			if (levelTagsUsed < MAX_LEVEL_LINES)
			{
				s_UseTool t;
				t.DrawingType = DRAWING_TEXT;
				t.BeginDateTime = rightEdge;
				t.BeginValue = tags[a].Price;
				t.Text = txt;
				t.Color = tags[a].Color;
				t.FontBackColor = Ref(T(C(tags[a].Color), 80), bg);
				t.TransparentLabelBackground = 0;
				t.FontSize = fontSize;
				t.FontBold = 0;
				t.TextAlignment = DT_LEFT | DT_VCENTER;
				UseSlot(sc, t, LN_LEVEL_TAG + levelTagsUsed);
				levelTagsUsed++;
			}
			a = b;
		}

		float bestUp = FLT_MAX, bestDn = FLT_MAX;
		for (const Tag& t : tags)
		{
			float d = t.Price - Cl[last];
			if (d > 0 && d < bestUp)
			{
				bestUp = d;
				nearUp.Format("%s +%s", t.Name.GetChars(), sc.FormatGraphValue(d, sc.BaseGraphValueFormat).GetChars());
			}
			if (d < 0 && -d < bestDn)
			{
				bestDn = -d;
				nearDn.Format("%s -%s", t.Name.GetChars(), sc.FormatGraphValue(-d, sc.BaseGraphValueFormat).GetChars());
			}
		}
	}
	DeleteSlots(sc, LN_LEVEL_LINE, levelLines, (std::max)(prevLevelLines, MAX_LEVEL_LINES));
	DeleteSlots(sc, LN_LEVEL_TAG, levelTagsUsed, (std::max)(prevLevelTags, MAX_LEVEL_LINES));
	DeleteSlots(sc, LN_FORMING, formingUsed, (std::max)(prevForming, MAX_FORMING));
	prevLevelLines = levelLines;
	prevLevelTags = levelTagsUsed;
	prevForming = formingUsed;

	int imbUsed = 0;
	if (showVI)
	{
		const int viStyle = sc.Input[IN_VI_STYLE].GetIndex();
		const SubgraphLineStyles ls = viStyle == 0 ? LINESTYLE_SOLID : viStyle == 2 ? LINESTYLE_DASH : LINESTYLE_DOT;
		for (const Imbalance& im : *imbalances)
		{
			if (imbUsed >= MAX_IMBALANCES)
				break;
			s_UseTool t;
			t.DrawingType = DRAWING_LINE;
			t.BeginIndex = im.Bar;
			t.BeginValue = im.Level;
			SCDateTime endDT = sc.BaseDateTimeIn[im.Bar];
			endDT += SCDateTime::SECONDS(barSec * viExtend);
			t.EndDateTime = endDT;
			t.EndValue = im.Level;
			t.Color = Ref(T(im.Dir == 1 ? G : R, 40), bg);
			t.LineWidth = (uint16_t)sc.Input[IN_VI_WIDTH].GetInt();
			t.LineStyle = ls;
			t.DrawUnderneathMainGraph = 1;
			UseSlot(sc, t, LN_IMBALANCE + imbUsed);
			imbUsed++;
		}
	}
	DeleteSlots(sc, LN_IMBALANCE, imbUsed, (std::max)(prevImbalanceLines, imbUsed));
	prevImbalanceLines = imbUsed;

	int divUsed = 0;
	if (showDiv && divLines)
	{
		for (int j = last; j >= 0 && j > last - 5000 && divUsed < MAX_DIV_LINES; j--)
		{
			if (sc.Subgraph[SG_DIV_BULL][j] != 0 && divbX1[j] >= 0)
			{
				s_UseTool t;
				t.DrawingType = DRAWING_LINE;
				t.BeginIndex = (int)divbX1[j];
				t.BeginValue = divbY1[j];
				t.EndIndex = j;
				t.EndValue = L[j];
				t.Color = Ref(T(G, (float)divTransp), bg);
				t.LineWidth = 1;
				UseSlot(sc, t, LN_DIV_LINE + divUsed);
				divUsed++;
			}
			if (sc.Subgraph[SG_DIV_BEAR][j] != 0 && divsX1[j] >= 0 && divUsed < MAX_DIV_LINES)
			{
				s_UseTool t;
				t.DrawingType = DRAWING_LINE;
				t.BeginIndex = (int)divsX1[j];
				t.BeginValue = divsY1[j];
				t.EndIndex = j;
				t.EndValue = H[j];
				t.Color = Ref(T(R, (float)divTransp), bg);
				t.LineWidth = 1;
				UseSlot(sc, t, LN_DIV_LINE + divUsed);
				divUsed++;
			}
		}
	}
	DeleteSlots(sc, LN_DIV_LINE, divUsed, (std::max)(prevDivLines, MAX_DIV_LINES));
	prevDivLines = divUsed;

	int rows = 0;
	if (showDash)
	{
		struct Row { SCString Text; COLORREF Color; bool Head; };
		std::vector<Row> out;
		const int blocks = sc.Input[IN_BAR_BLOCKS].GetInt();
		const int keyW = 15;
		auto row = [&](const char* k, const SCString& v, COLORREF c) { out.push_back(Row{ Pad(k, keyW) + v, c, false }); };
		auto head = [&](const char* k, const SCString& v) { out.push_back(Row{ Pad(k, keyW) + v, fg, true }); };
		auto barColor = [&](float pct) { return pct < 50 ? Ref(Grad(pct, 0, 50, R, grayC), bg) : Ref(Grad(pct, 50, 100, grayC, G), bg); };

		SCString hdr;
		hdr.Format("%s %s", sc.Symbol.GetChars(), TimeframeLabel(sc.SecondsPerBar).GetChars());
		head("NEBULA", hdr);
		bool fanUpL = fanVMA[last] > mg[last];
		row("Trend (cloud)", fanUpL ? "Up" : "Down", Ref(fanUpL ? G : R, bg));
		int wsL = (int)wave[last];
		row("Wave", wsL == 1 ? "Up" : wsL == 2 ? "Down" : "-", wsL == 1 ? Ref(G, bg) : wsL == 2 ? Ref(R, bg) : fg);
		float adxL = 100.0f * adx[last];
		float adxPct = (std::min)(100.0f, adxL * 2.0f);
		SCString v;
		v.Format("%s  %.0f %s", Bar(adxPct, blocks).GetChars(), adxL, adxL >= 25 ? "strong" : adxL >= 20 ? "building" : "choppy");
		row("ADX", v, barColor(50.0f + (diPlus[last] >= diMinus[last] ? 1.0f : -1.0f) * adxPct / 2.0f));
		float rsiL = rsi14[last];
		v.Format("%s  %.0f", Bar(rsiL, blocks).GetChars(), rsiL);
		row("RSI", v, barColor(rsiL));
		int tpcL = (int)tpCount[last];
		float confPct = (std::min)(100.0f, 100.0f * tpcL / (std::max)(1, tpAll));
		v.Format("%s  %d/%d", Bar(confPct, blocks).GetChars(), tpcL, tpAll);
		Col confTarget = wsL == 1 ? R : wsL == 2 ? G : C(PINE_ORANGE);
		row("Confluence", v, Ref(Grad(confPct, 0, 100, grayC, confTarget), bg));

		if (showLevels && sc.Input[IN_DASH_LEVELS].GetYesNo())
		{
			head("SESSION LEVELS", "");
			SCFloatArrayRef orHi = Arr(sc, A_S0_HI);
			SCFloatArrayRef orLo = Arr(sc, A_S0_LO);
			SCFloatArrayRef orState = Arr(sc, A_S0_STATE);
			int st = (int)orState[last];
			SCString orTxt = "-";
			if (st == NSS_DONE)
			{
				float sizeNow = orHi[last] - orLo[last];
				int doneBar = -1;
				for (int j = last; j >= 0; j--)
				{
					if (orDoneSize[j] != 0)
					{
						doneBar = j;
						break;
					}
				}
				double sum = 0;
				int n = 0;
				for (int j = doneBar - 1; j >= 0 && n < 20; j--)
				{
					if (orDoneSize[j] != 0)
					{
						sum += orDoneSize[j];
						n++;
					}
				}
				if (n > 0 && sum > 0)
					orTxt.Format("%s pts . %.1fx avg", sc.FormatGraphValue(sizeNow, sc.BaseGraphValueFormat).GetChars(), sizeNow / (sum / n));
				else
					orTxt.Format("%s pts", sc.FormatGraphValue(sizeNow, sc.BaseGraphValueFormat).GetChars());
			}
			else if (st == NSS_FORMING)
				orTxt = "Forming...";
			row("Opening range", orTxt, fg);
			int dirL = (int)orbDir[last];
			SCString brk = "-";
			COLORREF brkC = fg;
			if (st == NSS_DONE)
			{
				if (dirL != 0 && orbBar[last] >= 0)
				{
					SCDateTime bt = sc.ConvertDateTimeFromChartTimeZone(sc.BaseDateTimeIn[(int)orbBar[last]], TIMEZONE_NEW_YORK);
					brk.Format("%s at %02d:%02d", dirL == 1 ? "Up" : "Down", bt.GetHour(), bt.GetMinute());
					brkC = Ref(dirL == 1 ? G : R, bg);
				}
				else
					brk = "Inside";
			}
			row("OR break", brk, brkC);
			row("Nearest above", nearUp, fg);
			row("Nearest below", nearDn, fg);
		}

		const int pos = sc.Input[IN_DASH_POS].GetIndex();
		const float spacing = sc.Input[IN_DASH_SPACING].GetFloat();
		int xPct = 99;
		int align = DT_RIGHT;
		if (pos == 1 || pos == 6)
		{
			xPct = 50;
			align = DT_CENTER;
		}
		else if (pos == 2 || pos == 4 || pos == 7)
		{
			xPct = 1;
			align = DT_LEFT;
		}
		float total = spacing * (float)(out.size() - 1);
		float yTop = 97.0f;
		if (pos == 3 || pos == 4)
			yTop = 50.0f + total / 2.0f;
		else if (pos >= 5)
			yTop = 3.0f + total;
		const COLORREF panelBg = Ref(T(C(fg), 90), bg);
		const COLORREF headBg = Ref(T(C(fg), 80), bg);
		for (size_t r = 0; r < out.size() && (int)r < MAX_DASH_ROWS; r++)
		{
			s_UseTool t;
			t.DrawingType = DRAWING_TEXT;
			t.UseRelativeVerticalValues = 1;
			t.BeginDateTime = xPct;
			t.BeginValue = yTop - spacing * (float)r;
			t.Text = out[r].Text;
			t.Color = out[r].Color;
			t.FontBackColor = out[r].Head ? headBg : panelBg;
			t.TransparentLabelBackground = 0;
			t.FontFace = "Consolas";
			t.FontSize = fontSize;
			t.FontBold = out[r].Head ? 1 : 0;
			t.TextAlignment = align | DT_VCENTER;
			UseSlot(sc, t, LN_DASH + (int)r);
			rows++;
		}
	}
	DeleteSlots(sc, LN_DASH, rows, (std::max)(prevDashRows, MAX_DASH_ROWS));
	prevDashRows = rows;
}