// SqueezeChannel.cpp — version 1.1.1 (see CHANGELOG.md)
//
// Sierra Chart ACSIL port of the TradingView Pine Script v6 study "Squeeze Channel"
// (source: squeezechannel.pine in this repository).
// Documentation: USER_MANUAL.md (usage) and CONVERSION.md (porting/parity notes).
//
// Behavior summary (mirrors the Pine source):
//   - Squeeze = Bollinger Bands (SMA + population stdev) fully inside a Keltner
//     Channel (SMA basis + Wilder-ATR range).
//   - On squeeze entry a swing channel is seeded from the highest high / lowest
//     low of the last "Swing Lookback Bars", then expanded for the next
//     "Swing Lookforward Bars", then locked.
//   - Close beyond the locked channel => breakout signal; the channel is retired.
//   - After a breakout, the opposite channel extreme is watched for
//     "Reversal Watch Bars"; a close through it => failed-breakout reversal.
//   - The bar after a breakout, both channel extremes are stored as extended
//     S/R levels for "Extended Level Bars". Optionally (off by default, as in
//     Pine) closes through / back through those levels give retest signals.
//   - Channel coloring follows squeeze strength through a 5-color gradient
//     while the squeeze holds, and switches to the "Squeeze Ended" color once
//     the squeeze releases while the channel is still active.
//
// Parity and porting notes are documented in CONVERSION.md next to this file.
//
// Calculation fidelity:
//   - ta.stdev  -> population standard deviation (Pine default biased=true).
//   - ta.atr    -> Wilder RMA of true range, seeded with the SMA of the first
//                  <length> true ranges (undefined before bar length-1), exactly
//                  like Pine ta.rma(ta.tr(true), length).
//   - Pine "var" state is stored per bar in subgraph extra arrays, and each bar
//     is always recomputed from the previous bar's stored state. This gives the
//     same rollback behavior as Pine on the developing (real-time) bar.
//   - The extended S/R arrays (Pine array.* usage) are kept in persistent
//     std::vectors. Mutations are committed exactly once per closed bar; the
//     developing bar is evaluated against a temporary copy on every update, so
//     real-time recalculation can never double-apply list changes.
//
// Enhancements over the Pine source (approved 2026-07-03, see CONVERSION.md):
//   - Reversal watch window starts the bar AFTER the breakout, so exactly
//     "Reversal Watch Bars" bars are checked ("Legacy (Pine) Reversal Window
//     Timing" input restores the original behavior).
//   - Extended level plot slots show the 5 levels nearest to price instead of
//     the 5 oldest ("Plot Nearest Extended Levels" input, No = Pine behavior).
//   - Optional "Squeeze Re-Entry Resets Active Channel" = No keeps an active
//     channel until it resolves via breakout (Yes = Pine behavior, default).
//   - Optional unbiased (sample) standard deviation for the Bollinger Bands.
//   - Alert messages use the "Squeeze Channel:" prefix (source used "EDRF:").
//   - Extended S/R levels default to the DASH draw style (Pine used circles).
//   - Hidden "Squeeze On" / "Squeeze Strength" subgraphs expose squeeze state
//     to Alert Manager conditions, spreadsheets, and other studies.

#include "sierrachart.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

SCDLLName("Squeeze Channel")

// ------------------------------------------------------------------
// Subgraph layout
// ------------------------------------------------------------------
enum SqueezeChannelSubgraphs
{
	SG_CHANNEL_HIGH = 0,
	SG_CHANNEL_LOW,
	SG_CHANNEL_MID,
	SG_CHANNEL_FILL_TOP,     // transparent fill pair (kept adjacent)
	SG_CHANNEL_FILL_BOTTOM,
	SG_REVERSAL_LEVEL,
	SG_BULL_BREAKOUT,
	SG_BEAR_BREAKOUT,
	SG_BULL_REVERSAL,
	SG_BEAR_REVERSAL,
	SG_EXT_BULL_BREAKOUT,
	SG_EXT_BEAR_BREAKOUT,
	SG_EXT_BULL_REVERSAL,
	SG_EXT_BEAR_REVERSAL,
	SG_EXT_RESISTANCE_1,     // 5 contiguous resistance slots
	SG_EXT_RESISTANCE_2,
	SG_EXT_RESISTANCE_3,
	SG_EXT_RESISTANCE_4,
	SG_EXT_RESISTANCE_5,
	SG_EXT_SUPPORT_1,        // 5 contiguous support slots
	SG_EXT_SUPPORT_2,
	SG_EXT_SUPPORT_3,
	SG_EXT_SUPPORT_4,
	SG_EXT_SUPPORT_5,
	SG_SQUEEZE_ON,           // hidden data output: 1 = squeeze active
	SG_SQUEEZE_STRENGTH,     // hidden data output: 0..1 squeeze strength
	SG_COUNT
};

// ------------------------------------------------------------------
// Input layout (order and defaults mirror the Pine inputs)
// ------------------------------------------------------------------
enum SqueezeChannelInputs
{
	IN_BB_LENGTH = 0,
	IN_BB_MULT,
	IN_KC_LENGTH,
	IN_KC_MULT,
	IN_SWING_LOOKBACK,
	IN_SWING_LOOKFWD,
	IN_ENABLE_REVERSAL,
	IN_REVERSAL_BARS,
	IN_ENABLE_EXTENDED_LEVELS,
	IN_ENABLE_SR_RETEST,
	IN_EXTENDED_BARS,
	IN_SR_DELAY_BARS,
	IN_ATR_PROXIMITY,
	IN_ATR_LENGTH,
	IN_SHOW_CHANNEL,
	IN_SHOW_CHANNEL_FILL,
	IN_COLOR_WAITING,
	IN_SQ_COLOR_1,
	IN_SQ_COLOR_2,
	IN_SQ_COLOR_3,
	IN_SQ_COLOR_4,
	IN_SQ_COLOR_5,
	IN_MARKER_OFFSET_MULT,   // platform adaptation (Pine places shapes below/above bars automatically)
	IN_ENABLE_ALERTS,
	IN_ALERTS_ON_CLOSE,
	IN_LEGACY_REVERSAL_WINDOW,
	IN_PLOT_NEAREST_EXT_LEVELS,
	IN_REENTRY_RESETS_CHANNEL,
	IN_UNBIASED_STDEV,
	IN_COUNT
};

// Persistent storage keys
static const int PERSIST_KEY_STATE_POINTER   = 1;
static const int PERSIST_KEY_NEXT_TO_COMMIT  = 2;  // first bar index whose S/R list mutations are NOT yet committed
static const int PERSIST_KEY_LAST_ALERT_BAR  = 3;

