param([Parameter(Mandatory = $true)][string]$QtRoot)

# 在 x64 Visual Studio Developer PowerShell 中运行；仅生成 x64 下的测试产物。
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$testOutput = Join-Path $repo 'x64/CutSurfacePreviewTests'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$previousPath = $env:PATH
Push-Location $testOutput
try {
    $env:PATH = "$QtRoot/bin;$env:PATH"
    & "$QtRoot/bin/moc.exe" "$repo/include/ui/widgets/MachiningSettingsWidget.h" -o moc_MachiningSettingsWidget.cpp
    if ($LASTEXITCODE -ne 0) { throw 'Qt moc failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MD /utf-8 /W3 /Zc:__cplusplus /permissive- `
        "/I$repo/include" "/I$QtRoot/include" "/I$QtRoot/include/QtCore" `
        "/I$QtRoot/include/QtGui" "/I$QtRoot/include/QtWidgets" `
        "$repo/tests/CutSurfacePreviewTests.cpp" `
        "$repo/src/core/machining/CutSurfaceExpectation.cpp" `
        "$repo/src/core/machining/TubeSectionProjector.cpp" `
        "$repo/src/core/machine/RotaryKinematics.cpp" `
        "$repo/src/ui/widgets/MachiningSettingsWidget.cpp" moc_MachiningSettingsWidget.cpp `
        /Fe:CutSurfacePreviewTests.exe /link "/LIBPATH:$QtRoot/lib" Qt6Core.lib Qt6Gui.lib Qt6Widgets.lib
    if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
    & ./CutSurfacePreviewTests.exe -platform offscreen
    if ($LASTEXITCODE -ne 0) { throw 'Cut surface preview regression failed.' }
}
finally {
    $env:PATH = $previousPath
    Pop-Location
}
