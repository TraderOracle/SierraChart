#include "sierrachart.h"

SCDLLName("Linear Regressive Slope with Color")

SCSFExport scsf_LinearRegSlopeWithColor(SCStudyInterfaceRef sc)
{
    SCInputRef Length = sc.Input[0];
    SCInputRef SlopeLineColorAboveThreshold = sc.Input[1];
    SCInputRef SlopeLineColorBelowThreshold = sc.Input[2];
    SCInputRef SlopeLineColorNormal = sc.Input[3];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Linear Regressive Slope with Color";
        sc.StudyDescription = "Calculates the linear regressive slope and changes the color of the line based on the slope value.";
        
        Length.Name = "Length";
        Length.SetInt(14);
        Length.SetIntLimits(1, MAX_STUDY_LENGTH);

        SlopeLineColorAboveThreshold.Name = "Color Above Threshold";
        SlopeLineColorAboveThreshold.SetColor(RGB(0, 255, 0)); // Green

        SlopeLineColorBelowThreshold.Name = "Color Below Threshold";
        SlopeLineColorBelowThreshold.SetColor(RGB(255, 0, 0)); // Red

        SlopeLineColorNormal.Name = "Normal Color";
        SlopeLineColorNormal.SetColor(RGB(255, 255, 255)); // White

        sc.AutoLoop = 1;
        sc.GraphRegion = 2;

        return;
    }

    // Output for the linear regressive slope
    SCFloatArrayRef LinearRegSlope = sc.Subgraph[0].Data;
    SCSubgraphRef SlopeLine = sc.Subgraph[0];

    SlopeLine.Name = "Linear Regressive Slope";
    SlopeLine.DrawStyle = DRAWSTYLE_LINE;
    SlopeLine.PrimaryColor = SlopeLineColorNormal.GetColor();
    SlopeLine.LineWidth = 4;

    // Calculate the linear regressive slope
    sc.LinearRegressionSlope(sc.BaseDataIn[SC_LAST], LinearRegSlope, sc.Index, Length.GetInt());

    // Change the color based on the slope value
    if (LinearRegSlope[sc.Index] > 4)
    {
	SlopeLine.LineWidth = 4;
        SlopeLine.DataColor[sc.Index] = SlopeLineColorAboveThreshold.GetColor();
    }
    else if (LinearRegSlope[sc.Index] < -4)
    {
	SlopeLine.LineWidth = 4;
        SlopeLine.DataColor[sc.Index] = SlopeLineColorBelowThreshold.GetColor();
    }
    else
    {
        SlopeLine.DataColor[sc.Index] = SlopeLineColorNormal.GetColor();
    }
}