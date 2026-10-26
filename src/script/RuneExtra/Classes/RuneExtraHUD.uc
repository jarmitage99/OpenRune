class RuneExtraHUD extends RuneHUD;

var float BloodBarTop;

simulated function DrawStrength(Canvas Canvas, int X, int Y)
{
	local Texture TexFull;

	if(Owner.IsA('SarkRagnar'))
		TexFull = Texture'SarkBloodFull';
	else
		TexFull = Texture'BloodFull';
	BloodBarTop = Y - TexFull.VSize * BloodScale * HudScale;
	Super.DrawStrength(Canvas, X, Y);
}

simulated function DrawMessages(canvas Canvas)
{
	local int i;
	local float XL, YL, ClipY, Lowest, Limit;

	ClipY = Canvas.ClipY;
	if(BloodBarTop > 0)
	{
		if(MyFonts != None)
			Canvas.Font = MyFonts.GetStaticBigFont();
		else
			Canvas.Font = Canvas.BigFont;
		for(i=0; i<QueueSize; i++)
		{
			if(MessageQueue[i].Message != None && MessageQueue[i].Message.Default.bFromBottom)
			{
				Canvas.StrLen(MessageQueue[i].StringMessage, XL, YL);
				Lowest = FMax(Lowest, MessageQueue[i].Message.Static.GetOffset(MessageQueue[i].Switch, YL, ClipY) + YL);
			}
		}
		Limit = BloodBarTop - 4 * HudScale;
		if(Lowest > Limit)
			Canvas.ClipY = ClipY - (Lowest - Limit);
		BloodBarTop = 0;
	}
	Super.DrawMessages(Canvas);
	Canvas.ClipY = ClipY;
}
