#include "sierrachart.h" 

SCDLLName("Mancini Lines DLL") 

SCSFExport scsf_Mancini_Lines(SCStudyInterfaceRef sc)
{
	SCGraphData srcChart;
	SCGraphData ref;
	SCString Message = "";

	SCInputRef Input_Support = sc.Input[0];
	SCInputRef Input_Resistance = sc.Input[1];
    SCInputRef Input_Text = sc.Input[2];
	SCInputRef Input_TextSize = sc.Input[3];

    SCInputRef Input_RecalcInterval = sc.Input[5];

	SCSubgraphRef Subgraph_Support = sc.Subgraph[0];
	SCSubgraphRef Subgraph_SupportMajor = sc.Subgraph[1];
	SCSubgraphRef Subgraph_Resist = sc.Subgraph[2];
	SCSubgraphRef Subgraph_ResistMajor = sc.Subgraph[3];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Mancini Lines";
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

		Input_Support.Name = "Support";
		Input_Support.SetString("");

		Input_Resistance.Name = "Resistance";
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

        int idx = 1;

		SCString chtName = sc.GetChartSymbol(sc.ChartNumber);
		if (chtName.Left(2) == "ES")
		{
	    	SCString desc, price;
    		SCString sSupport = Input_Support.GetString();
    		std::vector<char*> tokens;
    		sSupport.Tokenize(", ", tokens);
	    	for (SCString s: tokens)
	    	{
                if (s.IndexOf('-') != -1)
                    continue;
	    		price = s.Left(4);
	    		float pr = atof(price.GetChars());
                Message.Format("Token: |%s|, Price: %f", s.GetChars(), pr);
				sc.AddMessageToLog(Message, 1);
	    		s_UseTool Tool;
	    		Tool.LineStyle = Subgraph_Support.LineStyle;
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
	    		Tool.ShowPrice = 0;
                sc.AddMessageToLog("2", 1);
                Tool.Color = Subgraph_Support.PrimaryColor;
				Tool.FontSize = Input_TextSize.GetInt();
                Tool.Text.Format(Input_Text.GetString());
                if (s.IndexOf('(') != -1)
                {
					SCString se = Input_Text.GetString();
	    		    Tool.Text.Format("%s (MAJOR)", se.GetChars());
    			    Tool.Color = Subgraph_SupportMajor.PrimaryColor;
					Tool.LineStyle = Subgraph_SupportMajor.LineStyle;
                    Tool.LineWidth = 2;
                    sc.AddMessageToLog("3", 1);
                }
    			sc.UseTool(Tool);
                sc.AddMessageToLog("5", 1);
                idx++;
    		}

            SCString sResist = Input_Resistance.GetString();
    		std::vector<char*> token;
    		sResist.Tokenize(", ", token);
	    	for (SCString s: token)
	    	{
                if (s.IndexOf('-') != -1)
                    continue;
                sc.AddMessageToLog("6", 1);
	    		price = s.Left(4);
	    		float pr = atof(price.GetChars());
                Message.Format("Token: |%s|, Price: %f", s.GetChars(), pr);
				sc.AddMessageToLog(Message, 1);
	    		s_UseTool Tool;
	    		Tool.LineStyle = Subgraph_Resist.LineStyle;
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
	    		Tool.ShowPrice = 0;
                sc.AddMessageToLog("7", 1);
                Tool.Color = Subgraph_Resist.PrimaryColor;
				Tool.FontSize = Input_TextSize.GetInt();
                Tool.Text.Format(Input_Text.GetString());
                if (s.IndexOf('(') != -1)
                {
					Tool.Color = Subgraph_ResistMajor.PrimaryColor;
					SCString se = Input_Text.GetString();
					Tool.LineStyle = Subgraph_ResistMajor.LineStyle;
	    		    Tool.Text.Format("%s (MAJOR)", se.GetChars());
                    Tool.LineWidth = 2;
                }
    			sc.UseTool(Tool);
                sc.AddMessageToLog("8", 1);
                idx++;
    		}
	    }
        else
        {
	    	sc.AddMessageToLog("You're NOT on an ES chart - please change to ES", 1);
        }
}
