#include "Ps4ModuleTestFixture.h"
#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "common/singleton.h"
#include "common/subsystems.h"
#include "common/threads.h"
#include "emulator.h"
#include "kernel/fileSystem.h"
#include "kernel/memory.h"
#include "kernel/pthread.h"
#include "libs/errno.h"
#include "libs/network.h"
#include "libs/ps4Kernel.h"
#include "loader/runtimeLinker.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <xbyak/xbyak.h>

namespace {
void Check(bool value, const char* message) {
	if (!value) {
		std::fprintf(stderr, "Ps4RuntimeTests: %s\n", message);
		std::_Exit(1);
	}
}
void Write(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char*>(data.data()), data.size());
	Check(out.good(), "write fixture");
}
std::vector<uint8_t> Fixture(bool implicit_cleanup = false) {
	using namespace Loader;
	using namespace Ps4Tests;
	auto bytes = Ps4Tests::Fixture();
	std::fill(bytes.begin() + 0x1000, bytes.begin() + 0x2800, 0);
	std::fill(bytes.begin() + Dynamic, bytes.end(), 0);
	const char* nids[] = {"1G3lF1Gg1k8", "UK2Tl2DWUns", "Cg4srZ6TKbU", "4wSze92BhLI",
	                      "mL8NDH86iQI", "cQke9UuBQOk", "6UgtwV+0zb4", "onNY9Byn-W8",
	                      "vNe1w4diLCs", "qH1gXoq71RY", "9UK1vLZQft4", "tn3VlD0hG60",
	                      "2Of0f+3mhhE", "1jfXLRVzisc", "4J2sUJmuHZQ"};
	std::string strings(1, '\0');
	auto        append = [&](std::string value) {
        auto offset = strings.size();
        strings += value;
        strings += '\0';
        return offset;
	};
	for (size_t i = 0; i < std::size(nids); ++i) {
		auto   name = append(std::string(nids[i]) + "#A#B");
		size_t sym  = Dynlib + 0x400 + (i + 1) * 24;
		Put(bytes, sym, name, 4);
		Put(bytes, sym + 4, 0x12, 1);
		size_t rel = Dynlib + 0x700 + i * 24;
		Put(bytes, rel, 0x4100 + i * 8, 8);
		Put(bytes, rel + 8, ((i + 1ull) << 32) | 7, 8);
	}
	auto library = append("libkernel");
	std::copy(strings.begin(), strings.end(), bytes.begin() + Dynlib);
	Tag(bytes, 0, DT_OS_STRTAB, 0);
	Tag(bytes, 1, DT_OS_STRSZ, strings.size());
	Tag(bytes, 2, DT_OS_SYMTAB, 0x400);
	Tag(bytes, 3, DT_OS_SYMTABSZ, (std::size(nids) + 1) * 24);
	Tag(bytes, 4, DT_OS_SYMENT, 24);
	Tag(bytes, 5, DT_OS_RELA, 0x700);
	Tag(bytes, 6, DT_OS_RELASZ, std::size(nids) * 24);
	Tag(bytes, 7, DT_OS_RELAENT, 24);
	Tag(bytes, 8, DT_OS_IMPORT_LIB, (1ull << 32) | library);
	Tag(bytes, 9, DT_OS_NEEDED_MODULE, (1ull << 48) | (1ull << 40) | library);
	Tag(bytes, 10, DT_INIT, 0);
	Tag(bytes, 11, DT_FINI, 0x40);
	Tag(bytes, 12, 0, 0);
	auto text = [&](size_t address, const char* value) {
		std::memcpy(bytes.data() + 0x2000 + address - 0x4000, value, std::strlen(value) + 1);
	};
	text(0x4600, "/app0/input.bin");
	text(0x4640, "/data/result.bin");
	text(0x4680, "/app0/../outside");
	text(0x46c0, "ps4-test");
	Put(bytes, 0x2560, 1, 8);
	Xbyak::ClearError();
	Xbyak::CodeGenerator a(0x1000, bytes.data() + 0x1000);
	auto                 pad = [&](size_t offset) {
        Check(a.getSize() <= offset, "fixture code capacity");
        while (a.getSize() < offset)
            a.nop();
	};
	a.mov(a.rax, 0x404508);
	a.inc(a.dword[a.rax]);
	a.xor_(a.eax, a.eax);
	a.ret();
	pad(0x40);
	a.mov(a.rax, 0x40450c);
	a.inc(a.dword[a.rax]);
	a.xor_(a.eax, a.eax);
	a.ret();
	pad(0x100);
	Xbyak::Label fail;
	auto         call = [&](int index) {
        a.mov(a.rax, 0x404100 + index * 8);
        a.call(a.ptr[a.rax]);
	};
	auto addr   = [&](const Xbyak::Reg64& reg, int offset) { a.lea(reg, a.ptr[a.r15 + offset]); };
	auto expect = [&](int result, int step) {
		a.mov(a.dword[a.r15 + 0x4500], step);
		a.mov(a.dword[a.r15 + 0x4578], a.eax);
		a.cmp(a.eax, result);
		a.jne(fail, Xbyak::CodeGenerator::T_NEAR);
	};
	a.push(a.r15);
	a.push(a.r12);
	a.push(a.r13);
	a.mov(a.r15, 0x400000);
	a.mov(a.qword[a.r15 + 0x4540], a.rsp);
	a.cmp(a.dword[a.rdi], 1);
	a.jne(fail, Xbyak::CodeGenerator::T_NEAR);
	addr(a.rdi, 0x4520);
	a.mov(a.esi, 0x4000);
	a.mov(a.edx, 3);
	a.xor_(a.ecx, a.ecx);
	addr(a.r8, 0x46c0);
	call(4);
	expect(0, 1);
	addr(a.rdi, 0x4600);
	a.xor_(a.esi, a.esi);
	a.xor_(a.edx, a.edx);
	call(0);
	a.mov(a.r12d, a.eax);
	a.mov(a.dword[a.r15 + 0x4570], a.eax);
	a.test(a.eax, a.eax);
	a.js(fail, Xbyak::CodeGenerator::T_NEAR);
	a.mov(a.edi, a.r12d);
	a.mov(a.rsi, a.qword[a.r15 + 0x4520]);
	a.mov(a.edx, 4);
	call(2);
	expect(4, 2);
	a.mov(a.rax, a.qword[a.r15 + 0x4520]);
	a.cmp(a.dword[a.rax], 0x78563412);
	a.jne(fail, Xbyak::CodeGenerator::T_NEAR);
	a.mov(a.edi, a.r12d);
	a.mov(a.esi, 1);
	a.mov(a.edx, 4);
	call(2);
	expect(Libs::LibKernel::KERNEL_ERROR_EFAULT, 3);
	a.mov(a.edi, a.r12d);
	call(1);
	expect(0, 4);
	addr(a.rdi, 0x4600);
	a.mov(a.esi, 0x601);
	a.mov(a.edx, 0600);
	call(0);
	expect(Libs::LibKernel::KERNEL_ERROR_EROFS, 5);
	addr(a.rdi, 0x4680);
	a.xor_(a.esi, a.esi);
	call(0);
	expect(Libs::LibKernel::KERNEL_ERROR_EACCES, 6);
	addr(a.rdi, 0x4640);
	a.mov(a.esi, 0x601);
	a.mov(a.edx, 0600);
	call(0);
	a.mov(a.r12d, a.eax);
	a.mov(a.dword[a.r15 + 0x4570], a.eax);
	a.test(a.eax, a.eax);
	a.js(fail, Xbyak::CodeGenerator::T_NEAR);
	a.mov(a.edi, a.r12d);
	a.mov(a.rsi, a.qword[a.r15 + 0x4520]);
	a.mov(a.edx, 4);
	call(3);
	expect(4, 7);
	if (!implicit_cleanup) {
		a.mov(a.edi, a.r12d);
		call(1);
		expect(0, 8);
	}
	addr(a.rdi, 0x4560);
	call(8);
	a.test(a.rax, a.rax);
	a.jz(fail, Xbyak::CodeGenerator::T_NEAR);
	a.mov(a.qword[a.r15 + 0x4548], a.rax);
	a.cmp(a.dword[a.rax], 0x12345678);
	a.jne(fail, Xbyak::CodeGenerator::T_NEAR);
	a.mov(a.dword[a.rax], 55);
	const uint8_t fs_load[] = {0x64, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0};
	a.db(fs_load, sizeof(fs_load));
	a.sub(a.rax, 32);
	a.cmp(a.rax, a.qword[a.r15 + 0x4548]);
	a.jne(fail, Xbyak::CodeGenerator::T_NEAR);
	addr(a.rdi, 0x4538);
	a.xor_(a.esi, a.esi);
	addr(a.rdx, 0x46c0);
	call(9);
	expect(0, 9);
	addr(a.rdi, 0x4538);
	call(10);
	expect(0, 10);
	addr(a.rdi, 0x4528);
	a.xor_(a.esi, a.esi);
	addr(a.rdx, 0xc00);
	a.xor_(a.ecx, a.ecx);
	addr(a.r8, 0x46c0);
	call(6);
	expect(0, 11);
	Xbyak::Label wait_worker, worker_started;
	a.mov(a.r13d, 10000);
	a.L(wait_worker);
	a.cmp(a.dword[a.r15 + 0x4514], 1);
	a.je(worker_started);
	a.mov(a.edi, 1000);
	call(13);
	a.dec(a.r13d);
	a.jnz(wait_worker);
	a.jmp(fail, Xbyak::CodeGenerator::T_NEAR);
	a.L(worker_started);
	a.mov(a.edi, 1000);
	call(13);
	a.cmp(a.dword[a.r15 + 0x4510], 0);
	a.jne(fail, Xbyak::CodeGenerator::T_NEAR);
	addr(a.rdi, 0x4538);
	call(11);
	expect(0, 12);
	if (!implicit_cleanup) {
		a.mov(a.rdi, a.qword[a.r15 + 0x4528]);
		addr(a.rsi, 0x4530);
		call(7);
		expect(0, 13);
		a.cmp(a.dword[a.r15 + 0x4530], 0x12345678);
		a.jne(fail, Xbyak::CodeGenerator::T_NEAR);
		addr(a.rdi, 0x4538);
		call(12);
		expect(0, 14);
	}
	addr(a.rdi, 0x4560);
	call(8);
	a.cmp(a.dword[a.rax], 55);
	a.jne(fail, Xbyak::CodeGenerator::T_NEAR);
	if (!implicit_cleanup) {
		a.mov(a.rdi, a.qword[a.r15 + 0x4520]);
		a.mov(a.esi, 0x4000);
		call(5);
		expect(0, 15);
	}
	call(14);
	a.mov(a.dword[a.r15 + 0x4500], 0x600dcafe);
	a.L(fail);
	a.pop(a.r13);
	a.pop(a.r12);
	a.pop(a.r15);
	a.ret();
	pad(0xc00);
	a.push(a.r15);
	a.push(a.r12);
	a.push(a.r13);
	a.mov(a.r15, 0x400000);
	a.mov(a.dword[a.r15 + 0x4514], 1);
	addr(a.rdi, 0x4538);
	call(10);
	addr(a.rdi, 0x4560);
	call(8);
	a.mov(a.qword[a.r15 + 0x4550], a.rax);
	a.mov(a.r12, a.qword[a.rax]);
	a.db(fs_load, sizeof(fs_load));
	a.sub(a.rax, 32);
	a.mov(a.dword[a.rax], 0xaa);
	a.inc(a.dword[a.r15 + 0x4510]);
	addr(a.rdi, 0x4538);
	call(11);
	a.mov(a.rax, a.r12);
	a.pop(a.r13);
	a.pop(a.r12);
	a.pop(a.r15);
	a.ret();
	a.ready();
	Check(Xbyak::GetError() == 0, Xbyak::ConvertErrorToString(Xbyak::GetError()));
	return bytes;
}
uint64_t Word(uint64_t address, size_t size = 8) {
	uint64_t value = 0;
	Check(Libs::LibKernel::Memory::IsGuestRangeAccessible(address, size,
	                                                      Common::VirtualMemory::Mode::Read) &&
	          Libs::LibKernel::Memory::EnsureGuestCommitted(address, size),
	      "read result");
	std::memcpy(&value, reinterpret_cast<const void*>(address), size);
	return value;
}
} // namespace
int main(int argc, char** argv) {
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const auto root =
	    argc == 3
	        ? std::filesystem::absolute(argv[2])
	        : std::filesystem::temp_directory_path() /
	              ("kyty_ps4_runtime_" +
	               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(root / "app0/sce_sys");
	auto bytes = Fixture();
	Write(root / "app0/eboot.bin", bytes);
	Write(root / "app0/sce_sys/param.sfo", GuestPlatformTests::Sfo());
	Write(root / "app0/input.bin", {0x12, 0x34, 0x56, 0x78});
	if (argc == 3 && std::string_view(argv[1]) == "--emit-fixture") return 0;
	if (argc == 3 && std::string_view(argv[1]) == "--run-profile") {
		Common::InitializeThreads();
		Emulator::RunOptions options;
		options.ps4_runtime             = true;
		options.config.printf_direction = Config::LogDirection::Console;
		options.app0_dir                = root / "app0";
		options.elf                     = "/app0/eboot.bin";
		const int     result            = Emulator::Run(options);
		std::ifstream output(std::filesystem::current_path() / "_Ps4Data/diagnostic/result.bin",
		                     std::ios::binary);
		std::vector<uint8_t> saved((std::istreambuf_iterator<char>(output)), {});
		Check(saved == std::vector<uint8_t>({0x12, 0x34, 0x56, 0x78}),
		      "application profile file output");
		return result;
	}
	Common::InitializeThreads();
	Common::Subsystems subsystems;
	subsystems.Initialize<Config::Lifecycle>();
	Config::ConfigOptions config;
	config.printf_direction = Config::LogDirection::Console;
	config.guest_platform   = Common::GuestPlatform::PS4;
	config.guest_arguments  = {"ps4-runtime-test"};
	Config::Load(config);
	subsystems.Initialize<Log::Lifecycle>();
	subsystems.Initialize<Libs::LibKernel::PthreadLifecycle>();
	subsystems.Initialize<Libs::Network::Lifecycle>();
	subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
	subsystems.Initialize<Libs::LibKernel::FileSystem::Lifecycle>();
	auto*       rt = Common::Singleton<Loader::RuntimeLinker>::Instance();
	std::string error;
	auto        bad           = bytes;
	bad[Ps4Tests::Dynlib + 1] = '?';
	Write(root / "bad.bin", bad);
	Check(!rt->LoadPs4Program(root / "bad.bin", error) && !error.empty(),
	      "missing import rejected before execution");
	bad = bytes;
	GuestPlatformTests::Put(bad, 64 + 4, Loader::PF_R | Loader::PF_W | Loader::PF_X, 4);
	Write(root / "bad.bin", bad);
	Check(!rt->LoadPs4Program(root / "bad.bin", error) && !error.empty(), "RWX image rejected");
	for (int variant: {0, 1, 2}) {
		const bool self     = variant == 1;
		const bool implicit = variant == 2;
		bytes               = Fixture(implicit);
		Write(root / "app0/eboot.bin", self ? Ps4Tests::Self(bytes) : bytes);
		auto* program = rt->LoadPs4Program(root / "app0/eboot.bin", error);
		Check(program, error.c_str());
		Check(rt->GetEntry() == 0x400100 && rt->FindProgramByAddr(0x400100) == program,
		      "registered entry and address lookup");
		using Mode  = Common::VirtualMemory::Mode;
		namespace M = Libs::LibKernel::Memory;
		Check(M::IsGuestRangeAccessible(0x400100, 1, Mode::Execute) &&
		          !M::IsGuestRangeAccessible(0x400100, 1, Mode::Write),
		      "RX text");
		Check(M::IsGuestRangeAccessible(0x404500, 1, Mode::ReadWrite) &&
		          !M::IsGuestRangeAccessible(0x404500, 1, Mode::Execute),
		      "RW nonexecutable data");
		Libs::Ps4::RuntimeStats stats;
		Check(rt->ExecutePs4(program, root / "app0", root / "data", error, &stats), error.c_str());
		auto status = Word(0x404500, 4);
		std::printf("PS4_RUNTIME: %s status=0x%llx last_return=0x%llx reads=%llu writes=%llu "
		            "threads=%llu/%llu TLS=%llu\n",
		            self       ? "SELF"
		            : implicit ? "ELF-auto-cleanup"
		                       : "ELF",
		            (unsigned long long)status, (unsigned long long)Word(0x404578, 4),
		            (unsigned long long)stats.file_reads, (unsigned long long)stats.file_writes,
		            (unsigned long long)stats.threads_started,
		            (unsigned long long)stats.threads_joined,
		            (unsigned long long)stats.tls_queries);
		Check(!Libs::LibKernel::FileSystem::IsOpenDescriptor(static_cast<int>(Word(0x404570, 4))),
		      "file descriptor released");
		Check(status == 0x600dcafe, "guest operations completed");
		Check(Word(0x404508, 4) == 1 && Word(0x40450c, 4) == 1 && Word(0x404510, 4) == 1,
		      "initializer/finalizer and worker");
		Check(!M::IsGuestRangeAccessible(Word(0x404520), 1, Mode::Read),
		      "mapping released by guest or teardown");
		Check(Word(0x404548) != Word(0x404550), "per-thread TLS isolation");
		Libs::LibKernel::Memory::VirtualQueryInfo info {};
		Check(!M::KernelVirtualQuery(reinterpret_cast<void*>(Word(0x404540)), 0, &info,
		                             sizeof(info)) &&
		          info.is_stack,
		      "guest main stack");
		Check(stats.allocations == 1 && stats.file_reads == 1 && stats.file_writes == 1 &&
		          stats.threads_started == 1 && stats.threads_joined == 1 && stats.tls_queries == 3,
		      "actual HLE effects");
		Check(!rt->ExecutePs4(program, root / "app0", root / "data", error),
		      "duplicate execution rejected");
		rt->UnloadProgram(program);
		Check(!M::IsGuestRangeAccessible(0x400100, 1, Mode::Execute), "image released");
		std::ifstream        out(root / "data/result.bin", std::ios::binary);
		std::vector<uint8_t> result((std::istreambuf_iterator<char>(out)), {});
		Check(result == std::vector<uint8_t>({0x12, 0x34, 0x56, 0x78}), "real file write");
	}
	rt->Clear();
	subsystems.Destroy();
	std::filesystem::remove_all(root);
	return 0;
}
