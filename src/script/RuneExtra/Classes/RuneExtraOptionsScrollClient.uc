class RuneExtraOptionsScrollClient extends RuneMenuScrollingDialogClient;

function Created()
{
	ClientClass = class'RuneExtraOptionsClientWindow';
	FixedAreaClass = None;
	Super.Created();
}

defaultproperties
{
}
