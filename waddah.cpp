#include "sierrachart.h" 

SCDLLName("Waddah Explosion DLL") 

SCSFExport scsf_WaddahExplosion(SCStudyInterfaceRef sc)
{
    if (sc.SetDefaults)
    {
        sc.GraphName = "Waddah Explosion";

        sc.Subgraph[0].Name = "Ignore";
		sc.Subgraph[0].DrawStyle = DRAWSTYLE_IGNORE;

        sc.Subgraph[1].Name = "Ignore";
		sc.Subgraph[1].DrawStyle = DRAWSTYLE_IGNORE;

        sc.Subgraph[5].Name = "Ignore";
		sc.Subgraph[5].DrawStyle = DRAWSTYLE_IGNORE;
		
        sc.Subgraph[2].Name = "Waddah Positive";
        sc.Subgraph[2].DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		sc.Subgraph[2].LineWidth = 12;
		sc.Subgraph[2].PrimaryColor = COLOR_GREEN;

        sc.Subgraph[3].Name = "Waddah Negative";
        sc.Subgraph[3].DrawStyle = DRAWSTYLE_BAR_BOTTOM;
		sc.Subgraph[3].LineWidth = 12;
		sc.Subgraph[3].PrimaryColor = COLOR_RED;

        sc.Subgraph[4].Name = "Explosion Line";
        sc.Subgraph[4].DrawStyle = DRAWSTYLE_LINE;
		sc.Subgraph[4].LineWidth = 2;
		sc.Subgraph[4].PrimaryColor = COLOR_WHITE;
		sc.Subgraph[4].SecondaryColor = COLOR_RED;

        sc.AutoLoop = 1;

        return;
    }

	sc.BollingerBands(sc.BaseData[SC_LAST], sc.Subgraph[5], 20, 2, MOVAVGTYPE_SIMPLE);
	float UpperBand = sc.Subgraph[5].Arrays[0][sc.Index];
	float LowerBand = sc.Subgraph[5].Arrays[1][sc.Index];
	float e1 = UpperBand - LowerBand;
	sc.Subgraph[4][sc.Index] = e1;

	sc.MovingAverage(sc.BaseDataIn[SC_LAST], sc.Subgraph[0], MOVAVGTYPE_EXPONENTIAL, 20);
	float a1 = sc.Subgraph[0][sc.Index];
	float b1 = sc.Subgraph[0][sc.Index - 1];
	
	sc.MovingAverage(sc.BaseDataIn[SC_LAST], sc.Subgraph[0], MOVAVGTYPE_EXPONENTIAL, 40);
	float a2 = sc.Subgraph[1][sc.Index];
	float b2 = sc.Subgraph[1][sc.Index - 1];

	float t1 = ((a1 - a2) - (b1 - b2)) * 150;
	
	if (t1 > 0)
		sc.Subgraph[2][sc.Index] = t1;
	else
		sc.Subgraph[3][sc.Index] = t1 * -1;
	
    return;
}
