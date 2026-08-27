// ZScoreChannel.cpp
//
// Rolling Z-Score Channel — ACSIL port of the Pine v6 "Adaptive Rolling
// Z-Score Channel" indicator (zchann.txt). Computes a rolling mean/stdev of
// the source, converts price to a z-score, derives band levels either as
// fixed z-values or adaptively from a weighted multi-period blend of rolling
// percentiles of the z-score, optionally smooths the z-score and/or band
// levels with one of four filters (LinReg, Hull, Super Smoother, Two-Pole
// Gaussian), inverts back to price space, and plots bands + basis with
// fills, gradient coloring, re-entry markers, and alerts.
//
// Fixes carried over from the Pine analysis (see design plan Part 1):
//  1. The minimum-band-Z floor is re-applied AFTER smoothing the band
//     levels (Pine only floors before smoothing, which lets an overshoot
//     collapse or invert the channel).
//  2. sc.DataStartIndex is set to the longest required window (rolling
//     window in Fixed mode, max(rolling window, longest blend period) in
//     Adaptive mode) so Sierra never draws the degenerate warmup region.

#include "sierrachart.h"

#include <algorithm>
#include <cmath>

SCDLLName("Rolling Z-Score Channel")

// ============================================================
// CONSTANTS
// ============================================================

static const double kPi = 3.14159265358979323846;
// Matches the input limits on the three blend-period inputs (10..2000), so
// a stack buffer of this size can always hold one full blend window.
static const int MAX_BLEND_PERIOD = 2000;

// ============================================================
// PURE MATH HELPERS
// (Mirrored formula-for-formula in the scratchpad test harness — keep the
// two in sync if either changes.)
// ============================================================

// Rolling sample mean + stdev (n-1 divisor, i.e. ta.stdev(..., false))
// computed by an explicit loop over the trailing window ending at idx.
static void RollingMeanStdev(SCFloatArrayRef arr, int idx, int n, float& outMean, float& outStdev)
{
    double sum = 0.0;
    for (int k = 0; k < n; ++k)
        sum += arr[idx - k];
    double mean = sum / n;

    double sqSum = 0.0;
    for (int k = 0; k < n; ++k)
    {
        double d = arr[idx - k] - mean;
        sqSum += d * d;
    }
    double variance = (n > 1) ? sqSum / (n - 1) : 0.0;

    outMean = (float)mean;
    outStdev = (float)std::sqrt(variance);
}

// ta.percentile_linear_interpolation equivalent. Sorts the trailing
// `period` values ending at idx ONCE into a stack buffer, then reads BOTH
// the upper and lower percentile off that single sorted buffer (avoids a
// second sort per period). Returns false if the window isn't fully
// available yet (idx < period - 1).
static bool PercentileBlendPair(SCFloatArrayRef arr, int idx, int period,
                                 float pctUpper, float pctLower,
                                 float& outUpper, float& outLower)
{
    if (period <= 0 || period > MAX_BLEND_PERIOD || idx < period - 1)
        return false;

    float buf[MAX_BLEND_PERIOD];
    for (int k = 0; k < period; ++k)
        buf[k] = arr[idx - period + 1 + k];

    std::sort(buf, buf + period);

    auto interp = [&](float pct) -> float
    {
        double r = (pct / 100.0) * (period - 1);
        int lo = (int)std::floor(r);
        int hi = (int)std::ceil(r);
        if (lo < 0) lo = 0;
        if (hi > period - 1) hi = period - 1;
        double frac = r - lo;
        return (float)(buf[lo] + (buf[hi] - buf[lo]) * frac);
    };

    outUpper = interp(pctUpper);
    outLower = interp(pctLower);
    return true;
}

// Linearly weighted moving average (weights n..1, most recent weighted
// highest) over the trailing `length` bars ending at idx. Gracefully
// degrades to the available history when idx has fewer than `length`
// prior bars, so the pipeline stays stable near the start of the chart
// instead of reading out of bounds.
static float WMA(SCFloatArrayRef arr, int idx, int length)
{
    int n = (std::min)(length, idx + 1);
    if (n <= 0)
        return 0.0f;

    double weightedSum = 0.0;
    double weightSum = 0.0;
    for (int k = 0; k < n; ++k)
    {
        double w = (double)(n - k);
        weightedSum += arr[idx - k] * w;
        weightSum += w;
    }
    return (float)(weightedSum / weightSum);
}

