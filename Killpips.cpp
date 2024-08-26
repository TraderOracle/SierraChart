#include "sierrachart.h" 

SCDLLName("Killpips DLL")

void DrawLines(SCString str, 
	SCStudyInterfaceRef sc, 
	SCSubgraphRef Subgraph_kvo,
	SCSubgraphRef Subgraph_BL,
	SCSubgraphRef Subgraph_range,
	SCSubgraphRef Subgraph_min,
	SCSubgraphRef Subgraph_max)
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
			Tool.Color = COLOR_LIME;
			if (desc.Left(2) == "BL")
			{
				Tool.LineStyle = Subgraph_BL.LineStyle;
				Tool.LineWidth = Subgraph_BL.LineWidth;
				Tool.Color = Subgraph_BL.PrimaryColor;
			}
			if (desc.Left(3) == "kvo")
			{
				Tool.LineStyle = Subgraph_kvo.LineStyle;
				Tool.LineWidth = Subgraph_kvo.LineWidth;
				Tool.Color = Subgraph_kvo.PrimaryColor;
			}
			if (desc.Left(5) == "range")
			{
				Tool.LineStyle = Subgraph_range.LineStyle;
				Tool.LineWidth = Subgraph_range.LineWidth;
				Tool.Color = Subgraph_range.PrimaryColor;
			}
			if (desc.Right(3) == "min")
			{
				Tool.LineStyle = Subgraph_min.LineStyle;
				Tool.LineWidth = Subgraph_min.LineWidth;
				Tool.Color = Subgraph_min.PrimaryColor;
			}
			if (desc.Right(3) == "max")
			{
				Tool.LineStyle = Subgraph_max.LineStyle;
				Tool.LineWidth = Subgraph_max.LineWidth;
				Tool.Color = Subgraph_max.PrimaryColor;
			}
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
	SCInputRef Input_RecalcInterval = sc.Input[7];

	SCSubgraphRef Subgraph_kvo = sc.Subgraph[0];
	SCSubgraphRef Subgraph_BL = sc.Subgraph[1];
	SCSubgraphRef Subgraph_range = sc.Subgraph[2];
	SCSubgraphRef Subgraph_min = sc.Subgraph[3];
	SCSubgraphRef Subgraph_max = sc.Subgraph[4];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Killpips Levels v1.3";
		sc.GraphRegion = 0;
		sc.AutoLoop = 1;

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
		Input_1_Lines.SetString("$NQ1!: range k max, 20185, range k+50%, 19987, range k 0, 19791, range k-50%, 19593, range k min, 19397, kvo2, 20086, kvo2, 19979, kvo2, 19904, kvo1, 19882, kvo1, 19832, kvo1, 19811, kvo1, 19773, kvo1, 19720, kvo1, 19674, kvo1, 19659, kvo2, 19517, kvo2, 19450");

		Input_2_Lines.Name = "Values";
		Input_2_Lines.SetString("$ES1!: range k max, 5730, range k+50%, 5691, range k 0, 5652, range k-50%, 5613, range k min, 5574, kvo2, 5720, kvo2, 5707, kvo2, 5685, kvo2, 5673, kvo2, 5662, kvo1, 5649, kvo1, 5642, kvo1, 5638, kvo2, 5631, kvo1, 5625, kvo1, 5615, kvo2, 5601, kvo2, 5682");

		Input_3_Lines.Name = "Values";
		Input_3_Lines.SetString("$YM1!: range k max, 41746, range k+50%, 41510, range k 0, 41274, range k-50%, 41036, range k min, 40797, kvo2, 41620, kvo2, 41326, kvo1, 41217, kvo1, 41169, kvo1, 41089, kvo1, 41060, kvo2, 40983, kvo2, 40882");

		Input_4_Lines.Name = "Values";
		Input_4_Lines.SetString("$CL1!: range k max, 77.18, range k+50%, 75.99, range k 0, 74.82, range k-50%, 73.65, range k min, 72.48, kvo2, 76.84, kvo2, 75.51, kvo2, 74.91, kvo1, 74.64, kvo1, 74.20, kvo1, 73.72, kvo1, 72.97");

		Input_5_Lines.Name = "Values";
		Input_5_Lines.SetString("$GC1!: range k max, 2586, range k+50%, 2566, range k 0, 2546, range k-50%, 2526, range k min, 2506, kvo2, 2570, kvo2, 2558, kvo1, 2551, kvo1, 2547, kvo1, 2541, kvo1, 2537, kvo1, 2531, kvo1, 2527, kvo1, 2518, kvo1, 2516");

		Input_6_Lines.Name = "Values";
		Input_6_Lines.SetString("$RTY1!: range k max, 2279, range k+50%, 2253, range k 0, 2227, range k-50%, 2201, range k min, 2175, kvo2, 2273, kvo2, 2261, kvo2, 2238, kvo2, 2231, kvo1, 2225, kvo1, 2222, kvo1, 2214, kvo2, 2188, kvo2, 2179");

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
	SCString sa = sc.GetChartSymbol(sc.ChartNumber);
	SCString chtName = sa.Format("$%s", sa.GetChars());
	if (chtName.Left(3) == s1.Left(3))
		DrawLines(s1, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max);
	if (chtName.Left(3) == s2.Left(3))
		DrawLines(s2, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max);
	if (chtName.Left(3) == s3.Left(3))
		DrawLines(s3, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max);
	if (chtName.Left(3) == s4.Left(3))
		DrawLines(s4, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max);
	if (chtName.Left(3) == s5.Left(3))
		DrawLines(s5, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max);
	if (chtName.Left(3) == s6.Left(3))
		DrawLines(s6, sc, Subgraph_kvo, Subgraph_BL, Subgraph_range, Subgraph_min, Subgraph_max);
}