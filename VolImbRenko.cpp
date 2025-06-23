#include "sierrachart.h"

SCDLLName("VolImb RENKO DLL")

    bool IsGreen(SCBaseDataRef InData, int index)
{
    return InData[SC_LAST][index] > InData[SC_OPEN][index];
}

bool IsRed(SCBaseDataRef InData, int index)
{
    return InData[SC_LAST][index] < InData[SC_OPEN][index];
}

bool IsVolImbGreen(SCStudyInterfaceRef sc, int index)
{
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    if (IsGreen(InData, index) && IsGreen(InData, index - 1) && InData[SC_OPEN][index] > InData[SC_LAST][index - 1])
        ret_flag = true;

    return ret_flag;
}

bool IsVolImbRed(SCStudyInterfaceRef sc, int index)
{
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    if (IsRed(InData, index) && IsRed(InData, index - 1) && InData[SC_OPEN][index] < InData[SC_LAST][index - 1])
        ret_flag = true;

    return ret_flag;
}

void SendDiscordWebhook(SCStudyInterfaceRef sc, const std::string &webhookUrl, const std::string &message)
{
    std::stringstream jsonPayload;
    jsonPayload << "{\"content\":\"" << message << "\"}";

    n_ACSIL::s_HTTPHeader HTTPHeader;
    HTTPHeader.Name = "Content-Type";
    HTTPHeader.Value = "application/json";

    sc.MakeHTTPPOSTRequest(
        webhookUrl.c_str(),
        jsonPayload.str().c_str(),
        &HTTPHeader,
        1 // Number of headers
    );
}

