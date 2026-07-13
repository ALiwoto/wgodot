# my own personal script for building wgodot on my own local machine.

param(
	[AllowNull()]
	[string]$RunTest = $null,

	[switch]$SkipBuild,
	[switch]$Templates
)

$defaultTests = @(
	"deadcode"
)

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64"

$target = "editor"
if ($Templates) {
	$target = "template_debug"
}

$sconsArgs = @(
	"platform=windows",
	"target=$target",
	"debug_symbols=no",
	"windows_subsystem=console",
	"accesskit=no",
	"angle=no",
	"d3d12=no",
	"winrt=no"
)

$shouldRunTests = $PSBoundParameters.ContainsKey("RunTest")

if (!$SkipBuild) {
	& scons @sconsArgs
	if ($LASTEXITCODE -ne 0) {
		exit $LASTEXITCODE
	}
}

if ($shouldRunTests) {
	$testsToRun = if ([string]::IsNullOrWhiteSpace($RunTest)) {
		$defaultTests
	} else {
		@($RunTest)
	}

	$godotExe = Join-Path $PSScriptRoot "bin\godot.windows.editor.x86_64.exe"
	if (!(Test-Path $godotExe)) {
		throw "Could not find test binary: $godotExe"
	}

	$testProjectPath = Join-Path $PSScriptRoot "modules\gdscript\tests\scripts"
	$deadcodeDir = Join-Path $PSScriptRoot "modules\gdscript\wgodot_gd\deadcode\in_class"

	foreach ($testName in $testsToRun) {
		switch ($testName) {
			"deadcode" {
				& $godotExe --headless --path $testProjectPath --script "res://wgodot/validate_deadcode_snippets.notest.gd" -- "--deadcode-dir=$deadcodeDir"
			}
			default {
				throw "Unknown WGodot local test: $testName"
			}
		}
		if ($LASTEXITCODE -ne 0) {
			exit $LASTEXITCODE
		}
	}
}
