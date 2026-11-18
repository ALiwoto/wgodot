# wgodot-changes::file
param(
    [ValidateSet('All', 'Native', 'Package', 'Dependencies', 'Java')]
    [string]$Phase = 'All',
    [string]$JavaDirectory = 'C:/Program Files/Eclipse Adoptium/jdk-17.0.6.10-hotspot',
    [string]$GradleExecutable,
    [string]$Proxy,
    [switch]$KeepGoing,
    [ValidateRange(0, 2147483647)]
    [int]$WaitForProcessId = 0,
    [string]$StatusPath
)

$ErrorActionPreference = 'Stop'
$buildExitCode = 1
$started = Get-Date -Format o
$previousJava = $env:JAVA_HOME
$previousPath = $env:PATH
$ownsStatus = $false
$apkPath = Join-Path $PSScriptRoot 'platform/android/java/editor/build/outputs/apk/horizonos/debug/android_editor-horizonos-debug.apk'
$nativePath = Join-Path $PSScriptRoot 'platform/android/java/lib/libs/tools/debug/arm64-v8a/libgodot_android.so'

function Write-BuildStatus {
    param([string]$State, [string]$CurrentPhase)

    if (!$StatusPath) {
        return
    }
    $outputPath = if ($CurrentPhase -eq 'Native') { $nativePath } else { $apkPath }
    @(
        '[build]'
        "state=$State"
        "phase=$CurrentPhase"
        "started=$started"
        "updated=$(Get-Date -Format o)"
        "pid=$PID"
        "exit_code=$buildExitCode"
        "output=$outputPath"
    ) | Set-Content -LiteralPath $StatusPath -Encoding utf8
}

try {
    if ($StatusPath) {
        $StatusPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($StatusPath)
        if (Test-Path -LiteralPath $StatusPath) {
            $oldStatus = Get-Content -LiteralPath $StatusPath
            $oldPidLine = $oldStatus | Where-Object { $_ -match '^pid=\d+$' }
            if (($oldStatus -contains 'state=running' -or $oldStatus -contains 'state=waiting') -and $oldPidLine) {
                $oldBuild = Get-Process -Id ($oldPidLine -replace '^pid=', '') -ErrorAction SilentlyContinue
                if ($oldBuild) {
                    throw "Build is already running: $StatusPath"
                }
            }
        }
        New-Item -ItemType Directory -Path (Split-Path -Parent $StatusPath) -Force | Out-Null
    }
    $ownsStatus = $true
    if ($WaitForProcessId) {
        Write-BuildStatus 'waiting' $Phase
        $previousBuild = Get-Process -Id $WaitForProcessId -ErrorAction SilentlyContinue
        if ($previousBuild) {
            $previousBuild | Wait-Process
        }
    }
    if (!(Test-Path -LiteralPath "$env:ANDROID_HOME/ndk/29.0.14206865" -PathType Container)) {
        throw 'Android NDK 29.0.14206865 is required under ANDROID_HOME. Install it before building.'
    }
    if (!(Test-Path -LiteralPath "$JavaDirectory/bin/java.exe" -PathType Leaf)) {
        throw "JDK 17 not found: $JavaDirectory"
    }
    $env:JAVA_HOME = $JavaDirectory
    $env:PATH = "$JavaDirectory/bin;$env:PATH"
    $python = (Get-Command python -ErrorAction Stop).Source

    if ($Phase -in 'All', 'Native') {
        Write-BuildStatus 'running' 'native'
        $sconsArguments = @(
            'platform=android', 'arch=arm64', 'target=editor',
            'optimize=none', 'lto=none', 'debug_symbols=no',
            'module_postgresql_enabled=no', 'generate_android_binaries=no',
            'linkflags=-Wl,-Map=bin/godot.android.editor.arm64.map',
            'progress=no'
        )
        if ($KeepGoing) {
            $sconsArguments += '--keep-going'
        }
        Push-Location $PSScriptRoot
        try {
            & $python -u -m SCons @sconsArguments
            if ($LASTEXITCODE) {
                throw "Android native build failed with exit code $LASTEXITCODE."
            }
        } finally {
            Pop-Location
        }
    }

    if ($Phase -ne 'Native') {
        Write-BuildStatus 'running' $(if ($Phase -eq 'All') { 'Package' } else { $Phase })
        if (!$GradleExecutable) {
            $GradleExecutable = Join-Path $PSScriptRoot 'platform/android/java/gradlew.bat'
        }
        if (!(Test-Path -LiteralPath $GradleExecutable -PathType Leaf)) {
            throw "Gradle executable not found: $GradleExecutable"
        }
        Push-Location (Join-Path $PSScriptRoot 'platform/android/java')
        try {
            # SCons owns native compilation; Gradle packages the same cached library.
            $gradleArguments = @('--console=plain', '--parallel')
            if ($Proxy) {
                $proxyUri = [uri]$Proxy
                $gradleArguments += @(
                    "-Dhttp.proxyHost=$($proxyUri.Host)", "-Dhttp.proxyPort=$($proxyUri.Port)",
                    "-Dhttps.proxyHost=$($proxyUri.Host)", "-Dhttps.proxyPort=$($proxyUri.Port)"
                )
            }
            if ($Phase -eq 'Dependencies') {
                $gradleArguments += @(':editor:dependencies', '--configuration', 'horizonosDebugRuntimeClasspath')
            } elseif ($Phase -eq 'Java') {
                $gradleArguments += @(':editor:compileHorizonosDebugKotlin', '-x', ':lib:compileGodotNativeLibsEditorDebugArm64')
            } else {
                $gradleArguments += @(':editor:assembleHorizonosDebug', '-x', ':lib:compileGodotNativeLibsEditorDebugArm64')
            }
            & $GradleExecutable @gradleArguments
            if ($LASTEXITCODE) {
                throw "Android APK packaging failed with exit code $LASTEXITCODE."
            }
        } finally {
            Pop-Location
        }
        if ($Phase -in 'All', 'Package' -and !(Test-Path -LiteralPath $apkPath -PathType Leaf)) {
            throw "Gradle did not produce the expected APK: $apkPath"
        }
        if ($Phase -in 'All', 'Package') {
            Write-Host "Built: $apkPath"
        }
    }
    $buildExitCode = 0
} finally {
    $env:JAVA_HOME = $previousJava
    $env:PATH = $previousPath
    if ($ownsStatus) {
        Write-BuildStatus $(if ($buildExitCode -eq 0) { 'succeeded' } else { 'failed' }) $Phase
    }
}
