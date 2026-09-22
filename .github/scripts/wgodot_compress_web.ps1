# wgodot-changes::file
#requires -Version 7.0
param(
    [Parameter(Mandatory)]
    [string]$Directory,
    [Parameter(Mandatory)]
    [string]$BaseName
)

$ErrorActionPreference = 'Stop'
$Directory = (Resolve-Path -LiteralPath $Directory).Path
Add-Type -Path "$PSScriptRoot/wgodot_brotli.cs"
$files = @(Get-ChildItem -LiteralPath $Directory -File | Where-Object {
    $_.Name.StartsWith("$BaseName.") -and $_.Extension -in @('.wasm', '.pck', '.js') -and
    $_.Name -ne "$BaseName.service.worker.js"
})
if (!$files) { throw "No Web assets found for $BaseName." }

Write-Host 'Compressing Web assets with Brotli quality 11, window 24...'
# Each stream stays intact for compression ratio; independent files use separate cores.
$results = @($files | ForEach-Object -Parallel {
    $ErrorActionPreference = 'Stop'
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $size = [WGodotBrotli]::Compress($_.FullName)
    [pscustomobject]@{ Name = $_.Name; Before = $_.Length; After = $size; Seconds = $timer.Elapsed.TotalSeconds }
} -ThrottleLimit ([Math]::Min([Environment]::ProcessorCount, 4)))
if ($results.Count -ne $files.Count) { throw 'Web compression did not finish for every asset.' }

$mappings = @{}
foreach ($result in $results) {
    $mappings[$result.Name] = "$($result.Name).br"
}
$configPattern = [regex]::new('(?m)^const GODOT_CONFIG = (.+);\r?$')
$entryPoints = 0
foreach ($html in Get-ChildItem -LiteralPath $Directory -Filter '*.html' -File) {
    $content = [IO.File]::ReadAllText($html.FullName)
    $match = $configPattern.Match($content)
    if (!$match.Success) { continue }
    $config = $match.Groups[1].Value | ConvertFrom-Json -AsHashtable
    $config['fileMappings'] = $mappings
    $configJson = ConvertTo-Json -InputObject $config -Depth 20 -Compress
    $content = $content.Remove($match.Index, $match.Length).Insert($match.Index, "const GODOT_CONFIG = $configJson;")
    foreach ($name in $mappings.Keys) {
        $content = $content.Replace("src=`"$name`"", "src=`"$($mappings[$name])`"")
    }
    [IO.File]::WriteAllText($html.FullName, $content, [Text.UTF8Encoding]::new($false))
    $entryPoints++
}
if (!$entryPoints) { throw 'Cannot find GODOT_CONFIG in the exported HTML shell.' }

$workerPath = Join-Path $Directory "$BaseName.service.worker.js"
if (Test-Path -LiteralPath $workerPath) {
    $content = [IO.File]::ReadAllText($workerPath)
    foreach ($name in $mappings.Keys) {
        $content = $content.Replace("'$name'", "'$($mappings[$name])'").Replace("`"$name`"", "`"$($mappings[$name])`"")
    }
    [IO.File]::WriteAllText($workerPath, $content, [Text.UTF8Encoding]::new($false))
}

$summary = @('', '| Web asset (Brotli 11) | Original bytes | Download bytes | Saved | Seconds |', '| --- | ---: | ---: | ---: | ---: |')
foreach ($result in $results | Sort-Object Name) {
    $saved = if ($result.Before) { 100 * (1 - $result.After / $result.Before) } else { 0 }
    $summary += '| {0} | {1:N0} | {2:N0} | {3:N2}% | {4:N1} |' -f $result.Name, $result.Before, $result.After, $saved, $result.Seconds
}
$summary | ForEach-Object { Write-Host $_ }
if ($env:GITHUB_STEP_SUMMARY) { $summary | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY }
# The HTML, loader and PWA now reference .br URLs, decoded by the browser via Content-Encoding.
foreach ($file in $files) { Remove-Item -LiteralPath $file.FullName }
