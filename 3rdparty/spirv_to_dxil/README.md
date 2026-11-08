# spirv_to_dxil

The D3D12 backend turns the SPIR-V that Kyty's shader recompiler makes into DXIL with Mesa's standalone `spirv_to_dxil`, and signs the result with the Windows SDK's `dxil.dll` validator. Nothing of Mesa is involved in rendering: the translation runs when a shader is first used.

`spirv_to_dxil.patch` goes on Mesa (26.2.0-devel, commit `e24dc5bd1e7fe6101bdc866fb16a15a8fcae1aae`; `git apply`). It adds:

- `spirv_to_dxil_linked`, which translates the stages of a graphics pipeline together (the DXIL signatures of consecutive stages only match when the stages are linked)
- `SV_Barycentrics` pixel shader inputs, and per-vertex ones (`PerVertexKHR`) read with `AttributeAtVertex` (shader model 6.1; the Xbox does not report support for them)
- the `clip_halfz`, `force_sample_rate_shading` and `shadow_sampler_space_offset` runtime options
- mesh shaders (shader model 6.5; not available on the Xbox): output counts, per-vertex and per-primitive outputs, triangle indices, `SV_CullPrimitive`
- fixes for DXIL validator 1.8 and for wave operations in helper lanes
- Y and Z flips per viewport: a shader that writes the viewport index takes that viewport's bit of the flip masks
- half to 32-bit integer conversions without 16-bit ops: NIR folds the shader's unpack to a float away, which left a 16-bit float conversion a device without 16-bit ops (the Xbox) rejects

Build it once, with the static C runtime so that the DLL imports nothing a UWP app does not have (it imports only `KERNEL32.dll` and an API set):

```
meson setup <build> <mesa> --backend=ninja --wrap-mode=nodownload -Dbuildtype=release
  -Db_ndebug=true -Db_vscrt=mt -Dplatforms=windows -Dgallium-drivers= -Dvulkan-drivers=microsoft-experimental
  -Dspirv-to-dxil=true -Dllvm=disabled -Dglx=disabled -Degl=disabled -Dgbm=disabled
  -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dspirv-tools=disabled -Dbuild-tests=false
  -Dvideo-codecs= -Dmin-windows-version=10 -Dzlib=disabled -Dzstd=disabled
ninja -C <build> src/microsoft/spirv_to_dxil/spirv_to_dxil.dll
```

Mesa needs its DirectX-Headers subproject present in `subprojects/` (no downloads). Copy `spirv_to_dxil.h` and `dxil_versions.h` (from `src/microsoft/spirv_to_dxil` and `src/microsoft/compiler`) to `<root>/include`, the import library to `<root>/lib/spirv_to_dxil.lib` and the DLL to `<root>/bin`, then configure Kyty with `-DKYTY_SPIRV_TO_DXIL_ROOT=<root>` (`uwp.ps1 configure` takes it from `$env:KYTY_SPIRV_TO_DXIL_ROOT`). The build puts `spirv_to_dxil.dll` and the SDK's `dxil.dll` next to the executable (for the UWP app, into the package layout).

## Mesa-UWP 26.2.2 local source

For `C:/Users/rodri/Dev1/Projetos/Mesa-UWP` at revision
`e1d9d2b601230c490ed803776a67199e06d87243`, use the cumulative
`mesa-uwp-e1d9d2b601.patch` on a separate source copy. It contains the Kyty
translator extensions above and preserves that fork's additional tessellation
configuration. The original Mesa checkout is left unchanged.

The port also enables full NIR for a standalone translator build (without a
Vulkan driver) and selects the compiler-verified atomic builtins for clang-cl.
References to fork-only NIR members/opcodes missing from this checkout are guarded:
`allocation_failed`, `fadd_rtne`, `input_attachment_index`, and
`lower_amd_gcn_shader_clock`. Those guards do not supply their missing NIR
implementations. In particular this build provides no recoverable-NIR-OOM guarantee.
Keep the corresponding `MESA_UWP_NIR_*` feature macros undefined for this snapshot.

The fork's vertex runtime structure is 200 bytes; Kyty supplies its 36-byte prefix.
The port adds an explicit `compact_vertex_runtime_data` mode and the exported
`spirv_to_dxil_get_vertex_runtime_size` query. Kyty checks the size and shared
field offsets while retaining its 12-dword root-constant allocation. The translator
rejects runtime-buffer accesses beyond the compact prefix. Constant viewport 0
uses the prefix width/height; shaders requiring additional viewports retain the
full ABI and are rejected by Kyty's compact mode. The default Mesa ABI remains
200 bytes. This does not implement those additional runtime fields in Kyty.

Build using the options above with `-Dvulkan-drivers=`. The standalone Meson fix
in this cumulative patch makes that possible. The build used clang-cl 22.1.3,
Meson 1.11.2, x64, Release and static `/MT`, with `--wrap-mode=nodownload` and no
`meson install`. An existing Python with Meson/Mako and an existing Visual Studio /
Windows SDK are required. Preserve the original file copyright/license headers in
the source copy.

The local distribution is assembled as:

- `include/spirv_to_dxil.h` from the patched source.
- `include/dxil_versions.h` from `src/microsoft/compiler`.
- `lib/spirv_to_dxil.lib` and `bin/spirv_to_dxil.dll` from the build.

The verified distribution for this workspace is
`build/stage3/spirv_to_dxil-dist`; `manifest.json` records source revision, patch
hash, compiler and artifact hashes. Its translator version `524f3d7345` is the
modified-source fingerprint used during the build, rather than the upstream Git
revision. Full build/configuration scripts and logs used for this local build are
under `build/stage3`.

Run the CPU smoke test with the existing `glslangValidator` and Windows SDK:

```powershell
3rdparty/spirv_to_dxil/smoke-test.ps1 -TranslatorRoot build/stage3/spirv_to_dxil-dist
```

It checks the DLL/header configuration size, translates a compute shader, calls
`spirv_to_dxil_linked` on a vertex/pixel pair, rejects an empty pipeline, and validates
all five DXIL objects with the SDK validator, including compact and full geometry
shaders. A geometry shader requiring viewport 1 must be rejected in compact mode. It writes only into its build output
folder and installs nothing. This verifies shader translation, not GPU rendering or
Xbox execution. The DLL also passed the project's named-import check against
Windows SDK `WindowsApp.lib`.

Configure UWP with the distribution root and the already available dependencies:

```powershell
src/uwp/uwp.ps1 configure -BuildDir build/stage3-uwp `
  -SpirvToDxilRoot build/stage3/spirv_to_dxil-dist `
  -PythonExecutable C:/msys64/ucrt64/bin/python.exe `
  -WinUIRoot C:/Users/rodri/Dev1/Projetos/teste1/microsoft.ui.xaml.2.8.7 `
  -WebView2Root C:/Users/rodri/Dev1/Projetos/teste1/microsoft.web.webview2.1.0.2849.39
```

The UWP script accepts either `-SpirvToDxilRoot` or the existing
`KYTY_SPIRV_TO_DXIL_ROOT` environment variable, and verifies the header, versions
header, import library, DLL and linked-stage API before configuring. A Mesa source
folder alone is not a distribution root. `-PythonExecutable` supplies an existing
interpreter when CMake's automatic host-Python search fails.
