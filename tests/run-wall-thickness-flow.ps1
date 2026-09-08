param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [Parameter(Mandatory = $true)][string]$OpenCVRoot,
    [Parameter(Mandatory = $true)][string]$ObjectDirectory
)

# 先构建 x64 Release，再从 x64 VS Developer PowerShell 运行。
# 复用生产对象文件，只替换 main；不需要改动应用的私有成员可见性。
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$testOutput = Join-Path $repo 'x64/TubeWallThicknessFlowTests'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$OpenCVRoot = (Resolve-Path -LiteralPath $OpenCVRoot).Path
$ObjectDirectory = (Resolve-Path -LiteralPath $ObjectDirectory).Path
$objects = @(Get-ChildItem -LiteralPath $ObjectDirectory -Filter '*.obj' |
    Where-Object Name -ne 'main.obj' | ForEach-Object FullName)
$previousPath = $env:PATH
Push-Location $testOutput
try {
    $env:PATH = "$QtRoot/bin;$repo/x64/Release;$env:PATH"
    & cl.exe /nologo /std:c++17 /EHsc /MD /utf-8 /Zc:__cplusplus /permissive- `
        "/I$repo/include" "/I$repo/include/libdxfrw" "/I$ObjectDirectory/qt/uic" `
        "/I$QtRoot/include" "/I$QtRoot/include/QtCore" "/I$QtRoot/include/QtGui" `
        "/I$QtRoot/include/QtWidgets" "/I$QtRoot/include/QtOpenGL" "/I$QtRoot/include/QtOpenGLWidgets" `
        "$repo/tests/TubeWallThicknessFlowTests.cpp" @objects /Fe:TubeWallThicknessFlowTests.exe `
        /link "/LIBPATH:$QtRoot/lib" "/LIBPATH:$OpenCVRoot/build/x64/vc16/lib" `
        Qt6Core.lib Qt6Gui.lib Qt6Widgets.lib Qt6OpenGL.lib Qt6OpenGLWidgets.lib `
        opencv_world4110.lib user32.lib gdi32.lib shell32.lib advapi32.lib crypt32.lib ole32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Flow test compilation failed.' }
    & ./TubeWallThicknessFlowTests.exe -platform offscreen
    if ($LASTEXITCODE -ne 0) { throw 'Wall thickness flow regression failed.' }
}
finally {
    $env:PATH = $previousPath
    Pop-Location
}
