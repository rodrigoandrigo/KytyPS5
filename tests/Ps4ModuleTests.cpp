#include "Ps4ModuleTestFixture.h"
#include "common/virtualMemory.h"
#include "loader/ps4Module.h"
#include "loader/runtimeLinker.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void Check(bool value, const char* text) {
	if (!value) {
		std::fprintf(stderr, "Ps4ModuleTests: %s\n", text);
		std::abort();
	}
}
int calls          = 0;
int exits          = 0;
using ExitCallback = void(KYTY_SYSV_ABI*)();
void KYTY_SYSV_ABI ExitHook() {
	++exits;
}
int KYTY_SYSV_ABI ImportedReturn(Loader::EntryParams* params, ExitCallback exit) {
	++calls;
	if (!params || params->argc != 1 || std::strcmp(params->argv[0], "ps4-loader-test") != 0 ||
	    !exit)
		return -1;
	exit();
	return 42;
}
uint64_t Word(const Loader::Ps4Module& module, size_t offset) {
	uint64_t value;
	std::memcpy(&value, module.image.data() + offset, 8);
	return value;
}
} // namespace
int main() {
	using namespace Loader;
	using namespace Ps4Tests;
	SymbolDatabase      exports;
	const SymbolResolve identity {
	    "AAAAAAAAAAA", "libSceLoaderTest", 1, "libSceLoaderModule", 1, 0, SymbolType::Func};
	exports.Add(identity, reinterpret_cast<uint64_t>(&ImportedReturn));
	std::string error;
	const auto  bytes = Fixture();
	for (bool self: {false, true}) {
		const auto file   = self ? Self(bytes) : bytes;
		const auto module = PreparePs4Module(file, 0x400000, exports, error, 7);
		Check(module.has_value(), error.c_str());
		Check(module->entry == 0x400100 && !module->shared, "main entry and platform");
		Check(module->resolved_imports.size() == 1 &&
		          Word(*module, 0x4100) == reinterpret_cast<uint64_t>(&ImportedReturn),
		      "versioned import resolution");
		Check(Word(*module, 0x4110) == 7 && Word(*module, 0x4118) == 12 &&
		          Word(*module, 0x4130) == uint64_t(-24),
		      "TLS module/offset relocations");
		Check(Word(*module, 0x4120) == 0x404500 && Word(*module, 0x4128) == 0x404508,
		      "relative and defined-symbol relocations");
		Check(module->tls_template.size() == 32 && module->tls_alignment == 16 &&
		          module->tls_template[0] == 0x78 && module->tls_template[8] == 0,
		      "TLS initial data and zero fill");
		Check(module->initializers.size() == 1 && module->initializers[0] == 0x400000,
		      "zero DT_INIT is present");
		Check(module->image[0x5fff] == 0, "BSS zero fill");
#if defined(__x86_64__) || defined(_M_X64)
		using namespace Common::VirtualMemory;
		constexpr uint64_t base = 0x400000, size = 0x10000;
		Check(AllocFixed(base, size, Mode::ReadWrite), "reserve actual PS4 main base");
		std::memcpy(reinterpret_cast<void*>(base), module->image.data(), module->image.size());
		Check(Protect(base, 0x4000, Mode::ExecuteRead) && FlushInstructionCache(base, 0x4000),
		      "publish RX code without RWX");
		using Initializer = int(KYTY_SYSV_ABI*)();
		using Entry       = int(KYTY_SYSV_ABI*)(Loader::EntryParams*, ExitCallback);
		Check(reinterpret_cast<Initializer>(module->initializers[0])() == 7, "native initializer");
		Loader::EntryParams params {};
		params.argc       = 1;
		params.argv[0]    = "ps4-loader-test";
		const auto result = reinterpret_cast<Entry>(module->entry)(&params, ExitHook);
		Check(result == 42, "native PS4 entry calls resolved import");
		std::printf("PS4_ENTRY: container=%s base=0x%llx entry=0x%llx imports=1 tls=32 return=%d\n",
		            self ? "SELF" : "ELF", static_cast<unsigned long long>(base),
		            static_cast<unsigned long long>(module->entry), result);
		Check(Free(base, size), "release image");
#endif
	}
#if defined(__x86_64__) || defined(_M_X64)
	Check(calls == 2 && exits == 2, "both native entries forwarded arguments and exit callbacks");
#endif
	auto short_tls = bytes;
	Phdr(short_tls, 4, PT_TLS, 0x2800, 0x4800, 8, 16, PF_R, 16);
	const auto padded_tls = PreparePs4Module(short_tls, 0x400000, exports, error);
	Check(padded_tls && padded_tls->tls_memory_size == 16 && padded_tls->tls_static_size == 32 &&
	          padded_tls->tls_template.size() == 32,
	      "TLS TCB alignment preserves the declared bounds");
	const auto reject = [&](const std::vector<uint8_t>& file, const char* reason) {
		Check(!PreparePs4Module(file, 0x400000, exports, error) && !error.empty(), reason);
	};
	auto bad = bytes;
	bad[8]   = 2;
	reject(bad, "reject PS5");
	bad = bytes;
	Put(bad, 24, 0x4000, 8);
	reject(bad, "entry must be executable");
	bad = bytes;
	Put(bad, 64 + 56 + 16, 0x800, 8);
	reject(bad, "segment overlap");
	bad = bytes;
	Put(bad, 64 + 56 + 40, 0x10000000, 8);
	reject(bad, "bounded image allocation");
	bad = bytes;
	Tag(bad, 3, DT_OS_SYMTABSZ, 95);
	reject(bad, "symbol table width");
	bad = bytes;
	Tag(bad, 6, DT_OS_RELASZ, 143);
	reject(bad, "relocation table width");
	bad = bytes;
	Rela(bad, 0, 0x100, 7, 1, 0);
	reject(bad, "no text relocation writes");
	bad = bytes;
	Rela(bad, 0, 0x4100, 255, 1, 0);
	reject(bad, "unknown relocation");
	bad = bytes;
	Rela(bad, 0, 0x4100, 7, 99, 0);
	reject(bad, "symbol bounds");
	bad = bytes;
	Rela(bad, 1, 0x4100, 16, 0, 0);
	reject(bad, "duplicate relocation target");
	bad = bytes;
	Rela(bad, 3, 0x4120, 8, 0, INT64_MIN);
	reject(bad, "relative underflow");
	bad = bytes;
	Tag(bad, 8, DT_OS_IMPORT_LIB, (uint64_t(2) << 32) | 17);
	reject(bad, "library versions must match");
	bad = bytes;
	Phdr(bad, 4, PT_TLS, 0x2800, 0x4800, 8, 4, PF_R, 16);
	reject(bad, "TLS file size exceeds memory");
	bad = bytes;
	Tag(bad, 10, DT_INIT, 0x4000);
	reject(bad, "initializer must be executable");
	bad = bytes;
	Tag(bad, 11, DT_OS_STRSZ, 42);
	reject(bad, "duplicate dynamic tag");
	bad = bytes;
	Put(bad, Dynlib + 15, 0x41, 1);
	reject(bad, "import identity bounds");
	bad = bytes;
	Rela(bad, 0, 0x4100, 6, 1, 123);
	auto glob = PreparePs4Module(bad, 0x400000, exports, error);
	Check(glob && Word(*glob, 0x4100) == reinterpret_cast<uint64_t>(&ImportedReturn),
	      "GLOB_DAT ignores addend");
	bad = bytes;
	Tag(bad, 11, DT_FINI, 0);
	Tag(bad, 12, 0, 0);
	auto fini = PreparePs4Module(bad, 0x400000, exports, error);
	Check(fini && fini->finalizers.size() == 1 && fini->finalizers[0] == 0x400000,
	      "zero DT_FINI is present");
	bad = bytes;
	Phdr(bad, 0, PT_LOAD, 0x1000, 0, 0x1000, 0x1000, PF_R | PF_X, 0x10000);
	reject(bad, "load alignment must be valid");
	bad = bytes;
	Phdr(bad, 2, PT_DYNAMIC, Dynamic, 0, 17 * 16, 17 * 16, PF_R);
	Tag(bad, 6, DT_OS_RELASZ, 8 * 24);
	Rela(bad, 6, 0x4200, 8, 0, 0);
	Rela(bad, 7, 0x4208, 8, 0, 0x100);
	Tag(bad, 11, DT_INIT_ARRAY, 0x4200);
	Tag(bad, 12, DT_INIT_ARRAYSZ, 16);
	Tag(bad, 13, DT_FINI_ARRAY, 0x4200);
	Tag(bad, 14, DT_FINI_ARRAYSZ, 16);
	Tag(bad, 15, DT_FINI, 0);
	Tag(bad, 16, 0, 0);
	auto lifecycle = PreparePs4Module(bad, 0x400000, exports, error);
	Check(lifecycle &&
	          lifecycle->initializers == std::vector<uint64_t> {0x400000, 0x400000, 0x400100} &&
	          lifecycle->finalizers == std::vector<uint64_t> {0x400100, 0x400000, 0x400000},
	      "relocated init arrays and reverse finalizer order");
	bad = Self(bytes);
	Put(bad, 64, 0x800, 8);
	reject(bad, "duplicate SELF payload index");
	bad = bytes;
	Put(bad, Dynlib + 0x100 + 3 * 24 + 6, 0xfff2, 2);
	reject(bad, "SHN_COMMON cannot masquerade as a defined object");
	bad = bytes;
	Rela(bad, 1, 0x4110, 16, 2, 0);
	auto local_tls = PreparePs4Module(bad, 0x400000, exports, error, 9);
	Check(local_tls && Word(*local_tls, 0x4110) == 9,
	      "DTPMOD64 accepts a defined local TLS symbol");
	SymbolDatabase empty;
	Check(!PreparePs4Module(bytes, 0x400000, empty, error) &&
	          error.find("Unresolved PS4 import") != std::string::npos,
	      "no fallback to PS5 symbols");
	bad = bytes;
	Put(bad, Dynlib + 0x100 + 24 + 4, 0x22, 1);
	auto weak = PreparePs4Module(bad, 0x400000, empty, error);
	Check(weak && Word(*weak, 0x4100) == 0, "undefined weak import is null");
	bad = bytes;
	Put(bad, 16, ET_DYNAMIC, 2);
	Put(bad, 24, 0, 8);
	auto shared = PreparePs4Module(bad, 0x2000000, exports, error);
	Check(shared && shared->shared && shared->entry == 0 && shared->initializers[0] == 0x2000000,
	      "PRX layout and absent entry");
	for (size_t n: {size_t(0), size_t(63), size_t(200), size_t(0x3100)})
		reject(std::vector<uint8_t>(bytes.begin(), bytes.begin() + n), "truncated file");
	std::puts("Ps4ModuleTests: PASS");
}