// Least-squares linear regression endpoint (value of the fitted line at the
// most recent bar in the window) — equivalent to ta.linreg(src, length, 0).
static float LinRegEndpoint(SCFloatArrayRef arr, int idx, int length)
{
    int n = (std::min)(length, idx + 1);
    if (n <= 1)
        return (n == 1) ? arr[idx] : 0.0f;

    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumXX = 0.0;
    for (int k = 0; k < n; ++k)
    {
        double x = (double)(n - 1 - k); // most recent bar -> x = n-1
        double y = arr[idx - k];
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumXX += x * x;
    }
    double denom = n * sumXX - sumX * sumX;
    if (std::fabs(denom) < 1e-12)
        return (float)(sumY / n);

    double slope = (n * sumXY - sumX * sumY) / denom;
    double intercept = (sumY - slope * sumX) / n;
    return (float)(intercept + slope * (n - 1));
}

// Hull MA: diff = 2*WMA(src, halfLen) - WMA(src, length); out = WMA(diff, sqrtLen).
// `diffArr` persists the diff series per-bar (a dedicated extra array) so the
// outer WMA can look back over it on subsequent bars.
static float HullMA(SCFloatArrayRef srcArr, SCFloatArrayRef diffArr, int idx, int length)
{
    int halfLen = (std::max)(1, length / 2);
    int sqrtLen = (std::max)(1, (int)std::lround(std::sqrt((double)length)));

    float wmaHalf = WMA(srcArr, idx, halfLen);
    float wmaFull = WMA(srcArr, idx, length);
    diffArr[idx] = 2.0f * wmaHalf - wmaFull;

    return WMA(diffArr, idx, sqrtLen);
}

// Ehlers Super Smoother (2-pole). Coefficients are recomputed from `length`
// every call (cheap). `outArr` holds the recursive output series; indices
// before the start of the array are treated as 0, matching Pine's nz().
static float SuperSmoother(SCFloatArrayRef xArr, SCFloatArrayRef outArr, int idx, int length)
{
    double a1 = std::exp(-std::sqrt(2.0) * kPi / length);
    double b1 = 2.0 * a1 * std::cos(std::sqrt(2.0) * kPi / length);
    double c2 = b1;
    double c3 = -a1 * a1;
    double c1 = 1.0 - c2 - c3;

    float x = xArr[idx];
    float xPrev = (idx >= 1) ? xArr[idx - 1] : 0.0f;
    float outPrev1 = (idx >= 1) ? outArr[idx - 1] : 0.0f;
    float outPrev2 = (idx >= 2) ? outArr[idx - 2] : 0.0f;

    float out = (float)(c1 * (x + xPrev) / 2.0 + c2 * outPrev1 + c3 * outPrev2);
    outArr[idx] = out;
    return out;
}

// Ehlers Two-Pole Gaussian filter. Same nz()-style guards as SuperSmoother.
static float TwoPoleGaussian(SCFloatArrayRef xArr, SCFloatArrayRef outArr, int idx, int length)
{
    double beta = (1.0 - std::cos(2.0 * kPi / length)) / (std::sqrt(2.0) - 1.0);
    double alpha = -beta + std::sqrt(beta * beta + 2.0 * beta);
    double a2 = alpha * alpha;
    double om = 1.0 - alpha;
    double om2 = om * om;

    float x = xArr[idx];
    float outPrev1 = (idx >= 1) ? outArr[idx - 1] : 0.0f;
    float outPrev2 = (idx >= 2) ? outArr[idx - 2] : 0.0f;

    float out = (float)(a2 * x + 2.0 * om * outPrev1 - om2 * outPrev2);
    outArr[idx] = out;
    return out;
}

