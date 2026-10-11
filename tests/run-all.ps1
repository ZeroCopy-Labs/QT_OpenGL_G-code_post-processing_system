param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [Parameter(Mandatory = $true)][string]$OpenCVRoot,
    [switch]$SkipBuild
)

# 统一测试入口：先在项目根目录构建 Release|x64，再依次运行两个测试套件。
# 需在 x64 Visual Studio Developer PowerShell 中运行（cl.exe / MSBuild.exe 在 PATH）。
# 测试设置会隔离到工作区内的临时目录，不会读写用户真实设置。
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

# 使用工作区内的临时目录，避免系统 TEMP 不可用时 QTemporaryDir 创建失败。
$tempRoot = Join-Path $repo 'x64\test-temp'
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
$env:TEMP = $tempRoot
$env:TMP = $tempRoot
$env:TMPDIR = $tempRoot

if (-not $SkipBuild)
{
    $msbuild = Get-Command 'MSBuild.exe' -ErrorAction SilentlyContinue
    if ($null -eq $msbuild)
    {
        throw '未找到 MSBuild.exe：请在 x64 Visual Studio Developer PowerShell 中运行，或先自行构建并加 -SkipBuild。'
    }

    & $msbuild.Source (Join-Path $repo 'G-code_post-processing_system.slnx') `
        /m /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0)
    {
        throw "Release|x64 构建失败（exit $LASTEXITCODE）。"
    }
}

# .slnx 以项目 Id 生成中间目录（G-code_p.<id8>），这里自动定位 Release 对象目录。
$objectDirectory = Get-ChildItem -Path $repo -Directory -Filter 'G-code_p.*' -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName 'x64\Release' } |
    Where-Object { (Get-ChildItem -Path $_ -Filter '*.obj' -ErrorAction SilentlyContinue | Measure-Object).Count -gt 0 } |
    Select-Object -First 1
if (-not $objectDirectory)
{
    throw '未找到中间对象目录（G-code_p.*\x64\Release\*.obj）：请先完整构建一次 Release|x64。'
}

$suites = @(
    @{
        Name = 'CutSurfacePreviewTests'
        Script = 'run-cut-surface-preview.ps1'
        Arguments = @{ QtRoot = $QtRoot }
    },
    @{
        Name = 'TubeWallThicknessFlowTests'
        Script = 'run-wall-thickness-flow.ps1'
        Arguments = @{ QtRoot = $QtRoot; OpenCVRoot = $OpenCVRoot; ObjectDirectory = $objectDirectory }
    }
)

$failed = New-Object System.Collections.Generic.List[string]
foreach ($suite in $suites)
{
    Write-Host "==== $($suite.Name) ====" -ForegroundColor Cyan
    try
    {
        # 哈希表需用变量名散开（@变量名），不能包成数组。
        $suiteArguments = $suite.Arguments
        & (Join-Path $PSScriptRoot $suite.Script) @suiteArguments
        if ($LASTEXITCODE -ne 0)
        {
            $failed.Add("$($suite.Name) (exit $LASTEXITCODE)")
        }
    }
    catch
    {
        Write-Host $_.Exception.Message -ForegroundColor Red
        $failed.Add("$($suite.Name)（$($_.Exception.Message)）")
    }
}

Write-Host ''
if ($failed.Count -eq 0)
{
    Write-Host '全部测试套件通过。' -ForegroundColor Green
    exit 0
}

Write-Host ('失败套件：' + ($failed -join '；')) -ForegroundColor Red
exit 1