// Alert numbers (Sierra Chart alert sound numbers; messages mirror the Pine alertconditions)
static const int ALERT_BULL_BREAKOUT     = 1;
static const int ALERT_BEAR_BREAKOUT     = 2;
static const int ALERT_BULL_REVERSAL     = 3;
static const int ALERT_BEAR_REVERSAL     = 4;
static const int ALERT_EXT_BULL_BREAKOUT = 5;
static const int ALERT_EXT_BEAR_BREAKOUT = 6;
static const int ALERT_EXT_BULL_REVERSAL = 7;
static const int ALERT_EXT_BEAR_REVERSAL = 8;
static const int ALERT_SQUEEZE_STARTED   = 9;

struct s_SqueezeSRLevel
{
	float Price;
	int   StartBarIndex;   // bar the level was stored on
	int   ExpiryBarIndex;  // StartBarIndex + Extended Level Bars
	bool  Broken;
};

struct s_SqueezeChannelPersist
{
	std::vector<s_SqueezeSRLevel> HighLevels;  // resistance (channel highs)
	std::vector<s_SqueezeSRLevel> LowLevels;   // support (channel lows)
};

static inline double SqcMax(double A, double B) { return A > B ? A : B; }
static inline double SqcMin(double A, double B) { return A < B ? A : B; }

