// TrendArchitect.cpp — Sierra Chart ACSIL Study: Trend Architect v9.3.1
// Requires Advanced Sierra Chart subscription.
// Build: compile with Sierra Chart's SierraChartStudy project template.
// Minimum Sierra Chart version: 2497 (for s_UseTool, persistent pointers).
//
// SCSFExport function name = "Trend Architect v9.3.1" in the Studies list.

#include "sierrachart.h"

// windows.h (pulled in by sierrachart.h) defines min/max macros that break
// std::min/std::max calls throughout the module headers below.
#undef min
#undef max
#include <algorithm>
#include <cmath>

SCDLLName("Trend Architect v9.3.1")

#include "include/TA_Algorithms.h"
#include "include/TA_ColorThemes.h"
#include "include/TA_DrawingIDs.h"
#include "include/TA_Inputs.h"
#include "include/TA_Subgraphs.h"
#include "include/TA_State.h"

#include "modules/mod_MAribbon.h"
#include "modules/mod_SuperChannel.h"
#include "modules/mod_Candles.h"
#include "modules/mod_CandleColoring.h"
#include "modules/mod_TrendCloud.h"
#include "modules/mod_TrendRegimeGate.h"
#include "modules/mod_RangeRegimeGate.h"
#include "modules/mod_AutoOptimizer.h"
#include "modules/mod_PRISMSignals.h"
#include "modules/mod_BoundaryForecast.h"
#include "modules/mod_InfoPanel.h"
#include "modules/mod_SignalLogger.h"

