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
    [string]$EditorPath,
    [string]$ConfigPath,
    [string]$SigningConfigPath,
    [string]$ModuleDirectory,
    [string]$OutputDirectory,
    [string]$ReleaseDirectory,
    [string]$ProjectName = 'DarkSurvivors',
    [string]$PythonExecutable = 'python',
    [string]$GradleExecutable,
    [string[]]$GradleArguments = @(),
    [ValidateRange(0, 1024)]
    [int]$Jobs = 0,
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
if (!$EditorPath) { $EditorPath = "$EngineDirectory/bin/$editorName" }
$EditorPath = (Resolve-Path -LiteralPath $EditorPath).Path
if (!$ModuleDirectory) { $ModuleDirectory = "$EngineDirectory/generated/main_game" }
if (!$OutputDirectory) { $OutputDirectory = "$EngineDirectory/game_build/$Edition" }
if (!$ReleaseDirectory) { $ReleaseDirectory = "$EngineDirectory/release_assets" }
$ModuleDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ModuleDirectory).Replace('\', '/')
$OutputDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$ReleaseDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReleaseDirectory)
if ($SigningConfigPath) { $SigningConfigPath = (Resolve-Path -LiteralPath $SigningConfigPath).Path }
$variants = @()
if ($BuildRelease) { $variants += 'release' }
if ($BuildDebug) { $variants += 'debug' }
if (!$variants) { throw 'Enable at least one game build variant.' }
if ($ConfigPath) {
    $ConfigPath = (Resolve-Path -LiteralPath $ConfigPath).Path
} else {
    $configSecretName = if ($Edition -eq 'ir') { 'GAME_IR_CONFIG_CONTENT' } else { 'GAME_GLOBAL_CONFIG_CONTENT' }
    $configContent = [Environment]::GetEnvironmentVariable($configSecretName, 'Process')
    if (!$configContent) { throw "Supply ConfigPath or $configSecretName." }
}
if ($Platform -eq 'web' -and $Edition -ne 'global') { throw 'Web exports must use the global instance.' }
if ($Platform -eq 'android' -and !$AndroidPackageId) { throw 'AndroidPackageId is required for Android editions.' }

# Match the project's wg wrapper while using the selected host editor.
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

