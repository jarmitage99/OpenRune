class RuneExtraOptionsTop extends RuneMenuOptionsTop;

var RuneButton ExtraButton;
var UWindowWindow ExtraWindow;

var localized string ExtraText;
var localized string ExtraHelp;

function Created()
{
	Super.Created();

	ControlsButton.WinLeft = 10;
	InputButton.WinLeft = 150;

	ExtraButton = RuneButton(CreateControl(class'RuneButton', 0, 0, 130, 30));
	ExtraButton.Text = ExtraText;
	ExtraButton.SetHelpText(ExtraHelp);
	ExtraButton.WinLeft = 290;
	ExtraButton.WinTop = 60;
}

function Notify(UWindowDialogControl C, byte E)
{
	if(E == DE_Click && C == ExtraButton)
	{
		HideAllWindows();
		if(ExtraWindow == None)
			ExtraWindow = Root.CreateWindow(class'RuneExtraOptionsScrollClient', 200, 100, 440, 360);
		if(ExtraWindow != None)
			ExtraWindow.ShowWindow();
		return;
	}
	Super.Notify(C, E);
}

function HideAllWindows()
{
	Super.HideAllWindows();
	if(ExtraWindow != None)
		ExtraWindow.HideWindow();
}

defaultproperties
{
     ExtraText="Extra"
     ExtraHelp="Extra Options"
}
