# wgodot-changes::file
# my own personal script for building wgodot on my own local machine.

param(
	[switch]$Templates,
	[switch]$Game,
	[switch]$Release,
	[switch]$Optimize,
	[string]$BuildProfilePath,
	[string]$PostgreSQLPath,
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

$withPostgreSQL = (!$Game -and !$Templates) -or ($Game -and $nativeBuild.GameTarget -eq 'server')
if ($withPostgreSQL) {
	if (!$PostgreSQLPath) {
		$PostgreSQLPath = Get-ChildItem -LiteralPath "$env:ProgramFiles/PostgreSQL" -Directory -ErrorAction SilentlyContinue |
			Where-Object { $_.Name -match '^\d+(\.\d+)*$' } |
			Sort-Object { [version]($_.Name + '.0') } -Descending |
			Select-Object -First 1 -ExpandProperty FullName
	}
	if (!$PostgreSQLPath -or !(Test-Path -LiteralPath "$PostgreSQLPath/include/libpq-fe.h" -PathType Leaf)) {
		throw 'Editor/server builds require an installed libpq SDK. Supply -PostgreSQLPath with its installation directory.'
	}
	$PostgreSQLPath = (Resolve-Path -LiteralPath $PostgreSQLPath).Path
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
if ($withPostgreSQL) {
	$sconsArgs += @('module_postgresql_enabled=yes', "postgresql_path=$PostgreSQLPath")
} else {
	$sconsArgs += 'module_postgresql_enabled=no'
}

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
		"wgodot_target=$($nativeBuild.GameTarget)",
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
$runtimeLibraries = @{}
if ($withPostgreSQL) {
	# Ship libpq and its installed DLL dependencies; system DLLs stay with Windows.
	$pendingLibraries = @('libpq.dll')
	while ($pendingLibraries.Count) {
		$libraryName = $pendingLibraries[0]
		$pendingLibraries = @($pendingLibraries | Select-Object -Skip 1)
		if ($runtimeLibraries.ContainsKey($libraryName)) {
			continue
		}
		$libraryPath = Join-Path $PostgreSQLPath "bin/$libraryName"
		$dependencies = & dumpbin /nologo /dependents $libraryPath
		if ($LASTEXITCODE -ne 0) {
			throw "Cannot inspect PostgreSQL runtime dependencies: $libraryPath"
		}
		foreach ($line in $dependencies) {
			if ($line -match '^\s+([\w.-]+\.dll)\s*$') {
				$dependency = $matches[1]
				if (Test-Path -LiteralPath (Join-Path $PostgreSQLPath "bin/$dependency") -PathType Leaf) {
					$pendingLibraries += $dependency
				}
			}
		}
		$libraryHash = (Get-FileHash -LiteralPath $libraryPath -Algorithm SHA256).Hash.ToLowerInvariant()
		$destination = Join-Path (Split-Path -Parent $binaryPath) $libraryName
		# Identical SDK files can already be loaded by an editor in this directory.
		$needsCopy = !(Test-Path -LiteralPath $destination -PathType Leaf)
		if (!$needsCopy) {
			$needsCopy = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $libraryHash
		}
		if ($needsCopy) {
			Copy-Item -LiteralPath $libraryPath -Destination $destination -Force
		}
		$runtimeLibraries[$libraryName] = $libraryHash
	}
}
if ($Game) {
	$buildManifest = @{
		target = $gameManifest.target
		generation = $gameManifest.generation
		binary_sha256 = (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
		runtime_libraries = $runtimeLibraries
	} | ConvertTo-Json
	[IO.File]::WriteAllText("$binaryPath.native.json", $buildManifest, [Text.UTF8Encoding]::new($false))
}
