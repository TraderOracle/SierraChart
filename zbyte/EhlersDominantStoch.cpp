// Ehlers Dominant Cycle Stochastic RSI — Sierra Chart ACSIL port
// Ported 1:1 from EhlersDominantStoch.pine (Pine Script v6).
// See CONVERSION.md for the parameter catalog, mapping notes, and known
// fidelity deviations (all confined to warm-up bars / cosmetics).
//
// State model: every Pine `var` / recursive series is stored in a per-bar
// float array (sc.Subgraph[].Arrays[]), so bar N is always computed only from
// bars < N plus bar N's inputs. This reproduces Pine's rollback semantics on
// realtime tick updates and gives deterministic full recalculations.

#include "sierrachart.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

SCDLLName("Ehlers Dominant Cycle Stochastic RSI")

// ---------------------------------------------------------------- helpers

// Pine nz(x[k]) on history that precedes the first bar -> 0
static inline float HistF(SCFloatArrayRef A, int Index)
{
	return Index >= 0 ? A[Index] : 0.0f;
}

// TV color.new(color, transp): approximate transparency by blending toward a
// configurable target color (Sierra subgraph colors have no alpha channel).
// Set the target to the chart background color for a faithful fade.
static inline COLORREF BlendColor(COLORREF C, COLORREF Target, int Transp)
{
	if (Transp <= 0)
		return C;
	if (Transp >= 100)
		return Target;
	const float t = Transp / 100.0f;
	const float f = 1.0f - t;
	return RGB(
		(int)(GetRValue(C) * f + GetRValue(Target) * t),
		(int)(GetGValue(C) * f + GetGValue(Target) * t),
		(int)(GetBValue(C) * f + GetBValue(Target) * t));
}

static inline int IRound(double v)  // Pine math.round: half away from zero
{
	return (int)(v >= 0.0 ? floor(v + 0.5) : ceil(v - 0.5));
}

// -------- moving averages (streaming, per-Index, on chart-length arrays) --

// Windowed SMA. Pine returns na until the window fills; here we use a
// partial window for those first Length-1 bars (documented deviation).
static float SmaAt(SCFloatArrayRef In, int Index, int Length)
{
	const int n = (Index + 1 < Length) ? Index + 1 : Length;
	float sum = 0.0f;
	for (int i = 0; i < n; ++i)
		sum += In[Index - i];
	return n > 0 ? sum / n : 0.0f;
}

// Windowed WMA, most recent bar weighted heaviest (Pine ta.wma).
static float WmaAt(SCFloatArrayRef In, int Index, int Length)
{
	const int n = (Index + 1 < Length) ? Index + 1 : Length;
	float sum = 0.0f, wsum = 0.0f;
	for (int i = 0; i < n; ++i)  // i bars back, weight n - i
	{
		const float w = (float)(n - i);
		sum += In[Index - i] * w;
		wsum += w;
	}
	return wsum > 0 ? sum / wsum : 0.0f;
}

// EMA seeded with the first input value (Pine ta.ema seeding).
static void EmaAt(SCFloatArrayRef In, SCFloatArrayRef Out, int Index, int Length)
{
	if (Index == 0)
	{
		Out[Index] = In[Index];
		return;
	}
	const float a = 2.0f / (Length + 1);
	Out[Index] = a * In[Index] + (1.0f - a) * Out[Index - 1];
}

// Wilder RMA. Pine seeds with SMA(Length) at bar Length-1 (na before);
// a cumulative mean over bars 0..Length-2 converges to the identical seed.
static void RmaAt(SCFloatArrayRef In, SCFloatArrayRef Out, int Index, int Length)
{
	if (Index == 0)
	{
		Out[Index] = In[Index];
		return;
	}
	if (Index + 1 < Length)
	{
		Out[Index] = (Out[Index - 1] * Index + In[Index]) / (Index + 1);
		return;
	}
	const float a = 1.0f / Length;
	Out[Index] = a * In[Index] + (1.0f - a) * Out[Index - 1];
}

// ALMA per the TV v6 reference implementation (non-floored m).
static float AlmaAt(SCFloatArrayRef In, int Index, int Length, float Offset, float Sigma)
{
	const int n = (Index + 1 < Length) ? Index + 1 : Length;
	if (n <= 1)
		return In[Index];
	const float m = Offset * (n - 1);
	const float s = n / Sigma;
	float norm = 0.0f, sum = 0.0f;
	for (int j = 0; j < n; ++j)  // j = 0 oldest .. n-1 newest
	{
		const float w = (float)exp(-((j - m) * (j - m)) / (2.0f * s * s));
		norm += w;
		sum += In[Index - (n - 1) + j] * w;
	}
	return norm > 0 ? sum / norm : In[Index];
}

// MA selector matching the Pine option list order:
// 0 EMA, 1 SMA, 2 WMA, 3 RMA, 4 HMA, 5 DEMA, 6 TEMA, 7 ALMA.
// E1/E2/E3 are scratch arrays owned by the caller (unique per MA instance);
// E3 doubles as the HMA intermediate series.
static void MaAt(int Type, SCFloatArrayRef In, SCFloatArrayRef Out,
	SCFloatArrayRef E1, SCFloatArrayRef E2, SCFloatArrayRef E3,
	int Index, int Length, float AlmaOff, float AlmaSig)
{
	switch (Type)
	{
	case 1:  // SMA
		Out[Index] = SmaAt(In, Index, Length);
		break;
	case 2:  // WMA
		Out[Index] = WmaAt(In, Index, Length);
		break;
	case 3:  // RMA
		RmaAt(In, Out, Index, Length);
		break;
	case 4:  // HMA = WMA(2*WMA(n/2) - WMA(n), round(sqrt(n)))
	{
		const int half = Length / 2 > 0 ? Length / 2 : 1;
		E3[Index] = 2.0f * WmaAt(In, Index, half) - WmaAt(In, Index, Length);
		const int sqLen = IRound(sqrt((double)Length));
		Out[Index] = WmaAt(E3, Index, sqLen > 0 ? sqLen : 1);
		break;
	}
	case 5:  // DEMA = 2*e1 - e2
		EmaAt(In, E1, Index, Length);
		EmaAt(E1, E2, Index, Length);
		Out[Index] = 2.0f * E1[Index] - E2[Index];
		break;
	case 6:  // TEMA = 3*(e1 - e2) + e3
		EmaAt(In, E1, Index, Length);
		EmaAt(E1, E2, Index, Length);
		EmaAt(E2, E3, Index, Length);
		Out[Index] = 3.0f * (E1[Index] - E2[Index]) + E3[Index];
		break;
	case 7:  // ALMA
		Out[Index] = AlmaAt(In, Index, Length, AlmaOff, AlmaSig);
		break;
	case 0:  // EMA (and Pine's default fallthrough)
	default:
		EmaAt(In, Out, Index, Length);
		break;
	}
}

// ---------------------------------------------------------------- study

