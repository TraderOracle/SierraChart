#include "sierrachart.h" 

SCDLLName("Mancini Lines DLL") 

SCSFExport scsf_Mancini_Lines(SCStudyInterfaceRef sc)
{
	SCGraphData srcChart;
	SCGraphData ref;
	SCString Message = "";

    // 5615 (major), 5611, 5604 (major), 5593 (major), 5585, 5572, 5566, 5560, 5551, 5546 (major), 5534, 5528, 5519 (major), 5511, 5502, 5487-90 (major), 5483, 5474-76 (major), 5462, 5457, 5450, 5443-46 (major), 5436 (major), 5429, 5423, 5414 (major), 5409, 5400-05 (major), 5390, 5379, 5372 (major).
    // 5615 (major), 5621, 5628 (major), 5636, 5642, 5650-54 (major), 5661, 5667 (major), 5672, 5679 (major), 5686, 5690, 5700-05 (major), 5715 (major), 5721, 5728 (major), 5737, 5753, 5765 (major).

	SCInputRef Input_Support = sc.Input[0];
	SCInputRef Input_Resistance = sc.Input[1];
    SCInputRef Input_RecalcInterval = sc.Input[2];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Mancini Lines";
		sc.GraphRegion = 0;
		sc.AutoLoop = 1;

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
	    		Tool.ShowPrice = 0;
                sc.AddMessageToLog("2", 1);
                Tool.Color = COLOR_LIME;
                Tool.Text.Format("Mancini");
                if (s.IndexOf('(') != -1)
                {
	    		    Tool.Text.Format("Mancini (MAJOR)");
    			    Tool.Color = COLOR_LIME;
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
	    		Tool.ShowPrice = 0;
                sc.AddMessageToLog("7", 1);
                Tool.Color = COLOR_RED;
                Tool.Text.Format("Mancini");
                if (s.IndexOf('(') != -1)
                {
	    		    Tool.Text.Format("Mancini (MAJOR)");
    			    Tool.Color = COLOR_ORANGE;
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
