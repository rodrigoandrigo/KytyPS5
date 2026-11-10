param(
    [Parameter(Mandatory)][string]$FixtureRoot,
    [Parameter(Mandatory)][string]$TranslatorRoot,
    [string]$VulkanRoot = $env:VULKAN_SDK,
    [string]$OutputDir = 'build/ps4-gcn-gpu'
)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$FixtureRoot = (Resolve-Path -LiteralPath $FixtureRoot).Path
$TranslatorRoot = (Resolve-Path -LiteralPath $TranslatorRoot).Path
if (-not $VulkanRoot) { throw 'Supply the existing Vulkan SDK with -VulkanRoot; nothing will be installed.' }
$vs = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -property installationPath
if (-not $vs) { throw 'An existing Visual Studio with clang-cl and Windows SDK is required.' }
& "$vs/Common7/Tools/Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$compiler = "$vs/VC/Tools/Llvm/x64/bin/clang-cl.exe"
$validator = "$env:WindowsSdkDir/bin/$($env:WindowsSDKVersion.TrimEnd('\'))/x64/dxil.dll"
foreach ($file in @($compiler, $validator, "$VulkanRoot/Include/vulkan/vulkan.h", "$VulkanRoot/Lib/vulkan-1.lib",
                    "$TranslatorRoot/include/spirv_to_dxil.h", "$TranslatorRoot/lib/spirv_to_dxil.lib", "$TranslatorRoot/bin/spirv_to_dxil.dll",
                    "$FixtureRoot/gcn-cfg-exit0.frag.spv", "$FixtureRoot/gcn-cfg-exit1.frag.spv",
                    "$FixtureRoot/gcn-cfg-frame0.frag.spv", "$FixtureRoot/gcn-cfg-frame1.frag.spv",
                    "$FixtureRoot/gcn-raw-load.frag.spv",
                    "$FixtureRoot/gcn-raw-sgpr.frag.spv",
                    "$FixtureRoot/gcn-raw-short1.frag.spv", "$FixtureRoot/gcn-raw-short2.frag.spv",
                    "$FixtureRoot/gcn-three.vert.spv", "$FixtureRoot/gcn-three.frag.spv",
                    "$FixtureRoot/gcn-mubuf-soffset.frag.spv",
                    "$FixtureRoot/gcn-mubuf-table.frag.spv",
                    "$FixtureRoot/gcn-mubuf-join.frag.spv",
                    "$FixtureRoot/gcn-mubuf-loop.frag.spv",
                    "$FixtureRoot/gcn-two-images.vert.spv", "$FixtureRoot/gcn-two-images.frag.spv",
                    "$FixtureRoot/gcn-image-srt-load.frag.spv", "$FixtureRoot/gcn-image-srt-sample.frag.spv",
                    "$FixtureRoot/gcn-eight.vert.spv", "$FixtureRoot/gcn-eight.frag.spv",
                    "$FixtureRoot/gcn-srt-store.frag.spv",
                    "$FixtureRoot/gcn-icmp0.frag.spv", "$FixtureRoot/gcn-icmp1.frag.spv", "$FixtureRoot/gcn-icmp2.frag.spv", "$FixtureRoot/gcn-icmp3.frag.spv", "$FixtureRoot/gcn-icmp4.frag.spv", "$FixtureRoot/gcn-icmp5.frag.spv", "$FixtureRoot/gcn-icmp6.frag.spv", "$FixtureRoot/gcn-else0.frag.spv", "$FixtureRoot/gcn-else1.frag.spv", "$FixtureRoot/gcn-else2.frag.spv", "$FixtureRoot/gcn-else3.frag.spv", "$FixtureRoot/gcn-else4.frag.spv", "$FixtureRoot/gcn-mask.frag.spv", "$FixtureRoot/gcn-mask-nested.frag.spv", "$FixtureRoot/gcn-select.frag.spv", "$FixtureRoot/gcn-external-fetch.vert.spv", "$FixtureRoot/gcn-flat.frag.spv", "$FixtureRoot/gcn-default0.frag.spv", "$FixtureRoot/gcn-default1.frag.spv", "$FixtureRoot/gcn-default2.frag.spv", "$FixtureRoot/gcn-default3.frag.spv", "$FixtureRoot/gcn-table-sample.vert.spv", "$FixtureRoot/gcn-table-sample.frag.spv", "$FixtureRoot/gcn-sample.vert.spv", "$FixtureRoot/gcn-sample.frag.spv", "$FixtureRoot/gcn.vert.spv", "$FixtureRoot/gcn-small.vert.spv", "$FixtureRoot/gcn.frag.spv", "$FixtureRoot/gcn-alpha.frag.spv", "$FixtureRoot/gcn-uniform0.frag.spv", "$FixtureRoot/gcn-uniform1.frag.spv", "$FixtureRoot/gcn-interp.vert.spv", "$FixtureRoot/gcn-perspective.vert.spv", "$FixtureRoot/gcn-interp.frag.spv", "$FixtureRoot/gcn-alu.frag.spv", "$FixtureRoot/gcn-many.vert.spv", "$FixtureRoot/gcn-param3.frag.spv", "$FixtureRoot/gcn-buffer.frag.spv", "$FixtureRoot/gcn-linear.frag.spv", "$FixtureRoot/gcn-remap.frag.spv", "$FixtureRoot/gcn-unary.frag.spv", "$FixtureRoot/gcn-fetch.vert.spv", "$FixtureRoot/gcn-indexed.frag.spv", "$FixtureRoot/gcn-indexed-dword.frag.spv", "$FixtureRoot/gcn-texture.vert.spv", "$FixtureRoot/gcn-texture.frag.spv")) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing existing artifact: $file" }
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
function Invoke-GcnTool([string]$ToolPath, [string[]]$ToolArguments) {
    & $ToolPath @ToolArguments
    if ($LASTEXITCODE -ne 0) { throw "GCN test tool failed: $ToolPath ($LASTEXITCODE)" }
}
Invoke-GcnTool "$VulkanRoot/Bin/glslangValidator.exe" @('--target-env', 'vulkan1.2', '-V',
    '-o', "$FixtureRoot/gcn-raster-storage.frag.spv", "$PSScriptRoot/Ps4RasterStorage.frag")
