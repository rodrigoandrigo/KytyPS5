#include <windows.h>

#include "spirv_to_dxil.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dxcapi.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
static void Check(bool value, const char *message) {
  if (!value) {
    std::fprintf(stderr, "spirv_to_dxil smoke: %s\n", message);
    std::_Exit(1);
  }
}
class Blob final : public IDxcBlob {
  std::atomic<ULONG> refs{1};
  void *data;
  size_t size;

public:
  Blob(void *data, size_t size) : data(data), size(size) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **output) override {
    if (!output)
      return E_POINTER;
    *output = nullptr;
    if (id != __uuidof(IUnknown) && id != __uuidof(IDxcBlob))
      return E_NOINTERFACE;
    *output = this;
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    auto count = --refs;
    if (!count)
      delete this;
    return count;
  }
  void *STDMETHODCALLTYPE GetBufferPointer() override { return data; }
  SIZE_T STDMETHODCALLTYPE GetBufferSize() override { return size; }
};
static std::vector<uint32_t> Read(const std::filesystem::path &file) {
  std::ifstream in(file, std::ios::binary | std::ios::ate);
  Check(in.good(), "open SPIR-V");
  auto size = in.tellg();
  Check(size > 0 && size % 4 == 0, "SPIR-V length");
  std::vector<uint32_t> words(size / 4);
  in.seekg(0);
  in.read(reinterpret_cast<char *>(words.data()), size);
  Check(in.good(), "read SPIR-V");
  return words;
}
static bool compact_rejected = false;
static void Log(void *, const char *text) {
  if (std::strstr(text, "outside the compact vertex runtime ABI"))
    compact_rejected = true;
  std::fprintf(stderr, "Mesa: %s", text);
}
int wmain(int argc, wchar_t **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  Check(argc == 3, "expected fixture folder and SDK dxil.dll");
  const std::filesystem::path root = argv[1];
  auto module = LoadLibraryW(argv[2]);
  Check(module != nullptr, "load SDK validator");
  auto create = reinterpret_cast<DxcCreateInstanceProc>(
      GetProcAddress(module, "DxcCreateInstance"));
  Check(create != nullptr, "validator export");
  ComPtr<IDxcValidator> validator;
  Check(SUCCEEDED(create(CLSID_DxcValidator, IID_PPV_ARGS(&validator))),
        "create validator");
  Check(spirv_to_dxil_get_runtime_conf_size() ==
            sizeof(dxil_spirv_runtime_conf),
        "header/DLL config size matches");
  auto validate = [&](dxil_spirv_object &object, const char *name) {
    Check(object.binary.buffer && object.binary.size, "nonempty DXIL");
    ComPtr<IDxcBlob> blob;
    blob.Attach(new Blob(object.binary.buffer, object.binary.size));
    ComPtr<IDxcOperationResult> result;
    Check(SUCCEEDED(validator->Validate(
              blob.Get(), DxcValidatorFlags_InPlaceEdit, &result)),
          "validate call");
    HRESULT status = E_FAIL;
    Check(SUCCEEDED(result->GetStatus(&status)), "validation status");
    if (FAILED(status)) {
      ComPtr<IDxcBlobEncoding> errors;
      result->GetErrorBuffer(&errors);
      if (errors)
        std::fwrite(errors->GetBufferPointer(), 1, errors->GetBufferSize(),
                    stderr);
    }
    Check(SUCCEEDED(status), "DXIL validation passed");
    std::ofstream out(root / (std::string(name) + ".dxil"), std::ios::binary);
    out.write(static_cast<const char *>(object.binary.buffer),
              object.binary.size);
    Check(out.good(), "save validated DXIL");
    std::printf("SPIRV_TO_DXIL_SMOKE: %s bytes=%zu validator=PASS\n", name,
                object.binary.size);
  };
  dxil_spirv_runtime_conf conf{};
  conf.shader_model_max = SHADER_MODEL_6_4;
  conf.runtime_data_cbv.register_space = 1001;
  conf.push_constant_cbv.register_space = 1000;
  conf.declared_read_only_images_as_srvs = true;
  conf.lower_16bit_ops = true;
  conf.compact_vertex_runtime_data = true;
  conf.first_vertex_and_base_instance_mode =
      DXIL_SPIRV_SYSVAL_TYPE_RUNTIME_DATA;
  Check(spirv_to_dxil_get_vertex_runtime_size(&conf) == 36,
        "compact runtime layout is 36 bytes");
  dxil_spirv_logger logger{nullptr, Log};
  dxil_spirv_debug_options debug{};
  auto compute = Read(root / "compute.spv");
  dxil_spirv_object output{};
  Check(spirv_to_dxil(compute.data(), compute.size(), nullptr, 0,
                      DXIL_SPIRV_SHADER_COMPUTE, "main", DXIL_VALIDATOR_1_8,
                      &debug, &conf, &logger, &output),
        "compute translation");
  validate(output, "compute");
  spirv_to_dxil_free(&output);
  auto vertex = Read(root / "triangle.vert.spv"),
       fragment = Read(root / "triangle.frag.spv");
  auto vertex_conf = conf, fragment_conf = conf;
  vertex_conf.clip_halfz = true;
  vertex_conf.yz_flip.mode = DXIL_SPIRV_YZ_FLIP_CONDITIONAL;
  fragment_conf.force_sample_rate_shading = true;
  dxil_spirv_stage stages[2]{};
  stages[0].words = vertex.data();
  stages[0].word_count = vertex.size();
  stages[0].stage = DXIL_SPIRV_SHADER_VERTEX;
  stages[0].entry_point_name = "main";
  stages[0].conf = &vertex_conf;
  stages[1].words = fragment.data();
  stages[1].word_count = fragment.size();
  stages[1].stage = DXIL_SPIRV_SHADER_FRAGMENT;
  stages[1].entry_point_name = "main";
  stages[1].conf = &fragment_conf;
  dxil_spirv_object linked[2]{};
  Check(!spirv_to_dxil_linked(nullptr, 0, DXIL_VALIDATOR_1_8, &debug, &logger,
                              linked),
        "empty pipeline rejected");
  Check(spirv_to_dxil_linked(stages, 2, DXIL_VALIDATOR_1_8, &debug, &logger,
                             linked),
        "linked vertex/fragment translation");
  validate(linked[0], "linked-vs");
  validate(linked[1], "linked-ps");
  spirv_to_dxil_free(&linked[0]);
  spirv_to_dxil_free(&linked[1]);
  auto point = Read(root / "point.geom.spv"),
       extended = Read(root / "extended.geom.spv");
  dxil_spirv_object point_output{};
  Check(spirv_to_dxil(point.data(), point.size(), nullptr, 0,
                      DXIL_SPIRV_SHADER_GEOMETRY, "main", DXIL_VALIDATOR_1_8,
                      &debug, &conf, &logger, &point_output),
        "compact viewport-zero geometry");
  validate(point_output, "compact-gs");
  spirv_to_dxil_free(&point_output);
  compact_rejected = false;
  Check(!spirv_to_dxil(extended.data(), extended.size(), nullptr, 0,
                       DXIL_SPIRV_SHADER_GEOMETRY, "main", DXIL_VALIDATOR_1_8,
                       &debug, &conf, &logger, &point_output) &&
            compact_rejected,
        "extended viewport reads rejected by compact ABI");
  auto full_conf = conf;
  full_conf.compact_vertex_runtime_data = false;
  Check(spirv_to_dxil_get_vertex_runtime_size(&full_conf) ==
            sizeof(dxil_spirv_vertex_runtime_data),
        "full runtime layout preserved");
  Check(spirv_to_dxil(extended.data(), extended.size(), nullptr, 0,
                      DXIL_SPIRV_SHADER_GEOMETRY, "main", DXIL_VALIDATOR_1_8,
                      &debug, &full_conf, &logger, &point_output),
        "full viewport geometry");
  validate(point_output, "full-gs");
  spirv_to_dxil_free(&point_output);
  std::printf("SPIRV_TO_DXIL_SMOKE: ABI=%zu version=0x%llx PASS\n",
              sizeof(conf),
              static_cast<unsigned long long>(spirv_to_dxil_get_version()));
  return 0;
}
