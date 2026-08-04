[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',

    [ValidateRange(60, 3600)]
    [int]$Frames = 180,

    [string]$Model = '',
    [string]$Motion = '',

    [ValidateSet('scene', 'default', 'none')]
    [string]$CaptureSource = 'scene',

    [switch]$SkipNativeDemos,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildPath = Join-Path $ProjectRoot 'build-verify'
$OutputRoot = Join-Path $ProjectRoot 'artifacts/render-smoke/windows'

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "命令执行失败（退出码 $LASTEXITCODE）：$FilePath $($Arguments -join ' ')"
    }
}

function Invoke-LoggedNative {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [Parameter(Mandatory = $true)]
        [string]$LogPath
    )

    $global:LASTEXITCODE = 0
    & $FilePath @Arguments 2>&1 | Tee-Object -FilePath $LogPath
    $ExitCode = $LASTEXITCODE
    if ($ExitCode -ne 0) {
        throw "命令执行失败（退出码 $ExitCode）：$FilePath $($Arguments -join ' ')"
    }
}

function Find-Executable {
    param([string[]]$Candidates)
    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }
    throw "找不到可执行文件。已检查：$($Candidates -join ', ')"
}

function Reset-Directory {
    param([string]$Path)
    Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Show-CaptureSummary {
    param([string]$Path)
    $AnalyzerArguments = $PythonPrefix + @(
        (Join-Path $ProjectRoot 'script/analyze_render_captures.py'),
        $Path
    )
    Invoke-Checked $PythonCommand $AnalyzerArguments
}

$CMake = (Get-Command cmake.exe -ErrorAction Stop).Source
$CTest = (Get-Command ctest.exe -ErrorAction Stop).Source

if (-not $SkipBuild) {
    Write-Host '==> 配置 Windows 验证构建（Native=ON）' -ForegroundColor Cyan
    Invoke-Checked $CMake @(
        '-S', $ProjectRoot,
        '-B', $BuildPath,
        '-DWISTERIA_BUILD_NATIVE=ON',
        '-DBUILD_TESTING=ON'
    )

    Write-Host '==> 编译 wisteria / tests / native' -ForegroundColor Cyan
    Invoke-Checked $CMake @(
        '--build', $BuildPath,
        '--config', $Configuration,
        '--target', 'wisteria', 'wisteria_unit_tests', 'wisteria_runtime_tests',
        'wisteria_integration_tests', 'wisteria_native',
        '--parallel'
    )
}

Write-Host '==> 运行 CTest' -ForegroundColor Cyan
Invoke-Checked $CTest @(
    '--test-dir', $BuildPath,
    '-C', $Configuration,
    '--output-on-failure'
)

$WisteriaExe = Find-Executable @(
    (Join-Path $BuildPath "$Configuration/wisteria.exe"),
    (Join-Path $BuildPath 'wisteria.exe')
)
$NativeLibrary = Find-Executable @(
    (Join-Path $BuildPath "$Configuration/wisteria_native.dll"),
    (Join-Path $BuildPath 'wisteria_native.dll')
)

$PythonCommand = $null
$PythonPrefix = @()
if (Get-Command py.exe -ErrorAction SilentlyContinue) {
    $PythonCommand = (Get-Command py.exe).Source
    $PythonPrefix = @('-3')
}
elseif (Get-Command python.exe -ErrorAction SilentlyContinue) {
    $PythonCommand = (Get-Command python.exe).Source
}
else {
    throw '找不到 Python 3；截图像素校验需要 Python。'
}

$OldEnvironment = @{
    WISTERIA_ASSET_ROOT = $env:WISTERIA_ASSET_ROOT
    WISTERIA_FRAME_PROFILE = $env:WISTERIA_FRAME_PROFILE
    WISTERIA_GL_DIAGNOSTICS = $env:WISTERIA_GL_DIAGNOSTICS
    WISTERIA_SCREENSHOT_DIR = $env:WISTERIA_SCREENSHOT_DIR
    WISTERIA_SCREENSHOT_INTERVAL = $env:WISTERIA_SCREENSHOT_INTERVAL
    WISTERIA_SCREENSHOT_SOURCE = $env:WISTERIA_SCREENSHOT_SOURCE
    WISTERIA_NATIVE_LIB = $env:WISTERIA_NATIVE_LIB
}

try {
    $env:WISTERIA_ASSET_ROOT = Join-Path $ProjectRoot 'assets'
    $env:WISTERIA_FRAME_PROFILE = '1'
    $env:WISTERIA_GL_DIAGNOSTICS = '1'
    $env:WISTERIA_SCREENSHOT_INTERVAL = '30'
    $env:WISTERIA_SCREENSHOT_SOURCE = $CaptureSource
    $env:WISTERIA_NATIVE_LIB = $NativeLibrary

    $DesktopOutput = Join-Path $OutputRoot 'desktop'
    Reset-Directory $DesktopOutput
    if ($CaptureSource -eq 'none') {
        Remove-Item Env:WISTERIA_SCREENSHOT_DIR -ErrorAction SilentlyContinue
    }
    else {
        $env:WISTERIA_SCREENSHOT_DIR = $DesktopOutput
    }

    $DesktopArguments = @('--frames', "$Frames", '--fixed-dt', '0.016666667')
    if (-not [string]::IsNullOrWhiteSpace($Model)) {
        $DesktopArguments += @('--model', $Model)
    }
    if (-not [string]::IsNullOrWhiteSpace($Motion)) {
        $DesktopArguments += @('--motion', $Motion)
    }

    Write-Host "==> C++ 桌面 demo：固定运行 $Frames 帧（capture=$CaptureSource）" -ForegroundColor Cyan
    Push-Location $ProjectRoot
    try {
        Invoke-LoggedNative `
            $WisteriaExe `
            $DesktopArguments `
            (Join-Path $DesktopOutput 'run.log')
    }
    finally {
        Pop-Location
    }
    if ($CaptureSource -ne 'none') {
        Show-CaptureSummary $DesktopOutput
    }

    if (-not $SkipNativeDemos) {
        $NativeOutput = Join-Path $OutputRoot 'native-single'
        Reset-Directory $NativeOutput
        if ($CaptureSource -eq 'none') {
            Remove-Item Env:WISTERIA_SCREENSHOT_DIR -ErrorAction SilentlyContinue
        }
        else {
            $env:WISTERIA_SCREENSHOT_DIR = $NativeOutput
        }
        $NativeArguments = @(
            'examples/python/native_window_demo.py',
            '--frames', "$Frames"
        )
        if (-not [string]::IsNullOrWhiteSpace($Model)) {
            $NativeArguments += @('--model', $Model)
        }
        if (-not [string]::IsNullOrWhiteSpace($Motion)) {
            $NativeArguments += @('--motion', $Motion)
        }

        Write-Host "==> Python ctypes 单 Context demo：$Frames 帧" -ForegroundColor Cyan
        Push-Location $ProjectRoot
        try {
            Invoke-LoggedNative `
                $PythonCommand `
                ($PythonPrefix + $NativeArguments) `
                (Join-Path $NativeOutput 'run.log')
        }
        finally {
            Pop-Location
        }
        if ($CaptureSource -ne 'none') {
            Show-CaptureSummary $NativeOutput
        }

        $MultiOutput = Join-Path $OutputRoot 'native-multi'
        Reset-Directory $MultiOutput
        if ($CaptureSource -eq 'none') {
            Remove-Item Env:WISTERIA_SCREENSHOT_DIR -ErrorAction SilentlyContinue
        }
        else {
            $env:WISTERIA_SCREENSHOT_DIR = $MultiOutput
        }
        $DestroyAt = [Math]::Max(30, [Math]::Floor($Frames / 2))
        $MultiArguments = @(
            'examples/python/native_multi_context_demo.py',
            '--frames', "$Frames",
            '--destroy-first-at', "$DestroyAt"
        )
        if (-not [string]::IsNullOrWhiteSpace($Model)) {
            $MultiArguments += @('--model', $Model)
        }
        if (-not [string]::IsNullOrWhiteSpace($Motion)) {
            $MultiArguments += @('--motion', $Motion)
        }

        Write-Host '==> Python ctypes 双 Context 生命周期 demo' -ForegroundColor Cyan
        Push-Location $ProjectRoot
        try {
            Invoke-LoggedNative `
                $PythonCommand `
                ($PythonPrefix + $MultiArguments) `
                (Join-Path $MultiOutput 'run.log')
        }
        finally {
            Pop-Location
        }
        if ($CaptureSource -ne 'none') {
            Show-CaptureSummary $MultiOutput
        }
    }
}
finally {
    foreach ($Name in $OldEnvironment.Keys) {
        Set-Item -Path "Env:$Name" -Value $OldEnvironment[$Name] -ErrorAction SilentlyContinue
        if ($null -eq $OldEnvironment[$Name]) {
            Remove-Item -Path "Env:$Name" -ErrorAction SilentlyContinue
        }
    }
}

Write-Host "验证完成。截图与日志目录：$OutputRoot" -ForegroundColor Green