SCSFExport scsf_VolImbRenko(SCStudyInterfaceRef sc)
{
    SCString txt;

    SCInputRef Input_BarColor = sc.Input[11];
    SCInputRef Input_BarColorWaddah = sc.Input[12];
    SCInputRef Input_BarColorLinda = sc.Input[13];

    SCSubgraphRef Subgraph_DotUp = sc.Subgraph[0];
    SCSubgraphRef Subgraph_DotDown = sc.Subgraph[1];
    SCSubgraphRef Subgraph_VolImbUp = sc.Subgraph[2];
    SCSubgraphRef Subgraph_VolImbDown = sc.Subgraph[3];

    SCSubgraphRef Subgraph_ColorBar = sc.Subgraph[17];
    SCSubgraphRef Subgraph_ColorUp = sc.Subgraph[18];
    SCSubgraphRef Subgraph_ColorDown = sc.Subgraph[19];
    SCSubgraphRef Subgraph_Calc = sc.Subgraph[27];
    SCSubgraphRef Subgraph_Intersection = sc.Subgraph[32];

    COLORREF UpColor = Subgraph_ColorUp.PrimaryColor;
    COLORREF DownColor = Subgraph_ColorDown.PrimaryColor;

    if (sc.SetDefaults)
    {
        sc.GraphName = "VolImb RENKO";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;

        Subgraph_ColorBar.Name = "Bar Color";
        Subgraph_ColorBar.DrawStyle = DRAWSTYLE_COLOR_BAR;
        Subgraph_ColorBar.LineWidth = 1;

        Subgraph_ColorUp.Name = "Bar Color Up";
        Subgraph_ColorUp.PrimaryColor = RGB(0, 255, 0);
        Subgraph_ColorUp.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_ColorUp.LineWidth = 1;
        Subgraph_ColorUp.DrawZeros = false;

        Subgraph_ColorDown.Name = "Bar Color Down";
        Subgraph_ColorDown.PrimaryColor = RGB(255, 0, 0);
        Subgraph_ColorDown.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_ColorDown.LineWidth = 1;
        Subgraph_ColorDown.DrawZeros = false;

        Subgraph_VolImbUp.Name = "Volume Imbalance Up";
        Subgraph_VolImbUp.PrimaryColor = RGB(255, 255, 255);
        Subgraph_VolImbUp.DrawStyle = DRAWSTYLE_POINT;
        Subgraph_VolImbUp.LineWidth = 1;
        Subgraph_VolImbUp.DrawZeros = false;

        Subgraph_VolImbDown.Name = "Volume Imbalance Down";
        Subgraph_VolImbDown.PrimaryColor = RGB(255, 255, 255);
        Subgraph_VolImbDown.DrawStyle = DRAWSTYLE_POINT;
        Subgraph_VolImbDown.LineWidth = 1;
        Subgraph_VolImbUp.DrawZeros = false;

        return;
    }

#pragma endregion

    int i = sc.Index;
    int BarCloseStatus = sc.GetBarHasClosedStatus() == BHCS_BAR_HAS_CLOSED;
    SCBaseDataRef in = sc.BaseData;
    double close = sc.Close[i];
    double open = sc.Open[i];
    double high = sc.High[i];
    double low = sc.Low[i];
    double pclose = sc.Close[i - 1];
    double popen = sc.Open[i - 1];
    double phigh = sc.High[i - 1];
    double plow = sc.Low[i - 1];
    double body = abs(open - close);
    double pbody = abs(popen - pclose);
    bool red = open > close;
    bool green = open < close;
    bool pdoji = false;
    double upperwick = 0;
    double lowerwick = 0;

    SCFloatArrayRef Array_Value = Subgraph_Calc.Arrays[0];

    /*
        if (sc.GetBarHasClosedStatus() == BHCS_BAR_HAS_CLOSED)
        {
            const int32_t numLines = sc.GetNumLinesUntilFutureIntersection(sc.ChartNumber, sc.StudyGraphInstanceID) - 1;
            if (numLines != 0)
            {
                for (int32_t lineIndex = numLines; lineIndex >= 0; --lineIndex)
                {
                    int32_t lineID{0};
                    int32_t startIndex{0};
                    int32_t endIndex{0};
                    float lineValue{0.0f};

                    if (sc.GetStudyLineUntilFutureIntersectionByIndex(sc.ChartNumber, sc.StudyGraphInstanceID, lineIndex, lineID, startIndex, lineValue, endIndex) != 0)
                    if (sc.GetStudyLineUntilFutureIntersectionByIndex(sc.ChartNumber
                                                                      ,
                                                                      sc.StudyGraphInstanceID
                                                                      ,
                                                                      lineIndex
                                                                      ,
                                                                      lineID
                                                                      ,
                                                                      startIndex
                                                                      ,
                                                                      lineValue
                                                                      ,
                                                                      endIndex))
                    {
                        if (endIndex - startIndex < 3)
                            sc.DeleteLineUntilFutureIntersection(startIndex, lineID);
                    }
                }
            }
        }

        if (BarCloseStatus)
        {
            int iEndings = 0;
            const int32_t numLines = sc.GetNumLinesUntilFutureIntersection(sc.ChartNumber, sc.StudyGraphInstanceID) - 1;
            if (numLines != 0)
            {
                for (int32_t lineIndex = numLines; lineIndex >= 0; --lineIndex)
                {
                    int32_t lineID{0};
                    int32_t startIndex{0};
                    int32_t endIndex{0};
                    float lineValue{0.0f};

                    if (sc.GetStudyLineUntilFutureIntersectionByIndex(sc.ChartNumber, sc.StudyGraphInstanceID, lineIndex, lineID, startIndex, lineValue, endIndex) != 0)
                    {
                        if (endIndex - startIndex < 3)
                        {
                            SCString txt;
                            txt.Format("endIndex %d, start %d, lineID %d lineValue %d", endIndex, startIndex, lineID, lineValue);
                            sc.AddMessageToLog(txt, 0);

                            iEndings++;
                            // DrawText(sc, Subgraph_3oU, "shit", 0, 5);
                            sc.DeleteLineUntilFutureIntersection(startIndex, lineID);
                            Subgraph_Intersection[i] = endIndex;
                        }
                    }
                }
            }
            Subgraph_Intersection[i] = iEndings;
        }
    */
    Subgraph_VolImbUp[i] = 0;
    Subgraph_VolImbDown[i] = 0;

    if (BarCloseStatus && IsVolImbGreen(sc, sc.CurrentIndex))
    {
        try
        {
            int ic = sc.CurrentIndex; double o = sc.Open[ic];
            double hc = sc.High[ic + 1]; double lc = sc.Low[ic + 1]; double hc2 = sc.High[ic + 2]; 
            double lc2 = sc.Low[ic + 2]; double hc3 = sc.High[ic + 3]; double lc3 = sc.Low[ic + 3];
            double hc2 = sc.High[ic + 2]; double lc2 = sc.Low[ic + 2]; double hc3 = sc.High[ic + 3]; 
            double lc3 = sc.Low[ic + 3]; double hc4 = sc.High[ic + 4]; double lc4 = sc.Low[ic + 4];
            if ((hc > o && lc < o) || (hc2 > o && lc2 < o) || (hc3 > o && lc3 < o) || (hc4 > o && lc4 < o))
                return;
        }
        catch (const std::exception &e)
        {
            sc.AddMessageToLog(e.what(), 0);
        }
        sc.AddLineUntilFutureIntersection(i, i, open, RGB(255, 255, 255), 2, LINESTYLE_SOLID, false, false, "");
        Subgraph_VolImbUp[i] = low - (2 * sc.TickSize);
        txt.Format("Volume Imbalance BUY at %.2d", close);
        // sc.AddMessageToLog(txt, 0);
        if (sc.IsNewBar(i))
            sc.AlertWithMessage(197, "Volume Imbalance BUY");
    }

    if (BarCloseStatus && IsVolImbRed(sc, sc.CurrentIndex))
    {
        try
        {
            int ic = sc.CurrentIndex; double o = sc.Open[ic];
            double hc = sc.High[ic + 1]; double lc = sc.Low[ic + 1]; double hc2 = sc.High[ic + 2]; 
            double lc2 = sc.Low[ic + 2]; double hc3 = sc.High[ic + 3]; double lc3 = sc.Low[ic + 3];
            double hc2 = sc.High[ic + 2]; double lc2 = sc.Low[ic + 2]; double hc3 = sc.High[ic + 3]; 
            double lc3 = sc.Low[ic + 3]; double hc4 = sc.High[ic + 4]; double lc4 = sc.Low[ic + 4];
            if ((hc > o && lc < o) || (hc2 > o && lc2 < o) || (hc3 > o && lc3 < o) || (hc4 > o && lc4 < o))
                return;
        }
        catch (const std::exception &e)
        {
            sc.AddMessageToLog(e.what(), 0);
        }
        sc.AddLineUntilFutureIntersection(i, i, open, RGB(255, 255, 255), 2, LINESTYLE_SOLID, false, false, "");
        Subgraph_VolImbDown[i] = high + (2 * sc.TickSize);
        txt.Format("Volume Imbalance SELL at %.2d", close);
        // sc.AddMessageToLog(txt, 0);
        if (sc.IsNewBar(i))
            sc.AlertWithMessage(198, "Volume Imbalance SELL");
    }
}