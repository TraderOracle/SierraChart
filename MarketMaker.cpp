#include "sierrachart.h" 

SCDLLName("MarketMaker.DLL")

void DrawLines(SCString str, SCStudyInterfaceRef sc)
{
	int idx = 1;

	SCString sMQ1 = str.Trim();

	std::vector<char*> tokens;
	sMQ1.Tokenize(",", tokens);
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

			if (desc.Right(2) == "L1" || desc.Right(2) == "L2" || desc.Right(2) == "L3" || desc.Right(2) == "L4" || desc.Right(2) == "L5")
				Tool.Color = RGB(132, 245, 66);
			if (desc.Right(2) == "H1" || desc.Right(2) == "H2" || desc.Right(2) == "H3" || desc.Right(2) == "H4" || desc.Right(2) == "H5")
				Tool.Color = RGB(156, 166, 70);
			if (desc.Right(3) == "Mid")
				Tool.Color = RGB(245, 200, 66);
			sc.UseTool(Tool);
		}
		else
			desc = s.GetChars();
		idx++;
	}

}

SCSFExport scsf_MoneyMaker_Levels(SCStudyInterfaceRef sc)
{
	SCGraphData srcChart;
	SCGraphData ref;
	SCString Message = "";

	SCInputRef Monday_MML = sc.Input[0];
	SCInputRef Tuesday_MML = sc.Input[1];
	SCInputRef Wednesday_MML = sc.Input[2];
	SCInputRef Thursday_MML = sc.Input[3];
	SCInputRef Friday_MML = sc.Input[4];
	SCInputRef Values = sc.Input[5];
	SCInputRef RecalcInterval = sc.Input[7];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Market Maker Levels";
		sc.GraphRegion = 0;
		sc.AutoLoop = 1;

		RecalcInterval.Name = "Recalc Interval (seconds)";
		RecalcInterval.SetInt(30);

		Monday_MML.Name = "Monday MML";
		Monday_MML.SetString("");

		Tuesday_MML.Name = "Tuesday MML";
		Tuesday_MML.SetString("");

		Wednesday_MML.Name = "Wednesday MML";
		Wednesday_MML.SetString("");

		Thursday_MML.Name = "Thursday MML";
		Thursday_MML.SetString("");

		Friday_MML.Name = "Friday MML";
		Friday_MML.SetString("");

		Values.Name = "Values";
		Values.SetString("");

		return;
	}

	int RecalcIntervalSec = Input_RecalcInterval.GetInt();
	int& LastUpdated = sc.GetPersistentInt(9);
	SCDateTime Now = sc.CurrentSystemDateTime;
	int TimeInSec = Now.GetTimeInSeconds();
	if (LastUpdated > 0 && LastUpdated + RecalcIntervalSec > TimeInSec && sc.Index != 0)
		return;
	LastUpdated = TimeInSec;

	SCString s1 = Monday_MML.GetString();
	SCString s2 = Tuesday_MML.GetString();
	SCString s3 = Wednesday_MML.GetString();
	SCString s4 = Thursday_MML.GetString();
	SCString s5 = Friday_MML.GetString();
	SCString s6 = Input_6_Lines.GetString();
	SCString sa = sc.GetChartSymbol(sc.ChartNumber);
	SCString chtName = sa.Format("$%s", sa.GetChars());
	if (s1 != "")
		DrawLines(s1, sc);
	if (s2 != "")
		DrawLines(s2, sc);
	if (s3 != "")
		DrawLines(s3, sc);
	if (s4 != "")
		DrawLines(s4, sc);
	if (s5 != "")
		DrawLines(s5, sc);
}
