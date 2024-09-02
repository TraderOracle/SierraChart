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

    if (sc.SetDefaults)
    {
        sc.GraphName = "Linear Regressive Slope with Color";
        sc.StudyDescription = "Calculates the linear regressive slope and changes the color of the line based on the slope value.";
        
        Length.Name = "Length";
        Length.SetInt(20);
        Length.SetIntLimits(1, MAX_STUDY_LENGTH);

        SlopeLineColorNormal.Name = "Normal Color";
        SlopeLineColorNormal.SetColor(RGB(255, 255, 255)); // White

        SlopeLineColorAboveThreshold.Name = "Color Above Threshold";
        SlopeLineColorAboveThreshold.SetColor(RGB(0, 255, 0)); // Green

        SlopeLineColorBelowThreshold.Name = "Color Below Threshold";
        SlopeLineColorBelowThreshold.SetColor(RGB(255, 0, 0)); // Red

        OuterEdge.Name = "Outer Extreme";
        OuterEdge.SetFloat(1.7); // White

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

    sc.LinearRegressionSlope(sc.BaseDataIn[SC_LAST], LinearRegSlope, sc.Index, Length.GetInt());

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