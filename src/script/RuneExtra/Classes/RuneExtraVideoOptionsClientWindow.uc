class RuneExtraVideoOptionsClientWindow extends RuneMenuVideoOptionsClientWindow;

var UWindowComboControl WindowModeCombo;
var bool bUpdatingWindowMode;
var localized string WindowModeText;
var localized string WindowModeHelp;
var localized string WindowedText;
var localized string BorderlessText;
var localized string FullscreenText;

function Created()
{
	local int CenterWidth, CenterPos;

	ControlOffset += 25;
	Super.Created();

	CenterWidth = (WinWidth/4)*3;
	CenterPos = (WinWidth - CenterWidth)/2;
	WindowModeCombo = UWindowComboControl(CreateControl(class'UWindowComboControl', CenterPos, 20, CenterWidth, 1));
	WindowModeCombo.SetText(WindowModeText);
	WindowModeCombo.SetHelpText(WindowModeHelp);
	WindowModeCombo.SetFont(F_Normal);
	WindowModeCombo.SetEditable(False);
	WindowModeCombo.AddItem(WindowedText, "windowed");
	WindowModeCombo.AddItem(BorderlessText, "borderless");
	WindowModeCombo.AddItem(FullscreenText, "fullscreen");
	LoadWindowMode();
}

function LoadWindowMode()
{
	local string Mode;
	local int Index;

	if(WindowModeCombo == None)
		return;
	Mode = GetPlayerOwner().ConsoleCommand("GetWindowMode");
	Index = WindowModeCombo.FindItemIndex2(Mode, True);
	if(Index < 0)
		Index = 0;
	bUpdatingWindowMode = True;
	WindowModeCombo.SetSelectedIndex(Index);
	bUpdatingWindowMode = False;
}

function LoadAvailableSettings()
{
	local string Resolution;

	Super.LoadAvailableSettings();
	if(GetPlayerOwner().ConsoleCommand("GetWindowMode") == "borderless")
	{
		bInitialized = False;
		Resolution = GetPlayerOwner().ConsoleCommand("GetCurrentRes");
		ResolutionCombo.Clear();
		ResolutionCombo.AddItem(Resolution);
		ResolutionCombo.SetValue(Resolution);
		bInitialized = True;
	}
}

function BeforePaint(Canvas C, float X, float Y)
{
	local int CenterWidth, CenterPos;

	Super.BeforePaint(C, X, Y);
	CenterWidth = (WinWidth/4)*3;
	CenterPos = (WinWidth - CenterWidth)/2;
	WindowModeCombo.SetSize(CenterWidth, 1);
	WindowModeCombo.WinLeft = CenterPos;
	WindowModeCombo.EditBoxWidth = 100;
	if(WindowModeCombo.GetValue2() != GetPlayerOwner().ConsoleCommand("GetWindowMode"))
	{
		LoadWindowMode();
		LoadAvailableSettings();
	}
}

function Notify(UWindowDialogControl C, byte E)
{
	Super.Notify(C, E);
	if(E == DE_Change && C == WindowModeCombo && !bUpdatingWindowMode)
	{
		GetPlayerOwner().ConsoleCommand("SetWindowMode "$WindowModeCombo.GetValue2());
		LoadWindowMode();
		LoadAvailableSettings();
	}
}

defaultproperties
{
     WindowModeText="Window Mode"
     WindowModeHelp="Choose windowed, borderless desktop or fullscreen display"
     WindowedText="Windowed"
     BorderlessText="Borderless"
     FullscreenText="Fullscreen"
}
