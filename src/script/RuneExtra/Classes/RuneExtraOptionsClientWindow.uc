class RuneExtraOptionsClientWindow extends RuneMenuPageWindow
	config;

var globalconfig bool bLargeText;
var UWindowCheckbox LargeTextCheck;
var localized string LargeTextText;
var localized string LargeTextHelp;
var localized string OpenGLText;
var localized string DisplayText;
var UWindowComboControl GraphicsCombo[5];
var UWindowCheckbox GraphicsCheck[3];
var string ComboKey[5];
var localized string ComboText[5];
var localized string ComboHelp[5];
var localized string ComboChoices[5];
var int ComboDefault[5];
var string CheckKey[3];
var localized string CheckText[3];
var localized string CheckHelp[3];
var bool bLoadingOptions;
var float ControlOffset;

function AddHeading(string Text)
{
	local UWindowLabelControl Label;

	Label = UWindowLabelControl(CreateControl(class'UWindowLabelControl', 0, ControlOffset, 1, 1));
	Label.SetText(Text);
	Label.SetFont(F_Bold);
	ControlOffset += 25;
}

function UWindowComboControl AddGraphicsCombo(int Index)
{
	local UWindowComboControl Combo;
	local string Choices, Text, Value;
	local int P, Count, Selected;

	Combo = UWindowComboControl(CreateControl(class'UWindowComboControl', 0, ControlOffset, 1, 1));
	Combo.SetText(ComboText[Index]);
	Combo.SetHelpText(ComboHelp[Index]);
	Combo.SetFont(F_Normal);
	Combo.SetEditable(False);
	Choices = ComboChoices[Index];
	while(Choices != "")
	{
		P = InStr(Choices, "|");
		if(P < 0)
		{
			Text = Choices;
			Choices = "";
		}
		else
		{
			Text = Left(Choices, P);
			Choices = Mid(Choices, P+1);
		}
		Combo.AddItem(Text, string(Count));
		Count++;
	}
	Value = GetPlayerOwner().ConsoleCommand("get ini:Engine.Engine.ViewportManager "$ComboKey[Index]);
	Selected = ComboDefault[Index];
	if(Value != "")
		Selected = int(Value);
	Combo.SetSelectedIndex(Clamp(Selected, 0, Count-1));
	ControlOffset += 25;
	return Combo;
}

function UWindowCheckbox AddGraphicsCheck(int Index)
{
	local UWindowCheckbox Check;

	Check = UWindowCheckbox(CreateControl(class'UWindowCheckbox', 0, ControlOffset, 1, 1));
	Check.SetText(CheckText[Index]);
	Check.SetHelpText(CheckHelp[Index]);
	Check.SetFont(F_Normal);
	Check.Align = TA_Left;
	Check.bChecked = bool(GetPlayerOwner().ConsoleCommand("get ini:Engine.Engine.ViewportManager "$CheckKey[Index]));
	ControlOffset += 25;
	return Check;
}

function Created()
{
	local int I;

	bLoadingOptions = True;
	Super.Created();
	LargeTextCheck = UWindowCheckbox(CreateControl(class'UWindowCheckbox', 0, ControlOffset, 1, 1));
	LargeTextCheck.bChecked = bLargeText;
	LargeTextCheck.SetText(LargeTextText);
	LargeTextCheck.SetHelpText(LargeTextHelp);
	LargeTextCheck.SetFont(F_Normal);
	LargeTextCheck.Align = TA_Left;
	ControlOffset += 25;
	AddHeading(OpenGLText);
	for(I=0; I<2; I++)
		GraphicsCombo[I] = AddGraphicsCombo(I);
	for(I=0; I<3; I++)
		GraphicsCheck[I] = AddGraphicsCheck(I);
	AddHeading(DisplayText);
	for(I=2; I<5; I++)
		GraphicsCombo[I] = AddGraphicsCombo(I);
	bLoadingOptions = False;
}

