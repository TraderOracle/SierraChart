#include "sierrachart.h" 

SCDLLName("Killpips DLL")

void DrawLines(SCString str, 
	SCStudyInterfaceRef sc, 
	SCSubgraphRef Subgraph_kvo,
	SCSubgraphRef Subgraph_BL,
	SCSubgraphRef Subgraph_range,
	SCSubgraphRef Subgraph_min,
	SCSubgraphRef Subgraph_max,
	SCSubgraphRef Subgraph_vix)
{
	int idx = 1;

	int iPos = str.IndexOf(':');
	if (iPos == -1)
		return;

	SCString sMQ1 = str.GetSubString(str.GetLength() - iPos - 1, iPos + 1);

	std::vector<char*> tokens;
	sMQ1.Tokenize(", ", tokens);
	SCString desc, price;
	for (SCString s : tokens)
	{
		if (idx % 2 == 0)
		{
			price = s.GetChars();
			float pr = atof(price.GetChars());

			s_UseTool Tool;
			Tool.LineStyle = LINESTYLE_SOLID;
			Tool.LineWidth = 1;
			Tool.TextAlignment = DT_RIGHT;
			Tool.DrawingType = DRAWING_HORIZONTALLINE;
			Tool.BeginValue = pr;
			Tool.EndValue = pr;
			Tool.ChartNumber = sc.ChartNumber;
			Tool.BeginDateTime = sc.BaseDateTimeIn[0];
			Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize - 1];
			Tool.AddMethod = UTAM_ADD_OR_ADJUST;
			Tool.ShowPrice = 0;
			Tool.Text.Format("%s", desc.GetChars());
			Tool.FontSize = 9;
			Tool.LineWidth = 1;
			Tool.LineNumber = idx;
			Tool.Color = COLOR_GAINSBORO;
			if (desc.Left(2) == "BL")
			{
				Tool.LineStyle = Subgraph_BL.LineStyle;
				Tool.LineWidth = Subgraph_BL.LineWidth;
				Tool.Color = Subgraph_BL.PrimaryColor;
			}
			if (desc.Left(3) == "vix" || desc.Left(3) == "HVL")
			{
				Tool.LineStyle = Subgraph_vix.LineStyle;
				Tool.LineWidth = Subgraph_vix.LineWidth;
				Tool.Color = Subgraph_vix.PrimaryColor;
			}
			if (desc.Left(3) == "kvo" || desc.Left(3) == "GEX")
			{
				Tool.LineStyle = Subgraph_kvo.LineStyle;
				Tool.LineWidth = Subgraph_kvo.LineWidth;
				Tool.Color = Subgraph_kvo.PrimaryColor;
			}
			if (desc.Left(5) == "range" || desc.Right(3) == "ort")
			{
				Tool.LineStyle = Subgraph_range.LineStyle;
				Tool.LineWidth = Subgraph_range.LineWidth;
				Tool.Color = Subgraph_range.PrimaryColor;
			}
			if (desc.Right(2) == "in" || desc.Left(3) == "HVL")
			{
				Tool.LineStyle = Subgraph_min.LineStyle;
				Tool.LineWidth = Subgraph_min.LineWidth;
				Tool.Color = Subgraph_min.PrimaryColor;
			}
			if (desc.Right(2) == "ax")
			{
				Tool.LineStyle = Subgraph_max.LineStyle;
				Tool.LineWidth = Subgraph_max.LineWidth;
				Tool.Color = Subgraph_max.PrimaryColor;
			}
			Tool.FontBold = true;
			sc.UseTool(Tool);
		}
		else
			desc = s.GetChars();
		idx++;
	}

}

