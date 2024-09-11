#include "sierrachart.h"

SCDLLName("Linear Regressive Slope with Color")

SCSFExport scsf_LinearRegSlopeWithColor(SCStudyInterfaceRef sc)
{
    SCInputRef Length = sc.Input[0];
    SCInputRef OuterEdge = sc.Input[1];
    SCInputRef SlopeLineColorAboveThreshold = sc.Input[2];
    SCInputRef SlopeLineColorBelowThreshold = sc.Input[3];
    SCInputRef SlopeLineColorNormal = sc.Input[4];

    SCFloatArrayRef LinearRegSlope = sc.Subgraph[0].Data;
    SCSubgraphRef SlopeLine = sc.Subgraph[0];
    SCSubgraphRef LineZero = sc.Subgraph[1];
    SCSubgraphRef BackColor = sc.Subgraph[2];
    SCSubgraphRef Histogram = sc.Subgraph[3];
    SCSubgraphRef Smoothed = sc.Subgraph[4];
    SCSubgraphRef Smoothed2 = sc.Subgraph[5];

    if (sc.SetDefaults)
    {
        sc.GraphName = "DaveC's LRS";
        sc.StudyDescription = "DaveC's LRS";
        
        Length.Name = "Length";
        Length.SetInt(20);
        Length.SetIntLimits(1, MAX_STUDY_LENGTH);

        SlopeLineColorNormal.Name = "Normal Color";
        SlopeLineColorNormal.SetColor(RGB(100, 100, 100)); 

        SlopeLineColorAboveThreshold.Name = "Color Above Threshold";
        SlopeLineColorAboveThreshold.SetColor(RGB(0, 255, 0)); 

        SlopeLineColorBelowThreshold.Name = "Color Below Threshold";
        SlopeLineColorBelowThreshold.SetColor(RGB(255, 0, 0)); 

        OuterEdge.Name = "Outer Extreme";
        OuterEdge.SetFloat(4); 

        sc.AutoLoop = 1;
        sc.GraphRegion = 1;

        return;
    }

    BackColor.Name = "Background on Cross";
    BackColor.DrawStyle = DRAWSTYLE_BACKGROUND;
    BackColor.PrimaryColor = RGB(255, 0, 0);
    BackColor.LineWidth = 5;

    SlopeLine.Name = "Linear Regressive Slope";
    SlopeLine.DrawStyle = DRAWSTYLE_LINE;
    SlopeLine.PrimaryColor = SlopeLineColorNormal.GetColor();
    SlopeLine.LineWidth = 3;

    LineZero.Name = "Zero Line";
    LineZero.DrawStyle = DRAWSTYLE_LINE;
    LineZero.PrimaryColor = RGB(255, 255, 255);
    LineZero.LineWidth = 1;
    LineZero.DrawZeros = true;

    Histogram.Name = "Histogram";
    Histogram.DrawStyle = DRAWSTYLE_BARBOTTOM;
    Histogram.PrimaryColor = RGB(80, 80, 80);
    Histogram.LineWidth = 7;
    Histogram.DrawZeros = true;

    Smoothed.Name = "Smoothed";
    Smoothed.DrawStyle = DRAWSTYLE_IGNORE;
    Smoothed.DrawZeros = true;

    // Linear Regression Slope
    // lrs = (lrc - lrc[1]) / 1
    sc.LinearRegressionSlope(sc.BaseDataIn[SC_LAST], LinearRegSlope, sc.Index, Length.GetInt());

    // Smooth Linear Regression Slope
    // slrs = ta.ema(lrs, slen)
    sc.MovingAverage(LinearRegSlope, Smoothed, MOVAVGTYPE_EXPONENTIAL, 4);

    // Signal Linear Regression Slope
    // alrs = ta.sma(slrs, glen)
    sc.MovingAverage(Smoothed, Histogram, MOVAVGTYPE_SIMPLE, 14);

    // uacce = lrs > alrs and lrs > 0
    if (LinearRegSlope[sc.Index] > Histogram[sc.Index] && LinearRegSlope[sc.Index - 1] > 0)
        Histogram.DataColor[sc.Index] = RGB(0, 112, 30);
    // dacce = lrs < alrs and lrs < 0
    else if (LinearRegSlope[sc.Index] < Histogram[sc.Index] && LinearRegSlope[sc.Index - 1] < 0)
        Histogram.DataColor[sc.Index] = RGB(110, 18, 0);
    else
        Histogram.DataColor[sc.Index] = RGB(88, 88, 88);

    if (LinearRegSlope[sc.Index] > 0 && LinearRegSlope[sc.Index - 1] < 0)
    {
        BackColor[sc.Index] = 1;
        BackColor.DataColor[sc.Index] = SlopeLineColorAboveThreshold.GetColor();
    }
    else if (LinearRegSlope[sc.Index] < 0 && LinearRegSlope[sc.Index - 1] > 0)
    {
        BackColor[sc.Index] = 1;
        BackColor.DataColor[sc.Index] = SlopeLineColorBelowThreshold.GetColor();
    }
    else
        BackColor[sc.Index] = 0;

    if (LinearRegSlope[sc.Index] > OuterEdge.GetFloat())
    {
	SlopeLine.LineWidth = 4;
        SlopeLine.DataColor[sc.Index] = SlopeLineColorAboveThreshold.GetColor();
    }
    else if (LinearRegSlope[sc.Index] < OuterEdge.GetFloat() * -1)
    {
	SlopeLine.LineWidth = 4;
        SlopeLine.DataColor[sc.Index] = SlopeLineColorBelowThreshold.GetColor();
    }
    else
    {
        SlopeLine.DataColor[sc.Index] = SlopeLineColorNormal.GetColor();
    }
}