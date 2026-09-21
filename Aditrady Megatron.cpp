// ============================================================================
// ADITRADYMEGATRON  -  Sierra Chart ACSIL port
//
// Ported from the NinjaTrader 8 NinjaScript indicator ADITRADYMEGATRON.cs,
// which itself unified: OptimusNinja / OptimusNinjaSignals, GodTrades21,
// God Trades for Sierra Chart, PredatorBollinger, ADITRADYSOLIDSNAKE and
// EngulfingBarsBB into one confluence / gap-engine / pattern indicator.
//
// This file is self contained: every moving average, oscillator and pattern
// test is implemented directly against Sierra's base OHLCV arrays, so no
// other custom study or "Study/Price Overlay" reference is required.
//
// BUILD: Add this .cpp to a Custom Studies DLL project (Sierra Chart's
// ACS_Source template, built with the bundled Visual Studio Build Tools /
// full Visual Studio), build the DLL, then load it in Sierra via
// Analysis > Studies > (your DLL) > ADITRADYMEGATRON.
//
// NOTE ON FIDELITY: A handful of NinjaTrader built-ins (FisherTransform,
// ADX, RSI, ATR, Parabolic SAR, KAMA, T3, HMA) are internal/black-box in
// NinjaTrader. They are reimplemented here from their standard published
// formulas (Wilder ADX/ATR/RSI, Wells Wilder Parabolic SAR, Ehlers Fisher
// Transform, Tillson T3, Hull MA, Kaufman KAMA), which is what those
// indicators are defined to compute. Everything else (confluence rules,
// gap engine, OBR, Bollinger Gap, FC continuation, spiderweb, SMI +
// divergence, Wave state, Vodka Shot, adaptive thresholds, all candle
// patterns) is a direct line-by-line port of the original logic.
//
// ALERT SOUNDS: the original exposed ~28 individual per-signal .wav file
// inputs (mostly duplicating "bullish"/"bearish"/generic sounds under the
// hood). That is collapsed here into three sound inputs (Bullish / Bearish
// / Neutral); every individual "Alert On/Off" checkbox is preserved.
// ============================================================================

#include "sierrachart.h"
#include <vector>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

SCDLLName("Aditrady_Megatron")

// ---------------------------------------------------------------------------
// Small color helper
// ---------------------------------------------------------------------------
#ifndef ADM_RGB
#define ADM_RGB(r,g,b) RGB((r),(g),(b))
#endif

// Scales a color's brightness (shade 0..255 => 0..100%), mirroring the original's ScaleBrush.
static inline COLORREF ADM_ScaleColor(COLORREF c, int shade)
{
	double f = shade / 255.0;
	int r = (int)(GetRValue(c) * f + 0.5);
	int g = (int)(GetGValue(c) * f + 0.5);
	int b = (int)(GetBValue(c) * f + 0.5);
	return ADM_RGB(r, g, b);
}

// ---------------------------------------------------------------------------
// Subgraph indices (mirrors the original plot layout 1:1)
// ---------------------------------------------------------------------------
enum ADMSubgraph
{
	SG_BUY = 0, SG_SELL, SG_KAMA, SG_BBU, SG_BBM, SG_BBL, SG_BARCOLOR,
	SG_LOWTOUCH, SG_UPTOUCH, SG_MIDCLOSE,
	SG_CONFLUENCE, SG_BIGARROW, SG_SQUEEZE, SG_PATTERN,
	SG_VOLIMB, SG_GAPTOUCH, SG_INVALIDTOUCH,
	SG_CONT, SG_BGAP, SG_OBR,
	SG_ACTIVEGAPS, SG_VALIDGAPS, SG_PENDING, SG_WEBCOUNT, SG_WEBWARN, SG_NEARGAP,
	SG_ENTRY, SG_STOP, SG_TARGET,
	SG_DIR, SG_CODE, SG_MLONG, SG_MSHORT,
	SG_SMI, SG_SMIAVG, SG_SMISTATE, SG_SMIDIV,
	SG_WAVE, SG_VODKA, SG_VOLRATIO,
	SG_COUNT
};

// ---------------------------------------------------------------------------
// Enums (mirrors the NinjaScript enums)
// ---------------------------------------------------------------------------
enum ADMFilterRules { ADM_Sierra = 0, ADM_NinjaLegacy = 1 };
enum ADMBarColorMode { ADM_BC_None = 0, ADM_BC_Waddah = 1, ADM_BC_Linda = 2, ADM_BC_Supertrend = 3 };
enum ADMSuperTrendAtr { ADM_ST_Hull = 0, ADM_ST_Wilder = 1 };
enum ADMEarlyTouch { ADM_ET_StopImmediately = 0, ADM_ET_IgnoreUntilValid = 1 };
enum ADMValidTouch { ADM_VT_StopOnly = 0, ADM_VT_StopAndContinuation = 1 };
enum ADMConfirmMode { ADM_CM_TouchOnly = 0, ADM_CM_CloseBeyondLine = 1, ADM_CM_CloseBeyondZone = 2 };
enum ADMLinePriceMode { ADM_LP_Midpoint = 0, ADM_LP_PrevCloseEdge = 1, ADM_LP_CurrentOpenEdge = 2 };
enum ADMTargetMode { ADM_TM_None = 0, ADM_TM_OppositeBand = 1, ADM_TM_FixedTicks = 2 };
enum ADMBollingerLocation { ADM_BL_Close = 0, ADM_BL_WickExtreme = 1, ADM_BL_HLC3 = 2, ADM_BL_BodyMid = 3 };
enum ADMObrLocationMode { ADM_OL_NearBand = 0, ADM_OL_PierceOrPrev = 1 };
enum ADMObrMarkerStyle { ADM_OM_Diamond = 0, ADM_OM_Arrow = 1, ADM_OM_None = 2 };
enum ADMGapLineExtension { ADM_GLE_Projected = 0, ADM_GLE_ToCurrentBar = 1 };
enum ADMBgLocationMode { ADM_BG_Proximity = 0, ADM_BG_SierraPierce = 1 };
enum ADMBrightnessMode { ADM_BM_Fixed = 0, ADM_BM_AutoScale = 1 };
enum ADMDashCorner { ADM_DC_TopRight = 0, ADM_DC_TopLeft = 1, ADM_DC_BottomRight = 2, ADM_DC_BottomLeft = 3 };

enum ADMAlertSignal
{
	AS_Buy, AS_Sell, AS_BigBuy, AS_BigSell, AS_SqueezeUp, AS_SqueezeDown,
	AS_VolImbBuy, AS_VolImbSell, AS_GapTouchBull, AS_GapTouchBear,
	AS_FcLong, AS_FcShort, AS_BgLong, AS_BgShort, AS_ObrLong, AS_ObrShort,
	AS_Tramp, AS_ThreeOutside, AS_Tweezer, AS_WickPat, AS_Stairs, AS_RevSquare,
	AS_KamaBounce, AS_VodkaBuy, AS_VodkaSell, AS_SmiDivBull, AS_SmiDivBear, AS_Spiderweb
};

// ---------------------------------------------------------------------------
// Generic numeric helpers
// ---------------------------------------------------------------------------
static inline bool ADM_IsZero(double x) { return fabs(x) < 1e-12; }

static inline double ADM_Ema(double prev, double price, int period, bool& init)
{
	double k = 2.0 / (period + 1.0);
	if (!init) { init = true; return price; }
	return k * price + (1.0 - k) * prev;
}

// Self-contained recursive EMA line (own seeding flag, no cross-talk between lines)
struct EmaState
{
	double value;
	bool init;
	EmaState() : value(0), init(false) {}
	double Push(double price, int period)
	{
		value = ADM_Ema(value, price, period, init);
		return value;
	}
	void Reset() { value = 0; init = false; }
};

// Wilder-style recursive smoothing: v += (x - v) / period, seeded with x on first use.
// This is the exact recurrence for Wilder's ATR, and a standard (widely used) way to
// smooth ADX's internal DM/TR/DX lines without needing a bootstrap window.
struct WilderState
{
	double value;
	bool init;
	WilderState() : value(0), init(false) {}
	double Push(double x, int period)
	{
		if (!init) { init = true; value = x; }
		else value = value + (x - value) / period;
		return value;
	}
	void Reset() { value = 0; init = false; }
};

// Rolling SMA using a deque + running sum (O(1) amortized)
static inline double ADM_RollingSma(std::deque<double>& win, double& sum, double value, int period)
{
	win.push_back(value);
	sum += value;
	if ((int)win.size() > period)
	{
		sum -= win.front();
		win.pop_front();
	}
	return win.empty() ? 0.0 : sum / (double)win.size();
}

// Linear regression endpoint at bar `idx` over the trailing `period` bars (matches
// NinjaTrader's LinReg last-bar value); uses fewer points during warmup, like NT does.
template <typename Getter>
static double ADM_LinRegAt(Getter get, int idx, int period)
{
	int n = (idx + 1 < period) ? (idx + 1) : period;
	if (n <= 0) return get(idx);
	double sx = 0, sy = 0, sxy = 0, sxx = 0;
	for (int i = 0; i < n; i++)
	{
		double x = (double)i;
		double y = get(idx - n + 1 + i);
		sx += x; sy += y; sxy += x * y; sxx += x * x;
	}
	double denom = (n * sxx - sx * sx);
	double slope = ADM_IsZero(denom) ? 0.0 : (n * sxy - sx * sy) / denom;
	double intercept = (sy - slope * sx) / n;
	return intercept + slope * (n - 1);
}

// Simple moving average of `get(idx-period+1..idx)`, shrinking the window during warmup.
template <typename Getter>
static double ADM_SmaAt(Getter get, int idx, int period)
{
	int n = (idx + 1 < period) ? (idx + 1) : period;
	if (n <= 0) return get(idx);
	double sum = 0;
	for (int i = 0; i < n; i++) sum += get(idx - i);
	return sum / n;
}

// Weighted moving average (most recent bar gets the highest weight).
template <typename Getter>
static double ADM_WmaAt(Getter get, int idx, int period)
{
	int n = (idx + 1 < period) ? (idx + 1) : period;
	if (n <= 0) return get(idx);
	double wsum = 0, sum = 0;
	for (int i = 0; i < n; i++) { int w = n - i; wsum += w * get(idx - i); sum += w; }
	return sum > 0 ? wsum / sum : get(idx);
}

template <typename Getter>
static double ADM_MaxAt(Getter get, int idx, int period)
{
	int n = (idx + 1 < period) ? (idx + 1) : period;
	double m = get(idx);
	for (int i = 1; i < n; i++) { double v = get(idx - i); if (v > m) m = v; }
	return m;
}

template <typename Getter>
static double ADM_MinAt(Getter get, int idx, int period)
{
	int n = (idx + 1 < period) ? (idx + 1) : period;
	double m = get(idx);
	for (int i = 1; i < n; i++) { double v = get(idx - i); if (v < m) m = v; }
	return m;
}

// Hull Moving Average of an arbitrary source: WMA(2*WMA(src,period/2) - WMA(src,period), sqrt(period))
template <typename Getter>
static double ADM_HmaAt(Getter get, int idx, int period)
{
	int half = (std::max)(1, period / 2);
	int sq = (std::max)(1, (int)(sqrt((double)period) + 0.5));
	auto raw = [&](int i) -> double {
		double w1 = ADM_WmaAt(get, i, half);
		double w2 = ADM_WmaAt(get, i, period);
		return 2.0 * w1 - w2;
	};
	return ADM_WmaAt(raw, idx, sq);
}

// ---------------------------------------------------------------------------
// Nested value types
// ---------------------------------------------------------------------------
struct SmiPivot
{
	int Bar;
	double Value;
	bool IsHigh;
};

struct GapInfo
{
	int Direction;
	int CreationBar;
	double PreviousClose;
	double CurrentOpen;
	double ZoneLow;
	double ZoneHigh;
	double LinePrice;
	bool IsValid;
	bool Touched;
	bool InvalidTooEarly;
	bool Drawn;
	int LineId;
	int ZoneId;
};

struct PendingContinuation
{
	int GapIndexInHistory; // index into g_GapHistory (stable storage, see below)
	int TouchBar;
};

struct SuperTrendState
{
	double Ub, Lb, St;
	int Dir;
	bool Init;
	bool SierraLag;
	double pLbBasic;

	SuperTrendState() : Ub(0), Lb(0), St(0), Dir(1), Init(false), SierraLag(false), pLbBasic(0) {}

	void Update(double close, double prevClose, double basePrice, double atr, double mult)
	{
		double ubBasic = basePrice + mult * atr;
		double lbBasic = basePrice - mult * atr;

		if (!Init)
		{
			Ub = ubBasic;
			Lb = lbBasic;
			Dir = close >= basePrice ? 1 : -1;
			St = Dir > 0 ? Lb : Ub;
			pLbBasic = lbBasic;
			Init = true;
			return;
		}

		double pUb = Ub;
		double pLb = Lb;

		Ub = (ubBasic < pUb || prevClose > pUb) ? ubBasic : pUb;
		double lbSource = SierraLag ? pLbBasic : lbBasic;
		Lb = (lbBasic > pLb || prevClose < pLb) ? lbSource : pLb;
		pLbBasic = lbBasic;

		if (Dir > 0) { if (close < Lb) Dir = -1; }
		else { if (close > Ub) Dir = 1; }

		St = Dir > 0 ? Lb : Ub;
	}
};

// ---------------------------------------------------------------------------
// Persistent engine state (one instance per chart/study, via GetPersistentPointer)
// ---------------------------------------------------------------------------
struct AdmState
{
	// ---- shadow series (mirrors every Series<T> in the original 1:1) ----
	std::vector<double> lindaLine, aoLine, fisherValue, sqzRaw;
	std::vector<double> sumDmPlus, sumDmMinus, diPlusIdx, diMinusIdx, adxIdx, adxvmaLine, adxvmaTrend;
	std::vector<double> tsiLine;
	std::vector<double> rsiUpS, rsiDnS, rsiCut;
	std::vector<double> vdkMacd, wadAbsS, volAtrS, lindaAbsS;
	std::vector<char>   brightGreenS, brightRedS, upVodkaS, downVodkaS;
	std::vector<int>    waveS;
	std::vector<double> smiNum1, smiDen1, smiNum2, smiDen2, smiLine, smiAvgLine;

	// scalar EMA/SMA/etc. state (only [0]/[1] ever referenced upstream)
	EmaState ema20, ema40;
	double sarValue; int sarTrend; double sarEp, sarAf;
	double kamaPrev; bool kamaInit;
	WilderState rsiGainW, rsiLossW;      // classic (NinjaLegacy) Wilder RSI
	std::vector<double> rsiStdV;         // classic RSI value per bar
	double fishRaw, fishNt; bool fishInit; // classic (NinjaLegacy) Ehlers Fisher Transform on Close
	EmaState t3Ema1[6], t3Ema2[6];
	EmaState sqEma;
	EmaState tsiNum1, tsiNum2, tsiDen1, tsiDen2, tsiSignal;
	EmaState vdkEmaFast, vdkEmaSlow, vdkSignal;
	std::vector<double> vdkWma1V, vdkWma2V; // vdkWma3 (=WMA(vdkWma2V,w3)) computed on demand
	double wadAbsAvgSum; std::deque<double> wadAbsAvgWin;
	double lindaAbsAvgSum; std::deque<double> lindaAbsAvgWin;
	double linWadAbsAvgSum; std::deque<double> linWadAbsAvgWin;
	double volAtrAvgSum; std::deque<double> volAtrAvgWin;
	WilderState volAtr, lizAtr, stWilderAtr, smiAtr, adxVolatility;

	// classic Wilder ADX (separate from the ADXVMA port, which has its own diPlusIdx/diMinusIdx above)
	WilderState adxTR, adxDmPlus, adxDmMinus, adxDx;

	// SuperTrend
	SuperTrendState stMain, stLiz;

	// gap engine
	std::vector<GapInfo> gapHistory;      // stable storage so pending continuations can reference by index
	std::vector<int> activeGapIdx;        // indices into gapHistory currently active
	std::vector<PendingContinuation> pendingContinuations;

	// SMI pivots
	std::vector<SmiPivot> smiPricePivots, smiOscPivots;
	std::string lastBullDivKey, lastBearDivKey;

	// stacking
	std::map<int,double> stackUp, stackDn;

	// misc per-bar state
	bool sqRelaxUp;
	bool bigArrowUp;
	std::string prevEvil;
	int warmupBars;
	int drawCounter;
	int nextLineId;
	bool spiderWarnPrev;
	int wavePrev;
	double volRatio;
	bool vodkaBuyRaw, vodkaSellRaw;

	// per-bar cache
	bool cGreen, cRed, cDoji;
	double cBody, cPBody;
	double bbU, bbM, bbL, bbU1, bbL1;
	double cT1, cLinda, cSar, cKama;
	double cWadAvg, cLindaAvg, cLinWadAvg;
	double cAo, cAo1, cAdx, cHma, cHma1, cT3, cFishNt, cFishNt1;
	double cRsi0, cRsi1, cRsi2, cSqHist, cSqHist1, cTsi, cTsiSig;
	double cSmi, cSmiPrev, cSmiAvg;
};

// Resets all engine state to its bar-0 condition (mirrors the original's
// SeedFirstBar / field initializers). Called both when constructing the
// state and whenever the chart does a full recalculation from bar 0.
static void ADM_ResetState(AdmState& s)
{
	s.lindaLine.clear(); s.aoLine.clear(); s.fisherValue.clear(); s.sqzRaw.clear();
	s.sumDmPlus.clear(); s.sumDmMinus.clear(); s.diPlusIdx.clear(); s.diMinusIdx.clear();
	s.adxIdx.clear(); s.adxvmaLine.clear(); s.adxvmaTrend.clear();
	s.tsiLine.clear();
	s.rsiUpS.clear(); s.rsiDnS.clear(); s.rsiCut.clear();
	s.vdkMacd.clear(); s.wadAbsS.clear(); s.volAtrS.clear(); s.lindaAbsS.clear();
	s.brightGreenS.clear(); s.brightRedS.clear(); s.upVodkaS.clear(); s.downVodkaS.clear();
	s.waveS.clear();
	s.smiNum1.clear(); s.smiDen1.clear(); s.smiNum2.clear(); s.smiDen2.clear();
	s.smiLine.clear(); s.smiAvgLine.clear();

	s.ema20.Reset(); s.ema40.Reset();
	s.sarValue = 0; s.sarTrend = 1; s.sarEp = 0; s.sarAf = 0;
	s.kamaPrev = 0; s.kamaInit = false;
	s.rsiGainW.Reset(); s.rsiLossW.Reset(); s.rsiStdV.clear();
	s.fishRaw = 0; s.fishNt = 0; s.fishInit = false;
	for (int i = 0; i < 6; i++) { s.t3Ema1[i].Reset(); s.t3Ema2[i].Reset(); }
	s.sqEma.Reset();
	s.tsiNum1.Reset(); s.tsiNum2.Reset(); s.tsiDen1.Reset(); s.tsiDen2.Reset(); s.tsiSignal.Reset();
	s.vdkEmaFast.Reset(); s.vdkEmaSlow.Reset(); s.vdkSignal.Reset();
	s.vdkWma1V.clear(); s.vdkWma2V.clear();
	s.wadAbsAvgSum = s.lindaAbsAvgSum = s.linWadAbsAvgSum = s.volAtrAvgSum = 0;
	s.wadAbsAvgWin.clear(); s.lindaAbsAvgWin.clear(); s.linWadAbsAvgWin.clear(); s.volAtrAvgWin.clear();
	s.volAtr.Reset(); s.lizAtr.Reset(); s.stWilderAtr.Reset(); s.smiAtr.Reset(); s.adxVolatility.Reset();
	s.adxTR.Reset(); s.adxDmPlus.Reset(); s.adxDmMinus.Reset(); s.adxDx.Reset();
	s.stMain = SuperTrendState();
	s.stLiz = SuperTrendState();
	s.gapHistory.clear(); s.activeGapIdx.clear(); s.pendingContinuations.clear();
	s.smiPricePivots.clear(); s.smiOscPivots.clear();
	s.lastBullDivKey.clear(); s.lastBearDivKey.clear();
	s.stackUp.clear(); s.stackDn.clear();
	s.sqRelaxUp = false;
	s.bigArrowUp = false;
	s.prevEvil.clear();
	s.warmupBars = 45;
	s.drawCounter = 0;
	s.nextLineId = 100000;
	s.spiderWarnPrev = false;
	s.wavePrev = -1;
	s.volRatio = 1.0;
	s.vodkaBuyRaw = s.vodkaSellRaw = false;
}

struct AdmStateInit
{
	static AdmState* Create()
	{
		AdmState* s = new AdmState();
		ADM_ResetState(*s);
		return s;
	}
};

// ---------------------------------------------------------------------------
// Dashboard / drawing helpers
// ---------------------------------------------------------------------------
static void ADM_DeleteDrawing(SCStudyInterfaceRef sc, int lineId)
{
	sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, lineId);
}

static void ADM_DrawText(SCStudyInterfaceRef sc, int lineId, int barIndex, double price,
	const SCString& text, COLORREF color, int fontSize, bool bold, bool addAlways = false)
{
	s_UseTool tool;
	tool.Clear();
	tool.ChartNumber = sc.ChartNumber;
	tool.DrawingType = DRAWING_TEXT;
	tool.LineNumber = lineId;
	tool.BeginDateTime = sc.BaseDateTimeIn[barIndex];
	tool.BeginValue = (float)price;
	tool.Color = RGB(255,255,255);
	tool.FontSize = 9;
	tool.FontBold = bold ? 1 : 0;
	tool.FontFace = "Arial";
	tool.Text = text;
	tool.TextAlignment = DT_CENTER | DT_VCENTER;
	tool.AddMethod = addAlways ? UTAM_ADD_ALWAYS : UTAM_ADD_OR_ADJUST;
	tool.AddAsUserDrawnDrawing = 0;
	sc.UseTool(tool);
}

static void ADM_DrawLine(SCStudyInterfaceRef sc, int lineId, int startBar, double startPrice,
	int endBar, double endPrice, COLORREF color, int lineStyle, int width, bool extendRight)
{
	s_UseTool tool;
	tool.Clear();
	tool.ChartNumber = sc.ChartNumber;
	tool.DrawingType = DRAWING_LINE;
	tool.LineNumber = lineId;
	tool.BeginDateTime = sc.BaseDateTimeIn[startBar];
	tool.BeginValue = (float)startPrice;
	tool.EndDateTime = sc.BaseDateTimeIn[endBar];
	tool.EndValue = (float)endPrice;
	tool.Color = RGB(255,255,255);
	tool.LineStyle = (SubgraphLineStyles)lineStyle;
	tool.LineWidth = width;
	tool.ExtendRight = extendRight ? 1 : 0;
	tool.AddMethod = UTAM_ADD_OR_ADJUST;
	tool.AddAsUserDrawnDrawing = 0;
	sc.UseTool(tool);
}

static void ADM_DrawRect(SCStudyInterfaceRef sc, int lineId, int startBar, double top,
	int endBar, double bottom, COLORREF fillColor, int transparency)
{
	s_UseTool tool;
	tool.Clear();
	tool.ChartNumber = sc.ChartNumber;
	tool.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
	tool.LineNumber = lineId;
	tool.BeginDateTime = sc.BaseDateTimeIn[startBar];
	tool.BeginValue = (float)top;
	tool.EndDateTime = sc.BaseDateTimeIn[endBar];
	tool.EndValue = (float)bottom;
	tool.Color = fillColor;
	tool.SecondaryColor = fillColor;
	tool.TransparencyLevel = transparency;
	tool.LineWidth = 1;
	tool.AddMethod = UTAM_ADD_OR_ADJUST;
	tool.AddAsUserDrawnDrawing = 0;
	sc.UseTool(tool);
}

static void ADM_DrawTextFixed(SCStudyInterfaceRef sc, int lineId, int anchorBar,
	float relativeY, const SCString& text, COLORREF color, int fontSize, bool bold, int alignment)
{
	s_UseTool tool;
	tool.Clear();
	tool.ChartNumber = sc.ChartNumber;
	tool.DrawingType = DRAWING_TEXT;
	tool.LineNumber = lineId;
	tool.BeginDateTime = sc.BaseDateTimeIn[anchorBar];
	tool.UseRelativeVerticalValues = 1;
	tool.BeginValue = relativeY;
	tool.Color = color;
	tool.FontSize = 9;
	tool.FontBold = bold ? 1 : 0;
	tool.FontFace = "Arial";
	tool.Text = text;
	tool.TextAlignment = alignment;
	tool.AddMethod = UTAM_ADD_OR_ADJUST;
	tool.AddAsUserDrawnDrawing = 0;
	sc.UseTool(tool);
}

static void ADM_DrawRectFixed(SCStudyInterfaceRef sc, int lineId, int anchorBar,
	float relTop, float relBottom, COLORREF fillColor, int transparency)
{
	s_UseTool tool;
	tool.Clear();
	tool.ChartNumber = sc.ChartNumber;
	tool.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
	tool.LineNumber = lineId;
	tool.BeginDateTime = sc.BaseDateTimeIn[anchorBar];
	tool.EndDateTime = sc.BaseDateTimeIn[anchorBar];
	tool.UseRelativeVerticalValues = 1;
	tool.BeginValue = relTop;
	tool.EndValue = relBottom;
	tool.Color = fillColor;
	tool.SecondaryColor = fillColor;
	tool.TransparencyLevel = transparency;
	tool.AddMethod = UTAM_ADD_OR_ADJUST;
	tool.AddAsUserDrawnDrawing = 0;
	sc.UseTool(tool);
}