SCSFExport scsf_SqueezeChannel(SCStudyInterfaceRef sc)
{
	SCSubgraphRef ChannelHigh    = sc.Subgraph[SG_CHANNEL_HIGH];
	SCSubgraphRef ChannelLow     = sc.Subgraph[SG_CHANNEL_LOW];
	SCSubgraphRef ChannelMid     = sc.Subgraph[SG_CHANNEL_MID];
	SCSubgraphRef FillTop        = sc.Subgraph[SG_CHANNEL_FILL_TOP];
	SCSubgraphRef FillBottom     = sc.Subgraph[SG_CHANNEL_FILL_BOTTOM];
	SCSubgraphRef ReversalLevel  = sc.Subgraph[SG_REVERSAL_LEVEL];
	SCSubgraphRef BullBreakout   = sc.Subgraph[SG_BULL_BREAKOUT];
	SCSubgraphRef BearBreakout   = sc.Subgraph[SG_BEAR_BREAKOUT];
	SCSubgraphRef BullReversal   = sc.Subgraph[SG_BULL_REVERSAL];
	SCSubgraphRef BearReversal   = sc.Subgraph[SG_BEAR_REVERSAL];
	SCSubgraphRef ExtBullBrk     = sc.Subgraph[SG_EXT_BULL_BREAKOUT];
	SCSubgraphRef ExtBearBrk     = sc.Subgraph[SG_EXT_BEAR_BREAKOUT];
	SCSubgraphRef ExtBullRev     = sc.Subgraph[SG_EXT_BULL_REVERSAL];
	SCSubgraphRef ExtBearRev     = sc.Subgraph[SG_EXT_BEAR_REVERSAL];
	SCSubgraphRef SqueezeOnSG    = sc.Subgraph[SG_SQUEEZE_ON];
	SCSubgraphRef SqueezeStrSG   = sc.Subgraph[SG_SQUEEZE_STRENGTH];

	SCInputRef InBBLength        = sc.Input[IN_BB_LENGTH];
	SCInputRef InBBMult          = sc.Input[IN_BB_MULT];
	SCInputRef InKCLength        = sc.Input[IN_KC_LENGTH];
	SCInputRef InKCMult          = sc.Input[IN_KC_MULT];
	SCInputRef InSwingLookback   = sc.Input[IN_SWING_LOOKBACK];
	SCInputRef InSwingLookfwd    = sc.Input[IN_SWING_LOOKFWD];
	SCInputRef InEnableReversal  = sc.Input[IN_ENABLE_REVERSAL];
	SCInputRef InReversalBars    = sc.Input[IN_REVERSAL_BARS];
	SCInputRef InEnableExtLevels = sc.Input[IN_ENABLE_EXTENDED_LEVELS];
	SCInputRef InEnableSRRetest  = sc.Input[IN_ENABLE_SR_RETEST];
	SCInputRef InExtendedBars    = sc.Input[IN_EXTENDED_BARS];
	SCInputRef InSRDelayBars     = sc.Input[IN_SR_DELAY_BARS];
	SCInputRef InATRProximity    = sc.Input[IN_ATR_PROXIMITY];
	SCInputRef InATRLength       = sc.Input[IN_ATR_LENGTH];
	SCInputRef InShowChannel     = sc.Input[IN_SHOW_CHANNEL];
	SCInputRef InShowFill        = sc.Input[IN_SHOW_CHANNEL_FILL];
	SCInputRef InColorWaiting    = sc.Input[IN_COLOR_WAITING];
	SCInputRef InSqColor1        = sc.Input[IN_SQ_COLOR_1];
	SCInputRef InSqColor2        = sc.Input[IN_SQ_COLOR_2];
	SCInputRef InSqColor3        = sc.Input[IN_SQ_COLOR_3];
	SCInputRef InSqColor4        = sc.Input[IN_SQ_COLOR_4];
	SCInputRef InSqColor5        = sc.Input[IN_SQ_COLOR_5];
	SCInputRef InMarkerOffset    = sc.Input[IN_MARKER_OFFSET_MULT];
	SCInputRef InEnableAlerts    = sc.Input[IN_ENABLE_ALERTS];
	SCInputRef InAlertsOnClose   = sc.Input[IN_ALERTS_ON_CLOSE];
	SCInputRef InLegacyRevWindow = sc.Input[IN_LEGACY_REVERSAL_WINDOW];
	SCInputRef InPlotNearestExt  = sc.Input[IN_PLOT_NEAREST_EXT_LEVELS];
	SCInputRef InReentryResets   = sc.Input[IN_REENTRY_RESETS_CHANNEL];
	SCInputRef InUnbiasedStdev   = sc.Input[IN_UNBIASED_STDEV];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Squeeze Channel";
		sc.StudyDescription = "Port of the TradingView 'Squeeze Channel' study. Detects Bollinger-inside-Keltner "
			"squeezes, builds a swing channel at squeeze entry, then signals channel breakouts, failed-breakout "
			"reversals and extended S/R retests. See CONVERSION.md in the source repository for parity notes.";

		sc.AutoLoop = 0;                       // manual looping; per-bar state machine below
		sc.GraphRegion = 0;                    // overlay on the price graph (Pine overlay=true)
		sc.ValueFormat = VALUEFORMAT_INHERITED;
		sc.ScaleRangeType = SCALE_SAMEASREGION;
		sc.AlertOnlyOncePerBar = 1;
		// Channel fill opacity: current Sierra Chart versions control this with
		// the per-study "Transparency Level for Fill Styles" setting on the
		// Subgraphs tab (~85 matches the Pine fill). The former ACSIL member
		// sc.TransparencyLevel is retired (TransparencyLevel_Unused).

		// --- channel plots (Pine plot.style_circles -> point draw style) ---
		ChannelHigh.Name = "Channel High";
		ChannelHigh.DrawStyle = DRAWSTYLE_POINT;
		ChannelHigh.LineWidth = 2;
		ChannelHigh.PrimaryColor = RGB(224, 64, 251);   // color.fuchsia #E040FB (waiting color)
		ChannelHigh.DrawZeros = false;

		ChannelLow.Name = "Channel Low";
		ChannelLow.DrawStyle = DRAWSTYLE_POINT;
		ChannelLow.LineWidth = 2;
		ChannelLow.PrimaryColor = RGB(224, 64, 251);
		ChannelLow.DrawZeros = false;

		ChannelMid.Name = "Channel Mid";
		ChannelMid.DrawStyle = DRAWSTYLE_POINT;
		ChannelMid.LineWidth = 1;
		ChannelMid.PrimaryColor = RGB(120, 123, 134);   // color.gray #787B86
		ChannelMid.DrawZeros = false;

		FillTop.Name = "Channel Fill Top";
		FillTop.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_TOP;
		FillTop.PrimaryColor = RGB(224, 64, 251);
		FillTop.DrawZeros = false;

		FillBottom.Name = "Channel Fill Bottom";
		FillBottom.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_BOTTOM;
		FillBottom.PrimaryColor = RGB(224, 64, 251);
		FillBottom.DrawZeros = false;

		ReversalLevel.Name = "Reversal Level";
		ReversalLevel.DrawStyle = DRAWSTYLE_POINT;
		ReversalLevel.LineWidth = 2;
		ReversalLevel.PrimaryColor = RGB(120, 123, 134);
		ReversalLevel.DrawZeros = false;

		// --- signal markers ---
		BullBreakout.Name = "Bull Breakout";
		BullBreakout.DrawStyle = DRAWSTYLE_TRIANGLE_UP;
		BullBreakout.LineWidth = 3;
		BullBreakout.PrimaryColor = RGB(8, 153, 129);   // color.teal #089981
		BullBreakout.DrawZeros = false;

		BearBreakout.Name = "Bear Breakout";
		BearBreakout.DrawStyle = DRAWSTYLE_TRIANGLE_DOWN;
		BearBreakout.LineWidth = 3;
		BearBreakout.PrimaryColor = RGB(242, 54, 69);   // color.red #F23645
		BearBreakout.DrawZeros = false;

		BullReversal.Name = "Bull Reversal";
		BullReversal.DrawStyle = DRAWSTYLE_DIAMOND;
		BullReversal.LineWidth = 4;
		BullReversal.PrimaryColor = RGB(0, 255, 255);   // #00FFFF
		BullReversal.DrawZeros = false;

		BearReversal.Name = "Bear Reversal";
		BearReversal.DrawStyle = DRAWSTYLE_DIAMOND;
		BearReversal.LineWidth = 4;
		BearReversal.PrimaryColor = RGB(255, 0, 255);   // #FF00FF
		BearReversal.DrawZeros = false;

		// Pine drew the extended signals with extra transparency; approximated
		// here with dimmed defaults (user adjustable in the Subgraphs tab).
		ExtBullBrk.Name = "Ext Bull Breakout";
		ExtBullBrk.DrawStyle = DRAWSTYLE_TRIANGLE_UP;
		ExtBullBrk.LineWidth = 3;
		ExtBullBrk.PrimaryColor = RGB(6, 107, 90);
		ExtBullBrk.DrawZeros = false;

		ExtBearBrk.Name = "Ext Bear Breakout";
		ExtBearBrk.DrawStyle = DRAWSTYLE_TRIANGLE_DOWN;
		ExtBearBrk.LineWidth = 3;
		ExtBearBrk.PrimaryColor = RGB(169, 38, 48);
		ExtBearBrk.DrawZeros = false;

		ExtBullRev.Name = "Ext Bull Reversal";
		ExtBullRev.DrawStyle = DRAWSTYLE_DIAMOND;
		ExtBullRev.LineWidth = 4;
		ExtBullRev.PrimaryColor = RGB(0, 179, 179);
		ExtBullRev.DrawZeros = false;

		ExtBearRev.Name = "Ext Bear Reversal";
		ExtBearRev.DrawStyle = DRAWSTYLE_DIAMOND;
		ExtBearRev.LineWidth = 4;
		ExtBearRev.PrimaryColor = RGB(179, 0, 179);
		ExtBearRev.DrawZeros = false;

		// --- extended S/R level plots ---
		for (int k = 0; k < 5; k++)
		{
			// DASH instead of Pine's circles: approved enhancement, reads as an
			// S/R level on Sierra charts. Adjustable per subgraph.
			SCSubgraphRef Res = sc.Subgraph[SG_EXT_RESISTANCE_1 + k];
			SCString ResName;
			ResName.Format("Ext Resistance %d", k + 1);
			Res.Name = ResName;
			Res.DrawStyle = DRAWSTYLE_DASH;
			Res.LineWidth = 1;
			Res.PrimaryColor = RGB(145, 32, 41);        // color.red dimmed (Pine transparency 40)
			Res.DrawZeros = false;

			SCSubgraphRef Sup = sc.Subgraph[SG_EXT_SUPPORT_1 + k];
			SCString SupName;
			SupName.Format("Ext Support %d", k + 1);
			Sup.Name = SupName;
			Sup.DrawStyle = DRAWSTYLE_DASH;
			Sup.LineWidth = 1;
			Sup.PrimaryColor = RGB(5, 92, 77);          // color.teal dimmed (Pine transparency 40)
			Sup.DrawZeros = false;
		}

		// --- hidden data outputs (for alerts, spreadsheets, other studies) ---
		SqueezeOnSG.Name = "Squeeze On";
		SqueezeOnSG.DrawStyle = DRAWSTYLE_IGNORE;
		SqueezeOnSG.DrawZeros = false;

		SqueezeStrSG.Name = "Squeeze Strength";
		SqueezeStrSG.DrawStyle = DRAWSTYLE_IGNORE;
		SqueezeStrSG.DrawZeros = false;

		// --- inputs (names, defaults and limits mirror the Pine inputs) ---
		InBBLength.Name = "Bollinger Length";
		InBBLength.SetInt(12);
		InBBLength.SetIntLimits(1, 10000);

		InBBMult.Name = "Bollinger Multiplier";
		InBBMult.SetFloat(2.0f);
		InBBMult.SetFloatLimits(0.1f, 100.0f);

		InKCLength.Name = "Keltner Length";
		InKCLength.SetInt(20);
		InKCLength.SetIntLimits(1, 10000);

		InKCMult.Name = "Keltner ATR Multiplier";
		InKCMult.SetFloat(2.0f);
		InKCMult.SetFloatLimits(0.1f, 100.0f);

		InSwingLookback.Name = "Swing Lookback Bars";
		InSwingLookback.SetInt(5);
		InSwingLookback.SetIntLimits(1, 10000);

		InSwingLookfwd.Name = "Swing Lookforward Bars";
		InSwingLookfwd.SetInt(3);
		InSwingLookfwd.SetIntLimits(1, 10000);

		InEnableReversal.Name = "Enable Failed Breakout Reversal";
		InEnableReversal.SetYesNo(1);

		InReversalBars.Name = "Reversal Watch Bars";
		InReversalBars.SetInt(7);
		InReversalBars.SetIntLimits(1, 10000);

		InEnableExtLevels.Name = "Show Extended S/R Levels";
		InEnableExtLevels.SetYesNo(1);

		InEnableSRRetest.Name = "Enable S/R Retest Signals";
		InEnableSRRetest.SetYesNo(0);

		InExtendedBars.Name = "Extended Level Bars";
		InExtendedBars.SetInt(75);
		InExtendedBars.SetIntLimits(10, 200);

		InSRDelayBars.Name = "S/R Signal Delay Bars";
		InSRDelayBars.SetInt(5);
		InSRDelayBars.SetIntLimits(1, 20);

		InATRProximity.Name = "ATR Proximity Threshold";
		InATRProximity.SetFloat(1.5f);
		InATRProximity.SetFloatLimits(0.1f, 100.0f);

		InATRLength.Name = "ATR Length";
		InATRLength.SetInt(14);
		InATRLength.SetIntLimits(1, 10000);

		InShowChannel.Name = "Show Swing Channel";
		InShowChannel.SetYesNo(1);

		InShowFill.Name = "Show Channel Fill";
		InShowFill.SetYesNo(1);

		InColorWaiting.Name = "Channel Color (Squeeze Ended)";
		InColorWaiting.SetColor(RGB(224, 64, 251));     // color.fuchsia

		InSqColor1.Name = "Squeeze 1 (Weak)";
		InSqColor1.SetColor(RGB(255, 255, 0));          // #FFFF00

		InSqColor2.Name = "Squeeze 2";
		InSqColor2.SetColor(RGB(255, 204, 0));          // #FFCC00

		InSqColor3.Name = "Squeeze 3 (Medium)";
		InSqColor3.SetColor(RGB(255, 153, 0));          // #FF9900

		InSqColor4.Name = "Squeeze 4";
		InSqColor4.SetColor(RGB(255, 85, 0));           // #FF5500

		InSqColor5.Name = "Squeeze 5 (Strong)";
		InSqColor5.SetColor(RGB(255, 0, 0));            // #FF0000

		InMarkerOffset.Name = "Signal Marker Offset (ATR Multiple)";
		InMarkerOffset.SetFloat(0.25f);
		InMarkerOffset.SetFloatLimits(0.0f, 10.0f);
		InMarkerOffset.SetDescription("Vertical distance of signal markers from the bar high/low, as a multiple of "
			"the S/R ATR. Pine positioned shapes automatically; Sierra Chart needs an explicit price offset.");

		InEnableAlerts.Name = "Enable Alerts";
		InEnableAlerts.SetYesNo(0);
		InEnableAlerts.SetDescription("Send Sierra Chart alerts (alert numbers 1-9) for the same conditions the Pine "
			"script exposed through alertcondition().");

		InAlertsOnClose.Name = "Alerts Only On Bar Close";
		InAlertsOnClose.SetYesNo(1);
		InAlertsOnClose.SetDescription("Yes: alert once per signal on the closed bar. No: alert as soon as the "
			"developing bar satisfies a condition (can repeat across bars, once per bar).");

		InLegacyRevWindow.Name = "Legacy (Pine) Reversal Window Timing";
		InLegacyRevWindow.SetYesNo(0);
		InLegacyRevWindow.SetDescription("No: the reversal watch starts on the bar after the breakout, so exactly "
			"'Reversal Watch Bars' bars are checked. Yes: replicate the original Pine timing, where the breakout "
			"bar consumes one watch bar (and Reversal Watch Bars = 1 can never trigger).");

		InPlotNearestExt.Name = "Plot Nearest Extended Levels";
		InPlotNearestExt.SetYesNo(1);
		InPlotNearestExt.SetDescription("Yes: the 5 plot slots per side show the active levels nearest to the "
			"current price. No: replicate Pine, which shows the 5 oldest active levels and hides newer ones.");

		InReentryResets.Name = "Squeeze Re-Entry Resets Active Channel";
		InReentryResets.SetYesNo(1);
		InReentryResets.SetDescription("Yes (Pine behavior): a new squeeze entry while a channel is still active "
			"re-seeds the channel. No: the existing channel is kept until it resolves via breakout, and the "
			"squeeze gradient coloring resumes on it.");

		InUnbiasedStdev.Name = "Use Unbiased (Sample) Std Dev";
		InUnbiasedStdev.SetYesNo(0);
		InUnbiasedStdev.SetDescription("No (Pine default): population standard deviation (divide by N) for the "
			"Bollinger Bands. Yes: sample standard deviation (divide by N-1); slightly wider bands, marginally "
			"fewer squeezes.");

		return;
	}

	// ------------------------------------------------------------------
	// Persistent extended S/R lists
	// ------------------------------------------------------------------
	s_SqueezeChannelPersist* Persist =
		(s_SqueezeChannelPersist*)sc.GetPersistentPointer(PERSIST_KEY_STATE_POINTER);

	if (sc.LastCallToFunction)
	{
		if (Persist != NULL)
		{
			delete Persist;
			sc.SetPersistentPointer(PERSIST_KEY_STATE_POINTER, NULL);
		}
		return;
	}

	if (sc.ArraySize <= 0)
		return;

	if (Persist == NULL)
	{
		Persist = new s_SqueezeChannelPersist;
		sc.SetPersistentPointer(PERSIST_KEY_STATE_POINTER, Persist);
	}

	// ------------------------------------------------------------------
	// Inputs -> locals
	// ------------------------------------------------------------------
	const int    BBLength       = InBBLength.GetInt();
	const double BBMult         = InBBMult.GetFloat();
	const int    KCLength       = InKCLength.GetInt();
	const double KCMult         = InKCMult.GetFloat();
	const int    SwingLookback  = InSwingLookback.GetInt();
	const int    SwingLookfwd   = InSwingLookfwd.GetInt();
	const bool   EnableReversal = InEnableReversal.GetYesNo() != 0;
	const int    ReversalBars   = InReversalBars.GetInt();
	const bool   ShowExtLevels  = InEnableExtLevels.GetYesNo() != 0;
	const bool   EnableRetest   = InEnableSRRetest.GetYesNo() != 0;
	const int    ExtendedBars   = InExtendedBars.GetInt();
	const int    SRDelayBars    = InSRDelayBars.GetInt();
	const double ATRProximity   = InATRProximity.GetFloat();
	const int    ATRLength      = InATRLength.GetInt();
	const bool   ShowChannel    = InShowChannel.GetYesNo() != 0;
	const bool   ShowFill       = InShowFill.GetYesNo() != 0;
	const double MarkerOffMult  = InMarkerOffset.GetFloat();
	const bool   EnableAlerts   = InEnableAlerts.GetYesNo() != 0;
	const bool   AlertsOnClose  = InAlertsOnClose.GetYesNo() != 0;
	const bool   LegacyRevWin   = InLegacyRevWindow.GetYesNo() != 0;
	const bool   PlotNearestExt = InPlotNearestExt.GetYesNo() != 0;
	const bool   ReentryResets  = InReentryResets.GetYesNo() != 0;
	const bool   UnbiasedStdev  = InUnbiasedStdev.GetYesNo() != 0;

	const int MaxCoreLength = BBLength > KCLength ? BBLength : KCLength;
	sc.DataStartIndex = MaxCoreLength - 1;

	// ------------------------------------------------------------------
	// Per-bar state arrays (Pine 'var' equivalents). Every bar is computed
	// from the previous bar's stored state, so recalculating any range of
	// bars - including tick-by-tick recalculation of the developing bar -
	// is deterministic and idempotent.
	// ------------------------------------------------------------------
	SCFloatArrayRef StChActive    = ChannelHigh.Arrays[0];
	SCFloatArrayRef StChHigh      = ChannelHigh.Arrays[1];
	SCFloatArrayRef StChLow       = ChannelHigh.Arrays[2];
	SCFloatArrayRef StBarsSince   = ChannelHigh.Arrays[3];
	SCFloatArrayRef StChLocked    = ChannelHigh.Arrays[4];
	SCFloatArrayRef StSqStillOn   = ChannelHigh.Arrays[5];
	SCFloatArrayRef StSqBucket    = ChannelHigh.Arrays[6];   // 1..5 gradient bucket of last squeeze bar

	SCFloatArrayRef StWatching    = ChannelLow.Arrays[0];
	SCFloatArrayRef StRevDir      = ChannelLow.Arrays[1];    // +1 after bull breakout, -1 after bear breakout
	SCFloatArrayRef StRevLevel    = ChannelLow.Arrays[2];
	SCFloatArrayRef StRevBarsLeft = ChannelLow.Arrays[3];
	SCFloatArrayRef StRevTrig     = ChannelLow.Arrays[4];
	SCFloatArrayRef FlagBullBrk   = ChannelLow.Arrays[5];
	SCFloatArrayRef FlagBearBrk   = ChannelLow.Arrays[6];
	SCFloatArrayRef FlagBullRev   = ChannelLow.Arrays[7];
	SCFloatArrayRef FlagBearRev   = ChannelLow.Arrays[8];

	SCFloatArrayRef AtrKC         = ChannelMid.Arrays[0];    // Wilder ATR, Keltner length
	SCFloatArrayRef AtrProx       = ChannelMid.Arrays[1];    // Wilder ATR, "ATR Length" (S/R proximity + marker offset)

	// ------------------------------------------------------------------
	// Calculation helpers (Pine-exact)
	// ------------------------------------------------------------------
	auto TrueRangeAt = [&](int Index) -> double
	{
		const double HighLow = sc.High[Index] - sc.Low[Index];
		if (Index == 0)
			return HighLow;  // Pine ta.tr(true): high-low when no prior close
		const double HighClose = fabs(sc.High[Index] - sc.Close[Index - 1]);
		const double LowClose  = fabs(sc.Low[Index]  - sc.Close[Index - 1]);
		return SqcMax(HighLow, SqcMax(HighClose, LowClose));
	};

	// Pine ta.atr = ta.rma(tr, length): undefined before bar length-1, SMA seed
	// at bar length-1, then (tr + (length-1)*prev)/length.
	auto UpdateWilderATR = [&](SCFloatArrayRef Out, int Index, int Length)
	{
		if (Index < Length - 1)
		{
			Out[Index] = 0.0f;
			return;
		}
		if (Index == Length - 1)
		{
			double Sum = 0.0;
			for (int k = Index - Length + 1; k <= Index; k++)
				Sum += TrueRangeAt(k);
			Out[Index] = (float)(Sum / Length);
			return;
		}
		Out[Index] = (float)((TrueRangeAt(Index) + (double)(Length - 1) * Out[Index - 1]) / Length);
	};

	auto CloseSMAAt = [&](int Index, int Length) -> double
	{
		double Sum = 0.0;
		for (int k = Index - Length + 1; k <= Index; k++)
			Sum += sc.Close[k];
		return Sum / Length;
	};

	// Pine ta.stdev default (biased=true): population standard deviation.
	// The "Use Unbiased (Sample) Std Dev" input switches to the N-1 divisor.
	auto CloseStdevAt = [&](int Index, int Length, double Mean) -> double
	{
		double SumSq = 0.0;
		for (int k = Index - Length + 1; k <= Index; k++)
		{
			const double Diff = sc.Close[k] - Mean;
			SumSq += Diff * Diff;
		}
		const int Divisor = (UnbiasedStdev && Length > 1) ? Length - 1 : Length;
		return sqrt(SumSq / Divisor);
	};

	auto StrengthToBucket = [](double Strength) -> int
	{
		if (Strength < 0.2) return 1;
		if (Strength < 0.4) return 2;
		if (Strength < 0.6) return 3;
		if (Strength < 0.8) return 4;
		return 5;
	};

	auto BucketColor = [&](int Bucket) -> COLORREF
	{
		switch (Bucket)
		{
		case 1:  return InSqColor1.GetColor();
		case 2:  return InSqColor2.GetColor();
		case 3:  return InSqColor3.GetColor();
		case 4:  return InSqColor4.GetColor();
		case 5:  return InSqColor5.GetColor();
		default: return InColorWaiting.GetColor();
		}
	};

	auto MarkerOffsetAt = [&](int Index) -> double
	{
		double Base = (Index >= ATRLength - 1) ? (double)AtrProx[Index] : (sc.High[Index] - sc.Low[Index]);
		double Offset = Base * MarkerOffMult;
		if (Offset <= 0.0)
			Offset = sc.TickSize > 0.0f ? sc.TickSize : 0.0;
		return Offset;
	};

	// Applies the extended S/R list transitions of one bar (Pine lines 196-279):
	// store levels the bar after a breakout, expire old levels, and (optionally)
	// generate retest signals. Mirrors the Pine statement order exactly.
	auto ProcessExtendedLevelsAt = [&](int Bar,
	                                   std::vector<s_SqueezeSRLevel>& Highs,
	                                   std::vector<s_SqueezeSRLevel>& Lows,
	                                   bool& OutExtBull, bool& OutExtBear,
	                                   bool& OutExtBullRev, bool& OutExtBearRev)
	{
		OutExtBull = OutExtBear = OutExtBullRev = OutExtBearRev = false;

		if (Bar > 0 && (FlagBullBrk[Bar - 1] != 0.0f || FlagBearBrk[Bar - 1] != 0.0f))
		{
			s_SqueezeSRLevel HighLevel;
			HighLevel.Price = StChHigh[Bar - 1];
			HighLevel.StartBarIndex = Bar;
			HighLevel.ExpiryBarIndex = Bar + ExtendedBars;
			HighLevel.Broken = false;
			Highs.push_back(HighLevel);

			s_SqueezeSRLevel LowLevel;
			LowLevel.Price = StChLow[Bar - 1];
			LowLevel.StartBarIndex = Bar;
			LowLevel.ExpiryBarIndex = Bar + ExtendedBars;
			LowLevel.Broken = false;
			Lows.push_back(LowLevel);
		}

		const double BarClose = sc.Close[Bar];

		for (int idx = (int)Highs.size() - 1; idx >= 0; idx--)
		{
			if (Bar > Highs[idx].ExpiryBarIndex)
			{
				Highs.erase(Highs.begin() + idx);
				continue;
			}
			if (!EnableRetest || Bar < Highs[idx].StartBarIndex + SRDelayBars)
				continue;
			if (!Highs[idx].Broken && BarClose > Highs[idx].Price)
			{
				Highs[idx].Broken = true;
				OutExtBull = true;
			}
			if (Highs[idx].Broken && BarClose < Highs[idx].Price)
			{
				OutExtBearRev = true;
				Highs.erase(Highs.begin() + idx);
			}
		}

		for (int idx = (int)Lows.size() - 1; idx >= 0; idx--)
		{
			if (Bar > Lows[idx].ExpiryBarIndex)
			{
				Lows.erase(Lows.begin() + idx);
				continue;
			}
			if (!EnableRetest || Bar < Lows[idx].StartBarIndex + SRDelayBars)
				continue;
			if (!Lows[idx].Broken && BarClose < Lows[idx].Price)
			{
				Lows[idx].Broken = true;
				OutExtBear = true;
			}
			if (Lows[idx].Broken && BarClose > Lows[idx].Price)
			{
				OutExtBullRev = true;
				Lows.erase(Lows.begin() + idx);
			}
		}
	};

	// Writes the extended level plot slots (5 per side, shown only when price is
	// within ATR*proximity of the level - Pine getExtHigh/LowLevel). Slot
	// selection: nearest-to-price (enhanced default) or oldest-first with
	// positional gaps (Pine-compatible).
	auto WriteExtendedPlotsAt = [&](int Bar,
	                                const std::vector<s_SqueezeSRLevel>& Highs,
	                                const std::vector<s_SqueezeSRLevel>& Lows)
	{
		const bool AtrValid = (Bar >= ATRLength - 1);
		const double Proximity = (double)AtrProx[Bar] * ATRProximity;
		const double BarClose = sc.Close[Bar];

		auto FillSlots = [&](const std::vector<s_SqueezeSRLevel>& Levels, int FirstSlotSubgraph)
		{
			float SlotValues[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

			if (ShowExtLevels && AtrValid)
			{
				if (PlotNearestExt)
				{
					std::vector<std::pair<double, float> > InRange;  // (distance, price)
					for (int n = 0; n < (int)Levels.size(); n++)
					{
						const double Distance = fabs(BarClose - Levels[n].Price);
						if (Distance <= Proximity)
							InRange.push_back(std::make_pair(Distance, Levels[n].Price));
					}
					std::sort(InRange.begin(), InRange.end());
					for (int k = 0; k < 5 && k < (int)InRange.size(); k++)
						SlotValues[k] = InRange[k].second;
				}
				else
				{
					for (int k = 0; k < 5 && k < (int)Levels.size(); k++)
					{
						if (fabs(BarClose - Levels[k].Price) <= Proximity)
							SlotValues[k] = Levels[k].Price;
					}
				}
			}

			for (int k = 0; k < 5; k++)
				sc.Subgraph[FirstSlotSubgraph + k][Bar] = SlotValues[k];
		};

		FillSlots(Highs, SG_EXT_RESISTANCE_1);
		FillSlots(Lows, SG_EXT_SUPPORT_1);
	};

	auto WriteExtendedMarkersAt = [&](int Bar, bool ExtBull, bool ExtBear, bool ExtBullR, bool ExtBearR)
	{
		const double Offset = MarkerOffsetAt(Bar);
		ExtBullBrk[Bar] = ExtBull  ? (float)(sc.Low[Bar]  - Offset) : 0.0f;
		ExtBearBrk[Bar] = ExtBear  ? (float)(sc.High[Bar] + Offset) : 0.0f;
		ExtBullRev[Bar] = ExtBullR ? (float)(sc.Low[Bar]  - Offset) : 0.0f;
		ExtBearRev[Bar] = ExtBearR ? (float)(sc.High[Bar] + Offset) : 0.0f;
	};

	// Fires the Pine alertcondition equivalents for one bar. Extended-signal
	// states are passed in; everything else is read from the per-bar arrays.
	auto FireAlertsForBar = [&](int Bar, bool ExtBull, bool ExtBear, bool ExtBullR, bool ExtBearR)
	{
		if (FlagBullBrk[Bar] != 0.0f)
			sc.SetAlert(ALERT_BULL_BREAKOUT, "Squeeze Channel: Bullish breakout");
		if (FlagBearBrk[Bar] != 0.0f)
			sc.SetAlert(ALERT_BEAR_BREAKOUT, "Squeeze Channel: Bearish breakout");
		if (FlagBullRev[Bar] != 0.0f)
			sc.SetAlert(ALERT_BULL_REVERSAL, "Squeeze Channel: Failed bear breakout - bull reversal");
		if (FlagBearRev[Bar] != 0.0f)
			sc.SetAlert(ALERT_BEAR_REVERSAL, "Squeeze Channel: Failed bull breakout - bear reversal");
		if (ExtBull)
			sc.SetAlert(ALERT_EXT_BULL_BREAKOUT, "Squeeze Channel: Extended resistance breakout");
		if (ExtBear)
			sc.SetAlert(ALERT_EXT_BEAR_BREAKOUT, "Squeeze Channel: Extended support breakdown");
		if (ExtBullR)
			sc.SetAlert(ALERT_EXT_BULL_REVERSAL, "Squeeze Channel: Extended support reclaim");
		if (ExtBearR)
			sc.SetAlert(ALERT_EXT_BEAR_REVERSAL, "Squeeze Channel: Extended resistance rejection");

		const bool SqueezeEntry = SqueezeOnSG[Bar] != 0.0f
			&& (Bar == 0 || SqueezeOnSG[Bar - 1] == 0.0f);
		if (SqueezeEntry)
			sc.SetAlert(ALERT_SQUEEZE_STARTED, "Squeeze Channel: Squeeze detected");
	};

	// ------------------------------------------------------------------
	// Main per-bar loop: squeeze math, channel state machine, breakout and
	// reversal signals, channel plots. (Pine lines 52-190 and 285-297.)
	// ------------------------------------------------------------------
	const int LastIndex = sc.ArraySize - 1;

	for (int i = sc.UpdateStartIndex; i < sc.ArraySize; i++)
	{
		// --- derived series ---
		UpdateWilderATR(AtrKC, i, KCLength);
		UpdateWilderATR(AtrProx, i, ATRLength);

		const bool BBValid = (i >= BBLength - 1);
		const bool KCValid = (i >= KCLength - 1);

		double BBUpper = 0.0, BBLower = 0.0, BBWidth = 0.0;
		double KCUpper = 0.0, KCLower = 0.0, KCWidth = 0.0;

		if (BBValid)
		{
			const double Basis = CloseSMAAt(i, BBLength);
			const double Dev = BBMult * CloseStdevAt(i, BBLength, Basis);
			BBUpper = Basis + Dev;
			BBLower = Basis - Dev;
			BBWidth = BBUpper - BBLower;
		}
		if (KCValid)
		{
			const double Basis = CloseSMAAt(i, KCLength);
			const double Range = KCMult * AtrKC[i];
			KCUpper = Basis + Range;
			KCLower = Basis - Range;
			KCWidth = KCUpper - KCLower;
		}

		const bool SqueezeOn = BBValid && KCValid && BBLower > KCLower && BBUpper < KCUpper;
		const bool PrevSqueezeOn = (i > 0) && SqueezeOnSG[i - 1] != 0.0f;
		const bool SqueezeEntry = SqueezeOn && !PrevSqueezeOn;

		double SqueezeStrength = 0.0;
		int StrengthBucket = 0;
		if (SqueezeOn)
		{
			const double Ratio = BBWidth / KCWidth;  // SqueezeOn implies KCWidth > 0
			SqueezeStrength = 1.0 - SqcMin(Ratio, 1.0);
			StrengthBucket = StrengthToBucket(SqueezeStrength);
		}

		SqueezeOnSG[i] = SqueezeOn ? 1.0f : 0.0f;
		SqueezeStrSG[i] = (float)SqueezeStrength;

		// --- previous-bar 'var' state ---
		bool   ChActive    = (i > 0) && StChActive[i - 1] != 0.0f;
		double ChHighVal   = (i > 0) ? StChHigh[i - 1] : 0.0;
		double ChLowVal    = (i > 0) ? StChLow[i - 1] : 0.0;
		int    BarsSince   = (i > 0) ? (int)StBarsSince[i - 1] : 0;
		bool   ChLocked    = (i > 0) && StChLocked[i - 1] != 0.0f;
		bool   SqStillOn   = (i > 0) && StSqStillOn[i - 1] != 0.0f;
		int    SqBucket    = (i > 0) ? (int)StSqBucket[i - 1] : 0;
		bool   Watching    = (i > 0) && StWatching[i - 1] != 0.0f;
		int    RevDir      = (i > 0) ? (int)StRevDir[i - 1] : 0;
		double RevLevelVal = (i > 0) ? StRevLevel[i - 1] : 0.0;
		int    RevBarsLeft = (i > 0) ? (int)StRevBarsLeft[i - 1] : 0;
		bool   RevTrig     = (i > 0) && StRevTrig[i - 1] != 0.0f;

		// --- channel state machine (Pine lines 112-142) ---
		// With "Squeeze Re-Entry Resets Active Channel" = No, a squeeze entry
		// while a channel is live falls through to the continuation branch
		// instead of re-seeding.
		if (SqueezeEntry && (ReentryResets || !ChActive))
		{
			ChActive = true;

			// ta.highest/ta.lowest over the last SwingLookback bars including
			// this one. (Window clamped at the start of the chart; Pine returns
			// na there and effectively disables the channel - see CONVERSION.md.)
			int From = i - SwingLookback + 1;
			if (From < 0)
				From = 0;
			ChHighVal = sc.High[i];
			ChLowVal = sc.Low[i];
			for (int k = From; k < i; k++)
			{
				ChHighVal = SqcMax(ChHighVal, sc.High[k]);
				ChLowVal = SqcMin(ChLowVal, sc.Low[k]);
			}

			BarsSince = 0;
			ChLocked = false;
			SqStillOn = true;
			SqBucket = StrengthBucket;
			Watching = false;
			RevDir = 0;
			RevLevelVal = 0.0;
			RevBarsLeft = 0;
			RevTrig = false;
		}
		else if (ChActive)
		{
			BarsSince++;

			if (BarsSince <= SwingLookfwd && !ChLocked)
			{
				if (sc.High[i] > ChHighVal)
					ChHighVal = sc.High[i];
				if (sc.Low[i] < ChLowVal)
					ChLowVal = sc.Low[i];
			}

			if (BarsSince > SwingLookfwd)
				ChLocked = true;

			if (SqueezeOn)
			{
				SqBucket = StrengthBucket;
				// Only reachable when re-entry resets are disabled: a squeeze
				// returning on a kept channel resumes the gradient coloring.
				if (SqueezeEntry)
					SqStillOn = true;
			}

			if (!SqueezeOn)
				SqStillOn = false;
		}

		// --- breakout signals (Pine lines 148-169) ---
		const double BarClose = sc.Close[i];
		const bool ValidZone = ChActive && ChLocked;
		const bool BullBrk = ValidZone && BarClose > ChHighVal;
		const bool BearBrk = ValidZone && BarClose < ChLowVal;

		if (BullBrk)
		{
			if (EnableReversal)
			{
				Watching = true;
				RevDir = 1;
				RevLevelVal = ChLowVal;
				RevBarsLeft = ReversalBars;
				RevTrig = false;
			}
			ChActive = false;
		}
		if (BearBrk)
		{
			if (EnableReversal)
			{
				Watching = true;
				RevDir = -1;
				RevLevelVal = ChHighVal;
				RevBarsLeft = ReversalBars;
				RevTrig = false;
			}
			ChActive = false;
		}

		// --- failed-breakout reversal detection (Pine lines 175-190) ---
		// Enhanced timing (default): the bar that arms the watch is skipped, so
		// exactly ReversalBars bars after the breakout are checked. Legacy mode
		// replicates Pine, where the breakout bar consumes one watch bar.
		bool BullRev = false;
		bool BearRev = false;
		const bool WatchArmedThisBar = BullBrk || BearBrk;
		if (EnableReversal && Watching && RevBarsLeft > 0 && !RevTrig
			&& (LegacyRevWin || !WatchArmedThisBar))
		{
			RevBarsLeft--;

			if (RevDir == 1 && BarClose < RevLevelVal)
			{
				BearRev = true;
				RevTrig = true;
			}
			if (RevDir == -1 && BarClose > RevLevelVal)
			{
				BullRev = true;
				RevTrig = true;
			}
		}
		if (RevBarsLeft <= 0)
			Watching = false;

		// --- store this bar's state ---
		StChActive[i]    = ChActive ? 1.0f : 0.0f;
		StChHigh[i]      = (float)ChHighVal;
		StChLow[i]       = (float)ChLowVal;
		StBarsSince[i]   = (float)BarsSince;
		StChLocked[i]    = ChLocked ? 1.0f : 0.0f;
		StSqStillOn[i]   = SqStillOn ? 1.0f : 0.0f;
		StSqBucket[i]    = (float)SqBucket;
		StWatching[i]    = Watching ? 1.0f : 0.0f;
		StRevDir[i]      = (float)RevDir;
		StRevLevel[i]    = (float)RevLevelVal;
		StRevBarsLeft[i] = (float)RevBarsLeft;
		StRevTrig[i]     = RevTrig ? 1.0f : 0.0f;
		FlagBullBrk[i]   = BullBrk ? 1.0f : 0.0f;
		FlagBearBrk[i]   = BearBrk ? 1.0f : 0.0f;
		FlagBullRev[i]   = BullRev ? 1.0f : 0.0f;
		FlagBearRev[i]   = BearRev ? 1.0f : 0.0f;

		// --- channel plots (Pine lines 285-297) ---
		const COLORREF ChannelColor = SqStillOn ? BucketColor(SqBucket) : InColorWaiting.GetColor();

		if (ShowChannel && ChActive)
		{
			ChannelHigh[i] = (float)ChHighVal;
			ChannelLow[i] = (float)ChLowVal;
			ChannelHigh.DataColor[i] = ChannelColor;
			ChannelLow.DataColor[i] = ChannelColor;
		}
		else
		{
			ChannelHigh[i] = 0.0f;
			ChannelLow[i] = 0.0f;
		}

		// Pine's fill() is anchored to the channel plots, so hiding the channel
		// also hides the fill; both visibility inputs must be on.
		if (ShowChannel && ShowFill && ChActive)
		{
			FillTop[i] = (float)ChHighVal;
			FillBottom[i] = (float)ChLowVal;
			FillTop.DataColor[i] = ChannelColor;
			FillBottom.DataColor[i] = ChannelColor;
		}
		else
		{
			FillTop[i] = 0.0f;
			FillBottom[i] = 0.0f;
		}

		ChannelMid[i] = (ShowChannel && ChActive && ChLocked)
			? (float)((ChHighVal + ChLowVal) / 2.0)
			: 0.0f;

		if (ShowChannel && EnableReversal && Watching && !RevTrig)
		{
			ReversalLevel[i] = (float)RevLevelVal;
			// Pine colored this from the opposing signal color (bear color while
			// watching for a bear reversal after a bull breakout, and vice versa).
			ReversalLevel.DataColor[i] = (RevDir == 1)
				? BearBreakout.PrimaryColor
				: BullBreakout.PrimaryColor;
		}
		else
		{
			ReversalLevel[i] = 0.0f;
		}

		// --- breakout / reversal markers (Pine plotshape calls) ---
		const double Offset = MarkerOffsetAt(i);
		BullBreakout[i] = BullBrk ? (float)(sc.Low[i] - Offset) : 0.0f;
		BearBreakout[i] = BearBrk ? (float)(sc.High[i] + Offset) : 0.0f;
		BullReversal[i] = BullRev ? (float)(sc.Low[i] - Offset) : 0.0f;
		BearReversal[i] = BearRev ? (float)(sc.High[i] + Offset) : 0.0f;
	}

	// ------------------------------------------------------------------
	// Extended S/R lists: commit closed bars exactly once; evaluate the
	// developing bar on a temporary copy every update.
	// ------------------------------------------------------------------
	int NextToCommit = sc.GetPersistentInt(PERSIST_KEY_NEXT_TO_COMMIT);

	// Full recalculation or a data rewind invalidates committed list state:
	// rebuild deterministically from the stored per-bar arrays.
	if (sc.UpdateStartIndex < NextToCommit)
	{
		Persist->HighLevels.clear();
		Persist->LowLevels.clear();
		NextToCommit = 0;
	}

	const bool AllowAlerts = EnableAlerts
		&& !sc.IsFullRecalculation
		&& sc.ChartIsDownloadingHistoricalData(sc.ChartNumber) == 0;

	int LastAlertBar = sc.GetPersistentInt(PERSIST_KEY_LAST_ALERT_BAR);

	// If the chart shrank (data reload, replay reset), pull the alert
	// checkpoint back so alerts resume with the next newly closed bar.
	if (LastAlertBar > LastIndex - 1)
	{
		LastAlertBar = LastIndex - 1;
		sc.SetPersistentInt(PERSIST_KEY_LAST_ALERT_BAR, LastAlertBar);
	}

	for (int b = NextToCommit; b <= LastIndex - 1; b++)
	{
		bool ExtBull = false, ExtBear = false, ExtBullR = false, ExtBearR = false;
		ProcessExtendedLevelsAt(b, Persist->HighLevels, Persist->LowLevels,
			ExtBull, ExtBear, ExtBullR, ExtBearR);
		WriteExtendedPlotsAt(b, Persist->HighLevels, Persist->LowLevels);
		WriteExtendedMarkersAt(b, ExtBull, ExtBear, ExtBullR, ExtBearR);

		if (AllowAlerts && AlertsOnClose && b == LastIndex - 1 && b > LastAlertBar)
		{
			FireAlertsForBar(b, ExtBull, ExtBear, ExtBullR, ExtBearR);
			LastAlertBar = b;
			sc.SetPersistentInt(PERSIST_KEY_LAST_ALERT_BAR, LastAlertBar);
		}
	}
	NextToCommit = LastIndex;  // committed through LastIndex - 1
	sc.SetPersistentInt(PERSIST_KEY_NEXT_TO_COMMIT, NextToCommit);

	{
		std::vector<s_SqueezeSRLevel> TempHighs = Persist->HighLevels;
		std::vector<s_SqueezeSRLevel> TempLows = Persist->LowLevels;

		bool ExtBull = false, ExtBear = false, ExtBullR = false, ExtBearR = false;
		ProcessExtendedLevelsAt(LastIndex, TempHighs, TempLows,
			ExtBull, ExtBear, ExtBullR, ExtBearR);
		WriteExtendedPlotsAt(LastIndex, TempHighs, TempLows);
		WriteExtendedMarkersAt(LastIndex, ExtBull, ExtBear, ExtBullR, ExtBearR);

		if (AllowAlerts && !AlertsOnClose)
			FireAlertsForBar(LastIndex, ExtBull, ExtBear, ExtBullR, ExtBearR);
	}
}
