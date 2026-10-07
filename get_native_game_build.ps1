# wgodot-changes::file
# Shared native build layout for code generation, compilation and packaging.

param(
	[ValidatePattern('^(?-i:[a-z][a-z0-9_]*)?$')]
	[string]$GameName = '',
	[ValidateSet('client', 'server')]
	[string]$GameTarget = 'client',
	[switch]$Release
)

$GameTarget = $GameTarget.ToLowerInvariant()
$engineTarget = if ($Release) { 'template_release' } else { 'template_debug' }
$gameDirectory = Join-Path $PSScriptRoot 'generated'
$extraSuffix = "$GameTarget.game"
if ($GameName) {
	$gameDirectory = Join-Path $gameDirectory $GameName
	$extraSuffix = "$GameName.$extraSuffix"
}

[pscustomobject]@{
	GameTarget = $GameTarget
	ModuleDirectory = Join-Path $gameDirectory "$GameTarget/main_game"
	ExtraSuffix = $extraSuffix
	EngineTarget = $engineTarget
	TemplatePath = Join-Path $PSScriptRoot "bin/godot.windows.$engineTarget.x86_64.$extraSuffix.exe"
}