function AfterCreate()
{
	Super.AfterCreate();
	DesiredWidth = 220;
	DesiredHeight = ControlOffset;
}

function BeforePaint(Canvas C, float X, float Y)
{
	local int CenterWidth, CenterPos;
	local UWindowWindow Child;
	local UWindowComboControl Combo;

	Super.BeforePaint(C, X, Y);
	CenterWidth = (WinWidth/4)*3;
	CenterPos = (WinWidth - CenterWidth)/2;
	for(Child=FirstChildWindow; Child!=None; Child=Child.NextSiblingWindow)
	{
		Child.WinLeft = CenterPos;
		Combo = UWindowComboControl(Child);
		if(Combo != None)
		{
			Combo.SetSize(CenterWidth, 1);
			Combo.EditBoxWidth = 100;
		}
		else if(UWindowCheckbox(Child) != None)
			Child.SetSize(CenterWidth-100+16, 1);
		else
			Child.SetSize(CenterWidth, 1);
	}
}

function Notify(UWindowDialogControl C, byte E)
{
	local int I;

	Super.Notify(C, E);
	if(E != DE_Change || bLoadingOptions)
		return;
	if(C == LargeTextCheck)
		LargeTextChecked();
	for(I=0; I<5; I++)
	{
		if(C == GraphicsCombo[I])
			GetPlayerOwner().ConsoleCommand("set ini:Engine.Engine.ViewportManager "$ComboKey[I]$" "$GraphicsCombo[I].GetValue2());
	}
	for(I=0; I<3; I++)
	{
		if(C == GraphicsCheck[I])
			GetPlayerOwner().ConsoleCommand("set ini:Engine.Engine.ViewportManager "$CheckKey[I]$" "$GraphicsCheck[I].bChecked);
	}
}

function LargeTextChecked()
{
	bLargeText = LargeTextCheck.bChecked;
	default.bLargeText = bLargeText;
	SaveConfig();
}

defaultproperties
{
     ControlOffset=20.000000
     LargeTextText="Large Text"
     LargeTextHelp="Draw messages and subtitles at double size"
     OpenGLText="OpenGL"
     DisplayText="Display"
     ComboKey(0)="Anisotropy"
     ComboText(0)="Anisotropic Filtering"
     ComboHelp(0)="Sharpens textures viewed at an angle. OpenGL only."
     ComboChoices(0)="Off|2x|4x|8x|16x"
     ComboKey(1)="Multisampling"
     ComboText(1)="Multisample AA"
     ComboHelp(1)="Smooths geometry edges. Higher settings cost more performance. OpenGL only."
     ComboChoices(1)="Off|2x|4x|8x"
     ComboKey(2)="DetailDistance"
     ComboText(2)="Detail Distance"
     ComboHelp(2)="How far detail textures remain visible."
     ComboChoices(2)="Normal|Far|Very Far"
     ComboKey(3)="VSync"
     ComboText(3)="Vertical Sync"
     ComboHelp(3)="Waits for display refresh. Adaptive skips the wait when a frame is late. Desktop only."
     ComboChoices(3)="Off|On|Adaptive"
     ComboDefault(3)=1
     ComboKey(4)="FrameRateLimit"
     ComboText(4)="Frame Rate Limit"
     ComboHelp(4)="Caps the frame rate to save power. Desktop only."
     ComboChoices(4)="Off|30|60|75|120|144|165|240"
     CheckKey(0)="UseTrilinear"
     CheckText(0)="Trilinear Filtering"
     CheckHelp(0)="Blends between texture detail levels. OpenGL only."
     CheckKey(1)="FXAA"
     CheckText(1)="FXAA"
     CheckHelp(1)="Smooths edges, including masked textures such as fences. OpenGL only."
     CheckKey(2)="SmoothLightmaps"
     CheckText(2)="Smooth Lighting"
     CheckHelp(2)="Softens blocky lightmap edges. OpenGL only."
}
