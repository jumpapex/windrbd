; Script generated by the Inno Setup Script Wizard.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING INNO SETUP SCRIPT FILES!
; Use iscc to compile this (in your innosetup program directory)

; Autogenerated by versioninfo.sh: contains MyAppVersion
#include "version.iss"
; Autogenerated by versioninfo.sh: contains MyResourceVersion
#include "resource-version.iss"

; Windows-style path to the root of the source distribution
; You probably have to change this.
; Update: those are now in Makefile.win in the BUILD_ENVironments
; #define WindrbdSource "X:\windrbd"
; Where the WinDRBD utils (drbdadm, drbdsetup, drbdmeta and windrbd EXEs)
; can be found. Note: the utils are not built by this makefile, you
; have to build them seperately.
; #define WindrbdUtilsSource "X:\drbd-utils-windows"

#define MyAppName "WinDRBD"
#define MyAppPublisher "Linbit"
#define MyAppURL "http://www.linbit.com/"
#define MyAppURLDocumentation "https://www.linbit.com/user-guides/"
#define DriverPath "C:\Windows\system32\drivers\windrbd.sys"

[Setup]
; NOTE: The value of AppId uniquely identifies this application.
; Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppId={{EB75FCBA-83D5-4DBF-9047-30F2B6C72DC9}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppCopyright=GPL
VersionInfoVersion={#MyResourceVersion}
;AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
ChangesEnvironment=yes
DefaultDirName={pf}\{#MyAppName}
DisableDirPage=auto
DefaultGroupName={#MyAppName}
LicenseFile={#WindrbdSource}\LICENSE.txt
InfoBeforeFile={#WindrbdSource}\inno-setup\about-windrbd.txt
OutputDir={#WindrbdSource}\inno-setup
OutputBaseFilename=install-windrbd-{#MyAppVersion}
PrivilegesRequired=admin
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
SetupIconFile=windrbd.ico
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "catalan"; MessagesFile: "compiler:Languages\Catalan.isl"
Name: "corsican"; MessagesFile: "compiler:Languages\Corsican.isl"
Name: "czech"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "danish"; MessagesFile: "compiler:Languages\Danish.isl"
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "finnish"; MessagesFile: "compiler:Languages\Finnish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "greek"; MessagesFile: "compiler:Languages\Greek.isl"
Name: "hebrew"; MessagesFile: "compiler:Languages\Hebrew.isl"
Name: "hungarian"; MessagesFile: "compiler:Languages\Hungarian.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "norwegian"; MessagesFile: "compiler:Languages\Norwegian.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "portuguese"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "scottishgaelic"; MessagesFile: "compiler:Languages\ScottishGaelic.isl"
Name: "serbiancyrillic"; MessagesFile: "compiler:Languages\SerbianCyrillic.isl"
Name: "serbianlatin"; MessagesFile: "compiler:Languages\SerbianLatin.isl"
Name: "slovenian"; MessagesFile: "compiler:Languages\Slovenian.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[Tasks]
; Name: modifypath; Description: &Add application directory to your environmental path;

[Dirs]
Name: "{code:WinDRBDRootDir}\bin"
Name: "{code:WinDRBDRootDir}\usr\sbin"
Name: "{code:WinDRBDRootDir}\var\run\drbd"
Name: "{code:WinDRBDRootDir}\var\lib\drbd"
Name: "{code:WinDRBDRootDir}\var\lock"

[Files]
Source: "{#WindrbdUtilsSource}\user\v9\drbdadm.exe"; DestDir: "{code:WinDRBDRootDir}\usr\sbin"; Flags: ignoreversion
Source: "{#WindrbdUtilsSource}\user\v9\drbdmeta.exe"; DestDir: "{code:WinDRBDRootDir}\usr\sbin"; Flags: ignoreversion
Source: "{#WindrbdUtilsSource}\user\v9\drbdsetup.exe"; DestDir: "{code:WinDRBDRootDir}\usr\sbin"; Flags: ignoreversion
Source: "{#WindrbdUtilsSource}\user\windrbd\windrbd.exe"; DestDir: "{code:WinDRBDRootDir}\usr\sbin"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\sysroot\README-windrbd.txt"; DestDir: "{code:WinDRBDRootDir}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\sysroot\etc\drbd.conf"; DestDir: "{code:WinDRBDRootDir}\etc"; Flags: ignoreversion onlyifdoesntexist
Source: "{#WindrbdSource}\inno-setup\sysroot\etc\drbd.d\global_common.conf"; DestDir: "{code:WinDRBDRootDir}\etc\drbd.d"; Flags: ignoreversion onlyifdoesntexist
Source: "{#WindrbdSource}\inno-setup\sysroot\etc\drbd.d\windrbd-sample.res"; DestDir: "{code:WinDRBDRootDir}\etc\drbd.d"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\install-windrbd.cmd"; DestDir: "{app}"; Flags: ignoreversion deleteafterinstall
Source: "{#WindrbdSource}\inno-setup\uninstall-windrbd.cmd"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygwin1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygrunsrv.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygpath.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygattr-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygbz2-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygiconv-2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygintl-8.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygncursesw-10.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygreadline7.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\bash.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cat.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\chmod.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cp.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\ls.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\mkdir.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\mv.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\sed.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\sync.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\unzip.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\{#WindrbdDriverDirectory}\windrbd.sys"; DestDir: "{app}"; Flags: ignoreversion
; must be in same folder as the sysfile.
Source: "{#WindrbdSource}\{#WindrbdDriverDirectory}\windrbd.inf"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\{#WindrbdDriverDirectory}\windrbd.cat"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\misc\drbd.cgi"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\misc\ipxe-windrbd.pxe"; DestDir: "{app}"; Flags: ignoreversion

; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{group}\{cm:ProgramOnTheWeb,{#MyAppName}}"; Filename: "{#MyAppURL}"
Name: "{group}\View {#MyAppName} Tech Guides"; Filename: "{#MyAppURLDocumentation}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{group}\Open {#MyAppName} configuration folder"; Filename: "{code:WinDRBDRootDir}\etc\drbd.d"
Name: "{group}\Open {#MyAppName} application folder"; Filename: "{app}"

[Run]
; TODO: System directory. Do not hardcode C:\Windows.
Filename: "C:\Windows\sysnative\cmd.exe"; Parameters: "/c install-windrbd.cmd"; WorkingDir: "{app}"; Flags: runascurrentuser waituntilterminated shellexec runhidden
Filename: "{#MyAppURLDocumentation}"; Description: "Download WinDRBD documentation"; Flags: postinstall shellexec skipifsilent

[UninstallRun]
Filename: "C:\Windows\sysnative\cmd.exe"; Parameters: "/c uninstall-windrbd.cmd"; WorkingDir: "{app}"; Flags: runascurrentuser waituntilterminated shellexec runhidden; RunOnceId: "UninstallWinDRBD"
; Filename: "{code:WinDRBDRootDir}\usr\sbin\windrbd.exe"; Parameters: "remove-bus-device windrbd.inf"; WorkingDir: "{app}"; Flags: runascurrentuser waituntilterminated shellexec runhidden; RunOnceId: "RemoveBusDeviceWinDRBD"

[Registry]

Root: HKLM; Subkey: "System\CurrentControlSet\services\eventlog\system\WinDRBD"; Flags: uninsdeletekey
Root: HKLM; Subkey: "System\CurrentControlSet\services\eventlog\system\WinDRBD"; ValueType: string; ValueName: "EventMessageFile"; ValueData: "{#DriverPath}"

[Code]

var WinDRBDRootDirPage: TInputDirWizardPage;

function WinDRBDRootDir(params: String) : String;
var root: string;
begin
	root := '';
	if not isUninstaller then
	begin
		root := WinDRBDRootDirPage.Values[0];
	end;
	if root = '' then begin
		if not RegQueryStringValue(HKEY_LOCAL_MACHINE, 'System\CurrentControlSet\Services\WinDRBD', 'WinDRBDRootWinPath', root) then
			root := 'C:\WinDRBD';
	end;
	Result := root;
end;

Function DeleteServiceDependency(serviceName: String): Boolean;
var res: Boolean;
begin
	Log('About to delete System\CurrentControlSet\Services\'+serviceName+'\DependOnService registry key ...');

	Res := RegDeleteValue(HKEY_LOCAL_MACHINE, 'System\CurrentControlSet\Services\'+serviceName, 'DependOnService');
        if not Res then
		Log('Could not delete it maybe it was not there ...')
	else
		Log('Deleted. This is a version <= 1.1.2 so stopping logger before driver');

	Result := Res;
end;

{ older WinDRBD (<=1.1.2) have dependency from windrbd to windrbdlog
  and were started only when windrbdlog was started. This dependency
  does not exist now. If left untouched WinDRBD won't start on boot.
  So if StartType is still 3 (DEMAND_START) set it to 1 (AUTO_START)
}

{ TODO: we can remove this ... inf file installation always overwrites
  this setting so we need to set it in the INF file
}

Procedure SetWinDRBDStartTypeTo1;
var res: Boolean;
    val: Cardinal;

begin
	Res := RegQueryDWordValue(HKEY_LOCAL_MACHINE, 'System\CurrentControlSet\Services\WinDRBD', 'Start', val);
	if res and (val = 3) then
	begin
		Log('Upgrading WinDRBD from 1.1.2 or before, setting StartType tto 1 (AUTO_START)');
		Res := RegWriteDWordValue(HKEY_LOCAL_MACHINE, 'System\CurrentControlSet\Services\WinDRBD', 'Start', 3);
		if not res then
			Log('Failed. Strange.');
	end;
end;

{ See https://stackoverflow.com/questions/1136770/how-to-get-an-output-of-an-execed-program-in-inno-setup }
{ Exec with output stored in result. }
{ ResultString will only be altered if True is returned. }

function ExecWithResult(const Filename, Params, WorkingDir, Path: String; const ShowCmd: Integer; const Wait: TExecWait; var ResultCode: Integer; var ResultString: String): Boolean;
var TempFilename: String;
    Command: String;

begin
	TempFilename := ExpandConstant('{tmp}\~execwithresult.txt');

		{ Exec via cmd and redirect output to file. Must use special string-behavior to work. }
                { Also, if given, add Path to the PATH environment variable }
	if Path <> '' then
		Command := Format('"%s" /S /C "SET PATH=%%PATH%%;%s& "%s" %s 1> "%s" 2>&1"', [ ExpandConstant('{cmd}'), Path, Filename, Params, TempFilename])
	else
		Command := Format('"%s" /S /C ""%s" %s 1> "%s" 2>&1"', [ ExpandConstant('{cmd}'), Filename, Params, TempFilename]);

	Result := Exec(ExpandConstant('{cmd}'), Command, WorkingDir, ShowCmd, Wait, ResultCode);
	if not Result then
		Exit;

	LoadStringFromFile(TempFilename, ResultString);  { Cannot fail }
	DeleteFile(TempFilename);

		{ Remove new-line at the end }
	if (Length(ResultString) >= 2) and (ResultString[Length(ResultString) - 1] = #13) and (ResultString[Length(ResultString)] = #10) then
		Delete(ResultString, Length(ResultString) - 1, 2);
		{ Unix linefeeds }
	if (Length(ResultString) >= 1) and (ResultString[Length(ResultString)] = #10) then
		Delete(ResultString, Length(ResultString), 1);
end;

function ExecWithLogging(const Filename, Params, WorkingDir, Path: String; const ShowCmd: Integer; const Wait: TExecWait; var ResultCode: Integer; var CommandOutput: String): Boolean;

begin
	Log(Format('About to Run %s %s (wd is %s, path is %s) ...', [ Filename, Params, WorkingDir, Path ]));
	Result := ExecWithResult(Filename, Params, WorkingDir, Path, ShowCmd, Wait, ResultCode, CommandOutput);
	if Result then
	begin
		Log(Format('The command %s was run, exit value is %d', [ Filename, ResultCode ]));
		Log(Format('Output of command is '+#13#10+'%s', [ CommandOutput ]));
	end
	else
	begin
		Log(Format('Could not run command %s does it exist?', [ Filename ]));
	end;
end;

function cygpath(WindowsPath: String): String;
var
	ExecStdout: string;
	ResultCode: integer;

begin
	if not ExecWithLogging(ExpandConstant('{app}')+'\cygpath', WindowsPath, ExpandConstant('{app}'), '', SW_HIDE, ewWaitUntilTerminated, ResultCode, ExecStdout) then
	begin
		MsgBox('Could not run cygpath', mbInformation, MB_OK);
	end;
	Result := ExecStdout;
end;

function WinDRBDRootDirCygwin(params: String): String;
begin
	result := cygpath(WinDRBDRootDir(params));
end;

const
	ModPathType = 'system';

function ModPathDir(): TArrayOfString;
var root_path: String;
begin
	root_path := WinDRBDRootDir('');
	setArrayLength(Result, 4);
	Result[0] := ExpandConstant('{app}');
	Result[1] := root_path + '\usr\sbin';
	Result[2] := root_path + '\usr\bin';
	Result[3] := root_path + '\bin';
end;

#include "modpath.iss"
#include "oldver.iss"
#include "services.iss"

var LoggerWasStarted: boolean;
var UmHelperWasStarted: boolean;
var LinstorSatelliteWasStarted: boolean;

Procedure StopUserModeServices;
Begin
	Log('about to stop user mode services');

		{ WinDRBD 1.1.2 and before have this unneccessary
		  dependency from windrbd to umhelper/logger. }

	DeleteServiceDependency('windrbdumhelper');
		{ If there is a dependecy we have to stop logger here.
		  Else WinDRBD driver cannot be stopped which eventually
                  will lead to a BSOD. }
	DeleteServiceDependency('windrbdlog');
	if GetVersionCode <= $10102 then
	begin
		LoggerWasStarted := MyStopService('windrbdlog');
		SetWinDRBDStartTypeTo1;
	end;

	UmHelperWasStarted := MyStopService('windrbdumhelper');
	LinstorSatelliteWasStarted := MyStopService('linstor-satellite');
End;

Procedure StopLogger;
Begin
	Log('about to stop windrbdlog service');
	if GetVersionCode > $10102 then
		LoggerWasStarted := MyStopService('windrbdlog');

		{ else logger already stopped in function above }
End;

Procedure StartUserModeServices;
Begin
	Log('about to start user mode services if they were running before installation');
	if LoggerWasStarted then begin
		MyStartService('windrbdlog');
	End;
	if UmHelperWasStarted then begin
		MyStartService('windrbdumhelper');
	End;
	if LinstorSatelliteWasStarted then begin
		MyStartService('linstor-satellite');
	End;
End;

Procedure InstallUserModeServices;
var ResultCode: Integer;
    CommandOutput: String;
Begin
	if not ExecWithLogging(ExpandConstant('{app}')+'\cygrunsrv', '-I windrbdlog -p '+ExpandConstant('{code:WinDRBDRootDirCygwin}')+'/usr/sbin/windrbd.exe -a log-server -1 '+ExpandConstant('{code:WinDRBDRootDirCygwin}')+'/windrbd-kernel.log -2 '+ExpandConstant('{code:WinDRBDRootDirCygwin}')+'/windrbd-kernel.log -t auto', ExpandConstant('{app}'), '', SW_HIDE, ewWaitUntilTerminated, ResultCode, CommandOutput) then
		MsgBox('Could not install WinDRBD log service', mbInformation, MB_OK);

	if not ExecWithLogging(ExpandConstant('{app}')+'\cygrunsrv', '-I windrbdumhelper -p '+ExpandConstant('{code:WinDRBDRootDirCygwin}')+'/usr/sbin/windrbd.exe -a user-mode-helper-daemon -1 '+ExpandConstant('{code:WinDRBDRootDirCygwin}')+'/windrbd-umhelper.log -2 '+ExpandConstant('{code:WinDRBDRootDirCygwin}')+'/windrbd-umhelper.log -t auto', ExpandConstant('{app}'), '', SW_HIDE, ewWaitUntilTerminated, ResultCode, CommandOutput) then
		MsgBox('Could not install WinDRBD user mode helper service', mbInformation, MB_OK);
end;

Procedure QuoteImagePath(reg_path: string);
var the_path: string;

Begin
	if RegQueryStringValue(HKEY_LOCAL_MACHINE, reg_path, 'ImagePath', the_path) then
	Begin
		if the_path[1] <> '"' then
		Begin
			the_path := '"' + the_path + '"';
			if not RegWriteStringValue(HKEY_LOCAL_MACHINE, reg_path, 'ImagePath', the_path) then
			Begin
				MsgBox('Could not write ImagePath value back in registry path '+reg_path, mbInformation, MB_OK);
			End;
		End;
	End
	Else
	Begin
		MsgBox('Could not find ImagePath value in registry path '+reg_path, mbInformation, MB_OK);
	End;
End;

Procedure PatchRegistry;
Begin
	QuoteImagePath('System\CurrentControlSet\Services\windrbdumhelper');
	QuoteImagePath('System\CurrentControlSet\Services\windrbdlog');
End;

procedure RemoveWinDRBDRootPath;
begin
	RegDeleteValue(HKEY_LOCAL_MACHINE, 'System\CurrentControlSet\Services\WinDRBD', 'WinDRBDRoot');
	RegDeleteValue(HKEY_LOCAL_MACHINE, 'System\CurrentControlSet\Services\WinDRBD', 'WinDRBDRootWinPath');
end;

function DriverIsRunning: Boolean;
var S: Longword;

Begin
	Result := False;
	if ServiceExists('windrbd') then begin
		S:= SimpleQueryService('windrbd');
		if S = SERVICE_RUNNING then begin
			Result := True;
		end;
	end;
end;

procedure stopDriver;
var ResultCode: Integer;
    CommandOutput: String;

begin

	if not DriverIsRunning then begin
		Log('Driver is not running not attempting to stop it.');
		driverWasUnloaded := true;	{ TODO: better name }
		exit;
	end;

	if not ExecWithLogging(ExpandConstant('{code:WinDRBDRootDir}\usr\sbin\drbdadm.exe'), 'disconnect all --force', ExpandConstant('{app}'), ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode, CommandOutput) then
	begin
		MsgBox('Could not disconnect DRBD resources', mbInformation, MB_OK);
	end;

	if not ExecWithLogging(ExpandConstant('{code:WinDRBDRootDir}\usr\sbin\drbdadm.exe'), 'down all', ExpandConstant('{app}'), ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode, CommandOutput) then
	begin
		MsgBox('Could not bring DRBD resources down', mbInformation, MB_OK);
	end;

	if not ExecWithLogging(ExpandConstant('{code:WinDRBDRootDir}\usr\sbin\windrbd.exe'), 'remove-bus-device windrbd.inf', ExpandConstant('{app}'), ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode, CommandOutput) then
	begin
		MsgBox('Could not remove bus device', mbInformation, MB_OK);
	end;

	if not ExecWithLogging(ExpandConstant('{code:WinDRBDRootDir}\usr\sbin\windrbd.exe'), 'set-shutdown-flag 1', ExpandConstant('{app}'), ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode, CommandOutput) then
	begin
		Log('Failed to set shutdown flag. The old WinDRBD version is probably less than 1.1.0, please make sure that drbdsetup events2 processes are stopped by yourself');
	end;

	if not ExecWithLogging(ExpandConstant('sc.exe'), 'stop windrbd', ExpandConstant('{app}'), '', SW_HIDE, ewWaitUntilTerminated, ResultCode, CommandOutput) then
	begin
		MsgBox('Could not stop driver', mbInformation, MB_OK);
	end;

	driverWasUnloaded := ResultCode = 0;
end;

procedure AddDriverToDriverStore;
var ResultCode: Integer;
    CommandOutput: String;

begin
	if not ExecWithLogging(ExpandConstant('pnputil.exe'), '-i -a windrbd.inf', ExpandConstant('{app}'), '', SW_HIDE, ewWaitUntilTerminated, ResultCode, CommandOutput) then
	begin
		MsgBox('Could not run pnputil', mbInformation, MB_OK);
	end;
end;

procedure InstallBusDevice;
var ResultCode: Integer;
    CommandOutput: String;

begin
	if not ExecWithLogging(ExpandConstant('{code:WinDRBDRootDir}\usr\sbin\windrbd.exe'), 'install-bus-device windrbd.inf', ExpandConstant('{app}'), ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode, CommandOutput) then
	begin
		MsgBox('Could not install bus device', mbInformation, MB_OK);
	end;

	{ This may be still set if for whatever reason the driver was not
          unloaded. drbdsetup (and drbdadm) will not work as long as this
          flag is set, so unset it here. }

	if not ExecWithLogging(ExpandConstant('{code:WinDRBDRootDir}\usr\sbin\windrbd.exe'), 'set-shutdown-flag 0', ExpandConstant('{app}'), ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode, CommandOutput) then
	begin
		Log('Failed to unset the shutdown flag. Lets see what happens next');
	end;

end;

function UninstallNeedRestart: Boolean;
begin
	Result:= not driverWasUnloaded;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var root: string;

begin
	if CurUninstallStep = usAppMutexCheck then begin
		StopUserModeServices();
		StopDriver();
		StopLogger();
	end;
	// only run during actual uninstall
	if CurUninstallStep = usUninstall then begin
		ModPath();

		root:= WinDRBDRootDir('');
		RemoveWinDRBDRootPath();

		if not UninstallSilent then
		begin
			MsgBox('Uninstall does not remove files in the '+root+' directory that are created by you (for example .res files in /etc/drbd.d). If you do not need them any more, please remove the '+root+' directory manually.', mbInformation, MB_OK);
		end;
	end;
	// cmd script stops user mode helpers, no need to do that here
end;

procedure InitializeWizard;
begin
	WinDRBDRootDirPage := CreateInputDirPage(wpSelectDir, 'Select WinDRBD root directory', '',  '', True, 'WinDRBD');
	WinDRBDRootDirPage.Add('WinDRBD root directory:'+#13#10#13#10+'This is the system root for WinDRBD where the etc, bin, usr, ... directories'+#13#10+'for the userland utilities are located.'+#13#10);
	WinDRBDRootDirPage.Values[0] := WinDRBDRootDir(''); 
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
	if CurPageID = WinDRBDRootDirPage.ID then
	begin
		if Pos(' ', WinDRBDRootDirPage.Values[0]) <> 0 then
		begin
			MsgBox('No whitespace allowed in WinDRBD root path', mbInformation, MB_OK);
			Result := false;
		end
		else
		begin
			Result := true;
		end;
	end
	else
	begin
		Result := true;
	end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
var unused: String;
begin
	if PageID = WinDRBDRootDirPage.ID then
		Result := RegQueryStringValue(HKEY_LOCAL_MACHINE, 'System\CurrentControlSet\Services\WinDRBD', 'WinDRBDRootWinPath', unused)
	else
		Result := false;
end;

procedure WriteWinDRBDRootPath;
var windrbd_root: String;

begin
	windrbd_root := cygpath(WinDRBDRootDir(''));
	if windrbd_root = '' then
	begin
		MsgBox('Could not convert Windows path to cygwin path, Windows path is '+WinDRBDRootDir(''), mbInformation, MB_OK);
	end
	else
	begin
		if not RegWriteStringValue(HKEY_LOCAL_MACHINE, 'System\CurrentControlSet\Services\WinDRBD', 'WinDRBDRoot', windrbd_root) then
		begin
			MsgBox('Could not write cygwin path to registry, cygwin path is '+windrbd_root, mbInformation, MB_OK);
		end;
		if not RegWriteStringValue(HKEY_LOCAL_MACHINE, 'System\CurrentControlSet\Services\WinDRBD', 'WinDRBDRootWinPath', WinDRBDRootDir('')) then
		begin
			MsgBox('Could not write windows path to registry, windows path is '+WinDRBDRootDir(''), mbInformation, MB_OK);
		end;
	end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
	if CurStep = ssInstall then begin
		ModPath();

		if GetOldVersion <> '' then begin
			StopUserModeServices();
			StopDriver();
			StopLogger();

			{ TODO: Also remove all windrbd drivers from driver store,
			  pnputil -d (pnputil /delete-driver) will do that but
			  need to parse pnputil -e (pnputil /enum-drivers) output
			}
		end
		else begin  { Also start user mode service if this is a fresh install }
			LoggerWasStarted := true;
			UmHelperWasStarted := true;
		end;
	end;
	if CurStep = ssPostInstall then begin
		WriteWinDRBDRootPath();
		InstallUserModeServices();
		PatchRegistry();
		AddDriverToDriverStore();
		InstallBusDevice();
		StartUserModeServices();
	end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
	if CurPageID = wpReady then
	begin
		Wizardform.ReadyMemo.Lines.Add(''); { Empty string }
		Wizardform.ReadyMemo.Lines.Add('WinDRBD Sysroot folder');
		Wizardform.ReadyMemo.Lines.Add('      '+WinDRBDRootDir(''));
		Wizardform.ReadyMemo.Lines.Add(''); { Empty string }
	end;
end;
