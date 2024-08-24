#include "sierrachart.h" 

SCDLLName("Killpips DLL") 

SCSFExport scsf_Killpips_Levels(SCStudyInterfaceRef sc)
{
	SCGraphData srcChart;
	SCGraphData ref;
	SCString Message = "";

	SCInputRef Input_MQ_Lines = sc.Input[0];
	SCInputRef Input_ES_Lines = sc.Input[1];
	SCInputRef Input_RecalcInterval = sc.Input[2];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Killpips Levels";
		sc.GraphRegion = 0;
		sc.AutoLoop = 1;

		Input_RecalcInterval.Name = "Recalc Interval (seconds)";
		Input_RecalcInterval.SetInt(30);

		Input_MQ_Lines.Name = "NQ Values";
		Input_MQ_Lines.SetString("$NQ1!: range k max, 20301, range k+50%, 20105, range k 0, 19912, range k-50%, 19713, range k min, 19517, kvo1, 19881, kvo2, 20018, kvo2, 20164, kvo2, 20229, kvo1, 19856, kvo1, 19822, kvo2, 19771, kvo2, 19741, kvo2, 19685, kvo2, 19599, kvo2, 19551");

		Input_ES_Lines.Name = "ES Values";
		Input_ES_Lines.SetString("$ES1!: range k max, 5721, range k+50%, 5681, range k 0, 5641, range k-50%, 5602, range k min, 5562, kvo2, 5704, kvo2, 5688, kvo2, 5671, kvo2, 5662, kvo2, 5655, kvo1, 5638, kvo1, 5632, kvo1, 5630, kvo1, 5620, kvo1, 5618, kvo2, 5610, kvo2, 5590, kvo2, 5575");

		return;
	}

	int RecalcIntervalSec = Input_RecalcInterval.GetInt();
	int& LastUpdated = sc.GetPersistentInt(9);
	SCDateTime Now = sc.CurrentSystemDateTime;
	int TimeInSec = Now.GetTimeInSeconds();
	if (LastUpdated > 0 && LastUpdated + RecalcIntervalSec > TimeInSec && sc.Index != 0)
		return;
	LastUpdated = TimeInSec;

	SCString sMQ = Input_MQ_Lines.GetString();
	SCString sES = Input_ES_Lines.GetString();
	SCString sa = sc.GetChartSymbol(sc.ChartNumber);

	SCString chtName = sa.Format("$%s", sa.GetChars());
	if (chtName.Left(3) == "$NQ")
	{
		int iPos = sMQ.IndexOf(':');
		if (iPos != -1)
		{
			SCString sMQ1 = sMQ.GetSubString(sMQ.GetLength() - iPos - 1, iPos + 1);
			SCString sES1 = sES.GetSubString(sES.GetLength() - iPos - 1, iPos + 1);

			int idx = 1;
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
					Tool.Color = COLOR_LIME;
					if (desc.Left(3) == " BL")
						Tool.Color = RGB(224, 206, 157);
					if (desc.Left(4) == " kvo")
						Tool.Color = RGB(156, 166, 70);
					if (desc.Left(5) == " range")
						Tool.Color = RGB(8, 150, 22);
					sc.UseTool(Tool);
				}
				else
					desc = s.GetChars();
				idx++;
			}
		}
	}

	if (chtName.Left(3) == "$ES")
	{
		int iPos = sMQ.IndexOf(':');
		if (iPos != -1)
		{
			SCString sMQ1 = sES.GetSubString(sES.GetLength() - iPos - 1, iPos + 1);

			int idx = 1;
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
					Tool.Color = COLOR_LIME;
					if (desc.Left(3) == " BL")
						Tool.Color = RGB(224, 206, 157);
					if (desc.Left(4) == " kvo")
						Tool.Color = RGB(156, 166, 70);
					if (desc.Left(5) == " range")
						Tool.Color = RGB(8, 150, 22);
					sc.UseTool(Tool);
				}
				else
					desc = s.GetChars();
				idx++;
			}
		}
	}
}