SCSFExport scsf_EhlersDominantCycleStochRSI(SCStudyInterfaceRef sc)
{
	// ----- visible subgraphs
	// Sierra Chart draws subgraphs in ascending order — later subgraphs paint
	// on top of earlier ones. Z-order therefore must be: background < zone
	// fills < histogram < reference lines < K/D lines < signal arrows.
	SCSubgraphRef SgBackground = sc.Subgraph[0];
	SCSubgraphRef SgZoneTopHi  = sc.Subgraph[1];
	SCSubgraphRef SgZoneTopLo  = sc.Subgraph[2];
	SCSubgraphRef SgZoneBotHi  = sc.Subgraph[3];
	SCSubgraphRef SgZoneBotLo  = sc.Subgraph[4];
	SCSubgraphRef SgHistTop    = sc.Subgraph[5];
	SCSubgraphRef SgHistBot    = sc.Subgraph[6];
	SCSubgraphRef SgOB         = sc.Subgraph[7];
	SCSubgraphRef SgMid        = sc.Subgraph[8];
	SCSubgraphRef SgOS         = sc.Subgraph[9];
	SCSubgraphRef SgK          = sc.Subgraph[10];
	SCSubgraphRef SgD          = sc.Subgraph[11];

	// ----- signal subgraphs (cross arrows, drawn above the lines)
	SCSubgraphRef SgBullSignal  = sc.Subgraph[12];
	SCSubgraphRef SgBearSignal  = sc.Subgraph[13];

	// ----- data-window subgraphs (mirrors the Pine display.data_window plots)
	SCSubgraphRef SgFinalPeriod = sc.Subgraph[14];
	SCSubgraphRef SgHilbert     = sc.Subgraph[15];
	SCSubgraphRef SgAutocorr    = sc.Subgraph[16];
	SCSubgraphRef SgDft         = sc.Subgraph[17];
	SCSubgraphRef SgConfidence  = sc.Subgraph[18];
	SCSubgraphRef SgRsiPeriod   = sc.Subgraph[19];
	SCSubgraphRef SgStochPeriod = sc.Subgraph[20];
	SCSubgraphRef SgRawRsi      = sc.Subgraph[21];
	SCSubgraphRef SgHistValue   = sc.Subgraph[22];
	SCSubgraphRef SgAcCorrPct   = sc.Subgraph[23];

	// ----- internal per-bar state arrays (the Pine `var` series)
	SCFloatArrayRef ProcSrc     = sc.Subgraph[0].Arrays[0];   // bandpass output or raw src
	SCFloatArrayRef PpHP        = sc.Subgraph[0].Arrays[1];   // highpass of ProcSrc
	SCFloatArrayRef PpFilt      = sc.Subgraph[0].Arrays[2];   // super smoother of PpHP
	SCFloatArrayRef HtSmooth    = sc.Subgraph[1].Arrays[0];   // hilbert: smoothed price
	SCFloatArrayRef HtDetrend   = sc.Subgraph[1].Arrays[1];
	SCFloatArrayRef HtI1        = sc.Subgraph[1].Arrays[2];
	SCFloatArrayRef HtQ1        = sc.Subgraph[1].Arrays[3];
	SCFloatArrayRef HtI2        = sc.Subgraph[1].Arrays[4];
	SCFloatArrayRef HtQ2        = sc.Subgraph[1].Arrays[5];
	SCFloatArrayRef HtRe        = sc.Subgraph[1].Arrays[6];
	SCFloatArrayRef HtIm        = sc.Subgraph[1].Arrays[7];
	SCFloatArrayRef HtPeriod    = sc.Subgraph[1].Arrays[8];
	SCFloatArrayRef HtSmPeriod  = sc.Subgraph[1].Arrays[9];
	SCFloatArrayRef AcDomPeriod = sc.Subgraph[2].Arrays[0];   // held between throttled scans
	SCFloatArrayRef AcMaxCorr   = sc.Subgraph[2].Arrays[1];   // held between throttled scans
	SCFloatArrayRef AcSmPeriod  = sc.Subgraph[2].Arrays[2];
	SCFloatArrayRef DftDomPeriod = sc.Subgraph[3].Arrays[0];  // held between throttled scans
	SCFloatArrayRef DftSmPeriod  = sc.Subgraph[3].Arrays[1];
	SCFloatArrayRef FinalPeriod = sc.Subgraph[4].Arrays[0];
	SCFloatArrayRef AvgGain     = sc.Subgraph[4].Arrays[1];
	SCFloatArrayRef AvgLoss     = sc.Subgraph[4].Arrays[2];
	SCFloatArrayRef CycleRsi    = sc.Subgraph[5].Arrays[0];
	SCFloatArrayRef RawK        = sc.Subgraph[5].Arrays[1];
	SCFloatArrayRef Hist        = sc.Subgraph[6].Arrays[0];
	SCFloatArrayRef KE1         = sc.Subgraph[7].Arrays[0];   // K MA scratch
	SCFloatArrayRef KE2         = sc.Subgraph[7].Arrays[1];
	SCFloatArrayRef KE3         = sc.Subgraph[7].Arrays[2];
	SCFloatArrayRef DE1         = sc.Subgraph[8].Arrays[0];   // D MA scratch
	SCFloatArrayRef DE2         = sc.Subgraph[8].Arrays[1];
	SCFloatArrayRef DE3         = sc.Subgraph[8].Arrays[2];

	// ----- inputs (Pine inputs in order, with the approved enhancements)
	SCInputRef InSource        = sc.Input[0];
	SCInputRef InMinPeriod     = sc.Input[1];
	SCInputRef InMaxPeriod     = sc.Input[2];
	SCInputRef InUseSpectral   = sc.Input[3];
	SCInputRef InBandwidth     = sc.Input[4];
	SCInputRef InConfirmTol    = sc.Input[5];
	SCInputRef InRsiMult       = sc.Input[6];
	SCInputRef InRsiCalc       = sc.Input[7];    // enhancement 2
	SCInputRef InStochMult     = sc.Input[8];
	SCInputRef InCapStochLookback = sc.Input[9]; // enhancement 1
	SCInputRef InKSmooth       = sc.Input[10];
	SCInputRef InKMaType       = sc.Input[11];
	SCInputRef InDSmooth       = sc.Input[12];
	SCInputRef InDMaType       = sc.Input[13];
	SCInputRef InAlmaOffset    = sc.Input[14];
	SCInputRef InAlmaSigma     = sc.Input[15];
	SCInputRef InVisualStyle   = sc.Input[16];
	SCInputRef InColorTheme    = sc.Input[17];
	SCInputRef InUpColor       = sc.Input[18];
	SCInputRef InDownColor     = sc.Input[19];
	SCInputRef InLineWidth     = sc.Input[20];
	SCInputRef InShowSignal    = sc.Input[21];
	SCInputRef InShowHist      = sc.Input[22];
	SCInputRef InHistNormLen   = sc.Input[23];
	SCInputRef InHistTransp    = sc.Input[24];
	SCInputRef InShowZones     = sc.Input[25];
	SCInputRef InShowSignalArrows = sc.Input[26]; // enhancement 3
	SCInputRef InFadeTarget    = sc.Input[27];   // enhancement 5
	SCInputRef InHideWarmup    = sc.Input[28];   // enhancement 6
	SCInputRef InUpdateInterval = sc.Input[29];
	SCInputRef InDftBufferMult  = sc.Input[30];
	SCInputRef InShowCycleInfo  = sc.Input[31];
	SCInputRef InShowConfidBg   = sc.Input[32];
	SCInputRef InEnableAlerts   = sc.Input[33];
	SCInputRef InAlertTiming    = sc.Input[34];  // enhancement 4
	SCInputRef InBullAlertNum   = sc.Input[35];  // enhancement 4
	SCInputRef InBearAlertNum   = sc.Input[36];  // enhancement 4
	SCInputRef InConfAlertNum   = sc.Input[37];  // enhancement 4
	SCInputRef InBullThreshold  = sc.Input[38];  // cross signals only with K below this
	SCInputRef InBearThreshold  = sc.Input[39];  // cross signals only with K above this
	SCInputRef InMinSignalConf  = sc.Input[40];  // confidence gate for arrows + cross alerts

	if (sc.SetDefaults)
	{
		sc.GraphName = "Ehlers Dominant Cycle Stochastic RSI";
		sc.StudyDescription = "Adaptive Stochastic RSI whose RSI/Stoch lookbacks track the "
			"dominant market cycle, detected by three confirmed methods: Hilbert Transform, "
			"Autocorrelation Periodogram, and Goertzel DFT. Port of the TradingView Pine study.";
		sc.GraphRegion = 1;          // Pine overlay=false
		sc.AutoLoop = 0;             // manual loop (heavy inner scans, direct array indexing)
		sc.ValueFormat = 2;
		sc.ScaleRangeType = SCALE_AUTO;

		SgK.Name = "StochRSI K";
		SgK.DrawStyle = DRAWSTYLE_LINE;
		SgK.PrimaryColor = RGB(0x00, 0xE5, 0xFF);
		SgK.LineWidth = 4;
		SgK.DrawZeros = 1;

		SgD.Name = "D (Signal)";
		SgD.DrawStyle = DRAWSTYLE_LINE;
		SgD.PrimaryColor = RGB(0x67, 0x6B, 0x72);
		SgD.LineWidth = 2;
		SgD.DrawZeros = 1;

		SgHistTop.Name = "Histogram Fill Top";
		SgHistTop.DrawStyle = DRAWSTYLE_FILL_RECTANGLE_TOP;
		SgHistTop.PrimaryColor = RGB(0x00, 0xA0, 0xB2);
		SgHistTop.DrawZeros = 1;
		SgHistBot.Name = "Histogram Fill Bottom";
		SgHistBot.DrawStyle = DRAWSTYLE_FILL_RECTANGLE_BOTTOM;
		SgHistBot.PrimaryColor = RGB(0x00, 0xA0, 0xB2);
		SgHistBot.DrawZeros = 1;

		SgOB.Name = "Overbought (80)";
		SgOB.DrawStyle = DRAWSTYLE_DASH;
		SgOB.PrimaryColor = RGB(0x99, 0x0E, 0x29);
		SgOB.LineWidth = 1;
		SgOB.DrawZeros = 1;
		SgMid.Name = "Midline (50)";
		SgMid.DrawStyle = DRAWSTYLE_DASH;
		SgMid.PrimaryColor = RGB(0x36, 0x37, 0x3C);
		SgMid.LineWidth = 1;
		SgMid.DrawZeros = 1;
		SgOS.Name = "Oversold (20)";
		SgOS.DrawStyle = DRAWSTYLE_DASH;
		SgOS.PrimaryColor = RGB(0x00, 0x89, 0x99);
		SgOS.LineWidth = 1;
		SgOS.DrawZeros = 1;

		SgZoneTopHi.Name = "OB Zone Top";
		SgZoneTopHi.DrawStyle = DRAWSTYLE_FILL_RECTANGLE_TOP;
		SgZoneTopHi.PrimaryColor = RGB(0x14, 0x02, 0x05);
		SgZoneTopHi.DrawZeros = 1;
		SgZoneTopLo.Name = "OB Zone Bottom";
		SgZoneTopLo.DrawStyle = DRAWSTYLE_FILL_RECTANGLE_BOTTOM;
		SgZoneTopLo.PrimaryColor = RGB(0x14, 0x02, 0x05);
		SgZoneTopLo.DrawZeros = 1;
		SgZoneBotHi.Name = "OS Zone Top";
		SgZoneBotHi.DrawStyle = DRAWSTYLE_FILL_RECTANGLE_TOP;
		SgZoneBotHi.PrimaryColor = RGB(0x00, 0x12, 0x14);
		SgZoneBotHi.DrawZeros = 1;
		SgZoneBotLo.Name = "OS Zone Bottom";
		SgZoneBotLo.DrawStyle = DRAWSTYLE_FILL_RECTANGLE_BOTTOM;
		SgZoneBotLo.PrimaryColor = RGB(0x00, 0x12, 0x14);
		SgZoneBotLo.DrawZeros = 1;

		SgBackground.Name = "Cycle Confidence Background";
		SgBackground.DrawStyle = DRAWSTYLE_IGNORE;
		SgBackground.PrimaryColor = RGB(0, 40, 0);
		SgBackground.DrawZeros = 0;

		SgFinalPeriod.Name = "Final Cycle Period";
		SgHilbert.Name     = "Hilbert Period";
		SgAutocorr.Name    = "Autocorrelation Period";
		SgDft.Name         = "DFT Period";
		SgConfidence.Name  = "Cycle Confidence %";
		SgRsiPeriod.Name   = "RSI Period";
		SgStochPeriod.Name = "Stoch Period";
		SgRawRsi.Name      = "Raw RSI";
		SgHistValue.Name   = "Histogram Value";
		SgAcCorrPct.Name   = "AC Correlation %";
		for (int i = 14; i <= 23; ++i)
		{
			sc.Subgraph[i].DrawStyle = DRAWSTYLE_HIDDEN;  // values only (TV data window)
			sc.Subgraph[i].PrimaryColor = RGB(128, 128, 128);
			sc.Subgraph[i].DrawZeros = 1;
		}

		SgBullSignal.Name = "Bull Cross Signal";
		SgBullSignal.DrawStyle = DRAWSTYLE_ARROW_UP;
		SgBullSignal.PrimaryColor = RGB(0x00, 0xE5, 0xFF);
		SgBullSignal.LineWidth = 2;
		SgBullSignal.DrawZeros = 0;

		SgBearSignal.Name = "Bear Cross Signal";
		SgBearSignal.DrawStyle = DRAWSTYLE_ARROW_DOWN;
		SgBearSignal.PrimaryColor = RGB(0xFF, 0x17, 0x44);
		SgBearSignal.LineWidth = 2;
		SgBearSignal.DrawZeros = 0;

		InSource.Name = "Source";
		InSource.SetInputDataIndex(SC_LAST);  // Pine default: close

		InMinPeriod.Name = "Min Period";
		InMinPeriod.SetInt(8);
		InMinPeriod.SetIntLimits(4, 20);

		InMaxPeriod.Name = "Max Period";
		InMaxPeriod.SetInt(50);
		InMaxPeriod.SetIntLimits(20, 100);

		InUseSpectral.Name = "Use Spectral Dilation (Bandpass Pre-filter)";
		InUseSpectral.SetYesNo(1);

		InBandwidth.Name = "Bandpass Bandwidth";
		InBandwidth.SetFloat(0.3f);
		InBandwidth.SetFloatLimits(0.1f, 0.5f);
		InBandwidth.SetDescription("Lower = narrower band, more selective");

		InConfirmTol.Name = "Confirmation Tolerance (%)";
		InConfirmTol.SetFloat(0.2f);
		InConfirmTol.SetFloatLimits(0.05f, 0.5f);
		InConfirmTol.SetDescription("Methods must agree within this % to confirm");

		InRsiMult.Name = "RSI Period Multiplier";
		InRsiMult.SetFloat(1.0f);
		InRsiMult.SetFloatLimits(0.25f, 2.0f);
		InRsiMult.SetDescription("Scales RSI period relative to detected cycle. 0.5 = half-cycle "
			"(more responsive), 1.0 = full cycle (smoother)");

		InRsiCalc.Name = "RSI Calculation";
		InRsiCalc.SetCustomInputStrings("EMA (Original);Wilder (Classic RSI)");
		InRsiCalc.SetCustomInputIndex(0);
		InRsiCalc.SetDescription("EMA = the Pine original (alpha 2/(P+1), more responsive). "
			"Wilder = classic RSI smoothing (alpha 1/P), comparable to standard RSI readings.");

		InStochMult.Name = "Stoch Period Multiplier";
		InStochMult.SetFloat(1.0f);
		InStochMult.SetFloatLimits(0.25f, 2.0f);
		InStochMult.SetDescription("Scales stochastic lookback relative to detected cycle. 0.5 = "
			"half-cycle (faster signals), 1.0 = full cycle (fewer false signals)");

		InCapStochLookback.Name = "Cap Stoch Lookback at Max Period (TV Behavior)";
		InCapStochLookback.SetYesNo(0);
		InCapStochLookback.SetDescription("The original Pine loop silently capped the stochastic "
			"lookback at Max Period bars, so multipliers > 1 were not fully honored when the "
			"detected cycle was near Max Period. No = honor the full lookback. Yes = reproduce "
			"the TradingView behavior exactly.");

		InKSmooth.Name = "K Smoothing Length";
		InKSmooth.SetInt(10);
		InKSmooth.SetIntLimits(1, 500);

		InKMaType.Name = "K Smoothing Type";
		InKMaType.SetCustomInputStrings("EMA;SMA;WMA;RMA;HMA;DEMA;TEMA;ALMA");
		InKMaType.SetCustomInputIndex(0);

		InDSmooth.Name = "D Smoothing Length";
		InDSmooth.SetInt(4);
		InDSmooth.SetIntLimits(1, 500);

		InDMaType.Name = "D Smoothing Type";
		InDMaType.SetCustomInputStrings("EMA;SMA;WMA;RMA;HMA;DEMA;TEMA;ALMA");
		InDMaType.SetCustomInputIndex(0);

		InAlmaOffset.Name = "ALMA Offset";
		InAlmaOffset.SetFloat(0.85f);
		InAlmaOffset.SetFloatLimits(0.0f, 1.0f);
		InAlmaOffset.SetDescription("Only used when an MA type is ALMA. Higher = more responsive / less lag.");

		InAlmaSigma.Name = "ALMA Sigma";
		InAlmaSigma.SetFloat(6.0f);
		InAlmaSigma.SetFloatLimits(1.0f, 100.0f);
		InAlmaSigma.SetDescription("Only used when an MA type is ALMA. Higher = smoother.");

		InVisualStyle.Name = "Appearance";
		InVisualStyle.SetCustomInputStrings("Modern;Classic");
		InVisualStyle.SetCustomInputIndex(0);
		InVisualStyle.SetDescription("Modern = single thick line colored by direction. Classic = separate K and D lines.");

		InColorTheme.Name = "Color Theme";
		InColorTheme.SetCustomInputStrings("Modern (Red/Cyan);Classic (Blue/Orange);Neon (Green/Magenta);"
			"Ocean (Teal/Indigo);Sunset (Gold/Pink);Mono (White/Gray);Custom");
		InColorTheme.SetCustomInputIndex(0);

		InUpColor.Name = "Custom Up Color";
		InUpColor.SetColor(RGB(0x00, 0xE5, 0xFF));
		InUpColor.SetDescription("Used only when Color Theme = Custom");

		InDownColor.Name = "Custom Down Color";
		InDownColor.SetColor(RGB(0xFF, 0x17, 0x44));
		InDownColor.SetDescription("Used only when Color Theme = Custom");

		InLineWidth.Name = "Modern Line Width";
		InLineWidth.SetInt(4);
		InLineWidth.SetIntLimits(1, 8);

		InShowSignal.Name = "Show Signal (D) Line";
		InShowSignal.SetYesNo(1);

		InShowHist.Name = "Show Histogram (K - D)";
		InShowHist.SetYesNo(1);

		InHistNormLen.Name = "Histogram Normalization Length";
		InHistNormLen.SetInt(50);
		InHistNormLen.SetIntLimits(5, 5000);
		InHistNormLen.SetDescription("The histogram (K - D) is normalized over this many bars and "
			"scaled to fill the 20-80 band, centered on the 50 midline.");

		InHistTransp.Name = "Histogram Transparency";
		InHistTransp.SetInt(30);
		InHistTransp.SetIntLimits(0, 100);
		InHistTransp.SetDescription("0 = solid, 100 = invisible. Bars losing momentum fade an extra step. "
			"Approximated by blending toward the Fade/Transparency Blend Color.");

		InShowZones.Name = "Highlight OB/OS Zones";
		InShowZones.SetYesNo(1);

		InShowSignalArrows.Name = "Show Cross Signal Arrows";
		InShowSignalArrows.SetYesNo(1);
		InShowSignalArrows.SetDescription("Draws an up arrow when K crosses above D in the oversold "
			"zone and a down arrow when K crosses below D in the overbought zone. The Bull/Bear "
			"Cross Signal subgraphs hold values even when hidden, for Alert Conditions and automation.");

		InFadeTarget.Name = "Fade/Transparency Blend Color";
		InFadeTarget.SetColor(RGB(0, 0, 0));
		InFadeTarget.SetDescription("Transparency effects (zone fills, histogram fade, background, "
			"dimmed lines) blend toward this color. Set it to your chart background color.");

		InHideWarmup.Name = "Hide Warm-up Bars";
		InHideWarmup.SetYesNo(1);
		InHideWarmup.SetDescription("Suppresses drawing of the first Max Period bars, where the "
			"adaptive filters are still settling and output is not meaningful. Values are still "
			"calculated. No = draw from the first bar like TradingView.");

		InUpdateInterval.Name = "Cycle Recalc Interval (bars)";
		InUpdateInterval.SetInt(3);
		InUpdateInterval.SetIntLimits(1, 20);
		InUpdateInterval.SetDescription("Recompute the heavy autocorrelation & DFT cycle scans every N "
			"bars instead of every bar. 1 = every bar (most accurate, slowest).");

		InDftBufferMult.Name = "DFT Sample Window (x Max Period)";
		InDftBufferMult.SetFloat(2.0f);
		InDftBufferMult.SetFloatLimits(1.0f, 2.0f);
		InDftBufferMult.SetDescription("How many bars the DFT analyzes, as a multiple of Max Period. "
			"2.0 = best resolution, 1.0 = fastest.");

		InShowCycleInfo.Name = "Show Cycle Detection Info";
		InShowCycleInfo.SetYesNo(0);

		InShowConfidBg.Name = "Cycle Correlation Confidence Background";
		InShowConfidBg.SetYesNo(0);
		InShowConfidBg.SetDescription("Background shows agreement between cycle detection methods: "
			"Green = high confidence, Yellow = moderate, Red = low");

		InEnableAlerts.Name = "Enable Alerts";
		InEnableAlerts.SetYesNo(0);
		InEnableAlerts.SetDescription("Master switch for the three alert conditions below "
			"(the Pine alertconditions).");

		InAlertTiming.Name = "Alert Timing";
		InAlertTiming.SetCustomInputStrings("Bar Close;Intrabar");
		InAlertTiming.SetCustomInputIndex(0);
		InAlertTiming.SetDescription("Bar Close = evaluate the last closed bar (TV 'Once Per Bar "
			"Close'). Intrabar = evaluate the developing bar, at most once per bar; note the "
			"condition can appear intrabar and be gone by the close.");

		InBullAlertNum.Name = "Bullish Cross Alert Number (0 = off)";
		InBullAlertNum.SetInt(1);
		InBullAlertNum.SetIntLimits(0, 150);
		InBullAlertNum.SetDescription("Sierra Chart alert/sound number for: K crossed above D in "
			"the oversold zone. 0 disables this alert.");

		InBearAlertNum.Name = "Bearish Cross Alert Number (0 = off)";
		InBearAlertNum.SetInt(2);
		InBearAlertNum.SetIntLimits(0, 150);
		InBearAlertNum.SetDescription("Sierra Chart alert/sound number for: K crossed below D in "
			"the overbought zone. 0 disables this alert.");

		InConfAlertNum.Name = "High Confidence Alert Number (0 = off)";
		InConfAlertNum.SetInt(3);
		InConfAlertNum.SetIntLimits(0, 150);
		InConfAlertNum.SetDescription("Sierra Chart alert/sound number for: cycle confidence rose "
			"to 66% or higher (all three methods agree). 0 disables this alert.");

		InBullThreshold.Name = "Bullish Cross Threshold (K Below)";
		InBullThreshold.SetFloat(30.0f);
		InBullThreshold.SetFloatLimits(0.0f, 100.0f);
		InBullThreshold.SetDescription("A bullish K/D cross only signals (arrow + alert) when K is "
			"below this level at the cross bar. 30 = the original hardcoded value; 20 matches the "
			"drawn oversold zone; 100 = signal every bullish cross.");

		InBearThreshold.Name = "Bearish Cross Threshold (K Above)";
		InBearThreshold.SetFloat(70.0f);
		InBearThreshold.SetFloatLimits(0.0f, 100.0f);
		InBearThreshold.SetDescription("A bearish K/D cross only signals (arrow + alert) when K is "
			"above this level at the cross bar. 70 = the original hardcoded value; 80 matches the "
			"drawn overbought zone; 0 = signal every bearish cross.");

		InMinSignalConf.Name = "Minimum Signal Confidence % (0 = Off)";
		InMinSignalConf.SetInt(0);
		InMinSignalConf.SetIntLimits(0, 100);
		InMinSignalConf.SetDescription("Suppress cross arrows and cross alerts unless the cycle "
			"confidence is at least this high. Confidence steps are 0/33/67/100: set 34 to require "
			"at least two of the three cycle methods to agree, 67 to require all three. Does not "
			"affect the High Confidence alert.");

		return;
	}

	// ------------------------------------------------------------ settings
	const int   minPeriod    = InMinPeriod.GetInt();
	const int   maxPeriod    = max(InMaxPeriod.GetInt(), minPeriod + 1);
	const int   useSpectral  = InUseSpectral.GetYesNo();
	const float bandwidth    = InBandwidth.GetFloat();
	const float confirmTol   = InConfirmTol.GetFloat();
	const float rsiMult      = InRsiMult.GetFloat();
	const bool  wilderRsi    = InRsiCalc.GetIndex() == 1;
	const float stochMult    = InStochMult.GetFloat();
	const bool  capStochLookback = InCapStochLookback.GetYesNo() != 0;
	const int   kSmoothLen   = InKSmooth.GetInt();
	const int   kMaType      = InKMaType.GetIndex();
	const int   dSmoothLen   = InDSmooth.GetInt();
	const int   dMaType      = InDMaType.GetIndex();
	const float almaOffset   = InAlmaOffset.GetFloat();
	const float almaSigma    = InAlmaSigma.GetFloat();
	const bool  isModern     = InVisualStyle.GetIndex() == 0;
	const int   theme        = InColorTheme.GetIndex();
	const int   lineWidth    = InLineWidth.GetInt();
	const bool  showSignal   = InShowSignal.GetYesNo() != 0;
	const bool  showHist     = InShowHist.GetYesNo() != 0;
	const int   histNormLen  = InHistNormLen.GetInt();
	const int   histTransp   = InHistTransp.GetInt();
	const bool  showZones    = InShowZones.GetYesNo() != 0;
	const bool  showSignalArrows = InShowSignalArrows.GetYesNo() != 0;
	const float bullThreshold = InBullThreshold.GetFloat();
	const float bearThreshold = InBearThreshold.GetFloat();
	const int   minSignalConf = InMinSignalConf.GetInt();
	const COLORREF fadeTarget = InFadeTarget.GetColor();
	const bool  hideWarmup   = InHideWarmup.GetYesNo() != 0;
	const int   updateInterval = InUpdateInterval.GetInt();
	const float dftBufferMult  = InDftBufferMult.GetFloat();
	const bool  showCycleInfo  = InShowCycleInfo.GetYesNo() != 0;
	const bool  showConfidBg   = InShowConfidBg.GetYesNo() != 0;

	// ------------------------------------------------------------ theme
	COLORREF upCol, downCol;
	switch (theme)
	{
	case 0:  upCol = RGB(0x00, 0xE5, 0xFF); downCol = RGB(0xFF, 0x17, 0x44); break; // Modern
	case 1:  upCol = RGB(0x29, 0x62, 0xFF); downCol = RGB(0xFF, 0x98, 0x00); break; // Classic
	case 2:  upCol = RGB(0x39, 0xFF, 0x14); downCol = RGB(0xFF, 0x00, 0xFF); break; // Neon
	case 3:  upCol = RGB(0x2D, 0xD4, 0xBF); downCol = RGB(0x63, 0x66, 0xF1); break; // Ocean
	case 4:  upCol = RGB(0xFF, 0xD6, 0x0A); downCol = RGB(0xFF, 0x1F, 0x6B); break; // Sunset
	case 5:  upCol = RGB(0xEC, 0xEF, 0xF1); downCol = RGB(0x60, 0x7D, 0x8B); break; // Mono
	default: upCol = InUpColor.GetColor();  downCol = InDownColor.GetColor(); break; // Custom
	}
	const COLORREF grayCol = RGB(0x78, 0x7B, 0x86);  // TV color.gray

	// Apply display configuration (theme/style driven, idempotent per update)
	SgK.LineWidth = isModern ? lineWidth : 2;
	SgK.PrimaryColor = upCol;
	SgD.DrawStyle = showSignal ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
	SgD.PrimaryColor = isModern ? BlendColor(grayCol, fadeTarget, 15) : downCol;
	SgHistTop.DrawStyle = showHist ? DRAWSTYLE_FILL_RECTANGLE_TOP : DRAWSTYLE_IGNORE;
	SgHistBot.DrawStyle = showHist ? DRAWSTYLE_FILL_RECTANGLE_BOTTOM : DRAWSTYLE_IGNORE;
	SgOB.PrimaryColor  = BlendColor(downCol, fadeTarget, 40);
	SgMid.PrimaryColor = BlendColor(grayCol, fadeTarget, 55);
	SgOS.PrimaryColor  = BlendColor(upCol, fadeTarget, 40);
	SgZoneTopHi.DrawStyle = showZones ? DRAWSTYLE_FILL_RECTANGLE_TOP : DRAWSTYLE_IGNORE;
	SgZoneTopLo.DrawStyle = showZones ? DRAWSTYLE_FILL_RECTANGLE_BOTTOM : DRAWSTYLE_IGNORE;
	SgZoneBotHi.DrawStyle = showZones ? DRAWSTYLE_FILL_RECTANGLE_TOP : DRAWSTYLE_IGNORE;
	SgZoneBotLo.DrawStyle = showZones ? DRAWSTYLE_FILL_RECTANGLE_BOTTOM : DRAWSTYLE_IGNORE;
	const COLORREF obZoneCol = BlendColor(downCol, fadeTarget, 92);
	const COLORREF osZoneCol = BlendColor(upCol, fadeTarget, 92);
	SgZoneTopHi.PrimaryColor = obZoneCol;
	SgZoneTopLo.PrimaryColor = obZoneCol;
	SgZoneBotHi.PrimaryColor = osZoneCol;
	SgZoneBotLo.PrimaryColor = osZoneCol;
	SgBackground.DrawStyle = showConfidBg ? DRAWSTYLE_BACKGROUND : DRAWSTYLE_IGNORE;
	SgBullSignal.DrawStyle = showSignalArrows ? DRAWSTYLE_ARROW_UP : DRAWSTYLE_IGNORE;
	SgBullSignal.PrimaryColor = upCol;
	SgBearSignal.DrawStyle = showSignalArrows ? DRAWSTYLE_ARROW_DOWN : DRAWSTYLE_IGNORE;
	SgBearSignal.PrimaryColor = downCol;

	// Warm-up suppression: skip drawing while the adaptive filters settle
	sc.DataStartIndex = hideWarmup ? maxPeriod : 0;

	SCFloatArrayRef Src = sc.BaseData[InSource.GetInputDataIndex()];

	// Derived constants (identical to the Pine globals)
	const int centerPeriod = (minPeriod + maxPeriod) / 2;      // Pine int division
	const int dftBufSize = max(maxPeriod, IRound(maxPeriod * dftBufferMult));
	const int histFade = min(histTransp + 30, 100);

	// Bandpass coefficients (constant per settings)
	const double bpBeta  = cos(2.0 * M_PI / centerPeriod);
	const double bpGamma = 1.0 / cos(4.0 * M_PI * bandwidth / centerPeriod);
	const double bpAlpha = bpGamma - sqrt(bpGamma * bpGamma - 1.0);

	// Highpass coefficients (length = maxPeriod)
	const double hpAlpha = (0.707 * 2.0 * M_PI) / maxPeriod;
	const double hpA  = exp(-hpAlpha);
	const double hpC2 = 2.0 * hpA * cos(hpAlpha);
	const double hpC3 = -hpA * hpA;
	const double hpC1 = (1.0 + hpC2 - hpC3) / 4.0;

	// Super smoother coefficients (fixed length 10, as in the Pine source)
	const double ssCoef = M_PI * sqrt(2.0) / 10.0;
	const double ssA1 = exp(-ssCoef);
	const double ssC2 = 2.0 * ssA1 * cos(ssCoef * sqrt(2.0));
	const double ssC3 = -ssA1 * ssA1;
	const double ssC1 = 1.0 - ssC2 - ssC3;

	// ------------------------------------------------------------ main loop
	for (int Index = sc.UpdateStartIndex; Index < sc.ArraySize; ++Index)
	{
		// ---- spectral dilation pre-filter (bandpass) or raw source
		if (useSpectral)
		{
			ProcSrc[Index] = (float)(0.5 * (1.0 - bpAlpha) * (Src[Index] - HistF(Src, Index - 2))
				+ bpBeta * (1.0 + bpAlpha) * HistF(ProcSrc, Index - 1)
				- bpAlpha * HistF(ProcSrc, Index - 2));
		}
		else
			ProcSrc[Index] = Src[Index];

		// ---- shared pre-filter: highpass(maxPeriod) then super smoother(10)
		PpHP[Index] = (float)(hpC1 * (ProcSrc[Index] - 2.0 * HistF(ProcSrc, Index - 1) + HistF(ProcSrc, Index - 2))
			+ hpC2 * HistF(PpHP, Index - 1) + hpC3 * HistF(PpHP, Index - 2));
		PpFilt[Index] = (float)(ssC1 * (PpHP[Index] + HistF(PpHP, Index - 1)) / 2.0
			+ ssC2 * HistF(PpFilt, Index - 1) + ssC3 * HistF(PpFilt, Index - 2));

		// ---- Hilbert Transform dominant cycle (per bar)
		HtSmooth[Index] = (4.0f * ProcSrc[Index] + 3.0f * HistF(ProcSrc, Index - 1)
			+ 2.0f * HistF(ProcSrc, Index - 2) + HistF(ProcSrc, Index - 3)) / 10.0f;

		if (Index > 5)
		{
			const double af = 0.075 * HistF(HtPeriod, Index - 1) + 0.54;

			HtDetrend[Index] = (float)((0.0962 * HtSmooth[Index] + 0.5769 * HistF(HtSmooth, Index - 2)
				- 0.5769 * HistF(HtSmooth, Index - 4) - 0.0962 * HistF(HtSmooth, Index - 6)) * af);

			HtQ1[Index] = (float)((0.0962 * HtDetrend[Index] + 0.5769 * HistF(HtDetrend, Index - 2)
				- 0.5769 * HistF(HtDetrend, Index - 4) - 0.0962 * HistF(HtDetrend, Index - 6)) * af);
			HtI1[Index] = HistF(HtDetrend, Index - 3);

			const double jI = (0.0962 * HtI1[Index] + 0.5769 * HistF(HtI1, Index - 2)
				- 0.5769 * HistF(HtI1, Index - 4) - 0.0962 * HistF(HtI1, Index - 6)) * af;
			const double jQ = (0.0962 * HtQ1[Index] + 0.5769 * HistF(HtQ1, Index - 2)
				- 0.5769 * HistF(HtQ1, Index - 4) - 0.0962 * HistF(HtQ1, Index - 6)) * af;

			double i2 = HtI1[Index] - jQ;
			double q2 = HtQ1[Index] + jI;
			i2 = 0.2 * i2 + 0.8 * HistF(HtI2, Index - 1);
			q2 = 0.2 * q2 + 0.8 * HistF(HtQ2, Index - 1);
			HtI2[Index] = (float)i2;
			HtQ2[Index] = (float)q2;

			double re = i2 * HistF(HtI2, Index - 1) + q2 * HistF(HtQ2, Index - 1);
			double im = i2 * HistF(HtQ2, Index - 1) - q2 * HistF(HtI2, Index - 1);
			re = 0.2 * re + 0.8 * HistF(HtRe, Index - 1);
			im = 0.2 * im + 0.8 * HistF(HtIm, Index - 1);
			HtRe[Index] = (float)re;
			HtIm[Index] = (float)im;

			double htP = HistF(HtPeriod, Index - 1);  // retained if no phase update
			if (im != 0.0 && re != 0.0)
				htP = 2.0 * M_PI / atan(im / re);

			// rate-of-change limiter, then clamp into [minPeriod, maxPeriod]
			const double prevP = HistF(HtPeriod, Index - 1);
			if (htP > 1.5 * prevP)
				htP = 1.5 * prevP;
			if (htP < 0.67 * prevP)
				htP = 0.67 * prevP;
			htP = max((double)minPeriod, min((double)maxPeriod, htP));
			HtPeriod[Index] = (float)htP;
			HtSmPeriod[Index] = (float)(0.33 * htP + 0.67 * HistF(HtSmPeriod, Index - 1));
		}
		else
		{
			// bars 0..5: Pine leaves all hilbert state at its prior value (0.0 init)
			HtDetrend[Index] = HistF(HtDetrend, Index - 1);
			HtI1[Index] = HistF(HtI1, Index - 1);
			HtQ1[Index] = HistF(HtQ1, Index - 1);
			HtI2[Index] = HistF(HtI2, Index - 1);
			HtQ2[Index] = HistF(HtQ2, Index - 1);
			HtRe[Index] = HistF(HtRe, Index - 1);
			HtIm[Index] = HistF(HtIm, Index - 1);
			HtPeriod[Index] = HistF(HtPeriod, Index - 1);
			HtSmPeriod[Index] = HistF(HtSmPeriod, Index - 1);
		}
		const float hilbertPeriod = HtSmPeriod[Index];

		// ---- throttle for the heavy spectral scans (Pine: bar_index % N)
		const bool doUpdate = updateInterval <= 1 || (Index % updateInterval == 0);

		// ---- Autocorrelation Periodogram (Pearson correlation per lag)
		if (doUpdate)
		{
			float maxCorr = 0.0f;
			int dominantPeriod = minPeriod;
			for (int lag = minPeriod; lag <= maxPeriod; ++lag)
			{
				const int n = maxPeriod - lag;
				double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
				for (int i = 0; i < n; ++i)
				{
					const double x = HistF(PpFilt, Index - i);
					const double y = HistF(PpFilt, Index - i - lag);
					sx += x;  sy += y;
					sxx += x * x;  syy += y * y;  sxy += x * y;
				}
				const double denom = sqrt((n * sxx - sx * sx) * (n * syy - sy * sy));
				const double r = denom != 0.0 ? (n * sxy - sx * sy) / denom : 0.0;
				if (r > maxCorr && r > 0.2)  // minimum correlation threshold
				{
					maxCorr = (float)r;
					dominantPeriod = lag;
				}
			}
			AcDomPeriod[Index] = (float)dominantPeriod;
			AcMaxCorr[Index] = maxCorr;
		}
		else
		{
			AcDomPeriod[Index] = Index > 0 ? AcDomPeriod[Index - 1] : (float)minPeriod;
			AcMaxCorr[Index] = HistF(AcMaxCorr, Index - 1);
		}
		AcSmPeriod[Index] = 0.2f * AcDomPeriod[Index] + 0.8f * HistF(AcSmPeriod, Index - 1);
		const float acPeriod = AcSmPeriod[Index];
		const float acConfidence = AcMaxCorr[Index];

		// ---- Goertzel DFT power scan (newest-first buffer, zero-padded)
		if (doUpdate)
		{
			double maxPower = 0.0;
			int domP = minPeriod;
			for (int period = minPeriod; period <= maxPeriod; ++period)
			{
				const double coeff = 2.0 * cos(2.0 * M_PI / period);
				double s1 = 0.0, s2 = 0.0;
				for (int i = 0; i < dftBufSize; ++i)
				{
					const double s0 = HistF(PpFilt, Index - i) + coeff * s1 - s2;
					s2 = s1;
					s1 = s0;
				}
				const double pwr = s1 * s1 + s2 * s2 - coeff * s1 * s2;
				if (pwr > maxPower)
				{
					maxPower = pwr;
					domP = period;
				}
			}
			DftDomPeriod[Index] = (float)domP;
		}
		else
			DftDomPeriod[Index] = Index > 0 ? DftDomPeriod[Index - 1] : (float)minPeriod;
		DftSmPeriod[Index] = 0.2f * DftDomPeriod[Index] + 0.8f * HistF(DftSmPeriod, Index - 1);
		const float dftPeriod = DftSmPeriod[Index];

		// ---- multiple-confirmation logic
		const float avgPeriod = (hilbertPeriod + acPeriod + dftPeriod) / 3.0f;
		const float hilbertDev = avgPeriod != 0 ? fabsf(hilbertPeriod - avgPeriod) / avgPeriod : 0.0f;
		const float acDev      = avgPeriod != 0 ? fabsf(acPeriod - avgPeriod) / avgPeriod : 0.0f;
		const float dftDev     = avgPeriod != 0 ? fabsf(dftPeriod - avgPeriod) / avgPeriod : 0.0f;

		int agreementCount = 0;
		if (hilbertDev <= confirmTol) ++agreementCount;
		if (acDev <= confirmTol)      ++agreementCount;
		if (dftDev <= confirmTol)     ++agreementCount;

		const float hilbertWeight = hilbertDev <= confirmTol ? 1.0f : 0.5f;
		const float acWeight = acConfidence * (acDev <= confirmTol ? 1.5f : 0.75f);
		const float dftWeight = dftDev <= confirmTol ? 1.0f : 0.5f;
		const float totalWeight = hilbertWeight + acWeight + dftWeight;
		const float weightedPeriod = hilbertPeriod * hilbertWeight + acPeriod * acWeight + dftPeriod * dftWeight;
		const float confirmedPeriod = totalWeight > 0 ? weightedPeriod / totalWeight : avgPeriod;

		float finalP = 0.15f * confirmedPeriod + 0.85f * HistF(FinalPeriod, Index - 1);
		finalP = max((float)minPeriod, min((float)maxPeriod, finalP));
		FinalPeriod[Index] = finalP;

		const float cycleConfidence = (agreementCount / 3.0f) * 100.0f;

		// ---- adaptive cycle RSI (EMA-alpha per the Pine source, or Wilder)
		const int rsiPeriod = max(2, IRound(finalP * rsiMult));
		const float change = Src[Index] - HistF(Src, Index - 1);
		const float gain = change > 0 ? change : 0.0f;
		const float loss = change < 0 ? -change : 0.0f;
		const float alphaRSI = wilderRsi ? 1.0f / rsiPeriod : 2.0f / (rsiPeriod + 1);
		AvgGain[Index] = alphaRSI * gain + (1.0f - alphaRSI) * HistF(AvgGain, Index - 1);
		AvgLoss[Index] = alphaRSI * loss + (1.0f - alphaRSI) * HistF(AvgLoss, Index - 1);
		const float cycleRSI = AvgLoss[Index] == 0 ? 100.0f
			: AvgGain[Index] == 0 ? 0.0f
			: 100.0f - (100.0f / (1.0f + AvgGain[Index] / AvgLoss[Index]));
		CycleRsi[Index] = cycleRSI;

		// ---- stochastic of RSI
		// The Pine loop ran i = 1..maxPeriod using only i < stochPeriod, which
		// silently capped the lookback at maxPeriod bars. Optional via input.
		const int stochPeriod = max(5, IRound(finalP * stochMult));
		float highestRSI = cycleRSI;
		float lowestRSI = cycleRSI;
		const int stochLookback = capStochLookback ? min(stochPeriod - 1, maxPeriod) : stochPeriod - 1;
		for (int i = 1; i <= stochLookback; ++i)
		{
			const float v = HistF(CycleRsi, Index - i);  // nz(): pre-chart history = 0
			if (v > highestRSI) highestRSI = v;
			if (v < lowestRSI)  lowestRSI = v;
		}
		RawK[Index] = highestRSI - lowestRSI != 0
			? 100.0f * (cycleRSI - lowestRSI) / (highestRSI - lowestRSI) : 50.0f;

		// ---- K and D smoothing with the selectable MA
		MaAt(kMaType, RawK, SgK.Data, KE1, KE2, KE3, Index, kSmoothLen, almaOffset, almaSigma);
		MaAt(dMaType, SgK.Data, SgD.Data, DE1, DE2, DE3, Index, dSmoothLen, almaOffset, almaSigma);
		const float kLine = SgK[Index];
		const float dLine = SgD[Index];

		// ---- Modern per-bar direction color (Classic keeps fixed colors)
		const float kPrev = Index > 0 ? SgK[Index - 1] : kLine;  // nz(kLine[1], kLine)
		SgK.DataColor[Index] = isModern ? (kLine >= kPrev ? upCol : downCol) : upCol;
		SgD.DataColor[Index] = isModern ? BlendColor(grayCol, fadeTarget, 15) : downCol;

		// ---- cross signals, filtered by zone threshold and optional
		// minimum confidence (defaults 30/70/off = the Pine alert conditions)
		const float dPrev = Index > 0 ? SgD[Index - 1] : dLine;
		const bool confOK = minSignalConf <= 0 || cycleConfidence >= (float)minSignalConf;
		const bool bullCross = kLine > dLine && kPrev <= dPrev && kLine < bullThreshold && confOK;
		const bool bearCross = kLine < dLine && kPrev >= dPrev && kLine > bearThreshold && confOK;
		SgBullSignal[Index] = bullCross ? max(kLine - 8.0f, 1.0f) : 0.0f;
		SgBearSignal[Index] = bearCross ? min(kLine + 8.0f, 99.0f) : 0.0f;

		// ---- histogram (K - D) centered on the 50 midline
		const float hist = kLine - dLine;
		Hist[Index] = hist;
		float histMaxAbs = 0.0f;
		const int histAvail = min(histNormLen, Index + 1);
		for (int i = 0; i < histAvail; ++i)
		{
			const float a = fabsf(Hist[Index - i]);
			if (a > histMaxAbs) histMaxAbs = a;
		}
		const float histNorm = histMaxAbs > 0 ? hist / histMaxAbs : 0.0f;
		// Pine hides the histogram until the normalization window fills (ta.highest = na)
		const float histDisp = (Index + 1 < histNormLen) ? 50.0f : 50.0f + histNorm * 30.0f;
		SgHistTop[Index] = max(histDisp, 50.0f);
		SgHistBot[Index] = min(histDisp, 50.0f);
		const bool histUp = hist >= 0;
		const float histPrev = Index > 0 ? Hist[Index - 1] : hist;  // nz(hist[1], hist)
		const bool histGrow = hist >= histPrev;
		const COLORREF histCol = histUp
			? BlendColor(upCol, fadeTarget, histGrow ? histTransp : histFade)
			: BlendColor(downCol, fadeTarget, histGrow ? histFade : histTransp);
		SgHistTop.DataColor[Index] = histCol;
		SgHistBot.DataColor[Index] = histCol;

		// ---- reference lines and OB/OS zone fills
		SgOB[Index] = 80.0f;
		SgMid[Index] = 50.0f;
		SgOS[Index] = 20.0f;
		SgZoneTopHi[Index] = 100.0f;
		SgZoneTopLo[Index] = 80.0f;
		SgZoneBotHi[Index] = 20.0f;
		SgZoneBotLo[Index] = 0.0f;

		// ---- confidence background
		SgBackground[Index] = 1.0f;
		SgBackground.DataColor[Index] = cycleConfidence >= 66.0f ? BlendColor(RGB(0x08, 0x99, 0x81), fadeTarget, 90)
			: cycleConfidence >= 33.0f ? BlendColor(RGB(0xFD, 0xD8, 0x35), fadeTarget, 92)
			: BlendColor(RGB(0xF2, 0x36, 0x45), fadeTarget, 92);

		// ---- data-window values (TV display.data_window plots)
		SgFinalPeriod[Index] = finalP;
		SgHilbert[Index] = hilbertPeriod;
		SgAutocorr[Index] = acPeriod;
		SgDft[Index] = dftPeriod;
		SgConfidence[Index] = cycleConfidence;
		SgRsiPeriod[Index] = (float)rsiPeriod;
		SgStochPeriod[Index] = (float)stochPeriod;
		SgRawRsi[Index] = cycleRSI;
		SgHistValue[Index] = hist;
		SgAcCorrPct[Index] = acConfidence * 100.0f;
	}

	// ------------------------------------------------------------ info table
	const int TextDrawingLineNumber = 87001;
	int& tableShown = sc.GetPersistentInt(10);
	if (showCycleInfo && sc.ArraySize > 0)
	{
		const int last = sc.ArraySize - 1;
		const float avgP = (SgHilbert[last] + SgAutocorr[last] + SgDft[last]) / 3.0f;
		auto dev = [&](float p) { return avgP != 0 ? fabsf(p - avgP) / avgP : 0.0f; };

		SCString txt;
		txt.Format("CYCLE  PERIOD\nHilbert   %5.1f %s\nAutocorr  %5.1f %s\nDFT       %5.1f %s\n"
			"CONFIRMED %5.1f\nConfidence %d%%",
			SgHilbert[last], dev(SgHilbert[last]) <= confirmTol ? "+" : "-",
			SgAutocorr[last], dev(SgAutocorr[last]) <= confirmTol ? "+" : "-",
			SgDft[last], dev(SgDft[last]) <= confirmTol ? "+" : "-",
			SgFinalPeriod[last], (int)(SgConfidence[last] + 0.5f));

		s_UseTool Tool;
		Tool.Clear();
		Tool.ChartNumber = sc.ChartNumber;
		Tool.DrawingType = DRAWING_TEXT;
		Tool.LineNumber = TextDrawingLineNumber;
		Tool.Region = sc.GraphRegion;
		Tool.AddMethod = UTAM_ADD_OR_ADJUST;
		Tool.UseRelativeVerticalValues = 1;
		Tool.BeginValue = 98;  // top of the study region
		Tool.BeginDateTime = sc.BaseDateTimeIn[last];
		Tool.Color = RGB(255, 255, 255);
		Tool.FontBackColor = RGB(20, 20, 20);
		Tool.FontSize = 8;
		Tool.FontBold = 0;
		Tool.TextAlignment = DT_RIGHT | DT_TOP;
		Tool.Text = txt;
		sc.UseTool(Tool);
		tableShown = 1;
	}
	else if (tableShown)
	{
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, TextDrawingLineNumber);
		tableShown = 0;
	}

	// ------------------------------------------------------------ alerts
	// The Pine alertconditions. Timing: Bar Close = evaluate the last closed
	// bar (TV "Once Per Bar Close"); Intrabar = evaluate the developing bar,
	// firing at most once per bar on the first occurrence (the condition can
	// appear intrabar and be gone again by the close).
	if (InEnableAlerts.GetYesNo() && sc.ArraySize >= 3)
	{
		const bool intrabar = InAlertTiming.GetIndex() == 1;
		const int j = intrabar ? sc.ArraySize - 1 : sc.ArraySize - 2;
		const int bullNum = InBullAlertNum.GetInt();
		const int bearNum = InBearAlertNum.GetInt();
		const int confNum = InConfAlertNum.GetInt();

		int& lastBull = sc.GetPersistentInt(1);
		int& lastBear = sc.GetPersistentInt(2);
		int& lastConf = sc.GetPersistentInt(3);

		// The signal subgraphs already encode the fully filtered cross
		// conditions (zone thresholds + confidence gate), so the alerts reuse
		// them and can never disagree with the arrows.
		if (bullNum > 0 && SgBullSignal[j] > 0.0f && lastBull != j)
		{
			sc.SetAlert(bullNum, "Ehlers Cycle StochRSI: K crossed above D in oversold zone");
			lastBull = j;
		}
		if (bearNum > 0 && SgBearSignal[j] > 0.0f && lastBear != j)
		{
			sc.SetAlert(bearNum, "Ehlers Cycle StochRSI: K crossed below D in overbought zone");
			lastBear = j;
		}
		if (confNum > 0 && SgConfidence[j] >= 66.0f && SgConfidence[j - 1] < 66.0f && lastConf != j)
		{
			sc.SetAlert(confNum, "Ehlers Cycle StochRSI: All three cycle detection methods agree");
			lastConf = j;
		}
	}
}