$temporaryConfigPath = $null
$temporaryKeystorePath = $null
$signingVariables = @(
    'GODOT_ANDROID_KEYSTORE_RELEASE_PATH',
    'GODOT_ANDROID_KEYSTORE_RELEASE_USER',
    'GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD'
)
$previousSigningEnvironment = @{}
foreach ($name in $signingVariables) {
    $previousSigningEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$presetPath = Join-Path $GameDirectory 'export_presets.cfg'
$originalPreset = $null
$presetChanged = $false

Push-Location $EngineDirectory
try {
    if ($Platform -eq 'android') {
        # Read SDK versions from the engine so CI follows upstream upgrades.
        $gradleConfig = Get-Content platform/android/java/app/config.gradle -Raw
        $sdkMatch = [regex]::Match($gradleConfig, '(?m)^\s*compileSdk\s*:\s*(\d+)')
        $toolsMatch = [regex]::Match($gradleConfig, "(?m)^\s*buildTools\s*:\s*'([^']+)'")
        $ndkMatch = [regex]::Match($gradleConfig, "(?m)^\s*ndkVersion\s*:\s*'([^']+)'")
        if (!$sdkMatch.Success -or !$toolsMatch.Success -or !$ndkMatch.Success) {
            throw 'Cannot read Android SDK versions from app/config.gradle.'
        }
        $compileSdk = $sdkMatch.Groups[1].Value
        $buildTools = $toolsMatch.Groups[1].Value
        $batchExtension = if ($IsWindows) { '.bat' } else { '' }
        $binaryExtension = if ($IsWindows) { '.exe' } else { '' }
        if ($env:GITHUB_ACTIONS -eq 'true') {
            Invoke-Checked "$env:ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager$batchExtension" @("platforms;android-$compileSdk", "build-tools;$buildTools")
        }
        $apkSigner = "$env:ANDROID_HOME/build-tools/$buildTools/apksigner$batchExtension"
        $zipAlign = "$env:ANDROID_HOME/build-tools/$buildTools/zipalign$binaryExtension"
        foreach ($requiredPath in @("$env:ANDROID_HOME/platforms/android-$compileSdk/android.jar", $apkSigner, $zipAlign)) {
            if (!(Test-Path -LiteralPath $requiredPath)) { throw "Required Android SDK file is missing: $requiredPath" }
        }
        if ($env:GITHUB_ACTIONS -ne 'true' -and !(Test-Path -LiteralPath "$env:ANDROID_HOME/ndk/$($ndkMatch.Groups[1].Value)/source.properties")) {
            throw "Install Android NDK $($ndkMatch.Groups[1].Value) in ANDROID_HOME before building."
        }
        if (!$GradleExecutable) { $GradleExecutable = "$EngineDirectory/platform/android/java/gradlew$batchExtension" }
        Get-Command $GradleExecutable -ErrorAction Stop | Out-Null
        if (!(Test-Path -LiteralPath "$env:JAVA_HOME/bin/java$binaryExtension")) {
            throw 'Set JAVA_HOME to the installed JDK before building Android.'
        }

        if ($BuildRelease) {
            if ($SigningConfigPath) {
                $signing = Get-Content -LiteralPath $SigningConfigPath -Raw | ConvertFrom-Json
                if (!$signing.KeystorePath) { throw 'SigningConfigPath requires KeystorePath.' }
                $keystorePath = $signing.KeystorePath
                if (![IO.Path]::IsPathRooted($keystorePath)) {
                    $keystorePath = Join-Path (Split-Path $SigningConfigPath -Parent) $keystorePath
                }
                $keystorePath = (Resolve-Path -LiteralPath $keystorePath).Path
            } else {
                if (!$env:GAME_ANDROID_SIGN_KEY -or !$env:GAME_ANDROID_SIGN_JSON) {
                    throw 'Supply SigningConfigPath or GAME_ANDROID_SIGN_KEY and GAME_ANDROID_SIGN_JSON.'
                }
                $signing = $env:GAME_ANDROID_SIGN_JSON | ConvertFrom-Json
                $temporaryKeystorePath = [IO.Path]::GetTempFileName()
                [IO.File]::WriteAllBytes($temporaryKeystorePath, [Convert]::FromBase64String($env:GAME_ANDROID_SIGN_KEY))
                $keystorePath = $temporaryKeystorePath
            }
            if (!$signing.Alias -or !$signing.Password) { throw 'Signing configuration requires Alias and Password.' }
            if ($env:GITHUB_ACTIONS -eq 'true') {
                Write-Output "::add-mask::$($signing.Alias)"
                Write-Output "::add-mask::$($signing.Password)"
            }
            $env:GODOT_ANDROID_KEYSTORE_RELEASE_PATH = $keystorePath
            $env:GODOT_ANDROID_KEYSTORE_RELEASE_USER = $signing.Alias
            $env:GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD = $signing.Password
        }
    }

    New-Item -ItemType Directory -Path $outputDirectory, $releaseDirectory -Force | Out-Null
    if (!$ConfigPath) {
        $temporaryConfigPath = [IO.Path]::GetTempFileName()
        [IO.File]::WriteAllText($temporaryConfigPath, $configContent, [Text.UTF8Encoding]::new($false))
        $ConfigPath = $temporaryConfigPath
    }
    Write-Host "Generating startup configuration from: $ConfigPath"
    & "$GameDirectory/scripts/gen_startup_config.ps1" -TargetConfigPath $ConfigPath `
        -TargetScriptPath "$GameDirectory/src/core/game_config/game_startup_config.gd"

    Write-Host 'Importing game resources...'
    Invoke-Checked $editorPath @('--headless', '--path', $GameDirectory, '--editor', '--import')

    Write-Host 'Generating native game code...'
    wg export-cpp $moduleDirectory
    $manifest = Get-Content "$moduleDirectory/main_game.json" -Raw | ConvertFrom-Json

    $sconsPlatform = if ($Platform -eq 'linux') { 'linuxbsd' } else { $Platform }
    $buildFlags = @(
        "platform=$sconsPlatform",
        'module_text_server_fb_enabled=yes',
        "custom_modules=$moduleDirectory",
        'custom_modules_recursive=no',
        'module_main_game_enabled=yes',
        'module_gdscript_enabled=no',
        'extra_suffix=game',
        "cache_path=$EngineDirectory/.scons_cache",
        'redirect_build_objects=no'
    )
    if ($Jobs) { $buildFlags += "-j$Jobs" }
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
            $variantFlags += @('production=yes', 'debug_symbols=no', 'optimize=speed', 'lto=full')
        } elseif ($Platform -eq 'android') {
            $variantFlags += @('production=no', 'debug_symbols=yes', 'optimize=debug', 'lto=none')
        } else {
            $variantFlags += @('production=yes', 'debug_symbols=no')
        }
        if ($Platform -eq 'android') {
            foreach ($architecture in @('arm64', 'arm32')) {
                Invoke-Checked $PythonExecutable (@('-m', 'SCons') + $variantFlags + "arch=$architecture")
            }
        } else {
            Invoke-Checked $PythonExecutable (@('-m', 'SCons') + $variantFlags)
        }
    }

    if ($Platform -eq 'android') {
        Push-Location platform/android/java
        try {
            # Gradle only packages variants whose native libraries were built.
            Invoke-Checked $GradleExecutable ($GradleArguments + 'generateGodotTemplates')
        }
        finally {
            Pop-Location
        }
        foreach ($variant in $variants) {
            Copy-Item -LiteralPath "bin/android_$variant.apk" -Destination $templates[$variant]
        }
    }

    # Apply export paths only for packaging, then restore the user's preset in finally.
    $originalPreset = [IO.File]::ReadAllBytes($presetPath)
    $presetArguments = @(
        '--headless', '--path', $GameDirectory, '--script', "$PSScriptRoot/wgodot_game_preset.gd", '--',
        $presetName, $moduleDirectory, $templates.debug, $templates.release
    )
    if ($Platform -eq 'android') { $presetArguments += $AndroidPackageId }
    $presetChanged = $true
    Invoke-Checked $editorPath $presetArguments

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
            linux { "$ProjectName.x86_64" }
            windows { "$ProjectName.exe" }
            android { "$ProjectName.apk" }
            web { "$webExportBase-$variant.html" }
        }
        $exportPath = Join-Path $variantDirectory $fileName
        Write-Host "Exporting $Platform native game ($variant)..."
        Invoke-Checked $editorPath @('--headless', '--path', $GameDirectory, "--export-$variant", $presetName, $exportPath)

        $assetName = "$ProjectName-$Edition-$Platform-$variant"
        switch ($Platform) {
            linux {
                Invoke-Checked 'chmod' @('+x', $exportPath)
                if ($variant -eq 'release') {
                    Invoke-Checked $PythonExecutable @("$PSScriptRoot/wgodot_package_game.py", 'tar-xz', $variantDirectory, "$releaseDirectory/$assetName-x86_64.tar.xz")
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
                    Invoke-Checked $PythonExecutable @("$PSScriptRoot/wgodot_package_game.py", 'report', $variantDirectory, $archivePath)
                } else {
                    Compress-Archive -Path "$variantDirectory/*" -DestinationPath "$releaseDirectory/$assetName-x86_64.zip" -Force
                }
            }
            android {
                if ($variant -eq 'release') {
                    $originalApkSize = (Get-Item -LiteralPath $exportPath).Length
                    $repackedPath = "$exportPath.repacked"
                    $alignedPath = "$exportPath.aligned"
                    Invoke-Checked $PythonExecutable @("$PSScriptRoot/wgodot_package_game.py", 'apk', $exportPath, $repackedPath)
                    # Android requires alignment before signing. Never modify the final signed APK.
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
                    Invoke-Checked $PythonExecutable @("$PSScriptRoot/wgodot_package_game.py", 'report', "$originalApkSize", "$releaseDirectory/$assetName.apk")
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
                    Invoke-Checked $PythonExecutable @("$PSScriptRoot/wgodot_package_game.py", 'zip', $variantDirectory, "$releaseDirectory/$assetName-wasm32.zip", '--original-size', "$webOriginalSize")
                } else {
                    Compress-Archive -Path "$variantDirectory/*" -DestinationPath "$releaseDirectory/$assetName-wasm32.zip" -Force
                }
            }
        }
    }
}
finally {
    if ($presetChanged) { [IO.File]::WriteAllBytes($presetPath, $originalPreset) }
    foreach ($temporaryPath in @($temporaryConfigPath, $temporaryKeystorePath)) {
        if ($temporaryPath) { Remove-Item -LiteralPath $temporaryPath -ErrorAction SilentlyContinue }
    }
    foreach ($name in $signingVariables) {
        [Environment]::SetEnvironmentVariable($name, $previousSigningEnvironment[$name], 'Process')
    }
    Pop-Location
}
