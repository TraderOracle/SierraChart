#include "sierrachart.h" 

SCDLLName("Mancini Plus DLL") 

SCSFExport scsf_ManciniPlus(SCStudyInterfaceRef sc)
{
	SCGraphData srcChart;
	SCGraphData ref;
	SCString Message = "";

	SCInputRef Input_Support = sc.Input[0];
	SCInputRef Input_Resistance = sc.Input[1];
	SCInputRef Input_RatioScanner = sc.Input[2];
	SCInputRef Input_Ratio = sc.Input[3];
    SCInputRef Input_Text = sc.Input[4];
	SCInputRef Input_TextSize = sc.Input[5];
    SCInputRef Input_SourceTicker = sc.Input[7];
	SCInputRef Input_ShowPrice = sc.Input[8];
	SCInputRef Input_RecalcInterval = sc.Input[9];

	SCSubgraphRef Subgraph_Support = sc.Subgraph[0];
	SCSubgraphRef Subgraph_SupportMajor = sc.Subgraph[1];
	SCSubgraphRef Subgraph_Resist = sc.Subgraph[2];
	SCSubgraphRef Subgraph_ResistMajor = sc.Subgraph[3];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Mancini Plus";
		sc.GraphRegion = 0;
		sc.AutoLoop = 1;

		Subgraph_Resist.Name = "Resistance";
		Subgraph_Resist.PrimaryColor = COLOR_RED;
		Subgraph_Resist.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_Resist.LineStyle = LINESTYLE_SOLID;
		Subgraph_Resist.LineWidth = 1;
		Subgraph_Resist.DrawZeros = false;

		Subgraph_ResistMajor.Name = "Major Resistance";
		Subgraph_ResistMajor.PrimaryColor = COLOR_ORANGE;
		Subgraph_ResistMajor.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_ResistMajor.LineStyle = LINESTYLE_SOLID;
		Subgraph_ResistMajor.LineWidth = 1;
		Subgraph_ResistMajor.DrawZeros = false;

		Subgraph_Support.Name = "Support";
		Subgraph_Support.PrimaryColor = COLOR_GREEN;
		Subgraph_Support.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_Support.LineStyle = LINESTYLE_SOLID;
		Subgraph_Support.LineWidth = 1;
		Subgraph_Support.DrawZeros = false;

		Subgraph_SupportMajor.Name = "Major Support";
		Subgraph_SupportMajor.PrimaryColor = COLOR_LIME;
		Subgraph_SupportMajor.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_SupportMajor.LineStyle = LINESTYLE_SOLID;
		Subgraph_SupportMajor.LineWidth = 1;
		Subgraph_SupportMajor.DrawZeros = false;

        Input_Text.Name = "Text to display";
		Input_Text.SetString("Mancini");

		Input_ShowPrice.Name = "Show Price";
		Input_ShowPrice.SetYesNo(1);

        Input_TextSize.Name = "Text Size";
        Input_TextSize.SetInt(7);

        Input_RecalcInterval.Name = "Redraw Interval (seconds)";
        Input_RecalcInterval.SetInt(30);

		Input_RatioScanner.Name = "Ratio Scanner";
		Input_RatioScanner.SetString("");

		Input_Ratio.Name = "Manual Ratio";
		Input_Ratio.SetString("");

		Input_SourceTicker.Name = "Source Symbol (first 2 letters)";
		Input_SourceTicker.SetString("ES");

		Input_Support.Name = "Mancini Support";
		Input_Support.SetString("5515 (major), 5511, 5504 (major), 5453 (major), 5295");

		Input_Resistance.Name = "Mancini Resistance";
		Input_Resistance.SetString("5600 (major), 5571, 5594 (major), 5555 (major), 5795");

		return;
	}
	
	int RecalcIntervalSec = Input_RecalcInterval.GetInt();
	int& LastUpdated = sc.GetPersistentInt(9);
	SCDateTime Now = sc.CurrentSystemDateTime;
	int TimeInSec = Now.GetTimeInSeconds();
	if (LastUpdated > 0 && LastUpdated + RecalcIntervalSec > TimeInSec && sc.Index != 0)
		return;
	LastUpdated = TimeInSec;

	SCString sRatio = Input_Ratio.GetString();
    float Ratio = atof(sRatio.GetChars());
	int maxChart = sc.GetHighestChartNumberUsedInChartBook();
	for (int ChartIndex = 1; ChartIndex <= maxChart; ChartIndex++)
	{
		SCString chtName = sc.GetChartSymbol(ChartIndex);
		if (chtName.CompareNoCase(Input_SourceTicker.GetString(), 2) == 0)
		{
			SCString scan, sL, price;
			int idx = 1;

			sc.GetChartBaseData(ChartIndex, srcChart);
			int iSize = srcChart[SC_LAST].GetArraySize();
			if (iSize != 0)
			{
				sc.GetChartBaseData(ChartIndex, srcChart);
				float srcClose = srcChart[SC_LAST].ElementAt(iSize-1);
				float destClose = sc.BaseDataIn[SC_LAST][sc.Index];
                float fscan = destClose / srcClose;
                scan.Format("%0.5f", fscan);
				Input_RatioScanner.SetString(scan);
				//Message.Format("srcClose: %f, destClose: %f, Ratio: %f", srcClose, destClose, Ratio);
				//sc.AddMessageToLog(Message, 1);

				std::vector<char*> tkSupport;
				SCString sSupport = Input_Support.GetString();
				sSupport.Tokenize(", ", tkSupport);
				for (SCString s: tkSupport)
				{
                    sL = s.Left(4);
					price = sL.GetChars();
					float pr = atof(price.GetChars()) * Ratio;
					Message.Format("Ratio: %f, Before: %s, After: %f", Ratio, price.GetChars(), pr);
					sc.AddMessageToLog(Message, 1);

					s_UseTool Tool;
					Tool.LineStyle = Subgraph_Support.LineStyle;
					Tool.LineNumber = idx;
					Tool.LineWidth = Subgraph_Support.LineWidth;
					Tool.TextAlignment = DT_RIGHT;
					Tool.DrawingType = DRAWING_HORIZONTALLINE;
					Tool.BeginValue = pr;
					Tool.EndValue = pr;
					Tool.ChartNumber = sc.ChartNumber;
					Tool.BeginDateTime = sc.BaseDateTimeIn[0];
					Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize - 1];
					Tool.AddMethod = UTAM_ADD_OR_ADJUST;
                    Tool.FontSize = Input_TextSize.GetInt();
					Tool.ShowPrice = 0;
					if (Input_ShowPrice.GetYesNo() == SC_YES);
						Tool.ShowPrice = 1;
					Tool.TransparencyLevel = 50;
                    Tool.Color = Subgraph_Support.PrimaryColor;
                    Tool.Text.Format(Input_Text.GetString());
                    if (s.IndexOf('(') != -1)
                    {
						Tool.LineStyle = Subgraph_SupportMajor.LineStyle;
						Tool.LineWidth = Subgraph_SupportMajor.LineWidth;
						Tool.Color = Subgraph_SupportMajor.PrimaryColor;
                        SCString se = Input_Text.GetString();
	    		        Tool.Text.Format("%s (MAJOR)", se.GetChars());
                    }
					sc.UseTool(Tool);
					idx++;
				}

				std::vector<char*> tkResist;
				SCString sResist = Input_Resistance.GetString();
				sResist.Tokenize(", ", tkResist);
				for (SCString s : tkResist)
				{
					// 5615 (major), 5611, 5604 (major)
					sL = s.Left(4);
					price = sL.GetChars();
					float pr = atof(price.GetChars()) * Ratio;
					Message.Format("Ratio: %f, Before: %s, After: %f", Ratio, price.GetChars(), pr);
					sc.AddMessageToLog(Message, 1);

					s_UseTool Tool;
					Tool.LineStyle = Subgraph_Resist.LineStyle;
					Tool.LineNumber = idx;
					Tool.LineWidth = Subgraph_Resist.LineWidth;
					Tool.TextAlignment = DT_RIGHT;
					Tool.DrawingType = DRAWING_HORIZONTALLINE;
					Tool.BeginValue = pr;
					Tool.EndValue = pr;
					Tool.ChartNumber = sc.ChartNumber;
					Tool.BeginDateTime = sc.BaseDateTimeIn[0];
					Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize - 1];
					Tool.AddMethod = UTAM_ADD_OR_ADJUST;
					Tool.FontSize = Input_TextSize.GetInt();
					Tool.ShowPrice = 0;
					if (Input_ShowPrice.GetYesNo() == SC_YES);
						Tool.ShowPrice = 1;
					Tool.TransparencyLevel = 50;
					Tool.Color = Subgraph_Resist.PrimaryColor;
					Tool.Text.Format(Input_Text.GetString());
					if (s.IndexOf('(') != -1)
					{
						Tool.LineStyle = Subgraph_ResistMajor.LineStyle;
						Tool.LineWidth = Subgraph_ResistMajor.LineWidth;
						SCString se = Input_Text.GetString();
						Tool.Text.Format("%s (MAJOR)", se.GetChars());
						Tool.Color = Subgraph_ResistMajor.PrimaryColor;
					}
					sc.UseTool(Tool);
					idx++;
				}
			}
		}
	}
}