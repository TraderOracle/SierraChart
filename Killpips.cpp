#include "sierrachart.h" 

SCDLLName("Killpips DLL")

void DrawLines(SCString str, SCStudyInterfaceRef sc)
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
			if (desc.Left(3) == "vix")
				Tool.Color = RGB(224, 206, 157);
			if (desc.Right(3) == "MAX" || desc.Right(3) == "max")
				Tool.Color = RGB(156, 166, 70);
			if (desc.Right(3) == "MIN" || desc.Right(3) == "min")
				Tool.Color = RGB(132, 245, 66);
			if (desc.Left(3) == "VAL")
				Tool.Color = RGB(13, 166, 112);
			if (desc.Left(3) == "VAH")
				Tool.Color = RGB(148, 24, 115);
			if (desc.Left(2) == "SD")
				Tool.Color = RGB(215, 66, 245);
			if (desc.Left(2) == "RD")
				Tool.Color = RGB(245, 200, 66);
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

	if (sc.SetDefaults)
	{
		sc.GraphName = "Killpips Levels v1.4";
		sc.GraphRegion = 0;
		sc.AutoLoop = 1;

		Input_RecalcInterval.Name = "Recalc Interval (seconds)";
		Input_RecalcInterval.SetInt(30);

		Input_1_Lines.Name = "Values";
		Input_1_Lines.SetString("$NQ1!: range k max, 20301");

		Input_2_Lines.Name = "Values";
		Input_2_Lines.SetString("$ES1!: range k max, 5721");

		Input_3_Lines.Name = "Values";
		Input_3_Lines.SetString("$YM1!: range k max, 41746");

		Input_4_Lines.Name = "Values";
		Input_4_Lines.SetString("$CL1!: range k max, 77.18");

		Input_5_Lines.Name = "Values";
		Input_5_Lines.SetString("$GC1!: range k max, 2586");

		Input_6_Lines.Name = "Values";
		Input_6_Lines.SetString("$RTY1!: range k max, 2279");

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
		DrawLines(s1, sc);
	if (chtName.Left(3) == s2.Left(3))
		DrawLines(s2, sc);
	if (chtName.Left(3) == s3.Left(3))
		DrawLines(s3, sc);
	if (chtName.Left(3) == s4.Left(3))
		DrawLines(s4, sc);
	if (chtName.Left(3) == s5.Left(3))
		DrawLines(s5, sc);
	if (chtName.Left(3) == s6.Left(3))
		DrawLines(s6, sc);
}