param(
    [Parameter(Mandatory = $true)][string]$TranslatorRoot,
    [string]$OutputDir = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path "$PSScriptRoot/../..").Path
if (-not $OutputDir) { $OutputDir = "$projectRoot/build/spirv-to-dxil-smoke" }
$TranslatorRoot = (Resolve-Path -LiteralPath $TranslatorRoot).Path
$vs = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -property installationPath
if (-not $vs) { throw 'An existing Visual Studio installation is required.' }
& "$vs/Common7/Tools/Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$compiler = "$vs/VC/Tools/Llvm/x64/bin/clang-cl.exe"
$glslang = Get-Command glslangValidator -ErrorAction SilentlyContinue
if (-not $glslang -and (Test-Path -LiteralPath 'C:/msys64/ucrt64/bin/glslangValidator.exe')) {
    $glslang = Get-Item -LiteralPath 'C:/msys64/ucrt64/bin/glslangValidator.exe'
}
if (-not $glslang) { throw 'An existing glslangValidator is required; no tools are installed by this script.' }
$glslangPath = if ($glslang -is [System.Management.Automation.CommandInfo]) { $glslang.Source } else { $glslang.FullName }
$sdkVersion = $env:WindowsSDKVersion.TrimEnd('\')
$validator = "$env:WindowsSdkDir/bin/$sdkVersion/x64/dxil.dll"
foreach ($required in @($compiler, $validator, "$TranslatorRoot/include/spirv_to_dxil.h", "$TranslatorRoot/include/dxil_versions.h", "$TranslatorRoot/lib/spirv_to_dxil.lib", "$TranslatorRoot/bin/spirv_to_dxil.dll")) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing dependency: $required" }
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
function Invoke-SmokeTool([string]$ToolPath, [string[]]$ToolArguments) {
    & $ToolPath @ToolArguments
    if ($LASTEXITCODE -ne 0) { throw "Smoke tool failed: $ToolPath ($LASTEXITCODE)" }
}
Invoke-SmokeTool $glslangPath @('-V', "$PSScriptRoot/smoke.comp", '-o', "$OutputDir/compute.spv")
Invoke-SmokeTool $glslangPath @('-V', "$PSScriptRoot/smoke.vert", '-o', "$OutputDir/triangle.vert.spv")
Invoke-SmokeTool $glslangPath @('-V', "$PSScriptRoot/smoke.frag", '-o', "$OutputDir/triangle.frag.spv")
Invoke-SmokeTool $glslangPath @('-V', "$PSScriptRoot/smoke.geom", '-o', "$OutputDir/point.geom.spv")
Invoke-SmokeTool $glslangPath @('-V', "$PSScriptRoot/smoke-extended.geom", '-o', "$OutputDir/extended.geom.spv")
Invoke-SmokeTool $compiler @('/nologo', '/std:c++20', '/EHsc', '/MT', '/DNOMINMAX', "/I$TranslatorRoot/include", "$PSScriptRoot/smoke.cpp", "/Fo$OutputDir/smoke.obj", "/Fe$OutputDir/smoke.exe", '/link', "$TranslatorRoot/lib/spirv_to_dxil.lib")
Copy-Item -LiteralPath "$TranslatorRoot/bin/spirv_to_dxil.dll" -Destination "$OutputDir/spirv_to_dxil.dll" -Force
Invoke-SmokeTool "$OutputDir/smoke.exe" @($OutputDir, $validator)
