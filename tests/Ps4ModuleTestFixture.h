#pragma once
#include "GuestPlatformTestFixture.h"
#include "loader/elf.h"

#include <cstring>

namespace Ps4Tests {
using GuestPlatformTests::Put;
inline constexpr size_t Dynamic = 0x3000;
inline constexpr size_t Dynlib  = 0x4000;
inline void Phdr(std::vector<uint8_t>& bytes, size_t index, uint32_t type, uint64_t offset,
                 uint64_t address, uint64_t file_size, uint64_t memory_size, uint32_t flags,
                 uint64_t alignment = 16) {
	const size_t p = 64 + index * 56;
	Put(bytes, p, type, 4);
	Put(bytes, p + 4, flags, 4);
	Put(bytes, p + 8, offset, 8);
	Put(bytes, p + 16, address, 8);
	Put(bytes, p + 32, file_size, 8);
	Put(bytes, p + 40, memory_size, 8);
	Put(bytes, p + 48, alignment, 8);
}
inline void Tag(std::vector<uint8_t>& bytes, size_t index, uint64_t tag, uint64_t value) {
	Put(bytes, Dynamic + index * 16, tag, 8);
	Put(bytes, Dynamic + index * 16 + 8, value, 8);
}
inline void Rela(std::vector<uint8_t>& bytes, size_t index, uint64_t target, uint32_t type,
                 uint32_t symbol, int64_t addend) {
	Put(bytes, Dynlib + 0x200 + index * 24, target, 8);
	Put(bytes, Dynlib + 0x208 + index * 24, (uint64_t(symbol) << 32) | type, 8);
	Put(bytes, Dynlib + 0x210 + index * 24, uint64_t(addend), 8);
}
inline std::vector<uint8_t> Fixture() {
	using namespace Loader;
	auto bytes = GuestPlatformTests::Elf(false);
	bytes.resize(0x5000);
	Put(bytes, 24, 0x100, 8);
	Put(bytes, 56, 5, 2);
	Phdr(bytes, 0, PT_LOAD, 0x1000, 0, 0x1000, 0x1000, PF_R | PF_X);
	Phdr(bytes, 1, PT_LOAD, 0x2000, 0x4000, 0x1000, 0x2000, PF_R | PF_W);
	Phdr(bytes, 2, PT_DYNAMIC, Dynamic, 0, 16 * 16, 16 * 16, PF_R);
	Phdr(bytes, 3, PT_OS_DYNLIBDATA, Dynlib, 0, 0x1000, 0x1000, PF_R);
	Phdr(bytes, 4, PT_TLS, 0x2800, 0x4800, 8, 32, PF_R, 16);
	const uint8_t init[] = {0xb8, 7, 0, 0, 0, 0xc3};
	std::copy(std::begin(init), std::end(init), bytes.begin() + 0x1000);
	const uint8_t entry[] = {0x48, 0x83, 0xec, 0x08, 0xff, 0x15, 0,   0,
	                         0,    0,    0x48, 0x83, 0xc4, 0x08, 0xc3};
	std::copy(std::begin(entry), std::end(entry), bytes.begin() + 0x1100);
	Put(bytes, 0x1106, 0x4100 - (0x100 + 10), 4);
	Put(bytes, 0x2800, 0x12345678, 8);
	std::string strings(1, '\0');
	const auto  append = [&](const std::string& value) {
        const auto offset = strings.size();
        strings += value;
        strings.push_back('\0');
        return offset;
	};
	const auto import = append("AAAAAAAAAAA#A#B"), library = append("libSceLoaderTest"),
	           module = append("libSceLoaderModule"), tls = append("tls"),
	           object = append("object");
	std::copy(strings.begin(), strings.end(), bytes.begin() + Dynlib);
	const auto symbol = [&](size_t index, uint32_t name, uint8_t info, uint16_t section,
	                        uint64_t value, uint64_t size) {
		const size_t p = Dynlib + 0x100 + index * 24;
		Put(bytes, p, name, 4);
		Put(bytes, p + 4, info, 1);
		Put(bytes, p + 6, section, 2);
		Put(bytes, p + 8, value, 8);
		Put(bytes, p + 16, size, 8);
	};
	symbol(1, uint32_t(import), 0x12, 0, 0, 0);
	symbol(2, uint32_t(tls), 0x16, 1, 8, 8);
	symbol(3, uint32_t(object), 0x11, 1, 0x4500, 8);
	Rela(bytes, 0, 0x4100, 7, 1, 0);
	Rela(bytes, 1, 0x4110, 16, 0, 0);
	Rela(bytes, 2, 0x4118, 17, 2, 4);
	Rela(bytes, 3, 0x4120, 8, 0, 0x4500);
	Rela(bytes, 4, 0x4128, 1, 3, 8);
	Rela(bytes, 5, 0x4130, 18, 2, 0);
	Tag(bytes, 0, DT_OS_STRTAB, 0);
	Tag(bytes, 1, DT_OS_STRSZ, strings.size());
	Tag(bytes, 2, DT_OS_SYMTAB, 0x100);
	Tag(bytes, 3, DT_OS_SYMTABSZ, 4 * 24);
	Tag(bytes, 4, DT_OS_SYMENT, 24);
	Tag(bytes, 5, DT_OS_RELA, 0x200);
	Tag(bytes, 6, DT_OS_RELASZ, 6 * 24);
	Tag(bytes, 7, DT_OS_RELAENT, 24);
	Tag(bytes, 8, DT_OS_IMPORT_LIB, (uint64_t(1) << 32) | library);
	Tag(bytes, 9, DT_OS_NEEDED_MODULE, (uint64_t(1) << 48) | (uint64_t(1) << 40) | module);
	Tag(bytes, 10, DT_INIT, 0);
	Tag(bytes, 11, 0, 0);
	return bytes;
}
inline std::vector<uint8_t> Self(const std::vector<uint8_t>& elf) {
	std::vector<uint8_t> bytes(0x500 + elf.size());
	Put(bytes, 0, 0x1d3d154f, 4);
	bytes[5] = 1;
	bytes[6] = 1;
	bytes[7] = 0x12;
	Put(bytes, 24, 5, 2);
	const size_t elf_offset = 32 + 5 * 32;
	std::copy_n(elf.begin(), 64 + 5 * 56, bytes.begin() + elf_offset);
	for (size_t i = 0; i < 5; ++i) {
		Loader::Elf64_Phdr h {};
		std::memcpy(&h, elf.data() + 64 + i * 56, 56);
		Put(bytes, 32 + i * 32, 0x800 | (i << 20), 8);
		Put(bytes, 40 + i * 32, 0x500 + h.p_offset, 8);
		Put(bytes, 48 + i * 32, h.p_filesz, 8);
		Put(bytes, 56 + i * 32, h.p_filesz, 8);
		std::copy_n(elf.begin() + h.p_offset, h.p_filesz, bytes.begin() + 0x500 + h.p_offset);
	}
	return bytes;
}
} // namespace Ps4Tests
