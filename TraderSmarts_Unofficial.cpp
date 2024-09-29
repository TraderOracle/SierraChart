#include "sierrachart.h" 

SCDLLName("TraderSmarts Unofficial DLL")

SCString ReadTextFile(SCStudyInterfaceRef sc, SCString FileLocation)
{
	char TextBuffer[1000] = {};
	int FileHandle = 1;
	unsigned int* p_BytesRead = new unsigned int(0);
	sc.OpenFile(FileLocation.GetChars(), n_ACSIL::FILE_MODE_OPEN_EXISTING_FOR_SEQUENTIAL_READING, FileHandle);
	sc.ReadFile(FileHandle, TextBuffer, 1000, p_BytesRead);
	sc.CloseFile(FileHandle);
	return TextBuffer;
}

SCSFExport scsf_TraderSmarts(SCStudyInterfaceRef sc)
{
	int i = sc.Index;

	SCInputRef Input_FileName = sc.Input[0];

	if (sc.SetDefaults)
	{
		sc.GraphName = "TraderSmarts Unofficial";
		sc.GraphRegion = 0;
		sc.AutoLoop = 1;

		Input_FileName.Name = "Filename to read";
		Input_FileName.SetPathAndFileName("C:\\temp\\tradersmarts.txt");

		return;
	}

	SCString PathAndFileName = Input_FileName.GetPathAndFileName();
	SCString MyInputString("");
	SCString w("");

	float& fStart = sc.GetPersistentFloat(0);
	float& fEnd = sc.GetPersistentFloat(1);
	SCString& desc = sc.GetPersistentSCString(2);

	if (sc.UpdateStartIndex == 0)
	{
		sc.AddMessageToLog("starting bro", 0);
		if (MyInputString == "")
			MyInputString.Format(ReadTextFile(sc, PathAndFileName));
		std::vector<SCString> sLines;
		MyInputString.ParseLines(sLines);
		int idx = 1;
		for (const SCString& ss : sLines)
		{
			if (strstr(ss, "MTS Numbers:"))
			{
				int i = ss.IndexOf(':');
				SCString yy = ss.Right(ss.GetLength() - i - 2);
				sc.AddMessageToLog(w.Format("MTS = |%s|", yy.GetChars()), 1);

				std::vector<char*> tokens;
				yy.Tokenize(", ", tokens);
				for (SCString s : tokens)
				{
					fStart = std::stof(s.GetChars());
					sc.AddMessageToLog(w.Format("Token = %f", fStart), 1);
					s_UseTool Tool;
					Tool.LineStyle = LINESTYLE_DASHDOTDOT;
					Tool.TextAlignment = DT_RIGHT;
					Tool.DrawingType = DRAWING_HORIZONTALLINE;
					Tool.BeginValue = fStart;
					Tool.EndValue = fStart;
					Tool.ChartNumber = sc.ChartNumber;
					Tool.BeginDateTime = sc.BaseDateTimeIn[0];
					Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize - 1];
					Tool.AddMethod = UTAM_ADD_OR_ADJUST;
					Tool.ShowPrice = 0;
					Tool.Text.Format("MTS");
					Tool.FontSize = 9;
					Tool.LineWidth = 1;
					Tool.LineNumber = idx;
					Tool.Color = COLOR_GAINSBORO;
					Tool.FontBold = true;
					sc.UseTool(Tool);
					idx++;
				}
			}

			//sc.AddMessageToLog(ss, 0);
			if (strstr(ss, "Sand") || strstr(ss, "Long") || strstr(ss, "Short"))
			{
				int id = ss.IndexOf('-');
				if (id > 0)
				{
					int i = ss.IndexOf(' ');
					if (i > 0)
					{
						// 20294.25 - 20283.25 Range Short
						fStart = std::stof(ss.Left(i).GetChars());
						int ix = ss.IndexOf(' ', i + 3);
						SCString yy = ss.GetSubString(ix - i - 3, i + 3);
						desc = ss.GetSubString(ss.GetLength() - ix - 2, ix + 1);
						fEnd = std::stof(yy.GetChars());
						sc.AddMessageToLog(w.Format("Split = %f, %f", fStart, fEnd), 1);

						s_UseTool Tool;
						Tool.LineStyle = LINESTYLE_DASHDOTDOT;
						Tool.LineWidth = 1;
						Tool.TransparencyLevel = 70;
						Tool.TextAlignment = DT_RIGHT;
						Tool.DrawingType = DRAWING_RECTANGLE_EXT_HIGHLIGHT;
						Tool.BeginValue = fStart;
						Tool.EndValue = fEnd;
						Tool.ChartNumber = sc.ChartNumber;
						Tool.BeginDateTime = sc.BaseDateTimeIn[0];
						Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize - 1];
						Tool.AddMethod = UTAM_ADD_OR_ADJUST;
						Tool.ShowPrice = 0;
						Tool.Text.Format("%s", desc.GetChars());
						Tool.FontSize = 9;
						Tool.LineNumber = idx;
						if (strstr(desc, "Sand"))
							Tool.Color = COLOR_GAINSBORO;
						else if (strstr(desc, "Short"))
							Tool.Color = COLOR_RED;
						else if (strstr(desc, "Long"))
							Tool.Color = COLOR_LIME;
						Tool.FontBold = true;
						Tool.SecondaryColor = Tool.Color;
						sc.UseTool(Tool);
					}
				}
				else
				{
					int i = ss.IndexOf(' ');
					if (i > 0)
					{
						fStart = std::stof(ss.Left(i).GetChars());
						desc = ss.GetSubString(ss.GetLength() - i - 2, i + 1);
						sc.AddMessageToLog(w.Format("%s = %f", desc.GetChars(), fStart), 1);

						s_UseTool Tool;
						Tool.LineStyle = LINESTYLE_DASHDOTDOT;
						Tool.TextAlignment = DT_RIGHT;
						Tool.DrawingType = DRAWING_HORIZONTALLINE;
						Tool.BeginValue = fStart;
						Tool.EndValue = fStart;
						Tool.ChartNumber = sc.ChartNumber;
						Tool.BeginDateTime = sc.BaseDateTimeIn[0];
						Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize - 1];
						Tool.AddMethod = UTAM_ADD_OR_ADJUST;
						Tool.ShowPrice = 0;
						Tool.Text.Format("%s", desc.GetChars());
						Tool.FontSize = 9;
						Tool.LineWidth = 1;
						Tool.LineNumber = idx;
						if (strstr(desc, "Sand"))
							Tool.Color = COLOR_GAINSBORO;
						else if (strstr(desc, "Short"))
							Tool.Color = COLOR_RED;
						else if (strstr(desc, "Long"))
							Tool.Color = COLOR_LIME;
						Tool.FontBold = true;
						sc.UseTool(Tool);
					}
				}
			}
			idx++;
		}
	}

}
