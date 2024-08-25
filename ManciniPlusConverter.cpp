#include "sierrachart.h" 

SCDLLName("Mancini Plus DLL") 

SCSFExport scsf_ManciniPlus(SCStudyInterfaceRef sc)
{
	SCGraphData srcChart;
	SCGraphData ref;
	SCString Message = "";

	SCInputRef Input_Support = sc.Input[1];
	SCInputRef Input_Resistance = sc.Input[2];
	SCInputRef Input_RatioScanner = sc.Input[3];
    SCInputRef Input_Text = sc.Input[4];
	SCInputRef Input_TextSize = sc.Input[5];
	SCInputRef Input_Ratio = sc.Input[6];
    SCInputRef Input_RecalcInterval = sc.Input[7];
    SCInputRef Input_SourceTicker = sc.Input[8];

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
		Subgraph_Resist.DrawZeros = false;

		Subgraph_ResistMajor.Name = "Major Resistance";
		Subgraph_ResistMajor.PrimaryColor = COLOR_ORANGE;
		Subgraph_ResistMajor.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_ResistMajor.LineStyle = LINESTYLE_SOLID;
		Subgraph_ResistMajor.DrawZeros = false;

		Subgraph_Support.Name = "Support";
		Subgraph_Support.PrimaryColor = COLOR_GREEN;
		Subgraph_Support.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_Support.LineStyle = LINESTYLE_SOLID;
		Subgraph_Support.DrawZeros = false;

		Subgraph_SupportMajor.Name = "Major Support";
		Subgraph_SupportMajor.PrimaryColor = COLOR_LIME;
		Subgraph_SupportMajor.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_SupportMajor.LineStyle = LINESTYLE_SOLID;
		Subgraph_SupportMajor.DrawZeros = false;

        Input_Text.Name = "Text to display";
		Input_Text.SetString("Mancini");

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
		Input_Support.SetString("5615 (major), 5611, 5604 (major), 5593 (major), 5585, 5572, 5566, 5560, 5551, 5546 (major), 5534, 5528, 5519 (major), 5511, 5502, 5487-90 (major), 5483, 5474-76 (major), 5462, 5457, 5450, 5443-46 (major), 5436 (major), 5429, 5423, 5414 (major), 5409, 5400-05 (major), 5390, 5379, 5372 (major)");

		Input_Resistance.Name = "Mancini Resistance";
		Input_Resistance.SetString("");

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
			sc.GetChartBaseData(ChartIndex, srcChart);
			int iSize = srcChart[SC_LAST].GetArraySize();
			if (iSize != 0)
			{
				sc.GetChartBaseData(ChartIndex, srcChart);
				float srcClose = srcChart[SC_LAST].ElementAt(iSize-1);
				float destClose = sc.BaseDataIn[SC_LAST][sc.Index];
                SCString scan;
                float fscan = destClose / srcClose;
                scan.Format("%0.4f", fscan);
				Input_RatioScanner.SetString(scan);
				//Message.Format("srcClose: %f, destClose: %f, Ratio: %f", srcClose, destClose, Ratio);
				//sc.AddMessageToLog(Message, 1);

				int idx = 1;
				std::vector<char*> tokens;
				SCString sSupport = Input_Support.GetString();
				sSupport.Tokenize(", ", tokens);
				SCString sL, price;
				for (SCString s: tokens)
				{
                    // 5615 (major), 5611, 5604 (major), 5593 (major), 5585, 5572, 5566, 5560, 5551, 5546 (major), 5534, 5528, 5519 (major), 5511, 5502, 5487-90 (major), 5483, 5474-76 (major), 5462, 5457, 5450, 5443-46 (major), 5436 (major), 5429, 5423, 5414 (major), 5409, 5400-05 (major), 5390, 5379, 5372 (major)
                    sL = s.Left(4);
					price = sL.GetChars();
					float pr = atof(price.GetChars()) * Ratio;
					Message.Format("Ratio: %f, Before: %s, After: %f", Ratio, price.GetChars(), pr);
					sc.AddMessageToLog(Message, 1);

					s_UseTool Tool;
					Tool.LineStyle = LINESTYLE_SOLID;
					Tool.LineNumber = idx;
					Tool.LineWidth = 1;
					Tool.TextAlignment = DT_RIGHT;
					Tool.DrawingType = DRAWING_HORIZONTALLINE;
					Tool.BeginValue = pr;
					Tool.EndValue = pr;
					Tool.ChartNumber = sc.ChartNumber;
					Tool.BeginDateTime = sc.BaseDateTimeIn[0];
					Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize - 1];
					Tool.AddMethod = UTAM_ADD_OR_ADJUST;
                    Tool.FontSize = Input_TextSize.GetInt();
					Tool.ShowPrice = 1;
					Tool.TransparencyLevel = 50;
                    Tool.Color = Subgraph_Support.PrimaryColor;
                    Tool.Text.Format(Input_Text.GetString());
                    if (s.IndexOf('(') != -1)
                    {
                        SCString se = Input_Text.GetString();
	    		        Tool.Text.Format("%s (MAJOR)", se.GetChars());
                        Tool.Color = Subgraph_SupportMajor.PrimaryColor;
                    }
					sc.UseTool(Tool);
					idx++;
				}
			}
		}
	}
}