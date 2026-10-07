# wgodot-changes::file
# my own personal script for building wgodot on my own local machine.

param(
	[switch]$Templates,
	[switch]$Game,
	[switch]$Release,
	[switch]$Optimize,
	[string]$BuildProfilePath,
	[ValidatePattern('^([a-z][a-z0-9_]*)?$')]
	[string]$GameName = '',
	[ValidateSet('client', 'server')]
	[string]$GameTarget = 'client'
)

$ErrorActionPreference = "Stop"

if ($Game -and $Templates) {
	throw "Use either -Game or -Templates."
}
if ($Release -and !($Game -or $Templates)) {
	throw "-Release requires -Game or -Templates."
}
if ($BuildProfilePath -and !$Game) {
	throw "-BuildProfilePath requires -Game."
}
if ($GameName -and !$Game) {
	throw "-GameName requires -Game."
}
if ($PSBoundParameters.ContainsKey('GameTarget') -and !$Game) {
	throw "-GameTarget requires -Game."
}
if ($BuildProfilePath) {
	$BuildProfilePath = (Resolve-Path -LiteralPath $BuildProfilePath).Path
}

$target = "editor"
$debugSymbols = "yes"
if ($Templates) {
	$target = if ($Release) { "template_release" } else { "template_debug" }
	$debugSymbols = "no"
}
if ($Game) {
	$nativeBuild = & "$PSScriptRoot/get_native_game_build.ps1" -GameName $GameName -GameTarget $GameTarget -Release:$Release
	$target = $nativeBuild.EngineTarget
	if ($Release) {
		$debugSymbols = "no"
	}
}

$sconsArgs = @(
	"platform=windows",
	"arch=x86_64",
	"target=$target",
	"debug_symbols=$debugSymbols",
	"windows_subsystem=console",
	"accesskit=no",
	"angle=no",
	"d3d12=no",
	"winrt=no"
)

if (!$Optimize) {
	$sconsArgs += @("optimize=none", "lto=none")
}

$binaryPath = Join-Path $PSScriptRoot "bin/godot.windows.$target.x86_64.exe"
if ($Game) {
	$gameModulePath = $nativeBuild.ModuleDirectory
	$binaryPath = $nativeBuild.TemplatePath
	foreach ($moduleFile in @("SCsub", "config.py", "register_types.h", "main_game.json")) {
		if (!(Test-Path -LiteralPath (Join-Path $gameModulePath $moduleFile) -PathType Leaf)) {
			throw "Generated native game module is missing '$moduleFile': $gameModulePath. The Plan Z C++ exporter must generate this module before -Game can build it."
		}
	}
	$gameManifest = Get-Content -LiteralPath (Join-Path $gameModulePath 'main_game.json') -Raw | ConvertFrom-Json
	if ($gameManifest.format -ne 1 -or !$gameManifest.generation -or !$gameManifest.files) {
		throw "Incomplete or unsupported native game manifest: $gameModulePath/main_game.json. Run the C++ exporter again."
	}
	if ($gameManifest.target -cne $nativeBuild.GameTarget) {
		throw "Native module target '$($gameManifest.target)' does not match requested target '$($nativeBuild.GameTarget)'. Regenerate this module with --target $($nativeBuild.GameTarget): $gameModulePath"
	}

	$sconsArgs += @(
		"custom_modules=$gameModulePath",
		"custom_modules_recursive=no",
		"module_main_game_enabled=yes",
		"module_gdscript_enabled=no",
		# Godot applies this to objects and libraries too, preserving both targets.
		"extra_suffix=$($nativeBuild.ExtraSuffix)"
	)
	if ($BuildProfilePath) {
		$sconsArgs += "build_profile=$BuildProfilePath"
	}
}
if ($Release) {
	$sconsArgs += "production=yes"
}

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
		target = $gameManifest.target
		generation = $gameManifest.generation
		binary_sha256 = (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
	} | ConvertTo-Json
	[IO.File]::WriteAllText("$binaryPath.native.json", $buildManifest, [Text.UTF8Encoding]::new($false))
}