SCSFExport scsf_Killpips_Levels(SCStudyInterfaceRef sc)
{
	SCGraphData srcChart;
	SCGraphData ref;
	SCString Message = "";

	SCInputRef Input_1_Lines = sc.Input[0];
	SCInputRef Input_2_Lines = sc.Input[1];
	SCInputRef Input_3_Lines = sc.Input[2];
	SCInputRef Input_4_Lines = sc.Input[3];
	SCInputRef Input_5_Lines = sc.Input[4];
	SCInputRef Input_6_Lines = sc.Input[5];
	SCInputRef Input_7_Lines = sc.Input[6];
	SCInputRef Input_8_Lines = sc.Input[7];
	SCInputRef Input_9_Lines = sc.Input[8];
	SCInputRef Input_10_Lines = sc.Input[9];
	SCInputRef Input_RecalcInterval = sc.Input[7];

	SCSubgraphRef Subgraph_kvo = sc.Subgraph[0];
	SCSubgraphRef Subgraph_BL = sc.Subgraph[1];
	SCSubgraphRef Subgraph_range = sc.Subgraph[2];
	SCSubgraphRef Subgraph_min = sc.Subgraph[3];
	SCSubgraphRef Subgraph_max = sc.Subgraph[4];
	SCSubgraphRef Subgraph_vix = sc.Subgraph[5];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Killpips Levels";
		sc.GraphRegion = 0;
		sc.AutoLoop = 1;

		Subgraph_vix.Name = "vix";
		Subgraph_vix.PrimaryColor = COLOR_WHITE;
		Subgraph_vix.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_vix.LineStyle = LINESTYLE_SOLID;
		Subgraph_vix.LineWidth = 1;
		Subgraph_vix.DrawZeros = false;

		Subgraph_kvo.Name = "kvo";
		Subgraph_kvo.PrimaryColor = COLOR_WHITE;
		Subgraph_kvo.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_kvo.LineStyle = LINESTYLE_SOLID;
		Subgraph_kvo.LineWidth = 1;
		Subgraph_kvo.DrawZeros = false;

		Subgraph_BL.Name = "BL";
		Subgraph_BL.PrimaryColor = COLOR_BLUE;
		Subgraph_BL.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_BL.LineStyle = LINESTYLE_SOLID;
		Subgraph_BL.LineWidth = 1;
		Subgraph_BL.DrawZeros = false;

		Subgraph_range.Name = "range";
		Subgraph_range.PrimaryColor = COLOR_YELLOW;
		Subgraph_range.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_range.LineStyle = LINESTYLE_SOLID;
		Subgraph_range.LineWidth = 1;
		Subgraph_range.DrawZeros = false;

		Subgraph_min.Name = "min";
		Subgraph_min.PrimaryColor = COLOR_GREEN;
		Subgraph_min.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_min.LineStyle = LINESTYLE_SOLID;
		Subgraph_min.LineWidth = 1;
		Subgraph_min.DrawZeros = false;

		Subgraph_max.Name = "max";
		Subgraph_max.PrimaryColor = COLOR_RED;
		Subgraph_max.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_max.LineStyle = LINESTYLE_SOLID;
		Subgraph_max.LineWidth = 1;
		Subgraph_max.DrawZeros = false;

		Input_RecalcInterval.Name = "Recalc Interval (seconds)";
		Input_RecalcInterval.SetInt(30);

		Input_1_Lines.Name = "Values";
		Input_1_Lines.SetString("NQ1!: vix r1, 19064, vix r2, 19095, vix s1, 18692, vix s2, 18661, range k max, 19368, range k+50%, 19168, range k 0, 18964, range k-50%, 18762, range k min, 18558, kvo, 19269, kvo, 19070, kvo, 18860, kvo, 18665, kvo, 18563, kvo, 19090");

		Input_2_Lines.Name = "Values";
		Input_2_Lines.SetString("$ES1!: vix r1, 5552, vix r2, 5561, vix s1, 5443, vix s2, 5434, range k max, 5593, kvo, 5573, range k+50%, 5554, kvo, 5532, range k 0, 5511.5, kvo, 5491, range k-50%, 5470.50, kvo, 5450, range k min, 5429.50, kvo, 5555.50, kvo, 5503");

		Input_3_Lines.Name = "Values";
		Input_3_Lines.SetString("$YM1!: vix r1, 41173, vix r2, 41242, vix s1, 40370, vix s2, 40303, range k max, 41323, kvo, 41235, range k+50%, 41076, kvo, 40995, range k 0, 40832, kvo, 40750, range k-50%, 40581, kvo, 40504, range k min, 40339, kvo, 40849, kvo, 41211");

		Input_4_Lines.Name = "Values";
		Input_4_Lines.SetString("$CL1!: range k max, 77.18, range k+50%, 75.99, range k 0, 74.82, range k-50%, 73.65, range k min, 72.48, kvo2, 76.84, kvo2, 75.51, kvo2, 74.91, kvo1, 74.64, kvo1, 74.20, kvo1, 73.72, kvo1, 72.97");

		Input_5_Lines.Name = "Values";
		Input_5_Lines.SetString("$GC1!: vix r1, 2565.8, vix r2, 2570.1, vix s1, 2515.7, vix s2, 2511.6, range k max, 2576, range k+50%, 2559.6, range k 0, 2543, range k-50%, 2526.6, range k min, 2510.1, kvo, 2567, kvo, 2551.5, kvo, 2534, kvo, 2517, kvo, 2511.4, kvo, 2537.1 ");

		Input_6_Lines.Name = "Values";
		Input_6_Lines.SetString("$RTY1!: vix r1, 2155.7, vix r2, 2159.3, vix s1, 2113.6, vix s2, 2110.1, range k max, 2181.9, range k+50%, 2159.6, range k 0, 2137.7, range k-50%, 2115.4, range k min, 2093.6, kvo, 2170, kvo, 2147, kvo, 2125, kvo, 2103, kvo, 2133.3, kvo, 2161.4");

		Input_7_Lines.Name = "Values";
		Input_7_Lines.SetString("$NG1!: vix r1, 2.281, vix r2, 2.285, vix s1, 2.237, vix s2, 2.233, range k max, 2.385, range k+50%, 2.320, range k 0, 2.253, range k-50%, 2.188, range k min, 2.123, kvo, 2.156, kvo, 2.223, kvo, 2.289, kvo, 2.353, kvo, 2.117, kvo, 2.248 ");

		Input_8_Lines.Name = "Values";
		Input_8_Lines.SetString("$FDAX1!: vix r1, 18766, vix r2, 18797, vix s1, 18399, vix s2, 18369, range k max, 18838, range k+50%, 18732, range k 0, 18625, range k-50%, 18517, range k min, 18410, kvo, 18465, kvo, 18572, kvo, 18680, kvo, 18787");

		Input_9_Lines.Name = "Values";
		Input_9_Lines.SetString("$SPX: vix r1, 5543, vix r2, 5553, vix s1, 5435, vix s2, 5426, range k max, 5569, kvo, 5552, range k+50%, 5535, kvo, 5519, range k 0, 5503, kvo, 5486, range k-50%, 5470.12, kvo, 5453, range k min, 5437, kvo, 5564 ");

		Input_10_Lines.Name = "Values";
		Input_10_Lines.SetString("$NDX: vix r1, 19069, vix r2, 19101, vix s1, 18697, vix s2, 18667, range k max, 19225, kvo, 19151, range k+50%, 19078, kvo, 19004, range k 0, 18930, kvo, 18856, range k-50%, 18782, kvo, 18708, range k min, 18634");

		return;
	}

	int RecalcIntervalSec = Input_RecalcInterval.GetInt();
	int& LastUpdated = sc.GetPersistentInt(9);
	SCDateTime Now = sc.CurrentSystemDateTime;
	int TimeInSec = Now.GetTimeInSeconds();
	if (LastUpdated > 0 && LastUpdated + RecalcIntervalSec > TimeInSec && sc.Index != 0)
		return;
	LastUpdated = TimeInSec;

	SCString s1 = Input_1_Lines.GetString();
	SCString s2 = Input_2_Lines.GetString();
	SCString s3 = Input_3_Lines.GetString();
	SCString s4 = Input_4_Lines.GetString();
	SCString s5 = Input_5_Lines.GetString();
	SCString s6 = Input_6_Lines.GetString();
	SCString s7 = Input_7_Lines.GetString();
	SCString s8 = Input_8_Lines.GetString();
	SCString s9 = Input_9_Lines.GetString();
	SCString s10 = Input_10_Lines.GetString();
	SCString sa = sc.GetChartSymbol(sc.ChartNumber);
	SCString chtName = sa.Format("$%s", sa.GetChars());
	if (chtName.Left(3) == s1.Left(3))
		DrawLines(s1, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max, Subgraph_vix);
	if (chtName.Left(3) == s2.Left(3))
		DrawLines(s2, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max, Subgraph_vix);
	if (chtName.Left(3) == s3.Left(3))
		DrawLines(s3, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max, Subgraph_vix);
	if (chtName.Left(3) == s4.Left(3))
		DrawLines(s4, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max, Subgraph_vix);
	if (chtName.Left(3) == s5.Left(3))
		DrawLines(s5, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max, Subgraph_vix);
	if (chtName.Left(3) == s6.Left(3))
		DrawLines(s6, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max, Subgraph_vix);
	if (chtName.Left(3) == s7.Left(3))
		DrawLines(s7, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max, Subgraph_vix);
	if (chtName.Left(3) == s8.Left(3))
		DrawLines(s8, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max, Subgraph_vix);
	if (chtName.Left(3) == s9.Left(3))
		DrawLines(s9, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max, Subgraph_vix);
	if (chtName.Left(3) == s10.Left(3))
		DrawLines(s10, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max, Subgraph_vix);
}