// Linearly interpolate between two COLORREFs (packed 0x00BBGGRR, the same
// layout the RGB() macro produces). Clamps t to [0,1].
static COLORREF LerpColor(COLORREF a, COLORREF b, double t)
{
    if (t < 0.0) t = 0.0;
    else if (t > 1.0) t = 1.0;

    int ar = (int)(a & 0xFF), ag = (int)((a >> 8) & 0xFF), ab = (int)((a >> 16) & 0xFF);
    int br = (int)(b & 0xFF), bg = (int)((b >> 8) & 0xFF), bb = (int)((b >> 16) & 0xFF);

    int r = (int)std::lround(ar + (br - ar) * t);
    int g = (int)std::lround(ag + (bg - ag) * t);
    int bch = (int)std::lround(ab + (bb - ab) * t);

    return RGB(r, g, bch);
}

// Pine ta.crossover / ta.crossunder semantics: strictly greater/less NOW,
// and NOT strictly greater/less on the previous bar.
static bool CrossesOver(float curA, float curB, float prevA, float prevB)
{
    return curA > curB && prevA <= prevB;
}
static bool CrossesUnder(float curA, float curB, float prevA, float prevB)
{
    return curA < curB && prevA >= prevB;
}

// ============================================================
// STUDY
// ============================================================