// ── Candle type from input string index ───────────────────────────────────────
static CandleType candle_type_from_idx(int idx) {
    switch (idx) {
        case 1: return CT_HEIKIN_ASHI;
        case 2: return CT_R2_ADAPTIVE;
        case 3: return CT_LINREG_HA;
        case 4: return CT_LINREG;
        default: return CT_REGULAR;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
SCSFExport scsf_TrendArchitect(SCStudyInterfaceRef sc)
{
    // ── SetDefaults ─────────────────────────────────────────────────────────────
    if (sc.SetDefaults) {
        sc.GraphName       = "Trend Architect v9.3.1";
        sc.StudyDescription= "Trend Architect v9.3.1 — Algorithmic trend analysis + signal engine";
        sc.AutoLoop        = 1;
        sc.GraphRegion     = 0;  // main price chart
        sc.GraphDrawType   = GDT_CUSTOM;  // REQUIRED: any other Graph Draw Type
                                          // overrides every subgraph's draw style
        // NOTE: DisplayAsMainPriceGraph must NEVER be set — per the SC docs the
        // study would "become the new underlying data for any other studies on
        // the chart" (a separate SuperTrend etc. would then compute off our
        // subgraphs instead of price). Native-bar handling is fully automatic:
        //   Indicator Candles / Candle Coloring ON → the bar-mask layer
        //     (SG_BAR_MASK_*, bottom of the z-order) blanks the native bars
        //     behind the synthetic candles.
        //   Both OFF → the study drops underneath the bars (runtime block) and
        //     the transparent fills keep the native candles fully visible.
        // No manual "Hide Main Price Graph" step is needed in either mode.
        // Panel/watermark use the 1-150 relative scale, which Sierra keeps
        // screen-anchored without UpdateAlways.
        sc.ScaleRangeType  = SCALE_AUTO;
        // [v9.3] Required for sc.AskVolume/sc.BidVolume and the
        // SC_ASKBID_DIFF_HIGH/LOW arrays (needs 1-Tick Intraday Data Storage;
        // arrays read 0 where the data is absent — everything self-disables)
        sc.MaintainAdditionalChartDataArrays = 1;
        // Fill transparency: there is NO sc.TransparencyLevel member (despite
        // the docs TOC listing one) — the runtime idx==0 block sets the real
        // alpha for the TRANSPARENT_FILL pairs via SetChartStudyTransparencyLevel
        // on every full recalculation, including right after study add.

        // ── Subgraph definitions ──────────────────────────────────────────────
        // Indices encode Pine's z-order (ascending = bottom → top); see
        // TA_Subgraphs.h. Candles render via paired per-subgraph draw styles.

        // Layer 0: Native-bar mask — per-bar background rectangles over the
        // chart's own bars while the study draws candles (see TA_Subgraphs.h)
        sc.Subgraph[SG_BAR_MASK_TOP].Name         = "Bar Mask Top";
        sc.Subgraph[SG_BAR_MASK_TOP].DrawStyle    = DRAWSTYLE_FILL_RECTANGLE_TOP;
        sc.Subgraph[SG_BAR_MASK_TOP].PrimaryColor = RGB(0, 0, 0);
        sc.Subgraph[SG_BAR_MASK_TOP].DrawZeros    = 0;

        sc.Subgraph[SG_BAR_MASK_BOT].Name         = "Bar Mask Bottom";
        sc.Subgraph[SG_BAR_MASK_BOT].DrawStyle    = DRAWSTYLE_FILL_RECTANGLE_BOTTOM;
        sc.Subgraph[SG_BAR_MASK_BOT].PrimaryColor = RGB(0, 0, 0);
        sc.Subgraph[SG_BAR_MASK_BOT].DrawZeros    = 0;

        // Layer 1a: TC base glow stack (Pine widths 10/20/30/45; SC pixels scaled)
        static const int  GLOW_SGS[4]    = {SG_TC_GLOW4, SG_TC_GLOW3, SG_TC_GLOW2, SG_TC_GLOW1};
        static const int  GLOW_WIDTH[4]  = {20, 14, 10, 6};
        static const char* GLOW_NAME[4]  = {"TC Glow 4", "TC Glow 3", "TC Glow 2", "TC Glow 1"};
        for (int g = 0; g < 4; g++) {
            sc.Subgraph[GLOW_SGS[g]].Name         = GLOW_NAME[g];
            sc.Subgraph[GLOW_SGS[g]].DrawStyle    = DRAWSTYLE_LINE;
            sc.Subgraph[GLOW_SGS[g]].PrimaryColor = RGB(255, 214, 0);
            sc.Subgraph[GLOW_SGS[g]].LineWidth    = GLOW_WIDTH[g];
            sc.Subgraph[GLOW_SGS[g]].DrawZeros    = 0;
        }

        // Layer 1b: Trend Cloud
        sc.Subgraph[SG_TC_FILL_TOP].Name             = "TC Fill Top";
        sc.Subgraph[SG_TC_FILL_TOP].DrawStyle        = DRAWSTYLE_TRANSPARENT_FILL_TOP;
        sc.Subgraph[SG_TC_FILL_TOP].PrimaryColor     = RGB(38, 166, 154);
        sc.Subgraph[SG_TC_FILL_TOP].DrawZeros        = 0;

        sc.Subgraph[SG_TC_FILL_BOT].Name             = "TC Fill Bottom";
        sc.Subgraph[SG_TC_FILL_BOT].DrawStyle        = DRAWSTYLE_TRANSPARENT_FILL_BOTTOM;
        sc.Subgraph[SG_TC_FILL_BOT].PrimaryColor     = RGB(38, 166, 154);
        sc.Subgraph[SG_TC_FILL_BOT].DrawZeros        = 0;

        sc.Subgraph[SG_TC_TOP].Name                  = "TC Top";
        sc.Subgraph[SG_TC_TOP].DrawStyle             = DRAWSTYLE_LINE;
        sc.Subgraph[SG_TC_TOP].PrimaryColor          = RGB(255, 214, 0);
        sc.Subgraph[SG_TC_TOP].LineWidth             = 2;   // Pine: solid, width 2
        sc.Subgraph[SG_TC_TOP].DrawZeros             = 0;

        sc.Subgraph[SG_TC_BASE].Name                 = "TC Base";
        sc.Subgraph[SG_TC_BASE].DrawStyle            = DRAWSTYLE_LINE;
        sc.Subgraph[SG_TC_BASE].PrimaryColor         = RGB(255, 214, 0);
        sc.Subgraph[SG_TC_BASE].LineWidth            = 3;
        sc.Subgraph[SG_TC_BASE].DrawZeros            = 0;

        // Layer 2: Super Channel
        sc.Subgraph[SG_SC_FILL_TOP].Name             = "SC Fill Top";
        sc.Subgraph[SG_SC_FILL_TOP].DrawStyle        = DRAWSTYLE_TRANSPARENT_FILL_TOP;
        sc.Subgraph[SG_SC_FILL_TOP].PrimaryColor     = RGB(120, 120, 120);
        sc.Subgraph[SG_SC_FILL_TOP].DrawZeros        = 0;

        sc.Subgraph[SG_SC_FILL_BOT].Name             = "SC Fill Bottom";
        sc.Subgraph[SG_SC_FILL_BOT].DrawStyle        = DRAWSTYLE_TRANSPARENT_FILL_BOTTOM;
        sc.Subgraph[SG_SC_FILL_BOT].PrimaryColor     = RGB(120, 120, 120);
        sc.Subgraph[SG_SC_FILL_BOT].DrawZeros        = 0;

        sc.Subgraph[SG_SC_TOP].Name                  = "SC Top";
        sc.Subgraph[SG_SC_TOP].DrawStyle             = DRAWSTYLE_LINE;
        sc.Subgraph[SG_SC_TOP].PrimaryColor          = RGB(239, 83, 80);
        sc.Subgraph[SG_SC_TOP].LineWidth             = 2;   // Pine: width 2
        sc.Subgraph[SG_SC_TOP].DrawZeros             = 0;

        sc.Subgraph[SG_SC_BOT].Name                  = "SC Bot";
        sc.Subgraph[SG_SC_BOT].DrawStyle             = DRAWSTYLE_LINE;
        sc.Subgraph[SG_SC_BOT].PrimaryColor          = RGB(38, 166, 154);
        sc.Subgraph[SG_SC_BOT].LineWidth             = 2;
        sc.Subgraph[SG_SC_BOT].DrawZeros             = 0;

        // Layer 3: MA Ribbon
        sc.Subgraph[SG_RIBBON_FILL_TOP].Name         = "MA Ribbon Fill Top";
        sc.Subgraph[SG_RIBBON_FILL_TOP].DrawStyle    = DRAWSTYLE_TRANSPARENT_FILL_TOP;
        sc.Subgraph[SG_RIBBON_FILL_TOP].PrimaryColor = RGB(38, 166, 154);
        sc.Subgraph[SG_RIBBON_FILL_TOP].DrawZeros    = 0;

        sc.Subgraph[SG_RIBBON_FILL_BOT].Name         = "MA Ribbon Fill Bottom";
        sc.Subgraph[SG_RIBBON_FILL_BOT].DrawStyle    = DRAWSTYLE_TRANSPARENT_FILL_BOTTOM;
        sc.Subgraph[SG_RIBBON_FILL_BOT].PrimaryColor = RGB(38, 166, 154);
        sc.Subgraph[SG_RIBBON_FILL_BOT].DrawZeros    = 0;

        sc.Subgraph[SG_ALMA1].Name                   = "MA Ribbon Fast";
        sc.Subgraph[SG_ALMA1].DrawStyle              = DRAWSTYLE_LINE;
        sc.Subgraph[SG_ALMA1].PrimaryColor           = RGB(38, 166, 154);
        sc.Subgraph[SG_ALMA1].LineWidth              = 2;
        sc.Subgraph[SG_ALMA1].DrawZeros              = 0;

        sc.Subgraph[SG_ALMA2].Name                   = "MA Ribbon Slow";
        sc.Subgraph[SG_ALMA2].DrawStyle              = DRAWSTYLE_LINE;
        sc.Subgraph[SG_ALMA2].PrimaryColor           = RGB(239, 83, 80);
        sc.Subgraph[SG_ALMA2].LineWidth              = 2;
        sc.Subgraph[SG_ALMA2].DrawZeros              = 0;

        // Layer 4: Custom Candles (BAR_TOP+BAR_BOTTOM wick pair;
        // CANDLESTICK_BODY_OPEN+CLOSE body pair)
        sc.Subgraph[SG_CANDLE_HIGH].Name             = "Candle High";
        sc.Subgraph[SG_CANDLE_HIGH].DrawStyle        = DRAWSTYLE_BAR_TOP;
        sc.Subgraph[SG_CANDLE_HIGH].PrimaryColor     = RGB(38, 166, 154);
        sc.Subgraph[SG_CANDLE_HIGH].LineWidth        = 1;
        sc.Subgraph[SG_CANDLE_HIGH].DrawZeros        = 0;

        sc.Subgraph[SG_CANDLE_LOW].Name              = "Candle Low";
        sc.Subgraph[SG_CANDLE_LOW].DrawStyle         = DRAWSTYLE_BAR_BOTTOM;
        sc.Subgraph[SG_CANDLE_LOW].PrimaryColor      = RGB(38, 166, 154);
        sc.Subgraph[SG_CANDLE_LOW].LineWidth         = 1;
        sc.Subgraph[SG_CANDLE_LOW].DrawZeros         = 0;

        sc.Subgraph[SG_CANDLE_OPEN].Name             = "Candle Open";
        sc.Subgraph[SG_CANDLE_OPEN].DrawStyle        = DRAWSTYLE_CANDLESTICK_BODY_OPEN;
        sc.Subgraph[SG_CANDLE_OPEN].PrimaryColor     = RGB(38, 166, 154);
        sc.Subgraph[SG_CANDLE_OPEN].DrawZeros        = 0;

        sc.Subgraph[SG_CANDLE_CLOSE].Name            = "Candle Close";
        sc.Subgraph[SG_CANDLE_CLOSE].DrawStyle       = DRAWSTYLE_CANDLESTICK_BODY_CLOSE;
        sc.Subgraph[SG_CANDLE_CLOSE].PrimaryColor    = RGB(38, 166, 154);  // bull
        sc.Subgraph[SG_CANDLE_CLOSE].SecondaryColor  = RGB(239, 83, 80);   // bear
        sc.Subgraph[SG_CANDLE_CLOSE].SecondaryColorUsed = 1;
        sc.Subgraph[SG_CANDLE_CLOSE].DrawZeros       = 0;

        sc.Subgraph[SG_CVD_BORDER].Name              = "CVD Border";
        sc.Subgraph[SG_CVD_BORDER].DrawStyle         = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_CVD_BORDER].DrawZeros         = 0;

        // Layer 5: PRISM signal markers
        sc.Subgraph[SG_PRISM_BULL].Name              = "PRISM Buy";
        sc.Subgraph[SG_PRISM_BULL].DrawStyle         = DRAWSTYLE_ARROW_UP;
        sc.Subgraph[SG_PRISM_BULL].PrimaryColor      = RGB(38, 166, 154);
        sc.Subgraph[SG_PRISM_BULL].LineWidth         = 3;
        sc.Subgraph[SG_PRISM_BULL].DrawZeros         = 0;

        sc.Subgraph[SG_PRISM_BEAR].Name              = "PRISM Sell";
        sc.Subgraph[SG_PRISM_BEAR].DrawStyle         = DRAWSTYLE_ARROW_DOWN;
        sc.Subgraph[SG_PRISM_BEAR].PrimaryColor      = RGB(239, 83, 80);
        sc.Subgraph[SG_PRISM_BEAR].LineWidth         = 3;
        sc.Subgraph[SG_PRISM_BEAR].DrawZeros         = 0;

        sc.Subgraph[SG_PRISM_MQ_BULL].Name           = "PRISM MQ Buy";
        sc.Subgraph[SG_PRISM_MQ_BULL].DrawStyle      = DRAWSTYLE_POINT;
        sc.Subgraph[SG_PRISM_MQ_BULL].PrimaryColor   = RGB(38, 166, 154);
        sc.Subgraph[SG_PRISM_MQ_BULL].LineWidth      = 4;
        sc.Subgraph[SG_PRISM_MQ_BULL].DrawZeros      = 0;

        sc.Subgraph[SG_PRISM_MQ_BEAR].Name           = "PRISM MQ Sell";
        sc.Subgraph[SG_PRISM_MQ_BEAR].DrawStyle      = DRAWSTYLE_POINT;
        sc.Subgraph[SG_PRISM_MQ_BEAR].PrimaryColor   = RGB(239, 83, 80);
        sc.Subgraph[SG_PRISM_MQ_BEAR].LineWidth      = 4;
        sc.Subgraph[SG_PRISM_MQ_BEAR].DrawZeros      = 0;

        // Raw output subgraphs (enhancement — hidden by default)
        sc.Subgraph[SG_RAW_ER].Name                  = "ER (hidden)";
        sc.Subgraph[SG_RAW_ER].DrawStyle             = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_RAW_CCO].Name                 = "CCO (hidden)";
        sc.Subgraph[SG_RAW_CCO].DrawStyle            = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_RAW_KAMA_ALIGN].Name          = "KAMA Align (hidden)";
        sc.Subgraph[SG_RAW_KAMA_ALIGN].DrawStyle     = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_RAW_AO_EFF_LEN].Name          = "AO Eff Len (hidden)";
        sc.Subgraph[SG_RAW_AO_EFF_LEN].DrawStyle     = DRAWSTYLE_IGNORE;

        sc.Subgraph[SG_TC_K10].Name                  = "TC KAMA K10 (hidden)";
        sc.Subgraph[SG_TC_K10].DrawStyle             = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_TC_K25].Name                  = "TC KAMA K25 (hidden)";
        sc.Subgraph[SG_TC_K25].DrawStyle             = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_TC_K50].Name                  = "TC KAMA K50 (hidden)";
        sc.Subgraph[SG_TC_K50].DrawStyle             = DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_TC_K75].Name                  = "TC KAMA K75 (hidden)";
        sc.Subgraph[SG_TC_K75].DrawStyle             = DRAWSTYLE_IGNORE;

        // Hidden storage subgraphs (all DRAWSTYLE_IGNORE)
        for (int i = SG_FIRST_HIDDEN_STORE; i <= SG_LAST_HIDDEN_STORE; i++) {
            sc.Subgraph[i].DrawStyle  = DRAWSTYLE_IGNORE;
            sc.Subgraph[i].DrawZeros  = 1;
        }
        sc.Subgraph[SG_KAMA_STORE_A].Name   = "Store A (KAMA k05-k30)";
        sc.Subgraph[SG_KAMA_STORE_B].Name   = "Store B (KAMA k35-k60)";
        sc.Subgraph[SG_KAMA_STORE_C].Name   = "Store C (KAMA k65-k90)";
        sc.Subgraph[SG_KAMA_STORE_D].Name   = "Store D (SRSI-K + SC work)";
        sc.Subgraph[SG_OHLC4_STORE].Name    = "Store ohlc4";
        sc.Subgraph[SG_FHA_CLOSE_PREV].Name = "FHA close prev";
        sc.Subgraph[SG_LRHA_CLOSE_PREV].Name= "LRHA close prev";
        sc.Subgraph[SG_CVD_STORE].Name      = "CVD store";
        sc.Subgraph[SG_SC_CCO].Name         = "SC CCO work";
        sc.Subgraph[SG_AI_KAMA_PREV].Name   = "AI KAMA prev";
        sc.Subgraph[SG_AI_MACD].Name        = "AI MACD";
        sc.Subgraph[SG_AI_HIST].Name        = "AI Hist";
        sc.Subgraph[SG_PRISM_ST1_LINE].Name = "PRISM ST1 (hidden)";
        sc.Subgraph[SG_PRISM_ST2_LINE].Name = "PRISM ST2 (hidden)";
        sc.Subgraph[SG_PRISM_POLY].Name     = "PRISM Poly (hidden)";
        sc.Subgraph[SG_RRG_STORE].Name      = "RRG ATR store";

        // ── Input definitions ─────────────────────────────────────────────────

        sc.Input[IN_SIM_MODE].Name = "Settings Mode";
        sc.Input[IN_SIM_MODE].SetCustomInputStrings("Simple;Advanced");
        sc.Input[IN_SIM_MODE].SetCustomInputIndex(0);  // Pine default: Simple

        sc.Input[IN_BG_MODE].Name = "Chart Background";
        sc.Input[IN_BG_MODE].SetCustomInputStrings("Dark Background;Light Background");
        sc.Input[IN_BG_MODE].SetCustomInputIndex(0);

        sc.Input[IN_DARK_THEME].Name = "Dark Theme";
        sc.Input[IN_DARK_THEME].SetCustomInputStrings(
            "Modern;Terminal;Cyberpunk;Neon Noir;Phosphor;Fire & Ice;Slate;"
            "Blood & Greed;Gold Standard;Ultraviolet;Infrared;Toxic;"
            "Crimson Tide;Vaporwave;Matrix;Arctic");
        sc.Input[IN_DARK_THEME].SetCustomInputIndex(0);

        sc.Input[IN_LIGHT_THEME].Name = "Light Theme";
        sc.Input[IN_LIGHT_THEME].SetCustomInputStrings(
            "Classic;Woodland;Solar;Twilight;Parchment;Shoreline;Graphite;"
            "Harvest;Guilded;Amethyst;Forge;Briar;Scarlet;Dusk;Fern;Nordic");
        sc.Input[IN_LIGHT_THEME].SetCustomInputIndex(0);

        sc.Input[IN_INFO_ENABLE].Name = "Show Info Panel";
        sc.Input[IN_INFO_ENABLE].SetYesNo(1);

        sc.Input[IN_INFO_LOCATION].Name = "Info Panel Location";
        sc.Input[IN_INFO_LOCATION].SetCustomInputStrings(
            "Top Left;Middle Left;Bottom Left;Middle Right;Bottom Right");
        sc.Input[IN_INFO_LOCATION].SetCustomInputIndex(3);  // Pine default: Middle Right

        sc.Input[IN_PRISM_SIG_OFFSET].Name = "Signal Offset (ATR multiplier)";
        sc.Input[IN_PRISM_SIG_OFFSET].SetFloat(0.9f);
        sc.Input[IN_PRISM_SIG_OFFSET].SetFloatLimits(0.0f, 2.0f);  // Pine maxval

        sc.Input[IN_GLOW_TC_ENABLE].Name = "TC Glow Effect";
        sc.Input[IN_GLOW_TC_ENABLE].SetYesNo(1);

        sc.Input[IN_PRISM_SIG_SIZE].Name = "Signal Size";
        sc.Input[IN_PRISM_SIG_SIZE].SetCustomInputStrings("Small;Normal");
        sc.Input[IN_PRISM_SIG_SIZE].SetCustomInputIndex(0);

        // Module enables
        sc.Input[IN_CMA_ENABLE].Name = "Enable MA Ribbon";
        sc.Input[IN_CMA_ENABLE].SetYesNo(1);

        sc.Input[IN_SC_ENABLE].Name = "Enable Super Channel";
        sc.Input[IN_SC_ENABLE].SetYesNo(1);

        sc.Input[IN_ALT_ENABLE].Name = "Enable Indicator Candles";
        sc.Input[IN_ALT_ENABLE].SetYesNo(1);

        sc.Input[IN_TC_ENABLE].Name = "Enable Trend Cloud";
        sc.Input[IN_TC_ENABLE].SetYesNo(1);

        sc.Input[IN_PRISM_ENABLE].Name = "Enable PRISM Signals";
        sc.Input[IN_PRISM_ENABLE].SetYesNo(1);

        sc.Input[IN_FORECAST_ENABLE].Name = "Enable Boundary Forecast";
        sc.Input[IN_FORECAST_ENABLE].SetYesNo(1);

        sc.Input[IN_TRG_ENABLE].Name = "Enable Trend Regime Gate";
        sc.Input[IN_TRG_ENABLE].SetYesNo(1);

        sc.Input[IN_AO_ENABLE].Name = "Enable Auto-Optimizer";
        sc.Input[IN_AO_ENABLE].SetYesNo(1);

        sc.Input[IN_CANDLE_COL_ENABLE].Name = "Enable Candle Coloring";
        sc.Input[IN_CANDLE_COL_ENABLE].SetYesNo(0);  // Pine/TV default: off

        // MA Ribbon
        sc.Input[IN_CMA_LENGTH].Name = "MA Ribbon Length";
        sc.Input[IN_CMA_LENGTH].SetInt(20);
        sc.Input[IN_CMA_LENGTH].SetIntLimits(5, 200);

        sc.Input[IN_CMA_OFFSET1].Name = "ALMA Offset 1 (fast)";
        sc.Input[IN_CMA_OFFSET1].SetFloat(0.85f);

        sc.Input[IN_CMA_OFFSET2].Name = "ALMA Offset 2 (slow)";
        sc.Input[IN_CMA_OFFSET2].SetFloat(0.77f);

        sc.Input[IN_CMA_SIGMA].Name = "ALMA Sigma";
        sc.Input[IN_CMA_SIGMA].SetFloat(6.0f);

        sc.Input[IN_CMA_BICOLOR].Name = "Bi-Color Ribbon Lines";
        sc.Input[IN_CMA_BICOLOR].SetYesNo(0);

        sc.Input[IN_CMA_DIR_COLOR].Name = "Dynamic Direction Color";
        sc.Input[IN_CMA_DIR_COLOR].SetYesNo(1);

        // Super Channel: no tuning inputs — Pine hardcodes every parameter
        // (KC/BB lengths & multipliers, MFI/CCI/StochRSI lengths, OB/OS levels);
        // input indices 30-39 stay reserved

        // Candles
        sc.Input[IN_CANDLE_TYPE].Name = "Candle Type";
        sc.Input[IN_CANDLE_TYPE].SetCustomInputStrings(
            "Regular;Heikin Ashi;R-Squared Adaptive;LinReg Heikin Ashi;LinReg Candles");
        sc.Input[IN_CANDLE_TYPE].SetCustomInputIndex(2);  // default: R2 Adaptive

        sc.Input[IN_CVD_ENABLE].Name = "CVD Border Highlight";
        sc.Input[IN_CVD_ENABLE].SetYesNo(1);

        sc.Input[IN_CVD_THRESHOLD].Name = "CVD Strong Threshold (percentile)";
        sc.Input[IN_CVD_THRESHOLD].SetFloat(95.0f);

        sc.Input[IN_LINREG_LEN].Name = "Regression Length";
        sc.Input[IN_LINREG_LEN].SetInt(10);
        sc.Input[IN_LINREG_LEN].SetIntLimits(2, 500);

        // Candle Coloring
        sc.Input[IN_CANDLE_COL_MODE].Name = "Candle Color Mode";
        sc.Input[IN_CANDLE_COL_MODE].SetCustomInputStrings(
            "MA Ribbon;Trend Regime;Dual Confirmation;Adaptive Impulse;Heikin Ashi;Trend Cloud Base");
        sc.Input[IN_CANDLE_COL_MODE].SetCustomInputIndex(1);  // Pine default: Trend Regime

        // Trend Cloud: KAMA base length (100) and cloud multiplier (10.0) are
        // Pine hardcodes — indices 60-62 stay reserved
        sc.Input[IN_TC_BODY_ENABLE].Name = "Enable Trend Cloud Body";
        sc.Input[IN_TC_BODY_ENABLE].SetYesNo(1);

        sc.Input[IN_TC_SLOPE_COLOR].Name = "Color Base by Trend Direction";
        sc.Input[IN_TC_SLOPE_COLOR].SetYesNo(1);

        // Trend Regime Gate
        sc.Input[IN_TRG_VOTES_REQ].Name = "Regime Gate Votes Required";
        sc.Input[IN_TRG_VOTES_REQ].SetInt(2);
        sc.Input[IN_TRG_VOTES_REQ].SetIntLimits(1, 3);

        sc.Input[IN_TRG_KAMA_THRESH].Name = "Regime KAMA Alignment %";
        sc.Input[IN_TRG_KAMA_THRESH].SetFloat(62.0f);

        sc.Input[IN_TRG_HURST_THRESH].Name = "Regime Hurst Threshold";
        sc.Input[IN_TRG_HURST_THRESH].SetFloat(0.50f);

        sc.Input[IN_TRG_HURST_LEN].Name = "Regime Hurst Lookback";
        sc.Input[IN_TRG_HURST_LEN].SetInt(100);
        sc.Input[IN_TRG_HURST_LEN].SetIntLimits(10, 300);

        sc.Input[IN_TRG_HURST_FREQ].Name = "Regime Hurst Update Freq (bars)";
        sc.Input[IN_TRG_HURST_FREQ].SetInt(5);
        sc.Input[IN_TRG_HURST_FREQ].SetIntLimits(1, 50);

        sc.Input[IN_TRG_ACCEL_SMOOTH].Name = "Regime Accel EMA Period";
        sc.Input[IN_TRG_ACCEL_SMOOTH].SetInt(3);
        sc.Input[IN_TRG_ACCEL_SMOOTH].SetIntLimits(1, 50);

        // PRISM Signals
        sc.Input[IN_PRISM_BASE_LEN].Name = "PRISM Base Lookback";
        sc.Input[IN_PRISM_BASE_LEN].SetInt(40);
        sc.Input[IN_PRISM_BASE_LEN].SetIntLimits(10, 200);

        sc.Input[IN_PRISM_ALPHA_FACTOR].Name = "PRISM Alpha Rail Factor";
        sc.Input[IN_PRISM_ALPHA_FACTOR].SetFloat(0.2f);

        sc.Input[IN_PRISM_SIGMA_FACTOR].Name = "PRISM Sigma Rail Factor";
        sc.Input[IN_PRISM_SIGMA_FACTOR].SetFloat(0.5f);

        sc.Input[IN_PRISM_BAR_QUAL_MIN].Name = "PRISM Bar Quality Min Ratio";
        sc.Input[IN_PRISM_BAR_QUAL_MIN].SetFloat(0.30f);

        sc.Input[IN_PRISM_BAR_QUAL_WAIT].Name = "PRISM Bar Quality Max Wait (bars)";
        sc.Input[IN_PRISM_BAR_QUAL_WAIT].SetInt(3);

        sc.Input[IN_PRISM_ER_THRESH].Name = "PRISM ER Quality Threshold";
        sc.Input[IN_PRISM_ER_THRESH].SetFloat(0.20f);

        sc.Input[IN_PRISM_KAMA_SLOPE_MIN].Name = "PRISM KAMA Slope Min";
        sc.Input[IN_PRISM_KAMA_SLOPE_MIN].SetFloat(0.03f);

        sc.Input[IN_PRISM_ALPHA_PERIOD].Name = "Alpha Rail Period";
        sc.Input[IN_PRISM_ALPHA_PERIOD].SetInt(10);
        sc.Input[IN_PRISM_ALPHA_PERIOD].SetIntLimits(1, 100);

        sc.Input[IN_PRISM_SIGMA_PERIOD].Name = "Sigma Rail Period";
        sc.Input[IN_PRISM_SIGMA_PERIOD].SetInt(20);
        sc.Input[IN_PRISM_SIGMA_PERIOD].SetIntLimits(1, 100);

        sc.Input[IN_PRISM_ADAPTIVE].Name = "PRISM Adaptive (timeframe scaling)";
        sc.Input[IN_PRISM_ADAPTIVE].SetYesNo(1);

        sc.Input[IN_PRISM_STRUCT_LOCK].Name = "PRISM Structure Lock";
        sc.Input[IN_PRISM_STRUCT_LOCK].SetYesNo(1);

        sc.Input[IN_PRISM_BQ_ENABLE].Name = "PRISM Bar Quality Gate";
        sc.Input[IN_PRISM_BQ_ENABLE].SetYesNo(1);

        sc.Input[IN_PRISM_MQ_ENABLE].Name = "PRISM Quality Gate (marginal dots)";
        sc.Input[IN_PRISM_MQ_ENABLE].SetYesNo(1);

        // Auto-Optimizer (base length comes from PRISM Base Lookback; index 90 reserved)
        sc.Input[IN_AO_SPREAD_PCT].Name = "AO Length Spread %";
        sc.Input[IN_AO_SPREAD_PCT].SetFloat(50.0f);
        sc.Input[IN_AO_SPREAD_PCT].SetFloatLimits(0.0f, 95.0f);

        sc.Input[IN_AO_NOISE_SUPPRESS].Name = "PRISM Noise Suppression";
        sc.Input[IN_AO_NOISE_SUPPRESS].SetYesNo(0);  // Pine default: off

        sc.Input[IN_AO_TIER1].Name = "AO Tier 1 ATR — Weak Win";
        sc.Input[IN_AO_TIER1].SetFloat(0.75f);

        sc.Input[IN_AO_TIER2].Name = "AO Tier 2 ATR — Solid Win";
        sc.Input[IN_AO_TIER2].SetFloat(1.5f);

        sc.Input[IN_AO_TIER3].Name = "AO Tier 3 ATR — Strong Win";
        sc.Input[IN_AO_TIER3].SetFloat(2.5f);

        sc.Input[IN_AO_MAX_BARS].Name = "AO Max Bars to Resolve";
        sc.Input[IN_AO_MAX_BARS].SetInt(7);
        sc.Input[IN_AO_MAX_BARS].SetIntLimits(3, 20);

        sc.Input[IN_AO_LOOKBACK].Name = "AO Signal Lookback";
        sc.Input[IN_AO_LOOKBACK].SetInt(20);
        sc.Input[IN_AO_LOOKBACK].SetIntLimits(5, 50);

        // Boundary Forecast
        sc.Input[IN_FC_BARS].Name = "Forecast Bars";
        sc.Input[IN_FC_BARS].SetInt(10);
        sc.Input[IN_FC_BARS].SetIntLimits(1, 20);

        sc.Input[IN_FC_REG_LEN].Name = "Forecast Regression Length";
        sc.Input[IN_FC_REG_LEN].SetInt(25);
        sc.Input[IN_FC_REG_LEN].SetIntLimits(10, 50);  // Pine minval/maxval

        sc.Input[IN_FC_MODE].Name = "Forecast Mode";
        sc.Input[IN_FC_MODE].SetCustomInputStrings("Regression;Slope Extension");
        sc.Input[IN_FC_MODE].SetCustomInputIndex(1);  // Pine default: Slope Extension

        sc.Input[IN_FC_SHOW_SC].Name = "Show Projected SC Bands";
        sc.Input[IN_FC_SHOW_SC].SetYesNo(1);

        sc.Input[IN_FC_SHOW_TC].Name = "Show Projected TC Bands";
        sc.Input[IN_FC_SHOW_TC].SetYesNo(1);

        // ── v9.3 Signal Enhancements [no Pine equivalent] ─────────────────────
        sc.Input[IN_SIG_CONFIRM_MODE].Name = "PRISM Signal Confirmation";
        sc.Input[IN_SIG_CONFIRM_MODE].SetCustomInputStrings(
            "Live (TradingView parity);Bar Close (confirmed)");
        sc.Input[IN_SIG_CONFIRM_MODE].SetCustomInputIndex(0);

        sc.Input[IN_OF_TRUE_DELTA].Name = "Delta Source: Real Bid/Ask Volume";
        sc.Input[IN_OF_TRUE_DELTA].SetYesNo(1);   // auto-falls back per bar

        sc.Input[IN_OF_AGREE_GATE].Name = "OF Gate: Delta Agreement";
        sc.Input[IN_OF_AGREE_GATE].SetYesNo(1);

        sc.Input[IN_OF_AGREE_BARS].Name = "OF Gate: Agreement Window (bars)";
        sc.Input[IN_OF_AGREE_BARS].SetInt(2);
        sc.Input[IN_OF_AGREE_BARS].SetIntLimits(1, 5);

        sc.Input[IN_OF_DIV_VETO].Name = "OF Gate: CVD Divergence Veto";
        sc.Input[IN_OF_DIV_VETO].SetYesNo(0);

        sc.Input[IN_OF_DIV_LOOKBACK].Name = "OF Gate: Divergence Lookback (bars)";
        sc.Input[IN_OF_DIV_LOOKBACK].SetInt(20);
        sc.Input[IN_OF_DIV_LOOKBACK].SetIntLimits(10, 50);

        sc.Input[IN_OF_ABSORB_VETO].Name = "OF Gate: Absorption Veto";
        sc.Input[IN_OF_ABSORB_VETO].SetYesNo(0);

        sc.Input[IN_OF_ABSORB_RETRACE].Name = "OF Gate: Absorption Retrace";
        sc.Input[IN_OF_ABSORB_RETRACE].SetFloat(0.60f);
        sc.Input[IN_OF_ABSORB_RETRACE].SetFloatLimits(0.30f, 0.90f);

        sc.Input[IN_PRISM_GATE_STRICT].Name = "PRISM: Strict Gate Mode";
        sc.Input[IN_PRISM_GATE_STRICT].SetYesNo(0);

        sc.Input[IN_PRISM_FLIP_GUARD].Name = "PRISM: Opposite-Signal Cooldown (bars, 0=off)";
        sc.Input[IN_PRISM_FLIP_GUARD].SetInt(0);
        sc.Input[IN_PRISM_FLIP_GUARD].SetIntLimits(0, 20);

        sc.Input[IN_ADAPT_TICK_EST].Name = "PRISM Adaptive: Estimate Bar Seconds (non-time bars)";
        sc.Input[IN_ADAPT_TICK_EST].SetYesNo(1);

        sc.Input[IN_ER_ADAPT_ENABLE].Name = "Quality Gate: Adaptive ER Percentile";
        sc.Input[IN_ER_ADAPT_ENABLE].SetYesNo(0);

        sc.Input[IN_ER_ADAPT_LOOKBACK].Name = "Adaptive ER: Lookback (bars)";
        sc.Input[IN_ER_ADAPT_LOOKBACK].SetInt(250);
        sc.Input[IN_ER_ADAPT_LOOKBACK].SetIntLimits(50, 1000);

        sc.Input[IN_ER_ADAPT_PCT].Name = "Adaptive ER: Chop Percentile";
        sc.Input[IN_ER_ADAPT_PCT].SetFloat(40.0f);
        sc.Input[IN_ER_ADAPT_PCT].SetFloatLimits(5.0f, 95.0f);

        sc.Input[IN_TRG_CAL_ENABLE].Name = "Regime Gate: Calibrated Hurst/Accel";
        sc.Input[IN_TRG_CAL_ENABLE].SetYesNo(0);

        sc.Input[IN_TRG_HURST_CAL].Name = "Calibrated Hurst Vote Threshold";
        sc.Input[IN_TRG_HURST_CAL].SetFloat(0.72f);
        sc.Input[IN_TRG_HURST_CAL].SetFloatLimits(0.50f, 0.95f);

        sc.Input[IN_TRG_ACCEL_DB].Name = "Calibrated Accel Vote Deadband";
        sc.Input[IN_TRG_ACCEL_DB].SetFloat(0.005f);
        sc.Input[IN_TRG_ACCEL_DB].SetFloatLimits(0.0f, 0.05f);

        sc.Input[IN_RRG_ENABLE].Name = "Range Regime Gate (CHOP/ADX/ATR%)";
        sc.Input[IN_RRG_ENABLE].SetYesNo(0);

        sc.Input[IN_RRG_CHOP_LEN].Name = "RRG: Choppiness Length";
        sc.Input[IN_RRG_CHOP_LEN].SetInt(14);
        sc.Input[IN_RRG_CHOP_LEN].SetIntLimits(5, 100);

        sc.Input[IN_RRG_CHOP_THRESH].Name = "RRG: Choppiness Threshold";
        sc.Input[IN_RRG_CHOP_THRESH].SetFloat(61.8f);

        sc.Input[IN_RRG_ADX_LEN].Name = "RRG: ADX Length";
        sc.Input[IN_RRG_ADX_LEN].SetInt(14);
        sc.Input[IN_RRG_ADX_LEN].SetIntLimits(5, 100);

        sc.Input[IN_RRG_ADX_FLOOR].Name = "RRG: ADX Floor";
        sc.Input[IN_RRG_ADX_FLOOR].SetFloat(20.0f);

        sc.Input[IN_RRG_ATRP_LOOKBACK].Name = "RRG: ATR Percentile Lookback";
        sc.Input[IN_RRG_ATRP_LOOKBACK].SetInt(200);
        sc.Input[IN_RRG_ATRP_LOOKBACK].SetIntLimits(50, 1000);

        sc.Input[IN_RRG_ATRP_FLOOR].Name = "RRG: ATR Percentile Floor";
        sc.Input[IN_RRG_ATRP_FLOOR].SetFloat(25.0f);

        sc.Input[IN_AO_STOP_ATR].Name = "AO: Stop-Out ATR (0=off)";
        sc.Input[IN_AO_STOP_ATR].SetFloat(0.0f);
        sc.Input[IN_AO_STOP_ATR].SetFloatLimits(0.0f, 5.0f);

        sc.Input[IN_SIGLOG_ENABLE].Name = "Log Signals to CSV";
        sc.Input[IN_SIGLOG_ENABLE].SetYesNo(1);

        sc.Input[IN_SIGLOG_HORIZON].Name = "Signal Log: Resolution Bars (0=AO Max Bars)";
        sc.Input[IN_SIGLOG_HORIZON].SetInt(0);
        sc.Input[IN_SIGLOG_HORIZON].SetIntLimits(0, 50);

        sc.Input[IN_SIGLOG_INCLUDE_MQ].Name = "Signal Log: Include Marginal Dots";
        sc.Input[IN_SIGLOG_INCLUDE_MQ].SetYesNo(1);

        return;
    }

    // ── LastCallToFunction cleanup ────────────────────────────────────────────
    if (sc.LastCallToFunction) {
        fc_delete_all(sc);
        info_delete_all(sc);
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, WATERMARK_ID);
        ta_free_state(sc);   // frees all recurrence state, deques and scratch
        return;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Per-bar computation
    // ─────────────────────────────────────────────────────────────────────────
    int idx = sc.Index;

    // ── Pine `var` commit/rollback semantics ──────────────────────────────────
    // Pine evaluates recurrences once per closed bar; ACSIL re-runs the live
    // bar on every tick. Keep two copies of all recurrence state: `committed`
    // (as of the last closed bar) and `working` (the bar being computed).
    TAState& tas = *ta_get_state(sc);
    bool stale_pass = false;   // set when limping on unreconstructable state
    bool bar_advanced = false; // first call for a genuinely new bar [v9.3]
    if (idx == 0) {
        // Full recalculation start: reset ALL recurrence state, no exceptions.
        tas.committed = TARecurrence();
        tas.working   = TARecurrence();
        tas.last_index = 0;
        // [v9.3] logger pendings reference bar state that a recalc may have
        // revised — drop them and re-init the live watermark on the next
        // gated call (prevents duplicate/garbage rows after recalcs)
        tas.log_pending.clear();
        tas.log_watermark_time = -1.0;
    } else if (idx > tas.last_index) {
        // First call for a new bar: the previous bar's final working state
        // becomes the committed baseline.
        tas.committed = tas.working;
        tas.last_index = idx;
        bar_advanced = true;
    } else if (idx == tas.last_index) {
        // Intrabar re-tick: roll the working state back to the committed
        // baseline and recompute the live bar from scratch (Pine rollback).
        tas.working = tas.committed;
    } else {
        // Jumped backward without passing through index 0 (historical data
        // revision). Scalar state cannot be reconstructed mid-series — request
        // a full recalculation and limp through this update on stale state.
        tas.working = tas.committed;
        sc.FlagFullRecalculate = 1;
        stale_pass = true;
    }

    // ── 0. ATR-14 — Wilder RMA, matching Pine ta.atr(14) ─────────────────────
    double atr14 = ta_wilder_atr_step(sc, idx, 14, tas.working.atr14);

    // ── 0. Theme ──────────────────────────────────────────────────────────────
    bool is_dark = (sc.Input[IN_BG_MODE].GetIndex() == 0);
    int theme_idx = is_dark ? sc.Input[IN_DARK_THEME].GetIndex()
                            : sc.Input[IN_LIGHT_THEME].GetIndex();
    ThemeProfile theme = get_theme(theme_idx, is_dark);
    // Blend target for faked transparency (Pine alpha → pre-blend toward bg)
    theme.bg = is_dark ? RGB(0, 0, 0) : RGB(255, 255, 255);

    // ── Settings Mode (Pine §0: Simple silently overrides tuning numerics) ────
    bool is_simple = (sc.Input[IN_SIM_MODE].GetIndex() == 0);

    // ── Read module enables ───────────────────────────────────────────────────
    // (Pine keeps separate Simple/Advanced enable inputs; SC exposes one set
    // that acts as the "Main Tools" panel in both modes — deliberate deviation)
    bool cma_enable     = (bool)sc.Input[IN_CMA_ENABLE].GetYesNo();
    bool sc_enable      = (bool)sc.Input[IN_SC_ENABLE].GetYesNo();
    bool alt_enable     = (bool)sc.Input[IN_ALT_ENABLE].GetYesNo();
    bool tc_enable      = (bool)sc.Input[IN_TC_ENABLE].GetYesNo();
    bool prism_enable   = (bool)sc.Input[IN_PRISM_ENABLE].GetYesNo();
    bool fc_enable      = (bool)sc.Input[IN_FORECAST_ENABLE].GetYesNo();
    bool trg_enable     = (bool)sc.Input[IN_TRG_ENABLE].GetYesNo();
    bool ao_enable      = (bool)sc.Input[IN_AO_ENABLE].GetYesNo();
    bool cc_enable      = (bool)sc.Input[IN_CANDLE_COL_ENABLE].GetYesNo();
    bool info_enable    = (bool)sc.Input[IN_INFO_ENABLE].GetYesNo();
    bool glow_enable    = (bool)sc.Input[IN_GLOW_TC_ENABLE].GetYesNo();
    bool tc_body_enable = (bool)sc.Input[IN_TC_BODY_ENABLE].GetYesNo();
    // Study draws candles over the native bars → bar mask on, underneath off
    bool mask_on        = (alt_enable || cc_enable);

    // ── v9.3 enhancement inputs ───────────────────────────────────────────────
    bool confirm_mode   = (sc.Input[IN_SIG_CONFIRM_MODE].GetIndex() == 1);
    bool of_true_delta  = (bool)sc.Input[IN_OF_TRUE_DELTA].GetYesNo();
    bool of_agree_gate  = (bool)sc.Input[IN_OF_AGREE_GATE].GetYesNo();
    int  of_agree_bars  = sc.Input[IN_OF_AGREE_BARS].GetInt();
    bool of_div_veto    = (bool)sc.Input[IN_OF_DIV_VETO].GetYesNo();
    int  of_div_lb      = sc.Input[IN_OF_DIV_LOOKBACK].GetInt();
    bool of_absorb_veto = (bool)sc.Input[IN_OF_ABSORB_VETO].GetYesNo();
    bool gate_strict    = (bool)sc.Input[IN_PRISM_GATE_STRICT].GetYesNo();
    int  flip_guard     = sc.Input[IN_PRISM_FLIP_GUARD].GetInt();
    bool rrg_enable     = (bool)sc.Input[IN_RRG_ENABLE].GetYesNo();

    // ── v9.3 once-per-recalc chart scans ─────────────────────────────────────
    if (idx == 0) {
        // Real bid/ask aggressor volume present? (>=50% of recent volume bars
        // carry a split — protects against mixed backfill segments)
        int n_scan = std::min(sc.ArraySize, 500);
        int n_vol = 0, n_ba = 0;
        for (int i = sc.ArraySize - n_scan; i < sc.ArraySize; i++) {
            if (i < 0) continue;
            if (sc.Volume[i] > 0.0f) {
                n_vol++;
                if (sc.AskVolume[i] + sc.BidVolume[i] > 0.0f) n_ba++;
            }
        }
        tas.of_data_present = (n_vol > 0) && (n_ba * 2 >= n_vol);

        // Effective seconds per bar: authoritative for time charts; median of
        // the most recent 300 bar durations for tick/volume/range charts
        // (sc.SecondsPerBar is deprecated and 0 on non-time bars — PRISM
        // Adaptive was silently dead on every tick chart before this).
        n_ACSIL::s_BarPeriod bp;
        sc.GetBarPeriodParameters(bp);
        bool is_time_bars = (bp.ChartDataType == INTRADAY_DATA
                          && bp.IntradayChartBarPeriodType == IBPT_DAYS_MINS_SECS);
        if (is_time_bars) {
            tas.est_sec_per_bar = (double)bp.IntradayChartBarPeriodParameter1;
            tas.log_period_str.Format("%ds", bp.IntradayChartBarPeriodParameter1);
        } else {
            tas.est_sec_per_bar = 0.0;
            if ((bool)sc.Input[IN_ADAPT_TICK_EST].GetYesNo() && sc.ArraySize > 20) {
                int nd = std::min(sc.ArraySize - 1, 300);
                float* dur = ta_scratch(tas.scratch_a, nd);
                for (int k = 0; k < nd; k++) {
                    int j = sc.ArraySize - 1 - k;
                    dur[k] = (float)((sc.BaseDateTimeIn[j].GetAsDouble()
                                    - sc.BaseDateTimeIn[j - 1].GetAsDouble()) * 86400.0);
                }
                std::nth_element(dur, dur + nd / 2, dur + nd);
                tas.est_sec_per_bar = std::max(1.0, (double)dur[nd / 2]);
            }
            const char* bt = (bp.IntradayChartBarPeriodType == IBPT_NUM_TRADES_PER_BAR) ? "T"
                           : (bp.IntradayChartBarPeriodType == IBPT_VOLUME_PER_BAR) ? "V" : "R";
            tas.log_period_str.Format("%d%s", bp.IntradayChartBarPeriodParameter1, bt);
        }
    }

    // Simple-mode numeric overrides (Pine TA9:504,532,536-538,675-676)
    int    eff_hurst_len    = is_simple ? 50   : sc.Input[IN_TRG_HURST_LEN].GetInt();
    int    eff_accel_smooth = is_simple ? 5    : sc.Input[IN_TRG_ACCEL_SMOOTH].GetInt();
    double eff_ka_thresh    = is_simple ? 0.72 : (double)sc.Input[IN_TRG_KAMA_THRESH].GetFloat() / 100.0;
    double eff_hurst_thresh = is_simple ? 0.55 : (double)sc.Input[IN_TRG_HURST_THRESH].GetFloat();
    int    eff_votes_req    = is_simple ? 2    : sc.Input[IN_TRG_VOTES_REQ].GetInt();

    // ── Display gating (Pine parity: enables hide PLOTS, never computation) ──
    // The Trend Cloud and Super Channel series feed PRISM elevation, the Trend
    // Regime Gate, candle coloring, alerts and the forecast, so their modules
    // always run; visibility is switched here via DrawStyle. Toggling any of
    // these inputs triggers a full recalculation, so idx==0 runs per change.
    if (idx == 0) {
        // Fills render with REAL transparency: Sierra alpha-blends the
        // TRANSPARENT_FILL pairs at TA_FILL_TRANSPARENCY over whatever is
        // underneath, so the native candles and studies drawn below us show
        // through the cloud/channel/ribbon. Per-fill Pine alphas are encoded
        // in the DataColors via theme_fill_dim() (TA_ColorThemes.h). The v9.0
        // pin-to-0 made the fills opaque blankets — with Indicator Candles
        // off, the chart's own candles vanished under them. Enforced at
        // runtime because SetDefaults doesn't reapply on DLL replacement.
        sc.SetChartStudyTransparencyLevel(sc.ChartNumber, sc.StudyGraphInstanceID,
                                          TA_FILL_TRANSPARENCY);
        // When the study draws no candles of its own, drop it UNDERNEATH the
        // main price graph (the docs' fix for "Fill* Draw Styles may overlap
        // the price bars"): bars stay fully crisp above the fills/glow, and
        // studies drawn after the main graph — e.g. a separate SuperTrend —
        // stay visible above our fills regardless of Studies-list order. With
        // Indicator Candles or Candle Coloring on, the study paints OVER the
        // native bars instead, with the bar-mask layer blanking them first.
        sc.DrawStudyUnderneathMainPriceGraph = mask_on ? 0 : 1;
        sc.Subgraph[SG_BAR_MASK_TOP].DrawStyle = mask_on ? DRAWSTYLE_FILL_RECTANGLE_TOP    : DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_BAR_MASK_BOT].DrawStyle = mask_on ? DRAWSTYLE_FILL_RECTANGLE_BOTTOM : DRAWSTYLE_IGNORE;

        // Post-renumber self-healing: per-instance subgraph settings are keyed
        // by INDEX and SetDefaults doesn't reapply on DLL replacement, so a
        // stale instance would render the v9.1 settings on the wrong (shifted)
        // subgraphs. Enforce every layout-critical DrawStyle/width/DrawZeros
        // here; re-adding the study (or Set Defaults) fixes names and colors.
        sc.Subgraph[SG_RIBBON_FILL_TOP].DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_TOP;
        sc.Subgraph[SG_RIBBON_FILL_BOT].DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_BOTTOM;
        sc.Subgraph[SG_ALMA1].DrawStyle           = DRAWSTYLE_LINE;
        sc.Subgraph[SG_ALMA2].DrawStyle           = DRAWSTYLE_LINE;
        sc.Subgraph[SG_CANDLE_HIGH].DrawStyle     = DRAWSTYLE_BAR_TOP;
        sc.Subgraph[SG_CANDLE_LOW].DrawStyle      = DRAWSTYLE_BAR_BOTTOM;
        sc.Subgraph[SG_CANDLE_OPEN].DrawStyle     = DRAWSTYLE_CANDLESTICK_BODY_OPEN;
        sc.Subgraph[SG_CANDLE_CLOSE].DrawStyle    = DRAWSTYLE_CANDLESTICK_BODY_CLOSE;
        sc.Subgraph[SG_PRISM_BULL].DrawStyle      = DRAWSTYLE_ARROW_UP;
        sc.Subgraph[SG_PRISM_BEAR].DrawStyle      = DRAWSTYLE_ARROW_DOWN;
        sc.Subgraph[SG_PRISM_MQ_BULL].DrawStyle   = DRAWSTYLE_POINT;
        sc.Subgraph[SG_PRISM_MQ_BEAR].DrawStyle   = DRAWSTYLE_POINT;
        sc.Subgraph[SG_CVD_BORDER].DrawStyle      = DRAWSTYLE_IGNORE;
        for (int i = SG_RAW_ER; i <= SG_LAST_HIDDEN_STORE; i++)
            sc.Subgraph[i].DrawStyle = DRAWSTYLE_IGNORE;  // 27..49 all hidden

        static const int GLOW_RT_SGS[4]   = {SG_TC_GLOW4, SG_TC_GLOW3, SG_TC_GLOW2, SG_TC_GLOW1};
        static const int GLOW_RT_WIDTH[4] = {20, 14, 10, 6};
        for (int g = 0; g < 4; g++)
            sc.Subgraph[GLOW_RT_SGS[g]].LineWidth = GLOW_RT_WIDTH[g];
        sc.Subgraph[SG_TC_BASE].LineWidth     = 3;
        sc.Subgraph[SG_TC_TOP].LineWidth      = 2;
        sc.Subgraph[SG_SC_TOP].LineWidth      = 2;
        sc.Subgraph[SG_SC_BOT].LineWidth      = 2;
        sc.Subgraph[SG_ALMA1].LineWidth       = 2;
        sc.Subgraph[SG_ALMA2].LineWidth       = 2;
        sc.Subgraph[SG_CANDLE_HIGH].LineWidth = 1;
        sc.Subgraph[SG_CANDLE_LOW].LineWidth  = 1;
        sc.Subgraph[SG_PRISM_MQ_BULL].LineWidth = 4;
        sc.Subgraph[SG_PRISM_MQ_BEAR].LineWidth = 4;

        for (int i = SG_BAR_MASK_TOP; i < SG_FIRST_HIDDEN_STORE; i++)
            sc.Subgraph[i].DrawZeros = 0;   // zeros = "not drawn" for visuals
        for (int i = SG_FIRST_HIDDEN_STORE; i <= SG_LAST_HIDDEN_STORE; i++)
            sc.Subgraph[i].DrawZeros = 1;   // stores keep legit zero values
        sc.Subgraph[SG_TC_BASE].DrawStyle     = tc_enable ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_TC_TOP].DrawStyle      = (tc_enable && tc_body_enable) ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_TC_FILL_TOP].DrawStyle = (tc_enable && tc_body_enable) ? DRAWSTYLE_TRANSPARENT_FILL_TOP : DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_TC_FILL_BOT].DrawStyle = (tc_enable && tc_body_enable) ? DRAWSTYLE_TRANSPARENT_FILL_BOTTOM : DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_TC_GLOW1].DrawStyle    = (tc_enable && glow_enable) ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_TC_GLOW2].DrawStyle    = (tc_enable && glow_enable) ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_TC_GLOW3].DrawStyle    = (tc_enable && glow_enable) ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_TC_GLOW4].DrawStyle    = (tc_enable && glow_enable) ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_SC_TOP].DrawStyle      = sc_enable ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_SC_BOT].DrawStyle      = sc_enable ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_SC_FILL_TOP].DrawStyle = sc_enable ? DRAWSTYLE_TRANSPARENT_FILL_TOP : DRAWSTYLE_IGNORE;
        sc.Subgraph[SG_SC_FILL_BOT].DrawStyle = sc_enable ? DRAWSTYLE_TRANSPARENT_FILL_BOTTOM : DRAWSTYLE_IGNORE;
    }

    // ── 3. MA Ribbon ─────────────────────────────────────────────────────────
    // Always computed (Pine computes the series unconditionally; the enable
    // input only hides the plots) — PRISM Structure Lock and the ribbon
    // crossover alerts need cma1/cma2 history even when the ribbon is hidden.
    MAribbonResult ribbon = compute_MAribbon(sc, tas, theme,
        sc.Input[IN_CMA_LENGTH].GetInt(),
        (double)sc.Input[IN_CMA_OFFSET1].GetFloat(),
        (double)sc.Input[IN_CMA_OFFSET2].GetFloat(),
        (double)sc.Input[IN_CMA_SIGMA].GetFloat(),
        (bool)sc.Input[IN_CMA_DIR_COLOR].GetYesNo(),
        (bool)sc.Input[IN_CMA_BICOLOR].GetYesNo(),
        cma_enable);

    // ── 4. Super Channel ─────────────────────────────────────────────────────
    // Always computed (Pine computes sc_cco unconditionally — the OB/OS alerts
    // fire even with the channel hidden). Visibility gated via DrawStyle above.
    SuperChannelResult sch = compute_SuperChannel(sc, tas, atr14, theme);

    // ── 5. Candles ────────────────────────────────────────────────────────────
    // Always computed (Pine computes cv_* and the CVD series unconditionally) —
    // PRISM's Quality Gate and elevation need cvd_rank/cvd_net even when
    // Indicator Candles are hidden. use_custom gates the subgraph writes.
    CandlesResult candles = compute_Candles(sc, tas, theme,
        candle_type_from_idx(sc.Input[IN_CANDLE_TYPE].GetIndex()),
        sc.Input[IN_LINREG_LEN].GetInt(),
        (double)sc.Input[IN_CVD_THRESHOLD].GetFloat(),
        // Pine eff_cvd_enable = alt_enable && cvd_border_enable (TA9:358);
        // cvd_rank/cvd_net stay ungated for PRISM elevation
        alt_enable && (bool)sc.Input[IN_CVD_ENABLE].GetYesNo(),
        alt_enable,      // use_custom (write to SG_CANDLE_*)
        of_true_delta,   // [v9.3 OF-1] real bid/ask delta (per-bar fallback)
        of_div_lb);      // [v9.3 OF-3] windowed cumulative-delta length
    if (!alt_enable) {
        if (cc_enable) {
            // Pine barcolor(): Indicator Candles off + Candle Coloring on
            // paints the RAW price candles with the mode color. Draw raw OHLC
            // here; the Candle Coloring block below applies the color.
            sc.Subgraph[SG_CANDLE_OPEN][idx]  = sc.Open[idx];
            sc.Subgraph[SG_CANDLE_HIGH][idx]  = sc.High[idx];
            sc.Subgraph[SG_CANDLE_LOW][idx]   = sc.Low[idx];
            sc.Subgraph[SG_CANDLE_CLOSE][idx] = sc.Close[idx];
        } else {
            // Pine parity: both off — the study draws NO candles; zeros are
            // skipped (DrawZeros=0), the bar mask is off and the study sits
            // underneath the main price graph, so the chart's own bars show
            // with their native colors. Fully automatic — no chart settings.
            sc.Subgraph[SG_CANDLE_OPEN][idx]  = 0.0f;
            sc.Subgraph[SG_CANDLE_HIGH][idx]  = 0.0f;
            sc.Subgraph[SG_CANDLE_LOW][idx]   = 0.0f;
            sc.Subgraph[SG_CANDLE_CLOSE][idx] = 0.0f;
        }
    }

    // ── 5A. Native-bar mask ───────────────────────────────────────────────────
    // While the study draws candles (indicator candles or coloring), blank the
    // chart's native bars with per-bar background-colored rectangles spanning
    // High..Low so they can't ghost around the synthetic candles. theme.bg is
    // the same background the theme fades already assume — keep the chart
    // background matched to the Dark/Light Background input.
    sc.Subgraph[SG_BAR_MASK_TOP][idx] = mask_on ? sc.High[idx] : 0.0f;
    sc.Subgraph[SG_BAR_MASK_BOT][idx] = mask_on ? sc.Low[idx]  : 0.0f;
    if (mask_on) {
        sc.Subgraph[SG_BAR_MASK_TOP].DataColor[idx] = theme.bg;
        sc.Subgraph[SG_BAR_MASK_BOT].DataColor[idx] = theme.bg;
    }

    // ── 6. Trend Cloud ────────────────────────────────────────────────────────
    // Always computed (Pine computes tc_base/k50/cloud_top unconditionally —
    // PRISM elevation, TRG, coloring and the forecast read this series even
    // with the cloud hidden). Visibility gated via DrawStyle above.
    TrendCloudResult tc = compute_TrendCloud(sc, tas, atr14, theme,
        (bool)sc.Input[IN_TC_SLOPE_COLOR].GetYesNo());

    // ── 6B. Trend Regime Gate ─────────────────────────────────────────────────
    // Always computed (Pine computes measures/votes unconditionally — the
    // enable gates only the suppress flags; keeps Hurst/accel state warm and
    // the info-panel regime rows live)
    TrendRegimeResult trg = compute_TrendRegimeGate(sc, tas, tc, atr14, trg_enable,
        eff_ka_thresh,
        eff_hurst_len,
        eff_hurst_thresh,
        sc.Input[IN_TRG_HURST_FREQ].GetInt(),
        eff_accel_smooth,
        eff_votes_req,
        (bool)sc.Input[IN_TRG_CAL_ENABLE].GetYesNo(),          // [v9.3 REGIME-3]
        (double)sc.Input[IN_TRG_HURST_CAL].GetFloat(),
        (double)sc.Input[IN_TRG_ACCEL_DB].GetFloat());
    // Per-bar suppress-flag history (hidden arrays) — the regime-activation
    // alerts need the previous bar's flags for rising-edge detection
    sc.Subgraph[SG_RAW_KAMA_ALIGN].Arrays[0][idx] = trg.suppress_sell ? 1.0f : 0.0f;
    sc.Subgraph[SG_RAW_KAMA_ALIGN].Arrays[1][idx] = trg.suppress_buy  ? 1.0f : 0.0f;
    // [v9.3] Per-bar TRG context history for the outcome logger
    sc.Subgraph[SG_RAW_KAMA_ALIGN].Arrays[2][idx] = (float)trg.bull_votes;
    sc.Subgraph[SG_RAW_KAMA_ALIGN].Arrays[3][idx] = (float)trg.bear_votes;
    sc.Subgraph[SG_RAW_KAMA_ALIGN].Arrays[4][idx] = (float)trg.hurst;

    // ── 6C. Range Regime Gate [v9.3 REGIME-2] ────────────────────────────────
    // Non-directional chop detector; range_active ORs into BOTH suppress
    // flags below, downgrading full arrows to marginal dots in detected
    // ranges via the existing PRISM path (never dropped, elevation intact).
    RangeRegimeResult rrg = compute_RangeRegimeGate(sc, tas, atr14, rrg_enable,
        sc.Input[IN_RRG_CHOP_LEN].GetInt(),
        (double)sc.Input[IN_RRG_CHOP_THRESH].GetFloat(),
        sc.Input[IN_RRG_ADX_LEN].GetInt(),
        (double)sc.Input[IN_RRG_ADX_FLOOR].GetFloat(),
        sc.Input[IN_RRG_ATRP_LOOKBACK].GetInt(),
        (double)sc.Input[IN_RRG_ATRP_FLOOR].GetFloat());

    // ── 5B. Candle Coloring ───────────────────────────────────────────────────
    // Runs regardless of Indicator Candles (Pine: barcolor recolors the raw
    // chart candles when custom candles are off — TA9:1365-1367)
    if (cc_enable) {
        CandleColorMode ccm = (CandleColorMode)sc.Input[IN_CANDLE_COL_MODE].GetIndex();
        bool tc_base_bull = (idx > 0)
            ? (tc.tc_base > (double)sc.Subgraph[SG_TC_BASE][idx - 1])
            : true;
        COLORREF col = compute_CandleColor(sc, tas, theme, ccm,
            ribbon.ribbon_bull, tc_base_bull,
            trg.bull_votes, trg.bear_votes,
            candles.cv_c, candles.cv_o,
            ribbon.cma1, ribbon.cma2);
        if (alt_enable) {
            // Body = mode color; wick slightly dimmed (Pine: 20% transparency),
            // except CVD-strong bars keep the highlight wick
            COLORREF wick = candles.cvd_strong ? theme.hilight : theme_color_dim(col, 80, theme.bg);
            sc.Subgraph[SG_CANDLE_OPEN].DataColor[idx]  = col;
            sc.Subgraph[SG_CANDLE_CLOSE].DataColor[idx] = col;
            sc.Subgraph[SG_CANDLE_HIGH].DataColor[idx]  = wick;
            sc.Subgraph[SG_CANDLE_LOW].DataColor[idx]   = wick;
        } else {
            // Pine barcolor(): one flat color for the whole bar, no CVD
            // highlight (eff_cvd_enable is false with Indicator Candles off)
            sc.Subgraph[SG_CANDLE_OPEN].DataColor[idx]  = col;
            sc.Subgraph[SG_CANDLE_CLOSE].DataColor[idx] = col;
            sc.Subgraph[SG_CANDLE_HIGH].DataColor[idx]  = col;
            sc.Subgraph[SG_CANDLE_LOW].DataColor[idx]   = col;
        }
    }

    // ── 7. PRISM Adaptive + Noise Suppression length scaling (Pine §7) ───────
    // Adaptive: scale lookback/rail periods up on sub-2-minute charts:
    //   tf_ratio = (120 / seconds_per_bar) ^ 0.45  when bar < 120s
    // Noise Suppression: extend lengths in choppy markets via smoothed ER:
    //   weight_full = 1 + (1 - EMA10(ER14)) * 0.25, weight_half at half strength
    // Results quantized to steps of 5 (f_quantize).
    // Simple mode forces Adaptive on and Noise Suppression off (TA9:675-676)
    bool   prism_adaptive = is_simple ? true  : (bool)sc.Input[IN_PRISM_ADAPTIVE].GetYesNo();
    bool   prism_ns       = is_simple ? false : (bool)sc.Input[IN_AO_NOISE_SUPPRESS].GetYesNo();
    // [v9.3 REGIME-1] est_sec_per_bar replaces the deprecated sc.SecondsPerBar:
    // identical for time charts (authoritative period), median bar duration
    // for tick/volume/range charts — PRISM Adaptive scaling was silently dead
    // there (SecondsPerBar reads 0, so `tf_sec > 0` never passed).
    int    tf_sec   = (int)tas.est_sec_per_bar;
    double tf_ratio = (prism_adaptive && tf_sec > 0 && tf_sec < 120)
        ? std::pow(120.0 / (double)tf_sec, 0.45) : 1.0;

    double ns_full = 1.0, ns_half = 1.0;
    {
        // ER(14) on close, EMA(10)-smoothed (Pine ta.ema, src-seeded — TA9:686).
        // Pine's `na > 0.0` is false during warm-up, so er_val is 0.0 (not na)
        // for the first 14 bars and the EMA seeds at bar 0 with 0.0 — step
        // every bar with the same values.
        const int ER_LEN = 14;
        double er_val = 0.0;
        if (idx >= ER_LEN) {
            double er_raw = 0.0;
            for (int i = 0; i < ER_LEN; i++)
                er_raw += std::abs((double)sc.Close[idx - i] - (double)sc.Close[idx - i - 1]);
            double er_net = std::abs((double)sc.Close[idx] - (double)sc.Close[idx - ER_LEN]);
            er_val = (er_raw > 0.0) ? er_net / er_raw : 0.0;
        }
        double er_smo = tas.working.ns_er_ema.step(er_val, 10);
        if (prism_ns) {
            ns_full = 1.0 + (1.0 - er_smo) * 0.25;
            ns_half = 1.0 + (1.0 - er_smo) * 0.25 * 0.5;
        }
    }

    int prism_base = f_quantize((double)sc.Input[IN_PRISM_BASE_LEN].GetInt() * tf_ratio * ns_full, 5);
    int st1_per    = f_quantize((double)sc.Input[IN_PRISM_ALPHA_PERIOD].GetInt() * tf_ratio * ns_half, 5);
    int st2_per    = f_quantize((double)sc.Input[IN_PRISM_SIGMA_PERIOD].GetInt() * tf_ratio * ns_half, 5);

    // ── 7A. Auto-Optimizer ────────────────────────────────────────────────────
    // Always called — the module computes the test lengths (info panel shows
    // them even in the Disabled state, like Pine) and early-outs when disabled
    AOResult ao = compute_AutoOptimizer(sc, tas, atr14, prism_base,
        st1_per, st2_per,
        sc.Input[IN_PRISM_ALPHA_FACTOR].GetFloat(),
        sc.Input[IN_PRISM_SIGMA_FACTOR].GetFloat(),
        sc.Input[IN_AO_SPREAD_PCT].GetFloat(),
        sc.Input[IN_AO_TIER1].GetFloat(),
        sc.Input[IN_AO_TIER2].GetFloat(),
        sc.Input[IN_AO_TIER3].GetFloat(),
        sc.Input[IN_AO_MAX_BARS].GetInt(),
        sc.Input[IN_AO_LOOKBACK].GetInt(),
        ao_enable,
        sc.Input[IN_AO_STOP_ATR].GetFloat());   // [v9.3 E5]

    int prism_eff_len = ao_enable ? ao.ao_eff_len : prism_base;

    // ── 7+7B. PRISM Signals ───────────────────────────────────────────────────
    // Always called — Pine gates only would_* on the enable; the engine
    // (rails, holds, dedup, fkama) runs unconditionally
    PRISMResult prism = compute_PRISMSignals(sc, tas, atr14, prism_eff_len,
            st1_per, st2_per,
            sc.Input[IN_PRISM_ALPHA_FACTOR].GetFloat(),
            sc.Input[IN_PRISM_SIGMA_FACTOR].GetFloat(),
            candles.cv_h, candles.cv_l,
            alt_enable,
            candles.cvd_rank, candles.cvd_net,
            tc.tc_base, tc.tc_k50, tc.tc_cloud_top,
            // [v9.3 REGIME-2] a detected range suppresses BOTH directions
            trg.suppress_buy  || rrg.range_active,
            trg.suppress_sell || rrg.range_active,
            sc.Input[IN_PRISM_ER_THRESH].GetFloat(),
            sc.Input[IN_PRISM_KAMA_SLOPE_MIN].GetFloat(),
            sc.Input[IN_PRISM_BAR_QUAL_MIN].GetFloat(),
            sc.Input[IN_PRISM_BAR_QUAL_WAIT].GetInt(),
            (bool)sc.Input[IN_PRISM_STRUCT_LOCK].GetYesNo(),
            (bool)sc.Input[IN_PRISM_BQ_ENABLE].GetYesNo(),
            (bool)sc.Input[IN_PRISM_MQ_ENABLE].GetYesNo(),
            prism_enable,
            gate_strict,                            // [v9.3 E2]
            flip_guard,                             // [v9.3 E3]
            (bool)sc.Input[IN_ER_ADAPT_ENABLE].GetYesNo(),   // [v9.3 REGIME-4]
            sc.Input[IN_ER_ADAPT_LOOKBACK].GetInt(),
            sc.Input[IN_ER_ADAPT_PCT].GetFloat());

    // ── 7C. Order-flow gates [v9.3 OF-2/3/4] — demote-only, never drop ──────
    // All three fire only on bars carrying REAL bid/ask data on a chart where
    // it is broadly present; a demoted arrow prints as a marginal dot. The
    // module's own recurrence keys off RAW rail signals, so post-hoc demotion
    // cannot desync gate state.
    if (tas.of_data_present
        && sc.Subgraph[SG_CVD_STORE].Arrays[2][idx] > 0.5f
        && (prism.full_bull || prism.full_bear)) {
        bool demote_bull = false, demote_bear = false;

        if (of_agree_gate) {
            // Net aggressor delta over the agreement window must side with
            // the signal (a lone opposing bar is forgiven by the window sum)
            double net = 0.0;
            int W = std::max(of_agree_bars, 1);
            for (int i = 0; i < W && i <= idx; i++)
                net += (double)sc.Subgraph[SG_CVD_STORE].Arrays[1][idx - i];
            if (prism.full_bull && net <= 0.0) demote_bull = true;
            if (prism.full_bear && net >= 0.0) demote_bear = true;
        }
        if (of_div_veto) {
            // Windowed cumulative-delta divergence: price advanced over the
            // window while net aggression ran the other way
            int L = std::max(of_div_lb, 1);
            int j = std::max(idx - L, 0);
            double cvd_L = (double)sc.Subgraph[SG_CVD_STORE].Arrays[3][idx];
            if (prism.full_bull && cvd_L < 0.0 && sc.Close[idx] > sc.Close[j]) demote_bull = true;
            if (prism.full_bear && cvd_L > 0.0 && sc.Close[idx] < sc.Close[j]) demote_bear = true;
        }
        if (of_absorb_veto) {
            // Intrabar absorption: delta made an extreme in the signal
            // direction then collapsed off it into the close — passive
            // liquidity absorbed the aggression (arrays read 0 without
            // 1-tick data → self-disabling)
            double dmax = (double)sc.BaseData[SC_ASKBID_DIFF_HIGH][idx];
            double dmin = (double)sc.BaseData[SC_ASKBID_DIFF_LOW][idx];
            double dcls = (double)sc.Subgraph[SG_CVD_STORE].Arrays[1][idx];
            double span = dmax - dmin;
            double rt   = (double)sc.Input[IN_OF_ABSORB_RETRACE].GetFloat();
            if (span > 0.0) {
                if (prism.full_bull && dmax > 0.0 && (dmax - dcls) >= rt * span) demote_bull = true;
                if (prism.full_bear && dmin < 0.0 && (dcls - dmin) >= rt * span) demote_bear = true;
            }
        }
        if (demote_bull) { prism.full_bull = false; prism.mq_bull = true; }
        if (demote_bear) { prism.full_bear = false; prism.mq_bear = true; }
    }

    // Apply Signal Size to arrow width (once per full recalculation)
    if (idx == 0) {
        int sig_w = (sc.Input[IN_PRISM_SIG_SIZE].GetIndex() == 0) ? 3 : 5;
        sc.Subgraph[SG_PRISM_BULL].LineWidth = sig_w;
        sc.Subgraph[SG_PRISM_BEAR].LineWidth = sig_w;
    }

    // [v9.3 V4] Intrabar flicker telemetry: count live-bar signal-state
    // changes. On the first tick of a new bar the FINISHED bar's count is
    // snapshotted BEFORE the reset, so the logger (later in this same pass)
    // reads the closed bar's flicker — never the freshly wiped counter.
    if (idx == sc.ArraySize - 1 && !sc.IsFullRecalculation) {
        double fbt = sc.BaseDateTimeIn[idx].GetAsDouble();
        unsigned char pack = (unsigned char)((prism.full_bull ? 1 : 0)
                           | (prism.full_bear ? 2 : 0)
                           | (prism.mq_bull   ? 4 : 0)
                           | (prism.mq_bear   ? 8 : 0));
        if (fbt != tas.flicker_bar_time) {
            tas.fin_flicker_time  = tas.flicker_bar_time;
            tas.fin_flicker_count = tas.flicker_count;
            tas.flicker_bar_time  = fbt;
            tas.flicker_count     = 0;
            tas.flicker_prev_pack = pack;
        } else if (pack != tas.flicker_prev_pack) {
            tas.flicker_count++;
            tas.flicker_prev_pack = pack;
        }
    }

    // Write signal subgraph values — anchored to the displayed (custom) candle
    // high/low like Pine, not the raw bar
    float sig_off = (float)(sc.Input[IN_PRISM_SIG_OFFSET].GetFloat() * atr14);
    float ref_h = alt_enable ? (float)candles.cv_h : sc.High[idx];
    float ref_l = alt_enable ? (float)candles.cv_l : sc.Low[idx];

    // [v9.3 P1] Snapshot the bar's final signal state + marker prices in the
    // rollback state — the committed copy drives Bar Close confirmation
    tas.working.sigc_full_bull = prism.full_bull;
    tas.working.sigc_full_bear = prism.full_bear;
    tas.working.sigc_mq_bull   = prism.mq_bull;
    tas.working.sigc_mq_bear   = prism.mq_bear;
    tas.working.sigc_bull_y    = ref_l - sig_off;
    tas.working.sigc_bear_y    = ref_h + sig_off;
    tas.working.sigc_mq_bull_y = ref_l - sig_off;
    tas.working.sigc_mq_bear_y = ref_h + sig_off;

    if (!confirm_mode) {
        // Live (TradingView parity): markers repaint intrabar like Pine
        sc.Subgraph[SG_PRISM_BULL][idx]    = prism.full_bull ? ref_l - sig_off : 0.0f;
        sc.Subgraph[SG_PRISM_BEAR][idx]    = prism.full_bear ? ref_h + sig_off : 0.0f;
        sc.Subgraph[SG_PRISM_MQ_BULL][idx] = prism.mq_bull   ? ref_l - sig_off : 0.0f;
        sc.Subgraph[SG_PRISM_MQ_BEAR][idx] = prism.mq_bear   ? ref_h + sig_off : 0.0f;
    } else {
        // Bar Close (confirmed): the live bar never shows an arrow; the
        // previous bar's marker is drawn from its COMMITTED (final) state on
        // every call — identical on live ticks and full recalculations.
        sc.Subgraph[SG_PRISM_BULL][idx]    = 0.0f;
        sc.Subgraph[SG_PRISM_BEAR][idx]    = 0.0f;
        sc.Subgraph[SG_PRISM_MQ_BULL][idx] = 0.0f;
        sc.Subgraph[SG_PRISM_MQ_BEAR][idx] = 0.0f;
        if (idx > 0) {
            const TARecurrence& cm = tas.committed;
            sc.Subgraph[SG_PRISM_BULL][idx - 1]    = cm.sigc_full_bull ? cm.sigc_bull_y    : 0.0f;
            sc.Subgraph[SG_PRISM_BEAR][idx - 1]    = cm.sigc_full_bear ? cm.sigc_bear_y    : 0.0f;
            sc.Subgraph[SG_PRISM_MQ_BULL][idx - 1] = cm.sigc_mq_bull   ? cm.sigc_mq_bull_y : 0.0f;
            sc.Subgraph[SG_PRISM_MQ_BEAR][idx - 1] = cm.sigc_mq_bear   ? cm.sigc_mq_bear_y : 0.0f;
        }
    }

    // ── Raw output subgraphs ──────────────────────────────────────────────────
    sc.Subgraph[SG_RAW_ER][idx]          = (float)prism.er;
    sc.Subgraph[SG_RAW_CCO][idx]         = (float)sch.sc_cco;
    sc.Subgraph[SG_RAW_KAMA_ALIGN][idx]  = (float)(trg.ka_bull_pct * 100.0);
    sc.Subgraph[SG_RAW_AO_EFF_LEN][idx] = (float)prism_eff_len;
    // [v9.3 V1] fire-time gate-context stash for the outcome logger
    sc.Subgraph[SG_RAW_ER].Arrays[0][idx]  = (float)prism.fk_norm;
    sc.Subgraph[SG_RAW_ER].Arrays[1][idx]  = (float)prism.bq_ratio;
    sc.Subgraph[SG_RAW_ER].Arrays[2][idx]  = prism.chop_fired ? 1.0f : 0.0f;
    sc.Subgraph[SG_RAW_ER].Arrays[3][idx]  = (float)prism.sig_path;
    sc.Subgraph[SG_RAW_ER].Arrays[4][idx]  = (float)prism.elev_mask;
    sc.Subgraph[SG_RAW_CCO].Arrays[0][idx] = (float)prism.er_rank;

    // (TC Glow visibility is handled by the DrawStyle gating block above —
    // real values stay in the subgraph.)

    // ── 8. Boundary Forecast (drawn on last bar; cubic refit runs every bar) ──
    compute_BoundaryForecast(sc, tas, theme, fc_enable,
        sc.Input[IN_FC_BARS].GetInt(),
        sc.Input[IN_FC_REG_LEN].GetInt(),
        (sc.Input[IN_FC_MODE].GetIndex() == 0),  // 0 = 'Regression'
        (bool)sc.Input[IN_FC_SHOW_TC].GetYesNo(),
        (bool)sc.Input[IN_FC_SHOW_SC].GetYesNo(),
        tc.tc_base, tc.tc_cloud_top);

    // ── 9. Info Panel + Watermark (last bar only) ─────────────────────────────
    // Purge a stale panel when the toggle is turned off — the input change
    // triggers a full recalc, so idx==0 runs exactly once per toggle.
    if (!info_enable && idx == 0)
        info_delete_all(sc);
    if (info_enable && idx == sc.ArraySize - 1) {
        InfoPanelData ipd = {};
        // Trend State (Pine TA9:1400-1403)
        ipd.bias_bull = (prism.cur_dir == -1);
        ipd.st_bull   = (ribbon.cma1 > ribbon.cma2);
        {
            double k20      = tc.tc_kama[3];
            double k20_prev = (idx > 0)
                ? (double)sc.Subgraph[SG_KAMA_STORE_A].Arrays[2][idx - 1] : k20;
            ipd.mt_bull = (k20 > k20_prev);
        }
        {
            double base_prev = (idx > 0) ? (double)sc.Subgraph[SG_TC_BASE][idx - 1] : tc.tc_base;
            ipd.lt_bull        = (tc.tc_base > base_prev);
            ipd.tc_base_rising = ipd.lt_bull;
        }
        // Momentum & Quality
        {
            double r0 = (double)sc.Subgraph[SG_CVD_STORE].Arrays[0][idx];
            double r1 = (idx > 0) ? (double)sc.Subgraph[SG_CVD_STORE].Arrays[0][idx - 1] : 0.0;
            double r2 = (idx > 1) ? (double)sc.Subgraph[SG_CVD_STORE].Arrays[0][idx - 2] : 0.0;
            ipd.delta_avg = (r0 + r1 + r2) / 3.0;
        }
        ipd.srsi_k      = (double)sc.Subgraph[SG_KAMA_STORE_D][idx];
        ipd.srsi_k_prev = (idx > 0) ? (double)sc.Subgraph[SG_KAMA_STORE_D][idx - 1] : ipd.srsi_k;
        ipd.sc_cco      = sch.sc_cco;
        ipd.er          = prism.er;
        // Trend Regime
        ipd.suppress_sell = trg.suppress_sell;
        ipd.suppress_buy  = trg.suppress_buy;
        ipd.hurst         = trg.hurst;
        ipd.ka_bull_pct   = trg.ka_bull_pct;
        ipd.ka_bear_pct   = trg.ka_bear_pct;
        ipd.ka_thresh     = eff_ka_thresh;
        ipd.accel_smooth  = trg.accel_smooth;
        // PRISM Adaptive
        ipd.adaptive_on      = prism_adaptive;
        ipd.ns_on            = prism_ns;
        ipd.prism_len        = prism_eff_len;
        ipd.prism_base_input = sc.Input[IN_PRISM_BASE_LEN].GetInt();
        ipd.st1_per          = st1_per;
        ipd.st2_per          = st2_per;
        ipd.st1_per_input    = sc.Input[IN_PRISM_ALPHA_PERIOD].GetInt();
        ipd.er_smo           = tas.working.ns_er_ema.value;
        // Auto-Optimizer
        ipd.ao_enabled  = ao_enable;
        ipd.ao_active   = !ao.is_warming_up;
        ipd.ao_len_s    = ao.ao_len_s;  ipd.ao_len_m = ao.ao_len_m;  ipd.ao_len_l = ao.ao_len_l;
        ipd.ao_eff_len  = ao.ao_eff_len;
        ipd.ao_avg_s    = ao.avg_s;     ipd.ao_avg_m = ao.avg_m;     ipd.ao_avg_l = ao.avg_l;
        ipd.ao_w_s      = ao.w_s;       ipd.ao_w_m   = ao.w_m;       ipd.ao_w_l   = ao.w_l;
        ipd.ao_sz_s     = ao.sz_s;      ipd.ao_sz_m  = ao.sz_m;      ipd.ao_sz_l  = ao.sz_l;
        ipd.ao_lookback = sc.Input[IN_AO_LOOKBACK].GetInt();

        compute_InfoPanel(sc, theme, ipd, sc.Input[IN_INFO_LOCATION].GetIndex());
    }
    if (idx == sc.ArraySize - 1)
        compute_Watermark(sc, theme);

    // ── Alerts — all 13 Pine alertconditions ─────────────────────────────────
    // Fire only on the live last bar — never while replaying history — and at
    // most once per alert per bar (ticks re-run this block; the latch is
    // outside the rollback state on purpose).
    if (!stale_pass && idx == sc.ArraySize - 1 && !sc.IsFullRecalculation && !sc.DownloadingHistoricalData) {
        double bar_time = sc.BaseDateTimeIn[idx].GetAsDouble();
        auto fire_once = [&](int alert_num, bool cond) {
            if (cond && tas.alert_fired_time[alert_num] != bar_time) {
                sc.SetAlert(alert_num);
                tas.alert_fired_time[alert_num] = bar_time;
            }
        };

        // Ribbon crossovers (Pine ta.crossover/crossunder on cma1/cma2) — from
        // the always-written hidden history arrays
        float c1n = sc.Subgraph[SG_ALMA1].Arrays[0][idx];
        float c2n = sc.Subgraph[SG_ALMA2].Arrays[0][idx];
        float c1p = (idx > 0) ? sc.Subgraph[SG_ALMA1].Arrays[0][idx - 1] : c1n;
        float c2p = (idx > 0) ? sc.Subgraph[SG_ALMA2].Arrays[0][idx - 1] : c2n;
        bool ribbon_x_up = (idx > 0) && (c1n > c2n) && (c1p <= c2p);
        bool ribbon_x_dn = (idx > 0) && (c1n < c2n) && (c1p >= c2p);

        // Super Channel OB/OS crossings (sc_cco always computes, like Pine —
        // these alerts fire even with the channel hidden)
        float cco_n = sc.Subgraph[SG_SC_CCO][idx];
        float cco_p = (idx > 0) ? sc.Subgraph[SG_SC_CCO][idx - 1] : cco_n;
        bool cco_ob = (idx > 0) && (cco_n > SC_HI_THRESH) && (cco_p <= SC_HI_THRESH);
        bool cco_os = (idx > 0) && (cco_n < SC_LO_THRESH) && (cco_p >= SC_LO_THRESH);

        // Regime rising edges from the per-bar suppress history
        bool regime_bull_on = trg.suppress_sell && (idx > 0)
            && (sc.Subgraph[SG_RAW_KAMA_ALIGN].Arrays[0][idx - 1] < 0.5f);
        bool regime_bear_on = trg.suppress_buy && (idx > 0)
            && (sc.Subgraph[SG_RAW_KAMA_ALIGN].Arrays[1][idx - 1] < 0.5f);

        // [v9.3 P2] In Bar Close mode the PRISM signal alerts (1-7) move to
        // the confirmed block below; the series-crossing alerts (8-13) keep
        // Pine's live semantics in both modes.
        if (!confirm_mode) {
            fire_once(1,  prism.full_bull);                    // PRISM Buy Signal
            fire_once(2,  prism.full_bear);                    // PRISM Sell Signal
            fire_once(3,  prism.full_bull || prism.full_bear); // PRISM Any Signal
            fire_once(4,  prism.mq_bull);                      // Marginal Buy Signal
            fire_once(5,  prism.mq_bear);                      // Marginal Sell Signal
            fire_once(6,  prism.mq_bull || prism.mq_bear);     // Marginal — Either
            fire_once(7,  prism.full_bull || prism.full_bear ||
                          prism.mq_bull   || prism.mq_bear);   // Any Signal
        }
        fire_once(8,  ribbon_x_up);                            // Ribbon Bullish Crossover
        fire_once(9,  ribbon_x_dn);                            // Ribbon Bearish Crossover
        fire_once(10, cco_ob);                                 // Super Channel Overbought
        fire_once(11, cco_os);                                 // Super Channel Oversold
        fire_once(12, regime_bull_on);                         // Bull Regime Activated
        fire_once(13, regime_bear_on);                         // Bear Regime Activated
    }

    // ── [v9.3 P2] Confirmed PRISM alerts — fire once per CLOSED signal bar ──
    // Runs on the first call of a genuinely new bar, from the previous bar's
    // COMMITTED state, latched on the closed bar's timestamp. bar_advanced
    // fires once per new bar even when several bars form in one update (the
    // ArraySize-1 gate above would miss the older one).
    if (confirm_mode && bar_advanced && idx > 0 && !stale_pass
        && !sc.IsFullRecalculation && !sc.DownloadingHistoricalData) {
        double sig_time = sc.BaseDateTimeIn[idx - 1].GetAsDouble();
        auto fire_conf = [&](int alert_num, bool cond) {
            if (cond && tas.alert_fired_time[alert_num] != sig_time) {
                sc.SetAlert(alert_num);
                tas.alert_fired_time[alert_num] = sig_time;
            }
        };
        const TARecurrence& cm = tas.committed;
        fire_conf(1, cm.sigc_full_bull);
        fire_conf(2, cm.sigc_full_bear);
        fire_conf(3, cm.sigc_full_bull || cm.sigc_full_bear);
        fire_conf(4, cm.sigc_mq_bull);
        fire_conf(5, cm.sigc_mq_bear);
        fire_conf(6, cm.sigc_mq_bull || cm.sigc_mq_bear);
        fire_conf(7, cm.sigc_full_bull || cm.sigc_full_bear ||
                     cm.sigc_mq_bull   || cm.sigc_mq_bear);
    }

    // ── [v9.3 V2] Signal-outcome logger — closed bars, live sessions only ───
    if (idx == sc.ArraySize - 1) {
        compute_SignalLogger(sc, tas,
            (bool)sc.Input[IN_SIGLOG_ENABLE].GetYesNo(),
            (bool)sc.Input[IN_SIGLOG_INCLUDE_MQ].GetYesNo(),
            sc.Input[IN_SIGLOG_HORIZON].GetInt() > 0
                ? sc.Input[IN_SIGLOG_HORIZON].GetInt()
                : sc.Input[IN_AO_MAX_BARS].GetInt(),
            sc.Input[IN_AO_TIER1].GetFloat(),
            sc.Input[IN_AO_TIER2].GetFloat(),
            sc.Input[IN_AO_TIER3].GetFloat(),
            tas.est_sec_per_bar);
    }
}
