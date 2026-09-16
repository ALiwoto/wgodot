# wgodot-changes::file
# my own personal script for building wgodot on my own local machine.

param(
	[AllowNull()]
	[string]$RunTest = $null,

	[switch]$SkipBuild,
	[switch]$Templates,
	[switch]$Game,
	[switch]$Release
)

$ErrorActionPreference = "Stop"
$shouldRunTests = $PSBoundParameters.ContainsKey("RunTest")

if ($Game -and $Templates) {
	throw "Use either -Game or -Templates."
}
if ($Release -and !($Game -or $Templates)) {
	throw "-Release requires -Game or -Templates."
}
if ($Game -and $shouldRunTests) {
	throw "-RunTest requires the editor; it cannot be combined with -Game."
}

$defaultTests = @(
	"deadcode"
)

$target = "editor"
if ($Templates -or $Game) {
	$target = if ($Release) { "template_release" } else { "template_debug" }
}

$sconsArgs = @(
	"platform=windows",
	"arch=x86_64",
	"target=$target",
	"debug_symbols=no",
	"windows_subsystem=console",
	"accesskit=no",
	"angle=no",
	"d3d12=no",
	"winrt=no"
)

$binarySuffix = ""
if ($Game) {
	$gameModulePath = Join-Path $PSScriptRoot "generated/main_game"
	foreach ($moduleFile in @("SCsub", "config.py", "register_types.h", "main_game.json")) {
		if (!(Test-Path -LiteralPath (Join-Path $gameModulePath $moduleFile) -PathType Leaf)) {
			throw "Generated native game module is missing '$moduleFile': $gameModulePath. The Plan Z C++ exporter must generate this module before -Game can build it."
		}
	}
	$gameManifest = Get-Content -LiteralPath (Join-Path $gameModulePath 'main_game.json') -Raw | ConvertFrom-Json
	if ($gameManifest.format -ne 1 -or !$gameManifest.generation -or !$gameManifest.files) {
		throw "Incomplete or unsupported native game manifest: $gameModulePath/main_game.json. Run the C++ exporter again."
	}

	$binarySuffix = ".game"
	$sconsArgs += @(
		"custom_modules=$gameModulePath",
		"custom_modules_recursive=no",
		"module_main_game_enabled=yes",
		"module_gdscript_enabled=no",
		"extra_suffix=game"
	)
	if (!$Release) {
		# Keep native game builds easy to step through in the Windows debugger.
		$sconsArgs = $sconsArgs | Where-Object { $_ -ne "debug_symbols=no" }
		$sconsArgs += @("debug_symbols=yes", "optimize=none", "lto=none")
	}
}
if ($Release) {
	$sconsArgs += "production=yes"
}

$binaryPath = Join-Path $PSScriptRoot "bin/godot.windows.$target.x86_64$binarySuffix.exe"

if (!$SkipBuild) {
	$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
	if (!$vs) {
		throw "Visual Studio with the C++ x64 build tools was not found."
	}
	Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
	Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64"

	# Only the executable being rebuilt needs to close; keep other editors/games open.
	$processName = [IO.Path]::GetFileNameWithoutExtension($binaryPath)
	Get-Process -Name $processName -ErrorAction SilentlyContinue |
		Where-Object { $_.Path -eq $binaryPath } |
		Stop-Process -Force

	Push-Location $PSScriptRoot
	try {
		& scons @sconsArgs
		if ($LASTEXITCODE -ne 0) {
			exit $LASTEXITCODE
		}
	}
	finally {
		Pop-Location
	}
	Write-Host "Built: $binaryPath"
	if ($Game) {
		$buildManifest = @{
			generation = $gameManifest.generation
			binary_sha256 = (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
		} | ConvertTo-Json
		[IO.File]::WriteAllText("$binaryPath.native.json", $buildManifest, [Text.UTF8Encoding]::new($false))
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
	$deadcodeDir = Join-Path $PSScriptRoot "modules\gdscript\wgodot_gd\editor\export\deadcode\in_class"

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
