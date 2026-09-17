# wgodot-changes::file
#requires -Version 7.0
param(
    [Parameter(Mandatory)]
    [ValidateSet('linux', 'windows', 'android', 'web')]
    [string]$Platform,
    [Parameter(Mandatory)]
    [string]$GameDirectory,
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
$outputDirectory = "$EngineDirectory/game_build"
$releaseDirectory = "$EngineDirectory/release_assets"
$variants = @()
if ($BuildRelease) { $variants += 'release' }
if ($BuildDebug) { $variants += 'debug' }
if (!$variants) { throw 'Enable at least one game build variant.' }
if (!$env:GAME_CONFIG_CONTENT) { throw 'GAME_CONFIG_CONTENT is required.' }

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
    [IO.File]::WriteAllText($configPath, $env:GAME_CONFIG_CONTENT, [Text.UTF8Encoding]::new($false))
    try {
        & "$GameDirectory/scripts/gen_startup_config.ps1" -TargetConfigPath $configPath `
            -TargetScriptPath "$GameDirectory/src/core/game_config/game_startup_config.gd"
    }
    finally {
        Remove-Item -LiteralPath $configPath
    }

    Write-Host 'Importing game resources...'
    Invoke-Checked $editorPath @('--headless', '--path', $GameDirectory, '--editor', '--import')
    Invoke-Checked $editorPath @(
        '--headless', '--path', $GameDirectory, '--script', "$PSScriptRoot/wgodot_game_preset.gd", '--',
        $presetName, $moduleDirectory, $templates.debug, $templates.release
    )

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
            web { 'index.html' }
        }
        $exportPath = Join-Path $variantDirectory $fileName
        Write-Host "Exporting $Platform native game ($variant)..."
        Invoke-Checked $editorPath @('--headless', '--path', $GameDirectory, "--export-$variant", $presetName, $exportPath)

        $assetName = "DarkSurvivors-$Platform-$variant"
        switch ($Platform) {
            linux {
                Invoke-Checked 'chmod' @('+x', $exportPath)
                Invoke-Checked 'tar' @('-czf', "$releaseDirectory/$assetName-x86_64.tar.gz", '-C', $variantDirectory, '.')
            }
            windows {
                Compress-Archive -Path "$variantDirectory/*" -DestinationPath "$releaseDirectory/$assetName-x86_64.zip" -Force
            }
            android {
                Invoke-Checked $apkSigner @('verify', $exportPath)
                Copy-Item -LiteralPath $exportPath -Destination "$releaseDirectory/$assetName.apk"
            }
            web {
                Compress-Archive -Path "$variantDirectory/*" -DestinationPath "$releaseDirectory/$assetName-wasm32.zip" -Force
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
