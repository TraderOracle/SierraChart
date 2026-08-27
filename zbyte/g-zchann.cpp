// Rolling Z-Score Channel
// ACSIL port of the Pine v6 "Rolling Z-Score Channel" indicator.
//
// Compile in Sierra Chart: Analysis >> Build Custom Studies DLL
// (place this file in ACS_Source or add it to the build list).
//
// Percentile method: Hyndman-Fan type 7 / Excel PERCENTILE.INC
//   pos = (p/100) * (n-1), linear interpolate. Matches Pine
//   ta.percentile_linear_interpolation.
// Stdev default: sample (n-1), matching Pine ta.stdev(src, len, false).
// IIR smoothers seed missing history as 0, matching Pine nz().
// Hull MA uses Subgraph.Arrays[0-3] internally; pre-smooth series is Arrays[4].

#include "sierrachart.h"
#include <algorithm>
#include <cmath>

SCDLLName("RollingZScoreChannel")

namespace
{
	const int kPercentileMaxLength = 500;
	const double kPi = 3.14159265358979323846;
	const double kSqrt2 = 1.41421356237309504880;
	const float kEps = 1e-10f;

	const COLORREF kBear = RGB(255, 20, 147);
	const COLORREF kBull = RGB(57, 255, 20);
	const COLORREF kNeutral = RGB(120, 120, 200);

	enum BandModeIndex
	{
		BANDMODE_ADAPTIVE = 0,
		BANDMODE_FIXED = 1
	};

	enum SmoothTypeIndex
	{
		SMOOTH_LINREG = 0,
		SMOOTH_HULL = 1,
		SMOOTH_SS = 2,
		SMOOTH_GAUSS = 3
	};

	enum StdevMethodIndex
	{
		STDEV_SAMPLE = 0,
		STDEV_POPULATION = 1
	};

	static void ClearOverlayBar(SCStudyInterfaceRef sc, int Index)
	{
		for (int sg = 0; sg <= 12; ++sg)
			sc.Subgraph[sg][Index] = 0.0f;
		sc.Subgraph[14][Index] = 0.0f;
		sc.Subgraph[15][Index] = 0.0f;
	}

	static float StdevWindow(SCFloatArrayRef Data, int Index, int Length, bool Sample)
	{
		if (Length < 1)
			return 0.0f;
		if (Sample && Length < 2)
			return 0.0f;

		const int Start = Index - Length + 1;
		if (Start < 0)
			return 0.0f;

		double Sum = 0.0;
		for (int i = Start; i <= Index; ++i)
			Sum += Data[i];
		const double Mean = Sum / static_cast<double>(Length);

		double Sq = 0.0;
		for (int i = Start; i <= Index; ++i)
		{
			const double d = static_cast<double>(Data[i]) - Mean;
			Sq += d * d;
		}

		const double Denom = Sample
			? static_cast<double>(Length - 1)
			: static_cast<double>(Length);
		if (Denom <= 0.0)
			return 0.0f;

		return static_cast<float>(sqrt(Sq / Denom));
	}

	// Returns false when the window is not fully inside valid samples.
	static bool PercentileLinear(
		SCFloatArrayRef Data,
		int Index,
		int Length,
		int Offset,
		int MinValidIndex,
		float Percentage,
		float& Out)
	{
		if (Length < 1 || Length > kPercentileMaxLength)
			return false;
		if (Offset < 0)
			return false;

		const int End = Index - Offset;
		const int Start = End - Length + 1;
		if (Start < MinValidIndex || End < Start)
			return false;

		float Buf[kPercentileMaxLength];
		for (int i = 0; i < Length; ++i)
			Buf[i] = Data[Start + i];

		std::sort(Buf, Buf + Length);

		if (Percentage < 0.0f)
			Percentage = 0.0f;
		if (Percentage > 100.0f)
			Percentage = 100.0f;

		const float Pos = (Percentage / 100.0f) * static_cast<float>(Length - 1);
		int Lo = static_cast<int>(Pos);
		if (Lo < 0)
			Lo = 0;
		if (Lo >= Length)
			Lo = Length - 1;
		int Hi = Lo + 1;
		if (Hi >= Length)
			Hi = Length - 1;
		const float Frac = Pos - static_cast<float>(Lo);
		Out = Buf[Lo] + (Buf[Hi] - Buf[Lo]) * Frac;
		return true;
	}