$common = @('/nologo', '/std:c++20', '/EHsc', '/MT', '/DNOMINMAX', "/I$workspace/src", "/Fo$OutputDir/")
Invoke-GcnTool $compiler ($common + @("/I$TranslatorRoot/include", "$PSScriptRoot/Ps4GcnDxilTests.cpp",
    "/Fe$OutputDir/gcn-dxil.exe", '/link', "$TranslatorRoot/lib/spirv_to_dxil.lib"))
Copy-Item -LiteralPath "$TranslatorRoot/bin/spirv_to_dxil.dll" -Destination "$OutputDir/spirv_to_dxil.dll" -Force
Invoke-GcnTool "$OutputDir/gcn-dxil.exe" @($FixtureRoot, $validator)
Invoke-GcnTool $compiler ($common + @('/DPS4_RASTER_D3D12', "$PSScriptRoot/Ps4RasterTests.cpp",
    "$workspace/src/graphics/guest_gpu/ps4Transfer.cpp", "$workspace/src/graphics/host_gpu/d3d12/ps4RasterProbe.cpp", "/Fe$OutputDir/gcn-raster-d3d12.exe",
    '/link', 'd3d12.lib', 'dxgi.lib', 'dxguid.lib'))
Invoke-GcnTool "$OutputDir/gcn-raster-d3d12.exe" @($FixtureRoot)
Invoke-GcnTool $compiler ($common + @("/I$VulkanRoot/Include", "$PSScriptRoot/Ps4RasterTests.cpp",
    "$workspace/src/graphics/guest_gpu/ps4Transfer.cpp", "$workspace/src/graphics/host_gpu/renderer/ps4TransferProbe.cpp",
    "/Fe$OutputDir/gcn-raster-vulkan.exe", '/link', "$VulkanRoot/Lib/vulkan-1.lib"))
Invoke-GcnTool "$OutputDir/gcn-raster-vulkan.exe" @($FixtureRoot)
Write-Host 'GCN SPIR-V/DXIL raster probes: PASS. No packages were installed or deployed.'
