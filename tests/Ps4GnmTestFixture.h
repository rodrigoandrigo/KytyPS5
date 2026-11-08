#include "Ps4TextureFixture.h"
#pragma once
#include "Ps4ModuleTestFixture.h"
#include "graphics/shader/recompiler/frontend/decode/GcnDecoder.h"

#include <bit>
#include <xbyak/xbyak.h>

namespace Ps4GnmTests {
constexpr uint64_t Base = 0x400000, Vs = 0x404400, Ps = 0x404500, VsRegs = 0x404600,
                   PsRegs = 0x404640;
constexpr uint64_t Arrays = 0x404700, Sizes = 0x404708, Status = 0x4047f0, Commands = 0x408000,
                   Target = 0x40c000;
constexpr uint32_t Width = 64, Height = 32, Bytes = Width * Height * 4;
inline void        Reg(std::vector<uint32_t>& out, uint32_t op, uint32_t offset,
                       std::initializer_list<uint32_t> values) {
    out.push_back(0xc0000000u | (uint32_t(values.size()) << 16) | (op << 8));
    out.push_back(offset);
    out.insert(out.end(), values);
}
inline std::vector<uint32_t> StateWords(uint64_t target, uint32_t user,
                                        bool interpolation = false) {
	std::vector<uint32_t> out;
	Reg(out, 0x69, 0x318,
	    {uint32_t(target >> 8), Width / 8 - 1, Width * Height / 64 - 1, 0, 0x28, 8});
	Reg(out, 0x69, 0xc, {0, Height << 16 | Width});
	Reg(out, 0x69, 0x80, {0, 0x80000000, Height << 16 | Width});
	Reg(out, 0x69, 0x8e, {15, 15});
	Reg(out, 0x69, 0x90, {0x80000000, Height << 16 | Width});
	Reg(out, 0x69, 0xb4, {0, 0x3f800000});
	Reg(out, 0x69, 0x10f,
	    {std::bit_cast<uint32_t>(Width * .5f), std::bit_cast<uint32_t>(Width * .5f),
	     std::bit_cast<uint32_t>(Height * .5f), std::bit_cast<uint32_t>(Height * .5f), 0x3f800000,
	     0});
	Reg(out, 0x69, 0x200, {0});
	Reg(out, 0x69, 0x202, {0xcc0010, 0, 1u << 19, 0, 0x3f, 0});
	Reg(out, 0x69, 0x1e0, {0});
	Reg(out, 0x79, 0x242, {4});
	out.insert(out.end(), {0xc0002f00, 1});
	if (interpolation) Reg(out, 0x69, 0x191, {0});
	Reg(out, 0x76, 0xc, {user});
	return out;
}
inline void Write(std::vector<uint32_t>& out, uint64_t address, uint32_t value) {
	out.insert(out.end(),
	           {0xc0033700u, 0x00100500u, uint32_t(address), uint32_t(address >> 32), value});
}
inline void Fill(std::vector<uint32_t>& out, uint64_t address, uint32_t bytes, uint32_t value) {
	out.insert(out.end(), {0xc0055000u, 0xc0000000u, value, 0, uint32_t(address),
	                       uint32_t(address >> 32), bytes});
}
inline std::vector<uint8_t> Fixture(bool interpolation = false, bool arithmetic = false,
                                    bool transfers = false, bool buffer_load = false,
                                    bool indexed = false, bool textured = false) {
	using namespace Loader;
	using namespace Ps4Tests;
	auto bytes = Ps4Tests::Fixture();
	std::fill(bytes.begin() + 0x1000, bytes.begin() + 0x2800, 0);
	std::fill(bytes.begin() + Dynamic, bytes.end(), 0);
	Phdr(bytes, 1, PT_LOAD, 0x2000, 0x4000, 0x1000, 0x10000, PF_R | PF_W);
	const char* nids[] = {"gAhCn6UiU4Y", "bQVd5YzCal0", "GGsn7jMTxw4", "zwY0YV91TTI",
	                      "yvZ73uQUqrk"};
	std::string strings(1, '\0');
	const auto  append = [&](const std::string& text) {
        auto n = strings.size();
        strings += text;
        strings += '\0';
        return n;
	};
	for (size_t i = 0; i < std::size(nids); ++i) {
		auto         name = append(std::string(nids[i]) + "#A#B");
		const size_t sym  = Dynlib + 0x400 + (i + 1) * 24;
		Put(bytes, sym, name, 4);
		Put(bytes, sym + 4, 0x12, 1);
		const size_t rel = Dynlib + 0x700 + i * 24;
		Put(bytes, rel, 0x4100 + i * 8, 8);
		Put(bytes, rel + 8, ((i + 1ull) << 32) | 7, 8);
	}
	auto library = append("libSceGnmDriver");
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
	Tag(bytes, 10, 0, 0);
	const auto data = [&](uint64_t address, const auto& words) {
		const size_t offset = size_t(address - 0x404000 + 0x2000);
		if (offset + words.size() * 4 > 0x3000) std::_Exit(1);
		std::memcpy(bytes.data() + offset, words.data(), words.size() * 4);
	};
	data(Vs, interpolation ? Libs::Graphics::ShaderRecompiler::Gcn::InterpolatedVertexFixture(true)
	                       : Libs::Graphics::ShaderRecompiler::Gcn::TriangleVertexFixture(false));
	auto ps_code = interpolation
	                   ? Libs::Graphics::ShaderRecompiler::Gcn::InterpolatedPixelFixture(arithmetic)
	                   : Libs::Graphics::ShaderRecompiler::Gcn::UniformBranchPixelFixture();
	if (buffer_load) ps_code = Libs::Graphics::ShaderRecompiler::Gcn::BufferPixelFixture(4);
	if (indexed) ps_code = Libs::Graphics::ShaderRecompiler::Gcn::IndexedBufferPixelFixture();
	if (transfers) ps_code[ps_code.size() - 4] = (ps_code[ps_code.size() - 4] & ~511u) | 240u;
	if (textured) {
		data(Vs, Libs::Graphics::ShaderRecompiler::Gcn::TextureVertexFixture());
		ps_code = Libs::Graphics::ShaderRecompiler::Gcn::TexturePixelFixture();
	}
	data(Ps, ps_code);
	const std::vector<uint32_t> vs_regs {uint32_t(Vs >> 8),
	                                     0,
	                                     textured        ? 0xc0043u
	                                     : interpolation ? 0xc0002u
	                                                     : 0xc0001u,
	                                     textured ? 32u : 0u,
	                                     0,
	                                     4,
	                                     0};
	const std::vector<uint32_t> ps_regs {uint32_t(Ps >> 8),
	                                     0,
	                                     textured        ? 0xc0084u
	                                     : indexed       ? 0xc0041u
	                                     : interpolation ? 0xc0001u
	                                                     : 0xc0000u,
	                                     textured      ? 32u
	                                     : indexed     ? 16u
	                                     : buffer_load ? 8u
	                                                   : 2u,
	                                     0,
	                                     9,
	                                     interpolation || textured ? 2u : 0u,
	                                     interpolation || textured ? 2u : 0u,
	                                     interpolation || textured ? 1u : 0u,
	                                     0,
	                                     0,
	                                     15};
	data(VsRegs, vs_regs);
	data(PsRegs, ps_regs);
	auto         state      = StateWords(Target, 1, interpolation || textured);
	const size_t user_index = state.size() - 1;
	if (buffer_load) {
		Reg(state, 0x76, 0xc, {0x404b80, 0, 64, 0xfac});
		const float           colors[] = {1, 0, 0, 1, .25f, .5f, .75f, 1};
		std::vector<uint32_t> words(16);
		for (size_t i = 0; i < 8; ++i)
			words[i] = std::bit_cast<uint32_t>(colors[i]);
		data(0x404b80, words);
	}
	if (indexed) {
		Reg(state, 0x76, 0xc, {0x404b80, 0, 16, 0xfac, 0x404bc0, 16u << 16, 4, 0xfac});
		data(0x404b80, std::vector<uint32_t> {1, 0, 0, 0});
		const float values[] = {1, 0, 1, 1, .25f, .5f, .75f, 1, .125f, .875f, .25f, 1, 0, 1, 0, 1};
		std::vector<uint32_t> records;
		for (float x: values)
			records.push_back(std::bit_cast<uint32_t>(x));
		data(0x404bc0, records);
	}
	if (textured) {
		const auto vs_image            = Ps4TextureTests::ImageWords(0x410800),
		           ps_image            = Ps4TextureTests::ImageWords(0x411000);
		std::vector<uint32_t> vs_users = {0x410000, 16u << 16, 4, 0xfac,
		                                  0x410040, 16u << 16, 4, 0xfac};
		std::vector<uint32_t> ps_users = {0x410100, 0, 16, 0xfac, 0x410200, 16u << 16, 4, 0xfac};
		vs_users.insert(vs_users.end(), vs_image.begin(), vs_image.end());
		ps_users.insert(ps_users.end(), ps_image.begin(), ps_image.end());
		state.push_back(0xc0107600);
		state.push_back(0x4c);
		state.insert(state.end(), vs_users.begin(), vs_users.end());
		state.push_back(0xc0107600);
		state.push_back(0xc);
		state.insert(state.end(), ps_users.begin(), ps_users.end());
	}
	std::vector<uint32_t> tail;
	if (transfers) {
		Reg(state, 0x69, 0x1e0, {0x65010504});
		Fill(state, Target, Bytes, 0xff1e140a);
		Write(tail, Target + (30 * Width + 60) * 4, 0xff665544);
		Write(tail, Target + (8 * Width + 16) * 4, 0xff000000);
		data(0x404b00, tail);
	}
	data(0x404900, state);
	const uint32_t total_words =
	    69 + uint32_t(state.size()) + 7 + uint32_t(tail.size()) + (transfers ? 7 : 0);
	Put(bytes, 0x2700, Commands, 8);
	Put(bytes, 0x2708, total_words * 4, 4);
	Xbyak::ClearError();
	Xbyak::CodeGenerator a(0xf00, bytes.data() + 0x1100);
	Xbyak::Label         fail, finish;
	const auto           call = [&](uint32_t index) {
        a.mov(a.rax, 0x404100 + index * 8);
        a.call(a.ptr[a.rax]);
	};
	const auto check = [&](uint32_t step) {
		a.mov(a.dword[a.r15 + 0x47f0], step);
		a.mov(a.dword[a.r15 + 0x47f4], a.eax);
		a.test(a.eax, a.eax);
		a.jnz(fail, Xbyak::CodeGenerator::T_NEAR);
	};
	const auto submit = [&]() {
		a.mov(a.edi, 1);
		a.mov(a.rsi, Arrays);
		a.mov(a.rdx, Sizes);
		a.xor_(a.ecx, a.ecx);
		a.xor_(a.r8d, a.r8d);
		call(3);
	};
	const auto pixel = [&](uint32_t x, uint32_t y, uint32_t expected) {
		a.mov(a.rax, Target + (y * Width + x) * 4);
		a.cmp(a.dword[a.rax], expected);
		a.jne(fail, Xbyak::CodeGenerator::T_NEAR);
	};
	const auto initialize_textures = [&](bool changed) {
		const auto write = [&](uint64_t address, uint32_t value) {
			a.mov(a.rax, address);
			a.mov(a.dword[a.rax], value);
		};
		const auto fill = [&](uint64_t address, uint32_t value, uint32_t bytes_count) {
			a.mov(a.rdi, address);
			a.mov(a.eax, value);
			a.mov(a.ecx, bytes_count / 4);
			a.rep();
			a.stosd();
		};
		const auto payload = Ps4TextureTests::Make(0, 0, changed);
		for (uint32_t i = 0; i < 16; ++i) {
			write(0x410000 + i * 4, std::bit_cast<uint32_t>(payload.positions[i]));
			const float color = i % 4 == 3 ? 1.f : changed && i % 4 == 0 ? .75f : .5f;
			write(0x410040 + i * 4, std::bit_cast<uint32_t>(color));
		}
		fill(0x410200, std::bit_cast<uint32_t>(1.f), 64);
		if (changed)
			for (uint32_t record = 0; record < 4; ++record)
				write(0x410200 + record * 16, std::bit_cast<uint32_t>(.5f));
		write(0x410100, changed ? 7 : 1);
		write(0x410104, changed ? 3 : 2);
		write(0x410108, changed ? 2 : 1);
		write(0x41010c, 0);
		fill(0x410800, changed ? 0xffffff80u : 0xffffffffu, 1024);
		fill(0x411000, changed ? 0xff204080u : 0xffc08040u, 1024);
	};
	a.push(a.r15);
	a.push(a.r12);
	a.sub(a.rsp, 8);
	a.mov(a.r15, Base);
	a.mov(a.r12, Commands);
	if (textured) initialize_textures(false);
	a.mov(a.rdi, Target);
	a.mov(a.ecx, Bytes / 4);
	a.mov(a.eax, 0xff1e140a);
	a.rep();
	a.stosd();
	a.mov(a.rdi, Commands + 69 * 4);
	a.mov(a.rsi, 0x404900);
	a.mov(a.ecx, uint32_t(state.size()));
	a.rep();
	a.movsd();
	a.mov(a.rdi, Commands);
	a.mov(a.esi, 29);
	a.mov(a.rdx, VsRegs);
	a.xor_(a.ecx, a.ecx);
	call(0);
	check(1);
	a.mov(a.rdi, Commands + 29 * 4);
	a.mov(a.esi, 40);
	a.mov(a.rdx, PsRegs);
	call(1);
	check(2);
	a.mov(a.rdi, Commands + (69 + state.size()) * 4);
	a.mov(a.esi, 7);
	a.mov(a.edx, 3);
	a.xor_(a.ecx, a.ecx);
	call(2);
	check(3);
	if (transfers) {
		a.mov(a.rdi, Commands + (69 + state.size() + 7) * 4);
		a.mov(a.rsi, 0x404b00);
		a.mov(a.ecx, uint32_t(tail.size()));
		a.rep();
		a.movsd();
		a.mov(a.rdi, Commands + (69 + state.size() + 7 + tail.size()) * 4);
		a.mov(a.esi, 7);
		a.mov(a.edx, 3);
		a.xor_(a.ecx, a.ecx);
		call(2);
		check(3);
	}
	submit();
	check(4);
	pixel(16, 8,
	      textured        ? 0xff604020u
	      : buffer_load   ? 0xffbf8040
	      : transfers     ? 0xff604060
	      : interpolation ? (arithmetic ? 0xff60469b : 0xff401a32)
	                      : 0xffbf80bf);
	pixel(60, 30, transfers ? 0xff665544 : 0xff1e140a);
	if (textured) {
		initialize_textures(true);
	} else if (indexed) {
		a.mov(a.rax, 0x404b80);
		a.mov(a.dword[a.rax], 2);
		a.mov(a.rax, 0x404be0);
		a.mov(a.dword[a.rax], std::bit_cast<uint32_t>(.75f));
		a.mov(a.dword[a.rax + 4], std::bit_cast<uint32_t>(.25f));
		a.mov(a.dword[a.rax + 8], std::bit_cast<uint32_t>(.5f));
	} else if (buffer_load) {
		a.mov(a.rax, 0x404b90);
		a.mov(a.dword[a.rax], std::bit_cast<uint32_t>(.75f));
		a.mov(a.dword[a.rax + 4], std::bit_cast<uint32_t>(.25f));
		a.mov(a.dword[a.rax + 8], std::bit_cast<uint32_t>(.5f));
	} else {
		a.mov(a.rax, Commands + (69 + user_index) * 4);
		a.mov(a.dword[a.rax], 0);
	}
	submit();
	check(5);
	pixel(16, 8,
	      textured        ? 0xff102018u
	      : buffer_load   ? 0xff8040bf
	      : transfers     ? 0xff604020
	      : interpolation ? (arithmetic ? 0xff60469b : 0xff401a32)
	                      : 0xffbf8040);
	pixel(60, 30, transfers ? 0xff665544 : 0xff1e140a);
	a.mov(a.rax, Commands + total_words * 4);
	a.mov(a.dword[a.rax], 0xc0009900);
	a.mov(a.dword[a.rax + 4], 0);
	a.mov(a.rax, Sizes);
	a.mov(a.dword[a.rax], total_words * 4 + 8);
	submit();
	a.mov(a.dword[a.r15 + 0x47f4], a.eax);
	a.test(a.eax, a.eax);
	a.jz(fail, Xbyak::CodeGenerator::T_NEAR);
	pixel(16, 8,
	      textured        ? 0xff102018u
	      : buffer_load   ? 0xff8040bf
	      : transfers     ? 0xff604020
	      : interpolation ? (arithmetic ? 0xff60469b : 0xff401a32)
	                      : 0xffbf8040);
	call(4);
	check(6);
	a.mov(a.dword[a.r15 + 0x47f0], 0x600d474e);
	a.jmp(finish);
	a.L(fail);
	a.L(finish);
	a.xor_(a.eax, a.eax);
	a.add(a.rsp, 8);
	a.pop(a.r12);
	a.pop(a.r15);
	a.ret();
	a.ready();
	if (Xbyak::GetError()) std::_Exit(1);
	return bytes;
}
} // namespace Ps4GnmTests