	static float SuperSmootherAt(SCFloatArrayRef In, SCFloatArrayRef Out, int Index, int Length)
	{
		if (Length < 1)
			Length = 1;

		const double A1 = exp(-kSqrt2 * kPi / static_cast<double>(Length));
		const double B1 = 2.0 * A1 * cos(kSqrt2 * kPi / static_cast<double>(Length));
		const double C2 = B1;
		const double C3 = -A1 * A1;
		const double C1 = 1.0 - C2 - C3;

		const double X0 = In[Index];
		const double X1 = (Index >= 1) ? In[Index - 1] : 0.0;
		const double Y1 = (Index >= 1) ? Out[Index - 1] : 0.0;
		const double Y2 = (Index >= 2) ? Out[Index - 2] : 0.0;
		return static_cast<float>(C1 * (X0 + X1) * 0.5 + C2 * Y1 + C3 * Y2);
	}

	static float Gaussian2PoleAt(SCFloatArrayRef In, SCFloatArrayRef Out, int Index, int Length)
	{
		if (Length < 1)
			Length = 1;

		const double Beta = (1.0 - cos(2.0 * kPi / static_cast<double>(Length))) / (kSqrt2 - 1.0);
		const double Alpha = -Beta + sqrt(Beta * Beta + 2.0 * Beta);
		const double A2 = Alpha * Alpha;
		const double Om = 1.0 - Alpha;
		const double Om2 = Om * Om;

		const double X0 = In[Index];
		const double Y1 = (Index >= 1) ? Out[Index - 1] : 0.0;
		const double Y2 = (Index >= 2) ? Out[Index - 2] : 0.0;
		return static_cast<float>(A2 * X0 + 2.0 * Om * Y1 - Om2 * Y2);
	}

	// OutWork.Arrays[0-3] are reserved for Hull internals.
	static void ApplySmooth(
		SCStudyInterfaceRef sc,
		SCFloatArrayRef In,
		SCSubgraphRef OutWork,
		int Index,
		int Length,
		int Type)
	{
		if (Length < 2)
			Length = 2;

		if (Type == SMOOTH_LINREG)
			sc.LinearRegressionIndicator(In, OutWork, Index, Length);
		else if (Type == SMOOTH_HULL)
			sc.HullMovingAverage(In, OutWork, Index, Length);
		else if (Type == SMOOTH_SS)
			OutWork[Index] = SuperSmootherAt(In, OutWork, Index, Length);
		else
			OutWork[Index] = Gaussian2PoleAt(In, OutWork, Index, Length);
	}

	static COLORREF LerpColor(COLORREF A, COLORREF B, float T)
	{
		if (T < 0.0f)
			T = 0.0f;
		if (T > 1.0f)
			T = 1.0f;
		const int AR = GetRValue(A);
		const int AG = GetGValue(A);
		const int AB = GetBValue(A);
		const int BR = GetRValue(B);
		const int BG = GetGValue(B);
		const int BB = GetBValue(B);
		return RGB(
			AR + static_cast<int>((BR - AR) * T + 0.5f),
			AG + static_cast<int>((BG - AG) * T + 0.5f),
			AB + static_cast<int>((BB - AB) * T + 0.5f));
	}

	static void ConfigLine(
		SCSubgraphRef sg,
		const char* Name,
		COLORREF Color,
		int Width,
		int DrawStyle)
	{
		sg.Name = Name;
		sg.DrawStyle = DrawStyle;
		sg.PrimaryColor = Color;
		sg.LineWidth = Width;
		sg.DrawZeros = false;
	}
}

