# wgodot-changes::file
#requires -Version 7.0
param(
    [Parameter(Mandatory)]
    [ValidateSet('linux', 'windows', 'android', 'web')]
    [string]$Platform,
    [Parameter(Mandatory)]
    [string]$GameDirectory,
    [Parameter(Mandatory)]
    [ValidateSet('ir', 'global')]
    [string]$Edition,
    [string]$AndroidPackageId,
    [string]$EngineDirectory = "$PSScriptRoot/../..",
    [switch]$BuildDebug,
    [switch]$BuildRelease = $true
)

$ErrorActionPreference = 'Stop'

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Program failed (exit $LASTEXITCODE)." }
}

$EngineDirectory = (Resolve-Path -LiteralPath $EngineDirectory).Path.Replace('\', '/')
$GameDirectory = (Resolve-Path -LiteralPath $GameDirectory).Path.Replace('\', '/')
$editorName = if ($IsWindows) { 'godot.windows.editor.x86_64.exe' } else { 'godot.linuxbsd.editor.x86_64' }
$editorPath = "$EngineDirectory/bin/$editorName"
$moduleDirectory = "$EngineDirectory/generated/main_game"
$configPath = "$GameDirectory/configs/config.ini"
$outputDirectory = "$EngineDirectory/game_build/$Edition"
$releaseDirectory = "$EngineDirectory/release_assets"
$variants = @()
if ($BuildRelease) { $variants += 'release' }
if ($BuildDebug) { $variants += 'debug' }
if (!$variants) { throw 'Enable at least one game build variant.' }
$configSecretName = if ($Edition -eq 'ir') { 'GAME_IR_CONFIG_CONTENT' } else { 'GAME_GLOBAL_CONFIG_CONTENT' }
$configContent = [Environment]::GetEnvironmentVariable($configSecretName, 'Process')
if (!$configContent) { throw "$configSecretName is required." }
if ($Platform -eq 'web' -and $Edition -ne 'global') { throw 'Web exports must use the global instance.' }
if ($Platform -eq 'android' -and !$AndroidPackageId) { throw 'AndroidPackageId is required for Android editions.' }

# Match the project's wg wrapper while using the editor built in this job.
function wg {
    Invoke-Checked $editorPath (@('--headless', '--path', $GameDirectory, '--wg') + $args)
}

$presetName = switch ($Platform) {
    linux { 'Linux Native' }
    windows { 'Windows Native' }
    android { 'Android Native' }
    web { 'Web Native' }
}
$templates = @{}
foreach ($variant in @('debug', 'release')) {
    $templateName = switch ($Platform) {
        linux { "godot.linuxbsd.template_$variant.x86_64.game" }
        windows { "godot.windows.template_$variant.x86_64.game.exe" }
        android { "android_$variant.game.apk" }
        web { "godot.web.template_$variant.wasm32.game.zip" }
    }
    $templates[$variant] = "$EngineDirectory/bin/$templateName"
}

$webExportBase = ''
if ($Platform -eq 'web') {
    $gameRevision = git -C $GameDirectory rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Could not read the game commit for web asset filenames.' }
    $gameRevision = $gameRevision.Substring(0, 7)
    $engineRevision = git -C $EngineDirectory rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Could not read the engine commit for web asset filenames.' }
    $engineRevision = $engineRevision.Substring(0, 7)
    # Native web binaries change when either the game or the engine changes.
    $webExportBase = "index-$gameRevision-$engineRevision"
}

$signingDirectory = $null
$signingVariables = @(
    'GODOT_ANDROID_KEYSTORE_RELEASE_PATH',
    'GODOT_ANDROID_KEYSTORE_RELEASE_USER',
    'GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD'
)

Push-Location $EngineDirectory
try {
    if ($Platform -eq 'android') {
        # Read SDK versions from the engine so CI follows upstream upgrades.
        $gradleConfig = Get-Content platform/android/java/app/config.gradle -Raw
        $sdkMatch = [regex]::Match($gradleConfig, '(?m)^\s*compileSdk\s*:\s*(\d+)')
        $toolsMatch = [regex]::Match($gradleConfig, "(?m)^\s*buildTools\s*:\s*'([^']+)'")
        if (!$sdkMatch.Success -or !$toolsMatch.Success) { throw 'Cannot read Android SDK versions from app/config.gradle.' }
        $compileSdk = $sdkMatch.Groups[1].Value
        $buildTools = $toolsMatch.Groups[1].Value
        Invoke-Checked "$env:ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" @("platforms;android-$compileSdk", "build-tools;$buildTools")
        $apkSigner = "$env:ANDROID_HOME/build-tools/$buildTools/apksigner"

        if ($BuildRelease) {
            if (!$env:GAME_ANDROID_SIGN_KEY -or !$env:GAME_ANDROID_SIGN_JSON) {
                throw 'Android release builds require GAME_ANDROID_SIGN_KEY and GAME_ANDROID_SIGN_JSON.'
            }
            $signing = $env:GAME_ANDROID_SIGN_JSON | ConvertFrom-Json
            if (!$signing.Alias -or !$signing.Password) { throw 'GAME_ANDROID_SIGN_JSON requires Alias and Password.' }
            Write-Output "::add-mask::$($signing.Alias)"
            Write-Output "::add-mask::$($signing.Password)"
            $signingDirectory = Join-Path $env:RUNNER_TEMP 'wgodot-game-signing'
            New-Item -ItemType Directory -Path $signingDirectory -Force | Out-Null
            $keystorePath = Join-Path $signingDirectory 'android-release.keystore'
            [IO.File]::WriteAllBytes($keystorePath, [Convert]::FromBase64String($env:GAME_ANDROID_SIGN_KEY))
            $env:GODOT_ANDROID_KEYSTORE_RELEASE_PATH = $keystorePath
            $env:GODOT_ANDROID_KEYSTORE_RELEASE_USER = $signing.Alias
            $env:GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD = $signing.Password
        }
    }

    New-Item -ItemType Directory -Path "$GameDirectory/configs", $outputDirectory, $releaseDirectory -Force | Out-Null
    # The edition's secret owns the complete configuration compiled into the game.
    [IO.File]::WriteAllText($configPath, $configContent, [Text.UTF8Encoding]::new($false))
    try {
        & "$GameDirectory/scripts/gen_startup_config.ps1" -TargetConfigPath $configPath `
            -TargetScriptPath "$GameDirectory/src/core/game_config/game_startup_config.gd"
    }
    finally {
        Remove-Item -LiteralPath $configPath
    }

    Write-Host 'Importing game resources...'
    Invoke-Checked $editorPath @('--headless', '--path', $GameDirectory, '--editor', '--import')
    $presetArguments = @(
        '--headless', '--path', $GameDirectory, '--script', "$PSScriptRoot/wgodot_game_preset.gd", '--',
        $presetName, $moduleDirectory, $templates.debug, $templates.release
    )
    if ($Platform -eq 'android') { $presetArguments += $AndroidPackageId }
    Invoke-Checked $editorPath $presetArguments

    Write-Host 'Generating native game code...'
    wg export-cpp $moduleDirectory
    $manifest = Get-Content "$moduleDirectory/main_game.json" -Raw | ConvertFrom-Json

    $sconsPlatform = if ($Platform -eq 'linux') { 'linuxbsd' } else { $Platform }
    $buildFlags = @(
        "platform=$sconsPlatform",
        'production=yes',
        'debug_symbols=no',
        'module_text_server_fb_enabled=yes',
        "custom_modules=$moduleDirectory",
        'custom_modules_recursive=no',
        'module_main_game_enabled=yes',
        'module_gdscript_enabled=no',
        'extra_suffix=game',
        "cache_path=$EngineDirectory/.scons_cache",
        'redirect_build_objects=no'
    )
    $buildFlags += switch ($Platform) {
        linux { @('arch=x86_64', 'accesskit=no') }
        windows { @('arch=x86_64', 'accesskit=no', 'angle=no', 'd3d12=no', 'windows_subsystem=console') }
        android { @('swappy=yes') }
        web { @('arch=wasm32', 'threads=yes', 'use_closure_compiler=yes') }
    }

    foreach ($variant in $variants) {
        Write-Host "Building $Platform native game ($variant)..."
        $variantFlags = $buildFlags + "target=template_$variant"
        if ($variant -eq 'release') {
            $variantFlags += @('optimize=speed', 'lto=full')
        }
        if ($Platform -eq 'android') {
            foreach ($architecture in @('arm64', 'arm32')) {
                Invoke-Checked 'scons' ($variantFlags + "arch=$architecture")
            }
        } else {
            Invoke-Checked 'scons' $variantFlags
        }
    }

    if ($Platform -eq 'android') {
        Push-Location platform/android/java
        try {
            # Gradle only packages variants whose native libraries were built.
            Invoke-Checked './gradlew' @('generateGodotTemplates')
        }
        finally {
            Pop-Location
        }
        foreach ($variant in $variants) {
            Copy-Item -LiteralPath "bin/android_$variant.apk" -Destination $templates[$variant]
        }
    }

    foreach ($variant in $variants) {
        $templatePath = $templates[$variant]
        $buildManifest = @{
            generation = $manifest.generation
            binary_sha256 = (Get-FileHash -LiteralPath $templatePath -Algorithm SHA256).Hash.ToLowerInvariant()
        } | ConvertTo-Json
        [IO.File]::WriteAllText("$templatePath.native.json", $buildManifest, [Text.UTF8Encoding]::new($false))

        $variantDirectory = Join-Path $outputDirectory $variant
        New-Item -ItemType Directory -Path $variantDirectory -Force | Out-Null
        $fileName = switch ($Platform) {
            linux { 'DarkSurvivors.x86_64' }
            windows { 'DarkSurvivors.exe' }
            android { 'DarkSurvivors.apk' }
            web { "$webExportBase-$variant.html" }
        }
        $exportPath = Join-Path $variantDirectory $fileName
        Write-Host "Exporting $Platform native game ($variant)..."
        Invoke-Checked $editorPath @('--headless', '--path', $GameDirectory, "--export-$variant", $presetName, $exportPath)

        $assetName = "DarkSurvivors-$Edition-$Platform-$variant"
        switch ($Platform) {
            linux {
                Invoke-Checked 'chmod' @('+x', $exportPath)
                if ($variant -eq 'release') {
                    Invoke-Checked 'python' @("$PSScriptRoot/wgodot_package_game.py", 'tar-xz', $variantDirectory, "$releaseDirectory/$assetName-x86_64.tar.xz")
                } else {
                    Invoke-Checked 'tar' @('-czf', "$releaseDirectory/$assetName-x86_64.tar.gz", '-C', $variantDirectory, '.')
                }
            }
            windows {
                if ($variant -eq 'release') {
                    $archivePath = "$releaseDirectory/$assetName-x86_64.7z"
                    # Recreate the archive: 7-Zip's update mode retains files removed from a later export.
                    if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath }
                    Push-Location $variantDirectory
                    try {
                        Invoke-Checked '7z' @('a', '-t7z', '-mx=9', '-m0=LZMA2:d=256m:fb=273', '-ms=on', '-mmt=2', $archivePath, '.')
                        Invoke-Checked '7z' @('t', $archivePath)
                    } finally {
                        Pop-Location
                    }
                    Invoke-Checked 'python' @("$PSScriptRoot/wgodot_package_game.py", 'report', $variantDirectory, $archivePath)
                } else {
                    Compress-Archive -Path "$variantDirectory/*" -DestinationPath "$releaseDirectory/$assetName-x86_64.zip" -Force
                }
            }
            android {
                if ($variant -eq 'release') {
                    $originalApkSize = (Get-Item -LiteralPath $exportPath).Length
                    $repackedPath = "$exportPath.repacked"
                    $alignedPath = "$exportPath.aligned"
                    Invoke-Checked 'python' @("$PSScriptRoot/wgodot_package_game.py", 'apk', $exportPath, $repackedPath)
                    # Android requires alignment before signing. Never modify the final signed APK.
                    $zipAlign = "$env:ANDROID_HOME/build-tools/$buildTools/zipalign"
                    Invoke-Checked $zipAlign @('-f', '-z', '-P', '16', '4', $repackedPath, $alignedPath)
                    Invoke-Checked $apkSigner @(
                        'sign', '--ks', $env:GODOT_ANDROID_KEYSTORE_RELEASE_PATH,
                        '--ks-key-alias', $env:GODOT_ANDROID_KEYSTORE_RELEASE_USER,
                        '--ks-pass', 'env:GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD',
                        '--key-pass', 'env:GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD',
                        '--v4-signing-enabled', 'false', $alignedPath
                    )
                    Invoke-Checked $apkSigner @('verify', $alignedPath)
                    Invoke-Checked $zipAlign @('-c', '-P', '16', '4', $alignedPath)
                    if ((Get-Item -LiteralPath $alignedPath).Length -lt $originalApkSize) {
                        Move-Item -LiteralPath $alignedPath -Destination $exportPath -Force
                    } else {
                        Remove-Item -LiteralPath $alignedPath
                    }
                    Remove-Item -LiteralPath $repackedPath
                }
                Invoke-Checked $apkSigner @('verify', $exportPath)
                Copy-Item -LiteralPath $exportPath -Destination "$releaseDirectory/$assetName.apk"
                if ($variant -eq 'release') {
                    Invoke-Checked 'python' @("$PSScriptRoot/wgodot_package_game.py", 'report', "$originalApkSize", "$releaseDirectory/$assetName.apk")
                }
            }
            web {
                # Godot generates all asset references from the versioned export basename.
                # Keep that HTML for the service worker, plus the stable hosting entry point.
                Copy-Item -LiteralPath $exportPath -Destination "$variantDirectory/index.html"
                Copy-Item -LiteralPath "$GameDirectory/src/html/telegram-web-app.js" -Destination $variantDirectory

                $webManifestPath = [IO.Path]::ChangeExtension($exportPath, '.manifest.json')
                if (Test-Path -LiteralPath $webManifestPath) {
                    # Installed PWAs must open the current game, not a previous commit's HTML.
                    $webManifest = Get-Content -LiteralPath $webManifestPath -Raw | ConvertFrom-Json
                    $webManifest.start_url = './index.html'
                    $webManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $webManifestPath -Encoding utf8
                }

                if ($variant -eq 'release') {
                    $webOriginalSize = (Get-ChildItem -LiteralPath $variantDirectory -Recurse -File | Measure-Object -Property Length -Sum).Sum
                    & "$PSScriptRoot/wgodot_compress_web.ps1" -Directory $variantDirectory -BaseName "$webExportBase-$variant"
                    Invoke-Checked 'python' @("$PSScriptRoot/wgodot_package_game.py", 'zip', $variantDirectory, "$releaseDirectory/$assetName-wasm32.zip", '--original-size', "$webOriginalSize")
                } else {
                    Compress-Archive -Path "$variantDirectory/*" -DestinationPath "$releaseDirectory/$assetName-wasm32.zip" -Force
                }
            }
        }
    }
}
finally {
    if ($signingDirectory) {
        Remove-Item -LiteralPath (Join-Path $signingDirectory 'android-release.keystore') -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $signingDirectory -ErrorAction SilentlyContinue
    }
    foreach ($name in $signingVariables) {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    Pop-Location
}
