// Offline test utility: actual GCN-produced SPIR-V -> Mesa linked DXIL -> SDK validator.
#include <windows.h>

#include "spirv_to_dxil.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <dxcapi.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
static void Check(bool ok, const char* message) {
	if (!ok) {
		std::fprintf(stderr, "PS4_GCN_DXIL FAIL: %s\n", message);
		std::_Exit(1);
	}
}
class TestBlob final: public IDxcBlob {
	std::atomic<ULONG> refs {1};
	void*              data;
	size_t             bytes;

public:
	TestBlob(void* data, size_t bytes): data(data), bytes(bytes) {}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override {
		if (!output) return E_POINTER;
		*output = nullptr;
		if (id != __uuidof(IUnknown) && id != __uuidof(IDxcBlob)) return E_NOINTERFACE;
		*output = this;
		AddRef();
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
	ULONG STDMETHODCALLTYPE Release() override {
		const auto n = --refs;
		if (!n) delete this;
		return n;
	}
	void* STDMETHODCALLTYPE  GetBufferPointer() override { return data; }
	SIZE_T STDMETHODCALLTYPE GetBufferSize() override { return bytes; }
};
static std::vector<uint32_t> Read(const std::filesystem::path& file) {
	std::ifstream stream(file, std::ios::binary | std::ios::ate);
	const auto    size = stream.tellg();
	Check(size > 0 && size % 4 == 0 && size < 16 * 1024 * 1024, "SPIR-V size");
	std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(words.data()), size);
	Check(bool(stream), "read SPIR-V");
	return words;
}
int wmain(int argc, wchar_t** argv) {
	Check(argc == 3, "expected fixture folder and SDK dxil.dll");
	const std::filesystem::path folder  = argv[1];
	const auto                  library = LoadLibraryW(argv[2]);
	Check(library != nullptr, "load validator");
	const auto create =
	    reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(library, "DxcCreateInstance"));
	Check(create != nullptr, "validator factory");
	ComPtr<IDxcValidator> validator;
	Check(SUCCEEDED(create(CLSID_DxcValidator, IID_PPV_ARGS(&validator))), "create validator");
	Check(spirv_to_dxil_get_runtime_conf_size() == sizeof(dxil_spirv_runtime_conf),
	      "Mesa config ABI");
	for (int variant = 0; variant < 17; ++variant) {
		const auto              vs_name = variant == 16   ? "gcn-texture.vert"
		                                  : variant == 13 ? "gcn-fetch.vert"
		                                  : variant >= 14 ? "gcn-small.vert"
		                                  : variant == 12 ? "gcn-perspective.vert"
		                                  : variant == 10 ? "gcn-many.vert"
		                                  : variant == 11 ? "gcn-small.vert"
		                                  : variant == 9  ? "gcn-small.vert"
		                                  : variant == 8  ? "gcn-many.vert"
		                                  : variant == 5  ? "gcn-interp.vert"
		                                  : variant >= 6  ? "gcn-perspective.vert"
		                                  : variant == 0  ? "gcn.vert"
		                                                  : "gcn-small.vert";
		const auto              ps_name = variant == 16   ? "gcn-texture.frag"
		                                  : variant == 13 ? "gcn-interp.frag"
		                                  : variant == 14 ? "gcn-indexed.frag"
		                                  : variant == 15 ? "gcn-indexed-dword.frag"
		                                  : variant == 12 ? "gcn-linear.frag"
		                                  : variant == 10 ? "gcn-remap.frag"
		                                  : variant == 11 ? "gcn-unary.frag"
		                                  : variant == 9  ? "gcn-buffer.frag"
		                                  : variant == 8  ? "gcn-param3.frag"
		                                  : variant == 7  ? "gcn-alu.frag"
		                                  : variant >= 5  ? "gcn-interp.frag"
		                                  : variant == 2  ? "gcn-alpha.frag"
		                                  : variant == 3  ? "gcn-uniform0.frag"
		                                  : variant == 4  ? "gcn-uniform1.frag"
		                                                  : "gcn.frag";
		const auto              vs      = Read(folder / (std::string(vs_name) + ".spv"));
		const auto              ps      = Read(folder / (std::string(ps_name) + ".spv"));
		dxil_spirv_runtime_conf conf[2] {};
		for (auto& c: conf) {
			c.shader_model_max                    = SHADER_MODEL_6_4;
			c.runtime_data_cbv.register_space     = 1001;
			c.push_constant_cbv.register_space    = 1000;
			c.lower_16bit_ops                     = true;
			c.compact_vertex_runtime_data         = true;
			c.first_vertex_and_base_instance_mode = DXIL_SPIRV_SYSVAL_TYPE_RUNTIME_DATA;
		}
		conf[0].yz_flip.mode = DXIL_SPIRV_YZ_FLIP_CONDITIONAL;
		dxil_spirv_stage stages[2] {};
		stages[0].words      = vs.data();
		stages[0].word_count = vs.size();
		stages[0].stage      = DXIL_SPIRV_SHADER_VERTEX;
		stages[1].words      = ps.data();
		stages[1].word_count = ps.size();
		stages[1].stage      = DXIL_SPIRV_SHADER_FRAGMENT;
		for (int i = 0; i < 2; ++i) {
			stages[i].conf             = &conf[i];
			stages[i].entry_point_name = "main";
		}
		dxil_spirv_debug_options debug {};
		dxil_spirv_logger        logger {nullptr,
                                  [](void*, const char* text) { std::fputs(text, stderr); }};
		dxil_spirv_object        objects[2] {};
		Check(spirv_to_dxil_linked(stages, 2, DXIL_VALIDATOR_1_8, &debug, &logger, objects),
		      "linked GCN shaders");
		for (int i = 0; i < 2; ++i) {
			ComPtr<IDxcBlob> blob;
			blob.Attach(new TestBlob(objects[i].binary.buffer, objects[i].binary.size));
			ComPtr<IDxcOperationResult> result;
			Check(
			    SUCCEEDED(validator->Validate(blob.Get(), DxcValidatorFlags_InPlaceEdit, &result)),
			    "validate DXIL call");
			HRESULT status;
			Check(SUCCEEDED(result->GetStatus(&status)), "get DXIL status");
			if (FAILED(status)) {
				ComPtr<IDxcBlobEncoding> errors;
				result->GetErrorBuffer(&errors);
				if (errors)
					std::fwrite(errors->GetBufferPointer(), 1, errors->GetBufferSize(), stderr);
			}
			Check(SUCCEEDED(status), "signed DXIL validation");
			std::ofstream out(folder / (std::string(i == 0 ? vs_name : ps_name) + ".dxil"),
			                  std::ios::binary);
			out.write(static_cast<const char*>(objects[i].binary.buffer), objects[i].binary.size);
			Check(bool(out), "save DXIL");
			std::printf("PS4_GCN_DXIL %s bytes=%zu SM6.4 validator=PASS\n",
			            i == 0 ? vs_name : ps_name, objects[i].binary.size);
		}
		for (auto& object: objects)
			spirv_to_dxil_free(&object);
	}
	validator.Reset();
	FreeLibrary(library);
	return 0;
}