/*==========================================================================*/
SCSFExport scsf_RollingZScoreChannel(SCStudyInterfaceRef sc)
{
	SCSubgraphRef SG_Upper = sc.Subgraph[0];
	SCSubgraphRef SG_Lower = sc.Subgraph[1];
	SCSubgraphRef SG_Basis = sc.Subgraph[2];
	SCSubgraphRef SG_InnerUp = sc.Subgraph[3];
	SCSubgraphRef SG_InnerDn = sc.Subgraph[4];
	SCSubgraphRef SG_FillUTop = sc.Subgraph[5];
	SCSubgraphRef SG_FillUBot = sc.Subgraph[6];
	SCSubgraphRef SG_FillLTop = sc.Subgraph[7];
	SCSubgraphRef SG_FillLBot = sc.Subgraph[8];
	SCSubgraphRef SG_SigUp = sc.Subgraph[9];
	SCSubgraphRef SG_SigDn = sc.Subgraph[10];
	SCSubgraphRef SG_ColorBar = sc.Subgraph[11];
	SCSubgraphRef SG_BgTint = sc.Subgraph[12];
	SCSubgraphRef SG_Z = sc.Subgraph[13];
	SCSubgraphRef SG_ZUp = sc.Subgraph[14];
	SCSubgraphRef SG_ZDn = sc.Subgraph[15];
	SCSubgraphRef SG_WorkZ = sc.Subgraph[16];
	SCSubgraphRef SG_SmoothZ = sc.Subgraph[17];
	SCSubgraphRef SG_SmoothU = sc.Subgraph[18];
	SCSubgraphRef SG_SmoothD = sc.Subgraph[19];

	SCInputRef In_InputData = sc.Input[0];
	SCInputRef In_RollWin = sc.Input[1];
	SCInputRef In_BandMode = sc.Input[2];
	SCInputRef In_FixedUp = sc.Input[3];
	SCInputRef In_FixedDn = sc.Input[4];
	SCInputRef In_Symmetric = sc.Input[5];
	SCInputRef In_MinBandZ = sc.Input[6];
	SCInputRef In_ShowInner = sc.Input[7];
	SCInputRef In_InnerFrac = sc.Input[8];
	SCInputRef In_ShowBasis = sc.Input[9];
	SCInputRef In_PctUpper = sc.Input[10];
	SCInputRef In_PctLower = sc.Input[11];
	SCInputRef In_PctLenS = sc.Input[12];
	SCInputRef In_PctLenM = sc.Input[13];
	SCInputRef In_PctLenL = sc.Input[14];
	SCInputRef In_PctOffset = sc.Input[15];
	SCInputRef In_SmoothZ = sc.Input[16];
	SCInputRef In_SmoothBands = sc.Input[17];
	SCInputRef In_SmoothType = sc.Input[18];
	SCInputRef In_SmoothLen = sc.Input[19];
	SCInputRef In_StdevMethod = sc.Input[20];
	SCInputRef In_ReentryOutside = sc.Input[21];
	SCInputRef In_ShowSignals = sc.Input[22];
	SCInputRef In_ColorBars = sc.Input[23];
	SCInputRef In_ShowFill = sc.Input[24];
	SCInputRef In_BasisGradient = sc.Input[25];
	SCInputRef In_BgTint = sc.Input[26];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Rolling Z-Score Channel";
		sc.StudyDescription =
			"Adaptive rolling z-score channel. Sample stdev matches Pine ta.stdev(..., false). "
			"Re-entry markers require a prior close outside the band by default.";
		sc.AutoLoop = 0;
		sc.GraphRegion = 0;
		sc.ValueFormat = VALUEFORMAT_INHERITED;
		sc.ScaleRangeType = SCALE_SAMEASREGION;
		sc.DrawStudyUnderneathMainPriceGraph = 1;
		// sc.TransparencyLevel was removed from s_sc; fill transparency is
		// study-level. SetDefaults is supported for this call in current ACSIL.
		sc.SetChartStudyTransparencyLevel(sc.ChartNumber, sc.StudyGraphInstanceID, 70);

		ConfigLine(SG_Upper, "Upper Band", kBear, 2, DRAWSTYLE_LINE);
		ConfigLine(SG_Lower, "Lower Band", kBull, 2, DRAWSTYLE_LINE);
		ConfigLine(SG_Basis, "Basis", kNeutral, 2, DRAWSTYLE_LINE);
		ConfigLine(SG_InnerUp, "Inner Upper", kBear, 1, DRAWSTYLE_LINE);
		ConfigLine(SG_InnerDn, "Inner Lower", kBull, 1, DRAWSTYLE_LINE);

		SG_FillUTop.Name = "Upper Fill Top";
		SG_FillUTop.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_TOP;
		SG_FillUTop.PrimaryColor = kBear;
		SG_FillUTop.DrawZeros = false;

		SG_FillUBot.Name = "Upper Fill Bottom";
		SG_FillUBot.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_BOTTOM;
		SG_FillUBot.PrimaryColor = kBear;
		SG_FillUBot.DrawZeros = false;

		SG_FillLTop.Name = "Lower Fill Top";
		SG_FillLTop.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_TOP;
		SG_FillLTop.PrimaryColor = kBull;
		SG_FillLTop.DrawZeros = false;

		SG_FillLBot.Name = "Lower Fill Bottom";
		SG_FillLBot.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_BOTTOM;
		SG_FillLBot.PrimaryColor = kBull;
		SG_FillLBot.DrawZeros = false;

		SG_SigUp.Name = "Re-Entry Up";
		SG_SigUp.DrawStyle = DRAWSTYLE_ARROW_UP;
		SG_SigUp.PrimaryColor = kBull;
		SG_SigUp.LineWidth = 2;
		SG_SigUp.DrawZeros = false;

		SG_SigDn.Name = "Re-Entry Down";
		SG_SigDn.DrawStyle = DRAWSTYLE_ARROW_DOWN;
		SG_SigDn.PrimaryColor = kBear;
		SG_SigDn.LineWidth = 2;
		SG_SigDn.DrawZeros = false;

		SG_ColorBar.Name = "Color Bar";
		SG_ColorBar.DrawStyle = DRAWSTYLE_COLOR_BAR;
		SG_ColorBar.PrimaryColor = kNeutral;
		SG_ColorBar.DrawZeros = false;

		SG_BgTint.Name = "Extreme Tint";
		SG_BgTint.DrawStyle = DRAWSTYLE_BACKGROUND_TRANSPARENT;
		SG_BgTint.PrimaryColor = kBear;
		SG_BgTint.DrawZeros = false;

		SG_Z.Name = "Z-Score";
		SG_Z.DrawStyle = DRAWSTYLE_IGNORE;
		SG_Z.PrimaryColor = kNeutral;
		SG_Z.DrawZeros = true;

		SG_ZUp.Name = "Upper Z";
		SG_ZUp.DrawStyle = DRAWSTYLE_IGNORE;
		SG_ZUp.PrimaryColor = kBear;
		SG_ZUp.DrawZeros = false;

		SG_ZDn.Name = "Lower Z";
		SG_ZDn.DrawStyle = DRAWSTYLE_IGNORE;
		SG_ZDn.PrimaryColor = kBull;
		SG_ZDn.DrawZeros = false;

		// Unnamed work subgraphs stay off the Subgraphs tab.
		SG_WorkZ.DrawStyle = DRAWSTYLE_IGNORE;
		SG_SmoothZ.DrawStyle = DRAWSTYLE_IGNORE;
		SG_SmoothU.DrawStyle = DRAWSTYLE_IGNORE;
		SG_SmoothD.DrawStyle = DRAWSTYLE_IGNORE;

		In_InputData.Name = "Input Data";
		In_InputData.SetInputDataIndex(SC_LAST);

		In_RollWin.Name = "Rolling Window";
		In_RollWin.SetInt(80);
		In_RollWin.SetIntLimits(10, 500);

		In_BandMode.Name = "Band Mode";
		In_BandMode.SetCustomInputStrings("Adaptive;Fixed Z-Score");
		In_BandMode.SetCustomInputIndex(0);

		In_FixedUp.Name = "Fixed Upper Z";
		In_FixedUp.SetFloat(2.0f);
		In_FixedUp.SetFloatLimits(0.1f, 6.0f);

		In_FixedDn.Name = "Fixed Lower Z";
		In_FixedDn.SetFloat(-2.0f);
		In_FixedDn.SetFloatLimits(-6.0f, -0.1f);

		In_Symmetric.Name = "Force Symmetric Bands";
		In_Symmetric.SetYesNo(false);

		In_MinBandZ.Name = "Minimum Band Z";
		In_MinBandZ.SetFloat(0.5f);
		In_MinBandZ.SetFloatLimits(0.1f, 3.0f);

		In_ShowInner.Name = "Show Inner Bands";
		In_ShowInner.SetYesNo(true);

		In_InnerFrac.Name = "Inner Band Fraction";
		In_InnerFrac.SetFloat(0.5f);
		In_InnerFrac.SetFloatLimits(0.1f, 0.9f);

		In_ShowBasis.Name = "Show Basis Line";
		In_ShowBasis.SetYesNo(true);

		In_PctUpper.Name = "Upper Percentile";
		In_PctUpper.SetFloat(95.0f);
		In_PctUpper.SetFloatLimits(50.0f, 99.0f);

		In_PctLower.Name = "Lower Percentile";
		In_PctLower.SetFloat(5.0f);
		In_PctLower.SetFloatLimits(1.0f, 50.0f);

		In_PctLenS.Name = "Percentile Length Short";
		In_PctLenS.SetInt(50);
		In_PctLenS.SetIntLimits(10, kPercentileMaxLength);

		In_PctLenM.Name = "Percentile Length Medium";
		In_PctLenM.SetInt(100);
		In_PctLenM.SetIntLimits(10, kPercentileMaxLength);

		In_PctLenL.Name = "Percentile Length Long";
		In_PctLenL.SetInt(200);
		In_PctLenL.SetIntLimits(10, kPercentileMaxLength);

		In_PctOffset.Name = "Percentile Offset";
		In_PctOffset.SetInt(0);
		In_PctOffset.SetIntLimits(0, 1);

		In_SmoothZ.Name = "Smooth Z-Score";
		In_SmoothZ.SetYesNo(false);

		In_SmoothBands.Name = "Smooth Band Levels";
		In_SmoothBands.SetYesNo(false);

		In_SmoothType.Name = "Smoothing Type";
		In_SmoothType.SetCustomInputStrings("LinReg;Hull MA;Super Smoother;Two-Pole Gaussian");
		In_SmoothType.SetCustomInputIndex(3);

		In_SmoothLen.Name = "Smoothing Length";
		In_SmoothLen.SetInt(5);
		In_SmoothLen.SetIntLimits(2, 50);

		In_StdevMethod.Name = "Stdev Method";
		In_StdevMethod.SetCustomInputStrings("Sample (Pine match);Population");
		In_StdevMethod.SetCustomInputIndex(0);

		In_ReentryOutside.Name = "Re-Entry Requires Outside";
		In_ReentryOutside.SetYesNo(true);

		In_ShowSignals.Name = "Plot Re-Entry Markers";
		In_ShowSignals.SetYesNo(true);

		In_ColorBars.Name = "Color Bars";
		In_ColorBars.SetYesNo(false);

		In_ShowFill.Name = "Show Channel Fill";
		In_ShowFill.SetYesNo(true);

		In_BasisGradient.Name = "Basis Gradient Coloring";
		In_BasisGradient.SetYesNo(true);

		In_BgTint.Name = "Background Tint on Extremes";
		In_BgTint.SetYesNo(false);

		return;
	}

	if (sc.LastCallToFunction)
		return;

	const int DataIndex = In_InputData.GetInputDataIndex();
	SCFloatArrayRef Src = sc.BaseDataIn[DataIndex];

	int RollWin = In_RollWin.GetInt();
	if (RollWin < 10)
		RollWin = 10;

	const int BandMode = In_BandMode.GetIndex();
	const float FixedUp = In_FixedUp.GetFloat();
	const float FixedDn = In_FixedDn.GetFloat();
	const bool Symmetric = In_Symmetric.GetYesNo() != 0;
	float MinBandZ = In_MinBandZ.GetFloat();
	if (MinBandZ < 0.1f)
		MinBandZ = 0.1f;
	const bool ShowInner = In_ShowInner.GetYesNo() != 0;
	float InnerFrac = In_InnerFrac.GetFloat();
	if (InnerFrac < 0.1f)
		InnerFrac = 0.1f;
	if (InnerFrac > 0.9f)
		InnerFrac = 0.9f;
	const bool ShowBasis = In_ShowBasis.GetYesNo() != 0;
	const float PctUpper = In_PctUpper.GetFloat();
	const float PctLower = In_PctLower.GetFloat();
	int PctLenS = In_PctLenS.GetInt();
	int PctLenM = In_PctLenM.GetInt();
	int PctLenL = In_PctLenL.GetInt();
	if (PctLenS < 10)
		PctLenS = 10;
	if (PctLenM < 10)
		PctLenM = 10;
	if (PctLenL < 10)
		PctLenL = 10;
	if (PctLenS > kPercentileMaxLength)
		PctLenS = kPercentileMaxLength;
	if (PctLenM > kPercentileMaxLength)
		PctLenM = kPercentileMaxLength;
	if (PctLenL > kPercentileMaxLength)
		PctLenL = kPercentileMaxLength;
	const int PctOffset = In_PctOffset.GetInt();
	const bool SmoothZ = In_SmoothZ.GetYesNo() != 0;
	const bool SmoothBands = In_SmoothBands.GetYesNo() != 0;
	const int SmoothType = In_SmoothType.GetIndex();
	int SmoothLen = In_SmoothLen.GetInt();
	if (SmoothLen < 2)
		SmoothLen = 2;
	const bool SampleStdev = (In_StdevMethod.GetIndex() == STDEV_SAMPLE);
	const bool ReentryOutside = In_ReentryOutside.GetYesNo() != 0;
	const bool ShowSignals = In_ShowSignals.GetYesNo() != 0;
	const bool ColorBars = In_ColorBars.GetYesNo() != 0;
	const bool ShowFill = In_ShowFill.GetYesNo() != 0;
	const bool BasisGradient = In_BasisGradient.GetYesNo() != 0;
	const bool BgTint = In_BgTint.GetYesNo() != 0;

	int DataStart = RollWin - 1;
	if (BandMode == BANDMODE_ADAPTIVE)
	{
		int PctMax = PctLenS;
		if (PctLenM > PctMax)
			PctMax = PctLenM;
		if (PctLenL > PctMax)
			PctMax = PctLenL;
		DataStart = RollWin + PctMax + PctOffset - 2;
	}
	if ((SmoothZ || SmoothBands) && DataStart < RollWin + SmoothLen - 2)
		DataStart = RollWin + SmoothLen - 2;
	if (DataStart < 0)
		DataStart = 0;
	sc.DataStartIndex = DataStart;

	SCFloatArrayRef ArrSMA = SG_WorkZ.Arrays[0];
	SCFloatArrayRef ArrStdev = SG_WorkZ.Arrays[1];
	SCFloatArrayRef ArrZRaw = SG_WorkZ;           // Data[]
	SCFloatArrayRef ArrZUpRaw = SG_SmoothU.Arrays[4];
	SCFloatArrayRef ArrZDnRaw = SG_SmoothD.Arrays[4];

	const int LastIndex = sc.ArraySize - 1;
	const int AlertIndex = LastIndex - 1;

	for (int Index = sc.UpdateStartIndex; Index < sc.ArraySize; ++Index)
	{
		ClearOverlayBar(sc, Index);
		SG_Z[Index] = 0.0f;
		ArrZRaw[Index] = 0.0f;
		ArrSMA[Index] = 0.0f;
		ArrStdev[Index] = 0.0f;

		if (Index < RollWin - 1)
			continue;

		sc.SimpleMovAvg(Src, ArrSMA, Index, RollWin);
		const float Location = ArrSMA[Index];
		const float Dispersion = StdevWindow(Src, Index, RollWin, SampleStdev);
		ArrStdev[Index] = Dispersion;

		float ZRaw = 0.0f;
		if (Dispersion > kEps)
			ZRaw = (Src[Index] - Location) / Dispersion;
		ArrZRaw[Index] = ZRaw;

		float ZScore = ZRaw;
		if (SmoothZ)
		{
			ApplySmooth(sc, ArrZRaw, SG_SmoothZ, Index, SmoothLen, SmoothType);
			ZScore = SG_SmoothZ[Index];
		}
		SG_Z[Index] = ZScore;

		float ZUpSel = 0.0f;
		float ZDnSel = 0.0f;
		bool BandsValid = false;

		if (BandMode == BANDMODE_FIXED)
		{
			ZUpSel = FixedUp;
			ZDnSel = FixedDn;
			BandsValid = true;
		}
		else
		{
			float UpS = 0.0f, UpM = 0.0f, UpL = 0.0f;
			float DnS = 0.0f, DnM = 0.0f, DnL = 0.0f;
			const int FirstValidZ = RollWin - 1;
			const bool OkUpS = PercentileLinear(SG_Z, Index, PctLenS, PctOffset, FirstValidZ, PctUpper, UpS);
			const bool OkUpM = PercentileLinear(SG_Z, Index, PctLenM, PctOffset, FirstValidZ, PctUpper, UpM);
			const bool OkUpL = PercentileLinear(SG_Z, Index, PctLenL, PctOffset, FirstValidZ, PctUpper, UpL);
			const bool OkDnS = PercentileLinear(SG_Z, Index, PctLenS, PctOffset, FirstValidZ, PctLower, DnS);
			const bool OkDnM = PercentileLinear(SG_Z, Index, PctLenM, PctOffset, FirstValidZ, PctLower, DnM);
			const bool OkDnL = PercentileLinear(SG_Z, Index, PctLenL, PctOffset, FirstValidZ, PctLower, DnL);
			if (OkUpS && OkUpM && OkUpL && OkDnS && OkDnM && OkDnL)
			{
				ZUpSel = (UpS + UpM + UpL) / 3.0f;
				ZDnSel = (DnS + DnM + DnL) / 3.0f;
				BandsValid = true;
			}
		}

		if (!BandsValid)
			continue;

		float ZUpFlr = ZUpSel;
		float ZDnFlr = ZDnSel;
		if (ZUpFlr < MinBandZ)
			ZUpFlr = MinBandZ;
		if (ZDnFlr > -MinBandZ)
			ZDnFlr = -MinBandZ;

		ArrZUpRaw[Index] = ZUpFlr;
		ArrZDnRaw[Index] = ZDnFlr;

		float ZUpPre = ZUpFlr;
		float ZDnPre = ZDnFlr;
		if (SmoothBands)
		{
			ApplySmooth(sc, ArrZUpRaw, SG_SmoothU, Index, SmoothLen, SmoothType);
			ApplySmooth(sc, ArrZDnRaw, SG_SmoothD, Index, SmoothLen, SmoothType);
			ZUpPre = SG_SmoothU[Index];
			ZDnPre = SG_SmoothD[Index];
		}

		float ZUp = ZUpPre;
		float ZDn = ZDnPre;
		if (Symmetric)
		{
			float Mag = static_cast<float>(fabs(ZUpPre));
			const float MagDn = static_cast<float>(fabs(ZDnPre));
			if (MagDn > Mag)
				Mag = MagDn;
			ZUp = Mag;
			ZDn = -Mag;
		}

		SG_ZUp[Index] = ZUp;
		SG_ZDn[Index] = ZDn;

		const float Upper = Location + ZUp * Dispersion;
		const float Lower = Location + ZDn * Dispersion;
		const float InnerUp = Location + ZUp * InnerFrac * Dispersion;
		const float InnerDn = Location + ZDn * InnerFrac * Dispersion;

		SG_Upper[Index] = Upper;
		SG_Lower[Index] = Lower;
		if (ShowBasis)
			SG_Basis[Index] = Location;
		if (ShowInner)
		{
			SG_InnerUp[Index] = InnerUp;
			SG_InnerDn[Index] = InnerDn;
		}
		if (ShowFill)
		{
			SG_FillUTop[Index] = Upper;
			SG_FillUBot[Index] = Location;
			SG_FillLTop[Index] = Location;
			SG_FillLBot[Index] = Lower;
		}

		const float SrcVal = Src[Index];
		const bool InOB = SrcVal > Upper;
		const bool InOS = SrcVal < Lower;

		if (BasisGradient)
		{
			COLORREF Grad;
			if (ZScore >= 0.0f)
			{
				const float Top = (ZUp > 0.01f) ? ZUp : 0.01f;
				Grad = LerpColor(SG_Basis.PrimaryColor, SG_Upper.PrimaryColor, ZScore / Top);
			}
			else
			{
				const float Bot = (ZDn < -0.01f) ? ZDn : -0.01f;
				Grad = LerpColor(SG_Lower.PrimaryColor, SG_Basis.PrimaryColor, (ZScore - Bot) / (0.0f - Bot));
			}
			SG_Basis.DataColor[Index] = Grad;
		}

		if (ColorBars)
		{
			COLORREF BarC = SG_Basis.PrimaryColor;
			if (BasisGradient)
				BarC = SG_Basis.DataColor[Index];
			if (InOB)
				BarC = SG_Upper.PrimaryColor;
			else if (InOS)
				BarC = SG_Lower.PrimaryColor;
			SG_ColorBar[Index] = 1.0f;
			SG_ColorBar.DataColor[Index] = BarC;
		}

		if (BgTint && (InOB || InOS))
		{
			SG_BgTint[Index] = 1.0f;
			SG_BgTint.DataColor[Index] = InOB ? SG_Upper.PrimaryColor : SG_Lower.PrimaryColor;
		}

		bool ReentryUp = false;
		bool ReentryDn = false;
		bool CrossUpUpper = false;
		bool CrossDnLower = false;
		bool CrossUpBasis = false;
		bool CrossDnBasis = false;
		if (Index >= 1)
		{
			const float SrcPrev = Src[Index - 1];
			const float LoPrev = SG_Lower[Index - 1];
			const float UpPrev = SG_Upper[Index - 1];
			// Upper/Lower Z are floored away from 0 once bands are valid.
			const bool PrevLoValid = SG_ZDn[Index - 1] != 0.0f;
			const bool PrevUpValid = SG_ZUp[Index - 1] != 0.0f;
			const bool PrevBasisValid = ShowBasis && SG_Basis[Index - 1] != 0.0f;

			if (PrevLoValid)
			{
				if (ReentryOutside)
					ReentryUp = (SrcPrev < LoPrev) && (SrcVal > Lower);
				else
					ReentryUp = (SrcPrev <= LoPrev) && (SrcVal > Lower);
				CrossDnLower = (SrcPrev > LoPrev) && (SrcVal < Lower);
			}
			if (PrevUpValid)
			{
				if (ReentryOutside)
					ReentryDn = (SrcPrev > UpPrev) && (SrcVal < Upper);
				else
					ReentryDn = (SrcPrev >= UpPrev) && (SrcVal < Upper);
				CrossUpUpper = (SrcPrev < UpPrev) && (SrcVal > Upper);
			}
			if (PrevBasisValid)
			{
				const float BasisPrev = SG_Basis[Index - 1];
				CrossUpBasis = (SrcPrev < BasisPrev) && (SrcVal > Location);
				CrossDnBasis = (SrcPrev > BasisPrev) && (SrcVal < Location);
			}
		}

		if (ShowSignals)
		{
			if (ReentryUp)
				SG_SigUp[Index] = sc.Low[Index];
			if (ReentryDn)
				SG_SigDn[Index] = sc.High[Index];
		}

		// Manual looping does not set sc.Index; pass Index to SetAlert.
		if (!sc.IsFullRecalculation
			&& !sc.DownloadingHistoricalData
			&& Index == AlertIndex
			&& AlertIndex >= 1)
		{
			if (CrossUpUpper)
				sc.SetAlert(1, Index, "Break Above Upper");
			if (CrossDnLower)
				sc.SetAlert(2, Index, "Break Below Lower");
			if (ReentryDn)
				sc.SetAlert(3, Index, "Re-Entry From Above");
			if (ReentryUp)
				sc.SetAlert(4, Index, "Re-Entry From Below");
			if (CrossUpBasis)
				sc.SetAlert(5, Index, "Basis Cross Up");
			if (CrossDnBasis)
				sc.SetAlert(6, Index, "Basis Cross Down");
		}
	}
}
