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
    $_.Name -in @("$BaseName.wasm", "$BaseName.pck")
})
if (!$files) { throw "No Web assets found for $BaseName." }

# Chrome does not expose native Brotli to JavaScript. Build the fallback from
# Godot's existing dependency with the same Emscripten toolchain as the game.
$brotliDirectory = (Resolve-Path -LiteralPath "$PSScriptRoot/../../thirdparty/brotli").Path
$decoderArguments = @(
    "$PSScriptRoot/wgodot_brotli_decoder.c",
    "-I$brotliDirectory/include",
    '-O3', '-flto', '--no-entry',
    '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sENVIRONMENT=web', '-sFILESYSTEM=0',
    '-sALLOW_MEMORY_GROWTH=1', '-sMAXIMUM_MEMORY=134217728', '-sMALLOC=emmalloc',
    '-sEXPORTED_FUNCTIONS=["_wg_brotli_create","_wg_brotli_destroy","_wg_brotli_input","_wg_brotli_output","_wg_brotli_output_size","_wg_brotli_decode"]',
    '-sEXPORTED_RUNTIME_METHODS=["HEAPU8"]',
    '-o', "$Directory/$BaseName.brotli-decoder.js"
)
$decoderArguments += @(Get-ChildItem "$brotliDirectory/common/*.c", "$brotliDirectory/dec/*.c" -File | ForEach-Object FullName)
& emcc @decoderArguments
if ($LASTEXITCODE -ne 0) { throw 'Brotli decoder compilation failed.' }
Copy-Item -LiteralPath "$PSScriptRoot/wgodot_web_decompress.js" -Destination "$Directory/$BaseName.decompress.js"
Copy-Item -LiteralPath "$brotliDirectory/LICENSE" -Destination "$Directory/$BaseName.brotli-LICENSE.txt"

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
    $mappings[$result.Name] = "$($result.Name).br.bin"
}
$configPattern = [regex]::new('(?m)^const GODOT_CONFIG = (.+);\r?$')
$entryPoints = 0
foreach ($html in Get-ChildItem -LiteralPath $Directory -Filter '*.html' -File) {
    $content = [IO.File]::ReadAllText($html.FullName)
    $match = $configPattern.Match($content)
    if (!$match.Success) { continue }
    $config = $match.Groups[1].Value | ConvertFrom-Json -AsHashtable
    $config['fileMappings'] = $mappings
    foreach ($result in $results) {
        $config['fileSizes'][$result.Name] = $result.After
    }
    $configJson = ConvertTo-Json -InputObject $config -Depth 20 -Compress
    $content = $content.Remove($match.Index, $match.Length).Insert($match.Index, "const GODOT_CONFIG = $configJson;")
    $scriptTag = "<script src=`"$BaseName.js`"></script>"
    if (!$content.Contains($scriptTag)) { throw "Cannot find engine script in $($html.Name)." }
    $content = $content.Replace($scriptTag, "<script src=`"$BaseName.decompress.js`"></script>`n$scriptTag")
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
    $cachePattern = [regex]::new('(?m)^const CACHED_FILES = (\[.*\]);\r?$')
    $cacheMatch = $cachePattern.Match($content)
    if (!$cacheMatch.Success) { throw 'Cannot find service worker cache list.' }
    $cachedFiles = @($cacheMatch.Groups[1].Value | ConvertFrom-Json)
    $cachedFiles += "$BaseName.decompress.js", "$BaseName.brotli-decoder.js", "$BaseName.brotli-decoder.wasm"
    $cacheJson = ConvertTo-Json -InputObject $cachedFiles -Compress
    $content = $content.Remove($cacheMatch.Index, $cacheMatch.Length).Insert($cacheMatch.Index, "const CACHED_FILES = $cacheJson;")
    [IO.File]::WriteAllText($workerPath, $content, [Text.UTF8Encoding]::new($false))
}

$summary = @('', '| Web asset (Brotli 11) | Original bytes | Download bytes | Saved | Seconds |', '| --- | ---: | ---: | ---: | ---: |')
foreach ($result in $results | Sort-Object Name) {
    $saved = if ($result.Before) { 100 * (1 - $result.After / $result.Before) } else { 0 }
    $summary += '| {0} | {1:N0} | {2:N0} | {3:N2}% | {4:N1} |' -f $result.Name, $result.Before, $result.After, $saved, $result.Seconds
}
$summary | ForEach-Object { Write-Host $_ }
if ($env:GITHUB_STEP_SUMMARY) { $summary | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY }
# Only the tagged binary payloads replace the originals. Scripts load normally.
foreach ($file in $files) { Remove-Item -LiteralPath $file.FullName }