// ---------------------------------------------------------------------------
// Input indices - named so SetDefaults and the runtime read stay in lock-step
// ---------------------------------------------------------------------------
enum ADMInput
{
	// 01. Confluence Filters
	IN_FilterRules, IN_SierraExactMode, IN_UseWaddah, IN_UseLindaMacd, IN_UsePsar,
	IN_UseSupertrend, IN_UseAO, IN_UseHma, IN_UseT3, IN_UseFisher, IN_UseSqueezeMomentum,
	IN_MinAdx, IN_UseAdxvma, IN_UseLizardSuperTrend, IN_UseMultiTsi, IN_UseSmi,
	IN_RequireCandleDirection, IN_IgnoreDojis,
	// 02. Filter Parameters
	IN_WaddahIntensity, IN_LindaFast, IN_LindaSlow, IN_LindaSignal,
	IN_PsarAcceleration, IN_PsarMax, IN_PsarStep, IN_AdxPeriod, IN_HmaPeriod,
	IN_T3Period, IN_T3TCount, IN_T3VFactor, IN_StPeriod, IN_StMultiplier, IN_StAtrType,
	IN_SqueezePeriod, IN_SqueezeUsesOpen, IN_AdxvmaPeriod, IN_TsiSmooth1, IN_TsiSmooth2,
	IN_TsiSignalPeriod, IN_LizStAtrPeriod, IN_LizStMultiplier,
	IN_SmiPeriodK, IN_SmiSmooth1, IN_SmiSmooth2, IN_SmiSignalPeriod, IN_SmiOverbought,
	IN_SmiOversold, IN_SmiMidline, IN_SmiRequireSlope, IN_SmiDivergenceLookback,
	// 03. Signals / patterns display
	IN_ShowBuySell, IN_ShowBigArrow, IN_ShowSqueezeStars, IN_ShowVolumeImbalanceArrows,
	IN_ShowTrampoline, IN_TrampolineRsiHigh, IN_TrampolineRsiLow, IN_TrampolineToleranceTicks,
	IN_ShowThreeOutside, IN_ShowTweezers, IN_TweezerToleranceTicks, IN_ShowWickPattern,
	IN_ShowStairs, IN_ShowReversalSquare, IN_ShowShavedCandles, IN_ShavedToleranceTicks,
	IN_ShowBbEngulfShading,
	// 04. SMI divergence
	IN_EnableSmiDivergence, IN_SmiIncludeHiddenDivergence, IN_ShowSmiDivergenceLines,
	// 05. KAMA
	IN_ShowKama, IN_KamaFast, IN_KamaPeriod, IN_KamaSlow, IN_ShowKamaBounceMarkers, IN_KamaLineWidth,
	// 06. Evil Times
	IN_ShowEvilTimes, IN_EvilTimesChartOffsetHours,
	// 07. Signal layout
	IN_StackSignals, IN_StackStepTicks, IN_TextSize, IN_ShowSignalLabels,
	// 08. Bar color
	IN_BarColor, IN_WaddahBarOffset, IN_WaddahBrightnessMode, IN_WaddahBarGain,
	IN_WaddahAutoLength, IN_WaddahAutoFullMultiple, IN_LindaBrightnessMode,
	IN_LindaBarIntensity, IN_LindaAutoLength, IN_LindaAutoFullMultiple, IN_LindaBarOffset,
	// 09. Bollinger
	IN_BbPeriod, IN_BbStdDev, IN_ShowBollingerBands, IN_BbLineWidth,
	IN_ShowBollingerMiddle, IN_BbMidWidth,
	// 10. Gap engine
	IN_MinGapSizeTicks, IN_MinBarsBeforeValid, IN_MinBodyTicks, IN_MaxGapBarRangeTicks,
	IN_MaxActiveGaps, IN_EarlyTouch, IN_ValidTouch, IN_LinePriceMode, IN_ShowGapLine,
	IN_ShowGapZone, IN_ShowTouchMarker, IN_UseTouchedLineColor, IN_GapLineWidth,
	IN_GapWidthBySize, IN_GapMinWidth, IN_GapMaxWidth, IN_GapMaxWidthAtTicks,
	IN_GapZoneOpacity, IN_GapLineExtension, IN_GapProjectionBars,
	// 11. FC continuation
	IN_EnableContinuation, IN_UseBbMidFilterFc, IN_FcLocationSource, IN_FcLongBelowMidPct,
	IN_FcShortAboveMidPct, IN_ConfirmMode, IN_ConfirmBarsAfterTouch,
	IN_RequireSignalCandleDirection, IN_RequireCorrectApproach, IN_ShowContinuationMarkers,
	// 12. OBR
	IN_EnableObr, IN_PaintObrBars, IN_ObrMarkerStyle, IN_UseBbMidFilterObr,
	IN_ObrLocationMode, IN_AllowObrOutsideBand, IN_BearObrTolTicks, IN_BullObrTolTicks,
	IN_ObrPierceTicks, IN_ObrRequireLargerBody, IN_ObrMinBarSizeTicks, IN_ObrShowEntryLine,
	IN_ObrEntryLineBars, IN_ObrShowZones, IN_ObrZoneBars, IN_ObrZoneOpacity,
	// 13. Bollinger Gap
	IN_EnableBollingerGap, IN_BgLocationMode, IN_BgProximityTicks, IN_ShowBgMarkers,
	// 14. Spiderweb
	IN_EnableSpiderweb, IN_ShowSpiderwebText, IN_SpiderwebDistanceTicks,
	IN_SpiderwebLineCount, IN_SpiderwebFontSize,
	// 15. Time filter / targets
	IN_UseSignalTimeFilter, IN_SignalStartTime, IN_SignalEndTime,
	IN_SuggestedStopOffsetTicks, IN_TargetMode, IN_FixedTargetTicks,
	// 16. Vodka Shot
	IN_EnableVodka, IN_VodkaRequireWave, IN_VodkaVolumeScale, IN_VodkaVolumeLength,
	IN_VodkaCooldownBars, IN_VodkaTrendLength, IN_VodkaMacdFast, IN_VodkaMacdSlow,
	IN_VodkaMacdSignal, IN_VodkaGlyphSize,
	// 17. Wave
	IN_WaveLookback, IN_WaveIgnoreDojis, IN_WaveDojiMaxTicks, IN_WaveFilterFc,
	IN_WaveFilterBg, IN_WaveFilterObr,
	// 18. Adaptive thresholds
	IN_AdaptiveThresholds, IN_AdaptAtrLength, IN_AdaptAverageLength, IN_AdaptMinRatio,
	IN_AdaptMaxRatio, IN_AdaptAdxFloor, IN_AdaptGapFilters, IN_AdaptBandDistances, IN_AdaptSpiderweb,
	// 19. Alerts
	IN_EnableAlerts, IN_PlaySounds,
	IN_AlertBuy, IN_AlertSell, IN_AlertBigBuy, IN_AlertBigSell, IN_AlertSqueezeUp, IN_AlertSqueezeDown,
	IN_AlertVolImbBuy, IN_AlertVolImbSell, IN_AlertGapTouchBull, IN_AlertGapTouchBear,
	IN_AlertFcLong, IN_AlertFcShort, IN_AlertBgLong, IN_AlertBgShort, IN_AlertObrLong, IN_AlertObrShort,
	IN_AlertTramp, IN_AlertThreeOutside, IN_AlertTweezer, IN_AlertWickPat, IN_AlertStairs,
	IN_AlertRevSquare, IN_AlertKamaBounce, IN_AlertVodkaBuy, IN_AlertVodkaSell,
	IN_AlertSmiDivBull, IN_AlertSmiDivBear, IN_AlertSpiderweb,
	IN_SoundBullish, IN_SoundBearish, IN_SoundNeutral,
	// 20. Offsets
	IN_BuyTriangleOffsetTicks, IN_SellTriangleOffsetTicks, IN_BigArrowUpOffsetTicks,
	IN_BigArrowDownOffsetTicks, IN_SqueezeStarUpOffsetTicks, IN_SqueezeStarDownOffsetTicks,
	IN_VolImbUpOffsetTicks, IN_VolImbDownOffsetTicks, IN_FcMarkerOffsetTicks,
	IN_BgMarkerOffsetTicks, IN_ObrMarkerOffsetTicks, IN_ObrArrowOffsetTicks,
	IN_SignalLabelOffsetTicks, IN_PatternLabelOffsetTicks, IN_TrampolineOffsetTicks,
	IN_ReversalSquareOffsetTicks, IN_EvilTimesOffsetTicks, IN_VodkaUpOffsetTicks, IN_VodkaDownOffsetTicks,
	// 21. Colors
	IN_WaddahUpColor, IN_WaddahDownColor, IN_BuyColor, IN_SellColor, IN_StarColor, IN_VolImbColor,
	IN_GapBullLineColor, IN_GapBearLineColor, IN_GapTouchedColor, IN_GapInvalidColor,
	IN_GapBullZoneColor, IN_GapBearZoneColor, IN_TouchMarkerColor,
	IN_BgLongColor, IN_BgShortColor, IN_FcLongColor, IN_FcShortColor,
	IN_ObrBullColor, IN_ObrBearColor, IN_ObrLightZoneColor, IN_ObrShadowZoneColor,
	IN_SpiderwebColor, IN_PatternTextColor, IN_PatternBullBackColor, IN_PatternBearBackColor,
	IN_TrampTextColor, IN_TrampBackColor, IN_BbEngulfGreenColor, IN_BbEngulfRedColor,
	IN_ShavedGreenColor, IN_ShavedRedColor, IN_KamaColor, IN_BbUpperColor, IN_BbLowerColor, IN_BbMidColor,
	IN_SmiBullDivColor, IN_SmiBearDivColor, IN_SmiHiddenBullDivColor, IN_SmiHiddenBearDivColor,
	IN_EvilTimesColor, IN_VodkaUpColor, IN_VodkaDownColor,
	// 22. Dashboard
	IN_ShowDashboard, IN_DashboardCorner, IN_DashboardFontSize, IN_DashboardOpacity,
	IN_COUNT
};
// ---------------------------------------------------------------------------
// Main study function
// ---------------------------------------------------------------------------
SCSFExport scsf_ADITRADYMEGATRON(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "Aditrady Megatron";
		sc.StudyDescription =
			"Unified confluence signals, candle patterns, gap engine (Volume Imbalance / "
			"FC continuation / Bollinger Gap / Outside Bar Reversal), spiderweb warning, "
			"SMI + divergence, Wave state, Vodka Shot, KAMA and Bollinger Bands, with a "
			"compact on-chart dashboard. Ported from the ADITRADYMEGATRON NinjaScript indicator.";
		sc.AutoLoop = 1;
		sc.GraphRegion = 0;
		sc.FreeDLL = 0;
		sc.ScaleRangeType = SCALE_SAMEASREGION;
		sc.CalculationPrecedence = LOW_PREC_LEVEL;

		// ---- Subgraphs (matches the original plot layout) ----
		sc.Subgraph[SG_BUY].Name = "Buy Triangle";
		sc.Subgraph[SG_BUY].DrawStyle = DRAWSTYLE_IGNORE; // drawn as a glyph, see DrawSignalGlyph()
		sc.Subgraph[SG_BUY].DrawZeros = false;
		sc.Subgraph[SG_SELL].Name = "Sell Triangle";
		sc.Subgraph[SG_SELL].DrawStyle = DRAWSTYLE_IGNORE;
		sc.Subgraph[SG_SELL].DrawZeros = false;
		sc.Subgraph[SG_KAMA].Name = "KAMA";
		sc.Subgraph[SG_KAMA].DrawStyle = DRAWSTYLE_LINE;
		sc.Subgraph[SG_KAMA].PrimaryColor = ADM_RGB(184, 134, 11);
		sc.Subgraph[SG_KAMA].LineWidth = 2;
		sc.Subgraph[SG_BBU].Name = "Bollinger Upper";
		sc.Subgraph[SG_BBU].DrawStyle = DRAWSTYLE_LINE;
		sc.Subgraph[SG_BBU].PrimaryColor = ADM_RGB(30, 144, 255);
		sc.Subgraph[SG_BBM].Name = "Bollinger Middle";
		sc.Subgraph[SG_BBM].DrawStyle = DRAWSTYLE_LINE;
		sc.Subgraph[SG_BBM].PrimaryColor = ADM_RGB(218, 165, 32);
		sc.Subgraph[SG_BBL].Name = "Bollinger Lower";
		sc.Subgraph[SG_BBL].DrawStyle = DRAWSTYLE_LINE;
		sc.Subgraph[SG_BBL].PrimaryColor = ADM_RGB(30, 144, 255);
		sc.Subgraph[SG_BARCOLOR].Name = "Bar Color";
		sc.Subgraph[SG_BARCOLOR].DrawStyle = DRAWSTYLE_IGNORE; // set to DRAWSTYLE_COLOR_BAR at runtime when a bar-color mode is active
		const char* hiddenNames[] = {
			"Lower Touch","Upper Touch","Middle Close","Confluence","Big Arrow","Squeeze Star",
			"Pattern Code","Volume Imbalance","Gap Touched","Invalid Gap Touched","Continuation",
			"Bollinger Gap","Outside Bar Reversal","Active Gaps","Valid Gaps","Pending Continuations",
			"Spiderweb Count","Spiderweb Warning","Nearest Gap Price","Suggested Entry",
			"Suggested Stop","Suggested Target","Signal Direction","Signal Code","Master Long",
			"Master Short","SMI","SMI Signal","SMI State","SMI Divergence","Wave State",
			"Vodka Signal","Volatility Ratio"
		};
		for (int i = SG_LOWTOUCH; i < SG_COUNT; i++)
		{
			sc.Subgraph[i].Name = hiddenNames[i - SG_LOWTOUCH];
			sc.Subgraph[i].DrawStyle = DRAWSTYLE_IGNORE;
			sc.Subgraph[i].DrawZeros = false;
		}

		// ================= INPUTS =================
		// ---- 01. Confluence Filters ----
		sc.Input[IN_FilterRules].Name = "01 - Filter Rules";
		sc.Input[IN_FilterRules].SetCustomInputStrings("Sierra;NinjaLegacy");
		sc.Input[IN_FilterRules].SetCustomInputIndex(0);
		sc.Input[IN_SierraExactMode].Name = "01 - Sierra Exact Mode";
		sc.Input[IN_SierraExactMode].SetYesNo(false);
		sc.Input[IN_UseWaddah].Name = "01 - Filter: Waddah Explosion";
		sc.Input[IN_UseWaddah].SetYesNo(true);
		sc.Input[IN_UseLindaMacd].Name = "01 - Filter: Linda MACD";
		sc.Input[IN_UseLindaMacd].SetYesNo(true);
		sc.Input[IN_UsePsar].Name = "01 - Filter: Parabolic SAR";
		sc.Input[IN_UsePsar].SetYesNo(true);
		sc.Input[IN_UseSupertrend].Name = "01 - Filter: SuperTrend";
		sc.Input[IN_UseSupertrend].SetYesNo(false);
		sc.Input[IN_UseAO].Name = "01 - Filter: Awesome Oscillator";
		sc.Input[IN_UseAO].SetYesNo(false);
		sc.Input[IN_UseHma].Name = "01 - Filter: Hull Moving Avg";
		sc.Input[IN_UseHma].SetYesNo(true);
		sc.Input[IN_UseT3].Name = "01 - Filter: T3";
		sc.Input[IN_UseT3].SetYesNo(false);
		sc.Input[IN_UseFisher].Name = "01 - Filter: Fisher Transform";
		sc.Input[IN_UseFisher].SetYesNo(true);
		sc.Input[IN_UseSqueezeMomentum].Name = "01 - Filter: Squeeze Momentum";
		sc.Input[IN_UseSqueezeMomentum].SetYesNo(false);
		sc.Input[IN_MinAdx].Name = "01 - Minimum ADX";
		sc.Input[IN_MinAdx].SetInt(11); sc.Input[IN_MinAdx].SetIntLimits(0, 100);
		sc.Input[IN_UseAdxvma].Name = "01 - Filter: ADXVMA Trend";
		sc.Input[IN_UseAdxvma].SetYesNo(false);
		sc.Input[IN_UseLizardSuperTrend].Name = "01 - Filter: Median SuperTrend (Lizard)";
		sc.Input[IN_UseLizardSuperTrend].SetYesNo(false);
		sc.Input[IN_UseMultiTsi].Name = "01 - Filter: Multi TSI";
		sc.Input[IN_UseMultiTsi].SetYesNo(false);
		sc.Input[IN_UseSmi].Name = "01 - Filter: SMI";
		sc.Input[IN_UseSmi].SetYesNo(false);
		sc.Input[IN_RequireCandleDirection].Name = "01 - Require Candle Direction";
		sc.Input[IN_RequireCandleDirection].SetYesNo(false);
		sc.Input[IN_IgnoreDojis].Name = "01 - Ignore Dojis";
		sc.Input[IN_IgnoreDojis].SetYesNo(true);

		// ---- 02. Filter Parameters ----
		sc.Input[IN_WaddahIntensity].Name = "02 - Waddah Intensity";
		sc.Input[IN_WaddahIntensity].SetInt(150); sc.Input[IN_WaddahIntensity].SetIntLimits(1, 100000);
		sc.Input[IN_LindaFast].Name = "02 - Linda MACD Fast";
		sc.Input[IN_LindaFast].SetInt(3); sc.Input[IN_LindaFast].SetIntLimits(1, 1000);
		sc.Input[IN_LindaSlow].Name = "02 - Linda MACD Slow";
		sc.Input[IN_LindaSlow].SetInt(9); sc.Input[IN_LindaSlow].SetIntLimits(1, 1000);
		sc.Input[IN_LindaSignal].Name = "02 - Linda MACD Signal";
		sc.Input[IN_LindaSignal].SetInt(16); sc.Input[IN_LindaSignal].SetIntLimits(1, 1000);
		sc.Input[IN_PsarAcceleration].Name = "02 - PSAR Acceleration";
		sc.Input[IN_PsarAcceleration].SetFloat(0.02f); sc.Input[IN_PsarAcceleration].SetFloatLimits(0.001f, 1.0f);
		sc.Input[IN_PsarMax].Name = "02 - PSAR Max";
		sc.Input[IN_PsarMax].SetFloat(0.2f); sc.Input[IN_PsarMax].SetFloatLimits(0.001f, 1.0f);
		sc.Input[IN_PsarStep].Name = "02 - PSAR Step";
		sc.Input[IN_PsarStep].SetFloat(0.02f); sc.Input[IN_PsarStep].SetFloatLimits(0.001f, 1.0f);
		sc.Input[IN_AdxPeriod].Name = "02 - ADX Period";
		sc.Input[IN_AdxPeriod].SetInt(14); sc.Input[IN_AdxPeriod].SetIntLimits(1, 500);
		sc.Input[IN_HmaPeriod].Name = "02 - HMA Period";
		sc.Input[IN_HmaPeriod].SetInt(10); sc.Input[IN_HmaPeriod].SetIntLimits(2, 500);
		sc.Input[IN_T3Period].Name = "02 - T3 Period";
		sc.Input[IN_T3Period].SetInt(10); sc.Input[IN_T3Period].SetIntLimits(1, 500);
		sc.Input[IN_T3TCount].Name = "02 - T3 Stage Count";
		sc.Input[IN_T3TCount].SetInt(3); sc.Input[IN_T3TCount].SetIntLimits(1, 6);
		sc.Input[IN_T3VFactor].Name = "02 - T3 V Factor";
		sc.Input[IN_T3VFactor].SetFloat(0.84f); sc.Input[IN_T3VFactor].SetFloatLimits(0.0f, 1.0f);
		sc.Input[IN_StPeriod].Name = "02 - SuperTrend Period";
		sc.Input[IN_StPeriod].SetInt(11); sc.Input[IN_StPeriod].SetIntLimits(1, 500);
		sc.Input[IN_StMultiplier].Name = "02 - SuperTrend Multiplier";
		sc.Input[IN_StMultiplier].SetFloat(2.0f); sc.Input[IN_StMultiplier].SetFloatLimits(0.1f, 20.0f);
		sc.Input[IN_StAtrType].Name = "02 - SuperTrend ATR Type";
		sc.Input[IN_StAtrType].SetCustomInputStrings("Hull;Wilder");
		sc.Input[IN_StAtrType].SetCustomInputIndex(0);
		sc.Input[IN_SqueezePeriod].Name = "02 - Squeeze Period";
		sc.Input[IN_SqueezePeriod].SetInt(20); sc.Input[IN_SqueezePeriod].SetIntLimits(2, 500);
		sc.Input[IN_SqueezeUsesOpen].Name = "02 - Squeeze Uses Open";
		sc.Input[IN_SqueezeUsesOpen].SetYesNo(true);
		sc.Input[IN_AdxvmaPeriod].Name = "02 - ADXVMA Period";
		sc.Input[IN_AdxvmaPeriod].SetInt(8); sc.Input[IN_AdxvmaPeriod].SetIntLimits(1, 200);
		sc.Input[IN_TsiSmooth1].Name = "02 - TSI Smooth 1";
		sc.Input[IN_TsiSmooth1].SetInt(20); sc.Input[IN_TsiSmooth1].SetIntLimits(1, 200);
		sc.Input[IN_TsiSmooth2].Name = "02 - TSI Smooth 2";
		sc.Input[IN_TsiSmooth2].SetInt(9); sc.Input[IN_TsiSmooth2].SetIntLimits(1, 200);
		sc.Input[IN_TsiSignalPeriod].Name = "02 - TSI Signal Period";
		sc.Input[IN_TsiSignalPeriod].SetInt(5); sc.Input[IN_TsiSignalPeriod].SetIntLimits(1, 200);
		sc.Input[IN_LizStAtrPeriod].Name = "02 - Lizard SuperTrend ATR Period";
		sc.Input[IN_LizStAtrPeriod].SetInt(15); sc.Input[IN_LizStAtrPeriod].SetIntLimits(1, 500);
		sc.Input[IN_LizStMultiplier].Name = "02 - Lizard SuperTrend Multiplier";
		sc.Input[IN_LizStMultiplier].SetFloat(2.5f); sc.Input[IN_LizStMultiplier].SetFloatLimits(0.1f, 20.0f);
		sc.Input[IN_SmiPeriodK].Name = "02 - SMI Period K";
		sc.Input[IN_SmiPeriodK].SetInt(8); sc.Input[IN_SmiPeriodK].SetIntLimits(1, 200);
		sc.Input[IN_SmiSmooth1].Name = "02 - SMI Smooth 1";
		sc.Input[IN_SmiSmooth1].SetInt(3); sc.Input[IN_SmiSmooth1].SetIntLimits(1, 200);
		sc.Input[IN_SmiSmooth2].Name = "02 - SMI Smooth 2";
		sc.Input[IN_SmiSmooth2].SetInt(5); sc.Input[IN_SmiSmooth2].SetIntLimits(1, 200);
		sc.Input[IN_SmiSignalPeriod].Name = "02 - SMI Signal Period";
		sc.Input[IN_SmiSignalPeriod].SetInt(7); sc.Input[IN_SmiSignalPeriod].SetIntLimits(1, 200);
		sc.Input[IN_SmiOverbought].Name = "02 - SMI Overbought";
		sc.Input[IN_SmiOverbought].SetFloat(50.0f);
		sc.Input[IN_SmiOversold].Name = "02 - SMI Oversold";
		sc.Input[IN_SmiOversold].SetFloat(-50.0f);
		sc.Input[IN_SmiMidline].Name = "02 - SMI Midline";
		sc.Input[IN_SmiMidline].SetFloat(0.0f);
		sc.Input[IN_SmiRequireSlope].Name = "02 - SMI Require Slope";
		sc.Input[IN_SmiRequireSlope].SetYesNo(true);
		sc.Input[IN_SmiDivergenceLookback].Name = "02 - SMI Divergence Lookback";
		sc.Input[IN_SmiDivergenceLookback].SetInt(10); sc.Input[IN_SmiDivergenceLookback].SetIntLimits(2, 200);

		// ---- 03. Signals / Patterns Display ----
		sc.Input[IN_ShowBuySell].Name = "03 - Show Buy/Sell Triangles";
		sc.Input[IN_ShowBuySell].SetYesNo(true);
		sc.Input[IN_ShowBigArrow].Name = "03 - Show Big Arrow (MACD/PSAR)";
		sc.Input[IN_ShowBigArrow].SetYesNo(true);
		sc.Input[IN_ShowSqueezeStars].Name = "03 - Show Squeeze Stars";
		sc.Input[IN_ShowSqueezeStars].SetYesNo(true);
		sc.Input[IN_ShowVolumeImbalanceArrows].Name = "03 - Show Volume Imbalance Arrows";
		sc.Input[IN_ShowVolumeImbalanceArrows].SetYesNo(true);
		sc.Input[IN_ShowTrampoline].Name = "03 - Show Trampoline";
		sc.Input[IN_ShowTrampoline].SetYesNo(true);
		sc.Input[IN_TrampolineRsiHigh].Name = "03 - Trampoline RSI High";
		sc.Input[IN_TrampolineRsiHigh].SetInt(80); sc.Input[IN_TrampolineRsiHigh].SetIntLimits(50, 100);
		sc.Input[IN_TrampolineRsiLow].Name = "03 - Trampoline RSI Low";
		sc.Input[IN_TrampolineRsiLow].SetInt(20); sc.Input[IN_TrampolineRsiLow].SetIntLimits(0, 50);
		sc.Input[IN_TrampolineToleranceTicks].Name = "03 - Trampoline Tolerance Ticks";
		sc.Input[IN_TrampolineToleranceTicks].SetInt(1); sc.Input[IN_TrampolineToleranceTicks].SetIntLimits(0, 100);
		sc.Input[IN_ShowThreeOutside].Name = "03 - Show Three Outside";
		sc.Input[IN_ShowThreeOutside].SetYesNo(true);
		sc.Input[IN_ShowTweezers].Name = "03 - Show Tweezers";
		sc.Input[IN_ShowTweezers].SetYesNo(true);
		sc.Input[IN_TweezerToleranceTicks].Name = "03 - Tweezer Tolerance Ticks";
		sc.Input[IN_TweezerToleranceTicks].SetInt(3); sc.Input[IN_TweezerToleranceTicks].SetIntLimits(0, 100);
		sc.Input[IN_ShowWickPattern].Name = "03 - Show Wick Pattern";
		sc.Input[IN_ShowWickPattern].SetYesNo(true);
		sc.Input[IN_ShowStairs].Name = "03 - Show Stairs";
		sc.Input[IN_ShowStairs].SetYesNo(false);
		sc.Input[IN_ShowReversalSquare].Name = "03 - Show Reversal Square";
		sc.Input[IN_ShowReversalSquare].SetYesNo(false);
		sc.Input[IN_ShowShavedCandles].Name = "03 - Show Shaved Candles";
		sc.Input[IN_ShowShavedCandles].SetYesNo(true);
		sc.Input[IN_ShavedToleranceTicks].Name = "03 - Shaved Tolerance Ticks";
		sc.Input[IN_ShavedToleranceTicks].SetInt(0); sc.Input[IN_ShavedToleranceTicks].SetIntLimits(0, 100);
		sc.Input[IN_ShowBbEngulfShading].Name = "03 - Show BB Engulf Shading";
		sc.Input[IN_ShowBbEngulfShading].SetYesNo(true);

		// ---- 04. SMI Divergence ----
		sc.Input[IN_EnableSmiDivergence].Name = "04 - Enable SMI Divergence";
		sc.Input[IN_EnableSmiDivergence].SetYesNo(false);
		sc.Input[IN_SmiIncludeHiddenDivergence].Name = "04 - Include Hidden Divergence";
		sc.Input[IN_SmiIncludeHiddenDivergence].SetYesNo(true);
		sc.Input[IN_ShowSmiDivergenceLines].Name = "04 - Show SMI Divergence Lines";
		sc.Input[IN_ShowSmiDivergenceLines].SetYesNo(true);

		// ---- 05. KAMA ----
		sc.Input[IN_ShowKama].Name = "05 - Show KAMA";
		sc.Input[IN_ShowKama].SetYesNo(true);
		sc.Input[IN_KamaFast].Name = "05 - KAMA Fast";
		sc.Input[IN_KamaFast].SetInt(2); sc.Input[IN_KamaFast].SetIntLimits(1, 200);
		sc.Input[IN_KamaPeriod].Name = "05 - KAMA Period";
		sc.Input[IN_KamaPeriod].SetInt(9); sc.Input[IN_KamaPeriod].SetIntLimits(1, 500);
		sc.Input[IN_KamaSlow].Name = "05 - KAMA Slow";
		sc.Input[IN_KamaSlow].SetInt(109); sc.Input[IN_KamaSlow].SetIntLimits(1, 500);
		sc.Input[IN_ShowKamaBounceMarkers].Name = "05 - Show KAMA Bounce Markers";
		sc.Input[IN_ShowKamaBounceMarkers].SetYesNo(false);
		sc.Input[IN_KamaLineWidth].Name = "05 - KAMA Line Width";
		sc.Input[IN_KamaLineWidth].SetInt(2); sc.Input[IN_KamaLineWidth].SetIntLimits(1, 10);

		// ---- 06. Evil Times ----
		sc.Input[IN_ShowEvilTimes].Name = "06 - Show Evil Times Session Labels";
		sc.Input[IN_ShowEvilTimes].SetYesNo(false);
		sc.Input[IN_EvilTimesChartOffsetHours].Name = "06 - Evil Times Chart Offset Hours";
		sc.Input[IN_EvilTimesChartOffsetHours].SetInt(0); sc.Input[IN_EvilTimesChartOffsetHours].SetIntLimits(-12, 12);

		// ---- 07. Signal layout ----
		sc.Input[IN_StackSignals].Name = "07 - Stack Overlapping Signals";
		sc.Input[IN_StackSignals].SetYesNo(true);
		sc.Input[IN_StackStepTicks].Name = "07 - Stack Step Ticks";
		sc.Input[IN_StackStepTicks].SetInt(8); sc.Input[IN_StackStepTicks].SetIntLimits(1, 200);
		sc.Input[IN_TextSize].Name = "07 - Label Text Size";
		sc.Input[IN_TextSize].SetInt(9); sc.Input[IN_TextSize].SetIntLimits(6, 30);
		sc.Input[IN_ShowSignalLabels].Name = "07 - Show Signal Labels (FC/BG/OBR)";
		sc.Input[IN_ShowSignalLabels].SetYesNo(true);

		// ---- 08. Bar Color ----
		sc.Input[IN_BarColor].Name = "08 - Bar Color Mode";
		sc.Input[IN_BarColor].SetCustomInputStrings("None;Waddah;LindaMACD;Supertrend");
		sc.Input[IN_BarColor].SetCustomInputIndex(0);
		sc.Input[IN_WaddahBarOffset].Name = "08 - Waddah Bar Offset";
		sc.Input[IN_WaddahBarOffset].SetInt(80); sc.Input[IN_WaddahBarOffset].SetIntLimits(0, 255);
		sc.Input[IN_WaddahBrightnessMode].Name = "08 - Waddah Brightness Mode";
		sc.Input[IN_WaddahBrightnessMode].SetCustomInputStrings("Fixed;AutoScale");
		sc.Input[IN_WaddahBrightnessMode].SetCustomInputIndex(0);
		sc.Input[IN_WaddahBarGain].Name = "08 - Waddah Bar Gain";
		sc.Input[IN_WaddahBarGain].SetFloat(1.0f);
		sc.Input[IN_WaddahAutoLength].Name = "08 - Waddah Auto Length";
		sc.Input[IN_WaddahAutoLength].SetInt(200); sc.Input[IN_WaddahAutoLength].SetIntLimits(2, 5000);
		sc.Input[IN_WaddahAutoFullMultiple].Name = "08 - Waddah Auto Full Multiple";
		sc.Input[IN_WaddahAutoFullMultiple].SetFloat(2.0f);
		sc.Input[IN_LindaBrightnessMode].Name = "08 - Linda Brightness Mode";
		sc.Input[IN_LindaBrightnessMode].SetCustomInputStrings("Fixed;AutoScale");
		sc.Input[IN_LindaBrightnessMode].SetCustomInputIndex(0);
		sc.Input[IN_LindaBarIntensity].Name = "08 - Linda Bar Intensity";
		sc.Input[IN_LindaBarIntensity].SetInt(40); sc.Input[IN_LindaBarIntensity].SetIntLimits(0, 255);
		sc.Input[IN_LindaAutoLength].Name = "08 - Linda Auto Length";
		sc.Input[IN_LindaAutoLength].SetInt(200); sc.Input[IN_LindaAutoLength].SetIntLimits(2, 5000);
		sc.Input[IN_LindaAutoFullMultiple].Name = "08 - Linda Auto Full Multiple";
		sc.Input[IN_LindaAutoFullMultiple].SetFloat(2.0f);
		sc.Input[IN_LindaBarOffset].Name = "08 - Linda Bar Offset";
		sc.Input[IN_LindaBarOffset].SetInt(40); sc.Input[IN_LindaBarOffset].SetIntLimits(0, 255);

		// ---- 09. Bollinger ----
		sc.Input[IN_BbPeriod].Name = "09 - Bollinger Period";
		sc.Input[IN_BbPeriod].SetInt(20); sc.Input[IN_BbPeriod].SetIntLimits(2, 500);
		sc.Input[IN_BbStdDev].Name = "09 - Bollinger StdDev";
		sc.Input[IN_BbStdDev].SetFloat(2.0f); sc.Input[IN_BbStdDev].SetFloatLimits(0.1f, 10.0f);
		sc.Input[IN_ShowBollingerBands].Name = "09 - Show Bollinger Bands";
		sc.Input[IN_ShowBollingerBands].SetYesNo(true);
		sc.Input[IN_BbLineWidth].Name = "09 - Bollinger Band Line Width";
		sc.Input[IN_BbLineWidth].SetInt(1); sc.Input[IN_BbLineWidth].SetIntLimits(1, 10);
		sc.Input[IN_ShowBollingerMiddle].Name = "09 - Show Bollinger Middle";
		sc.Input[IN_ShowBollingerMiddle].SetYesNo(true);
		sc.Input[IN_BbMidWidth].Name = "09 - Bollinger Middle Width";
		sc.Input[IN_BbMidWidth].SetInt(1); sc.Input[IN_BbMidWidth].SetIntLimits(1, 10);

		// ---- 10. Gap Engine ----
		sc.Input[IN_MinGapSizeTicks].Name = "10 - Min Gap Size Ticks";
		sc.Input[IN_MinGapSizeTicks].SetInt(1); sc.Input[IN_MinGapSizeTicks].SetIntLimits(1, 1000);
		sc.Input[IN_MinBarsBeforeValid].Name = "10 - Min Bars Before Valid";
		sc.Input[IN_MinBarsBeforeValid].SetInt(3); sc.Input[IN_MinBarsBeforeValid].SetIntLimits(0, 500);
		sc.Input[IN_MinBodyTicks].Name = "10 - Min Body Ticks";
		sc.Input[IN_MinBodyTicks].SetInt(0); sc.Input[IN_MinBodyTicks].SetIntLimits(0, 1000);
		sc.Input[IN_MaxGapBarRangeTicks].Name = "10 - Max Gap Bar Range Ticks (0=off)";
		sc.Input[IN_MaxGapBarRangeTicks].SetInt(0); sc.Input[IN_MaxGapBarRangeTicks].SetIntLimits(0, 5000);
		sc.Input[IN_MaxActiveGaps].Name = "10 - Max Active Gaps";
		sc.Input[IN_MaxActiveGaps].SetInt(300); sc.Input[IN_MaxActiveGaps].SetIntLimits(1, 5000);
		sc.Input[IN_EarlyTouch].Name = "10 - Early Touch Handling";
		sc.Input[IN_EarlyTouch].SetCustomInputStrings("StopLineImmediately;IgnoreTouchesUntilValid");
		sc.Input[IN_EarlyTouch].SetCustomInputIndex(0);
		sc.Input[IN_ValidTouch].Name = "10 - Valid Touch Behavior";
		sc.Input[IN_ValidTouch].SetCustomInputStrings("StopLineOnly;StopLineAndMarkContinuation");
		sc.Input[IN_ValidTouch].SetCustomInputIndex(1);
		sc.Input[IN_LinePriceMode].Name = "10 - Gap Line Price Mode";
		sc.Input[IN_LinePriceMode].SetCustomInputStrings("Midpoint;PreviousCloseEdge;CurrentOpenEdge");
		sc.Input[IN_LinePriceMode].SetCustomInputIndex(2);
		sc.Input[IN_ShowGapLine].Name = "10 - Show Gap Line";
		sc.Input[IN_ShowGapLine].SetYesNo(true);
		sc.Input[IN_ShowGapZone].Name = "10 - Show Gap Zone";
		sc.Input[IN_ShowGapZone].SetYesNo(false);
		sc.Input[IN_ShowTouchMarker].Name = "10 - Show Touch Marker";
		sc.Input[IN_ShowTouchMarker].SetYesNo(false);
		sc.Input[IN_UseTouchedLineColor].Name = "10 - Use Touched Line Color";
		sc.Input[IN_UseTouchedLineColor].SetYesNo(false);
		sc.Input[IN_GapLineWidth].Name = "10 - Gap Line Width";
		sc.Input[IN_GapLineWidth].SetInt(2); sc.Input[IN_GapLineWidth].SetIntLimits(1, 10);
		sc.Input[IN_GapWidthBySize].Name = "10 - Gap Width Scales With Size";
		sc.Input[IN_GapWidthBySize].SetYesNo(true);
		sc.Input[IN_GapMinWidth].Name = "10 - Gap Min Width";
		sc.Input[IN_GapMinWidth].SetInt(1); sc.Input[IN_GapMinWidth].SetIntLimits(1, 10);
		sc.Input[IN_GapMaxWidth].Name = "10 - Gap Max Width";
		sc.Input[IN_GapMaxWidth].SetInt(6); sc.Input[IN_GapMaxWidth].SetIntLimits(1, 20);
		sc.Input[IN_GapMaxWidthAtTicks].Name = "10 - Gap Max Width At Ticks";
		sc.Input[IN_GapMaxWidthAtTicks].SetInt(20); sc.Input[IN_GapMaxWidthAtTicks].SetIntLimits(1, 5000);
		sc.Input[IN_GapZoneOpacity].Name = "10 - Gap Zone Opacity (0-100)";
		sc.Input[IN_GapZoneOpacity].SetInt(12); sc.Input[IN_GapZoneOpacity].SetIntLimits(0, 100);
		sc.Input[IN_GapLineExtension].Name = "10 - Gap Line Extension";
		sc.Input[IN_GapLineExtension].SetCustomInputStrings("Projected;ToCurrentBar");
		sc.Input[IN_GapLineExtension].SetCustomInputIndex(0);
		sc.Input[IN_GapProjectionBars].Name = "10 - Gap Projection Bars";
		sc.Input[IN_GapProjectionBars].SetInt(300); sc.Input[IN_GapProjectionBars].SetIntLimits(1, 5000);

		// ---- 11. FC Continuation ----
		sc.Input[IN_EnableContinuation].Name = "11 - Enable FC Continuation";
		sc.Input[IN_EnableContinuation].SetYesNo(true);
		sc.Input[IN_UseBbMidFilterFc].Name = "11 - FC: Use Bollinger Midpoint Filter";
		sc.Input[IN_UseBbMidFilterFc].SetYesNo(true);
		sc.Input[IN_FcLocationSource].Name = "11 - FC Location Source";
		sc.Input[IN_FcLocationSource].SetCustomInputStrings("Close;WickExtreme;HLC3;BodyMidpoint");
		sc.Input[IN_FcLocationSource].SetCustomInputIndex(1);
		sc.Input[IN_FcLongBelowMidPct].Name = "11 - FC Long Below Mid Pct";
		sc.Input[IN_FcLongBelowMidPct].SetFloat(50.0f); sc.Input[IN_FcLongBelowMidPct].SetFloatLimits(0.0f, 100.0f);
		sc.Input[IN_FcShortAboveMidPct].Name = "11 - FC Short Above Mid Pct";
		sc.Input[IN_FcShortAboveMidPct].SetFloat(50.0f); sc.Input[IN_FcShortAboveMidPct].SetFloatLimits(0.0f, 100.0f);
		sc.Input[IN_ConfirmMode].Name = "11 - FC Confirmation Mode";
		sc.Input[IN_ConfirmMode].SetCustomInputStrings("TouchOnly;RequireCloseBeyondLine;RequireCloseBeyondFullZone");
		sc.Input[IN_ConfirmMode].SetCustomInputIndex(2);
		sc.Input[IN_ConfirmBarsAfterTouch].Name = "11 - FC Confirm Bars After Touch";
		sc.Input[IN_ConfirmBarsAfterTouch].SetInt(2); sc.Input[IN_ConfirmBarsAfterTouch].SetIntLimits(0, 50);
		sc.Input[IN_RequireSignalCandleDirection].Name = "11 - FC Require Signal Candle Direction";
		sc.Input[IN_RequireSignalCandleDirection].SetYesNo(true);
		sc.Input[IN_RequireCorrectApproach].Name = "11 - FC Require Correct Approach";
		sc.Input[IN_RequireCorrectApproach].SetYesNo(true);
		sc.Input[IN_ShowContinuationMarkers].Name = "11 - Show FC Markers";
		sc.Input[IN_ShowContinuationMarkers].SetYesNo(true);

		// ---- 12. OBR ----
		sc.Input[IN_EnableObr].Name = "12 - Enable Outside Bar Reversal";
		sc.Input[IN_EnableObr].SetYesNo(true);
		sc.Input[IN_PaintObrBars].Name = "12 - Paint OBR Bars";
		sc.Input[IN_PaintObrBars].SetYesNo(true);
		sc.Input[IN_ObrMarkerStyle].Name = "12 - OBR Marker Style";
		sc.Input[IN_ObrMarkerStyle].SetCustomInputStrings("Diamond;Arrow;None");
		sc.Input[IN_ObrMarkerStyle].SetCustomInputIndex(0);
		sc.Input[IN_UseBbMidFilterObr].Name = "12 - OBR: Use Bollinger Midpoint Filter";
		sc.Input[IN_UseBbMidFilterObr].SetYesNo(true);
		sc.Input[IN_ObrLocationMode].Name = "12 - OBR Location Mode";
		sc.Input[IN_ObrLocationMode].SetCustomInputStrings("NearBand;PierceOrPreviousBar");
		sc.Input[IN_ObrLocationMode].SetCustomInputIndex(0);
		sc.Input[IN_AllowObrOutsideBand].Name = "12 - Allow OBR Outside Band";
		sc.Input[IN_AllowObrOutsideBand].SetYesNo(true);
		sc.Input[IN_BearObrTolTicks].Name = "12 - Bear OBR Tolerance Ticks";
		sc.Input[IN_BearObrTolTicks].SetInt(4); sc.Input[IN_BearObrTolTicks].SetIntLimits(0, 500);
		sc.Input[IN_BullObrTolTicks].Name = "12 - Bull OBR Tolerance Ticks";
		sc.Input[IN_BullObrTolTicks].SetInt(4); sc.Input[IN_BullObrTolTicks].SetIntLimits(0, 500);
		sc.Input[IN_ObrPierceTicks].Name = "12 - OBR Pierce Ticks";
		sc.Input[IN_ObrPierceTicks].SetInt(2); sc.Input[IN_ObrPierceTicks].SetIntLimits(0, 500);
		sc.Input[IN_ObrRequireLargerBody].Name = "12 - OBR Require Larger Body";
		sc.Input[IN_ObrRequireLargerBody].SetYesNo(false);
		sc.Input[IN_ObrMinBarSizeTicks].Name = "12 - OBR Min Bar Size Ticks";
		sc.Input[IN_ObrMinBarSizeTicks].SetInt(0); sc.Input[IN_ObrMinBarSizeTicks].SetIntLimits(0, 5000);
		sc.Input[IN_ObrShowEntryLine].Name = "12 - OBR Show Entry Line";
		sc.Input[IN_ObrShowEntryLine].SetYesNo(false);
		sc.Input[IN_ObrEntryLineBars].Name = "12 - OBR Entry Line Bars";
		sc.Input[IN_ObrEntryLineBars].SetInt(2); sc.Input[IN_ObrEntryLineBars].SetIntLimits(1, 200);
		sc.Input[IN_ObrShowZones].Name = "12 - OBR Show Zones";
		sc.Input[IN_ObrShowZones].SetYesNo(false);
		sc.Input[IN_ObrZoneBars].Name = "12 - OBR Zone Bars";
		sc.Input[IN_ObrZoneBars].SetInt(10); sc.Input[IN_ObrZoneBars].SetIntLimits(1, 500);
		sc.Input[IN_ObrZoneOpacity].Name = "12 - OBR Zone Opacity (0-100)";
		sc.Input[IN_ObrZoneOpacity].SetInt(25); sc.Input[IN_ObrZoneOpacity].SetIntLimits(0, 100);

		// ---- 13. Bollinger Gap ----
		sc.Input[IN_EnableBollingerGap].Name = "13 - Enable Bollinger Gap";
		sc.Input[IN_EnableBollingerGap].SetYesNo(true);
		sc.Input[IN_BgLocationMode].Name = "13 - BG Location Mode";
		sc.Input[IN_BgLocationMode].SetCustomInputStrings("Proximity;SierraPierce");
		sc.Input[IN_BgLocationMode].SetCustomInputIndex(0);
		sc.Input[IN_BgProximityTicks].Name = "13 - BG Proximity Ticks";
		sc.Input[IN_BgProximityTicks].SetInt(8); sc.Input[IN_BgProximityTicks].SetIntLimits(0, 500);
		sc.Input[IN_ShowBgMarkers].Name = "13 - Show BG Markers";
		sc.Input[IN_ShowBgMarkers].SetYesNo(true);

		// ---- 14. Spiderweb ----
		sc.Input[IN_EnableSpiderweb].Name = "14 - Enable Spiderweb Warning";
		sc.Input[IN_EnableSpiderweb].SetYesNo(true);
		sc.Input[IN_ShowSpiderwebText].Name = "14 - Show Spiderweb Warning Text";
		sc.Input[IN_ShowSpiderwebText].SetYesNo(true);
		sc.Input[IN_SpiderwebDistanceTicks].Name = "14 - Spiderweb Distance Ticks";
		sc.Input[IN_SpiderwebDistanceTicks].SetInt(100); sc.Input[IN_SpiderwebDistanceTicks].SetIntLimits(1, 5000);
		sc.Input[IN_SpiderwebLineCount].Name = "14 - Spiderweb Line Count";
		sc.Input[IN_SpiderwebLineCount].SetInt(5); sc.Input[IN_SpiderwebLineCount].SetIntLimits(1, 100);
		sc.Input[IN_SpiderwebFontSize].Name = "14 - Spiderweb Font Size";
		sc.Input[IN_SpiderwebFontSize].SetInt(15); sc.Input[IN_SpiderwebFontSize].SetIntLimits(6, 40);

		// ---- 15. Time Filter / Targets ----
		sc.Input[IN_UseSignalTimeFilter].Name = "15 - Use Signal Time Filter";
		sc.Input[IN_UseSignalTimeFilter].SetYesNo(false);
		sc.Input[IN_SignalStartTime].Name = "15 - Signal Start Time (HHMMSS)";
		sc.Input[IN_SignalStartTime].SetInt(101500); sc.Input[IN_SignalStartTime].SetIntLimits(0, 235959);
		sc.Input[IN_SignalEndTime].Name = "15 - Signal End Time (HHMMSS)";
		sc.Input[IN_SignalEndTime].SetInt(150000); sc.Input[IN_SignalEndTime].SetIntLimits(0, 235959);
		sc.Input[IN_SuggestedStopOffsetTicks].Name = "15 - Suggested Stop Offset Ticks";
		sc.Input[IN_SuggestedStopOffsetTicks].SetInt(0); sc.Input[IN_SuggestedStopOffsetTicks].SetIntLimits(0, 500);
		sc.Input[IN_TargetMode].Name = "15 - Target Mode";
		sc.Input[IN_TargetMode].SetCustomInputStrings("None;OppositeBollingerBand;FixedTicks");
		sc.Input[IN_TargetMode].SetCustomInputIndex(1);
		sc.Input[IN_FixedTargetTicks].Name = "15 - Fixed Target Ticks";
		sc.Input[IN_FixedTargetTicks].SetInt(40); sc.Input[IN_FixedTargetTicks].SetIntLimits(1, 5000);

		// ---- 16. Vodka Shot ----
		sc.Input[IN_EnableVodka].Name = "16 - Enable Vodka Shot";
		sc.Input[IN_EnableVodka].SetYesNo(true);
		sc.Input[IN_VodkaRequireWave].Name = "16 - Vodka Requires Wave Match";
		sc.Input[IN_VodkaRequireWave].SetYesNo(true);
		sc.Input[IN_VodkaVolumeScale].Name = "16 - Vodka Volume Scale";
		sc.Input[IN_VodkaVolumeScale].SetFloat(1.0f); sc.Input[IN_VodkaVolumeScale].SetFloatLimits(0.01f, 100.0f);
		sc.Input[IN_VodkaVolumeLength].Name = "16 - Vodka Volume Length";
		sc.Input[IN_VodkaVolumeLength].SetInt(70); sc.Input[IN_VodkaVolumeLength].SetIntLimits(2, 2000);
		sc.Input[IN_VodkaCooldownBars].Name = "16 - Vodka Cooldown Bars";
		sc.Input[IN_VodkaCooldownBars].SetInt(4); sc.Input[IN_VodkaCooldownBars].SetIntLimits(0, 200);
		sc.Input[IN_VodkaTrendLength].Name = "16 - Vodka Trend Length";
		sc.Input[IN_VodkaTrendLength].SetInt(21); sc.Input[IN_VodkaTrendLength].SetIntLimits(3, 500);
		sc.Input[IN_VodkaMacdFast].Name = "16 - Vodka MACD Fast";
		sc.Input[IN_VodkaMacdFast].SetInt(12); sc.Input[IN_VodkaMacdFast].SetIntLimits(1, 500);
		sc.Input[IN_VodkaMacdSlow].Name = "16 - Vodka MACD Slow";
		sc.Input[IN_VodkaMacdSlow].SetInt(26); sc.Input[IN_VodkaMacdSlow].SetIntLimits(1, 500);
		sc.Input[IN_VodkaMacdSignal].Name = "16 - Vodka MACD Signal";
		sc.Input[IN_VodkaMacdSignal].SetInt(9); sc.Input[IN_VodkaMacdSignal].SetIntLimits(1, 500);
		sc.Input[IN_VodkaGlyphSize].Name = "16 - Vodka Glyph Size";
		sc.Input[IN_VodkaGlyphSize].SetInt(18); sc.Input[IN_VodkaGlyphSize].SetIntLimits(6, 60);

		// ---- 17. Wave ----
		sc.Input[IN_WaveLookback].Name = "17 - Wave Lookback";
		sc.Input[IN_WaveLookback].SetInt(200); sc.Input[IN_WaveLookback].SetIntLimits(1, 5000);
		sc.Input[IN_WaveIgnoreDojis].Name = "17 - Wave Ignore Dojis";
		sc.Input[IN_WaveIgnoreDojis].SetYesNo(false);
		sc.Input[IN_WaveDojiMaxTicks].Name = "17 - Wave Doji Max Ticks";
		sc.Input[IN_WaveDojiMaxTicks].SetInt(1); sc.Input[IN_WaveDojiMaxTicks].SetIntLimits(0, 100);
		sc.Input[IN_WaveFilterFc].Name = "17 - Wave Filters FC";
		sc.Input[IN_WaveFilterFc].SetYesNo(false);
		sc.Input[IN_WaveFilterBg].Name = "17 - Wave Filters BG";
		sc.Input[IN_WaveFilterBg].SetYesNo(false);
		sc.Input[IN_WaveFilterObr].Name = "17 - Wave Filters OBR";
		sc.Input[IN_WaveFilterObr].SetYesNo(false);

		// ---- 18. Adaptive Thresholds ----
		sc.Input[IN_AdaptiveThresholds].Name = "18 - Enable Volatility Adaptive Thresholds";
		sc.Input[IN_AdaptiveThresholds].SetYesNo(false);
		sc.Input[IN_AdaptAtrLength].Name = "18 - Adapt ATR Length";
		sc.Input[IN_AdaptAtrLength].SetInt(50); sc.Input[IN_AdaptAtrLength].SetIntLimits(1, 2000);
		sc.Input[IN_AdaptAverageLength].Name = "18 - Adapt Average Length";
		sc.Input[IN_AdaptAverageLength].SetInt(50); sc.Input[IN_AdaptAverageLength].SetIntLimits(1, 2000);
		sc.Input[IN_AdaptMinRatio].Name = "18 - Adapt Min Ratio";
		sc.Input[IN_AdaptMinRatio].SetFloat(0.5f); sc.Input[IN_AdaptMinRatio].SetFloatLimits(0.01f, 10.0f);
		sc.Input[IN_AdaptMaxRatio].Name = "18 - Adapt Max Ratio";
		sc.Input[IN_AdaptMaxRatio].SetFloat(2.5f); sc.Input[IN_AdaptMaxRatio].SetFloatLimits(0.1f, 20.0f);
		sc.Input[IN_AdaptAdxFloor].Name = "18 - Scale ADX Floor";
		sc.Input[IN_AdaptAdxFloor].SetYesNo(true);
		sc.Input[IN_AdaptGapFilters].Name = "18 - Scale Gap Filters";
		sc.Input[IN_AdaptGapFilters].SetYesNo(true);
		sc.Input[IN_AdaptBandDistances].Name = "18 - Scale Band Distances";
		sc.Input[IN_AdaptBandDistances].SetYesNo(true);
		sc.Input[IN_AdaptSpiderweb].Name = "18 - Scale Spiderweb Distance";
		sc.Input[IN_AdaptSpiderweb].SetYesNo(true);

		// ---- 19. Alerts ----
		sc.Input[IN_EnableAlerts].Name = "19 - Enable Alerts";
		sc.Input[IN_EnableAlerts].SetYesNo(false);
		sc.Input[IN_PlaySounds].Name = "19 - Play Alert Sounds";
		sc.Input[IN_PlaySounds].SetYesNo(true);
		sc.Input[IN_AlertBuy].Name = "19 - Alert: Buy"; sc.Input[IN_AlertBuy].SetYesNo(true);
		sc.Input[IN_AlertSell].Name = "19 - Alert: Sell"; sc.Input[IN_AlertSell].SetYesNo(true);
		sc.Input[IN_AlertBigBuy].Name = "19 - Alert: Big Buy"; sc.Input[IN_AlertBigBuy].SetYesNo(true);
		sc.Input[IN_AlertBigSell].Name = "19 - Alert: Big Sell"; sc.Input[IN_AlertBigSell].SetYesNo(true);
		sc.Input[IN_AlertSqueezeUp].Name = "19 - Alert: Squeeze Up"; sc.Input[IN_AlertSqueezeUp].SetYesNo(false);
		sc.Input[IN_AlertSqueezeDown].Name = "19 - Alert: Squeeze Down"; sc.Input[IN_AlertSqueezeDown].SetYesNo(false);
		sc.Input[IN_AlertVolImbBuy].Name = "19 - Alert: Volume Imbalance Buy"; sc.Input[IN_AlertVolImbBuy].SetYesNo(true);
		sc.Input[IN_AlertVolImbSell].Name = "19 - Alert: Volume Imbalance Sell"; sc.Input[IN_AlertVolImbSell].SetYesNo(true);
		sc.Input[IN_AlertGapTouchBull].Name = "19 - Alert: Gap Touch Bull"; sc.Input[IN_AlertGapTouchBull].SetYesNo(false);
		sc.Input[IN_AlertGapTouchBear].Name = "19 - Alert: Gap Touch Bear"; sc.Input[IN_AlertGapTouchBear].SetYesNo(false);
		sc.Input[IN_AlertFcLong].Name = "19 - Alert: FC Long"; sc.Input[IN_AlertFcLong].SetYesNo(true);
		sc.Input[IN_AlertFcShort].Name = "19 - Alert: FC Short"; sc.Input[IN_AlertFcShort].SetYesNo(true);
		sc.Input[IN_AlertBgLong].Name = "19 - Alert: BG Long"; sc.Input[IN_AlertBgLong].SetYesNo(true);
		sc.Input[IN_AlertBgShort].Name = "19 - Alert: BG Short"; sc.Input[IN_AlertBgShort].SetYesNo(true);
		sc.Input[IN_AlertObrLong].Name = "19 - Alert: OBR Long"; sc.Input[IN_AlertObrLong].SetYesNo(true);
		sc.Input[IN_AlertObrShort].Name = "19 - Alert: OBR Short"; sc.Input[IN_AlertObrShort].SetYesNo(true);
		sc.Input[IN_AlertTramp].Name = "19 - Alert: Trampoline"; sc.Input[IN_AlertTramp].SetYesNo(false);
		sc.Input[IN_AlertThreeOutside].Name = "19 - Alert: Three Outside"; sc.Input[IN_AlertThreeOutside].SetYesNo(false);
		sc.Input[IN_AlertTweezer].Name = "19 - Alert: Tweezer"; sc.Input[IN_AlertTweezer].SetYesNo(false);
		sc.Input[IN_AlertWickPat].Name = "19 - Alert: Wick Pattern"; sc.Input[IN_AlertWickPat].SetYesNo(false);
		sc.Input[IN_AlertStairs].Name = "19 - Alert: Stairs"; sc.Input[IN_AlertStairs].SetYesNo(false);
		sc.Input[IN_AlertRevSquare].Name = "19 - Alert: Reversal Square"; sc.Input[IN_AlertRevSquare].SetYesNo(false);
		sc.Input[IN_AlertKamaBounce].Name = "19 - Alert: KAMA Bounce"; sc.Input[IN_AlertKamaBounce].SetYesNo(false);
		sc.Input[IN_AlertVodkaBuy].Name = "19 - Alert: Vodka Buy"; sc.Input[IN_AlertVodkaBuy].SetYesNo(true);
		sc.Input[IN_AlertVodkaSell].Name = "19 - Alert: Vodka Sell"; sc.Input[IN_AlertVodkaSell].SetYesNo(true);
		sc.Input[IN_AlertSmiDivBull].Name = "19 - Alert: SMI Bull Divergence"; sc.Input[IN_AlertSmiDivBull].SetYesNo(false);
		sc.Input[IN_AlertSmiDivBear].Name = "19 - Alert: SMI Bear Divergence"; sc.Input[IN_AlertSmiDivBear].SetYesNo(false);
		sc.Input[IN_AlertSpiderweb].Name = "19 - Alert: Spiderweb Warning"; sc.Input[IN_AlertSpiderweb].SetYesNo(false);
		sc.Input[IN_SoundBullish].Name = "19 - Bullish Alert Sound (.wav)";
		sc.Input[IN_SoundBullish].SetString("Alert2.wav");
		sc.Input[IN_SoundBearish].Name = "19 - Bearish Alert Sound (.wav)";
		sc.Input[IN_SoundBearish].SetString("Alert3.wav");
		sc.Input[IN_SoundNeutral].Name = "19 - Neutral Alert Sound (.wav)";
		sc.Input[IN_SoundNeutral].SetString("Alert1.wav");

		// ---- 20. Offsets (in ticks) ----
		sc.Input[IN_BuyTriangleOffsetTicks].Name = "20 - Buy Triangle Offset Ticks";
		sc.Input[IN_BuyTriangleOffsetTicks].SetInt(2); sc.Input[IN_BuyTriangleOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_SellTriangleOffsetTicks].Name = "20 - Sell Triangle Offset Ticks";
		sc.Input[IN_SellTriangleOffsetTicks].SetInt(2); sc.Input[IN_SellTriangleOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_BigArrowUpOffsetTicks].Name = "20 - Big Arrow Up Offset Ticks";
		sc.Input[IN_BigArrowUpOffsetTicks].SetInt(7); sc.Input[IN_BigArrowUpOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_BigArrowDownOffsetTicks].Name = "20 - Big Arrow Down Offset Ticks";
		sc.Input[IN_BigArrowDownOffsetTicks].SetInt(7); sc.Input[IN_BigArrowDownOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_SqueezeStarUpOffsetTicks].Name = "20 - Squeeze Star Up Offset Ticks";
		sc.Input[IN_SqueezeStarUpOffsetTicks].SetInt(2); sc.Input[IN_SqueezeStarUpOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_SqueezeStarDownOffsetTicks].Name = "20 - Squeeze Star Down Offset Ticks";
		sc.Input[IN_SqueezeStarDownOffsetTicks].SetInt(2); sc.Input[IN_SqueezeStarDownOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_VolImbUpOffsetTicks].Name = "20 - Volume Imbalance Up Offset Ticks";
		sc.Input[IN_VolImbUpOffsetTicks].SetInt(2); sc.Input[IN_VolImbUpOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_VolImbDownOffsetTicks].Name = "20 - Volume Imbalance Down Offset Ticks";
		sc.Input[IN_VolImbDownOffsetTicks].SetInt(2); sc.Input[IN_VolImbDownOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_FcMarkerOffsetTicks].Name = "20 - FC Marker Offset Ticks";
		sc.Input[IN_FcMarkerOffsetTicks].SetInt(3); sc.Input[IN_FcMarkerOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_BgMarkerOffsetTicks].Name = "20 - BG Marker Offset Ticks";
		sc.Input[IN_BgMarkerOffsetTicks].SetInt(3); sc.Input[IN_BgMarkerOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_ObrMarkerOffsetTicks].Name = "20 - OBR Marker Offset Ticks";
		sc.Input[IN_ObrMarkerOffsetTicks].SetInt(3); sc.Input[IN_ObrMarkerOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_ObrArrowOffsetTicks].Name = "20 - OBR Arrow Offset Ticks";
		sc.Input[IN_ObrArrowOffsetTicks].SetInt(7); sc.Input[IN_ObrArrowOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_SignalLabelOffsetTicks].Name = "20 - Signal Label Offset Ticks";
		sc.Input[IN_SignalLabelOffsetTicks].SetInt(7); sc.Input[IN_SignalLabelOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_PatternLabelOffsetTicks].Name = "20 - Pattern Label Offset Ticks";
		sc.Input[IN_PatternLabelOffsetTicks].SetInt(7); sc.Input[IN_PatternLabelOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_TrampolineOffsetTicks].Name = "20 - Trampoline Offset Ticks";
		sc.Input[IN_TrampolineOffsetTicks].SetInt(7); sc.Input[IN_TrampolineOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_ReversalSquareOffsetTicks].Name = "20 - Reversal Square Offset Ticks";
		sc.Input[IN_ReversalSquareOffsetTicks].SetInt(7); sc.Input[IN_ReversalSquareOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_EvilTimesOffsetTicks].Name = "20 - Evil Times Offset Ticks";
		sc.Input[IN_EvilTimesOffsetTicks].SetInt(7); sc.Input[IN_EvilTimesOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_VodkaUpOffsetTicks].Name = "20 - Vodka Up Offset Ticks";
		sc.Input[IN_VodkaUpOffsetTicks].SetInt(4); sc.Input[IN_VodkaUpOffsetTicks].SetIntLimits(0, 200);
		sc.Input[IN_VodkaDownOffsetTicks].Name = "20 - Vodka Down Offset Ticks";
		sc.Input[IN_VodkaDownOffsetTicks].SetInt(4); sc.Input[IN_VodkaDownOffsetTicks].SetIntLimits(0, 200);

		// ---- 21. Colors (attractive dark-chart friendly palette) ----
		sc.Input[IN_WaddahUpColor].Name = "21 - Color: Waddah Up"; sc.Input[IN_WaddahUpColor].SetColor(ADM_RGB(0, 230, 118));
		sc.Input[IN_WaddahDownColor].Name = "21 - Color: Waddah Down"; sc.Input[IN_WaddahDownColor].SetColor(ADM_RGB(255, 82, 82));
		sc.Input[IN_BuyColor].Name = "21 - Color: Buy"; sc.Input[IN_BuyColor].SetColor(ADM_RGB(0, 230, 118));
		sc.Input[IN_SellColor].Name = "21 - Color: Sell"; sc.Input[IN_SellColor].SetColor(ADM_RGB(255, 82, 82));
		sc.Input[IN_StarColor].Name = "21 - Color: Squeeze Star"; sc.Input[IN_StarColor].SetColor(ADM_RGB(255, 214, 0));
		sc.Input[IN_VolImbColor].Name = "21 - Color: Volume Imbalance"; sc.Input[IN_VolImbColor].SetColor(ADM_RGB(224, 224, 224));
		sc.Input[IN_GapBullLineColor].Name = "21 - Color: Gap Bull Line"; sc.Input[IN_GapBullLineColor].SetColor(ADM_RGB(0, 188, 212));
		sc.Input[IN_GapBearLineColor].Name = "21 - Color: Gap Bear Line"; sc.Input[IN_GapBearLineColor].SetColor(ADM_RGB(255, 145, 0));
		sc.Input[IN_GapTouchedColor].Name = "21 - Color: Gap Touched"; sc.Input[IN_GapTouchedColor].SetColor(ADM_RGB(120, 120, 120));
		sc.Input[IN_GapInvalidColor].Name = "21 - Color: Gap Invalid"; sc.Input[IN_GapInvalidColor].SetColor(ADM_RGB(255, 82, 82));
		sc.Input[IN_GapBullZoneColor].Name = "21 - Color: Gap Bull Zone"; sc.Input[IN_GapBullZoneColor].SetColor(ADM_RGB(30, 144, 255));
		sc.Input[IN_GapBearZoneColor].Name = "21 - Color: Gap Bear Zone"; sc.Input[IN_GapBearZoneColor].SetColor(ADM_RGB(216, 27, 96));
		sc.Input[IN_TouchMarkerColor].Name = "21 - Color: Touch Marker"; sc.Input[IN_TouchMarkerColor].SetColor(ADM_RGB(255, 215, 0));
		sc.Input[IN_BgLongColor].Name = "21 - Color: BG Long"; sc.Input[IN_BgLongColor].SetColor(ADM_RGB(0, 230, 118));
		sc.Input[IN_BgShortColor].Name = "21 - Color: BG Short"; sc.Input[IN_BgShortColor].SetColor(ADM_RGB(255, 82, 82));
		sc.Input[IN_FcLongColor].Name = "21 - Color: FC Long"; sc.Input[IN_FcLongColor].SetColor(ADM_RGB(50, 205, 50));
		sc.Input[IN_FcShortColor].Name = "21 - Color: FC Short"; sc.Input[IN_FcShortColor].SetColor(ADM_RGB(255, 69, 0));
		sc.Input[IN_ObrBullColor].Name = "21 - Color: OBR Bull"; sc.Input[IN_ObrBullColor].SetColor(ADM_RGB(50, 205, 50));
		sc.Input[IN_ObrBearColor].Name = "21 - Color: OBR Bear"; sc.Input[IN_ObrBearColor].SetColor(ADM_RGB(255, 82, 82));
		sc.Input[IN_ObrLightZoneColor].Name = "21 - Color: OBR Light Zone"; sc.Input[IN_ObrLightZoneColor].SetColor(ADM_RGB(211, 211, 211));
		sc.Input[IN_ObrShadowZoneColor].Name = "21 - Color: OBR Shadow Zone"; sc.Input[IN_ObrShadowZoneColor].SetColor(ADM_RGB(135, 206, 235));
		sc.Input[IN_SpiderwebColor].Name = "21 - Color: Spiderweb Warning"; sc.Input[IN_SpiderwebColor].SetColor(ADM_RGB(255, 152, 0));
		sc.Input[IN_PatternTextColor].Name = "21 - Color: Pattern Text"; sc.Input[IN_PatternTextColor].SetColor(ADM_RGB(255, 235, 59));
		sc.Input[IN_PatternBullBackColor].Name = "21 - Color: Pattern Bull Back"; sc.Input[IN_PatternBullBackColor].SetColor(ADM_RGB(1, 97, 3));
		sc.Input[IN_PatternBearBackColor].Name = "21 - Color: Pattern Bear Back"; sc.Input[IN_PatternBearBackColor].SetColor(ADM_RGB(97, 1, 1));
		sc.Input[IN_TrampTextColor].Name = "21 - Color: Trampoline Text"; sc.Input[IN_TrampTextColor].SetColor(ADM_RGB(0, 0, 0));
		sc.Input[IN_TrampBackColor].Name = "21 - Color: Trampoline Back"; sc.Input[IN_TrampBackColor].SetColor(ADM_RGB(176, 224, 230));
		sc.Input[IN_BbEngulfGreenColor].Name = "21 - Color: BB Engulf Green"; sc.Input[IN_BbEngulfGreenColor].SetColor(ADM_RGB(0, 64, 0));
		sc.Input[IN_BbEngulfRedColor].Name = "21 - Color: BB Engulf Red"; sc.Input[IN_BbEngulfRedColor].SetColor(ADM_RGB(70, 0, 35));
		sc.Input[IN_ShavedGreenColor].Name = "21 - Color: Shaved Green"; sc.Input[IN_ShavedGreenColor].SetColor(ADM_RGB(170, 221, 255));
		sc.Input[IN_ShavedRedColor].Name = "21 - Color: Shaved Red"; sc.Input[IN_ShavedRedColor].SetColor(ADM_RGB(255, 128, 192));
		sc.Input[IN_KamaColor].Name = "21 - Color: KAMA"; sc.Input[IN_KamaColor].SetColor(ADM_RGB(184, 134, 11));
		sc.Input[IN_BbUpperColor].Name = "21 - Color: BB Upper/Lower"; sc.Input[IN_BbUpperColor].SetColor(ADM_RGB(30, 144, 255));
		sc.Input[IN_BbLowerColor].Name = "21 - (unused, mirrors BB Upper)"; sc.Input[IN_BbLowerColor].SetColor(ADM_RGB(30, 144, 255));
		sc.Input[IN_BbMidColor].Name = "21 - Color: BB Middle"; sc.Input[IN_BbMidColor].SetColor(ADM_RGB(218, 165, 32));
		sc.Input[IN_SmiBullDivColor].Name = "21 - Color: SMI Bull Divergence"; sc.Input[IN_SmiBullDivColor].SetColor(ADM_RGB(0, 230, 118));
		sc.Input[IN_SmiBearDivColor].Name = "21 - Color: SMI Bear Divergence"; sc.Input[IN_SmiBearDivColor].SetColor(ADM_RGB(255, 82, 82));
		sc.Input[IN_SmiHiddenBullDivColor].Name = "21 - Color: SMI Hidden Bull Div"; sc.Input[IN_SmiHiddenBullDivColor].SetColor(ADM_RGB(0, 229, 255));
		sc.Input[IN_SmiHiddenBearDivColor].Name = "21 - Color: SMI Hidden Bear Div"; sc.Input[IN_SmiHiddenBearDivColor].SetColor(ADM_RGB(255, 145, 0));
		sc.Input[IN_EvilTimesColor].Name = "21 - Color: Evil Times"; sc.Input[IN_EvilTimesColor].SetColor(ADM_RGB(255, 82, 82));
		sc.Input[IN_VodkaUpColor].Name = "21 - Color: Vodka Up"; sc.Input[IN_VodkaUpColor].SetColor(ADM_RGB(0, 230, 118));
		sc.Input[IN_VodkaDownColor].Name = "21 - Color: Vodka Down"; sc.Input[IN_VodkaDownColor].SetColor(ADM_RGB(255, 82, 82));

		// ---- 22. Dashboard ----
		sc.Input[IN_ShowDashboard].Name = "22 - Show Dashboard";
		sc.Input[IN_ShowDashboard].SetYesNo(true);
		sc.Input[IN_DashboardCorner].Name = "22 - Dashboard Corner";
		sc.Input[IN_DashboardCorner].SetCustomInputStrings("TopRight;TopLeft;BottomRight;BottomLeft");
		sc.Input[IN_DashboardCorner].SetCustomInputIndex(0);
		sc.Input[IN_DashboardFontSize].Name = "22 - Dashboard Font Size";
		sc.Input[IN_DashboardFontSize].SetInt(11); sc.Input[IN_DashboardFontSize].SetIntLimits(6, 24);
		sc.Input[IN_DashboardOpacity].Name = "22 - Dashboard Panel Opacity (0-100)";
		sc.Input[IN_DashboardOpacity].SetInt(72); sc.Input[IN_DashboardOpacity].SetIntLimits(0, 100);

		return;
	}

	// ================= PERSISTENT STATE =================
	AdmState* pState = (AdmState*)sc.GetPersistentPointer(1);
	if (sc.LastCallToFunction)
	{
		if (pState != NULL) { delete pState; sc.SetPersistentPointer(1, NULL); }
		return;
	}
	if (pState == NULL)
	{
		pState = AdmStateInit::Create();
		sc.SetPersistentPointer(1, pState);
	}
	AdmState& S = *pState;

	// ================= READ INPUTS =================
	ADMFilterRules FilterRules = (ADMFilterRules)sc.Input[IN_FilterRules].GetIndex();
	bool SierraExactMode = sc.Input[IN_SierraExactMode].GetYesNo();
	bool UseWaddah = sc.Input[IN_UseWaddah].GetYesNo();
	bool UseLindaMacd = sc.Input[IN_UseLindaMacd].GetYesNo();
	bool UsePsar = sc.Input[IN_UsePsar].GetYesNo();
	bool UseSupertrend = sc.Input[IN_UseSupertrend].GetYesNo();
	bool UseAO = sc.Input[IN_UseAO].GetYesNo();
	bool UseHma = sc.Input[IN_UseHma].GetYesNo();
	bool UseT3 = sc.Input[IN_UseT3].GetYesNo();
	bool UseFisher = sc.Input[IN_UseFisher].GetYesNo();
	bool UseSqueezeMomentum = sc.Input[IN_UseSqueezeMomentum].GetYesNo();
	int MinAdx = sc.Input[IN_MinAdx].GetInt();
	bool UseAdxvma = sc.Input[IN_UseAdxvma].GetYesNo();
	bool UseLizardSuperTrend = sc.Input[IN_UseLizardSuperTrend].GetYesNo();
	bool UseMultiTsi = sc.Input[IN_UseMultiTsi].GetYesNo();
	bool UseSmi = sc.Input[IN_UseSmi].GetYesNo();
	bool RequireCandleDirection = sc.Input[IN_RequireCandleDirection].GetYesNo();
	bool IgnoreDojis = sc.Input[IN_IgnoreDojis].GetYesNo();

	int WaddahIntensity = sc.Input[IN_WaddahIntensity].GetInt();
	int LindaFast = sc.Input[IN_LindaFast].GetInt();
	int LindaSlow = sc.Input[IN_LindaSlow].GetInt();
	int LindaSignal = sc.Input[IN_LindaSignal].GetInt();
	double PsarAcceleration = sc.Input[IN_PsarAcceleration].GetFloat();
	double PsarMax = sc.Input[IN_PsarMax].GetFloat();
	double PsarStep = sc.Input[IN_PsarStep].GetFloat();
	int AdxPeriod = sc.Input[IN_AdxPeriod].GetInt();
	int HmaPeriod = sc.Input[IN_HmaPeriod].GetInt();
	int T3Period = sc.Input[IN_T3Period].GetInt();
	int T3TCount = sc.Input[IN_T3TCount].GetInt();
	double T3VFactor = sc.Input[IN_T3VFactor].GetFloat();
	int StPeriod = sc.Input[IN_StPeriod].GetInt();
	double StMultiplier = sc.Input[IN_StMultiplier].GetFloat();
	ADMSuperTrendAtr StAtrType = (ADMSuperTrendAtr)sc.Input[IN_StAtrType].GetIndex();
	int SqueezePeriod = sc.Input[IN_SqueezePeriod].GetInt();
	bool SqueezeUsesOpen = sc.Input[IN_SqueezeUsesOpen].GetYesNo();
	int AdxvmaPeriod = sc.Input[IN_AdxvmaPeriod].GetInt();
	int TsiSmooth1 = sc.Input[IN_TsiSmooth1].GetInt();
	int TsiSmooth2 = sc.Input[IN_TsiSmooth2].GetInt();
	int TsiSignalPeriod = sc.Input[IN_TsiSignalPeriod].GetInt();
	int LizStAtrPeriod = sc.Input[IN_LizStAtrPeriod].GetInt();
	double LizStMultiplier = sc.Input[IN_LizStMultiplier].GetFloat();
	int SmiPeriodK = sc.Input[IN_SmiPeriodK].GetInt();
	int SmiSmooth1 = sc.Input[IN_SmiSmooth1].GetInt();
	int SmiSmooth2 = sc.Input[IN_SmiSmooth2].GetInt();
	int SmiSignalPeriod = sc.Input[IN_SmiSignalPeriod].GetInt();
	double SmiOverbought = sc.Input[IN_SmiOverbought].GetFloat();
	double SmiOversold = sc.Input[IN_SmiOversold].GetFloat();
	double SmiMidline = sc.Input[IN_SmiMidline].GetFloat();
	bool SmiRequireSlope = sc.Input[IN_SmiRequireSlope].GetYesNo();
	int SmiDivergenceLookback = sc.Input[IN_SmiDivergenceLookback].GetInt();

	bool ShowBuySell = sc.Input[IN_ShowBuySell].GetYesNo();
	bool ShowBigArrow = sc.Input[IN_ShowBigArrow].GetYesNo();
	bool ShowSqueezeStars = sc.Input[IN_ShowSqueezeStars].GetYesNo();
	bool ShowVolumeImbalanceArrows = sc.Input[IN_ShowVolumeImbalanceArrows].GetYesNo();
	bool ShowTrampoline = sc.Input[IN_ShowTrampoline].GetYesNo();
	int TrampolineRsiHigh = sc.Input[IN_TrampolineRsiHigh].GetInt();
	int TrampolineRsiLow = sc.Input[IN_TrampolineRsiLow].GetInt();
	int TrampolineToleranceTicks = sc.Input[IN_TrampolineToleranceTicks].GetInt();
	bool ShowThreeOutside = sc.Input[IN_ShowThreeOutside].GetYesNo();
	bool ShowTweezers = sc.Input[IN_ShowTweezers].GetYesNo();
	int TweezerToleranceTicks = sc.Input[IN_TweezerToleranceTicks].GetInt();
	bool ShowWickPattern = sc.Input[IN_ShowWickPattern].GetYesNo();
	bool ShowStairs = sc.Input[IN_ShowStairs].GetYesNo();
	bool ShowReversalSquare = sc.Input[IN_ShowReversalSquare].GetYesNo();
	bool ShowShavedCandles = sc.Input[IN_ShowShavedCandles].GetYesNo();
	int ShavedToleranceTicks = sc.Input[IN_ShavedToleranceTicks].GetInt();
	bool ShowBbEngulfShading = sc.Input[IN_ShowBbEngulfShading].GetYesNo();

	bool EnableSmiDivergence = sc.Input[IN_EnableSmiDivergence].GetYesNo();
	bool SmiIncludeHiddenDivergence = sc.Input[IN_SmiIncludeHiddenDivergence].GetYesNo();
	bool ShowSmiDivergenceLines = sc.Input[IN_ShowSmiDivergenceLines].GetYesNo();

	bool ShowKama = sc.Input[IN_ShowKama].GetYesNo();
	int KamaFast = sc.Input[IN_KamaFast].GetInt();
	int KamaPeriod = sc.Input[IN_KamaPeriod].GetInt();
	int KamaSlow = sc.Input[IN_KamaSlow].GetInt();
	bool ShowKamaBounceMarkers = sc.Input[IN_ShowKamaBounceMarkers].GetYesNo();
	int KamaLineWidth = sc.Input[IN_KamaLineWidth].GetInt();

	bool ShowEvilTimes = sc.Input[IN_ShowEvilTimes].GetYesNo();
	int EvilTimesChartOffsetHours = sc.Input[IN_EvilTimesChartOffsetHours].GetInt();

	bool StackSignals = sc.Input[IN_StackSignals].GetYesNo();
	int StackStepTicks = sc.Input[IN_StackStepTicks].GetInt();
	int TextSize = sc.Input[IN_TextSize].GetInt();
	bool ShowSignalLabels = sc.Input[IN_ShowSignalLabels].GetYesNo();

	ADMBarColorMode BarColor = (ADMBarColorMode)sc.Input[IN_BarColor].GetIndex();
	int WaddahBarOffset = sc.Input[IN_WaddahBarOffset].GetInt();
	ADMBrightnessMode WaddahBrightnessMode = (ADMBrightnessMode)sc.Input[IN_WaddahBrightnessMode].GetIndex();
	double WaddahBarGain = sc.Input[IN_WaddahBarGain].GetFloat();
	int WaddahAutoLength = sc.Input[IN_WaddahAutoLength].GetInt();
	double WaddahAutoFullMultiple = sc.Input[IN_WaddahAutoFullMultiple].GetFloat();
	ADMBrightnessMode LindaBrightnessMode = (ADMBrightnessMode)sc.Input[IN_LindaBrightnessMode].GetIndex();
	int LindaBarIntensity = sc.Input[IN_LindaBarIntensity].GetInt();
	int LindaAutoLength = sc.Input[IN_LindaAutoLength].GetInt();
	double LindaAutoFullMultiple = sc.Input[IN_LindaAutoFullMultiple].GetFloat();
	int LindaBarOffset = sc.Input[IN_LindaBarOffset].GetInt();

	int BbPeriod = sc.Input[IN_BbPeriod].GetInt();
	double BbStdDev = sc.Input[IN_BbStdDev].GetFloat();
	bool ShowBollingerBands = sc.Input[IN_ShowBollingerBands].GetYesNo();
	int BbLineWidth = sc.Input[IN_BbLineWidth].GetInt();
	bool ShowBollingerMiddle = sc.Input[IN_ShowBollingerMiddle].GetYesNo();
	int BbMidWidth = sc.Input[IN_BbMidWidth].GetInt();

	int MinGapSizeTicks = sc.Input[IN_MinGapSizeTicks].GetInt();
	int MinBarsBeforeValid = sc.Input[IN_MinBarsBeforeValid].GetInt();
	int MinBodyTicks = sc.Input[IN_MinBodyTicks].GetInt();
	int MaxGapBarRangeTicks = sc.Input[IN_MaxGapBarRangeTicks].GetInt();
	int MaxActiveGaps = sc.Input[IN_MaxActiveGaps].GetInt();
	ADMEarlyTouch EarlyTouch = (ADMEarlyTouch)sc.Input[IN_EarlyTouch].GetIndex();
	ADMValidTouch ValidTouch = (ADMValidTouch)sc.Input[IN_ValidTouch].GetIndex();
	ADMLinePriceMode LinePriceMode = (ADMLinePriceMode)sc.Input[IN_LinePriceMode].GetIndex();
	bool ShowGapLine = sc.Input[IN_ShowGapLine].GetYesNo();
	bool ShowGapZone = sc.Input[IN_ShowGapZone].GetYesNo();
	bool ShowTouchMarker = sc.Input[IN_ShowTouchMarker].GetYesNo();
	bool UseTouchedLineColor = sc.Input[IN_UseTouchedLineColor].GetYesNo();
	int GapLineWidth = sc.Input[IN_GapLineWidth].GetInt();
	bool GapWidthBySize = sc.Input[IN_GapWidthBySize].GetYesNo();
	int GapMinWidth = sc.Input[IN_GapMinWidth].GetInt();
	int GapMaxWidth = sc.Input[IN_GapMaxWidth].GetInt();
	int GapMaxWidthAtTicks = sc.Input[IN_GapMaxWidthAtTicks].GetInt();
	int GapZoneOpacity = sc.Input[IN_GapZoneOpacity].GetInt();
	ADMGapLineExtension GapLineExtension = (ADMGapLineExtension)sc.Input[IN_GapLineExtension].GetIndex();
	int GapProjectionBars = sc.Input[IN_GapProjectionBars].GetInt();

	bool EnableContinuation = sc.Input[IN_EnableContinuation].GetYesNo();
	bool UseBbMidFilterFc = sc.Input[IN_UseBbMidFilterFc].GetYesNo();
	ADMBollingerLocation FcLocationSource = (ADMBollingerLocation)sc.Input[IN_FcLocationSource].GetIndex();
	double FcLongBelowMidPct = sc.Input[IN_FcLongBelowMidPct].GetFloat();
	double FcShortAboveMidPct = sc.Input[IN_FcShortAboveMidPct].GetFloat();
	ADMConfirmMode ConfirmMode = (ADMConfirmMode)sc.Input[IN_ConfirmMode].GetIndex();
	int ConfirmBarsAfterTouch = sc.Input[IN_ConfirmBarsAfterTouch].GetInt();
	bool RequireSignalCandleDirection = sc.Input[IN_RequireSignalCandleDirection].GetYesNo();
	bool RequireCorrectApproach = sc.Input[IN_RequireCorrectApproach].GetYesNo();
	bool ShowContinuationMarkers = sc.Input[IN_ShowContinuationMarkers].GetYesNo();

	bool EnableObr = sc.Input[IN_EnableObr].GetYesNo();
	bool PaintObrBars = sc.Input[IN_PaintObrBars].GetYesNo();
	ADMObrMarkerStyle ObrMarkerStyle = (ADMObrMarkerStyle)sc.Input[IN_ObrMarkerStyle].GetIndex();
	bool UseBbMidFilterObr = sc.Input[IN_UseBbMidFilterObr].GetYesNo();
	ADMObrLocationMode ObrLocationMode = (ADMObrLocationMode)sc.Input[IN_ObrLocationMode].GetIndex();
	bool AllowObrOutsideBand = sc.Input[IN_AllowObrOutsideBand].GetYesNo();
	int BearObrTolTicks = sc.Input[IN_BearObrTolTicks].GetInt();
	int BullObrTolTicks = sc.Input[IN_BullObrTolTicks].GetInt();
	int ObrPierceTicks = sc.Input[IN_ObrPierceTicks].GetInt();
	bool ObrRequireLargerBody = sc.Input[IN_ObrRequireLargerBody].GetYesNo();
	int ObrMinBarSizeTicks = sc.Input[IN_ObrMinBarSizeTicks].GetInt();
	bool ObrShowEntryLine = sc.Input[IN_ObrShowEntryLine].GetYesNo();
	int ObrEntryLineBars = sc.Input[IN_ObrEntryLineBars].GetInt();
	bool ObrShowZones = sc.Input[IN_ObrShowZones].GetYesNo();
	int ObrZoneBars = sc.Input[IN_ObrZoneBars].GetInt();
	int ObrZoneOpacity = sc.Input[IN_ObrZoneOpacity].GetInt();

	bool EnableBollingerGap = sc.Input[IN_EnableBollingerGap].GetYesNo();
	ADMBgLocationMode BgLocationMode = (ADMBgLocationMode)sc.Input[IN_BgLocationMode].GetIndex();
	int BgProximityTicks = sc.Input[IN_BgProximityTicks].GetInt();
	bool ShowBgMarkers = sc.Input[IN_ShowBgMarkers].GetYesNo();

	bool EnableSpiderweb = sc.Input[IN_EnableSpiderweb].GetYesNo();
	bool ShowSpiderwebText = sc.Input[IN_ShowSpiderwebText].GetYesNo();
	int SpiderwebDistanceTicks = sc.Input[IN_SpiderwebDistanceTicks].GetInt();
	int SpiderwebLineCount = sc.Input[IN_SpiderwebLineCount].GetInt();
	int SpiderwebFontSize = sc.Input[IN_SpiderwebFontSize].GetInt();

	bool UseSignalTimeFilter = sc.Input[IN_UseSignalTimeFilter].GetYesNo();
	int SignalStartTime = sc.Input[IN_SignalStartTime].GetInt();
	int SignalEndTime = sc.Input[IN_SignalEndTime].GetInt();
	int SuggestedStopOffsetTicks = sc.Input[IN_SuggestedStopOffsetTicks].GetInt();
	ADMTargetMode TargetMode = (ADMTargetMode)sc.Input[IN_TargetMode].GetIndex();
	int FixedTargetTicks = sc.Input[IN_FixedTargetTicks].GetInt();

	bool EnableVodka = sc.Input[IN_EnableVodka].GetYesNo();
	bool VodkaRequireWave = sc.Input[IN_VodkaRequireWave].GetYesNo();
	double VodkaVolumeScale = sc.Input[IN_VodkaVolumeScale].GetFloat();
	int VodkaVolumeLength = sc.Input[IN_VodkaVolumeLength].GetInt();
	int VodkaCooldownBars = sc.Input[IN_VodkaCooldownBars].GetInt();
	int VodkaTrendLength = sc.Input[IN_VodkaTrendLength].GetInt();
	int VodkaMacdFast = sc.Input[IN_VodkaMacdFast].GetInt();
	int VodkaMacdSlow = sc.Input[IN_VodkaMacdSlow].GetInt();
	int VodkaMacdSignal = sc.Input[IN_VodkaMacdSignal].GetInt();
	int VodkaGlyphSize = sc.Input[IN_VodkaGlyphSize].GetInt();

	int WaveLookback = sc.Input[IN_WaveLookback].GetInt();
	bool WaveIgnoreDojis = sc.Input[IN_WaveIgnoreDojis].GetYesNo();
	int WaveDojiMaxTicks = sc.Input[IN_WaveDojiMaxTicks].GetInt();
	bool WaveFilterFc = sc.Input[IN_WaveFilterFc].GetYesNo();
	bool WaveFilterBg = sc.Input[IN_WaveFilterBg].GetYesNo();
	bool WaveFilterObr = sc.Input[IN_WaveFilterObr].GetYesNo();

	bool AdaptiveThresholds = sc.Input[IN_AdaptiveThresholds].GetYesNo();
	int AdaptAtrLength = sc.Input[IN_AdaptAtrLength].GetInt();
	int AdaptAverageLength = sc.Input[IN_AdaptAverageLength].GetInt();
	double AdaptMinRatio = sc.Input[IN_AdaptMinRatio].GetFloat();
	double AdaptMaxRatio = sc.Input[IN_AdaptMaxRatio].GetFloat();
	bool AdaptAdxFloor = sc.Input[IN_AdaptAdxFloor].GetYesNo();
	bool AdaptGapFilters = sc.Input[IN_AdaptGapFilters].GetYesNo();
	bool AdaptBandDistances = sc.Input[IN_AdaptBandDistances].GetYesNo();
	bool AdaptSpiderweb = sc.Input[IN_AdaptSpiderweb].GetYesNo();

	bool EnableAlerts = sc.Input[IN_EnableAlerts].GetYesNo();
	bool PlaySounds = sc.Input[IN_PlaySounds].GetYesNo();
	bool AlertBuy = sc.Input[IN_AlertBuy].GetYesNo(), AlertSell = sc.Input[IN_AlertSell].GetYesNo();
	bool AlertBigBuy = sc.Input[IN_AlertBigBuy].GetYesNo(), AlertBigSell = sc.Input[IN_AlertBigSell].GetYesNo();
	bool AlertSqueezeUp = sc.Input[IN_AlertSqueezeUp].GetYesNo(), AlertSqueezeDown = sc.Input[IN_AlertSqueezeDown].GetYesNo();
	bool AlertVolImbBuy = sc.Input[IN_AlertVolImbBuy].GetYesNo(), AlertVolImbSell = sc.Input[IN_AlertVolImbSell].GetYesNo();
	bool AlertGapTouchBull = sc.Input[IN_AlertGapTouchBull].GetYesNo(), AlertGapTouchBear = sc.Input[IN_AlertGapTouchBear].GetYesNo();
	bool AlertFcLong = sc.Input[IN_AlertFcLong].GetYesNo(), AlertFcShort = sc.Input[IN_AlertFcShort].GetYesNo();
	bool AlertBgLong = sc.Input[IN_AlertBgLong].GetYesNo(), AlertBgShort = sc.Input[IN_AlertBgShort].GetYesNo();
	bool AlertObrLong = sc.Input[IN_AlertObrLong].GetYesNo(), AlertObrShort = sc.Input[IN_AlertObrShort].GetYesNo();
	bool AlertTramp = sc.Input[IN_AlertTramp].GetYesNo();
	bool AlertThreeOutside = sc.Input[IN_AlertThreeOutside].GetYesNo();
	bool AlertTweezer = sc.Input[IN_AlertTweezer].GetYesNo();
	bool AlertWickPat = sc.Input[IN_AlertWickPat].GetYesNo();
	bool AlertStairs = sc.Input[IN_AlertStairs].GetYesNo();
	bool AlertRevSquare = sc.Input[IN_AlertRevSquare].GetYesNo();
	bool AlertKamaBounce = sc.Input[IN_AlertKamaBounce].GetYesNo();
	bool AlertVodkaBuy = sc.Input[IN_AlertVodkaBuy].GetYesNo(), AlertVodkaSell = sc.Input[IN_AlertVodkaSell].GetYesNo();
	bool AlertSmiDivBull = sc.Input[IN_AlertSmiDivBull].GetYesNo(), AlertSmiDivBear = sc.Input[IN_AlertSmiDivBear].GetYesNo();
	bool AlertSpiderweb = sc.Input[IN_AlertSpiderweb].GetYesNo();
	SCString SoundBullish = sc.Input[IN_SoundBullish].GetString();
	SCString SoundBearish = sc.Input[IN_SoundBearish].GetString();
	SCString SoundNeutral = sc.Input[IN_SoundNeutral].GetString();

	int BuyTriangleOffsetTicks = sc.Input[IN_BuyTriangleOffsetTicks].GetInt();
	int SellTriangleOffsetTicks = sc.Input[IN_SellTriangleOffsetTicks].GetInt();
	int BigArrowUpOffsetTicks = sc.Input[IN_BigArrowUpOffsetTicks].GetInt();
	int BigArrowDownOffsetTicks = sc.Input[IN_BigArrowDownOffsetTicks].GetInt();
	int SqueezeStarUpOffsetTicks = sc.Input[IN_SqueezeStarUpOffsetTicks].GetInt();
	int SqueezeStarDownOffsetTicks = sc.Input[IN_SqueezeStarDownOffsetTicks].GetInt();
	int VolImbUpOffsetTicks = sc.Input[IN_VolImbUpOffsetTicks].GetInt();
	int VolImbDownOffsetTicks = sc.Input[IN_VolImbDownOffsetTicks].GetInt();
	int FcMarkerOffsetTicks = sc.Input[IN_FcMarkerOffsetTicks].GetInt();
	int BgMarkerOffsetTicks = sc.Input[IN_BgMarkerOffsetTicks].GetInt();
	int ObrMarkerOffsetTicks = sc.Input[IN_ObrMarkerOffsetTicks].GetInt();
	int ObrArrowOffsetTicks = sc.Input[IN_ObrArrowOffsetTicks].GetInt();
	int SignalLabelOffsetTicks = sc.Input[IN_SignalLabelOffsetTicks].GetInt();
	int PatternLabelOffsetTicks = sc.Input[IN_PatternLabelOffsetTicks].GetInt();
	int TrampolineOffsetTicks = sc.Input[IN_TrampolineOffsetTicks].GetInt();
	int ReversalSquareOffsetTicks = sc.Input[IN_ReversalSquareOffsetTicks].GetInt();
	int EvilTimesOffsetTicks = sc.Input[IN_EvilTimesOffsetTicks].GetInt();
	int VodkaUpOffsetTicks = sc.Input[IN_VodkaUpOffsetTicks].GetInt();
	int VodkaDownOffsetTicks = sc.Input[IN_VodkaDownOffsetTicks].GetInt();

	COLORREF WaddahUpColor = sc.Input[IN_WaddahUpColor].GetColor();
	COLORREF WaddahDownColor = sc.Input[IN_WaddahDownColor].GetColor();
	COLORREF BuyColorC = sc.Input[IN_BuyColor].GetColor();
	COLORREF SellColorC = sc.Input[IN_SellColor].GetColor();
	COLORREF StarColor = sc.Input[IN_StarColor].GetColor();
	COLORREF VolImbColor = sc.Input[IN_VolImbColor].GetColor();
	COLORREF GapBullLineColor = sc.Input[IN_GapBullLineColor].GetColor();
	COLORREF GapBearLineColor = sc.Input[IN_GapBearLineColor].GetColor();
	COLORREF GapTouchedColor = sc.Input[IN_GapTouchedColor].GetColor();
	COLORREF GapInvalidColor = sc.Input[IN_GapInvalidColor].GetColor();
	COLORREF GapBullZoneColor = sc.Input[IN_GapBullZoneColor].GetColor();
	COLORREF GapBearZoneColor = sc.Input[IN_GapBearZoneColor].GetColor();
	COLORREF TouchMarkerColor = sc.Input[IN_TouchMarkerColor].GetColor();
	COLORREF BgLongColor = sc.Input[IN_BgLongColor].GetColor();
	COLORREF BgShortColor = sc.Input[IN_BgShortColor].GetColor();
	COLORREF FcLongColor = sc.Input[IN_FcLongColor].GetColor();
	COLORREF FcShortColor = sc.Input[IN_FcShortColor].GetColor();
	COLORREF ObrBullColor = sc.Input[IN_ObrBullColor].GetColor();
	COLORREF ObrBearColor = sc.Input[IN_ObrBearColor].GetColor();
	COLORREF ObrLightZoneColor = sc.Input[IN_ObrLightZoneColor].GetColor();
	COLORREF ObrShadowZoneColor = sc.Input[IN_ObrShadowZoneColor].GetColor();
	COLORREF SpiderwebColor = sc.Input[IN_SpiderwebColor].GetColor();
	COLORREF PatternTextColor = sc.Input[IN_PatternTextColor].GetColor();
	COLORREF PatternBullBackColor = sc.Input[IN_PatternBullBackColor].GetColor();
	COLORREF PatternBearBackColor = sc.Input[IN_PatternBearBackColor].GetColor();
	COLORREF TrampTextColor = sc.Input[IN_TrampTextColor].GetColor();
	COLORREF TrampBackColor = sc.Input[IN_TrampBackColor].GetColor();
	COLORREF BbEngulfGreenColor = sc.Input[IN_BbEngulfGreenColor].GetColor();
	COLORREF BbEngulfRedColor = sc.Input[IN_BbEngulfRedColor].GetColor();
	COLORREF ShavedGreenColor = sc.Input[IN_ShavedGreenColor].GetColor();
	COLORREF ShavedRedColor = sc.Input[IN_ShavedRedColor].GetColor();
	COLORREF KamaColorC = sc.Input[IN_KamaColor].GetColor();
	COLORREF BbUpperColorC = sc.Input[IN_BbUpperColor].GetColor();
	COLORREF BbMidColorC = sc.Input[IN_BbMidColor].GetColor();
	COLORREF SmiBullDivColor = sc.Input[IN_SmiBullDivColor].GetColor();
	COLORREF SmiBearDivColor = sc.Input[IN_SmiBearDivColor].GetColor();
	COLORREF SmiHiddenBullDivColor = sc.Input[IN_SmiHiddenBullDivColor].GetColor();
	COLORREF SmiHiddenBearDivColor = sc.Input[IN_SmiHiddenBearDivColor].GetColor();
	COLORREF EvilTimesColor = sc.Input[IN_EvilTimesColor].GetColor();
	COLORREF VodkaUpColor = sc.Input[IN_VodkaUpColor].GetColor();
	COLORREF VodkaDownColor = sc.Input[IN_VodkaDownColor].GetColor();

	bool ShowDashboard = sc.Input[IN_ShowDashboard].GetYesNo();
	ADMDashCorner DashboardCorner = (ADMDashCorner)sc.Input[IN_DashboardCorner].GetIndex();
	int DashboardFontSize = sc.Input[IN_DashboardFontSize].GetInt();
	int DashboardOpacity = sc.Input[IN_DashboardOpacity].GetInt();

	// live-update cosmetic subgraph styling (cheap, fine to redo every call)
	sc.Subgraph[SG_KAMA].DrawStyle = ShowKama ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
	sc.Subgraph[SG_KAMA].PrimaryColor = KamaColorC;
	sc.Subgraph[SG_KAMA].LineWidth = KamaLineWidth;
	sc.Subgraph[SG_BBU].DrawStyle = ShowBollingerBands ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
	sc.Subgraph[SG_BBU].PrimaryColor = BbUpperColorC;
	sc.Subgraph[SG_BBU].LineWidth = BbLineWidth;
	sc.Subgraph[SG_BBL].DrawStyle = ShowBollingerBands ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
	sc.Subgraph[SG_BBL].PrimaryColor = BbUpperColorC;
	sc.Subgraph[SG_BBL].LineWidth = BbLineWidth;
	sc.Subgraph[SG_BBM].DrawStyle = (ShowBollingerBands && ShowBollingerMiddle) ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
	sc.Subgraph[SG_BBM].PrimaryColor = BbMidColorC;
	sc.Subgraph[SG_BBM].LineWidth = BbMidWidth;
	sc.Subgraph[SG_BARCOLOR].DrawStyle = (BarColor != ADM_BC_None) ? DRAWSTYLE_COLOR_BAR : DRAWSTYLE_IGNORE;

	if (sc.ArraySize < 1)
		return;

	// ================= BAR-CLOSE GATE (Calculate.OnBarClose) =================
	if (sc.GetBarHasClosedStatus(sc.Index) != BHCS_BAR_HAS_CLOSED)
		return;

	const int cb = sc.Index;
	const double tick = sc.TickSize;
	const double Close0 = sc.Close[cb], Open0 = sc.Open[cb], High0 = sc.High[cb], Low0 = sc.Low[cb];
	const double Volume0 = sc.Volume[cb];
	const double Median0 = (High0 + Low0) / 2.0;

	auto Adapt = [&](double baseValue, bool scope) -> double {
		return (AdaptiveThresholds && scope) ? baseValue * S.volRatio : baseValue;
	};
	auto EffectiveMinAdx = [&]() -> double {
		if (!AdaptiveThresholds || !AdaptAdxFloor) return (double)MinAdx;
		return MinAdx * (std::max)(0.5, (std::min)(2.0, sqrt(S.volRatio)));
	};
	auto NextLineId = [&]() -> int { return S.nextLineId++; };
	auto GetHHMMSS = [&](int idx) -> int {
		int hh = 0, mm = 0, ss = 0;
		sc.BaseDateTimeIn[idx].GetTimeHMS(hh, mm, ss);
		return hh * 10000 + mm * 100 + ss;
	};

	// ---- reset this bar's signal outputs (0 == "no signal" convention, mirrors ResetOutputs) ----
	{
		// prune stacking maps (bars older than 6 back are no longer relevant)
		for (std::map<int,double>::iterator it = S.stackUp.begin(); it != S.stackUp.end(); )
			it = (it->first < cb - 6) ? S.stackUp.erase(it) : std::next(it);
		for (std::map<int,double>::iterator it = S.stackDn.begin(); it != S.stackDn.end(); )
			it = (it->first < cb - 6) ? S.stackDn.erase(it) : std::next(it);

		int zeroList[] = { SG_BUY, SG_SELL, SG_LOWTOUCH, SG_UPTOUCH, SG_MIDCLOSE, SG_CONFLUENCE,
			SG_BIGARROW, SG_SQUEEZE, SG_PATTERN, SG_VOLIMB, SG_GAPTOUCH, SG_INVALIDTOUCH, SG_CONT,
			SG_BGAP, SG_OBR, SG_ACTIVEGAPS, SG_VALIDGAPS, SG_PENDING, SG_WEBCOUNT, SG_WEBWARN,
			SG_NEARGAP, SG_ENTRY, SG_STOP, SG_TARGET, SG_DIR, SG_CODE, SG_MLONG, SG_MSHORT,
			SG_SMI, SG_SMIAVG, SG_SMISTATE, SG_SMIDIV, SG_WAVE, SG_VODKA };
		for (size_t i = 0; i < sizeof(zeroList)/sizeof(zeroList[0]); i++)
			sc.Subgraph[zeroList[i]][cb] = 0.0;
		sc.Subgraph[SG_VOLRATIO][cb] = 1.0;
		sc.Subgraph[SG_BARCOLOR].DataColor[cb] = sc.Subgraph[SG_BARCOLOR].PrimaryColor; // no-op unless overwritten below
	}

	// grow shadow-series vectors to cover this bar (no-op if already sized)
	auto Grow = [&](std::vector<double>& v) { if ((int)v.size() <= cb) v.resize(cb + 1, 0.0); };
	auto GrowC = [&](std::vector<char>& v) { if ((int)v.size() <= cb) v.resize(cb + 1, 0); };
	auto GrowI = [&](std::vector<int>& v)  { if ((int)v.size() <= cb) v.resize(cb + 1, 0); };
	Grow(S.lindaLine); Grow(S.aoLine); Grow(S.fisherValue); Grow(S.sqzRaw);
	Grow(S.sumDmPlus); Grow(S.sumDmMinus); Grow(S.diPlusIdx); Grow(S.diMinusIdx);
	Grow(S.adxIdx); Grow(S.adxvmaLine); Grow(S.adxvmaTrend); Grow(S.tsiLine);
	Grow(S.rsiUpS); Grow(S.rsiDnS); Grow(S.rsiCut);
	Grow(S.vdkMacd); Grow(S.wadAbsS); Grow(S.volAtrS); Grow(S.lindaAbsS);
	GrowC(S.brightGreenS); GrowC(S.brightRedS); GrowC(S.upVodkaS); GrowC(S.downVodkaS);
	GrowI(S.waveS);
	Grow(S.smiNum1); Grow(S.smiDen1); Grow(S.smiNum2); Grow(S.smiDen2); Grow(S.smiLine); Grow(S.smiAvgLine);
	Grow(S.rsiStdV); Grow(S.vdkWma1V); Grow(S.vdkWma2V);

	// ================= BAR 0: seed and reset engine state (mirrors SeedFirstBar) =================
	if (cb == 0)
	{
		ADM_ResetState(S);
		Grow(S.lindaLine); Grow(S.aoLine); Grow(S.fisherValue); Grow(S.sqzRaw);
		Grow(S.sumDmPlus); Grow(S.sumDmMinus); Grow(S.diPlusIdx); Grow(S.diMinusIdx);
		Grow(S.adxIdx); Grow(S.adxvmaLine); Grow(S.adxvmaTrend); Grow(S.tsiLine);
		Grow(S.rsiUpS); Grow(S.rsiDnS); Grow(S.rsiCut);
		Grow(S.vdkMacd); Grow(S.wadAbsS); Grow(S.volAtrS); Grow(S.lindaAbsS);
		GrowC(S.brightGreenS); GrowC(S.brightRedS); GrowC(S.upVodkaS); GrowC(S.downVodkaS);
		GrowI(S.waveS);
		Grow(S.smiNum1); Grow(S.smiDen1); Grow(S.smiNum2); Grow(S.smiDen2); Grow(S.smiLine); Grow(S.smiAvgLine);
		Grow(S.rsiStdV); Grow(S.vdkWma1V); Grow(S.vdkWma2V);

		S.warmupBars = (std::max)(BbPeriod + 2, (std::max)(SqueezePeriod + 2, 45));
		if (UseSmi || EnableSmiDivergence)
			S.warmupBars = (std::max)(S.warmupBars, SmiPeriodK + 3*SmiSmooth1 + 3*SmiSmooth2 + 2*SmiSignalPeriod);

		S.rsiUpS[0] = 0; S.rsiDnS[0] = 0; S.rsiCut[0] = 50; S.rsiStdV[0] = 50;
		S.vdkWma1V[0] = Close0; S.vdkWma2V[0] = Close0;
		double smiNum0 = Close0 - 0.5 * (High0 + Low0);
		double smiDen0 = High0 - Low0;
		S.smiNum1[0] = smiNum0; S.smiNum2[0] = smiNum0;
		S.smiDen1[0] = smiDen0; S.smiDen2[0] = smiDen0;
		S.smiLine[0] = ADM_IsZero(smiDen0) ? 0.0 : (std::min)(100.0, (std::max)(-100.0, 200.0 * smiNum0 / smiDen0));
		S.smiAvgLine[0] = S.smiLine[0];
		S.adxvmaLine[0] = Close0;
		S.kamaPrev = Close0; S.kamaInit = true;
		S.sarValue = Low0; S.sarTrend = 1; S.sarEp = High0; S.sarAf = PsarAcceleration;
		S.stMain.SierraLag = SierraExactMode;
		S.stMain.Update(Close0, Close0, Median0, 0, StMultiplier);
		S.stLiz.Update(Close0, Close0, Median0, 0, LizStMultiplier);
		S.volAtr.Push(High0 - Low0, AdaptAtrLength);

		sc.Subgraph[SG_KAMA][cb] = Close0;
		sc.Subgraph[SG_BBU][cb] = Close0; sc.Subgraph[SG_BBM][cb] = Close0; sc.Subgraph[SG_BBL][cb] = Close0;
		return;
	}

	// ================= UpdateEngine: recursive indicators, run every bar >= 1 =================
	const double prevClose = sc.Close[cb - 1];

	auto CloseAt  = [&](int i) { return sc.Close[i]; };
	auto OpenAt   = [&](int i) { return sc.Open[i]; };
	auto HighAt   = [&](int i) { return sc.High[i]; };
	auto LowAt    = [&](int i) { return sc.Low[i]; };
	auto MedianAt = [&](int i) { return (sc.High[i] + sc.Low[i]) / 2.0; };
	auto TRAt = [&](int i) -> double {
		double hi = sc.High[i], lo = sc.Low[i];
		if (i <= 0) return hi - lo;
		double pc = sc.Close[i - 1];
		return (std::max)(hi - lo, (std::max)(fabs(hi - pc), fabs(lo - pc)));
	};

	// ---- Bollinger Bands (population std-dev) ----
	auto BollingerAt = [&](int idx, double& mean, double& upper, double& lower) {
		int n = (idx + 1 < BbPeriod) ? (idx + 1) : BbPeriod;
		double sum = 0; for (int i = 0; i < n; i++) sum += sc.Close[idx - i];
		mean = sum / n;
		double sq = 0; for (int i = 0; i < n; i++) { double d = sc.Close[idx - i] - mean; sq += d * d; }
		upper = mean + BbStdDev * sqrt(sq / n);
		lower = mean - BbStdDev * sqrt(sq / n);
	};
	BollingerAt(cb, S.bbM, S.bbU, S.bbL);
	{ double m1; BollingerAt(cb - 1, m1, S.bbU1, S.bbL1); }

	// ---- Waddah Explosion (rate of change of the EMA20-EMA40 spread) ----
	double ema20Prev = S.ema20.value; bool ema20WasInit = S.ema20.init;
	double ema20Now = S.ema20.Push(Close0, 20);
	double ema40Prev = S.ema40.value; bool ema40WasInit = S.ema40.init;
	double ema40Now = S.ema40.Push(Close0, 40);
	S.cT1 = (ema20WasInit && ema40WasInit) ? ((ema20Now - ema40Now) - (ema20Prev - ema40Prev)) * WaddahIntensity : 0.0;
	S.wadAbsS[cb] = fabs(S.cT1);
	S.cWadAvg = ADM_RollingSma(S.wadAbsAvgWin, S.wadAbsAvgSum, S.wadAbsS[cb], WaddahAutoLength);
	S.cLinWadAvg = ADM_RollingSma(S.linWadAbsAvgWin, S.linWadAbsAvgSum, S.wadAbsS[cb], LindaAutoLength);

	// ---- Linda MACD histogram ----
	double smaLindaFast = ADM_SmaAt(CloseAt, cb, LindaFast);
	double smaLindaSlow = ADM_SmaAt(CloseAt, cb, SierraExactMode ? 9 : LindaSlow);
	S.lindaLine[cb] = smaLindaFast - smaLindaSlow;
	double smaLindaSig = ADM_SmaAt([&](int i) { return S.lindaLine[i]; }, cb, LindaSignal);
	S.cLinda = S.lindaLine[cb] - smaLindaSig;
	S.lindaAbsS[cb] = fabs(S.cLinda);
	S.cLindaAvg = ADM_RollingSma(S.lindaAbsAvgWin, S.lindaAbsAvgSum, S.lindaAbsS[cb], LindaAutoLength);

	// ---- Awesome Oscillator ----
	double smaAo5 = ADM_SmaAt(MedianAt, cb, 5);
	double smaAo34 = ADM_SmaAt(MedianAt, cb, 34);
	S.aoLine[cb] = smaAo5 - smaAo34;
	S.cAo = S.aoLine[cb];
	S.cAo1 = S.aoLine[cb - 1];

	// ---- Parabolic SAR (Wells Wilder) ----
	{
		double sar;
		double loLag1 = sc.Low[cb - 1], loLag2 = (cb >= 2) ? sc.Low[cb - 2] : sc.Low[cb - 1];
		double hiLag1 = sc.High[cb - 1], hiLag2 = (cb >= 2) ? sc.High[cb - 2] : sc.High[cb - 1];
		if (S.sarTrend > 0)
		{
			sar = S.sarValue + S.sarAf * (S.sarEp - S.sarValue);
			sar = (std::min)(sar, (std::min)(loLag1, loLag2));
			if (High0 > S.sarEp) { S.sarEp = High0; S.sarAf = (std::min)(PsarMax, S.sarAf + PsarStep); }
			if (Low0 < sar) { S.sarTrend = -1; sar = S.sarEp; S.sarEp = Low0; S.sarAf = PsarAcceleration; }
		}
		else
		{
			sar = S.sarValue - S.sarAf * (S.sarValue - S.sarEp);
			sar = (std::max)(sar, (std::max)(hiLag1, hiLag2));
			if (Low0 < S.sarEp) { S.sarEp = Low0; S.sarAf = (std::min)(PsarMax, S.sarAf + PsarStep); }
			if (High0 > sar) { S.sarTrend = 1; sar = S.sarEp; S.sarEp = High0; S.sarAf = PsarAcceleration; }
		}
		S.sarValue = sar;
		S.cSar = sar;
	}

	// ---- Classic Wilder ADX ----
	{
		double upMove = High0 - sc.High[cb - 1];
		double downMove = sc.Low[cb - 1] - Low0;
		double dmPlus = (upMove > downMove && upMove > 0) ? upMove : 0.0;
		double dmMinus = (downMove > upMove && downMove > 0) ? downMove : 0.0;
		double trW = S.adxTR.Push(TRAt(cb), AdxPeriod);
		double dmPlusW = S.adxDmPlus.Push(dmPlus, AdxPeriod);
		double dmMinusW = S.adxDmMinus.Push(dmMinus, AdxPeriod);
		double diPlus = ADM_IsZero(trW) ? 0.0 : 100.0 * dmPlusW / trW;
		double diMinus = ADM_IsZero(trW) ? 0.0 : 100.0 * dmMinusW / trW;
		double dx = (diPlus + diMinus) <= 1e-9 ? 0.0 : 100.0 * fabs(diPlus - diMinus) / (diPlus + diMinus);
		S.cAdx = S.adxDx.Push(dx, AdxPeriod);
	}

	// ---- HMA, T3, KAMA, SuperTrend ATRs ----
	S.cHma = ADM_HmaAt(CloseAt, cb, HmaPeriod);
	S.cHma1 = ADM_HmaAt(CloseAt, cb - 1, HmaPeriod);

	{
		double t3Src = Close0;
		int stages = (std::max)(1, (std::min)(6, T3TCount));
		for (int st = 0; st < stages; st++)
		{
			double e1 = S.t3Ema1[st].Push(t3Src, T3Period);
			double e2 = S.t3Ema2[st].Push(e1, T3Period);
			t3Src = e1 * (1.0 + T3VFactor) - e2 * T3VFactor;
		}
		S.cT3 = t3Src;
	}

	// ---- Fisher Transform: Sierra formulation (median-based, no ln step) ----
	{
		double fHigh = ADM_MaxAt(MedianAt, cb, 10);
		double fLow = ADM_MinAt(MedianAt, cb, 10);
		double fRange = fHigh - fLow;
		S.fisherValue[cb] = ADM_IsZero(fRange) ? 0.0 : 0.66 * ((Median0 - fLow) / fRange - 0.5) + 0.67 * S.fisherValue[cb - 1];
	}
	// ---- Fisher Transform: classic Ehlers formulation on Close (NinjaLegacy path) ----
	{
		double maxC = ADM_MaxAt(CloseAt, cb, 10);
		double minC = ADM_MinAt(CloseAt, cb, 10);
		double range = maxC - minC;
		double rawPrev = S.fishInit ? S.fishRaw : 0.0;
		double raw = ADM_IsZero(range) ? 0.0 : 0.33 * 2.0 * ((Close0 - minC) / range - 0.5) + 0.67 * rawPrev;
		raw = (std::max)(-0.999, (std::min)(0.999, raw));
		double fisherPrev = S.fishInit ? S.fishNt : 0.0;
		double fisherNow = 0.5 * log((1.0 + raw) / (1.0 - raw)) + 0.5 * fisherPrev;
		S.cFishNt1 = S.fishInit ? S.fishNt : fisherNow;
		S.cFishNt = fisherNow;
		S.fishRaw = raw; S.fishNt = fisherNow; S.fishInit = true;
	}

	// ---- KAMA ----
	{
		int per = (std::min)(KamaPeriod, cb);
		double changeSum = 0;
		for (int i = 0; i < per; i++) changeSum += fabs(sc.Close[cb - i] - sc.Close[cb - i - 1]);
		double change = (cb >= KamaPeriod) ? fabs(Close0 - sc.Close[cb - KamaPeriod]) : fabs(Close0 - sc.Close[0]);
		double er = ADM_IsZero(changeSum) ? 0.0 : change / changeSum;
		double fastSC = 2.0 / (KamaFast + 1.0), slowSC = 2.0 / (KamaSlow + 1.0);
		double scFactor = er * (fastSC - slowSC) + slowSC;
		scFactor *= scFactor;
		S.kamaPrev = S.kamaPrev + scFactor * (Close0 - S.kamaPrev);
		S.cKama = S.kamaPrev;
	}

	// ---- RSI: Sierra (simple-average) and classic (Wilder) ----
	{
		double chg = Close0 - prevClose;
		S.rsiUpS[cb] = (std::max)(chg, 0.0);
		S.rsiDnS[cb] = (std::max)(-chg, 0.0);
		double avgUp = ADM_SmaAt([&](int i) { return S.rsiUpS[i]; }, cb, 14);
		double avgDn = ADM_SmaAt([&](int i) { return S.rsiDnS[i]; }, cb, 14);
		S.rsiCut[cb] = (avgUp + avgDn <= 0) ? 50.0 : 100.0 * avgUp / (avgUp + avgDn);

		double gain = (std::max)(chg, 0.0), loss = (std::max)(-chg, 0.0);
		double avgGainW = S.rsiGainW.Push(gain, 14);
		double avgLossW = S.rsiLossW.Push(loss, 14);
		S.rsiStdV[cb] = (avgGainW + avgLossW <= 0) ? 50.0 : 100.0 * avgGainW / (avgGainW + avgLossW);
	}
	if (SierraExactMode)
	{
		S.cRsi0 = S.rsiCut[cb]; S.cRsi1 = S.rsiCut[cb - 1]; S.cRsi2 = (cb >= 2) ? S.rsiCut[cb - 2] : S.rsiCut[cb - 1];
	}
	else
	{
		S.cRsi0 = S.rsiStdV[cb]; S.cRsi1 = S.rsiStdV[cb - 1]; S.cRsi2 = (cb >= 2) ? S.rsiStdV[cb - 2] : S.rsiStdV[cb - 1];
	}

	// ---- SuperTrend (main = "God Trades" Hull/Wilder ATR band, liz = Lizard median band) ----
	{
		double hullAtr = (std::max)(0.0, ADM_HmaAt(TRAt, cb, StPeriod));
		double wilderAtr = S.stWilderAtr.Push(TRAt(cb), StPeriod);
		double lizAtrV = S.lizAtr.Push(TRAt(cb), LizStAtrPeriod);
		S.stMain.SierraLag = SierraExactMode;
		S.stMain.Update(Close0, prevClose, Median0, (StAtrType == ADM_ST_Hull) ? hullAtr : wilderAtr, StMultiplier);
		S.stLiz.Update(Close0, prevClose, Median0, lizAtrV, LizStMultiplier);
	}

	// ---- Squeeze Momentum histogram ----
	{
		double sqEmaVal = S.sqEma.Push(Close0, SqueezePeriod);
		double sqBasis;
		if (SierraExactMode)
		{
			double kSq = 2.0 / (SqueezePeriod + 1.0);
			double lr1 = (cb - 1 >= SqueezePeriod - 1) ? ADM_LinRegAt(CloseAt, cb - 1, SqueezePeriod) : prevClose;
			double anchor = (cb >= SqueezePeriod) ? lr1 : prevClose;
			sqBasis = kSq * Close0 + (1.0 - kSq) * anchor;
		}
		else
		{
			sqBasis = sqEmaVal;
		}
		double sqSource = (SqueezeUsesOpen || SierraExactMode) ? Open0 : Close0;
		double sqHighV = ADM_MaxAt(HighAt, cb, SqueezePeriod);
		double sqLowV = ADM_MinAt(LowAt, cb, SqueezePeriod);
		S.sqzRaw[cb] = sqSource - (((sqHighV + sqLowV) / 2.0) + sqBasis) / 2.0;
		S.cSqHist = ADM_LinRegAt([&](int i) { return S.sqzRaw[i]; }, cb, SqueezePeriod);
		S.cSqHist1 = ADM_LinRegAt([&](int i) { return S.sqzRaw[i]; }, cb - 1, SqueezePeriod);
	}

	// ---- ADXVMA trend (inline port of amaADXVMAPlus, Close-only) ----
	{
		int period = AdxvmaPeriod;
		double k = 1.0 / period;
		double deltaFactor = 0.5 / (10.0 * sqrt((double)period));
		int minBars = (int)(40.0 * sqrt((double)period));

		double in0 = Close0, in1 = prevClose;
		double dmPlus = (in0 - in1 > in1 - in0) ? (std::max)(in0 - in1, 0.0) : 0.0;
		double dmMinus = (in1 - in0 > in0 - in1) ? (std::max)(in1 - in0, 0.0) : 0.0;

		S.sumDmPlus[cb] = (cb < period) ? (S.sumDmPlus[cb - 1] + dmPlus) : ((1 - k) * S.sumDmPlus[cb - 1] + dmPlus);
		S.sumDmMinus[cb] = (cb < period) ? (S.sumDmMinus[cb - 1] + dmMinus) : ((1 - k) * S.sumDmMinus[cb - 1] + dmMinus);

		double diPlus = S.sumDmPlus[cb], diMinus = S.sumDmMinus[cb];
		double sum = diPlus + diMinus;
		S.diPlusIdx[cb] = ADM_IsZero(sum) ? 0.0 : (1 - k) * S.diPlusIdx[cb - 1] + diPlus / sum;
		S.diMinusIdx[cb] = ADM_IsZero(sum) ? 0.0 : (1 - k) * S.diMinusIdx[cb - 1] + diMinus / sum;

		sum = S.diPlusIdx[cb] + S.diMinusIdx[cb];
		double diff = fabs(S.diPlusIdx[cb] - S.diMinusIdx[cb]);
		S.adxIdx[cb] = ADM_IsZero(sum) ? S.adxIdx[cb - 1] : (1 - k) * S.adxIdx[cb - 1] + k * diff / sum;

		double hhp = ADM_MaxAt([&](int i) { return S.adxIdx[i]; }, cb - 1, period);
		double llp = ADM_MinAt([&](int i) { return S.adxIdx[i]; }, cb - 1, period);
		double hhv = (std::max)(S.adxIdx[cb], hhp);
		double llv = (std::min)(S.adxIdx[cb], llp);
		double vDiff = hhv - llv;
		double vIndex = ADM_IsZero(vDiff) ? 1.0 : (S.adxIdx[cb] - llv) / vDiff;

		S.adxvmaLine[cb] = (1 - k * vIndex) * S.adxvmaLine[cb - 1] + k * vIndex * in0;

		double vol1 = S.adxVolatility.value; // value as of the previous bar, before this bar's push
		S.adxVolatility.Push(TRAt(cb), 10 * AdxvmaPeriod);

		if (cb < minBars || cb < 3)
		{
			S.adxvmaTrend[cb] = 0;
		}
		else
		{
			double refValue = S.adxvmaLine[cb - 1] + S.adxvmaLine[cb - 2];
			double delta = deltaFactor * vol1;
			double prevTrend = S.adxvmaTrend[cb - 1];
			if (prevTrend > -0.5 && 2 * S.adxvmaLine[cb] > refValue + 3 * delta) S.adxvmaTrend[cb] = 1;
			else if (prevTrend < 0.5 && 2 * S.adxvmaLine[cb] < refValue - 3 * delta) S.adxvmaTrend[cb] = -1;
			else S.adxvmaTrend[cb] = 0;
		}
	}

	// ---- Multi TSI ----
	{
		double mom = Close0 - prevClose;
		double absMom = fabs(mom);
		double num1 = S.tsiNum1.Push(mom, TsiSmooth1);
		double num2 = S.tsiNum2.Push(num1, TsiSmooth2);
		double den1 = S.tsiDen1.Push(absMom, TsiSmooth1);
		double den2 = S.tsiDen2.Push(den1, TsiSmooth2);
		double epsilon = tick / (10.0 * (TsiSmooth1 + TsiSmooth2 + 1));
		S.tsiLine[cb] = (den2 < epsilon) ? S.tsiLine[cb - 1] : (std::min)(100.0, (std::max)(-100.0, 100.0 * num2 / den2));
		S.cTsi = S.tsiLine[cb];
		S.cTsiSig = S.tsiSignal.Push(S.tsiLine[cb], TsiSignalPeriod);
	}

	// ---- SMI (Stochastic Momentum Index) ----
	{
		double hh = ADM_MaxAt(HighAt, cb, SmiPeriodK);
		double ll = ADM_MinAt(LowAt, cb, SmiPeriodK);
		double num = Close0 - 0.5 * (hh + ll);
		double den = hh - ll;
		double k1 = 2.0 / (1 + SmiSmooth1), k2 = 2.0 / (1 + SmiSmooth2), k3 = 2.0 / (1 + SmiSignalPeriod);

		S.smiNum1[cb] = k1 * num + (1 - k1) * S.smiNum1[cb - 1];
		S.smiDen1[cb] = k1 * den + (1 - k1) * S.smiDen1[cb - 1];
		S.smiNum2[cb] = k2 * S.smiNum1[cb] + (1 - k2) * S.smiNum2[cb - 1];
		S.smiDen2[cb] = k2 * S.smiDen1[cb] + (1 - k2) * S.smiDen2[cb - 1];

		double smi = (ADM_IsZero(den) || ADM_IsZero(S.smiDen2[cb])) ? S.smiLine[cb - 1]
			: (std::min)(100.0, (std::max)(-100.0, 200.0 * S.smiNum2[cb] / S.smiDen2[cb]));
		S.smiLine[cb] = smi;
		S.smiAvgLine[cb] = (std::min)(100.0, (std::max)(-100.0, k3 * smi + (1 - k3) * S.smiAvgLine[cb - 1]));

		S.cSmi = smi;
		S.cSmiPrev = S.smiLine[cb - 1];
		S.cSmiAvg = S.smiAvgLine[cb];

		int state = 0;
		if (S.cSmi > S.cSmiAvg) state = S.cSmi < SmiOversold ? 5 : (S.cSmi > SmiMidline ? 4 : 3);
		else if (S.cSmi < S.cSmiAvg) state = S.cSmi > SmiOverbought ? -5 : (S.cSmi < SmiMidline ? -4 : -3);

		sc.Subgraph[SG_SMI][cb] = S.cSmi;
		sc.Subgraph[SG_SMIAVG][cb] = S.cSmiAvg;
		sc.Subgraph[SG_SMISTATE][cb] = state;
	}

	// ---- Volatility ratio (adaptive thresholds) ----
	{
		double atrNow = S.volAtr.Push(TRAt(cb), AdaptAtrLength);
		S.volAtrS[cb] = atrNow;
		double avgAtr = ADM_SmaAt([&](int i) { return S.volAtrS[i]; }, cb, AdaptAverageLength);
		double raw = 1.0;
		if (avgAtr > 0) raw = (std::max)(AdaptMinRatio, (std::min)(AdaptMaxRatio, atrNow / avgAtr));
		S.volRatio = AdaptiveThresholds ? raw : 1.0;
		sc.Subgraph[SG_VOLRATIO][cb] = raw;
	}

	// ---- Wave state (Tidal Wave) ----
	{
		int prev = (S.waveS[cb - 1] == 0) ? -1 : S.waveS[cb - 1];
		S.wavePrev = prev;
		int wave = prev;
		bool green = Close0 > Open0, red = Close0 < Open0;
		bool doji = WaveIgnoreDojis && fabs(Close0 - Open0) <= WaveDojiMaxTicks * tick;
		bool brightGreen = false, brightRed = false;
		int maxBack = (std::min)(WaveLookback, cb);

		if (green && !doji)
		{
			for (int i = 1; i <= maxBack; i++)
			{
				if (S.brightRedS[cb - i]) break;
				if (wave == 1 && sc.Close[cb - i] < sc.Open[cb - i]) break;
				if (wave == -1 && Open0 >= sc.Close[cb - i] && sc.Close[cb - i] > sc.Open[cb - i]) { brightGreen = true; wave = 1; break; }
			}
			if (Open0 >= sc.Close[cb - 1] && sc.Close[cb - 1] > sc.Open[cb - 1]) { wave = 1; brightGreen = true; }
		}
		if (red && !doji)
		{
			for (int i = 1; i <= maxBack; i++)
			{
				if (S.brightGreenS[cb - i]) break;
				if (wave == -1 && sc.Close[cb - i] > sc.Open[cb - i]) break;
				if (wave == 1 && Open0 <= sc.Close[cb - i] && sc.Close[cb - i] < sc.Open[cb - i]) { brightRed = true; wave = -1; break; }
			}
			if (sc.Close[cb - 1] < sc.Open[cb - 1] && Open0 < sc.Close[cb - 1]) { wave = -1; brightRed = true; }
		}

		S.brightGreenS[cb] = brightGreen ? 1 : 0;
		S.brightRedS[cb] = brightRed ? 1 : 0;
		S.waveS[cb] = wave;
		sc.Subgraph[SG_WAVE][cb] = wave;
	}

	// ---- Vodka Shot raw conditions ----
	{
		double macdVal = S.vdkEmaFast.Push(Close0, VodkaMacdFast) - S.vdkEmaSlow.Push(Close0, VodkaMacdSlow);
		S.vdkMacd[cb] = macdVal;
		double macdSignal = S.vdkSignal.Push(macdVal, VodkaMacdSignal);

		int lzW2 = (std::max)(1, (int)floor(VodkaTrendLength / 3.0 + 0.5));
		int lzW1 = (std::max)(1, (int)floor((VodkaTrendLength - lzW2) / 2.0 + 0.5));
		int lzW3 = (std::max)(1, (VodkaTrendLength - lzW2) / 2);
		S.vdkWma1V[cb] = ADM_WmaAt(CloseAt, cb, lzW1);
		S.vdkWma2V[cb] = ADM_WmaAt([&](int i) { return S.vdkWma1V[i]; }, cb, lzW2);
		double lazy0 = ADM_WmaAt([&](int i) { return S.vdkWma2V[i]; }, cb, lzW3);
		double lazy1 = ADM_WmaAt([&](int i) { return S.vdkWma2V[i]; }, cb - 1, lzW3);

		int nVol = (std::min)(VodkaVolumeLength, cb + 1);
		double sumVol = 0; for (int i = 0; i < nVol; i++) sumVol += sc.Volume[cb - i];
		double meanVol = nVol ? sumVol / nVol : 0;
		double sq = 0; for (int i = 0; i < nVol; i++) { double d = sc.Volume[cb - i] - meanVol; sq += d * d; }
		double stdVol = (nVol <= 1) ? 0 : sqrt(sq / nVol);
		double volAvg = (std::max)(1.0, meanVol);
		double volDev = (volAvg + 1.618034 * stdVol) / volAvg * 11.0 / 100.0;
		double volRel = Volume0 / volAvg;
		bool volumeOk = volRel * 0.145898 > volDev * VodkaVolumeScale;

		bool green = Close0 > Open0, red = Close0 < Open0;
		bool lazyUp = lazy0 > lazy1;
		bool energyUp = macdVal >= macdSignal, energyDown = macdVal < macdSignal;
		bool vodkaUp = lazyUp && energyUp && green && volumeOk;
		bool vodkaDown = !lazyUp && energyDown && red && volumeOk;

		bool quietUp = true, quietDown = true;
		int quiet = (std::min)(VodkaCooldownBars, cb);
		for (int k = 1; k <= quiet; k++)
		{
			if (S.upVodkaS[cb - k]) quietUp = false;
			if (S.downVodkaS[cb - k]) quietDown = false;
		}

		S.vodkaBuyRaw = vodkaUp && quietUp;
		S.vodkaSellRaw = vodkaDown && quietDown;
		S.upVodkaS[cb] = vodkaUp ? 1 : 0;
		S.downVodkaS[cb] = vodkaDown ? 1 : 0;
	}

	if (cb < S.warmupBars)
		return;

	// ================= CacheBar =================
	S.cGreen = Close0 > Open0;
	S.cRed = Close0 < Open0;
	S.cBody = fabs(Close0 - Open0);
	S.cPBody = fabs(prevClose - sc.Open[cb - 1]);
	{
		double upperWick = S.cGreen ? High0 - Close0 : High0 - Open0;
		double lowerWick = S.cGreen ? Open0 - Low0 : Close0 - Low0;
		if (SierraExactMode) { upperWick = trunc(upperWick); lowerWick = trunc(lowerWick); }
		S.cDoji = upperWick > S.cBody && lowerWick > S.cBody;
	}

	// ================= UpdateBasicOutputs =================
	sc.Subgraph[SG_KAMA][cb] = S.cKama;
	sc.Subgraph[SG_BBU][cb] = S.bbU;
	sc.Subgraph[SG_BBM][cb] = S.bbM;
	sc.Subgraph[SG_BBL][cb] = S.bbL;
	sc.Subgraph[SG_LOWTOUCH][cb] = (Low0 <= S.bbL) ? 1 : 0;
	sc.Subgraph[SG_UPTOUCH][cb] = (High0 >= S.bbU) ? -1 : 0;
	sc.Subgraph[SG_MIDCLOSE][cb] = (Close0 < S.bbM) ? 1 : (Close0 > S.bbM ? -1 : 0);

	// ================= Stacking + drawing helpers (mirror the original's signal layout) =================
	auto PlaceTicks = [&](bool above, int barIndex, double baseTicks, double spanTicks) -> double {
		if (!StackSignals) return baseTicks;
		std::map<int,double>& d = above ? S.stackUp : S.stackDn;
		std::map<int,double>::iterator it = d.find(barIndex);
		double start = (it != d.end()) ? (std::max)(baseTicks, it->second + StackStepTicks) : baseTicks;
		d[barIndex] = start + (std::max)(0.0, spanTicks);
		return start;
	};
	auto PriceAt = [&](bool above, int barsAgo, double ticks) -> double {
		return above ? sc.High[cb - barsAgo] + ticks * tick : sc.Low[cb - barsAgo] - ticks * tick;
	};
	auto StackY = [&](bool above, int baseTicks, int barsAgo) -> double {
		return PriceAt(above, barsAgo, PlaceTicks(above, cb - barsAgo, baseTicks, 0));
	};
	auto StackPairY = [&](bool above, int markerTicks, int labelTicks, bool withLabel, double& labelY) -> double {
		double span = withLabel ? (std::max)((double)(labelTicks - markerTicks), 4.0) : 0.0;
		double m = PlaceTicks(above, cb, markerTicks, span);
		labelY = PriceAt(above, 0, m + span);
		return PriceAt(above, 0, m);
	};
	auto DrawGlyph = [&](const char* glyph, double y, COLORREF color, int fontSize) {
		ADM_DrawText(sc, NextLineId(), cb, y, glyph, color, fontSize, true, true);
	};
	auto DrawLabel = [&](const char* text, int barsAgo, bool above, COLORREF fore, COLORREF back, int offsetTicks) {
		double y = StackY(above, offsetTicks, barsAgo);
		s_UseTool tool; tool.Clear();
		tool.ChartNumber = sc.ChartNumber;
		tool.DrawingType = DRAWING_TEXT;
		tool.LineNumber = NextLineId();
		tool.BeginDateTime = sc.BaseDateTimeIn[cb - barsAgo];
		tool.BeginValue = (float)y;
		tool.Color = RGB(255,255,255);
		tool.TransparencyLevel = 35;
		tool.FontSize = 9;
		tool.FontBold = 1;
		tool.Text = text;
		tool.TextAlignment = DT_CENTER | DT_VCENTER;
		tool.AddMethod = UTAM_ADD_ALWAYS;
		tool.AddAsUserDrawnDrawing = 0;
		sc.UseTool(tool);
	};
	// Same as DrawLabel but at an explicit, already-computed Y (used when a marker and its
	// label must share one stacking slot, e.g. FC/BG/OBR - mirrors the original's StackPairY).
	auto DrawTextAt = [&](const char* text, int barsAgo, double y, COLORREF fore, COLORREF back) {
		s_UseTool tool; tool.Clear();
		tool.ChartNumber = sc.ChartNumber;
		tool.DrawingType = DRAWING_TEXT;
		tool.LineNumber = NextLineId();
		tool.BeginDateTime = sc.BaseDateTimeIn[cb - barsAgo];
		tool.BeginValue = (float)y;
		tool.Color = RGB(255,255,255);
		tool.FontBackColor = back;
		tool.TransparencyLevel = 15;
		tool.FontSize = 9;
		tool.FontBold = 1;
		tool.Text = text;
		tool.TextAlignment = DT_CENTER | DT_VCENTER;
		tool.AddMethod = UTAM_ADD_ALWAYS;
		tool.AddAsUserDrawnDrawing = 0;
		sc.UseTool(tool);
	};

	// polarity: +1 bullish sound, -1 bearish sound, 0 neutral sound
	auto FireAlert = [&](bool enabledFlag, const char* title, int polarity) {
		if (!EnableAlerts || !enabledFlag) return;
		SCString text;
		text.Format("%s | %s | %g", title, sc.Symbol.GetChars(), Close0);
		sc.AddMessageToLog(text, 1);
		if (PlaySounds)
		{
			SCString snd = (polarity > 0) ? SoundBullish : (polarity < 0 ? SoundBearish : SoundNeutral);
			if (snd.GetLength() > 0) sc.PlaySound(snd.GetChars());
		}
	};

	// ================= Bar coloring (Waddah / Linda MACD / SuperTrend) =================
	COLORREF paintBase = 0; bool havePaintBase = false;
	COLORREF paintShaved = 0; bool havePaintShaved = false;
	COLORREF paintObr = 0; bool havePaintObr = false;
	COLORREF backShade = 0; bool haveBackShade = false;

	auto ComputeBaseBarColor = [&]() {
		if (BarColor == ADM_BC_None) return;
		if (BarColor == ADM_BC_Waddah)
		{
			double magnitude = fabs(S.cT1);
			int shade;
			if (WaddahBrightnessMode == ADM_BM_AutoScale)
			{
				double full = WaddahAutoFullMultiple * S.cWadAvg;
				double fraction = full > 0 ? (std::min)(1.0, magnitude / full) : 0.0;
				shade = (int)(std::min)(255.0, WaddahBarOffset + (255 - WaddahBarOffset) * fraction);
			}
			else
			{
				shade = (int)(std::min)(255.0, magnitude * WaddahBarGain + WaddahBarOffset);
			}
			paintBase = ADM_ScaleColor(S.cT1 > 0 ? WaddahUpColor : WaddahDownColor, shade);
			havePaintBase = true;
		}
		else if (BarColor == ADM_BC_Linda)
		{
			int shade;
			if (LindaBrightnessMode == ADM_BM_AutoScale)
			{
				double magnitude = SierraExactMode ? fabs(S.cT1) : fabs(S.cLinda);
				double average = SierraExactMode ? S.cLinWadAvg : S.cLindaAvg;
				double full = LindaAutoFullMultiple * average;
				double fraction = full > 0 ? (std::min)(1.0, magnitude / full) : 0.0;
				shade = (int)(std::min)(255.0, LindaBarOffset + (255 - LindaBarOffset) * fraction);
			}
			else
			{
				shade = SierraExactMode
					? (int)(std::min)(255.0, fabs(S.cT1) + LindaBarOffset)
					: (int)(std::min)(255.0, fabs(S.cLinda) * LindaBarIntensity);
			}
			paintBase = (S.cLinda > 0) ? ADM_RGB(0, shade, 0) : ADM_RGB(shade, 0, 0);
			havePaintBase = true;
		}
		else if (BarColor == ADM_BC_Supertrend)
		{
			paintBase = (S.stMain.Dir > 0) ? ADM_RGB(0, 230, 118) : ADM_RGB(255, 82, 82);
			havePaintBase = true;
		}
	};

	// ================= Squeeze Momentum stars =================
	auto EvaluateSqueezeStars = [&]() {
		bool turnUp = S.cSqHist <= 0 && S.cSqHist > S.cSqHist1;
		bool turnDn = S.cSqHist >= 0 && S.cSqHist < S.cSqHist1;
		if (turnUp)
		{
			if (!S.sqRelaxUp)
			{
				S.sqRelaxUp = true;
				sc.Subgraph[SG_SQUEEZE][cb] = 1;
				FireAlert(AlertSqueezeUp, "Squeeze Relaxer UP", 1);
				if (ShowSqueezeStars) DrawGlyph("✦", StackY(false, SqueezeStarUpOffsetTicks, 0), StarColor, 12);
			}
		}
		else if (turnDn)
		{
			if (S.sqRelaxUp)
			{
				S.sqRelaxUp = false;
				sc.Subgraph[SG_SQUEEZE][cb] = -1;
				FireAlert(AlertSqueezeDown, "Squeeze Relaxer DOWN", -1);
				if (ShowSqueezeStars) DrawGlyph("✦", StackY(true, SqueezeStarDownOffsetTicks, 0), StarColor, 12);
			}
		}
	};

	if (SierraExactMode)
		EvaluateSqueezeStars();

	// ================= Confluence (Buy/Sell), big arrow =================
	auto EvaluateConfluence = [&]() {
		bool sierra = (FilterRules == ADM_Sierra);
		double close = Close0;

		bool wadUp = sierra ? S.cT1 > 0 : S.cT1 >= 0;
		bool wadDn = !wadUp;
		bool macdUp = sierra ? S.cLinda >= 0 : S.cLinda > 0;
		bool macdDn = S.cLinda <= 0;
		bool sarUp = sierra ? S.cSar <= close : S.cSar < Low0;
		bool sarDn = sierra ? S.cSar >= close : !(S.cSar < Low0);
		bool fishUp = sierra ? S.fisherValue[cb] >= 0 : S.cFishNt > S.cFishNt1;
		bool fishDn = sierra ? S.fisherValue[cb] <= 0 : !(S.cFishNt > S.cFishNt1);
		bool t3Up = sierra ? S.cT3 <= close : close > S.cT3;
		bool t3Dn = sierra ? S.cT3 >= close : !(close > S.cT3);
		bool aoUp = sierra ? S.cAo >= 0 : S.cAo > S.cAo1;
		bool aoDn = sierra ? S.cAo <= 0 : !(S.cAo > S.cAo1);
		bool hmaUp = sierra ? S.cHma <= close : S.cHma > S.cHma1;
		bool hmaDn = sierra ? S.cHma >= close : !(S.cHma > S.cHma1);
		bool stUp = S.stMain.Dir > 0, stDn = S.stMain.Dir < 0;
		bool sqUp = S.cSqHist > 0, sqDn = !sqUp;
		bool advUp = S.adxvmaTrend[cb] > 0.5, advDn = S.adxvmaTrend[cb] < -0.5;
		bool lizUp = S.stLiz.Dir > 0, lizDn = S.stLiz.Dir < 0;
		bool tsiUp = S.cTsi > S.cTsiSig, tsiDn = S.cTsi < S.cTsiSig;
		bool smiUp = S.cSmi > S.cSmiAvg && (!SmiRequireSlope || S.cSmi > S.cSmiPrev);
		bool smiDn = S.cSmi < S.cSmiAvg && (!SmiRequireSlope || S.cSmi < S.cSmiPrev);

		bool adxOk = S.cAdx >= EffectiveMinAdx();
		bool showUp = adxOk, showDn = adxOk;

		if (UseWaddah) { showUp &= wadUp; showDn &= wadDn; }
		if (UseLindaMacd) { showUp &= macdUp; showDn &= macdDn; }
		if (UsePsar) { showUp &= sarUp; showDn &= sarDn; }
		if (UseFisher) { showUp &= fishUp; showDn &= fishDn; }
		if (UseT3) { showUp &= t3Up; showDn &= t3Dn; }
		if (UseAO) { showUp &= aoUp; showDn &= aoDn; }
		if (UseHma) { showUp &= hmaUp; showDn &= hmaDn; }
		if (UseSupertrend) { showUp &= stUp; showDn &= stDn; }
		if (UseSqueezeMomentum) { showUp &= sqUp; showDn &= sqDn; }
		if (UseAdxvma) { showUp &= advUp; showDn &= advDn; }
		if (UseLizardSuperTrend) { showUp &= lizUp; showDn &= lizDn; }
		if (UseMultiTsi) { showUp &= tsiUp; showDn &= tsiDn; }
		if (UseSmi) { showUp &= smiUp; showDn &= smiDn; }

		if (showUp && showDn && !SierraExactMode) { showUp = false; showDn = false; }

		if (RequireCandleDirection)
		{
			showUp = showUp && S.cGreen;
			showDn = showDn && S.cRed;
		}

		if (showUp)
		{
			sc.Subgraph[SG_CONFLUENCE][cb] = showDn ? 0 : 1;
			if (ShowBuySell) DrawGlyph("▲", Low0 - BuyTriangleOffsetTicks * tick, BuyColorC, 11);
			FireAlert(AlertBuy, "Standard BUY", 1);
		}
		if (showDn)
		{
			sc.Subgraph[SG_CONFLUENCE][cb] = showUp ? 0 : -1;
			if (ShowBuySell) DrawGlyph("▼", High0 + SellTriangleOffsetTicks * tick, SellColorC, 11);
			FireAlert(AlertSell, "Standard SELL", -1);
		}

		if (ShowBigArrow)
		{
			double bbWidth = S.bbU - S.bbL;
			bool sarBelow = S.cSar < Low0;
			if (S.cT1 > bbWidth && sarBelow && !S.bigArrowUp)
			{
				DrawGlyph("⬆", StackY(false, BigArrowUpOffsetTicks, 0), BuyColorC, 16);
				S.bigArrowUp = true;
				sc.Subgraph[SG_BIGARROW][cb] = 1;
				FireAlert(AlertBigBuy, "MACD/PSAR BUY", 1);
			}
			else if (S.cT1 < 0 && fabs(S.cT1) > bbWidth && !sarBelow && S.bigArrowUp)
			{
				DrawGlyph("⬇", StackY(true, BigArrowDownOffsetTicks, 0), SellColorC, 16);
				S.bigArrowUp = false;
				sc.Subgraph[SG_BIGARROW][cb] = -1;
				FireAlert(AlertBigSell, "MACD/PSAR SELL", -1);
			}
		}

		if (!SierraExactMode)
			EvaluateSqueezeStars();
	};

	// ================= Candle patterns =================
	auto IsBullishEngulfing = [&](int k) {
		return sc.Close[cb-k-1] < sc.Open[cb-k-1] && sc.Close[cb-k] > sc.Open[cb-k]
			&& sc.High[cb-k] > sc.High[cb-k-1] && sc.Low[cb-k] < sc.Low[cb-k-1]
			&& sc.Close[cb-k] > sc.Open[cb-k-1] && sc.Open[cb-k] < sc.Close[cb-k-1];
	};
	auto IsBearishEngulfing = [&](int k) {
		return sc.Close[cb-k-1] > sc.Open[cb-k-1] && sc.Close[cb-k] < sc.Open[cb-k]
			&& sc.High[cb-k] > sc.High[cb-k-1] && sc.Low[cb-k] < sc.Low[cb-k-1]
			&& sc.Open[cb-k] > sc.Close[cb-k-1] && sc.Close[cb-k] < sc.Open[cb-k-1];
	};
	auto IsGreenBar = [&](int barsAgo) { return sc.Close[cb-barsAgo] > sc.Open[cb-barsAgo]; };
	auto IsRedBar = [&](int barsAgo) { return sc.Close[cb-barsAgo] < sc.Open[cb-barsAgo]; };

	auto PatternLabel = [&](const char* key, const char* text, int barsAgo, bool above, bool bullishBack, int code, bool plain) {
		COLORREF back = plain ? ADM_RGB(0,0,0) : (bullishBack ? PatternBullBackColor : PatternBearBackColor);
		if (plain) { /* transparent-ish: use a very dark neutral so text stays legible without a loud box */ back = ADM_RGB(20,20,20); }
		DrawLabel(text, barsAgo, above, PatternTextColor, back, PatternLabelOffsetTicks);
		if (code != 0) sc.Subgraph[SG_PATTERN][cb] = code;
		if (strcmp(key,"3o")==0) FireAlert(AlertThreeOutside, text, code > 0 ? 1 : -1);
		else if (strcmp(key,"Eq")==0) FireAlert(AlertTweezer, text, code > 0 ? 1 : -1);
		else if (strcmp(key,"Wick")==0) FireAlert(AlertWickPat, "Wick pattern", code > 0 ? 1 : -1);
		else if (strcmp(key,"Stairs")==0) FireAlert(AlertStairs, "Stairs", 0);
	};

	auto TrampolineLabel = [&](int barsAgo, bool above, int code, bool sierraStyle) {
		DrawLabel("TR", barsAgo, above, TrampTextColor, sierraStyle ? ADM_RGB(255,82,82) : TrampBackColor, TrampolineOffsetTicks);
		sc.Subgraph[SG_PATTERN][cb] = code;
		FireAlert(AlertTramp, "Trampoline", code > 0 ? 1 : -1);
	};

	auto SierraTrampolineAt = [&](double band, double tol) -> int {
		bool bear = S.cRed && IsRedBar(1) && IsGreenBar(2) && Close0 < sc.Close[cb-1]
			&& (S.cRsi0 > TrampolineRsiHigh || S.cRsi1 > TrampolineRsiHigh || S.cRsi2 > TrampolineRsiHigh)
			&& sc.High[cb-2] >= band - tol;
		bool bull = S.cGreen && IsGreenBar(1) && IsRedBar(2) && Close0 > sc.Close[cb-1]
			&& (S.cRsi0 < TrampolineRsiLow || S.cRsi1 < TrampolineRsiLow || S.cRsi2 < TrampolineRsiLow)
			&& sc.Low[cb-2] <= band + tol;
		return bear ? -1 : (bull ? 1 : 0);
	};

	auto ShiftHHMMSS = [&](int hhmmss, int hours) -> int {
		int hh = hhmmss / 10000, mm = (hhmmss / 100) % 100, ss = hhmmss % 100;
		long total = (long)hh * 3600 + mm * 60 + ss - (long)hours * 3600;
		total = ((total % 86400) + 86400) % 86400;
		return (int)((total / 3600) * 10000 + ((total / 60) % 60) * 100 + (total % 60));
	};

	auto EvilTimesText = [&]() -> std::string {
		int curr = ShiftHHMMSS(GetHHMMSS(cb), EvilTimesChartOffsetHours);
		if (curr >= 90000 && curr <= 100000) return "Market Pivot (9-10am CST)";
		if (curr >= 100000 && curr <= 103000) return "Euro Move (10-10:30am CST)";
		if (curr >= 103000 && curr <= 110000) return "Inverse (10:30-11am CST)";
		if (curr >= 110000 && curr <= 120000) return "Inverse (11am-12pm CST)";
		if (curr >= 120000 && curr <= 133000) return "Bond Auctions (12-1:30pm CST)";
		if (curr >= 133000 && curr <= 144500) return "Capital Injection (1:30-2:45pm CST)";
		if (curr >= 144500 && curr <= 150000) return "Rug Pull (2:45-3pm CST)";
		return "";
	};

	auto EvaluatePatterns = [&]() {
		if (ShowShavedCandles)
		{
			double tol = ShavedToleranceTicks * tick;
			if (S.cRed && Close0 <= Low0 + tol) { paintShaved = ShavedRedColor; havePaintShaved = true; }
			else if (S.cGreen && Close0 >= High0 - tol) { paintShaved = ShavedGreenColor; havePaintShaved = true; }
		}

		if (ShowBbEngulfShading)
		{
			if (Low0 < S.bbL && sc.Low[cb-1] < S.bbL && S.cGreen && S.cBody > S.cPBody) { backShade = BbEngulfGreenColor; haveBackShade = true; }
			else if (High0 > S.bbU && sc.High[cb-1] > S.bbU && S.cRed && S.cBody > S.cPBody) { backShade = BbEngulfRedColor; haveBackShade = true; }
		}

		if (ShowThreeOutside)
		{
			if (S.cGreen && Close0 > sc.Close[cb-1] && IsBullishEngulfing(1)) PatternLabel("3o","3oU",0,false,true,1,false);
			if (S.cRed && Close0 < sc.Close[cb-1] && IsBearishEngulfing(1)) PatternLabel("3o","3oD",0,true,false,-1,false);
		}

		if (ShowTweezers && cb >= 4)
		{
			bool nearEqual = fabs(sc.Open[cb-1] - sc.Close[cb-2]) < TweezerToleranceTicks * tick;
			bool tweezerTop = nearEqual && Low0 < sc.Low[cb-1] && S.cRed && IsRedBar(1) && IsGreenBar(2) && IsGreenBar(3)
				&& (sc.High[cb-1] > S.bbU || sc.High[cb-2] > S.bbU);
			bool tweezerBottom = nearEqual && High0 > sc.High[cb-1] && S.cGreen && IsGreenBar(1) && IsRedBar(2) && IsRedBar(3)
				&& (sc.Low[cb-1] < S.bbL || sc.Low[cb-2] < S.bbL);
			if (tweezerTop) PatternLabel("Eq","Eq Hi",1,true,false,-2,false);
			if (tweezerBottom) PatternLabel("Eq","Eq Lo",1,false,true,2,false);
		}

		if (ShowTrampoline)
		{
			if (SierraExactMode)
			{
				double tol = trunc(tick);
				int atUpper = SierraTrampolineAt(S.bbU, tol);
				if (atUpper != 0) { TrampolineLabel(1, false, atUpper * 3, true); }
				else { int atLower = SierraTrampolineAt(S.bbL, tol); if (atLower != 0) TrampolineLabel(1, true, atLower * 3, true); }
			}
			else
			{
				double tol = TrampolineToleranceTicks * tick;
				bool bearTramp = S.cRed && IsRedBar(1) && IsGreenBar(2) && Close0 < sc.Close[cb-1]
					&& (S.cRsi0 > TrampolineRsiHigh || S.cRsi1 > TrampolineRsiHigh || S.cRsi2 > TrampolineRsiHigh)
					&& sc.High[cb-2] >= S.bbU - tol;
				bool bullTramp = S.cGreen && IsGreenBar(1) && IsRedBar(2) && Close0 > sc.Close[cb-1]
					&& (S.cRsi0 < TrampolineRsiLow || S.cRsi1 < TrampolineRsiLow || S.cRsi2 < TrampolineRsiLow)
					&& sc.Low[cb-2] <= S.bbL + tol;
				if (bearTramp) TrampolineLabel(1, true, -3, false);
				else if (bullTramp) TrampolineLabel(1, false, 3, false);
			}
		}

		if (ShowWickPattern)
		{
			bool upWickLarger = S.cRed && fabs(High0 - Open0) > fabs(Low0 - Close0);
			bool downWickLarger = S.cGreen && fabs(Low0 - Open0) > fabs(Close0 - High0);
			if (S.cRed && High0 > S.bbU && Open0 < S.bbU && Open0 > sc.Close[cb-1] && upWickLarger) PatternLabel("Wick","Wick",0,true,false,-4,false);
			if (S.cGreen && Low0 < S.bbL && Open0 > S.bbL && Open0 > sc.Close[cb-1] && downWickLarger) PatternLabel("Wick","Wick",0,false,true,4,false);
		}

		if (ShowStairs && cb >= 5)
		{
			double b0 = S.cBody, b1 = fabs(sc.Close[cb-1]-sc.Open[cb-1]), b2 = fabs(sc.Close[cb-2]-sc.Open[cb-2]);
			double b3 = fabs(sc.Close[cb-3]-sc.Open[cb-3]), b4 = fabs(sc.Close[cb-4]-sc.Open[cb-4]);
			if (b4 > b3 && b3 > b2 && b2 > b1 && b1 > b0)
			{
				bool rising = Close0 > sc.Close[cb-1] && sc.Close[cb-1] > sc.Close[cb-2] && sc.Close[cb-2] > sc.Close[cb-3];
				bool falling = Close0 < sc.Close[cb-1] && sc.Close[cb-1] < sc.Close[cb-2] && sc.Close[cb-2] < sc.Close[cb-3];
				if (rising || falling) PatternLabel("Stairs","Stairs",0,S.cGreen,false,0,true);
			}
		}

		if (ShowReversalSquare)
		{
			double range0 = High0-Low0, range1 = sc.High[cb-1]-sc.Low[cb-1];
			if (range0 > 0 && range1 > 0)
			{
				double upTrades = Volume0*(Close0-Low0)/range0, dnTrades = Volume0*(High0-Close0)/range0;
				double pupTrades = sc.Volume[cb-1]*(sc.Close[cb-1]-sc.Low[cb-1])/range1;
				double pdnTrades = sc.Volume[cb-1]*(sc.High[cb-1]-sc.Close[cb-1])/range1;
				if (upTrades > pdnTrades && upTrades > pupTrades && upTrades > dnTrades && Low0 < S.bbL)
				{
					DrawGlyph("■", StackY(false, ReversalSquareOffsetTicks, 0), BuyColorC, 11);
					FireAlert(AlertRevSquare, "Reversal Square (bullish)", 1);
				}
				if (dnTrades > pupTrades && dnTrades > pdnTrades && dnTrades > upTrades && High0 > S.bbU)
				{
					DrawGlyph("■", StackY(true, ReversalSquareOffsetTicks, 0), SellColorC, 11);
					FireAlert(AlertRevSquare, "Reversal Square (bearish)", -1);
				}
			}
		}

		bool kamaBearBounce = High0 > S.cKama && Open0 < S.cKama && Close0 < S.cKama;
		bool kamaBullBounce = Low0 < S.cKama && Open0 > S.cKama && Close0 > S.cKama;
		if (kamaBearBounce || kamaBullBounce)
		{
			if (ShowKamaBounceMarkers) PatternLabel("KB","KB",0,kamaBearBounce,false,0,true);
			FireAlert(AlertKamaBounce, kamaBearBounce ? "KAMA Bounce (bearish)" : "KAMA Bounce (bullish)", kamaBearBounce ? -1 : 1);
		}

		if (ShowEvilTimes)
		{
			std::string evil = EvilTimesText();
			if (!evil.empty() && evil != S.prevEvil)
			{
				DrawLabel(evil.c_str(), 0, true, EvilTimesColor, ADM_RGB(20,20,20), EvilTimesOffsetTicks);
				S.prevEvil = evil;
			}
		}
	};

	if (!(IgnoreDojis && S.cDoji))
	{
		ComputeBaseBarColor();
		EvaluateConfluence();
		EvaluatePatterns();
	}

	// ================= Vodka Shot =================
	auto DrawVodka = [&](bool buy) {
		double y = StackY(!buy, buy ? VodkaUpOffsetTicks : VodkaDownOffsetTicks, 0);
		DrawGlyph("✚", y, buy ? VodkaUpColor : VodkaDownColor, VodkaGlyphSize);
	};
	if (EnableVodka && cb >= VodkaVolumeLength)
	{
		if (S.vodkaBuyRaw && (!VodkaRequireWave || S.wavePrev == 1))
		{
			sc.Subgraph[SG_VODKA][cb] = 1;
			DrawVodka(true);
			FireAlert(AlertVodkaBuy, "Vodka Shot BUY", 1);
		}
		else if (S.vodkaSellRaw && (!VodkaRequireWave || S.wavePrev == -1))
		{
			sc.Subgraph[SG_VODKA][cb] = -1;
			DrawVodka(false);
			FireAlert(AlertVodkaSell, "Vodka Shot SELL", -1);
		}
	}

	// ================= Gap engine: Volume Imbalance / FC Continuation / Bollinger Gap / Spiderweb =================
	auto IsSignalTimeAllowed = [&]() -> bool {
		if (!UseSignalTimeFilter) return true;
		int t = GetHHMMSS(cb);
		if (SignalStartTime <= SignalEndTime) return t >= SignalStartTime && t <= SignalEndTime;
		return t >= SignalStartTime || t <= SignalEndTime;
	};

	auto SetSuggestedPrices = [&](int direction) {
		sc.Subgraph[SG_ENTRY][cb] = Close0;
		if (direction > 0)
		{
			sc.Subgraph[SG_STOP][cb] = Low0 - SuggestedStopOffsetTicks * tick;
			if (TargetMode == ADM_TM_OppositeBand && cb >= BbPeriod) sc.Subgraph[SG_TARGET][cb] = S.bbU;
			else if (TargetMode == ADM_TM_FixedTicks) sc.Subgraph[SG_TARGET][cb] = Close0 + FixedTargetTicks * tick;
			else sc.Subgraph[SG_TARGET][cb] = 0;
		}
		else
		{
			sc.Subgraph[SG_STOP][cb] = High0 + SuggestedStopOffsetTicks * tick;
			if (TargetMode == ADM_TM_OppositeBand && cb >= BbPeriod) sc.Subgraph[SG_TARGET][cb] = S.bbL;
			else if (TargetMode == ADM_TM_FixedTicks) sc.Subgraph[SG_TARGET][cb] = Close0 - FixedTargetTicks * tick;
			else sc.Subgraph[SG_TARGET][cb] = 0;
		}
	};

	auto GetBollingerTestPrice = [&](int direction) -> double {
		if (FcLocationSource == ADM_BL_Close) return Close0;
		if (FcLocationSource == ADM_BL_HLC3) return (High0 + Low0 + Close0) / 3.0;
		if (FcLocationSource == ADM_BL_BodyMid) return (Open0 + Close0) / 2.0;
		return direction > 0 ? Low0 : High0;
	};
	auto PassesBollingerMidpointFilter = [&](int direction, bool enabled) -> bool {
		if (!enabled) return true;
		if (cb < BbPeriod) return false;
		double testPrice = GetBollingerTestPrice(direction);
		if (direction > 0) { double threshold = S.bbM - ((S.bbM - S.bbL) * (FcLongBelowMidPct / 100.0)); return testPrice <= threshold; }
		double thresholdShort = S.bbM + ((S.bbU - S.bbM) * (FcShortAboveMidPct / 100.0));
		return testPrice >= thresholdShort;
	};

	auto SetContinuationSignal = [&](int direction) {
		sc.Subgraph[SG_CONT][cb] = direction;
		sc.Subgraph[SG_DIR][cb] = direction;
		sc.Subgraph[SG_CODE][cb] = 2 * direction;
		if (direction > 0) sc.Subgraph[SG_MLONG][cb] = 1; else sc.Subgraph[SG_MSHORT][cb] = -1;
		SetSuggestedPrices(direction);
		if (ShowContinuationMarkers)
		{
			bool fcAbove = direction < 0;
			double fcLabelY;
			double fcMarkerY = StackPairY(fcAbove, FcMarkerOffsetTicks, SignalLabelOffsetTicks, ShowSignalLabels, fcLabelY);
			COLORREF col = direction > 0 ? FcLongColor : FcShortColor;
			DrawGlyph(direction > 0 ? "▲" : "▼", fcMarkerY, col, 13);
			if (ShowSignalLabels) DrawTextAt("FC", 0, fcLabelY, col, ADM_RGB(15,15,15));
		}
		FireAlert(direction > 0 ? AlertFcLong : AlertFcShort, direction > 0 ? "FC LONG" : "FC SHORT", direction);
	};

	auto DrawBollingerMarker = [&](int direction) {
		double bgLabelY;
		double bgMarkerY = StackPairY(direction < 0, BgMarkerOffsetTicks, SignalLabelOffsetTicks, ShowSignalLabels, bgLabelY);
		COLORREF col = direction > 0 ? BgLongColor : BgShortColor;
		DrawGlyph(direction > 0 ? "▲" : "▼", bgMarkerY, col, 13);
		if (ShowSignalLabels) DrawTextAt("BG", 0, bgLabelY, col, ADM_RGB(15,15,15));
	};

	auto MarkBollingerGapIfNeeded = [&](int direction) -> bool {
		if (!EnableBollingerGap) return false;
		if (WaveFilterBg && !(direction > 0 ? S.wavePrev == 1 : S.wavePrev == -1)) return false;
		if (!IsSignalTimeAllowed()) return false;
		if (cb < BbPeriod + 1) return false;

		double proximity = Adapt(BgProximityTicks, AdaptBandDistances) * tick;
		bool pierce = SierraExactMode || (BgLocationMode == ADM_BG_SierraPierce);

		if (direction > 0)
		{
			bool nearLower = pierce ? (sc.Low[cb-1] < S.bbL || Low0 < S.bbL) : (sc.Low[cb-1] <= S.bbL1 + proximity && Low0 <= S.bbL + proximity);
			if (!nearLower) return false;
			sc.Subgraph[SG_BGAP][cb] = 1; sc.Subgraph[SG_MLONG][cb] = 1; sc.Subgraph[SG_DIR][cb] = 1; sc.Subgraph[SG_CODE][cb] = 1;
			SetSuggestedPrices(1);
			if (ShowBgMarkers) DrawBollingerMarker(1);
			FireAlert(AlertBgLong, "BG LONG", 1);
			return true;
		}
		bool nearUpper = pierce ? (sc.High[cb-1] > S.bbU || High0 > S.bbU) : (sc.High[cb-1] >= S.bbU1 - proximity && High0 >= S.bbU - proximity);
		if (!nearUpper) return false;
		sc.Subgraph[SG_BGAP][cb] = -1; sc.Subgraph[SG_MSHORT][cb] = -1; sc.Subgraph[SG_DIR][cb] = -1; sc.Subgraph[SG_CODE][cb] = -1;
		SetSuggestedPrices(-1);
		if (ShowBgMarkers) DrawBollingerMarker(-1);
		FireAlert(AlertBgShort, "BG SHORT", -1);
		return true;
	};

	auto GetLinePrice = [&](const GapInfo& gap) -> double {
		if (LinePriceMode == ADM_LP_PrevCloseEdge) return gap.PreviousClose;
		if (LinePriceMode == ADM_LP_CurrentOpenEdge) return gap.CurrentOpen;
		return (gap.ZoneLow + gap.ZoneHigh) * 0.5;
	};

	auto GetGapWidth = [&](const GapInfo& gap) -> int {
		if (!GapWidthBySize) return GapLineWidth;
		double sizeTicks = (gap.ZoneHigh - gap.ZoneLow) / tick;
		double lo = MinGapSizeTicks, hi = (std::max)(lo + 1, (double)GapMaxWidthAtTicks);
		double t = (std::min)(1.0, (std::max)(0.0, (sizeTicks - lo) / (hi - lo)));
		int minW = (std::min)(GapMinWidth, GapMaxWidth), maxW = (std::max)(GapMinWidth, GapMaxWidth);
		return (std::max)(1, (int)(minW + t * (maxW - minW) + 0.5));
	};

	auto GetLineColor = [&](const GapInfo& gap, bool touched, bool invalid) -> COLORREF {
		if (invalid) return GapInvalidColor;
		if (touched && UseTouchedLineColor) return GapTouchedColor;
		return gap.Direction > 0 ? GapBullLineColor : GapBearLineColor;
	};

	auto DrawGap = [&](GapInfo& gap, bool touched, bool invalid, bool finalize) {
		bool projected = (GapLineExtension == ADM_GLE_Projected);
		if (projected && !finalize && gap.Drawn) return;

		int endBar = (projected && !finalize) ? (std::min)(cb + GapProjectionBars, sc.ArraySize - 1) : cb;

		if (ShowGapLine)
			ADM_DrawLine(sc, gap.LineId, gap.CreationBar, gap.LinePrice, endBar, gap.LinePrice,
				GetLineColor(gap, touched, invalid), LINESTYLE_SOLID, GetGapWidth(gap), projected && !finalize);
		else
			ADM_DeleteDrawing(sc, gap.LineId);

		if (ShowGapZone)
			ADM_DrawRect(sc, gap.ZoneId, gap.CreationBar, gap.ZoneHigh, endBar, gap.ZoneLow,
				invalid ? GapInvalidColor : (gap.Direction > 0 ? GapBullZoneColor : GapBearZoneColor), 100 - GapZoneOpacity);
		else
			ADM_DeleteDrawing(sc, gap.ZoneId);

		gap.Drawn = true;
	};

	auto PassesBodyFilter = [&]() -> bool {
		if (MinBodyTicks <= 0) return true;
		double prevBodyTicks = fabs(sc.Close[cb-1] - sc.Open[cb-1]) / tick;
		double curBodyTicks = fabs(Close0 - Open0) / tick;
		double minBody = Adapt(MinBodyTicks, AdaptGapFilters);
		return prevBodyTicks >= minBody && curBodyTicks >= minBody;
	};
	auto PassesGapBarRangeFilter = [&]() -> bool {
		if (MaxGapBarRangeTicks <= 0) return true;
		return (High0 - Low0) / tick <= Adapt(MaxGapBarRangeTicks, AdaptGapFilters);
	};

	auto CreateGap = [&](int direction, double zoneLow, double zoneHigh) -> int {
		GapInfo gap;
		gap.Direction = direction;
		gap.CreationBar = cb;
		gap.PreviousClose = sc.Close[cb-1];
		gap.CurrentOpen = Open0;
		gap.ZoneLow = (std::min)(zoneLow, zoneHigh);
		gap.ZoneHigh = (std::max)(zoneLow, zoneHigh);
		gap.LinePrice = GetLinePrice(gap);
		gap.IsValid = false; gap.Touched = false; gap.InvalidTooEarly = false; gap.Drawn = false;
		gap.LineId = NextLineId(); gap.ZoneId = NextLineId();
		S.gapHistory.push_back(gap);
		int histIdx = (int)S.gapHistory.size() - 1;
		S.activeGapIdx.push_back(histIdx);
		DrawGap(S.gapHistory[histIdx], false, false, false);
		return histIdx;
	};

	auto DistanceFromPriceToZone = [&](double price, const GapInfo& gap) -> double {
		if (price >= gap.ZoneLow && price <= gap.ZoneHigh) return 0;
		if (price < gap.ZoneLow) return gap.ZoneLow - price;
		return price - gap.ZoneHigh;
	};

	auto PassesContinuationApproach = [&](const GapInfo& gap) -> bool {
		if (!RequireCorrectApproach) return true;
		if (gap.Direction > 0) return sc.Close[cb-1] >= gap.ZoneHigh || Open0 >= gap.ZoneHigh;
		return sc.Close[cb-1] <= gap.ZoneLow || Open0 <= gap.ZoneLow;
	};

	auto IsLongContinuation = [&](const GapInfo& gap) -> bool {
		if (RequireSignalCandleDirection && Close0 <= Open0) return false;
		if (!PassesBollingerMidpointFilter(1, UseBbMidFilterFc)) return false;
		if (ConfirmMode == ADM_CM_TouchOnly) return true;
		if (ConfirmMode == ADM_CM_CloseBeyondLine) return Close0 >= gap.LinePrice;
		return Close0 >= gap.ZoneHigh;
	};
	auto IsShortContinuation = [&](const GapInfo& gap) -> bool {
		if (RequireSignalCandleDirection && Close0 >= Open0) return false;
		if (!PassesBollingerMidpointFilter(-1, UseBbMidFilterFc)) return false;
		if (ConfirmMode == ADM_CM_TouchOnly) return true;
		if (ConfirmMode == ADM_CM_CloseBeyondLine) return Close0 <= gap.LinePrice;
		return Close0 <= gap.ZoneLow;
	};

	auto EvaluatePendingContinuation = [&](PendingContinuation& pc) -> bool {
		if (pc.GapIndexInHistory < 0 || pc.GapIndexInHistory >= (int)S.gapHistory.size()) return false;
		GapInfo& gap = S.gapHistory[pc.GapIndexInHistory];
		if (!IsSignalTimeAllowed()) return false;
		if (WaveFilterFc && !(gap.Direction > 0 ? S.wavePrev == 1 : S.wavePrev == -1)) return false;
		if (gap.Direction > 0) { if (IsLongContinuation(gap)) { SetContinuationSignal(1); return true; } }
		else { if (IsShortContinuation(gap)) { SetContinuationSignal(-1); return true; } }
		return false;
	};

	auto CreatePendingContinuationAndEvaluate = [&](int gapIdx) {
		if (!EnableContinuation) return;
		if (ValidTouch != ADM_VT_StopAndContinuation) return;
		if (!IsSignalTimeAllowed()) return;
		GapInfo& gap = S.gapHistory[gapIdx];
		if (!PassesContinuationApproach(gap)) return;

		PendingContinuation pc; pc.GapIndexInHistory = gapIdx; pc.TouchBar = cb;
		bool signaled = EvaluatePendingContinuation(pc);
		if (!signaled && ConfirmBarsAfterTouch > 0)
			S.pendingContinuations.push_back(pc);
	};

	auto DetectNewGap = [&]() {
		bool previousBullish = sc.Close[cb-1] > sc.Open[cb-1];
		bool currentBullish = Close0 > Open0;
		bool previousBearish = sc.Close[cb-1] < sc.Open[cb-1];
		bool currentBearish = Close0 < Open0;

		if (!PassesBodyFilter()) return;
		if (!PassesGapBarRangeFilter()) return;

		bool bullishGap = previousBullish && currentBullish && Open0 > sc.Close[cb-1]
			&& ((Open0 - sc.Close[cb-1]) / tick) >= Adapt(MinGapSizeTicks, AdaptGapFilters);
		bool bearishGap = previousBearish && currentBearish && Open0 < sc.Close[cb-1]
			&& ((sc.Close[cb-1] - Open0) / tick) >= Adapt(MinGapSizeTicks, AdaptGapFilters);

		if (bullishGap)
		{
			int idx = CreateGap(1, sc.Close[cb-1], Open0);
			sc.Subgraph[SG_VOLIMB][cb] = 1;
			bool bg = MarkBollingerGapIfNeeded(1);
			if ((!bg || SierraExactMode) && ShowVolumeImbalanceArrows)
				DrawGlyph("△", StackY(false, VolImbUpOffsetTicks, 0), VolImbColor, 12);
			if (!bg && !SierraExactMode) FireAlert(AlertVolImbBuy, "Volume Imbalance BUY", 1);
			(void)idx;
		}
		else if (bearishGap)
		{
			int idx = CreateGap(-1, Open0, sc.Close[cb-1]);
			sc.Subgraph[SG_VOLIMB][cb] = -1;
			bool bg = MarkBollingerGapIfNeeded(-1);
			if ((!bg || SierraExactMode) && ShowVolumeImbalanceArrows)
				DrawGlyph("▽", StackY(true, VolImbDownOffsetTicks, 0), VolImbColor, 12);
			if (!bg && !SierraExactMode) FireAlert(AlertVolImbSell, "Volume Imbalance SELL", -1);
			(void)idx;
		}
	};

	auto UpdateActiveGaps = [&]() {
		for (int i = (int)S.activeGapIdx.size() - 1; i >= 0; i--)
		{
			GapInfo& gap = S.gapHistory[S.activeGapIdx[i]];
			int age = cb - gap.CreationBar;
			gap.IsValid = age >= MinBarsBeforeValid;
			bool shouldCheckTouch = cb > gap.CreationBar;
			if (EarlyTouch == ADM_ET_IgnoreUntilValid && !gap.IsValid) shouldCheckTouch = false;
			bool touched = shouldCheckTouch && High0 >= gap.ZoneLow && Low0 <= gap.ZoneHigh;

			if (touched)
			{
				gap.Touched = true;
				gap.InvalidTooEarly = !gap.IsValid;
				sc.Subgraph[SG_GAPTOUCH][cb] = gap.Direction;
				if (gap.InvalidTooEarly) { sc.Subgraph[SG_INVALIDTOUCH][cb] = gap.Direction; DrawGap(gap, true, true, true); }
				else DrawGap(gap, true, false, true);

				if (ShowTouchMarker) ADM_DrawText(sc, NextLineId(), cb, gap.LinePrice, "●", TouchMarkerColor, 8, false, true);

				FireAlert(gap.Direction > 0 ? AlertGapTouchBull : AlertGapTouchBear,
					gap.Direction > 0 ? "Bullish gap touched" : "Bearish gap touched", gap.Direction);

				if (gap.IsValid) CreatePendingContinuationAndEvaluate(S.activeGapIdx[i]);

				S.activeGapIdx.erase(S.activeGapIdx.begin() + i);
			}
			else
			{
				DrawGap(gap, false, false, false);
			}
		}
	};

	auto UpdatePendingContinuations = [&]() {
		for (int i = (int)S.pendingContinuations.size() - 1; i >= 0; i--)
		{
			PendingContinuation& pc = S.pendingContinuations[i];
			int barsSinceTouch = cb - pc.TouchBar;
			if (barsSinceTouch <= 0) continue;
			if (barsSinceTouch > ConfirmBarsAfterTouch) { S.pendingContinuations.erase(S.pendingContinuations.begin() + i); continue; }
			if (EvaluatePendingContinuation(pc)) S.pendingContinuations.erase(S.pendingContinuations.begin() + i);
		}
	};

	auto TrimOldestActiveGaps = [&]() {
		while ((int)S.activeGapIdx.size() > MaxActiveGaps)
		{
			GapInfo& oldest = S.gapHistory[S.activeGapIdx[0]];
			DrawGap(oldest, false, false, true);
			S.activeGapIdx.erase(S.activeGapIdx.begin());
		}
	};

	auto UpdateContextPlots = [&]() {
		int activeCount = (int)S.activeGapIdx.size();
		int pendingCount = (int)S.pendingContinuations.size();
		sc.Subgraph[SG_ACTIVEGAPS][cb] = activeCount;
		sc.Subgraph[SG_PENDING][cb] = pendingCount;

		if (activeCount == 0)
		{
			S.spiderWarnPrev = false;
			ADM_DeleteDrawing(sc, 1); // spiderweb HUD line id
			return;
		}

		double nearestDistance = 1e300, nearestPrice = 0;
		int validActiveCount = 0, spiderwebCount = 0;
		double webDistance = Adapt(SpiderwebDistanceTicks, AdaptSpiderweb);

		for (int idx : S.activeGapIdx)
		{
			GapInfo& gap = S.gapHistory[idx];
			int age = cb - gap.CreationBar;
			bool valid = age >= MinBarsBeforeValid;
			if (valid) validActiveCount++;
			double distance = DistanceFromPriceToZone(Close0, gap);
			double distanceTicks = distance / tick;
			if (distance < nearestDistance) { nearestDistance = distance; nearestPrice = gap.LinePrice; }
			if (valid && distanceTicks <= webDistance) spiderwebCount++;
		}

		sc.Subgraph[SG_VALIDGAPS][cb] = validActiveCount;
		sc.Subgraph[SG_WEBCOUNT][cb] = spiderwebCount;
		sc.Subgraph[SG_NEARGAP][cb] = nearestPrice;

		bool warning = EnableSpiderweb && spiderwebCount >= SpiderwebLineCount;
		sc.Subgraph[SG_WEBWARN][cb] = warning ? 1 : 0;
		if (warning && !S.spiderWarnPrev)
		{
			SCString detail; detail.Format("%d gaps", spiderwebCount);
			FireAlert(AlertSpiderweb, "SPIDERWEB WARNING", 0);
		}
		S.spiderWarnPrev = warning;

		if (warning && ShowSpiderwebText)
		{
			SCString msg; msg.Format("SPIDERWEB WARNING - %d valid gaps within %d ticks", spiderwebCount, (int)(webDistance + 0.5));
			double y = (std::max)(High0, S.bbU) + (std::max)(20.0 * tick, (High0 - Low0) * 2.0);
			ADM_DrawText(sc, 1, cb, y, msg, SpiderwebColor, SpiderwebFontSize, true, false);
		}
		else
		{
			ADM_DeleteDrawing(sc, 1);
		}
	};

	// ================= Outside Bar Reversal (OBR) =================
	auto DrawObrVisuals = [&](int direction) {
		bool longSide = direction > 0;
		COLORREF col = longSide ? ObrBullColor : ObrBearColor;
		if (ObrMarkerStyle == ADM_OM_Diamond)
		{
			double obrLabelY;
			double obrMarkerY = StackPairY(!longSide, ObrMarkerOffsetTicks, SignalLabelOffsetTicks, ShowSignalLabels, obrLabelY);
			DrawGlyph("◆", obrMarkerY, col, 12);
			if (ShowSignalLabels) DrawTextAt("OBR", 0, obrLabelY, col, ADM_RGB(15,15,15));
		}
		else if (ObrMarkerStyle == ADM_OM_Arrow)
		{
			DrawGlyph(longSide ? "▲" : "▼", StackY(!longSide, ObrArrowOffsetTicks, 0), col, 13);
		}
		if (ObrShowEntryLine)
		{
			double y = longSide ? High0 + tick : Low0 - tick;
			ADM_DrawLine(sc, NextLineId(), (std::max)(0, cb - ObrEntryLineBars), y, cb, y, col, LINESTYLE_SOLID, 3, false);
		}
		if (ObrShowZones)
		{
			double range = High0 - Low0;
			int zoneStart = (std::max)(0, cb - ObrZoneBars);
			ADM_DrawRect(sc, NextLineId(), cb, High0, zoneStart, Low0, ObrLightZoneColor, 100 - ObrZoneOpacity);
			if (longSide) ADM_DrawRect(sc, NextLineId(), cb, Low0, zoneStart, Low0 - range, ObrShadowZoneColor, 100 - ObrZoneOpacity);
			else ADM_DrawRect(sc, NextLineId(), cb, High0, zoneStart, High0 + range, ObrShadowZoneColor, 100 - ObrZoneOpacity);
		}
	};

	auto DetectOutsideBarReversal = [&]() {
		if (!EnableObr) return;
		if (!IsSignalTimeAllowed()) return;
		if (cb < BbPeriod) return;

		bool previousBullish = sc.Close[cb-1] > sc.Open[cb-1];
		bool previousBearish = sc.Close[cb-1] < sc.Open[cb-1];
		bool currentBullish = Close0 > Open0;
		bool currentBearish = Close0 < Open0;

		bool bearishBodyEngulf = previousBullish && currentBearish && Open0 >= sc.Close[cb-1] && Close0 <= sc.Open[cb-1];
		bool bullishBodyEngulf = previousBearish && currentBullish && Open0 <= sc.Close[cb-1] && Close0 >= sc.Open[cb-1];

		if (ObrRequireLargerBody)
		{
			bearishBodyEngulf = bearishBodyEngulf && S.cBody > S.cPBody;
			bullishBodyEngulf = bullishBodyEngulf && S.cBody > S.cPBody;
		}

		if (ObrMinBarSizeTicks > 0 && (High0 - Low0) / tick < Adapt(ObrMinBarSizeTicks, AdaptBandDistances)) return;

		bool bearishBandLocation, bullishBandLocation;
		if (ObrLocationMode == ADM_OL_NearBand)
		{
			bearishBandLocation = High0 >= S.bbU - Adapt(BearObrTolTicks, AdaptBandDistances) * tick && (AllowObrOutsideBand || High0 <= S.bbU);
			bullishBandLocation = Low0 <= S.bbL + Adapt(BullObrTolTicks, AdaptBandDistances) * tick && (AllowObrOutsideBand || Low0 >= S.bbL);
		}
		else
		{
			bearishBandLocation = (High0 - Adapt(ObrPierceTicks, AdaptBandDistances) * tick >= S.bbU) || sc.High[cb-1] >= S.bbU1;
			bullishBandLocation = (Low0 + Adapt(ObrPierceTicks, AdaptBandDistances) * tick <= S.bbL) || sc.Low[cb-1] <= S.bbL1;
		}

		bool bearishObr = bearishBodyEngulf && bearishBandLocation && PassesBollingerMidpointFilter(-1, UseBbMidFilterObr)
			&& (!WaveFilterObr || S.wavePrev == -1);
		bool bullishObr = bullishBodyEngulf && bullishBandLocation && PassesBollingerMidpointFilter(1, UseBbMidFilterObr)
			&& (!WaveFilterObr || S.wavePrev == 1);

		if (bearishObr)
		{
			sc.Subgraph[SG_OBR][cb] = -1;
			if (sc.Subgraph[SG_CODE][cb] == 0)
			{
				sc.Subgraph[SG_DIR][cb] = -1; sc.Subgraph[SG_CODE][cb] = -3; sc.Subgraph[SG_MSHORT][cb] = -1;
				SetSuggestedPrices(-1);
			}
			if (PaintObrBars) { paintObr = ObrBearColor; havePaintObr = true; }
			DrawObrVisuals(-1);
			FireAlert(AlertObrShort, "OBR SHORT", -1);
		}
		else if (bullishObr)
		{
			sc.Subgraph[SG_OBR][cb] = 1;
			if (sc.Subgraph[SG_CODE][cb] == 0)
			{
				sc.Subgraph[SG_DIR][cb] = 1; sc.Subgraph[SG_CODE][cb] = 3; sc.Subgraph[SG_MLONG][cb] = 1;
				SetSuggestedPrices(1);
			}
			if (PaintObrBars) { paintObr = ObrBullColor; havePaintObr = true; }
			DrawObrVisuals(1);
			FireAlert(AlertObrLong, "OBR LONG", 1);
		}
	};

	// ---- run the gap engine + OBR in the original's exact order ----
	UpdatePendingContinuations();
	UpdateActiveGaps();
	if (!(SierraExactMode && IgnoreDojis && S.cDoji))
		DetectNewGap();
	DetectOutsideBarReversal();
	TrimOldestActiveGaps();
	UpdateContextPlots();

	// ================= SMI divergence (fractal pivots on price and on SMI; regular + hidden) =================
	const int SmiFractalBars = 2, SmiMaxPivots = 20;

	auto AddSmiPivot = [&](std::vector<SmiPivot>& list, double value, bool isHigh) {
		SmiPivot p; p.Bar = cb - SmiFractalBars; p.Value = value; p.IsHigh = isHigh;
		list.push_back(p);
		if ((int)list.size() > SmiMaxPivots) list.erase(list.begin());
	};

	auto IsSmiFractalPrice = [&](bool useHigh, int barsAgo, double minMove) -> bool {
		double c = useHigh ? sc.High[cb - barsAgo] : sc.Low[cb - barsAgo];
		for (int i = 1; i <= SmiFractalBars; i++)
		{
			double a = useHigh ? sc.High[cb - barsAgo - i] : sc.Low[cb - barsAgo - i];
			double b = useHigh ? sc.High[cb - barsAgo + i] : sc.Low[cb - barsAgo + i];
			if (useHigh) { if (a >= c || b >= c) return false; }
			else { if (a <= c || b <= c) return false; }
		}
		double n1 = useHigh ? sc.Low[cb - barsAgo - 1] : sc.High[cb - barsAgo - 1];
		double n2 = useHigh ? sc.Low[cb - barsAgo + 1] : sc.High[cb - barsAgo + 1];
		return useHigh ? (c - (std::min)(n1, n2)) >= minMove : ((std::max)(n1, n2) - c) >= minMove;
	};
	auto IsSmiFractalOsc = [&](bool high, int barsAgo, double minMove) -> bool {
		double c = S.smiLine[cb - barsAgo];
		for (int i = 1; i <= SmiFractalBars; i++)
		{
			double a = S.smiLine[cb - barsAgo - i], b = S.smiLine[cb - barsAgo + i];
			if (high) { if (a >= c || b >= c) return false; }
			else { if (a <= c || b <= c) return false; }
		}
		double n1 = S.smiLine[cb - barsAgo - 1], n2 = S.smiLine[cb - barsAgo + 1];
		return high ? (c - (std::min)(n1, n2)) >= minMove : ((std::max)(n1, n2) - c) >= minMove;
	};

	auto RecentSmiPivots = [&](const std::vector<SmiPivot>& src, bool isHigh) -> std::vector<SmiPivot> {
		std::vector<SmiPivot> r;
		for (const SmiPivot& p : src) if (p.IsHigh == isHigh && cb - p.Bar <= SmiDivergenceLookback) r.push_back(p);
		return r;
	};
	auto NearestSmiPivot = [&](int targetBar, const std::vector<SmiPivot>& list, int maxDistance, bool& found) -> SmiPivot {
		SmiPivot best; found = false; int bestD = 1 << 30;
		for (const SmiPivot& p : list) { int d = std::abs(p.Bar - targetBar); if (d <= maxDistance && d < bestD) { bestD = d; best = p; found = true; } }
		return best;
	};

	auto DetectSmiDivergence = [&](bool bullish) -> int {
		std::vector<SmiPivot> pp = RecentSmiPivots(S.smiPricePivots, !bullish);
		std::vector<SmiPivot> op = RecentSmiPivots(S.smiOscPivots, !bullish);
		if (pp.size() < 2 || op.size() < 2) return 0;
		SmiPivot p1 = pp[pp.size()-1], p2 = pp[pp.size()-2];
		bool f1, f2;
		SmiPivot o1 = NearestSmiPivot(p1.Bar, op, 3, f1);
		SmiPivot o2 = NearestSmiPivot(p2.Bar, op, 3, f2);
		if (!f1 || !f2) return 0;

		int type = 0;
		if (bullish)
		{
			if (p1.Value < p2.Value && o1.Value > o2.Value) type = 1;
			else if (SmiIncludeHiddenDivergence && p1.Value > p2.Value && o1.Value < o2.Value) type = 2;
		}
		else
		{
			if (p1.Value > p2.Value && o1.Value < o2.Value) type = 1;
			else if (SmiIncludeHiddenDivergence && p1.Value < p2.Value && o1.Value > o2.Value) type = 2;
		}
		if (type == 0) return 0;

		char keyBuf[64];
		snprintf(keyBuf, sizeof(keyBuf), "%s%d_%d_%d", bullish ? "B" : "S", type, p1.Bar, p2.Bar);
		std::string key(keyBuf);
		std::string& lastKey = bullish ? S.lastBullDivKey : S.lastBearDivKey;
		if (key == lastKey) return 0;
		lastKey = key;

		if (ShowSmiDivergenceLines)
		{
			COLORREF col = bullish ? (type == 1 ? SmiBullDivColor : SmiHiddenBullDivColor) : (type == 1 ? SmiBearDivColor : SmiHiddenBearDivColor);
			ADM_DrawLine(sc, NextLineId(), p1.Bar, p1.Value, p2.Bar, p2.Value, col, LINESTYLE_SOLID, 2, false);
		}
		return type;
	};

	if (EnableSmiDivergence)
	{
		int cbFrac = SmiFractalBars;
		double minPriceMove = S.smiAtr.Push(TRAt(cb), 14) * 0.5;
		if (cb - cbFrac >= cbFrac)
		{
			if (IsSmiFractalPrice(true, cbFrac, minPriceMove)) AddSmiPivot(S.smiPricePivots, sc.High[cb-cbFrac], true);
			if (IsSmiFractalPrice(false, cbFrac, minPriceMove)) AddSmiPivot(S.smiPricePivots, sc.Low[cb-cbFrac], false);
			if (cb - cbFrac >= cbFrac)
			{
				if (IsSmiFractalOsc(true, cbFrac, 5.0)) AddSmiPivot(S.smiOscPivots, S.smiLine[cb-cbFrac], true);
				if (IsSmiFractalOsc(false, cbFrac, 5.0)) AddSmiPivot(S.smiOscPivots, S.smiLine[cb-cbFrac], false);
			}

			int bull = DetectSmiDivergence(true);
			int bear = DetectSmiDivergence(false);
			if (bull != 0) sc.Subgraph[SG_SMIDIV][cb] = bull;
			else if (bear != 0) sc.Subgraph[SG_SMIDIV][cb] = -bear;

			if (bull != 0) FireAlert(AlertSmiDivBull, bull == 2 ? "SMI Bullish Divergence (hidden)" : "SMI Bullish Divergence", 1);
			else if (bear != 0) FireAlert(AlertSmiDivBear, bear == 2 ? "SMI Bearish Divergence (hidden)" : "SMI Bearish Divergence", -1);
		}
	}
	else
	{
		S.smiAtr.Push(TRAt(cb), 14); // keep this auxiliary ATR warm in case divergence is toggled on later
	}

	// ================= Apply bar paint (Waddah / Linda / SuperTrend / Shaved / OBR / BB-engulf shading) =================
	{
		COLORREF finalColor = 0; bool haveFinal = false;
		if (havePaintObr) { finalColor = paintObr; haveFinal = true; }
		else if (havePaintShaved) { finalColor = paintShaved; haveFinal = true; }
		else if (havePaintBase) { finalColor = paintBase; haveFinal = true; }
		if (haveFinal) sc.Subgraph[SG_BARCOLOR].DataColor[cb] = finalColor;

		if (haveBackShade)
			ADM_DrawRect(sc, NextLineId(), cb, High0 + 4 * tick, cb, Low0 - 4 * tick, backShade, 88);
	}

	// ================= Compact dashboard (attractive small HUD, anchored near the latest bar) =================
	if (ShowDashboard)
	{
		bool alignLeft = (DashboardCorner == ADM_DC_TopLeft || DashboardCorner == ADM_DC_BottomLeft);
		bool corTop = (DashboardCorner == ADM_DC_TopRight || DashboardCorner == ADM_DC_TopLeft);
		int align = (alignLeft ? DT_LEFT : DT_RIGHT) | DT_VCENTER;

		COLORREF neutralTextColor = ADM_RGB(225, 225, 230);
		COLORREF dimTextColor = ADM_RGB(150, 150, 160);
		COLORREF goodColor = ADM_RGB(0, 230, 118);
		COLORREF badColor = ADM_RGB(255, 82, 82);

		std::vector< std::pair<SCString,COLORREF> > rows;
		SCString line;

		rows.push_back(std::make_pair(SCString("◈  ADITRADYMEGATRON"), ADM_RGB(255, 209, 102)));

		line.Format("Trend    %s   ADX %.1f", (S.stMain.Dir > 0) ? "UP" : "DOWN", S.cAdx);
		rows.push_back(std::make_pair(line, (S.stMain.Dir > 0) ? goodColor : badColor));

		line.Format("Wave     %s", (S.wavePrev == 1) ? "UP" : "DOWN");
		rows.push_back(std::make_pair(line, (S.wavePrev == 1) ? goodColor : badColor));

		line.Format("SMI      %.1f  (%s)", S.cSmi, (S.cSmi > S.cSmiAvg) ? "bull" : ((S.cSmi < S.cSmiAvg) ? "bear" : "flat"));
		rows.push_back(std::make_pair(line, (S.cSmi > S.cSmiAvg) ? goodColor : ((S.cSmi < S.cSmiAvg) ? badColor : dimTextColor)));

		line.Format("Squeeze  %s", S.sqRelaxUp ? "up" : "down");
		rows.push_back(std::make_pair(line, S.sqRelaxUp ? goodColor : badColor));

		int activeGaps = (int)sc.Subgraph[SG_ACTIVEGAPS][cb];
		int validGaps = (int)sc.Subgraph[SG_VALIDGAPS][cb];
		bool webWarn = sc.Subgraph[SG_WEBWARN][cb] != 0;
		line.Format("Gaps     %d active / %d valid", activeGaps, validGaps);
		rows.push_back(std::make_pair(line, webWarn ? SpiderwebColor : neutralTextColor));
		if (webWarn) rows.push_back(std::make_pair(SCString("⚠  SPIDERWEB WARNING"), SpiderwebColor));

		line.Format("VolRatio  x%.2f", sc.Subgraph[SG_VOLRATIO][cb]);
		rows.push_back(std::make_pair(line, dimTextColor));

		// Anchor to real price/time (Sierra's "relative value" text positioning does not behave the
		// way a screen-fixed HUD would need, so this pins the panel near recent price action instead;
		// it will drift with price/zoom like any other chart-anchored drawing, e.g. the FC/BG/OBR labels).
		int lookback = (std::min)(cb, 80);
		double refHigh = ADM_MaxAt(HighAt, cb, lookback);
		double refLow = ADM_MinAt(LowAt, cb, lookback);
		double refRange = (std::max)(refHigh - refLow, 40.0 * tick);
		double rowStep = refRange * 0.05;
		double yTop = corTop ? (refHigh + refRange * 0.15) : (refLow - refRange * 0.15 + (double)(rows.size() - 1) * rowStep);

		COLORREF chipBack = ADM_RGB(12, 12, 16);
		int chipTransparency = 100 - DashboardOpacity;
		for (size_t i = 0; i < rows.size(); i++)
		{
			double y = yTop - (double)i * rowStep;
			s_UseTool tool; tool.Clear();
			tool.ChartNumber = sc.ChartNumber;
			tool.DrawingType = DRAWING_TEXT;
			tool.LineNumber = 1501 + (int)i;
			tool.BeginDateTime = sc.BaseDateTimeIn[cb];
			tool.BeginValue = (float)y;
			tool.Color = RGB(255,255,255); //rows[i].second;
			tool.TransparencyLevel = 30;
			tool.FontSize = 9;
			tool.FontBold = (i == 0) ? 1 : 0;
			tool.FontFace = "Arial";
			tool.Text = rows[i].first;
			tool.TextAlignment = align;
			tool.AddMethod = UTAM_ADD_OR_ADJUST;
			tool.AddAsUserDrawnDrawing = 0;
			sc.UseTool(tool);
		}
		for (size_t i = rows.size(); i < 12; i++)
			ADM_DeleteDrawing(sc, 1501 + (int)i); // clear stale rows if a previous bar had more (e.g. spiderweb warning went away)
	}
	else
	{
		for (int i = 0; i < 12; i++) ADM_DeleteDrawing(sc, 1501 + i);
	}
}