SCSFExport scsf_RollingZScoreChannel(SCStudyInterfaceRef sc)
{
    SCSubgraphRef Subgraph_UpperBand        = sc.Subgraph[0];
    SCSubgraphRef Subgraph_LowerBand        = sc.Subgraph[1];
    SCSubgraphRef Subgraph_Basis            = sc.Subgraph[2];
    SCSubgraphRef Subgraph_InnerUpper       = sc.Subgraph[3];
    SCSubgraphRef Subgraph_InnerLower       = sc.Subgraph[4];
    SCSubgraphRef Subgraph_UpperFillTop     = sc.Subgraph[5];
    SCSubgraphRef Subgraph_UpperFillBottom  = sc.Subgraph[6];
    SCSubgraphRef Subgraph_LowerFillTop     = sc.Subgraph[7];
    SCSubgraphRef Subgraph_LowerFillBottom  = sc.Subgraph[8];
    SCSubgraphRef Subgraph_ReEntryUp        = sc.Subgraph[9];
    SCSubgraphRef Subgraph_ReEntryDown      = sc.Subgraph[10];
    SCSubgraphRef Subgraph_BarColor         = sc.Subgraph[11];
    SCSubgraphRef Subgraph_ZScore           = sc.Subgraph[12];
    SCSubgraphRef Subgraph_UpperZ           = sc.Subgraph[13];
    SCSubgraphRef Subgraph_LowerZ           = sc.Subgraph[14];

    SCInputRef Input_Data           = sc.Input[0];
    SCInputRef Input_RollingWindow  = sc.Input[1];
    SCInputRef Input_BandMode       = sc.Input[2];
    SCInputRef Input_FixedZUp       = sc.Input[3];
    SCInputRef Input_FixedZDn       = sc.Input[4];
    SCInputRef Input_ForceSymmetric = sc.Input[5];
    SCInputRef Input_MinBandZ       = sc.Input[6];
    SCInputRef Input_InnerFrac      = sc.Input[7];
    SCInputRef Input_PctUpper       = sc.Input[8];
    SCInputRef Input_PctLower       = sc.Input[9];
    SCInputRef Input_SmoothZ        = sc.Input[10];
    SCInputRef Input_SmoothBands    = sc.Input[11];
    SCInputRef Input_SmoothType     = sc.Input[12];
    SCInputRef Input_SmoothLength   = sc.Input[13];
    SCInputRef Input_BlendPeriodS   = sc.Input[14];
    SCInputRef Input_BlendPeriodM   = sc.Input[15];
    SCInputRef Input_BlendPeriodL   = sc.Input[16];
    SCInputRef Input_BlendWeightS   = sc.Input[17];
    SCInputRef Input_BlendWeightM   = sc.Input[18];
    SCInputRef Input_BlendWeightL   = sc.Input[19];
    SCInputRef Input_GradientBasis  = sc.Input[20];
    SCInputRef Input_EnableAlerts   = sc.Input[21];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Rolling Z-Score Channel";
        sc.StudyDescription =
            "Adaptive rolling z-score channel ported from Pine v6. Computes a "
            "rolling mean/stdev z-score of the source, derives band levels "
            "from a fixed z or a weighted multi-period percentile blend, "
            "optionally smooths z-score and/or bands, and inverts to price.";
        sc.AutoLoop = 1;
        sc.GraphRegion = 0;
        sc.ValueFormat = VALUEFORMAT_INHERITED;

        // ---- Subgraphs ----
        Subgraph_UpperBand.Name = "Upper Band";
        Subgraph_UpperBand.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_UpperBand.PrimaryColor = RGB(255, 60, 60);
        Subgraph_UpperBand.LineWidth = 2;
        Subgraph_UpperBand.DrawZeros = 0;

        Subgraph_LowerBand.Name = "Lower Band";
        Subgraph_LowerBand.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_LowerBand.PrimaryColor = RGB(0, 200, 120);
        Subgraph_LowerBand.LineWidth = 2;
        Subgraph_LowerBand.DrawZeros = 0;

        Subgraph_Basis.Name = "Basis";
        Subgraph_Basis.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_Basis.PrimaryColor = RGB(160, 160, 160);
        Subgraph_Basis.LineWidth = 2;
        Subgraph_Basis.DrawZeros = 0;

        Subgraph_InnerUpper.Name = "Inner Upper";
        Subgraph_InnerUpper.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_InnerUpper.PrimaryColor = RGB(255, 60, 60);
        Subgraph_InnerUpper.LineWidth = 1;
        Subgraph_InnerUpper.DrawZeros = 0;

        Subgraph_InnerLower.Name = "Inner Lower";
        Subgraph_InnerLower.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_InnerLower.PrimaryColor = RGB(0, 200, 120);
        Subgraph_InnerLower.LineWidth = 1;
        Subgraph_InnerLower.DrawZeros = 0;

        Subgraph_UpperFillTop.Name = "Upper Fill Top";
        Subgraph_UpperFillTop.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_TOP;
        Subgraph_UpperFillTop.PrimaryColor = RGB(255, 150, 150);
        Subgraph_UpperFillTop.DrawZeros = 0;

        Subgraph_UpperFillBottom.Name = "Upper Fill Bottom";
        Subgraph_UpperFillBottom.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_BOTTOM;
        Subgraph_UpperFillBottom.PrimaryColor = RGB(255, 150, 150);
        Subgraph_UpperFillBottom.DrawZeros = 0;

        Subgraph_LowerFillTop.Name = "Lower Fill Top";
        Subgraph_LowerFillTop.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_TOP;
        Subgraph_LowerFillTop.PrimaryColor = RGB(140, 220, 180);
        Subgraph_LowerFillTop.DrawZeros = 0;

        Subgraph_LowerFillBottom.Name = "Lower Fill Bottom";
        Subgraph_LowerFillBottom.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_BOTTOM;
        Subgraph_LowerFillBottom.PrimaryColor = RGB(140, 220, 180);
        Subgraph_LowerFillBottom.DrawZeros = 0;

        Subgraph_ReEntryUp.Name = "Re-Entry Up";
        Subgraph_ReEntryUp.DrawStyle = DRAWSTYLE_ARROW_UP;
        Subgraph_ReEntryUp.PrimaryColor = RGB(0, 200, 120);
        Subgraph_ReEntryUp.LineWidth = 2;
        Subgraph_ReEntryUp.DrawZeros = 0;

        Subgraph_ReEntryDown.Name = "Re-Entry Down";
        Subgraph_ReEntryDown.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        Subgraph_ReEntryDown.PrimaryColor = RGB(255, 60, 60);
        Subgraph_ReEntryDown.LineWidth = 2;
        Subgraph_ReEntryDown.DrawZeros = 0;

        Subgraph_BarColor.Name = "Bar Color";
        Subgraph_BarColor.DrawStyle = DRAWSTYLE_IGNORE; // opt-in: set to DRAWSTYLE_COLOR_BAR to enable
        Subgraph_BarColor.PrimaryColor = RGB(160, 160, 160);
        Subgraph_BarColor.DrawZeros = 0;

        Subgraph_ZScore.Name = "Z-Score";
        Subgraph_ZScore.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_ZScore.DrawZeros = 0;

        Subgraph_UpperZ.Name = "Upper Z";
        Subgraph_UpperZ.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_UpperZ.DrawZeros = 0;

        Subgraph_LowerZ.Name = "Lower Z";
        Subgraph_LowerZ.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_LowerZ.DrawZeros = 0;

        // ---- Inputs ----
        Input_Data.Name = "Input Data";
        Input_Data.SetInputDataIndex(SC_LAST);

        Input_RollingWindow.Name = "Rolling Window";
        Input_RollingWindow.SetInt(80);
        Input_RollingWindow.SetIntLimits(10, 500);

        Input_BandMode.Name = "Band Mode";
        Input_BandMode.SetCustomInputStrings("Fixed Z-Score;Adaptive");
        Input_BandMode.SetCustomInputIndex(1); // Adaptive

        Input_FixedZUp.Name = "Fixed Upper Z";
        Input_FixedZUp.SetFloat(2.0f);
        Input_FixedZUp.SetFloatLimits(0.1f, 6.0f);

        Input_FixedZDn.Name = "Fixed Lower Z";
        Input_FixedZDn.SetFloat(-2.0f);
        Input_FixedZDn.SetFloatLimits(-6.0f, -0.1f);

        Input_ForceSymmetric.Name = "Force Symmetric Bands";
        Input_ForceSymmetric.SetYesNo(false);

        Input_MinBandZ.Name = "Minimum Band Z";
        Input_MinBandZ.SetFloat(0.5f);
        Input_MinBandZ.SetFloatLimits(0.1f, 3.0f);

        Input_InnerFrac.Name = "Inner Band Fraction";
        Input_InnerFrac.SetFloat(0.5f);
        Input_InnerFrac.SetFloatLimits(0.1f, 0.9f);

        Input_PctUpper.Name = "Upper Percentile";
        Input_PctUpper.SetFloat(95.0f);
        Input_PctUpper.SetFloatLimits(50.0f, 99.0f);

        Input_PctLower.Name = "Lower Percentile";
        Input_PctLower.SetFloat(5.0f);
        Input_PctLower.SetFloatLimits(1.0f, 50.0f);

        Input_SmoothZ.Name = "Smooth Z-Score";
        Input_SmoothZ.SetYesNo(false);

        Input_SmoothBands.Name = "Smooth Band Levels";
        Input_SmoothBands.SetYesNo(false);

        Input_SmoothType.Name = "Smoothing Type";
        Input_SmoothType.SetCustomInputStrings("LinReg;Hull MA;Super Smoother;Two-Pole Gaussian");
        Input_SmoothType.SetCustomInputIndex(3); // Two-Pole Gaussian

        Input_SmoothLength.Name = "Smoothing Length";
        Input_SmoothLength.SetInt(5);
        Input_SmoothLength.SetIntLimits(2, 50);

        Input_BlendPeriodS.Name = "Blend Period Short";
        Input_BlendPeriodS.SetInt(50);
        Input_BlendPeriodS.SetIntLimits(10, 2000);

        Input_BlendPeriodM.Name = "Blend Period Medium";
        Input_BlendPeriodM.SetInt(100);
        Input_BlendPeriodM.SetIntLimits(10, 2000);

        Input_BlendPeriodL.Name = "Blend Period Long";
        Input_BlendPeriodL.SetInt(200);
        Input_BlendPeriodL.SetIntLimits(10, 2000);

        Input_BlendWeightS.Name = "Blend Weight Short";
        Input_BlendWeightS.SetFloat(1.0f);
        Input_BlendWeightS.SetFloatLimits(0.0f, 100.0f);

        Input_BlendWeightM.Name = "Blend Weight Medium";
        Input_BlendWeightM.SetFloat(1.0f);
        Input_BlendWeightM.SetFloatLimits(0.0f, 100.0f);

        Input_BlendWeightL.Name = "Blend Weight Long";
        Input_BlendWeightL.SetFloat(1.0f);
        Input_BlendWeightL.SetFloatLimits(0.0f, 100.0f);

        Input_GradientBasis.Name = "Gradient Basis Coloring";
        Input_GradientBasis.SetYesNo(true);

        Input_EnableAlerts.Name = "Enable Alerts";
        Input_EnableAlerts.SetYesNo(false);

        return;
    }

    if (sc.LastCallToFunction)
        return;

    // ============================================================
    // READ INPUTS
    // ============================================================
    int rollWin = Input_RollingWindow.GetInt();
    bool isFixed = (Input_BandMode.GetIndex() == 0);
    float fixedZUp = Input_FixedZUp.GetFloat();
    float fixedZDn = Input_FixedZDn.GetFloat();
    bool forceSymmetric = Input_ForceSymmetric.GetYesNo();
    float minBandZ = Input_MinBandZ.GetFloat();
    float innerFrac = Input_InnerFrac.GetFloat();
    float pctUpper = Input_PctUpper.GetFloat();
    float pctLower = Input_PctLower.GetFloat();
    bool smoothZEnabled = Input_SmoothZ.GetYesNo();
    bool smoothBandsEnabled = Input_SmoothBands.GetYesNo();
    int smoothType = Input_SmoothType.GetIndex(); // 0=LinReg 1=Hull MA 2=Super Smoother 3=Two-Pole Gaussian
    int smoothLength = Input_SmoothLength.GetInt();
    int periodS = Input_BlendPeriodS.GetInt();
    int periodM = Input_BlendPeriodM.GetInt();
    int periodL = Input_BlendPeriodL.GetInt();
    float weightS = Input_BlendWeightS.GetFloat();
    float weightM = Input_BlendWeightM.GetFloat();
    float weightL = Input_BlendWeightL.GetFloat();
    bool gradientBasis = Input_GradientBasis.GetYesNo();
    bool alertsEnabled = Input_EnableAlerts.GetYesNo();

    SCFloatArrayRef InData = sc.BaseDataIn[Input_Data.GetInputDataIndex()];

    // ============================================================
    // DATA START INDEX (Pine fix #2: suppress plotting/state entirely
    // until the longest required window has real data, rather than
    // silently falling back to the min-Z floor and then jumping).
    // Only blend periods with a positive weight participate.
    // ============================================================
    int longestBlend = 0;
    if (weightS > 0.0f) longestBlend = (std::max)(longestBlend, periodS);
    if (weightM > 0.0f) longestBlend = (std::max)(longestBlend, periodM);
    if (weightL > 0.0f) longestBlend = (std::max)(longestBlend, periodL);
    bool adaptiveInUse = !isFixed && longestBlend > 0;
    sc.DataStartIndex = adaptiveInUse ? ((std::max)(rollWin, longestBlend) - 1) : (rollWin - 1);

    int idx = sc.Index;
    float src = InData[idx];

    // ============================================================
    // LOCATION AND DISPERSION
    // ============================================================
    float mean = 0.0f, disp = 0.0f;
    bool haveRollWin = (idx >= rollWin - 1);
    if (haveRollWin)
        RollingMeanStdev(InData, idx, rollWin, mean, disp);

    float zRaw = (haveRollWin && disp > 1e-12f) ? (src - mean) / disp : 0.0f;
    Subgraph_ZScore.Arrays[0][idx] = zRaw;

    // ============================================================
    // SMOOTHED Z-SCORE (only the selected smoother type is computed)
    // ============================================================
    float smZ;
    if (smoothType == 0)
        smZ = LinRegEndpoint(Subgraph_ZScore.Arrays[0], idx, smoothLength);
    else if (smoothType == 1)
        smZ = HullMA(Subgraph_ZScore.Arrays[0], Subgraph_ZScore.Arrays[2], idx, smoothLength);
    else if (smoothType == 2)
        smZ = SuperSmoother(Subgraph_ZScore.Arrays[0], Subgraph_ZScore.Arrays[1], idx, smoothLength);
    else
        smZ = TwoPoleGaussian(Subgraph_ZScore.Arrays[0], Subgraph_ZScore.Arrays[1], idx, smoothLength);

    float zScore = smoothZEnabled ? smZ : zRaw;
    Subgraph_ZScore.Data[idx] = zScore;

    // ============================================================
    // PERCENTILE BAND LEVELS IN Z UNITS (adaptive multi-period blend)
    // ============================================================
    float wSum = 0.0f, upWSum = 0.0f, dnWSum = 0.0f;
    float upP, dnP;

    // Skipped entirely in Fixed mode and for zero-weight periods — the sort
    // per period is the most expensive step in the study.
    if (adaptiveInUse)
    {
        if (weightS > 0.0f && PercentileBlendPair(Subgraph_ZScore.Data, idx, periodS, pctUpper, pctLower, upP, dnP))
        {
            upWSum += upP * weightS;
            dnWSum += dnP * weightS;
            wSum += weightS;
        }
        if (weightM > 0.0f && PercentileBlendPair(Subgraph_ZScore.Data, idx, periodM, pctUpper, pctLower, upP, dnP))
        {
            upWSum += upP * weightM;
            dnWSum += dnP * weightM;
            wSum += weightM;
        }
        if (weightL > 0.0f && PercentileBlendPair(Subgraph_ZScore.Data, idx, periodL, pctUpper, pctLower, upP, dnP))
        {
            upWSum += upP * weightL;
            dnWSum += dnP * weightL;
            wSum += weightL;
        }
    }

    bool adaptiveAvailable = (wSum > 0.0f);
    float zAdaptUp = adaptiveAvailable ? (upWSum / wSum) : 0.0f;
    float zAdaptDn = adaptiveAvailable ? (dnWSum / wSum) : 0.0f;

    float zUpSel = isFixed ? fixedZUp : (adaptiveAvailable ? zAdaptUp : fixedZUp);
    float zDnSel = isFixed ? fixedZDn : (adaptiveAvailable ? zAdaptDn : fixedZDn);

    float zUpFlr = (std::max)(zUpSel, minBandZ);
    float zDnFlr = (std::min)(zDnSel, -minBandZ);

    Subgraph_UpperZ.Arrays[0][idx] = zUpFlr;
    Subgraph_LowerZ.Arrays[0][idx] = zDnFlr;

    // ============================================================
    // SMOOTHED BAND LEVELS (re-clamped after smoothing — Pine fix #1)
    // ============================================================
    float smU, smD;
    if (smoothType == 0)
    {
        smU = LinRegEndpoint(Subgraph_UpperZ.Arrays[0], idx, smoothLength);
        smD = LinRegEndpoint(Subgraph_LowerZ.Arrays[0], idx, smoothLength);
    }
    else if (smoothType == 1)
    {
        smU = HullMA(Subgraph_UpperZ.Arrays[0], Subgraph_UpperZ.Arrays[2], idx, smoothLength);
        smD = HullMA(Subgraph_LowerZ.Arrays[0], Subgraph_LowerZ.Arrays[2], idx, smoothLength);
    }
    else if (smoothType == 2)
    {
        smU = SuperSmoother(Subgraph_UpperZ.Arrays[0], Subgraph_UpperZ.Arrays[1], idx, smoothLength);
        smD = SuperSmoother(Subgraph_LowerZ.Arrays[0], Subgraph_LowerZ.Arrays[1], idx, smoothLength);
    }
    else
    {
        smU = TwoPoleGaussian(Subgraph_UpperZ.Arrays[0], Subgraph_UpperZ.Arrays[1], idx, smoothLength);
        smD = TwoPoleGaussian(Subgraph_LowerZ.Arrays[0], Subgraph_LowerZ.Arrays[1], idx, smoothLength);
    }

    float zUp, zDn;
    if (smoothBandsEnabled)
    {
        zUp = (std::max)(smU, minBandZ);
        zDn = (std::min)(smD, -minBandZ);
    }
    else
    {
        zUp = zUpFlr;
        zDn = zDnFlr;
    }

    if (forceSymmetric)
    {
        float mag = (std::max)(std::fabs(zUp), std::fabs(zDn));
        zUp = mag;
        zDn = -mag;
    }

    Subgraph_UpperZ.Data[idx] = zUp;
    Subgraph_LowerZ.Data[idx] = zDn;

    // ============================================================
    // INVERSION BACK TO PRICE
    // ============================================================
    float basis = mean;
    float upper = mean + zUp * disp;
    float lower = mean + zDn * disp;
    float innerUp = mean + zUp * innerFrac * disp;
    float innerDn = mean + zDn * innerFrac * disp;

    Subgraph_UpperBand.Data[idx] = upper;
    Subgraph_LowerBand.Data[idx] = lower;
    Subgraph_Basis.Data[idx] = basis;
    Subgraph_InnerUpper.Data[idx] = innerUp;
    Subgraph_InnerLower.Data[idx] = innerDn;

    Subgraph_UpperFillTop.Data[idx] = upper;
    Subgraph_UpperFillBottom.Data[idx] = basis;
    Subgraph_LowerFillTop.Data[idx] = basis;
    Subgraph_LowerFillBottom.Data[idx] = lower;

    // ============================================================
    // COLORS
    // ============================================================
    COLORREF basisColor = Subgraph_Basis.PrimaryColor;
    COLORREF upperColor = Subgraph_UpperBand.PrimaryColor;
    COLORREF lowerColor = Subgraph_LowerBand.PrimaryColor;

    COLORREF gradColor = basisColor;
    if (gradientBasis)
    {
        if (zScore >= 0.0f)
        {
            float t = (zUp > 1e-12f) ? (zScore / zUp) : 0.0f;
            gradColor = LerpColor(basisColor, upperColor, t);
        }
        else
        {
            float t = (zDn < -1e-12f) ? (zScore / zDn) : 0.0f;
            gradColor = LerpColor(basisColor, lowerColor, t);
        }
    }
    Subgraph_Basis.DataColor[idx] = gradColor;

    // ============================================================
    // BAR COLORING (opt-in: user sets Bar Color subgraph DrawStyle to
    // DRAWSTYLE_COLOR_BAR; Data/DataColor are always kept current)
    // ============================================================
    Subgraph_BarColor.Data[idx] = 1.0f;
    if (src > upper)
        Subgraph_BarColor.DataColor[idx] = upperColor;
    else if (src < lower)
        Subgraph_BarColor.DataColor[idx] = lowerColor;
    else
        Subgraph_BarColor.DataColor[idx] = gradColor;

    // ============================================================
    // READOUTS (data-window values; already written above as the
    // authoritative final state — Z-Score, Upper Z, Lower Z)
    // ============================================================

    // ============================================================
    // SIGNALS (Pine crossover/crossunder semantics: strictly beyond NOW,
    // not strictly beyond on the previous bar)
    // ============================================================
    Subgraph_ReEntryUp.Data[idx] = 0.0f;
    Subgraph_ReEntryDown.Data[idx] = 0.0f;

    bool prevValid = (idx >= 1) && (idx - 1 >= sc.DataStartIndex);
    bool reentryUp = false, reentryDn = false;
    bool crossOverUpper = false, crossUnderLower = false;
    bool crossOverBasis = false, crossUnderBasis = false;

    if (prevValid)
    {
        float prevSrc = InData[idx - 1];
        float prevUpper = Subgraph_UpperBand.Data[idx - 1];
        float prevLower = Subgraph_LowerBand.Data[idx - 1];
        float prevBasis = Subgraph_Basis.Data[idx - 1];

        reentryUp = CrossesOver(src, lower, prevSrc, prevLower);
        reentryDn = CrossesUnder(src, upper, prevSrc, prevUpper);
        crossOverUpper = CrossesOver(src, upper, prevSrc, prevUpper);
        crossUnderLower = CrossesUnder(src, lower, prevSrc, prevLower);
        crossOverBasis = CrossesOver(src, basis, prevSrc, prevBasis);
        crossUnderBasis = CrossesUnder(src, basis, prevSrc, prevBasis);

        if (reentryUp)
            Subgraph_ReEntryUp.Data[idx] = sc.Low[idx] - 2.0f * sc.TickSize;
        if (reentryDn)
            Subgraph_ReEntryDown.Data[idx] = sc.High[idx] + 2.0f * sc.TickSize;
    }

    // ============================================================
    // ALERTS
    // ============================================================
    if (alertsEnabled && prevValid && sc.GetBarHasClosedStatus(idx) == BHCS_BAR_HAS_CLOSED)
    {
        if (crossOverUpper)
            sc.SetAlert(1, "Break Above Upper");
        if (crossUnderLower)
            sc.SetAlert(2, "Break Below Lower");
        if (reentryDn)
            sc.SetAlert(3, "Re-Entry From Above");
        if (reentryUp)
            sc.SetAlert(4, "Re-Entry From Below");
        if (crossOverBasis)
            sc.SetAlert(5, "Basis Cross Up");
        if (crossUnderBasis)
            sc.SetAlert(6, "Basis Cross Down");
    }
}
